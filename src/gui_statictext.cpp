// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "common/unicode_text_layout.h" // measure and clip decoded Unicode, never UTF-8 bytes

#include <strsafe.h>

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

//****************************************************************************
//
// CStaticText
//

CStaticText::CStaticText(HWND hDlg, int ctrlID, DWORD flags)
    : CWindow(hDlg, ctrlID, ooAllocated
#ifndef _UNICODE
              , TRUE // Native text messages must retain UTF-16 instead of passing through an ANSI subclass thunk.
#endif
              )
{
    if ((flags & STF_HANDLEPREFIX) && ((flags & STF_END_ELLIPSIS) || (flags & STF_PATH_ELLIPSIS)))
    {
        TRACE_E("Flag STF_HANDLEPREFIX cannot be used with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS.");
        flags &= ~STF_HANDLEPREFIX;
    }

    Flags = flags;
    Text = NULL;
    TextLen = 0;
    TextW = NULL;
    TextLenW = 0;
    Text2 = NULL;
    Text2Len = 0;
    Text2W = NULL;
    Text2LenW = 0;
    Bitmap = NULL;
    HFont = NULL;
    DestroyFont = FALSE;
    ClipDraw = FALSE;
    Text2Draw = FALSE;
    Alignment = 0; // left
    PathSeparator = '\\';
    MouseIsTracked = FALSE;
    ToolTipText = NULL;
    HToolTipNW = NULL;
    ToolTipID = 0;
    HintMode = FALSE;

    if (HWindow == NULL)
        return; // avoid flickering the screen

    UIState = (WORD)SendMessage(HWindow, WM_QUERYUISTATE, 0, 0);

    // get the alignment
    DWORD style = (DWORD)GetWindowLongPtr(HWindow, GWL_STYLE);
    if (style & SS_RIGHT)
        Alignment = 2;
    else if (style & SS_CENTER)
        Alignment = 1;

    // measure the maximum size of the static
    RECT r;
    GetClientRect(HWindow, &r);
    Width = r.right - r.left;
    Height = r.bottom - r.top;

    // if we should draw via cache, we create a bitmap
    if (Flags & STF_CACHED_PAINT)
    {
        Bitmap = new CBitmap(); // if allocation fails, paint won't be cached
        if (Bitmap != NULL)
        {
            HDC hDC = HANDLES(GetDC(HWindow));
            if (!Bitmap->CreateBmp(hDC, Width, Height))
            {
                delete Bitmap;
                Bitmap = NULL;
            }
            HANDLES(ReleaseDC(HWindow, hDC));
        }
    }

    // obtain the default font from the static
    HFont = (HFONT)SendMessage(HWindow, WM_GETFONT, 0, 0);
    if ((Flags & STF_BOLD) || (Flags & STF_UNDERLINE))
    {
        // if the text is BOLD or UNDERLINE, prepare our own font
        LOGFONT lf;
        GetObject(HFont, sizeof(lf), &lf);
        if (Flags & STF_BOLD)
            lf.lfWeight = FW_BOLD;
        if (Flags & STF_UNDERLINE)
            lf.lfUnderline = TRUE;
        HFont = HANDLES(CreateFontIndirect(&lf));
        DestroyFont = TRUE;
    }

    // obtain the initial text of the static
    // Read the original resource caption through the Unicode procedure before replacing its text storage.
    int captionLength = (int)CWindow::WindowProc(WM_GETTEXTLENGTH, 0, 0);
    if (captionLength > 0)
    {
        try
        {
            std::wstring caption((size_t)captionLength + 1, L'\0');
            int copied = (int)CWindow::WindowProc(WM_GETTEXT, caption.size(), (LPARAM)&caption[0]);
            std::string utf8;
            if (WideTextToUtf8(caption.c_str(), copied, utf8))
                SetText(utf8.c_str());
        }
        catch (const std::bad_alloc&)
        {
            TRACE_E(LOW_MEMORY); // keep an empty control instead of falling back to a corrupted ANSI caption
        }
    }
}

