// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <objbase.h>
#include <memory>
#include "conditional_file_publication.h"
#include "recovery_evidence.h"
#include "recovery_line_reader.h"
#include "ftp_download_file_system.h"
#pragma comment(lib, "ole32.lib")

inline std::string FtpMetadataHex(const std::string& value)
{
    const char* digits = "0123456789abcdef";
    std::string encoded;
    for (unsigned char byte : value) { encoded += digits[byte >> 4]; encoded += digits[byte & 15]; }
    return encoded;
}
inline std::string FtpMetadataPath(const std::wstring& path)
{
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(), (int)path.size(), NULL, 0, NULL, NULL);
    if (count <= 0) return std::string();
    std::string result(count, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path.c_str(), (int)path.size(), &result[0], count, NULL, NULL) != count)
        return std::string();
    return FtpMetadataHex(result);
}

// The item, disk work and finalization request share this owner. Worker handles
// are borrowed, so cancellation or rejected queue admission cannot lose ownership.
// Only disk-thread work may mutate it. The sidecar lease excludes another process;
// a crash leaves evidence and the old destination for a conservative retry.
class CFtpTransactionalDownload
{
public:
    CFtpTransactionalDownload() = default;
    ~CFtpTransactionalDownload()
    {
        if (Metadata != INVALID_HANDLE_VALUE) ::CloseHandle(Metadata);
    }
    CFtpTransactionalDownload(const CFtpTransactionalDownload&) = delete;
    CFtpTransactionalDownload& operator=(const CFtpTransactionalDownload&) = delete;

    HANDLE File() const { return Publication.TemporaryHandle(); }
    const std::wstring& StagePath() const { return Stage; }
    const std::wstring& MetadataPath() const { return Sidecar; }
    const std::wstring& TargetPath() const { return Target; }
    BOOL MatchesTarget(const std::wstring& target) const
    { return _wcsicmp(Target.c_str(), target.c_str()) == 0; }
    BOOL Matches(const std::wstring& target, const std::string& identity) const
    { return MatchesTarget(target) && Identity == identity; }
    BOOL IsPublished() const { return Published; }
    BOOL IsUsable() const { return File() != INVALID_HANDLE_VALUE && !Published && !Terminal; }

