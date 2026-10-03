// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "journal.h"

namespace reorg
{

enum EReconcileState
{
    RecDone = 0,
    RecNotDone,
    RecManual,
    RecBlocked
};

struct CReconcileResult
{
    int Index;
    EReconcileState State;
    std::wstring Evidence;
};

std::vector<CReconcileResult> Reconcile(const CCompiledPlan& plan, const CJournal& journal, IFileSystemProbe& probe);

} // namespace reorg
