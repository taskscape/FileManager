// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <climits>
#include "recovery_evidence.h"
#include "recovery_line_reader.h"
#include "recovery_journal_lease.h"
#include "conditional_file_publication.h"

// The application and native fault tests share this parser and mutation boundary.
// An entire claimed journal is validated before any of its records authorize I/O.
struct CRecoveryItem
{
    int Index = -1, Attempt = 1;
    std::string Target, Temporary, Publication, Backup, Detail;
    CRecoveryObjectEvidence ExpectedTarget, ExpectedTemporary, Parent;
    BOOL Ready = FALSE, Automatic = FALSE, PreserveSecurity = TRUE, Resolved = FALSE;
    BOOL DiscardPlanned = FALSE;
    void ResetReadiness()
    {
        Ready = Automatic = Resolved = DiscardPlanned = FALSE;
        Publication.clear(); Backup.clear(); Detail.clear();
    }
};

inline std::vector<std::string> RecoveryFields(const std::string& line)
{
    std::vector<std::string> fields;
    size_t begin = 0;
    while (true)
    {
        const size_t end = line.find('|', begin);
        fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return fields;
        begin = end + 1;
    }
}

inline BOOL RecoveryNumber(const std::string& text, int& value)
{
    if (text.empty()) return FALSE;
    value = 0;
    for (char digit : text)
    {
        if (digit < '0' || digit > '9' || value > (INT_MAX - (digit - '0')) / 10) return FALSE;
        value = value * 10 + digit - '0';
    }
    return TRUE;
}

inline BOOL RecoveryAttempt(const std::string& text, int& value)
{
    return text.compare(0, 8, "attempt=") == 0 && RecoveryNumber(text.substr(8), value) && value > 0;
}

inline BOOL RecoveryWidePath(const std::string& text, std::wstring& path)
{
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, NULL, 0);
    if (count <= 1) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    std::vector<WCHAR> buffer(count);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.c_str(), -1, buffer.data(), count)) return FALSE;
    path.assign(buffer.data());
    // Journal paths must remain absolute; startup's current directory is unrelated to the original operation.
    if (!(path.size() > 3 && path[1] == L':' && path[2] == L'\\') && path.compare(0, 2, L"\\\\") != 0)
    { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    return TRUE;
}

inline std::string RecoveryUtf8Path(const WCHAR* path)
{
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, NULL, 0, NULL, NULL);
    if (count <= 0) return std::string();
    std::vector<char> buffer(count);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, buffer.data(), count, NULL, NULL)) return std::string();
    return buffer.data();
}

class COperationRecovery
{
public:
    CRecoveryJournalLease Lease;
    std::map<int, CRecoveryItem> Items;
    DWORD Error = ERROR_SUCCESS;
    int Format = 0;
    BOOL Finished = FALSE, Reconciled = FALSE;
    std::string Path, Detail;

    BOOL Load(const WCHAR* path)
    {
        Path = RecoveryUtf8Path(path);
        if (!Lease.Open(path)) { Error = GetLastError(); return FALSE; }
        return Parse();
    }
    BOOL Parse()
    {
        Items.clear(); Error = ERROR_SUCCESS; Format = 0; Finished = Reconciled = FALSE;
        LARGE_INTEGER start = {};
        if (!SetFilePointerEx(Lease.Get(), start, NULL, FILE_BEGIN)) return Fail(GetLastError(), "Cannot seek the claimed journal.");
        CRecoveryLineReader reader(Lease.Get());
        std::string line;
        CRecoveryLineReader::EResult result;
        while ((result = reader.Next(line)) == CRecoveryLineReader::Line)
            if (!ParseLine(line)) return Fail(ERROR_INVALID_DATA, "Malformed or unsupported recovery record; files were preserved.");
        if (result == CRecoveryLineReader::Failed) return Fail(reader.GetError(), "Incomplete or unreadable journal; files were preserved.");
        if (Format == 0) return Fail(ERROR_INVALID_DATA, "Journal format is missing; files were preserved.");
        return TRUE;
    }

