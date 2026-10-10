// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "utf8_menu_native.h"
#include "unicode_text_layout.h"

inline void CopyMenuTextUtf8(char* target, int capacity, const char* text)
{
    // Plug-in and menu query buffers count bytes; never leave half a character at their clipping boundary.
    if (target == NULL || capacity <= 0)
        return;
    int keep = text == NULL ? 0 : Utf8BoundedPrefixLength(text, static_cast<int>(strlen(text)), capacity);
    if (keep != 0)
        memcpy(target, text, keep);
    target[keep] = 0;
}

inline bool GetMenuItemTextUtf8(HMENU menu, UINT item, BOOL byPosition, std::string& text)
{
    // Query the Unicode length first; fixed ANSI-sized buffers lose long or multibyte menu captions.
    text.clear();
    MENUITEMINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    if (!GetMenuItemInfoW(menu, item, byPosition, &info))
        return false;
    try
    {
        std::wstring wide(info.cch + 1, L'\0');
        info.dwTypeData = &wide[0];
        info.cch = static_cast<UINT>(wide.size());
        if (!GetMenuItemInfoW(menu, item, byPosition, &info))
            return false;
        return WideTextToUtf8(wide.c_str(), static_cast<int>(info.cch), text);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline int DrawMenuTextUtf8(HDC dc, const char* text, int length, RECT* rectangle, UINT flags)
{
    // Popup columns and menu bars must measure and paint the same decoded characters, including accelerators.
    std::wstring wide;
    if (!Utf8TextToWide(text, length, wide))
        return 0;
    return DrawTextW(dc, wide.c_str(), static_cast<int>(wide.size()), rectangle, flags);
}
