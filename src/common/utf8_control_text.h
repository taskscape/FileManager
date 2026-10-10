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

// Captions read for comparison or later restoration must retain UTF-8, not the host ANSI code page.
inline int ReadUtf8ControlText(HWND control, char* text, int capacity)
{
    if (text == NULL || capacity <= 0)
        return 0;
    text[0] = 0;
    if (control == NULL)
        return 0;
    int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return 0;
    std::wstring wide(length + 1, L'\0');
    if (GetWindowTextW(control, &wide[0], length + 1) == 0)
        return 0;
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(), -1, text, capacity, NULL, NULL);
    if (bytes == 0)
        text[0] = 0; // A small caller buffer must not publish a partial UTF-8 caption.
    return bytes > 0 ? bytes - 1 : 0;
}

// Localized preview samples use UTF-16 GDI text just like native dialog controls.
inline BOOL PaintUtf8ControlText(HDC dc, int x, int y, UINT options, const RECT* rect, const char* text)
{
    if (text == NULL)
        return FALSE;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length == 0)
        return FALSE;
    std::wstring wide(length, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &wide[0], length) == 0)
        return FALSE;
    return ExtTextOutW(dc, x, y, options, rect, wide.c_str(), length - 1, NULL);
}

// Owner-data providers still produce UTF-8 LVITEMA data; Unicode notifications must return UTF-16 to the list.
class CUtf8ListViewDispInfo
{
    NMLVDISPINFOA Data;
    NMLVDISPINFOW* Wide;
    NMLVDISPINFOA* Legacy;
    std::string Buffer;

public:
    explicit CUtf8ListViewDispInfo(LPARAM notification)
        : Data(), Wide(NULL), Legacy((NMLVDISPINFOA*)notification)
    {
        if (Legacy->hdr.code != LVN_GETDISPINFOW)
            return;
        Wide = (NMLVDISPINFOW*)notification;
        Data.hdr = Wide->hdr;
        // LVITEMA/W share all scalar fields; only the text pointer and its capacity change encoding.
        static_assert(sizeof(LVITEMA) == sizeof(LVITEMW), "list-view item layouts must match");
        memcpy(&Data.item, &Wide->item, sizeof(Data.item));
        if (Wide->item.mask & LVIF_TEXT)
        {
            // Existing providers may copy a MAX_PATH value even when the control requests a short preview.
            size_t bytes = Wide->item.cchTextMax > 0 ? (size_t)Wide->item.cchTextMax * 4 : 0;
            Buffer.resize(bytes > 4096 ? bytes : 4096, '\0');
            Data.item.pszText = &Buffer[0];
            Data.item.cchTextMax = (int)Buffer.size();
        }
        Legacy = &Data;
    }

    NMLVDISPINFOA* Get() { return Legacy; }

    ~CUtf8ListViewDispInfo()
    {
        if (Wide == NULL)
            return;
        WCHAR* output = Wide->item.pszText;
        int capacity = Wide->item.cchTextMax;
        memcpy(&Wide->item, &Data.item, sizeof(Wide->item));
        Wide->item.pszText = output;
        Wide->item.cchTextMax = capacity;
        if (!(Data.item.mask & LVIF_TEXT) || output == NULL || capacity <= 0)
            return;
        output[0] = 0;
        const char* text = Data.item.pszText;
        if (text == NULL || text == LPSTR_TEXTCALLBACKA)
            return;
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
        if (length == 0)
            return;
        std::wstring decoded(length, L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, &decoded[0], length) == 0)
            return;
        int copied = length - 1 < capacity - 1 ? length - 1 : capacity - 1;
        // A clipped cell must never return half of a supplementary Unicode character.
        if (copied > 0 && decoded[copied - 1] >= 0xd800 && decoded[copied - 1] <= 0xdbff)
            --copied;
        memcpy(output, decoded.c_str(), copied * sizeof(WCHAR));
        output[copied] = 0;
    }

    CUtf8ListViewDispInfo(const CUtf8ListViewDispInfo&) = delete;
    CUtf8ListViewDispInfo& operator=(const CUtf8ListViewDispInfo&) = delete;
};

// Existing plug-in profiles may still contain ACP text; preserve those names while accepting UTF-8 profiles.
inline std::string LegacyControlTextToUtf8(const char* text)
{
    if (text == NULL)
        return std::string();
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0) != 0)
        return std::string(text);
    int length = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (length == 0)
        return std::string();
    std::wstring wide(length, L'\0');
    if (MultiByteToWideChar(CP_ACP, 0, text, -1, &wide[0], length) == 0)
        return std::string();
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (bytes == 0)
        return std::string();
    std::string result(bytes, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.c_str(), -1, &result[0], bytes, NULL, NULL) == 0)
        return std::string();
    result.resize(bytes - 1);
    return result;
}

// Retain the legacy-data fallback only at boundaries that explicitly accept old stored names.
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
