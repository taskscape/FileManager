// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "validate.h"

namespace reorg
{
namespace
{

CIssue MakeIssue(const wchar_t* code, EIssueSeverity severity, const std::vector<std::wstring>& paths, const std::wstring& text)
{
    CIssue issue;
    issue.Code = code;
    issue.Severity = severity;
    issue.Nodes = paths;
    issue.Text = text;
    issue.Key = MakeIssueKey(code, paths);
    return issue;
}

void AddRes(CIssue& issue, EResolutionChoice choice)
{
    issue.Resolutions.push_back(choice);
}

bool Acknowledged(const CPlanDocument& plan, const std::wstring& key)
{
    for (size_t i = 0; i < plan.Acknowledgements.size(); ++i)
    {
        if (plan.Acknowledgements[i].IssueKey == key)
            return true;
    }
    return false;
}

void CheckCollisions(const CAnalysisContext& context, CIssueSink& sink)
{
    std::map<std::wstring, std::vector<std::wstring>> byPath;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = context.Overlay->Nodes.begin(); it != context.Overlay->Nodes.end(); ++it)
    {
        if (it->second.Displaced || it->second.ProposedPath.empty())
            continue;
        std::wstring key = it->second.ProposedPath;
        const COverlayNode* parent = context.Overlay->FindKey(it->second.ParentKey);
        bool sensitive = parent && parent->Info.CaseSensitive;
        if (!sensitive)
        {
            for (size_t i = 0; i < key.size(); ++i)
            {
                if (key[i] >= L'A' && key[i] <= L'Z')
                    key[i] = (wchar_t)(key[i] - L'A' + L'a');
                else if (key[i] >= L'a' && key[i] <= L'z')
                    key[i] = key[i];
            }
        }
        byPath[key].push_back(it->first);
        if (parent && parent->Info.CaseSensitive)
        {
            // COL-003 is reported from the case-sensitive sibling scan below.
        }
    }
    for (std::map<std::wstring, std::vector<std::wstring>>::const_iterator it = byPath.begin(); it != byPath.end(); ++it)
    {
        if (it->second.size() < 2)
            continue;
        std::vector<std::wstring> paths;
        bool caseOnly = true;
        std::wstring firstName;
        for (size_t i = 0; i < it->second.size(); ++i)
        {
            const COverlayNode* node = context.Overlay->FindKey(it->second[i]);
            if (node == NULL)
                continue;
            paths.push_back(node->ProposedPath);
            if (firstName.empty())
                firstName = node->Name;
            else if (node->Name != firstName)
                caseOnly = false;
        }
        const COverlayNode* sample = context.Overlay->FindKey(it->second[0]);
        const COverlayNode* parent = sample ? context.Overlay->FindKey(sample->ParentKey) : NULL;
        if (caseOnly && parent && parent->Info.CaseSensitive)
        {
            CIssue issue = MakeIssue(L"COL-003", SevWarning, paths, L"Names differ only by case.");
            AddRes(issue, ResRename);
            sink.Add(issue);
        }
        else
        {
            CIssue issue = MakeIssue(L"COL-001", SevError, paths, L"Two items end at the same proposed path.");
            AddRes(issue, ResKeepBoth);
            AddRes(issue, ResRename);
            AddRes(issue, ResSkip);
            sink.Add(issue);
        }
    }
    for (std::map<std::wstring, COverlayNode>::const_iterator it = context.Overlay->Nodes.begin(); it != context.Overlay->Nodes.end(); ++it)
    {
        if (it->second.Synthetic || it->second.Displaced || it->second.Change == ChangeUnchanged || it->second.Change == ChangeContains || it->second.Change == ChangeMovedWithFolder)
            continue;
        const CSnapshotItem* occupant = context.Snapshot->Find(it->second.ProposedPath);
        if (occupant == NULL)
            continue;
        const COverlayNode* occupantNode = context.Overlay->FindKey(occupant->Path);
        // A displaced occupant goes to the recovery store before the incoming move (replace).
        if (occupantNode && occupantNode->Displaced)
            continue;
        if (occupantNode && occupantNode->Change != ChangeUnchanged && occupantNode->Change != ChangeContains && occupantNode->Change != ChangeMovedWithFolder)
            continue;
        if (PathsEqual(occupant->Path, it->second.OriginalPath, false))
            continue;
        std::vector<std::wstring> paths;
        paths.push_back(it->second.OriginalPath);
        paths.push_back(it->second.ProposedPath);
        CIssue issue = MakeIssue(L"COL-002", SevError, paths, L"The proposed path is already occupied.");
        AddRes(issue, ResKeepBoth);
        AddRes(issue, ResSkip);
        AddRes(issue, ResReplace);
        AddRes(issue, ResRename);
        if (it->second.IsDir && occupant->IsDir)
            AddRes(issue, ResMerge);
        // BuildOverlay already applied saved resolutions and the conflict default. A conflict that is
        // still present was not resolved by them (for example merge onto a file), so it keeps blocking.
        sink.Add(issue);
    }
    for (std::map<std::wstring, COverlayNode>::const_iterator it = context.Overlay->Nodes.begin(); it != context.Overlay->Nodes.end(); ++it)
    {
        if (it->second.ResolutionNote != L"mergedInto")
            continue;
        std::vector<std::wstring> paths;
        paths.push_back(it->second.OriginalPath);
        paths.push_back(it->second.ResolutionTarget);
        sink.Add(MakeIssue(L"COL-006", SevWarning, paths, L"Folders will be merged."));
    }
}

void CheckDestinations(const CAnalysisContext& context, CIssueSink& sink)
{
    for (std::map<std::wstring, COverlayNode>::const_iterator it = context.Overlay->Nodes.begin(); it != context.Overlay->Nodes.end(); ++it)
    {
        const COverlayNode& node = it->second;
        if (node.Change == ChangeUnchanged || node.Change == ChangeContains)
            continue;
        // node.Info describes the item where it is now. Name, read-only, and size limits depend on the
        // folder that will hold it, which may be synthetic or moved, so walk to the real location.
        const COverlayNode* location = FindLocationNode(*context.Overlay, ParentPath(node.ProposedPath));
        const CSnapshotItem& destination = location != NULL ? location->Info : node.Info;
        std::wstring reason;
        if (!node.Name.empty() && IsInvalidTargetName(node.Name, destination.FileSystem, reason))
        {
            std::vector<std::wstring> paths;
            paths.push_back(node.ProposedPath);
            CIssue issue = MakeIssue(L"DST-006", SevError, paths, reason);
            AddRes(issue, ResRename);
            sink.Add(issue);
        }
        if (node.ProposedPath.size() > 32767)
        {
            std::vector<std::wstring> paths;
            paths.push_back(node.ProposedPath);
            sink.Add(MakeIssue(L"DST-005", SevError, paths, L"The path is longer than 32,767 characters."));
        }
        else if (node.ProposedPath.size() >= 260)
        {
            std::vector<std::wstring> paths;
            paths.push_back(node.ProposedPath);
            sink.Add(MakeIssue(L"DST-005", SevWarning, paths, L"Some applications may not open this file."));
        }
        if (destination.ReadOnlyVolume && node.Change != ChangeMovedWithFolder)
        {
            std::vector<std::wstring> paths;
            paths.push_back(node.ProposedPath);
            sink.Add(MakeIssue(L"DST-002", SevError, paths, L"The destination volume is read-only."));
        }
        if (!destination.FileSystem.empty())
        {
            std::wstring fs = destination.FileSystem;
            if (fs.find(L"FAT32") != std::wstring::npos && !node.IsDir && node.Info.Size > 0xFFFFFFFFull)
            {
                std::vector<std::wstring> paths;
                paths.push_back(node.ProposedPath);
                sink.Add(MakeIssue(L"DST-007", SevError, paths, L"FAT32 cannot store a file larger than 4 GiB."));
            }
        }
        if (node.ProposedPath.size() >= 2 && node.ProposedPath[0] == L'\\' && node.ProposedPath[1] == L'\\')
        {
            std::vector<std::wstring> paths;
            paths.push_back(node.ProposedPath);
            sink.Add(MakeIssue(L"DST-010", SevInfo, paths, L"The destination is a network share."));
        }
    }
}

void CheckSources(const CAnalysisContext& context, CIssueSink& sink)
{
    for (size_t i = 0; i < context.Plan->Baseline.size(); ++i)
    {
        const CBaselineItem& baseline = context.Plan->Baseline[i];
        const CSnapshotItem* item = context.Snapshot->Find(baseline.Path);
        std::vector<std::wstring> paths;
        paths.push_back(baseline.Path);
        if (item == NULL)
        {
            sink.Add(MakeIssue(L"SRC-002", SevError, paths, L"The source is missing."));
            continue;
        }
        if (!baseline.FileId.empty() && item->HasFileId)
        {
            if (!NamesEqual(baseline.FileId, FormatFileId(item->FileId), false))
            {
                sink.Add(MakeIssue(L"SRC-002", SevError, paths, L"A different object is now at the source path."));
                continue;
            }
        }
        unsigned __int64 baselineWrite = 0;
        IsoToFileTime(baseline.LastWriteUtc, baselineWrite);
        if (item->Size != baseline.Size || (baselineWrite != 0 && item->LastWrite != baselineWrite) || item->Attributes != baseline.Attributes)
            sink.Add(MakeIssue(L"SRC-001", SevWarning, paths, L"The source changed since it was staged."));
        if (item->ReparseTag != 0)
        {
            const COverlayNode* node = context.Overlay->FindKey(item->Path);
            if (node && !PathsEqual(node->ProposedPath, node->OriginalPath, false) && node->Info.VolumeSerial != 0)
                sink.Add(MakeIssue(L"SRC-004", SevError, paths, L"A cross-volume move cannot include a reparse point."));
        }
        if (item->CloudPlaceholder)
            sink.Add(MakeIssue(L"SRC-005", SevWarning, paths, L"A cross-volume move would hydrate a cloud placeholder."));
        if (item->HardLinkCount > 1)
            sink.Add(MakeIssue(L"SRC-006", SevWarning, paths, L"The file has more than one hard link."));
        if ((item->Attributes & FILE_ATTRIBUTE_SYSTEM) != 0 || NamesEqual(item->Name, L"desktop.ini", false) || NamesEqual(item->Name, L"thumbs.db", false))
            sink.Add(MakeIssue(L"SRC-007", SevWarning, paths, L"The item is a protected system file."));
        if (item->CloudSynced)
            sink.Add(MakeIssue(L"SRC-008", SevInfo, paths, L"The source is inside a cloud-synced folder."));
    }
    if (!context.Overlay->BuildError.empty())
    {
        std::vector<std::wstring> paths;
        sink.Add(MakeIssue(L"COL-004", SevError, paths, context.Overlay->BuildError));
    }
    for (size_t i = 0; i < context.Plan->Edits.size(); ++i)
    {
        if (context.Plan->Edits[i].Op == EditRemoveEmptyFolder && context.Plan->Kind != PlanRevert)
        {
            std::vector<std::wstring> paths;
            paths.push_back(context.Plan->Edits[i].Path);
            sink.Add(MakeIssue(L"PLN-006", SevError, paths, L"removeEmptyFolder is only valid in a revert plan."));
        }
    }
}

void CheckReferences(const CAnalysisContext& context, CIssueSink& sink)
{
    std::vector<std::wstring> paths;
    sink.Add(MakeIssue(L"REF-006", SevInfo, paths, L"References from outside the scanned folders cannot be detected."));
    for (size_t i = 0; i < context.ApplicationPaths.size(); ++i)
    {
        for (std::map<std::wstring, COverlayNode>::const_iterator it = context.Overlay->Nodes.begin(); it != context.Overlay->Nodes.end(); ++it)
        {
            if (it->second.Change == ChangeUnchanged || it->second.Change == ChangeContains)
                continue;
            if (IsUnderPath(context.ApplicationPaths[i], it->second.OriginalPath, false) || PathsEqual(context.ApplicationPaths[i], it->second.OriginalPath, false))
            {
                std::vector<std::wstring> nodes;
                nodes.push_back(it->second.OriginalPath);
                nodes.push_back(context.ApplicationPaths[i]);
                sink.Add(MakeIssue(L"REF-005", SevWarning, nodes, L"An application path points into a moved item."));
            }
        }
    }
}

} // namespace

