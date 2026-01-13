// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#ifndef OPENSAL_VERSION // version built for Salamander distribution will not be backward compatible with version 5.0 (unnecessary complication)
#define SALSDK_COMPATIBLE_WITH_VER 103
#define REQUIRE_COMPATIBLE_SAL_VERSION "This plugin requires Open Salamander 5.0 or later."
#endif // OPENSAL_VERSION

//#define TIMINGTEST

#define SALAMANDER
//#define TRACE_ENABLE
#define WM_APP_ICONLOADED (WM_APP + 1)
#define WM_APP_DIRPOPULATED (WM_APP + 2) //wParam = 0; lParam = CZRoot
#define WM_APP_LOGUPDATED (WM_APP + 3)   //wParam = count; lParam = logger

#define WIN32_LEAN_AND_MEAN // exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>
#include <CommDlg.h>
#include <ShellAPI.h>
#include <shlobj.h>
#include <Windowsx.h> // helper macros

// C RunTime Header Files
#include <malloc.h>
#include <tchar.h>

// CRT diagnostics and utility headers
#include <crtdbg.h>
#include <limits.h>
#include <process.h>
#include <commctrl.h>
#include <ostream>
#include <stdio.h>
#include <time.h>

// Salamander headers
#include "versinfo.rh2"

#include "spl_com.h"
//#include "spl_arc.h"
#include "spl_base.h"
#include "spl_gen.h"
//#include "spl_fs.h"
#include "spl_menu.h"
//#include "spl_thum.h"
//#include "spl_view.h"
#include "spl_vers.h"
#include "spl_gui.h"

#include "dbg.h"
#include "auxtools.h"
//#include "mhandles.h"

//#include "DiskMapPlugin.rh"
//#include "arraylt.h"
//#include "winliblt.h"
//#include "dialogs.h"
//#include "demoplug.h"
//#include "demoplug.rh"

// TODO: reference additional headers your program requires here

#ifndef ARRAYSIZE
#define RTL_NUMBER_OF_V1(A) (sizeof(A) / sizeof((A)[0]))
#define RTL_NUMBER_OF_V2(A) RTL_NUMBER_OF_V1(A)
#define ARRAYSIZE(A) RTL_NUMBER_OF_V2(A)
#endif // ARRAYSIZE

#include "../DiskMap/Resources/DiskMap.rh2"
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


#endif // SALAMANDER_UTF8_WINAPI
