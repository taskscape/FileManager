// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <shobjidl.h>

#include "common/thread_owner.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "pack.h"
#include "execute.h"
#include "shellib.h"
#include "menu.h"

// User-menu icon data, background icon reader, and user-menu items extracted from
// path_utils.cpp as a mechanical move. The UserMenuIconBkgndReader global moves
// along with its class; its extern declaration in usermenu.h keeps other
// translation units linked.
CUserMenuIconBkgndReader UserMenuIconBkgndReader;

//
// ****************************************************************************
// CUserMenuIconData
//

CUserMenuIconData::CUserMenuIconData(const char* fileName, DWORD iconIndex, const char* umCommand)
{
    strcpy_s(FileName, fileName);
    IconIndex = iconIndex;
    strcpy_s(UMCommand, umCommand);
    LoadedIcon = NULL;
}

CUserMenuIconData::~CUserMenuIconData()
{
    if (LoadedIcon != NULL)
    {
        HANDLES(DestroyIcon(LoadedIcon));
        LoadedIcon = NULL;
    }
}

void CUserMenuIconData::Clear()
{
    FileName[0] = 0;
    IconIndex = -1;
    UMCommand[0] = 0;
    LoadedIcon = NULL;
}

//
// ****************************************************************************
// CUserMenuIconDataArr
//

HICON
CUserMenuIconDataArr::GiveIconForUMI(const char* fileName, DWORD iconIndex, const char* umCommand)
{
    CALL_STACK_MESSAGE1("CUserMenuIconDataArr::GiveIconForUMI(, ,)");
    for (int i = 0; i < Count; i++)
    {
        CUserMenuIconData* item = At(i);
        if (item->IconIndex == iconIndex &&
            strcmp(item->FileName, fileName) == 0 &&
            strcmp(item->UMCommand, umCommand) == 0)
        {
            HICON icon = item->LoadedIcon; // set LoadedIcon to NULL, otherwise it would be deallocated (via DestroyIcon())
            item->Clear();                 // we do not want to compact the array (on delete) - slow+pointless, so we at least clear the item so it is skipped faster during search
            return icon;
        }
    }
    TRACE_E("CUserMenuIconDataArr::GiveIconForUMI(): unexpected situation: item not found!");
    return NULL;
}

//
// ****************************************************************************
// CUserMenuIconBkgndReader
//

CUserMenuIconBkgndReader::CUserMenuIconBkgndReader()
{
    SysColorsChanged = FALSE;
    HANDLES(InitializeCriticalSection(&CS));
    IconReaderThreadUID = 1;
    CurIRThreadIDIsValid = FALSE;
    CurIRThreadID = -1;
    AlreadyStopped = FALSE;
    UserMenuIconsInUse = 0;
    UserMenuIIU_BkgndReaderData = NULL;
    UserMenuIIU_ThreadID = 0;
}

CUserMenuIconBkgndReader::~CUserMenuIconBkgndReader()
{
    if (UserMenuIIU_BkgndReaderData != NULL) // they really will not be needed anymore, so we release them
    {
        delete UserMenuIIU_BkgndReaderData;
        UserMenuIIU_BkgndReaderData = NULL;
    }
    HANDLES(DeleteCriticalSection(&CS));
}

unsigned BkgndReadingIconsThreadBody(void* param)
{
    CALL_STACK_MESSAGE1("BkgndReadingIconsThreadBody()");
    SetThreadNameInVCAndTrace("UMIconReader");
    TRACE_I("Begin");
    // so GetFileOrPathIconAux works (contains COM/OLE mess)
    if (OleInitialize(NULL) != S_OK)
        TRACE_E("Error in OleInitialize.");

    CUserMenuIconDataArr* bkgndReaderData = (CUserMenuIconDataArr*)param;
    DWORD threadID = bkgndReaderData->GetIRThreadID();

    for (int i = 0; UserMenuIconBkgndReader.IsCurrentIRThreadID(threadID) && i < bkgndReaderData->Count; i++)
    {
        CUserMenuIconData* item = bkgndReaderData->At(i);
        HICON umIcon;
        if (item->FileName[0] != 0 &&
            SalGetFileAttributes(item->FileName) != INVALID_FILE_ATTRIBUTES && // access check (instead of CheckPath)
            ExtractIconEx(item->FileName, item->IconIndex, NULL, &umIcon, 1) == 1)
        {
            HANDLES_ADD(__htIcon, __hoLoadImage, umIcon); // add the handle of 'umIcon' to HANDLES
        }
        else
        {
            umIcon = NULL;
            if (item->UMCommand[0] != 0)
            { // in case the previous method failed - try to get the icon from the system
                DWORD attrs = SalGetFileAttributes(item->UMCommand);
                if (attrs != INVALID_FILE_ATTRIBUTES) // access check (instead of CheckPath)
                {
                    umIcon = GetFileOrPathIconAux(item->UMCommand, FALSE,
                                                  (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)));
                }
            }
        }
        item->LoadedIcon = umIcon; // store result: loaded icon or NULL on error
    }

    UserMenuIconBkgndReader.ReadingFinished(threadID, bkgndReaderData);
    OleUninitialize();
    TRACE_I("End");
    return 0;
}

unsigned BkgndReadingIconsThreadEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return BkgndReadingIconsThreadBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread BkgndReadingIconsThread: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harder exit (this one still calls something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI BkgndReadingIconsThread(void* param)
{
#ifndef CALLSTK_DISABLE
    CCallStack stack;
#endif // CALLSTK_DISABLE
    return BkgndReadingIconsThreadEH(param);
}

void CUserMenuIconBkgndReader::StartBkgndReadingIcons(CUserMenuIconDataArr* bkgndReaderData)
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::StartBkgndReadingIcons()");
    HANDLE thread = NULL;
    HANDLES(EnterCriticalSection(&CS));
    CurIRThreadIDIsValid = FALSE;
    if (!AlreadyStopped && bkgndReaderData != NULL && bkgndReaderData->Count > 0)
    {
        DWORD newThreadID = IconReaderThreadUID++;
        bkgndReaderData->SetIRThreadID(newThreadID);
        thread = HANDLES(CreateThread(NULL, 0, BkgndReadingIconsThread, bkgndReaderData, 0, NULL));
        if (thread != NULL)
        {
            // main thread runs at higher priority; whether icons should be read equally fast
            // as before reading was moved to a background thread, we must raise its priority as well
            SetThreadPriority(thread, THREAD_PRIORITY_ABOVE_NORMAL);

            bkgndReaderData = NULL; // they are passed to the thread; we will not free them here
            CurIRThreadIDIsValid = TRUE;
            CurIRThreadID = newThreadID;
            // The reader uses shared menu state, so shutdown tracks it to a safe join.
            AddAuxThread(thread, FALSE, "user-menu icon reader");
        }
        else
            TRACE_E("CUserMenuIconBkgndReader::StartBkgndReadingIcons(): unable to start thread for reading user menu icons.");
    }
    if (bkgndReaderData != NULL)
        delete bkgndReaderData;
    HANDLES(LeaveCriticalSection(&CS));

    // pause briefly; if icons are read quickly, "simple" icons are not shown at all
    // variants (less flickering) + some users reported that simultaneous icon loading into the panel
    // slowed reading of icons into the user menu quite severely, so their icons on the user menu toolbar appear
    // with a large delay, which is ugly; this should prevent that (it will simply handle only slow
    // loading of user menu icons, which is the goal of this whole exercise)
    if (thread != NULL)
    {
        //    TRACE_I("Waiting for finishing of thread for reading user menu icons...");
        BOOL finished = WaitForSingleObject(thread, 500) == WAIT_OBJECT_0;
        //    TRACE_I("Thread for reading user menu icons is " << (finished ? "FINISHED." : "still running..."));
    }
}

void CUserMenuIconBkgndReader::EndProcessing()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::EndProcessing()");
    HANDLES(EnterCriticalSection(&CS));
    CurIRThreadIDIsValid = FALSE;
    AlreadyStopped = TRUE;
    HANDLES(LeaveCriticalSection(&CS));
}

BOOL CUserMenuIconBkgndReader::IsCurrentIRThreadID(DWORD threadID)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::IsCurrentIRThreadID(%d)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = CurIRThreadIDIsValid && CurIRThreadID == threadID;
    HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

BOOL CUserMenuIconBkgndReader::IsReadingIcons()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::IsReadingIcons()");
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = CurIRThreadIDIsValid;
    HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

void CUserMenuIconBkgndReader::ReadingFinished(DWORD threadID, CUserMenuIconDataArr* bkgndReaderData)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::ReadingFinished(%d,)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ok = CurIRThreadIDIsValid && CurIRThreadID == threadID;
    HWND mainWnd = ok ? MainWindow->HWindow : NULL;
    HANDLES(LeaveCriticalSection(&CS));

    if (ok) // the User Menu is still waiting for these icons
        PostMessage(mainWnd, WM_USER_USERMENUICONS_READY, (WPARAM)bkgndReaderData, (LPARAM)threadID);
    else
        delete bkgndReaderData;
}

