// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef _WIN32

#include <windows.h>
#include <stdlib.h>

static WCHAR* WinScpUtf8AllocWide(const char* src)
{
    if (src == NULL)
        return NULL;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
    if (len <= 0)
        return NULL;
    WCHAR* buf = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (buf == NULL)
        return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, buf, len) == 0)
    {
        free(buf);
        return NULL;
    }
    return buf;
}

static BOOL WinScpWideToUtf8Buffer(const WCHAR* src, char* dst, int dstSize)
{
    if (dst == NULL || dstSize <= 0)
        return FALSE;
    if (src == NULL)
    {
        dst[0] = 0;
        return TRUE;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstSize, NULL, NULL);
    if (len == 0)
        dst[0] = 0;
    return len != 0;
}

static BOOL WinScpConvertFindDataWToUtf8Local(const WIN32_FIND_DATAW* src, WIN32_FIND_DATAA* dst)
{
    if (dst == NULL || src == NULL)
        return FALSE;
    ZeroMemory(dst, sizeof(*dst));
    dst->dwFileAttributes = src->dwFileAttributes;
    dst->ftCreationTime = src->ftCreationTime;
    dst->ftLastAccessTime = src->ftLastAccessTime;
    dst->ftLastWriteTime = src->ftLastWriteTime;
    dst->nFileSizeHigh = src->nFileSizeHigh;
    dst->nFileSizeLow = src->nFileSizeLow;
    dst->dwReserved0 = src->dwReserved0;
    dst->dwReserved1 = src->dwReserved1;
    WinScpWideToUtf8Buffer(src->cFileName, dst->cFileName, (int)sizeof(dst->cFileName));
    WinScpWideToUtf8Buffer(src->cAlternateFileName, dst->cAlternateFileName, (int)sizeof(dst->cAlternateFileName));
    return TRUE;
}

static HANDLE WinScpCreateFileUtf8(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                   LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                                   DWORD flagsAndAttributes, HANDLE templateFile)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = WinScpUtf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    h = CreateFileW(fileNameW, desiredAccess, shareMode, securityAttributes,
                    creationDisposition, flagsAndAttributes, templateFile);
    free(fileNameW);
    return h;
}

static BOOL WinScpDeleteFileUtf8(const char* fileName)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = WinScpUtf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = DeleteFileW(fileNameW);
    free(fileNameW);
    return ok;
}

static BOOL WinScpMoveFileUtf8(const char* srcName, const char* destName)
{
    BOOL ok = FALSE;
    WCHAR* srcNameW = WinScpUtf8AllocWide(srcName);
    WCHAR* destNameW = WinScpUtf8AllocWide(destName);
    if (srcNameW == NULL || destNameW == NULL)
    {
        free(srcNameW);
        free(destNameW);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = MoveFileW(srcNameW, destNameW);
    free(srcNameW);
    free(destNameW);
    return ok;
}

static BOOL WinScpCopyFileUtf8(const char* existingFileName, const char* newFileName, BOOL failIfExists)
{
    BOOL ok = FALSE;
    WCHAR* existingW = WinScpUtf8AllocWide(existingFileName);
    WCHAR* newW = WinScpUtf8AllocWide(newFileName);
    if (existingW == NULL || newW == NULL)
    {
        free(existingW);
        free(newW);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = CopyFileW(existingW, newW, failIfExists);
    free(existingW);
    free(newW);
    return ok;
}

static DWORD WinScpGetFileAttributesUtf8(const char* fileName)
{
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    WCHAR* fileNameW = WinScpUtf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return attrs;
    }
    attrs = GetFileAttributesW(fileNameW);
    free(fileNameW);
    return attrs;
}

static BOOL WinScpSetFileAttributesUtf8(const char* fileName, DWORD attrs)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = WinScpUtf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = SetFileAttributesW(fileNameW, attrs);
    free(fileNameW);
    return ok;
}

static BOOL WinScpCreateDirectoryUtf8(const char* dirName, LPSECURITY_ATTRIBUTES securityAttributes)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = WinScpUtf8AllocWide(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = CreateDirectoryW(dirNameW, securityAttributes);
    free(dirNameW);
    return ok;
}

static BOOL WinScpRemoveDirectoryUtf8(const char* dirName)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = WinScpUtf8AllocWide(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = RemoveDirectoryW(dirNameW);
    free(dirNameW);
    return ok;
}

static HANDLE WinScpFindFirstFileUtf8(const char* fileName, WIN32_FIND_DATAA* findFileData)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = WinScpUtf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    WIN32_FIND_DATAW dataW;
    h = FindFirstFileW(fileNameW, &dataW);
    free(fileNameW);
    if (h == INVALID_HANDLE_VALUE)
        return h;
    if (findFileData != NULL)
        WinScpConvertFindDataWToUtf8Local(&dataW, findFileData);
    return h;
}

static BOOL WinScpFindNextFileUtf8(HANDLE hFindFile, WIN32_FIND_DATAA* findFileData)
{
    WIN32_FIND_DATAW dataW;
    if (!FindNextFileW(hFindFile, &dataW))
        return FALSE;
    if (findFileData != NULL)
        WinScpConvertFindDataWToUtf8Local(&dataW, findFileData);
    return TRUE;
}

#endif // _WIN32
