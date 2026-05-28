// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//
// ****************************************************************************

// TRUE = first running instance of version 3.0 or newer.
// Determined based on a mutex in the global namespace, so it can see mutexes
// from other sessions (remote desktop, fast user switching).
extern BOOL FirstInstance_3_or_later;

// Shared memory contains:
//  DWORD                  - PID of the process to break
//  DWORD                  - number of items in the list
//  MAX_TL_ITEMS * CTLItem - item list

#define MAX_TL_ITEMS 500 // maximum number of items in shared memory; cannot be changed!

#define TASKLIST_TODO_HIGHLIGHT 1 // the process window specified by 'PID' should be highlighted
#define TASKLIST_TODO_BREAK 2     // the process specified by 'PID' should be broken into
#define TASKLIST_TODO_TERMINATE 3 // the process specified by 'PID' should be terminated
#define TASKLIST_TODO_ACTIVATE 4  // the process specified by 'PID' should be activated

#define TASKLIST_TODO_TIMEOUT 5000 // 5 seconds that processes have to handle the todo

#define PROCESS_STATE_STARTING 1 // our process is starting; the main window does not exist yet
#define PROCESS_STATE_RUNNING 2  // our process is running; we have the main window
#define PROCESS_STATE_ENDING 3   // our process is ending; we no longer have the main window

#pragma pack(push, enter_include_tasklist) // keep structures independent of the current alignment setting
#pragma pack(4)

//extern HANDLE HSalmonProcess;

// CAUTION: x64 and x86 processes communicate through this structure; watch types with different widths, e.g. HANDLE.
struct CProcessListItem
{
    DWORD PID;            // ProcessID, unique during the process lifetime; it may be reused later
    SYSTEMTIME StartTime; // When the process was started
    DWORD IntegrityLevel; // Process Integrity Level, used to distinguish processes running at different permission levels
    BYTE SID_MD5[16];     // MD5 computed from the process SID, used to distinguish processes running under different users; SID has unknown length, hence this workaround
    DWORD ProcessState;   // Salamander state, see PROCESS_STATE_xxx
    UINT64 HMainWindow;   // (x64 friendly) main window handle, if it already/still exists (set during creation/destruction)
    DWORD SalmonPID;      // Salmon ProcessID, so the breaking process can guarantee it the right to call SetForegroundWindow

    CProcessListItem()
    {
        PID = GetCurrentProcessId();
        GetLocalTime(&StartTime);
        GetProcessIntegrityLevel(&IntegrityLevel);
        GetSidMD5(SID_MD5);
        ProcessState = PROCESS_STATE_STARTING;
        HMainWindow = NULL;
        SalmonPID = 0;
        //    if (HSalmonProcess != NULL)
        //      SalmonPID = GetProcessId(HSalmonProcess); // Salmon is already running at this point
    }
};

// Open Salamander Process List
// !!! CAUTION: only append fields to this structure because older Salamander versions also use it.
struct CProcessList
{
    DWORD Version; // newer Salamander versions may increase 'Version' and start using ReservedX variables

    DWORD ItemsCount;    // number of valid items in the Items array
    DWORD ItemsStateUID; // Items list "version"; increases with every change; signals the Tasks dialog to refresh
    CProcessListItem Items[MAX_TL_ITEMS];

    DWORD Todo;          // determines what to do after firing the event via FireEvent; contains one TASKLIST_TODO_* value
    DWORD TodoUID;       // order number of the sent request, increased for each subsequent request
    DWORD TodoTimestamp; // GetTickCount() value from the moment when the Todo request was created
    DWORD PID;           // PID for which the action from Todo should be performed
                         //CCommandLineParams CommandLineParams;// panel paths and other activation parameters
                         // CAUTION: if this structure needs to be extended, it would be reasonable to extend CCommandLineParams first, for example
                         // by reserving some MAX_PATH buffers and a few DWORDs if we want to pass new command-line parameters
};

#pragma pack(pop, enter_include_tasklist)

class CTaskList
{
protected:
    HANDLE FMO;                // file-mapping-object, shared memory
    CProcessList* ProcessList; // pointer into shared memory
    HANDLE FMOMutex;           // mutex for managing access to FMO
    HANDLE Event;              // when this event is signaled, the other processes should check
                               // whether the action specified by Todo should be performed
    HANDLE EventProcessed;     // if one of the processes performs the action in Todo, it sets this
                               // event to signaled to tell the controlling process that it is done
    BOOL OK;                   // did construction complete successfully?

public:
    CTaskList();
    ~CTaskList();

    BOOL Init();

    // Fills task-list items; items is an array of at least MAX_TL_ITEMS CTLItem structures. Returns the item count.
    // 'items' may be NULL if we only care about 'itemsStateUID'.
    // Returns the process list "version"; the version increases with every list change (when an item is added or removed).
    // Used by the dialog as information that it should refresh the list; 'itemsStateUID' may be NULL.
    // If 'timeouted' is not NULL, sets whether the failure was caused by a timeout while waiting for shared memory.
    int GetItems(CProcessListItem* items, DWORD* itemsStateUID, BOOL* timeouted = NULL);

    // Requests process 'pid' to perform the action specified by 'todo' (except TASKLIST_TODO_ACTIVATE).
    // If 'timeouted' is not NULL, sets whether the failure was caused by a timeout while waiting for shared memory.
    BOOL FireEvent(DWORD todo, DWORD pid, BOOL* timeouted = NULL);

protected:
    // Walks through the process list and removes nonexistent items.
    // Must be called after successfully entering the 'FMOMutex' critical section.
    // Sets 'changed' to TRUE if any item was discarded; otherwise to FALSE.
    BOOL RemoveKilledItems(BOOL* changed);
};

extern CTaskList TaskList;
