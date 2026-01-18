// SPDX-FileCopyrightText: 2023 Open Salamander Authors
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
};

// Result callback - called when icon extraction completes
// Note: This is called from a worker thread, synchronization is the caller's responsibility
typedef void (*IconPoolResultCallback)(CIconWorkItem* item, void* context);

class CIconThreadPool
{
protected:
    // Worker threads
    HANDLE Workers[ICON_POOL_MAX_WORKERS];
    int WorkerCount;
    
    // Work queue (circular buffer)
    CIconWorkItem WorkQueue[ICON_POOL_QUEUE_SIZE];
    volatile LONG QueueHead;        // Next slot to write to
    volatile LONG QueueTail;        // Next slot to read from
    volatile LONG QueueCount;       // Number of items in queue
    
    // Synchronization
    CRITICAL_SECTION QueueLock;
    HANDLE WorkAvailableEvent;      // Signaled when work is available
    HANDLE TerminateEvent;          // Signaled to terminate workers
    
    // Callback for results
    IconPoolResultCallback ResultCallback;
    void* CallbackContext;
    
    // Request ID counter
    volatile LONG NextRequestId;
    
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
    
    // Submit a work item to the pool
    // Returns request ID on success, 0 on failure (queue full)
    DWORD SubmitGetFileIcon(const char* path, CIconSizeEnum iconSize);
    DWORD SubmitExtractIcon(const char* path, int index, CIconSizeEnum iconSize);
    DWORD SubmitLoadImageIcon(const char* path, CIconSizeEnum iconSize);
    
    // Check if there are pending work items
    BOOL HasPendingWork();
    
    // Get count of pending work items
    int GetPendingCount();
    
    // Cancel all pending work items
    void CancelAllPending();
    
    // Wait for all pending work to complete (with timeout in ms)
    // Returns TRUE if all work completed, FALSE on timeout
    BOOL WaitForCompletion(DWORD timeoutMs = INFINITE);
    
    // Check if pool is initialized
    BOOL IsInitialized() const { return Initialized; }

protected:
    // Worker thread function
    static DWORD WINAPI WorkerThreadProc(LPVOID param);
    
    // Process a single work item
    void ProcessWorkItem(CIconWorkItem* item);
    
    // Submit a work item (internal)
    DWORD SubmitWorkItem(CIconWorkItem* item);
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
