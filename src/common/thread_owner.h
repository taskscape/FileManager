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
