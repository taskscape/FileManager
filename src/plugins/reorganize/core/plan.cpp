// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "plan.h"

namespace reorg
{
namespace
{

CEdit MakeEdit(CPlanDocument& plan, EEditOp op, const std::wstring& origin)
{
    CEdit edit;
    edit.Seq = NextSeq(plan);
    edit.Op = op;
    edit.Origin = origin.empty() ? L"user" : origin;
    edit.CreatedUtc = UtcNowIso();
    return edit;
}

bool NameRejected(const std::wstring& name, std::wstring& error)
{
    return IsInvalidTargetName(name, L"NTFS", error);
}

} // namespace

int NextSeq(const CPlanDocument& plan)
{
    int seq = 0;
    for (size_t i = 0; i < plan.Edits.size(); ++i)
    {
        if (plan.Edits[i].Seq > seq)
            seq = plan.Edits[i].Seq;
    }
    return seq + 1;
}

CHistoryState CPlanHistory::Capture(const CPlanDocument& plan)
{
    CHistoryState state;
    state.Edits = plan.Edits;
    state.Rules = plan.Rules;
    state.Resolutions = plan.Resolutions;
    state.Acknowledgements = plan.Acknowledgements;
    return state;
}

void CPlanHistory::Restore(CPlanDocument& plan, const CHistoryState& state)
{
    plan.Edits = state.Edits;
    plan.Rules = state.Rules;
    plan.Resolutions = state.Resolutions;
    plan.Acknowledgements = state.Acknowledgements;
    plan.Dirty = true;
}

void CPlanHistory::Remember(CPlanDocument& plan)
{
    // History is an in-memory command log. It is not written into the plan file.
    if (UndoStack.size() >= kLimit)
        UndoStack.erase(UndoStack.begin());
    UndoStack.push_back(Capture(plan));
    RedoStack.clear();
}

bool CPlanHistory::Undo(CPlanDocument& plan)
{
    if (UndoStack.empty())
        return false;
    RedoStack.push_back(Capture(plan));
    Restore(plan, UndoStack.back());
    UndoStack.pop_back();
    return true;
}

bool CPlanHistory::Redo(CPlanDocument& plan)
{
    if (RedoStack.empty())
        return false;
    UndoStack.push_back(Capture(plan));
    Restore(plan, RedoStack.back());
    RedoStack.pop_back();
    return true;
}

void CPlanHistory::Clear()
{
    UndoStack.clear();
    RedoStack.clear();
}

CStageResult StageMove(CPlanDocument& plan, CPlanHistory& history, const CSnapshot& snapshot, const std::wstring& source,
                       const std::wstring& destinationDir, const std::wstring& newName, const std::wstring& origin)
{
    std::wstring sourcePath, destPath, error;
    if (!NormalizePath(source, sourcePath, error) || !NormalizePath(destinationDir, destPath, error))
        return CStageResult::Failure(error.empty() ? L"path is not a disk path" : error);
    if (!IsDiskPath(sourcePath) || !IsDiskPath(destPath))
        return CStageResult::Failure(L"Destination is not a disk path.");
    std::wstring leaf = newName.empty() ? LeafName(sourcePath) : newName;
    if (NameRejected(leaf, error))
        return CStageResult::Failure(error);
    COverlay preview = BuildOverlay(snapshot, plan);
    const COverlayNode* sourceNode = preview.FindKey(sourcePath);
    if (sourceNode == NULL)
        sourceNode = preview.FindProposed(sourcePath);
    COverlayNode* dest = NULL;
    const COverlayNode* destNode = preview.FindProposed(destPath);
    if (sourceNode && sourceNode->IsDir && destNode)
    {
        if (WouldCreateCycle(preview, sourceNode->Key, destNode->Key) || IsUnderPath(destPath, sourceNode->ProposedPath, false))
            return CStageResult::Failure(L"Moving a folder into its own descendant is not allowed.");
    }
    else if (sourceNode && sourceNode->IsDir && IsUnderPath(destPath, sourceNode->OriginalPath, false))
        return CStageResult::Failure(L"Moving a folder into its own descendant is not allowed.");
    (void)dest;
    history.Remember(plan);
    CEdit edit = MakeEdit(plan, EditMove, origin);
    edit.Source = sourcePath;
    edit.DestinationDir = destPath;
    edit.NewName = newName;
    plan.Edits.push_back(edit);
    plan.Dirty = true;
    plan.ModifiedUtc = edit.CreatedUtc;
    return CStageResult::Success();
}

CStageResult StageRename(CPlanDocument& plan, CPlanHistory& history, const CSnapshot& snapshot, const std::wstring& source, const std::wstring& newName, const std::wstring& origin)
{
    (void)snapshot;
    std::wstring error;
    if (NameRejected(newName, error))
        return CStageResult::Failure(error);
    std::wstring sourcePath;
    if (!NormalizePath(source, sourcePath, error))
        return CStageResult::Failure(error);
    history.Remember(plan);
    CEdit edit = MakeEdit(plan, EditRename, origin);
    edit.Source = sourcePath;
    edit.NewName = newName;
    plan.Edits.push_back(edit);
    plan.Dirty = true;
    return CStageResult::Success();
}

CStageResult StageCreateFolder(CPlanDocument& plan, CPlanHistory& history, const std::wstring& path, const std::wstring& origin)
{
    std::wstring normalized, error;
    if (!NormalizePath(path, normalized, error))
        return CStageResult::Failure(error);
    if (NameRejected(LeafName(normalized), error))
        return CStageResult::Failure(error);
    if (plan.Kind != PlanRevert)
    {
        // removeEmptyFolder is rejected here; createFolder is valid in both kinds.
    }
    history.Remember(plan);
    CEdit edit = MakeEdit(plan, EditCreateFolder, origin);
    edit.Path = normalized;
    plan.Edits.push_back(edit);
    plan.Dirty = true;
    return CStageResult::Success();
}

CStageResult StageUnstage(CPlanDocument& plan, CPlanHistory& history, const std::wstring& source, const std::wstring& origin)
{
    std::wstring sourcePath, error;
    if (!NormalizePath(source, sourcePath, error))
        sourcePath = source;
    history.Remember(plan);
    CEdit edit = MakeEdit(plan, EditUnstage, origin);
    edit.Source = sourcePath;
    plan.Edits.push_back(edit);
    plan.Dirty = true;
    return CStageResult::Success();
}

CStageResult StageExclude(CPlanDocument& plan, CPlanHistory& history, const std::wstring& source, const std::wstring& origin)
{
    std::wstring sourcePath, error;
    if (!NormalizePath(source, sourcePath, error))
        return CStageResult::Failure(error);
    history.Remember(plan);
    CEdit edit = MakeEdit(plan, EditExclude, origin);
    edit.Source = sourcePath;
    plan.Edits.push_back(edit);
    plan.Dirty = true;
    return CStageResult::Success();
}

CStageResult AddRule(CPlanDocument& plan, CPlanHistory& history, const CRule& rule)
{
    if (!rule.TemplateError.empty() && rule.Enabled)
        return CStageResult::Failure(rule.TemplateError);
    history.Remember(plan);
    plan.Rules.push_back(rule);
    plan.Dirty = true;
    return CStageResult::Success();
}

CStageResult AddResolution(CPlanDocument& plan, CPlanHistory& history, const CResolution& resolution)
{
    history.Remember(plan);
    bool replaced = false;
    for (size_t i = 0; i < plan.Resolutions.size(); ++i)
    {
        if (plan.Resolutions[i].IssueKey == resolution.IssueKey)
        {
            plan.Resolutions[i] = resolution;
            replaced = true;
        }
    }
    if (!replaced)
        plan.Resolutions.push_back(resolution);
    plan.Dirty = true;
    return CStageResult::Success();
}

void Acknowledge(CPlanDocument& plan, const std::wstring& issueKey)
{
    for (size_t i = 0; i < plan.Acknowledgements.size(); ++i)
    {
        if (plan.Acknowledgements[i].IssueKey == issueKey)
            return;
    }
    CAcknowledgement ack;
    ack.IssueKey = issueKey;
    ack.AckUtc = UtcNowIso();
    plan.Acknowledgements.push_back(ack);
    plan.Dirty = true;
}

void MarkReviewed(CPlanDocument& plan, const std::wstring& hash, int stepCount)
{
    plan.Review.Present = true;
    plan.Review.CompiledSha256 = hash;
    plan.Review.ReviewedUtc = UtcNowIso();
    plan.Review.StepCount = stepCount;
    plan.Dirty = true;
}

} // namespace reorg
