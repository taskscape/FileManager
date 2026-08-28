// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h>

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
    DWORD written = 0;
    return WriteFile(file, data, length, &written, NULL) && written == length;
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

BOOL IsTerminalJournal(const char* content)
{
    return strstr(content, "OPERATION|completed") != NULL ||
           strstr(content, "OPERATION|cancelled") != NULL ||
           strstr(content, "OPERATION|failed") != NULL ||
           strstr(content, "OPERATION|reconciled") != NULL;
}

char* ReadJournal(const char* path)
{
    HANDLE file = HANDLES_Q(CreateFileUtf8(path, GENERIC_READ,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
    if (file == INVALID_HANDLE_VALUE)
        return NULL;
    LARGE_INTEGER fileSize;
    // Recovery journals are deliberately bounded, so reject large values before narrowing to the read-buffer size.
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0 ||
        (ULONGLONG)fileSize.QuadPart > 16 * 1024 * 1024)
    {
        HANDLES(CloseHandle(file));
        return NULL;
    }
    DWORD size = (DWORD)fileSize.QuadPart;
    char* content = (char*)malloc(size + 1);
    DWORD read = 0;
    BOOL ok = content != NULL && ReadFile(file, content, size, &read, NULL) && read == size;
    HANDLES(CloseHandle(file));
    if (!ok)
    {
        if (content != NULL)
            free(content);
        return NULL;
    }
    content[size] = 0;
    return content;
}

void AppendRecoveryRecord(const char* path, const char* record)
{
    HANDLE file = HANDLES_Q(CreateFileUtf8(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL));
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteAll(file, record);
        FlushFileBuffers(file);
        HANDLES(CloseHandle(file));
    }
}

struct CRecoveryItem
{
    int Index;
    char* Target;
    char* Temporary;
    BOOL TemporaryReady;
    CRecoveryItem() : Index(-1), Target(NULL), Temporary(NULL), TemporaryReady(FALSE) {}
    ~CRecoveryItem()
    {
        if (Target != NULL) free(Target);
        if (Temporary != NULL) free(Temporary);
    }
};

CRecoveryItem* FindRecoveryItem(TDirectArray<CRecoveryItem*>& items, int index)
{
    int i;
    for (i = 0; i < items.Count; i++)
        if (items[i]->Index == index) return items[i];
    return NULL;
}

void SplitFields(char* line, char** fields, int fieldCount)
{
    int i;
    fields[0] = line;
    for (i = 1; i < fieldCount; i++)
    {
        char* separator = strchr(fields[i - 1], '|');
        if (separator == NULL)
            fields[i] = (char*)"";
        else
        {
            *separator = 0;
            fields[i] = separator + 1;
        }
    }
}

void ParseRecoveryItems(char* content, TDirectArray<CRecoveryItem*>& items)
{
    char* line = content;
    while (line != NULL && *line != 0)
    {
        char* next = strchr(line, '\n');
        if (next != NULL) *next++ = 0;
        int length = (int)strlen(line);
        if (length > 0 && line[length - 1] == '\r') line[length - 1] = 0;
        char* fields[6];
        SplitFields(line, fields, _countof(fields));
        if (strcmp(fields[0], "ITEM") == 0)
        {
            CRecoveryItem* item = new CRecoveryItem;
            if (item != NULL)
            {
                item->Index = atoi(fields[1]);
                item->Target = _strdup(fields[4]);
                // TDirectArray::Add returns the inserted index (including zero), so only -1 means ownership was not transferred.
                if (items.Add(item) == -1) delete item;
            }
        }
        else if (strcmp(fields[0], "TEMP") == 0)
        {
            CRecoveryItem* item = FindRecoveryItem(items, atoi(fields[1]));
            if (item != NULL)
            {
                if (item->Temporary != NULL) free(item->Temporary);
                item->Temporary = _strdup(fields[2]);
            }
        }
        else if (strcmp(fields[0], "STATE") == 0 && strcmp(fields[2], "temporary-ready") == 0)
        {
            CRecoveryItem* item = FindRecoveryItem(items, atoi(fields[1]));
            if (item != NULL) item->TemporaryReady = TRUE;
        }
        line = next;
    }
}

