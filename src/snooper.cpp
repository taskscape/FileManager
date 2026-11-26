// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "snooper.h"

CWindowArray WindowArray(10, 5);
CObjectArray ObjectArray(10, 5);

HANDLE Thread = NULL;
CThreadOwner* SnooperThreadOwner = NULL; // owns the legacy snooper handle through its final event-driven join
HANDLE DataUsageMutex = NULL;       // because of arrays with data for thread and process
HANDLE RefreshFinishedEvent = NULL; // because of "PostMessage", waits for processing
HANDLE WantDataEvent = NULL;        // main thread wants to access shared data
HANDLE TerminateEvent = NULL;       // main thread wants to terminate snooper thread
HANDLE ContinueEvent = NULL;        // auxiliary event for synchronization
HANDLE BeginSuspendEvent = NULL;    // start of suspend mode
HANDLE EndSuspendEvent = NULL;      // end of suspend mode for snooper
HANDLE SharesEvent = NULL;          // will be signaled if LanMan Shares change

int SnooperSuspended = 0;

volatile LONGLONG SnooperRefreshPosts = 0;
volatile LONGLONG SnooperRefreshWaits = 0;
volatile LONGLONG SnooperSuspendRefreshPosts = 0;

CRITICAL_SECTION TimeCounterSection; // for synchronizing access to MyTimeCounter
int MyTimeCounter = 0;               // current time

HANDLE SafeFindCloseThread = NULL;              // thread "safe handle killer"
CThreadOwner* SafeFindCloseThreadOwner = NULL; // retains the closer until its provider callback has returned
TDirectArray<HANDLE> SafeFindCloseCNArr(10, 5); // safe (non-hanging) closing of change-notify handles
CRITICAL_SECTION SafeFindCloseCS;               // critical section for access to handle array
BOOL SafeFindCloseTerminate = FALSE;            // for thread termination
HANDLE SafeFindCloseStart = NULL;               // thread "starter" - if non-signaled, waits
HANDLE SafeFindCloseFinished = NULL;            // signaled -> thread has already closed all handles

DWORD WINAPI ThreadFindCloseChangeNotification(void* param);
DWORD WINAPI ThreadSnooperOwned(void* param, HANDLE stopEvent);
DWORD WINAPI ThreadFindCloseChangeNotificationOwned(void* param, HANDLE stopEvent);

void DoWantDataEvent()
{
    ReleaseMutex(DataUsageMutex);                  // release data for main thread
    WaitForSingleObject(WantDataEvent, INFINITE);  // wait until it takes them
    WaitForSingleObject(DataUsageMutex, INFINITE); // when finished, they're ours again
    SetEvent(ContinueEvent);                       // they're ours now, let main thread continue
}