void CUserMenuIconBkgndReader::BeginUserMenuIconsInUse()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::BeginUserMenuIconsInUse()");
    HANDLES(EnterCriticalSection(&CS));
    UserMenuIconsInUse++;
    if (UserMenuIconsInUse > 2)
        TRACE_E("CUserMenuIconBkgndReader::BeginUserMenuIconsInUse(): unexpected situation, report to Petr!");
    HANDLES(LeaveCriticalSection(&CS));
}

void CUserMenuIconBkgndReader::EndUserMenuIconsInUse()
{
    CALL_STACK_MESSAGE1("CUserMenuIconBkgndReader::EndUserMenuIconsInUse()");
    HANDLES(EnterCriticalSection(&CS));
    if (UserMenuIconsInUse == 0)
        TRACE_E("CUserMenuIconBkgndReader::EndUserMenuIconsInUse(): unexpected situation, report to Petr!");
    else
    {
        UserMenuIconsInUse--;
        if (UserMenuIconsInUse == 0 && UserMenuIIU_BkgndReaderData != NULL)
        { // last lock, if we have stored data to process, send it
            if (CurIRThreadIDIsValid && CurIRThreadID == UserMenuIIU_ThreadID)
            {
                PostMessage(MainWindow->HWindow, WM_USER_USERMENUICONS_READY,
                            (WPARAM)UserMenuIIU_BkgndReaderData, (LPARAM)UserMenuIIU_ThreadID);
            }
            else // nobody wants the data anymore, just free it
                delete UserMenuIIU_BkgndReaderData;
            UserMenuIIU_BkgndReaderData = NULL;
            UserMenuIIU_ThreadID = 0;
        }
    }
    HANDLES(LeaveCriticalSection(&CS));
}

BOOL CUserMenuIconBkgndReader::EnterCSIfCanUpdateUMIcons(CUserMenuIconDataArr** bkgndReaderData, DWORD threadID)
{
    CALL_STACK_MESSAGE2("CUserMenuIconBkgndReader::EnterCSIfCanUpdateUMIcons(, %d)", threadID);
    HANDLES(EnterCriticalSection(&CS));
    BOOL ret = FALSE;
    if (CurIRThreadIDIsValid && CurIRThreadID == threadID)
    {
        if (UserMenuIconsInUse > 0)
        {
            if (UserMenuIIU_BkgndReaderData != NULL) // if some are already stored, release them (enter cfg during loading, then color change and it comes here again)
                delete UserMenuIIU_BkgndReaderData;
            UserMenuIIU_BkgndReaderData = *bkgndReaderData;
            UserMenuIIU_ThreadID = threadID;
            *bkgndReaderData = NULL; // the caller has handed the data over to us; we will free it ourselves later
        }
        else
        {
            ret = TRUE;
            TRACE_I("Updating user menu icons to results from reading thread no. " << threadID);
        }
    }
    if (!ret)
        HANDLES(LeaveCriticalSection(&CS));
    return ret;
}

void CUserMenuIconBkgndReader::LeaveCSAfterUMIconsUpdate()
{
    CurIRThreadIDIsValid = FALSE; // icons are handed to usermenu by this (IsReadingIcons() must return FALSE)
    HANDLES(LeaveCriticalSection(&CS));
}

//
// ****************************************************************************
// CUserMenuItem
//

CUserMenuItem::CUserMenuItem(char* name, char* umCommand, char* arguments, char* initDir, char* icon,
                             int throughShell, int closeShell, int useWindow, int showInToolbar, CUserMenuItemType type,
                             CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
    ThroughShell = throughShell;
    CloseShell = closeShell;
    UseWindow = useWindow;
    ShowInToolbar = showInToolbar;
    Type = type;
    Set(name, umCommand, arguments, initDir, icon);
    if (Type == umitItem || Type == umitSubmenuBegin)
        GetIconHandle(bkgndReaderData, FALSE);
}

CUserMenuItem::CUserMenuItem()
{
    UMIcon = NULL;
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
    ThroughShell = TRUE;
    CloseShell = TRUE;
    UseWindow = TRUE;
    ShowInToolbar = TRUE;
    Type = umitItem;
    static char emptyBuffer[] = "";
    static char nameBuffer[] = "\"$(Name)\"";
    static char fullPathBuffer[] = "$(FullPath)";
    Set(emptyBuffer, emptyBuffer, nameBuffer, fullPathBuffer, emptyBuffer);
}

