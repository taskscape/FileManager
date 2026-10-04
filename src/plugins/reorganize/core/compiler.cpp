// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "compiler.h"

#include <algorithm>

namespace reorg
{
namespace
{

struct CSimNode
{
    std::wstring NodeKey;
    bool IsDir;
    DWORD Volume;
    bool FromCrossVolume;
    // Spec 7.6.3 step 8: the step that created this entry or moved it to its current path, or -1 for a
    // snapshot entry that is still where it was. Carried descendants keep their own; the step that
    // carried them is the producer of the folder above them.
    int Producer;
    // Nodes are created field-by-field during expansion; no field may be left indeterminate.
    CSimNode() : IsDir(false), Volume(0), FromCrossVolume(false), Producer(-1) {}
};

// Simulated filesystem S of spec 7.6.3. Nodes is keyed by the current simulated path and Where maps a
// node key back to it, so a step reads its source from S rather than from the snapshot once a folder
// holding the node has moved.
struct CSim
{
    typedef std::map<std::wstring, CSimNode>::iterator iterator;
    typedef std::map<std::wstring, CSimNode>::const_iterator const_iterator;

    std::map<std::wstring, CSimNode> Nodes;
    std::map<std::wstring, std::wstring> Where;

    void Put(const std::wstring& path, const CSimNode& node)
    {
        iterator old = Nodes.find(path);
        if (old != Nodes.end())
            Forget(old);
        Nodes[path] = node;
        Where[node.NodeKey] = path;
    }

    void Erase(iterator it)
    {
        Forget(it);
        Nodes.erase(it);
    }

    // Every move takes the node's whole simulated subtree along, so descendants, occupancy and later
    // sources are all seen at the new location. Only the moved root records 'producer'; descendants
    // reach it through the ancestor walk of AddPlacementDeps.
    void MoveSubtree(const std::wstring& from, const std::wstring& to, int producer)
    {
        std::vector<std::pair<std::wstring, CSimNode>> moved;
        for (iterator it = Nodes.begin(); it != Nodes.end();)
        {
            iterator next = it;
            ++next;
            if (IsUnderPath(it->first, from, false))
            {
                CSimNode node = it->second;
                if (it->first.size() == from.size())
                    node.Producer = producer;
                moved.push_back(std::make_pair(to + it->first.substr(from.size()), node));
                Erase(it);
            }
            it = next;
        }
        for (size_t i = 0; i < moved.size(); ++i)
            Put(moved[i].first, moved[i].second);
    }

    // The node's current path, or an empty string when S does not hold it.
    std::wstring PathOf(const std::wstring& key) const
    {
        std::map<std::wstring, std::wstring>::const_iterator where = Where.find(key);
        return where == Where.end() ? std::wstring() : where->second;
    }

