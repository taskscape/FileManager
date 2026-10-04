// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "revert.h"

namespace reorg
{

std::wstring ChooseRecoveryStore(const CPlanDocument& plan, const CSnapshotItem& item, const std::wstring& proposedPath, std::wstring& error)
{
    (void)proposedPath;
    if (plan.Options.RecoveryStore.Explicit)
    {
        std::wstring serial = FormatVolumeSerial(item.VolumeSerial);
        std::map<std::wstring, std::wstring>::const_iterator it = plan.Options.RecoveryStore.Paths.find(serial);
        if (it == plan.Options.RecoveryStore.Paths.end())
        {
            error = L"DST-011";
            return std::wstring();
        }
        return it->second;
    }
    const std::vector<CRoot>* groups[] = {&plan.DestinationRoots, &plan.ScopeRoots};
    for (int g = 0; g < 2; ++g)
    {
        for (size_t i = 0; i < groups[g]->size(); ++i)
        {
            const CRoot& root = (*groups[g])[i];
            DWORD serial = 0;
            ParseVolumeSerial(root.VolumeSerial, serial);
            if (serial == item.VolumeSerial && IsUnderPath(item.Path, root.Path, false))
                return JoinPath(root.Path, L".reorg-recovery");
        }
    }
    error = L"DST-011";
    return std::wstring();
}

bool MaterializeRecoveryStores(CCompiledPlan& compiled, const CPlanDocument& plan, const COverlay& overlay,
                               const std::wstring& applyId, std::wstring& error)
{
    error.clear();
    if (applyId.empty())
    {
        error = L"An apply id is required for the recovery store.";
        return false;
    }
    std::map<std::wstring, std::wstring> bases; // volume serial text -> <root>\.reorg-recovery
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        CCompiledStep& step = compiled.Steps[i];
        if (step.StorePlaceholder.empty())
            continue;
        bool base = step.Target.rfind(L"{storebase:", 0) == 0;
        size_t open = step.Target.find(L':');
        size_t close = step.Target.find(L'}');
        if ((!base && step.Target.rfind(L"{store:", 0) != 0) || close == std::wstring::npos || open > close)
        {
            error = L"A recovery-store step has no store placeholder.";
            return false;
        }
        std::wstring serial = step.Target.substr(open + 1, close - open - 1);
        std::map<std::wstring, std::wstring>::iterator found = bases.find(serial);
        if (found == bases.end())
        {
            // The compiler names the stored item as each store step's node, so its location picks
            // the root on the same volume (spec 7.6.4).
            const COverlayNode* node = overlay.FindKey(step.Node);
            std::wstring storeError;
            std::wstring root = node != NULL ? ChooseRecoveryStore(plan, node->Info, node->ProposedPath, storeError) : std::wstring();
            if (root.empty())
            {
                error = L"DST-011: no recovery store is available on volume " + serial + L".";
                return false;
            }
            found = bases.insert(std::make_pair(serial, root)).first;
        }
        std::wstring store = JoinPath(found->second, applyId);
        step.Target = (base ? found->second : store) + step.Target.substr(close + 1);
        compiled.StoreRoots[serial] = store;
    }
    return true;
}

CPlanDocument BuildRevertPlan(const CPlanDocument& original, const CCompiledPlan& compiled, const CJournal& journal, bool finalized)
{
    CPlanDocument plan;
    plan.Kind = PlanRevert;
    plan.PlanId = NewGuid();
    plan.Name = L"Revert " + original.Name;
    plan.RevertOfPlanId = original.PlanId;
    plan.CreatedUtc = UtcNowIso();
    plan.ModifiedUtc = plan.CreatedUtc;
    plan.ScopeRoots = original.ScopeRoots;
    plan.DestinationRoots = original.DestinationRoots;
    plan.FormatVersion = 1;
    // A revert restores the pre-apply tree: folders it empties existed before the apply (or are removed
    // by removeEmptyFolder edits), so sweeping them into a recovery store would not restore that tree.
    plan.Options.CleanupEmptiedFolders = false;
    std::map<int, std::string> done;
    for (size_t i = 0; i < journal.Records.size(); ++i)
    {
        if (journal.Records[i].Type == "DONE" && !journal.Records[i].Fields.empty())
            done[atoi(journal.Records[i].Fields[0].c_str())] = journal.Records[i].Type;
    }
    for (int i = (int)compiled.Steps.size() - 1; i >= 0; --i)
    {
        if (done.find(i) == done.end())
            continue;
        const CCompiledStep& step = compiled.Steps[i];
        if (step.Kind == StepCopyDirTime || step.Role == RoleTempRename)
            continue;
        CEdit edit;
        edit.Seq = NextSeq(plan);
        edit.Origin = L"revert";
        edit.CreatedUtc = plan.CreatedUtc;
        if (step.Kind == StepMove)
        {
            edit.Op = EditMove;
            edit.Source = step.Target;
            edit.DestinationDir = ParentPath(step.Source);
            edit.NewName = LeafName(step.Source);
            if (finalized && (step.Role == RoleDisplace || step.Role == RoleCleanup))
                edit.Note = L"REV-004";
        }
        else if (step.Kind == StepCreateDir)
        {
            edit.Op = EditRemoveEmptyFolder;
            edit.Path = step.Target;
        }
        else if (step.Kind == StepRemoveEmptyDir)
        {
            edit.Op = EditCreateFolder;
            edit.Path = step.Source;
        }
        else
            continue;
        plan.Edits.push_back(edit);
    }
    return plan;
}

} // namespace reorg
