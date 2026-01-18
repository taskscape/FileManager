// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "geticon.h"
#include "iconpool.h"

// Global icon thread pool instance
CIconThreadPool IconPool;

// Thread-local batch tracking
static __declspec(thread) BOOL InBatch = FALSE;
static __declspec(thread) int BatchSubmitCount = 0;

//
// ****************************************************************************
// CIconThreadPool
//

CIconThreadPool::CIconThreadPool()
{
    memset(Workers, 0, sizeof(Workers));
    WorkerCount = 0;
    QueueHead = 0;
    QueueTail = 0;
    QueueCount = 0;
    memset(WorkQueue, 0, sizeof(WorkQueue));
    WorkAvailableEvent = NULL;
    TerminateEvent = NULL;
    ResultCallback = NULL;
    CallbackContext = NULL;
    NextRequestId = 1;
    Initialized = FALSE;
}

CIconThreadPool::~CIconThreadPool()
{
    Shutdown();
}

BOOL CIconThreadPool::Initialize(int numWorkers)
{
    if (Initialized)
        return TRUE;
    
    if (numWorkers <= 0)
        numWorkers = 1;
    if (numWorkers > ICON_POOL_MAX_WORKERS)
        numWorkers = ICON_POOL_MAX_WORKERS;
    
    HANDLES(InitializeCriticalSection(&QueueLock));
    
    WorkAvailableEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL)); // auto-reset
    if (WorkAvailableEvent == NULL)
    {
        HANDLES(DeleteCriticalSection(&QueueLock));
        return FALSE;
    }
    
    TerminateEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL)); // manual-reset
    if (TerminateEvent == NULL)
    {
        HANDLES(CloseHandle(WorkAvailableEvent));
        WorkAvailableEvent = NULL;
        HANDLES(DeleteCriticalSection(&QueueLock));
        return FALSE;
    }
    
    // Create worker threads
    WorkerCount = 0;
    for (int i = 0; i < numWorkers; i++)
    {
        DWORD threadId;
        Workers[i] = HANDLES(CreateThread(NULL, 0, WorkerThreadProc, this, 0, &threadId));
        if (Workers[i] != NULL)
        {
            // Set lower priority so icon loading doesn't interfere with UI
            SetThreadPriority(Workers[i], THREAD_PRIORITY_BELOW_NORMAL);
            WorkerCount++;
        }
        else
        {
            TRACE_E("CIconThreadPool::Initialize(): Failed to create worker thread " << i);
        }
    }
    
    if (WorkerCount == 0)
    {
        HANDLES(CloseHandle(TerminateEvent));
        TerminateEvent = NULL;
        HANDLES(CloseHandle(WorkAvailableEvent));
        WorkAvailableEvent = NULL;
        HANDLES(DeleteCriticalSection(&QueueLock));
        return FALSE;
    }
    
    Initialized = TRUE;
    TRACE_I("CIconThreadPool::Initialize(): Created " << WorkerCount << " worker threads");
    return TRUE;
}

void CIconThreadPool::Shutdown()
{
    if (!Initialized)
        return;
    
    // Signal all workers to terminate
    SetEvent(TerminateEvent);
    
    // Wake up all workers
    for (int i = 0; i < WorkerCount; i++)
        SetEvent(WorkAvailableEvent);
    
    // Wait for all workers to finish
    if (WorkerCount > 0)
    {
        WaitForMultipleObjects(WorkerCount, Workers, TRUE, 5000);
    }
    
    // Close worker handles
    for (int i = 0; i < WorkerCount; i++)
    {
        if (Workers[i] != NULL)
        {
            HANDLES(CloseHandle(Workers[i]));
            Workers[i] = NULL;
        }
    }
    WorkerCount = 0;
    
    // Cleanup synchronization objects
    if (TerminateEvent != NULL)
    {
        HANDLES(CloseHandle(TerminateEvent));
        TerminateEvent = NULL;
    }
    if (WorkAvailableEvent != NULL)
    {
        HANDLES(CloseHandle(WorkAvailableEvent));
        WorkAvailableEvent = NULL;
    }
    
    HANDLES(DeleteCriticalSection(&QueueLock));
    
    // Destroy any remaining icons in the queue
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        if (WorkQueue[i].ResultIcon != NULL)
        {
            DestroyIcon(WorkQueue[i].ResultIcon);
            WorkQueue[i].ResultIcon = NULL;
        }
    }
    
    Initialized = FALSE;
    TRACE_I("CIconThreadPool::Shutdown(): Thread pool shut down");
}

