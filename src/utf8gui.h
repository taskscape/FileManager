// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// Shared UTF-8 aware wrappers around the ANSI Win32 text APIs.
//
// Strings are kept internally as UTF-8 (char*). Passing them to the ANSI form of a
// text API (SetWindowText/SetDlgItemText/SendMessage(WM_SETTEXT)/DrawText/ExtTextOut/
// ListView_SetItemText/CB_ADDSTRING/...) makes the system reinterpret the bytes through
// the ANSI code page, producing mojibake (e.g. a NBSP thousands separator 0xC2 0xA0
// shows up as "Â "). These helpers convert UTF-8 to UTF-16 and call the Unicode (...W)
// variant instead.
//
// NOTE: several .cpp files still carry file-local "static" copies of some of these
// helpers (identical implementations). Such files must NOT include this header, to avoid
// redefinition conflicts - they already have working equivalents. Include this header
// only in files that need a wrapper and do not define it locally.

#include "strutils.h" // ConvertAllocUtf8ToWide, CStrP

inline void SetWindowTextUtf8(HWND hWnd, const char* text)
{
    CStrP wide(ConvertAllocUtf8ToWide(text != NULL ? text : "", -1));
    SendMessageW(hWnd, WM_SETTEXT, 0, (LPARAM)(wide != NULL ? wide.Ptr : L""));
}

inline void SetDlgItemTextUtf8(HWND hWnd, int itemID, const char* text)
{
    SetWindowTextUtf8(GetDlgItem(hWnd, itemID), text);
}

inline void GetWindowTextUtf8(HWND hWnd, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return;
    buf[0] = 0;
    int len = GetWindowTextLengthW(hWnd);
    if (len <= 0)
        return;
    CStrP wide((WCHAR*)malloc(sizeof(WCHAR) * (len + 1)));
    if (wide == NULL)
        return;
    GetWindowTextW(hWnd, wide, len + 1);
    ConvertWideToUtf8(wide, -1, buf, bufSize);
}

inline void GetDlgItemTextUtf8(HWND hWnd, int itemID, char* buf, int bufSize)
{
    GetWindowTextUtf8(GetDlgItem(hWnd, itemID), buf, bufSize);
}

// Sets a list view (sub)item's text. LVM_SETITEMTEXTW is a control-defined message
// (> WM_USER) so it is delivered to the control verbatim - it works regardless of whether
// the list view window itself is ANSI or Unicode.
inline void ListViewSetItemTextUtf8(HWND hList, int item, int subItem, const char* text)
{
    CStrP wide(ConvertAllocUtf8ToWide(text != NULL ? text : "", -1));
    LVITEMW lvi;
    lvi.iSubItem = subItem;
    lvi.pszText = (LPWSTR)(wide != NULL ? wide.Ptr : L"");
    SendMessageW(hList, LVM_SETITEMTEXTW, (WPARAM)item, (LPARAM)&lvi);
}

inline int DrawTextUtf8(HDC hDC, const char* text, int textLen, LPRECT rect, UINT format)
{
    if (text == NULL)
        return 0;
    if (textLen == -1)
        textLen = (int)strlen(text);
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, textLen, NULL, 0);
    if (wideLen <= 0)
        return DrawText(hDC, text, textLen, rect, format); // fallback to ANSI on conversion failure
    wchar_t* wideText = (wchar_t*)_alloca((wideLen + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, text, textLen, wideText, wideLen + 1);
    wideText[wideLen] = 0;
    return DrawTextW(hDC, wideText, wideLen, rect, format);
}

inline BOOL ExtTextOutUtf8(HDC hdc, int x, int y, UINT options, const RECT* lprc,
                           const char* text, UINT len, const INT* dx)
{
    if (text == NULL || len == 0)
        return ExtTextOutW(hdc, x, y, options, lprc, L"", 0, NULL);
    CStrP textW(ConvertAllocUtf8ToWide(text, (int)len));
    if (textW == NULL)
        return ExtTextOutW(hdc, x, y, options, lprc, L"?", 1, NULL);
    // UTF-8 conversion returns an owned terminated Unicode buffer for this GDI call.
    int wlen = static_cast<int>(wcslen(textW));
    return ExtTextOutW(hdc, x, y, options, lprc, textW, wlen, dx);
}

inline BOOL GetTextExtentPoint32Utf8(HDC hdc, const char* text, int len, SIZE* size)
{
    size->cx = 0;
    size->cy = 0;
    if (text == NULL || len <= 0)
        return TRUE;
    CStrP textW(ConvertAllocUtf8ToWide(text, len));
    if (textW == NULL)
        return GetTextExtentPoint32(hdc, text, len, size); // fallback to ANSI on conversion failure
    // UTF-8 conversion returns an owned terminated Unicode buffer for this GDI measurement.
    int wlen = static_cast<int>(wcslen(textW));
    return GetTextExtentPoint32W(hdc, textW, wlen, size);
}
