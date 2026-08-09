// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

// Shutdown and close handlers: WM_CLOSE, WM_ENDSESSION, WM_QUERYENDSESSION,
// WM_USER_CLOSE_MAINWND, WM_USER_FORCECLOSE_MAINWND

#include "precomp.h"

#include <shlwapi.h>
#undef PathIsPrefix // otherwise conflicts with CSalamanderGeneral::PathIsPrefix

#include "htmlhelp.h"
#include "stswnd.h"
#include "editwnd.h"
#include "usermenu.h"
#include "execute.h"
#include "plugins.h"
#include "fileswnd.h"
#include "toolbar.h"
#include "mainwnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "execlog.h"
#include "snooper.h"
#include "shellib.h"
#include "menu.h"
#include "pack.h"
#include "filesbox.h"
#include "drivelst.h"
#include "cache.h"
#include "gui.h"
#include <uxtheme.h>
#include "zip.h"
#include "tasklist.h"
#include "jumplist.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "worker.h"
#include "find.h"
#include "viewer.h"

// Maximum time allowed in WM_QUERYENDSESSION before Windows kills the process.
#define QUERYENDSESSION_TIMEOUT 4500

// Globals defined in mainwnd_messages.cpp, updated during shutdown save sequence.
extern CWaitWindow* GlobalSaveWaitWindow;
extern int GlobalSaveWaitWindowProgress;