void CIconThreadPool::SetResultCallback(IconPoolResultCallback callback, void* context)
{
    ResultCallback = callback;
    CallbackContext = context;
}

DWORD CIconThreadPool::SubmitWorkItem(CIconWorkItem* item)
{
    if (!Initialized)
        return 0;
    
    HANDLES(EnterCriticalSection(&QueueLock));
    
    if (QueueCount >= ICON_POOL_QUEUE_SIZE)
    {
        HANDLES(LeaveCriticalSection(&QueueLock));
        TRACE_I("CIconThreadPool::SubmitWorkItem(): Queue full");
        return 0; // Queue full
    }
    
    DWORD requestId = InterlockedIncrement(&NextRequestId);
    if (requestId == 0)
        requestId = InterlockedIncrement(&NextRequestId); // Skip 0
    
    item->RequestId = requestId;
    item->ResultIcon = NULL;
    item->Completed = 0;
    item->Cancelled = 0;
    
    // Copy to queue
    int slot = QueueHead;
    memcpy(&WorkQueue[slot], item, sizeof(CIconWorkItem));
    QueueHead = (QueueHead + 1) % ICON_POOL_QUEUE_SIZE;
    InterlockedIncrement(&QueueCount);
    
    HANDLES(LeaveCriticalSection(&QueueLock));
    
    // Signal that work is available
    SetEvent(WorkAvailableEvent);
    
    return requestId;
}

DWORD CIconThreadPool::SubmitGetFileIcon(const char* path, CIconSizeEnum iconSize)
{
    CIconWorkItem item;
    item.Type = iwtGetFileIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = 0;
    item.IconSize = iconSize;
    return SubmitWorkItem(&item);
}

DWORD CIconThreadPool::SubmitExtractIcon(const char* path, int index, CIconSizeEnum iconSize)
{
    CIconWorkItem item;
    item.Type = iwtExtractIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = index;
    item.IconSize = iconSize;
    return SubmitWorkItem(&item);
}

DWORD CIconThreadPool::SubmitLoadImageIcon(const char* path, CIconSizeEnum iconSize)
{
    CIconWorkItem item;
    item.Type = iwtLoadImageIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = 0;
    item.IconSize = iconSize;
    return SubmitWorkItem(&item);
}

BOOL CIconThreadPool::HasPendingWork()
{
    return QueueCount > 0;
}

int CIconThreadPool::GetPendingCount()
{
    return QueueCount;
}

void CIconThreadPool::CancelAllPending()
{
    HANDLES(EnterCriticalSection(&QueueLock));
    
    // Mark all pending items as cancelled
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        if (WorkQueue[i].RequestId != 0 && WorkQueue[i].Completed == 0)
        {
            InterlockedExchange(&WorkQueue[i].Cancelled, 1);
        }
    }
    
    HANDLES(LeaveCriticalSection(&QueueLock));
}

BOOL CIconThreadPool::WaitForCompletion(DWORD timeoutMs)
{
    DWORD startTime = GetTickCount();
    while (QueueCount > 0)
    {
        DWORD elapsed = GetTickCount() - startTime;
        if (timeoutMs != INFINITE && elapsed >= timeoutMs)
            return FALSE;
        
        Sleep(1);
    }
    return TRUE;
}

