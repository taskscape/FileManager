// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>

// Keep the menu writer usable by legacy plug-in compilers as well as the current VS toolchain.
class CUtf8MenuNativeText
{
private:
    CUtf8MenuNativeText(const CUtf8MenuNativeText&);
    CUtf8MenuNativeText& operator=(const CUtf8MenuNativeText&);

public:
    wchar_t* Text;
    int Length;
    CUtf8MenuNativeText() : Text(NULL), Length(0) {}
    ~CUtf8MenuNativeText() { free(Text); }
    bool Convert(const char* text)
    {
        free(Text);
        Text = NULL;
        Length = 0;
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
        if (count <= 0)
            return false;
        Text = static_cast<wchar_t*>(malloc(static_cast<size_t>(count) * sizeof(wchar_t)));
        if (Text == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return false;
        }
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, Text, count) != count)
            return false;
        Length = count - 1;
        return true;
    }
};

// Native menus store Unicode: UTF-8 command names must be converted before an HMENU round trip.
inline bool MenuItemHasText(const MENUITEMINFOA& item)
{
    return (item.fMask & MIIM_STRING) != 0 ||
           ((item.fMask & MIIM_TYPE) != 0 && (item.fType & (MFT_BITMAP | MFT_OWNERDRAW | MFT_SEPARATOR)) == 0);
}

inline BOOL WriteMenuItemUtf8(HMENU menu, UINT item, BOOL byPosition, const MENUITEMINFOA* info, bool insert)
{
    if (info == NULL || info->cbSize != sizeof(MENUITEMINFOA))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    typedef char MenuMetadataLayoutsMustMatch[sizeof(MENUITEMINFOA) == sizeof(MENUITEMINFOW) ? 1 : -1];
    MENUITEMINFOW wideInfo;
    memcpy(&wideInfo, info, sizeof(wideInfo));
    CUtf8MenuNativeText text;
    if (MenuItemHasText(*info) && info->dwTypeData != NULL)
    {
        if (!text.Convert(info->dwTypeData))
            return FALSE;
        wideInfo.dwTypeData = text.Text;
        wideInfo.cch = static_cast<UINT>(text.Length);
    }
    // Owner-draw data, bitmap handles, separators, states and submenu IDs retain their native meaning.
    return insert ? InsertMenuItemW(menu, item, byPosition, &wideInfo) : SetMenuItemInfoW(menu, item, byPosition, &wideInfo);
}

inline BOOL InsertMenuItemUtf8(HMENU menu, UINT item, BOOL byPosition, const MENUITEMINFOA* info)
{
    return WriteMenuItemUtf8(menu, item, byPosition, info, true);
}

inline BOOL SetMenuItemInfoUtf8(HMENU menu, UINT item, BOOL byPosition, const MENUITEMINFOA* info)
{
    return WriteMenuItemUtf8(menu, item, byPosition, info, false);
}

inline BOOL WriteMenuUtf8(HMENU menu, UINT position, UINT flags, UINT_PTR id, const char* value, int operation)
{
    CUtf8MenuNativeText text;
    const wchar_t* wideValue = reinterpret_cast<const wchar_t*>(value);
    if ((flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) == 0 && value != NULL)
    {
        if (!text.Convert(value))
            return FALSE;
        wideValue = text.Text;
    }
    if (operation == 0)
        return InsertMenuW(menu, position, flags, id, wideValue);
    if (operation == 1)
        return AppendMenuW(menu, flags, id, wideValue);
    return ModifyMenuW(menu, position, flags, id, wideValue);
}

inline BOOL InsertMenuUtf8(HMENU menu, UINT position, UINT flags, UINT_PTR id, const char* text)
{
    return WriteMenuUtf8(menu, position, flags, id, text, 0);
}

inline BOOL AppendMenuUtf8(HMENU menu, UINT flags, UINT_PTR id, const char* text)
{
    return WriteMenuUtf8(menu, 0, flags, id, text, 1);
}

inline BOOL ModifyMenuUtf8(HMENU menu, UINT position, UINT flags, UINT_PTR id, const char* text)
{
    return WriteMenuUtf8(menu, position, flags, id, text, 2);
}

