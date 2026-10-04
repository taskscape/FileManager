// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "../../src/plugins/reorganize/core/planio.h"
#include "../../src/plugins/reorganize/core/mapping.h"
#include "../../src/plugins/reorganize/core/compiler.h"
#include "../../src/plugins/reorganize/core/journal.h"
#include "../../src/plugins/reorganize/core/reconcile.h"
#include "../../src/plugins/reorganize/core/revert.h"
#include "../../src/plugins/reorganize/core/report.h"
#include "../../src/plugins/reorganize/core/rules.h"

namespace
{
using namespace reorg;

int FailReorg(const char* message)
{
    fprintf(stderr, "NativeSafetyTests: %s\n", message);
    return 1;
}

CSnapshotItem FileItem(const wchar_t* path, DWORD volume, unsigned __int64 size)
{
    CSnapshotItem item;
    std::wstring normalized, error;
    NormalizePath(path, normalized, error);
    item.Path = normalized;
    item.ParentPath = ParentPath(normalized);
    item.Name = LeafName(normalized);
    item.IsDir = false;
    item.Size = size;
    item.VolumeSerial = volume;
    item.HasFileId = true;
    item.FileId[15] = (BYTE)(volume & 0xFF);
    item.Attributes = FILE_ATTRIBUTE_NORMAL;
    item.FileSystem = L"NTFS";
    item.PersistentFileIds = true;
    return item;
}

CSnapshotItem DirItem(const wchar_t* path, DWORD volume)
{
    CSnapshotItem item = FileItem(path, volume, 0);
    item.IsDir = true;
    item.Attributes = FILE_ATTRIBUTE_DIRECTORY;
    item.HasFileId = true;
    return item;
}

int StepIndex(const CCompiledPlan& compiled, EStepKind kind, const wchar_t* source, const wchar_t* target)
{
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        const CCompiledStep& step = compiled.Steps[i];
        if (step.Kind == kind && (source == NULL || PathsEqual(step.Source, source, false)) &&
            (target == NULL || PathsEqual(step.Target, target, false)))
            return (int)i;
    }
    return -1;
}

// Cleanup assertions count store moves by role, because their store targets are numbered placeholders.
int CountRole(const CCompiledPlan& compiled, EStepRole role)
{
    int count = 0;
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        if (compiled.Steps[i].Role == role)
            ++count;
    }
    return count;
}

int CountMovesFrom(const CCompiledPlan& compiled, const wchar_t* source)
{
    int count = 0;
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        if (compiled.Steps[i].Kind == StepMove && PathsEqual(compiled.Steps[i].Source, source, false))
            ++count;
    }
    return count;
}

CPlanDocument TwoVolumePlan()
{
    CPlanDocument plan;
    plan.PlanId = L"0a1b2c3d-4e5f-4a6b-8c7d-9e0f1a2b3c4d";
    plan.Name = L"Review fixes";
    CRoot scope;
    scope.Id = L"s1";
    scope.Path = L"D:\\Inherited";
    scope.VolumeSerial = L"0x00000011";
    plan.ScopeRoots.push_back(scope);
    CRoot dest;
    dest.Id = L"d1";
    dest.Label = L"Standard";
    dest.Path = L"D:\\Standard";
    dest.VolumeSerial = L"0x00000011";
    plan.DestinationRoots.push_back(dest);
    CRoot archive;
    archive.Id = L"d2";
    archive.Label = L"Archive";
    archive.Path = L"E:\\Archive";
    archive.VolumeSerial = L"0x00000022";
    plan.DestinationRoots.push_back(archive);
    return plan;
}

CPlanDocument WithResolution(const CPlanDocument& plan, const std::wstring& key, EResolutionChoice choice)
{
    CPlanDocument resolved = plan;
    CResolution resolution;
    resolution.IssueKey = key;
    resolution.Choice = choice;
    resolved.Resolutions.push_back(resolution);
    return resolved;
}

CIssueSink Validate(const CPlanDocument& plan, const CSnapshot& snapshot, const COverlay& overlay)
{
    CIssueSink sink;
    CAnalysisContext context;
    context.Plan = &plan;
    context.Snapshot = &snapshot;
    context.Overlay = &overlay;
    ValidatePlan(context, sink);
    return sink;
}

struct CReplayEntry
{
    std::wstring Key;
    bool Dir;
};

bool ReplayParentExists(const std::map<std::wstring, CReplayEntry>& fs, const std::wstring& path)
{
    std::wstring parent = ParentPath(path);
    if (parent.empty() || IsRootPath(parent))
        return true;
    std::map<std::wstring, CReplayEntry>::const_iterator found = fs.find(parent);
    return found != fs.end() && found->second.Dir;
}

bool ReplayHasChildren(const std::map<std::wstring, CReplayEntry>& fs, const std::wstring& dir)
{
    std::wstring prefix = dir + L"\\";
    std::map<std::wstring, CReplayEntry>::const_iterator next = fs.lower_bound(prefix);
    return next != fs.end() && next->first.compare(0, prefix.size(), prefix) == 0;
}

typedef std::map<std::wstring, CReplayEntry> CReplayFs;

CReplayFs ReplaySnapshot(const CSnapshot& snapshot)
{
    CReplayFs fs;
    for (std::map<std::wstring, CSnapshotItem>::const_iterator it = snapshot.Items.begin(); it != snapshot.Items.end(); ++it)
    {
        CReplayEntry entry;
        entry.Key = it->first;
        entry.Dir = it->second.IsDir;
        fs[it->first] = entry;
    }
    return fs;
}

std::string ReplayWhere(const CCompiledPlan& compiled, size_t i)
{
    const CCompiledStep& step = compiled.Steps[i];
    return "step " + std::to_string((long long)i) + " (" + WideToUtf8(step.Source) + " -> " + WideToUtf8(step.Target) + "): ";
}

// Performs one step the way the host does and returns why it would be refused, or an empty string. A
// cleanup must also find its folder empty, or the store move would carry items the plan placed elsewhere.
std::string ReplayStep(CReplayFs& fs, const CCompiledStep& step)
{
    if (step.Kind == StepCreateDir)
    {
        CReplayFs::iterator existing = fs.find(step.Target);
        if (existing != fs.end())
        {
            if (!existing->second.Dir || (step.Flags & kStepFCreateDirAcceptExisting) == 0)
                return "createDir target is occupied";
            return std::string();
        }
        if (!ReplayParentExists(fs, step.Target))
            return "createDir parent is missing";
        CReplayEntry entry;
        entry.Key = step.Node;
        entry.Dir = true;
        fs[step.Target] = entry;
    }
    else if (step.Kind == StepMove)
    {
        if (fs.find(step.Source) == fs.end())
            return "move source does not exist";
        if (fs.find(step.Target) != fs.end())
            return "move target is occupied";
        if (!ReplayParentExists(fs, step.Target))
            return "move target parent is missing";
        if (IsUnderPath(step.Target, step.Source, false))
            return "move target is inside its source";
        if (step.Role == RoleCleanup && ReplayHasChildren(fs, step.Source))
            return "cleanup source is not empty";
        std::vector<std::pair<std::wstring, CReplayEntry>> moved;
        for (CReplayFs::iterator it = fs.begin(); it != fs.end();)
        {
            if (it->first == step.Source || it->first.compare(0, step.Source.size() + 1, step.Source + L"\\") == 0)
            {
                moved.push_back(std::make_pair(step.Target + it->first.substr(step.Source.size()), it->second));
                it = fs.erase(it);
            }
            else
                ++it;
        }
        for (size_t m = 0; m < moved.size(); ++m)
            fs[moved[m].first] = moved[m].second;
    }
    else if (step.Kind == StepCopyDirTime)
    {
        if (fs.find(step.Source) == fs.end() || fs.find(step.Target) == fs.end())
            return "copyDirTime source or target does not exist";
    }
    else if (step.Kind == StepRemoveEmptyDir)
    {
        CReplayFs::iterator existing = fs.find(step.Source);
        if (existing == fs.end() || !existing->second.Dir)
            return "removeEmptyDir source does not exist";
        if (ReplayHasChildren(fs, step.Source))
            return "removeEmptyDir source is not empty";
        fs.erase(existing);
    }
    return std::string();
}

// Replays only the steps marked in 'run', in plan order, from the snapshot.
std::string ReplaySubset(const CSnapshot& snapshot, const CCompiledPlan& compiled, const std::vector<bool>& run)
{
    CReplayFs fs = ReplaySnapshot(snapshot);
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        if (!run[i])
            continue;
        std::string error = ReplayStep(fs, compiled.Steps[i]);
        if (!error.empty())
            return ReplayWhere(compiled, i) + error;
    }
    return std::string();
}

// Spec 7.6.3 step 8 checked by execution. Deps are sorted, unique, earlier steps; a step's dependencies,
// followed transitively, are enough for it to run; and with one step left out together with everything
// that depends on it, as Resume leaves out a manual step and the steps it blocks (spec 7.6.6), every
// remaining step still runs. A missing dependency shows up as a step the host would refuse.
std::string ReplayDependencies(const CSnapshot& snapshot, const CCompiledPlan& compiled)
{
    const size_t count = compiled.Steps.size();
    for (size_t i = 0; i < count; ++i)
    {
        const std::vector<int>& deps = compiled.Steps[i].Deps;
        for (size_t d = 0; d < deps.size(); ++d)
        {
            if (deps[d] < 0 || deps[d] >= (int)i || (d > 0 && deps[d] <= deps[d - 1]))
                return ReplayWhere(compiled, i) + "dependencies are not sorted, unique and earlier";
        }
    }
    for (size_t s = 0; s < count; ++s)
    {
        std::vector<bool> run(count, false);
        run[s] = true;
        // Dependencies name earlier steps, so one backward pass collects the whole closure.
        for (size_t i = s + 1; i-- > 0;)
        {
            if (!run[i])
                continue;
            for (size_t d = 0; d < compiled.Steps[i].Deps.size(); ++d)
                run[compiled.Steps[i].Deps[d]] = true;
        }
        std::string error = ReplaySubset(snapshot, compiled, run);
        if (!error.empty())
            return "with only the dependencies of step " + std::to_string((long long)s) + ": " + error;
    }
    for (size_t m = 0; m < count; ++m)
    {
        std::vector<bool> run(count, true);
        run[m] = false;
        for (size_t i = m + 1; i < count; ++i)
        {
            for (size_t d = 0; d < compiled.Steps[i].Deps.size() && run[i]; ++d)
                run[i] = run[compiled.Steps[i].Deps[d]];
        }
        std::string error = ReplaySubset(snapshot, compiled, run);
        if (!error.empty())
            return "without step " + std::to_string((long long)m) + " and its dependents: " + error;
    }
    return std::string();
}

bool HasDep(const CCompiledPlan& compiled, int step, int dep)
{
    if (step < 0 || dep < 0 || step >= (int)compiled.Steps.size())
        return false;
    const std::vector<int>& deps = compiled.Steps[step].Deps;
    for (size_t i = 0; i < deps.size(); ++i)
    {
        if (deps[i] == dep)
            return true;
    }
    return false;
}

