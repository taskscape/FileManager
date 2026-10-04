// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "overlay.h"
#include "validate.h"

#include <algorithm>
#include <set>

namespace reorg
{
namespace
{

void IndexChildren(COverlay& overlay)
{
    overlay.Children.clear();
    overlay.PathToKey.clear();
    for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        if (!it->second.ParentKey.empty())
            overlay.Children[it->second.ParentKey].push_back(it->first);
        // A displaced occupant keeps its path until the store move runs; the proposed path
        // belongs to the item that replaces it.
        if (!it->second.ProposedPath.empty() && !it->second.Displaced)
            overlay.PathToKey[it->second.ProposedPath] = it->first;
    }
}

void RecomputePaths(COverlay& overlay)
{
    for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        it->second.ProposedPath.clear();
    bool progress = true;
    int guard = 0;
    while (progress && guard < 1000000)
    {
        progress = false;
        ++guard;
        for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        {
            if (!it->second.ProposedPath.empty())
                continue;
            if (it->second.ParentKey.empty())
            {
                it->second.ProposedPath = it->second.OriginalPath.empty() ? it->second.Name : it->second.OriginalPath;
                progress = true;
                continue;
            }
            std::map<std::wstring, COverlayNode>::iterator parent = overlay.Nodes.find(it->second.ParentKey);
            if (parent == overlay.Nodes.end())
                continue;
            if (parent->second.ProposedPath.empty())
                continue;
            it->second.ProposedPath = JoinPath(parent->second.ProposedPath, it->second.Name);
            progress = true;
        }
    }
    IndexChildren(overlay);
}

COverlayNode* EnsureImplicit(COverlay& overlay, const std::wstring& proposedPath, const std::wstring& origin)
{
    std::map<std::wstring, std::wstring>::iterator existing = overlay.PathToKey.find(proposedPath);
    if (existing != overlay.PathToKey.end())
    {
        std::map<std::wstring, COverlayNode>::iterator node = overlay.Nodes.find(existing->second);
        if (node != overlay.Nodes.end())
            return &node->second;
    }
    if (IsRootPath(proposedPath))
        return NULL;
    std::wstring parentPath = ParentPath(proposedPath);
    COverlayNode* parent = EnsureImplicit(overlay, parentPath, origin);
    COverlayNode created;
    created.Key = L"new:imp:" + proposedPath;
    created.Synthetic = true;
    created.IsDir = true;
    created.Name = LeafName(proposedPath);
    created.Origin = origin;
    created.ParentKey = parent ? parent->Key : std::wstring();
    if (!parent && !parentPath.empty())
    {
        // The parent is a real directory that should already be a node. Fall back to its path key.
        if (overlay.Nodes.find(parentPath) != overlay.Nodes.end())
            created.ParentKey = parentPath;
    }
    overlay.Nodes[created.Key] = created;
    RecomputePaths(overlay);
    std::map<std::wstring, COverlayNode>::iterator inserted = overlay.Nodes.find(created.Key);
    return inserted == overlay.Nodes.end() ? NULL : &inserted->second;
}

void Classify(COverlay& overlay, bool applied)
{
    for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        COverlayNode& node = it->second;
        node.Carried = false;
        node.ContainsChanges = 0;
        if (node.Synthetic)
        {
            node.Change = ChangeNewFolder;
            continue;
        }
        bool sameParentNode = node.ParentKey == node.OriginalParent || (node.ParentKey.empty() && node.OriginalParent.empty());
        bool sameName = NamesEqual(node.Name, node.OriginalName, false);
        bool samePath = PathsEqual(node.ProposedPath, node.OriginalPath, false);
        if (applied && !samePath)
        {
            node.Change = ChangeApplied;
            continue;
        }
        if (sameParentNode && sameName && samePath)
        {
            node.Change = ChangeUnchanged;
            continue;
        }
        if (sameParentNode && sameName && !samePath)
        {
            node.Change = ChangeMovedWithFolder;
            node.Carried = true;
            continue;
        }
        if (sameName && !sameParentNode)
            node.Change = ChangeMovedHere;
        else if (!sameName && (sameParentNode || PathsEqual(ParentPath(node.ProposedPath), ParentPath(node.OriginalPath), false)))
            node.Change = ChangeRenamed;
        else
            node.Change = ChangeMovedAndRenamed;
        if (node.ResolutionNote == L"keepBoth")
            node.Change = ChangeKeptBoth;
        else if (node.ResolutionNote == L"replace")
            node.Change = ChangeReplaces;
        else if (node.ResolutionNote == L"merge")
            node.Change = ChangeMerged;
    }
    for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        if (!it->second.IsDir || it->second.Change != ChangeUnchanged)
            continue;
        int count = 0;
        std::vector<std::wstring> stack = overlay.Children[it->first];
        for (size_t i = 0; i < stack.size(); ++i)
        {
            const COverlayNode* child = overlay.FindKey(stack[i]);
            if (child == NULL)
                continue;
            if (child->Change != ChangeUnchanged)
                ++count;
            if (child->IsDir)
            {
                const std::vector<std::wstring>& grand = overlay.Children[child->Key];
                stack.insert(stack.end(), grand.begin(), grand.end());
            }
        }
        if (count > 0)
        {
            it->second.ContainsChanges = count;
            it->second.Change = ChangeContains;
        }
    }
}

