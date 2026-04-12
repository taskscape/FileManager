// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "svg.h"
#include "gui.h"
#include "toolbar.h"
#include "menu.h"
#include "tooltip.h"
#include <uxtheme.h>
#include <vssym32.h>

#include "nanosvg\nanosvg.h"
#include "nanosvg\nanosvgrast.h"

#include "mainwnd.h"

#include "gui_bitmap.h"

//****************************************************************************
//
// CProgressBar
//

CProgressBar::CProgressBar(HWND hDlg, int ctrlID)
    : CWindow(hDlg, ctrlID, ooAllocated)
{
    if (HWindow != NULL)
    {
        RECT r;
        GetClientRect(HWindow, &r);
        Width = r.right - r.left;
        Height = r.bottom - r.top;
    }
    else
    {
        Width = 10;
        Height = 10;
    }

    Progress = 0;
    SelfMoveTime = 0xFFFFFFFF; // after calling SetProgress(-1) the rectangle will move indefinitely
    SelfMoveTicks = 0;
    SelfMoveSpeed = 50; // 20 moves per second
    TimerIsRunning = FALSE;
    Bitmap = new CBitmap();
    if (Bitmap != NULL)
    {
        HDC hDC = HANDLES(GetDC(NULL));
        if (!Bitmap->CreateBmp(hDC, Width, Height))
        {
            delete Bitmap;
            Bitmap = NULL;
        }
        HANDLES(ReleaseDC(NULL, hDC));
    }
    Text = NULL;

    // get the default font from the dialog
    HFont = (HFONT)SendMessage(hDlg, WM_GETFONT, 0, 0);
    if (HFont == NULL)
        HFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT); // it uses the system font, so obtain it from the system
}

CProgressBar::~CProgressBar()
{
    Stop();
    if (Bitmap != NULL)
        delete (Bitmap);
    if (Text != NULL)
        free(Text);
}

void CProgressBar::SetProgress(DWORD progress, const char* text)
{
    // use SendMessage instead of a direct call to cross the thread boundary
    SendMessage(HWindow, WM_USER_SETPROGRESS, progress, (LPARAM)text);
}

void CProgressBar::SetProgress2(const CQuadWord& progressCurrent, const CQuadWord& progressTotal, const char* text)
{
    // it can happen that progressTotal is 1 and progressCurrent is a large number,
    // making the calculation meaningless (RTC also fails), so we must explicitly set 0% or 100% (value 1000)
    SetProgress(progressCurrent >= progressTotal ? (progressTotal.Value == 0 ? 0 : 1000) : (DWORD)((progressCurrent * CQuadWord(1000, 0)) / progressTotal).Value,
                text);
}

void CProgressBar::SetSelfMoveTime(DWORD time)
{
    SelfMoveTime = time;
}

void CProgressBar::SetSelfMoveSpeed(DWORD moveTime)
{
    SelfMoveSpeed = moveTime;
    if (TimerIsRunning)
    {
        KillTimer(HWindow, IDT_PROGRESSSELFMOVE);
        SetTimer(HWindow, IDT_PROGRESSSELFMOVE, SelfMoveSpeed, NULL);
    }
}

void CProgressBar::Stop()
{
    if (TimerIsRunning)
    {
        KillTimer(HWindow, IDT_PROGRESSSELFMOVE);
        TimerIsRunning = FALSE;
    }
}

