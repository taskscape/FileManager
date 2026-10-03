// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "scanner.h"
#include "text_util.h"

#include <deque>

namespace handoff
{

#ifndef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
#define FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS 0x00400000
#endif
#ifndef IO_REPARSE_TAG_CLOUD
#define IO_REPARSE_TAG_CLOUD 0x9000001AL
#endif

bool IsCloudReparseTag(DWORD tag)
{
    // IO_REPARSE_TAG_CLOUD_1..F differ only in bits 12-15.
    return (tag & 0xFFFF0FFFUL) == (DWORD)IO_REPARSE_TAG_CLOUD;
}

namespace
{

struct PendingDirectory
{
    std::wstring Full;
    std::wstring Rel;
    int Depth;
};

class Scanner
{
public:
    Scanner(const ScanInput& input, IProgress& progress, ScanResult& result)
        : Input(input), Progress(progress), Result(result)
    {
    }

    void Run()
    {
        if (Input.SelectionOnly)
        {
            for (const std::wstring& name : Input.SelectedNames)
            {
                if (!Step())
                    return;
                WIN32_FIND_DATAW data;
                std::wstring full = PathJoin(Input.WorkingRoot, name);
                HANDLE find = FindFirstFileExW(LongPath(full).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                               NULL, 0);
                if (find == INVALID_HANDLE_VALUE)
                {
                    Result.Findings.push_back(MakeFinding(L"HO-SCAN-001", name, {name, Win32ErrorText(GetLastError())}));
                    continue;
                }
                FindClose(find);
                Visit(data, Input.WorkingRoot, std::wstring(), 0);
            }
        }
        else
            Queue.push_back(PendingDirectory{Input.WorkingRoot, std::wstring(), 0});

        // Breadth-first and iterative: depth and entry limits bound the work
        // and no input can exhaust the stack.
        while (!Queue.empty())
        {
            PendingDirectory directory = Queue.front();
            Queue.pop_front();
            if (!EnumerateDirectory(directory))
                return;
        }
    }

private:
    const ScanInput& Input;
    IProgress& Progress;
    ScanResult& Result;
    std::deque<PendingDirectory> Queue;
    size_t Entries = 0;
    ULONGLONG LastReport = 0;

    bool Step()
    {
        Entries++;
        if (Entries > Input.MaxEntries)
        {
            if (!Result.LimitExceeded)
                Result.Findings.push_back(MakeFinding(L"HO-SCAN-004", L"", {NumberText((int64_t)Input.MaxEntries)}));
            Result.LimitExceeded = true;
            return false;
        }
        if ((Entries & 255) == 0)
        {
            if (Progress.StopRequested())
            {
                Result.Cancelled = true;
                return false;
            }
            ULONGLONG now = GetTickCount64();
            if (now - LastReport >= 100)
            {
                LastReport = now;
                Progress.Report(Phase::Scan, Entries, 0, std::wstring());
            }
        }
        return true;
    }

    bool EnumerateDirectory(const PendingDirectory& directory)
    {
        if (Progress.StopRequested())
        {
            Result.Cancelled = true;
            return false;
        }
        WIN32_FIND_DATAW data;
        std::wstring pattern = LongPath(PathJoin(directory.Full, L"*"));
        HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, NULL,
                                       FIND_FIRST_EX_LARGE_FETCH);
        if (find == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND)
                Result.Findings.push_back(MakeFinding(L"HO-SCAN-001", directory.Rel,
                                                      {directory.Rel.empty() ? L"." : directory.Rel, Win32ErrorText(error)}));
            return true;
        }
        do
        {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            if (!Step())
            {
                FindClose(find);
                return false;
            }
            Visit(data, directory.Full, directory.Rel, directory.Depth);
        } while (FindNextFileW(find, &data));
        DWORD error = GetLastError();
        FindClose(find);
        if (error != ERROR_NO_MORE_FILES)
            Result.Findings.push_back(MakeFinding(L"HO-SCAN-001", directory.Rel,
                                                  {directory.Rel.empty() ? L"." : directory.Rel, Win32ErrorText(error)}));
        return true;
    }

    void Visit(const WIN32_FIND_DATAW& data, const std::wstring& parentFull, const std::wstring& parentRel, int depth)
    {
        std::wstring name = data.cFileName;
        std::wstring rel = RelJoin(parentRel, name);
        bool reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        DWORD tag = reparse ? data.dwReserved0 : 0;
        bool cloudTag = reparse && IsCloudReparseTag(tag);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Specifications live in .handoff folders and are never delivered.
            if (EqualsNoCase(name, L".handoff"))
                return;
            if (reparse && !cloudTag)
            {
                // Junctions, symbolic links, and mount points are boundaries (architecture.md 5.2.1).
                Result.Findings.push_back(MakeFinding(L"HO-SCAN-002", rel, {rel}));
                return;
            }
            if (depth + 1 > Input.MaxDepth)
            {
                if (!Result.LimitExceeded)
                    Result.Findings.push_back(MakeFinding(L"HO-SCAN-004", rel, {L"depth " + NumberText(Input.MaxDepth)}));
                Result.LimitExceeded = true;
                return;
            }
            Queue.push_back(PendingDirectory{PathJoin(parentFull, name), rel, depth + 1});
            return;
        }
        if (reparse && !cloudTag)
        {
            Result.Findings.push_back(MakeFinding(L"HO-SCAN-003", rel, {rel}));
            return;
        }
        ScannedFile file;
        file.Rel = rel;
        file.Name = name;
        file.Size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
        file.Attributes = data.dwFileAttributes;
        file.LastWrite = data.ftLastWriteTime;
        file.CloudPlaceholder =
            (data.dwFileAttributes & (FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_OFFLINE)) != 0;
        Result.Files.push_back(file);
    }
};

} // namespace

ScanResult ScanWorkingMaterial(const ScanInput& input, IProgress& progress)
{
    ScanResult result;
    Scanner scanner(input, progress, result);
    scanner.Run();
    progress.Report(Phase::Scan, result.Files.size(), result.Files.size(), std::wstring());
    return result;
}

} // namespace handoff