bool HasExplicit(const CPlanDocument& plan, const std::wstring& source)
{
    bool explicitEdit = false;
    for (size_t i = 0; i < plan.Edits.size(); ++i)
    {
        const CEdit& edit = plan.Edits[i];
        if (!PathsEqual(edit.Source, source, false))
            continue;
        if (edit.Op == EditUnstage)
            explicitEdit = false;
        else if (edit.Op == EditMove || edit.Op == EditRename || edit.Op == EditExclude)
            explicitEdit = true;
    }
    return explicitEdit;
}

bool IsStayingChange(EChangeKind change)
{
    return change == ChangeUnchanged || change == ChangeContains || change == ChangeMovedWithFolder;
}

// Case-folded form for occupancy lookups, so a scan of every node does not fall back to the
// linear case-insensitive search of CSnapshot::Find and COverlay::FindProposed.
std::wstring FoldPath(const std::wstring& path)
{
    std::wstring folded = path;
    if (!folded.empty())
        LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, path.c_str(), (int)path.size(),
                      &folded[0], (int)folded.size(), NULL, NULL, 0);
    return folded;
}

struct CResolvedConflict
{
    std::wstring Key;
    std::wstring NodeKey;
    std::wstring OccupantKey;
    std::wstring ProposedPath;
    EResolutionChoice Choice;
    std::wstring ResultName;
};

// Mirrors the COL-002 test in validate.cpp so a saved resolution is found by the same issue key the
// user resolved. Only conflicts with a saved resolution or a non-Ask default are returned.
std::vector<CResolvedConflict> FindResolvedConflicts(const COverlay& overlay, const CPlanDocument& plan,
                                                     const std::map<std::wstring, std::wstring>& snapshotByFolded)
{
    std::vector<CResolvedConflict> conflicts;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        const COverlayNode& node = it->second;
        if (node.Synthetic || node.Displaced || IsStayingChange(node.Change))
            continue;
        std::map<std::wstring, std::wstring>::const_iterator occupant = snapshotByFolded.find(FoldPath(node.ProposedPath));
        if (occupant == snapshotByFolded.end())
            continue;
        const COverlayNode* occupantNode = overlay.FindKey(occupant->second);
        if (occupantNode != NULL && (occupantNode->Displaced || !IsStayingChange(occupantNode->Change)))
            continue;
        if (PathsEqual(occupant->second, node.OriginalPath, false))
            continue;
        std::vector<std::wstring> paths;
        paths.push_back(node.OriginalPath);
        paths.push_back(node.ProposedPath);
        CResolvedConflict conflict;
        conflict.Key = MakeIssueKey(L"COL-002", paths);
        conflict.NodeKey = it->first;
        conflict.OccupantKey = occupant->second;
        conflict.ProposedPath = node.ProposedPath;
        const CResolution* saved = NULL;
        for (size_t i = 0; i < plan.Resolutions.size(); ++i)
        {
            if (plan.Resolutions[i].IssueKey == conflict.Key)
                saved = &plan.Resolutions[i];
        }
        if (saved != NULL)
        {
            conflict.Choice = saved->Choice;
            conflict.ResultName = saved->ResultName;
        }
        else if (plan.Options.ConflictDefault == ConflictKeepBoth)
            conflict.Choice = ResKeepBoth;
        else if (plan.Options.ConflictDefault == ConflictSkip)
            conflict.Choice = ResSkip;
        else
            continue; // Ask: the conflict stays an Error until the user resolves it
        conflicts.push_back(conflict);
    }
    return conflicts;
}

