// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#include "precomp.h"
//#include <windows.h>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif // _MSC_VER
#include <limits.h>
#include <process.h>
//#include <commctrl.h>
#include <ostream>

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include "spl_com.h"
#include "spl_base.h"
#include "dbg.h"
#include "auxtools.h"

// Plug-in workers may still be executing plug-in code during release. A
// deadline makes a slow cooperative shutdown diagnosable without reviving the
// unsafe forced-termination path.
class CPluginThreadShutdownDeadline
{
public:
    CPluginThreadShutdownDeadline(const char* workerName)
        : WorkerName(workerName != NULL ? workerName : "unnamed plug-in worker")
    {
    }

    DWORD WaitForSafeJoin(HANDLE worker) const
    {
        DWORD wait = WaitForSingleObject(worker, 5000);
        if (wait != WAIT_TIMEOUT)
            return wait;

        TraceBreach("cancellation", 5000, worker);
        wait = WaitForSingleObject(worker, 30000);
        if (wait != WAIT_TIMEOUT)
            return WAIT_TIMEOUT;

        TraceBreach("operation recovery", 30000, worker);
        WaitForSingleObject(worker, INFINITE);
        return WAIT_TIMEOUT;
    }

private:
    void TraceBreach(const char* phase, DWORD milliseconds, HANDLE worker) const
    {
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeThread(worker, &exitCode))
            exitCode = GetLastError();
        TRACE_E("Plug-in queue " << WorkerName << " breached " << phase << " deadline after "
                                  << milliseconds << " ms; thread state=" << exitCode
                                  << ". Keeping the process alive for a safe join.");
    }

private:
    const char* WorkerName;
};

//
// ****************************************************************************
// CThreadQueue
//

CThreadQueue::CThreadQueue(const char* queueName)
{
    QueueName = queueName;
    Head = NULL;
}

CThreadQueue::~CThreadQueue()
{
    // The queue owns callback state, so workers must be joined before the
    // critical section is destroyed by the containing queue object.
    KillAll(TRUE, 0, 0);
    if (Head != NULL)
        TRACE_E("Some thread is still in " << QueueName << " queue!");
}

CThreadQueueItem::CThreadQueueItem(CPluginThreadOwner* owner, DWORD tid)
{
    Owner = owner;
    Thread = owner != NULL ? owner->GetThreadHandle() : NULL;
    ThreadID = tid;
    Next = NULL;
    Locks = 0;
}

CThreadQueueItem::CThreadQueueItem(HANDLE thread, DWORD tid)
{
    // DiskMap creates workers directly, but the queue must still become the
    // sole handle owner so waits and final cleanup follow the shared invariant.
    Owner = new CPluginThreadOwner(thread, tid);
    Thread = thread;
    ThreadID = tid;
    Next = NULL;
    Locks = 0;
}

CThreadQueueItem::~CThreadQueueItem()
{
    delete Owner;
}

void CThreadQueue::ClearFinishedThreads()
{
    CThreadQueueItem* last = NULL;
    CThreadQueueItem* act = Head;
    while (act != NULL)
    {
        if (act->Locks == 0 && act->Owner->WaitForCompletion(0) != WAIT_TIMEOUT)
        { // tento thread neni zamceny + uz skoncil, vyhodime ho ze seznamu
            if (last != NULL)
                last->Next = act->Next;
            else
                Head = act->Next;
            delete act;
            act = (last != NULL ? last->Next : Head);
        }
        else
        {
            last = act;
            act = act->Next;
        }
    }
}

BOOL CThreadQueue::Add(CThreadQueueItem* item)
{
    // nejprve vyhodime thready, ktere se jiz ukoncily
    ClearFinishedThreads();

    // pridame novy thread
    if (item != NULL)
    {
        item->Next = Head;
        Head = item;
        return TRUE;
    }
    return FALSE;
}

BOOL CThreadQueue::FindAndLockItem(HANDLE thread)
{
    CScopedQueueLock lock(CS);

    CThreadQueueItem* act = Head; // zkusime najit otevreny handle threadu
    while (act != NULL)
    {
        if (act->Thread == thread)
        {
            act->Locks++;
            break;
        }
        act = act->Next;
    }

    return act != NULL; // NULL = nenalezeno
}

void CThreadQueue::UnlockItem(HANDLE thread, BOOL deleteIfUnlocked)
{
    CScopedQueueLock lock(CS);

    CThreadQueueItem* last = NULL;
    CThreadQueueItem* act = Head; // zkusime najit otevreny handle threadu
    while (act != NULL)
    {
        if (act->Thread == thread)
            break;
        last = act;
        act = act->Next;
    }
    if (act != NULL) // always true (bylo zamknute, neslo smazat)
    {
        if (act->Locks <= 0)
            TRACE_E("CThreadQueue::UnlockItem(): thread has not locks!");
        else
        {
            if (--(act->Locks) == 0 && deleteIfUnlocked) // thread uz neni zamceny a mame ho smazat
            {
                if (last != NULL)
                    last->Next = act->Next;
                else
                    Head = act->Next;
                delete act;
            }
        }
    }
    else
        TRACE_E("CThreadQueue::UnlockItem(): unable to find thread!"); // to nebyl zamknuty, ze je smazany?

}

