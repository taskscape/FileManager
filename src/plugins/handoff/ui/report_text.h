// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Common file dialogs (IFileOpenDialog/IFileSaveDialog on the calling STA
// thread), "Save report..." (T6.9), and clipboard export.

#include <string>
#include <vector>

#include "findings.h"

bool BrowseOpenFile(HWND owner, int titleId, int filterNameId, const wchar_t* pattern, const std::wstring& initialFolder,
                    std::wstring& path);
bool BrowseFolder(HWND owner, int titleId, const std::wstring& initialFolder, std::wstring& path);
bool BrowseSaveFile(HWND owner, int filterNameId, const wchar_t* pattern, const wchar_t* extension,
                    const std::wstring& defaultName, const std::wstring& initialFolder, std::wstring& path);

// Writes 'bytes' to a user-chosen report file. Reports are never written into
// 'forbiddenRoot' (the package being verified), so they cannot change it.
void SaveReport(HWND owner, const std::wstring& defaultName, const std::string& bytes, const std::wstring& forbiddenRoot);

bool CopyTextToClipboard(HWND owner, const std::wstring& text);

// Finding lines for reports: "Error HO-PDF-010 path: message".
std::vector<std::wstring> FindingLines(const std::vector<handoff::Finding>& findings);
