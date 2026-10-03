// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "util.h"

#include <bcrypt.h>
#include <objbase.h>
#include <stdio.h>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")

namespace reorg
{
namespace
{

bool IsLetter(wchar_t ch)
{
    return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z');
}

wchar_t Upper(wchar_t ch)
{
    if (ch >= L'a' && ch <= L'z')
        return (wchar_t)(ch - L'a' + L'A');
    return ch;
}

} // namespace

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), NULL, 0);
    if (need <= 0)
    {
        need = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0);
        if (need <= 0)
            return std::wstring();
    }
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], need);
    return out;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), NULL, 0, NULL, NULL);
    if (need <= 0)
        return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), &out[0], need, NULL, NULL);
    return out;
}

bool IsRootPath(const std::wstring& path)
{
    if (path.size() == 3 && IsLetter(path[0]) && path[1] == L':' && path[2] == L'\\')
        return true;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
    {
        // \\server\share is a root; a trailing component means it is not.
        int slashes = 0;
        for (size_t i = 2; i < path.size(); ++i)
        {
            if (path[i] == L'\\')
                ++slashes;
        }
        return slashes == 1 && path.back() != L'\\';
    }
    return false;
}

bool IsDiskPath(const std::wstring& path)
{
    if (path.size() >= 4 && path[0] == L'\\' && path[1] == L'\\' && path[2] == L'.' && path[3] == L'\\')
        return false;
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
        return true;
    return path.size() >= 3 && IsLetter(path[0]) && path[1] == L':' && path[2] == L'\\';
}

bool NormalizePath(const std::wstring& input, std::wstring& output, std::wstring& error)
{
    output.clear();
    error.clear();
    std::wstring text = input;
    if (text.rfind(L"\\\\?\\GLOBALROOT", 0) == 0 || text.rfind(L"\\\\.\\", 0) == 0)
    {
        error = L"device paths are not reorganization targets";
        return false;
    }
    if (text.rfind(L"\\\\?\\UNC\\", 0) == 0)
        text = L"\\\\" + text.substr(8);
    else if (text.rfind(L"\\\\?\\", 0) == 0)
    {
        if (text.size() < 7 || !IsLetter(text[4]) || text[5] != L':')
        {
            error = L"unsupported NT namespace path";
            return false;
        }
        text = text.substr(4);
    }
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'/')
            text[i] = L'\\';
    }
    bool unc = text.size() >= 2 && text[0] == L'\\' && text[1] == L'\\';
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (size_t i = unc ? 2 : 0; i <= text.size(); ++i)
    {
        if (i == text.size() || text[i] == L'\\')
        {
            if (!cur.empty())
            {
                if (cur == L".")
                {
                }
                else if (cur == L"..")
                {
                    if (parts.empty())
                    {
                        error = L"path escapes its root";
                        return false;
                    }
                    parts.pop_back();
                }
                else
                    parts.push_back(cur);
            }
            cur.clear();
        }
        else
            cur.push_back(text[i]);
    }
    if (!unc)
    {
        if (parts.empty() || parts[0].size() < 2 || !IsLetter(parts[0][0]) || parts[0][1] != L':')
        {
            error = L"path is not absolute";
            return false;
        }
        parts[0][0] = Upper(parts[0][0]);
        if (parts[0].size() != 2)
        {
            error = L"path is not absolute";
            return false;
        }
        output = parts[0] + L"\\";
        for (size_t i = 1; i < parts.size(); ++i)
        {
            if (i > 1)
                output.push_back(L'\\');
            output += parts[i];
        }
        if (parts.size() == 1)
            return true;
        return true;
    }
    if (parts.size() < 2)
    {
        error = L"UNC path needs a server and a share";
        return false;
    }
    output = L"\\\\";
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i)
            output.push_back(L'\\');
        output += parts[i];
    }
    return true;
}

