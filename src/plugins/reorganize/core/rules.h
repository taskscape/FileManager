// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "snapshot.h"

namespace reorg
{

struct CRuleHit
{
    std::wstring Source;
    std::wstring DestinationDir;
    std::wstring NewName;
    std::wstring RuleId;
};

bool MatchRule(const CRule& rule, const CSnapshotItem& item, const CPlanDocument& plan, const CSnapshot& snapshot);
bool ExpandTemplate(const std::wstring& templ, const CSnapshotItem& item, const CPlanDocument& plan, const CSnapshot& snapshot,
                    std::wstring& destinationDir, std::wstring& newName, std::wstring& error);
std::vector<CRuleHit> ApplyRules(const CPlanDocument& plan, const CSnapshot& snapshot);
std::wstring ExpandKeepBoth(const std::wstring& pattern, const std::wstring& fileName, int n);

} // namespace reorg
