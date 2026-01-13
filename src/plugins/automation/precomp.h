// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

/*
	Automation Plugin for Open Salamander
	
	Copyright (c) 2009-2023 Milan Kase <manison@manison.cz>
	Copyright (c) 2010-2023 Open Salamander Authors
	
	stdafx.h
	Precompiled header.
*/

#pragma once

#define ISOLATION_AWARE_ENABLED 1
#define WIN32_LEAN_AND_MEAN // exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <windowsx.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <tchar.h>
#include <stdlib.h>
#include <dispex.h>
#include <vsstyle.h>
#include <uxtheme.h>
#include <Tlhelp32.h>
#include "salamander.h"

#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>

#include <comdef.h>

#include "unkimpl.h"

#include <ActivScp.h>
#include <ActivDbg.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#ifdef _DEBUG
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
#undef assert
#define assert _ASSERTE
#else
#include <assert.h>
#endif

// already defined in VS2015 and latest SDK 10.0.10240.0
#ifndef __ICanHandleException_INTERFACE_DEFINED__
MIDL_INTERFACE("c5598e60-b307-11d1-b27d-006008c3fbfb")
ICanHandleException : public IUnknown
{
    //If a call to InvokeEx, or similar, results in an exception, the
    //called method can use this interface to determine if the caller
    //is capable of dealing with the exception. The first parameter is
    //an EXCEPINFO structure containing the information that will be reported
    //to the host if no error handlers are found. The second
    //parameter is a value associated with the exception, such as the value
    //thrown by a throw statement. This parameter may be NULL.

    //These values can be used by the caller to decide whether or
    //not it can handle the exception. If the caller can handle the exception
    //the function returns S_OK. Otherwise it returns E_FAIL.

    virtual HRESULT STDMETHODCALLTYPE CanHandleException(
        __in EXCEPINFO * pExcepInfo,
        __in_opt VARIANT * pvar) = 0;
};
#endif

/// The script engines return this to the host when there has been an unhandled
/// error that the host has already been informed about via OnScriptError.
/// \see http://support.microsoft.com/kb/247784
/// \see http://blogs.msdn.com/ericlippert/archive/2003/10/22/53267.aspx
#ifndef SCRIPT_E_REPORTED
#define SCRIPT_E_REPORTED _HRESULT_TYPEDEF_(0x80020101)
#endif

/// Recorded error is being propagated up the call stack to a waiting catch handler.
/// \see http://blogs.msdn.com/ericlippert/archive/2003/10/22/53267.aspx
#ifndef SCRIPT_E_PROPAGATE
#define SCRIPT_E_PROPAGATE _HRESULT_TYPEDEF_(0x80020102)
#endif

/// This code is used by WScript.Quit method to raise exception
/// and terminate the script.
#define SALAUT_E_ABORT _HRESULT_TYPEDEF_(0x8004fffd)

/// Property cannot be set while the progress dialog is shown.
#define SALAUT_E_READONLYWHILEPROGRESSSHOWN MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 1)

/// Internal exception raised during TraceE and TraceI.
#define SALAUT_E_TRACE MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 2)

/// File mask syntax error.
#define SALAUT_E_FILEMASKSYNTAX MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 3)

/// Trying to assign non-component object to the GUI container.
#define SALAUT_E_NOTCOMPONENT MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 4)

/// The specified path is invalid.
#define SALAUT_E_INVALIDPATH MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 5)

#include "strconv.h"

extern CSalamanderGeneralAbstract* SalamanderGeneral;
extern CSalamanderGUIAbstract* SalamanderGUI;
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
