// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

// HtmlHelp support: MessageBoxHelpCallback, CSalamanderHelp, OpenHtmlHelp

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

#include <strsafe.h>

//****************************************************************************
//
// HtmlHelp support
//

// universal callback for our MessageBox when the user clicks the HELP button
// should be called, for example, like this:
//    MSGBOXEX_PARAMS params;
//    params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
//    params.ContextHelpId = IDH_LICENSE;
//    params.HelpCallback = MessageBoxHelpCallback;
void CALLBACK MessageBoxHelpCallback(LPHELPINFO helpInfo)
{
    OpenHtmlHelp(NULL, MainWindow->HWindow, HHCDisplayContext, (UINT)helpInfo->dwContextId, FALSE); // MSGBOXEX_PARAMS::ContextHelpId
}

CSalamanderHelp SalamanderHelp;

void CSalamanderHelp::OnHelp(HWND hWindow, UINT helpID, HELPINFO* helpInfo,
                             BOOL ctrlPressed, BOOL shiftPressed)
{
    if (!ctrlPressed && !shiftPressed)
    {
        OpenHtmlHelp(NULL, hWindow, HHCDisplayContext, helpID, FALSE);
    }
}

void CSalamanderHelp::OnContextMenu(HWND hWindow, WORD xPos, WORD yPos)
{
}

typedef struct tagHH_LAST_ERROR
{
    int cbStruct;
    HRESULT hr;
    BSTR description;
} HH_LAST_ERROR;

static BOOL CopyHelpPath(char* destination, size_t destinationCount, const char* source)
{
    // Help locations are filesystem identities; callers must not append to a truncated base path.
    return SUCCEEDED(StringCchCopyA(destination, destinationCount, source));
}

