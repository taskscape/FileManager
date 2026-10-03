// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "text_util.h"

#include <stdio.h>
#include <wchar.h>
#include <algorithm>

namespace handoff
{

bool Utf8ToWide(const char* data, size_t length, std::wstring& out)
{
    out.clear();
    if (length == 0)
        return true;
    if (length > 0x7FFFFFFF)
        return false;
    // MB_ERR_INVALID_CHARS keeps malformed input visible instead of silently substituting.
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, (int)length, NULL, 0);
    if (needed <= 0)
        return false;
    out.resize((size_t)needed);
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data, (int)length, &out[0], needed) != needed)
    {
        out.clear();
        return false;
    }
    return true;
}

bool Utf8ToWide(const std::string& text, std::wstring& out)
{
    return Utf8ToWide(text.data(), text.size(), out);
}

std::wstring Utf8ToWideLossy(const std::string& text)
{
    std::wstring out;
    if (Utf8ToWide(text, out))
        return out;
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0);
    if (needed <= 0)
        return std::wstring();
    out.resize((size_t)needed);
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], needed);
    return out;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
    if (needed <= 0)
        return std::string();
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], needed, NULL, NULL);
    return out;
}

bool IsValidUtf8(const unsigned char* data, size_t length)
{
    if (length == 0)
        return true;
    if (length > 0x7FFFFFFF)
        return false;
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)data, (int)length, NULL, 0) > 0;
}

std::wstring NormalizeNfc(const std::wstring& text)
{
    if (text.empty())
        return text;
    int needed = NormalizeString(NormalizationC, text.c_str(), (int)text.size(), NULL, 0);
    for (int attempt = 0; attempt < 4 && needed > 0; attempt++)
    {
        std::wstring out((size_t)needed, L'\0');
        int written = NormalizeString(NormalizationC, text.c_str(), (int)text.size(), &out[0], needed);
        if (written > 0)
        {
            out.resize((size_t)written);
            return out;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;
        needed = -written; // the API returns a negative size estimate on a short buffer
    }
    // Invalid input (for example an unpaired surrogate) is compared as-is rather than lost.
    return text;
}

static std::wstring MapInvariant(const std::wstring& text, DWORD flags)
{
    if (text.empty())
        return text;
    int needed = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL, 0);
    if (needed <= 0)
        return text;
    std::wstring out((size_t)needed, L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, flags, text.c_str(), (int)text.size(), &out[0], needed, NULL, NULL, 0);
    return out;
}

std::wstring UpperInvariant(const std::wstring& text) { return MapInvariant(text, LCMAP_UPPERCASE); }
std::wstring LowerInvariant(const std::wstring& text) { return MapInvariant(text, LCMAP_LOWERCASE); }

std::wstring FoldKey(const std::wstring& text)
{
    return UpperInvariant(NormalizeNfc(text));
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b)
{
    return CompareNoCase(a, b) == 0;
}

int CompareNoCase(const std::wstring& a, const std::wstring& b)
{
    int r = CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE);
    if (r == 0)
        return a.compare(b);
    return r - CSTR_EQUAL;
}

std::wstring Trim(const std::wstring& text)
{
    size_t start = 0;
    while (start < text.size() && iswspace(text[start]))
        start++;
    size_t end = text.size();
    while (end > start && iswspace(text[end - 1]))
        end--;
    return text.substr(start, end - start);
}

