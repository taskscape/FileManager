// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//
// ****************************************************************************

// TRUE = first running instance of version 3.0 or newer
// determined based on mutex in global namespace, so it's visible with mutexes
// from other sessions (remote desktop, fast user switching)
extern BOOL FirstInstance_3_or_later;

// shared memory contains:
//  DWORD                  - PID of process to be broken
//  DWORD                  - number of items in list
//  MAX_TL_ITEMS * CTLItem - list of items

#define MAX_TL_ITEMS 500 // maximum number of items in shared memory, cannot be changed!

#define TASKLIST_TODO_HIGHLIGHT 1 // window of process given in 'PID' should be highlighted
#define TASKLIST_TODO_BREAK 2     // process given in 'PID' should be broken
#define TASKLIST_TODO_TERMINATE 3 // process given in 'PID' should be terminated
#define TASKLIST_TODO_ACTIVATE 4  // process given in 'PID' should be activated

#define TASKLIST_TODO_TIMEOUT 5000 // 5 seconds that processes have for todo processing

#define PROCESS_STATE_STARTING 1 // our process is starting, main window doesn't exist yet
#define PROCESS_STATE_RUNNING 2  // our process is running, we have main window
#define PROCESS_STATE_ENDING 3   // our process is ending, we don't have main window anymore

#pragma pack(push, enter_include_tasklist) // so that structures are independent of the set alignment
#pragma pack(4)

extern HANDLE HSalmonProcess;

// WARNING, x64 and x86 processes communicate through this structure, beware of types (e.g. HANDLE) that have different widths
struct CProcessListItem
{
    DWORD PID;            // ProcessID, unique during process runtime, can be reused afterwards
    SYSTEMTIME StartTime; // When process was started
    DWORD IntegrityLevel; // Process Integrity Level, serves to distinguish processes run at different permission levels
    BYTE SID_MD5[16];     // MD5 calculated from process SID, serves to distinguish processes running under different users; SID has unknown length, hence this workaround
    DWORD ProcessState;   // State Salamander is in, see PROCESS_STATE_xxx
    UINT64 HMainWindow;   // (x64 friendly) Main window handle, if it already/still exists (set during its creation/destruction)
    DWORD SalmonPID;      // Salmon ProcessID, so breaking process can guarantee SetForegroundWindow right

    CProcessListItem()
    {
        PID = GetCurrentProcessId();
        GetLocalTime(&StartTime);
        GetProcessIntegrityLevel(&IntegrityLevel);
        GetSidMD5(SID_MD5);
        ProcessState = PROCESS_STATE_STARTING;
        HMainWindow = NULL;
        SalmonPID = 0;
        if (HSalmonProcess != NULL)
            SalmonPID = SalGetProcessId(HSalmonProcess); // v tuto dobu jiz Salmon bezi
    }
};

// WARNING, items can only be added to structure, because older versions of Salamander also access it
// WARNING, x64 and x86 processes communicate through this structure, beware of types (e.g. HANDLE) that have different widths
// WARNING, there's probably no point in increasing version and extending structure, because added data in that case
//        won't always be available (old Salamander version started first = in shared memory new
//        items won't be present at all) => correct solution is probably to change AS_PROCESSLIST_NAME etc. +
//        modify data as desired (feel free to enlarge, clear, change order, etc.)
struct CCommandLineParams
{
    DWORD Version;               // newer Salamander versions can increase 'Version' and start using ReservedX variables
    DWORD RequestUID;            // unique (increasing) ID of activation request
    DWORD RequestTimestamp;      // GetTickCount() value from when activation request was created
    char LeftPath[2 * MAX_PATH]; // paths to panels (left, right, or active); if empty, should not be set
    char RightPath[2 * MAX_PATH];
    char ActivePath[2 * MAX_PATH];
    DWORD ActivatePanel;         // which panel should be activated 0-none, 1-left, 2-right
    BOOL SetTitlePrefix;         // if TRUE, title prefix will be set according to TitlePrefix
    char TitlePrefix[MAX_PATH];  // title prefix, if empty, don't change; length preferably declared as MAX_PATH, instead of TITLE_PREFIX_MAX, which could change
    BOOL SetMainWindowIconIndex; // if TRUE, main window icon will be set according to MainWindowIconIndex
    DWORD MainWindowIconIndex;   // 0: first icon, 1: second icon, ...
    // WARNING, structure can be extended only if it's still declared as last in CProcessList,
    // otherwise it's too late and it must not be touched

