// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>

// One request owns one persistent predicate. The condition variable registers
// waiting while releasing the predicate lock, so an early completion is retained.
class CFileCloseCompletion
{
public:
    CFileCloseCompletion() : Done(FALSE), Cancelled(FALSE), Error(ERROR_IO_PENDING)
    { InitializeCriticalSection(&Lock); InitializeConditionVariable(&Changed); }
    ~CFileCloseCompletion() { DeleteCriticalSection(&Lock); }
    void Complete(DWORD error)
    {
        EnterCriticalSection(&Lock);
        if (!Done) { Error = error; Done = TRUE; }
        WakeAllConditionVariable(&Changed);
        LeaveCriticalSection(&Lock);
    }
    void CancelWait()
    {
        EnterCriticalSection(&Lock); Cancelled = TRUE;
        WakeAllConditionVariable(&Changed); LeaveCriticalSection(&Lock);
    }
    BOOL TryGet(DWORD& error)
    {
        EnterCriticalSection(&Lock);
        const BOOL done = Done; error = Done ? Error : ERROR_IO_PENDING;
        LeaveCriticalSection(&Lock); return done;
    }
    BOOL Wait(DWORD timeout, DWORD& error)
    {
        const ULONGLONG start = GetTickCount64();
        EnterCriticalSection(&Lock);
        while (!Done && !Cancelled)
        {
            const ULONGLONG elapsed = GetTickCount64() - start;
            if (timeout != INFINITE && elapsed >= timeout) break;
            const DWORD remaining = timeout == INFINITE ? INFINITE : timeout - (DWORD)elapsed;
            if (!SleepConditionVariableCS(&Changed, &Lock, remaining) && GetLastError() != ERROR_TIMEOUT)
            {
                error = GetLastError(); LeaveCriticalSection(&Lock); return FALSE;
            }
            // Recheck under the reacquired lock even when the wait timed out.
        }
        error = Done ? Error : Cancelled ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT;
        const BOOL complete = Done;
        LeaveCriticalSection(&Lock);
        return complete;
    }
private:
    CFileCloseCompletion(const CFileCloseCompletion&);
    CFileCloseCompletion& operator=(const CFileCloseCompletion&);
    CRITICAL_SECTION Lock;
    CONDITION_VARIABLE Changed;
    BOOL Done, Cancelled;
    DWORD Error;
};
