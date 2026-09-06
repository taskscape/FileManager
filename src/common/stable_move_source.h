// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include "../operation_execution_filesystem.h"

// A move owns one source object until its verified destination is committed.
// Readers may be reopened for retries, but releasing one never admits a writer
// or permits deletion to switch to another occupant of the source pathname.
class CStableMoveSource
{
public:
    CStableMoveSource() : File(INVALID_HANDLE_VALUE), DataFile(INVALID_HANDLE_VALUE) {}
    ~CStableMoveSource() { Close(); }

    BOOL Open(const WCHAR* path)
    {
        Close();
        DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return FALSE;
        DWORD access = GENERIC_READ | DELETE;
        if (attributes & FILE_ATTRIBUTE_READONLY)
            access |= FILE_WRITE_ATTRIBUTES;
        File = CreateFileW(path, access, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (File == INVALID_HANDLE_VALUE)
            return FALSE;
        BY_HANDLE_FILE_INFORMATION information;
        if (!GetFileInformationByHandle(File, &information))
        {
            Close();
            return FALSE;
        }
        if (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            // Keep both the link to delete and the followed content immutable;
            // reopening readers through the data handle cannot follow a new link.
            DataFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                                   FILE_FLAG_SEQUENTIAL_SCAN, NULL);
            if (DataFile == INVALID_HANDLE_VALUE)
            {
                Close();
                return FALSE;
            }
        }
        return TRUE;
    }

    HANDLE Get() const { return File; }

    HANDLE OpenReader(DWORD flags) const
    {
        // Share the owner's DELETE access without weakening its denial of writes
        // and renames. ReOpenFile retains identity even if an ancestor was renamed.
        return ReOpenFile(DataFile != INVALID_HANDLE_VALUE ? DataFile : File, GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_DELETE, flags);
    }

    BOOL Delete(COperationExecutionFileSystem& fileSystem)
    {
        FILE_BASIC_INFO original;
        if (!GetFileInformationByHandleEx(File, FileBasicInfo, &original, sizeof(original)))
            return FALSE;
        const BOOL readOnly = (original.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
        if (readOnly)
        {
            FILE_BASIC_INFO writable = {};
            writable.FileAttributes = original.FileAttributes & ~FILE_ATTRIBUTE_READONLY;
            if (writable.FileAttributes == 0)
                writable.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            if (!fileSystem.SetFileInformationByHandle(File, FileBasicInfo, &writable, sizeof(writable)))
                return FALSE;
        }
        FILE_DISPOSITION_INFO disposition = {TRUE};
        if (!fileSystem.SetFileInformationByHandle(File, FileDispositionInfo, &disposition, sizeof(disposition)))
        {
            const DWORD error = GetLastError();
            if (readOnly)
            {
                // Failed deletion must not leave the retained source writable.
                FILE_BASIC_INFO restore = {};
                restore.FileAttributes = original.FileAttributes;
                if (!SetFileInformationByHandle(File, FileBasicInfo, &restore, sizeof(restore)))
                    return FALSE;
            }
            SetLastError(error);
            return FALSE;
        }
        HANDLE deleted = File;
        File = INVALID_HANDLE_VALUE;
        return CloseHandle(deleted);
    }

    void Close()
    {
        const DWORD error = GetLastError();
        if (DataFile != INVALID_HANDLE_VALUE)
            CloseHandle(DataFile);
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
        DataFile = File = INVALID_HANDLE_VALUE;
        SetLastError(error);
    }

private:
    CStableMoveSource(const CStableMoveSource&);
    CStableMoveSource& operator=(const CStableMoveSource&);
    HANDLE File;
    HANDLE DataFile;
};
