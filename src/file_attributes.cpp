// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "file_attributes.h"

// Compression/encryption attribute helpers extracted from async_copy.cpp as a
// mechanical move; behavior is unchanged.
DWORD CompressFile(char* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if (attrs & FILE_ATTRIBUTE_COMPRESSED)
        return ret; // already compressed

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesUtf8(fileNameCrFile, attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    HANDLE file = HANDLES_Q(CreateFileUtf8(fileNameCrFile, FILE_READ_DATA | FILE_WRITE_DATA,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL));
    if (file == INVALID_HANDLE_VALUE)
        ret = GetLastError();
    else
    {
        USHORT state = COMPRESSION_FORMAT_DEFAULT;
        ULONG length;
        if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &state,
                             sizeof(USHORT), NULL, 0, &length, FALSE))
            ret = GetLastError();
        HANDLES(CloseHandle(file));
    }
    if (attrsChange)
        SetFileAttributesUtf8(fileNameCrFile, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}

DWORD UncompressFile(char* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        return ret; // not compressed

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~FILE_ATTRIBUTE_READONLY);
    }

    HANDLE file = HANDLES_Q(CreateFileW(fileNameW, FILE_READ_DATA | FILE_WRITE_DATA,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                                        NULL));
    if (file == INVALID_HANDLE_VALUE)
        ret = GetLastError();
    else
    {
        USHORT state = COMPRESSION_FORMAT_NONE;
        ULONG length;
        if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &state,
                             sizeof(USHORT), NULL, 0, &length, FALSE))
            ret = GetLastError();
        HANDLES(CloseHandle(file));
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}

DWORD MyEncryptFile(HWND hProgressDlg, char* fileName, DWORD attrs, DWORD finalAttrs,
                    CProgressDlgData& dlgData, BOOL& cancelOper, BOOL preserveDate)
{
    DWORD retEnc = ERROR_SUCCESS;
    cancelOper = FALSE;
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED)
        return retEnc; // already encrypted

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    // if the file has the SYSTEM attribute, the EncryptFile API function reports "access denied"; handle it:
    if ((attrs & FILE_ATTRIBUTE_SYSTEM) && (finalAttrs & FILE_ATTRIBUTE_SYSTEM))
    { // if it has and will keep the SYSTEM attribute, ask the user whether they really mean it
        if (!dlgData.EncryptSystemAll)
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return retEnc;

            if (dlgData.SkipAllEncryptSystem)
                return retEnc;

            int ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_CONFIRMSFILEENCRYPT);
            data[2] = fileName;
            data[3] = LoadStr(IDS_ENCRYPTSFILE);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
            switch (ret)
            {
            case IDB_ALL:
                dlgData.EncryptSystemAll = TRUE;
            case IDYES:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllEncryptSystem = TRUE;
            case IDB_SKIP:
                return retEnc;

            case IDCANCEL:
            {
                cancelOper = TRUE;
                return retEnc;
            }
            }
        }
    }

    BOOL attrsChange = FALSE;
    if (attrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY))
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY));
    }
    if (preserveDate)
    {
        HANDLE file;
        file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING,
                                     (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                     NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME ftCreated, /*ftAccessed,*/ ftModified;
            GetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
            HANDLES(CloseHandle(file));

            if (!EncryptFileW(fileNameW))
                retEnc = GetLastError();

            file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING,
                                         (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                         NULL));
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
                HANDLES(CloseHandle(file));
            }
        }
        else
            retEnc = GetLastError();
    }
    else
    {
        if (!EncryptFileW(fileNameW))
            retEnc = GetLastError();
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return retEnc;
}

DWORD MyDecryptFile(char* fileName, DWORD attrs, BOOL preserveDate)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        return ret; // not encrypted

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    if (preserveDate)
    {
        HANDLE file;
        file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING,
                                     (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                     NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME ftCreated, /*ftAccessed,*/ ftModified;
            GetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
            HANDLES(CloseHandle(file));

            if (!DecryptFileW(fileNameW, 0))
                ret = GetLastError();

            file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING,
                                         (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                         NULL));
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
                HANDLES(CloseHandle(file));
            }
        }
        else
            ret = GetLastError();
    }
    else
    {
        if (!DecryptFileW(fileNameW, 0))
            ret = GetLastError();
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}
