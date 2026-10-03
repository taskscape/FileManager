// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "inspect.h"
#include "text_util.h"

#include <ctype.h>
#include <string.h>

namespace handoff
{

HANDLE OpenForInspection(const std::wstring& path)
{
    return CreateFileW(LongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
}

static bool ReadAt(HANDLE file, uint64_t offset, size_t length, std::vector<unsigned char>& out)
{
    out.resize(length);
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN))
        return false;
    size_t done = 0;
    while (done < length)
    {
        DWORD read = 0;
        if (!ReadFile(file, out.data() + done, (DWORD)(length - done), &read, NULL))
            return false;
        if (read == 0)
            break;
        done += read;
    }
    out.resize(done);
    return true;
}

bool ReadHead(const std::wstring& path, size_t maximum, std::vector<unsigned char>& head, DWORD& error)
{
    head.clear();
    HANDLE file = OpenForInspection(path);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    bool ok = ReadAt(file, 0, maximum, head);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    return ok;
}

bool ReadWholeFile(const std::wstring& path, size_t maximum, std::vector<unsigned char>& data, DWORD& error)
{
    data.clear();
    HANDLE file = OpenForInspection(path);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE;
    if (ok && (uint64_t)size.QuadPart > maximum)
    {
        CloseHandle(file);
        error = ERROR_FILE_TOO_LARGE;
        return false;
    }
    ok = ok && ReadAt(file, 0, (size_t)size.QuadPart, data);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    return ok;
}

bool HasNamedStreams(const std::wstring& path)
{
    WIN32_FIND_STREAM_DATA stream;
    HANDLE find = FindFirstStreamW(LongPath(path).c_str(), FindStreamInfoStandard, &stream, 0);
    if (find == INVALID_HANDLE_VALUE)
        return false;
    bool named = false;
    do
    {
        // "::$DATA" is the default stream; anything else (Zone.Identifier, ...) is named.
        if (wcscmp(stream.cStreamName, L"::$DATA") != 0)
            named = true;
    } while (!named && FindNextStreamW(find, &stream));
    FindClose(find);
    return named;
}

static const unsigned char* FindBytes(const std::vector<unsigned char>& data, size_t from, size_t to, const char* text)
{
    size_t n = strlen(text);
    if (to > data.size())
        to = data.size();
    for (size_t i = from; i + n <= to; i++)
        if (memcmp(data.data() + i, text, n) == 0)
            return data.data() + i;
    return nullptr;
}

PdfStructure InspectPdfStructure(const std::wstring& path)
{
    // Bounded scan (C.5.4.2): the first and last 64 KiB are enough for the
    // header, trailer, encryption dictionary reference, and linearization hint.
    PdfStructure result;
    HANDLE file = OpenForInspection(path);
    if (file == INVALID_HANDLE_VALUE)
    {
        result.Error = GetLastError();
        return result;
    }
    LARGE_INTEGER size = {};
    GetFileSizeEx(file, &size);
    const size_t window = 64 * 1024;
    std::vector<unsigned char> head, tail;
    bool ok = ReadAt(file, 0, window, head);
    uint64_t tailStart = (uint64_t)size.QuadPart > window ? (uint64_t)size.QuadPart - window : 0;
    ok = ok && ReadAt(file, tailStart, window, tail);
    if (!ok)
        result.Error = GetLastError();
    CloseHandle(file);
    if (!ok)
        return result;

    const unsigned char* header = FindBytes(head, 0, 1024, "%PDF-");
    if (header != nullptr)
    {
        size_t offset = (size_t)(header - head.data()) + 5;
        if (offset + 3 <= head.size() && isdigit(head[offset]) && head[offset + 1] == '.' && isdigit(head[offset + 2]))
            result.Version = std::wstring{(wchar_t)head[offset], L'.', (wchar_t)head[offset + 2]};
    }
    // Trailing whitespace and NUL padding after %%EOF is common and tolerated.
    size_t end = tail.size();
    while (end > 0 && (tail[end - 1] == 0 || isspace(tail[end - 1])))
        end--;
    size_t trailerFrom = end > 1024 ? end - 1024 : 0;
    bool hasEof = FindBytes(tail, trailerFrom, end, "%%EOF") != nullptr;
    bool hasStartXref = FindBytes(tail, trailerFrom, end, "startxref") != nullptr;
    result.Valid = header != nullptr && !result.Version.empty() && hasEof && hasStartXref;
    result.Encrypted = FindBytes(tail, 0, tail.size(), "/Encrypt") != nullptr;
    result.Linearized = FindBytes(head, 0, 1024, "/Linearized") != nullptr;
    return result;
}

} // namespace handoff