    // The entry holding 'path', compared without case like a name lookup on the volume.
    const_iterator Occupant(const std::wstring& path) const
    {
        const_iterator exact = Nodes.find(path);
        if (exact != Nodes.end())
            return exact;
        for (const_iterator it = Nodes.begin(); it != Nodes.end(); ++it)
        {
            if (PathsEqual(it->first, path, false))
                return it;
        }
        return Nodes.end();
    }

private:
    void Forget(iterator it)
    {
        // A cross-volume source folder kept for excluded items still names the node that moved, so only
        // an index entry that points at this very path is dropped.
        std::map<std::wstring, std::wstring>::iterator where = Where.find(it->second.NodeKey);
        if (where != Where.end() && where->second == it->first)
            Where.erase(where);
    }
};

// Orders names the way the occupancy test compares them, without case.
struct CPathLess
{
    bool operator()(const std::wstring& a, const std::wstring& b) const { return ComparePaths(a, b, false) < 0; }
};

// Spec 7.6.3 step 8 bookkeeping kept beside S. Reconciliation (spec 7.6.6) blocks every step that
// depends on a manual one, so a missing dependency would let Resume run a step whose premise is unknown.
struct CDepIndex
{
    // The last step that took an entry away from each name.
    std::map<std::wstring, int, CPathLess> Vacated;
    // Folder node key -> every step that moved something out of the folder or removed a folder in it.
    std::map<std::wstring, std::vector<int>> MovedOut;
    // createDir steps of folders S does not hold: recovery-store placeholders, and the target folders of a
    // cross-volume expansion until the expansion records its tree in S.
    std::map<std::wstring, int> Made;
};

// The steps that put 'path' where S has it now: the step that created or moved the entry itself and,
// for a carried subtree, the step that moved each folder above it.
void AddPlacementDeps(const CSim& sim, const CDepIndex& index, const std::wstring& path, std::vector<int>& deps)
{
    for (std::wstring cursor = path; !cursor.empty(); cursor = ParentPath(cursor))
    {
        CSim::const_iterator found = sim.Nodes.find(cursor);
        if (found != sim.Nodes.end())
        {
            if (found->second.Producer >= 0)
                deps.push_back(found->second.Producer);
            continue;
        }
        std::map<std::wstring, int>::const_iterator made = index.Made.find(cursor);
        if (made != index.Made.end())
            deps.push_back(made->second);
    }
}

// The step that created the target parent (or moved a folder above it there) and the step that
// vacated the target name.
void AddTargetDeps(const CSim& sim, const CDepIndex& index, const std::wstring& target, std::vector<int>& deps)
{
    AddPlacementDeps(sim, index, ParentPath(target), deps);
    std::map<std::wstring, int, CPathLess>::const_iterator vacated = index.Vacated.find(target);
    if (vacated != index.Vacated.end())
        deps.push_back(vacated->second);
}

// A step moving a folder relies on the earlier steps that moved items out of it: the scheduler let them
// leave first so the folder would not carry them, and a cleanup or removal needs the folder empty.
void AddMovedOutDeps(const CDepIndex& index, const std::wstring& folderKey, std::vector<int>& deps)
{
    std::map<std::wstring, std::vector<int>>::const_iterator out = index.MovedOut.find(folderKey);
    if (out != index.MovedOut.end())
        deps.insert(deps.end(), out->second.begin(), out->second.end());
}

// Records that 'step' takes the entry at 'source' to 'target' (empty when the entry is removed): the
// name is vacated, and each folder above the source that does not also hold the target loses an item.
// Called before S is updated, while the source's folders are still found at their paths.
void RecordLeave(const CSim& sim, CDepIndex& index, const std::wstring& source, const std::wstring& target, int step)
{
    index.Vacated[source] = step;
    for (std::wstring cursor = ParentPath(source); !cursor.empty(); cursor = ParentPath(cursor))
    {
        if (!target.empty() && IsUnderPath(target, cursor, false))
            break;
        CSim::const_iterator folder = sim.Nodes.find(cursor);
        if (folder != sim.Nodes.end() && folder->second.IsDir)
            index.MovedOut[folder->second.NodeKey].push_back(step);
    }
}

// Deps enter the hash, so they are stored sorted and without duplicates.
void SetDeps(CCompiledStep& step, std::vector<int>& deps)
{
    std::sort(deps.begin(), deps.end());
    deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    step.Deps = deps;
}

struct CIntent
{
    EStepKind Kind;
    EStepRole Role;
    std::wstring NodeKey;
    // The node's snapshot path. Steps never read it directly (CurrentSource does); it only breaks
    // scheduling ties and picks the cycle member, which must not depend on earlier steps.
    std::wstring Source;
    std::wstring Target;
    // Spec 7.6.3 step 2(a): the overlay node the proposed tree puts at the target's parent path. Targets
    // are final paths, so another entry found there may still leave and carry the item away. Empty for
    // store moves, whose placeholder targets have no such node.
    std::wstring ParentKey;
    bool IsDir;
    bool Cross;
    bool Emitted;
    std::wstring Reason;
    int Depth;
    CIntent() : Kind(StepMove), Role(RoleEdit), IsDir(false), Cross(false), Emitted(false), Depth(0) {}
};

int DepthOf(const std::wstring& path)
{
    int depth = 0;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == L'\\')
            ++depth;
    }
    return depth;
}

// A synthetic folder has no volume metadata, and a moved node's Info describes its source, so the
// target volume comes from the nearest real ancestor that stays where it is.
DWORD VolumeOf(const COverlay& overlay, const std::wstring& path, DWORD fallback)
{
    const COverlayNode* location = FindLocationNode(overlay, path);
    if (location != NULL && location->Info.VolumeSerial)
        return location->Info.VolumeSerial;
    return fallback;
}

CCompiledStep MakeStoreDirStep(const std::wstring& placeholder, const std::wstring& node)
{
    CCompiledStep step;
    step.Kind = StepCreateDir;
    step.Role = RoleStore;
    step.Target = placeholder;
    step.StorePlaceholder = placeholder;
    step.Dir = true;
    step.Node = node;
    step.Reason = L"recovery store";
    step.Flags = kStepFSourceIsDir | kStepFCreateDirAcceptExisting | kStepFTargetMustNotExist;
    step.Class = RevExact;
    return step;
}

bool IsStoreRole(const CIntent& intent)
{
    return intent.Role == RoleCleanup || intent.Role == RoleDisplace;
}

// Spec 7.6.3 step 3: a step names the path the host will find at that point, which is the node's
// current place in S and differs from the snapshot path once a folder holding it has moved.
std::wstring CurrentSource(const CSim& sim, const CIntent& intent)
{
    if (intent.Kind == StepCreateDir)
        return std::wstring();
    std::wstring current = sim.PathOf(intent.NodeKey);
    return current.empty() ? intent.Source : current;
}

// True when a pending intent reads a path strictly inside 'dir'. Simulated keys under a folder share
// its exact spelling, so one ordered lookup finds them.
bool PendingInside(const std::map<std::wstring, int>& pending, const std::wstring& dir)
{
    std::wstring prefix = dir + L"\\";
    std::map<std::wstring, int>::const_iterator next = pending.lower_bound(prefix);
    return next != pending.end() && next->first.compare(0, prefix.size(), prefix) == 0;
}

// True when a pending intent still has to move a folder that holds 'path' and would carry it away.
bool PendingAncestor(const std::map<std::wstring, int>& pending, const std::wstring& path)
{
    for (std::wstring cursor = ParentPath(path); !cursor.empty(); cursor = ParentPath(cursor))
    {
        if (pending.find(cursor) != pending.end())
            return true;
    }
    return false;
}

