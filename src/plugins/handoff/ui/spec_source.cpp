// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "file_system.h"
#include "inspect.h"
#include "json.h"
#include "sdk_strings.h"
#include "spec_source.h"
#include "text_util.h"

namespace
{

bool IsDirectory(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(handoff::LongPath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFile(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(handoff::LongPath(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void AddChoice(std::vector<SpecChoice>& choices, const std::wstring& path, int originId)
{
    for (const SpecChoice& existing : choices)
        if (handoff::EqualsNoCase(existing.Path, path))
            return; // duplicates by full path keep their first (highest-priority) origin
    choices.push_back(SpecChoice{path, originId});
}

} // namespace

std::vector<std::wstring> ListSpecFiles(const std::wstring& folder)
{
    std::vector<std::wstring> files;
    if (folder.empty())
        return files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(handoff::LongPath(handoff::PathJoin(folder, L"*.handoff.json")).c_str(), FindExInfoBasic,
                                   &data, FindExSearchNameMatch, NULL, 0);
    if (find == INVALID_HANDLE_VALUE)
        return files;
    do
    {
        // Links are not followed: a specification must be a plain file in the folder.
        if ((data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
            handoff::EndsWithNoCase(data.cFileName, L".handoff.json"))
            files.push_back(handoff::PathJoin(folder, data.cFileName));
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(files.begin(), files.end(),
              [](const std::wstring& a, const std::wstring& b) { return handoff::CompareNoCase(a, b) < 0; });
    return files;
}

std::vector<SpecChoice> DiscoverSpecs(const std::wstring& workingRoot)
{
    std::vector<SpecChoice> choices;
    // 1. The nearest .handoff folder from the working root up to its volume root.
    std::wstring folder = handoff::StripLongPrefix(handoff::FullPathOf(workingRoot));
    for (int depth = 0; !folder.empty() && depth < 256; depth++)
    {
        std::wstring candidate = handoff::PathJoin(folder, L".handoff");
        if (IsDirectory(candidate))
        {
            for (const std::wstring& path : ListSpecFiles(candidate))
                AddChoice(choices, path, IDS_SPEC_ORIGIN_PROJECT);
            break;
        }
        std::wstring parent = handoff::ParentOf(folder);
        if (parent.empty() || handoff::EqualsNoCase(parent, folder))
            break;
        folder = parent;
    }
    // 2. The configured library folder.
    HandoffConfig config = GetConfig();
    for (const std::wstring& path : ListSpecFiles(config.SpecLibrary))
        AddChoice(choices, path, IDS_SPEC_ORIGIN_LIBRARY);
    // 3. Recent specifications that still exist; missing ones are pruned.
    std::vector<std::wstring> missing;
    for (const std::wstring& path : config.RecentSpecs)
    {
        if (IsFile(path))
            AddChoice(choices, path, IDS_SPEC_ORIGIN_RECENT);
        else
            missing.push_back(path);
    }
    if (!missing.empty())
        UpdateConfig([&missing](HandoffConfig& current) {
            auto& recent = current.RecentSpecs;
            recent.erase(std::remove_if(recent.begin(), recent.end(),
                                        [&missing](const std::wstring& item) {
                                            for (const std::wstring& gone : missing)
                                                if (handoff::EqualsNoCase(gone, item))
                                                    return true;
                                            return false;
                                        }),
                         recent.end());
        });
    return choices;
}

LoadedSpec LoadSpecText(const std::wstring& path, const std::string& utf8)
{
    LoadedSpec spec;
    spec.Path = path;
    spec.Read = true;
    spec.Bytes.assign(utf8.begin(), utf8.end());
    spec.Result = handoff::ParseSpecification(spec.Bytes.data(), spec.Bytes.size(), Catalog());
    spec.Result.Model.Sha256 = handoff::Sha256Hex(spec.Bytes.data(), spec.Bytes.size());
    spec.Text = handoff::Utf8ToWideLossy(utf8);
    return spec;
}

LoadedSpec LoadSpecFile(const std::wstring& path)
{
    LoadedSpec spec;
    spec.Path = path;
    size_t limit = handoff::SpecJsonLimits().MaxBytes;
    if (!handoff::ReadWholeFile(path, limit, spec.Bytes, spec.Error))
    {
        if (spec.Error == ERROR_FILE_TOO_LARGE)
        {
            // Too large to parse safely: reported like any other limit (C.4.1).
            spec.Read = true;
            spec.Result.Findings.push_back(
                handoff::MakeFinding(L"HO-SPEC-006", L"", {handoff::FormatSize((uint64_t)limit)}));
        }
        return spec;
    }
    spec.Read = true;
    spec.Result = handoff::ParseSpecification(spec.Bytes.data(), spec.Bytes.size(), Catalog());
    // The hash covers the exact bytes, so verification can detect any later edit.
    spec.Result.Model.Sha256 = handoff::Sha256Hex(spec.Bytes.data(), spec.Bytes.size());
    spec.Text = handoff::Utf8ToWideLossy(std::string(spec.Bytes.begin(), spec.Bytes.end()));
    return spec;
}

std::wstring SpecSummary(const LoadedSpec& spec)
{
    if (!spec.Read)
        return TextF(IDS_READ_FAILED, {handoff::FileNameOf(spec.Path), handoff::Win32ErrorText(spec.Error)});
    for (const handoff::Finding& finding : spec.Result.Findings)
        if (finding.Sev == handoff::Severity::Error)
        {
            std::wstring where = finding.Line > 0 ? L" (" + std::to_wstring(finding.Line) + L":" + std::to_wstring(finding.Column) + L")" : L"";
            return TextF(IDS_SPEC_INVALID, {finding.Code + where + L" " + handoff::FindingMessage(finding, Catalog())});
        }
    const handoff::Spec& model = spec.Result.Model;
    std::wstring first = model.Name;
    if (!model.Description.empty())
        first += L": " + model.Description;
    return first + L"\r\n" + TextF(IDS_SPEC_SUMMARY, {std::to_wstring(model.Rules.size()), model.Revision});
}