void CProgressBar::Paint(HDC hDC)
{
    BOOL releaseDC = FALSE;
    if (hDC == NULL)
    {
        hDC = HANDLES(GetDC(HWindow));
        releaseDC = TRUE;
    }

    // if we have a bitmap, use the cache
    HDC hMemDC = NULL;
    if (Bitmap != NULL && Progress != -1) // caching Progress==-1 is pointless, there's nothing to flicker
        hMemDC = Bitmap->HMemDC;
    else
        hMemDC = hDC;

    if (Progress == -1)
    {
        // indeterminate mode: white, blue rectangle, white
        RECT r;
        r.top = 1;
        r.bottom = Height - 1;

        int mid = BarX + 1;
        int midW = Height * 2;

        SelectObject(hMemDC, HFont);

        COLORREF oldBkColor = SetBkColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_BK_NORMAL]));

        r.left = 1;
        r.right = mid - midW;
        if (r.left < 1)
            r.left = 1;
        if (r.left > Width - 1)
            r.left = Width - 1;
        if (r.right < 1)
            r.right = 1;
        if (r.right > Width - 1)
            r.right = Width - 1;
        ExtTextOut(hMemDC, 0, 0, ETO_OPAQUE, &r, "", 0, NULL);

        SetBkColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_BK_SELECTED]));
        r.left = 1 + mid - midW;
        r.right = mid + midW;
        if (r.left < 1)
            r.left = 1;
        if (r.left > Width - 1)
            r.left = Width - 1;
        if (r.right < 1)
            r.right = 1;
        if (r.right > Width - 1)
            r.right = Width - 1;
        ExtTextOut(hMemDC, 0, 0, ETO_OPAQUE, &r, "", 0, NULL);

        SetBkColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_BK_NORMAL]));
        r.left = 1 + mid + midW;
        r.right = Width - 1;
        if (r.left < 1)
            r.left = 1;
        if (r.left > Width - 1)
            r.left = Width - 1;
        if (r.right < 1)
            r.right = 1;
        if (r.right > Width - 1)
            r.right = Width - 1;
        ExtTextOut(hMemDC, 0, 0, ETO_OPAQUE, &r, "", 0, NULL);

        SetBkColor(hMemDC, oldBkColor);
    }
    else
    {
        // prepare and measure the string
        char buff[50];

        char* progress;
        int progressLen;

        if (Text != NULL)
        {
            progress = Text;
            progressLen = (int)strlen(progress);
        }
        else
        {
            progress = buff;
            progressLen = sprintf(progress, "%d %%", (int)((Progress /*+ 5*/) / 10)); // we do not round the progress, beacause otherwise 100% is visible from 99.5%-100%, which annoys some users (notable with FTP, where it can last half a minute)
        }

        SIZE sz;
        GetTextExtentPoint32(hMemDC, progress, progressLen, &sz);

        // text position -- centered in both axes
        int x = (Width - sz.cx) / 2;
        int y = (Height - sz.cy) / 2;

        // left part of the progress (SELECTED)
        RECT r;
        r.left = 1;
        r.right = 1 + (Width - 2) * Progress / 1000;
        r.top = 1;
        r.bottom = Height - 1;

        SelectObject(hMemDC, HFont);

        COLORREF oldTextColor = SetTextColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_FG_SELECTED]));
        COLORREF oldBkColor = SetBkColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_BK_SELECTED]));
        ExtTextOut(hMemDC, x, y, ETO_OPAQUE | ETO_CLIPPED, &r, progress, progressLen, NULL);

        // right part of the progress (NORMAL)
        r.left = r.right;
        r.right = Width - 1;

        SetTextColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_FG_NORMAL]));
        SetBkColor(hMemDC, GetCOLORREF(CurrentColors[PROGRESS_BK_NORMAL]));
        ExtTextOut(hMemDC, x, y, ETO_OPAQUE | ETO_CLIPPED, &r, progress, progressLen, NULL);
        SetTextColor(hMemDC, oldTextColor);
        SetBkColor(hMemDC, oldBkColor);
    }

    HPEN hOldPen = (HPEN)SelectObject(hMemDC, BtnShadowPen);
    MoveToEx(hMemDC, 0, 0, NULL);
    LineTo(hMemDC, Width - 1, 0);
    LineTo(hMemDC, Width - 1, Height - 1);
    LineTo(hMemDC, 0, Height - 1);
    LineTo(hMemDC, 0, 0);
    SelectObject(hMemDC, hOldPen);

    // if drawing through the cache, copy it to the screen
    if (Bitmap != NULL && hMemDC != hDC)
        BitBlt(hDC, 0, 0, Width, Height, hMemDC, 0, 0, SRCCOPY);

    if (releaseDC)
        HANDLES(ReleaseDC(HWindow, hDC));
}

void CProgressBar::MoveBar()
{
    if (Progress != -1)
    {
        // start the movement
        Progress = -1;
        BarX = 0;
        MoveBarRight = TRUE;
    }
    else
    {
        if (MoveBarRight)
        {
            BarX += 4;
            if (BarX > Width - 2)
            {
                BarX = Width - 2;
                MoveBarRight = !MoveBarRight;
            }
        }
        else
        {
            BarX -= 4;
            if (BarX < 0)
            {
                BarX = 0;
                MoveBarRight = !MoveBarRight;
            }
        }
    }
}

LRESULT
CProgressBar::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CProgressBar::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_TIMER:
    {
        if (wParam == IDT_PROGRESSSELFMOVE)
        {
            if ((SelfMoveTime != 0xFFFFFFFF) && (GetTickCount() - SelfMoveTicks > SelfMoveTime))
            {
                KillTimer(HWindow, IDT_PROGRESSSELFMOVE);
                TimerIsRunning = FALSE;
            }
            else
            {
                MoveBar();
                Paint(NULL);
            }
            return 0;
        }
        break;
    }

    case WM_USER_SETPROGRESS:
    {
        DWORD progress = (DWORD)wParam;
        const char* text = (const char*)lParam;

        BOOL paint = TRUE;
        BOOL textChanged = FALSE;

        if ((text != NULL || Text != NULL) &&
            (text == NULL || Text == NULL || strcmp(text, Text) != 0))
        {
            textChanged = TRUE;
            if (Text != NULL)
            {
                free(Text);
                Text = NULL;
            }
            if (text != NULL)
            {
                Text = DupStr(text);
                if (Text == NULL)
                    TRACE_E(LOW_MEMORY);
            }
        }

        if (progress == (DWORD)-1)
        {
            if (SelfMoveTime > 0)
            {
                SelfMoveTicks = GetTickCount();
                if (!TimerIsRunning)
                {
                    SetTimer(HWindow, IDT_PROGRESSSELFMOVE, SelfMoveSpeed, NULL);
                    TimerIsRunning = TRUE;
                    MoveBar();
                }
                else
                    paint = FALSE; // the change will occur on the timer, no reason to redraw now
            }
            else
                MoveBar();
        }
        else
        {
            if (TimerIsRunning)
                Stop();
            if (progress > 1000)
                progress = 1000; // max. 100% (a copy of the "active" file may report progress >100%)
            if (progress != Progress)
                Progress = progress;
            else
                paint = textChanged; // progress didn't change; if the text didn't change either, redrawing is skipped
                                     /*
        BOOL redraw = Progress == 0 ||                     // always show 0% 
                      Progress == 1000 ||                  // always show 100%
                      Progress - DisplayedProgress >= 100; // always show a change greater than 10%
        if (redraw && Progress != DisplayedProgress)
          Paint(NULL);
        */
        }
        if (paint)
            Paint(NULL);
        return 0;
    }

    case WM_SIZE:
    {
        Width = LOWORD(lParam);
        Height = HIWORD(lParam);
        Bitmap->Enlarge(Width, Height);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hDC = HANDLES(BeginPaint(HWindow, &ps));
        Paint(hDC);
        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}