    BOOL NeedsRecovery() const
    {
        if (Error != ERROR_SUCCESS) return TRUE;
        for (const auto& entry : Items)
            if (!entry.second.Temporary.empty() && !entry.second.Resolved) return TRUE;
        return !Finished && !Reconciled;
    }

    void Reconcile(int action, COperationExecutionFileSystem& fileSystem,
                   int& resumed, int& rolledBack, int& unresolved)
    {
        if (Error != ERROR_SUCCESS) { ++unresolved; return; }
        BOOL pending = FALSE;
        for (auto& entry : Items)
        {
            CRecoveryItem& item = entry.second;
            if (item.Temporary.empty() || item.Resolved) continue;
            if (action == IDCANCEL)
            {
                item.Detail = "Deferred by user; recovery remains pending.";
                pending = TRUE; ++unresolved; continue;
            }
            if (!RecoverItem(item, action, fileSystem))
            {
                pending = TRUE; ++unresolved;
                if (item.Detail.empty()) item.Detail = "Recovery failed (Win32 " + std::to_string(GetLastError()) + "); files were preserved where possible.";
            }
            else if (item.Detail == "discarded") ++rolledBack;
            else ++resumed;
        }
        // A report and a user's cancellation are not terminal outcomes. Per-item
        // records let a later startup skip successes in an otherwise pending journal.
        if (action != IDCANCEL && !pending)
        {
            if (!Lease.Append(fileSystem, "OPERATION|reconciled\r\n"))
            { ++unresolved; Detail = "Could not durably finish reconciliation; recovery remains pending."; }
            else Reconciled = TRUE;
        }
    }

private:
    BOOL Fail(DWORD error, const char* detail)
    {
        Error = error == ERROR_SUCCESS ? ERROR_INVALID_DATA : error;
        Detail = detail; SetLastError(Error); return FALSE;
    }
    BOOL ParseLine(const std::string& line)
    {
        const std::vector<std::string> fields = RecoveryFields(line);
        const std::string& kind = fields[0];
        if (kind == "FORMAT")
        {
            if (Format != 0 || fields.size() != 2 || (fields[1] != "1" && fields[1] != "2")) return FALSE;
            Format = fields[1] == "2" ? 2 : 1; return TRUE;
        }
        if (Format == 0) return FALSE;
        if (kind == "CORRELATION" || kind == "PLAN" || kind == "PLANITEM") return TRUE;
        if (kind == "OPERATION")
        {
            if (fields.size() < 2) return FALSE;
            if (fields[1] == "reconciled") Reconciled = TRUE;
            else if (fields[1] == "completed" || fields[1] == "cancelled" || fields[1] == "failed") Finished = TRUE;
            else if (fields[1] != "planned" && fields[1] != "memory-pressure") return FALSE;
            return TRUE;
        }
        int index;
        if (fields.size() < 3 || !RecoveryNumber(fields[1], index)) return FALSE;
        if (kind == "ITEM")
        {
            if (fields.size() < 6 || Items.find(index) != Items.end()) return FALSE;
            CRecoveryItem& item = Items[index]; item.Index = index; item.Target = fields[4]; return TRUE;
        }
        const auto found = Items.find(index);
        if (found == Items.end()) return FALSE;
        CRecoveryItem& item = found->second;
        if (kind == "TEMP")
        {
            if (fields.size() != 3 || fields[2].empty()) return FALSE;
            item.ResetReadiness(); item.Temporary = fields[2]; return TRUE;
        }
        if (kind == "RETRY")
        {
            int attempt;
            if (fields.size() != 3 || !RecoveryAttempt(fields[2], attempt) || attempt <= item.Attempt) return FALSE;
            item.Attempt = attempt; item.ResetReadiness(); return TRUE;
        }
        if (kind == "READY2")
        {
            int attempt;
            if (Format != 2 || fields.size() != 11 || !RecoveryAttempt(fields[2], attempt) || attempt != item.Attempt ||
                (fields[3] != "auto=0" && fields[3] != "auto=1") || fields[4] != item.Target || fields[5] != item.Temporary ||
                !ParseRecoveryEvidence(fields[6], item.ExpectedTarget) || !ParseRecoveryEvidence(fields[7], item.ExpectedTemporary) ||
                !ParseRecoveryEvidence(fields[8], item.Parent) || !item.ExpectedTemporary.Present || !item.Parent.Present ||
                (fields[9] != "security=0" && fields[9] != "security=1") || fields[10] != "end") return FALSE;
            item.Ready = TRUE; item.Automatic = fields[3] == "auto=1"; item.PreserveSecurity = fields[9] == "security=1";
            return TRUE;
        }
        if (kind == "STATE")
        {
            int attempt = item.Attempt;
            if (Format == 2 && (fields.size() != 4 || !RecoveryAttempt(fields[3], attempt) || attempt != item.Attempt)) return FALSE;
            // A historical ready marker never creates evidence. A retry or a new
            // TEMP record has already revoked readiness for the previous object.
            if (fields[2] == "prepared") item.ResetReadiness();
            else if (fields[2] == "committed") item.Resolved = !item.Ready || item.Publication == "publication-complete";
            else if (fields[2] != "failed" && fields[2] != "temporary-ready") return FALSE;
            return TRUE;
        }
        if (kind == "PUBLICATION" || kind == "RECOVERY")
        {
            int attempt;
            if (fields.size() != 5 || !RecoveryAttempt(fields[3], attempt) || attempt != item.Attempt) return FALSE;
            if (kind == "PUBLICATION")
            {
                if (!item.Ready && Format == 2 && fields[2] != "temporary-discarded") return FALSE;
                item.Publication = fields[2]; item.Backup = fields[4];
                if (fields[2] == "publication-complete" || fields[2] == "temporary-discarded") item.Resolved = TRUE;
            }
            else if (fields[2] == "resumed" || fields[2] == "discarded") item.Resolved = TRUE;
            else if (fields[2] == "discard-planned") item.DiscardPlanned = TRUE;
            else return FALSE;
            return TRUE;
        }
        return FALSE;
    }
    struct CRecorder
    {
        COperationRecovery* Owner;
        COperationExecutionFileSystem* FileSystem;
        CRecoveryItem* Item;
        static BOOL Append(void* context, const char* state, const WCHAR* backup)
        {
            CRecorder& recorder = *(CRecorder*)context;
            const std::string name = RecoveryUtf8Path(backup);
            if (name.empty()) return FALSE;
            const std::string record = "PUBLICATION|" + std::to_string(recorder.Item->Index) + "|" + state +
                "|attempt=" + std::to_string(recorder.Item->Attempt) + "|" + name + "\r\n";
            if (!recorder.Owner->Lease.Append(*recorder.FileSystem, record)) return FALSE;
            recorder.Item->Publication = state; recorder.Item->Backup = name; return TRUE;
        }
    };
    BOOL Outcome(CRecoveryItem& item, const char* state, COperationExecutionFileSystem& fileSystem)
    {
        if (!Lease.Append(fileSystem, "RECOVERY|" + std::to_string(item.Index) + "|" + state +
                                      "|attempt=" + std::to_string(item.Attempt) + "|end\r\n")) return FALSE;
        item.Resolved = TRUE; item.Detail = state; return TRUE;
    }
    static BOOL LeafAbsent(HANDLE directory, const std::wstring& path)
    {
        const std::wstring leaf = path.substr(path.rfind(L'\\') + 1);
        HANDLE file = OpenRelativePublicationFile(directory, leaf.c_str(), FILE_READ_ATTRIBUTES,
                                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        if (file == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
        CloseHandle(file); SetLastError(ERROR_ALREADY_EXISTS); return FALSE;
    }
    BOOL FinishInterruptedDiscard(CRecoveryItem& item, CConditionalFilePublication& publication,
                                  const std::wstring& target, const std::wstring& temporary,
                                  COperationExecutionFileSystem& fileSystem)
    {
        // A durable discard intent can outlive a failed outcome append. Only a
        // positively opened, matching directory makes a missing leaf meaningful;
        // an offline or redirected volume remains pending.
        CRecoveryObjectEvidence parent;
        HANDLE directory = publication.DirectoryHandle();
        if (!item.DiscardPlanned || directory == INVALID_HANDLE_VALUE ||
            !ReadRecoveryObjectIdentity(directory, parent) || !SameRecoveryObject(item.Parent, parent) ||
            !LeafAbsent(directory, temporary) || !LeafAbsent(directory, publication.BackupName())) return FALSE;
        BOOL matches = FALSE;
        if (!item.ExpectedTarget.Present) matches = LeafAbsent(directory, target);
        else
        {
            const std::wstring leaf = target.substr(target.rfind(L'\\') + 1);
            HANDLE file = OpenRelativePublicationFile(directory, leaf.c_str(), GENERIC_READ, FILE_SHARE_READ);
            if (file != INVALID_HANDLE_VALUE)
            {
                matches = VerifyRecoveryEvidence(file, item.ExpectedTarget) && RecoveryHasOnlyPrimaryStream(file);
                CloseHandle(file);
            }
        }
        return matches && Outcome(item, "discarded", fileSystem);
    }
    BOOL RecoverItem(CRecoveryItem& item, int action, COperationExecutionFileSystem& fileSystem)
    {
        if (!item.Ready || !item.Automatic)
        {
            item.Detail = "No complete automatic-recovery evidence (legacy, partial, named-stream or reparse file); manual recovery required.";
            SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
        }
        if (!item.Publication.empty() && item.Publication != "publication-planned" && item.Publication != "destination-restored")
        {
            item.Detail = "Publication was interrupted; preserve the target and recorded previous-version backup for manual recovery.";
            SetLastError(ERROR_INVALID_DATA); return FALSE;
        }
        std::wstring target, temporary;
        if (!RecoveryWidePath(item.Target, target) || !RecoveryWidePath(item.Temporary, temporary)) return FALSE;
        CConditionalFilePublication publication;
        if (!publication.Open(target.c_str(), temporary.c_str(), item.ExpectedTarget.Present, item.PreserveSecurity))
        {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND && FinishInterruptedDiscard(item, publication, target, temporary, fileSystem)) return TRUE;
            SetLastError(error); return FALSE;
        }
        CRecoveryObjectEvidence actualParent;
        if (!ReadRecoveryObjectIdentity(publication.DirectoryHandle(), actualParent) || !SameRecoveryObject(item.Parent, actualParent) ||
            !LeafAbsent(publication.DirectoryHandle(), publication.BackupName()) ||
            (!item.ExpectedTarget.Present && !LeafAbsent(publication.DirectoryHandle(), target)) ||
            !VerifyRecoveryEvidence(publication.TemporaryHandle(), item.ExpectedTemporary) ||
            !RecoveryHasOnlyPrimaryStream(publication.TemporaryHandle()) ||
            (item.ExpectedTarget.Present && (!VerifyRecoveryEvidence(publication.TargetHandle(), item.ExpectedTarget) ||
                                            !RecoveryHasOnlyPrimaryStream(publication.TargetHandle()))))
        {
            item.Detail = "The directory, destination or staging file no longer matches its recorded identity and content; files were preserved.";
            SetLastError(ERROR_FILE_INVALID); return FALSE;
        }
        if (action == IDYES)
        {
            CRecorder recorder = {this, &fileSystem, &item};
            const CPublicationOutcome outcome = publication.Commit(fileSystem, CRecorder::Append, &recorder);
            if (outcome.Error != ERROR_SUCCESS || outcome.CleanupError != ERROR_SUCCESS || !outcome.Committed)
            {
                SetLastError(outcome.Error != ERROR_SUCCESS ? outcome.Error : outcome.CleanupError);
                item.Detail = "Publication did not durably finish; preserve any recorded backup and retry or recover manually.";
                return FALSE;
            }
            return Outcome(item, "resumed", fileSystem);
        }
        // Record discard intent before deletion, then require a durable outcome;
        // a failed record append never becomes an operation-wide success.
        if (!Lease.Append(fileSystem, "RECOVERY|" + std::to_string(item.Index) + "|discard-planned|attempt=" +
                                      std::to_string(item.Attempt) + "|end\r\n")) return FALSE;
        if (!publication.DiscardTemporary(fileSystem)) return FALSE;
        return Outcome(item, "discarded", fileSystem);
    }
};
