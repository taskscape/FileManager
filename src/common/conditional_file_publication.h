// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <aclapi.h>
#include <string>
#include <vector>
#include "../operation_execution_filesystem.h"

#include "relative_file_operations.h"

struct CPublicationOutcome
{
    DWORD Error;
    DWORD CleanupError;
    BOOL Committed;
    BOOL BackupRetained;
    CPublicationOutcome() : Error(ERROR_SUCCESS), CleanupError(ERROR_SUCCESS), Committed(FALSE), BackupRetained(FALSE) {}
};

// A durable callback records intent before removing the old destination name,
// and facts afterwards. It must fail closed when the journal cannot be flushed.
typedef BOOL (*CPublicationRecorder)(void* context, const char* state, const WCHAR* backup);

// Retained file and directory handles protect the approved object through the
// destructive boundary. Every rename refuses an unexpected destination entry.
class CConditionalFilePublication
{
public:
    CConditionalFilePublication() : Directory(INVALID_HANDLE_VALUE), Target(INVALID_HANDLE_VALUE), Temporary(INVALID_HANDLE_VALUE) {}
    ~CConditionalFilePublication() { Close(); }

    BOOL Open(const WCHAR* target, const WCHAR* temporary, BOOL expectedPresent, BOOL preserveTargetSecurity = TRUE,
              BOOL createNewTemporary = FALSE)
    {
        Close();
        PreserveTargetSecurity = expectedPresent && preserveTargetSecurity;
        if (!FullPath(target, TargetPath) || !FullPath(temporary, TemporaryPath)) return FALSE;
        const size_t parentEnd = TargetPath.rfind(L'\\');
        if (parentEnd == std::wstring::npos || TemporaryPath.rfind(L'\\') != parentEnd ||
            _wcsnicmp(TargetPath.c_str(), TemporaryPath.c_str(), parentEnd + 1) != 0 ||
            _wcsicmp(TargetPath.c_str(), TemporaryPath.c_str()) == 0)
        { SetLastError(ERROR_INVALID_NAME); return FALSE; }
        const std::wstring parentPath = TargetPath.substr(0, parentEnd + 1);
        Directory = CreateFileW(parentPath.c_str(), FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS, NULL);
        if (Directory == INVALID_HANDLE_VALUE) return FALSE;
        // FAT/exFAT have no persistent ACL to preserve. On network filesystems
        // that cannot answer this query, the security operation itself decides.
        DWORD fileSystemFlags = 0;
        if (PreserveTargetSecurity && GetVolumeInformationByHandleW(Directory, NULL, 0, NULL, NULL,
                                                                     &fileSystemFlags, NULL, 0) &&
            (fileSystemFlags & FILE_PERSISTENT_ACLS) == 0)
            PreserveTargetSecurity = FALSE;
        TargetLeaf = TargetPath.substr(parentEnd + 1);
        TemporaryLeaf = TemporaryPath.substr(parentEnd + 1);
        BackupLeaf = TemporaryLeaf + L".previous";
        // Journal the physical directory, so a retargeted junction cannot make
        // an intentionally retained backup disappear from recovery's location.
        const DWORD directoryLength = GetFinalPathNameByHandleW(Directory, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (directoryLength == 0) return FALSE;
        std::vector<WCHAR> directoryPath(directoryLength + 1);
        const DWORD directoryCopied = GetFinalPathNameByHandleW(Directory, directoryPath.data(),
                                                                 (DWORD)directoryPath.size(), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (directoryCopied == 0 || directoryCopied >= directoryPath.size()) return FALSE;
        BackupPath.assign(directoryPath.data(), directoryCopied);
        if (BackupPath.back() != L'\\') BackupPath += L'\\';
        BackupPath += BackupLeaf;
        // FTP creates its unique stage relative to the same retained parent
        // used for publication, excluding pathname/reparse redirection.
        Temporary = createNewTemporary ? OpenRelativePublicationFile(Directory, TemporaryLeaf.c_str(),
            GENERIC_READ | GENERIC_WRITE | DELETE | (PreserveTargetSecurity ? WRITE_DAC : 0),
            FILE_SHARE_READ, TRUE) : OpenWritableTemporary();
        if (Temporary == INVALID_HANDLE_VALUE) return FALSE;
        if (expectedPresent)
        {
            Target = OpenRelativePublicationFile(Directory, TargetLeaf.c_str());
            if (Target == INVALID_HANDLE_VALUE) return FALSE;
        }
        // An absent destination is enforced atomically by the non-replacing
        // publication, so an attribute check cannot authorize an overwrite.
        return TRUE;
    }

    HANDLE TargetHandle() const { return Target; }
    HANDLE TemporaryHandle() const { return Temporary; }
    HANDLE DirectoryHandle() const { return Directory; }
    const WCHAR* BackupName() const { return BackupPath.c_str(); }

    // Recovery may discard only the verified object it still owns by handle.
    BOOL DiscardTemporary(COperationExecutionFileSystem& fileSystem)
    {
        return DeleteOwnedFile(fileSystem, Temporary);
    }

    CPublicationOutcome Commit(COperationExecutionFileSystem& fileSystem,
                               CPublicationRecorder record, void* context)
    {
        CPublicationOutcome outcome;
        if (PreserveTargetSecurity && !CopyTargetSecurity())
        { outcome.Error = GetLastError(); return outcome; }
        if (record == NULL || !record(context, "publication-planned", BackupPath.c_str()))
        { outcome.Error = ERROR_WRITE_FAULT; return outcome; }
        if (Target != INVALID_HANDLE_VALUE)
        {
            if (!fileSystem.RenameFileByHandle(Target, Directory, BackupLeaf.c_str()))
            { outcome.Error = GetLastError(); return outcome; }
            outcome.BackupRetained = TRUE;
            if (!record(context, "destination-backed-up", BackupPath.c_str()))
                outcome.Error = ERROR_WRITE_FAULT;
        }
        if (outcome.Error == ERROR_SUCCESS)
        {
            outcome.Committed = fileSystem.RenameFileByHandle(Temporary, Directory, TargetLeaf.c_str());
            if (!outcome.Committed) outcome.Error = GetLastError();
        }
        if (!outcome.Committed)
        {
            if (outcome.BackupRetained)
            {
                // A failed publication may mean a new occupant exists. Restore
                // only to an empty name; otherwise preserve all three objects.
                if (fileSystem.RenameFileByHandle(Target, Directory, TargetLeaf.c_str()))
                {
                    outcome.BackupRetained = FALSE;
                    if (!record(context, "destination-restored", BackupPath.c_str()))
                        outcome.CleanupError = ERROR_WRITE_FAULT;
                }
                else outcome.CleanupError = GetLastError();
            }
            return outcome;
        }
        // The source of a move must survive a failed post-rename flush. Keeping
        // the backup also leaves a recoverable old destination after that error.
        if (!fileSystem.FlushFileBuffers(Temporary))
        {
            outcome.Error = GetLastError();
            return outcome;
        }
        if (!record(context, "destination-published", BackupPath.c_str()))
        {
            // Publication already happened. Never retry it or discard the
            // backup if its durable completion record could not be written.
            outcome.Error = ERROR_WRITE_FAULT;
            return outcome;
        }
        if (outcome.BackupRetained)
        {
            if (!DeleteBackup(fileSystem)) outcome.CleanupError = GetLastError();
            else outcome.BackupRetained = FALSE;
        }
        if (!outcome.BackupRetained && !record(context, "publication-complete", BackupPath.c_str()))
            outcome.CleanupError = ERROR_WRITE_FAULT;
        return outcome;
    }

    void Close()
    {
        const DWORD error = GetLastError();
        if (Temporary != INVALID_HANDLE_VALUE) CloseHandle(Temporary);
        if (Target != INVALID_HANDLE_VALUE) CloseHandle(Target);
        Target = Temporary = INVALID_HANDLE_VALUE;
        if (Directory != INVALID_HANDLE_VALUE) CloseHandle(Directory);
        Directory = INVALID_HANDLE_VALUE;
        SetLastError(error);
    }

    // Remote deletion requires known local close results. Never retry a failed
    // close using a handle value that might already have been recycled.
    template<class TFileSystem> BOOL CloseChecked(TFileSystem& fileSystem)
    {
        DWORD error = ERROR_SUCCESS;
        for (HANDLE* slot : { &Temporary, &Target, &Directory })
        {
            const HANDLE file = *slot;
            *slot = INVALID_HANDLE_VALUE;
            if (file != INVALID_HANDLE_VALUE && !fileSystem.CloseFile(file) && error == ERROR_SUCCESS)
                error = GetLastError();
        }
        SetLastError(error);
        return error == ERROR_SUCCESS;
    }

private:
    CConditionalFilePublication(const CConditionalFilePublication&);
    CConditionalFilePublication& operator=(const CConditionalFilePublication&);
    HANDLE Directory;
    std::wstring TargetPath, TemporaryPath, BackupPath, TargetLeaf, TemporaryLeaf, BackupLeaf;
    HANDLE Target, Temporary;
    BOOL PreserveTargetSecurity = FALSE;
    static BOOL FullPath(const WCHAR* path, std::wstring& fullPath)
    {
        const DWORD length = GetFullPathNameW(path, 0, NULL, NULL);
        if (length == 0) return FALSE;
        std::vector<WCHAR> buffer(length);
        const DWORD copied = GetFullPathNameW(path, length, buffer.data(), NULL);
        if (copied == 0 || copied >= length) { SetLastError(ERROR_INVALID_NAME); return FALSE; }
        fullPath.assign(buffer.data(), copied);
        return TRUE;
    }
    HANDLE OpenWritableTemporary()
    {
        // Preserve read-only source attributes while acquiring the WRITE access
        // required by FlushFileBuffers. The attribute handle follows its object
        // if a race replaces the pathname during this acquisition-only phase.
        HANDLE attributes = OpenRelativePublicationFile(Directory, TemporaryLeaf.c_str(),
                                                          FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
        if (attributes == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION before = {};
        BOOL ok = GetFileInformationByHandle(attributes, &before);
        const BOOL readOnly = ok && (before.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (readOnly)
        {
            FILE_BASIC_INFO writable = {};
            writable.FileAttributes = before.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY;
            if (writable.FileAttributes == 0) writable.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            ok = SetFileInformationByHandle(attributes, FileBasicInfo, &writable, sizeof(writable));
        }
        HANDLE file = ok ? OpenRelativePublicationFile(Directory, TemporaryLeaf.c_str(),
                                                        GENERIC_READ | GENERIC_WRITE | DELETE |
                                                        (PreserveTargetSecurity ? WRITE_DAC : 0)) : INVALID_HANDLE_VALUE;
        DWORD error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (file != INVALID_HANDLE_VALUE)
        {
            BY_HANDLE_FILE_INFORMATION after;
            if (!GetFileInformationByHandle(file, &after)) error = GetLastError();
            else if (before.dwVolumeSerialNumber != after.dwVolumeSerialNumber ||
                     before.nFileIndexHigh != after.nFileIndexHigh || before.nFileIndexLow != after.nFileIndexLow)
                error = ERROR_INVALID_DATA;
        }
        if (readOnly)
        {
            FILE_BASIC_INFO restore = {};
            restore.FileAttributes = before.dwFileAttributes;
            if (!SetFileInformationByHandle(attributes, FileBasicInfo, &restore, sizeof(restore)) && error == ERROR_SUCCESS)
                error = GetLastError();
        }
        if (!CloseHandle(attributes) && error == ERROR_SUCCESS) error = GetLastError();
        if (error != ERROR_SUCCESS)
        {
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            SetLastError(error);
            return INVALID_HANDLE_VALUE;
        }
        return file;
    }
    BOOL CopyTargetSecurity()
    {
        // Preserve the previous ReplaceFile DACL behavior unless the user chose
        // source-security copying. Default directory inheritance must not widen
        // access to a destination that had an explicitly restricted ACL.
        PACL dacl = NULL;
        PSECURITY_DESCRIPTOR descriptor = NULL;
        DWORD error = GetSecurityInfo(Target, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                      NULL, NULL, &dacl, NULL, &descriptor);
        if (error == ERROR_SUCCESS)
        {
            SECURITY_DESCRIPTOR_CONTROL control;
            DWORD revision;
            if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) error = GetLastError();
            else error = SetSecurityInfo(Temporary, SE_FILE_OBJECT,
                                          DACL_SECURITY_INFORMATION | ((control & SE_DACL_PROTECTED) ?
                                              PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                                          NULL, NULL, dacl, NULL);
        }
        if (descriptor != NULL) LocalFree(descriptor);
        if (error != ERROR_SUCCESS) SetLastError(error);
        return error == ERROR_SUCCESS;
    }
    BOOL DeleteBackup(COperationExecutionFileSystem& fileSystem)
    {
        return DeleteOwnedFile(fileSystem, Target);
    }
    static BOOL DeleteOwnedFile(COperationExecutionFileSystem& fileSystem, HANDLE& file)
    {
        FILE_BASIC_INFO original;
        if (!GetFileInformationByHandleEx(file, FileBasicInfo, &original, sizeof(original))) return FALSE;
        const BOOL readOnly = (original.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (readOnly)
        {
            FILE_BASIC_INFO writable = {};
            writable.FileAttributes = original.FileAttributes & ~FILE_ATTRIBUTE_READONLY;
            if (writable.FileAttributes == 0) writable.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            if (!fileSystem.SetFileInformationByHandle(file, FileBasicInfo, &writable, sizeof(writable))) return FALSE;
        }
        FILE_DISPOSITION_INFO disposition = {TRUE};
        if (!fileSystem.SetFileInformationByHandle(file, FileDispositionInfo, &disposition, sizeof(disposition)))
        {
            const DWORD error = GetLastError();
            FILE_BASIC_INFO restore = {};
            restore.FileAttributes = original.FileAttributes;
            if (readOnly) SetFileInformationByHandle(file, FileBasicInfo, &restore, sizeof(restore));
            SetLastError(error);
            return FALSE;
        }
        HANDLE deleted = file;
        file = INVALID_HANDLE_VALUE;
        return CloseHandle(deleted);
    }
};