bool HasPendingIntent(const std::map<std::wstring, std::vector<size_t>>& byNode, const std::vector<CIntent>& intents,
                      const std::wstring& key)
{
    std::map<std::wstring, std::vector<size_t>>::const_iterator found = byNode.find(key);
    if (found == byNode.end())
        return false;
    for (size_t i = 0; i < found->second.size(); ++i)
    {
        if (!intents[found->second[i]].Emitted)
            return true;
    }
    return false;
}

// Spec 7.6.3 step 3 tie-break, behind one preference: a folder move that still has pending intents
// inside it goes after any other ready intent, so children that can leave do so first and the
// folder move does not carry them along.
bool ScheduledBefore(const CIntent& a, bool aPreferred, const CIntent& b, bool bPreferred)
{
    if (aPreferred != bPreferred)
        return aPreferred;
    int da = DepthOf(a.Target);
    int db = DepthOf(b.Target);
    if (da != db)
        return da < db;
    int byTarget = ComparePaths(a.Target, b.Target, true);
    if (byTarget != 0)
        return byTarget < 0;
    return ComparePaths(a.Source, b.Source, true) < 0;
}

// Spec 7.6.3 step 7: a move verifies its source against the snapshot identity, except for an object
// produced by an earlier cross-volume step, whose new file ID cannot be known at compile time.
void SetSourceIdentity(CCompiledStep& step, const COverlayNode* node, bool produced)
{
    if (produced)
    {
        step.Flags &= ~kStepFVerifySourceIdentity;
        return;
    }
    if (node == NULL || !node->Info.HasFileId)
        return;
    step.VerifyIdentity = true;
    step.ExpectedIdentity.Valid = true;
    step.ExpectedIdentity.VolumeSerial = node->Info.VolumeSerial;
    memcpy(step.ExpectedIdentity.FileId, node->Info.FileId, 16);
    step.ExpectedIdentity.IsDir = node->IsDir;
    if (!node->IsDir)
    {
        step.ExpectedIdentity.Size = node->Info.Size;
        step.ExpectedIdentity.LastWrite = node->Info.LastWrite;
    }
}

// Spec 7.6.3 step 4: ~reorg-<plan prefix>-<k> with the smallest k whose name is free in S and is no
// snapshot or proposed path, so two cycles broken in one folder never share a temporary name.
std::wstring FreeTempName(const CSim& sim, const COverlay& overlay, const std::wstring& dir, const std::wstring& planPrefix)
{
    for (int k = 1;; ++k)
    {
        std::wstring candidate = JoinPath(dir, L"~reorg-" + planPrefix + L"-" + std::to_wstring((long long)k));
        bool taken = sim.Occupant(candidate) != sim.Nodes.end();
        for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end() && !taken; ++it)
        {
            taken = (!it->second.OriginalPath.empty() && PathsEqual(it->second.OriginalPath, candidate, false)) ||
                    (!it->second.ProposedPath.empty() && PathsEqual(it->second.ProposedPath, candidate, false));
        }
        if (!taken)
            return candidate;
    }
}

void ReportBlocked(CCompiledPlan& compiled, CIssueSink& sink)
{
    compiled.Error = L"PLN-005";
    CIssue issue;
    issue.Code = L"PLN-005";
    issue.Severity = SevError;
    issue.Text = L"The compiler left an intent blocked without a cycle.";
    issue.Key = MakeIssueKey(L"PLN-005", std::vector<std::wstring>());
    sink.Add(issue);
    compiled.Ok = false;
}

bool ShallowerFirst(const std::wstring& a, const std::wstring& b)
{
    int da = DepthOf(a);
    int db = DepthOf(b);
    if (da != db)
        return da < db;
    return ComparePaths(a, b, true) < 0;
}

std::wstring CanonicalStep(const CCompiledStep& step)
{
    const char* kind = "move";
    if (step.Kind == StepCreateDir)
        kind = "createDir";
    else if (step.Kind == StepCopyDirTime)
        kind = "copyDirTime";
    else if (step.Kind == StepRemoveEmptyDir)
        kind = "removeEmptyDir";
    // The source is always the real path: a store move must not hash like another item's store move
    // with the same placeholder target. Only a store location enters as its placeholder.
    std::wstring src = step.Source;
    std::wstring dst = step.StorePlaceholder.empty() ? step.Target : step.StorePlaceholder;
    std::string line = kind;
    line += "\t";
    line += WideToUtf8(src);
    line += "\t";
    line += WideToUtf8(dst);
    line += "\t";
    char flags[32];
    _snprintf_s(flags, _countof(flags), _TRUNCATE, "%lu", step.Flags);
    line += flags;
    line += "\t";
    for (size_t i = 0; i < step.Deps.size(); ++i)
    {
        if (i)
            line += ",";
        char dep[16];
        _snprintf_s(dep, _countof(dep), _TRUNCATE, "%d", step.Deps[i]);
        line += dep;
    }
    return Utf8ToWide(line);
}

} // namespace

std::wstring StoreToken(DWORD volumeSerial)
{
    return L"{store:" + FormatVolumeSerial(volumeSerial) + L"}";
}

std::wstring StoreBaseToken(DWORD volumeSerial)
{
    return L"{storebase:" + FormatVolumeSerial(volumeSerial) + L"}";
}