BOOL CThreadQueue::WaitForExit(HANDLE thread, int milliseconds)
{
    CALL_STACK_MESSAGE2("CThreadQueue::WaitForExit(, %d)", milliseconds);
    BOOL ret = TRUE;
    if (thread != NULL)
    {
        if (FindAndLockItem(thread)) // thread handle found and locked - we can wait for it, then delete it
        {
            CPluginThreadOwner* owner = NULL;
            {
                CScopedQueueLock lock(CS);
                CThreadQueueItem* item = Head;
                while (item != NULL && item->Thread != thread)
                    item = item->Next;
                if (item != NULL)
                    owner = item->Owner;
            }
            ret = owner != NULL && owner->WaitForCompletion(milliseconds) != WAIT_TIMEOUT;

            UnlockItem(thread, ret);
        }
    }
    else
        TRACE_E("CThreadQueue::WaitForExit(): Nothing to wait for (parameter 'thread'==NULL)!");
    return ret;
}

void CThreadQueue::KillThread(HANDLE thread, DWORD exitCode)
{
    CALL_STACK_MESSAGE2("CThreadQueue::KillThread(, %d)", exitCode);
    (void)exitCode; // Compatibility parameter: force no longer permits unsafe thread termination.
    if (thread != NULL)
    {
        if (FindAndLockItem(thread)) // thread handle found and locked - request stop before deletion
        {
            CPluginThreadOwner* owner = NULL;
            {
                CScopedQueueLock lock(CS);
                CThreadQueueItem* item = Head;
                while (item != NULL && item->Thread != thread)
                    item = item->Next;
                if (item != NULL)
                    owner = item->Owner;
            }
            if (owner != NULL)
            {
                owner->RequestStop();
                CPluginThreadShutdownDeadline(QueueName).WaitForSafeJoin(thread);
            }

            UnlockItem(thread, TRUE);
        }
    }
    else
        TRACE_E("CThreadQueue::KillThread(): Nothing to kill (parameter 'thread'==NULL)!");
}

BOOL CThreadQueue::KillAll(BOOL force, int waitTime, int forceWaitTime, DWORD exitCode)
{
    CALL_STACK_MESSAGE5("CThreadQueue::KillAll(%d, %d, %d, %d)", force, waitTime, forceWaitTime, exitCode);
    (void)exitCode; // Compatibility parameter: cooperative joins replace forced termination.
    const DWORD initialWait = force ? forceWaitTime : waitTime;
    const ULONGLONG deadline = initialWait == INFINITE ? 0 : GetTickCount64() + initialWait;

    for (;;)
    {
        HANDLE thread = NULL;
        CPluginThreadOwner* owner = NULL;
        {
            CScopedQueueLock lock(CS);
            ClearFinishedThreads();
            if (Head == NULL)
                return TRUE;

            CThreadQueueItem* item = Head;
            while (item != NULL && item->Owner->WaitForCompletion(0) != WAIT_TIMEOUT)
                item = item->Next;
            if (item == NULL)
                continue;

            item->Locks++;
            thread = item->Thread;
            owner = item->Owner;
        }

        const ULONGLONG now = GetTickCount64();
        const DWORD remaining = initialWait == INFINITE ? INFINITE :
            (now >= deadline ? 0 : (DWORD)(deadline - now));
        if (remaining != 0 && owner->WaitForCompletion(remaining) != WAIT_TIMEOUT)
        {
            UnlockItem(thread, TRUE);
            continue;
        }

        if (!force)
        {
            UnlockItem(thread, FALSE);
            TRACE_I("KillAll(): At least one thread is still running in " << QueueName << " queue.");
            return FALSE;
        }

        // Force now means request cancellation and log its bounded phases; it
        // never permits destruction of a plug-in callback while it is running.
        owner->RequestStop();
        CPluginThreadShutdownDeadline(QueueName).WaitForSafeJoin(thread);
        UnlockItem(thread, TRUE);
    }
}

struct CThreadQueue::CThreadQueueLaunchData
{
    unsigned(WINAPI* LegacyBody)(void*);
    CThreadQueueStopBody StopBody;
    void* Parameter;
    HANDLE Accepted;
};

