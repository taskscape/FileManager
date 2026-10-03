// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Runtime-generated fixtures: every file a test needs is produced below one
// GUID directory under %TEMP% and removed afterwards, so no binary fixtures
// are checked in (handoff-spec.md T8.1).

#include <windows.h>
#include <string>
#include <utility>
#include <vector>

#include "spec.h"

namespace fixtures
{

bool CreateRoot();
bool RemoveRoot();
const std::wstring& Root();
// A fresh, empty directory below the fixture root.
std::wstring NewDir(const wchar_t* label);

bool WriteBytes(const std::wstring& path, const std::string& bytes);
std::string ReadBytes(const std::wstring& path);
bool Exists(const std::wstring& path);
std::wstring FileSha(const std::wstring& path);
// Recursive listing ('/'-separated, relative) of files below 'root'.
std::vector<std::wstring> ListFiles(const std::wstring& root);
std::vector<std::wstring> ListEntries(const std::wstring& root); // files and folders

// Page sizes in points (1/72 inch).
std::string MakePdf(const std::vector<std::pair<double, double>>& pages, bool encrypted = false);
const double A4W = 595.276, A4H = 841.890, A3W = 841.890, A3H = 1190.551;

// Encodes a WIC image (container GUID_ContainerFormatPng/Jpeg/Tiff...).
bool MakeImage(const std::wstring& path, const GUID& container, UINT width, UINT height, double dpi,
               const GUID& pixelFormat, bool gps = false);
// PSD header with optional ResolutionInfo resource (colour mode 3 = RGB, 4 = CMYK).
std::string MakePsd(unsigned width, unsigned height, unsigned short mode, bool withResolution, double dpi);
std::string MakeOtf();
bool MakeJunction(const std::wstring& link, const std::wstring& target);

std::wstring RepositoryRoot();
std::string ReadTemplate(const wchar_t* fileName);
handoff::SpecLoadResult ParseSpecText(const std::string& json);

// The A.5.1 quick-start working tree, including its .handoff specification.
void BuildQuickStartTree(const std::wstring& working);

} // namespace fixtures