DWORD WINAPI CIconThreadPool::WorkerThreadProc(LPVOID param)
{
    CIconThreadPool* pool = (CIconThreadPool*)param;
    
    SetThreadNameInVCAndTrace("IconPoolWorker");
    
    // Initialize COM/OLE for this thread (required for shell icon operations)
    HRESULT hr = OleInitialize(NULL);
    if (FAILED(hr))
    {
        TRACE_E("CIconThreadPool::WorkerThreadProc(): OleInitialize failed");
        // Continue anyway, some operations may still work
    }
    
    HANDLE handles[2];
    handles[0] = pool->TerminateEvent;
    handles[1] = pool->WorkAvailableEvent;
    
    while (TRUE)
    {
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, 100); // 100ms timeout for periodic check
        
        if (wait == WAIT_OBJECT_0) // Terminate event
            break;
        
        // Try to get a work item
        CIconWorkItem* workItem = NULL;
        
        HANDLES(EnterCriticalSection(&pool->QueueLock));
        
        if (pool->QueueCount > 0)
        {
            workItem = &pool->WorkQueue[pool->QueueTail];
            
            // Check if already processed or cancelled
            if (workItem->Completed || workItem->Cancelled)
            {
                // Clear the slot and move on
                workItem->RequestId = 0;
                pool->QueueTail = (pool->QueueTail + 1) % ICON_POOL_QUEUE_SIZE;
                InterlockedDecrement(&pool->QueueCount);
                workItem = NULL;
            }
        }
        
        HANDLES(LeaveCriticalSection(&pool->QueueLock));
        
        if (workItem != NULL && !workItem->Cancelled)
        {
            // Process the work item
            pool->ProcessWorkItem(workItem);
            
            // Mark as completed
            InterlockedExchange(&workItem->Completed, 1);
            
            // Call result callback if set
            if (pool->ResultCallback != NULL)
            {
                pool->ResultCallback(workItem, pool->CallbackContext);
            }
            
            // Remove from queue
            HANDLES(EnterCriticalSection(&pool->QueueLock));
            workItem->RequestId = 0;
            pool->QueueTail = (pool->QueueTail + 1) % ICON_POOL_QUEUE_SIZE;
            InterlockedDecrement(&pool->QueueCount);
            HANDLES(LeaveCriticalSection(&pool->QueueLock));
        }
    }
    
    OleUninitialize();
    
    return 0;
}

void CIconThreadPool::ProcessWorkItem(CIconWorkItem* item)
{
    item->ResultIcon = NULL;
    
    if (item->Cancelled)
        return;
    
    switch (item->Type)
    {
    case iwtGetFileIcon:
        {
            // Use the same icon loading code as the main icon thread
            HICON hIcon = NULL;
            if (GetFileIcon(item->Path, FALSE, &hIcon, item->IconSize, FALSE, FALSE))
            {
                item->ResultIcon = hIcon;
            }
        }
        break;
        
    case iwtExtractIcon:
        {
            HICON hIcon = NULL;
            if (ExtractIcons(item->Path, item->Index, IconSizes[item->IconSize], IconSizes[item->IconSize],
                             &hIcon, NULL, 1, IconLRFlags) == 1)
            {
                item->ResultIcon = hIcon;
            }
        }
        break;
        
    case iwtLoadImageIcon:
        {
            HICON hIcon = (HICON)LoadImage(NULL, item->Path, IMAGE_ICON,
                                           IconSizes[item->IconSize], IconSizes[item->IconSize],
                                           LR_LOADFROMFILE | IconLRFlags);
            if (hIcon != NULL)
            {
                item->ResultIcon = hIcon;
            }
            else
            {
                // Fallback to ExtractIcons for first icon
                if (ExtractIcons(item->Path, 0, IconSizes[item->IconSize], IconSizes[item->IconSize],
                                 &hIcon, NULL, 1, IconLRFlags) == 1)
                {
                    item->ResultIcon = hIcon;
                }
            }
        }
        break;
    }
}

//
// ****************************************************************************
// Helper functions for the icon reader thread
//

void IconPoolBeginBatch()
{
    InBatch = TRUE;
    BatchSubmitCount = 0;
}

int IconPoolEndBatch(DWORD timeoutMs)
{
    if (!InBatch)
        return 0;
    
    InBatch = FALSE;
    
    if (BatchSubmitCount == 0)
        return 0;
    
    // Wait for all submitted work to complete
    IconPool.WaitForCompletion(timeoutMs);
    
    int count = BatchSubmitCount;
    BatchSubmitCount = 0;
    return count;
}

BOOL IconPoolIsAvailable()
{
    return IconPool.IsInitialized();
}