//****************************************************************************
//
// Vista+: ShutdownBlockReason helpers (used only during shutdown)
//
BOOL MyShutdownBlockReasonCreate(HWND hWnd, LPCWSTR pwszReason)
{
    typedef BOOL(WINAPI * FT_ShutdownBlockReasonCreate)(HWND hWnd, LPCWSTR pwszReason);
    static FT_ShutdownBlockReasonCreate shutdownBlockReasonCreate = NULL;
    if (shutdownBlockReasonCreate == NULL && User32DLL != NULL && WindowsVistaAndLater)
    {
        shutdownBlockReasonCreate = (FT_ShutdownBlockReasonCreate)GetProcAddress(User32DLL,
                                                                                 "ShutdownBlockReasonCreate"); // Min: Vista
    }
    if (shutdownBlockReasonCreate != NULL)
        return shutdownBlockReasonCreate(hWnd, pwszReason);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

BOOL MyShutdownBlockReasonDestroy(HWND hWnd)
{
    typedef BOOL(WINAPI * FT_ShutdownBlockReasonDestroy)(HWND hWnd);
    static FT_ShutdownBlockReasonDestroy shutdownBlockReasonDestroy = NULL;
    if (shutdownBlockReasonDestroy == NULL && User32DLL != NULL && WindowsVistaAndLater)
    {
        shutdownBlockReasonDestroy = (FT_ShutdownBlockReasonDestroy)GetProcAddress(User32DLL,
                                                                                   "ShutdownBlockReasonDestroy"); // Min: Vista
    }
    if (shutdownBlockReasonDestroy != NULL)
        return shutdownBlockReasonDestroy(hWnd);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}

//****************************************************************************
//
// CMainWindow::HandleShutdown
//
// Handles WM_CLOSE, WM_ENDSESSION (with deliberate fall-through to the shared
// WM_QUERYENDSESSION / WM_USER_CLOSE_MAINWND / WM_USER_FORCECLOSE_MAINWND case),
// and those three combined cases.
//

LRESULT CMainWindow::HandleShutdown(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
    {
        PostMessage(HWindow, WM_USER_CLOSE_MAINWND, 0, 0);
        return 0;
    }

    case WM_ENDSESSION:
    {
        if (!wParam)
            return 0; // no shutdown or log off requested, nothing to handle

        // normal shutdown/log off should not come here at all; it is handled when
        // WM_QUERYENDSESSION arrives, at its end the main window closes and Salamander
        // is killed. Theoretically, TRUE should be returned to call WM_ENDSESSION so this
        // could arrive, everything is already done, just return 0 according to MSDN, but so far all Windows versions prefer killing the app.
        //
        // here we handle so-called "critical shutdown" (including log off) which
        // has the ENDSESSION_CRITICAL flag in lParam; it can be triggered using calling (EWX_FORCE is crucial):
        // ExitWindowsEx(EWX_LOGOFF | EWX_FORCE, SHTDN_REASON_MAJOR_OPERATINGSYSTEM |
        //               SHTDN_REASON_MINOR_UPGRADE | SHTDN_REASON_FLAG_PLANNED);
        // the code is here (search for SE_SHUTDOWN_NAME): https://msdn.microsoft.com/en-us/library/windows/desktop/aa376871%28v=vs.85%29.aspx
        //
        // Vista+ only: we also handle shutdown with EWX_FORCEIFHUNG flag here; it doesn't have
        // the ENDSESSION_CRITICAL flag set but forces the application to exit regardless of the return value
        // WM_QUERYENDSESSION is followed by WM_ENDSESSION and after it completes the app is killed
        // (unless the user interrupts the action with Cancel from the system dialog shown after 5s):
        // - I call this mode "forced shutdown"
        // - the system won't kill our process, no timeout is running, we won't forcibly terminate anything
        // - we must notify the user if they want to interrupt the shutdown; if they cancel, after processing this WM_ENDSESSION, the software will continue running normally
        //
        // during a "critical shutdown" (including log off):
        // - on W2K the system kills our process without warning, nothing to handle
        // - on XP it's annoying that WM_QUERYENDSESSION doesn't reveal it's "critical shutdown",
        //   so unless something stops it we'll start saving the configuration and if we don't finish within 5s
        //   Windows kills the process and the configuration is lost; theoretically, making a copy each during every shutdown would solve it,
        //   but XP rarely loses configuration,
        //   on XP, regardless of WM_QUERYENDSESSION's return value (even if no response comes within 5s,
        //   e.g. a prompt asking to cancel ongoing disk operations), WM_ENDSESSION is still sent,
        //   so we don't save configuration in WM_ENDSESSION, only perform the worst-case cleanup
        //   (stop ongoing disk operations)
        // - on Vista+ we first back up the configuration registry key in WM_QUERYENDSESSION (5s limit),
        //   then return TRUE to continue shutdown, and the system gives us another 5s to finish in WM_ENDSESSION, which we dedicate to saving the configuration,
        //   it might not finish in time, then we get killed and the configuration is left broken;
        //   on the next start we delete it and copy the last configuration from the backup created in WM_QUERYENDSESSION;
        //   on Vista+ when closing without saving configuration, we first wait 5s in WM_QUERYENDSESSION
        //   for disk operations to finish and then another 5s here in WM_ENDSESSION

        // Experimentally determined behavior during three types of shutdowns:
        //
        // EWX_FORCE:  (since Vista the ENDSESSION_CRITICAL flag is set)
        // W2K: kill without anything
        // XP: WM_QUERYENDSESSION, without a reply: WM_ENDSESSION arrives after 5s and after another 5s a kill
        //     WM_QUERYENDSESSION returns TRUE/FALSE: WM_ENDSESSION follows, kill after 5s
        // Win7-10: WM_QUERYENDSESSION, if there is no response: in 5s -> kill
        // +Vista   WM_QUERYENDSESSION returns TRUE/FALSE: WM_ENDSESSION will follow, then a kill after 5s
        //
        // EWX_FORCEIFHUNG:  (when the ENDSESSION_CRITICAL flag is not set)
        // W2K: behaves like Log Off from the Start menu
        // XP: same as W2K: Log Off from the Start menu
        // Win7-10: WM_QUERYENDSESSION, if there is no response in 5s: a black screen and Kill/Cancel (Cancel aborts the shutdown)
        // +Vista   WARNING: if the main window is not disabled (no parent message box or wait window) it kills after 5s,
        //          WM_QUERYENDSESSION returns TRUE/FALSE: WM_ENDSESSION follows,
        //                                               after 5s a black screen Kill/Cancel (Cancel aborts the shutdown)
        //
        // Log Off from the Start menu:
        // W2K: WM_QUERYENDSESSION, after 5s a message box Kill/Cancel (Cancel aborts the shutdown),
        //      WM_QUERYENDSESSION returns TRUE -> WM_ENDSESSION arrives, after 5s a Kill/Cancel message box (Cancel acts like Kill)
        //      WM_QUERYENDSESSION returns FALSE - aborts shutdown
        // XP: same as W2K
        // Win7-10: WM_QUERYENDSESSION, if there is no response: after 5s a black screen with Kill/Cancel (Cancel aborts the shutdown),
        // +Vista   WM_QUERYENDSESSION returns TRUE: WM_ENDSESSION arrives,
        //                                         after 5s a black screen with Kill/Cancel (Cancel aborts the shutdown)
        //          WM_QUERYENDSESSION returns FALSE: immediately shows a black screen with Kill/Cancel (Cancel aborts the shutdown)

        // see above for "forced shutdown" description; give the user a chance to stop the shutdown manually,
        // if they refuse, they can at least cancel running disk operations and will only lose configuration saving
        if (!SaveCfgInEndSession && !WaitInEndSession && WindowsVistaAndLater &&
            (lParam & ENDSESSION_CRITICAL) == 0)
        {
            if (ProgressDlgArray.RemoveFinishedDlgs() > 0)
            {
                WCHAR blockReason[MAX_STR_BLOCKREASON];
                if (HLanguage != NULL &&
                    LoadStringW(HLanguage, IDS_BLOCKSHUTDOWNDISKOPER, blockReason, _countof(blockReason)))
                {
                    MyShutdownBlockReasonCreate(HWindow, blockReason);
                }

                if (SalMessageBox(HWindow, LoadStr(IDS_FORCEDSHUTDOWNDISKOPER),
                                  SALAMANDER_TEXT_VERSION, MB_YESNO | MB_ICONQUESTION) == IDYES)
                {
                    ProgressDlgArray.PostCancelToAllDlgs(); // dialogs and workers run in their own threads, they may exit
                    while (ProgressDlgArray.RemoveFinishedDlgs() > 0)
                        Sleep(200); // wait until disk operations cancel
                }

                MyShutdownBlockReasonDestroy(HWindow);
            }
            else
                SalMessageBox(HWindow, LoadStr(IDS_FORCEDSHUTDOWN), SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONINFORMATION);
            // unfortunately there's no way to tell whether shutdown is still running or the user
            // has cancelled it (black full screen window on Win7). If not, the OS kills the app; we
            // already warned the user, nothing more to do.
            // Disk operations may still be running; if they don't get canceled, files remain in an
            // incomplete state, e.g. during copying the full file size is allocated but the content is not
            // copied, just filled with zeros, and the configuration won't be saved.
            return 0;
        }

        if (!SaveCfgInEndSession) // configuration should not be saved (handled later)
        {                         // WaitInEndSession or XP "critical shutdown": wait for disk operations to complete (if any are running)
            if (!WindowsVistaAndLater && ProgressDlgArray.RemoveFinishedDlgs() > 0)
                ProgressDlgArray.PostCancelToAllDlgs(); // dialogs and workers run in their own threads; there is a chance that they might exit

            while (ProgressDlgArray.RemoveFinishedDlgs() > 0)
                Sleep(200);
            return 0; // let the software close
        }

        if ((lParam & ENDSESSION_CRITICAL) == 0) // theoretically cannot happen (SaveCfgInEndSession is always TRUE)
        {
            TRACE_E("WM_ENDSESSION: unexpected SaveCfgInEndSession: it is not ENDSESSION_CRITICAL!");
            return 0;
        }

        // break; // this break is not missing! only "critical shutdown" (including log off) continues below to save configuration
    }
    case WM_QUERYENDSESSION:
    case WM_USER_CLOSE_MAINWND:
    case WM_USER_FORCECLOSE_MAINWND:
    {
        CALL_STACK_MESSAGE1("WM_USER_CLOSE_MAINWND::1");

        DWORD msgArrivalTime = GetTickCount(); // critical shutdown lasts 5s + 5s; if exceeded, we are killed, so we measure the time

        if (uMsg == WM_QUERYENDSESSION)
        {
            TRACE_I("WM_QUERYENDSESSION: message received");
            SaveCfgInEndSession = FALSE;
            WaitInEndSession = FALSE;

            if ((lParam & ENDSESSION_CRITICAL) != 0)
            {
                // precaution against WM_ENDSESSION being triggered from the code handling IdleCheckClipboard
                // when OnEnterIdle() and CannotCloseSalMainWnd were TRUE (WM_ENDSESSION would refuse to run)
                // the program is about to terminate; handling IDLE makes no sense here, and it only causes delays
                DisableIdleProcessing = TRUE;
            }
        }

        // Windows XP: during critical shutdown they don't set ENDSESSION_CRITICAL, W2K: during critical
        // shutdown they don't even send WM_QUERYENDSESSION, see above at WM_ENDSESSION

        // Vista+: endAfterCleanup: TRUE = critical shutdown (killed within 5s) cannot be refused, the app
        // will 100% terminate, so perform at least the worst-case cleanup: cancel ongoing disk operations
        // (stopping searches and closing Find windows and viewers is pointless, just read)
        BOOL endAfterCleanup = FALSE;

        if (!CanClose)
        {
            if (CanCloseButInEndSuspendMode &&
                (uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION))
            { // CanClose is FALSE only because of window activation; it doesn't prevent shutdown
            }
            else // "startup not completed" or "window close postponed until activation", exit now
            {
                if (uMsg == WM_QUERYENDSESSION)
                    TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: CanClose is FALSE");
                if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0)
                    endAfterCleanup = TRUE; // cannot be refused -> perform minimal cleanup
                else
                    return 0; // refuse close/shutdown/logoff; a forced shutdown will be detected in WM_ENDSESSION
            }
        }

        if (!endAfterCleanup && CannotCloseSalMainWnd)
        {
            TRACE_E("WM_USER_CLOSE_MAINWND: CannotCloseSalMainWnd == TRUE!");
            if (uMsg == WM_QUERYENDSESSION)
                TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: CannotCloseSalMainWnd is TRUE");
            if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0)
                endAfterCleanup = TRUE; // cannot be refused -> perform minimal cleanup
            else
                return 0; // refuse close/shutdown/logoff; a forced shutdown will be detected in WM_ENDSESSION
        }

        if (!endAfterCleanup && uMsg != WM_ENDSESSION)
        { // with WM_ENDSESSION the busy state was set in WM_QUERYENDSESSION, skip the test
            if (!SalamanderBusy)
            {
                SalamanderBusy = TRUE; // already BUSY, continue processing WM_USER_CLOSE_MAINWND
                LastSalamanderIdleTime = GetTickCount();
            }
            else
            {
                if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0)
                    endAfterCleanup = TRUE; // cannot be refused -> perform minimal cleanup
                else
                {
                    if (LockedUIReason != NULL && HasLockedUI())
                        SalMessageBox(HWindow, LockedUIReason, SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONINFORMATION);
                    else
                        TRACE_E("WM_USER_CLOSE_MAINWND: SalamanderBusy == TRUE!");
                    if (uMsg == WM_QUERYENDSESSION)
                        TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: SalamanderBusy is TRUE");
                    return 0; // refuse close/shutdown/logoff; a forced shutdown will be detected in WM_ENDSESSION
                }
            }
        }

        if (!endAfterCleanup && IsInPlugin())
        {
            TRACE_E("WM_USER_CLOSE_MAINWND: AlreadyInPlugin > 0!");
            if (uMsg == WM_QUERYENDSESSION)
                TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: AlreadyInPlugin > 0");
            if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0)
                endAfterCleanup = TRUE; // cannot be refused -> perform minimal cleanup
            else                        // cannot unload the plugin while we are in it!
                return 0;               // refuse close/shutdown/logoff; a forced shutdown will be detected in WM_ENDSESSION
        }

        // if OnClose confirmation is enabled, ask the user to confirm closing the program
        if (uMsg == WM_USER_CLOSE_MAINWND && Configuration.CnfrmOnSalClose)
        {
            MSGBOXEX_PARAMS params;
            memset(&params, 0, sizeof(params));
            params.HParent = HWindow;
            params.Flags = MSGBOXEX_YESNO | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
            params.Caption = LoadStr(IDS_QUESTION);
            params.Text = LoadStr(IDS_CANCLOSESALAMANDER);
            params.CheckBoxText = LoadStr(IDS_DONTSHOWAGAINCS);
            BOOL dontShow = !Configuration.CnfrmOnSalClose;
            params.CheckBoxValue = &dontShow;
            int ret = SalMessageBoxEx(&params);
            Configuration.CnfrmOnSalClose = !dontShow;

            if (ret != IDYES)
                return 0;
        }

        // we have some dialogs with disk operations running
        WCHAR blockReason[MAX_STR_BLOCKREASON];
        if (ProgressDlgArray.RemoveFinishedDlgs() > 0)
        {
            if ((uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION) && (lParam & ENDSESSION_CRITICAL) != 0)
            {                                               // "critical shutdown" (including log off) = no time to discuss, cancel everything so
                                                            // no "unfinished" mess remains on disk
                if (uMsg == WM_QUERYENDSESSION)             // cancel only upon the first critical shutdown message
                    ProgressDlgArray.PostCancelToAllDlgs(); // dialogs and workers run in their own threads, there is a chance that they may exit
            }
            else // report it in a window and wait for everything to finish; WM_ENDSESSION cannot arrive here
            {
                if (uMsg == WM_QUERYENDSESSION && HLanguage != NULL &&
                    LoadStringW(HLanguage, IDS_BLOCKSHUTDOWNDISKOPER, blockReason, _countof(blockReason)))
                {
                    MyShutdownBlockReasonCreate(HWindow, blockReason);
                }
                CExitingOpenSal dlg(HWindow);
                INT_PTR res = dlg.Execute();
                if (uMsg == WM_QUERYENDSESSION)
                    MyShutdownBlockReasonDestroy(HWindow);
                if (res == IDCANCEL)
                {
                    if (uMsg == WM_QUERYENDSESSION)
                        TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: user rejects to close all disk operation progress dialogs");
                    // the user does not want to exit yet
                    return 0; // refuse closing/shutdown/logoff; any "forced shutdown" will be detected later in WM_ENDSESSION
                }
                UpdateWindow(HWindow);
            }
        }

        // critical shutdown cannot be refused; alternative solution: don't save the configuration, do only
        // the bare minimum cleanup and then terminate the app (the system may kill us sooner, current mode: kill within 5s),
        // for simplicity we do not proceed with closing Find and viewer windows, the first is unnecessary,
        // the second would be nice (temporary files in TEMP would vanish)
        if (endAfterCleanup)
        { // always true: uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0
            // wait up to five seconds from receiving WM_QUERYENDSESSION for disk operations to finish
            while (ProgressDlgArray.RemoveFinishedDlgs() > 0 &&
                   GetTickCount() - msgArrivalTime <= QUERYENDSESSION_TIMEOUT - 200)
                Sleep(200);
            WaitInEndSession = TRUE;
            return TRUE; // continue to WM_ENDSESSION where we will either finish or be killed while waiting
        }

        int i = 0;
        TDirectArray<HWND> destroyArray(10, 5); // array of windows to destroy
        if (uMsg != WM_ENDSESSION)
        {
            BeginStopRefresh(); // we no longer want any panel refreshes

            // gather all Find windows
            FindDialogQueue.AddToArray(destroyArray);
        }

        CALL_STACK_MESSAGE1("WM_USER_CLOSE_MAINWND::2");

        if (uMsg != WM_ENDSESSION)
        {
            HCURSOR hOldCursor = NULL;
            CWaitWindow closingFindWin(HWindow, IDS_CLOSINGFINDWINDOWS, FALSE, ooStatic);
            BOOL showCloseFindWin = destroyArray.Count > 0;
            if (showCloseFindWin)
            {
                if (uMsg == WM_QUERYENDSESSION && HLanguage != NULL &&
                    LoadStringW(HLanguage, IDS_BLOCKSHUTDOWNFINDFILES, blockReason, _countof(blockReason)))
                {
                    MyShutdownBlockReasonCreate(HWindow, blockReason);
                }

                hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
                closingFindWin.Create();
                EnableWindow(HWindow, FALSE);
            }

            // ask whether Find windows can be closed; running searches will be stopped if requested
            BOOL endProcessing = FALSE;
            for (i = 0; i < destroyArray.Count; i++)
            {
                if (IsWindow(destroyArray[i])) // if the window still exists
                {
                    BOOL canclose = TRUE; // in case the upcoming SendMessage fails

                    WindowsManager.CS.Enter(); // we do not want any changes to WindowsManager
                    CFindDialog* findDlg = (CFindDialog*)WindowsManager.GetWindowPtr(destroyArray[i]);
                    if (findDlg != NULL) // if the window still exists, we send it a close query (otherwise it is pointless)
                    {
                        BOOL myPost = findDlg->StateOfFindCloseQuery == sofcqNotUsed;
                        if (myPost) // if this is not nesting (maybe possible, not verified but unlikely)
                        {
                            findDlg->StateOfFindCloseQuery = sofcqSentToFind;
                            PostMessage(destroyArray[i], WM_USER_QUERYCLOSEFIND, 0,
                                        uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0); // during critical shutdown we don't ask, we just cancel
                        }
                        BOOL cont = TRUE;
                        while (cont)
                        {
                            cont = FALSE;
                            WindowsManager.CS.Leave();
                            // pretend we are responding software by pumping messages
                            MSG msg;
                            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                            {
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                            }
                            // give the Find thread some time to react
                            Sleep(50);
                            // time to check whether our query has been answered
                            WindowsManager.CS.Enter(); // no changes to WindowsManager allowed
                            findDlg = (CFindDialog*)WindowsManager.GetWindowPtr(destroyArray[i]);
                            if (findDlg != NULL) // handle only if the window still exists (otherwise it is pointless)
                            {
                                if (findDlg->StateOfFindCloseQuery == sofcqCanClose ||
                                    findDlg->StateOfFindCloseQuery == sofcqCannotClose)
                                { // decision made, we are done
                                    if (findDlg->StateOfFindCloseQuery == sofcqCannotClose)
                                        canclose = FALSE;
                                    if (myPost)
                                        findDlg->StateOfFindCloseQuery = sofcqNotUsed;
                                }
                                else
                                    cont = TRUE; // keep waiting for a response from the Find thread
                            }
                        }
                    }
                    WindowsManager.CS.Leave();

                    if (!canclose)
                    {
                        if (uMsg == WM_QUERYENDSESSION)
                        {
                            TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: unable to close all Find windows");
                            MyShutdownBlockReasonDestroy(HWindow);
                        }
                        if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0)
                        {
                            // EndStopRefresh(); // during critical shutdown we don't end stop-refresh (refreshes are sent to panels)
                            endAfterCleanup = TRUE; // cannot be refused -> perform minimal cleanup
                        }
                        else
                        {
                            EndStopRefresh();
                            endProcessing = TRUE;
                        }
                        break;
                    }
                }
            }

            if (showCloseFindWin)
            {
                EnableWindow(HWindow, TRUE);
                DestroyWindow(closingFindWin.HWindow);
                SetCursor(hOldCursor);
            }

            if (endProcessing)
                return 0; // refuse close/shutdown/logoff; a forced shutdown will be detected later in WM_ENDSESSION

            // let Find windows close in their own thread
            // not done during critical shutdown: closing Find windows is pointless (column widths,
            // window size and a few other minor things won't be saved, but we ignore that)
            // note: the !endAfterCleanup check here is unnecessary because outside critical shutdown
            // endAfterCleanup is always FALSE
            if (uMsg != WM_QUERYENDSESSION || (lParam & ENDSESSION_CRITICAL) == 0) // mimo criticky shutdown
            {
                for (i = 0; i < destroyArray.Count; i++)
                {
                    if (IsWindow(destroyArray[i])) // if the window still exists
                        SendMessage(destroyArray[i], WM_USER_CLOSEFIND, 0, 0);
                }
            }

            if (showCloseFindWin && !endAfterCleanup && uMsg == WM_QUERYENDSESSION)
                MyShutdownBlockReasonDestroy(HWindow);

            if (!endAfterCleanup)
            {
                // close viewer windows (they are not child windows -> WM_DESTROY is not sent automatically)
                // we also do this during critical shutdown so TEMP files get cleaned up,
                // which might otherwise be harmful (e.g. a viewer with a decrypted file starts
                // shredding the temporary file after closing and the system kills us during shredding).
                // A better approach is to shred properly after system restart, handled in DeleteTmpCopy() method;
                // shredding does not happen during critical shutdown
                ViewerWindowQueue.BroadcastMessage(WM_CLOSE, 0, 0);

                // add a delay before calling plugin unload  - if there are Find windows or the internal viewer
                // they have time to close here (they might hold Encrypt files)
                int winsCount = ViewerWindowQueue.GetWindowCount() + FindDialogQueue.GetWindowCount();
                int timeOut = 3;
                while (winsCount > 0 && timeOut--)
                {
                    Sleep(100);
                    int c = ViewerWindowQueue.GetWindowCount() + FindDialogQueue.GetWindowCount();
                    if (winsCount > c) // windows are still closing; wait at least another 300 ms
                    {
                        winsCount = c;
                        timeOut = 3;
                    }
                }
            }
        }

        // a critical shutdown cannot be refused; workaround: skip saving the configuration,
        // perform the bare minimum cleanup and then exit the software (the system may kill us earlier, current mode: kill within 5s),
        if (endAfterCleanup)
        {
            // always true: uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0
            // wait up to five seconds from WM_QUERYENDSESSION for disk operations to finish
            while (ProgressDlgArray.RemoveFinishedDlgs() > 0 &&
                   GetTickCount() - msgArrivalTime <= QUERYENDSESSION_TIMEOUT - 200)
                Sleep(200);

            WaitInEndSession = TRUE;
            return TRUE; // continue to WM_ENDSESSION where we finish or are killed if we wait longer
        }

        if (uMsg == WM_QUERYENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0) // this applies to Vista+
        {
            BOOL cfgOK = FALSE;
            if (SALAMANDER_ROOT_REG != NULL)
            {
                // ensure exclusive access to the configuration in the registry
                LoadSaveToRegistryMutex.Enter();

                HKEY salamander;
                if (OpenKeyAux(NULL, HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salamander))
                {
                    DWORD saveInProgress;
                    if (!GetValueAux(NULL, salamander, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)))
                    { // configuration is not corrupted
                        cfgOK = TRUE;
                    }
                    CloseKeyAux(salamander);
                }
                if (!cfgOK)
                    LoadSaveToRegistryMutex.Leave(); // done with the configuration; exit the section
                                                     // NOTE: LoadSaveToRegistryMutex.Leave() is called again in WM_ENDSESSION after saving the config (see below)
            }

            BOOL backupOK = FALSE;
            if (cfgOK) // old configuration seems OK; back it up in case saving the new configuration fails
            {
                char backup[200];
                sprintf_s(backup, "%s.backup.63A7CD13", SALAMANDER_ROOT_REG); // "63A7CD13" prevents the key name from matching a user key
                SHDeleteKey(HKEY_CURRENT_USER, backup);                       // delete the old backup if one exists
                HKEY salBackup;
                if (!OpenKeyAux(NULL, HKEY_CURRENT_USER, backup, salBackup)) // check that no backup exists
                {
                    if (CreateKeyAux(NULL, HKEY_CURRENT_USER, backup, salBackup)) // create a key for the backup
                    {
                        // I tried RegCopyTree (without KEY_ALL_ACCESS it failed) and it was as fast as SHCopyKey
                        if (SHCopyKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG, salBackup, 0) == ERROR_SUCCESS)
                        { // creating the backup
                            DWORD copyIsOK = 1;
                            if (SetValueAux(NULL, salBackup, SALAMANDER_COPY_IS_OK, REG_DWORD, &copyIsOK, sizeof(DWORD)))
                                backupOK = TRUE;
                        }
                        CloseKeyAux(salBackup);
                    }
                }
                else
                    CloseKeyAux(salBackup);
                if (!backupOK)
                    LoadSaveToRegistryMutex.Leave(); // done with the configuration; exit the section
            }

            // wait up to five seconds from WM_QUERYENDSESSION for disk operations to finish
            while (ProgressDlgArray.RemoveFinishedDlgs() > 0 &&
                   GetTickCount() - msgArrivalTime <= QUERYENDSESSION_TIMEOUT - 200)
                Sleep(200);

            if (backupOK)                   // backup done, configuration will be saved in WM_ENDSESSION,
                SaveCfgInEndSession = TRUE; // if we get killed during it, the configuration will load from the backup
            else
            {
                // EndStopRefresh();  // during critical shutdown we don't end stop-refresh (refreshes are sent to panels)
                WaitInEndSession = TRUE; // backup failed, we won't risk saving the configuration
            }
            return TRUE; // we want 5s in WM_ENDSESSION, so return TRUE
        }

        if ((uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION) && HLanguage != NULL &&
            LoadStringW(HLanguage, IDS_BLOCKSHUTDOWNSAVECFG, blockReason, _countof(blockReason)))
        {
            MyShutdownBlockReasonCreate(HWindow, blockReason);
        }

        HCURSOR hOldCursor = NULL;
        CWaitWindow analysing(HWindow, IDS_SAVINGCONFIGURATION, FALSE, ooStatic, TRUE);
        HWND oldPluginMsgBoxParent = PluginMsgBoxParent;
        BOOL shutdown = uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION;
        if (shutdown) // during shutdown/log-off/restart show a wait window for all Saves (including plugins) and process the message loop (so we aren't marked as "not responding" and killed early)
        {
            // start a thread that will handle registry work while saving the configuration;
            // meanwhile this (main) thread will pump messages in the message loop
            RegistryWorkerThread.StartThread();

            hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
            analysing.SetProgressMax(7 /* number from CMainWindow::SaveConfig() -- MUST stay in sync! */ +
                                     Plugins.GetPluginSaveCount()); // minus one so they can enjoy a viewing 100%
            analysing.Create();
            GlobalSaveWaitWindow = &analysing;
            GlobalSaveWaitWindowProgress = 0;
            EnableWindow(HWindow, FALSE);

            // SaveConfiguration of plugins will be called too -> parent must be set for their message boxes
            PluginMsgBoxParent = analysing.HWindow;
        }

        // declare a "critical shutdown" so all routines should respect it and terminate everything as quickly as possible
        CriticalShutdown = uMsg == WM_ENDSESSION && (lParam & ENDSESSION_CRITICAL) != 0;

        // unload all plugins (paths in panels may point to fixed drives)
        SetDoNotLoadAnyPlugins(TRUE); // for now due to thumbnails
        if (!Plugins.UnloadAll(shutdown ? analysing.HWindow : HWindow))
        {
            SetDoNotLoadAnyPlugins(FALSE);

            if (uMsg == WM_QUERYENDSESSION)
                TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: unable to unload all plugins");

        EXIT_WM_USER_CLOSE_MAINWND:
            if (shutdown)
            {
                GlobalSaveWaitWindow = NULL;
                GlobalSaveWaitWindowProgress = 0;
                EnableWindow(HWindow, TRUE);
                PluginMsgBoxParent = oldPluginMsgBoxParent;
                DestroyWindow(analysing.HWindow);
                SetCursor(hOldCursor);

                // stop the thread that handled registry work during configuration saving...
                RegistryWorkerThread.StopThread();
            }
            if (uMsg == WM_QUERYENDSESSION || uMsg == WM_ENDSESSION)
                MyShutdownBlockReasonDestroy(HWindow);

            if (uMsg != WM_ENDSESSION) // during critical shutdown we don't end stop-refresh (refreshes are sent to the panels)
            {
                EndStopRefresh();
                return 0; // refuse close/shutdown/logoff; any "forced shutdown" will be detected in WM_ENDSESSION
            }
            else
            {
                // wait for disk operations to finish; the drive system might kill our process before that
                while (ProgressDlgArray.RemoveFinishedDlgs() > 0)
                    Sleep(200);
                CriticalShutdown = FALSE; // just to be safe
                return 0;                 // application exit
            }
        }

        // if CShellExecuteWnd windows exist, offer to abort closing or send a bug report and terminate
        char reason[BUG_REPORT_REASON_MAX]; // problem reason + list of windows (multiline)
        strcpy(reason, "Some faulty shell extension has locked our main window.");
        if (EnumCShellExecuteWnd(shutdown ? analysing.HWindow : HWindow,
                                 reason + (int)strlen(reason), BUG_REPORT_REASON_MAX - ((int)strlen(reason) + 1)) > 0)
        {
            // ask whether Salamander should continue or generate a bug report
            if (CriticalShutdown || // during critical shutdown there's no point in asking anything, let the system terminate us quietly
                SalMessageBox(shutdown ? analysing.HWindow : HWindow,
                              LoadStr(IDS_SHELLEXTBREAK3), SALAMANDER_TEXT_VERSION,
                              MSGBOXEX_CONTINUEABORT | MB_ICONINFORMATION) != IDABORT)
            {
                if (uMsg == WM_QUERYENDSESSION)
                    TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: some faulty shell extension has locked our main window");
                goto EXIT_WM_USER_CLOSE_MAINWND; // we should continue
            }

            // and break here
            strcpy(BugReportReasonBreak, reason);
            TaskList.FireEvent(TASKLIST_TODO_BREAK, GetCurrentProcessId());
            // freeze this thread
            while (1)
                Sleep(1000);
        }

        CALL_STACK_MESSAGE1("WM_USER_CLOSE_MAINWND::3");

        // ask the panels whether we can exit
        if (LeftPanel != NULL && RightPanel != NULL)
        {
            BOOL canClose = FALSE;
            BOOL detachFS1, detachFS2;
            if (LeftPanel->PrepareCloseCurrentPath(shutdown ? analysing.HWindow : LeftPanel->HWindow, TRUE, FALSE, detachFS1, FSTRYCLOSE_UNLOADCLOSEFS /* unnecessary - plugins (including FS) already unloaded */))
            {
                if (RightPanel->PrepareCloseCurrentPath(shutdown ? analysing.HWindow : RightPanel->HWindow, TRUE, FALSE, detachFS2, FSTRYCLOSE_UNLOADCLOSEFS /* unnecessary - plugins (including FS) already unloaded */))
                {
                    canClose = TRUE; // close both panels at the same time or none
                    if (RightPanel->UseSystemIcons || RightPanel->UseThumbnails)
                        RightPanel->SleepIconCacheThread();
                    RightPanel->CloseCurrentPath(shutdown ? analysing.HWindow : RightPanel->HWindow, FALSE, detachFS2, FALSE, FALSE, TRUE); // close the right panel

                    // protect the list box from errors caused by redraw requests (we just cut its data)
                    RightPanel->ListBox->SetItemsCount(0, 0, 0, TRUE);
                    RightPanel->SelectedCount = 0;
                    // If WM_USER_UPDATEPANEL is delivered, the panel contents are redrawn
                    // and scroll bars adjusted. The message loop may deliver it when creating
                    // a message box. Otherwise the panel would appear unchanged and the message
                    // would be removed from the queue.
                    PostMessage(RightPanel->HWindow, WM_USER_UPDATEPANEL, 0, 0);
                }
                if (canClose)
                {
                    if (LeftPanel->UseSystemIcons || LeftPanel->UseThumbnails)
                        LeftPanel->SleepIconCacheThread();
                    LeftPanel->CloseCurrentPath(shutdown ? analysing.HWindow : LeftPanel->HWindow, FALSE, detachFS1, FALSE, FALSE, TRUE); // close the left panel

                    // Protect the list box from errors caused by redraw requests (after we just cut the data)
                    LeftPanel->ListBox->SetItemsCount(0, 0, 0, TRUE);
                    LeftPanel->SelectedCount = 0;
                    // If WM_USER_UPDATEPANEL is delivered, the panel contents are redrawn
                    // and scroll bars adjusted. The message loop may deliver it when creating
                    // a message box. Otherwise the panel would appear unchanged and the message
                    // would be removed from the queue.
                    PostMessage(LeftPanel->HWindow, WM_USER_UPDATEPANEL, 0, 0);
                }
                else
                    LeftPanel->CloseCurrentPath(shutdown ? analysing.HWindow : LeftPanel->HWindow, TRUE, detachFS1, FALSE, FALSE, TRUE); // cancel closing the left panel
            }
            if (!canClose)
            {
                SetDoNotLoadAnyPlugins(FALSE);
                if (uMsg == WM_QUERYENDSESSION)
                    TRACE_I("WM_QUERYENDSESSION: cancelling shutdown: unable to close paths in panels");
                goto EXIT_WM_USER_CLOSE_MAINWND; // panels cannot be closed
            }
        }

        CALL_STACK_MESSAGE1("WM_USER_CLOSE_MAINWND::4");

        // !!! WARNING: from this point (until DestroyWindow) no interruption must occur,
        // if the window opens up, the user would find both panels empty (listing released).
        // This is already violated during Shutdown / Log Off / Restart because we must distribute
        // messages, otherwise we are considered "not responding" and the system kills us prematurely.

        if (StrICmp(Configuration.SLGName, Configuration.LoadedSLGName) != 0) // if the user changed Salamander's language
        {
            Plugins.ClearLastSLGNames(); // so that a new fallback language will be selected for all plugins if needed
            Configuration.UseAsAltSLGInOtherPlugins = FALSE;
            Configuration.AltPluginSLGName[0] = 0;
        }

        // Commit any state changed during shutdown as a final safeguard; normal changes save earlier.
        SaveConfig();

        if (uMsg == WM_ENDSESSION)
            LoadSaveToRegistryMutex.Leave(); // pairs with Enter() called when WM_QUERYENDSESSION was received

        if (shutdown)
        {
            GlobalSaveWaitWindow = NULL;
            GlobalSaveWaitWindowProgress = 0;
            EnableWindow(HWindow, TRUE);
            PluginMsgBoxParent = oldPluginMsgBoxParent;
            DestroyWindow(analysing.HWindow);
            SetCursor(hOldCursor);

            // stop the thread that handled registry work during configuration saving...
            RegistryWorkerThread.StopThread();
        }

        CALL_STACK_MESSAGE1("WM_USER_CLOSE_MAINWND::5");

        DiskCache.PrepareForShutdown(); // clean any empty tmp directories from disk

        //      if (TipOfTheDayDialog != NULL)
        //        DestroyWindow(TipOfTheDayDialog->HWindow);  // the dialog already saved its data (transfer happens there at runtime)

        MainWindowCS.SetClosed();

        CanDestroyMainWindow = TRUE; // it's now safe to call DestroyWindow on MainWindow

        DestroyWindow(HWindow);

        // WM_QUERYENDSESSION and WM_ENDSESSION: all Windows versions kill the process as soon as
        // the main window is destroyed during shutdown, so the following code is dead code in that case

        CriticalShutdown = FALSE; // just to be safe

        if (uMsg == WM_QUERYENDSESSION)
        {
            TRACE_I("WM_QUERYENDSESSION: allowing shutdown...");
            // main window already closed - nobody to deliver WM_ENDSESSION to, neither WaitInEndSession
            // and SaveCfgInEndSession needs to be set
            return TRUE; // if it gets this far, allow the shutdown
        }
        return 0; // return value for WM_USER_CLOSE_MAINWND, WM_USER_FORCECLOSE_MAINWND and WM_ENDSESSION
    }
    }
    return 0;
}
