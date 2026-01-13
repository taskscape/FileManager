// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//#define WIN32_LEAN_AND_MEAN // exclude rarely-used stuff from Windows headers

#include <tchar.h>
#include <windows.h>
#include <windowsx.h>
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#ifdef _WIN64
#define PICTVIEW_DLL_IN_SEPARATE_PROCESS // the x64 version of PictView uses the 32-bit pvw32cnv.dll via IPC (inter-process communication) with salpvenv.exe
#define ENABLE_WIA                       // the x64 version of PictView uses WIA 1.0 for scanning
#else                                    // _WIN64
#define ENABLE_TWAIN32                   // the x86 version of PictView uses TWAIN 1.x for scanning (which internally also supports WIA)
#endif                                   // _WIN64

// workaround for runtime check failure in the debug build: the original version of the macro casts rgb to WORD,
// reporting a data loss (the RED component)
#undef GetGValue
#define GetGValue(rgb) ((BYTE)(((rgb) >> 8) & 0xFF))

#include "versinfo.rh2"

#include "pictview.rh"
#include "pictview.rh2"
#include "lang\lang.rh"
#include "spl_com.h"
#include "spl_base.h"
#include "spl_arc.h"
#include "spl_gen.h"
#include "spl_menu.h"
#include "spl_thum.h"
#include "spl_view.h"
#include "spl_gui.h"
#include "spl_vers.h"
#include "dbg.h"
#include "arraylt.h"
#include "winliblt.h"
#include "auxtools.h"
#include "lukas\gdi.h"

// When we have an old version of windowsx.h
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(x) HIWORD(x)
#define GET_X_LPARAM(x) LOWORD(x)
#endif

#ifndef INT32
#define INT32 int
#define UINT32 unsigned int
#endif

#ifndef SetWindowLongPtr
// compiling on VC6 w/o reasonably new SDK
#define SetWindowLongPtr SetWindowLong
#define GetWindowLongPtr GetWindowLong
#define GWLP_USERDATA GWL_USERDATA
#endif

#define SizeOf(a) sizeof(a) / sizeof(a[0])
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