std::vector<std::wstring> Split(const std::wstring& text, wchar_t separator)
{
    std::vector<std::wstring> parts;
    size_t start = 0;
    for (;;)
    {
        size_t pos = text.find(separator, start);
        if (pos == std::wstring::npos)
        {
            parts.push_back(text.substr(start));
            return parts;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

std::wstring Join(const std::vector<std::wstring>& parts, const std::wstring& separator)
{
    std::wstring out;
    for (size_t i = 0; i < parts.size(); i++)
    {
        if (i > 0)
            out += separator;
        out += parts[i];
    }
    return out;
}

bool StartsWithNoCase(const std::wstring& text, const std::wstring& prefix)
{
    return text.size() >= prefix.size() && EqualsNoCase(text.substr(0, prefix.size()), prefix);
}

bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix)
{
    return text.size() >= suffix.size() && EqualsNoCase(text.substr(text.size() - suffix.size()), suffix);
}

std::wstring ReplaceAll(std::wstring text, const std::wstring& from, const std::wstring& to)
{
    if (from.empty())
        return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::wstring::npos)
    {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

std::wstring FormatPositional(const std::wstring& pattern, const std::vector<std::wstring>& args)
{
    std::wstring out;
    out.reserve(pattern.size() + 32);
    for (size_t i = 0; i < pattern.size(); i++)
    {
        wchar_t c = pattern[i];
        if (c == L'{' && i + 2 < pattern.size() && iswdigit(pattern[i + 1]) && pattern[i + 2] == L'}')
        {
            size_t index = (size_t)(pattern[i + 1] - L'0');
            if (index < args.size())
                out += args[index];
            i += 2;
            continue;
        }
        out += c;
    }
    return out;
}

std::wstring ToSlashes(const std::wstring& path)
{
    std::wstring out(path);
    std::replace(out.begin(), out.end(), L'\\', L'/');
    return out;
}

std::wstring ToBackslashes(const std::wstring& path)
{
    std::wstring out(path);
    std::replace(out.begin(), out.end(), L'/', L'\\');
    return out;
}

std::wstring PathJoin(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
        return right;
    if (right.empty())
        return left;
    std::wstring out(left);
    if (out.back() != L'\\' && out.back() != L'/')
        out += L'\\';
    out += ToBackslashes(right);
    return out;
}

std::wstring RelJoin(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
        return right;
    if (right.empty())
        return left;
    return left + L"/" + right;
}

std::wstring FileNameOf(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring ParentOf(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

std::wstring StemOf(const std::wstring& name)
{
    std::wstring file = FileNameOf(name);
    size_t dot = file.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return file;
    return file.substr(0, dot);
}

std::wstring ExtensionOf(const std::wstring& name)
{
    std::wstring file = FileNameOf(name);
    size_t dot = file.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return std::wstring();
    return file.substr(dot);
}

std::wstring StripLongPrefix(const std::wstring& path)
{
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        return L"\\\\" + path.substr(8);
    if (path.compare(0, 4, L"\\\\?\\") == 0)
        return path.substr(4);
    return path;
}

std::wstring LongPath(const std::wstring& fullPath)
{
    if (fullPath.compare(0, 4, L"\\\\?\\") == 0)
        return fullPath;
    std::wstring path = ToBackslashes(fullPath);
    if (path.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}

std::wstring FullPathOf(const std::wstring& path)
{
    std::wstring input = StripLongPrefix(ToBackslashes(path));
    DWORD needed = GetFullPathNameW(input.c_str(), 0, NULL, NULL);
    if (needed == 0)
        return std::wstring();
    std::wstring out((size_t)needed, L'\0');
    DWORD written = GetFullPathNameW(input.c_str(), needed, &out[0], NULL);
    if (written == 0 || written >= needed)
        return std::wstring();
    out.resize(written);
    // A trailing separator is kept only for drive roots ("C:\").
    while (out.size() > 3 && out.back() == L'\\')
        out.pop_back();
    return out;
}

bool IsPathInsideOrEqual(const std::wstring& root, const std::wstring& path)
{
    std::wstring r = StripLongPrefix(root);
    std::wstring p = StripLongPrefix(path);
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/'))
        r.pop_back();
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    if (p.size() < r.size() || !EqualsNoCase(p.substr(0, r.size()), r))
        return false;
    if (p.size() == r.size())
        return true;
    // Segment boundary: "C:\Work" must not contain "C:\Workshop".
    wchar_t next = p[r.size()];
    return next == L'\\' || next == L'/' || r.back() == L'\\';
}

bool IsSafeRelativePath(const std::wstring& relative)
{
    if (relative.empty())
        return false;
    std::wstring path = ToSlashes(relative);
    if (path[0] == L'/' || path.find(L':') != std::wstring::npos)
        return false;
    for (const std::wstring& segment : Split(path, L'/'))
    {
        if (segment.empty() || segment == L"." || segment == L"..")
            return false;
    }
    return true;
}

bool IsReservedDeviceName(const std::wstring& name)
{
    std::wstring base = name;
    size_t dot = base.find(L'.');
    if (dot != std::wstring::npos)
        base = base.substr(0, dot);
    base = UpperInvariant(Trim(base));
    static const wchar_t* fixed[] = {L"CON", L"PRN", L"AUX", L"NUL"};
    for (const wchar_t* reserved : fixed)
        if (base == reserved)
            return true;
    if (base.size() == 4 && (base.compare(0, 3, L"COM") == 0 || base.compare(0, 3, L"LPT") == 0) &&
        base[3] >= L'1' && base[3] <= L'9')
        return true;
    return false;
}

bool HasInvalidWindowsNameChars(const std::wstring& name)
{
    for (wchar_t c : name)
    {
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c) != NULL)
            return true;
    }
    return false;
}

std::wstring DecimalText(double value, int decimals)
{
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.*f", decimals, value);
    return buffer;
}

std::wstring NumberText(int64_t value)
{
    wchar_t buffer[32];
    swprintf_s(buffer, L"%lld", (long long)value);
    return buffer;
}

std::wstring FormatSize(uint64_t bytes)
{
    static const wchar_t* units[] = {L"KB", L"MB", L"GB", L"TB"};
    if (bytes < 1024)
        return NumberText((int64_t)bytes) + L" B";
    double value = (double)bytes / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3)
    {
        value /= 1024.0;
        unit++;
    }
    // Kilobytes are whole numbers; larger units keep one decimal (C.7.3).
    if (unit == 0)
        return NumberText((int64_t)(value + 0.5)) + L" KB";
    return DecimalText(value, 1) + L" " + units[unit];
}

bool ParseSize(const std::wstring& text, uint64_t& bytes)
{
    std::wstring s = Trim(text);
    if (s.empty())
        return false;
    size_t pos = 0;
    double number = 0;
    bool any = false, fraction = false;
    double scale = 0.1;
    while (pos < s.size() && (iswdigit(s[pos]) || s[pos] == L'.'))
    {
        if (s[pos] == L'.')
        {
            if (fraction)
                return false;
            fraction = true;
        }
        else if (!fraction)
        {
            number = number * 10 + (s[pos] - L'0');
            any = true;
        }
        else
        {
            number += (s[pos] - L'0') * scale;
            scale /= 10;
            any = true;
        }
        pos++;
        if (number > 1e18)
            return false;
    }
    if (!any)
        return false;
    std::wstring unit = UpperInvariant(Trim(s.substr(pos)));
    double multiplier = 1;
    if (unit.empty() || unit == L"B")
        multiplier = 1;
    else if (unit == L"KB")
        multiplier = 1024.0;
    else if (unit == L"MB")
        multiplier = 1024.0 * 1024;
    else if (unit == L"GB")
        multiplier = 1024.0 * 1024 * 1024;
    else if (unit == L"TB")
        multiplier = 1024.0 * 1024 * 1024 * 1024;
    else
        return false;
    double result = number * multiplier;
    if (result < 0 || result > 9.0e18)
        return false;
    bytes = (uint64_t)(result + 0.5);
    return true;
}

std::wstring FormatIsoUtc(const FILETIME& utc)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&utc, &st))
        return std::wstring();
    wchar_t buffer[32];
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond);
    return buffer;
}

