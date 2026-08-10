// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "geticon.h"
#include "iconpool.h"
#include "common/thread_owner.h"

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
    QueueCount = 0;
    ActiveCount = 0;
    CurrentGeneration = 1;
    memset(WorkQueue, 0, sizeof(WorkQueue));
    WorkAvailableEvent = NULL;
    TerminateEvent = NULL;
    AllWorkCompletedEvent = NULL;
    ResultCallback = NULL;
    CallbackContext = NULL;
    NextRequestId = 1;
    SubmittedCount = 0;
    CompletedCount = 0;
    DeduplicatedCount = 0;
    CancelledCount = 0;
    BackpressureRejectedCount = 0;
    VisiblePreemptionCount = 0;
    HighWaterMark = 0;
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
    
    // A manual-reset event wakes enough workers to drain every ready fixed slot.
    WorkAvailableEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
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

    // Waiting on this event replaces polling and includes in-flight work.
    AllWorkCompletedEvent = HANDLES(CreateEvent(NULL, TRUE, TRUE, NULL));
    if (AllWorkCompletedEvent == NULL)
    {
        HANDLES(CloseHandle(TerminateEvent));
        TerminateEvent = NULL;
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
        HANDLES(CloseHandle(AllWorkCompletedEvent));
        AllWorkCompletedEvent = NULL;
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
    
    // Obsolete requests must not extend shutdown after the owner starts teardown.
    CancelAllPending();

    // Signal all workers to terminate
    SetEvent(TerminateEvent);
    
    // Wake up all workers
    for (int i = 0; i < WorkerCount; i++)
        SetEvent(WorkAvailableEvent);
    
    // A diagnostic deadline is not permission to free queue state a worker may still use.
    for (int i = 0; i < WorkerCount; i++)
        CThreadShutdownDeadline("icon work pool").WaitForSafeJoin(Workers[i]);
    
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
    if (AllWorkCompletedEvent != NULL)
    {
        HANDLES(CloseHandle(AllWorkCompletedEvent));
        AllWorkCompletedEvent = NULL;
    }
    
    // Every slot owns its result until a callback consumes it synchronously.
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
        ClearWorkItemLocked(WorkQueue[i]);

    HANDLES(DeleteCriticalSection(&QueueLock));

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
    if (!Initialized || item == NULL)
        return 0;

    HANDLES(EnterCriticalSection(&QueueLock));

    item->Generation = item->Generation != 0 ? item->Generation : CurrentGeneration;
    item->Priority = item->Priority == iwpVisible ? iwpVisible : iwpBackground;

    // Coalescing identical requests prevents repeated paints from multiplying work.
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        CIconWorkItem& queued = WorkQueue[i];
        if (queued.RequestId != 0 && !queued.Cancelled && IsSameWorkItem(queued, *item))
        {
            if (item->Priority > queued.Priority)
                queued.Priority = item->Priority;
            InterlockedIncrement64(&DeduplicatedCount);
            DWORD requestId = queued.RequestId;
            HANDLES(LeaveCriticalSection(&QueueLock));
            return requestId;
        }
    }

    if (QueueCount >= ICON_POOL_QUEUE_SIZE)
    {
        int preemptedSlot = -1;
        if (item->Priority == iwpVisible)
        {
            // Visible work may reclaim only dormant background slots; active I/O stays cooperative.
            for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
            {
                if (WorkQueue[i].RequestId != 0 && !WorkQueue[i].InProgress &&
                    !WorkQueue[i].Cancelled && WorkQueue[i].Priority == iwpBackground)
                {
                    preemptedSlot = i;
                    break;
                }
            }
        }

        if (preemptedSlot >= 0)
        {
            CancelWorkItemLocked(WorkQueue[preemptedSlot]);
            DiscardCancelledWorkItemsLocked();
            InterlockedIncrement64(&VisiblePreemptionCount);
        }
        else
        {
            InterlockedIncrement64(&BackpressureRejectedCount);
            HANDLES(LeaveCriticalSection(&QueueLock));
            TRACE_I("CIconThreadPool::SubmitWorkItem(): bounded queue rejected work");
            return 0;
        }
    }

    DWORD requestId = InterlockedIncrement(&NextRequestId);
    if (requestId == 0)
        requestId = InterlockedIncrement(&NextRequestId); // Skip 0
    
    item->RequestId = requestId;
    item->ResultIcon = NULL;
    item->Completed = 0;
    item->Cancelled = 0;
    item->InProgress = 0;

    int slot = -1;
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        if (WorkQueue[i].RequestId == 0)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        // This should be unreachable after the capacity/preemption branch, but remains bounded if corrupted.
        InterlockedIncrement64(&BackpressureRejectedCount);
        HANDLES(LeaveCriticalSection(&QueueLock));
        return 0;
    }

    memcpy(&WorkQueue[slot], item, sizeof(CIconWorkItem));
    InterlockedIncrement(&QueueCount);
    InterlockedIncrement64(&SubmittedCount);
    LONG highWater = InterlockedCompareExchange(&QueueCount, 0, 0);
    for (LONGLONG observed = InterlockedCompareExchange64(&HighWaterMark, 0, 0);
         highWater > observed && InterlockedCompareExchange64(&HighWaterMark, highWater, observed) != observed;
         observed = InterlockedCompareExchange64(&HighWaterMark, 0, 0))
    {
    }
    ResetEvent(AllWorkCompletedEvent);
    UpdateWorkAvailableSignalLocked();
    HANDLES(LeaveCriticalSection(&QueueLock));

    return requestId;
}

