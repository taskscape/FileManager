// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Shared critical sections are acquired from lower to higher ranks so nested
// calls have one documented direction instead of an implicit call-order rule.
enum CLockRank
{
    lkrNone = 0,
    lkrProcessLifetime = 10,
    lkrConfiguration = 20,
    lkrPluginRegistry = 30,
    lkrWorkerQueue = 40,
    lkrOperationState = 50,
    lkrUiState = 60,
    lkrExternalBroker = 70,
};

// These functions retain CRITICAL_SECTION blocking semantics while Debug
// builds diagnose rank inversions and long waits before entering the lock.
void LockOrderEnter(CRITICAL_SECTION* criticalSection, CLockRank rank, const char* lockName,
                    DWORD timeoutMilliseconds = 10000);
void LockOrderLeave(CRITICAL_SECTION* criticalSection, CLockRank rank, const char* lockName);
