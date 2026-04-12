// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "menu.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "mainwnd.h"
#include "plugins.h"
#include "filesbox.h"
#include "fileswnd.h"
#include "stswnd.h"
#include "editwnd.h"
#include "zip.h"
#include "cache.h"
#include "viewer.h"
#include "codetbl.h"
#include "shellib.h"
#include "gui.h"
#include "tasklist.h"
#include "olespy.h"
#include "md5.h"
#include "geticon.h"
#include "pack.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "crypt\fileenc.h"
#include "crypt\sha1.h"
#include "pwdmngr.h"

CPackerConfig PackerConfig;
CUnpackerConfig UnpackerConfig;

const char* STR_NONE = "(none)";

CSalamanderDirectory GlobalEmptySalDir(FALSE); // returned as an empty sal-dir (instead of NULL) - only for archives

HWND ProgressDialogActivateDrop = NULL;

//
// ****************************************************************************
// CZIPUnpackProgress
//

CZIPUnpackProgress::CZIPUnpackProgress() : CCommonDialog(HLanguage, IDD_ZIPUNPACKPROG, NULL, ooStatic)
{
    Init();
}

void CZIPUnpackProgress::Init()
{
    SetTotal(CQuadWord(0, 0), CQuadWord(0, 0));
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    Title = NULL;
    Cancel = FALSE;
    SetRemapNames(NULL, NULL);
    int i;
    for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        LinesCache[i][0] = 0;
    CacheIndex = 0;
    CacheIsDirty = FALSE;
    SizeIsDirty = FALSE;
    Size2IsDirty = FALSE;
    LastTickCount = 0;
    FileProgress = FALSE;
    TaskBarList3 = NULL;
}

CZIPUnpackProgress::CZIPUnpackProgress(const char* title, HWND parent, const CQuadWord& totalSize, CITaskBarList3* taskBarList3)
    : CCommonDialog(HLanguage, IDD_ZIPUNPACKPROG, parent, ooStatic)
{
    SetTotal(totalSize, CQuadWord(0, 0));
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    Title = title;
    Cancel = FALSE;
    SetRemapNames(NULL, NULL);
    int i;
    for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        LinesCache[i][0] = 0;
    CacheIndex = 0;
    CacheIsDirty = FALSE;
    SizeIsDirty = FALSE;
    Size2IsDirty = FALSE;
    LastTickCount = 0;
    FileProgress = FALSE;
    TaskBarList3 = taskBarList3;
}

void CZIPUnpackProgress::Set(const char* title, HWND parent, const CQuadWord& totalSize, BOOL fileProgress)
{
    ResID = IDD_ZIPUNPACKPROG;
    SetTotal(totalSize, CQuadWord(0, 0));
    SetParent(parent);
    Title = title;
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    FileProgress = fileProgress;
}

void CZIPUnpackProgress::Set(const char* title, HWND parent, const CQuadWord& totalSize1,
                             const CQuadWord& totalSize2)
{
    ResID = IDD_ZIPUNPACKPROG2;
    SetTotal(totalSize1, totalSize2);
    SetParent(parent);
    Title = title;
    ActualSize = CQuadWord(0, 0);
    ActualSize2 = CQuadWord(0, 0);
    FileProgress = FALSE;
}

void CZIPUnpackProgress::SetTotal(const CQuadWord& total1, const CQuadWord& total2)
{
    if (total1 != CQuadWord(-1, -1))
    {
        TotalSize = max(CQuadWord(1, 0), total1);
        SizeIsDirty = FALSE;
    }
    if (total2 != CQuadWord(-1, -1))
    {
        TotalSize2 = max(CQuadWord(1, 0), total2);
        Size2IsDirty = FALSE;
    }
}

void CZIPUnpackProgress::SetRemapNames(const char* nameFrom, const char* nameTo)
{
    RemapNameFrom = nameFrom;
    RemapNameTo = nameTo;
}