bool IsSupportedDateFormat(const std::wstring& format)
{
    for (size_t i = 0; i < format.size();)
    {
        if (format.compare(i, 4, L"yyyy") == 0)
            i += 4;
        else if (format.compare(i, 2, L"MM") == 0 || format.compare(i, 2, L"dd") == 0 ||
                 format.compare(i, 2, L"HH") == 0 || format.compare(i, 2, L"mm") == 0)
            i += 2;
        else if (iswalpha(format[i]))
            return false; // unknown letters are rejected so typos are caught at validation
        else
            i++;
    }
    return true;
}

std::wstring FormatDate(const SYSTEMTIME& time, const std::wstring& format)
{
    std::wstring out;
    wchar_t buffer[8];
    for (size_t i = 0; i < format.size();)
    {
        if (format.compare(i, 4, L"yyyy") == 0)
        {
            swprintf_s(buffer, L"%04u", time.wYear);
            out += buffer;
            i += 4;
        }
        else if (format.compare(i, 2, L"MM") == 0)
        {
            swprintf_s(buffer, L"%02u", time.wMonth);
            out += buffer;
            i += 2;
        }
        else if (format.compare(i, 2, L"dd") == 0)
        {
            swprintf_s(buffer, L"%02u", time.wDay);
            out += buffer;
            i += 2;
        }
        else if (format.compare(i, 2, L"HH") == 0)
        {
            swprintf_s(buffer, L"%02u", time.wHour);
            out += buffer;
            i += 2;
        }
        else if (format.compare(i, 2, L"mm") == 0)
        {
            swprintf_s(buffer, L"%02u", time.wMinute);
            out += buffer;
            i += 2;
        }
        else
            out += format[i++];
    }
    return out;
}