// Executes compiled steps against the snapshot the way the host does, one action per step, and then
// compares the result with the proposed tree. A step that names a path the host would not find at
// that point (a stale source, a missing parent, an occupied target) fails here instead of on disk.
// The step dependencies are then checked by partial replays.
std::string ReplayCompiled(const CSnapshot& snapshot, const COverlay& overlay, const CCompiledPlan& compiled)
{
    CReplayFs fs = ReplaySnapshot(snapshot);
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        std::string error = ReplayStep(fs, compiled.Steps[i]);
        if (!error.empty())
            return ReplayWhere(compiled, i) + error;
    }
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        const COverlayNode& node = it->second;
        if (node.Displaced || node.ProposedPath.empty())
            continue;
        std::map<std::wstring, CReplayEntry>::const_iterator final = fs.find(node.ProposedPath);
        if (final == fs.end())
            return "nothing ends at the proposed path " + WideToUtf8(node.ProposedPath);
        if (node.Synthetic ? !final->second.Dir : final->second.Key != it->first)
            return "the wrong item ends at the proposed path " + WideToUtf8(node.ProposedPath);
    }
    return ReplayDependencies(snapshot, compiled);
}

int FailReplay(const char* message, const std::string& detail, const CCompiledPlan& compiled)
{
    fprintf(stderr, "NativeSafetyTests: %s: %s\n", message, detail.c_str());
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        // Dependencies are printed with each step so a failed dependency assertion names its schedule.
        std::string deps;
        for (size_t d = 0; d < compiled.Steps[i].Deps.size(); ++d)
            deps += (d ? "," : "") + std::to_string((long long)compiled.Steps[i].Deps[d]);
        fprintf(stderr, "  %d kind=%d %s -> %s deps=[%s]\n", (int)i, (int)compiled.Steps[i].Kind,
                WideToUtf8(compiled.Steps[i].Source).c_str(), WideToUtf8(compiled.Steps[i].Target).c_str(), deps.c_str());
    }
    return 1;
}

CSnapshot MovedFolderTree()
{
    CSnapshot tree;
    tree.Add(DirItem(L"D:\\Inherited", 0x11));
    tree.Add(DirItem(L"D:\\Standard", 0x11));
    tree.Add(DirItem(L"E:\\Archive", 0x22));
    tree.Add(DirItem(L"D:\\Inherited\\A", 0x11));
    tree.Add(FileItem(L"D:\\Inherited\\A\\x.txt", 0x11, 3));
    tree.Add(FileItem(L"D:\\Inherited\\A\\y.txt", 0x11, 4));
    return tree;
}

