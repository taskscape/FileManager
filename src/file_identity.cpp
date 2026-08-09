// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common/scoped_kernel_handle.h"
#include "file_operation_filesystem.h"
#include "worker.h"

namespace
{
const DWORD FileIdentityNotCaptured = 0;
const DWORD FileIdentityAbsent = 1;
const DWORD FileIdentityPresent = 2;

unsigned __int64 HashFinalPath(HANDLE handle, DWORD* error)
{
    DWORD length = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0)
    {
        *error = GetLastError();
        return 0;
    }

    WCHAR* path = (WCHAR*)malloc((length + 1) * sizeof(WCHAR));
    if (path == NULL)
    {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    DWORD copied = GetFinalPathNameByHandleW(handle, path, length + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (copied == 0 || copied > length)
    {
        *error = GetLastError();
        free(path);
        return 0;
    }

    // FNV-1a gives a compact, case-preserving record of the path which the
    // handle actually opened.  The file ID remains the primary identity.
    unsigned __int64 hash = 1469598103934665603ULL;
    DWORD i;
    for (i = 0; i < copied; i++)
    {
        hash ^= (unsigned __int64)path[i];
        hash *= 1099511628211ULL;
    }
    free(path);
    return hash;
}

BOOL ReadFileIdentity(HANDLE handle, COperation::CFileIdentity* identity, DWORD* error)
{
    BY_HANDLE_FILE_INFORMATION information;
    if (!GetFileInformationByHandle(handle, &information))
    {
        *error = GetLastError();
        return FALSE;
    }

    unsigned __int64 finalPathHash = HashFinalPath(handle, error);
    if (finalPathHash == 0)
        return FALSE;

    identity->State = FileIdentityPresent;
    identity->VolumeSerialNumber = information.dwVolumeSerialNumber;
    identity->FileIndexHigh = information.nFileIndexHigh;
    identity->FileIndexLow = information.nFileIndexLow;
    identity->FinalPathHash = finalPathHash;
    return TRUE;
}

BOOL CaptureFileIdentity(const char* path, COperation::CFileIdentity* identity, BOOL allowAbsent, DWORD* error)
{
    memset(identity, 0, sizeof(*identity));
    identity->State = FileIdentityNotCaptured;
    // Identity capture often exits through validation failures, so keep its
    // tracked file handle owned until the complete record has been produced.
    CScopedKernelHandle handle(HANDLES_Q(CreateFileUtf8(path, FILE_READ_ATTRIBUTES,
                                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                         NULL, OPEN_EXISTING,
                                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                                         NULL)));
    if (!handle.IsValid())
    {
        *error = GetLastError();
        if (allowAbsent && (*error == ERROR_FILE_NOT_FOUND || *error == ERROR_PATH_NOT_FOUND))
        {
            identity->State = FileIdentityAbsent;
            *error = ERROR_SUCCESS;
            return TRUE;
        }
        return FALSE;
    }

    BOOL result = ReadFileIdentity(handle.Get(), identity, error);
    DWORD closeError;
    if (!handle.Close(&closeError) && result)
    {
        *error = closeError;
        result = FALSE;
    }
    return result;
}

BOOL SameFileIdentity(const COperation::CFileIdentity& left, const COperation::CFileIdentity& right)
{
    return left.State == right.State &&
           (left.State == FileIdentityAbsent ||
            (left.State == FileIdentityPresent &&
             left.VolumeSerialNumber == right.VolumeSerialNumber &&
             left.FileIndexHigh == right.FileIndexHigh &&
             left.FileIndexLow == right.FileIndexLow &&
             left.FinalPathHash == right.FinalPathHash));
}

BOOL OpenVerifiedFileForDelete(const char* path, const COperation::CFileIdentity& expected,
                               CScopedKernelHandle* handle, DWORD* error)
{
    handle->Reset();
    if (expected.State != FileIdentityPresent)
    {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }

    // The output wrapper makes the verified handle's ownership transfer clear
    // until deletion has either committed or returned a recoverable error.
    handle->Reset(HANDLES_Q(CreateFileUtf8(path, DELETE | FILE_READ_ATTRIBUTES,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                           NULL, OPEN_EXISTING,
                                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                           NULL)));
    if (!handle->IsValid())
    {
        *error = GetLastError();
        return FALSE;
    }

    COperation::CFileIdentity actual;
    if (!ReadFileIdentity(handle->Get(), &actual, error) || !SameFileIdentity(expected, actual))
    {
        if (*error == ERROR_SUCCESS)
            *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    return TRUE;
}
} // namespace

BOOL CaptureOperationFileIdentities(COperation* operation, DWORD* error)
{
    memset(&operation->SourceIdentity, 0, sizeof(operation->SourceIdentity));
    memset(&operation->TargetIdentity, 0, sizeof(operation->TargetIdentity));

    BOOL needsSourceIdentity = operation->Opcode == ocMoveFile || operation->Opcode == ocMoveDir ||
                               operation->Opcode == ocDeleteFile || operation->Opcode == ocDeleteDir ||
                               operation->Opcode == ocDeleteDirLink;
    BOOL needsTargetIdentity = operation->Opcode == ocCopyFile || operation->Opcode == ocMoveFile ||
                               operation->Opcode == ocMoveDir;
    if (needsSourceIdentity && !CaptureFileIdentity(operation->SourceName, &operation->SourceIdentity, FALSE, error))
        return FALSE;
    if (needsTargetIdentity && !CaptureFileIdentity(operation->TargetName, &operation->TargetIdentity, TRUE, error))
        return FALSE;
    *error = ERROR_SUCCESS;
    return TRUE;
}

BOOL VerifyFileIdentity(const char* path, const COperation::CFileIdentity& expected, DWORD* error)
{
    if (expected.State == FileIdentityNotCaptured)
    {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    COperation::CFileIdentity actual;
    if (!CaptureFileIdentity(path, &actual, expected.State == FileIdentityAbsent, error))
        return FALSE;
    if (!SameFileIdentity(expected, actual))
    {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    return TRUE;
}

BOOL VerifyFileHandleIdentity(HANDLE handle, const COperation::CFileIdentity& expected, DWORD* error)
{
    if (expected.State != FileIdentityPresent)
    {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    COperation::CFileIdentity actual;
    if (!ReadFileIdentity(handle, &actual, error))
        return FALSE;
    if (!SameFileIdentity(expected, actual))
    {
        *error = ERROR_INVALID_DATA;
        return FALSE;
    }
    *error = ERROR_SUCCESS;
    return TRUE;
}

BOOL DeleteFileWithVerifiedIdentity(const char* path, const COperation::CFileIdentity& expected, DWORD* error)
{
    CScopedKernelHandle handle;
    if (!OpenVerifiedFileForDelete(path, expected, &handle, error))
        return FALSE;

    FILE_DISPOSITION_INFO disposition;
    disposition.DeleteFile = TRUE;
    BOOL result = OperationExecutionFileSystem().SetFileInformationByHandle(handle.Get(), FileDispositionInfo,
                                                                             &disposition, sizeof(disposition));
    if (!result && GetLastError() == ERROR_ACCESS_DENIED)
    {
        FILE_BASIC_INFO basicInfo;
        if (GetFileInformationByHandleEx(handle.Get(), FileBasicInfo, &basicInfo, sizeof(basicInfo)))
        {
            basicInfo.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
            if (OperationExecutionFileSystem().SetFileInformationByHandle(handle.Get(), FileBasicInfo,
                                                                           &basicInfo, sizeof(basicInfo)))
                result = OperationExecutionFileSystem().SetFileInformationByHandle(handle.Get(), FileDispositionInfo,
                                                                                     &disposition, sizeof(disposition));
        }
    }
    if (!result)
        *error = GetLastError();
    DWORD closeError;
    if (!handle.Close(&closeError) && result)
    {
        *error = closeError;
        result = FALSE;
    }
    return result;
}
