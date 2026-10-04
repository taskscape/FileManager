// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "rules.h"

namespace reorg
{

struct COverlayNode
{
    std::wstring Key;
    std::wstring ParentKey;
    std::wstring Name;
    std::wstring OriginalPath;
    std::wstring OriginalParent;
    std::wstring OriginalName;
    std::wstring ProposedPath;
    bool IsDir;
    bool Synthetic;
    bool Excluded;
    bool Carried;
    bool Displaced;
    std::wstring Origin;
    CSnapshotItem Info;
    EChangeKind Change;
    EReversibility Reversible;
    int IssueErrors;
    int IssueWarnings;
    int ContainsChanges;
    std::wstring ResolutionNote;
    std::wstring ResolutionTarget; // for a merged directory: the occupied folder that received its children

    COverlayNode()
        : IsDir(false), Synthetic(false), Excluded(false), Carried(false), Displaced(false),
          Change(ChangeUnchanged), Reversible(RevExact), IssueErrors(0), IssueWarnings(0), ContainsChanges(0)
    {
    }
};

class COverlay
{
public:
    std::map<std::wstring, COverlayNode> Nodes;
    std::map<std::wstring, std::vector<std::wstring>> Children;
    std::map<std::wstring, std::wstring> PathToKey;
    int Generation;
    std::wstring BuildError;

    COverlay() : Generation(0) {}

    const COverlayNode* FindKey(const std::wstring& key) const;
    const COverlayNode* FindProposed(const std::wstring& path) const;
    std::vector<const COverlayNode*> List(const std::wstring& proposedDir) const;
};

COverlay BuildOverlay(const CSnapshot& snapshot, const CPlanDocument& plan);
bool WouldCreateCycle(const COverlay& overlay, const std::wstring& sourceKey, const std::wstring& newParentKey);

// Returns the real node that physically holds 'path' (the path itself or its nearest ancestor that
// exists at its original location). Its Info describes the volume and filesystem an item placed at
// 'path' will live on. Synthetic folders and moved nodes are skipped because their Info describes
// another location. NULL when no captured ancestor is known.
const COverlayNode* FindLocationNode(const COverlay& overlay, const std::wstring& path);

// Real folders the plan empties while leaving them at their original location (spec 7.6.2 step 5):
// each had captured children, and every child it still holds in the proposed tree is displaced or is
// itself such a folder. Plan roots and synthetic, displaced, excluded, or moved folders never qualify.
// Keys are returned deepest first, so a nested folder precedes the folder that holds it.
std::vector<std::wstring> FindEmptiedFolders(const COverlay& overlay, const CPlanDocument& plan);

} // namespace reorg
