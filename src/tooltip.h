// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//
// CToolTip
//
// This tooltip is intended to remove the basic disadvantage of the original tooltip design.
// Each window had its own tooltip object. Another disadvantage was
// that the list of areas over which tooltips should open had to be passed
// to this object.
//
// New design: CMainWindow will own only one tooltip (object instance).
// The tooltip window is created only when needed, in the thread that
// requested it to be shown. Reason: we need the tooltip window to run in this thread;
// up to and including 2.6b6 the tooltip window ran in Salamander's main thread, and if
// that thread was blocked, tooltips were not displayed.
// When the mouse moves over a control that uses this tooltip, the control
// will call SetCurrentID when entering a new area.
//
// The interface for working with the tooltip will be in const.h so it is available to all
// controls without needing to include mainwnd.h and tooltip.h.
//

// Used messages:
// WM_USER_TTGETTEXT - queries text with a specific ID
//   wParam = ID passed during SetCurrentToolTip
//   lParam = buffer (points into the tooltip buffer); maximum character count is TOOLTIP_TEXT_MAX
//            before calling this message, a terminator is placed at character zero
//            text may contain \n to move to a new line and \t to insert a tab
// if the window writes a null-terminated string to the buffer, it will be displayed in the tooltip
// otherwise the tooltip will not be displayed
//

class CToolTip : public CWindow
{
    enum TipTimerModeEnum
    {
        ttmNone,         // no timer is running
        ttmWaitingOpen,  // waiting for the tooltip to open
        ttmWaitingClose, // waiting for the tooltip to close
        ttmWaitingKill,  // waiting to leave display mode
    };

protected:
    char Text[TOOLTIP_TEXT_MAX];
    int TextLen;
    HWND HNotifyWindow;
    DWORD LastID;
    TipTimerModeEnum WaitingMode;
    DWORD HideCounter;
    DWORD HideCounterMax;
    POINT LastCursorPos;
    BOOL IsModal;     // is our message loop currently running?
    BOOL ExitASAP;    // close as soon as possible and stop being modal
    UINT_PTR TimerID; // returned by SetTimer, needed for KillTimer

public:
    CToolTip(CObjectOrigin origin = ooStatic);
    ~CToolTip();

    BOOL RegisterClass();

    // hParent is required so the tooltip also closes when the parent is closed.
    // Without it, the parent thread could end while the tooltip window remained
    // open but could no longer be closed (its thread no longer existed) -> crashes during
    // Salamander shutdown (fortunately this was before release 2.5b7).
    BOOL Create(HWND hParent);

    // This method starts a timer and, if it is not called again before the timer expires,
    // asks window 'hNotifyWindow' for text using the WM_USER_TTGETTEXT message,
    // then displays it under the cursor at its current coordinates.
    // Variable 'id' distinguishes the area when communicating with window 'hNotifyWindow'.
    // If this method is called multiple times with the same 'id' parameter,
    // the additional calls are ignored.
    // Value 0 of parameter 'hNotifyWindow' is reserved for hiding the window and stopping
    // the running timer.
    // Parameter 'showDelay' has meaning if 'hNotifyWindow' != NULL.
    // If it is greater than or equal to 1, it determines how long before the tooltip is shown in [ms].
    // If it equals 0, the default delay is used.
    // If it is -1, the timer is not started at all.
    void SetCurrentToolTip(HWND hNotifyWindow, DWORD id, int showDelay);

    // Suppresses tooltip display at the current mouse coordinates.
    // Useful to call when activating a window that uses tooltips.
    // This prevents unwanted tooltip display.
    void SuppressToolTipOnCurrentMousePos();

    // If the text is displayed successfully, returns TRUE; if no new text is supplied, returns FALSE.
    // If considerCursor==TRUE, measures the cursor and moves the tooltip under it.
    // If modal==TRUE, starts a message loop that watches for tooltip close messages and returns only after it is hidden.
    BOOL Show(int x, int y, BOOL considerCursor, BOOL modal, HWND hParent);

    // Hides the tooltip.
    void Hide();

    void OnTimer();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    BOOL GetText();
    void GetNeededWindowSize(SIZE* sz);

    void MessageLoop(); // for the modal tooltip variant

    void MySetTimer(DWORD elapse);
    void MyKillTimer();

    DWORD GetTime(BOOL init);
};
