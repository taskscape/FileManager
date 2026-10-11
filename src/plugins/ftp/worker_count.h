// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Explicit alignment survives the FTP classes' packed layout: Win32 interlocked
// operations require an aligned LONG even when this snapshot is a packed member.
#pragma pack(push, 4)
class __declspec(align(4)) CFTPWorkerCountSnapshot
{
    volatile LONG Count;

public:
    CFTPWorkerCountSnapshot() : Count(0) {}

    // Readers may hold WorkerCritSect. Never enter WorkersListCritSect here:
    // UI rendering and confirmation wake-ups acquire those locks in reverse.
    int Get() { return (int)InterlockedCompareExchange(&Count, 0, 0); }

    // Publish only while the list lock serializes completed additions/removals;
    // this is a count snapshot, not permission to access the worker array.
    void Publish(int count) { InterlockedExchange(&Count, (LONG)count); }
};
#pragma pack(pop)