DWORD CIconThreadPool::SubmitGetFileIcon(const char* path, CIconSizeEnum iconSize, DWORD generation, BOOL visible)
{
    CIconWorkItem item;
    memset(&item, 0, sizeof(item));
    item.Type = iwtGetFileIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = 0;
    item.IconSize = iconSize;
    item.Generation = generation;
    item.Priority = visible ? iwpVisible : iwpBackground;
    return SubmitWorkItem(&item);
}

DWORD CIconThreadPool::SubmitExtractIcon(const char* path, int index, CIconSizeEnum iconSize, DWORD generation, BOOL visible)
{
    CIconWorkItem item;
    memset(&item, 0, sizeof(item));
    item.Type = iwtExtractIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = index;
    item.IconSize = iconSize;
    item.Generation = generation;
    item.Priority = visible ? iwpVisible : iwpBackground;
    return SubmitWorkItem(&item);
}

DWORD CIconThreadPool::SubmitLoadImageIcon(const char* path, CIconSizeEnum iconSize, DWORD generation, BOOL visible)
{
    CIconWorkItem item;
    memset(&item, 0, sizeof(item));
    item.Type = iwtLoadImageIcon;
    lstrcpynA(item.Path, path, MAX_PATH);
    item.Index = 0;
    item.IconSize = iconSize;
    item.Generation = generation;
    item.Priority = visible ? iwpVisible : iwpBackground;
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

CIconQueueMetrics CIconThreadPool::GetMetrics()
{
    CIconQueueMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.Capacity = ICON_POOL_QUEUE_SIZE;

    if (Initialized)
    {
        HANDLES(EnterCriticalSection(&QueueLock));
        metrics.Queued = QueueCount - ActiveCount;
        metrics.Active = ActiveCount;
        HANDLES(LeaveCriticalSection(&QueueLock));
    }

    metrics.Submitted = InterlockedCompareExchange64(&SubmittedCount, 0, 0);
    metrics.Completed = InterlockedCompareExchange64(&CompletedCount, 0, 0);
    metrics.Deduplicated = InterlockedCompareExchange64(&DeduplicatedCount, 0, 0);
    metrics.Cancelled = InterlockedCompareExchange64(&CancelledCount, 0, 0);
    metrics.BackpressureRejected = InterlockedCompareExchange64(&BackpressureRejectedCount, 0, 0);
    metrics.VisiblePreemptions = InterlockedCompareExchange64(&VisiblePreemptionCount, 0, 0);
    metrics.HighWaterMark = InterlockedCompareExchange64(&HighWaterMark, 0, 0);
    return metrics;
}

void CIconThreadPool::CancelAllPending()
{
    HANDLES(EnterCriticalSection(&QueueLock));

    // Cancellation frees dormant slots immediately while active providers observe it cooperatively.
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
        CancelWorkItemLocked(WorkQueue[i]);
    DiscardCancelledWorkItemsLocked();
    UpdateWorkAvailableSignalLocked();
    UpdateCompletionSignalLocked();
    HANDLES(LeaveCriticalSection(&QueueLock));
}

void CIconThreadPool::CancelObsoleteGenerations(DWORD currentGeneration)
{
    if (!Initialized || currentGeneration == 0)
        return;

    HANDLES(EnterCriticalSection(&QueueLock));
    if (currentGeneration > CurrentGeneration)
        CurrentGeneration = currentGeneration;
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        if (WorkQueue[i].RequestId != 0 && WorkQueue[i].Generation < CurrentGeneration)
            CancelWorkItemLocked(WorkQueue[i]);
    }
    DiscardCancelledWorkItemsLocked();
    UpdateWorkAvailableSignalLocked();
    UpdateCompletionSignalLocked();
    HANDLES(LeaveCriticalSection(&QueueLock));
}

