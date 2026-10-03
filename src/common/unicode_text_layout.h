// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <new>
#include <limits.h>
#include <string.h>

// Narrow UI strings are UTF-8; conversions must never silently substitute ACP characters.
inline bool Utf8TextToWide(const char* text, int length, std::wstring& wide)
{
    wide.clear();
    if (length == 0)
        return true;
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, NULL, 0);
    if (count <= 0)
        return false;
    try
    {
        wide.resize(count);
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, &wide[0], count) != count)
        {
            wide.clear();
            return false;
        }
        if (length == -1)
            wide.resize(count - 1);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool WideTextToUtf8(const wchar_t* text, int length, std::string& utf8)
{
    utf8.clear();
    if (length == 0)
        return true;
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, NULL, 0, NULL, NULL);
    if (count <= 0)
        return false;
    try
    {
        utf8.resize(count);
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, &utf8[0], count, NULL, NULL) != count)
        {
            utf8.clear();
            return false;
        }
        if (length == -1)
            utf8.resize(count - 1);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool Win32ErrorTextUtf8(DWORD error, DWORD language, std::string& text)
{
    // System messages use Unicode regardless of the host ACP; LocalFree owns only the Windows allocation.
    wchar_t* message = NULL;
    DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, error, language, (wchar_t*)&message, 0, NULL);
    text.clear();
    if (count == 0)
        return false;
    bool converted = WideTextToUtf8(message, static_cast<int>(count), text);
    LocalFree(message);
    if (converted)
        while (!text.empty() && (text.back() == '\r' || text.back() == '\n'))
            text.pop_back();
    return converted;
}

inline bool FormatUtf8Pair(const char* format, const char* first, const char* second, std::string& result)
{
    // Indexed resource insertions retain their locale-defined ordering without exposing UTF-8 to an ANSI formatter.
    result.clear();
    std::wstring wideFormat, wideFirst, wideSecond;
    if (!Utf8TextToWide(format, -1, wideFormat) || !Utf8TextToWide(first, -1, wideFirst) ||
        !Utf8TextToWide(second, -1, wideSecond))
        return false;
    DWORD_PTR args[2] = {reinterpret_cast<DWORD_PTR>(wideFirst.c_str()), reinterpret_cast<DWORD_PTR>(wideSecond.c_str())};
    wchar_t* message = NULL;
    DWORD count = FormatMessageW(FORMAT_MESSAGE_FROM_STRING | FORMAT_MESSAGE_ARGUMENT_ARRAY | FORMAT_MESSAGE_ALLOCATE_BUFFER,
                                 wideFormat.c_str(), 0, 0, (wchar_t*)&message, 0, reinterpret_cast<va_list*>(args));
    bool converted = count != 0 && WideTextToUtf8(message, static_cast<int>(count), result);
    LocalFree(message);
    return converted;
}

// GDI counts UTF-16 units. Keep every cut outside a supplementary character's surrogate pair.
inline size_t UnicodeBoundaryBefore(const std::wstring& text, size_t index)
{
    if (index > text.size())
        index = text.size();
    if (index > 0 && index < text.size() && text[index - 1] >= 0xd800 && text[index - 1] <= 0xdbff &&
        text[index] >= 0xdc00 && text[index] <= 0xdfff)
        --index;
    return index;
}

inline size_t UnicodeNextBoundary(const std::wstring& text, size_t index)
{
    if (index >= text.size())
        return text.size();
    ++index;
    if (index < text.size() && text[index - 1] >= 0xd800 && text[index - 1] <= 0xdbff &&
        text[index] >= 0xdc00 && text[index] <= 0xdfff)
        ++index;
    return index;
}

