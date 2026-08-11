// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#include "plugin_thread_owner.h"

typedef DWORD(WINAPI* CThreadQueueStopBody)(void* parameter, HANDLE stopEvent);

//
// ****************************************************************************
// CThreadQueue
//

struct CThreadQueueItem
{
    HANDLE Thread;
    CPluginThreadOwner* Owner;
    DWORD ThreadID; // only for debugging purposes (finding thread in thread list in debugger)
    int Locks;      // number of locks; if > 0, 'Thread' must not be closed
    CThreadQueueItem* Next;

    CThreadQueueItem(CPluginThreadOwner* owner, DWORD tid);
    ~CThreadQueueItem();
};

class CThreadQueue
{
protected:
    const char* QueueName; // queue name (only for debugging purposes)
    CThreadQueueItem* Head;
    struct CCS // access from multiple threads -> synchronization required
    {
        CRITICAL_SECTION cs;

        CCS() { InitializeCriticalSection(&cs); }
        ~CCS() { DeleteCriticalSection(&cs); }

        void Enter() { EnterCriticalSection(&cs); }
        void Leave() { LeaveCriticalSection(&cs); }
    } CS;

    // Queue mutations must release the lock on every timeout/error path so a
    // cooperative shutdown never strands later waiters behind stale ownership.
    class CScopedQueueLock
    {
    public:
        CScopedQueueLock(CCS& section) : Section(section) { Section.Enter(); }
        ~CScopedQueueLock() { Section.Leave(); }
    private:
        CCS& Section;
    };

public:
    CThreadQueue(const char* queueName /* e.g. "DemoPlug Viewers" */);
    ~CThreadQueue();

    // runs function 'body' with parameter 'param' in a newly created thread with a stack
    // of size 'stack_size' (0 = default); returns thread handle or NULL on error,
    // also writes the result to 'threadHandle' before starting the thread (resume)
    // (if not NULL), use returned thread handle only for NULL tests and for calling
    // CThreadQueue methods: WaitForExit() and KillThread(); closing the thread handle is handled
    // by this queue object
    // WARNING: -the legacy callback cannot observe cancellation; migrate it to
    //          the stop-aware overload before relying on prompt shutdown
    //        -returned thread handle may already be closed if thread finishes before
    //         return from StartThread() and StartThread() is called from another thread or
    //         KillAll()
    // can be called from any thread
    HANDLE StartThread(unsigned(WINAPI* body)(void*), void* param, unsigned stack_size = 0,
                       HANDLE* threadHandle = NULL, DWORD* threadID = NULL);

    // New callbacks receive a manual-reset stop event owned until their safe
    // join, allowing plug-ins to migrate without changing the queue surface.
    HANDLE StartThread(CThreadQueueStopBody body, void* param, unsigned stack_size = 0,
                       HANDLE* threadHandle = NULL, DWORD* threadID = NULL);

    // waits for thread termination from this queue; 'thread' is thread handle, which may already
    // be closed (this object closes it when calling StartThread and KillAll); if
    // waits for thread termination, removes the thread from the queue, and closes its handle
    BOOL WaitForExit(HANDLE thread, int milliseconds = INFINITE);

    // Requests cooperative stop and safely joins this queue entry. Legacy
    // callbacks may take longer because they do not yet consume the stop event.
    void KillThread(HANDLE thread, DWORD exitCode = 666);

    // verifies that all threads have finished; if 'force' is TRUE and some thread is still running,
    // waits 'forceWaitTime' (in ms) for all threads to finish, then requests
    // cooperative stop and safely joins running threads; returns TRUE with
    // force TRUE only after every callback has exited;
    // if 'force' is FALSE and some thread is still running, waits 'waitTime' (in ms) for termination
    // of all threads, if something is still running then, returns FALSE; time INFINITE = unlimited
    // waiting
    // can be called from any thread
    BOOL KillAll(BOOL force, int waitTime = 1000, int forceWaitTime = 200, DWORD exitCode = 666);

protected:                                                 // internal unsynchronized methods
    BOOL Add(CThreadQueueItem* item);                      // adds item to queue, returns success
    BOOL FindAndLockItem(HANDLE thread);                   // finds item for 'thread' in queue and locks it
    void UnlockItem(HANDLE thread, BOOL deleteIfUnlocked); // unlocks item for 'thread' in queue, optionally deletes it
    void ClearFinishedThreads();                           // removes already finished threads from the queue
    struct CThreadQueueLaunchData;
    static DWORD WINAPI ThreadBase(void* param, HANDLE stopEvent);
    static void WINAPI NameThread(const char* name);
    HANDLE StartThreadInternal(unsigned(WINAPI* legacyBody)(void*), CThreadQueueStopBody stopBody,
                               void* param, unsigned stackSize, HANDLE* threadHandle, DWORD* threadID);
};

//
// ****************************************************************************
// CThread
//
// WARNING: must be allocated (cannot have CThread only on stack); deallocates itself
//        only if the thread is successfully created by Create()

class CThread
{
public:
    // thread handle (NULL = thread is not running yet / has not run yet), CAUTION: after thread
    // termination it closes itself (is invalid), and this object is already deallocated
    HANDLE Thread;

protected:
    char Name[101]; // buffer for thread name (used in TRACE and CALL-STACK for thread identification)
                    // WARNING: if thread data will contain references to stack or other temporary objects,
                    //        ensure these references are used only while they are valid

public:
    CThread(const char* name = NULL);
    virtual ~CThread() {} // so descendant destructors are called correctly

    // creates (starts) a thread in thread queue 'queue'; 'stack_size' is the stack size of the
    // new thread in bytes (0 = default); returns handle of new thread or NULL on error;
    // handle closing is ensured by 'queue' object; if thread creation succeeds, this object
    // is deallocated when the thread terminates; on startup error, object deallocation is up to caller
    // WARNING: without adding synchronization thread can finish even before returning from Create() ->
    //        pointer "this" must therefore be considered invalid after successful call to Create(),
    //        the same applies to the returned thread handle (use only for NULL tests and for calling
    //        CThreadQueue methods: WaitForExit() and KillThread())
    // can be called from any thread
    HANDLE Create(CThreadQueue& queue, unsigned stack_size = 0, DWORD* threadID = NULL);

    // returns 'Thread', see above
    HANDLE GetHandle() { return Thread; }

    // returns thread name
    const char* GetName() { return Name; }

    // this method contains the thread body
    virtual unsigned Body() = 0;

protected:
    static unsigned WINAPI UniversalBody(void* param); // helper method for starting a thread
};