std::wstring ParentPath(const std::wstring& path)
{
    if (IsRootPath(path))
        return std::wstring();
    size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return std::wstring();
    std::wstring parent = path.substr(0, pos);
    if (parent.size() == 2 && IsLetter(parent[0]) && parent[1] == L':')
        parent.push_back(L'\\');
    return parent;
}

std::wstring LeafName(const std::wstring& path)
{
    if (IsRootPath(path))
        return path;
    size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos)
        return path;
    return path.substr(pos + 1);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
    if (dir.empty())
        return name;
    if (name.empty())
        return dir;
    if (dir.size() >= 1 && dir.back() == L'\\')
        return dir + name;
    return dir + L"\\" + name;
}

bool IsUnderPath(const std::wstring& path, const std::wstring& root, bool caseSensitive)
{
    if (PathsEqual(path, root, caseSensitive))
        return true;
    std::wstring prefix = root;
    if (!prefix.empty() && prefix.back() != L'\\')
        prefix.push_back(L'\\');
    if (path.size() < prefix.size())
        return false;
    return NamesEqual(path.substr(0, prefix.size()), prefix, caseSensitive);
}

int ComparePaths(const std::wstring& a, const std::wstring& b, bool caseSensitive)
{
    return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), caseSensitive ? FALSE : TRUE) - 2;
}

bool PathsEqual(const std::wstring& a, const std::wstring& b, bool caseSensitive)
{
    return ComparePaths(a, b, caseSensitive) == 0;
}

bool NamesEqual(const std::wstring& a, const std::wstring& b, bool caseSensitive)
{
    return ComparePaths(a, b, caseSensitive) == 0;
}

std::wstring NewGuid()
{
    GUID id;
    if (CoCreateGuid(&id) != S_OK)
        return L"00000000-0000-0000-0000-000000000000";
    wchar_t buffer[40];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE,
                 L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                 id.Data1, id.Data2, id.Data3,
                 id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3],
                 id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
    return buffer;
}

std::wstring FileTimeToIso(unsigned __int64 fileTime)
{
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)fileTime;
    ft.dwHighDateTime = (DWORD)(fileTime >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&ft, &st);
    wchar_t buffer[40];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

std::wstring UtcNowIso()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    FILETIME ft;
    SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return FileTimeToIso(u.QuadPart);
}

bool IsoToFileTime(const std::wstring& iso, unsigned __int64& fileTime)
{
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (swscanf_s(iso.c_str(), L"%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6)
        return false;
    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = (WORD)day;
    st.wHour = (WORD)hour;
    st.wMinute = (WORD)minute;
    st.wSecond = (WORD)second;
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft))
        return false;
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    fileTime = u.QuadPart;
    return true;
}

std::wstring FormatFileId(const BYTE id[16])
{
    wchar_t buffer[40];
    for (int i = 0; i < 16; ++i)
        _snwprintf_s(buffer + i * 2, 40 - i * 2, _TRUNCATE, L"%02X", id[i]);
    return buffer;
}

bool ParseFileId(const std::wstring& text, BYTE id[16])
{
    if (text.size() != 32)
        return false;
    memset(id, 0, 16);
    for (int i = 0; i < 16; ++i)
    {
        unsigned value = 0;
        if (swscanf_s(text.c_str() + i * 2, L"%02x", &value) != 1)
            return false;
        id[i] = (BYTE)value;
    }
    return true;
}

std::wstring FormatVolumeSerial(DWORD serial)
{
    wchar_t buffer[16];
    _snwprintf_s(buffer, _countof(buffer), _TRUNCATE, L"0x%08X", serial);
    return buffer;
}

bool ParseVolumeSerial(const std::wstring& text, DWORD& serial)
{
    unsigned value = 0;
    if (swscanf_s(text.c_str(), L"0x%x", &value) != 1 && swscanf_s(text.c_str(), L"%x", &value) != 1)
        return false;
    serial = value;
    return true;
}