BOOL OpenHtmlHelp(char* helpFileName, HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet)
{
    //  SalMessageBox(parent, "This beta version doesn't contain help.\nPlease wait for the next beta version.",
    //                "Open Salamander Help", MB_OK | MB_ICONINFORMATION);

    HANDLES(EnterCriticalSection(&OpenHtmlHelpCS));

    char helpPath[MAX_PATH + 50];
    if (CurrentHelpDir[0] == 0)
    {
        char helpSubdir[MAX_PATH];
        helpSubdir[0] = 0;
        CLanguage language;
        if (language.Init(Configuration.LoadedSLGName, NULL))
        {
            if (!CopyHelpPath(helpSubdir, _countof(helpSubdir), language.HelpDir))
                helpSubdir[0] = 0;
            language.Free();
        }
        if (helpSubdir[0] == 0)
        {
            TRACE_E("OpenHtmlHelp(): unable to get (or empty) SLGHelpDir!");
            strcpy(helpSubdir, "english");
        }
        BOOL ok = FALSE;
        if (GetModuleFileName(HInstance, CurrentHelpDir, MAX_PATH) != 0 &&
            CutDirectory(CurrentHelpDir) &&
            SalPathAppend(CurrentHelpDir, "help", MAX_PATH) &&
            DirExists(CurrentHelpDir))
        {
            if (!CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) ||
                !SalPathAppend(helpPath, helpSubdir, MAX_PATH) ||
                !DirExists(helpPath))
            { // the directory from the current .slg file does not exist
                if (!CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) ||
                    _stricmp(helpSubdir, "english") == 0 || // we already tested "english" and it does not exist so no point in trying again
                    !SalPathAppend(helpPath, "english", MAX_PATH) ||
                    !DirExists(helpPath))
                { // the ENGLISH directory does not exist
                    if (CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) &&
                        SalPathAppend(helpPath, "*", MAX_PATH))
                    { // try to find at least some other directory
                        WIN32_FIND_DATAW dataW;
                        WIN32_FIND_DATA data;
                        CStrP helpPathW(ConvertAllocUtf8ToWide(helpPath, -1));
                        HANDLE find = helpPathW != NULL ? HANDLES_Q(FindFirstFileW(helpPathW, &dataW)) : INVALID_HANDLE_VALUE;
                        if (find != INVALID_HANDLE_VALUE)
                        {
                            do
                            {
                                ConvertFindDataWToUtf8(dataW, &data);
                                if (strcmp(data.cFileName, ".") != 0 && strcmp(data.cFileName, "..") != 0 &&
                                    (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) // only if it is a directory
                                {
                                    if (CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) &&
                                        SalPathAppend(helpPath, data.cFileName, MAX_PATH))
                                    {
                                        ok = TRUE;
                                        break;
                                    }
                                }
                            } while (FindNextFileW(find, &dataW));
                            HANDLES(FindClose(find));
                        }
                    }
                }
                else
                    ok = TRUE;
            }
            else
                ok = TRUE;
            if (ok)
                ok = CopyHelpPath(CurrentHelpDir, _countof(CurrentHelpDir), helpPath);
        }
        if (!ok)
        {
            CurrentHelpDir[0] = 0;

            HANDLES(LeaveCriticalSection(&OpenHtmlHelpCS));

            if (!quiet)
            {
                SalMessageBox(parent, LoadStr(IDS_FAILED_TO_FIND_HELP),
                              LoadStr(IDS_HELPERROR), MB_OK | MB_ICONEXCLAMATION);
            }
            return FALSE;
        }
    }

    HANDLES(LeaveCriticalSection(&OpenHtmlHelpCS));

    HH_FTS_QUERY query;
    DWORD uCommand = 0;
    switch (command)
    {
    case HHCDisplayTOC:
    {
        uCommand = HH_DISPLAY_TOC;
        break;
    }

    case HHCDisplayIndex:
    {
        uCommand = HH_DISPLAY_INDEX;
        if (dwData == 0)
            dwData = 0;
        break;
    }

    case HHCDisplaySearch:
    {
        uCommand = HH_DISPLAY_SEARCH;
        if (dwData == 0)
        {
            ZeroMemory(&query, sizeof(query));
            query.cbStruct = sizeof(query);
            dwData = (DWORD_PTR)&query;
        }
        break;
    }

    case HHCDisplayContext:
    {
        uCommand = HH_HELP_CONTEXT;
        break;
    }

    default:
    {
        TRACE_E("OpenHtmlHelp(): unknown command = " << command);
        return FALSE;
    }
    }

    if (helpFileName != NULL) // plugin help: to open the window in the right position
    {                         // with remembered Favorites, we must open "salamand.chm" first (then
                              // the plugin help opens in this same window)
        if (CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) &&
            SalPathAppend(helpPath, "salamand.chm", MAX_PATH) &&
            FileExists(helpPath))
        {
            HtmlHelp(NULL, helpPath, HH_DISPLAY_TOC, 0); // ignore potential error
        }
    }

    BOOL ret = FALSE;

    if (CopyHelpPath(helpPath, _countof(helpPath), CurrentHelpDir) &&
        SalPathAppend(helpPath, helpFileName == NULL ? "salamand.chm" : helpFileName, MAX_PATH) &&
        FileExists(helpPath))
    {
        if (HtmlHelp(NULL, helpPath, uCommand, dwData) == NULL)
        {
            BOOL errorHandled = FALSE;
            HH_LAST_ERROR lasterror;
            lasterror.cbStruct = sizeof(lasterror);
            if (HtmlHelp(NULL, NULL, HH_GET_LAST_ERROR, (DWORD_PTR)&lasterror) != NULL)
            {
                // Only report an error if we found one:
                if (FAILED(lasterror.hr))
                {
                    // Is there a text message to display...
                    if (lasterror.description)
                    {
                        if (!quiet)
                        {
                            char buff[5000];
                            // Convert the String to ANSI
                            WideCharToMultiByte(CP_ACP, 0, lasterror.description, -1, buff, 5000, NULL, NULL);
                            buff[5000 - 1] = 0;
                            SysFreeString(lasterror.description);

                            // Display
                            SalMessageBox(parent, buff, LoadStr(IDS_HELPERROR), MB_OK);
                        }
                        errorHandled = TRUE;
                    }
                }
            }
            if (!errorHandled && !quiet)
            {
                SalMessageBox(parent, LoadStr(IDS_FAILED_TO_LAUNCH_HELP),
                              LoadStr(IDS_HELPERROR), MB_OK | MB_ICONEXCLAMATION);
            }
        }
        else
        {
            ret = TRUE;
        }
    }
    else
    {
        if (!quiet)
        {
            SalMessageBox(parent, LoadStr(IDS_FAILED_TO_FIND_HELP),
                          LoadStr(IDS_HELPERROR), MB_OK | MB_ICONEXCLAMATION);
        }
    }
    return ret;
}