// The occupant and the content it physically holds travel to the store as one move. Items the plan
// moves into the occupant are not part of it, so they stay visible and keep their own conflicts.
void MarkDisplaced(COverlay& overlay, const std::wstring& key)
{
    std::vector<std::wstring> stack(1, key);
    while (!stack.empty())
    {
        std::wstring current = stack.back();
        stack.pop_back();
        std::map<std::wstring, COverlayNode>::iterator node = overlay.Nodes.find(current);
        if (node == overlay.Nodes.end() || node->second.Displaced)
            continue;
        if (current != key && (node->second.Synthetic || !IsStayingChange(node->second.Change)))
            continue;
        node->second.Displaced = true;
        std::map<std::wstring, std::vector<std::wstring>>::const_iterator kids = overlay.Children.find(current);
        if (kids != overlay.Children.end())
            stack.insert(stack.end(), kids->second.begin(), kids->second.end());
    }
}

void ApplyResolution(COverlay& overlay, const CSnapshot& snapshot, const CPlanDocument& plan,
                     const CResolvedConflict& conflict, std::set<std::wstring>& taken)
{
    std::map<std::wstring, COverlayNode>::iterator node = overlay.Nodes.find(conflict.NodeKey);
    if (node == overlay.Nodes.end())
        return;
    COverlayNode& incoming = node->second;
    switch (conflict.Choice)
    {
    case ResKeepBoth:
    case ResRename:
    {
        std::wstring parent = ParentPath(incoming.ProposedPath);
        std::wstring name = conflict.ResultName;
        if (name.empty())
        {
            if (conflict.Choice == ResRename)
                return; // a rename resolution is only valid with the chosen name
            for (int n = 2; n < 10000 && name.empty(); ++n)
            {
                std::wstring candidate = ExpandKeepBoth(plan.Options.KeepBothPattern, incoming.Name, n);
                if (!candidate.empty() && taken.find(FoldPath(JoinPath(parent, candidate))) == taken.end())
                    name = candidate;
            }
            if (name.empty())
                return;
        }
        incoming.Name = name;
        incoming.ResolutionNote = conflict.Choice == ResKeepBoth ? L"keepBoth" : L"rename";
        taken.insert(FoldPath(JoinPath(parent, name)));
        break;
    }
    case ResSkip:
        // The item stays where it is, exactly as if its move had been unstaged.
        incoming.ParentKey = snapshot.Items.find(incoming.OriginalParent) != snapshot.Items.end() ? incoming.OriginalParent : std::wstring();
        incoming.Name = incoming.OriginalName;
        incoming.ResolutionNote = L"skip";
        break;
    case ResReplace:
        // The occupant cannot be moved to the store while the incoming item still sits inside it.
        if (IsUnderPath(incoming.OriginalPath, conflict.OccupantKey, false))
            return;
        MarkDisplaced(overlay, conflict.OccupantKey);
        incoming.ResolutionNote = L"replace";
        break;
    case ResMerge:
    {
        std::map<std::wstring, COverlayNode>::iterator occupant = overlay.Nodes.find(conflict.OccupantKey);
        if (occupant == overlay.Nodes.end() || !incoming.IsDir || !occupant->second.IsDir)
            return; // merge is only defined for a directory onto a directory
        // The children move individually into the existing folder; each child clash becomes its own conflict.
        std::vector<std::wstring> children = overlay.Children[conflict.NodeKey];
        for (size_t i = 0; i < children.size(); ++i)
        {
            std::map<std::wstring, COverlayNode>::iterator child = overlay.Nodes.find(children[i]);
            if (child == overlay.Nodes.end())
                continue;
            child->second.ParentKey = conflict.OccupantKey;
            child->second.ResolutionNote = L"merge";
        }
        incoming.ParentKey = snapshot.Items.find(incoming.OriginalParent) != snapshot.Items.end() ? incoming.OriginalParent : std::wstring();
        incoming.Name = incoming.OriginalName;
        incoming.ResolutionNote = L"mergedInto";
        incoming.ResolutionTarget = conflict.ProposedPath;
        break;
    }
    }
    RecomputePaths(overlay);
}

