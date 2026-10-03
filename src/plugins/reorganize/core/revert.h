// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "journal.h"

namespace reorg
{

std::wstring ChooseRecoveryStore(const CPlanDocument& plan, const CSnapshotItem& item, const std::wstring& proposedPath, std::wstring& error);
CPlanDocument BuildRevertPlan(const CPlanDocument& original, const CCompiledPlan& compiled, const CJournal& journal, bool finalized);

} // namespace reorg
