// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>

#include "salbreak.h"
#include "tasklist.h"

#define NOHANDLES(function) function // defense against introducing HANDLES macros into the sources via CheckHnd

CTaskList TaskList;

// the process list is shared across all Salamanders in the local session
// since AS 3.0 we change the meaning of the "Break" event - it raises an exception in the target, so we get a full bug report, but the target also exits
// therefore I change the following constants "TaskscapeLtdSalamander*" -> "TaskscapeLtdSalamander3*", so that we are separated from older versions
const char* AS_PROCESSLIST_NAME = "TaskscapeLtdSalamander3bProcessList";                               // shared memory of CProcessList
const char* AS_PROCESSLIST_MUTEX_NAME = "TaskscapeLtdSalamander3bProcessListMutex";                    // synchronization for access to the shared memory
const char* AS_PROCESSLIST_EVENT_NAME = "TaskscapeLtdSalamander3bProcessListEvent";                    // firing the event (what to do is stored in the shared memory)
const char* AS_PROCESSLIST_EVENT_PROCESSED_NAME = "TaskscapeLtdSalamander3bProcessListEventProcessed"; // the fired event has been processed

const char* FIRST_SALAMANDER_MUTEX_NAME = "TaskscapeLtdSalamanderFirstInstance";     // introduced in AS 2.52 beta 1
const char* LOADSAVE_REGISTRY_MUTEX_NAME = "TaskscapeLtdSalamanderLoadSaveRegistry"; // introduced in AS 2.52 beta 1

//
// ****************************************************************************
// CTaskList
//

// We want to learn about SEH exceptions even on x64 Windows 7 SP1 and later
// http://blog.paulbetts.org/index.php/2010/07/20/the-case-of-the-disappearing-onload-exception-user-mode-callback-exceptions-in-x64/
// http://connect.microsoft.com/VisualStudio/feedback/details/550944/hardware-exceptions-on-x64-machines-are-silently-caught-in-wndproc-messages
// http://support.microsoft.com/kb/976038
void EnableExceptionsOn64()
{
    typedef BOOL(WINAPI * FSetProcessUserModeExceptionPolicy)(DWORD dwFlags);
    typedef BOOL(WINAPI * FGetProcessUserModeExceptionPolicy)(LPDWORD dwFlags);
    typedef BOOL(WINAPI * FIsWow64Process)(HANDLE, PBOOL);
#define PROCESS_CALLBACK_FILTER_ENABLED 0x1

    HINSTANCE hDLL = LoadLibrary("KERNEL32.DLL");
    if (hDLL != NULL)
    {
        FIsWow64Process isWow64 = (FIsWow64Process)GetProcAddress(hDLL, "IsWow64Process");
        FSetProcessUserModeExceptionPolicy set = (FSetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "SetProcessUserModeExceptionPolicy");
        FGetProcessUserModeExceptionPolicy get = (FGetProcessUserModeExceptionPolicy)GetProcAddress(hDLL, "GetProcessUserModeExceptionPolicy");
        if (isWow64 != NULL && set != NULL && get != NULL)
        {
            BOOL bIsWow64;
            if (isWow64(GetCurrentProcess(), &bIsWow64) && bIsWow64)
            {
                DWORD dwFlags;
                if (get(&dwFlags))
                    set(dwFlags & ~PROCESS_CALLBACK_FILTER_ENABLED);
            }
        }
        FreeLibrary(hDLL);
    }
}

CTaskList::CTaskList()
{
    EnableExceptionsOn64();

    OK = FALSE;
    FMO = NULL;
    ProcessList = NULL;
    FMOMutex = NULL;
    Event = NULL;
    EventProcessed = NULL;
}

BOOL CTaskList::Init()
{
    OK = FALSE;

    //---  try to connect to the FMO mutex - also a test of whether any Salamander is already running
    FMOMutex = NOHANDLES(OpenMutex(SYNCHRONIZE, FALSE, AS_PROCESSLIST_MUTEX_NAME));
    if (FMOMutex == NULL) // no Salamander 3.0 or newer in the local session = we are done
    {
        return FALSE;
    }
    else // another instance, we just connect ...
    {
        //---  acquire the FMO
        DWORD waitRet = WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT);
        if (waitRet == WAIT_TIMEOUT)
            return FALSE; // fail

        //---  connect to the other system objects for communication
        FMO = NOHANDLES(OpenFileMapping(FILE_MAP_WRITE, FALSE, AS_PROCESSLIST_NAME));
        if (FMO == NULL)
            return FALSE;                                                                    // fail
        ProcessList = (CProcessList*)NOHANDLES(MapViewOfFile(FMO, FILE_MAP_WRITE, 0, 0, 0)); // FIXME_X64 are we not passing x86/x64-incompatible data?
        if (ProcessList == NULL)
            return FALSE; // fail
        // so that SetEvent() can be called on the event, it must have EVENT_MODIFY_STATE set; Wait* needs SYNCHRONIZE
        Event = NOHANDLES(OpenEvent(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, AS_PROCESSLIST_EVENT_NAME));
        if (Event == NULL)
            return FALSE; // fail
        EventProcessed = NOHANDLES(OpenEvent(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, AS_PROCESSLIST_EVENT_PROCESSED_NAME));
        if (EventProcessed == NULL)
            return FALSE; // fail

        //---  add a record to the shared memory
        // we are not Salamander, we do not add anything

        //---  release the FMO
        ReleaseMutex(FMOMutex);
    }

    OK = TRUE;
    return TRUE;
}

