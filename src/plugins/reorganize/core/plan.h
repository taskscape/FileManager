// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "overlay.h"

namespace reorg
{

struct CStageResult
{
    bool Ok;
    std::wstring Error;
    CStageResult() : Ok(false) {}
    static CStageResult Success()
    {
        CStageResult r;
        r.Ok = true;
        return r;
    }
    static CStageResult Failure(const std::wstring& error)
    {
        CStageResult r;
        r.Error = error;
        return r;
    }
};

struct CHistoryState
{
    std::vector<CEdit> Edits;
    std::vector<CRule> Rules;
    std::vector<CResolution> Resolutions;
    std::vector<CAcknowledgement> Acknowledgements;
};

class CPlanHistory
{
public:
    void Remember(CPlanDocument& plan);
    bool Undo(CPlanDocument& plan);
    bool Redo(CPlanDocument& plan);
    void Clear();

private:
    std::vector<CHistoryState> UndoStack;
    std::vector<CHistoryState> RedoStack;
    static const size_t kLimit = 10000;
    static CHistoryState Capture(const CPlanDocument& plan);
    static void Restore(CPlanDocument& plan, const CHistoryState& state);
};

CStageResult StageMove(CPlanDocument& plan, CPlanHistory& history, const CSnapshot& snapshot, const std::wstring& source,
                       const std::wstring& destinationDir, const std::wstring& newName, const std::wstring& origin);
CStageResult StageRename(CPlanDocument& plan, CPlanHistory& history, const CSnapshot& snapshot, const std::wstring& source, const std::wstring& newName, const std::wstring& origin);
CStageResult StageCreateFolder(CPlanDocument& plan, CPlanHistory& history, const std::wstring& path, const std::wstring& origin);
CStageResult StageUnstage(CPlanDocument& plan, CPlanHistory& history, const std::wstring& source, const std::wstring& origin);
CStageResult StageExclude(CPlanDocument& plan, CPlanHistory& history, const std::wstring& source, const std::wstring& origin);
CStageResult AddRule(CPlanDocument& plan, CPlanHistory& history, const CRule& rule);
CStageResult AddResolution(CPlanDocument& plan, CPlanHistory& history, const CResolution& resolution);
void Acknowledge(CPlanDocument& plan, const std::wstring& issueKey);
void MarkReviewed(CPlanDocument& plan, const std::wstring& hash, int stepCount);

int NextSeq(const CPlanDocument& plan);

} // namespace reorg
