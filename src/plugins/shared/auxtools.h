// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

//
// ****************************************************************************
// CThreadQueue
//

struct CThreadQueueItem
{
    HANDLE Thread;
    DWORD ThreadID; // only for debugging purposes (finding thread in thread list in debugger)
    int Locks;      // pocet zamku, je-li > 0 nesmime zavrit 'Thread'
    CThreadQueueItem* Next;

    CThreadQueueItem(HANDLE thread, DWORD tid)
    {
        Thread = thread;
        ThreadID = tid;
        Next = NULL;
        Locks = 0;
    }
};

class CThreadQueue
{
protected:
    const char* QueueName; // queue name (only for debugging purposes)
    CThreadQueueItem* Head;
    HANDLE Continue; // we must wait for data transfer to started thread

    struct CCS // pristup z vice threadu -> nutna synchronizace
    {
        CRITICAL_SECTION cs;

        CCS() { InitializeCriticalSection(&cs); }
        ~CCS() { DeleteCriticalSection(&cs); }

        void Enter() { EnterCriticalSection(&cs); }
        void Leave() { LeaveCriticalSection(&cs); }
    } CS;

public:
    CThreadQueue(const char* queueName /* napr. "DemoPlug Viewers" */);
    ~CThreadQueue();

    // spusti funkci 'body' s parametrem 'param' v nove vytvorenem threadu se zasobnikem
    // of size 'stack_size' (0 = default); returns thread handle or NULL on error,
    // zaroven vysledek zapise pred spustenim threadu (resume) i do 'threadHandle'
    // (if not NULL), use returned thread handle only for NULL tests and for calling
    // metod CThreadQueue: WaitForExit() a KillThread(); zavreni handlu threadu zajistuje
    // tento objekt fronty
    // WARNING: -thread may start with delay after return from StartThread()
    //         (je-li 'param' ukazatel na strukturu ulozenou na zasobniku, je nutne
    //          synchronize data transfer from 'param' - main thread must wait
    //          na prevzeti dat novym threadem)
    //        -returned thread handle may already be closed if thread finishes before
    //         return from StartThread() and StartThread() is called from another thread or
    //         KillAll()
    // mozne volat z libovolneho threadu
    HANDLE StartThread(unsigned(WINAPI* body)(void*), void* param, unsigned stack_size = 0,
                       HANDLE* threadHandle = NULL, DWORD* threadID = NULL);

    // waits for thread termination from this queue; 'thread' is thread handle, which may already
    // be closed (this object closes it when calling StartThread and KillAll); if
    // docka ukonceni threadu, vyradi thread z fronty a zavre jeho handle
    BOOL WaitForExit(HANDLE thread, int milliseconds = INFINITE);

    // zabije thread z teto fronty (pres TerminateThread()); 'thread' je handle threadu,
    // which may already be closed (this object closes it when calling StartThread and KillAll);
    // if thread is found, kills it, removes from queue and closes its handle (thread object
    // is not deallocated, because its state is unknown, possibly inconsistent)
    void KillThread(HANDLE thread, DWORD exitCode = 666);

    // verifies that all threads have finished; if 'force' is TRUE and some thread is still running,
    // waits 'forceWaitTime' (in ms) for all threads to finish, then kills running threads
    // (their objects are not deallocated, because their state is unknown, possibly inconsistent);
    // returns TRUE, if all threads are terminated, with 'force' TRUE always returns TRUE;
    // je-li 'force' FALSE a nejaky thread jeste bezi, ceka 'waitTime' (v ms) na ukonceni
    // of all threads, if something is still running then, returns FALSE; time INFINITE = unlimited
    // dlouhe cekani
    // mozne volat z libovolneho threadu
    BOOL KillAll(BOOL force, int waitTime = 1000, int forceWaitTime = 200, DWORD exitCode = 666);

protected:                                                 // internal unsynchronized methods
    BOOL Add(CThreadQueueItem* item);                      // adds item to queue, returns success
    BOOL FindAndLockItem(HANDLE thread);                   // finds item for 'thread' in queue and locks it
    void UnlockItem(HANDLE thread, BOOL deleteIfUnlocked); // unlocks item for 'thread' in queue, optionally deletes it
    void ClearFinishedThreads();                           // vyradi z fronty thready, ktere jiz dobehly
    static DWORD WINAPI ThreadBase(void* param);           // univerzalni body threadu
};

//
// ****************************************************************************
// CThread
//
// WARNING: must be allocated (cannot have CThread only on stack); deallocates itself
//        jen v pripade uspesneho vytvoreni threadu metodou Create()

class CThread
{
public:
    // handle threadu (NULL = thread jeste nebezi/nebezel), POZOR: po ukonceni threadu se
    // sam zavira (je neplatny), navic tento objekt uz je dealokovany
    HANDLE Thread;

protected:
    char Name[101]; // buffer for thread name (used in TRACE and CALL-STACK for thread identification)
                    // WARNING: if thread data will contain references to stack or other temporary objects,
                    //        je potreba zajistit, aby se s temito odkazy pracovalo jen po dobu jejich platnosti

public:
    CThread(const char* name = NULL);
    virtual ~CThread() {} // aby se spravne volaly destruktory potomku

    // vytvoreni (start) threadu ve fronte threadu 'queue'; 'stack_size' je velikost zasobniku
    // new thread in bytes (0 = default); returns handle of new thread or NULL on error;
    // handle closing is ensured by 'queue' object; if thread creation succeeds, this object
    // dealokovan pri ukonceni threadu, pri chybe spousteni je dealokace objektu na volajicim
    // WARNING: without adding synchronization thread can finish even before returning from Create() ->
    //        pointer "this" must therefore be considered invalid after successful call to Create(),
    //        to same plati pro vraceny handle threadu (pouzivat jen na testy na NULL a pro volani
    //        metod CThreadQueue: WaitForExit() a KillThread())
    // mozne volat z libovolneho threadu
    HANDLE Create(CThreadQueue& queue, unsigned stack_size = 0, DWORD* threadID = NULL);

    // vraci 'Thread' viz vyse
    HANDLE GetHandle() { return Thread; }

    // vraci jmeno threadu
    const char* GetName() { return Name; }

    // tato metoda obsahuje telo threadu
    virtual unsigned Body() = 0;

protected:
    static unsigned WINAPI UniversalBody(void* param); // pomocna metoda pro spousteni threadu
};