std::wstring MakeIssueKey(const std::wstring& code, const std::vector<std::wstring>& paths)
{
    std::vector<std::wstring> sorted = paths;
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        for (size_t j = i + 1; j < sorted.size(); ++j)
        {
            if (ComparePaths(sorted[j], sorted[i], false) < 0)
            {
                std::wstring tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }
    std::string canonical = WideToUtf8(code);
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        canonical.push_back('\n');
        canonical += WideToUtf8(sorted[i]);
    }
    std::wstring hash = Sha256Hex(canonical);
    if (hash.size() > 32)
        hash.resize(32);
    return code + L":" + hash;
}

void ValidatePlan(const CAnalysisContext& context, CIssueSink& sink)
{
    if (context.Plan == NULL || context.Snapshot == NULL || context.Overlay == NULL)
        return;
    CheckCollisions(context, sink);
    CheckDestinations(context, sink);
    CheckSources(context, sink);
    CheckReferences(context, sink);
    for (size_t i = 0; i < sink.Issues.size();)
    {
        if (sink.Issues[i].Severity == SevWarning && Acknowledged(*context.Plan, sink.Issues[i].Key))
            sink.Issues.erase(sink.Issues.begin() + (ptrdiff_t)i);
        else
            ++i;
    }
}

bool HasBlockingErrors(const CIssueSink& sink)
{
    for (size_t i = 0; i < sink.Issues.size(); ++i)
    {
        if (sink.Issues[i].Severity == SevError)
            return true;
    }
    return false;
}

} // namespace reorg