void CZIPUnpackProgress::DoRemapNames(char* txt, int bufLen)
{
    if (RemapNameFrom != NULL && RemapNameTo != NULL)
    {
        char* s = strstr(txt, RemapNameFrom);
        if (s != NULL)
        {
            int len = (int)strlen(txt);
            int lenFrom = (int)strlen(RemapNameFrom);
            int lenTo = (int)strlen(RemapNameTo);
            if (len - lenFrom + lenTo < bufLen)
            {
                memmove(s + lenTo, s + lenFrom, len - ((s + lenFrom) - txt) + 1);
                memcpy(s, RemapNameTo, lenTo);
            }
            else
                TRACE_E("Remap: too long name.");
        }
    }
}

void CZIPUnpackProgress::SetTaskBarList3(CITaskBarList3* taskBarList3)
{
    TaskBarList3 = taskBarList3;
}

void CZIPUnpackProgress::DispatchMessages()
{
    // pump the message queue
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (!IsWindow(HWindow) || !IsDialogMessage(HWindow, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

BOOL CZIPUnpackProgress::HasTwoProgress()
{
    return ResID == IDD_ZIPUNPACKPROG2;
}

int CZIPUnpackProgress::SetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint)
{
    // does the user want to set size1?
    if (size1 != CQuadWord(-1, -1))
    {
        CQuadWord newSize = max(CQuadWord(0, 0), size1);
        if (newSize != ActualSize)
        {
            ActualSize = newSize;
            SizeIsDirty = TRUE;
        }
    }
    // does the user want to set size2?
    if (size2 != CQuadWord(-1, -1))
    {
        CQuadWord newSize = max(CQuadWord(0, 0), size2);
        if (newSize != ActualSize2)
        {
            ActualSize2 = newSize;
            Size2IsDirty = TRUE;
        }
    }
    return AddSize(0, delayedPaint);
}

int CZIPUnpackProgress::AddSize(int size, BOOL delayedPaint)
{
    // time-critical function
    //  CALL_STACK_MESSAGE2("CZIPUnpackProgress::AddSize(%d)", size);

    ActualSize += CQuadWord(size, 0);
    if (size != 0)
        SizeIsDirty = TRUE;

    if (HasTwoProgress())
    {
        ActualSize2 += CQuadWord(size, 0);
        if (size != 0)
            Size2IsDirty = TRUE;
    }

    if (!delayedPaint)
    {
        // should we draw the text immediately
        FlushDataToControls();
    }

    // every 100 ms redraw the changed data (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        // if we have not repainted a moment ago, do it now
        if (delayedPaint)
            FlushDataToControls();
    }

    DispatchMessages(); // give the user a moment ...

    return !Cancel;
}

void CZIPUnpackProgress::NewLine(const char* txt, BOOL delayedPaint)
{
    // time-critical function
    //  CALL_STACK_MESSAGE2("CZIPUnpackProgress::NewLine(%s)", txt);
    if (txt == NULL)
        return;

    while (1) // output even multiple lines into the dialog
    {
        while (*txt != 0 && (*txt == '\r' || *txt == '\n' || *txt == ' ' || *txt == '\t'))
            txt++;
        if (*txt == 0)
            break;

        // store it in the cache that we display on WM_TIMER

        // the cache index cycles through the items
        CacheIndex++;
        if (CacheIndex >= ZIP_UNPACK_NUMLINES)
            CacheIndex = 0;

        char* s = LinesCache[CacheIndex];
        char* sEnd = s + 300 - 1;
        while (*txt != 0 && *txt != '\r' && *txt != '\n') // read one line + convert '/' -> '\\'
        {
            if (*txt == '/')
            {
                if (s < sEnd)
                    *s++ = '\\';
                txt++;
            }
            else
            {
                if (s < sEnd)
                    *s++ = *txt++;
                else
                    txt++; // simply ignore the rest of the text (it would not fit in the dialog anyway)
            }
        }
        *s = 0;
        DoRemapNames(LinesCache[CacheIndex], 300);

        // we dirtied the cache
        CacheIsDirty = TRUE;
    }

    if (!delayedPaint)
    {
        // should we draw the text immediately
        FlushDataToControls();
    }

    // every 100 ms redraw the changed data (text + progress bars)
    DWORD ticks = GetTickCount();
    if (ticks - LastTickCount > 100)
    {
        LastTickCount = ticks;
        // if we have not repainted a moment ago, do it now
        if (delayedPaint)
            FlushDataToControls();
    }

    // be careful, do not call here
    DispatchMessages(); // give the user a moment ...
}

void CZIPUnpackProgress::EnableCancel(BOOL enable)
{
    if (HWindow != NULL)
    {
        HWND cancel = GetDlgItem(HWindow, IDCANCEL);
        if (IsWindowEnabled(cancel) != enable)
        {
            EnableWindow(cancel, enable);
            if (enable)
                SetFocus(cancel);
            PostMessage(cancel, BM_SETSTYLE, enable ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON, TRUE);

            DispatchMessages(); // give the user a moment ...
        }
    }
}

void CZIPUnpackProgress::FlushDataToControls()
{
    // texts
    if (CacheIsDirty)
    {
        int index = CacheIndex;
        int i;
        for (i = ZIP_UNPACK_NUMLINES - 1; i >= 0; i--)
        {
            if (Lines[i] != NULL)
                Lines[i]->SetText(LinesCache[index]);
            index--;
            if (index < 0)
                index = ZIP_UNPACK_NUMLINES - 1;
        }
        CacheIsDirty = FALSE;
    }

    if (TaskBarList3 != NULL)
    {
        if (HasTwoProgress())
        {
            if (Size2IsDirty)
                TaskBarList3->SetProgress2(ActualSize2, TotalSize2);
        }
        else
        {
            if (SizeIsDirty)
                TaskBarList3->SetProgress2(ActualSize, TotalSize);
        }
    }

    // size
    if (SizeIsDirty)
    {
        if (Summary != NULL)
        {
            Summary->SetProgress2(ActualSize, TotalSize);
        }
        SizeIsDirty = FALSE;
    }

    // size2
    if (Size2IsDirty)
    {
        if (Summary2 != NULL)
        {
            Summary2->SetProgress2(ActualSize2, TotalSize2);
        }
        Size2IsDirty = FALSE;
    }
}

INT_PTR
CZIPUnpackProgress::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        if (ResID == IDD_ZIPUNPACKPROG && FileProgress) // it is necessary to replace the text "Total:" with "File:"
            SetDlgItemText(HWindow, IDT_PROGTITLE, LoadStr(IDS_UNPACKFILEPROGRESS));

        SetWindowText(HWindow, Title);
        // the entire object assumes that the allocations may have failed
        int i;
        for (i = 0; i < ZIP_UNPACK_NUMLINES; i++)
        {
            if ((Lines[i] = new CStaticText(HWindow, IDS_ZIPLINE1 + i, STF_PATH_ELLIPSIS | STF_CACHED_PAINT)) == NULL)
                TRACE_E(LOW_MEMORY);
        }
        if ((Summary = new CProgressBar(HWindow, IDC_ZIPSUMMARY)) == NULL)
            TRACE_E(LOW_MEMORY);
        if (HasTwoProgress())
        {
            if ((Summary2 = new CProgressBar(HWindow, IDC_ZIPSUMMARY2)) == NULL)
                TRACE_E(LOW_MEMORY);
        }
        break;
    }

    case WM_DESTROY:
    {
        if (TaskBarList3 != NULL)
            TaskBarList3->SetProgressState(TBPF_NOPROGRESS);
        break;
    }

    case WM_COMMAND:
    {
        // if the user clicked the Cancel button and has not confirmed it earlier, ask again
        if (LOWORD(wParam) == IDCANCEL && !Cancel)
        {
            // the Cancel button must be enabled
            if (IsWindowEnabled(GetDlgItem(HWindow, IDCANCEL)))
            {
                // to avoid repainting under the message box, repaint explicitly now
                FlushDataToControls();

                // ask the user whether they want to abort the operation
                Cancel = (SalMessageBox(HWindow, LoadStr(IDS_CANCELOPERATION), LoadStr(IDS_QUESTION),
                                        MB_YESNO | MB_ICONQUESTION) == IDYES);
            }
        }
        // do not let the command fall through, otherwise the dialog would close
        return TRUE;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