    CCommandLineParams()
    {
        ZeroMemory(this, sizeof(CCommandLineParams));
    }
};

// Open Salamander Process List
// !!! WARNING, items can only be added to structure, because older versions of Salamander also access it
struct CProcessList
{
    DWORD Version; // newer Salamander versions can increase 'Version' and start using ReservedX variables

    DWORD ItemsCount;    // number of valid items in Items array
    DWORD ItemsStateUID; // "version" of Items list; increases with each change; serves as signal for Tasks dialog to refresh
    CProcessListItem Items[MAX_TL_ITEMS];

    DWORD Todo;                           // determines what should be done after firing event using FireEvent, contains one of TASKLIST_TODO_* values
    DWORD TodoUID;                        // order of sent request, increases for each next request
    DWORD TodoTimestamp;                  // GetTickCount() value from when Todo request was created
    DWORD PID;                            // PID for which Todo action should be performed
    CCommandLineParams CommandLineParams; // paths for panels and other parameters for activation
                                          // WARNING, if there's need to extend this structure, it would be reasonable to first extend CCommandLineParams, for example
                                          // reserve some MAX_PATH buffers and few DWORDs, if we wanted to pass new command line parameters
};

#pragma pack(pop, enter_include_tasklist)

class CTaskList
{
protected:
    HANDLE FMO;                // file-mapping-object, shared memory
    CProcessList* ProcessList; // pointer to shared memory
    HANDLE FMOMutex;           // mutex for solving FMO access
    HANDLE Event;              // event, if signaled, other processes should check
                               // if they should perform action given in Todo
    HANDLE EventProcessed;     // if one of processes performs action in Todo, it sets this
                               // event to signaled as signal to controlling process that it's done
    HANDLE TerminateEvent;     // event for terminating break-thread
    HANDLE ControlThread;      // control-thread (waits for events, which it processes immediately)
    BOOL OK;                   // did construction succeed?

public:
    CTaskList();
    ~CTaskList();

    BOOL Init();

    // fills task-list items, items - array of at least MAX_TL_ITEMS CTLItem structures, returns item count
    // 'items' can be NULL if we're only interested in 'itemsStateUID'
    // returns "version" of process list; version increases with each change in list (if item is added or removed)
    // serves for dialog as information that it should refresh list; 'itemsStateUID' can be NULL
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout when waiting for shared memory
    int GetItems(CProcessListItem* items, DWORD* itemsStateUID, BOOL* timeouted = NULL);

    // asks process 'pid' to perform action according to 'todo' (except TASKLIST_TODO_ACTIVATE)
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout when waiting for shared memory
    BOOL FireEvent(DWORD todo, DWORD pid, BOOL* timeouted = NULL);

    // if 'timeouted' is not NULL, sets whether failure was caused by timeout when waiting for shared memory
    BOOL ActivateRunningInstance(const CCommandLineParams* cmdLineParams, BOOL* timeouted = NULL);

    // finds us in process list and sets 'ProcessState' and 'HMainWindow'; returns TRUE on success, otherwise FALSE
    // if 'timeouted' is not NULL, sets whether failure was caused by timeout when waiting for shared memory
    BOOL SetProcessState(DWORD processState, HWND hMainWindow, BOOL* timeouted = NULL);

protected:
    // walks through process list and filters out non-existing items
    // must be called after successful entry to critical section 'FMOMutex'!
    // sets 'changed' to TRUE if any item was discarded, otherwise to FALSE
    BOOL RemoveKilledItems(BOOL* changed);

    friend DWORD WINAPI FControlThread(void* param);
};

extern CTaskList TaskList;

// protection of CommandLineParams access
extern CRITICAL_SECTION CommandLineParamsCS;
// serves to pass parameters for Salamander activation from Control thread to main thread
extern CCommandLineParams CommandLineParams;
// event is "signaled" as soon as main thread takes over parameters
extern HANDLE CommandLineParamsProcessed;