CStaticText::~CStaticText()
{
    if (ToolTipText != NULL)
        free(ToolTipText);
    if (Text != NULL)
        free(Text);
    if (TextW != NULL)
        free(TextW);
    if (Text2 != NULL)
        free(Text2);
    if (Text2W != NULL)
        free(Text2W);
    if (Bitmap != NULL)
        delete Bitmap;
    if (HFont != NULL && DestroyFont)
        HANDLES(DeleteObject(HFont));
}

BOOL CStaticText::SetText(const char* text)
{
    CALL_STACK_MESSAGE2("CStaticText::SetText(%s)", text);
    if (text == NULL)
        text = "";
    if (Text != NULL && strcmp(Text, text) == 0)
        return TRUE;

    // Publish matching UTF-8/UTF-16 buffers together; a failed conversion/allocation must retain the previous label.
    std::wstring wide;
    if (!Utf8TextToWide(text, -1, wide))
        return FALSE;
    size_t bytes = strlen(text) + 1;
    if (bytes > INT_MAX - 3 || wide.size() > INT_MAX - 4)
        return FALSE;
    char* next = (char*)malloc(bytes);
    wchar_t* nextWide = (wchar_t*)malloc((wide.size() + 1) * sizeof(wchar_t));
    BOOL ellipsis = (Flags & (STF_PATH_ELLIPSIS | STF_END_ELLIPSIS)) != 0;
    char* nextShort = ellipsis ? (char*)malloc(bytes + 3) : NULL;
    wchar_t* nextShortWide = ellipsis ? (wchar_t*)malloc((wide.size() + 4) * sizeof(wchar_t)) : NULL;
    if (next == NULL || nextWide == NULL || (ellipsis && (nextShort == NULL || nextShortWide == NULL)))
    {
        free(next);
        free(nextWide);
        free(nextShort);
        free(nextShortWide);
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    memcpy(next, text, bytes);
    memcpy(nextWide, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
    free(Text);
    free(TextW);
    free(Text2);
    free(Text2W);
    Text = next;
    TextW = nextWide;
    Text2 = nextShort;
    Text2W = nextShortWide;
    TextLen = (int)bytes - 1;
    TextLenW = (int)wide.size();
    Text2Len = Text2LenW = 0;
    PrepareForPaint();
    InvalidateRect(HWindow, NULL, FALSE);
    UpdateWindow(HWindow);
    return TRUE;
}

BOOL CStaticText::SetTextToDblQuotesIfNeeded(const char* text)
{
    CALL_STACK_MESSAGE2("CStaticText::SetTextToDblQuotesIfNeeded(%s)", text);

    if (text != NULL)
    {
        int len = (int)strlen(text);
        if (len > 0 && (text[0] <= ' ' || text[len - 1] <= ' ') && len < 2 * MAX_PATH)
        {
            char buf[2 * MAX_PATH + 2];
            sprintf(buf, "\"%s\"", text); // spaces at the beginning and end will be visible in quotes (otherwise they are invisible)
            return SetText(buf);
        }
    }
    return SetText(text);
}

// Layout and painting share UTF-16 positions; UTF-8 byte offsets must never select an ellipsis boundary.
void CStaticText::PrepareForPaint()
{
    ClipDraw = FALSE;
    Text2Draw = FALSE;
    TextWidth = TextHeight = 0;
    if (TextW == NULL || TextLenW == 0)
        return;
    HDC hDC = HANDLES(GetDC(HWindow));
    if (hDC == NULL)
        return;
    HFONT hOldFont = (HFONT)SelectObject(hDC, HFont);
    SIZE size = {};
    if (Flags & (STF_PATH_ELLIPSIS | STF_END_ELLIPSIS))
    {
        std::wstring original, shortened;
        std::string utf8;
        if (Utf8TextToWide(Text, TextLen, original) &&
            EllipsizeUnicodeText(hDC, original, Width, (Flags & STF_PATH_ELLIPSIS) != 0,
                                (wchar_t)(unsigned char)PathSeparator, shortened, size) &&
            WideTextToUtf8(shortened.c_str(), (int)shortened.size(), utf8))
        {
            // Both representations are derived from the same complete character sequence.
            memcpy(Text2W, shortened.c_str(), (shortened.size() + 1) * sizeof(wchar_t));
            memcpy(Text2, utf8.c_str(), utf8.size() + 1);
            Text2LenW = (int)shortened.size();
            Text2Len = (int)utf8.size();
            Text2Draw = TRUE;
        }
        else
            GetTextExtentPoint32W(hDC, TextW, TextLenW, &size);
        TextWidth = size.cx;
        TextHeight = size.cy;
    }
    else if (Flags & STF_HANDLEPREFIX)
    {
        RECT r = {0, 0, Width, Height};
        DrawTextW(hDC, TextW, TextLenW, &r, DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
        TextWidth = r.right;
        TextHeight = r.bottom;
    }
    else
    {
        GetTextExtentPoint32W(hDC, TextW, TextLenW, &size);
        TextWidth = size.cx + 1;
        TextHeight = size.cy;
    }
    if (TextWidth > Width)
    {
        TextWidth = Width;
        ClipDraw = TRUE;
    }
    if (TextHeight > Height)
    {
        TextHeight = Height;
        ClipDraw = TRUE;
    }
    SelectObject(hDC, hOldFont);
    HANDLES(ReleaseDC(HWindow, hDC));
}

void CStaticText::SetPathSeparator(char separator)
{
    if (separator == 0)
        TRACE_E("CStaticText::SetPathSeparator == 0");
    else
    {
        if (separator != PathSeparator)
        {
            PathSeparator = separator;
            InvalidateRect(HWindow, NULL, FALSE);
            PrepareForPaint();
        }
    }
}

int CStaticText::GetTextXOffset()
{
    int xOffset = 0; // SS_LEFT
    if (Alignment == 1)
        xOffset = (Width - TextWidth) / 2; // SS_CENTER
    else if (Alignment == 2)
        xOffset = Width - TextWidth; // SS_RIGHT
    return xOffset;
}

BOOL CStaticText::TextHitTest(POINT* screenCursorPos)
{
    POINT p = *screenCursorPos;
    ScreenToClient(HWindow, &p);

    int xOffset = GetTextXOffset();

    RECT r;
    r.left = xOffset;
    r.top = 0;
    r.right = xOffset + TextWidth;
    r.bottom = TextHeight;

    return PtInRect(&r, p);
}

BOOL CStaticText::SetToolTipText(const char* text)
{
    if (text != NULL && ToolTipText != NULL && strcmp(ToolTipText, text) == 0)
        return TRUE;

    if (text == NULL)
    {
        if (ToolTipText != NULL)
            free(ToolTipText);
        ToolTipText = NULL;
        HToolTipNW = NULL;
        ToolTipID = 0;
        return TRUE;
    }

    char* newText = DupStr(text);
    if (newText == NULL)
        return FALSE;

    if (ToolTipText != NULL)
        free(ToolTipText);

    ToolTipText = newText;
    HToolTipNW = NULL;
    ToolTipID = 0;

    PostMessage(MainWindow->ToolTip->HWindow, WM_USER_REFRESHTOOLTIP, 0, 0); // ask the window to load the new text and redraw

    return TRUE;
}

void CStaticText::SetToolTip(HWND hNotifyWindow, DWORD id)
{
    if (ToolTipText != NULL)
        free(ToolTipText);
    ToolTipText = NULL;

    HToolTipNW = hNotifyWindow;
    ToolTipID = id;
}

void CStaticText::EnableHintToolTip(BOOL enable)
{
    HintMode = enable;
}

BOOL CStaticText::ToolTipAssigned()
{
    return ToolTipText != NULL || HToolTipNW != NULL;
}

void CStaticText::DrawFocus(HDC hDC)
{
    BOOL releaseDC = FALSE;
    if (hDC == NULL)
    {
        hDC = HANDLES(GetDC(HWindow));
        releaseDC = TRUE;
    }

    int xOffset = GetTextXOffset();

    RECT r;
    r.left = xOffset;
    r.top = 0;
    r.right = xOffset + TextWidth;
    r.bottom = TextHeight;

    int oldColor = SetTextColor(hDC, GetSysColor(COLOR_BTNFACE));
    int oldBkColor = SetBkColor(hDC, GetSysColor(COLOR_BTNTEXT));
    POINT oldBrushPoint;
    SetBrushOrgEx(hDC, 0, 0, &oldBrushPoint); // under XP with the Normal skin the paint misbehaved if the static was placed on a gradient background (FTP configuration)
    DrawFocusRect(hDC, &r);
    SetBrushOrgEx(hDC, oldBrushPoint.x, oldBrushPoint.y, NULL);
    SetTextColor(hDC, oldColor);
    SetBkColor(hDC, oldBkColor);

    if (releaseDC)
        HANDLES(ReleaseDC(HWindow, hDC));
}

BOOL CStaticText::ShowHint()
{
    SetCurrentToolTip(NULL, 0);

    RECT r;
    GetWindowRect(HWindow, &r);
    int xOffset = GetTextXOffset();

    MainWindow->ToolTip->SetCurrentToolTip(HWindow, 1, -1);
    MainWindow->ToolTip->Show(r.left + xOffset, r.bottom, FALSE, TRUE, HWindow);
    // note: Show has the parameter 'modal'==TRUE, so control returns here only after the tooltip is closed
    return TRUE;
}

LRESULT
// Message procedure of the owner-drawn static text control: resizes the
// off-screen bitmap and re-prepares layout on size/enable changes, suppresses
// background erasing (paint covers everything), and forwards mouse messages
// for tooltip/copy behavior.
CStaticText::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CStaticText::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_SIZE:
    {
        Width = LOWORD(lParam);
        Height = HIWORD(lParam);
        if (Bitmap != NULL)
        {
            if (!Bitmap->Enlarge(Width, Height))
            {
                delete Bitmap;
                Bitmap = NULL;
            }
        }
        InvalidateRect(HWindow, NULL, FALSE);
        PrepareForPaint();
        return 0;
    }

    case WM_ERASEBKGND:
    {
        // background will erased in paint
        return TRUE;
    }

    case WM_ENABLE:
    {
        InvalidateRect(HWindow, NULL, FALSE);
        PrepareForPaint();
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (ToolTipAssigned())
        {
            POINT p;
            DWORD messagePos = GetMessagePos();
            p.x = GET_X_LPARAM(messagePos);
            p.y = GET_Y_LPARAM(messagePos);
            if (TextHitTest(&p))
            {
                if (ToolTipText != NULL)
                    SetCurrentToolTip(HWindow, 1);
                else if (HToolTipNW != NULL)
                    SetCurrentToolTip(HWindow, ToolTipID);
            }
            else
                SetCurrentToolTip(NULL, 0);

            if (!MouseIsTracked)
            {
                TRACKMOUSEEVENT tme;
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = HWindow;
                MouseIsTracked = TrackMouseEvent(&tme);
            }
        }
        break;
    }

    case WM_SHOWWINDOW:
    {
        if (wParam == TRUE)
            break;
        // if someone hides us, we must dismiss the tooltip
        if (MainWindow != NULL && MainWindow->ToolTip != NULL && MainWindow->ToolTip->HWindow != NULL)
            MainWindow->ToolTip->Hide();
        //PostMessage(MainWindow->ToolTip->HWindow, WM_CANCELMODE, 0, 0);
    } // fall through to WM_MOUSELEAVE
    case WM_MOUSELEAVE:
    {
        if (ToolTipAssigned())
        {
            SetCurrentToolTip(NULL, 0);
            MouseIsTracked = FALSE;
        }
        break;
    }

    case WM_USER_TTGETTEXT:
    {
        if (ToolTipText != NULL)
        {
            // The tooltip protocol supplies this fixed capacity, so never expose a partial or unterminated label.
            if (FAILED(StringCchCopyA((char*)lParam, TOOLTIP_TEXT_MAX, ToolTipText)))
                ((char*)lParam)[0] = 0;
        }
        return 0;
    }

    case WM_SETTEXT:
    {
        // Win32 delivers UTF-16 to this Unicode subclass; SetText retains the plug-in API's UTF-8 contract.
        std::string utf8;
        const wchar_t* wide = lParam != 0 ? (const wchar_t*)lParam : L"";
        return WideTextToUtf8(wide, -1, utf8) && SetText(utf8.c_str());
    }

    case WM_GETTEXTLENGTH:
        return TextLenW;

    case WM_GETTEXT:
    {
        if (lParam == 0 || wParam == 0)
            return 0;
        size_t count = (size_t)TextLenW < wParam - 1 ? (size_t)TextLenW : wParam - 1;
        // A short destination must not receive half of a supplementary character or lose its terminator.
        if (count > 0 && count < (size_t)TextLenW && TextW[count - 1] >= 0xd800 && TextW[count - 1] <= 0xdbff &&
            TextW[count] >= 0xdc00 && TextW[count] <= 0xdfff)
            --count;
        if (count != 0)
            memcpy((wchar_t*)lParam, TextW, count * sizeof(wchar_t));
        ((wchar_t*)lParam)[count] = 0;
        return count;
    }

    case WM_GETDLGCODE:
    {
        LRESULT ret = DLGC_STATIC;
        if (HintMode)
            ret |= DLGC_WANTARROWS;
        return ret;
    }

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    {
        if (GetWindowLongPtr(HWindow, GWL_STYLE) & WS_TABSTOP)
        {
            DrawFocus(NULL);
        }
        break;
    }

    case WM_LBUTTONDOWN:
    {
        if (HintMode)
            ShowHint();
        break;
    }

    case WM_KEYDOWN:
    {
        if (HintMode && (wParam == VK_SPACE || wParam == VK_UP || wParam == VK_DOWN))
            ShowHint();
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HANDLES(BeginPaint(HWindow, &ps));

        // if we have a bitmap,we will draw into it, otherwise directly to the screen
        HDC hDC;
        if (Bitmap != NULL)
            hDC = Bitmap->HMemDC;
        else
            hDC = ps.hdc;

        RECT r;
        r.left = 0;
        r.top = 0;
        r.right = Width;
        r.bottom = Height;

        // display our own text
        if (Text != NULL)
        {
            // under XPTheme we have to let Windows erase the background
            BOOL bkErased = FALSE;
            if (IsAppThemed())
            {
                DrawThemeParentBackground(HWindow, hDC, &r);
                bkErased = TRUE;
            }

            // set the DC parameters and store their original values
            int oldBkMode = SetBkMode(hDC, TRANSPARENT);

            HWND hParent = GetParent(HWindow);
            if (hParent != NULL)
                SendMessage(hParent, WM_CTLCOLORSTATIC, (WPARAM)hDC, (LPARAM)HWindow);
            if (Flags & STF_HYPERLINK_COLOR)
                SetTextColor(hDC, RGB(0, 0, 255));
            BOOL enabled = IsWindowEnabled(HWindow);
            if (!enabled)
                SetTextColor(hDC, GetSysColor(COLOR_GRAYTEXT));

            //        COLORREF textClr;
            //        if (Flags & STF_HYPERLINK_COLOR)
            //          textClr = RGB(0, 0, 255);
            //        else
            //          textClr = GetSysColor(COLOR_BTNTEXT);
            //        COLORREF oldTextColor = SetTextColor(hDC, textClr);
            //        COLORREF oldBkColor = SetBkColor(hDC, GetSysColor(COLOR_BTNFACE));
            HFONT hOldFont = (HFONT)SelectObject(hDC, HFont);

            // we draw the text
            // The stored text and every shortened variant have validated UTF-16 counterparts.
            if (Flags & STF_HANDLEPREFIX)
            {
                DWORD drawFlags = DT_SINGLELINE | DT_TOP;
                if (Alignment == 1)
                    drawFlags |= DT_CENTER;
                else if (Alignment == 2)
                    drawFlags |= DT_RIGHT;
                else
                    drawFlags |= DT_LEFT;
                // because ClearType spills beyond the control and leaves stray colored dots, we must
                // clip everything; the issue is visible in the Plugins Manager Salamander 2.51 when scrolling
                // the plugin list, leaving a red dot before the URL
                // if (!ClipDraw)
                drawFlags |= DT_NOCLIP;

                if (UIState & UISF_HIDEACCEL)
                    drawFlags |= DT_HIDEPREFIX;

                DrawTextW(hDC, TextW, TextLenW, &r, drawFlags);
            }
            else
            {
                DWORD drawFlags = (bkErased) ? 0 : ETO_OPAQUE;
                // if (ClipDraw) // same problem as above
                drawFlags |= ETO_CLIPPED;

                int xOffset = GetTextXOffset();
                
                const wchar_t* textW = Text2Draw ? Text2W : TextW;
                int drawLength = Text2Draw ? Text2LenW : TextLenW;
                ExtTextOutW(hDC, r.left + xOffset, r.top, drawFlags, &r, textW, drawLength, NULL);
            }

            if (Flags & STF_DOTUNDERLINE)
            {
                // dotted underline
                int xOffset = GetTextXOffset();

                HPEN hDottedPen = HANDLES(CreatePen(PS_DOT, 0, GetTextColor(hDC)));
                HPEN hOldPen = (HPEN)SelectObject(hDC, hDottedPen);
                MoveToEx(hDC, r.left + xOffset, r.bottom - 1, NULL);
                LineTo(hDC, r.left + xOffset + TextWidth, r.bottom - 1);
                SelectObject(hDC, hOldPen);
                HANDLES(DeleteObject(hDottedPen));
            }

            // restore the original DC values
            SelectObject(hDC, hOldFont);
            //        SetBkColor(hDC, oldBkColor);
            //        SetTextColor(hDC, oldTextColor);
            SetBkMode(hDC, oldBkMode);
        }
        else
        {
            // no text stored; we must at least erase the background
            if (IsAppThemed())
            {
                DrawThemeParentBackground(HWindow, hDC, &r);
            }
            else
                FillRect(hDC, &r, (HBRUSH)(COLOR_BTNFACE + 1));
        }

        if ((GetWindowLongPtr(HWindow, GWL_STYLE) & WS_TABSTOP) && GetFocus() == HWindow)
            DrawFocus(hDC);

        if (Bitmap != NULL)
        {
            // if using the cache, we must copy it to the window
            BitBlt(ps.hdc, 0, 0, Width, Height, Bitmap->HMemDC, 0, 0, SRCCOPY);
        }

        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }

    case WM_UPDATEUISTATE:
    {
        // unfortunately we cannot rely on the standard static handling because
        // under Vista (and maybe earlier) it draws the Alt underline at a nonsensical
        // position; one solution would be to capture the text into our buffer and draw from it,
        // but I chose a different approach and maintain the state ourselves
        if (LOWORD(wParam) == UIS_CLEAR)
            UIState &= ~HIWORD(wParam);
        else if (LOWORD(wParam) == UIS_SET)
            UIState |= HIWORD(wParam);

        BOOL showAccel = (LOWORD(wParam) == UIS_CLEAR) && ((HIWORD(wParam) & UISF_HIDEACCEL) != 0);
        if (showAccel)
        {
            InvalidateRect(HWindow, NULL, TRUE); // if not cached we flicker a little, but never mind
            UpdateWindow(HWindow);
        }
        return 0;
    }
    }

    return CWindow::WindowProc(uMsg, wParam, lParam);
}
