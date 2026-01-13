// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define WIN32_LEAN_AND_MEAN // exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <CommDlg.h>
#include <crtdbg.h>
#include <ostream>
#include <tchar.h>
#include <stdio.h>
#include <commctrl.h>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_file.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_vers.h"
#include "dbg.h"

#include "array2.h"

#include "unrar.h"
#include "dialogs.h"

#include "unrar.rh"
#include "unrar.rh2"
#include "lang\lang.rh"
#include <stdlib.h>

#ifndef SALAMANDER_UTF8_WINAPI
#define SALAMANDER_UTF8_WINAPI

static WCHAR* Utf8AllocWide(const char* src)
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

static BOOL WideToUtf8Buffer(const WCHAR* src, char* dst, int dstSize)
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

static BOOL ConvertFindDataWToUtf8Local(const WIN32_FIND_DATAW* src, WIN32_FIND_DATAA* dst)
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
    WideToUtf8Buffer(src->cFileName, dst->cFileName, (int)sizeof(dst->cFileName));
    WideToUtf8Buffer(src->cAlternateFileName, dst->cAlternateFileName, (int)sizeof(dst->cAlternateFileName));
    return TRUE;
}

static HANDLE CreateFileUtf8Local(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                 LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                                 DWORD flagsAndAttributes, HANDLE templateFile)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = Utf8AllocWide(fileName);
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

static BOOL DeleteFileUtf8Local(const char* fileName)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = Utf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = DeleteFileW(fileNameW);
    free(fileNameW);
    return ok;
}

static BOOL MoveFileUtf8Local(const char* srcName, const char* destName)
{
    BOOL ok = FALSE;
    WCHAR* srcNameW = Utf8AllocWide(srcName);
    WCHAR* destNameW = Utf8AllocWide(destName);
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

static BOOL CopyFileUtf8Local(const char* existingFileName, const char* newFileName, BOOL failIfExists)
{
    BOOL ok = FALSE;
    WCHAR* existingW = Utf8AllocWide(existingFileName);
    WCHAR* newW = Utf8AllocWide(newFileName);
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

static DWORD GetFileAttributesUtf8Local(const char* fileName)
{
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    WCHAR* fileNameW = Utf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return attrs;
    }
    attrs = GetFileAttributesW(fileNameW);
    free(fileNameW);
    return attrs;
}

static BOOL SetFileAttributesUtf8Local(const char* fileName, DWORD attrs)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = Utf8AllocWide(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = SetFileAttributesW(fileNameW, attrs);
    free(fileNameW);
    return ok;
}

static BOOL CreateDirectoryUtf8Local(const char* dirName, LPSECURITY_ATTRIBUTES securityAttributes)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = Utf8AllocWide(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = CreateDirectoryW(dirNameW, securityAttributes);
    free(dirNameW);
    return ok;
}

static BOOL RemoveDirectoryUtf8Local(const char* dirName)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = Utf8AllocWide(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = RemoveDirectoryW(dirNameW);
    free(dirNameW);
    return ok;
}

static HANDLE FindFirstFileUtf8Local(const char* fileName, WIN32_FIND_DATAA* findFileData)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = Utf8AllocWide(fileName);
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
        ConvertFindDataWToUtf8Local(&dataW, findFileData);
    return h;
}

