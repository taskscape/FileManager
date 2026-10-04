// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reconcile.h"

namespace reorg
{

std::vector<CReconcileResult> Reconcile(const CCompiledPlan& plan, const CJournal& journal, IFileSystemProbe& probe)
{
    std::vector<int> terminal(plan.Steps.size(), 0);
    for (size_t i = 0; i < journal.Records.size(); ++i)
    {
        const CJournalRecord& record = journal.Records[i];
        if (record.Fields.empty())
            continue;
        if (record.Type == "DONE" || record.Type == "SKIP" || record.Type == "FAIL")
        {
            int index = atoi(record.Fields[0].c_str());
            if (index >= 0 && index < (int)terminal.size())
                terminal[index] = 1;
        }
    }
    std::vector<CReconcileResult> results;
    for (size_t i = 0; i < plan.Steps.size(); ++i)
    {
        if (terminal[i])
            continue;
        const CCompiledStep& step = plan.Steps[i];
        CReconcileResult result;
        result.Index = (int)i;
        result.State = RecNotDone;
        if (step.Kind == StepCopyDirTime)
        {
            result.State = RecNotDone;
            result.Evidence = L"copyDirTime is rerun";
            results.push_back(result);
            continue;
        }
        bool sourceExists = false, sourceDir = false, targetExists = false, targetDir = false;
        DWORD sourceAttr = 0, targetAttr = 0, error = 0;
        if (!step.Source.empty())
            sourceExists = probe.Exists(step.Source, sourceDir, sourceAttr, error);
        if (!step.Target.empty())
            targetExists = probe.Exists(step.Target, targetDir, targetAttr, error);
        if (step.Kind == StepCreateDir)
            result.State = targetExists ? RecDone : RecNotDone;
        else if (step.Kind == StepRemoveEmptyDir)
            result.State = sourceExists ? RecNotDone : RecDone;
        else if (step.Kind == StepMove)
        {
            CFileId sourceId, targetId;
            bool sourceMatch = false, targetMatch = false;
            if (sourceExists && probe.ReadIdentity(step.Source, sourceId, error) && step.ExpectedIdentity.Valid)
                sourceMatch = sourceId.VolumeSerial == step.ExpectedIdentity.VolumeSerial && memcmp(sourceId.FileId, step.ExpectedIdentity.FileId, 16) == 0;
            if (targetExists && probe.ReadIdentity(step.Target, targetId, error))
            {
                if (!step.CrossVolume && step.ExpectedIdentity.Valid)
                    targetMatch = targetId.VolumeSerial == step.ExpectedIdentity.VolumeSerial && memcmp(targetId.FileId, step.ExpectedIdentity.FileId, 16) == 0;
                else
                    targetMatch = targetId.Size == step.ExpectedIdentity.Size;
            }
            if (sourceExists && sourceMatch && !targetExists)
                result.State = RecNotDone;
            else if (!sourceExists && targetExists && targetMatch)
                result.State = RecDone;
            else if (sourceExists && targetExists)
                result.State = step.CrossVolume ? RecNotDone : RecManual;
            else
                result.State = RecManual;
        }
        result.Evidence = result.State == RecDone ? L"done" : (result.State == RecNotDone ? L"notDone" : L"manual");
        results.push_back(result);
    }
    // Spec 7.6.6: a not-done step that depends on a manual step is blocked, and so is one that depends
    // on a blocked step, because Resume runs neither. The compiler only names earlier steps as
    // dependencies, so one pass in step order settles every chain. The state changes in place so each
    // step keeps one result; a step with a terminal record or done on disk is never blocked.
    std::vector<int> resultOf(plan.Steps.size(), -1);
    for (size_t i = 0; i < results.size(); ++i)
        resultOf[results[i].Index] = (int)i;
    for (size_t s = 0; s < plan.Steps.size(); ++s)
    {
        if (resultOf[s] < 0 || results[resultOf[s]].State != RecNotDone)
            continue;
        const std::vector<int>& deps = plan.Steps[s].Deps;
        for (size_t d = 0; d < deps.size(); ++d)
        {
            if (deps[d] < 0 || deps[d] >= (int)s || resultOf[deps[d]] < 0)
                continue;
            EReconcileState state = results[resultOf[deps[d]]].State;
            if (state == RecManual || state == RecBlocked)
            {
                results[resultOf[s]].State = RecBlocked;
                results[resultOf[s]].Evidence = L"blocked";
                break;
            }
        }
    }
    return results;
}

} // namespace reorg
