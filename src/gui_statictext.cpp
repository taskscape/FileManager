// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

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
    : CWindow(hDlg, ctrlID, ooAllocated)
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
    AlpDX = NULL;
    Allocated = 0;
    AllocatedW = 0;
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
    char buff[4096];
    CWindow::WindowProc(WM_GETTEXT, 4096, (LPARAM)buff);
    buff[4095] = 0; // just to be sure...
    if (buff[0] != 0)
        SetText(buff);
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
    if (AlpDX != NULL)
        free(AlpDX);
    if (Bitmap != NULL)
        delete Bitmap;
    if (HFont != NULL && DestroyFont)
        HANDLES(DeleteObject(HFont));
}

// prevents numerous reallocations when gradually allocating larger and larger strings
#define ST_ALLOC_GRANULARITY 20

BOOL CStaticText::SetText(const char* text)
{
    CALL_STACK_MESSAGE2("CStaticText::SetText(%s)", text);

    if (text == NULL)
        text = "";
    if (Text != NULL && strcmp(Text, text) == 0)
        return TRUE;

    int l = (int)strlen(text) + 1;
    if (Allocated < l)
    {
        char* newText = (char*)realloc(Text, l + ST_ALLOC_GRANULARITY);
        if (newText == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        if (Flags & (STF_PATH_ELLIPSIS | STF_END_ELLIPSIS))
        {
            int* newAlpDX = (int*)realloc(AlpDX, (l + ST_ALLOC_GRANULARITY) * sizeof(int));
            if (newAlpDX == NULL)
            {
                TRACE_E(LOW_MEMORY);
                free(newText);
                return FALSE;
            }
            char* newText2 = (char*)realloc(Text2, l + ST_ALLOC_GRANULARITY + 3); // 3: space for "..." (I can remove W and add "...")
            if (newText2 == NULL)
            {
                TRACE_E(LOW_MEMORY);
                free(newText);
                free(newAlpDX);
                return FALSE;
            }
            AlpDX = newAlpDX;
            Text2 = newText2;
        }
        Text = newText;
        Allocated = l + ST_ALLOC_GRANULARITY;
    }
    memmove(Text, text, l);
    TextLen = l - 1;

    // Convert UTF-8 to wide characters for proper Unicode display
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (wideLen > 0)
    {
        if (AllocatedW < wideLen)
        {
            wchar_t* newTextW = (wchar_t*)realloc(TextW, (wideLen + ST_ALLOC_GRANULARITY) * sizeof(wchar_t));
            if (newTextW == NULL)
            {
                TRACE_E(LOW_MEMORY);
                // Continue without wide text - will fall back to ANSI display
            }
            else
            {
                TextW = newTextW;
                AllocatedW = wideLen + ST_ALLOC_GRANULARITY;
            }
            if (Flags & (STF_PATH_ELLIPSIS | STF_END_ELLIPSIS))
            {
                wchar_t* newText2W = (wchar_t*)realloc(Text2W, (wideLen + ST_ALLOC_GRANULARITY + 3) * sizeof(wchar_t));
                if (newText2W != NULL)
                    Text2W = newText2W;
            }
        }
        if (TextW != NULL)
        {
            MultiByteToWideChar(CP_UTF8, 0, text, -1, TextW, AllocatedW);
            TextLenW = wideLen - 1;
        }
    }

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

void CStaticText::PrepareForPaint()
{
    ClipDraw = FALSE;
    Text2Draw = FALSE;

    if (Text == NULL || TextLen == 0) // the algorithm is designed only for a non-zero number of characters
    {
        TextWidth = 0;
        TextHeight = 0;
        return;
    }

    HDC hDC = HANDLES(GetDC(HWindow));
    HFONT hOldFont = (HFONT)SelectObject(hDC, HFont);
    SIZE sz;
    
    // Use wide character APIs for proper Unicode support
    BOOL useWide = (TextW != NULL && TextLenW > 0);
    
    if (Flags & (STF_PATH_ELLIPSIS | STF_END_ELLIPSIS))
    {
        if (Flags & STF_END_ELLIPSIS)
        {
            // STF_END_ELLIPSIS: the string will end with an ellipsis
            // we need lengths only for the characters that fit
            int fitChars;
            if (useWide)
                GetTextExtentExPointW(hDC, TextW, TextLenW, Width, &fitChars, AlpDX, &sz);
            else
                GetTextExtentExPoint(hDC, Text, TextLen, Width, &fitChars, AlpDX, &sz);
            
            int textLen = useWide ? TextLenW : TextLen;

            if (fitChars < textLen)
            {
                //we it did not fit -- we must insert an ellipsis

                // we get the width of "..." for the ellipsis
                SIZE ellipsisSZ;
                GetTextExtentPoint32W(hDC, L"...", 3, &ellipsisSZ);
                int ellipsisWidth = ellipsisSZ.cx;

                // we search from the right end to find how much to trim so we can append the ellipsis
                while (fitChars > 0 && AlpDX[fitChars - 1] + ellipsisWidth > Width)
                    fitChars--;
                if (fitChars > 0)
                {
                    if (useWide && Text2W != NULL)
                    {
                        memmove(Text2W, TextW, fitChars * sizeof(wchar_t));
                        Text2LenW = fitChars;
                    }
                    memmove(Text2, Text, fitChars);
                    TextWidth = AlpDX[fitChars - 1];
                    Text2Len = fitChars;
                }
                else
                {
                    TextWidth = 0;
                    Text2Len = 0;
                    Text2LenW = 0;
                }
                strcpy(Text2 + fitChars, "...");
                if (useWide && Text2W != NULL)
                    wcscpy(Text2W + fitChars, L"...");
                TextWidth += ellipsisWidth;
                Text2Len += 3;
                Text2LenW = Text2Len;

                Text2Draw = TRUE;
            }
            else
            {
                TextWidth = sz.cx;
            }
        }
        else
        {
            // STF_PATH_ELLIPSIS: the ellipsis will be inside the text
            // we need lengths of all substrings
            if (useWide)
                GetTextExtentExPointW(hDC, TextW, TextLenW, 0, NULL, AlpDX, &sz);
            else
                GetTextExtentExPoint(hDC, Text, TextLen, 0, NULL, AlpDX, &sz);
            
            int textLen = useWide ? TextLenW : TextLen;

            if (sz.cx > Width)
            {
                // we did not fit -- we must insert an ellipsis

                // get the width of "..." for the ellipsis
                SIZE ellipsisSZ;
                GetTextExtentPoint32W(hDC, L"...", 3, &ellipsisSZ);
                int ellipsisWidth = ellipsisSZ.cx;

                // search from the right end for the path separator
                int pIndex;
                if (useWide)
                {
                    const wchar_t* p = TextW + TextLenW - 1;
                    wchar_t pathSepW = (wchar_t)PathSeparator;
                    while (*p != pathSepW && p > TextW)
                        p--;
                    const wchar_t* p2 = p;
                    if (p > TextW)
                        p--;
                    pIndex = (int)(p - TextW);
                    
                    // the text from 'p' and further should fit entirely including the ellipsis
                    if (ellipsisWidth + sz.cx - AlpDX[pIndex] > Width)
                    {
                        // it did not fit =>we search from the left end for a place to insert the ellipsis
                        while (pIndex < TextLenW && (ellipsisWidth + sz.cx - AlpDX[pIndex] > Width))
                            pIndex++;

                        // we insert the ellipsis and then the rest of the text behind it
                        pIndex++;
                        if (Text2W != NULL)
                            wcscpy(Text2W, L"...");
                        strcpy(Text2, "...");
                        Text2Len = 3;
                        Text2LenW = 3;
                        TextWidth = ellipsisWidth;
                        if (pIndex < TextLenW)
                        {
                            if (Text2W != NULL)
                            {
                                memmove(Text2W + 3, TextW + pIndex, (TextLenW - pIndex + 1) * sizeof(wchar_t));
                                Text2LenW += TextLenW - pIndex;
                            }
                            memmove(Text2 + 3, Text + pIndex, TextLen - pIndex + 1);
                            Text2Len += TextLen - pIndex;
                            TextWidth += sz.cx - AlpDX[pIndex - 1];
                        }
                    }
                    else
                    {
                        int rightPartWidth = sz.cx - AlpDX[pIndex];
                        // we determine how many characters to keep on the left side of the ellipsis
                        while (pIndex >= 0 && (AlpDX[pIndex] + ellipsisWidth + rightPartWidth) > Width)
                            pIndex--;
                        // left part
                        Text2Len = 0;
                        Text2LenW = 0;
                        TextWidth = 0;
                        if (pIndex >= 0)
                        {
                            if (Text2W != NULL)
                            {
                                memmove(Text2W, TextW, (pIndex + 1) * sizeof(wchar_t));
                                Text2LenW += pIndex + 1;
                            }
                            memmove(Text2, Text, pIndex + 1);
                            Text2Len += pIndex + 1;
                            TextWidth += AlpDX[pIndex];
                        }
                        // ellipsis
                        if (Text2W != NULL)
                        {
                            memmove(Text2W + Text2LenW, L"...", 3 * sizeof(wchar_t));
                            Text2LenW += 3;
                        }
                        memmove(Text2 + Text2Len, "...", 3);
                        Text2Len += 3;
                        TextWidth += ellipsisWidth;
                        // right part
                        int rightPartLen = TextLenW - (int)(p2 - TextW);
                        if (Text2W != NULL)
                        {
                            memmove(Text2W + Text2LenW, p2, (rightPartLen + 1) * sizeof(wchar_t));
                            Text2LenW += rightPartLen;
                        }
                        int rightPartLenA = TextLen - (int)((Text + TextLen) - (Text + pIndex + 1 + (p2 - (TextW + pIndex + 1))));
                        // Approximate - use same ratio
                        rightPartLenA = TextLen - pIndex - 1;
                        const char* p2A = Text + TextLen - rightPartLen;
                        memmove(Text2 + Text2Len, p2A, rightPartLen + 1);
                        Text2Len += rightPartLen;
                        TextWidth += rightPartWidth;
                    }
                }
                else
                {
                    // ANSI fallback
                    const char* p = Text + TextLen - 1;
                    while (*p != PathSeparator && p > Text)
                        p--;
                    const char* p2 = p;
                    if (p > Text)
                        p--;
                    pIndex = (int)(p - Text);

                    // the text from 'p' and further should fit entirely including the ellipsis
                    if (ellipsisWidth + sz.cx - AlpDX[pIndex] > Width)
                    {
                        // it did not fit =>we search from the left end for a place to insert the ellipsis
                        while (pIndex < TextLen && (ellipsisWidth + sz.cx - AlpDX[pIndex] > Width))
                            pIndex++;

                        // we insert the ellipsis and then the rest of the text behind it
                        pIndex++;
                        strcpy(Text2, "...");
                        Text2Len = 3;
                        TextWidth = ellipsisWidth;
                        if (pIndex < TextLen)
                        {
                            memmove(Text2 + 3, Text + pIndex, TextLen - pIndex + 1); // including the terminator
                            Text2Len += TextLen - pIndex;
                            TextWidth += sz.cx - AlpDX[pIndex - 1];
                        }
                    }
                    else
                    {
                        int rightPartWidth = sz.cx - AlpDX[pIndex];
                        // we determine how many characters to keep on the left side of the ellipsis
                        while (pIndex >= 0 && (AlpDX[pIndex] + ellipsisWidth + rightPartWidth) > Width)
                            pIndex--;
                        // left part
                        Text2Len = 0;
                        TextWidth = 0;
                        if (pIndex >= 0)
                        {
                            memmove(Text2, Text, pIndex + 1);
                            Text2Len += pIndex + 1;
                            TextWidth += AlpDX[pIndex];
                        }
                        // ellipsis
                        memmove(Text2 + Text2Len, "...", 3);
                        Text2Len += 3;
                        TextWidth += ellipsisWidth;
                        // right part
                        int rightPartLen = TextLen - (int)(p2 - Text);
                        memmove(Text2 + Text2Len, p2, rightPartLen + 1);
                        Text2Len += rightPartLen;
                        TextWidth += rightPartWidth;
                    }
                }

                Text2Draw = TRUE;
            }
            else
            {
                TextWidth = sz.cx;
            }
        }
        TextHeight = sz.cy;
    }
    else
    {
        // the overall dimensions are sufficient
        if (Flags & STF_HANDLEPREFIX)
        {
            RECT r;
            GetClientRect(HWindow, &r);
            if (useWide)
                DrawTextW(hDC, TextW, TextLenW, &r, DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
            else
                DrawText(hDC, Text, TextLen, &r, DT_CALCRECT | DT_SINGLELINE | DT_LEFT);
            TextWidth = r.right;
            TextHeight = r.bottom;
        }
        else
        {
            if (useWide)
                GetTextExtentPoint32W(hDC, TextW, TextLenW, &sz);
            else
                GetTextExtentPoint32(hDC, Text, TextLen, &sz);
            TextWidth = sz.cx + 1;
            TextHeight = sz.cy;
        }
    }
    // if the text would cross the window boundary, we must clip during drawing
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
        return SetText((char*)lParam);
    }

    case WM_GETTEXT:
    {
        if (Text == NULL || wParam < 2)
            return 0;

        int len = (int)strlen(Text);
        if (len > (int)wParam - 1)
            len = (int)wParam - 1;
        memcpy((char*)lParam, Text, len);
        ((char*)lParam)[len + 1] = 0;
        return len;
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
            // Use wide character APIs for proper Unicode support
            BOOL useWide = (TextW != NULL && TextLenW > 0);
            
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

                if (useWide)
                    DrawTextW(hDC, TextW, TextLenW, &r, drawFlags);
                else
                    DrawText(hDC, Text, TextLen, &r, drawFlags);
            }
            else
            {
                DWORD drawFlags = (bkErased) ? 0 : ETO_OPAQUE;
                // if (ClipDraw) // same problem as above
                drawFlags |= ETO_CLIPPED;

                int xOffset = GetTextXOffset();
                
                if (useWide)
                {
                    const wchar_t* textW;
                    int textLenW;
                    if (Text2Draw && Text2W != NULL)
                    {
                        textW = Text2W;
                        textLenW = Text2LenW;
                    }
                    else
                    {
                        textW = TextW;
                        textLenW = TextLenW;
                    }
                    ExtTextOutW(hDC, r.left + xOffset, r.top, drawFlags, &r, textW, textLenW, NULL);
                }
                else
                {
                    const char* text;
                    int textLen;
                    if (Text2Draw)
                    {
                        text = Text2;
                        textLen = Text2Len;
                    }
                    else
                    {
                        text = Text;
                        textLen = TextLen;
                    }
                    ExtTextOut(hDC, r.left + xOffset, r.top, drawFlags, &r, text, textLen, NULL);
                }
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