BOOL IsSiblingTransactionalTemporary(const char* temporaryPath, const char* targetPath)
{
    if (temporaryPath == NULL || targetPath == NULL) return FALSE;
    char temporaryDirectory[3 * MAX_PATH];
    char targetDirectory[3 * MAX_PATH];
    // Transactional sibling checks must compare complete directories, not clipped prefixes.
    if (FAILED(StringCchCopyA(temporaryDirectory, _countof(temporaryDirectory), temporaryPath)) ||
        FAILED(StringCchCopyA(targetDirectory, _countof(targetDirectory), targetPath)))
        return FALSE;
    if (!CutDirectory(temporaryDirectory) || !CutDirectory(targetDirectory) ||
        StrICmp(temporaryDirectory, targetDirectory) != 0) return FALSE;
    const char* name = strrchr(temporaryPath, '\\');
    name = name == NULL ? temporaryPath : name + 1;
    return _strnicmp(name, "SALCP", 5) == 0;
}

BOOL MoveTemporaryToTarget(const char* temporaryPath, const char* targetPath)
{
    CStrP temporaryPathW(ConvertAllocUtf8ToWide(temporaryPath, -1));
    CStrP targetPathW(ConvertAllocUtf8ToWide(targetPath, -1));
    return temporaryPathW != NULL && targetPathW != NULL &&
           MoveFileExW(temporaryPathW, targetPathW, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

void ReconcileJournal(const char* path, int action, int& resumed, int& rolledBack, int& unresolved)
{
    char* content = ReadJournal(path);
    if (content == NULL) { unresolved++; return; }
    TDirectArray<CRecoveryItem*> items(8, 8);
    ParseRecoveryItems(content, items);
    int i;
    for (i = 0; i < items.Count; i++)
    {
        CRecoveryItem* item = items[i];
        if (item->Temporary == NULL || item->Target == NULL ||
            !IsSiblingTransactionalTemporary(item->Temporary, item->Target) || !FileExists(item->Temporary))
            continue;
        if (action == IDYES && item->TemporaryReady)
        {
            if (MoveTemporaryToTarget(item->Temporary, item->Target)) resumed++; else unresolved++;
        }
        else if (action == IDNO)
        {
            if (DeleteFileUtf8(item->Temporary)) rolledBack++; else unresolved++;
        }
        else unresolved++;
    }
    for (i = 0; i < items.Count; i++) delete items[i];
    free(content);
    AppendRecoveryRecord(path, "OPERATION|reconciled\r\n");
}

BOOL WriteReconciliationReport(char* reportPath, int reportPathLen, TDirectArray<char*>& journals,
                               int resumed, int rolledBack, int unresolved)
{
    char directory[MAX_PATH];
    if (!GetJournalDirectory(directory, _countof(directory), TRUE)) return FALSE;
    // Preserve the fixed recovery-report name while folding uptime beyond the 32-bit tick cycle.
    const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
    _snprintf_s(reportPath, reportPathLen, _TRUNCATE, "%s\\reconciliation-%08lX.txt", directory,
                (DWORD)(timeSeed ^ (timeSeed >> 32)));
    HANDLE report = HANDLES_Q(CreateFileUtf8(reportPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL));
    if (report == INVALID_HANDLE_VALUE) return FALSE;
    char summary[256];
    _snprintf_s(summary, _countof(summary), _TRUNCATE,
                "Open Salamander operation recovery report\r\nResumed commits: %d\r\nRolled back temporary files: %d\r\nUnresolved items: %d\r\n\r\n",
                resumed, rolledBack, unresolved);
    BOOL ok = WriteAll(report, summary);
    int i;
    for (i = 0; ok && i < journals.Count; i++)
    {
        char heading[3 * MAX_PATH + 32];
        _snprintf_s(heading, _countof(heading), _TRUNCATE, "Journal: %s\r\n", journals.At(i));
        char* content = ReadJournal(journals.At(i));
        ok = WriteAll(report, heading) && content != NULL;
        if (content != NULL)
        {
            ok = ok && WriteAll(report, content) && WriteAll(report, "\r\n");
            free(content);
        }
    }
    if (ok) ok = FlushFileBuffers(report);
    HANDLES(CloseHandle(report));
    return ok;
}
} // namespace

COperationJournal::COperationJournal()
    : File(INVALID_HANDLE_VALUE), CurrentItem(-1), CurrentAttempt(0), Buffer(NULL), BufferUsed(0)
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
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;
    if (BufferUsed <= 0)
        return TRUE;
    if (!WriteAll(File, Buffer, (DWORD)BufferUsed))
        return FALSE;
    BufferUsed = 0;
    return TRUE;
}

