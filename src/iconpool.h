// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "consts.h" // for CIconSizeEnum

//
// ****************************************************************************
// CIconThreadPool - Thread pool for parallel icon extraction
//
// This module provides a worker thread pool specifically optimized for icon
// extraction operations. It allows multiple icons to be extracted in parallel,
// improving performance on multi-core systems.
//

// Maximum number of worker threads in the pool
#define ICON_POOL_MAX_WORKERS 4

// Maximum number of pending work items in the queue
#define ICON_POOL_QUEUE_SIZE 64

// Visible panel entries must not wait behind speculative background warming.
enum EIconWorkPriority
{
    iwpBackground = 0,
    iwpVisible = 1,
};

// Work item types
enum EIconWorkType
{
    iwtGetFileIcon,     // Get icon from a file path using GetFileIcon()
    iwtExtractIcon,     // Extract icon by index using ExtractIcons()
    iwtLoadImageIcon,   // Load icon from .ico file using LoadImage()
};

// Work item structure - represents a single icon extraction request
struct CIconWorkItem
{
    EIconWorkType Type;           // Type of icon extraction operation
    char Path[MAX_PATH];          // File path for icon extraction
    int Index;                    // Icon index (for iwtExtractIcon)
    CIconSizeEnum IconSize;       // Desired icon size
    volatile HICON ResultIcon;    // Result: extracted icon handle (or NULL on failure)
    volatile LONG Completed;      // 1 when work is done, 0 while pending
    volatile LONG Cancelled;      // 1 if work was cancelled
    DWORD RequestId;              // Unique ID for this request
    DWORD Generation;             // Panel/listing generation that owns this request
    EIconWorkPriority Priority;   // Visible work is selected before background warming
    volatile LONG InProgress;     // Keeps a fixed queue slot stable while a worker uses it
};

// Backpressure is observable so callers can distinguish a slow consumer from
// an icon-provider failure without making the queue grow beyond its fixed budget.
struct CIconQueueMetrics
{
    LONG Capacity;
    LONG Queued;
    LONG Active;
    LONGLONG Submitted;
    LONGLONG Completed;
    LONGLONG Deduplicated;
    LONGLONG Cancelled;
    LONGLONG BackpressureRejected;
    LONGLONG VisiblePreemptions;
    LONGLONG HighWaterMark;
};

// Result callback - called when icon extraction completes.  The callback must
// copy ResultIcon if it needs to retain it because the queue owns and destroys it.
typedef void (*IconPoolResultCallback)(CIconWorkItem* item, void* context);

class CThreadOwner;

class CIconThreadPool
{
protected:
    // Each owner retains its worker handle until the pool has stopped using
    // the queue and synchronization objects that the callback can reference.
    CThreadOwner* Workers[ICON_POOL_MAX_WORKERS];
    int WorkerCount;
    
    // Fixed work slots bound memory while workers keep their selected slot stable.
    CIconWorkItem WorkQueue[ICON_POOL_QUEUE_SIZE];
    volatile LONG QueueCount;       // Number of queued or active work items
    volatile LONG ActiveCount;      // Number of items currently held by workers
    DWORD CurrentGeneration;        // Newer panel generations invalidate stale work
    
    // Synchronization
    CRITICAL_SECTION QueueLock;
    HANDLE WorkAvailableEvent;      // Signaled when work is available
    HANDLE TerminateEvent;          // Signaled to terminate workers
    HANDLE AllWorkCompletedEvent;   // Signaled only when no queued or active work remains
    
    // Callback for results
    IconPoolResultCallback ResultCallback;
    void* CallbackContext;
    
    // Request ID counter
    volatile LONG NextRequestId;

    // Monotonic counters expose loss/coalescing without retaining completed items.
    volatile LONGLONG SubmittedCount;
    volatile LONGLONG CompletedCount;
    volatile LONGLONG DeduplicatedCount;
    volatile LONGLONG CancelledCount;
    volatile LONGLONG BackpressureRejectedCount;
    volatile LONGLONG VisiblePreemptionCount;
    volatile LONGLONG HighWaterMark;
    
    // Pool state
    BOOL Initialized;

public:
    CIconThreadPool();
    ~CIconThreadPool();
    
    // Initialize the thread pool with the specified number of workers
    // Returns TRUE on success
    BOOL Initialize(int numWorkers = ICON_POOL_MAX_WORKERS);
    
    // Shutdown the thread pool and wait for workers to finish
    void Shutdown();
    
    // Set the callback for completed work items
    void SetResultCallback(IconPoolResultCallback callback, void* context);
    
    // Begin a newer panel/listing generation and cooperatively discard older work.
    DWORD BeginGeneration();

    // Submit a work item to the pool.  A visible item may preempt queued
    // background warming, but the fixed-capacity queue never allocates more slots.
    // Returns request ID on success, 0 when bounded backpressure rejects the request.
    DWORD SubmitGetFileIcon(const char* path, CIconSizeEnum iconSize,
                            DWORD generation = 0, BOOL visible = FALSE);
    DWORD SubmitExtractIcon(const char* path, int index, CIconSizeEnum iconSize,
                            DWORD generation = 0, BOOL visible = FALSE);
    DWORD SubmitLoadImageIcon(const char* path, CIconSizeEnum iconSize,
                              DWORD generation = 0, BOOL visible = FALSE);
    
    // Check if there are pending work items
    BOOL HasPendingWork();
    
    // Get count of pending work items
    int GetPendingCount();

    // Snapshot bounded-queue pressure, cancellations, and coalescing for diagnostics.
    CIconQueueMetrics GetMetrics();
    
    // Cancel all pending work items
    void CancelAllPending();

    // Cancel queued work older than the supplied visible panel/listing generation.
    void CancelObsoleteGenerations(DWORD currentGeneration);
    
    // Wait for all pending work to complete (with timeout in ms)
    // Returns TRUE if all work completed, FALSE on timeout
    BOOL WaitForCompletion(DWORD timeoutMs = INFINITE);
    
    // Check if pool is initialized
    BOOL IsInitialized() const { return Initialized; }

protected:
    // Worker thread function
    static DWORD WINAPI WorkerThreadProc(void* param, HANDLE stopEvent);
    
    // Process a single work item
    void ProcessWorkItem(CIconWorkItem* item);
    
    // Submit a work item (internal)
    DWORD SubmitWorkItem(CIconWorkItem* item);

    BOOL IsSameWorkItem(const CIconWorkItem& left, const CIconWorkItem& right) const;
    int FindReadyWorkItemLocked() const;
    void CancelWorkItemLocked(CIconWorkItem& item);
    void DiscardCancelledWorkItemsLocked();
    void ClearWorkItemLocked(CIconWorkItem& item);
    void UpdateWorkAvailableSignalLocked();
    void UpdateCompletionSignalLocked();
};

// Global icon thread pool instance
extern CIconThreadPool IconPool;

// Helper functions for the icon reader thread
// These provide a simple interface for parallel icon extraction

// Start a batch of parallel icon extractions
// Call this before submitting work items in a batch
void IconPoolBeginBatch();

// End a batch and wait for all submitted work to complete
// Returns the number of successfully extracted icons
int IconPoolEndBatch(DWORD timeoutMs = 5000);

// Check if the icon pool is available for use
BOOL IconPoolIsAvailable();