// Saved resolutions (and the plan's non-Ask default) are applied to the proposed tree itself, so
// validation, the panel, and the compiler all see the resolved result rather than an occupied target.
void ApplyResolutions(COverlay& overlay, const CSnapshot& snapshot, const CPlanDocument& plan)
{
    if (plan.Resolutions.empty() && plan.Options.ConflictDefault == ConflictAsk)
        return;
    std::map<std::wstring, std::wstring> snapshotByFolded;
    for (std::map<std::wstring, CSnapshotItem>::const_iterator it = snapshot.Items.begin(); it != snapshot.Items.end(); ++it)
        snapshotByFolded[FoldPath(it->first)] = it->first;
    std::set<std::wstring> processed;
    // A merge exposes child conflicts, which are resolved in the next round.
    for (int round = 0; round < 64; ++round)
    {
        Classify(overlay, false);
        std::vector<CResolvedConflict> conflicts = FindResolvedConflicts(overlay, plan, snapshotByFolded);
        std::set<std::wstring> taken;
        for (std::map<std::wstring, std::wstring>::const_iterator it = snapshotByFolded.begin(); it != snapshotByFolded.end(); ++it)
            taken.insert(it->first);
        for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        {
            if (!it->second.Displaced && !it->second.ProposedPath.empty())
                taken.insert(FoldPath(it->second.ProposedPath));
        }
        bool applied = false;
        for (size_t i = 0; i < conflicts.size(); ++i)
        {
            if (!processed.insert(conflicts[i].Key).second)
                continue;
            ApplyResolution(overlay, snapshot, plan, conflicts[i], taken);
            applied = true;
        }
        if (!applied)
            break;
    }
}

// Orders (depth, key) pairs deepest first, then by key, for bottom-up folder decisions.
bool DeeperFirst(const std::pair<int, std::wstring>& a, const std::pair<int, std::wstring>& b)
{
    if (a.first != b.first)
        return a.first > b.first;
    return ComparePaths(a.second, b.second, true) < 0;
}

} // namespace

const COverlayNode* FindLocationNode(const COverlay& overlay, const std::wstring& path)
{
    std::wstring cursor = path;
    for (int guard = 0; !cursor.empty() && guard < 32768; ++guard)
    {
        // A real object captured at this path proves where the path lives, even if the plan moves it away.
        const COverlayNode* original = overlay.FindKey(cursor);
        if (original != NULL && !original->Synthetic && !original->OriginalPath.empty())
            return original;
        const COverlayNode* proposed = overlay.FindProposed(cursor);
        if (proposed != NULL && !proposed->Synthetic && !proposed->OriginalPath.empty() &&
            PathsEqual(proposed->ProposedPath, proposed->OriginalPath, false))
            return proposed;
        std::wstring parent = ParentPath(cursor);
        if (parent == cursor)
            break;
        cursor = parent;
    }
    return NULL;
}