inline bool EllipsizeUnicodeText(HDC dc, const std::wstring& text, int width, bool path,
                                wchar_t separator, std::wstring& result, SIZE& size)
{
    if (text.size() > INT_MAX - 3)
        return false;
    try
    {
        if (!GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size))
            return false;
        result = text;
        if (size.cx <= width || text.empty())
            return true;
        std::vector<int> advances(text.size());
        if (!GetTextExtentExPointW(dc, text.c_str(), static_cast<int>(text.size()), 0, NULL, advances.data(), &size))
            return false;
        SIZE dots = {};
        if (!GetTextExtentPoint32W(dc, L"...", 3, &dots))
            return false;
        size_t suffix = text.size();
        if (path)
        {
            suffix = text.rfind(separator);
            if (suffix == std::wstring::npos)
                suffix = 0;
        }
        const auto prefixWidth = [&advances](size_t count) { return count == 0 ? 0 : advances[count - 1]; };
        // Preserve the last path component where possible, otherwise trim its left edge on character boundaries.
        while (suffix < text.size() && dots.cx + size.cx - prefixWidth(suffix) > width)
            suffix = UnicodeNextBoundary(text, suffix);
        size_t prefix = path ? suffix : text.size();
        if (path && suffix != text.rfind(separator))
            prefix = 0;
        while (prefix > 0 && prefixWidth(prefix) + dots.cx + size.cx - prefixWidth(suffix) > width)
            prefix = UnicodeBoundaryBefore(text, prefix - 1);
        result = text.substr(0, prefix) + L"..." + text.substr(suffix);
        if (!GetTextExtentPoint32W(dc, result.c_str(), static_cast<int>(result.size()), &size))
            return false;
        // Joining two runs can change font metrics; verify the actual displayed text rather than assuming additive widths.
        while (size.cx > width && (prefix > 0 || suffix < text.size()))
        {
            if (prefix > 0)
                prefix = UnicodeBoundaryBefore(text, prefix - 1);
            else
                suffix = UnicodeNextBoundary(text, suffix);
            result = text.substr(0, prefix) + L"..." + text.substr(suffix);
            if (!GetTextExtentPoint32W(dc, result.c_str(), static_cast<int>(result.size()), &size))
                return false;
        }
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}

inline bool TruncateUtf8Substring(HDC dc, const char* text, int start, int length,
                                  int width, bool messageBox, std::string& result)
{
    // Byte offsets belong to the UTF-8 interface; only decoded character positions may drive layout.
    const size_t bytes = strlen(text);
    if (start < 0 || length < 0 || static_cast<size_t>(start) > bytes || static_cast<size_t>(length) > bytes - start || bytes > INT_MAX - 3)
        return false;
    std::wstring prefix, body, suffix;
    if (!Utf8TextToWide(text, start, prefix) || !Utf8TextToWide(text + start, length, body) ||
        !Utf8TextToWide(text + start + length, -1, suffix))
        return false;
    try
    {
        const std::wstring original = prefix + body + suffix;
        SIZE size = {}, fixed = {}, dots = {};
        const std::wstring measured = messageBox ? body : original;
        if (measured.size() > INT_MAX || !GetTextExtentPoint32W(dc, measured.c_str(), static_cast<int>(measured.size()), &size))
            return false;
        if (size.cx <= width || body.empty())
            return WideTextToUtf8(original.c_str(), static_cast<int>(original.size()), result);
        if (!GetTextExtentPoint32W(dc, L"...", 3, &dots) ||
            !GetTextExtentPoint32W(dc, (prefix + suffix).c_str(), static_cast<int>(prefix.size() + suffix.size()), &fixed))
            return false;
        int available = width - dots.cx - (messageBox ? 0 : fixed.cx);
        int fit = 0;
        if (!GetTextExtentExPointW(dc, body.c_str(), static_cast<int>(body.size()), available > 0 ? available : 0, &fit, NULL, &size))
            return false;
        size_t keep = available > 0 ? UnicodeBoundaryBefore(body, fit) : 0;
        std::wstring shortened;
        do
        {
            shortened = prefix + body.substr(0, keep) + L"..." + suffix;
            const std::wstring displayed = messageBox ? body.substr(0, keep) + L"..." : shortened;
            if (!GetTextExtentPoint32W(dc, displayed.c_str(), static_cast<int>(displayed.size()), &size))
                return false;
            if (size.cx <= width || keep == 0)
                break;
            keep = UnicodeBoundaryBefore(body, keep - 1);
        } while (true);
        return WideTextToUtf8(shortened.c_str(), static_cast<int>(shortened.size()), result);
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}
