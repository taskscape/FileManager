// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "types.h"

namespace reorg
{

std::wstring Utf8ToWide(const std::string& text);
std::string WideToUtf8(const std::wstring& text);

// Storage form: backslashes, no trailing slash except roots, no dot segments,
// upper-case drive letter, and the \\?\ prefix removed. Device paths are rejected.
bool NormalizePath(const std::wstring& input, std::wstring& output, std::wstring& error);
bool IsDiskPath(const std::wstring& path);
std::wstring ParentPath(const std::wstring& path);
std::wstring LeafName(const std::wstring& path);
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);
bool IsRootPath(const std::wstring& path);
bool IsUnderPath(const std::wstring& path, const std::wstring& root, bool caseSensitive);

int ComparePaths(const std::wstring& a, const std::wstring& b, bool caseSensitive);
bool PathsEqual(const std::wstring& a, const std::wstring& b, bool caseSensitive);
bool NamesEqual(const std::wstring& a, const std::wstring& b, bool caseSensitive);

std::wstring NewGuid();
std::wstring UtcNowIso();
std::wstring FileTimeToIso(unsigned __int64 fileTime);
bool IsoToFileTime(const std::wstring& iso, unsigned __int64& fileTime);
std::wstring FormatFileId(const BYTE id[16]);
bool ParseFileId(const std::wstring& text, BYTE id[16]);
std::wstring FormatVolumeSerial(DWORD serial);
bool ParseVolumeSerial(const std::wstring& text, DWORD& serial);

std::wstring Sha256Hex(const std::string& data);
std::wstring Sha256HexOfWide(const std::wstring& data);
DWORD Crc32(const char* data, size_t length);

std::wstring SplitName(const std::wstring& fileName, std::wstring& ext);
bool AgreeMaskWide(const std::wstring& fileName, const std::wstring& mask, bool hasExtension);
bool MatchGlob(const std::wstring& relativePath, const std::wstring& glob);

bool IsReservedDeviceName(const std::wstring& name);
bool IsInvalidTargetName(const std::wstring& name, const std::wstring& fileSystem, std::wstring& reason);

std::string PercentEncode(const std::string& value);
bool PercentDecode(const std::string& value, std::string& output);

std::wstring Trim(const std::wstring& text);
std::string TrimAscii(const std::string& text);

} // namespace reorg
