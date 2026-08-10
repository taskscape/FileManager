// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "lock_ordering.h"

#ifdef _DEBUG

struct CLockOrderEntry
{
    CRITICAL_SECTION* CriticalSection;
    CLockRank Rank;
    const char* Name;
};

// The rank stack is per thread so unrelated workers never influence each
// other's ordering assertions or timeout diagnostics.
static __declspec(thread) CLockOrderEntry LockOrderStack[32];
static __declspec(thread) unsigned int LockOrderStackCount = 0;

static void ReportLockWaitTimeout(CRITICAL_SECTION* criticalSection, CLockRank rank, const char* lockName,
                                  DWORD timeoutMilliseconds)
{
    const DWORD waiterThreadId = GetCurrentThreadId();
    const DWORD ownerThreadId = (DWORD)(ULONG_PTR)criticalSection->OwningThread;
    const LONG recursionCount = criticalSection->RecursionCount;
    const CLockRank heldRank = LockOrderStackCount == 0 ? lkrNone : LockOrderStack[LockOrderStackCount - 1].Rank;
    char message[512];

    // Avoid trace infrastructure here: a diagnostic must not acquire another application lock while one is stalled.
    _snprintf_s(message, _countof(message), _TRUNCATE,
                "Open Salamander lock wait timeout: waiter=%lu lock=%s rank=%d owner=%lu recursion=%ld held-rank=%d timeout-ms=%lu\n",
                waiterThreadId, lockName != NULL ? lockName : "unnamed", (int)rank, ownerThreadId, recursionCount,
                (int)heldRank, timeoutMilliseconds);
    OutputDebugStringA(message);
}

static BOOL IsRecursiveAcquisition(CRITICAL_SECTION* criticalSection, CLockRank rank)
{
    return LockOrderStackCount != 0 &&
           LockOrderStack[LockOrderStackCount - 1].CriticalSection == criticalSection &&
           LockOrderStack[LockOrderStackCount - 1].Rank == rank;
}

#endif

void LockOrderEnter(CRITICAL_SECTION* criticalSection, CLockRank rank, const char* lockName, DWORD timeoutMilliseconds)
{
#ifdef _DEBUG
    _ASSERTE(criticalSection != NULL);
    _ASSERTE(rank != lkrNone);

    const BOOL recursive = IsRecursiveAcquisition(criticalSection, rank);
    if (!recursive && LockOrderStackCount != 0)
    {
        // Equal ranks are reserved for recursive acquisition of the same lock; peers need an explicit order.
        _ASSERTE(rank > LockOrderStack[LockOrderStackCount - 1].Rank);
    }

    if (!TryEnterCriticalSection(criticalSection))
    {
        const ULONGLONG waitStarted = GetTickCount64();
        while (!TryEnterCriticalSection(criticalSection))
        {
            if (GetTickCount64() - waitStarted >= timeoutMilliseconds)
            {
                ReportLockWaitTimeout(criticalSection, rank, lockName, timeoutMilliseconds);
                // Preserve the legacy contract after recording enough state to diagnose the blocking owner.
                EnterCriticalSection(criticalSection);
                break;
            }
            SwitchToThread();
        }
    }

    _ASSERTE(LockOrderStackCount < _countof(LockOrderStack));
    if (LockOrderStackCount < _countof(LockOrderStack))
    {
        LockOrderStack[LockOrderStackCount].CriticalSection = criticalSection;
        LockOrderStack[LockOrderStackCount].Rank = rank;
        LockOrderStack[LockOrderStackCount].Name = lockName;
        ++LockOrderStackCount;
    }
#else
    UNREFERENCED_PARAMETER(rank);
    UNREFERENCED_PARAMETER(lockName);
    UNREFERENCED_PARAMETER(timeoutMilliseconds);
    EnterCriticalSection(criticalSection);
#endif
}

void LockOrderLeave(CRITICAL_SECTION* criticalSection, CLockRank rank, const char* lockName)
{
#ifdef _DEBUG
    _ASSERTE(LockOrderStackCount != 0);
    if (LockOrderStackCount != 0)
    {
        const CLockOrderEntry& last = LockOrderStack[LockOrderStackCount - 1];
        // LIFO release keeps the stack faithful to the actual lock ownership used by later rank checks.
        _ASSERTE(last.CriticalSection == criticalSection && last.Rank == rank && last.Name == lockName);
        --LockOrderStackCount;
    }
#else
    UNREFERENCED_PARAMETER(rank);
    UNREFERENCED_PARAMETER(lockName);
#endif
    LeaveCriticalSection(criticalSection);
}
