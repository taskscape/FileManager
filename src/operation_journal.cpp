// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h>
#include <memory>
#include "common/operation_recovery.h" // startup uses the same claimed recovery boundary as native tests

#include "operation_journal.h"
#include "worker.h"

namespace
{
const char* const JournalDirectoryName = "operation-journals";

BOOL GetJournalDirectory(char* directory, int directoryLen, BOOL create)
{
    BOOL gotPath = create ? CreateOurPathInRoamingAPPDATA(directory, directoryLen)
                          : GetOurPathInRoamingAPPDATA(directory, directoryLen);
    if (!gotPath || !SalPathAppend(directory, JournalDirectoryName, directoryLen))
        return FALSE;
    if (create)
        CreateDirectoryUtf8(directory, NULL); // an existing directory is expected
    return TRUE;
}

const int JournalWriteBufferCapacity = 64 * 1024;

BOOL WriteAll(HANDLE file, const char* data, DWORD length)
{
    // Short writes are legal; zero progress or an error makes the record incomplete.
    DWORD offset = 0;
    while (offset < length)
    {
        DWORD written = 0;
        if (!WriteFile(file, data + offset, length - offset, &written, NULL)) return FALSE;
        if (written == 0 || written > length - offset) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        offset += written;
    }
    return TRUE;
}

BOOL WriteAll(HANDLE file, const char* text)
{
    return WriteAll(file, text, (DWORD)strlen(text));
}

const char* OpcodeName(COperationCode opcode)
{
    switch (opcode)
    {
    case ocCopyFile: return "copy-file";
    case ocMoveFile: return "move-file";
    case ocDeleteFile: return "delete-file";
    case ocCreateDir: return "create-dir";
    case ocMoveDir: return "move-dir";
    case ocDeleteDir: return "delete-dir";
    case ocDeleteDirLink: return "delete-dir-link";
    default: return NULL;
    }
}

BOOL WriteReconciliationReport(char* reportPath, int reportPathLen,
                               const std::vector<std::unique_ptr<COperationRecovery>>& journals,
                               int resumed, int rolledBack, int unresolved)
{
    char directory[MAX_PATH];
    if (!GetJournalDirectory(directory, _countof(directory), TRUE)) return FALSE;
    const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
    _snprintf_s(reportPath, reportPathLen, _TRUNCATE, "%s\\reconciliation-%08lX.txt", directory,
                (DWORD)(timeSeed ^ (timeSeed >> 32)));
    HANDLE report = HANDLES_Q(CreateFileUtf8(reportPath, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL));
    if (report == INVALID_HANDLE_VALUE) return FALSE;
    char summary[256];
    _snprintf_s(summary, _countof(summary), _TRUNCATE,
                "Open Salamander operation recovery report\r\nResumed commits: %d\r\nRolled back temporary files: %d\r\nUnresolved items: %d\r\n\r\n",
                resumed, rolledBack, unresolved);
    BOOL ok = WriteAll(report, summary);
    // Report from the claimed snapshot; reopening a pathname would relinquish
    // ownership and can read another operation or fail against our own lease.
    for (const auto& journal : journals)
    {
        const std::string heading = "Journal: " + journal->Path + "\r\n" + journal->Detail + "\r\n";
        ok = ok && WriteAll(report, heading.c_str());
        for (const auto& entry : journal->Items)
        {
            const CRecoveryItem& item = entry.second;
            if (item.Temporary.empty()) continue;
            const std::string line = "Item " + std::to_string(item.Index) + ": " + item.Detail +
                "\r\n  Target: " + item.Target + "\r\n  Temporary: " + item.Temporary +
                "\r\n  Backup: " + item.Backup + "\r\n";
            ok = ok && WriteAll(report, line.c_str());
        }
    }
    if (ok) ok = FlushFileBuffers(report);
    if (!HANDLES(CloseHandle(report))) ok = FALSE;
    return ok;
}
} // namespace