DWORD WINAPI CThreadQueue::ThreadBase(void* param, HANDLE stopEvent)
{
    CThreadQueueLaunchData* data = (CThreadQueueLaunchData*)param;
    unsigned(WINAPI * legacyBody)(void*) = data->LegacyBody;
    CThreadQueueStopBody stopBody = data->StopBody;
    void* threadParam = data->Parameter;
    SetEvent(data->Accepted); // The caller may release stack-backed arguments after this copy.
    delete data;

    if (stopBody != NULL)
        return stopBody(threadParam, stopEvent);
    return legacyBody != NULL ? SalamanderDebug->CallWithCallStack(legacyBody, threadParam) : ERROR_INVALID_PARAMETER;
}

void WINAPI CThreadQueue::NameThread(const char* name)
{
    SalamanderDebug->SetThreadNameInVCAndTrace(name);
}

HANDLE CThreadQueue::StartThreadInternal(unsigned(WINAPI* legacyBody)(void*), CThreadQueueStopBody stopBody,
                                         void* param, unsigned stackSize, HANDLE* threadHandle, DWORD* threadID)
{
    if (threadHandle != NULL)
        *threadHandle = NULL;
    if (threadID != NULL)
        *threadID = 0;

    // Hold the queue lock until the worker has copied its launch record. This
    // retains the legacy stack-argument guarantee without a shared hand-off event.
    CScopedQueueLock lock(CS);
    CPluginThreadOwner* owner = new CPluginThreadOwner;
    CThreadQueueLaunchData* data = new CThreadQueueLaunchData;
    if (owner == NULL || data == NULL)
    {
        delete data;
        delete owner;
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

    data->LegacyBody = legacyBody;
    data->StopBody = stopBody;
    data->Parameter = param;
    data->Accepted = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE accepted = data->Accepted;
    if (accepted == NULL || !owner->Start(ThreadBase, data, QueueName, NameThread, stackSize))
    {
        if (accepted != NULL)
            CloseHandle(accepted);
        delete data;
        delete owner;
        return NULL;
    }

    CThreadQueueItem* item = new CThreadQueueItem(owner, owner->GetThreadID());
    if (item == NULL)
    {
        TRACE_E("Unable to add thread to the " << QueueName << " queue.");
        owner->StopAndJoin(INFINITE); // Do not orphan a started callback when queue allocation fails.
        CloseHandle(accepted);
        return NULL;
    }
    if (!Add(item))
    {
        TRACE_E("Unable to add thread to the " << QueueName << " queue.");
        delete item; // Its owner requests stop and safely joins before releasing launch state.
        CloseHandle(accepted);
        return NULL;
    }

    HANDLE thread = item->Thread;
    if (WaitForSingleObject(accepted, 10000) == WAIT_TIMEOUT)
    {
        // Never return while a legacy callback could still dereference the caller's stack data.
        TRACE_E("Timed out transferring startup data to " << QueueName << " queue worker.");
        owner->RequestStop();
        CPluginThreadShutdownDeadline(QueueName).WaitForSafeJoin(thread);
        CloseHandle(accepted);
        ClearFinishedThreads();
        return NULL;
    }
    CloseHandle(accepted);

    SalamanderDebug->TraceAttachThread(thread, item->ThreadID);
    if (threadHandle != NULL)
        *threadHandle = thread;
    if (threadID != NULL)
        *threadID = item->ThreadID;
    return thread;
}

HANDLE CThreadQueue::StartThread(unsigned(WINAPI* body)(void*), void* param, unsigned stackSize,
                                 HANDLE* threadHandle, DWORD* threadID)
{
    CALL_STACK_MESSAGE2("CThreadQueue::StartThread(, , %d, ,)", stackSize);
    return StartThreadInternal(body, NULL, param, stackSize, threadHandle, threadID);
}

HANDLE CThreadQueue::StartThread(CThreadQueueStopBody body, void* param, unsigned stackSize,
                                 HANDLE* threadHandle, DWORD* threadID)
{
    CALL_STACK_MESSAGE2("CThreadQueue::StartThread(stop-aware, , %d, ,)", stackSize);
    return StartThreadInternal(NULL, body, param, stackSize, threadHandle, threadID);
}

//
// ****************************************************************************
// CThread
//

CThread::CThread(const char* name)
{
    if (name != NULL)
        lstrcpyn(Name, name, 101);
    else
        Name[0] = 0;
    Thread = NULL;
}

unsigned WINAPI
CThread::UniversalBody(void* param)
{
    CThread* thread = (CThread*)param;
    CALL_STACK_MESSAGE2("CThread::UniversalBody(thread name = \"%s\")", thread->Name);
    SalamanderDebug->SetThreadNameInVCAndTrace(thread->Name);

    unsigned ret = thread->Body(); // run thread body

    delete thread; // destroy thread object
    return ret;
}

HANDLE
CThread::Create(CThreadQueue& queue, unsigned stack_size, DWORD* threadID)
{
    // CAUTION: after calling StartThread(), 'this' can be invalid (therefore 'Thread' is written inside)
    return queue.StartThread(UniversalBody, this, stack_size, &Thread, threadID);
}
