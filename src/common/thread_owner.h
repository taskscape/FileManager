// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "handles.h"

#include <objbase.h>
#include <process.h>

// precomp.h declares this later through consts.h; keep the worker seam usable
// by focused sources that include it before the application declarations.
void SetThreadNameInVCAndTrace(const char* name);

// Centralizes the lifetime contract for newly touched CRT-backed workers: the
// owner retains their handle, stop request, launch data, and completion signal
// until the callback has returned and the thread has been joined.
typedef DWORD(WINAPI* CThreadOwnerEntry)(void* parameter, HANDLE stopEvent);

// Shutdown has two observable phases: a worker first gets time to observe its
// cancellation request, then a longer recovery window to persist/close work.
// A breach is never permission to terminate the thread because its callback
// can still hold application-owned state.
class CThreadShutdownDeadline
{
public:
    CThreadShutdownDeadline(LPCSTR workerName,
                            DWORD cancellationDeadline = 5000,
                            DWORD recoveryDeadline = 30000)
        : WorkerName(workerName != NULL ? workerName : "unnamed worker"),
          CancellationDeadline(cancellationDeadline),
          RecoveryDeadline(recoveryDeadline)
    {
    }

    // Returns WAIT_TIMEOUT when either diagnostic deadline was breached, even
    // though the final safe join below has completed.
    DWORD WaitForSafeJoin(HANDLE worker) const
    {
        DWORD waitResult = WaitForSingleObject(worker, CancellationDeadline);
        if (waitResult != WAIT_TIMEOUT)
            return waitResult;

        TraceDeadlineBreach("cancellation", CancellationDeadline, worker);
        waitResult = WaitForSingleObject(worker, RecoveryDeadline);
        if (waitResult != WAIT_TIMEOUT)
            return WAIT_TIMEOUT;

        TraceDeadlineBreach("operation recovery", RecoveryDeadline, worker);
        // This worker has not been proven independent of process state, so it
        // must finish before shutdown frees the state it can still reference.
        WaitForSingleObject(worker, INFINITE);
        return WAIT_TIMEOUT;
    }

private:
    void TraceDeadlineBreach(LPCSTR phase, DWORD deadline, HANDLE worker) const
    {
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeThread(worker, &exitCode))
            exitCode = GetLastError();
        TRACE_E("Shutdown " << phase << " deadline breached for " << WorkerName
                            << " after " << deadline << " ms; thread state=" << exitCode
                            << ". Keeping the process alive for a safe join.");
    }

private:
    LPCSTR WorkerName;
    DWORD CancellationDeadline;
    DWORD RecoveryDeadline;
};

class CThreadOwner
{
public:
    CThreadOwner()
        : Thread(NULL), StopEvent(NULL), CompletionEvent(NULL)
    {
    }

    ~CThreadOwner()
    {
        // Owners may be destroyed only after their callback has stopped, so a
        // forgotten shutdown path cannot leave launch data pointing at dead state.
        StopAndJoin(INFINITE);
    }

private:
    CThreadOwner(const CThreadOwner&);
    CThreadOwner& operator=(const CThreadOwner&);

public:
    // The caller keeps parameter alive through WaitForCompletion or StopAndJoin;
    // the wrapper itself owns the copied launch record for the whole callback.
    BOOL Start(CThreadOwnerEntry entry, void* parameter, LPCSTR name,
               BOOL initializeCOM = FALSE, DWORD coInit = COINIT_APARTMENTTHREADED)
    {
        if (entry == NULL)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }

        if (Thread != NULL)
        {
            if (WaitForCompletion(0) != WAIT_OBJECT_0)
            {
                SetLastError(ERROR_BUSY);
                return FALSE;
            }
            Join();
            CloseOwnedHandles();
        }

        StopEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
        CompletionEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
        if (StopEvent == NULL || CompletionEvent == NULL)
        {
            CloseOwnedHandles();
            return FALSE;
        }

        CThreadLaunchData* launch = (CThreadLaunchData*)malloc(sizeof(CThreadLaunchData));
        if (launch == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            CloseOwnedHandles();
            return FALSE;
        }

        launch->Entry = entry;
        launch->Parameter = parameter;
        launch->StopEvent = StopEvent;
        launch->CompletionEvent = CompletionEvent;
        launch->InitializeCOM = initializeCOM;
        launch->CoInit = coInit;
        lstrcpynA(launch->Name, name != NULL ? name : "OpenSalamanderWorker", sizeof(launch->Name));

