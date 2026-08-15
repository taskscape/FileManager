// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Time points and durations are deliberately named at call sites. Their
// 64-bit representation keeps long-running timers correct after GetTickCount
// would have wrapped.
typedef ULONGLONG CMonotonicTimePoint;
typedef ULONGLONG CMonotonicDuration;

class CMonotonicClock
{
public:
    // GetTickCount64 is monotonic and is not affected by wall-clock changes.
    static CMonotonicTimePoint Now()
    {
#if _WIN32_WINNT < 0x0600
        // The portable-device plug-in retains an XP SDK target, where GetTickCount64 is not declared.
        LARGE_INTEGER frequency;
        LARGE_INTEGER counter;
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0)
            return 0;
        return (CMonotonicTimePoint)(counter.QuadPart / frequency.QuadPart) * 1000 +
               (CMonotonicTimePoint)(counter.QuadPart % frequency.QuadPart) * 1000 / frequency.QuadPart;
#else
        return GetTickCount64();
#endif
    }

    static CMonotonicTimePoint DeadlineAfter(CMonotonicDuration duration)
    {
        return Now() + duration;
    }

    // Initializing a throttle as already expired must also be safe during the
    // first few milliseconds of process uptime, before a full duration elapsed.
    static CMonotonicTimePoint AtLeastDurationAgo(CMonotonicDuration duration)
    {
        const CMonotonicTimePoint now = Now();
        return now > duration ? now - duration : 0;
    }

    static CMonotonicDuration Elapsed(CMonotonicTimePoint start, CMonotonicTimePoint now)
    {
        // A defensive zero also makes injected/synthetic backward samples safe.
        return now >= start ? now - start : 0;
    }

    static BOOL HasElapsed(CMonotonicTimePoint start, CMonotonicDuration duration,
                           CMonotonicTimePoint now)
    {
        return Elapsed(start, now) >= duration;
    }

    static BOOL HasReached(CMonotonicTimePoint deadline, CMonotonicTimePoint now)
    {
        return now >= deadline;
    }

    // Win32 timers retain DWORD delays, so saturate only at that API boundary.
    static DWORD RemainingWin32TimerDelay(CMonotonicTimePoint deadline,
                                          CMonotonicTimePoint now)
    {
        if (HasReached(deadline, now))
            return 0;

        const CMonotonicDuration remaining = deadline - now;
        return remaining > MAXDWORD ? MAXDWORD : (DWORD)remaining;
    }
};