COperationJournal::COperationJournal()
    : File(INVALID_HANDLE_VALUE), CurrentItem(-1), CurrentAttempt(0), Buffer(NULL), BufferUsed(0), WriteFailed(FALSE)
{
    Path[0] = 0;
}

COperationJournal::~COperationJournal()
{
    if (File != INVALID_HANDLE_VALUE) HANDLES(CloseHandle(File));
    if (Buffer != NULL) free(Buffer);
}

BOOL COperationJournal::SpillBuffer()
{
    if (File == INVALID_HANDLE_VALUE || WriteFailed)
        return FALSE;
    if (BufferUsed <= 0)
        return TRUE;
    if (!WriteAll(File, Buffer, (DWORD)BufferUsed))
    {
        WriteFailed = TRUE;
        return FALSE;
    }
    BufferUsed = 0;
    return TRUE;
}

BOOL COperationJournal::FlushDurable()
{
    // Recovery needs a complete record plus updated file-size metadata, not a
    // WRITE_THROUGH payload whose last bytes never became part of the on-disk size.
    if (!SpillBuffer() || File == INVALID_HANDLE_VALUE) return FALSE;
    if (!FlushFileBuffers(File)) { WriteFailed = TRUE; return FALSE; }
    return TRUE;
}

BOOL COperationJournal::Append(const char* text)
{
    // Checkpoint durability belongs in FlushDurable; flushing every fragment of a
    // multi-Append record stalled thousands of files at 0% for over an hour.
    if (text == NULL || File == INVALID_HANDLE_VALUE || Buffer == NULL || WriteFailed)
        return FALSE;
    const char* cursor = text;
    int remaining = (int)strlen(text);
    while (remaining > 0)
    {
        if (BufferUsed == JournalWriteBufferCapacity && !SpillBuffer())
            return FALSE;
        int space = JournalWriteBufferCapacity - BufferUsed;
        int chunk = remaining < space ? remaining : space;
        memcpy(Buffer + BufferUsed, cursor, chunk);
        BufferUsed += chunk;
        cursor += chunk;
        remaining -= chunk;
    }
    return TRUE;
}

BOOL COperationJournal::AppendPlanOperand(EOperationPlanOperandKind kind, const char* path, DWORD value)
{
    switch (kind)
    {
    case opokNone:
        return Append("");
    case opokPath:
        return Append(path == NULL ? "" : path);
    case opokDWORD:
    {
        char text[16];
        _snprintf_s(text, _countof(text), _TRUNCATE, "0x%08lX", value);
        return Append(text);
    }
    }
    return FALSE;
}

BOOL COperationJournal::AppendGoldenMasterPlan(COperations& operations)
{
    COperationPlan plan;
    if (!plan.Capture(operations))
        return FALSE;

    char header[128];
    _snprintf_s(header, _countof(header), _TRUNCATE, "PLAN|1|operation=%s|items=%d\r\n",
                plan.GetOperationId(), plan.GetCount());
    if (!Append(header))
        return FALSE;

    for (int index = 0; index < plan.GetCount(); ++index)
    {
        if (operations.IsCancellationRequested())
            return FALSE;
        const COperationPlanItem& item = plan.At(index);
        char prefix[192];
        _snprintf_s(prefix, _countof(prefix), _TRUNCATE,
                    "PLANITEM|%d|%s|size=%08lX:%08lX|file-size=%08lX:%08lX|attr=0x%08lX|flags=0x%08lX|source=",
                    index, COperationPlan::GetOpcodeName(item.Opcode),
                    item.Size.HiDWord, item.Size.LoDWord,
                    item.FileSize.HiDWord, item.FileSize.LoDWord,
                    item.Attr, item.OpFlags);
        if (!Append(prefix) ||
            !AppendPlanOperand(item.SourceKind, item.SourcePath, item.SourceValue) ||
            !Append("|target=") ||
            !AppendPlanOperand(item.TargetKind, item.TargetPath, item.TargetValue) ||
            !Append("\r\n"))
            return FALSE;
    }
    return TRUE;
}