// Body of the "snooper" thread: the shared low-priority watcher. Registers
// registry-change notifications on the LanMan shares key (detects new/removed
// network shares), then runs a WaitForMultipleObjects loop over its object
// array (data-usage requests, termination, per-window refresh events),
// dispatching notifications (drive changes, share changes, media events) back
// to the main thread. Must not call main-thread functions from here.
unsigned ThreadSnooperBody(void* /*param*/) // don't call main thread functions (not even TRACE) !!!
{
    CALL_STACK_MESSAGE1("ThreadSnooperBody()");
    SetThreadNameInVCAndTrace("Snooper");
    TRACE_I("Begin");

    DWORD res;
    HKEY sharesKey;
    res = HANDLES_Q(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                                 "system\\currentcontrolset\\services\\lanmanserver\\shares",
                                 0, KEY_NOTIFY, &sharesKey));
    if (res != ERROR_SUCCESS)
    {
        sharesKey = NULL;
        TRACE_E("Unable to open key in registry (LanMan Shares). error: " << GetErrorText(res));
    }
    else // key is ok, enable notifications (without this RegNotifyChangeKeyValue won't be called anymore)
    {
        if ((res = RegNotifyChangeKeyValue(sharesKey, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, SharesEvent,
                                           TRUE)) != ERROR_SUCCESS)
        {
            TRACE_E("Unable to monitor registry (LanMan Shares). error: " << GetErrorText(res));
        }
    }

    if (WaitForSingleObject(DataUsageMutex, INFINITE) == WAIT_OBJECT_0)
    {
        SetEvent(ContinueEvent); // now data belongs to snooper, main thread can continue

        WindowArray.Add(NULL); // basic objects, must be at the beginning!
        WindowArray.Add(NULL);
        WindowArray.Add(NULL);
        WindowArray.Add(NULL);
        ObjectArray.Add(WantDataEvent);
        ObjectArray.Add(TerminateEvent);
        ObjectArray.Add(BeginSuspendEvent);
        ObjectArray.Add(SharesEvent);

        BOOL ignoreRefreshes = FALSE;        // TRUE = ignore refreshes (changes in directories), otherwise function normally
        // Refresh suppression can span the legacy tick wrap, so retain a monotonic deadline.
        CMonotonicTimePoint ignoreRefreshesDeadline = 0;
        BOOL notEnd = TRUE;
        while (notEnd)
        {
            DWORD timeout = ignoreRefreshes ? CMonotonicClock::RemainingWin32TimerDelay(ignoreRefreshesDeadline, CMonotonicClock::Now()) : INFINITE;
            if (ignoreRefreshes && timeout == 0)
            {
                ignoreRefreshes = FALSE;
                ignoreRefreshesDeadline = 0;
                timeout = INFINITE;
            }
            //      TRACE_I("Snooper is waiting for: " << (ignoreRefreshes ? min(4, ObjectArray.Count) : ObjectArray.Count) << " events");
            res = WaitForMultipleObjects(ignoreRefreshes ? min(4, ObjectArray.Count) : ObjectArray.Count,
                                         (HANDLE*)ObjectArray.GetData(),
                                         FALSE, timeout);
            CALL_STACK_MESSAGE2("ThreadSnooperBody::wait_satisfied: 0x%X", res);
            switch (res)
            {
            case WAIT_OBJECT_0:
                DoWantDataEvent();
                break; // WantDataEvent
            case WAIT_OBJECT_0 + 1:
                notEnd = FALSE;
                break;              // TerminateEvent
            case WAIT_OBJECT_0 + 2: // BeginSuspendMode
            {
                TRACE_I("Start suspend mode");

                SetEvent(ContinueEvent); // we're in suspend now -> let main thread continue

                TDirectArray<HWND> refreshPanels(10, 5); // in case monitored directory is deleted

                ObjectArray[2] = EndSuspendEvent; // instead of begin, now end of suspend mode

                BOOL setSharesEvent = FALSE; // TRUE => re-enable registry monitoring
                BOOL suspendNotFinished = TRUE;
                while (suspendNotFinished) // wait for end of suspend mode
                {                          // handling everything except changes in directories
                    timeout = ignoreRefreshes ? CMonotonicClock::RemainingWin32TimerDelay(ignoreRefreshesDeadline, CMonotonicClock::Now()) : INFINITE;
                    if (ignoreRefreshes && timeout == 0)
                    {
                        ignoreRefreshes = FALSE;
                        ignoreRefreshesDeadline = 0;
                        timeout = INFINITE;
                    }
                    res = WaitForMultipleObjects(ignoreRefreshes ? min(4, ObjectArray.Count) : ObjectArray.Count,
                                                 (HANDLE*)ObjectArray.GetData(),
                                                 FALSE, timeout);

                    CALL_STACK_MESSAGE2("ThreadSnooperBody::suspend_wait_satisfied: 0x%X", res);
                    switch (res)
                    {
                    case WAIT_OBJECT_0:
                        DoWantDataEvent();
                        break; // WantDataEvent
                    case WAIT_OBJECT_0 + 1:
                        suspendNotFinished = notEnd = FALSE;
                        break; // TerminateEvent
                    case WAIT_OBJECT_0 + 2:
                        suspendNotFinished = FALSE;
                        break;              // EndSuspendEvent
                    case WAIT_OBJECT_0 + 3: // SharesEvent
                    {
                        // refresh shares + refresh panels if needed (via WM_USER_REFRESH_SHARES)
                        setSharesEvent = TRUE;
                        break;
                    }

                    case WAIT_TIMEOUT:
                        break; // ignore (end of directory change ignoring mode)

                    default:
                    {
                        int index = res - WAIT_OBJECT_0;
                        if (index < 0 || index >= WindowArray.Count)
                        {
                            TRACE_E("Unexpected value returned from WaitForMultipleObjects(): " << res);
                            break; // in case of some other res value
                        }

                        // calling FindCloseChangeNotification invalidates other handles to same path
                        // (happens with UNC paths), so we simulate signaled-state forcibly
                        HANDLE sameHandle = NULL; // != NULL -> handle to same path
                        CFilesWindow* actWin = WindowArray[index];
                        int e;
                        for (e = 0; e < WindowArray.Count; e++)
                        {
                            CFilesWindow* w = WindowArray[e];
                            if (w != NULL && w != actWin && actWin->SamePath(w))
                            {
                                sameHandle = (HANDLE)ObjectArray[e];
                                break;
                            }
                        }

                        // change already occurred, we don't care about more, refresh will come after suspend
                        if (MainWindowCS.LockIfNotClosed())
                        {
                            //                  TRACE_I("Change notification in suspend mode: " << (MainWindow->LeftPanel == WindowArray[index] ? "left" : "right"));
                            MainWindowCS.Unlock();
                        }
                        HDEVNOTIFY panelDevNotification = WindowArray[index]->DeviceNotification;
                        if (panelDevNotification != NULL)
                        {
                            UnregisterDeviceNotification(panelDevNotification);
                            WindowArray[index]->DeviceNotification = NULL;
                        }
                        HANDLES(FindCloseChangeNotification((HANDLE)ObjectArray[index]));
                        refreshPanels.Add(WindowArray[index]->HWindow); // add to refresh list
                        ObjectArray.Delete(index);                      // remove from list
                        WindowArray.Delete(index);

                        // if we need to work around system bug, do it here
                        if (sameHandle != NULL)
                        {
                            for (index = 0; index < ObjectArray.Count; index++)
                            {
                                if ((HANDLE)ObjectArray[index] == sameHandle)
                                {
                                    HDEVNOTIFY panelDevNotification2 = WindowArray[index]->DeviceNotification;
                                    if (panelDevNotification2 != NULL)
                                    {
                                        UnregisterDeviceNotification(panelDevNotification2);
                                        WindowArray[index]->DeviceNotification = NULL;
                                    }
                                    HANDLES(FindCloseChangeNotification((HANDLE)ObjectArray[index]));
                                    refreshPanels.Add(WindowArray[index]->HWindow); // add to refresh list
                                    ObjectArray.Delete(index);                      // remove from list
                                    WindowArray.Delete(index);
                                }
                            }
                        }
                        break;
                    }
                    }
                }
                SetEvent(ContinueEvent); // we're not in suspend anymore -> let main thread continue

                if (setSharesEvent) // will monitor next changes in registry
                {
                    if (MainWindowCS.LockIfNotClosed())
                    {
                        if (MainWindow != NULL)
                            PostMessage(MainWindow->HWindow, WM_USER_REFRESH_SHARES, 0, 0);
                        MainWindowCS.Unlock();
                    }
                    if ((res = RegNotifyChangeKeyValue(sharesKey, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, SharesEvent,
                                                       TRUE)) != ERROR_SUCCESS)
                    {
                        TRACE_E("Unable to monitor registry (LanMan Shares). error: " << GetErrorText(res));
                    }
                }

                ObjectArray[2] = BeginSuspendEvent;
                TRACE_I("End suspend mode");

                CALL_STACK_MESSAGE1("ThreadSnooperBody::post_refresh");

                HANDLES(EnterCriticalSection(&TimeCounterSection));
                // refresh changed panels
                int i;
                for (i = 0; i < refreshPanels.Count; i++)
                {
                    HWND wnd = refreshPanels[i];
                    if (IsWindow(wnd))
                    {
                        // Suspended panels are emitted as one batch, never as an unbounded retry stream.
                        InterlockedIncrement64(&SnooperSuspendRefreshPosts);
                        PostMessage(wnd, WM_USER_S_REFRESH_DIR, FALSE, MyTimeCounter++);
                    }
                }
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                // also send notification about suspend mode end
                if (MainWindowCS.LockIfNotClosed())
                {
                    if (MainWindow != NULL && MainWindow->LeftPanel != NULL && MainWindow->RightPanel != NULL)
                    {
                        PostMessage(MainWindow->LeftPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                        PostMessage(MainWindow->RightPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                    }
                    MainWindowCS.Unlock();
                }

                if (refreshPanels.Count > 0)
                {
                    // take a break so system doesn't get overwhelmed
                    ignoreRefreshes = TRUE;
                    // Preserve the refresh dampening interval through the old 32-bit wrap boundary.
                    ignoreRefreshesDeadline = CMonotonicClock::DeadlineAfter(REFRESH_PAUSE);
                }
                break;
            }

            case WAIT_OBJECT_0 + 3: // SharesEvent
            {                       // let the panels refresh
                if (MainWindowCS.LockIfNotClosed())
                {
                    if (MainWindow != NULL)
                        PostMessage(MainWindow->HWindow, WM_USER_REFRESH_SHARES, 0, 0);
                    MainWindowCS.Unlock();
                }
                // we will watch for further changes in the registry
                if ((res = RegNotifyChangeKeyValue(sharesKey, TRUE, REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET, SharesEvent,
                                                   TRUE)) != ERROR_SUCCESS)
                {
                    TRACE_E("Unable to monitor registry (LanMan Shares). error: " << GetErrorText(res));
                }
                break;
            }

            case WAIT_TIMEOUT:
                break; // ignore it (end of the mode that ignores directory changes)

            default:
            {
                int index;
                index = res - WAIT_OBJECT_0;
                if (index < 0 || index >= WindowArray.Count)
                {
                    DWORD err = GetLastError();
                    TRACE_E("Unexpected value returned from WaitForMultipleObjects(): " << res);
                    break; // in case of some other value of res
                }

                // calling FindNextChangeNotification invalidates other handles to the same path
                // (it does that for UNC paths), so we forcibly simulate the signaled state
                HANDLE sameHandle = NULL; // != NULL -> handle to the same path
                CFilesWindow* actWin = WindowArray[index];
                int e;
                for (e = 0; e < WindowArray.Count; e++)
                {
                    CFilesWindow* w = WindowArray[e];
                    if (w != NULL && w != actWin && actWin->SamePath(w))
                    {
                        sameHandle = (HANDLE)ObjectArray[e];
                        break;
                    }
                }

                if (MainWindowCS.LockIfNotClosed())
                {
                    //            TRACE_I("Change notification: " << (MainWindow->LeftPanel == WindowArray[index] ? "left" : "right"));
                    MainWindowCS.Unlock();
                }
                HANDLES(EnterCriticalSection(&TimeCounterSection));
                // The following acknowledgement wait keeps at most one live snooper refresh outstanding.
                InterlockedIncrement64(&SnooperRefreshPosts);
                PostMessage(WindowArray[index]->HWindow, WM_USER_REFRESH_DIR, TRUE, MyTimeCounter++);
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                FindNextChangeNotification((HANDLE)ObjectArray[index]); // reset this change notification
                                                                        // indexes may change...
            ERROR_BYPASS:

                HANDLE objects[4];
                objects[0] = WantDataEvent;        // data may change during the refresh
                objects[1] = TerminateEvent;       // in case of exit before the refresh finishes
                objects[2] = BeginSuspendEvent;    // in case BeginSuspendMode is called during refresh
                objects[3] = RefreshFinishedEvent; // message from the main thread about refresh completion

                BOOL refreshNotFinished = TRUE;
                InterlockedIncrement64(&SnooperRefreshWaits);
                while (refreshNotFinished) // wait for processing to finish
                {                          // handling of everything except directory changes
                    res = WaitForMultipleObjects(4, objects, FALSE, INFINITE);

                    switch (res)
                    {
                    case WAIT_OBJECT_0 + 0:
                        DoWantDataEvent();
                        break;              // WantDataEvent
                    case WAIT_OBJECT_0 + 1: // TerminateEvent
                        refreshNotFinished = notEnd = FALSE;
                        break;
                    case WAIT_OBJECT_0 + 2: // BeginSuspendEvent
                        refreshNotFinished = FALSE;
                        SetEvent(BeginSuspendEvent);
                        break;
                    default:
                        refreshNotFinished = FALSE;
                        break; // RefreshFinishedEvent
                    }
                }

                // if a system bug needs to be worked around, do it here
                if (sameHandle != NULL)
                {
                    for (index = 0; index < ObjectArray.Count; index++)
                    {
                        if (sameHandle == (HANDLE)ObjectArray[index])
                        {
                            int r = WaitForSingleObject(sameHandle, 0); // simulate the wait function in case the bug disappears
                            sameHandle = NULL;

                            HANDLES(EnterCriticalSection(&TimeCounterSection));
                            InterlockedIncrement64(&SnooperRefreshPosts);
                            PostMessage(WindowArray[index]->HWindow, WM_USER_REFRESH_DIR, TRUE, MyTimeCounter++);
                            HANDLES(LeaveCriticalSection(&TimeCounterSection));

                            if (r != WAIT_TIMEOUT) // if there is no bug, look for another change
                            {
                                FindNextChangeNotification((HANDLE)ObjectArray[index]); // reset this change notification
                            }

                            goto ERROR_BYPASS;
                        }
                    }
                }

                // take a break so the system does not get overwhelmed
                ignoreRefreshes = TRUE;
                // The post-refresh pause must use the same monotonic deadline as suspend mode.
                ignoreRefreshesDeadline = CMonotonicClock::DeadlineAfter(REFRESH_PAUSE);

                break;
            }
            }
        }
        ReleaseMutex(DataUsageMutex);
    }
    if (sharesKey != NULL)
        HANDLES(RegCloseKey(sharesKey));
    TRACE_I("End");
    return 0;
}

CDirectoryRefreshMetrics GetDirectoryRefreshMetrics()
{
    CDirectoryRefreshMetrics metrics;
    metrics.Posted = InterlockedCompareExchange64(&SnooperRefreshPosts, 0, 0);
    metrics.Awaited = InterlockedCompareExchange64(&SnooperRefreshWaits, 0, 0);
    metrics.SuspendBatched = InterlockedCompareExchange64(&SnooperSuspendRefreshPosts, 0, 0);
    return metrics;
}

unsigned ThreadSnooperEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadSnooperBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Snooper: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadSnooper(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return ThreadSnooperEH(param);
}

DWORD WINAPI ThreadSnooperOwned(void* param, HANDLE /*stopEvent*/)
{
    // TerminateEvent is the established cancellation protocol; the owner supplies lifetime and joining only.
    return ThreadSnooper(param);
}

BOOL InitializeThread()
{
    //---  creation of events and mutexes for synchronization
    DataUsageMutex = HANDLES(CreateMutex(NULL, FALSE, NULL));
    if (DataUsageMutex == NULL)
    {
        TRACE_E("Unable to create DataUsageMutex mutex.");
        return FALSE;
    }
    WantDataEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (WantDataEvent == NULL)
    {
        TRACE_E("Unable to create WantDataEvent event.");
        return FALSE;
    }
    ContinueEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (ContinueEvent == NULL)
    {
        TRACE_E("Unable to create ContinueEvent event.");
        return FALSE;
    }
    RefreshFinishedEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (RefreshFinishedEvent == NULL)
    {
        TRACE_E("Unable to create RefreshFinishedEvent event.");
        return FALSE;
    }
    TerminateEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (TerminateEvent == NULL)
    {
        TRACE_E("Unable to create TerminateEvent event.");
        return FALSE;
    }
    BeginSuspendEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (BeginSuspendEvent == NULL)
    {
        TRACE_E("Unable to create BeginSuspendEvent event.");
        return FALSE;
    }
    EndSuspendEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (EndSuspendEvent == NULL)
    {
        TRACE_E("Unable to create EndSuspendEvent event.");
        return FALSE;
    }
    SharesEvent = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (SharesEvent == NULL)
    {
        TRACE_E("Unable to create SharesEvent event.");
        return FALSE;
    }

    // "starter" event for the "safe handle killer" thread
    SafeFindCloseStart = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (SafeFindCloseStart == NULL)
    {
        TRACE_E("Unable to create SafeFindCloseStart event.");
        return FALSE;
    }
    SafeFindCloseFinished = HANDLES(CreateEvent(NULL, FALSE, FALSE, NULL));
    if (SafeFindCloseFinished == NULL)
    {
        TRACE_E("Unable to create SafeFindCloseFinished event.");
        return FALSE;
    }

    HANDLES(InitializeCriticalSection(&TimeCounterSection));
    // Keep the legacy termination event while moving the raw thread handle to the shared ownership boundary.
    SnooperThreadOwner = new CThreadOwner;
    if (SnooperThreadOwner == NULL || !SnooperThreadOwner->Start(ThreadSnooperOwned, NULL, "Snooper"))
    {
        delete SnooperThreadOwner;
        SnooperThreadOwner = NULL;
        TRACE_E("Unable to start Snooper thread.");
        return FALSE;
    }
    Thread = SnooperThreadOwner->GetThreadHandle();
    //  SetThreadPriority(Thread, THREAD_PRIORITY_LOWEST);
    WaitForSingleObject(ContinueEvent, INFINITE); // wait for the snooper to acquire the data

    HANDLES(InitializeCriticalSection(&SafeFindCloseCS));
    // The closer retains its event protocol but its handle must stay owned until the callback is finished.
    SafeFindCloseThreadOwner = new CThreadOwner;
    if (SafeFindCloseThreadOwner == NULL || !SafeFindCloseThreadOwner->Start(ThreadFindCloseChangeNotificationOwned, NULL, "SafeHandleKiller"))
    {
        delete SafeFindCloseThreadOwner;
        SafeFindCloseThreadOwner = NULL;
        TRACE_E("Unable to start safe-handle-killer thread.");
        return FALSE;
    }
    SafeFindCloseThread = SafeFindCloseThreadOwner->GetThreadHandle();
    // priority must be raised so it runs before the main thread (main thread
    // needs the handles closed immediately; on error there is no active waiting, all good)
    SetThreadPriority(SafeFindCloseThread, THREAD_PRIORITY_HIGHEST);

    return TRUE;
}

void TerminateThread()
{
    if (Thread != NULL) // terminating the snooper thread
    {
        SetEvent(TerminateEvent);              // ask the snooper to stop working
        // Directory notifications can be delayed by a provider, so retain the
        // event and shared counters through cancellation and recovery phases.
        SnooperThreadOwner->StopAndJoin(CThreadShutdownDeadline("directory snooper"));
        Thread = NULL;
        delete SnooperThreadOwner;
        SnooperThreadOwner = NULL;
    }
    if (DataUsageMutex != NULL)
        HANDLES(CloseHandle(DataUsageMutex));
    if (RefreshFinishedEvent != NULL)
        HANDLES(CloseHandle(RefreshFinishedEvent));
    if (WantDataEvent != NULL)
        HANDLES(CloseHandle(WantDataEvent));
    if (ContinueEvent != NULL)
        HANDLES(CloseHandle(ContinueEvent));
    if (TerminateEvent != NULL)
        HANDLES(CloseHandle(TerminateEvent));
    if (BeginSuspendEvent != NULL)
        HANDLES(CloseHandle(BeginSuspendEvent));
    if (EndSuspendEvent != NULL)
        HANDLES(CloseHandle(EndSuspendEvent));
    if (SharesEvent != NULL)
        HANDLES(CloseHandle(SharesEvent));
    HANDLES(DeleteCriticalSection(&TimeCounterSection));

    if (SafeFindCloseThread != NULL)
    {
        SafeFindCloseTerminate = TRUE; // request that the closer exits after its current work
        SetEvent(SafeFindCloseStart);
        // The closer owns its critical section until its callback returns; a
        // deadline reports a stuck provider but never tears down that state.
        SafeFindCloseThreadOwner->StopAndJoin(CThreadShutdownDeadline("safe notification-handle closer"));
        SafeFindCloseThread = NULL;
        delete SafeFindCloseThreadOwner;
        SafeFindCloseThreadOwner = NULL;
    }
    if (SafeFindCloseStart != NULL)
        HANDLES(CloseHandle(SafeFindCloseStart));
    if (SafeFindCloseFinished != NULL)
        HANDLES(CloseHandle(SafeFindCloseFinished));
    HANDLES(DeleteCriticalSection(&SafeFindCloseCS));
}

void AddDirectory(CFilesWindow* win, const char* path, BOOL registerDevNotification)
{
    CALL_STACK_MESSAGE3("AddDirectory(, %s, %d)", path, registerDevNotification);
    SetEvent(WantDataEvent);                       // ask the snooper to release the DataUsageMutex
    WaitForSingleObject(DataUsageMutex, INFINITE); // wait for it
    SetEvent(WantDataEvent);                       // snooper can start waiting for DataUsageMutex again
                                                   //---  the data now belongs to the main thread, the snooper is waiting
    // if the path ends with a space/dot, append '\\', otherwise FindFirstChangeNotification
    // it trims spaces/dots and thus works with a different path
    char pathCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(path, pathCopy);
    HANDLE h = HANDLES_Q(FindFirstChangeNotification(path, FALSE,
                                                     FILE_NOTIFY_CHANGE_FILE_NAME |
                                                         FILE_NOTIFY_CHANGE_DIR_NAME |
                                                         FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                                         FILE_NOTIFY_CHANGE_SIZE |
                                                         FILE_NOTIFY_CHANGE_LAST_WRITE));
    if (h != INVALID_HANDLE_VALUE)
    {
        win->SetAutomaticRefresh(TRUE);
        WindowArray.Add(win);
        ObjectArray.Add(h);

        if (registerDevNotification)
        {
            // register the panel window to receive media change messages (removal, etc.)
            DEV_BROADCAST_HANDLE dbh;
            memset(&dbh, 0, sizeof(dbh));
            dbh.dbch_size = sizeof(dbh);
            dbh.dbch_devicetype = DBT_DEVTYP_HANDLE;
            dbh.dbch_handle = h;
            if (win->DeviceNotification != NULL)
            {
                TRACE_E("AddDirectory(): unexpected situation: win->DeviceNotification != NULL");
                UnregisterDeviceNotification(win->DeviceNotification);
            }
            win->DeviceNotification = RegisterDeviceNotificationA(win->HWindow, &dbh, DEVICE_NOTIFY_WINDOW_HANDLE);
        }
    }
    else
    {
        win->SetAutomaticRefresh(FALSE);
        TRACE_W("Unable to receive change notifications for directory '" << path << "' (auto-refresh will not work).");
    }
    //---
    ReleaseMutex(DataUsageMutex);                 // release the DataUsageMutex to the snooper
    WaitForSingleObject(ContinueEvent, INFINITE); // and wait until it takes it
}

// thread in which we close the handle of a "disconnected" network device (long wait)
unsigned ThreadFindCloseChangeNotificationBody(void* param)
{
    CALL_STACK_MESSAGE1("ThreadFindCloseChangeNotificationBody()");
    SetThreadNameInVCAndTrace("SafeHandleKiller");
    //  TRACE_I("Begin");

    while (!SafeFindCloseTerminate)
    {
        WaitForSingleObject(SafeFindCloseStart, INFINITE); // wait for start or termination

        while (1)
        {
            // fetch the handle
            HANDLES(EnterCriticalSection(&SafeFindCloseCS));
            HANDLE h;
            BOOL br = FALSE;

            if (SafeFindCloseCNArr.IsGood() && SafeFindCloseCNArr.Count > 0)
            {
                h = SafeFindCloseCNArr[SafeFindCloseCNArr.Count - 1];
                SafeFindCloseCNArr.Delete(SafeFindCloseCNArr.Count - 1);
                if (!SafeFindCloseCNArr.IsGood())
                    SafeFindCloseCNArr.ResetState(); // cannot fail, it only reports insufficient memory for shrinking the array
            }
            else
                br = TRUE;
            HANDLES(LeaveCriticalSection(&SafeFindCloseCS));

            if (br)
                break; // nothing left to close, we will wait for the next start

            // close the handle
            //      TRACE_I("Killing ... " << h);
            HANDLES(FindCloseChangeNotification(h));
        }

        SetEvent(SafeFindCloseFinished); // let the main thread continue ...
    }
    //  TRACE_I("End");
    return 0;
}

unsigned ThreadFindCloseChangeNotificationEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadFindCloseChangeNotificationBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Safe Handle Killer: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadFindCloseChangeNotification(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return ThreadFindCloseChangeNotificationEH(param);
}

DWORD WINAPI ThreadFindCloseChangeNotificationOwned(void* param, HANDLE /*stopEvent*/)
{
    // SafeFindCloseStart and SafeFindCloseTerminate remain the compatibility cancellation contract for this worker.
    return ThreadFindCloseChangeNotification(param);
}

void ChangeDirectory(CFilesWindow* win, const char* newPath, BOOL registerDevNotification)
{
    CALL_STACK_MESSAGE3("ChangeDirectory(, %s, %d)", newPath, registerDevNotification);
    SetEvent(WantDataEvent);                       // ask the snooper to release the DataUsageMutex
    WaitForSingleObject(DataUsageMutex, INFINITE); // wait for it
    SetEvent(WantDataEvent);                       // snooper can start waiting for DataUsageMutex again
    BOOL registerDevNot = FALSE;
    HANDLE registerDevNotHandle = NULL;
    //---  the data now belongs to the main thread, the snooper is waiting
    if (win->DeviceNotification != NULL)
    {
        UnregisterDeviceNotification(win->DeviceNotification);
        win->DeviceNotification = NULL;
    }

    int i;
    for (i = 0; i < WindowArray.Count; i++)
        if (win == WindowArray[i])
        {
            // if the change notification is on a disconnected network drive
            // we cannot afford to wait ... let another thread close it
            HANDLES(EnterCriticalSection(&SafeFindCloseCS));
            SafeFindCloseCNArr.Add(ObjectArray[i]);
            if (!SafeFindCloseCNArr.IsGood())
                SafeFindCloseCNArr.ResetState(); // ignore errors
            HANDLES(LeaveCriticalSection(&SafeFindCloseCS));
            ResetEvent(SafeFindCloseFinished);               // we will wait for it to come up...
            SetEvent(SafeFindCloseStart);                    // start the cleanup
            WaitForSingleObject(SafeFindCloseFinished, 200); // 200 ms timeout for closing the handle

            // if the path ends with a space/dot, append '\\', otherwise FindFirstChangeNotification
            // it trims spaces/dots and thus works with a different path
            char newPathCopy[3 * MAX_PATH];
            MakeCopyWithBackslashIfNeeded(newPath, newPathCopy);
            ObjectArray[i] = HANDLES_Q(FindFirstChangeNotification(newPath, FALSE,
                                                                   FILE_NOTIFY_CHANGE_FILE_NAME |
                                                                       FILE_NOTIFY_CHANGE_DIR_NAME |
                                                                       FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                                                       FILE_NOTIFY_CHANGE_SIZE |
                                                                       FILE_NOTIFY_CHANGE_LAST_WRITE));
            if ((HANDLE)ObjectArray[i] == INVALID_HANDLE_VALUE)
            {
                win->SetAutomaticRefresh(FALSE);
                ObjectArray.Delete(i); // remove it from the list
                WindowArray.Delete(i);
                TRACE_W("Unable to receive change notifications for directory '" << newPath << "' (auto-refresh will not work).");
            }
            else
            {
                if (registerDevNotification)
                {
                    registerDevNot = TRUE;
                    registerDevNotHandle = (HANDLE)ObjectArray[i];
                }
            }
            break;
        }
    //---  not found -> add it
    if (i == WindowArray.Count)
    {
        // if the path ends with a space/dot, append '\\', otherwise FindFirstChangeNotification
        // it trims spaces/dots and thus works with a different path
        char newPathCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(newPath, newPathCopy);
        HANDLE h = HANDLES_Q(FindFirstChangeNotification(newPath, FALSE,
                                                         FILE_NOTIFY_CHANGE_FILE_NAME |
                                                             FILE_NOTIFY_CHANGE_DIR_NAME |
                                                             FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                                             FILE_NOTIFY_CHANGE_SIZE |
                                                             FILE_NOTIFY_CHANGE_LAST_WRITE));
        if (h != INVALID_HANDLE_VALUE)
        {
            win->SetAutomaticRefresh(TRUE);
            WindowArray.Add(win);
            ObjectArray.Add(h);
            if (registerDevNotification)
            {
                registerDevNot = TRUE;
                registerDevNotHandle = h;
            }
        }
        else
        {
            win->SetAutomaticRefresh(FALSE);
            TRACE_W("Unable to receive change notifications for directory '" << newPath << "' (auto-refresh will not work).");
        }
    }
    if (registerDevNot)
    {
        // register the panel window to receive media change messages (removal, etc.)
        DEV_BROADCAST_HANDLE dbh;
        memset(&dbh, 0, sizeof(dbh));
        dbh.dbch_size = sizeof(dbh);
        dbh.dbch_devicetype = DBT_DEVTYP_HANDLE;
        dbh.dbch_handle = registerDevNotHandle;
        win->DeviceNotification = RegisterDeviceNotificationA(win->HWindow, &dbh, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
    //---
    ReleaseMutex(DataUsageMutex);                 // release the DataUsageMutex to the snooper
    WaitForSingleObject(ContinueEvent, INFINITE); // and wait until it takes it
}

void DetachDirectory(CFilesWindow* win, BOOL waitForHandleClosure, BOOL closeDevNotifification)
{
    CALL_STACK_MESSAGE3("DetachDirectory(, %d, %d)", waitForHandleClosure, closeDevNotifification);
    SetEvent(WantDataEvent);                       // ask the snooper to release the DataUsageMutex
    WaitForSingleObject(DataUsageMutex, INFINITE); // wait for it
    SetEvent(WantDataEvent);                       // snooper can start waiting for DataUsageMutex again
                                                   //---  the data now belongs to the main thread, the snooper is waiting
    if (closeDevNotifification && win->DeviceNotification != NULL)
    {
        UnregisterDeviceNotification(win->DeviceNotification);
        win->DeviceNotification = NULL;
    }

    int i;
    for (i = 0; i < WindowArray.Count; i++)
        if (win == WindowArray[i])
        {
            // if the change notification is on a disconnected network drive
            // we cannot afford to wait ... let another thread close it
            HANDLES(EnterCriticalSection(&SafeFindCloseCS));
            SafeFindCloseCNArr.Add(ObjectArray[i]);
            if (!SafeFindCloseCNArr.IsGood())
                SafeFindCloseCNArr.ResetState(); // ignore errors
            HANDLES(LeaveCriticalSection(&SafeFindCloseCS));
            ResetEvent(SafeFindCloseFinished);                                             // we will wait for it to come up...
            SetEvent(SafeFindCloseStart);                                                  // start the cleanup
            WaitForSingleObject(SafeFindCloseFinished, waitForHandleClosure ? 5000 : 200); // 200 ms timeout for closing the handle

            ObjectArray.Delete(i); // remove it from the list
            WindowArray.Delete(i);
            win->SetAutomaticRefresh(FALSE);
        }
    //---
    ReleaseMutex(DataUsageMutex);                 // release the DataUsageMutex to the snooper
    WaitForSingleObject(ContinueEvent, INFINITE); // and wait until it takes it
}

/*
#define SUSPMODESTACKSIZE 50

class CSuspModeStack
{
  protected:
    DWORD CallerCalledFromArr[SUSPMODESTACKSIZE];  // array of return addresses of the functions from which BeginSuspendMode() was called
    DWORD CalledFromArr[SUSPMODESTACKSIZE];        // array of addresses from which BeginSuspendMode() was called
    int Count;                                     // number of elements in the previous two arrays
    int Ignored;                                   // number of BeginSuspendMode() calls we had to ignore (SUSPMODESTACKSIZE too small -> possibly increase it)

  public:
    CSuspModeStack() {Count = 0; Ignored = 0;}
    ~CSuspModeStack() {CheckIfEmpty(1);}  // one BeginSuspendMode() is OK: it is called when the main Salamander window is deactivated (before the main window is closed)

    void Push(DWORD caller_called_from, DWORD called_from);
    void Pop(DWORD caller_called_from, DWORD called_from);
    void CheckIfEmpty(int checkLevel);
};

void
CSuspModeStack::Push(DWORD caller_called_from, DWORD called_from)
{
  if (Count < SUSPMODESTACKSIZE)
  {
    CallerCalledFromArr[Count] = caller_called_from;
    CalledFromArr[Count] = called_from;
    Count++;
  }
  else
  {
    Ignored++;
    TRACE_E("CSuspModeStack::Push(): you should increase SUSPMODESTACKSIZE! ignored=" << Ignored);
  }
}

void
CSuspModeStack::Pop(DWORD caller_called_from, DWORD called_from)
{
  if (Ignored == 0)
  {
    if (Count > 0)
    {
      Count--;
      if (CallerCalledFromArr[Count] != caller_called_from)
      {
        TRACE_E("CSuspModeStack::Pop(): strange situation: BeginCallerCalledFrom!=StopCallerCalledFrom - BeginCalledFrom,StopCalledFrom");
        TRACE_E("CSuspModeStack::Pop(): strange situation: 0x" << std::hex <<
                CallerCalledFromArr[Count] << "!=0x" << caller_called_from << " - 0x" <<
                CalledFromArr[Count] << ",0x" << called_from << std::dec);
      }
    }
    else TRACE_E("CSuspModeStack::Pop(): unexpected call!");
  }
  else Ignored--;
}

void
CSuspModeStack::CheckIfEmpty(int checkLevel)
{
  if (Count > checkLevel)
  {
    TRACE_E("CSuspModeStack::CheckIfEmpty(" << checkLevel << "): listing remaining BeginSuspendMode calls: CallerCalledFrom,CalledFrom");
    int i;
    for (i = 0; i < Count; i++)
    {
      TRACE_E("CSuspModeStack::CheckIfEmpty():: 0x" << std::hex <<
              CallerCalledFromArr[i] << ",0x" << CalledFromArr[i] << std::dec);
    }
  }
}

CSuspModeStack SuspModeStack;
*/

void BeginSuspendMode(BOOL debugDoNotTestCaller)
{
    /*
#ifdef _DEBUG     // test whether BeginSuspendMode() and EndSuspendMode() are called from the same function (by caller return address -> does not detect an "error" when called from different functions that are both called by the same function)
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

if this code ever needs to be revived, take advantage of the fact that it can be replaced (x86 and x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  SuspModeStack.Push(debugDoNotTestCaller ? 0 : caller_called_from, called_from);
#endif // _DEBUG
*/

    if (SnooperSuspended == 0)
    {
        SetEvent(BeginSuspendEvent);
        WaitForSingleObject(ContinueEvent, INFINITE);
    }
    SnooperSuspended++;
}

//#ifdef _DEBUG
//void EndSuspendModeBody()
//#else // _DEBUG
void EndSuspendMode(BOOL debugDoNotTestCaller)
//#endif // _DEBUG
{
    CALL_STACK_MESSAGE1("EndSuspendMode()");

    if (SnooperSuspended < 1)
    {
        TRACE_E("Incorrect call to EndSuspendMode()");
        SnooperSuspended = 0; // is someone misusing CM_LEFTREFRESH, CM_RIGHTREFRESH, or CM_ACTIVEREFRESH again?
    }
    else
    {
        if (SnooperSuspended == 1)
        {
            SetEvent(EndSuspendEvent);
            WaitForSingleObject(ContinueEvent, INFINITE);
        }
        SnooperSuspended--;
    }
}

/*
#ifdef _DEBUG     // test whether BeginSuspendMode() and EndSuspendMode() are called from the same function (by caller return address -> does not detect an "error" when called from different functions that are both called by the same function)
void EndSuspendMode(BOOL debugDoNotTestCaller)
{
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

if this code ever needs to be revived, take advantage of the fact that it can be replaced (x86 and x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  SuspModeStack.Pop(debugDoNotTestCaller ? 0 : caller_called_from, called_from);

  EndSuspendModeBody();
}
#endif // _DEBUG
*/