DWORD CIconThreadPool::BeginGeneration()
{
    if (!Initialized)
        return 0;

    HANDLES(EnterCriticalSection(&QueueLock));
    CurrentGeneration++;
    if (CurrentGeneration == 0)
        CurrentGeneration = 1;
    DWORD generation = CurrentGeneration;
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        if (WorkQueue[i].RequestId != 0 && WorkQueue[i].Generation < generation)
            CancelWorkItemLocked(WorkQueue[i]);
    }
    DiscardCancelledWorkItemsLocked();
    UpdateWorkAvailableSignalLocked();
    UpdateCompletionSignalLocked();
    HANDLES(LeaveCriticalSection(&QueueLock));
    return generation;
}

BOOL CIconThreadPool::WaitForCompletion(DWORD timeoutMs)
{
    return AllWorkCompletedEvent != NULL &&
           WaitForSingleObject(AllWorkCompletedEvent, timeoutMs) == WAIT_OBJECT_0;
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
        DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        
        if (wait == WAIT_OBJECT_0) // Terminate event
            break;

        if (wait != WAIT_OBJECT_0 + 1)
            continue;
        
        // Select a ready fixed slot under the lock so no two workers process it.
        CIconWorkItem* workItem = NULL;
        
        HANDLES(EnterCriticalSection(&pool->QueueLock));

        int slot = pool->FindReadyWorkItemLocked();
        if (slot >= 0)
        {
            workItem = &pool->WorkQueue[slot];
            InterlockedExchange(&workItem->InProgress, 1);
            InterlockedIncrement(&pool->ActiveCount);
        }
        pool->UpdateWorkAvailableSignalLocked();
        HANDLES(LeaveCriticalSection(&pool->QueueLock));
        
        if (workItem != NULL)
        {
            if (!workItem->Cancelled)
            {
                // Process the work item
                pool->ProcessWorkItem(workItem);

                // Mark as completed
                InterlockedExchange(&workItem->Completed, 1);

                // The callback runs before slot reuse; it must copy the icon to retain it.
                if (!workItem->Cancelled && pool->ResultCallback != NULL)
                    pool->ResultCallback(workItem, pool->CallbackContext);
            }
            
            // Release this fixed slot only after the worker and synchronous callback are done.
            HANDLES(EnterCriticalSection(&pool->QueueLock));
            if (!workItem->Cancelled)
                InterlockedIncrement64(&pool->CompletedCount);
            pool->ClearWorkItemLocked(*workItem);
            InterlockedDecrement(&pool->QueueCount);
            InterlockedDecrement(&pool->ActiveCount);
            pool->UpdateWorkAvailableSignalLocked();
            pool->UpdateCompletionSignalLocked();
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

BOOL CIconThreadPool::IsSameWorkItem(const CIconWorkItem& left, const CIconWorkItem& right) const
{
    return left.Type == right.Type && left.Index == right.Index &&
           left.IconSize == right.IconSize && left.Generation == right.Generation &&
           lstrcmpiA(left.Path, right.Path) == 0;
}

int CIconThreadPool::FindReadyWorkItemLocked() const
{
    int selected = -1;
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        const CIconWorkItem& candidate = WorkQueue[i];
        if (candidate.RequestId == 0 || candidate.Cancelled || candidate.InProgress)
            continue;
        if (selected < 0 || candidate.Priority > WorkQueue[selected].Priority ||
            (candidate.Priority == WorkQueue[selected].Priority && candidate.RequestId < WorkQueue[selected].RequestId))
            selected = i;
    }
    return selected;
}

void CIconThreadPool::CancelWorkItemLocked(CIconWorkItem& item)
{
    if (item.RequestId != 0 && !item.Completed && !item.Cancelled)
    {
        InterlockedExchange(&item.Cancelled, 1);
        InterlockedIncrement64(&CancelledCount);
    }
}

void CIconThreadPool::DiscardCancelledWorkItemsLocked()
{
    for (int i = 0; i < ICON_POOL_QUEUE_SIZE; i++)
    {
        CIconWorkItem& item = WorkQueue[i];
        if (item.RequestId != 0 && item.Cancelled && !item.InProgress)
        {
            ClearWorkItemLocked(item);
            InterlockedDecrement(&QueueCount);
        }
    }
}

void CIconThreadPool::ClearWorkItemLocked(CIconWorkItem& item)
{
    if (item.ResultIcon != NULL)
        DestroyIcon(item.ResultIcon);
    memset(&item, 0, sizeof(item));
}

void CIconThreadPool::UpdateWorkAvailableSignalLocked()
{
    if (FindReadyWorkItemLocked() >= 0)
        SetEvent(WorkAvailableEvent);
    else
        ResetEvent(WorkAvailableEvent);
}

void CIconThreadPool::UpdateCompletionSignalLocked()
{
    if (QueueCount == 0)
        SetEvent(AllWorkCompletedEvent);
    else
        ResetEvent(AllWorkCompletedEvent);
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