// The scheduler must read every step's source from the simulated state (§7.6.3 step 3): a folder
// move carries its whole subtree, so later steps for its descendants start at the new location.
int TestReorganizeCurrentPaths()
{
    CSnapshot tree = MovedFolderTree();
    CPlanHistory history;

    // Child first: the file can reach its new folder before the parent leaves, so it moves out of the
    // original folder and the folder move no longer carries it.
    CPlanDocument childFirst = TwoVolumePlan();
    if (!StageMove(childFirst, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(childFirst, history, tree, L"D:\\Inherited\\A\\x.txt", L"D:\\Standard\\Deep", L"", L"user").Ok)
        return FailReorg("staging a folder move with a separately placed child was rejected");
    COverlay childFirstOverlay = BuildOverlay(tree, childFirst);
    CIssueSink childFirstIssues = Validate(childFirst, tree, childFirstOverlay);
    CCompiledPlan childFirstCompiled = CompilePlan(childFirstOverlay, childFirst, childFirstIssues);
    std::string replay = ReplayCompiled(tree, childFirstOverlay, childFirstCompiled);
    if (!childFirstCompiled.Ok || !replay.empty())
        return FailReplay("a folder move with a child placed elsewhere did not replay", replay, childFirstCompiled);
    int childOut = StepIndex(childFirstCompiled, StepMove, L"D:\\Inherited\\A\\x.txt", L"D:\\Standard\\Deep\\x.txt");
    int folderMove = StepIndex(childFirstCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
    if (childOut < 0 || folderMove < 0 || childOut > folderMove)
        return FailReplay("a child that could leave first was not moved out before its folder", "", childFirstCompiled);
    // Spec 7.6.3 step 8: the child needs its new parent, and the folder move relies on the child having
    // left, since otherwise it would carry the child along.
    int deep = StepIndex(childFirstCompiled, StepCreateDir, NULL, L"D:\\Standard\\Deep");
    if (!HasDep(childFirstCompiled, childOut, deep) || !HasDep(childFirstCompiled, folderMove, childOut))
        return FailReplay("a child leaving before its folder did not get its dependencies", "", childFirstCompiled);

    // Folder first: the child takes the name the folder vacates, so it can only move after the folder
    // and its step must start inside the folder's new location.
    CPlanDocument folderFirst = TwoVolumePlan();
    if (!StageMove(folderFirst, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(folderFirst, history, tree, L"D:\\Inherited\\A\\x.txt", L"D:\\Inherited", L"A", L"user").Ok)
        return FailReorg("staging a child into the name its folder vacates was rejected");
    COverlay folderFirstOverlay = BuildOverlay(tree, folderFirst);
    CIssueSink folderFirstIssues = Validate(folderFirst, tree, folderFirstOverlay);
    CCompiledPlan folderFirstCompiled = CompilePlan(folderFirstOverlay, folderFirst, folderFirstIssues);
    replay = ReplayCompiled(tree, folderFirstOverlay, folderFirstCompiled);
    if (!folderFirstCompiled.Ok || !replay.empty())
        return FailReplay("a child moved after its folder did not replay", replay, folderFirstCompiled);
    folderMove = StepIndex(folderFirstCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
    int childAfter = StepIndex(folderFirstCompiled, StepMove, L"D:\\Standard\\A\\x.txt", L"D:\\Inherited\\A");
    if (folderMove < 0 || childAfter < 0 || childAfter < folderMove ||
        StepIndex(folderFirstCompiled, StepMove, L"D:\\Inherited\\A\\x.txt", NULL) >= 0)
        return FailReplay("a child moved after its folder kept its original source path", "", folderFirstCompiled);
    if (!folderFirstCompiled.Steps[childAfter].VerifyIdentity ||
        memcmp(folderFirstCompiled.Steps[childAfter].ExpectedIdentity.FileId, tree.Find(L"D:\\Inherited\\A\\x.txt")->FileId, 16) != 0)
        return FailReplay("a child carried by a same-volume folder move lost its identity check", "", folderFirstCompiled);
    // The folder move both carried the child's source and vacated the name the child takes.
    if (!HasDep(folderFirstCompiled, childAfter, folderMove))
        return FailReplay("a child moved after its folder did not depend on the folder move", "", folderFirstCompiled);

    // A child inside a moved folder that moves into a new subfolder of the folder's new location.
    CPlanDocument inside = TwoVolumePlan();
    if (!StageMove(inside, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(inside, history, tree, L"D:\\Inherited\\A\\x.txt", L"D:\\Standard\\A\\Old", L"", L"user").Ok)
        return FailReorg("staging a child into a new subfolder of its moved folder was rejected");
    COverlay insideOverlay = BuildOverlay(tree, inside);
    CIssueSink insideIssues;
    CCompiledPlan insideCompiled = CompilePlan(insideOverlay, inside, insideIssues);
    replay = ReplayCompiled(tree, insideOverlay, insideCompiled);
    if (!insideCompiled.Ok || !replay.empty() ||
        StepIndex(insideCompiled, StepMove, L"D:\\Standard\\A\\x.txt", L"D:\\Standard\\A\\Old\\x.txt") < 0)
        return FailReplay("a child moved within its moved folder did not start at the new location", replay, insideCompiled);
    // The new subfolder's parent was put there by the folder move; the child needs both.
    int insideFolder = StepIndex(insideCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
    int insideOld = StepIndex(insideCompiled, StepCreateDir, NULL, L"D:\\Standard\\A\\Old");
    int insideChild = StepIndex(insideCompiled, StepMove, L"D:\\Standard\\A\\x.txt", L"D:\\Standard\\A\\Old\\x.txt");
    if (!HasDep(insideCompiled, insideOld, insideFolder) || !HasDep(insideCompiled, insideChild, insideOld) ||
        !HasDep(insideCompiled, insideChild, insideFolder))
        return FailReplay("a child moved into a new folder inside its moved folder lacked dependencies", "", insideCompiled);

    // Excluding a child from a folder move leaves it in a residual folder at the original path. The
    // same-volume folder move carries it, so it must come back from the folder's new location.
    for (int order = 0; order < 2; ++order)
    {
        CPlanDocument excluded = TwoVolumePlan();
        bool staged = order == 0
                          ? StageMove(excluded, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok &&
                                StageExclude(excluded, history, L"D:\\Inherited\\A\\x.txt", L"user").Ok
                          : StageExclude(excluded, history, L"D:\\Inherited\\A\\x.txt", L"user").Ok &&
                                StageMove(excluded, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok;
        if (!staged)
            return FailReorg("staging a folder move with an excluded child was rejected");
        COverlay excludedOverlay = BuildOverlay(tree, excluded);
        const COverlayNode* stays = excludedOverlay.FindKey(L"D:\\Inherited\\A\\x.txt");
        if (stays == NULL || !PathsEqual(stays->ProposedPath, L"D:\\Inherited\\A\\x.txt", false))
            return FailReorg("an excluded child did not stay at its original path in the proposed tree");
        CIssueSink excludedIssues;
        CCompiledPlan excludedCompiled = CompilePlan(excludedOverlay, excluded, excludedIssues);
        replay = ReplayCompiled(tree, excludedOverlay, excludedCompiled);
        if (!excludedCompiled.Ok || !replay.empty())
            return FailReplay("a folder move with an excluded child did not replay", replay, excludedCompiled);
        folderMove = StepIndex(excludedCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
        int residual = StepIndex(excludedCompiled, StepCreateDir, NULL, L"D:\\Inherited\\A");
        int back = StepIndex(excludedCompiled, StepMove, L"D:\\Standard\\A\\x.txt", L"D:\\Inherited\\A\\x.txt");
        if (folderMove < 0 || residual < 0 || back < 0 || !(folderMove < residual && residual < back))
            return FailReplay("an excluded child was not returned to its residual folder", "", excludedCompiled);
        // The residual folder takes the name the folder move vacated; the child returns into it from
        // where the folder move carried it.
        if (!HasDep(excludedCompiled, residual, folderMove) || !HasDep(excludedCompiled, back, residual) ||
            !HasDep(excludedCompiled, back, folderMove))
            return FailReplay("an excluded child's return lacked dependencies", "", excludedCompiled);
    }

    // Across volumes the expansion leaves the excluded child and its folder where they are, so the
    // residual folder already exists and neither needs a step.
    CPlanDocument crossExcluded = TwoVolumePlan();
    if (!StageMove(crossExcluded, history, tree, L"D:\\Inherited\\A", L"E:\\Archive", L"", L"user").Ok ||
        !StageExclude(crossExcluded, history, L"D:\\Inherited\\A\\x.txt", L"user").Ok)
        return FailReorg("staging a cross-volume folder move with an excluded child was rejected");
    COverlay crossOverlay = BuildOverlay(tree, crossExcluded);
    CIssueSink crossIssues;
    CCompiledPlan crossCompiled = CompilePlan(crossOverlay, crossExcluded, crossIssues);
    replay = ReplayCompiled(tree, crossOverlay, crossCompiled);
    if (!crossCompiled.Ok || !replay.empty())
        return FailReplay("a cross-volume folder move with an excluded child did not replay", replay, crossCompiled);
    if (StepIndex(crossCompiled, StepMove, L"D:\\Inherited\\A\\y.txt", L"E:\\Archive\\A\\y.txt") < 0 ||
        StepIndex(crossCompiled, StepMove, L"D:\\Inherited\\A\\x.txt", NULL) >= 0 ||
        StepIndex(crossCompiled, StepRemoveEmptyDir, L"D:\\Inherited\\A", NULL) >= 0 ||
        StepIndex(crossCompiled, StepCreateDir, NULL, L"D:\\Inherited\\A") >= 0)
        return FailReplay("a cross-volume folder move did not leave its excluded child in place", "", crossCompiled);
    // The settled residual folder and child emit no step, so nothing may depend on one; the expansion's
    // own steps still chain: the file needs its new folder, whose time is copied after the file arrives.
    int crossRoot = StepIndex(crossCompiled, StepCreateDir, NULL, L"E:\\Archive\\A");
    int crossFile = StepIndex(crossCompiled, StepMove, L"D:\\Inherited\\A\\y.txt", L"E:\\Archive\\A\\y.txt");
    int crossTime = StepIndex(crossCompiled, StepCopyDirTime, L"D:\\Inherited\\A", L"E:\\Archive\\A");
    if (crossRoot < 0 || !crossCompiled.Steps[crossRoot].Deps.empty() || !HasDep(crossCompiled, crossFile, crossRoot) ||
        !HasDep(crossCompiled, crossTime, crossRoot) || !HasDep(crossCompiled, crossTime, crossFile))
        return FailReplay("a cross-volume expansion with a settled child lacked dependencies", "", crossCompiled);

    // A name swap inside a folder that moved earlier: the temporary name belongs in the folder's
    // current location, and the swap steps start there too.
    CSnapshot swapTree = MovedFolderTree();
    CPlanDocument swap = TwoVolumePlan();
    if (!StageMove(swap, history, swapTree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageRename(swap, history, swapTree, L"D:\\Inherited\\A\\x.txt", L"y.txt", L"user").Ok ||
        !StageRename(swap, history, swapTree, L"D:\\Inherited\\A\\y.txt", L"x.txt", L"user").Ok)
        return FailReorg("staging a name swap inside a moved folder was rejected");
    COverlay swapOverlay = BuildOverlay(swapTree, swap);
    CIssueSink swapIssues;
    CCompiledPlan swapCompiled = CompilePlan(swapOverlay, swap, swapIssues);
    replay = ReplayCompiled(swapTree, swapOverlay, swapCompiled);
    if (!swapCompiled.Ok || !replay.empty() || swapIssues.Count(L"COL-005") != 1)
        return FailReplay("a name swap inside a moved folder did not replay", replay, swapCompiled);
    std::wstring tempName = L"D:\\Standard\\A\\~reorg-0a1b2c3d-1";
    int temp = StepIndex(swapCompiled, StepMove, L"D:\\Standard\\A\\x.txt", tempName.c_str());
    if (temp < 0 || swapCompiled.Steps[temp].Role != RoleTempRename || !swapCompiled.Steps[temp].VerifyIdentity ||
        StepIndex(swapCompiled, StepMove, tempName.c_str(), L"D:\\Standard\\A\\y.txt") < 0)
        return FailReplay("a swap inside a moved folder did not use a temporary name at the current location", "", swapCompiled);
    // The temporary rename starts where the folder move carried it and frees the name the other swap
    // member takes; the member leaves its temporary name only after the other one vacated its target.
    int swapFolder = StepIndex(swapCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
    int swapInto = StepIndex(swapCompiled, StepMove, L"D:\\Standard\\A\\y.txt", L"D:\\Standard\\A\\x.txt");
    int swapBack = StepIndex(swapCompiled, StepMove, tempName.c_str(), L"D:\\Standard\\A\\y.txt");
    if (!HasDep(swapCompiled, temp, swapFolder) || !HasDep(swapCompiled, swapInto, temp) || !HasDep(swapCompiled, swapInto, swapFolder) ||
        !HasDep(swapCompiled, swapBack, temp) || !HasDep(swapCompiled, swapBack, swapInto))
        return FailReplay("a swap with a temporary name lacked dependencies", "", swapCompiled);

    // Intent targets are final paths, so the entry at a target's parent path counts only when it is the
    // intended parent (spec 7.6.3 step 2(a)). z moves into the folder that takes the name A vacates: a new
    // folder, or the residual folder kept for an excluded x. A's target is deeper than z's, so a test for
    // any entry at D:\Inherited\A let the tie-break move z into the old A, whose move then carried z away.
    // Across volumes the expansion removes the emptied old folder, or keeps it as the residual folder.
    for (int variant = 0; variant < 4; ++variant)
    {
        const bool crossVolume = (variant & 1) != 0;
        const bool residualFolder = (variant & 2) != 0;
        CSnapshot vacatedTree = MovedFolderTree();
        vacatedTree.Add(FileItem(L"D:\\Inherited\\z.txt", 0x11, 6));
        CPlanDocument vacated = TwoVolumePlan();
        const wchar_t* deeper = crossVolume ? L"E:\\Archive\\Deep\\Deeper" : L"D:\\Standard\\Deep\\Deeper";
        if (!StageMove(vacated, history, vacatedTree, L"D:\\Inherited\\A", deeper, L"", L"user").Ok ||
            (residualFolder && !StageExclude(vacated, history, L"D:\\Inherited\\A\\x.txt", L"user").Ok) ||
            !StageMove(vacated, history, vacatedTree, L"D:\\Inherited\\z.txt", L"D:\\Inherited\\A", L"", L"user").Ok)
            return FailReorg("staging a file into the folder that takes a vacated name was rejected");
        COverlay vacatedOverlay = BuildOverlay(vacatedTree, vacated);
        const COverlayNode* z = vacatedOverlay.FindKey(L"D:\\Inherited\\z.txt");
        if (z == NULL || !PathsEqual(z->ProposedPath, L"D:\\Inherited\\A\\z.txt", false) || z->ParentKey == L"D:\\Inherited\\A")
            return FailReorg("a file staged into a vacated name was not placed into the folder that replaces it");
        CIssueSink vacatedIssues;
        CCompiledPlan vacatedCompiled = CompilePlan(vacatedOverlay, vacated, vacatedIssues);
        replay = ReplayCompiled(vacatedTree, vacatedOverlay, vacatedCompiled);
        if (!vacatedCompiled.Ok || !replay.empty())
            return FailReplay("a file moved into the folder that takes a vacated name did not replay", replay, vacatedCompiled);
        int into = StepIndex(vacatedCompiled, StepMove, L"D:\\Inherited\\z.txt", L"D:\\Inherited\\A\\z.txt");
        int remade = StepIndex(vacatedCompiled, StepCreateDir, NULL, L"D:\\Inherited\\A");
        int removed = StepIndex(vacatedCompiled, StepRemoveEmptyDir, L"D:\\Inherited\\A", NULL);
        if (crossVolume && residualFolder)
        {
            // The kept source folder becomes the residual folder in place, so nothing frees or makes the name.
            if (into < 0 || remade >= 0 || removed >= 0)
                return FailReplay("a cross-volume source folder kept for an excluded child was not reused", "", vacatedCompiled);
            continue;
        }
        // The folder at the name is made after the old one left (or, across volumes, was removed), and z
        // waits for it.
        int left = crossVolume ? removed : StepIndex(vacatedCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\Deep\\Deeper\\A");
        if (left < 0 || remade < 0 || into < 0 || !(left < remade && remade < into) ||
            !HasDep(vacatedCompiled, remade, left) || !HasDep(vacatedCompiled, into, remade))
            return FailReplay("a file was moved into the old folder at its target parent path", "", vacatedCompiled);
    }

    // Z moves into the new folder at A's old name and A then moves into Z. Z must not enter the old A,
    // which could then only move into its own subtree; the new folder waits for the name A still holds,
    // so a temporary name for A frees it instead of a PLN-005 block.
    CSnapshot nestedTree = MovedFolderTree();
    nestedTree.Add(DirItem(L"D:\\Inherited\\Z", 0x11));
    CPlanDocument nestedBack = TwoVolumePlan();
    if (!StageMove(nestedBack, history, nestedTree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(nestedBack, history, nestedTree, L"D:\\Inherited\\Z", L"D:\\Inherited\\A", L"", L"user").Ok ||
        !StageMove(nestedBack, history, nestedTree, L"D:\\Inherited\\A", L"D:\\Inherited\\A\\Z", L"", L"user").Ok)
        return FailReorg("staging a folder into a folder placed at its own old name was rejected");
    COverlay nestedOverlay = BuildOverlay(nestedTree, nestedBack);
    CIssueSink nestedIssues;
    CCompiledPlan nestedCompiled = CompilePlan(nestedOverlay, nestedBack, nestedIssues);
    replay = ReplayCompiled(nestedTree, nestedOverlay, nestedCompiled);
    if (!nestedCompiled.Ok || !replay.empty())
        return FailReplay("a folder moved into a folder placed at its own old name did not replay", replay, nestedCompiled);
    std::wstring nestedTemp = L"D:\\Inherited\\~reorg-0a1b2c3d-1";
    int nestedAway = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\A", nestedTemp.c_str());
    int nestedNew = StepIndex(nestedCompiled, StepCreateDir, NULL, L"D:\\Inherited\\A");
    int nestedZ = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Z", L"D:\\Inherited\\A\\Z");
    int nestedBackA = StepIndex(nestedCompiled, StepMove, nestedTemp.c_str(), L"D:\\Inherited\\A\\Z\\A");
    if (nestedAway < 0 || nestedCompiled.Steps[nestedAway].Role != RoleTempRename || nestedIssues.Count(L"COL-005") != 1 ||
        !(nestedAway < nestedNew && nestedNew < nestedZ && nestedZ < nestedBackA) || !HasDep(nestedCompiled, nestedNew, nestedAway) ||
        !HasDep(nestedCompiled, nestedZ, nestedNew) || !HasDep(nestedCompiled, nestedBackA, nestedZ) ||
        !HasDep(nestedCompiled, nestedBackA, nestedAway))
        return FailReplay("a folder holding the name its new parent's parent takes was not given a temporary name", "", nestedCompiled);

    // Hash determinism: identical input compiles to the same hash, staging the same edits in another
    // order yields the same proposed tree and hash, and a different schedule yields a different one.
    CCompiledPlan childFirstAgain = CompilePlan(childFirstOverlay, childFirst, childFirstIssues);
    CPlanDocument reordered = TwoVolumePlan();
    if (!StageMove(reordered, history, tree, L"D:\\Inherited\\A\\x.txt", L"D:\\Standard\\Deep", L"", L"user").Ok ||
        !StageMove(reordered, history, tree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging the reordered edits was rejected");
    COverlay reorderedOverlay = BuildOverlay(tree, reordered);
    CIssueSink reorderedIssues;
    CCompiledPlan reorderedCompiled = CompilePlan(reorderedOverlay, reordered, reorderedIssues);
    if (childFirstCompiled.Hash.size() != 64 || childFirstAgain.Hash != childFirstCompiled.Hash ||
        reorderedCompiled.Hash != childFirstCompiled.Hash || folderFirstCompiled.Hash == childFirstCompiled.Hash)
        return FailReorg("the compiled hash was not deterministic for a folder move with a separately placed child");
    CCompiledPlan swapAgain = CompilePlan(swapOverlay, swap, swapIssues);
    if (swapAgain.Hash != swapCompiled.Hash)
        return FailReorg("the compiled hash with a temporary rename was not deterministic");
    return 0;
}

// Regressions for the Reorganize review findings: CSV quoting, snapshot refresh, destination
// volume/filesystem, cross-volume tree expansion, conflict resolutions, and the recovery store.
int TestReorganizeReviewFixes()
{
    CSnapshot base;
    base.Add(DirItem(L"D:\\Inherited", 0x11));
    base.Add(FileItem(L"D:\\Inherited\\a.txt", 0x11, 4));
    CMappingResult quoted = ParseMapping("source,destination,note\r\n\"D:\\Inherited\\a.txt\",\"D:\\Standard\\A,B\\x.txt\",\"two\r\nlines\"\r\n",
                                         L"D:\\Inherited", L"D:\\Standard", base);
    if (quoted.Rows.size() != 1 || !quoted.Rows[0].Accepted || quoted.Rows[0].DestinationDir != L"D:\\Standard\\A,B" ||
        quoted.Rows[0].NewName != L"x.txt" || quoted.Rows[0].Note != L"two\r\nlines")
        return FailReorg("a quoted CSV field with a comma or line break was split");

    CMemoryFileSystem memory;
    memory.AddDir(L"D:\\Inherited", 0x11, L"NTFS");
    memory.AddFile(L"D:\\Inherited\\gone.txt", 1, 1, FILE_ATTRIBUTE_NORMAL, 0x11, NULL);
    std::vector<std::wstring> roots(1, L"D:\\Inherited");
    CCancellation cancel;
    CSnapshot captured;
    if (!captured.Capture(memory, roots, true, cancel) || captured.Find(L"D:\\Inherited\\gone.txt") == NULL)
        return FailReorg("the snapshot did not capture a file");
    memory.Nodes.erase(L"D:\\Inherited\\gone.txt");
    if (!captured.Capture(memory, roots, true, cancel) || captured.Find(L"D:\\Inherited\\gone.txt") != NULL ||
        !captured.Children[L"D:\\Inherited"].empty())
        return FailReorg("a snapshot refresh kept a deleted item");
    roots.push_back(L"D:\\Missing");
    if (captured.Capture(memory, roots, true, cancel) || captured.LastErrorPath != L"D:\\Missing")
        return FailReorg("a failed snapshot capture was reported as complete");

    CSnapshot tree;
    tree.Add(DirItem(L"D:\\Inherited", 0x11));
    tree.Add(DirItem(L"D:\\Standard", 0x11));
    tree.Add(DirItem(L"E:\\Archive", 0x22));
    tree.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    tree.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    tree.Add(DirItem(L"D:\\Inherited\\Docs\\Sub", 0x11));
    tree.Add(FileItem(L"D:\\Inherited\\Docs\\Sub\\c.txt", 0x11, 9));

    CPlanDocument crossPlan = TwoVolumePlan();
    CPlanHistory history;
    if (!StageMove(crossPlan, history, tree, L"D:\\Inherited\\Docs", L"E:\\Archive", L"", L"user").Ok)
        return FailReorg("staging a cross-volume folder move was rejected");
    COverlay crossOverlay = BuildOverlay(tree, crossPlan);
    CIssueSink crossIssues;
    CCompiledPlan cross = CompilePlan(crossOverlay, crossPlan, crossIssues);
    int createRoot = StepIndex(cross, StepCreateDir, NULL, L"E:\\Archive\\Docs");
    int createSub = StepIndex(cross, StepCreateDir, NULL, L"E:\\Archive\\Docs\\Sub");
    int moveB = StepIndex(cross, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"E:\\Archive\\Docs\\b.txt");
    int moveC = StepIndex(cross, StepMove, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"E:\\Archive\\Docs\\Sub\\c.txt");
    int timeSub = StepIndex(cross, StepCopyDirTime, L"D:\\Inherited\\Docs\\Sub", L"E:\\Archive\\Docs\\Sub");
    int timeRoot = StepIndex(cross, StepCopyDirTime, L"D:\\Inherited\\Docs", L"E:\\Archive\\Docs");
    int removeSub = StepIndex(cross, StepRemoveEmptyDir, L"D:\\Inherited\\Docs\\Sub", NULL);
    int removeRoot = StepIndex(cross, StepRemoveEmptyDir, L"D:\\Inherited\\Docs", NULL);
    if (!cross.Ok || createRoot < 0 || createSub < 0 || moveB < 0 || moveC < 0 || timeSub < 0 || timeRoot < 0 || removeSub < 0 || removeRoot < 0)
        return FailReorg("a cross-volume folder move did not expand its whole subtree");
    if (!(createRoot < createSub && createSub < moveB && createSub < moveC && moveC < timeSub && timeSub < timeRoot &&
          timeRoot < removeSub && removeSub < removeRoot))
        return FailReorg("a cross-volume folder expansion was not top-down creates, moves, then bottom-up times and removals");
    if ((cross.Steps[moveC].Flags & kStepFAllowCrossVolume) == 0 || cross.Steps[createSub].VerifyIdentity)
        return FailReorg("cross-volume expansion steps carried the wrong flags");

    CPlanDocument newFolderPlan = TwoVolumePlan();
    if (!StageMove(newFolderPlan, history, tree, L"D:\\Inherited\\Docs\\b.txt", L"E:\\Archive\\New\\Deeper", L"", L"user").Ok)
        return FailReorg("staging a move into a new folder was rejected");
    COverlay newFolderOverlay = BuildOverlay(tree, newFolderPlan);
    CIssueSink newFolderIssues;
    CCompiledPlan newFolder = CompilePlan(newFolderOverlay, newFolderPlan, newFolderIssues);
    int deepMove = StepIndex(newFolder, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"E:\\Archive\\New\\Deeper\\b.txt");
    if (deepMove < 0 || !newFolder.Steps[deepMove].CrossVolume || (newFolder.Steps[deepMove].Flags & kStepFAllowCrossVolume) == 0)
        return FailReorg("a move into a new folder on another volume was compiled as same-volume");

    CSnapshot fat = tree;
    CSnapshotItem fatRoot = DirItem(L"E:\\Archive", 0x22);
    fatRoot.FileSystem = L"FAT32";
    fatRoot.ReadOnlyVolume = true;
    fat.Items[fatRoot.Path] = fatRoot;
    fat.Add(FileItem(L"D:\\Inherited\\big.iso", 0x11, 5ull * 1024ull * 1024ull * 1024ull));
    CPlanDocument fatPlan = TwoVolumePlan();
    if (!StageMove(fatPlan, history, fat, L"D:\\Inherited\\big.iso", L"E:\\Archive\\New", L"", L"user").Ok)
        return FailReorg("staging a move to FAT32 was rejected");
    COverlay fatOverlay = BuildOverlay(fat, fatPlan);
    CIssueSink fatIssues = Validate(fatPlan, fat, fatOverlay);
    if (fatIssues.Count(L"DST-002") == 0 || fatIssues.Count(L"DST-007") == 0)
        return FailReorg("destination checks used the source volume instead of the destination");

    CSnapshot occupied;
    occupied.Add(DirItem(L"D:\\Inherited", 0x11));
    occupied.Add(DirItem(L"D:\\Standard", 0x11));
    occupied.Add(FileItem(L"D:\\Inherited\\a.txt", 0x11, 4));
    occupied.Add(FileItem(L"D:\\Standard\\a.txt", 0x11, 5));
    CPlanDocument clash = TwoVolumePlan();
    if (!StageMove(clash, history, occupied, L"D:\\Inherited\\a.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a move onto an occupied name was rejected");
    COverlay askOverlay = BuildOverlay(occupied, clash);
    CIssueSink askIssues = Validate(clash, occupied, askOverlay);
    std::wstring clashKey;
    for (size_t i = 0; i < askIssues.Issues.size(); ++i)
    {
        if (askIssues.Issues[i].Code == L"COL-002")
            clashKey = askIssues.Issues[i].Key;
    }
    if (clashKey.empty())
        return FailReorg("an occupied target was not reported as COL-002");

    CPlanDocument keep = WithResolution(clash, clashKey, ResKeepBoth);
    COverlay keepOverlay = BuildOverlay(occupied, keep);
    const COverlayNode* kept = keepOverlay.FindKey(L"D:\\Inherited\\a.txt");
    CIssueSink keepIssues = Validate(keep, occupied, keepOverlay);
    CCompiledPlan keepCompiled = CompilePlan(keepOverlay, keep, keepIssues);
    if (kept == NULL || !PathsEqual(kept->ProposedPath, L"D:\\Standard\\a (2).txt", false) || kept->Change != ChangeKeptBoth ||
        keepIssues.Count(L"COL-001") != 0 || keepIssues.Count(L"COL-002") != 0 ||
        StepIndex(keepCompiled, StepMove, L"D:\\Inherited\\a.txt", L"D:\\Standard\\a (2).txt") < 0)
        return FailReorg("a keep-both resolution did not rename the incoming item");

    CPlanDocument skipDefault = clash;
    skipDefault.Options.ConflictDefault = ConflictSkip;
    COverlay skipOverlay = BuildOverlay(occupied, skipDefault);
    const COverlayNode* skipped = skipOverlay.FindKey(L"D:\\Inherited\\a.txt");
    CIssueSink skipIssues = Validate(skipDefault, occupied, skipOverlay);
    CCompiledPlan skipCompiled = CompilePlan(skipOverlay, skipDefault, skipIssues);
    if (skipped == NULL || skipped->Change != ChangeUnchanged || skipIssues.Count(L"COL-002") != 0 ||
        StepIndex(skipCompiled, StepMove, L"D:\\Inherited\\a.txt", NULL) >= 0)
        return FailReorg("the skip conflict default did not keep the item in place");

    CPlanDocument badMerge = WithResolution(clash, clashKey, ResMerge);
    COverlay badMergeOverlay = BuildOverlay(occupied, badMerge);
    if (Validate(badMerge, occupied, badMergeOverlay).Count(L"COL-002") != 1)
        return FailReorg("a resolution that cannot apply suppressed the conflict");

    CPlanDocument replace = WithResolution(clash, clashKey, ResReplace);
    COverlay replaceOverlay = BuildOverlay(occupied, replace);
    const COverlayNode* occupant = replaceOverlay.FindKey(L"D:\\Standard\\a.txt");
    CIssueSink replaceIssues = Validate(replace, occupied, replaceOverlay);
    if (occupant == NULL || !occupant->Displaced || replaceIssues.Count(L"COL-001") != 0 || replaceIssues.Count(L"COL-002") != 0)
        return FailReorg("a replace resolution did not displace the occupant");
    CCompiledPlan replaced = CompilePlan(replaceOverlay, replace, replaceIssues);
    int storeBase = StepIndex(replaced, StepCreateDir, NULL, L"{storebase:0x00000011}");
    int store = StepIndex(replaced, StepCreateDir, NULL, L"{store:0x00000011}");
    int bucket = StepIndex(replaced, StepCreateDir, NULL, L"{store:0x00000011}\\000001");
    int displace = StepIndex(replaced, StepMove, L"D:\\Standard\\a.txt", L"{store:0x00000011}\\000001\\a.txt");
    int incoming = StepIndex(replaced, StepMove, L"D:\\Inherited\\a.txt", L"D:\\Standard\\a.txt");
    if (!replaced.Ok || storeBase < 0 || store < 0 || bucket < 0 || displace < 0 || incoming < 0 ||
        !(storeBase < store && store < bucket && bucket < displace && displace < incoming) ||
        replaced.Steps[displace].Role != RoleDisplace)
        return FailReorg("a replace did not move the occupant to the recovery store before the incoming move");

    CCompiledPlan materialized = replaced;
    std::wstring storeError;
    if (!MaterializeRecoveryStores(materialized, replace, replaceOverlay, L"apply-1", storeError))
        return FailReorg("recovery store placeholders were not resolved");
    if (!PathsEqual(materialized.Steps[storeBase].Target, L"D:\\Standard\\.reorg-recovery", false) ||
        !PathsEqual(materialized.Steps[displace].Target, L"D:\\Standard\\.reorg-recovery\\apply-1\\000001\\a.txt", false))
        return FailReorg("recovery store placeholders resolved to the wrong folders");
    for (size_t i = 0; i < materialized.Steps.size(); ++i)
    {
        if (materialized.Steps[i].Target.find(L"{store") != std::wstring::npos)
            return FailReorg("a recovery store placeholder survived materialization");
    }
    CPlanDocument rootless = replace;
    rootless.ScopeRoots.clear();
    rootless.DestinationRoots.clear();
    CCompiledPlan noStore = replaced;
    if (MaterializeRecoveryStores(noStore, rootless, replaceOverlay, L"apply-1", storeError) || storeError.rfind(L"DST-011", 0) != 0)
        return FailReorg("a missing recovery store root was not reported as DST-011");

    // The review gate compares hashes, so store placeholders must keep the hash stable across compiles.
    CCompiledPlan rehashed = CompilePlan(replaceOverlay, replace, replaceIssues);
    if (rehashed.Hash != replaced.Hash || rehashed.Hash.size() != 64)
        return FailReorg("the compiled hash with a store move was not deterministic");

    CSnapshot folders;
    folders.Add(DirItem(L"D:\\Inherited", 0x11));
    folders.Add(DirItem(L"D:\\Standard", 0x11));
    folders.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    folders.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    folders.Add(FileItem(L"D:\\Inherited\\Docs\\y.txt", 0x11, 2));
    folders.Add(DirItem(L"D:\\Standard\\Docs", 0x11));
    folders.Add(FileItem(L"D:\\Standard\\Docs\\b.txt", 0x11, 3));
    CPlanDocument mergePlan = TwoVolumePlan();
    if (!StageMove(mergePlan, history, folders, L"D:\\Inherited\\Docs", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a folder onto an existing folder was rejected");
    COverlay folderOverlay = BuildOverlay(folders, mergePlan);
    std::wstring folderKey;
    CIssueSink folderIssues = Validate(mergePlan, folders, folderOverlay);
    for (size_t i = 0; i < folderIssues.Issues.size(); ++i)
    {
        if (folderIssues.Issues[i].Code == L"COL-002")
            folderKey = folderIssues.Issues[i].Key;
    }
    CPlanDocument merge = WithResolution(mergePlan, folderKey, ResMerge);
    // The merged child that clashes again is resolved by the plan default in a later round.
    merge.Options.ConflictDefault = ConflictKeepBoth;
    COverlay mergeOverlay = BuildOverlay(folders, merge);
    const COverlayNode* mergedY = mergeOverlay.FindKey(L"D:\\Inherited\\Docs\\y.txt");
    const COverlayNode* mergedB = mergeOverlay.FindKey(L"D:\\Inherited\\Docs\\b.txt");
    CIssueSink mergeIssues = Validate(merge, folders, mergeOverlay);
    CCompiledPlan merged = CompilePlan(mergeOverlay, merge, mergeIssues);
    int mergeMoveY = StepIndex(merged, StepMove, L"D:\\Inherited\\Docs\\y.txt", L"D:\\Standard\\Docs\\y.txt");
    int mergeMoveB = StepIndex(merged, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard\\Docs\\b (2).txt");
    if (folderKey.empty() || mergedY == NULL || mergedB == NULL ||
        !PathsEqual(mergedY->ProposedPath, L"D:\\Standard\\Docs\\y.txt", false) ||
        !PathsEqual(mergedB->ProposedPath, L"D:\\Standard\\Docs\\b (2).txt", false) ||
        mergeIssues.Count(L"COL-001") != 0 || mergeIssues.Count(L"COL-002") != 0 || mergeIssues.Count(L"COL-006") != 1 ||
        mergeMoveY < 0 || mergeMoveB < 0)
        return FailReorg("a merge resolution did not move the children into the existing folder");
    // The merged source folder is never moved onto the occupant; once its children have left it is
    // only cleaned up into the recovery store (spec 7.6.2 step 4).
    int mergeCleanup = StepIndex(merged, StepMove, L"D:\\Inherited\\Docs", NULL);
    if (CountMovesFrom(merged, L"D:\\Inherited\\Docs") != 1 || mergeCleanup < 0 || merged.Steps[mergeCleanup].Role != RoleCleanup ||
        mergeCleanup < mergeMoveY || mergeCleanup < mergeMoveB)
        return FailReorg("a merged source folder was not cleaned up after its children moved");
    // Folder moves with separately placed or excluded descendants must schedule from current paths.
    return TestReorganizeCurrentPaths();
}

// Regressions for emptied-folder cleanup (spec 7.6.2 step 5). An emptied folder that stays where it is
// keeps the Unchanged classification, so detection must not depend on its change kind.
int TestReorganizeCleanup()
{
    CPlanHistory history;
    CSnapshot single;
    single.Add(DirItem(L"D:\\Inherited", 0x11));
    single.Add(DirItem(L"D:\\Standard", 0x11));
    single.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    single.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    CPlanDocument singlePlan = TwoVolumePlan();
    if (!StageMove(singlePlan, history, single, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a folder's only child out of it was rejected");
    COverlay singleOverlay = BuildOverlay(single, singlePlan);
    std::vector<std::wstring> emptied = FindEmptiedFolders(singleOverlay, singlePlan);
    // The scope root is left without children too, but it holds the recovery store and is never emptied.
    if (emptied.size() != 1 || emptied[0] != L"D:\\Inherited\\Docs")
        return FailReorg("an emptied folder that stays in place was not detected, or a plan root was");
    CIssueSink singleIssues = Validate(singlePlan, single, singleOverlay);
    CCompiledPlan cleaned = CompilePlan(singleOverlay, singlePlan, singleIssues);
    int moveOut = StepIndex(cleaned, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard\\b.txt");
    int storeBase = StepIndex(cleaned, StepCreateDir, NULL, L"{storebase:0x00000011}");
    int store = StepIndex(cleaned, StepCreateDir, NULL, L"{store:0x00000011}");
    int bucket = StepIndex(cleaned, StepCreateDir, NULL, L"{store:0x00000011}\\000001");
    int cleanup = StepIndex(cleaned, StepMove, L"D:\\Inherited\\Docs", L"{store:0x00000011}\\000001\\Docs");
    if (!cleaned.Ok || singleIssues.Count(L"PLN-005") != 0 || moveOut < 0 || cleanup < 0 || CountRole(cleaned, RoleCleanup) != 1)
        return FailReorg("an emptied folder did not get a cleanup step");
    const CCompiledStep& cleanupStep = cleaned.Steps[cleanup];
    if (cleanupStep.Role != RoleCleanup || cleanupStep.Class != RevUntilFinalized || !cleanupStep.Dir ||
        (cleanupStep.Flags & kStepFSourceIsDir) == 0 || !cleanupStep.VerifyIdentity)
        return FailReorg("a cleanup step was not a verified, until-finalized folder move");
    if (!(moveOut < cleanup))
        return FailReorg("a cleanup step ran before the move out of its folder");
    if (storeBase < 0 || store < 0 || bucket < 0 || !(storeBase < store && store < bucket && bucket < cleanup) ||
        cleaned.Steps[bucket].Role != RoleStore)
        return FailReorg("the recovery store folders were not created before the cleanup move");
    CCompiledPlan recleaned = CompilePlan(singleOverlay, singlePlan, singleIssues);
    if (recleaned.Hash != cleaned.Hash)
        return FailReorg("the compiled hash with a cleanup step was not deterministic");

    CCompiledPlan materialized = cleaned;
    std::wstring storeError;
    if (!MaterializeRecoveryStores(materialized, singlePlan, singleOverlay, L"apply-1", storeError) ||
        !PathsEqual(materialized.Steps[cleanup].Target, L"D:\\Inherited\\.reorg-recovery\\apply-1\\000001\\Docs", false))
        return FailReorg("a cleanup step did not resolve to the store on its own volume");

    // Turning the option off changes the steps, so the review hash Apply checks requires a new review.
    CPlanDocument keepPlan = singlePlan;
    keepPlan.Options.CleanupEmptiedFolders = false;
    CCompiledPlan kept = CompilePlan(singleOverlay, keepPlan, singleIssues);
    if (!kept.Ok || CountRole(kept, RoleCleanup) != 0 || CountRole(kept, RoleStore) != 0 || kept.Hash == cleaned.Hash)
        return FailReorg("cleanupEmptiedFolders=false still produced cleanup steps or the same hash");

    CSnapshot nested;
    nested.Add(DirItem(L"D:\\Inherited", 0x11));
    nested.Add(DirItem(L"D:\\Standard", 0x11));
    // Captured so a move to E: is known to cross volumes and is expanded.
    nested.Add(DirItem(L"E:\\Archive", 0x22));
    nested.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    nested.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    nested.Add(DirItem(L"D:\\Inherited\\Docs\\Sub", 0x11));
    nested.Add(FileItem(L"D:\\Inherited\\Docs\\Sub\\c.txt", 0x11, 9));
    CPlanDocument nestedPlan = TwoVolumePlan();
    if (!StageMove(nestedPlan, history, nested, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(nestedPlan, history, nested, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging the nested files out was rejected");
    COverlay nestedOverlay = BuildOverlay(nested, nestedPlan);
    std::vector<std::wstring> nestedEmptied = FindEmptiedFolders(nestedOverlay, nestedPlan);
    if (nestedEmptied.size() != 2 || nestedEmptied[0] != L"D:\\Inherited\\Docs\\Sub" || nestedEmptied[1] != L"D:\\Inherited\\Docs")
        return FailReorg("nested emptied folders were not detected deepest first");
    CIssueSink nestedIssues = Validate(nestedPlan, nested, nestedOverlay);
    CCompiledPlan nestedCompiled = CompilePlan(nestedOverlay, nestedPlan, nestedIssues);
    int nestedMoveB = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard\\b.txt");
    int nestedMoveC = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"D:\\Standard\\c.txt");
    int bucketSub = StepIndex(nestedCompiled, StepCreateDir, NULL, L"{store:0x00000011}\\000001");
    int bucketDocs = StepIndex(nestedCompiled, StepCreateDir, NULL, L"{store:0x00000011}\\000002");
    int cleanSub = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\Sub", L"{store:0x00000011}\\000001\\Sub");
    int cleanDocs = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs", L"{store:0x00000011}\\000002\\Docs");
    if (!nestedCompiled.Ok || nestedMoveB < 0 || nestedMoveC < 0 || bucketSub < 0 || bucketDocs < 0 || cleanSub < 0 || cleanDocs < 0 ||
        CountRole(nestedCompiled, RoleCleanup) != 2)
        return FailReorg("nested emptied folders did not each get a cleanup step");
    if (!(nestedMoveC < cleanSub && cleanSub < cleanDocs && nestedMoveB < cleanDocs && bucketSub < cleanSub && bucketDocs < cleanDocs))
        return FailReorg("nested cleanups did not run bottom-up after the moves out of them");

    // A folder that still holds a non-displaced child stays; only its emptied subfolder is cleaned up.
    CPlanDocument partialPlan = TwoVolumePlan();
    if (!StageMove(partialPlan, history, nested, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging the nested file out was rejected");
    COverlay partialOverlay = BuildOverlay(nested, partialPlan);
    CIssueSink partialIssues = Validate(partialPlan, nested, partialOverlay);
    CCompiledPlan partial = CompilePlan(partialOverlay, partialPlan, partialIssues);
    if (!partial.Ok || CountRole(partial, RoleCleanup) != 1 ||
        StepIndex(partial, StepMove, L"D:\\Inherited\\Docs\\Sub", L"{store:0x00000011}\\000001\\Sub") < 0 ||
        CountMovesFrom(partial, L"D:\\Inherited\\Docs") != 0)
        return FailReorg("a folder that still holds an item was cleaned up");

    // A new folder created inside the emptied folder keeps it.
    CPlanDocument newChildPlan = TwoVolumePlan();
    if (!StageMove(newChildPlan, history, single, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Inherited\\Docs\\New", L"", L"user").Ok)
        return FailReorg("staging a move into a new subfolder was rejected");
    COverlay newChildOverlay = BuildOverlay(single, newChildPlan);
    CIssueSink newChildIssues = Validate(newChildPlan, single, newChildOverlay);
    CCompiledPlan newChild = CompilePlan(newChildOverlay, newChildPlan, newChildIssues);
    if (!newChild.Ok || CountRole(newChild, RoleCleanup) != 0)
        return FailReorg("a folder holding a new folder was cleaned up");

    // A folder that moves takes its content along; neither it nor the root it left is cleaned up.
    CPlanDocument movedPlan = TwoVolumePlan();
    if (!StageMove(movedPlan, history, nested, L"D:\\Inherited\\Docs", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a folder move was rejected");
    COverlay movedOverlay = BuildOverlay(nested, movedPlan);
    CIssueSink movedIssues = Validate(movedPlan, nested, movedOverlay);
    CCompiledPlan moved = CompilePlan(movedOverlay, movedPlan, movedIssues);
    if (!moved.Ok || CountRole(moved, RoleCleanup) != 0 || StepIndex(moved, StepMove, L"D:\\Inherited\\Docs", L"D:\\Standard\\Docs") < 0)
        return FailReorg("a moved folder was cleaned up instead of moved");
    CPlanDocument crossPlan = TwoVolumePlan();
    if (!StageMove(crossPlan, history, nested, L"D:\\Inherited\\Docs", L"E:\\Archive", L"", L"user").Ok)
        return FailReorg("staging a cross-volume folder move was rejected");
    COverlay crossOverlay = BuildOverlay(nested, crossPlan);
    CIssueSink crossIssues = Validate(crossPlan, nested, crossOverlay);
    CCompiledPlan cross = CompilePlan(crossOverlay, crossPlan, crossIssues);
    if (!cross.Ok || CountRole(cross, RoleCleanup) != 0 || StepIndex(cross, StepRemoveEmptyDir, L"D:\\Inherited\\Docs", NULL) < 0)
        return FailReorg("a cross-volume folder move was cleaned up instead of expanded");

    // The displaced occupant leaves its folder, but the incoming item keeps it; the incoming item's
    // own folder is emptied and cleaned up after the move out of it.
    CSnapshot replaced;
    replaced.Add(DirItem(L"D:\\Inherited", 0x11));
    replaced.Add(DirItem(L"D:\\Standard", 0x11));
    replaced.Add(DirItem(L"D:\\Inherited\\Src", 0x11));
    replaced.Add(FileItem(L"D:\\Inherited\\Src\\a.txt", 0x11, 4));
    replaced.Add(DirItem(L"D:\\Standard\\Old", 0x11));
    replaced.Add(FileItem(L"D:\\Standard\\Old\\a.txt", 0x11, 5));
    CPlanDocument clash = TwoVolumePlan();
    if (!StageMove(clash, history, replaced, L"D:\\Inherited\\Src\\a.txt", L"D:\\Standard\\Old", L"", L"user").Ok)
        return FailReorg("staging a move onto an occupied name was rejected");
    COverlay clashOverlay = BuildOverlay(replaced, clash);
    CIssueSink clashIssues = Validate(clash, replaced, clashOverlay);
    std::wstring clashKey;
    for (size_t i = 0; i < clashIssues.Issues.size(); ++i)
    {
        if (clashIssues.Issues[i].Code == L"COL-002")
            clashKey = clashIssues.Issues[i].Key;
    }
    CPlanDocument replacePlan = WithResolution(clash, clashKey, ResReplace);
    COverlay replaceOverlay = BuildOverlay(replaced, replacePlan);
    CIssueSink replaceIssues = Validate(replacePlan, replaced, replaceOverlay);
    CCompiledPlan replacing = CompilePlan(replaceOverlay, replacePlan, replaceIssues);
    int incoming = StepIndex(replacing, StepMove, L"D:\\Inherited\\Src\\a.txt", L"D:\\Standard\\Old\\a.txt");
    int cleanSrc = StepIndex(replacing, StepMove, L"D:\\Inherited\\Src", NULL);
    if (clashKey.empty() || !replacing.Ok || CountRole(replacing, RoleDisplace) != 1 || CountRole(replacing, RoleCleanup) != 1 ||
        incoming < 0 || cleanSrc < 0 || replacing.Steps[cleanSrc].Role != RoleCleanup || !(incoming < cleanSrc) ||
        CountMovesFrom(replacing, L"D:\\Standard\\Old") != 0)
        return FailReorg("a replace cleaned up the receiving folder or did not clean up the emptied source");

    // A revert restores the pre-apply tree, so it must not sweep folders it empties into a store.
    CPlanDocument revert = BuildRevertPlan(singlePlan, cleaned, CJournal(), false);
    if (revert.Kind != PlanRevert || revert.Options.CleanupEmptiedFolders)
        return FailReorg("a revert plan would clean up the folders it empties");
    return 0;
}

std::wstring IssueKeyOf(const CIssueSink& issues, const wchar_t* code)
{
    std::wstring key;
    for (size_t i = 0; i < issues.Issues.size(); ++i)
    {
        if (issues.Issues[i].Code == code)
            key = issues.Issues[i].Key;
    }
    return key;
}

EReconcileState StateOf(const std::vector<CReconcileResult>& results, int index, int& count)
{
    EReconcileState state = RecDone;
    count = 0;
    for (size_t i = 0; i < results.size(); ++i)
    {
        if (results[i].Index == index)
        {
            state = results[i].State;
            ++count;
        }
    }
    return state;
}

// Spec 7.6.3 step 8 dependencies of cross-volume expansions, recovery-store steps and cleanups, and
// their use in reconciliation (spec 7.6.6), where a step depending on a manual step becomes blocked.
int TestReorganizeDependencies()
{
    CPlanHistory history;
    CSnapshot tree;
    tree.Add(DirItem(L"D:\\Inherited", 0x11));
    tree.Add(DirItem(L"D:\\Standard", 0x11));
    tree.Add(DirItem(L"E:\\Archive", 0x22));
    tree.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    tree.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    tree.Add(DirItem(L"D:\\Inherited\\Docs\\Sub", 0x11));
    tree.Add(FileItem(L"D:\\Inherited\\Docs\\Sub\\c.txt", 0x11, 9));

    // Expansion: each file needs its new folder; a folder's time is copied after its children arrive;
    // a source folder is removed after its own and its parent's times are copied and once emptied.
    CPlanDocument crossPlan = TwoVolumePlan();
    if (!StageMove(crossPlan, history, tree, L"D:\\Inherited\\Docs", L"E:\\Archive", L"", L"user").Ok)
        return FailReorg("staging a cross-volume folder move was rejected");
    COverlay crossOverlay = BuildOverlay(tree, crossPlan);
    CIssueSink crossIssues;
    CCompiledPlan cross = CompilePlan(crossOverlay, crossPlan, crossIssues);
    std::string replay = ReplayCompiled(tree, crossOverlay, cross);
    if (!cross.Ok || !replay.empty())
        return FailReplay("a cross-volume folder expansion did not replay with its dependencies", replay, cross);
    int createRoot = StepIndex(cross, StepCreateDir, NULL, L"E:\\Archive\\Docs");
    int createSub = StepIndex(cross, StepCreateDir, NULL, L"E:\\Archive\\Docs\\Sub");
    int moveB = StepIndex(cross, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"E:\\Archive\\Docs\\b.txt");
    int moveC = StepIndex(cross, StepMove, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"E:\\Archive\\Docs\\Sub\\c.txt");
    int timeSub = StepIndex(cross, StepCopyDirTime, L"D:\\Inherited\\Docs\\Sub", L"E:\\Archive\\Docs\\Sub");
    int timeRoot = StepIndex(cross, StepCopyDirTime, L"D:\\Inherited\\Docs", L"E:\\Archive\\Docs");
    int removeSub = StepIndex(cross, StepRemoveEmptyDir, L"D:\\Inherited\\Docs\\Sub", NULL);
    int removeRoot = StepIndex(cross, StepRemoveEmptyDir, L"D:\\Inherited\\Docs", NULL);
    if (createRoot < 0 || !cross.Steps[createRoot].Deps.empty() || !HasDep(cross, createSub, createRoot) ||
        !HasDep(cross, moveB, createRoot) || !HasDep(cross, moveC, createSub) ||
        !HasDep(cross, timeSub, createSub) || !HasDep(cross, timeSub, moveC) ||
        !HasDep(cross, timeRoot, createRoot) || !HasDep(cross, timeRoot, moveB) || !HasDep(cross, timeRoot, timeSub) ||
        !HasDep(cross, removeSub, timeSub) || !HasDep(cross, removeSub, timeRoot) || !HasDep(cross, removeSub, moveC) ||
        !HasDep(cross, removeRoot, timeRoot) || !HasDep(cross, removeRoot, moveB) || !HasDep(cross, removeRoot, removeSub))
        return FailReplay("a cross-volume folder expansion lacked dependencies", "", cross);

    // A name freed by a cross-volume removal is taken only after that removal.
    CSnapshot noteTree = MovedFolderTree();
    noteTree.Add(FileItem(L"D:\\Inherited\\note.txt", 0x11, 5));
    CPlanDocument reuse = TwoVolumePlan();
    if (!StageMove(reuse, history, noteTree, L"D:\\Inherited\\A", L"E:\\Archive", L"", L"user").Ok ||
        !StageRename(reuse, history, noteTree, L"D:\\Inherited\\note.txt", L"A", L"user").Ok)
        return FailReorg("staging a rename into a name vacated across volumes was rejected");
    COverlay reuseOverlay = BuildOverlay(noteTree, reuse);
    CIssueSink reuseIssues;
    CCompiledPlan reused = CompilePlan(reuseOverlay, reuse, reuseIssues);
    replay = ReplayCompiled(noteTree, reuseOverlay, reused);
    if (!reused.Ok || !replay.empty())
        return FailReplay("a rename into a name vacated across volumes did not replay", replay, reused);
    int removeA = StepIndex(reused, StepRemoveEmptyDir, L"D:\\Inherited\\A", NULL);
    int noteMove = StepIndex(reused, StepMove, L"D:\\Inherited\\note.txt", L"D:\\Inherited\\A");
    if (removeA < 0 || !HasDep(reused, noteMove, removeA))
        return FailReplay("a rename into a name freed by a cross-volume removal did not depend on it", "", reused);

    // A new folder inside the cross-volume target depends on the expansion's createDir, and so does a
    // separately placed child moved into it.
    CPlanDocument intoNew = TwoVolumePlan();
    if (!StageMove(intoNew, history, noteTree, L"D:\\Inherited\\A", L"E:\\Archive", L"", L"user").Ok ||
        !StageMove(intoNew, history, noteTree, L"D:\\Inherited\\A\\x.txt", L"E:\\Archive\\A\\Old", L"", L"user").Ok)
        return FailReorg("staging a child into a new folder of a cross-volume target was rejected");
    COverlay intoNewOverlay = BuildOverlay(noteTree, intoNew);
    CIssueSink intoNewIssues;
    CCompiledPlan intoNewCompiled = CompilePlan(intoNewOverlay, intoNew, intoNewIssues);
    replay = ReplayCompiled(noteTree, intoNewOverlay, intoNewCompiled);
    if (!intoNewCompiled.Ok || !replay.empty())
        return FailReplay("a child placed into a new folder of a cross-volume target did not replay", replay, intoNewCompiled);
    int createA = StepIndex(intoNewCompiled, StepCreateDir, NULL, L"E:\\Archive\\A");
    int createOld = StepIndex(intoNewCompiled, StepCreateDir, NULL, L"E:\\Archive\\A\\Old");
    int moveX = StepIndex(intoNewCompiled, StepMove, L"D:\\Inherited\\A\\x.txt", L"E:\\Archive\\A\\Old\\x.txt");
    if (createA < 0 || !HasDep(intoNewCompiled, createOld, createA) || !HasDep(intoNewCompiled, moveX, createOld) ||
        !HasDep(intoNewCompiled, moveX, createA))
        return FailReplay("a folder created inside a cross-volume target did not depend on the expansion", "", intoNewCompiled);

    // Recovery store: each store folder needs the one above it, the displaced item its bucket, and the
    // incoming item the name the displacement vacated.
    CSnapshot occupied;
    occupied.Add(DirItem(L"D:\\Inherited", 0x11));
    occupied.Add(DirItem(L"D:\\Standard", 0x11));
    occupied.Add(FileItem(L"D:\\Inherited\\a.txt", 0x11, 4));
    occupied.Add(FileItem(L"D:\\Standard\\a.txt", 0x11, 5));
    CPlanDocument clash = TwoVolumePlan();
    if (!StageMove(clash, history, occupied, L"D:\\Inherited\\a.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a move onto an occupied name was rejected");
    COverlay clashOverlay = BuildOverlay(occupied, clash);
    std::wstring clashKey = IssueKeyOf(Validate(clash, occupied, clashOverlay), L"COL-002");
    CPlanDocument replace = WithResolution(clash, clashKey, ResReplace);
    COverlay replaceOverlay = BuildOverlay(occupied, replace);
    CIssueSink replaceIssues = Validate(replace, occupied, replaceOverlay);
    CCompiledPlan replaced = CompilePlan(replaceOverlay, replace, replaceIssues);
    replay = ReplayCompiled(occupied, replaceOverlay, replaced);
    if (clashKey.empty() || !replaced.Ok || !replay.empty())
        return FailReplay("a replace did not replay with its dependencies", replay, replaced);
    int storeBase = StepIndex(replaced, StepCreateDir, NULL, L"{storebase:0x00000011}");
    int store = StepIndex(replaced, StepCreateDir, NULL, L"{store:0x00000011}");
    int bucket = StepIndex(replaced, StepCreateDir, NULL, L"{store:0x00000011}\\000001");
    int displace = StepIndex(replaced, StepMove, L"D:\\Standard\\a.txt", L"{store:0x00000011}\\000001\\a.txt");
    int incoming = StepIndex(replaced, StepMove, L"D:\\Inherited\\a.txt", L"D:\\Standard\\a.txt");
    if (storeBase < 0 || !replaced.Steps[storeBase].Deps.empty() || !HasDep(replaced, store, storeBase) ||
        !HasDep(replaced, bucket, store) || !HasDep(replaced, displace, bucket) || !HasDep(replaced, incoming, displace))
        return FailReplay("recovery-store and displacement steps lacked dependencies", "", replaced);

    // Cleanup: every step that moved something out of the folder, nested cleanups included, and the
    // bucket it is stored in. The plan does not end at its proposed tree (the folders go to the store),
    // so only the dependency replays apply.
    CSnapshot nested;
    nested.Add(DirItem(L"D:\\Inherited", 0x11));
    nested.Add(DirItem(L"D:\\Standard", 0x11));
    nested.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    nested.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    nested.Add(DirItem(L"D:\\Inherited\\Docs\\Sub", 0x11));
    nested.Add(FileItem(L"D:\\Inherited\\Docs\\Sub\\c.txt", 0x11, 9));
    CPlanDocument nestedPlan = TwoVolumePlan();
    if (!StageMove(nestedPlan, history, nested, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(nestedPlan, history, nested, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging the nested files out was rejected");
    COverlay nestedOverlay = BuildOverlay(nested, nestedPlan);
    CIssueSink nestedIssues = Validate(nestedPlan, nested, nestedOverlay);
    CCompiledPlan nestedCompiled = CompilePlan(nestedOverlay, nestedPlan, nestedIssues);
    replay = ReplayDependencies(nested, nestedCompiled);
    if (!nestedCompiled.Ok || !replay.empty())
        return FailReplay("nested cleanups did not replay with their dependencies", replay, nestedCompiled);
    int nestedB = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\b.txt", L"D:\\Standard\\b.txt");
    int nestedC = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\Sub\\c.txt", L"D:\\Standard\\c.txt");
    int nestedStore = StepIndex(nestedCompiled, StepCreateDir, NULL, L"{store:0x00000011}");
    int bucketSub = StepIndex(nestedCompiled, StepCreateDir, NULL, L"{store:0x00000011}\\000001");
    int bucketDocs = StepIndex(nestedCompiled, StepCreateDir, NULL, L"{store:0x00000011}\\000002");
    int cleanSub = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs\\Sub", L"{store:0x00000011}\\000001\\Sub");
    int cleanDocs = StepIndex(nestedCompiled, StepMove, L"D:\\Inherited\\Docs", L"{store:0x00000011}\\000002\\Docs");
    if (cleanSub < 0 || cleanDocs < 0 || !HasDep(nestedCompiled, cleanSub, nestedC) || !HasDep(nestedCompiled, cleanSub, bucketSub) ||
        HasDep(nestedCompiled, cleanSub, nestedB) || !HasDep(nestedCompiled, cleanDocs, nestedB) ||
        !HasDep(nestedCompiled, cleanDocs, nestedC) || !HasDep(nestedCompiled, cleanDocs, cleanSub) ||
        !HasDep(nestedCompiled, cleanDocs, bucketDocs) || !HasDep(nestedCompiled, bucketDocs, nestedStore))
        return FailReplay("nested cleanups did not depend on every move out of their folders", "", nestedCompiled);
    CCompiledPlan nestedAgain = CompilePlan(nestedOverlay, nestedPlan, nestedIssues);
    if (nestedAgain.Hash != nestedCompiled.Hash)
        return FailReorg("the compiled hash with dependencies was not deterministic");

    // Reconciliation. The folder moves away, a new folder takes its old name, and a file moves into the
    // new folder: the file depends only on the new folder, which depends on the folder move.
    CSnapshot blockTree = MovedFolderTree();
    blockTree.Add(FileItem(L"D:\\Inherited\\z.txt", 0x11, 6));
    blockTree.Add(FileItem(L"D:\\Inherited\\w.txt", 0x11, 7));
    CPlanDocument blockPlan = TwoVolumePlan();
    if (!StageMove(blockPlan, history, blockTree, L"D:\\Inherited\\A", L"D:\\Standard", L"", L"user").Ok ||
        !StageMove(blockPlan, history, blockTree, L"D:\\Inherited\\z.txt", L"D:\\Inherited\\A", L"", L"user").Ok ||
        !StageMove(blockPlan, history, blockTree, L"D:\\Inherited\\w.txt", L"D:\\Standard", L"", L"user").Ok)
        return FailReorg("staging a file into a new folder at a vacated name was rejected");
    COverlay blockOverlay = BuildOverlay(blockTree, blockPlan);
    CIssueSink blockIssues;
    CCompiledPlan blockCompiled = CompilePlan(blockOverlay, blockPlan, blockIssues);
    replay = ReplayCompiled(blockTree, blockOverlay, blockCompiled);
    if (!blockCompiled.Ok || !replay.empty())
        return FailReplay("a new folder at a vacated name did not replay", replay, blockCompiled);
    int moveA = StepIndex(blockCompiled, StepMove, L"D:\\Inherited\\A", L"D:\\Standard\\A");
    int remade = StepIndex(blockCompiled, StepCreateDir, NULL, L"D:\\Inherited\\A");
    int moveZ = StepIndex(blockCompiled, StepMove, L"D:\\Inherited\\z.txt", L"D:\\Inherited\\A\\z.txt");
    int moveW = StepIndex(blockCompiled, StepMove, L"D:\\Inherited\\w.txt", L"D:\\Standard\\w.txt");
    if (moveA < 0 || moveW < 0 || !HasDep(blockCompiled, remade, moveA) || !HasDep(blockCompiled, moveZ, remade) ||
        HasDep(blockCompiled, moveZ, moveA) || !blockCompiled.Steps[moveW].Deps.empty())
        return FailReplay("a new folder at a vacated name did not chain its dependencies", "", blockCompiled);

    // The apply stopped with the folder at neither its source nor its target, which needs a person
    // (manual). The new folder depends on it and is blocked; the file is blocked through the new folder;
    // the unrelated file stays notDone. Each step keeps exactly one result.
    CMemoryFileSystem disk;
    disk.AddDir(L"D:\\Inherited", 0x11, L"NTFS");
    disk.AddDir(L"D:\\Standard", 0x11, L"NTFS");
    disk.AddFile(L"D:\\Inherited\\z.txt", 6, 0, FILE_ATTRIBUTE_NORMAL, 0x11, blockTree.Find(L"D:\\Inherited\\z.txt")->FileId);
    disk.AddFile(L"D:\\Inherited\\w.txt", 7, 0, FILE_ATTRIBUTE_NORMAL, 0x11, blockTree.Find(L"D:\\Inherited\\w.txt")->FileId);
    std::vector<CReconcileResult> reconciled = Reconcile(blockCompiled, CJournal(), disk);
    int countA = 0, countRemade = 0, countZ = 0, countW = 0;
    EReconcileState stateA = StateOf(reconciled, moveA, countA);
    EReconcileState stateRemade = StateOf(reconciled, remade, countRemade);
    EReconcileState stateZ = StateOf(reconciled, moveZ, countZ);
    EReconcileState stateW = StateOf(reconciled, moveW, countW);
    if (reconciled.size() != blockCompiled.Steps.size() || countA != 1 || countRemade != 1 || countZ != 1 || countW != 1)
        return FailReorg("reconciliation did not report exactly one result per step");
    if (stateA != RecManual || stateRemade != RecBlocked || stateZ != RecBlocked || stateW != RecNotDone)
        return FailReorg("a step depending on a manual step, directly or through a blocked one, was not blocked");

    // With the folder move journaled as done, nothing depends on a manual step any more.
    CJournal journal;
    CJournalRecord done;
    done.Type = "DONE";
    done.Fields.push_back(std::to_string((long long)moveA));
    done.ValidCrc = true;
    journal.Records.push_back(done);
    reconciled = Reconcile(blockCompiled, journal, disk);
    StateOf(reconciled, moveA, countA);
    stateRemade = StateOf(reconciled, remade, countRemade);
    stateZ = StateOf(reconciled, moveZ, countZ);
    if (countA != 0 || countRemade != 1 || stateRemade != RecNotDone || countZ != 1 || stateZ != RecNotDone)
        return FailReorg("a step after a journaled step was blocked");
    return 0;
}

int TestReorganizeCore()
{
    CJsonParseResult bad = JsonParse("{");
    if (bad.Ok)
        return FailReorg("JSON accepted a truncated object");
    CJsonParseResult dup = JsonParse("{\"a\":1,\"a\":2}");
    if (dup.Ok)
        return FailReorg("JSON accepted a duplicate key");
    CJsonParseResult surrogate = JsonParse("\"\\uD800\"");
    if (surrogate.Ok)
        return FailReorg("JSON accepted a lone surrogate");
    CJsonParseResult deep = JsonParse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]");
    if (deep.Ok)
        return FailReorg("JSON accepted nesting past the depth limit");
    CJsonParseResult number = JsonParse("{\"n\":9223372036854775807}");
    if (!number.Ok || number.Value.Find("n") == NULL || number.Value.Find("n")->Kind != JsonInt)
        return FailReorg("JSON rejected the maximum int64");
    std::string round = JsonWrite(number.Value);
    CJsonParseResult again = JsonParse(round);
    if (!again.Ok || again.Value.Find("n")->Int != INT64_MAX)
        return FailReorg("JSON did not round-trip an int64");

    CPlanDocument plan;
    plan.PlanId = L"6f1c2a8e-3b7d-4c1e-9a52-0d4b8f2e7c11";
    plan.Name = L"Acme migration";
    plan.CreatedUtc = L"2026-10-03T09:12:44Z";
    plan.ModifiedUtc = plan.CreatedUtc;
    plan.FormatVersion = 1;
    CRoot scope;
    scope.Id = L"s1";
    scope.Path = L"D:\\Inherited";
    scope.VolumeSerial = L"0x00000011";
    scope.FileSystem = L"NTFS";
    plan.ScopeRoots.push_back(scope);
    CRoot dest;
    dest.Id = L"d1";
    dest.Label = L"Standard";
    dest.Path = L"D:\\Standard";
    dest.VolumeSerial = L"0x00000011";
    dest.FileSystem = L"NTFS";
    plan.DestinationRoots.push_back(dest);
    plan.UnknownJson = "{\"futureFlag\":true}\n";
    std::string saved = SavePlanJson(plan);
    CPlanDocument loaded;
    CPlanIoResult io = LoadPlanJson(saved, loaded);
    if (!io.Ok || loaded.Name != plan.Name || loaded.UnknownJson.find("futureFlag") == std::string::npos)
        return FailReorg("plan round-trip dropped an unknown field");
    CPlanIoResult newer = LoadPlanJson("{\"format\":\"open-salamander.reorganization-plan\",\"formatVersion\":2,\"planId\":\"x\",\"name\":\"n\",\"kind\":\"reorganize\",\"createdUtc\":\"t\",\"modifiedUtc\":\"t\"}", loaded);
    if (newer.Ok)
        return FailReorg("a newer plan format was accepted");

    CMemoryFileSystem memory;
    memory.AddDir(L"D:\\Inherited", 0x11, L"NTFS");
    memory.AddFile(L"D:\\Inherited\\a.txt", 4, 10, FILE_ATTRIBUTE_NORMAL, 0x11, FileItem(L"D:\\Inherited\\a.txt", 0x11, 4).FileId);
    std::string original = SavePlanJson(plan);
    memory.Files[L"D:\\plan.reorgplan"] = original;
    memory.Nodes[L"D:\\plan.reorgplan"].Content = original;
    memory.FailReplace = true;
    CPlanIoResult failed = SavePlanFile(memory, L"D:\\plan.reorgplan", plan);
    if (failed.Ok || memory.Files[L"D:\\plan.reorgplan"] != original)
        return FailReorg("an injected replace failure changed the saved plan");

    CSnapshot snapshot;
    snapshot.Add(DirItem(L"D:\\Inherited", 0x11));
    snapshot.Add(DirItem(L"D:\\Standard", 0x11));
    snapshot.Add(FileItem(L"D:\\Inherited\\a.txt", 0x11, 4));
    snapshot.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    snapshot.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    CPlanHistory history;
    CStageResult moved = StageMove(loaded, history, snapshot, L"D:\\Inherited\\a.txt", L"D:\\Standard", L"", L"user");
    if (!moved.Ok)
        return FailReorg("staging a move was rejected");
    COverlay overlay = BuildOverlay(snapshot, loaded);
    const COverlayNode* node = overlay.FindKey(L"D:\\Inherited\\a.txt");
    if (node == NULL || !PathsEqual(node->ProposedPath, L"D:\\Standard\\a.txt", false))
        return FailReorg("the proposed tree did not show the staged move");
    if (!history.Undo(loaded))
        return FailReorg("undo did not restore the plan");
    overlay = BuildOverlay(snapshot, loaded);
    node = overlay.FindKey(L"D:\\Inherited\\a.txt");
    if (node == NULL || node->Change != ChangeUnchanged)
        return FailReorg("undo left a staged move in the proposed tree");
    if (!history.Redo(loaded))
        return FailReorg("redo did not restore the staged move");

    CStageResult cycle = StageMove(loaded, history, snapshot, L"D:\\Inherited", L"D:\\Inherited\\Docs", L"", L"user");
    if (cycle.Ok)
        return FailReorg("a move into a descendant was stored");

    CRule rule;
    rule.Id = L"r1";
    rule.Name = L"Text";
    rule.Order = 1;
    rule.Match.NameMask = L"*.txt";
    rule.Match.ItemType = ItemFile;
    rule.Destination = L"{dest:Standard}\\Inbox\\{name}{ext}";
    std::wstring destDir, newName, error;
    if (!ExpandTemplate(rule.Destination, *snapshot.Find(L"D:\\Inherited\\a.txt"), loaded, snapshot, destDir, newName, error))
        return FailReorg("template expansion failed");
    if (!PathsEqual(destDir, L"D:\\Standard\\Inbox", false) || newName != L"a.txt")
        return FailReorg("template tokens did not expand");
    if (AgreeMaskWide(L"a.txt", L"*.txt", true) == false || AgreeMaskWide(L"a.txt", L"*.doc", true))
        return FailReorg("mask matching diverged from AgreeMask");

    CMappingResult mapping = ParseMapping("source,destination,note\r\n\"D:\\Inherited\\a.txt\",\"D:\\Standard\\renamed.txt\",\"keep\"\r\n", L"D:\\Inherited", L"D:\\Standard", snapshot);
    if (mapping.Rows.size() != 1 || !mapping.Rows[0].Accepted || mapping.Rows[0].NewName != L"renamed.txt")
        return FailReorg("CSV mapping did not accept a quoted row");

    CIssueSink issues;
    overlay = BuildOverlay(snapshot, loaded);
    CAnalysisContext context;
    context.Plan = &loaded;
    context.Snapshot = &snapshot;
    context.Overlay = &overlay;
    ValidatePlan(context, issues);
    CCompiledPlan compiled = CompilePlan(overlay, loaded, issues);
    if (!compiled.Ok || compiled.Steps.empty() || compiled.Hash.size() != 64)
        return FailReorg("the compiler did not produce a hashed plan");
    CCompiledPlan againPlan = CompilePlan(overlay, loaded, issues);
    if (againPlan.Hash != compiled.Hash)
        return FailReorg("the compiled hash changed between identical runs");

    std::vector<std::string> fields;
    fields.push_back("1");
    fields.push_back("plan=" + WideToUtf8(loaded.PlanId));
    std::string record = FormatJournalRecord("REORGJOURNAL", fields);
    CJournal journal;
    if (!ParseJournal(record, journal) || journal.Records.size() != 1)
        return FailReorg("journal round-trip failed");
    std::string torn = record + "DONE|1|crc=00000000";
    if (!ParseJournal(torn, journal) || journal.Records.size() != 1 || journal.ManualOnly)
        return FailReorg("a torn journal suffix was not discarded");
    // Interior corruption must make the whole journal manual-only; only a torn final record may be dropped.
    std::string corrupt = record;
    corrupt.insert(record.size() - 2, "|extra");
    corrupt += FormatJournalRecord("DONE", std::vector<std::string>(1, "0"));
    if (ParseJournal(corrupt, journal) || !journal.ManualOnly)
        return FailReorg("data after an interior journal checksum was accepted");
    std::string interior = record + FormatJournalRecord("DONE", std::vector<std::string>(1, "0"));
    interior.replace(interior.find("crc="), 12, "crc=DEADBEEF");
    // The replaced checksum belongs to the first record only when it is not last. Append a valid tail.
    interior += FormatJournalRecord("END-APPLY", std::vector<std::string>(1, "status=failed"));
    if (ParseJournal(interior, journal) || !journal.ManualOnly)
        return FailReorg("an interior journal record with a bad checksum was accepted");

    CReportRow row;
    row.Kind = L"Move";
    row.Original = L"=cmd|'/c calc'!A1";
    row.Proposed = L"D:\\Standard\\a.txt";
    std::vector<CReportRow> rows;
    rows.push_back(row);
    std::string csv = WriteReportCsv(rows);
    if (csv.find("\"'=cmd") == std::string::npos)
        return FailReorg("CSV report did not neutralize a formula");
    std::string html = WriteReportHtml(L"Acme <migration>", loaded.PlanId, compiled.Hash, rows);
    if (html.find("<migration>") != std::string::npos)
        return FailReorg("HTML report did not escape a value");

    std::wstring storeError;
    CSnapshotItem displaced = *snapshot.Find(L"D:\\Inherited\\a.txt");
    std::wstring store = ChooseRecoveryStore(loaded, displaced, L"D:\\Standard\\a.txt", storeError);
    if (store.empty())
        return FailReorg("recovery store selection failed on the same volume");

    int result = TestReorganizeReviewFixes();
    // Cleanup runs after the review fixes so a failure names its own regression.
    if (result == 0)
        result = TestReorganizeCleanup();
    // Dependencies run last; they assert on schedules the earlier tests already check for order.
    return result != 0 ? result : TestReorganizeDependencies();
}
}