CUserMenuItem::CUserMenuItem(CUserMenuItem& item, CUserMenuIconDataArr* bkgndReaderData)
{
    UMIcon = NULL;
    ItemName = UMCommand = Arguments = InitDir = Icon = NULL;
    ThroughShell = item.ThroughShell;
    CloseShell = item.CloseShell;
    UseWindow = item.UseWindow;
    ShowInToolbar = item.ShowInToolbar;
    Type = item.Type;
    Set(item.ItemName, item.UMCommand, item.Arguments, item.InitDir, item.Icon);
    if (Type == umitItem)
    {
        if (bkgndReaderData == NULL) // this is a copy into the cfg dialog; we do not propagate newly loaded icons there (we wait until the dialog ends)
        {
            UMIcon = DuplicateIcon(NULL, item.UMIcon); // GetIconHandle(); it slowed things down needlessly
            if (UMIcon != NULL)                        // add the handle of 'UMIcon' to HANDLES
                HANDLES_ADD(__htIcon, __hoLoadImage, UMIcon);
        }
        else
            GetIconHandle(bkgndReaderData, FALSE);
    }
    if (Type == umitSubmenuBegin)
    {
        if (item.UMIcon != HGroupIcon)
            TRACE_E("CUserMenuItem::CUserMenuItem(): unexpected submenu item icon.");
        UMIcon = HGroupIcon;
    }
}

CUserMenuItem::~CUserMenuItem()
{
    // umitSubmenuBegin shares one icon
    if (UMIcon != NULL && Type != umitSubmenuBegin)
        HANDLES(DestroyIcon(UMIcon));
    if (ItemName != NULL)
        free(ItemName);
    if (UMCommand != NULL)
        free(UMCommand);
    if (Arguments != NULL)
        free(Arguments);
    if (InitDir != NULL)
        free(InitDir);
    if (Icon != NULL)
        free(Icon);
}