// Context-help mode extracted from mainwnd_commands.cpp as a mechanical move;
// it belongs with the other help support in this file.
// CMainWindow methods declared in mainwnd.h.

// useful message ranges (moved here with their only user, ProcessHelpMsg)
#define WM_SYSKEYFIRST WM_SYSKEYDOWN
#define WM_SYSKEYLAST WM_SYSDEADCHAR

#define WM_NCMOUSEFIRST WM_NCMOUSEMOVE
#define WM_NCMOUSELAST WM_NCMBUTTONDBLCLK

// hit-testing helper defined in mainwnd_commands.cpp
BOOL IsDescendant(HWND hWndParent, HWND hWndChild);
BOOL CMainWindow::CanEnterHelpMode()
{
    CALL_STACK_MESSAGE1("CMainWindow::CanEnterHelpMode()");
    if (HelpMode == HELP_ACTIVE) // already in help mode?
        return FALSE;

    if (HHelpCursor == NULL)
    {
        HHelpCursor = LoadCursor(NULL, IDC_HELP);
        if (HHelpCursor == NULL)
            return FALSE;
    }

    return TRUE;
}

void CMainWindow::OnContextHelp()
{
    CALL_STACK_MESSAGE1("CMainWindow::OnContextHelp()");
    // don't enter twice, and don't enter if initialization fails
    if (HelpMode == HELP_ACTIVE || !CanEnterHelpMode())
        return;

    // don't enter help mode with pending WM_USER_EXITHELPMODE message
    MSG msg;
    if (PeekMessage(&msg, HWindow, WM_USER_EXITHELPMODE, WM_USER_EXITHELPMODE, PM_REMOVE | PM_NOYIELD))
        return;

    BOOL bHelpMode = HelpMode;
    if (HelpMode != HELP_INACTIVE && HelpMode != HELP_ENTERING)
        return;
    HelpMode = HELP_ACTIVE;

    if (bHelpMode == HELP_INACTIVE)
    {
        // need to delay help startup until later
        PostMessage(HWindow, WM_COMMAND, CM_HELP_CONTEXT, 0);
        HelpMode = HELP_ENTERING;
        return;
    }

    IdleRefreshStates = TRUE; // trigger idle update
    OnEnterIdle();            // redraw the toolbar

    if (HelpMode != HELP_ACTIVE)
        return;

    MenuBar->SetHelpMode(TRUE);

    // reset the bottom toolbar to its normal state
    BottomToolBar->SetState(btbsNormal);
    BottomToolBar->UpdateItemsState();

    // if someone is monitoring the mouse, stop monitoring
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_QUERY;
    if (TrackMouseEvent(&tme) && tme.hwndTrack != NULL)
        SendMessage(tme.hwndTrack, WM_MOUSELEAVE, 0, 0);

    DWORD dwContext = 0;
    POINT point;

    GetCursorPos(&point);
    SetHelpCapture(point, NULL);
    LONG lIdleCount = 0;

    BOOL first = TRUE;

    HWND hDirtyWindow = NULL;
    while (HelpMode)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
        {
            if (!ProcessHelpMsg(msg, &dwContext, &hDirtyWindow))
                break;
            if (dwContext != 0)
                return;
        }
        else
        {
            if (first)
            {
                // buffer a mouse move to fall through to the toolbar when the cursor is over disabled buttons
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(HWindow, &p);
                PostMessage(HWindow, WM_MOUSEMOVE, 0, MAKELPARAM(point.x, point.y));
                // originally this buffering was before the loop, but that misbehaved
                // if a tooltip for a disabled button was shown and I pressed Shift+F1:
                // when the tooltip vanished, WM_MOUSELEAVE was delivered (PeekMessage distributes messages, see MSDN).
                // The button under the cursor then drew as enabled and then immediately as disabled again.
                // With this trick I wait until all messages are processed and MOUSEMOVE is definitely handled

                first = FALSE;
            }
            else
            {
                WaitMessage();
            }
        }
    }
    if (hDirtyWindow != NULL)
        SendMessage(hDirtyWindow, WM_USER_HELP_MOUSELEAVE, 0, 0);

    MenuBar->SetHelpMode(FALSE);
    HelpMode = HELP_INACTIVE;
    ReleaseCapture();

    // make sure the cursor is set appropriately
    SetCapture(HWindow);
    ReleaseCapture();

    if (dwContext != 0)
        OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, dwContext, FALSE);

    IdleRefreshStates = TRUE; // trigger idle update
}

