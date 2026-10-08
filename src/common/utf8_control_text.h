// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>

// Native control messages take UTF-16 text; narrow application and plug-in strings are UTF-8.
inline LRESULT SendUtf8ControlString(HWND control, UINT message, WPARAM index, const char* text)
{
    if (control == NULL || text == NULL)
        return -1;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length == 0)
        return -1;
    std::wstring wide(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &wide[0], length) == 0)
        return -1;
    return SendMessageW(control, message, index, (LPARAM)wide.c_str());
}

inline LRESULT SendUtf8DialogControlString(HWND dialog, int id, UINT message, WPARAM index, const char* text)
{
    return SendUtf8ControlString(GetDlgItem(dialog, id), message, index, text);
}

// Existing plug-in profiles may still contain ACP text; preserve those names while accepting UTF-8 profiles.
inline LRESULT SendUtf8OrAcpControlString(HWND control, UINT message, WPARAM index, const char* text)
{
    if (control == NULL || text == NULL)
        return -1;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) != 0)
        return SendUtf8ControlString(control, message, index, text);
    return SendMessageA(control, message, index, (LPARAM)text);
}

inline int InsertListViewColumnUtf8(HWND control, int index, const LVCOLUMNA* column)
{
    if (column == NULL || column->pszText == NULL)
        return -1;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, column->pszText, -1, NULL, 0);
    if (length == 0)
        return -1;
    std::wstring text(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, column->pszText, -1, &text[0], length) == 0)
        return -1;
    // LVCOLUMNA and LVCOLUMNW differ only in the encoding of their text pointer.
    LVCOLUMNW wideColumn = {};
    wideColumn.mask = column->mask;
    if (column->mask & LVCF_FMT)
        wideColumn.fmt = column->fmt;
    if (column->mask & LVCF_WIDTH)
        wideColumn.cx = column->cx;
    wideColumn.pszText = &text[0];
    wideColumn.cchTextMax = length;
    if (column->mask & LVCF_SUBITEM)
        wideColumn.iSubItem = column->iSubItem;
    if (column->mask & LVCF_IMAGE)
        wideColumn.iImage = column->iImage;
    if (column->mask & LVCF_ORDER)
        wideColumn.iOrder = column->iOrder;
    if (column->mask & LVCF_MINWIDTH)
        wideColumn.cxMin = column->cxMin;
    if (column->mask & LVCF_DEFAULTWIDTH)
        wideColumn.cxDefault = column->cxDefault;
    if (column->mask & LVCF_IDEALWIDTH)
        wideColumn.cxIdeal = column->cxIdeal;
    return (int)SendMessageW(control, LVM_INSERTCOLUMNW, index, (LPARAM)&wideColumn);
}

inline BOOL SetListViewItemTextUtf8(HWND control, int item, int subItem, const char* text)
{
    if (text == NULL)
        return FALSE;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length == 0)
        return FALSE;
    std::wstring wide(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &wide[0], length) == 0)
        return FALSE;
    LVITEMW itemText = {};
    itemText.iSubItem = subItem;
    itemText.pszText = &wide[0];
    return (BOOL)SendMessageW(control, LVM_SETITEMTEXTW, item, (LPARAM)&itemText);
}
