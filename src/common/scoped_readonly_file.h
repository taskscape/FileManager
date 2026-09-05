// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// ReplaceFile rejects read-only inputs. Keep an attribute handle so restoration
// follows the same file after a rename and cannot modify a new occupant of its path.
class CScopedReadOnlyFile
{
public:
    CScopedReadOnlyFile() : File(INVALID_HANDLE_VALUE), Changed(FALSE) {}
    ~CScopedReadOnlyFile()
    {
        const DWORD error = GetLastError();
        Restore();
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
        SetLastError(error);
    }

    BOOL MakeWritable(const WCHAR* path, BOOL allowMissing = FALSE)
    {
        DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return allowMissing && GetLastError() == ERROR_FILE_NOT_FOUND;
        if ((attributes & FILE_ATTRIBUTE_READONLY) == 0)
            return TRUE;

        File = CreateFileW(path, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (File == INVALID_HANDLE_VALUE)
            return FALSE;
        FILE_BASIC_INFO information;
        if (!GetFileInformationByHandleEx(File, FileBasicInfo, &information, sizeof(information)))
            return FALSE;
        if ((information.FileAttributes & FILE_ATTRIBUTE_READONLY) == 0)
            return TRUE;

        // Zero timestamps leave file times unchanged while only read-only is cleared.
        FILE_BASIC_INFO update = {};
        update.FileAttributes = information.FileAttributes & ~FILE_ATTRIBUTE_READONLY;
        if (update.FileAttributes == 0)
            update.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        Changed = SetFileInformationByHandle(File, FileBasicInfo, &update, sizeof(update));
        return Changed;
    }

    BOOL Restore()
    {
        if (!Changed)
            return TRUE;
        FILE_BASIC_INFO information;
        if (!GetFileInformationByHandleEx(File, FileBasicInfo, &information, sizeof(information)))
            return FALSE;
        FILE_BASIC_INFO update = {};
        update.FileAttributes = (information.FileAttributes & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_READONLY;
        if (!SetFileInformationByHandle(File, FileBasicInfo, &update, sizeof(update)))
            return FALSE;
        Changed = FALSE;
        return TRUE;
    }

    // A successful replacement deleted the old destination; only its successor
    // should have attributes restored, using the replacement's own handle.
    void Dismiss() { Changed = FALSE; }

private:
    CScopedReadOnlyFile(const CScopedReadOnlyFile&);
    CScopedReadOnlyFile& operator=(const CScopedReadOnlyFile&);
    HANDLE File;
    BOOL Changed;
};
