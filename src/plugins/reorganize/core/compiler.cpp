// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "compiler.h"

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
};

struct CIntent
{
    EStepKind Kind;
    EStepRole Role;
    std::wstring NodeKey;
    std::wstring Source;
    std::wstring Target;
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

DWORD VolumeOf(const COverlay& overlay, const std::wstring& path, DWORD fallback)
{
    const COverlayNode* node = overlay.FindProposed(path);
    if (node && node->Info.VolumeSerial)
        return node->Info.VolumeSerial;
    const COverlayNode* original = overlay.FindKey(path);
    if (original && original->Info.VolumeSerial)
        return original->Info.VolumeSerial;
    return fallback;
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
    std::wstring src = step.StorePlaceholder.empty() ? step.Source : step.StorePlaceholder;
    std::wstring dst = step.Target;
    if (step.Role == RoleDisplace || step.Role == RoleCleanup || step.Role == RoleStore)
    {
        if (dst.find(L"{store:") == std::wstring::npos && !step.StorePlaceholder.empty())
            dst = step.StorePlaceholder;
    }
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

CCompiledPlan CompilePlan(const COverlay& overlay, const CPlanDocument& plan, CIssueSink& sink)
{
    CCompiledPlan compiled;
    std::map<std::wstring, CSimNode> sim;
    for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
    {
        if (it->second.Synthetic || it->second.OriginalPath.empty())
            continue;
        CSimNode node;
        node.NodeKey = it->first;
        node.IsDir = it->second.IsDir;
        node.Volume = it->second.Info.VolumeSerial;
        node.FromCrossVolume = false;
        sim[it->second.OriginalPath] = node;
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
            intent.IsDir = true;
            intent.Reason = node.Origin.empty() ? L"compiler" : node.Origin;
            intent.Depth = DepthOf(node.ProposedPath);
            intents.push_back(intent);
            continue;
        }
        if (node.Synthetic || node.OriginalPath.empty())
            continue;
        if (node.Carried || node.Change == ChangeUnchanged || node.Change == ChangeContains || node.Change == ChangeMovedWithFolder)
            continue;
        if (PathsEqual(node.ProposedPath, node.OriginalPath, false))
            continue;
        CIntent intent;
        intent.Kind = StepMove;
        intent.NodeKey = node.Key;
        intent.Source = node.OriginalPath;
        intent.Target = node.ProposedPath;
        intent.IsDir = node.IsDir;
        intent.Reason = node.Origin.empty() ? L"user" : node.Origin;
        DWORD targetVolume = VolumeOf(overlay, ParentPath(node.ProposedPath), node.Info.VolumeSerial);
        intent.Cross = targetVolume != 0 && node.Info.VolumeSerial != 0 && targetVolume != node.Info.VolumeSerial;
        intent.Depth = DepthOf(node.ProposedPath);
        intents.push_back(intent);
    }
    if (plan.Options.CleanupEmptiedFolders)
    {
        for (std::map<std::wstring, COverlayNode>::const_iterator it = overlay.Nodes.begin(); it != overlay.Nodes.end(); ++it)
        {
            if (!it->second.IsDir || it->second.Synthetic)
                continue;
            if (it->second.Change == ChangeUnchanged || it->second.Change == ChangeMovedWithFolder)
                continue;
            bool hadChildren = false;
            bool stillHas = false;
            for (std::map<std::wstring, COverlayNode>::const_iterator child = overlay.Nodes.begin(); child != overlay.Nodes.end(); ++child)
            {
                if (child->second.OriginalParent == it->second.OriginalPath)
                    hadChildren = true;
                if (child->second.ParentKey == it->first && !child->second.Displaced)
                    stillHas = true;
            }
            if (hadChildren && !stillHas && PathsEqual(it->second.ProposedPath, it->second.OriginalPath, false))
            {
                CIntent intent;
                intent.Kind = StepMove;
                intent.Role = RoleCleanup;
                intent.NodeKey = it->first;
                intent.Source = it->second.OriginalPath;
                intent.IsDir = true;
                intent.Target = L"{store:" + FormatVolumeSerial(it->second.Info.VolumeSerial) + L"}\\000001\\" + it->second.Name;
                intent.Reason = L"cleanup";
                intent.Depth = DepthOf(it->second.OriginalPath);
                intents.push_back(intent);
            }
        }
    }

    int storeCounter = 1;
    std::vector<CCompiledStep> steps;
    int guard = 0;
    while (guard++ < 100000)
    {
        int ready = -1;
        for (size_t i = 0; i < intents.size(); ++i)
        {
            if (intents[i].Emitted)
                continue;
            bool parentOk = true;
            bool nameFree = true;
            if (intents[i].Kind == StepCreateDir || intents[i].Kind == StepMove)
            {
                std::wstring parent = ParentPath(intents[i].Target);
                if (!parent.empty() && !IsRootPath(parent) && sim.find(parent) == sim.end() && intents[i].Role != RoleCleanup)
                {
                    // A parent that is still at its original location counts as existing.
                    parentOk = sim.find(parent) != sim.end() || overlay.FindKey(parent) != NULL;
                }
                if (intents[i].Role != RoleCleanup)
                {
                    for (std::map<std::wstring, CSimNode>::const_iterator occ = sim.begin(); occ != sim.end(); ++occ)
                    {
                        if (PathsEqual(occ->first, intents[i].Target, false))
                            nameFree = false;
                    }
                }
            }
            if (!parentOk || !nameFree)
                continue;
            if (ready < 0 || DepthOf(intents[i].Target) < DepthOf(intents[ready].Target) ||
                (DepthOf(intents[i].Target) == DepthOf(intents[ready].Target) && ComparePaths(intents[i].Target, intents[ready].Target, true) < 0))
                ready = (int)i;
        }
        if (ready < 0)
        {
            int blocked = 0;
            for (size_t i = 0; i < intents.size(); ++i)
            {
                if (!intents[i].Emitted)
                    ++blocked;
            }
            if (blocked == 0)
                break;
            // Break a name cycle with a temporary rename of the smallest original path.
            int cycle = -1;
            for (size_t i = 0; i < intents.size(); ++i)
            {
                if (intents[i].Emitted || intents[i].Kind != StepMove)
                    continue;
                if (cycle < 0 || ComparePaths(intents[i].Source, intents[cycle].Source, true) < 0)
                    cycle = (int)i;
            }
            if (cycle < 0)
            {
                compiled.Error = L"PLN-005";
                CIssue issue;
                issue.Code = L"PLN-005";
                issue.Severity = SevError;
                issue.Text = L"The compiler left an intent blocked without a cycle.";
                issue.Key = MakeIssueKey(L"PLN-005", std::vector<std::wstring>());
                sink.Add(issue);
                compiled.Ok = false;
                return compiled;
            }
            std::wstring planPrefix = plan.PlanId.size() >= 8 ? plan.PlanId.substr(0, 8) : plan.PlanId;
            std::wstring temp = JoinPath(ParentPath(intents[cycle].Source), L"~reorg-" + planPrefix + L"-1");
            CCompiledStep step;
            step.Kind = StepMove;
            step.Role = RoleTempRename;
            step.Source = intents[cycle].Source;
            step.Target = temp;
            step.Dir = intents[cycle].IsDir;
            step.Node = intents[cycle].NodeKey;
            step.Reason = L"compiler";
            step.Flags = kStepFTargetMustNotExist | kStepFVerifySourceIdentity;
            if (step.Dir)
                step.Flags |= kStepFSourceIsDir;
            step.Class = RevExact;
            std::map<std::wstring, CSimNode>::iterator located = sim.find(intents[cycle].Source);
            if (located != sim.end())
            {
                sim[temp] = located->second;
                sim.erase(located);
            }
            intents[cycle].Source = temp;
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
        if (intent.Kind == StepMove && intent.IsDir && intent.Cross)
        {
            // Cross-volume directory moves are expanded here so each journal step is one host action.
            CCompiledStep create;
            create.Kind = StepCreateDir;
            create.Target = intent.Target;
            create.Dir = true;
            create.Role = RoleEdit;
            create.Node = intent.NodeKey;
            create.Reason = intent.Reason;
            create.Flags = kStepFSourceIsDir | kStepFCreateDirAcceptExisting | kStepFTargetMustNotExist;
            create.Class = RevWithLoss;
            create.CrossVolume = true;
            steps.push_back(create);
            for (std::map<std::wstring, CSimNode>::iterator child = sim.begin(); child != sim.end();)
            {
                if (child->first == intent.Source || !IsUnderPath(child->first, intent.Source, false) || child->second.IsDir)
                {
                    ++child;
                    continue;
                }
                CCompiledStep move;
                move.Kind = StepMove;
                move.Source = child->first;
                std::wstring relative = child->first.substr(intent.Source.size());
                move.Target = intent.Target + relative;
                move.Role = RoleEdit;
                move.Node = child->second.NodeKey;
                move.Reason = intent.Reason;
                move.CrossVolume = true;
                move.Flags = kStepFAllowCrossVolume | kStepFTargetMustNotExist | kStepFMetadataLossAccepted | kStepFVerifySourceIdentity;
                move.ExpectedMetadataLosses = kLossCreationLastAccess;
                move.Class = RevWithLoss;
                const COverlayNode* node = overlay.FindKey(child->second.NodeKey);
                if (node && node->Info.HasFileId)
                {
                    move.ExpectedIdentity.Valid = true;
                    move.ExpectedIdentity.VolumeSerial = node->Info.VolumeSerial;
                    memcpy(move.ExpectedIdentity.FileId, node->Info.FileId, 16);
                    move.ExpectedIdentity.Size = node->Info.Size;
                    move.ExpectedIdentity.LastWrite = node->Info.LastWrite;
                    move.VerifyIdentity = true;
                }
                std::map<std::wstring, CSimNode>::iterator next = child;
                ++next;
                CSimNode moved = child->second;
                moved.FromCrossVolume = true;
                sim.erase(child);
                sim[move.Target] = moved;
                steps.push_back(move);
                child = next;
            }
            CCompiledStep copyTime;
            copyTime.Kind = StepCopyDirTime;
            copyTime.Source = intent.Source;
            copyTime.Target = intent.Target;
            copyTime.Dir = true;
            copyTime.Flags = kStepFSourceIsDir;
            copyTime.Class = RevWithLoss;
            copyTime.Role = RoleEdit;
            copyTime.Node = intent.NodeKey;
            steps.push_back(copyTime);
            CCompiledStep remove;
            remove.Kind = StepRemoveEmptyDir;
            remove.Source = intent.Source;
            remove.Dir = true;
            remove.Flags = kStepFSourceIsDir;
            remove.Class = RevWithLoss;
            remove.Role = RoleEdit;
            remove.Node = intent.NodeKey;
            steps.push_back(remove);
            sim.erase(intent.Source);
            CSimNode created;
            created.NodeKey = intent.NodeKey;
            created.IsDir = true;
            created.FromCrossVolume = true;
            sim[intent.Target] = created;
            continue;
        }
        CCompiledStep step;
        step.Kind = intent.Kind == StepCreateDir ? StepCreateDir : StepMove;
        step.Role = intent.Role;
        step.Source = intent.Source;
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
            std::map<std::wstring, CSimNode>::iterator located = sim.find(intent.Source);
            bool produced = located != sim.end() && located->second.FromCrossVolume;
            if (!produced && node && node->Info.HasFileId)
            {
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
            else if (produced)
                step.Flags &= ~kStepFVerifySourceIdentity;
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
        if (intent.Role == RoleCleanup || intent.Role == RoleDisplace)
        {
            wchar_t counter[16];
            _snwprintf_s(counter, _countof(counter), _TRUNCATE, L"%06d", storeCounter++);
            DWORD volume = 0;
            const COverlayNode* node = overlay.FindKey(intent.NodeKey);
            if (node)
                volume = node->Info.VolumeSerial;
            step.StorePlaceholder = L"{store:" + FormatVolumeSerial(volume) + L"}\\" + counter + L"\\" + LeafName(intent.Source);
            step.Target = step.StorePlaceholder;
            step.Class = RevUntilFinalized;
        }
        if (intent.Kind == StepMove)
        {
            std::map<std::wstring, CSimNode>::iterator located = sim.find(intent.Source);
            if (located != sim.end())
            {
                CSimNode moved = located->second;
                if (intent.Cross)
                    moved.FromCrossVolume = true;
                sim.erase(located);
                sim[step.Target] = moved;
            }
        }
        else
        {
            CSimNode created;
            created.NodeKey = intent.NodeKey;
            created.IsDir = true;
            sim[step.Target] = created;
        }
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
