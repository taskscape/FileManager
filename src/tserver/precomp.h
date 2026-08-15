// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <crtdbg.h>
#include <limits.h>
#include <stdio.h>
#include <ostream>
#include <commctrl.h> // need LPCOLORMAP
#include <aclapi.h>
#include <process.h>
#include <math.h>
#include <tchar.h>

static int FormatTestServerDateTime(const SYSTEMTIME* time, DWORD flags, LPCWSTR format,
                                    WCHAR* buffer, int bufferSize, BOOL isDate)
{
    // Test-server exports are Unicode, so retain their custom 24-hour format after locale-name resolution.
    WCHAR localeName[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(localeName, ARRAYSIZE(localeName)) == 0)
        return 0;
    return isDate
               ? GetDateFormatEx(localeName, flags, time, format, buffer, bufferSize, NULL)
               : GetTimeFormatEx(localeName, flags, time, format, buffer, bufferSize);
}

//#if defined(_DEBUG) && defined(_MSC_VER)  // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
//#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
//#endif
