// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Format detection by extension and content signature (C.4.7). Every
// extension maps to exactly one format; the signature then confirms that the
// content really is what the extension claims.

#include <string>
#include <vector>

namespace handoff
{

struct MagicBytes
{
    size_t Offset = 0;
    std::vector<unsigned char> Bytes;
};

struct FormatDef
{
    std::wstring Id;
    std::wstring Label;
    std::vector<std::wstring> Extensions; // lower-case, with the dot
    std::vector<MagicBytes> Magic;        // custom formats: alternatives
    bool Text = false;                    // custom formats: apply the text check
    bool Raster = false;
    bool BuiltIn = true;
};

const std::vector<FormatDef>& BuiltInFormats();
bool IsBuiltInFormat(const std::wstring& id);
bool IsRasterFormat(const std::wstring& id);
// Looks in custom definitions first, then built-ins; nullptr when unknown.
const FormatDef* FindFormat(const std::vector<FormatDef>& custom, const std::wstring& id);
const FormatDef* FormatForExtension(const std::vector<FormatDef>& custom, const std::wstring& extension);
std::wstring FormatLabel(const std::vector<FormatDef>& custom, const std::wstring& id);

const size_t FormatHeadBytes = 4096;

struct FormatDetection
{
    std::wstring Id;         // "unknown" when the extension maps to no format
    bool Known = false;
    bool SignatureOk = true; // false: content does not match the extension (HO-FMT-002)
};

// 'head' holds up to FormatHeadBytes from the start of the file; an empty file
// is never reported as a signature mismatch (HO-CONT-004 covers it).
FormatDetection DetectFormat(const std::wstring& fileName, const unsigned char* head, size_t headLength,
                             const std::vector<FormatDef>& custom);

bool LooksLikeText(const unsigned char* head, size_t headLength);

} // namespace handoff