bool ParseIsoDate(const std::wstring& text, SYSTEMTIME& date)
{
    std::wstring s = Trim(text);
    if (s.size() != 10 || s[4] != L'-' || s[7] != L'-')
        return false;
    for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (!iswdigit(s[i]))
            return false;
    ZeroMemory(&date, sizeof(date));
    date.wYear = (WORD)_wtoi(s.substr(0, 4).c_str());
    date.wMonth = (WORD)_wtoi(s.substr(5, 2).c_str());
    date.wDay = (WORD)_wtoi(s.substr(8, 2).c_str());
    FILETIME ft;
    // SystemTimeToFileTime rejects impossible calendar dates such as 2026-02-30.
    return date.wMonth >= 1 && date.wMonth <= 12 && date.wDay >= 1 && SystemTimeToFileTime(&date, &ft) != FALSE;
}

int64_t DaysBetween(const SYSTEMTIME& from, const SYSTEMTIME& to)
{
    SYSTEMTIME a = {}, b = {};
    a.wYear = from.wYear, a.wMonth = from.wMonth, a.wDay = from.wDay;
    b.wYear = to.wYear, b.wMonth = to.wMonth, b.wDay = to.wDay;
    FILETIME fa, fb;
    if (!SystemTimeToFileTime(&a, &fa) || !SystemTimeToFileTime(&b, &fb))
        return 0;
    ULARGE_INTEGER ua, ub;
    ua.LowPart = fa.dwLowDateTime, ua.HighPart = fa.dwHighDateTime;
    ub.LowPart = fb.dwLowDateTime, ub.HighPart = fb.dwHighDateTime;
    const int64_t day = 864000000000LL;
    return ((int64_t)ub.QuadPart - (int64_t)ua.QuadPart) / day;
}

std::wstring ToHex(const unsigned char* data, size_t length)
{
    static const wchar_t digits[] = L"0123456789abcdef";
    std::wstring out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; i++)
    {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

std::wstring HResultText(long hr)
{
    wchar_t buffer[16];
    swprintf_s(buffer, L"0x%08lX", (unsigned long)hr);
    return buffer;
}

std::wstring Win32ErrorText(DWORD error)
{
    wchar_t* message = nullptr;
    DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                      FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, error, 0, (LPWSTR)&message, 0, NULL);
    std::wstring text = length > 0 && message != nullptr ? std::wstring(message, length) : std::wstring();
    if (message != nullptr)
        LocalFree(message);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'.'))
        text.pop_back();
    if (text.empty())
        text = L"error " + NumberText(error);
    return text;
}

size_t EditDistance(const std::wstring& a, const std::wstring& b)
{
    std::vector<size_t> previous(b.size() + 1), current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++)
        previous[j] = j;
    for (size_t i = 1; i <= a.size(); i++)
    {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); j++)
        {
            size_t cost = towlower(a[i - 1]) == towlower(b[j - 1]) ? 0 : 1;
            current[j] = (std::min)({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous.swap(current);
    }
    return previous[b.size()];
}

} // namespace handoff