CTaskList::~CTaskList()
{
    if (ProcessList != NULL)
        NOHANDLES(UnmapViewOfFile(ProcessList));
    if (FMO != NULL)
        NOHANDLES(CloseHandle(FMO));
    if (FMOMutex != NULL)
        NOHANDLES(CloseHandle(FMOMutex));
    if (Event != NULL)
        NOHANDLES(CloseHandle(Event));
    if (EventProcessed != NULL)
        NOHANDLES(CloseHandle(EventProcessed));
}

int CTaskList::GetItems(CProcessListItem* items, DWORD* itemsStateUID, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;
    if (OK)
    {
        BOOL changed = FALSE;
        //---  acquire the FMO
        if (WaitForSingleObject(FMOMutex, TASKLIST_TODO_TIMEOUT) == WAIT_TIMEOUT)
        {
            if (timeouted != NULL)
                *timeouted = TRUE;
            return 0; // fail
        }

        CProcessListItem* ptr = ProcessList->Items;

        //---  remove killed processes
        RemoveKilledItems(&changed);

        //---  return values
        if (items != NULL)
            memcpy(items, ptr, ProcessList->ItemsCount * sizeof(CProcessListItem));
        if (changed)
            ProcessList->ItemsStateUID++;
        if (itemsStateUID != NULL)
            *itemsStateUID = ProcessList->ItemsStateUID;

        int count = ProcessList->ItemsCount;
        //---  release the FMO
        ReleaseMutex(FMOMutex);
        return count;
    }
    else
        return 0;
}

BOOL CTaskList::FireEvent(DWORD todo, DWORD pid, BOOL* timeouted)
{
    if (timeouted != NULL)
        *timeouted = FALSE;
    if (OK)
    {
        // acquire the ProcessList
        DWORD waitRet = WaitForSingleObject(FMOMutex, 2000);
        if (waitRet == WAIT_FAILED)
            return FALSE;
        if (waitRet == WAIT_TIMEOUT)
        {
            if (timeouted != NULL)
                *timeouted = TRUE;
            return FALSE; // fail
        }

        // set the parameters being passed
        ProcessList->Todo = todo;
        ProcessList->TodoUID++;
        ProcessList->TodoTimestamp = GetTickCount();
        ProcessList->PID = pid;

        // when breaking another Salamander instance we bring its Salmon above us
        if (todo == TASKLIST_TODO_BREAK)
        {
            for (DWORD i = 0; i < ProcessList->ItemsCount; i++)
            {
                if (ProcessList->Items[i].PID == pid)
                {
                    AllowSetForegroundWindow(ProcessList->Items[i].PID);       // we also allow our own Salamander, even though that is probably unnecessary...
                    AllowSetForegroundWindow(ProcessList->Items[i].SalmonPID); // we definitely must bring its Salmon above us
                    break;
                }
            }
        }

        // release the ProcessList
        ReleaseMutex(FMOMutex);

        // start the check in all Salamanders
        ResetEvent(EventProcessed);
        SetEvent(Event);

        //---  give a moment to react (during this time someone should catch it and complete the task)
        BOOL ret = (WaitForSingleObject(EventProcessed, 1000) == WAIT_OBJECT_0);

        //---  tell all Salamanders to get ready for the next command
        ResetEvent(Event);

        //---  set the break PID back
        //    ProcessList->Todo = 0;
        //    ProcessList->PID = 0;

        //---  release the FMO

        return ret;
    }
    return FALSE;
}

BOOL CTaskList::RemoveKilledItems(BOOL* changed)
{
    if (!OK)
        return FALSE;

    if (changed != NULL)
        *changed = FALSE;
    CProcessListItem* ptr = ProcessList->Items;
    int c = ProcessList->ItemsCount;

    int i;
    for (i = 0; i < c; i++)
    {
        HANDLE h = NOHANDLES(OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, ptr[i].PID));
        if (h != NULL)
        {
            BOOL cont = FALSE;
            DWORD exitcode;
            if (!GetExitCodeProcess(h, &exitcode) || exitcode == STILL_ACTIVE)
                cont = TRUE;
            NOHANDLES(CloseHandle(h));
            if (cont)
                continue; // leave the process in the list
        }
        else
        {
            DWORD lastError = GetLastError();
            if (lastError == ERROR_ACCESS_DENIED)
            {
                continue; // leave the process in the list
            }
        }
        memmove(ptr + i, ptr + i + 1, (c - i - 1) * sizeof(CProcessListItem));
        c--;
        i--;
        if (changed != NULL)
            *changed = TRUE;
    }
    ProcessList->ItemsCount = c;

    /*
// does not work under XP if processes within one session are running under different users
// we do not have the right to open a handle of another process
//---  remove killed processes
int i;
    for (i = 0; i < c; i++)
    {
      HANDLE h = NOHANDLES(OpenProcess(PROCESS_TERMINATE, FALSE, ptr[i].PID));
      if (h != NULL)
      {
        BOOL cont = FALSE;
        DWORD exitcode;
        if (!GetExitCodeProcess(h, &exitcode) || exitcode == STILL_ACTIVE) cont = TRUE;
        NOHANDLES(CloseHandle(h));
        if (cont) continue;  // leave the process in the list
      }
//---  kick the process out of the list
      memmove(ptr + i, ptr + i + 1, (c - i - 1) * sizeof(CTLItem));
      c--;
      i--;
    }
    ((DWORD *)SharedMem)[0] = c;   // items-count
    memcpy(items, ptr, c * sizeof(CTLItem));
*/

    return TRUE;
}
