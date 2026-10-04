// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "validate.h"

namespace reorg
{

CCompiledPlan CompilePlan(const COverlay& overlay, const CPlanDocument& plan, CIssueSink& sink);

// Recovery-store placeholders used in compiled steps so the hash is known at review time.
// {store:<serial>} stands for <root>\.reorg-recovery\<applyId> and {storebase:<serial>} for
// <root>\.reorg-recovery; MaterializeRecoveryStores substitutes both when an apply starts.
std::wstring StoreToken(DWORD volumeSerial);
std::wstring StoreBaseToken(DWORD volumeSerial);

} // namespace reorg
