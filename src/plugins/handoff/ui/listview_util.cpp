// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "listview_util.h"
#include "sdk_strings.h"

namespace
{

UINT WindowDpi(HWND window)
{
    UINT dpi = window != NULL ? GetDpiForWindow(window) : 0;
    return dpi != 0 ? dpi : 96;
}

HICON LoadSystemSmallIcon(LPCWSTR id, int size)
{
    // Shared system icons are owned by the system; ImageList_AddIcon copies them.
    return (HICON)LoadImageW(NULL, id, IMAGE_ICON, size, size, LR_SHARED);
}

} // namespace

int DipToPixels(HWND window, int dip)
{
    return MulDiv(dip, (int)WindowDpi(window), 96);
}

int PixelsToDip(HWND window, int pixels)
{
    return MulDiv(pixels, 96, (int)WindowDpi(window));
}

void InitListView(HWND list, bool checkBoxes, bool severityIcons)
{
    DWORD style = LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER;
    if (checkBoxes)
        style |= LVS_EX_CHECKBOXES;
    ListView_SetExtendedListViewStyle(list, style);
    if (severityIcons)
    {
        int size = GetSystemMetricsForDpi(SM_CXSMICON, WindowDpi(list));
        HIMAGELIST images = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 3, 0);
        if (images != NULL)
        {
            // Order matches SeverityImage(): info, warning, error.
            LPCWSTR ids[3] = {HO_IDI_INFORMATION, HO_IDI_WARNING, HO_IDI_ERROR};
            for (LPCWSTR id : ids)
            {
                HICON icon = LoadSystemSmallIcon(id, size);
                ImageList_AddIcon(images, icon);
            }
            // Without LVS_SHAREIMAGELISTS the list view destroys the image list with itself.
            ListView_SetImageList(list, images, LVSIL_SMALL);
        }
    }
}

int SeverityImage(handoff::Severity severity)
{
    switch (severity)
    {
    case handoff::Severity::Error:
        return 2;
    case handoff::Severity::Warning:
        return 1;
    default:
        return 0;
    }
}

void AddColumns(HWND list, const std::vector<ColumnDef>& columns, const std::vector<unsigned char>& savedWidthsDip)
{
    // Saved widths are used only when they describe exactly these columns.
    bool useSaved = savedWidthsDip.size() == columns.size() * sizeof(int32_t);
    for (size_t i = 0; i < columns.size(); i++)
    {
        int widthDip = columns[i].WidthDip;
        if (useSaved)
        {
            int32_t saved;
            memcpy(&saved, savedWidthsDip.data() + i * sizeof(int32_t), sizeof(saved));
            if (saved >= 16 && saved <= 4000)
                widthDip = saved;
        }
        std::wstring title = Text(columns[i].TextId);
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT | LVCF_SUBITEM;
        column.fmt = columns[i].Format;
        column.cx = DipToPixels(list, widthDip);
        column.pszText = &title[0];
        column.iSubItem = (int)i;
        SendMessageW(list, LVM_INSERTCOLUMNW, i, (LPARAM)&column);
    }
}

std::vector<unsigned char> SaveColumnWidths(HWND list)
{
    std::vector<unsigned char> bytes;
    HWND header = ListView_GetHeader(list);
    int count = header != NULL ? Header_GetItemCount(header) : 0;
    for (int i = 0; i < count; i++)
    {
        int32_t width = PixelsToDip(list, ListView_GetColumnWidth(list, i));
        const unsigned char* raw = (const unsigned char*)&width;
        bytes.insert(bytes.end(), raw, raw + sizeof(width));
    }
    return bytes;
}

int AddRow(HWND list, const std::wstring& text, LPARAM param, int image)
{
    LVITEMW item = {};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.pszText = const_cast<LPWSTR>(text.c_str());
    item.lParam = param;
    if (image >= 0)
    {
        item.mask |= LVIF_IMAGE;
        item.iImage = image;
    }
    return (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
}

void SetCell(HWND list, int row, int column, const std::wstring& text)
{
    LVITEMW item = {};
    item.iSubItem = column;
    item.pszText = const_cast<LPWSTR>(text.c_str());
    SendMessageW(list, LVM_SETITEMTEXTW, row, (LPARAM)&item);
}

LPARAM RowParam(HWND list, int row)
{
    LVITEMW item = {};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&item))
        return -1;
    return item.lParam;
}

int FocusedOrSelectedRow(HWND list)
{
    int row = ListView_GetNextItem(list, -1, LVNI_SELECTED | LVNI_FOCUSED);
    if (row < 0)
        row = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    return row;
}

std::vector<int> SelectedRows(HWND list)
{
    std::vector<int> rows;
    for (int row = ListView_GetNextItem(list, -1, LVNI_SELECTED); row >= 0;
         row = ListView_GetNextItem(list, row, LVNI_SELECTED))
        rows.push_back(row);
    return rows;
}

void SelectRow(HWND list, int row)
{
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    if (row < 0)
        return;
    ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, row, FALSE);
}

int FindRowByParam(HWND list, LPARAM param)
{
    LVFINDINFOW find = {};
    find.flags = LVFI_PARAM;
    find.lParam = param;
    return (int)SendMessageW(list, LVM_FINDITEMW, (WPARAM)-1, (LPARAM)&find);
}

int ComboAdd(HWND combo, const std::wstring& text, LPARAM data)
{
    int index = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    if (index >= 0)
        SendMessageW(combo, CB_SETITEMDATA, index, data);
    return index;
}

std::wstring ComboItemText(HWND combo, int index)
{
    LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, index, 0);
    if (length == CB_ERR || length <= 0)
        return std::wstring();
    std::wstring text((size_t)length + 1, L'\0');
    LRESULT copied = SendMessageW(combo, CB_GETLBTEXT, index, (LPARAM)&text[0]);
    text.resize(copied > 0 ? (size_t)copied : 0);
    return text;
}

std::wstring ListAsText(HWND list)
{
    std::wstring text;
    HWND header = ListView_GetHeader(list);
    int columns = header != NULL ? Header_GetItemCount(header) : 0;
    std::vector<wchar_t> buffer(4096);
    for (int column = 0; column < columns; column++)
    {
        LVCOLUMNW info = {};
        info.mask = LVCF_TEXT;
        info.pszText = buffer.data();
        info.cchTextMax = (int)buffer.size();
        buffer[0] = 0;
        SendMessageW(list, LVM_GETCOLUMNW, column, (LPARAM)&info);
        if (column > 0)
            text += L'\t';
        text += buffer.data();
    }
    text += L"\r\n";
    int rows = ListView_GetItemCount(list);
    for (int row = 0; row < rows; row++)
    {
        for (int column = 0; column < columns; column++)
        {
            LVITEMW item = {};
            item.iSubItem = column;
            item.pszText = buffer.data();
            item.cchTextMax = (int)buffer.size();
            buffer[0] = 0;
            SendMessageW(list, LVM_GETITEMTEXTW, row, (LPARAM)&item);
            if (column > 0)
                text += L'\t';
            text += buffer.data();
        }
        text += L"\r\n";
    }
    return text;
}
