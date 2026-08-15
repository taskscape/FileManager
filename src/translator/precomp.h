// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <tchar.h>
#include <crtdbg.h>
#include <ostream>
#include <limits.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <process.h>

#include "lstrfix.h"
#include "trace.h"
#include "messages.h"
#include "handles.h"

#include "array.h"
#include "winlib.h"
#include "str.h"
#include "strutils.h"

#include "consts.h"

#include "versinfo.h"
#include "trldata.h"
#include "resource.h"

static DWORD GetFileAttributesUtf8Local(const char* fileName)
{
    if (fileName == NULL)
        return INVALID_FILE_ATTRIBUTES;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1, NULL, 0);
    if (len <= 0)
        return INVALID_FILE_ATTRIBUTES;
    WCHAR* buf = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * len);
    if (buf == NULL)
        return INVALID_FILE_ATTRIBUTES;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName, -1, buf, len) == 0)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return INVALID_FILE_ATTRIBUTES;
    }
    DWORD attrs = GetFileAttributesW(buf);
    HeapFree(GetProcessHeap(), 0, buf);
    return attrs;
}

static BOOL GetFileSizeDwordLocal(HANDLE file, DWORD* size)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize))
        return FALSE;
    if (fileSize.QuadPart < 0 || (ULONGLONG)fileSize.QuadPart > MAXDWORD)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    // Translator loaders allocate/read DWORD-sized buffers, so reject oversize files before narrowing the 64-bit result.
    *size = (DWORD)fileSize.QuadPart;
    return TRUE;
}
