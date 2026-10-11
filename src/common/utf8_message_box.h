// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <string>
#include <new>

// Keep the shared SDK boundary compatible with legacy C++ consumers; only the Windows dialog needs UTF-16.
inline int MessageBoxUtf8(HWND owner, const char* text, const char* caption, UINT type)
{
    int textLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    int captionLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, caption, -1, NULL, 0);
    if (textLength <= 0 || captionLength <= 0)
        return 0;
    try
    {
        std::wstring wideText(textLength, L'\0'), wideCaption(captionLength, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &wideText[0], textLength) != textLength ||
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, caption, -1, &wideCaption[0], captionLength) != captionLength)
            return 0;
        return MessageBoxW(owner, wideText.c_str(), wideCaption.c_str(), type);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
}
