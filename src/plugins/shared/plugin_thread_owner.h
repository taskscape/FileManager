// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <process.h>

typedef DWORD(WINAPI* CPluginThreadOwnerEntry)(void* parameter, HANDLE stopEvent);
typedef void(WINAPI* CPluginThreadOwnerNameThread)(const char* name);

// Plug-ins cannot link the host-only CThreadOwner implementation, so this
// adapter keeps the same ownership invariant at the shared SDK boundary.
class CPluginThreadOwner
{
public:
    CPluginThreadOwner()
        : Thread(NULL), StopEvent(NULL), CompletionEvent(NULL), ThreadID(0)
    {
    }

    ~CPluginThreadOwner()
    {
        StopAndJoin(INFINITE);
    }

private:
    CPluginThreadOwner(const CPluginThreadOwner&);
    CPluginThreadOwner& operator=(const CPluginThreadOwner&);

public:
    BOOL Start(CPluginThreadOwnerEntry entry, void* parameter, const char* name,
               CPluginThreadOwnerNameThread nameThread, unsigned stackSize)
    {
        if (entry == NULL || Thread != NULL)
        {
            SetLastError(entry == NULL ? ERROR_INVALID_PARAMETER : ERROR_BUSY);
            return FALSE;
        }

        StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        CompletionEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (StopEvent == NULL || CompletionEvent == NULL)
        {
            CloseOwnedHandles();
            return FALSE;
        }

        CLaunch* launch = new CLaunch;
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
        launch->NameThread = nameThread;
        lstrcpynA(launch->Name, name != NULL ? name : "OpenSalamanderPluginWorker", sizeof(launch->Name));

        unsigned threadID = 0;
        Thread = (HANDLE)_beginthreadex(NULL, stackSize, ThreadMain, launch, 0, &threadID);
        if (Thread == NULL)
        {
            delete launch;
            CloseOwnedHandles();
            return FALSE;
        }
        ThreadID = threadID;
        return TRUE;
    }

    HANDLE GetThreadHandle() const { return Thread; }
    DWORD GetThreadID() const { return ThreadID; }
    HANDLE GetStopEvent() const { return StopEvent; }
    DWORD WaitForCompletion(DWORD timeout) const
    {
        return CompletionEvent != NULL ? WaitForSingleObject(CompletionEvent, timeout) : WAIT_OBJECT_0;
    }

    void RequestStop()
    {
        if (StopEvent != NULL)
            SetEvent(StopEvent);
    }

    // A timeout is diagnostic only. The owner remains alive until its callback
    // exits, preventing a plug-in unload from freeing callback state in use.
    DWORD StopAndJoin(DWORD timeout)
    {
        if (Thread == NULL)
            return WAIT_OBJECT_0;

        RequestStop();
        DWORD completion = WaitForCompletion(timeout);
        WaitForSingleObject(Thread, INFINITE);
        CloseOwnedHandles();
        return completion;
    }

private:
    struct CLaunch
    {
        CPluginThreadOwnerEntry Entry;
        void* Parameter;
        HANDLE StopEvent;
        HANDLE CompletionEvent;
        CPluginThreadOwnerNameThread NameThread;
        char Name[64];
    };

    static unsigned __stdcall ThreadMain(void* parameter)
    {
        CLaunch* launch = (CLaunch*)parameter;
        DWORD result = ERROR_UNHANDLED_EXCEPTION;
        if (launch->NameThread != NULL)
            launch->NameThread(launch->Name);

        try
        {
            result = launch->Entry(launch->Parameter, launch->StopEvent);
        }
        catch (...)
        {
            result = ERROR_UNHANDLED_EXCEPTION;
        }

        SetEvent(launch->CompletionEvent);
        delete launch;
        return result;
    }

    void CloseOwnedHandles()
    {
        if (Thread != NULL)
        {
            CloseHandle(Thread);
            Thread = NULL;
            ThreadID = 0;
        }
        if (StopEvent != NULL)
        {
            CloseHandle(StopEvent);
            StopEvent = NULL;
        }
        if (CompletionEvent != NULL)
        {
            CloseHandle(CompletionEvent);
            CompletionEvent = NULL;
        }
    }

private:
    HANDLE Thread;
    HANDLE StopEvent;
    HANDLE CompletionEvent;
    DWORD ThreadID;
};