BOOL COperationJournal::FlushDurable()
{
    // Recovery needs a complete record plus updated file-size metadata, not a
    // WRITE_THROUGH payload whose last bytes never became part of the on-disk size.
    return SpillBuffer() && File != INVALID_HANDLE_VALUE && FlushFileBuffers(File);
}

BOOL COperationJournal::Append(const char* text)
{
    // Checkpoint durability belongs in FlushDurable; flushing every fragment of a
    // multi-Append record stalled thousands of files at 0% for over an hour.
    if (text == NULL || File == INVALID_HANDLE_VALUE || Buffer == NULL)
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
                "FORMAT|1\r\nCORRELATION|operation=%s\r\nOPERATION|planned|items=%d\r\n",
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

BOOL COperationJournal::MarkTemporaryReady()
{
    if (CurrentItem < 0) return FALSE;
    char line[80];
    _snprintf_s(line, _countof(line), _TRUNCATE, "STATE|%d|temporary-ready\r\n", CurrentItem);
    return Append(line) && FlushDurable();
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
    TDirectArray<char*> journals(4, 4);
    do
    {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            char name[MAX_PATH * 3];
            if (ConvertWideToUtf8(findData.cFileName, -1, name, _countof(name)) == 0)
                continue;
            char path[3 * MAX_PATH];
            _snprintf_s(path, _countof(path), _TRUNCATE, "%s\\%s", directory, name);
            char* content = ReadJournal(path);
            if (content != NULL)
            {
                if (!IsTerminalJournal(content))
                {
                    char* copy = _strdup(path);
                    // Preserve the first discovered journal: index zero is a successful insertion, not an allocation failure.
                    if (copy != NULL && journals.Add(copy) == -1) free(copy);
                }
                free(content);
            }
        }
    } while (FindNextFileW(find, &findData));
    HANDLES(FindClose(find));
    if (journals.Count == 0) return;
    int action = SalMessageBox(parent,
                               "Open Salamander found incomplete file operations.\r\n\r\n"
                               "Yes resumes only fully written transactional targets.\r\n"
                               "No rolls back known uncommitted transactional targets.\r\n"
                               "Cancel leaves files unchanged and writes a reconciliation report.",
                               "Recover file operations", MB_YESNOCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON3);
    int resumed = 0, rolledBack = 0, unresolved = 0;
    int i;
    for (i = 0; i < journals.Count; i++) ReconcileJournal(journals[i], action, resumed, rolledBack, unresolved);
    char reportPath[3 * MAX_PATH];
    if (WriteReconciliationReport(reportPath, _countof(reportPath), journals, resumed, rolledBack, unresolved))
    {
        char message[3 * MAX_PATH + 160];
        _snprintf_s(message, _countof(message), _TRUNCATE,
                    "Recovery reconciliation completed.\r\nResumed: %d\r\nRolled back: %d\r\nUnresolved: %d\r\n\r\nReport: %s",
                    resumed, rolledBack, unresolved, reportPath);
        SalMessageBox(parent, message, "File operation recovery", MB_OK | MB_ICONINFORMATION);
    }
    for (i = 0; i < journals.Count; i++) free(journals[i]);
}