BOOL CUserMenuItem::Set(char* name, char* umCommand, char* arguments, char* initDir, char* icon)
{
    if (name == NULL || umCommand == NULL || arguments == NULL || initDir == NULL || icon == NULL)
    {
        TRACE_E("CUserMenuItem::Set(): unexpected NULL parameter.");
        return FALSE;
    }
    char* itemName = (char*)malloc(strlen(name) + 1);
    char* commandName = (char*)malloc(strlen(umCommand) + 1);
    char* argumentsName = (char*)malloc(strlen(arguments) + 1);
    char* initDirName = (char*)malloc(strlen(initDir) + 1);
    char* iconName = (char*)malloc(strlen(icon) + 1);
    if (itemName == NULL || commandName == NULL ||
        argumentsName == NULL || initDirName == NULL || iconName == NULL)
    {
        free(itemName);
        free(commandName);
        free(argumentsName);
        free(initDirName);
        free(iconName);
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    // Each allocation is sized from its source, so copy the exact payload and terminator without retaining unchecked string APIs.
    memcpy(itemName, name, strlen(name) + 1);
    memcpy(commandName, umCommand, strlen(umCommand) + 1);
    memcpy(argumentsName, arguments, strlen(arguments) + 1);
    memcpy(initDirName, initDir, strlen(initDir) + 1);
    memcpy(iconName, icon, strlen(icon) + 1);

    if (ItemName != NULL)
        free(ItemName);
    if (UMCommand != NULL)
        free(UMCommand);
    if (Arguments != NULL)
        free(Arguments);
    if (InitDir != NULL)
        free(InitDir);
    if (Icon != NULL)
        free(Icon);

    ItemName = itemName;
    UMCommand = commandName;
    Arguments = argumentsName;
    InitDir = initDirName;
    Icon = iconName;
    return TRUE;
}

void CUserMenuItem::SetType(CUserMenuItemType type)
{
    if (Type != type)
    {
        if (type == umitSubmenuBegin)
        {
            // switching to the shared icon, delete the allocated one
            if (UMIcon != NULL)
            {
                HANDLES(DestroyIcon(UMIcon));
                UMIcon = NULL;
            }
        }
        if (Type == umitSubmenuBegin)
            UMIcon = NULL; // leaving the shared icon
    }
    Type = type;
}

BOOL CUserMenuItem::GetIconHandle(CUserMenuIconDataArr* bkgndReaderData, BOOL getIconsFromReader)
{
    if (Type == umitSubmenuBegin)
    {
        UMIcon = HGroupIcon;
        return TRUE;
    }

    if (UMIcon != NULL)
    {
        HANDLES(DestroyIcon(UMIcon));
        UMIcon = NULL;
    }

    if (Type == umitSeparator) // a separator has no icon
        return TRUE;

    // try to extract the icon from the specified file
    char fileName[MAX_PATH];
    fileName[0] = 0;
    DWORD iconIndex = -1;
    if (MainWindow != NULL && Icon != NULL && Icon[0] != 0)
    {
        // Icon is in the format "file name,resID"
        // parse it
        char* iterator = Icon + strlen(Icon) - 1;
        while (iterator > Icon && *iterator != ',')
            iterator--;
        if (iterator > Icon && *iterator == ',')
        {
            strncpy(fileName, Icon, iterator - Icon);
            fileName[iterator - Icon] = 0;
            iterator++;
            iconIndex = atoi(iterator);
        }
    }

    if (bkgndReaderData == NULL && fileName[0] != 0 && // we are supposed to read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, fileName) == ERROR_SUCCESS &&
        ExtractIconEx(fileName, iconIndex, NULL, &UMIcon, 1) == 1)
    {
        HANDLES_ADD(__htIcon, __hoLoadImage, UMIcon); // add the handle of 'UMIcon' to HANDLES
        return TRUE;
    }

    // in case the previous method failed - try to get the icon from the system
    char umCommand[MAX_PATH];
    if (MainWindow != NULL && UMCommand != NULL && UMCommand[0] != 0 &&
        ExpandCommand(MainWindow->HWindow, UMCommand, umCommand, MAX_PATH, TRUE))
    {
        while (strlen(umCommand) > 2 && CutDoubleQuotesFromBothSides(umCommand))
            ;
    }
    else
        umCommand[0] = 0;

    if (bkgndReaderData == NULL && umCommand[0] != 0 && // we are supposed to read icons right here
        MainWindow->GetActivePanel() != NULL &&
        MainWindow->GetActivePanel()->CheckPath(FALSE, umCommand) == ERROR_SUCCESS)
    {
        DWORD attrs = SalGetFileAttributes(umCommand);
        UMIcon = GetFileOrPathIconAux(umCommand, FALSE,
                                      (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)));
        if (UMIcon != NULL)
            return TRUE;
    }

    if (bkgndReaderData != NULL)
    {
        if (getIconsFromReader) // the icons are already loaded, we just take the right one
        {
            UMIcon = bkgndReaderData->GiveIconForUMI(fileName, iconIndex, umCommand);
            if (UMIcon != NULL)
                return TRUE;
        }
        else // request loading of the needed icon
            bkgndReaderData->Add(new CUserMenuIconData(fileName, iconIndex, umCommand));
    }

    // get the default icon from shell32.dll
    UMIcon = SalLoadImage(2, 1, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
    return TRUE;
}

BOOL CUserMenuItem::GetHotKey(char* key)
{
    if (ItemName == NULL || Type == umitSeparator)
        return FALSE;
    char* iterator = ItemName;
    while (*iterator != 0)
    {
        if (*iterator == '&' && *(iterator + 1) != 0 && *(iterator + 1) != '&')
        {
            *key = *(iterator + 1);
            return TRUE;
        }
        iterator++;
    }
    return FALSE;
}

//
// ****************************************************************************
// CUserMenuItems
//

BOOL CUserMenuItems::LoadUMI(CUserMenuItems& source, BOOL readNewIconsOnBkgnd)
{
    CUserMenuItem* item;
    DestroyMembers();
    CUserMenuIconDataArr* bkgndReaderData = readNewIconsOnBkgnd ? new CUserMenuIconDataArr() : NULL;
    int i;
    for (i = 0; i < source.Count; i++)
    {
        item = new CUserMenuItem(*source[i], bkgndReaderData);
        Add(item);
    }
    if (readNewIconsOnBkgnd)
        UserMenuIconBkgndReader.StartBkgndReadingIcons(bkgndReaderData); // CAUTION: releases 'bkgndReaderData'
    return TRUE;
}

int CUserMenuItems::GetSubmenuEndIndex(int index)
{
    int level = 1;
    int i;
    for (i = index + 1; i < Count; i++)
    {
        CUserMenuItem* item = At(i);
        if (item->Type == umitSubmenuBegin)
            level++;
        else
        {
            if (item->Type == umitSubmenuEnd)
            {
                level--;
                if (level == 0)
                    return i;
            }
        }
    }
    return -1;
}