    BOOL Create(const WCHAR* target, const std::string& remoteIdentity, BOOL overwrite,
                 BOOL seedFromTarget, CFtpDownloadFileSystem& fileSystem)
    {
        if (Metadata != INVALID_HANDLE_VALUE || File() != INVALID_HANDLE_VALUE)
            return Fail(ERROR_INVALID_STATE);
        const DWORD length = GetFullPathNameW(target, 0, NULL, NULL);
        if (length == 0) return FALSE;
        std::vector<WCHAR> full(length);
        const DWORD copied = GetFullPathNameW(target, length, full.data(), NULL);
        if (copied == 0 || copied >= length) return Fail(ERROR_INVALID_NAME);
        Target.assign(full.data(), copied);
        const size_t separator = Target.rfind(L'\\');
        if (separator == std::wstring::npos || separator + 1 == Target.size()) return Fail(ERROR_INVALID_NAME);
        const DWORD attributes = GetFileAttributesW(Target.c_str());
        const BOOL present = attributes != INVALID_FILE_ATTRIBUTES;
        if (!present && GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
        if (present && !overwrite) return Fail(ERROR_FILE_EXISTS);
        if (present && (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
            return Fail(ERROR_NOT_SUPPORTED);
        GUID id;
        if (FAILED(CoCreateGuid(&id))) return Fail(ERROR_NOT_ENOUGH_MEMORY);
        WCHAR identifier[40];
        if (StringFromGUID2(id, identifier, ARRAYSIZE(identifier)) == 0) return Fail(ERROR_INVALID_DATA);
        Stage = Target.substr(0, separator + 1) + L".salftp-" + identifier + L".part";
        if (!Publication.Open(Target.c_str(), Stage.c_str(), present, TRUE, TRUE)) return FALSE;
        // Use the physical parent for restart metadata even if a junction is retargeted.
        const std::wstring backup = Publication.BackupName();
        Stage = backup.substr(0, backup.size() - wcslen(L".previous"));
        Sidecar = Stage + L".meta";
        Metadata = OpenRelativePublicationFile(Publication.DirectoryHandle(),
            Sidecar.substr(Sidecar.rfind(L'\\') + 1).c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, TRUE);
        if (Metadata == INVALID_HANDLE_VALUE) return FALSE;
        Identity = remoteIdentity;
        if (!ReadRecoveryObjectIdentity(Publication.DirectoryHandle(), ParentEvidence)) return FALSE;
        if (present && (!CaptureRecoveryEvidence(Publication.TargetHandle(), TargetEvidence) ||
                         !RecoveryHasOnlyPrimaryStream(Publication.TargetHandle())))
            return Fail(ERROR_NOT_SUPPORTED);
        if (!Append(fileSystem, "FTPSTAGE|1\nTARGET|" + FtpMetadataPath(Target) + "\nREMOTE|" +
            FtpMetadataHex(Identity) + "\nPARENT|" + SerializeRecoveryEvidence(ParentEvidence) +
            "\nORIGINAL|" + SerializeRecoveryEvidence(TargetEvidence) + "\nWRITING\n")) return FALSE;
        if (seedFromTarget && present)
        {
            LARGE_INTEGER zero = {};
            if (!SetFilePointerEx(Publication.TargetHandle(), zero, NULL, FILE_BEGIN)) return FALSE;
            std::vector<BYTE> buffer(64 * 1024);
            DWORD read;
            do
            {
                if (!ReadFile(Publication.TargetHandle(), buffer.data(), (DWORD)buffer.size(), &read, NULL)) return FALSE;
                DWORD offset = 0;
                while (offset < read)
                {
                    DWORD written = 0;
                    if (!fileSystem.WriteFile(File(), buffer.data() + offset, read - offset, &written, NULL)) return FALSE;
                    if (written == 0 || written > read - offset) return Fail(ERROR_WRITE_FAULT);
                    offset += written;
                }
            } while (read != 0);
        }
        return BeginWriting(fileSystem, FALSE);
    }

    BOOL BeginWriting(CFtpDownloadFileSystem& fileSystem, BOOL reset)
    {
        if (!IsUsable()) return Fail(ERROR_INVALID_STATE);
        // Durable revocation precedes any new write or truncation of a checkpoint.
        if (!Append(fileSystem, "WRITING\n")) return FALSE;
        if (reset && !fileSystem.Truncate(File(), 0)) return FALSE;
        LARGE_INTEGER zero = {};
        return SetFilePointerEx(File(), zero, NULL, FILE_BEGIN);
    }

    BOOL RestartIdentity(const std::string& identity, CFtpDownloadFileSystem& fileSystem)
    {
        // A mode/version change discards the private prefix while retaining the
        // approved target handle. Never reopen that target as a new approval.
        std::string nextIdentity(identity);
        if (!BeginWriting(fileSystem, FALSE) ||
            !Append(fileSystem, "REMOTE|" + FtpMetadataHex(nextIdentity) + "\n") ||
            !fileSystem.Truncate(File(), 0)) return FALSE;
        Identity.swap(nextIdentity);
        return TRUE;
    }

    BOOL Checkpoint(CFtpDownloadFileSystem& fileSystem)
    {
        if (!IsUsable()) return Fail(ERROR_INVALID_STATE);
        CRecoveryObjectEvidence current;
        if (!fileSystem.FlushFileBuffers(File()) || !CaptureRecoveryEvidence(File(), current) ||
            !RecoveryHasOnlyPrimaryStream(File())) return FALSE;
        return Append(fileSystem, "CHECKPOINT|" + SerializeRecoveryEvidence(current) + "\n");
    }

    BOOL Finish(CFtpDownloadFileSystem& fileSystem, ULONGLONG expectedLength, BOOL expectedKnown,
                 const FILETIME* writeTime)
    {
        if (!IsUsable()) return Fail(ERROR_INVALID_STATE);
        LARGE_INTEGER actual = {};
        if (!fileSystem.GetSize(File(), actual)) return FALSE;
        if (actual.QuadPart < 0 || (expectedKnown && (ULONGLONG)actual.QuadPart != expectedLength))
            return Fail(ERROR_INVALID_DATA);
        if (writeTime != NULL && !fileSystem.SetFileTime(File(), NULL, NULL, writeTime)) return FALSE;
        if (!Checkpoint(fileSystem)) return FALSE;
        ActiveFileSystem = &fileSystem;
        const CPublicationOutcome result = Publication.Commit(fileSystem, RecordPublication, this);
        ActiveFileSystem = NULL;
        Published = result.Committed;
        // After publication begins, ambiguity preserves sidecar/backup and the
        // remote source. No retry may rewrite a now-published destination.
        Terminal = Published || result.BackupRetained;
        if (result.Error != ERROR_SUCCESS) return Fail(result.Error);
        if (result.CleanupError != ERROR_SUCCESS) return Fail(result.CleanupError);
        if (!Published) return Fail(ERROR_WRITE_FAULT);
        if (!Publication.CloseChecked(fileSystem)) return FALSE;
        if (!Append(fileSystem, "DURABLE\n")) return FALSE;
        // Remove only the sidecar object still owned by this request, never a
        // pathname occupant. Failed cleanup retains the remote move source.
        FILE_DISPOSITION_INFO disposition = { TRUE };
        if (!fileSystem.SetFileInformationByHandle(Metadata, FileDispositionInfo, &disposition, sizeof(disposition))) return FALSE;
        const HANDLE file = Metadata;
        Metadata = INVALID_HANDLE_VALUE;
        if (!fileSystem.CloseFile(file)) return FALSE;
        // Restart never replays remote deletion, including an unknown close result.
        return TRUE;
    }

    static std::shared_ptr<CFtpTransactionalDownload> TryResume(const WCHAR* target, const std::string& identity,
                                                               CFtpDownloadFileSystem& fileSystem)
    {
        std::wstring path(target);
        const size_t separator = path.rfind(L'\\');
        if (separator == std::wstring::npos) return nullptr;
        const std::wstring pattern = path.substr(0, separator + 1) + L".salftp-*.part.meta";
        WIN32_FIND_DATAW data;
        const HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) return nullptr;
        std::shared_ptr<CFtpTransactionalDownload> recovered;
        do
        {
            if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) continue;
            auto candidate = std::make_shared<CFtpTransactionalDownload>();
            const std::wstring sidecar = path.substr(0, separator + 1) + data.cFileName;
            if (candidate->Load(sidecar, path, identity) && candidate->BeginWriting(fileSystem, FALSE))
            { recovered = candidate; break; }
        } while (FindNextFileW(find, &data));
        FindClose(find);
        return recovered;
    }

private:
    CConditionalFilePublication Publication;
    HANDLE Metadata = INVALID_HANDLE_VALUE;
    std::wstring Target, Stage, Sidecar;
    std::string Identity;
    CRecoveryObjectEvidence ParentEvidence, TargetEvidence;
    CFtpDownloadFileSystem* ActiveFileSystem = NULL;
    DWORD WriteError = ERROR_SUCCESS;
    BOOL Published = FALSE, Terminal = FALSE;
    static BOOL Fail(DWORD error) { SetLastError(error); return FALSE; }
    BOOL Append(CFtpDownloadFileSystem& fileSystem, const std::string& line)
    {
        if (WriteError != ERROR_SUCCESS) return Fail(WriteError);
        LARGE_INTEGER zero = {};
        if (Metadata == INVALID_HANDLE_VALUE || !SetFilePointerEx(Metadata, zero, NULL, FILE_END))
            return Fail(WriteError = GetLastError());
        size_t offset = 0;
        while (offset < line.size())
        {
            DWORD written = 0;
            if (!fileSystem.WriteFile(Metadata, line.data() + offset, (DWORD)(line.size() - offset), &written, NULL))
                return Fail(WriteError = GetLastError());
            if (written == 0 || written > line.size() - offset) return Fail(WriteError = ERROR_WRITE_FAULT);
            offset += written;
        }
        if (!fileSystem.FlushFileBuffers(Metadata)) return Fail(WriteError = GetLastError());
        return TRUE;
    }
    static BOOL RecordPublication(void* context, const char* state, const WCHAR* backup)
    {
        auto* owner = static_cast<CFtpTransactionalDownload*>(context);
        return owner->Append(*owner->ActiveFileSystem, "PUBLICATION|" + std::string(state) + "|" + FtpMetadataPath(backup) + "\n");
    }
    BOOL Load(const std::wstring& sidecar, const std::wstring& target, const std::string& identity)
    {
        Metadata = CreateFileW(sidecar.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, NULL,
                               OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, NULL);
        if (Metadata == INVALID_HANDLE_VALUE) return FALSE;
        CRecoveryObjectEvidence journalIdentity;
        if (!ReadRecoveryObjectIdentity(Metadata, journalIdentity) || !journalIdentity.PlainFile) return FALSE;
        CRecoveryLineReader reader(Metadata);
        std::string line;
        if (reader.Next(line) != CRecoveryLineReader::Line || line != "FTPSTAGE|1" ||
            reader.Next(line) != CRecoveryLineReader::Line || line != "TARGET|" + FtpMetadataPath(target) ||
            reader.Next(line) != CRecoveryLineReader::Line || line.compare(0, 7, "REMOTE|") != 0)
            return Fail(ERROR_INVALID_DATA);
        std::string encodedIdentity = line.substr(7);
        if (reader.Next(line) != CRecoveryLineReader::Line || line.compare(0, 7, "PARENT|") != 0 ||
            !ParseRecoveryEvidence(line.substr(7), ParentEvidence) || !ParentEvidence.Present ||
            reader.Next(line) != CRecoveryLineReader::Line || line.compare(0, 9, "ORIGINAL|") != 0 ||
            !ParseRecoveryEvidence(line.substr(9), TargetEvidence)) return Fail(ERROR_INVALID_DATA);
        CRecoveryObjectEvidence checkpoint;
        BOOL resumable = FALSE;
        for (;;)
        {
            const auto result = reader.Next(line);
            if (result == CRecoveryLineReader::Failed) return FALSE;
            if (result == CRecoveryLineReader::End) break;
            if (line == "WRITING") resumable = FALSE;
            else if (line.compare(0, 7, "REMOTE|") == 0)
            {
                // Only a later verified checkpoint can authorize the new identity.
                encodedIdentity = line.substr(7);
                resumable = FALSE;
            }
            else if (line.compare(0, 11, "CHECKPOINT|") == 0)
                resumable = ParseRecoveryEvidence(line.substr(11), checkpoint) && checkpoint.Present && checkpoint.DigestValid;
            else return Fail(ERROR_INVALID_DATA); // published/ambiguous/unknown records never authorize resume
        }
        if (!resumable || encodedIdentity != FtpMetadataHex(identity)) return Fail(ERROR_INVALID_DATA);
        Target = target; Sidecar = sidecar; Stage = sidecar.substr(0, sidecar.size() - 5); Identity = identity;
        if (!Publication.Open(Target.c_str(), Stage.c_str(), TargetEvidence.Present)) return FALSE;
        CRecoveryObjectEvidence parent;
        if (!ReadRecoveryObjectIdentity(Publication.DirectoryHandle(), parent) || !SameRecoveryObject(ParentEvidence, parent) ||
            !VerifyRecoveryEvidence(File(), checkpoint) || !RecoveryHasOnlyPrimaryStream(File())) return Fail(ERROR_INVALID_DATA);
        if (TargetEvidence.Present)
        {
            if (!VerifyRecoveryEvidence(Publication.TargetHandle(), TargetEvidence) ||
                !RecoveryHasOnlyPrimaryStream(Publication.TargetHandle())) return Fail(ERROR_INVALID_DATA);
        }
        else
        {
            const HANDLE unexpected = OpenRelativePublicationFile(Publication.DirectoryHandle(),
                Target.substr(Target.rfind(L'\\') + 1).c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
            if (unexpected != INVALID_HANDLE_VALUE) { CloseHandle(unexpected); return Fail(ERROR_FILE_EXISTS); }
            if (GetLastError() != ERROR_FILE_NOT_FOUND) return FALSE;
        }
        return TRUE;
    }
};
