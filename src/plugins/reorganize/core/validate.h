// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "plan.h"

namespace reorg
{

struct CAnalysisContext
{
    const CPlanDocument* Plan;
    const CSnapshot* Snapshot;
    const COverlay* Overlay;
    bool Preflight;
    std::vector<std::wstring> ApplicationPaths;
    CAnalysisContext() : Plan(NULL), Snapshot(NULL), Overlay(NULL), Preflight(false) {}
};

struct CIssueSink
{
    std::vector<CIssue> Issues;
    void Add(const CIssue& issue) { Issues.push_back(issue); }
    int Count(const wchar_t* code) const
    {
        int n = 0;
        for (size_t i = 0; i < Issues.size(); ++i)
        {
            if (Issues[i].Code == code)
                ++n;
        }
        return n;
    }
};

std::wstring MakeIssueKey(const std::wstring& code, const std::vector<std::wstring>& paths);
void ValidatePlan(const CAnalysisContext& context, CIssueSink& sink);
bool HasBlockingErrors(const CIssueSink& sink);

} // namespace reorg