HWND CMainWindow::SetHelpCapture(POINT point, BOOL* pbDescendant)
// set or release capture, depending on where the mouse is
// also assign the proper cursor to be displayed.
{
    CALL_STACK_MESSAGE1("CMainWindow::SetHelpCapture(,)");
    if (!HelpMode)
        return NULL;

    HWND hWndCapture = GetCapture();
    HWND hWndHit = WindowFromPoint(point);
    HWND hTopHit = GetTopLevelParent(hWndHit);
    HWND hTopActive = GetTopLevelParent(GetActiveWindow());
    BOOL bDescendant = FALSE;
    DWORD hCurTask = GetCurrentThreadId();
    DWORD hTaskHit = hWndHit != NULL ? GetWindowThreadProcessId(hWndHit, NULL) : NULL;

    if (hTopActive == NULL || hWndHit == GetDesktopWindow())
    {
        if (hWndCapture == HWindow)
            ReleaseCapture();
        SetCursor(HHelpCursor);
    }
    else if (hTopActive == NULL ||
             hWndHit == NULL || hCurTask != hTaskHit ||
             !IsDescendant(HWindow, hWndHit))
    {
        if (hCurTask != hTaskHit)
            hWndHit = NULL;
        if (hWndCapture == HWindow)
            ReleaseCapture();
    }
    else
    {
        bDescendant = TRUE;
        if (hTopActive != hTopHit)
            hWndHit = NULL;
        else
        {
            if (hWndCapture != HWindow)
                SetCapture(HWindow);
            SetCursor(HHelpCursor);
        }
    }
    if (pbDescendant != NULL)
        *pbDescendant = bDescendant;
    return hWndHit;
}

