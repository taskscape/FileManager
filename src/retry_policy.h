// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/monotonic_time.h"

// Automatic retries are centralized so transient transport faults are paced
// consistently, while uncertain destructive commits always require a user.
enum ERetryOperationKind
{
    rokReadOnly,
    rokDestructiveCommit
};

enum
{
    kAutomaticRetryLimit = 3,
    kAutomaticRetryBaseDelayMs = 100,
    kAutomaticRetryMaximumDelayMs = 1000
};

inline BOOL IsTransientOperationError(DWORD error)
{
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_BUSY || error == ERROR_NETWORK_BUSY ||
           error == ERROR_NETNAME_DELETED || error == ERROR_BAD_NET_RESP ||
           error == ERROR_UNEXP_NET_ERR || error == ERROR_CONNECTION_ABORTED ||
           error == ERROR_NOT_READY || error == ERROR_SEM_TIMEOUT ||
           error == ERROR_TIMEOUT;
}

inline DWORD GetAutomaticRetryDelay(int attempt)
{
    DWORD delay = kAutomaticRetryBaseDelayMs;
    for (int retry = 1; retry < attempt && delay < kAutomaticRetryMaximumDelayMs; ++retry)
        delay = delay > kAutomaticRetryMaximumDelayMs / 2 ?
                    kAutomaticRetryMaximumDelayMs : delay * 2;

    // Spread simultaneous network clients across a bounded +/- 25 percent window.
    DWORD jitterRange = delay / 2 + 1;
    // The retry dephasing seed must continue varying after the 32-bit tick counter would wrap.
    const CMonotonicTimePoint jitterSample = CMonotonicClock::Now();
    DWORD jitter = (DWORD)(jitterSample ^ (attempt * 0x9e3779b9UL)) % jitterRange;
    DWORD randomizedDelay = delay - delay / 4 + jitter;
    return randomizedDelay > kAutomaticRetryMaximumDelayMs ?
               kAutomaticRetryMaximumDelayMs : randomizedDelay;
}

inline BOOL PrepareAutomaticRetry(DWORD error, int* attempts, ERetryOperationKind kind,
                                  HANDLE cancellationEvent, DWORD* delay)
{
    if (attempts == NULL || delay == NULL || kind == rokDestructiveCommit ||
        !IsTransientOperationError(error) ||
        (cancellationEvent != NULL && WaitForSingleObject(cancellationEvent, 0) == WAIT_OBJECT_0) ||
        *attempts >= kAutomaticRetryLimit)
        return FALSE;

    *delay = GetAutomaticRetryDelay(++*attempts);
    return TRUE;
}

inline BOOL WaitForAutomaticRetry(HANDLE cancellationEvent, DWORD delay)
{
    // A cancellation event interrupts backoff instead of leaving the worker asleep.
    if (cancellationEvent != NULL)
        return WaitForSingleObject(cancellationEvent, delay) == WAIT_TIMEOUT;
    return WaitForSingleObject(GetCurrentProcess(), delay) == WAIT_TIMEOUT;
}