        unsigned threadID = 0;
        Thread = (HANDLE)HANDLES(_beginthreadex(NULL, 0, ThreadMain, launch, 0, &threadID));
        if (Thread == NULL)
        {
            free(launch);
            CloseOwnedHandles();
            return FALSE;
        }
        return TRUE;
    }

    BOOL HasThread() const
    {
        return Thread != NULL;
    }

    HANDLE GetStopEvent() const
    {
        return StopEvent;
    }

    // Exposes the owner's completion signal so a coordinator can wait for
    // work without repeatedly probing the thread exit code.
    HANDLE GetCompletionEvent() const
    {
        return CompletionEvent;
    }

    DWORD WaitForCompletion(DWORD timeout) const
    {
        return CompletionEvent != NULL ? WaitForSingleObject(CompletionEvent, timeout) : WAIT_OBJECT_0;
    }

    BOOL GetExitCode(DWORD* exitCode) const
    {
        if (Thread == NULL || exitCode == NULL)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        return GetExitCodeThread(Thread, exitCode);
    }

    void RequestStop()
    {
        if (StopEvent != NULL)
            SetEvent(StopEvent);
    }

    // A timeout is diagnostic only: the owner still joins before releasing its
    // state, preserving the no-use-after-free shutdown invariant.
    DWORD StopAndJoin(DWORD timeout)
    {
        if (Thread == NULL)
            return WAIT_OBJECT_0;

        RequestStop();
        const DWORD completion = WaitForCompletion(timeout);
        WaitForSingleObject(Thread, INFINITE);
        CloseOwnedHandles();
        return completion;
    }

    // The declared shutdown policy makes cancellation and recovery delays
    // visible while retaining the owner until the callback has returned.
    DWORD StopAndJoin(const CThreadShutdownDeadline& deadline)
    {
        if (Thread == NULL)
            return WAIT_OBJECT_0;

        RequestStop();
        const DWORD completion = deadline.WaitForSafeJoin(Thread);
        CloseOwnedHandles();
        return completion;
    }

private:
    struct CThreadLaunchData
    {
        CThreadOwnerEntry Entry;
        void* Parameter;
        HANDLE StopEvent;
        HANDLE CompletionEvent;
        BOOL InitializeCOM;
        DWORD CoInit;
        char Name[64];
    };

    static unsigned __stdcall ThreadMain(void* parameter)
    {
        CThreadLaunchData* launch = (CThreadLaunchData*)parameter;
        DWORD result = ERROR_UNHANDLED_EXCEPTION;
        HRESULT comResult = S_OK;
        BOOL comInitialized = FALSE;

        if (launch->Name[0] != 0)
            SetThreadNameInVCAndTrace(launch->Name);

        if (launch->InitializeCOM)
        {
            comResult = CoInitializeEx(NULL, launch->CoInit);
            comInitialized = SUCCEEDED(comResult);
        }

        if (!launch->InitializeCOM || comInitialized)
        {
            // Contain C++ exceptions at the common worker boundary so every
            // normal failure path reaches the completion event below.
            try
            {
                result = launch->Entry(launch->Parameter, launch->StopEvent);
            }
            catch (...)
            {
                result = ERROR_UNHANDLED_EXCEPTION;
            }
        }
        else
            result = (DWORD)comResult;

        if (comInitialized)
            CoUninitialize();

        // Completion is signaled before freeing the launch record, letting the
        // owner safely join and release its events without polling the handle.
        SetEvent(launch->CompletionEvent);
        free(launch);
        return result;
    }

    void Join()
    {
        if (Thread != NULL)
            WaitForSingleObject(Thread, INFINITE);
    }

    void CloseOwnedHandles()
    {
        if (Thread != NULL)
        {
            HANDLES(CloseHandle(Thread));
            Thread = NULL;
        }
        if (StopEvent != NULL)
        {
            HANDLES(CloseHandle(StopEvent));
            StopEvent = NULL;
        }
        if (CompletionEvent != NULL)
        {
            HANDLES(CloseHandle(CompletionEvent));
            CompletionEvent = NULL;
        }
    }

private:
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE CompletionEvent;
};
