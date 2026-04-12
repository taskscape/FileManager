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
            lstrcpyn(helpSubdir, language.HelpDir, MAX_PATH);
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
            lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
            if (!SalPathAppend(helpPath, helpSubdir, MAX_PATH) ||
                !DirExists(helpPath))
            { // the directory from the current .slg file does not exist
                lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
                if (_stricmp(helpSubdir, "english") == 0 || // we already tested "english" and it does not exist so no point in trying again
                    !SalPathAppend(helpPath, "english", MAX_PATH) ||
                    !DirExists(helpPath))
                { // the ENGLISH directory does not exist
                    lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
                    if (SalPathAppend(helpPath, "*", MAX_PATH))
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
                                    lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
                                    if (SalPathAppend(helpPath, data.cFileName, MAX_PATH))
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
                lstrcpyn(CurrentHelpDir, helpPath, MAX_PATH);
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
        lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
        if (SalPathAppend(helpPath, "salamand.chm", MAX_PATH) &&
            FileExists(helpPath))
        {
            HtmlHelp(NULL, helpPath, HH_DISPLAY_TOC, 0); // ignore potential error
        }
    }

    BOOL ret = FALSE;

    lstrcpyn(helpPath, CurrentHelpDir, MAX_PATH);
    if (SalPathAppend(helpPath, helpFileName == NULL ? "salamand.chm" : helpFileName, MAX_PATH) &&
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

