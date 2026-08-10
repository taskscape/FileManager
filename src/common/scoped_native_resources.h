// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <stdlib.h>

#include "lock_ordering.h"

// Keeps malloc-owned scratch storage on the stack so callback failures and
// early returns cannot bypass the allocator paired with the legacy ABI.
class CScopedHeapBuffer
{
public:
    explicit CScopedHeapBuffer(void* buffer = NULL) : Buffer(buffer)
    {
    }

    ~CScopedHeapBuffer()
    {
        // Cleanup must not replace the Win32 error a caller is returning.
        const DWORD error = GetLastError();
        Reset();
        SetLastError(error);
    }

private:
    CScopedHeapBuffer(const CScopedHeapBuffer&);
    CScopedHeapBuffer& operator=(const CScopedHeapBuffer&);

public:
    void* Get() const
    {
        return Buffer;
    }

    void Reset(void* buffer = NULL)
    {
        if (Buffer != NULL)
            free(Buffer);
        Buffer = buffer;
    }

    void* Release()
    {
        void* buffer = Buffer;
        Buffer = NULL;
        return buffer;
    }

private:
    void* Buffer;
};

// Owns a MapViewOfFile result without imposing a C++ object layout on either
// side of a plug-in boundary.
class CScopedMappingView
{
public:
    explicit CScopedMappingView(void* view = NULL) : View(view)
    {
    }

    ~CScopedMappingView()
    {
        // Unmapping is best-effort cleanup and must retain the caller's error.
        const DWORD error = GetLastError();
        Reset();
        SetLastError(error);
    }

private:
    CScopedMappingView(const CScopedMappingView&);
    CScopedMappingView& operator=(const CScopedMappingView&);

public:
    void* Get() const
    {
        return View;
    }

    void Reset(void* view = NULL)
    {
        if (View != NULL)
            UnmapViewOfFile(View);
        View = view;
    }

    void* Release()
    {
        void* view = View;
        View = NULL;
        return view;
    }

private:
    void* View;
};

// Pairs a ranked CRITICAL_SECTION acquisition at callback boundaries so future
// returns or unwinding cannot strand callers or bypass the lock-order checks.
class CScopedCriticalSection
{
public:
    CScopedCriticalSection(CRITICAL_SECTION* criticalSection, CLockRank rank = lkrNone, const char* lockName = NULL)
        : CriticalSection(criticalSection), Rank(rank), LockName(lockName)
    {
        if (Rank != lkrNone)
            LockOrderEnter(CriticalSection, Rank, LockName);
        else
            EnterCriticalSection(CriticalSection);
    }

    ~CScopedCriticalSection()
    {
        if (Rank != lkrNone)
            LockOrderLeave(CriticalSection, Rank, LockName);
        else
            LeaveCriticalSection(CriticalSection);
    }

private:
    CScopedCriticalSection(const CScopedCriticalSection&);
    CScopedCriticalSection& operator=(const CScopedCriticalSection&);

private:
    CRITICAL_SECTION* CriticalSection;
    CLockRank Rank;
    const char* LockName;
};