BOOL COperationJournal::Begin(COperations& operations)
{
    if (File != INVALID_HANDLE_VALUE || Buffer != NULL)
        return FALSE;
    char directory[MAX_PATH];
    if (!GetJournalDirectory(directory, _countof(directory), TRUE)) return FALSE;
    // Allocate before CREATE_NEW so an OOM does not leave an empty journal behind.
    Buffer = (char*)malloc(JournalWriteBufferCapacity);
    if (Buffer == NULL)
        return FALSE;
    BufferUsed = 0;
    // Keep journal names fixed-width while avoiding a 49.7-day reuse of their timestamp component.
    const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
    _snprintf_s(Path, _countof(Path), _TRUNCATE, "%s\\operation-%08lX-%08lX.opj",
                directory, GetCurrentProcessId(), (DWORD)(timeSeed ^ (timeSeed >> 32)));
    File = HANDLES_Q(CreateFileUtf8(Path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL));
    if (File == INVALID_HANDLE_VALUE) return FALSE;
    char line[160];
    // Persist the ID independently of filenames so recovery matches an unfinished journal to diagnostics.
    _snprintf_s(line, _countof(line), _TRUNCATE,
                "FORMAT|2\r\nCORRELATION|operation=%s\r\nOPERATION|planned|items=%d\r\n",
                operations.GetCorrelationId(), operations.Count);
    if (!Append(line) || !AppendGoldenMasterPlan(operations)) return FALSE;
    int i;
    for (i = 0; i < operations.Count; i++)
    {
        if (operations.IsCancellationRequested())
            return FALSE;
        const COperation* operation = &operations.At(i);
        const char* opcode = OpcodeName(operation->Opcode);
        if (opcode == NULL) continue;
        const char* source = operation->Opcode == ocCreateDir ? operation->TargetName : operation->SourceName;
        const char* target = operation->TargetName;
        char prefix[160];
        char sequence[16];
        _snprintf_s(prefix, _countof(prefix), _TRUNCATE, "ITEM|%d|%s|", i, opcode);
        _snprintf_s(sequence, _countof(sequence), _TRUNCATE, "%d", i);
        // Recovery uses source/target paths, not a pre-opened identity. Opening every
        // source on removable/exFAT media blocked the copy dialog before any file ran.
        if (!Append(prefix) || !Append(source == NULL ? "" : source) || !Append("|") ||
            !Append(target == NULL ? "" : target) || !Append("|unavailable") ||
            !Append("|operation=") || !Append(operations.GetCorrelationId()) ||
            !Append("|sequence=") || !Append(sequence) || !Append("|attempt=1\r\n"))
            return FALSE;
    }
    return FlushDurable();
}

BOOL COperationJournal::BeginItem(int itemIndex, const COperation* operation, int attempt)
{
    if (OpcodeName(operation->Opcode) == NULL) return TRUE;
    CurrentItem = itemIndex;
    CurrentAttempt = attempt;
    char line[96];
    _snprintf_s(line, _countof(line), _TRUNCATE, "STATE|%d|prepared|attempt=%d\r\n", CurrentItem, CurrentAttempt);
    return Append(line) && FlushDurable();
}

void COperationJournal::RecordRetry(int attempt)
{
    if (CurrentItem >= 0)
    {
        CurrentAttempt = attempt;
        char line[80];
        // Retry history distinguishes another attempt from a duplicate callback after cancellation.
        _snprintf_s(line, _countof(line), _TRUNCATE, "RETRY|%d|attempt=%d\r\n", CurrentItem, CurrentAttempt);
        if (Append(line))
            FlushDurable();
    }
}

BOOL COperationJournal::SetTemporaryPath(const char* temporaryPath)
{
    if (CurrentItem < 0 || temporaryPath == NULL) return FALSE;
    char prefix[64];
    _snprintf_s(prefix, _countof(prefix), _TRUNCATE, "TEMP|%d|", CurrentItem);
    return Append(prefix) && Append(temporaryPath) && Append("\r\n") && FlushDurable();
}

