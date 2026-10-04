// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "journal.h"

namespace reorg
{

std::wstring ChooseRecoveryStore(const CPlanDocument& plan, const CSnapshotItem& item, const std::wstring& proposedPath, std::wstring& error);

// Replaces the {storebase:..}/{store:..} placeholders of a reviewed compiled plan with concrete
// folders: <root>\.reorg-recovery and <root>\.reorg-recovery\<applyId>. Call after the review-hash
// check and before marshaling steps. Fails with DST-011 when a volume has no usable store root.
bool MaterializeRecoveryStores(CCompiledPlan& compiled, const CPlanDocument& plan, const COverlay& overlay,
                               const std::wstring& applyId, std::wstring& error);
CPlanDocument BuildRevertPlan(const CPlanDocument& original, const CCompiledPlan& compiled, const CJournal& journal, bool finalized);

} // namespace reorg