std::wstring Sha256Hex(const std::string& data)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD objectLength = 0, returned = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0)
        return std::wstring();
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (BYTE*)&objectLength, sizeof(objectLength), &returned, 0) < 0)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::wstring();
    }
    std::vector<BYTE> object(objectLength);
    BYTE digest[32];
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, NULL, 0, 0) < 0 ||
        BCryptHashData(hash, (PUCHAR)data.data(), (ULONG)data.size(), 0) < 0 ||
        BCryptFinishHash(hash, digest, 32, 0) < 0)
    {
        if (hash)
            BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::wstring();
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    wchar_t hex[65];
    for (int i = 0; i < 32; ++i)
        _snwprintf_s(hex + i * 2, 65 - i * 2, _TRUNCATE, L"%02x", digest[i]);
    return hex;
}

std::wstring Sha256HexOfWide(const std::wstring& data)
{
    return Sha256Hex(WideToUtf8(data));
}

DWORD Crc32(const char* data, size_t length)
{
    // Same reflected polynomial as src/common/crc32.cpp, so journal CRCs match the host.
    static DWORD table[256];
    static bool ready = false;
    if (!ready)
    {
        for (DWORD n = 0; n < 256; ++n)
        {
            DWORD c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xedb88320L ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        ready = true;
    }
    DWORD c = 0xffffffffL;
    for (size_t i = 0; i < length; ++i)
        c = table[(c ^ (BYTE)data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffL;
}

std::wstring SplitName(const std::wstring& fileName, std::wstring& ext)
{
    size_t pos = fileName.find_last_of(L'.');
    if (pos == std::wstring::npos || pos == 0)
    {
        ext.clear();
        return fileName;
    }
    ext = fileName.substr(pos);
    return fileName.substr(0, pos);
}

namespace
{

bool AgreeMaskRec(const wchar_t* filename, const wchar_t* maskPtr, bool hasExtension)
{
    // Core re-implementation of AgreeMask (src/masks.cpp): '?' is one character,
    // '*' is a sequence, and a file with no extension still matches "*.".
    while (*filename != 0)
    {
        if (*maskPtr == 0)
            return false;
        if (Upper(*filename) == Upper(*maskPtr) || *maskPtr == L'?')
        {
            ++filename;
            ++maskPtr;
        }
        else if (*maskPtr == L'*')
        {
            ++maskPtr;
            while (*filename != 0)
            {
                if (AgreeMaskRec(filename, maskPtr, hasExtension))
                    return true;
                ++filename;
            }
            break;
        }
        else
            return false;
    }
    if (*maskPtr == L'*')
        ++maskPtr;
    if (!hasExtension && *maskPtr == L'.')
        return *(maskPtr + 1) == 0 || (*(maskPtr + 1) == L'*' && *(maskPtr + 2) == 0);
    return *maskPtr == 0;
}

} // namespace

bool AgreeMaskWide(const std::wstring& fileName, const std::wstring& mask, bool hasExtension)
{
    return AgreeMaskRec(fileName.c_str(), mask.c_str(), hasExtension);
}

namespace
{

bool GlobRec(const wchar_t* text, const wchar_t* glob)
{
    while (*glob != 0)
    {
        if (glob[0] == L'*' && glob[1] == L'*')
        {
            glob += 2;
            if (*glob == L'\\')
                ++glob;
            if (*glob == 0)
                return true;
            for (const wchar_t* p = text;; ++p)
            {
                if (GlobRec(p, glob))
                    return true;
                if (*p == 0)
                    return false;
            }
        }
        if (*glob == L'*')
        {
            ++glob;
            for (const wchar_t* p = text;; ++p)
            {
                if (GlobRec(p, glob))
                    return true;
                if (*p == 0 || *p == L'\\')
                    return false;
            }
        }
        if (*text == 0)
            return false;
        if (*glob == L'?')
        {
            if (*text == L'\\')
                return false;
            ++text;
            ++glob;
            continue;
        }
        if (Upper(*text) != Upper(*glob))
            return false;
        ++text;
        ++glob;
    }
    return *text == 0;
}

} // namespace

bool MatchGlob(const std::wstring& relativePath, const std::wstring& glob)
{
    if (glob.empty())
        return true;
    return GlobRec(relativePath.c_str(), glob.c_str());
}

bool IsReservedDeviceName(const std::wstring& name)
{
    std::wstring stem = name;
    size_t dot = stem.find(L'.');
    if (dot != std::wstring::npos)
        stem = stem.substr(0, dot);
    for (size_t i = 0; i < stem.size(); ++i)
        stem[i] = Upper(stem[i]);
    // Superscript digits are the same reserved names as ASCII digits (DST-006).
    for (size_t i = 0; i < stem.size(); ++i)
    {
        if (stem[i] >= 0x2070 && stem[i] <= 0x2079)
            stem[i] = (wchar_t)(L'0' + (stem[i] - 0x2070));
        else if (stem[i] == 0x00B9)
            stem[i] = L'1';
        else if (stem[i] == 0x00B2)
            stem[i] = L'2';
        else if (stem[i] == 0x00B3)
            stem[i] = L'3';
    }
    if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL")
        return true;
    if (stem.size() == 4 && (stem.rfind(L"COM", 0) == 0 || stem.rfind(L"LPT", 0) == 0) && stem[3] >= L'0' && stem[3] <= L'9')
        return true;
    return false;
}

bool IsInvalidTargetName(const std::wstring& name, const std::wstring& fileSystem, std::wstring& reason)
{
    reason.clear();
    if (name.empty() || name == L"." || name == L"..")
    {
        reason = L"empty name";
        return true;
    }
    if (name.back() == L' ' || name.back() == L'.')
    {
        reason = L"trailing space or dot";
        return true;
    }
    const wchar_t* illegal = L"<>:\"/\\|?*";
    for (size_t i = 0; i < name.size(); ++i)
    {
        if (name[i] < 0x20 || wcschr(illegal, name[i]) != NULL)
        {
            reason = L"illegal character";
            return true;
        }
    }
    if (IsReservedDeviceName(name))
    {
        reason = L"reserved device name";
        return true;
    }
    std::wstring fs = fileSystem;
    for (size_t i = 0; i < fs.size(); ++i)
        fs[i] = Upper(fs[i]);
    if (fs.find(L"FAT") != std::wstring::npos)
    {
        if (name.find(L'+') != std::wstring::npos || name.find(L',') != std::wstring::npos || name.find(L';') != std::wstring::npos || name.find(L'=') != std::wstring::npos || name.find(L'[') != std::wstring::npos || name.find(L']') != std::wstring::npos)
        {
            reason = L"character not allowed on FAT";
            return true;
        }
    }
    return false;
}

std::string PercentEncode(const std::string& value)
{
    std::string out;
    for (size_t i = 0; i < value.size(); ++i)
    {
        unsigned char ch = (unsigned char)value[i];
        if (ch == '%' || ch == '|' || ch == '\r' || ch == '\n')
        {
            char buf[8];
            _snprintf_s(buf, _countof(buf), _TRUNCATE, "%%%02X", ch);
            out += buf;
        }
        else
            out.push_back((char)ch);
    }
    return out;
}

bool PercentDecode(const std::string& value, std::string& output)
{
    output.clear();
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '%')
        {
            if (i + 2 >= value.size())
                return false;
            unsigned hex = 0;
            if (sscanf_s(value.c_str() + i + 1, "%02x", &hex) != 1)
                return false;
            output.push_back((char)hex);
            i += 2;
        }
        else
            output.push_back(value[i]);
    }
    return true;
}

std::wstring Trim(const std::wstring& text)
{
    size_t begin = 0;
    while (begin < text.size() && iswspace(text[begin]))
        ++begin;
    size_t end = text.size();
    while (end > begin && iswspace(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

std::string TrimAscii(const std::string& text)
{
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' || text[begin] == '\n'))
        ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' || text[end - 1] == '\n'))
        --end;
    return text.substr(begin, end - begin);
}

} // namespace reorg