BOOL CMainWindow::ProcessHelpMsg(MSG& msg, DWORD* pContext, HWND* hDirtyWindow)
{
    CALL_STACK_MESSAGE4("CMainWindow::ProcessHelpMsg(0x%X, 0x%IX, 0x%IX,)", msg.message, msg.wParam, msg.lParam);

    if (pContext == NULL)
    {
        TRACE_E("pContext == NULL");
        return FALSE;
    }
    if (msg.message == WM_USER_EXITHELPMODE ||
        (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE))
    {
        PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);
        return FALSE;
    }

    POINT point;
    if ((msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) ||
        (msg.message >= WM_NCMOUSEFIRST && msg.message <= WM_NCMOUSELAST))
    {
        BOOL bDescendant;
        HWND hWndHit = SetHelpCapture(msg.pt, &bDescendant);
        if (hWndHit == NULL)
        {
            PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE); // eat the message
            return TRUE;
        }

        if (bDescendant)
        {
            if (msg.message != WM_LBUTTONDOWN)
            {
                // Hit one of our owned windows -- eat the message.
                PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);

                // notify windows that wish to highlight items during Shift+F1 mode
                if (msg.message == WM_MOUSEMOVE)
                {
                    if (*hDirtyWindow != NULL && *hDirtyWindow != hWndHit)
                        SendMessage(*hDirtyWindow, WM_USER_HELP_MOUSELEAVE, 0, 0);
                    *hDirtyWindow = hWndHit; // this window will need to receive a LEAVE message

                    POINT p = msg.pt;
                    ScreenToClient(hWndHit, &p);
                    SendMessage(hWndHit, WM_USER_HELP_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
                }
                return TRUE;
            }
            int iHit = (int)SendMessage(hWndHit, WM_NCHITTEST, 0,
                                        MAKELONG(msg.pt.x, msg.pt.y));
            if (iHit == HTSYSMENU)
            {
                if (GetCapture() != HWindow)
                {
                    TRACE_E("GetCapture() != HWindow");
                    return FALSE;
                }
                ReleaseCapture();
                // the message we peeked changes into a non-client because
                // of the release capture.
                GetMessage(&msg, NULL, WM_NCLBUTTONDOWN, WM_NCLBUTTONDOWN);
                DispatchMessage(&msg);
                GetCursorPos(&point);
                SetHelpCapture(point, NULL);
            }
            else if (iHit == HTCLIENT)
            {
                if (hWndHit == MenuBar->HWindow)
                {
                    PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                    ReleaseCapture();
                    POINT p = msg.pt;
                    ScreenToClient(hWndHit, &p);
                    msg.lParam = MAKELPARAM(p.x, p.y);
                    msg.hwnd = hWndHit;
                    msg.message = WM_MOUSEMOVE;
                    DispatchMessage(&msg);
                    msg.message = WM_LBUTTONDOWN;
                    DispatchMessage(&msg);
                    GetCursorPos(&point);
                    SetHelpCapture(point, NULL);
                }
                else
                {
                    *pContext = MapClientArea(msg.pt);
                    PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                    return FALSE;
                }
            }
            else
            {
                *pContext = MapNonClientArea(iHit);
                PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);
                return FALSE;
            }
        }
        else
        {
            // Hit one of our apps windows (or desktop) -- dispatch the message.
            PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE);

            // Dispatch mouse messages that hit the desktop!
            DispatchMessage(&msg);
        }
    }
    else if (msg.message == WM_SYSCOMMAND ||
             (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST))
    {
        if (GetCapture() != NULL)
        {
            ReleaseCapture();
            MSG msg2;
            while (PeekMessage(&msg2, NULL, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE | PM_NOYIELD))
                ;
        }
        if (PeekMessage(&msg, NULL, msg.message, msg.message, PM_NOREMOVE))
        {
            GetMessage(&msg, NULL, msg.message, msg.message);

            // ensure sending messages to our menu (avoiding the need for a keyboard hook)
            // this supports entering the menu via Alt/F10/Alt+letter during help mode
            if (MenuBar == NULL || !MenuBar->IsMenuBarMessage(&msg))
            {
                TranslateMessage(&msg);
                if (msg.message == WM_SYSCOMMAND ||
                    (msg.message >= WM_SYSKEYFIRST &&
                     msg.message <= WM_SYSKEYLAST))
                {
                    // only dispatch system keys and system commands
                    DispatchMessage(&msg);
                }
            }
        }
        GetCursorPos(&point);
        SetHelpCapture(point, NULL);
    }
    else
    {
        // allow all other messages to go through (capture still set)
        if (PeekMessage(&msg, NULL, msg.message, msg.message, PM_REMOVE))
            DispatchMessage(&msg);
    }

    return TRUE;
}

void CMainWindow::ExitHelpMode()
{
    CALL_STACK_MESSAGE1("CMainWindow::ExitHelpMode()");
    // if not in help mode currently, this is a no-op
    if (!HelpMode)
        return;

    // only post new WM_EXITHELPMODE message if one doesn't already exist
    //  in the queue.
    MSG msg;
    if (!PeekMessage(&msg, HWindow, WM_USER_EXITHELPMODE, WM_USER_EXITHELPMODE, PM_REMOVE | PM_NOYIELD))
        PostMessage(HWindow, WM_USER_EXITHELPMODE, 0, 0);

    // release capture if this window has it
    if (GetCapture() == HWindow)
        ReleaseCapture();

    HelpMode = HELP_INACTIVE;
    IdleRefreshStates = TRUE; // trigger idle update
}