static BOOL FindNextFileUtf8Local(HANDLE hFindFile, WIN32_FIND_DATAA* findFileData)
{
    WIN32_FIND_DATAW dataW;
    if (!FindNextFileW(hFindFile, &dataW))
        return FALSE;
    if (findFileData != NULL)
        ConvertFindDataWToUtf8Local(&dataW, findFileData);
    return TRUE;
}
#ifndef SALAMANDER_WINAPI_MACROS_GUARD
#define SALAMANDER_WINAPI_MACROS_GUARD
#ifdef CreateFile
#pragma push_macro("CreateFile")
#undef CreateFile
#define SALAMANDER_POP_CREATEFILE
#endif
#ifdef DeleteFile
#pragma push_macro("DeleteFile")
#undef DeleteFile
#define SALAMANDER_POP_DELETEFILE
#endif
#ifdef MoveFile
#pragma push_macro("MoveFile")
#undef MoveFile
#define SALAMANDER_POP_MOVEFILE
#endif
#ifdef CopyFile
#pragma push_macro("CopyFile")
#undef CopyFile
#define SALAMANDER_POP_COPYFILE
#endif
#ifdef GetFileAttributes
#pragma push_macro("GetFileAttributes")
#undef GetFileAttributes
#define SALAMANDER_POP_GETFILEATTRIBUTES
#endif
#ifdef SetFileAttributes
#pragma push_macro("SetFileAttributes")
#undef SetFileAttributes
#define SALAMANDER_POP_SETFILEATTRIBUTES
#endif
#ifdef CreateDirectory
#pragma push_macro("CreateDirectory")
#undef CreateDirectory
#define SALAMANDER_POP_CREATEDIRECTORY
#endif
#ifdef RemoveDirectory
#pragma push_macro("RemoveDirectory")
#undef RemoveDirectory
#define SALAMANDER_POP_REMOVEDIRECTORY
#endif
#ifdef FindFirstFile
#pragma push_macro("FindFirstFile")
#undef FindFirstFile
#define SALAMANDER_POP_FINDFIRSTFILE
#endif
#ifdef FindNextFile
#pragma push_macro("FindNextFile")
#undef FindNextFile
#define SALAMANDER_POP_FINDNEXTFILE
#endif
#endif // SALAMANDER_WINAPI_MACROS_GUARD
#define CreateFile CreateFileUtf8Local
#define DeleteFile DeleteFileUtf8Local
#define MoveFile MoveFileUtf8Local
#define CopyFile CopyFileUtf8Local
#define GetFileAttributes GetFileAttributesUtf8Local
#define SetFileAttributes SetFileAttributesUtf8Local
#define CreateDirectory CreateDirectoryUtf8Local
#define RemoveDirectory RemoveDirectoryUtf8Local
#define FindFirstFile FindFirstFileUtf8Local
#define FindNextFile FindNextFileUtf8Local

#ifdef SALAMANDER_RESTORE_WINAPI_MACROS
#ifdef SALAMANDER_POP_CREATEFILE
#pragma pop_macro("CreateFile")
#undef SALAMANDER_POP_CREATEFILE
#endif
#ifdef SALAMANDER_POP_DELETEFILE
#pragma pop_macro("DeleteFile")
#undef SALAMANDER_POP_DELETEFILE
#endif
#ifdef SALAMANDER_POP_MOVEFILE
#pragma pop_macro("MoveFile")
#undef SALAMANDER_POP_MOVEFILE
#endif
#ifdef SALAMANDER_POP_COPYFILE
#pragma pop_macro("CopyFile")
#undef SALAMANDER_POP_COPYFILE
#endif
#ifdef SALAMANDER_POP_GETFILEATTRIBUTES
#pragma pop_macro("GetFileAttributes")
#undef SALAMANDER_POP_GETFILEATTRIBUTES
#endif
#ifdef SALAMANDER_POP_SETFILEATTRIBUTES
#pragma pop_macro("SetFileAttributes")
#undef SALAMANDER_POP_SETFILEATTRIBUTES
#endif
#ifdef SALAMANDER_POP_CREATEDIRECTORY
#pragma pop_macro("CreateDirectory")
#undef SALAMANDER_POP_CREATEDIRECTORY
#endif
#ifdef SALAMANDER_POP_REMOVEDIRECTORY
#pragma pop_macro("RemoveDirectory")
#undef SALAMANDER_POP_REMOVEDIRECTORY
#endif
#ifdef SALAMANDER_POP_FINDFIRSTFILE
#pragma pop_macro("FindFirstFile")
#undef SALAMANDER_POP_FINDFIRSTFILE
#endif
#ifdef SALAMANDER_POP_FINDNEXTFILE
#pragma pop_macro("FindNextFile")
#undef SALAMANDER_POP_FINDNEXTFILE
#endif
#endif // SALAMANDER_RESTORE_WINAPI_MACROS
#endif // SALAMANDER_UTF8_WINAPI

