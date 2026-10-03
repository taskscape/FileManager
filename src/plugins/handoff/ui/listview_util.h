// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Report-mode list views: columns with persisted widths in DIP, rows carrying
// an index into the window's model, and severity shown as text plus a system
// icon (never colour alone, handoff-spec.md C.8.1).

#include <string>
#include <vector>

#include "findings.h"

// System icons as wide resource names: the plug-in is built without UNICODE,
// so the IDI_* macros would produce narrow names for the W loaders.
#define HO_IDI_ERROR MAKEINTRESOURCEW(32513)
#define HO_IDI_WARNING MAKEINTRESOURCEW(32515)
#define HO_IDI_INFORMATION MAKEINTRESOURCEW(32516)

struct ColumnDef
{
    int TextId;
    int WidthDip;
    int Format = LVCFMT_LEFT;
};

void InitListView(HWND list, bool checkBoxes, bool severityIcons);
void AddColumns(HWND list, const std::vector<ColumnDef>& columns, const std::vector<unsigned char>& savedWidthsDip);
std::vector<unsigned char> SaveColumnWidths(HWND list);

int AddRow(HWND list, const std::wstring& text, LPARAM param, int image = -1);
void SetCell(HWND list, int row, int column, const std::wstring& text);
LPARAM RowParam(HWND list, int row);
int FocusedOrSelectedRow(HWND list);
std::vector<int> SelectedRows(HWND list);
void SelectRow(HWND list, int row);
int FindRowByParam(HWND list, LPARAM param);

// Image index for LVSIL_SMALL of a list created with severityIcons.
int SeverityImage(handoff::Severity severity);

// All rows as tab-separated text with a header line (copy and reports).
std::wstring ListAsText(HWND list);

int DipToPixels(HWND window, int dip);
int PixelsToDip(HWND window, int pixels);

// Combo and list box helpers for Unicode item text.
int ComboAdd(HWND combo, const std::wstring& text, LPARAM data);
std::wstring ComboItemText(HWND combo, int index);
