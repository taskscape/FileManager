// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "overlay.h"

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
        if (!it->second.ProposedPath.empty())
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

} // namespace

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
                if (residual)
                {
                    residual->Key = L"residual:" + stayParent;
                    node = overlay.Nodes.find(edit.Source);
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
