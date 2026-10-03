// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Text, path, size, and date helpers shared by the host-independent Handoff
// engine. The engine works in UTF-16 internally; UTF-8 appears only at file
// and SDK boundaries, so conversions here are strict and never lossy.

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace handoff
{

// Strict UTF-8 -> UTF-16; returns false (and an empty string) on invalid input.
bool Utf8ToWide(const char* data, size_t length, std::wstring& out);
bool Utf8ToWide(const std::string& text, std::wstring& out);
std::wstring Utf8ToWideLossy(const std::string& text);
// UTF-16 -> UTF-8; unpaired surrogates are replaced so output is always valid UTF-8.
std::string WideToUtf8(const std::wstring& text);
bool IsValidUtf8(const unsigned char* data, size_t length);

// Unicode-aware comparisons and keys (ordinal, invariant, NFC).
std::wstring NormalizeNfc(const std::wstring& text);
std::wstring UpperInvariant(const std::wstring& text);
std::wstring LowerInvariant(const std::wstring& text);
// Collision/sort key: NFC normalization followed by invariant upper-casing.
std::wstring FoldKey(const std::wstring& text);
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
// <0, 0, >0 like wcscmp, ordinal and case-insensitive.
int CompareNoCase(const std::wstring& a, const std::wstring& b);

std::wstring Trim(const std::wstring& text);
std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator);
std::wstring Join(const std::vector<std::wstring>& parts, const std::wstring& separator);
bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix);
bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix);
std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to);

// Replaces {0}, {1}, ... with arguments; positional placeholders keep
// translations free to reorder them (Polish word order differs from English).
std::wstring FormatPositional(const std::wstring& pattern, const std::vector<std::wstring>& args);

// Paths. Relative paths inside the engine always use '/' separators.
std::wstring ToSlashes(const std::wstring& path);
std::wstring ToBackslashes(const std::wstring& path);
std::wstring PathJoin(const std::wstring& left, const std::wstring& right); // Win32 '\'
std::wstring RelJoin(const std::wstring& left, const std::wstring& right);  // engine '/'
std::wstring FileNameOf(const std::wstring& path);
std::wstring ParentOf(const std::wstring& path);
std::wstring StemOf(const std::wstring& name);
std::wstring ExtensionOf(const std::wstring& name); // includes the dot, or empty
// Full path with "\\?\" (or "\\?\UNC\") so APIs are not limited to MAX_PATH.
std::wstring LongPath(const std::wstring& fullPath);
std::wstring StripLongPrefix(const std::wstring& path);
// Absolute normalized path without the long prefix; empty on failure.
std::wstring FullPathOf(const std::wstring& path);
// True when 'path' equals 'root' or lies below it on a segment boundary.
bool IsPathInsideOrEqual(const std::wstring& root, const std::wstring& path);
// Specification paths must be relative and stay inside their root (C.10.1).
bool IsSafeRelativePath(const std::wstring& relative);
// Windows-reserved device names (CON, PRN, AUX, NUL, COM1-9, LPT1-9) with any extension.
bool IsReservedDeviceName(const std::wstring& name);
bool HasInvalidWindowsNameChars(const std::wstring& name);

// Sizes: binary multiples, one decimal ("310 KB", "2.2 MB").
std::wstring FormatSize(uint64_t bytes);
// Accepts "123", "1 KB", "20 GB" (binary multiples, as Open Salamander displays sizes).
bool ParseSize(const std::wstring& text, uint64_t& bytes);

// Dates.
std::wstring FormatIsoUtc(const FILETIME& utc);  // 2026-10-03T14:22:05Z
std::wstring FormatDate(const SYSTEMTIME& time, const std::wstring& format); // yyyy MM dd HH mm
bool IsSupportedDateFormat(const std::wstring& format);
bool ParseIsoDate(const std::wstring& text, SYSTEMTIME& date); // yyyy-MM-dd
// Days from 'from' to 'to' (calendar dates only).
int64_t DaysBetween(const SYSTEMTIME& from, const SYSTEMTIME& to);

std::wstring ToHex(const unsigned char* data, size_t length);
std::wstring HResultText(long hr); // "0x80070005"
// Windows error message for 'error' (system language), without trailing line breaks.
std::wstring Win32ErrorText(DWORD error);
std::wstring NumberText(int64_t value);
std::wstring DecimalText(double value, int decimals);

// Levenshtein distance used for "did you mean" suggestions.
size_t EditDistance(const std::wstring& a, const std::wstring& b);

} // namespace handoff