BOOL COperationJournal::MarkTemporaryReady(const char* targetPath, const char* temporaryPath,
                                           HANDLE directory, HANDLE target, HANDLE temporary, BOOL preserveTargetSecurity)
{
    if (CurrentItem < 0 || targetPath == NULL || temporaryPath == NULL) return FALSE;
    CRecoveryObjectEvidence parent, targetEvidence, temporaryEvidence;
    if (!ReadRecoveryObjectIdentity(directory, parent) || !ReadRecoveryObjectIdentity(temporary, temporaryEvidence) ||
        (target != INVALID_HANDLE_VALUE && !ReadRecoveryObjectIdentity(target, targetEvidence))) return FALSE;
    // Readiness is captured only after copy verification from the retained
    // publication handles. Unsupported stream/reparse cases stay manual-only.
    const BOOL automatic = temporaryEvidence.PlainFile && RecoveryHasOnlyPrimaryStream(temporary) &&
        (target == INVALID_HANDLE_VALUE || (targetEvidence.PlainFile && RecoveryHasOnlyPrimaryStream(target)));
    if (automatic && (!CaptureRecoveryEvidence(temporary, temporaryEvidence) ||
                      (target != INVALID_HANDLE_VALUE && !CaptureRecoveryEvidence(target, targetEvidence)))) return FALSE;
    const std::string record = "READY2|" + std::to_string(CurrentItem) + "|attempt=" + std::to_string(CurrentAttempt) +
        (automatic ? "|auto=1|" : "|auto=0|") + targetPath + "|" + temporaryPath + "|" +
        SerializeRecoveryEvidence(targetEvidence) + "|" + SerializeRecoveryEvidence(temporaryEvidence) + "|" +
        SerializeRecoveryEvidence(parent) + (preserveTargetSecurity ? "|security=1|end\r\n" : "|security=0|end\r\n");
    char line[96];
    _snprintf_s(line, _countof(line), _TRUNCATE, "STATE|%d|temporary-ready|attempt=%d\r\n", CurrentItem, CurrentAttempt);
    return Append(record.c_str()) && Append(line) && FlushDurable();
}

void COperationJournal::CompleteItem(BOOL succeeded)
{
    if (CurrentItem >= 0)
    {
        char line[96];
        _snprintf_s(line, _countof(line), _TRUNCATE, "STATE|%d|%s|attempt=%d\r\n", CurrentItem,
                    succeeded ? "committed" : "failed", CurrentAttempt);
        if (Append(line))
            FlushDurable();
        CurrentItem = -1;
        CurrentAttempt = 0;
    }
}

BOOL COperationJournal::RecordPublicationState(const char* state, const WCHAR* backupPath)
{
    // Keep the backup name in every durable boundary record so even a truncated
    // later append leaves enough information to locate the previous version.
    if (CurrentItem < 0 || state == NULL || backupPath == NULL) return FALSE;
    char* backupUtf8 = ConvertAllocWideToUtf8(backupPath, -1);
    if (backupUtf8 == NULL) return FALSE;
    char prefix[96];
    _snprintf_s(prefix, _countof(prefix), _TRUNCATE, "PUBLICATION|%d|%s|attempt=%d|",
                CurrentItem, state, CurrentAttempt);
    BOOL ok = Append(prefix) && Append(backupUtf8) && Append("\r\n") && FlushDurable();
    free(backupUtf8);
    return ok;
}

void COperationJournal::Finish(BOOL failed, BOOL cancelled)
{
    if (File != INVALID_HANDLE_VALUE)
    {
        Append(cancelled ? "OPERATION|cancelled\r\n" : failed ? "OPERATION|failed\r\n" : "OPERATION|completed\r\n");
        FlushDurable();
        HANDLES(CloseHandle(File));
        File = INVALID_HANDLE_VALUE;
    }
}