std::vector<std::wstring> FindEmptiedFolders(const COverlay& overlay, const CPlanDocument& plan)
{
    // Classify leaves an emptied folder Unchanged because it only counts the children the folder still
    // holds, so emptied folders are recognized from the proposed tree's shape, not their change kind.
    std::set<std::wstring> hadChildren;
    std::vector<std::pair<int, std::wstring>> candidates; // (depth, key)
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        const COverlayNode& node = it->second;
        if (node.Synthetic || node.OriginalPath.empty())
            continue;
        if (!node.OriginalParent.empty())
            hadChildren.insert(node.OriginalParent);
        // A capture root has no parent node; it and the other plan roots hold the recovery store.
        if (!node.IsDir || node.Displaced || node.Excluded || node.ParentKey.empty() ||
            (node.Change != ChangeUnchanged && node.Change != ChangeContains))
            continue;
        int depth = 0;
        for (size_t i = 0; i < node.OriginalPath.size(); ++i)
        {
            if (node.OriginalPath[i] == L'\\')
                ++depth;
        }
        candidates.push_back(std::make_pair(depth, it->first));
    }
    // A staying folder's children are one level deeper, so deepest-first decides every child before
    // its parent; the path order keeps the result, and the compiled hash, deterministic.
    std::sort(candidates.begin(), candidates.end(), DeeperFirst);
    const std::vector<CRoot>* roots[] = {&plan.ScopeRoots, &plan.DestinationRoots};
    std::set<std::wstring> emptied;
    std::vector<std::wstring> result;
    for (size_t c = 0; c < candidates.size(); ++c)
    {
        const COverlayNode* folder = overlay.FindKey(candidates[c].second);
        if (folder == NULL || hadChildren.find(folder->OriginalPath) == hadChildren.end())
            continue;
        bool planRoot = false;
        for (int g = 0; g < 2 && !planRoot; ++g)
        {
            for (size_t r = 0; r < roots[g]->size() && !planRoot; ++r)
                planRoot = PathsEqual((*roots[g])[r].Path, folder->OriginalPath, false);
        }
        if (planRoot)
            continue;
        bool empty = true;
        std::map<std::wstring, std::vector<std::wstring>>::const_iterator kids = overlay.Children.find(folder->Key);
        if (kids != overlay.Children.end())
        {
            for (size_t k = 0; k < kids->second.size() && empty; ++k)
            {
                // A displaced occupant leaves for the store; anything else must itself be emptied.
                const COverlayNode* child = overlay.FindKey(kids->second[k]);
                if (child != NULL && !child->Displaced && emptied.find(child->Key) == emptied.end())
                    empty = false;
            }
        }
        if (!empty)
            continue;
        emptied.insert(folder->Key);
        result.push_back(folder->Key);
    }
    return result;
}

const COverlayNode* COverlay::FindKey(const std::wstring& key) const
{
    std::map<std::wstring, COverlayNode>::const_iterator it = Nodes.find(key);
    return it == Nodes.end() ? NULL : &it->second;
}

const COverlayNode* COverlay::FindProposed(const std::wstring& path) const
{
    std::map<std::wstring, std::wstring>::const_iterator it = PathToKey.find(path);
    if (it != PathToKey.end())
        return FindKey(it->second);
    for (std::map<std::wstring, std::wstring>::const_iterator scan = PathToKey.begin(); scan != PathToKey.end(); ++scan)
    {
        if (PathsEqual(scan->first, path, false))
            return FindKey(scan->second);
    }
    return NULL;
}

std::vector<const COverlayNode*> COverlay::List(const std::wstring& proposedDir) const
{
    std::vector<const COverlayNode*> result;
    const COverlayNode* dir = proposedDir.empty() ? NULL : FindProposed(proposedDir);
    if (dir == NULL)
        return result;
    std::map<std::wstring, std::vector<std::wstring>>::const_iterator kids = Children.find(dir->Key);
    if (kids == Children.end())
        return result;
    for (size_t i = 0; i < kids->second.size(); ++i)
    {
        const COverlayNode* child = FindKey(kids->second[i]);
        if (child && !child->Displaced)
            result.push_back(child);
    }
    return result;
}

bool WouldCreateCycle(const COverlay& overlay, const std::wstring& sourceKey, const std::wstring& newParentKey)
{
    const COverlayNode* source = overlay.FindKey(sourceKey);
    if (source == NULL || !source->IsDir)
        return false;
    for (std::wstring cursor = newParentKey; !cursor.empty();)
    {
        if (cursor == sourceKey)
            return true;
        const COverlayNode* node = overlay.FindKey(cursor);
        if (node == NULL)
            break;
        cursor = node->ParentKey;
    }
    return false;
}

