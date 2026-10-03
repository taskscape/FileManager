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