void COperationJournal::PersistEmergencyShutdownState()
{
    // The normal journal remains authoritative for individual items; this
    // small marker explains why a later recovery scan found it unfinished.
    char directory[MAX_PATH];
    if (!GetJournalDirectory(directory, _countof(directory), TRUE)) return;
    char path[3 * MAX_PATH];
    // Preserve the established recovery-marker format while folding the 64-bit monotonic seed.
    const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
    _snprintf_s(path, _countof(path), _TRUNCATE, "%s\\memory-pressure-%08lX-%08lX.opj",
                directory, GetCurrentProcessId(), (DWORD)(timeSeed ^ (timeSeed >> 32)));
    HANDLE file = HANDLES_Q(CreateFileUtf8(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL));
    if (file == INVALID_HANDLE_VALUE) return;
    WriteAll(file, "FORMAT|1\r\nOPERATION|memory-pressure\r\n");
    FlushFileBuffers(file);
    HANDLES(CloseHandle(file));
}

void COperationJournal::OfferRecovery(HWND parent)
{
    char directory[MAX_PATH];
    if (!GetJournalDirectory(directory, _countof(directory), FALSE)) return;
    char pattern[3 * MAX_PATH];
    _snprintf_s(pattern, _countof(pattern), _TRUNCATE, "%s\\*.opj", directory);
    // Recovery runs in Release too, so enumerate through the common owned
    // wide-path boundary rather than the debug-only handle tracker wrapper.
    CWidePath patternW(pattern);
    const WCHAR* apiPattern = patternW.GetPathForWin32Api();
    if (apiPattern == NULL) return;
    WIN32_FIND_DATAW findData;
    HANDLE find = HANDLES_Q(FindFirstFileW(apiPattern, &findData));
    if (find == INVALID_HANDLE_VALUE) return;
    std::vector<std::unique_ptr<COperationRecovery>> journals;
    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        // Keep the complete Unicode name from enumeration through acquisition;
        // an unreadable journal must not disappear because a UTF-8 buffer clipped it.
        std::wstring journalPath(apiPattern);
        journalPath.resize(journalPath.rfind(L'\\') + 1);
        journalPath += findData.cFileName;
        std::unique_ptr<COperationRecovery> journal(new COperationRecovery);
        // The exclusive handle is acquired before parsing and retained across
        // the prompt, mutation, durable outcomes and report generation.
        if (!journal->Load(journalPath.c_str()))
        {
            if (journal->Error == ERROR_SHARING_VIOLATION || journal->Error == ERROR_LOCK_VIOLATION) continue;
            journal->Detail += " Win32 error " + std::to_string(journal->Error) + ".";
        }
        if (journal->NeedsRecovery()) journals.push_back(std::move(journal));
    } while (FindNextFileW(find, &findData));
    HANDLES(FindClose(find));
    if (journals.empty()) return;
    const int action = SalMessageBox(parent,
                               "Open Salamander found incomplete or unreadable file operations.\r\n\r\n"
                               "Yes resumes verified transactional targets.\r\n"
                               "No removes verified uncommitted temporary files.\r\n"
                               "Changed, partial or unverified files are preserved for manual recovery.\r\n"
                               "Cancel leaves recovery pending and writes a report.",
                               "Recover file operations", MB_YESNOCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON3);
    int resumed = 0, rolledBack = 0, unresolved = 0;
    for (const auto& journal : journals)
        journal->Reconcile(action, OperationExecutionFileSystem(), resumed, rolledBack, unresolved);
    char reportPath[3 * MAX_PATH];
    if (WriteReconciliationReport(reportPath, _countof(reportPath), journals, resumed, rolledBack, unresolved))
    {
        char message[3 * MAX_PATH + 160];
        _snprintf_s(message, _countof(message), _TRUNCATE,
                    "Recovery attempt finished.\r\nResumed: %d\r\nRolled back: %d\r\nUnresolved: %d\r\n\r\nReport: %s",
                    resumed, rolledBack, unresolved, reportPath);
        SalMessageBox(parent, message, "File operation recovery", MB_OK | MB_ICONINFORMATION);
    }
    else
        SalMessageBox(parent, "The recovery report could not be saved. Unresolved operations remain pending.",
                       "File operation recovery", MB_OK | MB_ICONEXCLAMATION);
}