COverlay BuildOverlay(const CSnapshot& snapshot, const CPlanDocument& plan)
{
    COverlay overlay;
    for (std::map<std::wstring, CSnapshotItem>::const_iterator it = snapshot.Items.begin(); it != snapshot.Items.end(); ++it)
    {
        COverlayNode node;
        node.Key = it->second.Path;
        node.OriginalPath = it->second.Path;
        node.OriginalParent = it->second.ParentPath;
        node.OriginalName = it->second.Name;
        node.Name = it->second.Name;
        node.IsDir = it->second.IsDir;
        node.Info = it->second;
        node.ParentKey = snapshot.Items.find(it->second.ParentPath) != snapshot.Items.end() ? it->second.ParentPath : std::wstring();
        if (node.ParentKey.empty())
            node.ProposedPath = node.OriginalPath;
        overlay.Nodes[node.Key] = node;
    }
    RecomputePaths(overlay);

    std::vector<CRuleHit> rules = ApplyRules(plan, snapshot);
    for (size_t i = 0; i < rules.size(); ++i)
    {
        if (HasExplicit(plan, rules[i].Source))
            continue;
        std::map<std::wstring, COverlayNode>::iterator node = overlay.Nodes.find(rules[i].Source);
        if (node == overlay.Nodes.end())
            continue;
        COverlayNode* dest = EnsureImplicit(overlay, rules[i].DestinationDir, L"rule:" + rules[i].RuleId);
        if (dest == NULL)
            continue;
        if (WouldCreateCycle(overlay, node->first, dest->Key))
            continue;
        node = overlay.Nodes.find(rules[i].Source);
        if (node == overlay.Nodes.end())
            continue;
        node->second.ParentKey = dest->Key;
        node->second.Name = rules[i].NewName.empty() ? node->second.OriginalName : rules[i].NewName;
        node->second.Origin = L"rule:" + rules[i].RuleId;
        RecomputePaths(overlay);
    }

    for (size_t i = 0; i < plan.Edits.size(); ++i)
    {
        const CEdit& edit = plan.Edits[i];
        if (edit.Op == EditCreateFolder || edit.Op == EditRemoveEmptyFolder)
        {
            // createFolder is a synthetic node. removeEmptyFolder only exists so a revert plan can name the directory it will remove.
            std::wstring path = edit.Path;
            COverlayNode* created = EnsureImplicit(overlay, path, edit.Origin.empty() ? L"user" : edit.Origin);
            if (created != NULL && edit.Op == EditCreateFolder && created->Key.rfind(L"new:imp:", 0) == 0)
            {
                std::wstring oldKey = created->Key;
                COverlayNode copy = *created;
                copy.Key = L"new:" + std::to_wstring((long long)edit.Seq);
                copy.Origin = edit.Origin.empty() ? L"user" : edit.Origin;
                overlay.Nodes.erase(oldKey);
                for (std::map<std::wstring, COverlayNode>::iterator child = overlay.Nodes.begin(); child != overlay.Nodes.end(); ++child)
                {
                    if (child->second.ParentKey == oldKey)
                        child->second.ParentKey = copy.Key;
                }
                overlay.Nodes[copy.Key] = copy;
                RecomputePaths(overlay);
            }
            continue;
        }
        std::map<std::wstring, COverlayNode>::iterator node = overlay.Nodes.find(edit.Source);
        if (node == overlay.Nodes.end())
        {
            for (std::map<std::wstring, COverlayNode>::iterator scan = overlay.Nodes.begin(); scan != overlay.Nodes.end(); ++scan)
            {
                if (!scan->second.OriginalPath.empty() && PathsEqual(scan->second.OriginalPath, edit.Source, false))
                {
                    node = scan;
                    break;
                }
            }
        }
        if (node == overlay.Nodes.end())
            continue;
        if (edit.Op == EditUnstage)
        {
            node->second.ParentKey = snapshot.Items.find(node->second.OriginalParent) != snapshot.Items.end() ? node->second.OriginalParent : std::wstring();
            node->second.Name = node->second.OriginalName;
            node->second.Excluded = false;
            node->second.Origin.clear();
            RecomputePaths(overlay);
            continue;
        }
        if (edit.Op == EditExclude)
        {
            node->second.Excluded = true;
            node->second.Origin = edit.Origin.empty() ? L"user" : edit.Origin;
            std::wstring stayParent = node->second.OriginalParent;
            const COverlayNode* occupant = overlay.FindProposed(stayParent);
            if (occupant == NULL || occupant->Key == node->first)
            {
                COverlayNode* residual = EnsureImplicit(overlay, stayParent, L"kept for items that stay");
                // Re-key the map entry, not only the Key field: children name their parent by map key, so
                // a renamed field alone left the excluded item under a missing parent with no path.
                if (residual != NULL && residual->Key.rfind(L"new:imp:", 0) == 0)
                {
                    std::wstring oldKey = residual->Key;
                    COverlayNode copy = *residual;
                    copy.Key = L"residual:" + stayParent;
                    overlay.Nodes.erase(oldKey);
                    for (std::map<std::wstring, COverlayNode>::iterator child = overlay.Nodes.begin(); child != overlay.Nodes.end(); ++child)
                    {
                        if (child->second.ParentKey == oldKey)
                            child->second.ParentKey = copy.Key;
                    }
                    overlay.Nodes[copy.Key] = copy;
                    RecomputePaths(overlay);
                }
            }
            node = overlay.Nodes.find(edit.Source);
            occupant = overlay.FindProposed(stayParent);
            if (node != overlay.Nodes.end() && occupant)
                node->second.ParentKey = occupant->Key;
            if (node != overlay.Nodes.end())
                node->second.Name = node->second.OriginalName;
            RecomputePaths(overlay);
            continue;
        }
        std::wstring destDir = edit.Op == EditRename ? ParentPath(node->second.ProposedPath) : edit.DestinationDir;
        std::wstring newName = edit.NewName.empty() ? node->second.Name : edit.NewName;
        COverlayNode* dest = EnsureImplicit(overlay, destDir, L"compiler: required by edit " + std::to_wstring((long long)edit.Seq));
        node = overlay.Nodes.find(edit.Source);
        if (node == overlay.Nodes.end() || dest == NULL)
            continue;
        if (WouldCreateCycle(overlay, node->first, dest->Key))
        {
            overlay.BuildError = L"Moving a folder into its own descendant is not allowed.";
            continue;
        }
        node->second.ParentKey = dest->Key;
        node->second.Name = newName;
        node->second.Origin = edit.Origin.empty() ? L"user" : edit.Origin;
        RecomputePaths(overlay);
    }

    // Residual folders: a moved directory can leave descendants at its old path.
    std::vector<std::wstring> movedDirs;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        if (it->second.IsDir && !it->second.Synthetic && !PathsEqual(it->second.ProposedPath, it->second.OriginalPath, false))
            movedDirs.push_back(it->first);
    }
    for (size_t i = 0; i < movedDirs.size(); ++i)
    {
        const COverlayNode* moved = overlay.FindKey(movedDirs[i]);
        if (moved == NULL)
            continue;
        std::wstring original = moved->OriginalPath;
        bool need = false;
        for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        {
            if (it->first == movedDirs[i] || it->second.Displaced)
                continue;
            if (it->second.Excluded && PathsEqual(it->second.OriginalParent, original, false))
                need = true;
        }
        if (!need)
            continue;
        if (overlay.FindProposed(original) == NULL)
        {
            COverlayNode residual;
            residual.Key = L"residual:" + original;
            residual.Synthetic = true;
            residual.IsDir = true;
            residual.Name = LeafName(original);
            residual.Origin = L"kept for items that stay";
            residual.OriginalPath = original;
            const COverlayNode* parent = overlay.FindProposed(ParentPath(original));
            residual.ParentKey = parent ? parent->Key : std::wstring();
            overlay.Nodes[residual.Key] = residual;
            RecomputePaths(overlay);
        }
        const COverlayNode* residual = overlay.FindProposed(original);
        if (residual == NULL)
            continue;
        for (std::map<std::wstring, COverlayNode>::iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        {
            if (it->second.Excluded && PathsEqual(it->second.OriginalParent, original, false))
                it->second.ParentKey = residual->Key;
        }
        RecomputePaths(overlay);
    }

    ApplyResolutions(overlay, snapshot, plan);
    Classify(overlay, false);
    for (size_t i = 0; i < plan.Applies.size(); ++i)
    {
        if (plan.Applies[i].Status == L"completed" || plan.Applies[i].Status == L"finalized")
            Classify(overlay, true);
    }
    overlay.Generation = (int)plan.Edits.size() + (int)plan.Rules.size();
    return overlay;
}

} // namespace reorg