CCompiledPlan CompilePlan(const COverlay& overlay, const CPlanDocument& plan, CIssueSink& sink)
{
    CCompiledPlan compiled;
    CSim sim;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        if (it->second.Synthetic || it->second.OriginalPath.empty())
            continue;
        CSimNode node;
        node.NodeKey = it->first;
        node.IsDir = it->second.IsDir;
        node.Volume = it->second.Info.VolumeSerial;
        node.FromCrossVolume = false;
        sim.Put(it->second.OriginalPath, node);
    }
    std::vector<CIntent> intents;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        const COverlayNode& node = it->second;
        if (node.Synthetic && node.IsDir)
        {
            CIntent intent;
            intent.Kind = StepCreateDir;
            intent.Role = node.Key.rfind(L"residual:", 0) == 0 ? RoleEdit : RoleEdit;
            intent.NodeKey = node.Key;
            intent.Target = node.ProposedPath;
            intent.ParentKey = node.ParentKey;
            intent.IsDir = true;
            intent.Reason = node.Origin.empty() ? L"compiler" : node.Origin;
            intent.Depth = DepthOf(node.ProposedPath);
            intents.push_back(intent);
            continue;
        }
        if (node.Synthetic || node.OriginalPath.empty())
            continue;
        if (node.Displaced)
        {
            // A replace resolution sends the occupant to the recovery store; it must precede the
            // incoming move, which waits for the name to become free. Content travels with its root.
            const COverlayNode* parent = overlay.FindKey(node.ParentKey);
            if (parent != NULL && parent->Displaced)
                continue;
            CIntent displace;
            displace.Kind = StepMove;
            displace.Role = RoleDisplace;
            displace.NodeKey = node.Key;
            displace.Source = node.OriginalPath;
            displace.Target = StoreToken(node.Info.VolumeSerial) + L"\\" + node.Name;
            displace.IsDir = node.IsDir;
            displace.Reason = L"replace";
            displace.Depth = DepthOf(node.OriginalPath);
            intents.push_back(displace);
            continue;
        }
        if (node.Carried || node.Change == ChangeUnchanged || node.Change == ChangeContains || node.Change == ChangeMovedWithFolder)
            continue;
        // A node that keeps its path under a different parent (a child excluded from its folder's move,
        // now in a residual folder) still gets an intent: the folder move may carry it away and it must
        // come back. The scheduler settles it without a step when nothing carries it.
        CIntent intent;
        intent.Kind = StepMove;
        intent.NodeKey = node.Key;
        intent.Source = node.OriginalPath;
        intent.Target = node.ProposedPath;
        intent.ParentKey = node.ParentKey;
        intent.IsDir = node.IsDir;
        intent.Reason = node.Origin.empty() ? L"user" : node.Origin;
        DWORD targetVolume = VolumeOf(overlay, ParentPath(node.ProposedPath), node.Info.VolumeSerial);
        intent.Cross = targetVolume != 0 && node.Info.VolumeSerial != 0 && targetVolume != node.Info.VolumeSerial;
        intent.Depth = DepthOf(node.ProposedPath);
        intents.push_back(intent);
    }
    if (plan.Options.CleanupEmptiedFolders)
    {
        // Emptied folders stay Unchanged in the overlay, so a Change-based test never saw them. The
        // store-move readiness check below emits each one after every move out of it, which also
        // puts nested folders before their parents (spec 7.6.2 step 5).
        std::vector<std::wstring> emptied = FindEmptiedFolders(overlay, plan);
        for (size_t i = 0; i < emptied.size(); ++i)
        {
            const COverlayNode* folder = overlay.FindKey(emptied[i]);
            if (folder == NULL)
                continue;
            CIntent intent;
            intent.Kind = StepMove;
            intent.Role = RoleCleanup;
            intent.NodeKey = folder->Key;
            intent.Source = folder->OriginalPath;
            intent.IsDir = true;
            intent.Target = StoreToken(folder->Info.VolumeSerial) + L"\\" + folder->Name;
            intent.Reason = L"cleanup";
            intent.Depth = DepthOf(folder->OriginalPath);
            intents.push_back(intent);
        }
    }

    int storeCounter = 1;
    std::vector<CCompiledStep> steps;
    CDepIndex index;
    const std::wstring planPrefix = plan.PlanId.size() >= 8 ? plan.PlanId.substr(0, 8) : plan.PlanId;
    // Moves and store moves by node, so an occupant in S can be tied to the steps that still move it.
    std::map<std::wstring, std::vector<size_t>> intentsOfNode;
    for (size_t i = 0; i < intents.size(); ++i)
    {
        if (intents[i].Kind == StepMove)
            intentsOfNode[intents[i].NodeKey].push_back(i);
    }
    // Each pass emits or settles an intent, or gives an occupant a temporary name (at most once per
    // intent), so a schedule that makes progress never reaches this bound; one that does is blocked.
    const size_t passLimit = intents.size() * 3 + 16;
    for (size_t pass = 0;; ++pass)
    {
        if (pass > passLimit)
        {
            ReportBlocked(compiled, sink);
            return compiled;
        }
        // Current sources of pending intents, counted because a node can have both a move and a cleanup.
        std::map<std::wstring, int> pending;
        bool anyPending = false;
        for (size_t i = 0; i < intents.size(); ++i)
        {
            if (intents[i].Emitted)
                continue;
            anyPending = true;
            if (intents[i].Kind != StepCreateDir)
                ++pending[CurrentSource(sim, intents[i])];
        }
        if (!anyPending)
            break;
        // An intent whose node already sits at its target, with nothing pending that would carry it
        // away, needs no step: a child a cross-volume expansion left in place, or the source folder the
        // expansion kept for it, which already is the residual folder.
        bool settled = false;
        for (size_t i = 0; i < intents.size(); ++i)
        {
            if (intents[i].Emitted || IsStoreRole(intents[i]))
                continue;
            if (intents[i].Kind == StepCreateDir)
            {
                CSim::const_iterator occupant = sim.Occupant(intents[i].Target);
                if (occupant == sim.Nodes.end() || !occupant->second.IsDir || PendingAncestor(pending, occupant->first) ||
                    HasPendingIntent(intentsOfNode, intents, occupant->second.NodeKey))
                    continue;
                const std::wstring keptPath = occupant->first;
                // No step is emitted, so the kept folder keeps its producer and later steps into it
                // depend on whatever placed it, never on a step that does not exist.
                CSimNode kept = occupant->second;
                kept.NodeKey = intents[i].NodeKey;
                sim.Put(keptPath, kept);
            }
            else
            {
                std::wstring current = CurrentSource(sim, intents[i]);
                if (!PathsEqual(current, intents[i].Target, false) || PendingAncestor(pending, current))
                    continue;
            }
            intents[i].Emitted = true;
            settled = true;
        }
        if (settled)
            continue;
        int ready = -1;
        bool readyPreferred = false;
        for (size_t i = 0; i < intents.size(); ++i)
        {
            if (intents[i].Emitted)
                continue;
            const std::wstring source = CurrentSource(sim, intents[i]);
            if (IsStoreRole(intents[i]))
            {
                // A folder goes to the store only after every other pending intent that still reads it
                // or a path inside it, so nothing the plan places elsewhere is carried into the store.
                // Store moves target a placeholder whose folders are created with the move itself.
                std::map<std::wstring, int>::const_iterator self = pending.find(source);
                if ((self != pending.end() && self->second > 1) || PendingInside(pending, source))
                    continue;
            }
            else
            {
                // The target parent must exist in S itself: a folder that has moved no longer counts at
                // its old path, and one that has not moved yet is still found there.
                std::wstring parent = ParentPath(intents[i].Target);
                if (!parent.empty() && !IsRootPath(parent))
                {
                    CSim::const_iterator holder = sim.Nodes.find(parent);
                    if (holder == sim.Nodes.end())
                        continue;
                    // Spec 7.6.3 step 2(a): only the intended parent counts, or an entry that nothing pending
                    // moves or carries away. A different node still due to leave (the old folder at a name
                    // a new folder takes) would carry the item off its final path. A kept source folder is
                    // re-keyed when a residual folder settles onto it, and a cross-volume expansion target
                    // takes the moved folder's key, so both match the proposed tree.
                    if (holder->second.NodeKey != intents[i].ParentKey &&
                        (HasPendingIntent(intentsOfNode, intents, holder->second.NodeKey) || PendingAncestor(pending, parent)))
                        continue;
                }
                if (sim.Occupant(intents[i].Target) != sim.Nodes.end())
                    continue;
            }
            const bool preferred = !(intents[i].Kind == StepMove && intents[i].IsDir && !IsStoreRole(intents[i]) &&
                                     PendingInside(pending, source));
            if (ready < 0 || ScheduledBefore(intents[i], preferred, intents[ready], readyPreferred))
            {
                ready = (int)i;
                readyPreferred = preferred;
            }
        }
        if (ready < 0)
        {
            // Spec 7.6.3 step 4: nothing is ready, so look for pending moves whose node holds a name a
            // blocked intent waits for; a temporary name for that occupant frees the name. An intent
            // that waits only for a parent, or for an ancestor to carry it away, is not helped by a
            // rename, and with no such occupant the plan is blocked without a cycle (PLN-005). When
            // another node holds the parent's path, the parent's own intent waits for that name, so the
            // holder is found through it.
            int cycle = -1;
            for (size_t i = 0; i < intents.size(); ++i)
            {
                if (intents[i].Emitted || IsStoreRole(intents[i]))
                    continue;
                CSim::const_iterator occupant = sim.Occupant(intents[i].Target);
                if (occupant == sim.Nodes.end() || occupant->second.NodeKey == intents[i].NodeKey)
                    continue;
                std::map<std::wstring, std::vector<size_t>>::const_iterator held = intentsOfNode.find(occupant->second.NodeKey);
                if (held == intentsOfNode.end())
                    continue;
                for (size_t h = 0; h < held->second.size(); ++h)
                {
                    const CIntent& member = intents[held->second[h]];
                    // Store moves never occupy a planned name, so renaming one cannot break a cycle.
                    if (member.Emitted || IsStoreRole(member))
                        continue;
                    if (cycle < 0 || ComparePaths(member.Source, intents[cycle].Source, true) < 0)
                        cycle = (int)held->second[h];
                }
            }
            if (cycle < 0)
            {
                ReportBlocked(compiled, sink);
                return compiled;
            }
            const CIntent& member = intents[cycle];
            // The temporary name goes next to the member's current location, which may be inside a
            // folder that moved earlier, and the member's subtree goes with it.
            const std::wstring source = CurrentSource(sim, member);
            const std::wstring temp = FreeTempName(sim, overlay, ParentPath(source), planPrefix);
            CCompiledStep step;
            step.Kind = StepMove;
            step.Role = RoleTempRename;
            step.Source = source;
            step.Target = temp;
            step.Dir = member.IsDir;
            step.Node = member.NodeKey;
            step.Reason = L"compiler";
            step.Flags = kStepFTargetMustNotExist | kStepFVerifySourceIdentity;
            if (step.Dir)
                step.Flags |= kStepFSourceIsDir;
            step.Class = RevExact;
            CSim::const_iterator located = sim.Nodes.find(source);
            SetSourceIdentity(step, overlay.FindKey(member.NodeKey), located != sim.Nodes.end() && located->second.FromCrossVolume);
            // The temporary name is a step like any other: it relies on whatever placed the member and,
            // for a folder, on the items that left it; the blocked intent then waits on it for the name.
            const int self = (int)steps.size();
            std::vector<int> deps;
            AddPlacementDeps(sim, index, source, deps);
            AddTargetDeps(sim, index, temp, deps);
            if (member.IsDir)
                AddMovedOutDeps(index, member.NodeKey, deps);
            SetDeps(step, deps);
            RecordLeave(sim, index, source, temp, self);
            sim.MoveSubtree(source, temp, self);
            steps.push_back(step);
            CIssue info;
            info.Code = L"COL-005";
            info.Severity = SevInfo;
            info.Text = L"A swap was resolved with a temporary name.";
            info.Key = MakeIssueKey(L"COL-005", std::vector<std::wstring>());
            sink.Add(info);
            continue;
        }
        CIntent& intent = intents[ready];
        intent.Emitted = true;
        // Spec 7.6.3 step 3: the step starts where the node is now in S, which is no longer its
        // snapshot path once a folder holding it has moved or it was given a temporary name.
        const std::wstring source = CurrentSource(sim, intent);
        if (intent.Kind == StepMove && intent.IsDir && intent.Cross)
        {
            // Cross-volume directory moves are expanded here so each journal step is one host action.
            // The host creates and removes one folder per step, so the whole tree is expanded:
            // createDir top-down, file moves, copyDirTime bottom-up, then removeEmptyDir bottom-up.
            // Only descendants whose final placement is the same relative spot in the new tree are
            // part of it; excluded or separately placed items keep their own intents. The subtree is
            // collected under the folder's current path, so items moved into it earlier are included.
            std::vector<std::wstring> dirs(1, source);
            std::vector<std::wstring> files;
            for (CSim::const_iterator child = sim.Nodes.begin(); child != sim.Nodes.end(); ++child)
            {
                if (PathsEqual(child->first, source, false) || !IsUnderPath(child->first, source, false))
                    continue;
                const COverlayNode* node = overlay.FindKey(child->second.NodeKey);
                std::wstring expected = intent.Target + child->first.substr(source.size());
                if (node == NULL || node->Displaced || !PathsEqual(node->ProposedPath, expected, false))
                    continue;
                if (child->second.IsDir)
                    dirs.push_back(child->first);
                else
                    files.push_back(child->first);
            }
            std::sort(dirs.begin(), dirs.end(), ShallowerFirst);
            // Spec 7.6.3 step 8 inside the expansion. The new folders enter S only after the removals
            // below, so until then index.Made answers for them; a folder's time is copied after every
            // step that put a child in it (spec 7.6.2 step 6.3), and is copied before the folder or a
            // child folder is removed from the source.
            std::map<std::wstring, std::vector<int>> filledBy; // target folder -> steps that placed a child
            std::map<std::wstring, int> copyTimeOf;             // source folder -> its copyDirTime step
            for (size_t d = 0; d < dirs.size(); ++d)
            {
                CSim::const_iterator located = sim.Nodes.find(dirs[d]);
                CCompiledStep create;
                create.Kind = StepCreateDir;
                create.Target = intent.Target + dirs[d].substr(source.size());
                create.Dir = true;
                create.Role = RoleEdit;
                create.Node = located != sim.Nodes.end() ? located->second.NodeKey : intent.NodeKey;
                create.Reason = intent.Reason;
                create.Flags = kStepFSourceIsDir | kStepFCreateDirAcceptExisting | kStepFTargetMustNotExist;
                create.Class = RevWithLoss;
                create.CrossVolume = true;
                std::vector<int> deps;
                AddTargetDeps(sim, index, create.Target, deps);
                SetDeps(create, deps);
                index.Made[create.Target] = (int)steps.size();
                steps.push_back(create);
            }
            for (size_t f = 0; f < files.size(); ++f)
            {
                CSim::iterator child = sim.Nodes.find(files[f]);
                if (child == sim.Nodes.end())
                    continue;
                CCompiledStep move;
                move.Kind = StepMove;
                move.Source = child->first;
                move.Target = intent.Target + child->first.substr(source.size());
                move.Role = RoleEdit;
                move.Node = child->second.NodeKey;
                move.Reason = intent.Reason;
                move.CrossVolume = true;
                move.Flags = kStepFAllowCrossVolume | kStepFTargetMustNotExist | kStepFMetadataLossAccepted;
                move.ExpectedMetadataLosses = kLossCreationLastAccess;
                move.Class = RevWithLoss;
                const COverlayNode* node = overlay.FindKey(child->second.NodeKey);
                // A file produced by an earlier cross-volume step has a file ID unknown at compile time.
                if (!child->second.FromCrossVolume && node && node->Info.HasFileId)
                {
                    move.Flags |= kStepFVerifySourceIdentity;
                    move.ExpectedIdentity.Valid = true;
                    move.ExpectedIdentity.VolumeSerial = node->Info.VolumeSerial;
                    memcpy(move.ExpectedIdentity.FileId, node->Info.FileId, 16);
                    move.ExpectedIdentity.Size = node->Info.Size;
                    move.ExpectedIdentity.LastWrite = node->Info.LastWrite;
                    move.VerifyIdentity = true;
                }
                // A file moved into the folder earlier depends on that step, which is also what orders
                // a source produced by an earlier cross-volume step (spec 7.6.3 step 7).
                const int self = (int)steps.size();
                std::vector<int> deps;
                AddPlacementDeps(sim, index, child->first, deps);
                AddTargetDeps(sim, index, move.Target, deps);
                SetDeps(move, deps);
                RecordLeave(sim, index, child->first, move.Target, self);
                filledBy[ParentPath(move.Target)].push_back(self);
                CSimNode moved = child->second;
                moved.FromCrossVolume = true;
                moved.Producer = self;
                sim.Erase(child);
                sim.Put(move.Target, moved);
                steps.push_back(move);
            }
            // Times are copied before any source folder is removed, because removing a child folder
            // updates its parent's last-write time.
            for (size_t d = dirs.size(); d-- > 0;)
            {
                CCompiledStep copyTime;
                copyTime.Kind = StepCopyDirTime;
                copyTime.Source = dirs[d];
                copyTime.Target = intent.Target + dirs[d].substr(source.size());
                copyTime.Dir = true;
                copyTime.Flags = kStepFSourceIsDir;
                copyTime.Class = RevWithLoss;
                copyTime.Role = RoleEdit;
                copyTime.Node = intent.NodeKey;
                const int self = (int)steps.size();
                std::vector<int> deps;
                AddPlacementDeps(sim, index, copyTime.Source, deps);
                AddPlacementDeps(sim, index, copyTime.Target, deps);
                const std::vector<int>& filled = filledBy[copyTime.Target];
                deps.insert(deps.end(), filled.begin(), filled.end());
                SetDeps(copyTime, deps);
                copyTimeOf[dirs[d]] = self;
                filledBy[ParentPath(copyTime.Target)].push_back(self);
                steps.push_back(copyTime);
            }
            for (size_t d = dirs.size(); d-- > 0;)
            {
                std::wstring target = intent.Target + dirs[d].substr(source.size());
                CSim::iterator located = sim.Nodes.find(dirs[d]);
                CSimNode created;
                if (located != sim.Nodes.end())
                    created = located->second;
                else
                    created.NodeKey = intent.NodeKey;
                created.IsDir = true;
                created.FromCrossVolume = true;
                // Simulated keys under a folder share its exact spelling, so one ordered lookup finds them.
                std::wstring prefix = dirs[d] + L"\\";
                CSim::const_iterator rest = sim.Nodes.lower_bound(prefix);
                bool stillHolds = rest != sim.Nodes.end() && rest->first.compare(0, prefix.size(), prefix) == 0;
                // A source folder that still holds excluded items stays where it is.
                if (!stillHolds)
                {
                    CCompiledStep remove;
                    remove.Kind = StepRemoveEmptyDir;
                    remove.Source = dirs[d];
                    remove.Dir = true;
                    remove.Flags = kStepFSourceIsDir;
                    remove.Class = RevWithLoss;
                    remove.Role = RoleEdit;
                    remove.Node = created.NodeKey;
                    // Removing a folder changes its parent's last-write time, so both times are copied
                    // first; like a cleanup, the removal needs every step that emptied the folder.
                    const int self = (int)steps.size();
                    std::vector<int> deps;
                    AddPlacementDeps(sim, index, dirs[d], deps);
                    deps.push_back(copyTimeOf[dirs[d]]);
                    std::map<std::wstring, int>::const_iterator parentTime = copyTimeOf.find(ParentPath(dirs[d]));
                    if (parentTime != copyTimeOf.end())
                        deps.push_back(parentTime->second);
                    if (located != sim.Nodes.end())
                        AddMovedOutDeps(index, located->second.NodeKey, deps);
                    SetDeps(remove, deps);
                    RecordLeave(sim, index, dirs[d], std::wstring(), self);
                    steps.push_back(remove);
                    if (located != sim.Nodes.end())
                        sim.Erase(located);
                }
                // A kept source folder still names the moved node in Nodes, but Where now points at the
                // new folder, so later sources resolve there. The new folder's producer is its createDir,
                // which S now answers for instead of index.Made.
                std::map<std::wstring, int>::iterator made = index.Made.find(target);
                if (made != index.Made.end())
                {
                    created.Producer = made->second;
                    index.Made.erase(made);
                }
                sim.Put(target, created);
            }
            continue;
        }
        CCompiledStep step;
        step.Kind = intent.Kind == StepCreateDir ? StepCreateDir : StepMove;
        step.Role = intent.Role;
        step.Source = source;
        step.Target = intent.Target;
        step.Dir = intent.IsDir;
        step.CrossVolume = intent.Cross;
        step.Node = intent.NodeKey;
        step.Reason = intent.Reason;
        step.Flags = kStepFTargetMustNotExist;
        if (intent.IsDir)
            step.Flags |= kStepFSourceIsDir;
        if (intent.Kind == StepCreateDir)
            step.Flags |= kStepFCreateDirAcceptExisting;
        if (intent.Kind == StepMove)
        {
            step.Flags |= kStepFVerifySourceIdentity;
            const COverlayNode* node = overlay.FindKey(intent.NodeKey);
            // Looked up at the current path, so a node produced by an earlier cross-volume step is
            // recognised even after a later folder move carried it elsewhere.
            CSim::const_iterator located = sim.Nodes.find(source);
            SetSourceIdentity(step, node, located != sim.Nodes.end() && located->second.FromCrossVolume);
            if (intent.Cross)
            {
                step.Flags |= kStepFAllowCrossVolume | kStepFMetadataLossAccepted;
                step.ExpectedMetadataLosses = kLossCreationLastAccess;
                step.Class = RevWithLoss;
            }
            else
                step.Class = intent.Role == RoleCleanup || intent.Role == RoleDisplace ? RevUntilFinalized : RevExact;
            if (node != NULL && !node->Info.PersistentFileIds)
                step.Class = RevWithLoss;
        }
        else
            step.Class = RevExact;
        const bool storeRole = intent.Role == RoleCleanup || intent.Role == RoleDisplace;
        if (storeRole)
        {
            wchar_t counter[16];
            _snwprintf_s(counter, _countof(counter), _TRUNCATE, L"%06d", storeCounter++);
            DWORD volume = 0;
            const COverlayNode* node = overlay.FindKey(intent.NodeKey);
            if (node)
                volume = node->Info.VolumeSerial;
            std::wstring serial = FormatVolumeSerial(volume);
            std::wstring store = StoreToken(volume);
            // The host creates one folder level per step and refuses a move into a missing folder,
            // so the store folders are steps of their own, emitted before their first use.
            if (compiled.StoreRoots.find(serial) == compiled.StoreRoots.end())
            {
                compiled.StoreRoots[serial] = store;
                const int baseStep = (int)steps.size();
                index.Made[StoreBaseToken(volume)] = baseStep;
                steps.push_back(MakeStoreDirStep(StoreBaseToken(volume), intent.NodeKey));
                // {store:} becomes <storebase>\<applyId> when materialized, a parent its placeholder
                // path does not show, so the dependency is named here.
                CCompiledStep storeDir = MakeStoreDirStep(store, intent.NodeKey);
                storeDir.Deps.push_back(baseStep);
                index.Made[store] = (int)steps.size();
                steps.push_back(storeDir);
            }
            std::wstring bucket = store + L"\\" + counter;
            CCompiledStep bucketDir = MakeStoreDirStep(bucket, intent.NodeKey);
            std::vector<int> bucketDeps;
            AddTargetDeps(sim, index, bucket, bucketDeps);
            SetDeps(bucketDir, bucketDeps);
            index.Made[bucket] = (int)steps.size();
            steps.push_back(bucketDir);
            // Stored items keep their original leaf name (spec 7.6.4), whatever path they left from.
            step.StorePlaceholder = bucket + L"\\" + LeafName(intent.Source);
            step.Target = step.StorePlaceholder;
            step.Class = RevUntilFinalized;
        }
        // Spec 7.6.3 step 8, read from S before the step changes it: the target parent and vacated
        // name, and for a move the steps that placed its source; a folder also waits for the items
        // that left it, which for a cleanup is every move out of the directory.
        const int self = (int)steps.size();
        std::vector<int> deps;
        AddTargetDeps(sim, index, step.Target, deps);
        if (intent.Kind == StepMove)
        {
            AddPlacementDeps(sim, index, source, deps);
            if (intent.IsDir)
                AddMovedOutDeps(index, intent.NodeKey, deps);
            RecordLeave(sim, index, source, step.Target, self);
            // Every move, ordinary or to the store, takes the node's whole simulated subtree along;
            // re-keying only the node itself left descendants at paths that no longer exist.
            sim.MoveSubtree(source, step.Target, self);
            CSim::iterator moved = sim.Nodes.find(step.Target);
            if (intent.Cross && moved != sim.Nodes.end())
                moved->second.FromCrossVolume = true;
        }
        else
        {
            CSimNode created;
            created.NodeKey = intent.NodeKey;
            created.IsDir = true;
            created.Producer = self;
            sim.Put(step.Target, created);
        }
        SetDeps(step, deps);
        steps.push_back(step);
    }
    compiled.Steps = steps;
    std::string hashText;
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        if (i)
            hashText.push_back('\n');
        hashText += WideToUtf8(CanonicalStep(compiled.Steps[i]));
    }
    compiled.Hash = Sha256Hex(hashText);
    compiled.Ok = compiled.Error.empty();
    return compiled;
}

} // namespace reorg
