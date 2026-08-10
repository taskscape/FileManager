// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <shlwapi.h>
#undef PathIsPrefix // otherwise conflicts with CSalamanderGeneral::PathIsPrefix

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
#include "operation_journal.h"

// variables used when saving configuration during shutdown, log-off or restart
// we must pump messages so the system does not kill us as "not responding"
CWaitWindow* GlobalSaveWaitWindow = NULL; // if a global wait window for Save exists, it's here (otherwise NULL)
int GlobalSaveWaitWindowProgress = 0;     // current progress value of the global wait window for Save

// borrow constants from a newer SDK
#define WM_APPCOMMAND 0x0319
#define FAPPCOMMAND_MOUSE 0x8000
#define FAPPCOMMAND_KEY 0
#define FAPPCOMMAND_OEM 0x1000
#define FAPPCOMMAND_MASK 0xF000
#define GET_APPCOMMAND_LPARAM(lParam) ((short)(HIWORD(lParam) & ~FAPPCOMMAND_MASK))
#define APPCOMMAND_BROWSER_BACKWARD 1
#define APPCOMMAND_BROWSER_FORWARD 2
/* not supported yet
#define APPCOMMAND_BROWSER_SEARCH         5
#define APPCOMMAND_HELP                   27
#define APPCOMMAND_BROWSER_REFRESH        3
#define APPCOMMAND_FIND                   28
#define APPCOMMAND_COPY                   36
#define APPCOMMAND_CUT                    37
#define APPCOMMAND_PASTE                  38
*/

const int SPLIT_LINE_WIDTH = 3; // width of the split line in points
// if the middle toolbar is visible, the composition will be SPLIT_LINE_WIDTH + toolbar + SPLIT_LINE_WIDTH

const int MIN_WIN_WIDTH = 2; // minimal panel width

extern BOOL CacheNextSetFocus;

BOOL MainFrameIsActive = FALSE;

// code for testing time losses
/*
  const char *s1 = "aj hjka sakjSJKAHS AJKSH JKDSHFJSDH FJS HDFJSD HFJS";
  const char *s2 = "Aj hjka sakjSJKAHS AJKSH JKDSHFJSDH FJS HDFJSD HFJS";

  LARGE_INTEGER t1, t2, t3, f;

  int len1 = strlen(s1);
  int count = 100000;
  QueryPerformanceCounter(&t1);
  int c = 0;
  int i;
  for (i = 0; i < count; i++)
    c += MemICmp(s1, s2, len1);
  QueryPerformanceCounter(&t2);
  c = 0;
  for (i = 0; i < count; i++)
    c += StrICmp(s1, len1, s2, len1);
  QueryPerformanceCounter(&t3);

  QueryPerformanceFrequency(&f);

  char buff[200];
  double a = (double)(t2.QuadPart - t1.QuadPart) / f.QuadPart;
  double b = (double)(t3.QuadPart - t2.QuadPart) / f.QuadPart;
  sprintf(buff, "t1=%1.4lg\nt2=%1.4lg", a, b);
  MessageBox(HWindow, buff, "Results", MB_OK);
*/


// HtmlHelp support (MessageBoxHelpCallback, CSalamanderHelp, OpenHtmlHelp)
// has been moved to mainwnd_help.cpp.

//****************************************************************************
//
// CMWDropTarget
//
// used only for moving dragged images
//

class CMWDropTarget : public IDropTarget
{
private:
    long RefCount; // object lifetime

public:
    CMWDropTarget()
    {
        RefCount = 1;
    }

    virtual ~CMWDropTarget()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of object");
    }

    STDMETHOD(QueryInterface)
    (REFIID refiid, void FAR* FAR* ppv)
    {
        if (refiid == IID_IUnknown || refiid == IID_IDropTarget)
        {
            *ppv = this;
            AddRef();
            return NOERROR;
        }
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
    }

    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // cannot touch the object anymore, it no longer exists
        }
        return RefCount;
    }

    STDMETHOD(DragEnter)
    (IDataObject* pDataObject, DWORD grfKeyState,
     POINTL pt, DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragEnter(pt.x, pt.y);
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragOver)
    (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect)
    {
        if (ImageDragging)
            ImageDragMove(pt.x, pt.y);
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    STDMETHOD(DragLeave)
    ()
    {
        if (ImageDragging)
            ImageDragLeave();
        return E_UNEXPECTED;
    }

    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect)
    {
        *pdwEffect = DROPEFFECT_NONE;
        return E_UNEXPECTED;
    }
};

//
// ****************************************************************************
// MyShutdownBlockReasonCreate a MyShutdownBlockReasonDestroy
//
// Vista+: dynamically obtain functions for setting/clearing shutdown block reasons
//


// MyShutdownBlockReasonCreate/Destroy have been moved to mainwnd_shutdown.cpp.


//
// ****************************************************************************
// CMainWindow
//

VOID CALLBACK SkipOneARTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
    SkipOneActivateRefresh = FALSE;
    KillTimer(hwnd, idEvent);
}

void CMainWindow::SafeHandleMenuNewMsg2(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    __try
    {
        IContextMenu3* contextMenu3 = NULL;
        *plResult = 0;
        if (uMsg == WM_MENUCHAR)
        {
            if (SUCCEEDED(ContextMenuNew->GetMenu2()->QueryInterface(IID_IContextMenu3, (void**)&contextMenu3)))
            {
                contextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, plResult);
                contextMenu3->Release();
                return;
            }
        }
        // the menu is destroyed directly from the menu it was attached to
        ContextMenuNew->GetMenu2()->HandleMenuMsg(uMsg, wParam, lParam); // this call occasionally crashes
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 11))
    {
        MenuNewExceptionHasOccured++;
        if (ContextMenuNew != NULL)
            ContextMenuNew->Release(); // substitute for calling ReleaseMenuNew
                                       //    ReleaseMenuNew();
    }
}

void CMainWindow::PostChangeOnPathNotification(const char* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE3("CMainWindow::PostChangeOnPathNotification(%s, %d)", path, includingSubdirs);

    HANDLES(EnterCriticalSection(&DispachChangeNotifCS));

    // add this notification to the array (for later processing)
    CChangeNotifData data;
    lstrcpyn(data.Path, path, MAX_PATH);
    data.IncludingSubdirs = includingSubdirs;
    ChangeNotifArray.Add(data);
    if (!ChangeNotifArray.IsGood())
        ChangeNotifArray.ResetState(); // ignore errors (at worst we won't refresh)

    // post a request to distribute path change notifications
    HANDLES(EnterCriticalSection(&TimeCounterSection));
    int t1 = MyTimeCounter++;
    HANDLES(LeaveCriticalSection(&TimeCounterSection));
    PostMessage(HWindow, WM_USER_DISPACHCHANGENOTIF, 0, t1);

    HANDLES(LeaveCriticalSection(&DispachChangeNotifCS));
}


void BroadcastConfigChanged()
{
    // Internal Viewer and Find: refresh all windows (for example after global font change)
    ViewerWindowQueue.BroadcastMessage(WM_USER_CFGCHANGED, 0, 0);
    FindDialogQueue.BroadcastMessage(WM_USER_CFGCHANGED, 0, 0);
}

void CMainWindow::FillViewModeMenu(CMenuPopup* popup, int firstIndex, int type)
{
    char buff[VIEW_NAME_MAX + 10];

    DWORD fistCMID;
    CFilesWindow* panel;

    switch (type)
    {
    case 0:
    {
        fistCMID = CM_ACTIVEMODE_1;
        panel = GetActivePanel();
        break;
    }

    case 1:
    {
        fistCMID = CM_LEFTMODE_1;
        panel = LeftPanel;
        break;
    }

    case 2:
    {
        fistCMID = CM_RIGHTMODE_1;
        panel = RightPanel;
        break;
    }

    default:
    {
        TRACE_E("Uknown type=" << type);
        return;
    }
    }

    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE |
               MENU_MASK_ID /*| MENU_MASK_SKILLLEVEL*/;
    mii.Type = MENU_TYPE_STRING | MENU_TYPE_RADIOCHECK;
    mii.String = buff;
    int i;
    for (i = 0; i < VIEW_TEMPLATES_COUNT; i++)
    {
        if (i == 0) // tree view is not shown yet
            continue;

        CViewTemplate* tmpl = &ViewTemplates.Items[i];
        if (tmpl->Name[0] != 0)
        {
            sprintf(buff, "%s\tAlt+%d", tmpl->Name, i < VIEW_TEMPLATES_COUNT - 1 ? i + 1 : 0);

            mii.ID = fistCMID + i;

            //      mii.SkillLevel = MENU_LEVEL_INTERMEDIATE | MENU_LEVEL_ADVANCED;
            //      if (i > 2)
            //        mii.SkillLevel |= MENU_LEVEL_BEGINNER;

            mii.State = panel->ViewTemplate == tmpl ? MENU_STATE_CHECKED : 0;

            popup->InsertItem(firstIndex, TRUE, &mii);
            firstIndex++;
        }
    }
}

void CMainWindow::SetDoNotLoadAnyPlugins(BOOL doNotLoad)
{
    if (doNotLoad)
    {
        DoNotLoadAnyPlugins = TRUE;
    }
    else
    {
        DoNotLoadAnyPlugins = FALSE;
        if (!CriticalShutdown)
        {
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            int t2 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));

            if (LeftPanel->GetViewMode() == vmThumbnails)
            {
                PostMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1); // ensure the icon cache is refilled (thumbnails can be shown again)
            }
            if (RightPanel->GetViewMode() == vmThumbnails)
            {
                PostMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t2); // ensure the icon cache is refilled (thumbnails can be shown again)
            }
        }
    }
}

void CMainWindow::ShowHideTwoDriveBarsInternal(BOOL show)
{
    LockWindowUpdate(HWindow);

    if (show)
    {
        REBARBANDINFO rbi;
        rbi.cbSize = sizeof(REBARBANDINFO);

        int count = (int)SendMessage(HTopRebar, RB_GETBANDCOUNT, 0, 0);
        // drive bar 1
        int index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR, 0);
        SendMessage(HTopRebar, RB_MOVEBAND, (WPARAM)index, (LPARAM)count - 1);
        rbi.fMask = RBBIM_STYLE;
        rbi.fStyle = RBBS_NOGRIPPER | RBBS_BREAK;
        SendMessage(HTopRebar, RB_SETBANDINFO, count - 1, (LPARAM)&rbi);

        // drive bar 2
        index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR2, 0);
        SendMessage(HTopRebar, RB_MOVEBAND, (WPARAM)index, (LPARAM)count - 1);
        rbi.fMask = RBBIM_STYLE;
        rbi.fStyle = RBBS_NOGRIPPER;
        SendMessage(HTopRebar, RB_SETBANDINFO, count - 1, (LPARAM)&rbi);
    }
    else
    {
        int index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR, 0);
        SendMessage(HTopRebar, RB_SHOWBAND, index, FALSE);

        index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR2, 0);
        SendMessage(HTopRebar, RB_SHOWBAND, index, FALSE);
    }

    LockWindowUpdate(NULL);
}

int CMainWindow::GetSplitBarWidth()
{
    if (MiddleToolBar != NULL && MiddleToolBar->HWindow != NULL)
        return 2 * SPLIT_LINE_WIDTH + MiddleToolBar->GetNeededWidth();
    else
        return SPLIT_LINE_WIDTH;
}

BOOL CMainWindow::IsPanelZoomed(BOOL leftPanel)
{
    if (leftPanel)
        return SplitPosition >= 0.99;
    else
        return SplitPosition <= 0.01;
}

void CMainWindow::ToggleSmartColumnMode(CFilesWindow* panel)
{
    if (panel->GetViewMode() == vmDetailed) // the panel must be running in detailed mode
    {
        if (panel->Columns.Count < 1)
            return;
        CColumn* column = &panel->Columns[0];
        BOOL leftPanel = (panel == LeftPanel);
        BOOL smartMode = !(!column->FixedWidth &&
                           (leftPanel && panel->ViewTemplate->LeftSmartMode ||
                            !leftPanel && panel->ViewTemplate->RightSmartMode));
        if (smartMode && column->FixedWidth)
        { // smart mode works only for elastic columns (must be changed in the view template)
            if (leftPanel)
                panel->ViewTemplate->Columns[0].LeftFixedWidth = 0;
            else
                panel->ViewTemplate->Columns[0].RightFixedWidth = 0;
        }
        if (leftPanel)
        {
            panel->ViewTemplate->LeftSmartMode = smartMode;
            LeftPanel->SelectViewTemplate(LeftPanel->GetViewTemplateIndex(), TRUE, FALSE, VALID_DATA_ALL, TRUE);
        }
        else
        {
            panel->ViewTemplate->RightSmartMode = smartMode;
            RightPanel->SelectViewTemplate(RightPanel->GetViewTemplateIndex(), TRUE, FALSE, VALID_DATA_ALL, TRUE);
        }
    }
}

BOOL CMainWindow::GetSmartColumnMode(CFilesWindow* panel)
{
    if (panel->Columns.Count < 1)
        return FALSE;
    CColumn* column = &panel->Columns[0];
    BOOL smartMode = (!column->FixedWidth &&
                      (panel == LeftPanel && panel->ViewTemplate->LeftSmartMode ||
                       panel == RightPanel && panel->ViewTemplate->RightSmartMode));
    return smartMode;
}

void CMainWindow::SafeHandleMenuChngDrvMsg2(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult)
{
    CALL_STACK_MESSAGE_NONE
    __try
    {
        IContextMenu3* contextMenu3 = NULL;
        *plResult = 0;
        if (uMsg == WM_MENUCHAR)
        {
            if (SUCCEEDED(ContextMenuChngDrv->QueryInterface(IID_IContextMenu3, (void**)&contextMenu3)))
            {
                contextMenu3->HandleMenuMsg2(uMsg, wParam, lParam, plResult);
                contextMenu3->Release();
                return;
            }
        }
        ContextMenuChngDrv->HandleMenuMsg(uMsg, wParam, lParam);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 3))
    {
    }
}

void CMainWindow::ApplyCommandLineParams(const CCommandLineParams* cmdLineParams, BOOL setActivePanelAndPanelPaths)
{
    if (setActivePanelAndPanelPaths)
    {
        // first set the active panel
        if (cmdLineParams->ActivatePanel == 1 && GetActivePanel() == RightPanel ||
            cmdLineParams->ActivatePanel == 2 && GetActivePanel() == LeftPanel)
        {
            ChangePanel(FALSE);
        }
        // then we can set the path in the active panel
        if (cmdLineParams->LeftPath[0] == 0 && cmdLineParams->RightPath[0] == 0 && cmdLineParams->ActivePath[0] != 0)
            GetActivePanel()->ChangeDir(cmdLineParams->ActivePath); // makes no sense to combine with setting the left/right panel
        else
        {
            if (cmdLineParams->LeftPath[0] != 0)
                LeftPanel->ChangeDir(cmdLineParams->LeftPath);
            if (cmdLineParams->RightPath[0] != 0)
                RightPanel->ChangeDir(cmdLineParams->RightPath);
        }
    }

    if (cmdLineParams->SetMainWindowIconIndex)
    {
        Configuration.MainWindowIconIndexForced = cmdLineParams->MainWindowIconIndex;
        SetWindowIcon();
    }
    if (cmdLineParams->SetTitlePrefix)
    {
        Configuration.UseTitleBarPrefixForced = TRUE;
        lstrcpyn(Configuration.TitleBarPrefixForced, cmdLineParams->TitlePrefix, TITLE_PREFIX_MAX);
        SetWindowTitle();
    }
}

BOOL CMainWindow::SHChangeNotifyInitialize()
{
    if (SHChangeNotifyRegisterID != 0)
    {
        TRACE_E("SHChangeNotifyRegisterID != 0");
        return FALSE;
    }

    LPITEMIDLIST pidl;
    if (!SUCCEEDED(SHGetSpecialFolderLocation(HWindow, CSIDL_DESKTOP, &pidl)))
    {
        TRACE_E("SHGetSpecialFolderLocation failed on CSIDL_DESKTOP");
        return FALSE;
    }

    SHChangeNotifyEntry entry;
    entry.pidl = pidl;
    entry.fRecursive = TRUE;

    // message WM_USER_SHCHANGENOTIFY, which will be delivered to us on notifications, crosses process boundaries
    // by using the constant SHCNRF_NewDelivery (also known as SHCNF_NO_PROXY) we assume responsibility
    // for accessing the memory passed with the message (via SHChangeNotification_Lock) and tell the OS not to
    // create proxy windows (note: a bug has been reported on XP where the proxy window is created but not destroyed):
    // http://groups.google.com/groups?selm=3CDFD449.6BA0CDB4%40ic.ac.uk&output=gplain
    //
    // through SHCNE_ASSOCCHANGED we receive notifications about association changes
    SHChangeNotifyRegisterID = SHChangeNotifyRegister(HWindow, SHCNRF_ShellLevel | SHCNRF_NewDelivery,
                                                      SHCNE_MEDIAINSERTED | SHCNE_MEDIAREMOVED | SHCNE_DRIVEREMOVED |
                                                          SHCNE_DRIVEADD | SHCNE_NETSHARE | SHCNE_NETUNSHARE |
                                                          SHCNE_DRIVEADDGUI | SHCNE_ASSOCCHANGED | SHCNE_UPDATEITEM,
                                                      WM_USER_SHCHANGENOTIFY,
                                                      1, &entry);

    // dealokace pidl
    IMalloc* alloc;
    if (SUCCEEDED(CoGetMalloc(1, &alloc)))
    {
        alloc->Free(pidl);
        alloc->Release();
    }

    return TRUE;
}

BOOL CMainWindow::SHChangeNotifyRelease()
{
    if (SHChangeNotifyRegisterID != 0)
    {
        SHChangeNotifyDeregister(SHChangeNotifyRegisterID);
        SHChangeNotifyRegisterID = 0;
    }
    return TRUE;
}

typedef WINSHELLAPI BOOL(WINAPI* FT_FileIconInit)(
    BOOL bFullInit);

BOOL CMainWindow::OnAssociationsChangedNotification(BOOL showWaitWnd)
{
    // tweak the icon size

    LoadSaveToRegistryMutex.Enter(); // users reported shrunken icons, see /viewtopic.php?t=638
    // this synchronization ensures that two Salamanders do not interfere with each other
    // unfortunately the trick with changing "Shell Icon Size" to rebuild the cache is used by many tools (including Tweak UI),
    // so if they refresh at the same time as Salamander, conflicts occur
    // we try to avoid this by postponing the following mess using IDT_ASSOCIATIONSCHNG

    HKEY hKey;
    if (HANDLES(RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", 0, KEY_READ | KEY_WRITE, &hKey)) == ERROR_SUCCESS)
    {
        // older SHELL32.DLL versions may not export this, fileIconInit will be NULL
        FT_FileIconInit fileIconInit = NULL;
        fileIconInit = (FT_FileIconInit)GetProcAddress(Shell32DLL, MAKEINTRESOURCE(660)); // no header available

        char size[50];
        BOOL deleteVal = FALSE;
        if (!GetValueAux(NULL, hKey, "Shell Icon Size", REG_SZ, size, 50))
        {
            // The values for the icon size are Shell Icon Size and
            // Shell Small Icon Size (both are stored as strings - not
            // DWORDs). You only need to change one of them to cause
            // the refresh to happen (typically the large icon size). If those
            // values don't exist, the shell uses the SM_CXICON metric
            // (GetSystemMetrics) as the default size for large icons, and
            // half of that for the small icon size. If you're trying to cause
            // a refresh and the registry entry doesn't exist, you can just
            // assume that the size is set to SM_CXICON.
            sprintf(size, "%d", GetSystemMetrics(SM_CXICON));
            deleteVal = TRUE;
        }
        int val = atoi(size);
        if (val > 0) // unfortunately (according to net) users set icon sizes randomly (72, 96, 128, etc.) so we cannot filter out "strange" sizes
        {
            IgnoreWM_SETTINGCHANGE = TRUE;

            sprintf(size, "%d", val - 1);
            SetValueAux(NULL, hKey, "Shell Icon Size", REG_SZ, size, -1);
            SendMessage(MainWindow->HWindow, WM_SETTINGCHANGE, SPI_SETICONMETRICS, (LPARAM) "WindowMetrics");
            if (fileIconInit != NULL)
                fileIconInit(FALSE);
            sprintf(size, "%d", val);
            SetValueAux(NULL, hKey, "Shell Icon Size", REG_SZ, size, -1);
            SendMessage(MainWindow->HWindow, WM_SETTINGCHANGE, SPI_SETICONMETRICS, (LPARAM) "WindowMetrics");
            if (fileIconInit != NULL)
                fileIconInit(TRUE);
            if (deleteVal)
                RegDeleteValue(hKey, "Shell Icon Size"); // clean up after ourselves
            HANDLES(RegCloseKey(hKey));

            IgnoreWM_SETTINGCHANGE = FALSE;
        }
    }

    LoadSaveToRegistryMutex.Leave();

    /*
  if (fileIconInit != NULL)
    fileIconInit(TRUE);

  // debug icon display
  SHFILEINFO shi;
  HIMAGELIST systemIL = (HIMAGELIST)SHGetFileInfo("C:\\TEST.QWE", 0, &shi, sizeof(shi),
                                       SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_SHELLICONSIZE);
  TRACE_I("systemIL="<<hex<<systemIL <<" index="<<dec<<shi.iIcon);
  if (systemIL != NULL)
  {
    HDC hDC = GetWindowDC(MainWindow->HWindow);
    ImageList_Draw(systemIL, shi.iIcon, hDC, 0, 0, ILD_NORMAL);
    ImageList_Draw(systemIL, shi.iIcon, hDC, 0, 25, ILD_NORMAL);
    ReleaseDC(MainWindow->HWindow, hDC);
  }
  */

    // our own associations refresh
    BOOL lCanDrawItems = LeftPanel->CanDrawItems;
    LeftPanel->CanDrawItems = FALSE;
    BOOL rCanDrawItems = RightPanel->CanDrawItems;
    RightPanel->CanDrawItems = FALSE;
    Associations.Release();
    Associations.ReadAssociations(showWaitWnd);
    LeftPanel->CanDrawItems = lCanDrawItems;
    RightPanel->CanDrawItems = rCanDrawItems;
    HANDLES(EnterCriticalSection(&TimeCounterSection));
    int t1 = MyTimeCounter++;
    int t2 = MyTimeCounter++;
    HANDLES(LeaveCriticalSection(&TimeCounterSection));
    SendMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
    SendMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t2);

    return TRUE;
}

void CMainWindow::RebuildDriveBarsIfNeeded(BOOL useDrivesMask, DWORD drivesMask, BOOL checkCloudStorages,
                                           DWORD cloudStoragesMask)
{
    if ((DriveBar != NULL && DriveBar->HWindow != NULL) || (DriveBar2 != NULL && DriveBar2->HWindow != NULL))
    {
        if (!useDrivesMask)
        {
            DWORD netDrives; // bit array of network drives
            GetNetworkDrives(netDrives, NULL);
            drivesMask = GetLogicalDrives() | netDrives;
        }

        CDriveBar* copyDrivesListFrom = NULL;
        if (DriveBar != NULL && DriveBar->HWindow != NULL)
        {
            if (DriveBar->GetCachedDrivesMask() != drivesMask ||
                checkCloudStorages && DriveBar->GetCachedCloudStoragesMask() != cloudStoragesMask)
            {
                // notifications about drive changes or cloud storage availability do not work; rebuild the drive bar manually
                TRACE_I("Forced drives rebuild for DriveBar!");
                DriveBar->RebuildDrives();
                copyDrivesListFrom = DriveBar;
            }
        }
        if (DriveBar2 != NULL && DriveBar2->HWindow != NULL)
        {
            if (DriveBar2->GetCachedDrivesMask() != drivesMask ||
                checkCloudStorages && DriveBar2->GetCachedCloudStoragesMask() != cloudStoragesMask)
            {
                // notifications about drive changes or cloud storage availability do not work; rebuild the drive bar manually
                TRACE_I("Forced drives rebuild for DriveBar2!");
                DriveBar2->RebuildDrives(copyDrivesListFrom);
            }
        }
    }
}

LRESULT
CMainWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CMainWindow::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CREATE:
    {
        // Register before creating worker-facing services so their notifications carry this window generation.
        DeleteManager.RegisterCallbackWindow(HWindow);
        SHChangeNotifyInitialize(); // request receiving Shell Notifications
        ExecLogStartupPhase("main window create");

        SetTimer(HWindow, IDT_ADDNEWMODULES, 15000, NULL); // timer after 15 seconds for AddNewlyLoadedModulesToGlobalModulesStore()

        CMWDropTarget* dropTarget = new CMWDropTarget();
        if (dropTarget != NULL)
        {
            HANDLES(RegisterDragDrop(HWindow, dropTarget));
            dropTarget->Release(); // RegisterDragDrop called AddRef()
        }

        HMENU h = GetSystemMenu(HWindow, FALSE);
        if (h != NULL)
        {
            int items = GetMenuItemCount(h);
            int pos = items; // append new items at the end of the menu

            // if the last two menu items are a separator and Close, insert above them
            // (users have long complained they accidentally click our AOT instead of the intended Close)
            if (items > 2)
            {
                UINT predLastCmd = GetMenuItemID(h, items - 2);
                UINT lastCmd = GetMenuItemID(h, items - 1);
                if (predLastCmd == 0 && lastCmd == SC_CLOSE)
                    pos = items - 2;
            }

            /* used by the export_mnu.py script which generates salmenu.mnu for Translator.
   Keep this synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM AddToSystemMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_ALWAYSONTOP
  {MNTT_PE, 0
};
*/
            InsertMenu(h, pos, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            InsertMenu(h, pos + 1, MF_BYPOSITION | MF_STRING | MF_ENABLED | (Configuration.AlwaysOnTop ? MF_CHECKED : MF_UNCHECKED),
                       CM_ALWAYSONTOP, LoadStr(IDS_ALWAYSONTOP));
        }
        SetWindowPos(HWindow,
                     Configuration.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

        HTopRebar = CreateWindowEx(WS_EX_TOOLWINDOW, REBARCLASSNAME, "",
                                   WS_VISIBLE | WS_BORDER | WS_CHILD |
                                       WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
                                       RBS_VARHEIGHT | CCS_NODIVIDER |
                                       RBS_BANDBORDERS | CCS_NOPARENTALIGN |
                                       RBS_AUTOSIZE,
                                   0, 0, 0, 0, // dummy
                                   HWindow, (HMENU)0, HInstance, NULL);
        if (HTopRebar == NULL)
        {
            TRACE_E("CreateWindowEx on " << REBARCLASSNAME);
            return -1;
        }

        // we do not want visual styles for the rebar
        // disable them
        SetWindowTheme(HTopRebar, (L" "), (L" "));

        // enforce WS_BORDER which somehow "disappeared"
        DWORD style = (DWORD)GetWindowLongPtr(HTopRebar, GWL_STYLE);
        style |= WS_BORDER;
        SetWindowLongPtr(HTopRebar, GWL_STYLE, style);

        MenuBar = new CMenuBar(&MainMenu, HWindow);
        if (MenuBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        if (!MenuBar->CreateWnd(HTopRebar))
            return -1;

        LeftPanel = new CFilesWindow(this);
        if (LeftPanel == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        if (!LeftPanel->Create(CWINDOW_CLASSNAME2, "",
                               WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                               0, 0, 0, 0,
                               HWindow,
                               NULL,
                               HInstance,
                               LeftPanel))
        {
            TRACE_E("LeftPanel->Create failed");
            return -1;
        }
        SetActivePanel(LeftPanel);
        //      ReleaseMenuNew();
        RightPanel = new CFilesWindow(this);
        if (RightPanel == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        if (!RightPanel->Create(CWINDOW_CLASSNAME2, "",
                                WS_VISIBLE | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                0, 0, 0, 0,
                                HWindow,
                                NULL,
                                HInstance,
                                RightPanel))
        {
            TRACE_E("RightPanel->Create failed");
            return -1;
        }

        EditWindow = new CEditWindow;
        if (EditWindow == NULL || !EditWindow->IsGood())
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }

        TopToolBar = new CMainToolBar(HWindow, mtbtTop);
        if (TopToolBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        TopToolBar->SetImageList(HGrayToolBarImageList);
        TopToolBar->SetHotImageList(HHotToolBarImageList);
        TopToolBar->SetStyle(TLB_STYLE_IMAGE | TLB_STYLE_ADJUSTABLE);
        TOOLBAR_PADDING padding;
        TopToolBar->GetPadding(&padding);
        padding.ToolBarVertical = 1;
        padding.IconLeft = 2;
        padding.IconRight = 3;
        TopToolBar->SetPadding(&padding);

        MiddleToolBar = new CMainToolBar(HWindow, mtbtMiddle);
        if (MiddleToolBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        MiddleToolBar->SetImageList(HGrayToolBarImageList);
        MiddleToolBar->SetHotImageList(HHotToolBarImageList);
        MiddleToolBar->SetStyle(TLB_STYLE_IMAGE | TLB_STYLE_ADJUSTABLE | TLB_STYLE_VERTICAL);
        MiddleToolBar->GetPadding(&padding);
        padding.ToolBarVertical = 1;
        padding.IconLeft = 2;
        padding.IconRight = 3;
        MiddleToolBar->SetPadding(&padding);

        PluginsBar = new CPluginsBar(HWindow);
        if (PluginsBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }

        //      AnimateBar = new CAnimate(HWorkerBitmap, 50, 0, RGB(255, 255, 255)); // 50 frames total, loop from 0, white background
        //      AnimateBar = new CAnimate(HWorkerBitmap, 43, 3, RGB(0, 0, 0)); // 43 frames total, loop from 3, black background
        //      if (AnimateBar == NULL)
        //      {
        //        TRACE_E(LOW_MEMORY);
        //        return -1;
        //      }
        //      if (!AnimateBar->IsGood())
        //        return -1;

        // User Menu Bar
        UMToolBar = new CUserMenuBar(HWindow);
        if (UMToolBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        UMToolBar->GetPadding(&padding);
        padding.IconLeft = 2;
        padding.IconRight = 3;
        padding.ButtonIconText = 2;
        padding.TextRight = 4;
        UMToolBar->SetPadding(&padding);

        // Hot Path Bar
        HPToolBar = new CHotPathsBar(HWindow);
        if (HPToolBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        HPToolBar->GetPadding(&padding);
        padding.IconLeft = 2;
        padding.IconRight = 3;
        padding.ButtonIconText = 2;
        padding.TextRight = 4;
        HPToolBar->SetPadding(&padding);

        // Drive Bar
        DriveBar = new CDriveBar(HWindow);
        if (DriveBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        DriveBar2 = new CDriveBar(HWindow);
        if (DriveBar2 == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }

        BottomToolBar = new CBottomToolBar(HWindow);
        if (BottomToolBar == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return -1;
        }
        BottomToolBar->SetImageList(HBottomTBImageList);
        BottomToolBar->SetHotImageList(HHotBottomTBImageList);

        TaskbarRestartMsg = RegisterWindowMessage(TEXT("TaskbarCreated"));

        Created = TRUE;
        ExecLogStartupComplete();
        return 0;
    }

    // case WM_CHANGEUISTATE: // it seems both messages always arrive
    case WM_UPDATEUISTATE:
    {
        if (MenuBar != NULL && MenuBar->HWindow != NULL)
            SendMessage(MenuBar->HWindow, WM_UPDATEUISTATE, wParam, lParam);
        // TRACE_I("KeyboardCuesAlwaysVisible="<<std::hex<<KeyboardCuesAlwaysVisible );
        break;
    }

    case WM_SYSCOLORCHANGE:
    {
        UserMenuIconBkgndReader.SetSysColorsChanged();

        // propagate the color change to the rebar
        if (HTopRebar != NULL)
            SendMessage(HTopRebar, uMsg, wParam, lParam);

        // the color depth may have changed - rebuild image lists to obtain new icons
        ColorsChanged(TRUE, FALSE, TRUE); // rebuild everything; we have enough time
        return 0;
    }

    case WM_SETTINGCHANGE:
    {
        if (IgnoreWM_SETTINGCHANGE || LeftPanel == NULL || RightPanel == NULL) // a bug report showed that WM_SETTINGCHANGE was delivered immediately from WM_CREATE of the main window (panels didn't exist yet, causing a NULL access)
            return 0;

        // detection based on EXPLORER.EXE on NT4
        if (lParam != 0 && stricmp((LPCTSTR)lParam, "Environment") == 0)
        {
            // environment variables changed, refresh them
            if (Configuration.ReloadEnvVariables)
                RegenEnvironmentVariables();
            return 0;
        }
        if (lParam != 0 && stricmp((LPCTSTR)lParam, "Extensions") == 0)
        {
            // file associations changed, refresh them
            // this path is probably no longer used, it's some old branch,
            // nowadays SHCNE_ASSOCCHANGED broadcasts the change, but NT4 Explorer
            // still handles this branch

            // delay one second so we don't collide with other software using the icon size change trick to reset the icon cache
            if (!SetTimer(HWindow, IDT_ASSOCIATIONSCHNG, 1000, NULL))
                OnAssociationsChangedNotification(FALSE);
            return 0;
        }

        // unknown change, rebuild everything

        GotMouseWheelScrollLines = FALSE; // reload number of lines for wheel scrolling
        InitLocales();
        SetFont(); // panel font follows the system font by default
        SetEnvFont();

        GetShortcutOverlay();
        // Internal Viewer and Find: refresh all windows (font already changed)
        BroadcastConfigChanged();
        if (!IsIconic(HWindow))
        {
            // ensure child windows are laid out again - toolbar sizes may have changed
            RECT wr;
            GetWindowRect(HWindow, &wr);
            int width = wr.right - wr.left;
            int height = wr.bottom - wr.top;
            SetWindowPos(HWindow, NULL, 0, 0, width + 1, height + 1,
                         SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
            SetWindowPos(HWindow, NULL, 0, 0, width, height,
                         SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
        }
        LeftPanel->RefreshListBox(-1, -1, LeftPanel->FocusedIndex, FALSE, FALSE);
        RightPanel->RefreshListBox(-1, -1, RightPanel->FocusedIndex, FALSE, FALSE);
        RefreshDiskFreeSpace();

        // the font changed; notify plugins so their toolbars and menu bars call SetFont()
        Plugins.Event(PLUGINEVENT_SETTINGCHANGE, 0);

        return 0;
    }

    case WM_USER_SHCHANGENOTIFY: // received thanks to SHChangeNotifyRegister
    {
        LONG wEventId;
        HANDLE hLock = NULL;

        //      TRACE_E("WM_USER_SHCHANGENOTIFY lParam="<<hex<<lParam<<" wParam="<<hex<<wParam);

        // with newer shell32.dll we must request access to mapped memory containing the parameters
        // (memory cannot be passed between processes and this message came from Explorer)
        // see doc\interesting.zip\Shell Notifications.mht (http://www.geocities.com/SiliconValley/4942/notify.html)

        LPITEMIDLIST* ppidl;
        hLock = SHChangeNotification_Lock((HANDLE)wParam, (DWORD)lParam, &ppidl, &wEventId); // FIXME_X64 - verify casting to (DWORD)
        if (hLock == NULL)
        {
            TRACE_E("SHChangeNotification_Lock failed");
            break;
        }

        // convert PIDL to a path
        char szPath[2 * MAX_PATH];
        szPath[0] = 0; // an empty path means everything changed
        if (ppidl != NULL)
        {
            switch (wEventId)
            {
            case SHCNE_CREATE:
            case SHCNE_DELETE:
            case SHCNE_MKDIR:
            case SHCNE_RMDIR:
            case SHCNE_MEDIAINSERTED:
            case SHCNE_MEDIAREMOVED:
            case SHCNE_DRIVEREMOVED:
            case SHCNE_DRIVEADD:
            case SHCNE_NETSHARE:
            case SHCNE_NETUNSHARE:
            case SHCNE_ATTRIBUTES:
            case SHCNE_UPDATEDIR:
            case SHCNE_UPDATEITEM:
            case SHCNE_SERVERDISCONNECT:
            case SHCNE_DRIVEADDGUI:
            case SHCNE_EXTENDED_EVENT:
            {
                if (!SHGetPathFromIDList(ppidl[0], szPath))
                    szPath[0] = 0;
                break;
            }
            }
        }
        SHChangeNotification_Unlock(hLock); // ppidl is translated, we can free the memory
        ppidl = NULL;

        if (wEventId == SHCNE_UPDATEITEM)
        {
            //        TRACE_I("SHCNE_UPDATEITEM: " << szPath);
            if (LeftPanel != NULL && RightPanel != NULL)
            {
                LeftPanel->IconOverlaysChangedOnPath(szPath);
                RightPanel->IconOverlaysChangedOnPath(szPath);
                if (CutDirectory(szPath))
                {
                    LeftPanel->IconOverlaysChangedOnPath(szPath);
                    RightPanel->IconOverlaysChangedOnPath(szPath);
                }
            }
        }
        else
        {
            if (wEventId == SHCNE_ASSOCCHANGED)
            {
                // change in associations
                // delay one second so we don't collide with other software using the icon size change trick to reset the icon cache
                if (!SetTimer(HWindow, IDT_ASSOCIATIONSCHNG, 1000, NULL))
                    OnAssociationsChangedNotification(FALSE);
            }
            else
            {
                // change in media or drives

                // after media insertion, automatically perform Retry in the "drive not ready" message box
                // (if it is displayed for the drive with inserted media)
                if (wEventId == SHCNE_MEDIAINSERTED)
                {
                    if (CheckPathRootWithRetryMsgBox[0] != 0 &&
                        HasTheSameRootPath(CheckPathRootWithRetryMsgBox, szPath))
                    {
                        if (LastDriveSelectErrDlgHWnd != NULL)
                            PostMessage(LastDriveSelectErrDlgHWnd, WM_COMMAND, IDRETRY, 0);
                    }
                }

                // if the Alt+F1/F2 menu is open, refresh (read the volume name)
                CFilesWindow* panel = GetActivePanel();
                if (panel != NULL)
                    PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);

                // if the panels show CD-ROM or removable media, refresh them
                while (1)
                {
                    if ((panel->Is(ptDisk) || panel->Is(ptZIPArchive)) && !IsUNCPath(panel->GetPath()))
                    {
                        UINT type = MyGetDriveType(panel->GetPath());
                        if (type == DRIVE_CDROM || type == DRIVE_REMOVABLE)
                        {
                            HANDLES(EnterCriticalSection(&TimeCounterSection)); // capture the time when a refresh is needed
                            int t1 = MyTimeCounter++;
                            HANDLES(LeaveCriticalSection(&TimeCounterSection));
                            PostMessage(panel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
                        }
                        if (type == DRIVE_NO_ROOT_DIR) // device disappeared (the drive is invalid)
                        {
                            if (LeftPanel == panel)
                            {
                                if (!ChangeLeftPanelToFixedWhenIdleInProgress)
                                    ChangeLeftPanelToFixedWhenIdle = TRUE;
                            }
                            else
                            {
                                if (!ChangeRightPanelToFixedWhenIdleInProgress)
                                    ChangeRightPanelToFixedWhenIdle = TRUE;
                            }
                        }
                    }
                    if (panel != GetNonActivePanel())
                        panel = GetNonActivePanel();
                    else
                        break;
                }
            }
        }
        break;
    }

        /*
    // WM_DEVICECHANGE didn't work well, for example under Win XP when connecting the DSC F707 camera.
    // A notification about device connection arrived, but the subsequent device name detection
    // (if the Alt+F1/2 menu was displayed) via SHGetFileInfo returned an empty string.
    // I found a thread on Google where someone complains about the same problem
    //
    // http://groups.google.com/groups?hl=en&lr=&ie=UTF-8&oe=UTF-8&threadm=99a435fa.0203280715.69a286a8%40posting.
    // google.com&rnum=1&prev=/groups%3Fhl%3Den%26lr%3D%26ie%3DUTF-8%26oe%3DUTF-8%26q%3Ddevice%2Bname%2Bshgetfileinfo
    //
    // and he solved it with a wait. People recommended abandoning WM_DEVICECHANGE and switching to
    // the undocumented function SHChangeNotifyRegister...
    // (http://www.geocities.com/SiliconValley/4942/notify.html)
    case WM_DEVICECHANGE:
    {
      if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE ||
          wParam == DBT_CONFIGCHANGED)  // CD-ROM media change
      {
        // if the Alt+F1/F2 menu is open, refresh (read volume name)
        CFilesWindow *panel = GetActivePanel();
        if (panel != NULL)
          PostMessage(MainWindow->HWindow, WM_USER_DRIVES_CHANGE, 0, 0);

        // if the panels show CD-ROM or removable media, refresh them
        while (1)
        {
          if (panel->Is(ptDisk) || panel->Is(ptZIPArchive))
          {
            UINT type = MyGetDriveType(panel->GetPath());
            if (type == DRIVE_CDROM || type == DRIVE_REMOVABLE)
            {
              HANDLES(EnterCriticalSection(&TimeCounterSection));  // capture the time when a refresh is needed
              int t1 = MyTimeCounter++;
              HANDLES(LeaveCriticalSection(&TimeCounterSection));
              PostMessage(panel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            }
          }
          if (panel != GetNonActivePanel()) panel = GetNonActivePanel();
          else break;
        }
      }
      break;
    }
    */

    case WM_USER_PROCESSDELETEMAN:
    {
        // A queued worker notification is valid only for the still-registered main-window generation.
        if (!DeleteManager.IsCurrentCallbackWindow(HWindow, (DWORD)wParam))
            return 0;
        // delay data processing due to the main window activation after ESC from the viewer on WinXP;
        // without this hack, it somehow did not catch up - the main window stayed inactive and the safe-wait window never appeared
        if (!SetTimer(HWindow, IDT_DELETEMNGR_PROCESS, 200, NULL))
            DeleteManager.ProcessData(); // if the timer fails, run immediately; forget about WinXP
        return 0;
    }

    case WM_USER_DRIVES_CHANGE:
    {
        CFilesWindow* panel = GetActivePanel();
        if (panel->OpenedDrivesList != NULL)
        {
            // rebuild the menu
            panel->OpenedDrivesList->RebuildMenu();
        }
        CDriveBar* copyDrivesListFrom = NULL;
        if (DriveBar != NULL && DriveBar->HWindow != NULL)
        {
            DriveBar->RebuildDrives();
            copyDrivesListFrom = DriveBar;
        }
        if (DriveBar2 != NULL && DriveBar2->HWindow != NULL)
            DriveBar2->RebuildDrives(copyDrivesListFrom);
        return 0;
    }

    case WM_USER_ENTERMENULOOP:
    case WM_USER_LEAVEMENULOOP:
    {
        // turn off any tooltip
        SetCurrentToolTip(NULL, 0);

        // if someone is monitoring the mouse, end the monitoring
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_QUERY;
        if (TrackMouseEvent(&tme) && tme.hwndTrack != NULL)
            SendMessage(tme.hwndTrack, WM_MOUSELEAVE, 0, 0);

        // let the existing caret hide (or show again) so it does not distract the user
        CancelPanelsUI(); // cancel QuickSearch and QuickEdit
        if (EditMode)
        {
            if (uMsg == WM_USER_ENTERMENULOOP)
                EditWindow->HideCaret();
            else
                EditWindow->ShowCaret();
        }

        if (uMsg == WM_USER_ENTERMENULOOP)
            UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
        else
            UserMenuIconBkgndReader.EndUserMenuIconsInUse();

        // Ensure the enablers are set correctly so enabled items in the menu reflect
        // the real state. Also update the bottom toolbar status.
        OnEnterIdle();
        return 0;
    }

    case WM_USER_TBDROPDOWN:
    {
        CToolBar* tlb = (CToolBar*)WindowsManager.GetWindowPtr((HWND)wParam);
        if (tlb == NULL)
            return 0;
        int index = (int)lParam;
        TLBI_ITEM_INFO2 tii;
        tii.Mask = TLBI_MASK_ID;
        if (!tlb->GetItemInfo2(index, TRUE, &tii))
            return 0;

        DWORD id = tii.ID;

        RECT r;
        tlb->GetItemRect(index, r);

        switch (id)
        {
        case CM_LCHANGEDRIVE:
        case CM_RCHANGEDRIVE:
        {
            SendMessage(HWindow, WM_COMMAND, id, 0);
            break;
        }

        case CM_OPENHOTPATHSDROP:
        {
            CMenuPopup menu;
            HotPaths.FillHotPathsMenu(&menu, CM_ACTIVEHOTPATH_MIN);
            menu.Track(0, r.left, r.bottom, HWindow, &r);
            break;
        }

        case CM_USERMENUDROP:
        {
            UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
            CMenuPopup menu;
            FillUserMenu(&menu);
            // another lock/unlock cycle (BeginUserMenuIconsInUse + EndUserMenuIconsInUse)
            // will occur in WM_USER_ENTERMENULOOP + WM_USER_LEAVEMENULOOP, but
            // it is nested and lightweight, so we ignore it and do not fight it
            menu.Track(0, r.left, r.bottom, HWindow, &r);
            UserMenuIconBkgndReader.EndUserMenuIconsInUse();
            break;
        }

        case CM_NEWDROP:
        {
            CMenuPopup menu(CML_FILES_NEW);
            menu.Track(0, r.left, r.bottom, HWindow, &r);
            break;
        }

        case CM_OPEN_FOLDER_DROP:
        {
            CMenuPopup menu;

            CGUIMenuPopupAbstract* popup = MainMenu.GetSubMenu(CML_COMMANDS, FALSE);
            if (popup != NULL)
            {
                popup = popup->GetSubMenu(CML_COMMANDS_FOLDERS, FALSE);
                if (popup != NULL)
                    popup->Track(0, r.left, r.bottom, HWindow, &r);
            }
            break;
        }

        case CM_ACTIVEBACK:
        case CM_ACTIVEFORWARD:
        case CM_LBACK:
        case CM_LFORWARD:
        case CM_RBACK:
        case CM_RFORWARD:
        {
            BOOL forward = id == CM_ACTIVEFORWARD || id == CM_LFORWARD || id == CM_RFORWARD;

            CMenuPopup menu;
            CFilesWindow* panel = GetActivePanel();
            if (id == CM_LBACK || id == CM_LFORWARD)
                panel = LeftPanel;
            if (id == CM_RBACK || id == CM_RFORWARD)
                panel = RightPanel;
            panel->PathHistory->FillBackForwardPopupMenu(&menu, forward);
            DWORD cmd = menu.Track(MENU_TRACK_RETURNCMD,
                                   r.left, r.bottom,
                                   HWindow, &r);
            if (cmd != 0)
                panel->PathHistory->Execute(cmd, forward, panel);
            break;
        }

        case CM_ACTIVEVIEWMODE:
        case CM_LEFTVIEWMODE:
        case CM_RIGHTVIEWMODE:
        {
            CMenuPopup menu;
            int type = 0;
            if (id == CM_LEFTVIEWMODE)
                type = 1;
            else if (id == CM_RIGHTVIEWMODE)
                type = 2;
            FillViewModeMenu(&menu, 0, type);
            menu.Track(0, r.left, r.bottom, HWindow, &r);
            break;
        }

        case CM_VIEW:
        case CM_EDIT:
        {
            CFilesWindow* activePanel = GetActivePanel();
            if (activePanel == NULL)
                break;

            CMenuPopup popup(id == CM_VIEW ? CML_FILES_VIEWWITH : 0);

            if (id == CM_VIEW)
                activePanel->FillViewWithMenu(&popup);
            else
                activePanel->FillEditWithMenu(&popup);

            popup.Track(0, r.left, r.bottom, HWindow, &r);
            break;
        }
        }

        if (id >= CM_USERMENU_MIN && id <= CM_USERMENU_MAX)
        {
            // user clicked a group in the User Menu Toolbar
            int iterator = id - CM_USERMENU_MIN;
            int endIndex = UserMenuItems->GetSubmenuEndIndex(iterator);
            if (endIndex != -1)
            {
                UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
                iterator++;
                CMenuPopup menu;
                FillUserMenu2(&menu, &iterator, endIndex);
                // another lock/unlock cycle (BeginUserMenuIconsInUse + EndUserMenuIconsInUse)
                // will occur in WM_USER_ENTERMENULOOP + WM_USER_LEAVEMENULOOP,
                // but it is nested and lightweight, so we ignore it
                menu.Track(0, r.left, r.bottom, HWindow, &r);
                UserMenuIconBkgndReader.EndUserMenuIconsInUse();
            }
        }

        if (id >= CM_PLUGINCMD_MIN && id <= CM_PLUGINCMD_MAX)
        {
            // user clicked on the plugin icon in the PluginsBar;
            int index2 = id - CM_PLUGINCMD_MIN; // index of the plugin in CPlugions::Data
            CMenuPopup menu(CML_PLUGINS_SUBMENU);
            if (Plugins.InitPluginMenuItemsForBar(HWindow, index2, &menu))
                menu.Track(0, r.left, r.bottom, HWindow, &r);
        }

        if (id >= CM_DRIVEBAR_MIN && id <= CM_DRIVEBAR_MAX)
            DriveBar->Execute(id);
        if (id >= CM_DRIVEBAR2_MIN && id <= CM_DRIVEBAR2_MAX)
            DriveBar2->Execute(id);
        return 0;
    }

    case WM_USER_REPAINTALLICONS:
    {
        if (LeftPanel != NULL)
            LeftPanel->RepaintIconOnly(-1); // all
        if (RightPanel != NULL)
            RightPanel->RepaintIconOnly(-1); // all
        return 0;
    }

    case WM_USER_REPAINTSTATUSBARS:
    {
        if (LeftPanel != NULL && LeftPanel->DirectoryLine != NULL)
            LeftPanel->DirectoryLine->InvalidateAndUpdate(FALSE);
        if (RightPanel != NULL && RightPanel->DirectoryLine != NULL)
            RightPanel->DirectoryLine->InvalidateAndUpdate(FALSE);
        return 0;
    }

    case WM_USER_SHOWWINDOW:
    {
        if (!SalamanderBusy)
        {
            SalamanderBusy = TRUE; // now BUSY
            LastSalamanderIdleTime = GetTickCount();
            BringWindowToTop(HWindow); // probably not important, but I saw it in a sample so I am adding it here too
            if (IsIconic(HWindow))
            {
                // SetForegroundWindow: this is crucial. If we don't call it and
                // "only one instance" with the tray is active, Salamander sometimes
                // appears in the background and only later moves to the front.
                SetForegroundWindow(HWindow);
                ShowWindow(HWindow, SW_RESTORE);
            }
            else
                SetForegroundWindow(HWindow);
        }
        return 0;
    }

    case WM_USER_SKIPONEREFRESH:
    {
        if (!SetTimer(NULL, 0, 500, SkipOneARTimerProc))
        {
            SkipOneActivateRefresh = FALSE;
        }
        return 0;
    }

        /*
    case WM_USER_SETPATHS:
    {
      if (!SalamanderBusy && MainWindow != NULL && MainWindow->CanClose)  // not BUSY and already started, otherwise ignore requests from other processes
      {
        SalamanderBusy = TRUE;   // now BUSY
        LastSalamanderIdleTime = GetTickCount();
        CSetPathsParams params;
        ZeroMemory(&params, sizeof(params)); // default values
        HANDLE sendingProcess = HANDLES_Q(OpenProcess(PROCESS_DUP_HANDLE, FALSE, wParam));
        HANDLE sendingFM = (HGLOBAL)lParam;

        HANDLE fm;
        BOOL alreadyDone = FALSE;
        if (sendingProcess != NULL &&
            HANDLES(DuplicateHandle(sendingProcess, sendingFM,          // sending-process file-mapping
                                    GetCurrentProcess(), &fm,           // this process file-mapping
                                    0, FALSE, DUPLICATE_SAME_ACCESS)))
        {
          CSetPathsParams *unsafe = (CSetPathsParams *)HANDLES(MapViewOfFile(fm, FILE_MAP_WRITE, 0, 0, sizeof(CSetPathsParams))); // FIXME_X64 are we passing x86/x64 incompatible data?
          if (unsafe != NULL)
          {
            alreadyDone = unsafe->Received;
            if (!alreadyDone)
            {
              lstrcpyn(params.LeftPath, unsafe->LeftPath, MAX_PATH);
              lstrcpyn(params.RightPath, unsafe->RightPath, MAX_PATH - 1);

              if (unsafe->MagicSignature1 == 0x07f2ab13 && unsafe->MagicSignature2 == 0x471e0901)
              {
                // new features since 2.52
                // WORD version = unsafe->StructVersion; // not used yet, the first version is recognized by the presence of signatures
                lstrcpyn(params.ActivePath, unsafe->ActivePath, MAX_PATH);
                params.ActivatePanel = unsafe->ActivatePanel;
              }
              // we return the result value having taken over the data
              unsafe->Received = TRUE;
            }
            HANDLES(UnmapViewOfFile(unsafe));
          }
          HANDLES(CloseHandle(fm));
        }
        if (sendingProcess != NULL) HANDLES(CloseHandle(sendingProcess));

        if (!alreadyDone)
          ApplyCommandLineParams(&params);
      }
      return 0;
    }
*/

    case WM_USER_AUTOCONFIG:
    {
        PackAutoconfig(HWindow);
        return 0;
    }

    case WM_USER_VIEWERCONFIG:
    {
        if (GetForegroundWindow() != HWindow)
            SetForegroundWindow(HWindow); // so we rise above the viewer
        WindowProc(WM_USER_CONFIGURATION, 3, 0);
        HWND hCaller = (HWND)wParam;
        if (IsWindow(hCaller))
        {
            // If the window that invoked us still exists, try to bring it to
            // the foreground. This is a bit dirty because if it opens a modal
            // dialog in the meantime, it won't get activation. But I don't care,
            // the viewer will (hopefully) end up inside Salamander - in the plugin ;-)
            SetForegroundWindow(hCaller);
        }
        return 0;
    }

    case WM_USER_CONFIGURATION:
    {
        if (!SalamanderBusy)
        {
            SalamanderBusy = TRUE; // now BUSY
            LastSalamanderIdleTime = GetTickCount();
        }

        BeginStopRefresh(); // snooper takes a break

        BOOL oldStatusArea = Configuration.StatusArea;
        BOOL oldPanelCaption = Configuration.ShowPanelCaption;
        BOOL oldPanelZoom = Configuration.ShowPanelZoom;

        UserMenuIconBkgndReader.ResetSysColorsChanged(); // now, we start watching system color changes (icon reload required)
        BOOL readingUMIcons = UserMenuIconBkgndReader.IsReadingIcons();
        if (readingUMIcons) // new icons are on their way to the user menu; show them after configuration is done (on OK reload icons again so newly added ones are read as well)
            UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
        BOOL oldUseCustomPanelFont = UseCustomPanelFont;
        LOGFONT oldLogFont = LogFont;
        CConfigurationDlg dlg(HWindow, UserMenuItems, (int)wParam, (int)lParam);
        int res = dlg.Execute(LoadStr(IDS_BUTTON_OK), LoadStr(IDS_BUTTON_CANCEL),
                              LoadStr(IDS_BUTTON_HELP));
        if (readingUMIcons)
            UserMenuIconBkgndReader.EndUserMenuIconsInUse();

        // dialog closed - the user could have changed the clipboard, check it
        IdleRefreshStates = TRUE;  // force status variable check on next Idle
        IdleCheckClipboard = TRUE; // also check the clipboard

        if (res == IDOK) // values changed -> refresh everything possible
        {
            if (dlg.PageView.IsDirty())
            {
                // user changed something in the view configuration - rebuild the columns
                LeftPanel->SelectViewTemplate(LeftPanel->GetViewTemplateIndex(), TRUE, FALSE);
                RightPanel->SelectViewTemplate(RightPanel->GetViewTemplateIndex(), TRUE, FALSE);
            }
            if (memcmp(&oldLogFont, &LogFont, sizeof(LogFont)) != 0 ||
                oldUseCustomPanelFont != UseCustomPanelFont)
            {
                SetFont();
                // if the header line is shown, we must set its correct size
                LeftPanel->LayoutListBoxChilds();
                RightPanel->LayoutListBoxChilds();
            }

            if (Configuration.ThumbnailSize != LeftPanel->GetThumbnailSize() ||
                Configuration.ThumbnailSize != RightPanel->GetThumbnailSize())
            {
                // if the thumbnail size changed, it must be propagated to the panels
                LeftPanel->SetThumbnailSize(Configuration.ThumbnailSize);
                RightPanel->SetThumbnailSize(Configuration.ThumbnailSize);
            }

            if (oldStatusArea != Configuration.StatusArea)
            {
                if (Configuration.StatusArea)
                    AddTrayIcon();
                else
                    RemoveTrayIcon();
            }

            if (UMToolBar != NULL && UMToolBar->HWindow != NULL)
                UMToolBar->CreateButtons();

            if (HPToolBar != NULL && HPToolBar->HWindow != NULL)
                HPToolBar->CreateButtons();

            if (Windows7AndLater)
                CreateJumpList();

            // the user could have enabled/disabled Documents
            CDriveBar* copyDrivesListFrom = NULL;
            if (DriveBar != NULL && DriveBar->HWindow != NULL)
            {
                DriveBar->RebuildDrives(DriveBar); // we don't need slow drive enumeration
                copyDrivesListFrom = DriveBar;
            }
            if (DriveBar2 != NULL && DriveBar2->HWindow != NULL)
                DriveBar2->RebuildDrives(copyDrivesListFrom);

            if (oldPanelCaption != Configuration.ShowPanelCaption || oldPanelZoom != Configuration.ShowPanelZoom)
            {
                if (LeftPanel->DirectoryLine != NULL && LeftPanel->DirectoryLine->HWindow != NULL)
                    LeftPanel->DirectoryLine->Repaint();
                if (RightPanel->DirectoryLine != NULL && RightPanel->DirectoryLine->HWindow != NULL)
                    RightPanel->DirectoryLine->Repaint();
            }

            // main window icon
            SetWindowIcon();
            // icon in progress windows
            ProgressDlgArray.PostIconChange();

            // tell both panels they need to refresh
            LeftPanel->RefreshForConfig();
            RightPanel->RefreshForConfig();

            // clear stored data in SalShExtPastedData (the archiver may have changed)
            SalShExtPastedData.ReleaseStoredArchiveData();

            // Internal Viewer and Find: refresh all windows (font already changed)
            BroadcastConfigChanged();

            // distribute this news among plugins as well
            Plugins.Event(PLUGINEVENT_CONFIGURATIONCHANGED, 0);

            // Persist the accepted dialog changes now; exit-time saving is only a final safeguard.
            SaveConfig();
        }

        EndStopRefresh(); // snooper starts again now
        return 0;
    }

    case WM_SYSCOMMAND:
    {
        if (HasLockedUI())
            break;

        // if the user pressed the Alt button while the initial splash window was shown,
        // the system menu could be entered before MainWindow appeared and the splash
        // window remained open until the user pressed Escape
        // if MainWindow is not yet visible, disable entering the Window menu
        if (wParam == SC_KEYMENU && !IsWindowVisible(HWindow))
            return 0;

        // set status bar as appropriate
        UINT nItemID = wParam != CM_ALWAYSONTOP ? ((UINT)wParam & 0xFFF0) : (UINT)wParam;

        // don't interfere with system commands if not in help mode
        if (HelpMode)
        {
            switch (nItemID)
            {
            case SC_SIZE:
            case SC_MOVE:
            case SC_MINIMIZE:
            case SC_MAXIMIZE:
            case SC_NEXTWINDOW:
            case SC_PREVWINDOW:
            case SC_CLOSE:
            case SC_RESTORE:
            case SC_TASKLIST:
            {
                OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, IDH_SYSMENUCMDS, FALSE);
                return 0;
            }

            case CM_ALWAYSONTOP:
            {
                OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, nItemID, FALSE);
                return 0;
            }
            }
        }

        if (wParam == CM_ALWAYSONTOP)
            WindowProc(WM_COMMAND, wParam, lParam); // pass it on

        if (Configuration.StatusArea && wParam == SC_MINIMIZE)
        {
            ShowWindow(HWindow, SW_MINIMIZE);
            ShowWindow(HWindow, SW_HIDE);
            return 0;
        }
        break;
    }

    case WM_USER_FLASHWINDOW:
    {
        FlashWindow(HWindow, TRUE);
        Sleep(100);
        FlashWindow(HWindow, FALSE);
        return 0;
    }

    case WM_APPCOMMAND:
    {
        // we catch messages coming especially from newer mice (4th button and above)
        // and multimedia keyboards
        // viz /viewtopic.php?t=192
        DWORD cmd = GET_APPCOMMAND_LPARAM(lParam);
        switch (cmd)
        {
        case APPCOMMAND_BROWSER_BACKWARD:
        {
            SendMessage(HWindow, WM_COMMAND, CM_ACTIVEBACK, 0);
            return TRUE;
        }

        case APPCOMMAND_BROWSER_FORWARD:
        {
            SendMessage(HWindow, WM_COMMAND, CM_ACTIVEFORWARD, 0);
            return TRUE;
        }
        }
        break;
    }

    case WM_COMMAND:
        // Menu and accelerator commands commit settings; control notifications are excluded to avoid saves while typing.
        if (lParam == 0)
            ScheduleConfigSave();
        // Command dispatch extracted to HandleWmCommand() in mainwnd_commands.cpp.
        return HandleWmCommand(wParam, lParam);


    case WM_USER_DISPACHCHANGENOTIF:
    {
        if (LastDispachChangeNotifTime < lParam) // not an outdated message
        {
            if (IsInPlugin() || StopRefresh > 0)
                NeedToResentDispachChangeNotif = TRUE;
            else
            {
                char path[MAX_PATH];
                BOOL includingSubdirs;
                BOOL ok = TRUE;
                while (1)
                {
                    HANDLES(EnterCriticalSection(&DispachChangeNotifCS));
                    if (ChangeNotifArray.Count > 0)
                    {
                        CChangeNotifData* item = &ChangeNotifArray[ChangeNotifArray.Count - 1];
                        strcpy(path, item->Path);
                        includingSubdirs = item->IncludingSubdirs;
                        ChangeNotifArray.Delete(ChangeNotifArray.Count - 1);
                        if (!ChangeNotifArray.IsGood())
                        {
                            ChangeNotifArray.ResetState();
                            ChangeNotifArray.DestroyMembers();
                            ChangeNotifArray.ResetState();
                            ok = FALSE;
                        }
                    }
                    else
                        ok = FALSE;
                    if (!ok) // store the time of the last refresh (still in the critical section)
                    {
                        HANDLES(EnterCriticalSection(&TimeCounterSection));
                        LastDispachChangeNotifTime = MyTimeCounter++;
                        HANDLES(LeaveCriticalSection(&TimeCounterSection));
                    }
                    HANDLES(LeaveCriticalSection(&DispachChangeNotifCS));

                    if (ok) // distribute a notification about the change on 'path' with 'includingSubdirs'
                    {
                        // send the message to all loaded plugins
                        Plugins.AcceptChangeOnPathNotification(path, includingSubdirs);

                        if (GetNonActivePanel() != NULL) // non-active panel first (due to timestamps of subdirectory changes on NTFS)
                        {
                            GetNonActivePanel()->AcceptChangeOnPathNotification(path, includingSubdirs);
                        }
                        if (GetActivePanel() != NULL) // then the active panel
                        {
                            GetActivePanel()->AcceptChangeOnPathNotification(path, includingSubdirs);
                        }

                        if (DetachedFSList->Count > 0)
                        {
                            // for better input/output optimization with plugins, the EnterPlugin/LeavePlugin section
                            // is exported here (not inside the interface encapsulation)
                            EnterPlugin();
                            int i;
                            for (i = 0; i < DetachedFSList->Count; i++)
                            {
                                CPluginFSInterfaceEncapsulation* fs = DetachedFSList->At(i);
                                fs->AcceptChangeOnPathNotification(fs->GetPluginFSName(), path, includingSubdirs);
                            }
                            LeavePlugin();
                        }
                    }
                    else
                        break; // end of loop
                }
            }
        }
        return 0;
    }

    case WM_USER_DISPACHCFGCHANGE:
    {
        // broadcast a message about configuration changes to the plugins
        Plugins.Event(PLUGINEVENT_CONFIGURATIONCHANGED, 0);
        return 0;
    }

    case WM_USER_TBCHANGED:
    {
        HWND hToolBar = (HWND)wParam;
        if (TopToolBar != NULL && hToolBar == TopToolBar->HWindow)
        {
            TopToolBar->Save(Configuration.TopToolBar);
        }
        if (MiddleToolBar != NULL && hToolBar == MiddleToolBar->HWindow)
        {
            MiddleToolBar->Save(Configuration.MiddleToolBar);
        }
        if (LeftPanel->DirectoryLine->ToolBar != NULL && hToolBar == LeftPanel->DirectoryLine->ToolBar->HWindow)
        {
            LeftPanel->DirectoryLine->LayoutWindow();
            LeftPanel->DirectoryLine->ToolBar->Save(Configuration.LeftToolBar);
        }
        if (RightPanel->DirectoryLine->ToolBar != NULL && hToolBar == RightPanel->DirectoryLine->ToolBar->HWindow)
        {
            RightPanel->DirectoryLine->LayoutWindow();
            RightPanel->DirectoryLine->ToolBar->Save(Configuration.RightToolBar);
        }
        return FALSE; // we have no buttons
    }

    case WM_USER_TBENUMBUTTON2:
    {
        HWND hToolBar = (HWND)wParam;
        // we forward it to our toolbar
        if (TopToolBar != NULL && hToolBar == TopToolBar->HWindow)
            return TopToolBar->OnEnumButton(lParam);
        if (MiddleToolBar != NULL && hToolBar == MiddleToolBar->HWindow)
            return MiddleToolBar->OnEnumButton(lParam);
        if (LeftPanel->DirectoryLine->ToolBar != NULL && hToolBar == LeftPanel->DirectoryLine->ToolBar->HWindow)
            return LeftPanel->DirectoryLine->ToolBar->OnEnumButton(lParam);
        if (RightPanel->DirectoryLine->ToolBar != NULL && hToolBar == RightPanel->DirectoryLine->ToolBar->HWindow)
            return RightPanel->DirectoryLine->ToolBar->OnEnumButton(lParam);
        return FALSE; // we have no buttons
    }

    case WM_USER_TBRESET:
    {
        HWND hToolBar = (HWND)wParam;
        // forward to our toolbar
        if (TopToolBar != NULL && hToolBar == TopToolBar->HWindow)
            TopToolBar->OnReset();
        if (MiddleToolBar != NULL && hToolBar == MiddleToolBar->HWindow)
            MiddleToolBar->OnReset();
        if (LeftPanel->DirectoryLine->ToolBar != NULL && hToolBar == LeftPanel->DirectoryLine->ToolBar->HWindow)
            LeftPanel->DirectoryLine->ToolBar->OnReset();
        if (RightPanel->DirectoryLine->ToolBar != NULL && hToolBar == RightPanel->DirectoryLine->ToolBar->HWindow)
            RightPanel->DirectoryLine->ToolBar->OnReset();
        return FALSE; // we have no buttons
    }

    case WM_USER_TBGETTOOLTIP:
    {
        HWND hToolBar = (HWND)wParam;
        // we forward it to our toolbar
        if (TopToolBar != NULL && hToolBar == TopToolBar->HWindow)
            TopToolBar->OnGetToolTip(lParam);
        if (MiddleToolBar != NULL && hToolBar == MiddleToolBar->HWindow)
            MiddleToolBar->OnGetToolTip(lParam);
        if (PluginsBar != NULL && hToolBar == PluginsBar->HWindow)
            PluginsBar->OnGetToolTip(lParam);
        if (UMToolBar != NULL && hToolBar == UMToolBar->HWindow)
            UMToolBar->OnGetToolTip(lParam);
        if (HPToolBar != NULL && hToolBar == HPToolBar->HWindow)
            HPToolBar->OnGetToolTip(lParam);
        if (DriveBar != NULL && hToolBar == DriveBar->HWindow)
            DriveBar->OnGetToolTip(lParam);
        if (DriveBar2 != NULL && hToolBar == DriveBar2->HWindow)
            DriveBar2->OnGetToolTip(lParam);
        if (LeftPanel->DirectoryLine->ToolBar != NULL && hToolBar == LeftPanel->DirectoryLine->ToolBar->HWindow)
            LeftPanel->DirectoryLine->ToolBar->OnGetToolTip(lParam);
        if (RightPanel->DirectoryLine->ToolBar != NULL && hToolBar == RightPanel->DirectoryLine->ToolBar->HWindow)
            RightPanel->DirectoryLine->ToolBar->OnGetToolTip(lParam);
        if (BottomToolBar != NULL && hToolBar == BottomToolBar->HWindow)
            BottomToolBar->OnGetToolTip(lParam);
        return FALSE; // we have no buttons
    }

    case WM_USER_TBENDADJUST:
    {
        // some toolbar was configured - force an update
        IdleForceRefresh = TRUE;
        IdleRefreshStates = TRUE;
        return 0;
    }

    case WM_USER_LEAVEMENULOOP2:
    {
        // this message arrives after the command, so any New menu command has already been processed
        if (ContextMenuNew != NULL)
            ContextMenuNew->Release();
        return 0;
    }

    case WM_USER_UNINITMENUPOPUP:
    {
        CMenuPopup* popup = (CMenuPopup*)(CGUIMenuPopupAbstract*)wParam;
        WORD popupID = HIWORD(lParam);

        switch (popupID)
        {
        case CML_OPTIONS_PLUGINS:
        case CML_HELP_ABOUTPLUGINS:
        case CML_PLUGINS:
        case CML_PLUGINS_SUBMENU:
        case CML_FILES_VIEWWITH:
        {
            HIMAGELIST hIcons = popup->GetImageList();
            if (hIcons != NULL)
            {
                popup->SetImageList(NULL); // just to be safe, so the popup doesn't own an invalid handle
                ImageList_Destroy(hIcons);
            }
            hIcons = popup->GetHotImageList();
            if (hIcons != NULL)
            {
                popup->SetHotImageList(NULL); // just to be safe, so the popup doesn't own an invalid handle
                ImageList_Destroy(hIcons);
            }
            if (popupID == CML_PLUGINS) // closing the Plugins menu; dynamic icons can be freed (they are rebuilt before each next menu opening)
                Plugins.ReleasePluginDynMenuIcons();
            break;
        }

        case CML_FILES_NEW:
        {
            popup->SetTemplateMenu(NULL);
            EndStopRefresh(); // closed in WM_USER_UNINITMENUPOPUP/WM_USER_INITMENUPOPUP
            break;
        }
        }
        return 0;
    }

    case WM_USER_INITMENUPOPUP:
    {
        CMenuPopup* popup = (CMenuPopup*)(CGUIMenuPopupAbstract*)wParam;
        WORD popupID = HIWORD(lParam);

        switch (popupID)
        {
        case CML_LEFT:
        case CML_RIGHT:
        {
            BOOL left = popupID == CML_LEFT;

            popup->CheckItem(left ? CM_LCHANGEFILTER : CM_RCHANGEFILTER, FALSE,
                             (left ? LeftPanel : RightPanel)->FilterEnabled);

            DWORD firstID = left ? CML_LEFT_VIEWS1 : CML_RIGHT_VIEWS1;
            DWORD lastID = left ? CML_LEFT_VIEWS2 : CML_RIGHT_VIEWS2;
            // find the separator above and below the views
            int firstIndex = popup->FindItemPosition(firstID);
            int lastIndex = popup->FindItemPosition(lastID);
            if (firstIndex == -1 || lastIndex == -1)
            {
                TRACE_E("Requested items were not found");
            }
            else
            {
                // remove the current contents
                if (firstIndex + 1 < lastIndex - 1)
                    popup->RemoveItemsRange(firstIndex + 1, lastIndex - 1);

                // populate the list of views
                FillViewModeMenu(popup, firstIndex + 1, left ? 1 : 2);
            }
            break;
        }

        case CML_LEFT_GO:
        case CML_RIGHT_GO:
        {
            static int GO_ITEMS_COUNT = -1;

            int count = popup->GetItemCount();
            if (GO_ITEMS_COUNT == -1)
                GO_ITEMS_COUNT = count;

            if (count > GO_ITEMS_COUNT)
            {
                // remove the existing contents
                popup->RemoveItemsRange(GO_ITEMS_COUNT, count - 1);
            }

            // append hot paths, if any exist
            DWORD firstID = popupID == CML_LEFT_GO ? CM_LEFTHOTPATH_MIN : CM_RIGHTHOTPATH_MIN;
            HotPaths.FillHotPathsMenu(popup, firstID, FALSE, FALSE, FALSE, TRUE);

            // append directory history, at most 10 items
            firstID = popupID == CML_LEFT_GO ? CM_LEFTHISTORYPATH_MIN : CM_RIGHTHISTORYPATH_MIN;
            DirHistory->FillHistoryPopupMenu(popup, firstID, 10, TRUE);
            break;
        }

        case CML_LEFT_VISIBLE:
        {
            popup->CheckItem(CM_LEFTDIRLINE, FALSE, LeftPanel->DirectoryLine->HWindow != NULL);
            popup->EnableItem(CM_LEFTHEADER, FALSE, LeftPanel->GetViewMode() == vmDetailed);
            popup->CheckItem(CM_LEFTHEADER, FALSE, LeftPanel->GetViewMode() == vmDetailed && LeftPanel->HeaderLineVisible);
            popup->CheckItem(CM_LEFTSTATUS, FALSE, LeftPanel->StatusLine->HWindow != NULL);
            break;
        }

        case CML_RIGHT_VISIBLE:
        {
            popup->CheckItem(CM_RIGHTDIRLINE, FALSE, RightPanel->DirectoryLine->HWindow != NULL);
            popup->EnableItem(CM_RIGHTHEADER, FALSE, RightPanel->GetViewMode() == vmDetailed);
            popup->CheckItem(CM_RIGHTHEADER, FALSE, RightPanel->GetViewMode() == vmDetailed && RightPanel->HeaderLineVisible);
            popup->CheckItem(CM_RIGHTSTATUS, FALSE, RightPanel->StatusLine->HWindow != NULL);
            break;
        }

        case CML_LEFT_SORTBY:
        case CML_RIGHT_SORTBY:
        {
            BOOL left = popupID == CML_LEFT_SORTBY;
            (left ? LeftPanel : RightPanel)->FillSortByMenu(popup);
            break;
        }

        case CML_FILES:
        {
            break;
        }

        case CML_EDIT:
        {
            // If this is a "change directory" paste operation, show it in the Paste item
            char text[220];
            char tail[50];
            tail[0] = 0;

            strcpy(text, LoadStr(IDS_MENU_EDIT_PASTE));

            CFilesWindow* activePanel = GetActivePanel();
            BOOL activePanelIsDisk = (activePanel != NULL && activePanel->Is(ptDisk));
            if (EnablerPastePath &&
                (!activePanelIsDisk || !EnablerPasteFiles) && // PasteFiles has higher priority
                !EnablerPasteFilesToArcOrFS)                  // PasteFilesToArcOrFS has higher priority
            {
                char* p = strrchr(text, '\t');
                if (p != NULL)
                    strcpy(tail, p);
                else
                    p = text + strlen(text);

                sprintf(p, " (%s)%s", LoadStr(IDS_PASTE_CHANGE_DIRECTORY), tail);
            }

            MENU_ITEM_INFO mii;
            mii.Mask = MENU_MASK_STRING;
            mii.String = text;
            popup->SetItemInfo(CM_CLIPPASTE, FALSE, &mii);
            break;
        }

        case CML_FILES_NEW:
        {
            CFilesWindow* activePanel = GetActivePanel();
            if (activePanel == NULL)
                break;
            BeginStopRefresh(); // we close in WM_USER_UNINITMENUPOPUP/CML_FILES_NEW,
                                // which is guaranteed to pair with this entry

            // if the menu does not exist, let it be created
            if ((!ContextMenuNew->MenuIsAssigned()) && activePanel->Is(ptDisk) &&
                activePanel->CheckPath(FALSE) == ERROR_SUCCESS)
                GetNewOrBackgroundMenu(HWindow, activePanel->GetPath(), ContextMenuNew, CM_NEWMENU_MIN, CM_NEWMENU_MAX, FALSE);

            // if the menu exists, build our menu based on it
            if (ContextMenuNew->MenuIsAssigned())
                popup->SetTemplateMenu(ContextMenuNew->GetMenu());
            else
            {
                // otherwise insert a message that the New menu is unavailable
                popup->RemoveAllItems();
                MENU_ITEM_INFO mii;
                mii.Mask = MENU_MASK_TYPE | MENU_MASK_STRING | MENU_MASK_STATE;
                mii.Type = MENU_TYPE_STRING;
                mii.String = LoadStr(IDS_NEWISNOTAVAILABLE);
                mii.State = MENU_STATE_GRAYED;
                popup->InsertItem(0, TRUE, &mii);
            }
            break;
        }

        case CML_FILES_VIEWWITH:
        {
            CFilesWindow* activePanel = GetActivePanel();
            if (activePanel == NULL)
                break;

            HIMAGELIST hIcons = Plugins.CreateIconsList(FALSE); // the image list will be destroyed in WM_USER_UNINITMENUPOPUP
            HIMAGELIST hIconsGray = Plugins.CreateIconsList(TRUE);
            popup->SetImageList(hIconsGray);
            popup->SetHotImageList(hIcons);

            activePanel->FillViewWithMenu(popup);
            break;
        }

        case CML_FILES_EDITWITH:
        {
            CFilesWindow* activePanel = GetActivePanel();
            if (activePanel == NULL)
                break;
            activePanel->FillEditWithMenu(popup);
            break;
        }

        case CML_COMMANDS_USERMENU:
        {
            popup->RemoveAllItems();
            FillUserMenu(popup); // expanding the user menu here is handled via WM_USER_ENTERMENULOOP/WM_USER_LEAVEMENULOOP (UserMenuIconBkgndReader.BeginUserMenuIconsInUse / EndUserMenuIconsInUse)
            break;
        }

        case CML_PLUGINS:
        {
            // initialize the Plugins menu
            HIMAGELIST hIcons = Plugins.CreateIconsList(FALSE); // the image list will be destroyed in WM_USER_UNINITMENUPOPUP
            HIMAGELIST hIconsGray = Plugins.CreateIconsList(TRUE);
            popup->SetImageList(hIconsGray);
            popup->SetHotImageList(hIcons);

            Plugins.InitMenuItems(HWindow, popup);
            popup->AssignHotKeys();
            break;
        }

        case CML_PLUGINS_SUBMENU:
        {
            // initialize a submenu of one of the plugins
            Plugins.InitSubMenuItems(HWindow, popup);
            break;
        }

        case CML_OPTIONS:
        {
            popup->CheckItem(CM_ALWAYSONTOP, FALSE, Configuration.AlwaysOnTop);
            break;
        }

        case CML_OPTIONS_PLUGINS:
        {
            popup->RemoveAllItems();

            HIMAGELIST hIcons = Plugins.CreateIconsList(FALSE); // the image list will be destroyed in WM_USER_UNINITMENUPOPUP
            HIMAGELIST hIconsGray = Plugins.CreateIconsList(TRUE);
            popup->SetImageList(hIconsGray);
            popup->SetHotImageList(hIcons);
            // we want only plugins with configuration options
            if (Plugins.AddNamesToMenu(popup, CM_PLUGINCFG_MIN, CM_PLUGINCFG_MAX - CM_PLUGINCFG_MIN, TRUE))
                popup->AssignHotKeys();
            break;
        }

        case CML_OPTIONS_VISIBLE:
        {
            popup->CheckItem(CM_TOGGLETOPTOOLBAR, FALSE, TopToolBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEPLUGINSBAR, FALSE, PluginsBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEMIDDLETOOLBAR, FALSE, MiddleToolBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEUSERMENUTOOLBAR, FALSE, UMToolBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEHOTPATHSBAR, FALSE, HPToolBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEDRIVEBAR, FALSE, DriveBar->HWindow != NULL && DriveBar2->HWindow == NULL);
            popup->CheckItem(CM_TOGGLEDRIVEBAR2, FALSE, DriveBar2->HWindow != NULL);
            popup->CheckItem(CM_TOGGLEEDITLINE, FALSE, EditPermanentVisible);
            popup->CheckItem(CM_TOGGLEBOTTOMTOOLBAR, FALSE, BottomToolBar->HWindow != NULL);
            popup->CheckItem(CM_TOGGLE_UMLABELS, FALSE, Configuration.UserMenuToolbarLabels);
            popup->CheckItem(CM_TOGGLE_GRIPS, FALSE, !Configuration.GripsVisible);
            break;
        }

        case CML_HELP_ABOUTPLUGINS:
        {
            popup->RemoveAllItems();

            HIMAGELIST hIcons = Plugins.CreateIconsList(FALSE); // the image list will be destroyed in WM_USER_UNINITMENUPOPUP
            HIMAGELIST hIconsGray = Plugins.CreateIconsList(TRUE);
            popup->SetImageList(hIconsGray);
            popup->SetHotImageList(hIcons);
            // we want all plugins
            if (Plugins.AddNamesToMenu(popup, CM_PLUGINABOUT_MIN, CM_PLUGINABOUT_MAX - CM_PLUGINABOUT_MIN, FALSE))
                popup->AssignHotKeys();
            break;
        }
        }
        return 0;
    }

    case WM_INITMENUPOPUP: // note: similar code is also in CFileListBox
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_MENUCHAR:
    {
        LRESULT plResult = 0;
        if (ContextMenuChngDrv != NULL)
        {
            // if the user right-clicks HotPath in the ChangeDrive menu, it comes here
            CALL_STACK_MESSAGE1("CMainWindow::WindowProc::ContextMenuChngDrv");
            SafeHandleMenuChngDrvMsg2(uMsg, wParam, lParam, &plResult);
        }
        if (ContextMenuNew != NULL && ContextMenuNew->MenuIsAssigned())
        {
            CALL_STACK_MESSAGE1("CMainWindow::WindowProc::SafeHandleMenuMsg2");
            SafeHandleMenuNewMsg2(uMsg, wParam, lParam, &plResult);
        }
        return plResult;
    }

    case WM_SETCURSOR:
    {
        if (HasLockedUI())
            break;
        if (HelpMode)
        {
            SetCursor(HHelpCursor);
            return TRUE;
        }
        POINT p, p2;
        GetCursorPos(&p);
        p2 = p;
        ScreenToClient(HWindow, &p);
        RECT r;
        GetSplitRect(r);
        if (IsWindowEnabled(HWindow) && PtInRect(&r, p) && GetCapture() == NULL)
        {
            BOOL aboveMiddle = FALSE;
            if (MiddleToolBar != NULL && MiddleToolBar->HWindow != NULL)
            {
                GetWindowRect(MiddleToolBar->HWindow, &r);
                aboveMiddle = PtInRect(&r, p2);
            }
            if (!aboveMiddle)
            {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    }

    case WM_CONTEXTMENU:
    {
        if (HasLockedUI())
            break;
        if (!DragMode)
        {
            OnWmContextMenu((HWND)wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        break;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    {
        if (HasLockedUI())
            break;

        POINT p;
        p.x = (short)LOWORD(lParam);
        p.y = (short)HIWORD(lParam);

        RECT r;
        GetSplitRect(r);

        if (PtInRect(&r, p))
        {
            if (uMsg == WM_LBUTTONDOWN) // click -> start dragging
            {
                UpdateWindow(HWindow);        // if Salamander is underneath, repaint all windows
                MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit
                BeginStopIconRepaint();       // we do not want any icon repaints
                if (!DragFullWindows)
                    BeginStopStatusbarRepaint(); // skip throbber repaints when dragging the XOR split bar

                DragMode = TRUE;
                DragAnchorX = p.x - r.left;
                SetCapture(HWindow);

                HWND toolTip = CreateWindowEx(0,
                                              TOOLTIPS_CLASS,
                                              NULL,
                                              TTS_ALWAYSTIP | TTS_NOPREFIX,
                                              CW_USEDEFAULT,
                                              CW_USEDEFAULT,
                                              CW_USEDEFAULT,
                                              CW_USEDEFAULT,
                                              NULL,
                                              NULL,
                                              HInstance,
                                              NULL);
                ToolTipWindow.AttachToWindow(toolTip);
                ToolTipWindow.SetToolWindow(HWindow);
                TOOLINFO ti;
                ti.cbSize = sizeof(TOOLINFO);
                ti.uFlags = TTF_SUBCLASS | TTF_ABSOLUTE | TTF_TRACK;
                ti.hwnd = HWindow;
                ti.uId = 1;
                GetClientRect(HWindow, &ti.rect);
                ti.hinst = HInstance;
                ti.lpszText = LPSTR_TEXTCALLBACK;
                SendMessage(ToolTipWindow.HWindow, TTM_ADDTOOL, 0, (LPARAM)&ti);

                int splitWidth = MainWindow->GetSplitBarWidth();
                DragSplitPosition = SplitPosition;
                POINT mp;
                GetCursorPos(&mp);
                POINT p2;
                p2.x = r.left;
                p2.y = 0;
                ClientToScreen(HWindow, &p2);
                mp.x = p2.x;
                SendMessage(ToolTipWindow.HWindow, TTM_TRACKPOSITION, 0, (LPARAM)(DWORD)MAKELONG(mp.x + splitWidth + 2, mp.y + 10));
                SendMessage(ToolTipWindow.HWindow, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);

                GetWindowSplitRect(r);
                DragSplitX = p.x - DragAnchorX;
                DrawSplitLine(HWindow, DragSplitX, -1, r);
                return 0;
            }
            if (uMsg == WM_LBUTTONDBLCLK)
            {
                if (SplitPosition != 0.5)
                {
                    SplitPosition = 0.5;
                    LayoutWindows();
                    FocusPanel(GetActivePanel());
                }
                return 0;
            }
        }
        break;
    }

    case WM_MOUSEMOVE:
    {
        if (HasLockedUI())
            break;
        if (DragMode && (wParam & MK_LBUTTON))
        {
            int x = (short)LOWORD(lParam);
            RECT r;
            GetWindowSplitRect(r);

            int splitWidth = MainWindow->GetSplitBarWidth();

            // stopper at the center
            double splitPosition = (double)(x - DragAnchorX) / (WindowWidth - splitWidth);

            if (splitPosition >= 0.49 && splitPosition <= 0.51)
            {
                x = (WindowWidth - splitWidth) / 2 + DragAnchorX;
                splitPosition = 0.5;
            }

            if (splitPosition < 0)
                splitPosition = 0;
            if (splitPosition > 1)
                splitPosition = 1;

            int leftWidth = x - DragAnchorX;
            if (leftWidth < MIN_WIN_WIDTH + 1)
                leftWidth = MIN_WIN_WIDTH + 1;
            int rightWidth = WindowWidth - 2 - leftWidth - splitWidth;
            if (rightWidth < MIN_WIN_WIDTH - 1)
            {
                rightWidth = MIN_WIN_WIDTH - 1;
                leftWidth = WindowWidth - 2 - splitWidth - rightWidth;
            }

            TOOLINFO ti;
            ti.cbSize = sizeof(TOOLINFO);
            ti.uFlags = 0;
            ti.hwnd = HWindow;
            ti.uId = 1;
            GetClientRect(HWindow, &ti.rect);

            DragSplitPosition = splitPosition;

            POINT p;
            GetCursorPos(&p);
            POINT p2;
            p2.x = leftWidth;
            p2.y = 0;
            ClientToScreen(HWindow, &p2);
            p.x = p2.x;
            SendMessage(ToolTipWindow.HWindow, TTM_TRACKPOSITION, 0, (LPARAM)(DWORD)MAKELONG(p.x + splitWidth + 2, p.y + 10));
            UpdateWindow(HWindow);

            if (DragFullWindows)
            {
                if (DragSplitX != leftWidth)
                {
                    DragSplitX = leftWidth;
                    SplitPosition = DragSplitPosition;
                    LayoutWindows();
                }
            }
            else
            {
                DrawSplitLine(HWindow, leftWidth, DragSplitX, r);
                DragSplitX = leftWidth;
            }

            //        ti.hinst = HInstance;
            //        ti.lpszText = LPSTR_TEXTCALLBACK;
            //        SendMessage(ToolTipWindow.HWindow, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);
        }
        break;
    }

    case WM_CANCELMODE:
    case WM_LBUTTONUP:
    {
        if (HasLockedUI())
            break;
        if (DragMode)
        {
            RECT r;
            GetClientRect(HWindow, &r);
            RECT r2;
            GetSplitRect(r2);
            r2.left = r.left;
            r2.right = r.right;
            DrawSplitLine(HWindow, -1, DragSplitX, r2);
            SendMessage(ToolTipWindow.HWindow, TTM_ACTIVATE, FALSE, 0);
            DestroyWindow(ToolTipWindow.HWindow); // just detaches the tooltip from the control
            if (uMsg == WM_LBUTTONUP)
            {
                // accept the position only when the drag finishes legally
                //          int splitWidth = MainWindow->GetSplitBarWidth();
                //          SplitPosition = (double)DragSplitX / (WindowWidth - splitWidth);
                SplitPosition = DragSplitPosition;
                LayoutWindows();
                // The splitter position is configuration, so queue its persistence once the drag commits.
                ScheduleConfigSave();
            }
            DragMode = FALSE;
            ReleaseCapture();
            FocusPanel(GetActivePanel());
            EndStopIconRepaint(TRUE); // resume icon repainting and repaint them now
            if (!DragFullWindows)
                EndStopStatusbarRepaint(); // resume throbber repaints when dragging the XOR split bar
            return 0;
        }
        break;
    }

    case WM_NOTIFY:
    {
        if (!Created)
            break;
        if (HasLockedUI())
            break;
        LPNMHDR lphdr = (LPNMHDR)lParam;
        if (lphdr->code == TTN_NEEDTEXT && lphdr->hwndFrom == ToolTipWindow.HWindow)
        {
            char* text = ((LPTOOLTIPTEXT)lParam)->szText;
            sprintf(text, "%.1lf %%", DragSplitPosition * 100);
            PointToLocalDecimalSeparator(text, 15);
            return 0;
        }

        if (lphdr->code == NM_RCLICK &&
            (LeftPanel->DirectoryLine->ToolBar != NULL &&
             lphdr->hwndFrom == LeftPanel->DirectoryLine->ToolBar->HWindow))
        {
            CToolBar* toolBar = LeftPanel->DirectoryLine->ToolBar;
            DWORD pos = GetMessagePos();
            POINT p;
            p.x = GET_X_LPARAM(pos);
            p.y = GET_Y_LPARAM(pos);
            ScreenToClient(toolBar->HWindow, &p);
            int index = toolBar->HitTest(p.x, p.y);
            if (index >= 0)
            {
                TLBI_ITEM_INFO2 tii;
                tii.Mask = TLBI_MASK_ID;
                if (toolBar->GetItemInfo2(index, TRUE, &tii))
                {
                    if (tii.ID == CM_LCHANGEDRIVE)
                    {
                        LeftPanel->UserWorkedOnThisPath = TRUE;
                        ShellAction(LeftPanel, saContextMenu, FALSE);
                        return 1;
                    }
                }
            }
            break;
        }

        if (lphdr->code == NM_RCLICK &&
            (RightPanel->DirectoryLine->ToolBar != NULL &&
             lphdr->hwndFrom == RightPanel->DirectoryLine->ToolBar->HWindow))
        {
            CToolBar* toolBar = RightPanel->DirectoryLine->ToolBar;
            DWORD pos = GetMessagePos();
            POINT p;
            p.x = GET_X_LPARAM(pos);
            p.y = GET_Y_LPARAM(pos);
            ScreenToClient(toolBar->HWindow, &p);
            int index = toolBar->HitTest(p.x, p.y);
            if (index >= 0)
            {
                TLBI_ITEM_INFO2 tii;
                tii.Mask = TLBI_MASK_ID;
                if (toolBar->GetItemInfo2(index, TRUE, &tii))
                {
                    if (tii.ID == CM_RCHANGEDRIVE)
                    {
                        RightPanel->UserWorkedOnThisPath = TRUE;
                        ShellAction(RightPanel, saContextMenu, FALSE);
                        return 1;
                    }
                }
            }
            break;
        }

        if (lphdr->code == NM_RCLICK &&
            (DriveBar != NULL && DriveBar->HWindow != NULL &&
             lphdr->hwndFrom == DriveBar->HWindow))
        {
            if (DriveBar->OnContextMenu())
                return 1;
            break;
        }

        if (lphdr->code == NM_RCLICK &&
            (DriveBar2 != NULL && DriveBar2->HWindow != NULL &&
             lphdr->hwndFrom == DriveBar2->HWindow))
        {
            if (DriveBar2->OnContextMenu())
                return 1;
            break;
        }

        if (lphdr->code == TBN_TOOLBARCHANGE)
        {
            if (LeftPanel->DirectoryLine->ToolBar != NULL &&
                lphdr->hwndFrom == LeftPanel->DirectoryLine->ToolBar->HWindow)
                LeftPanel->DirectoryLine->LayoutWindow();
            if (RightPanel->DirectoryLine->ToolBar != NULL &&
                lphdr->hwndFrom == RightPanel->DirectoryLine->ToolBar->HWindow)
                RightPanel->DirectoryLine->LayoutWindow();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            return 0;
        }
        if (lphdr->code == RBN_AUTOSIZE)
        {
            LPNMRBAUTOSIZE lpnmas = (LPNMRBAUTOSIZE)lParam;
            LayoutWindows();
            return 0;
        }
        if (lphdr->code == RBN_LAYOUTCHANGED)
        {
            StoreBandsPos();
            // Rebar layout changes are not commands; persist the positions through the shared debounce.
            ScheduleConfigSave();
            return 0;
        }

        if (lphdr->code == RBN_BEGINDRAG && DriveBar2->HWindow != NULL)
        {
            // hide the drive bars while dragging bands
            ShowHideTwoDriveBarsInternal(FALSE);
            return 0;
        }

        if (lphdr->code == RBN_ENDDRAG && DriveBar2->HWindow != NULL)
        {
            // after dragging, show our two bands again and move them to the end
            ShowHideTwoDriveBarsInternal(TRUE);
            return 0;
        }

        break;
    }

    case WM_WINDOWPOSCHANGED:
    {
        GetWindowRect(HWindow, &WindowRect);
        // Persist the final window placement after interactive move/resize messages settle.
        ScheduleConfigSave();
        break;
    }

    case WM_SIZE: // panel size adjustment
    {
        // at Tonda's, WM_SIZE arrives before WM_CREATE finishes
        // (bug report execution address = 0x004743C3)
        if (!Created)
        {
            PostMessage(HWindow, uMsg, wParam, lParam);
            break;
        }

        WindowWidth = LOWORD(lParam);
        WindowHeight = HIWORD(lParam);

        if (SplitPosition < 0)
            SplitPosition = 0;
        if (SplitPosition > 1)
            SplitPosition = 1;

        int splitWidth = GetSplitBarWidth();
        int middleToolbarWidth = 0;
        if (MiddleToolBar->HWindow != NULL)
            middleToolbarWidth = MiddleToolBar->GetNeededWidth();

        int leftWidth = (int)((WindowWidth - splitWidth) * SplitPosition) - 1;
        if (leftWidth < MIN_WIN_WIDTH)
            leftWidth = MIN_WIN_WIDTH;
        int rightWidth = WindowWidth - 2 - leftWidth - splitWidth;
        if (rightWidth < MIN_WIN_WIDTH)
        {
            rightWidth = MIN_WIN_WIDTH;
            leftWidth = WindowWidth - 2 - rightWidth - splitWidth;
        }
        SplitPositionPix = 1 + leftWidth;

        TopRebarHeight = 0;
        BottomToolBarHeight = 0;
        EditHeight = 0;
        PanelsHeight = WindowHeight - 1;

        int windowsCount = 3;

        RECT rebRect;
        GetWindowRect(HTopRebar, &rebRect);
        TopRebarHeight = rebRect.bottom - rebRect.top;

        if (MiddleToolBar->HWindow != NULL)
        {
            windowsCount++;
        }
        if (BottomToolBar->HWindow != NULL)
        {
            windowsCount++;
            BottomToolBarHeight = BottomToolBar->GetNeededHeight();
        }
        if (EditWindow->HWindow != NULL)
        {
            windowsCount++;
            EditHeight = EditWindow->GetNeededHeight() + 1;
        }

        PanelsHeight -= TopRebarHeight + BottomToolBarHeight + EditHeight;
        if (PanelsHeight < 0)
            PanelsHeight = 0;

        HDWP hdwp = HANDLES(BeginDeferWindowPos(windowsCount));
        if (hdwp != NULL)
        {
            hdwp = HANDLES(DeferWindowPos(hdwp, HTopRebar, NULL,
                                          0, 0, WindowWidth, TopRebarHeight,
                                          SWP_NOACTIVATE | SWP_NOZORDER));

            hdwp = HANDLES(DeferWindowPos(hdwp, LeftPanel->HWindow, NULL,
                                          1, TopRebarHeight, leftWidth, PanelsHeight,
                                          SWP_NOACTIVATE | SWP_NOZORDER));
            hdwp = HANDLES(DeferWindowPos(hdwp, RightPanel->HWindow, NULL,
                                          SplitPositionPix + splitWidth, TopRebarHeight, rightWidth, PanelsHeight,
                                          SWP_NOACTIVATE | SWP_NOZORDER));

            if (MiddleToolBar->HWindow != NULL)
            {
                // move the toolbar down if any panel has a directory line
                int offset1 = 0;
                int offset2 = 0;
                if (LeftPanel->DirectoryLine != NULL && LeftPanel->DirectoryLine->HWindow != NULL)
                    offset1 = LeftPanel->DirectoryLine->GetNeededHeight();
                if (RightPanel->DirectoryLine != NULL && RightPanel->DirectoryLine->HWindow != NULL)
                    offset2 = RightPanel->DirectoryLine->GetNeededHeight();
                int offset = max(offset1, offset2);
                hdwp = HANDLES(DeferWindowPos(hdwp, MiddleToolBar->HWindow, NULL,
                                              SplitPositionPix + SPLIT_LINE_WIDTH, TopRebarHeight + offset,
                                              middleToolbarWidth, PanelsHeight - offset,
                                              SWP_NOACTIVATE | SWP_NOZORDER));
            }

            // HWND_BOTTOM - prevents flickering during window resize
            // if the bottom toolbar ends up down there, it flickers when resizing
            if (EditWindow->HWindow != NULL)
                hdwp = HANDLES(DeferWindowPos(hdwp, EditWindow->HWindow, HWND_BOTTOM,
                                              0, TopRebarHeight + PanelsHeight + 2, WindowWidth, EditHeight + 150,
                                              SWP_NOACTIVATE /*| SWP_NOZORDER*/));

            if (BottomToolBar->HWindow != NULL)
                hdwp = HANDLES(DeferWindowPos(hdwp, BottomToolBar->HWindow, NULL,
                                              1, TopRebarHeight + PanelsHeight + EditHeight + 1, WindowWidth - 2, BottomToolBarHeight,
                                              SWP_NOACTIVATE | SWP_NOZORDER));
            HANDLES(EndDeferWindowPos(hdwp));
        }
        if (DriveBar2->HWindow != NULL)
        {
            REBARBANDINFO rbi;
            rbi.cbSize = sizeof(REBARBANDINFO);
            rbi.fMask = RBBIM_SIZE;

            RECT r;
            // at Tomas Jelinek the second band strip could stick to the right side after maximizing the main window
            // and refused to move; this might solve the problem
            GetClientRect(RightPanel->HWindow, &r);
            rbi.cx = r.right;
            int index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR2, 0);
            SendMessage(HTopRebar, RB_SETBANDINFO, index, (LPARAM)&rbi);

            GetClientRect(LeftPanel->HWindow, &r);
            rbi.cx = r.right + MainWindow->GetSplitBarWidth() / 2 - 1;
            index = (int)SendMessage(HTopRebar, RB_IDTOINDEX, BANDID_DRIVEBAR, 0);
            SendMessage(HTopRebar, RB_SETBANDINFO, index, (LPARAM)&rbi);
        }
        break;
    }

    case WM_NCACTIVATE:
    {
        // set the global variable indicating the main window frame state
        CaptionIsActive = (BOOL)wParam;

        // repaint the directory line of the active window
        // if selection is being lost, request an update quickly so we don't
        // destroy the buffer of the opening window with CS_SAVEBITS
        CFilesWindow* panel = GetActivePanel();
        if (panel != NULL && panel->DirectoryLine != NULL)
            panel->DirectoryLine->InvalidateAndUpdate(!CaptionIsActive);

        if (!CaptionIsActive)
        {
            // let the bottom toolbar reset to its default position
            UpdateBottomToolBar();
        }
        break;
    }

    case WM_ENABLE:
    {
        if (WindowsVistaAndLater)
        {
            // Windows Vista UAC patch: when starting a file from the panels caused the UAC elevation prompt to appear
            // and then was closed using Cancel, Salamander would lose focus from the panel.
            // The main window is disabled at the time messages like WM_ACTIVATE or WM_SETFOCUS arrive, and the focus is received by Microsoft IME-supported popups.
            BOOL enabled = (BOOL)wParam;
            if (enabled)
            {
                HWND hFocused = GetFocus();
                HWND hPanelListbox = NULL;
                CFilesWindow* activePanel = GetActivePanel();
                if (activePanel != NULL && !EditMode)
                {
                    hPanelListbox = activePanel->GetListBoxHWND();
                    if (hFocused == NULL || hFocused != hPanelListbox)
                        FocusPanel(activePanel);
                }
            }
        }
        break;
    }

    case WM_ACTIVATE:
    {
        int active = LOWORD(wParam);
        if (active == WA_INACTIVE)
            CacheNextSetFocus = TRUE; // for a smooth switch to Salamander; otherwise focus would be drawn aggressively (like old versions)
        else
            SuppressToolTipOnCurrentMousePos(); // suppress an unwanted tooltip when switching to the window
        ExitHelpMode();

        // ensure hiding/showing the Wait window if it exists
        ShowSafeWaitWindow(active != WA_INACTIVE);

        if (active != WA_INACTIVE)
            BringLockedUIToolWnd();

        if (active == WA_ACTIVE || active == WA_CLICKACTIVE)
        {
            if (!EditMode)
            {
                if (GetActivePanel() != NULL)
                {
                    FocusPanel(GetActivePanel());
                    return 0;
                }
            }
            else
            {
                if (EditWindow->HWindow != NULL)
                {
                    SetFocus(EditWindow->HWindow);
                    return 0;
                }
            }
        }
        break;
    }

    case WM_USER_POSTCMDORUNLOADPLUGIN:
    {
        CPluginData* data = Plugins.GetPluginData((CPluginInterfaceAbstract*)wParam);
        if (data != NULL && data->GetLoaded())
        {
            if (lParam == 0)
                data->ShouldUnload = TRUE; // set the flag to unload the plugin
            else
            {
                if (lParam == 1)
                    data->ShouldRebuildMenu = TRUE; // set the flag to rebuild the plugin menu
                else
                    data->Commands.Add(LOWORD(lParam - 2)); // add salCmd/menuCmd
            }
            ExecCmdsOrUnloadMarkedPlugins = TRUE; // inform Salamander to scan all plugin data
        }
        else
        {
            // may occur while waiting for Release(force==TRUE) method of the plugin to finish
            //        TRACE_E("Unexpected situation in WM_USER_POSTCMDORUNLOADPLUGIN.");
        }
        return 0;
    }

    case WM_USER_POSTMENUEXTCMD:
    {
        CPluginData* data = Plugins.GetPluginData((CPluginInterfaceAbstract*)wParam);
        if (data != NULL && data->GetLoaded())
        {
            if (data->GetPluginInterfaceForMenuExt()->NotEmpty())
            {
                CALL_STACK_MESSAGE4("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d,) (%s v. %s)",
                                    (int)lParam, data->DLLName, data->Version);

                // lower the thread priority to "normal" (so operations don't burden the system)
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                data->GetPluginInterfaceForMenuExt()->ExecuteMenuItem(NULL, HWindow, (int)lParam, 0);

                // raise the thread priority again, the operation has finished
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            }
            else
            {
                TRACE_E("Plugin must have PluginInterfaceForMenuExt when "
                        "calling CSalamanderGeneral::PostMenuExtCommand()!");
            }
        }
        else
        {
            // it must be loaded because post-menu-ext-cmd was invoked from a loaded plugin...
            // post-unload runs during "idle", so the unload couldn't have happened yet...
            TRACE_E("Unexpected situation in WM_USER_POSTMENUEXTCMD.");
        }
        return 0;
    }

    case WM_USER_SALSHEXT_TRYRELDATA:
    {
        //      TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: begin");
        if (SalShExtSharedMemView != NULL) // shared memory is available (we cannot handle cut/copy&paste errors)
        {
            WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
            BOOL needRelease = TRUE;
            if (!SalShExtSharedMemView->BlockPasteDataRelease)
            {
                if (!SalShExtPastedData.IsLocked())
                {
                    BOOL isOnClipboard = FALSE;
                    if (SalShExtSharedMemView->DoPasteFromSalamander &&
                        SalShExtSharedMemView->SalamanderMainWndPID == GetCurrentProcessId() &&
                        SalShExtSharedMemView->SalamanderMainWndTID == GetCurrentThreadId() &&
                        SalShExtSharedMemView->PastedDataID == SalShExtPastedData.GetDataID())
                    {
                        ReleaseMutex(SalShExtSharedMemMutex);
                        needRelease = FALSE;

                        IDataObject* dataObj;
                        if (OleGetClipboard(&dataObj) == S_OK && dataObj != NULL)
                        {
                            if (IsFakeDataObject(dataObj, NULL, NULL, 0))
                            {
                                isOnClipboard = TRUE;
                            }
                            dataObj->Release();
                        }
                    }

                    if (!isOnClipboard)
                    {
                        if (needRelease)
                            ReleaseMutex(SalShExtSharedMemMutex);
                        needRelease = FALSE;

                        //TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: clearing paste-data!");
                        SalShExtPastedData.Clear();
                    }
                    //            else TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: fake-data-object is still on clipboard");
                }
                //          else TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: paste-data is locked");
            }
            //        else TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: release of paste-data is blocked");
            if (needRelease)
                ReleaseMutex(SalShExtSharedMemMutex);
        }
        //      TRACE_I("WM_USER_SALSHEXT_TRYRELDATA: end");
        return 0;
    }

    case WM_USER_SALSHEXT_PASTE:
    {
        //      TRACE_I("WM_USER_SALSHEXT_PASTE: begin");
        if (SalShExtSharedMemView != NULL) // shared memory is available (we cannot handle cut/copy&paste errors)
        {
            BOOL tmpPasteDone = FALSE;
            char tgtPath[MAX_PATH];
            tgtPath[0] = 0;
            int operation = 0;
            DWORD dataID = -1;
            WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
            if (SalShExtSharedMemView->PostMsgIndex == (int)wParam) // process only the "current" messages
            {
                if (SalamanderBusy)
                    SalShExtSharedMemView->SalBusyState = 2 /* Salamander is busy, postpone paste for later */;
                else
                {
                    SalamanderBusy = TRUE;
                    SalShExtPastedData.SetLock(TRUE);
                    LastSalamanderIdleTime = GetTickCount();
                    SalShExtSharedMemView->SalBusyState = 1 /* Salamander is not busy and now is waiting for a paste operation */;
                    SalShExtSharedMemView->PasteDone = FALSE;

                    int count = 0;
                    while (count++ < 50) // wait no longer than 5 seconds
                    {
                        ReleaseMutex(SalShExtSharedMemMutex);
                        Sleep(100); // give the copy hook 100 ms to respond
                        WaitForSingleObject(SalShExtSharedMemMutex, INFINITE);
                        if (SalShExtSharedMemView->PasteDone) // copy hook supplied the target path for Paste and other data
                        {
                            //                TRACE_I("WM_USER_SALSHEXT_PASTE: copy hook returned: paste done!");
                            lstrcpyn(tgtPath, SalShExtSharedMemView->TargetPath, MAX_PATH);
                            operation = SalShExtSharedMemView->Operation;
                            dataID = SalShExtSharedMemView->PastedDataID;
                            tmpPasteDone = TRUE;
                            break;
                        }
                    }
                    SalamanderBusy = FALSE;
                }
            }
            ReleaseMutex(SalShExtSharedMemMutex);

            if (tmpPasteDone && operation == SALSHEXT_COPY && SalShExtPastedData.GetDataID() == dataID) // perform the Paste operation
            {
                SalamanderBusy = TRUE;
                LastSalamanderIdleTime = GetTickCount();
                //          TRACE_I("WM_USER_SALSHEXT_PASTE: calling SalShExtPastedData.DoPasteOperation");
                ProgressDialogActivateDrop = LastWndFromPasteGetData;
                SalShExtPastedData.DoPasteOperation(operation == SALSHEXT_COPY, tgtPath);
                ProgressDialogActivateDrop = NULL; // clear global variable for next use of the progress dialog
                LastWndFromPasteGetData = NULL;    // reset for the next Paste operation here
                SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH, tgtPath, NULL);
                SalamanderBusy = FALSE;
            }
            SalShExtPastedData.SetLock(FALSE);
            PostMessage(HWindow, WM_USER_SALSHEXT_TRYRELDATA, 0, 0); // after unlocking, optionally release the data
        }
        //      TRACE_I("WM_USER_SALSHEXT_PASTE: end");
        return 0;
    }

    case WM_USER_REFRESH_SHARES:
    {
        Shares.Refresh();
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        int t2 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        if (LeftPanel != NULL && LeftPanel->Is(ptDisk) && !LeftPanel->GetNetworkDrive())
        {
            PostMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        }
        if (RightPanel != NULL && RightPanel->Is(ptDisk) && !RightPanel->GetNetworkDrive())
        {
            PostMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        }
        return 0;
    }

    case WM_USER_END_SUSPMODE:
    {
        // if the main window is minimized (slow restore or opening a context menu),
        // postpone panel content check ("retry" may occur when removing a disk, etc.)
        if (IsIconic(HWindow))
        {
            SetTimer(HWindow, IDT_POSTENDSUSPMODE, 500, NULL);
            //      originally instead of using a timer: PostMessage(HWindow, WM_USER_END_SUSPMODE, 0, 0);
            return 0;
        }

        if (--ActivateSuspMode < 0)
        {
            ActivateSuspMode = 0;
            // TRACE_E("WM_USER_END_SUSPMODE: problem 2");  // opening a message box with a NULL parent resends WM_ACTIVATEAPP "activate" (Salamander is already active)
            return 0; // the message was already cancelled
        }
        HCURSOR oldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));

        // first we must finish activating the window
        static BOOL recursion = FALSE;
        if (!recursion)
        {
            recursion = TRUE;
            MSG msg;
            CanCloseButInEndSuspendMode = CanClose;
            BOOL oldCanClose = CanClose;
            CanClose = FALSE; // don't let ourselves be closed; we are inside the method
            BOOL postWM_USER_CLOSE_MAINWND = FALSE;
            BOOL postWM_USER_FORCECLOSE_MAINWND = FALSE;
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_USER_CLOSE_MAINWND && msg.hwnd == HWindow)
                    postWM_USER_CLOSE_MAINWND = TRUE;
                else
                {
                    if (msg.message == WM_USER_FORCECLOSE_MAINWND && msg.hwnd == HWindow)
                        postWM_USER_FORCECLOSE_MAINWND = TRUE;
                    else
                    {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }
            }
            CanClose = oldCanClose;
            CanCloseButInEndSuspendMode = FALSE;

            if (postWM_USER_CLOSE_MAINWND)
                PostMessage(HWindow, WM_USER_CLOSE_MAINWND, 0, 0);
            if (postWM_USER_FORCECLOSE_MAINWND)
                PostMessage(HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);

            recursion = FALSE;
        }
        //      else
        //      {
        //#pragma message (__FILE__ " (2120): remove")
        //        SalMessageBox(HWindow, "problem3", "problem3", MB_OK); // debug message
        //      }

        // window is activated, perform a refresh
        // EndSuspendMode();   // removed, we want to refresh even when the main window is inactive

        LeftPanel->Activate(FALSE);
        RightPanel->Activate(FALSE);

        // if OneDrive Personal/Business was connected or disconnected, refresh the Drive bars
        // so the icon or drop down menu disappears or appears
        BOOL oneDrivePersonal = OneDrivePath[0] != 0;
        int oneDriveBusinessStoragesCount = OneDriveBusinessStorages.Count;
        InitOneDrivePath();
        if (oneDrivePersonal != (OneDrivePath[0] != 0) ||
            oneDriveBusinessStoragesCount != OneDriveBusinessStorages.Count)
        {
            PostMessage(HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
        }

        SetCursor(oldCur);
        return 0;
    }

    case WM_TIMER:
    {
        switch (wParam)
        {
        case IDT_DELETEMNGR_PROCESS:
        {
            KillTimer(HWindow, IDT_DELETEMNGR_PROCESS);
            DeleteManager.ProcessData();
            break;
        }

        case IDT_POSTENDSUSPMODE:
        {
            KillTimer(HWindow, IDT_POSTENDSUSPMODE);
            PostMessage(HWindow, WM_USER_END_SUSPMODE, 0, 0); // if ActivateSuspMode < 1, nothing happens
            break;
        }

        case IDT_ADDNEWMODULES:
        {
            AddNewlyLoadedModulesToGlobalModulesStore();
            break;
        }

        case IDT_PLUGINFSTIMERS:
        {
            Plugins.HandlePluginFSTimers();
            break;
        }

        case IDT_ASSOCIATIONSCHNG:
        {
            KillTimer(HWindow, IDT_ASSOCIATIONSCHNG);
            OnAssociationsChangedNotification(FALSE);
            break;
        }

        case IDT_SAVECONFIG:
        {
            KillTimer(HWindow, IDT_SAVECONFIG);
            // The debounce elapsed, so save the state committed by the preceding user interaction.
            SaveConfig();
            break;
        }

        default:
        {
            TRACE_E("Unknown WM_TIMER wParam=" << wParam);
            break;
        }
        }
        break;
    }

    case WM_USER_SLGINCOMPLETE:
    {
        char buff[1000];
        sprintf(buff, "%s\n", LoadStr(IDS_SLGINCOMPLETE_TEXT));
        Configuration.ShowSLGIncomplete = FALSE;
        // Dismissing the one-time language notice changes configuration outside the command dispatcher.
        ScheduleConfigSave();
        CMessageBox(HWindow, MSGBOXEX_OK | MSGBOXEX_ESCAPEENABLED | MSGBOXEX_SILENT | MSGBOXEX_ICONINFORMATION,
                    LoadStr(IDS_SLGINCOMPLETE_TITLE), buff, NULL,
                    NULL, NULL, 0, NULL, NULL, IsSLGIncomplete, NULL)
            .Execute();
        break;
    }

    case WM_USER_USERMENUICONS_READY:
    {
        CUserMenuIconDataArr* bkgndReaderData = (CUserMenuIconDataArr*)wParam;
        DWORD threadID = (DWORD)lParam;
        if (bkgndReaderData != NULL && // "always true"
            UserMenuIconBkgndReader.EnterCSIfCanUpdateUMIcons(&bkgndReaderData, threadID))
        { // if the user menu still wants these icons:
            // if icons can be updated immediately, lock user menu access from Find and update them; otherwise
            // postpone the update until the menu with icons closes (we cannot pull the rug from under it) or after closing
            // configuration dialog: after OK the newly loaded icons would be overwritten and reloading wouldn't start, leaving icons unloaded
            for (int i = 0; i < UserMenuItems->Count; i++)
                UserMenuItems->At(i)->GetIconHandle(bkgndReaderData, TRUE);
            UserMenuIconBkgndReader.LeaveCSAfterUMIconsUpdate();
            if (UMToolBar != NULL && UMToolBar->HWindow != NULL) // refresh the user menu toolbar
                UMToolBar->CreateButtons();
        }
        if (bkgndReaderData != NULL)
            delete bkgndReaderData;
        break;
    }

    case WM_ACTIVATEAPP:
    {
        //      TRACE_I("WM_ACTIVATEAPP: " << (wParam == TRUE ? "activate" : "deactivate"));
        if (FirstActivateApp)
        {
            if (IsWindowVisible(HWindow))
                FirstActivateApp = FALSE;
            else
                break;
        }

        // do the work for lost and undelivered messages
        int actSusMode = (wParam == TRUE) ? 1 : 0; // ActivateSuspMode should be 1 when activating, otherwise 0
        if (ActivateSuspMode < 0)
        {
            ActivateSuspMode = 0;
            TRACE_E("WM_USER_END_SUSPMODE: problem 6");
        }
        else
        {
            if (ActivateSuspMode != actSusMode) // e.g. two deactivations in a row or missed activation
            {
                KillTimer(HWindow, IDT_POSTENDSUSPMODE); // if activation hasn't happened yet, cancel (it may start again)

                MSG msg; // pump WM_USER_END_SUSPMODE from the queue, otherwise suspend mode ends shortly (e.g. opening File Comparator triggers activation+deactivation after 10ms)
                while (PeekMessage(&msg, HWindow, WM_USER_END_SUSPMODE, WM_USER_END_SUSPMODE, PM_REMOVE))
                    ;

                while (ActivateSuspMode > actSusMode)
                {
                    // EndSuspendMode();  // removed, we want to refresh even when the main window is inactive
                    ActivateSuspMode--;
                }
            }
        }

        //      if (IsWindowVisible(HWindow))    // now handled by FirstActivateApp
        //      {
        if (wParam == TRUE) // activating the app
        {
            if (!LeftPanel->DontClearNextFocusName)
                LeftPanel->NextFocusName[0] = 0;
            else
                LeftPanel->DontClearNextFocusName = FALSE;
            if (!RightPanel->DontClearNextFocusName)
                RightPanel->NextFocusName[0] = 0;
            else
                RightPanel->DontClearNextFocusName = FALSE;
            if (Windows7AndLater && IsIconic(HWindow))
            {
                SetTimer(HWindow, IDT_POSTENDSUSPMODE, 200, NULL); // hopefully we'll never find out why this timer existed; commented out because it delays directory refresh by 200 ms after operations (e.g. moving a file into a subdirectory, it is visible on a local disk)
            }
            else
            {
                // until 2.53b1 only this branch existed and the timer version was commented out
                // on Windows 7 users reported activation issues when icon grouping was enabled
                // and Salamander was minimized; sometimes clicking its preview (or Alt+Tab)
                // would not restore Salamander, only a beep; see /viewtopic.php?f=6&t=3791
                //
                // so we enable the delayed variant (200ms) again, but only on W7 and only if the window is minimized
                PostMessage(HWindow, WM_USER_END_SUSPMODE, 0, 0); // if ActivateSuspMode is not >= 1, nothing happens
            }
            IdleRefreshStates = TRUE;  // on the next Idle, force a check of status variables
            IdleCheckClipboard = TRUE; // also let it check the clipboard
        }
        else // deactivating the app
        {
            // when the main window deactivates, cancel quick search and quick rename modes
            CancelPanelsUI();

            //        BeginSuspendMode();    // removed, we want refresh even with inactive main window
            ActivateSuspMode++;
            //        }
            //      }
            //      if (wParam == FALSE)  // when deactivating, leave directories displayed in panels
            //      {                     // so other software can delete or disconnect them
            if (CanChangeDirectory())
            {
                SetCurrentDirectoryToSystem();
            }
        }
        break;
    }

    case WM_CLOSE:
    case WM_ENDSESSION:
    case WM_QUERYENDSESSION:
    case WM_USER_CLOSE_MAINWND:
    case WM_USER_FORCECLOSE_MAINWND:
        // Shutdown handling extracted to HandleShutdown() in mainwnd_shutdown.cpp.
        return HandleShutdown(uMsg, wParam, lParam);

    case WM_USER_ALLOCATION_EMERGENCY:
        // The allocator only posted this pre-registered message; now that the
        // UI thread owns execution it may write recovery state and close safely.
        COperationJournal::PersistEmergencyShutdownState();
        PostMessage(HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);
        return 0;


    case WM_ERASEBKGND:
    {
        /*
      HDC dc = (HDC)wParam;
      HPEN oldPen = (HPEN)SelectObject(dc, BtnFacePen);
      MoveToEx(dc, 0, 0, NULL);
      LineTo(dc, 0, WindowHeight - 1);
      LineTo(dc, WindowWidth - 1, WindowHeight - 1);
      LineTo(dc, WindowWidth - 1, 0);
      SelectObject(dc, oldPen);
*/
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;

        HDC dc = HANDLES(BeginPaint(HWindow, &ps));
        HPEN oldPen = (HPEN)SelectObject(dc, BtnShadowPen);

        RECT r;
        if (TopToolBar->HWindow != NULL)
        {
            MoveToEx(dc, 0, 0, NULL);
            LineTo(dc, WindowWidth + 1, 0);
            SelectObject(dc, BtnHilightPen);
            MoveToEx(dc, 0, 1, NULL);
            LineTo(dc, WindowWidth + 1, 1);
        }

        if (PanelsHeight > 0)
        {
            r.left = SplitPositionPix;
            r.top = TopRebarHeight;
            r.right = SplitPositionPix + MainWindow->GetSplitBarWidth();
            //        SelectObject(dc, shadowPen);
            //        MoveToEx(dc, r.left, r.top, NULL);
            //        LineTo(dc, r.right, r.top);
            //        SelectObject(dc, lightPen);
            //        MoveToEx(dc, r.left, r.top + 1, NULL);
            //        LineTo(dc, r.right, r.top + 1);
            r.bottom = r.top + PanelsHeight;
            FillRect(dc, &r, HDialogBrush);

            SelectObject(dc, BtnFacePen);
            MoveToEx(dc, 0, 0, NULL);
            LineTo(dc, 0, WindowHeight - 1);
            LineTo(dc, WindowWidth - 1, WindowHeight - 1);
            LineTo(dc, WindowWidth - 1, 0);
        }

        if (EditWindow->HWindow != NULL)
        {
            r.left = 0;
            r.top = TopRebarHeight + PanelsHeight;
            r.right = WindowWidth;
            r.bottom = r.top + 2;
            FillRect(dc, &r, HDialogBrush);
        }

        if (BottomToolBar->HWindow != NULL)
        {
            r.left = 0;
            r.top = TopRebarHeight + PanelsHeight + EditHeight;
            r.right = WindowWidth;
            r.bottom = r.top + 2;
            FillRect(dc, &r, HDialogBrush);
        }

        SelectObject(dc, oldPen);
        HANDLES(EndPaint(HWindow, &ps));
        return 0;
    }

    case WM_DESTROY:
    {
        // Invalidate before child teardown so concurrent workers cannot target this closing HWND.
        DeleteManager.InvalidateCallbackWindow(HWindow);
        if (!CanDestroyMainWindow)
        {
            // some crazy shell extension has just called DestroyWindow on Salamander's main window

            MSG msg; // flush the message queue (WMP9 buffered Enter and dismissed our OK)
            // while (PeekMessage(&msg, HWindow, 0, 0, PM_REMOVE));  // Petr: I replaced it by discarding key messages only; without TranslateMessage and DispatchMessage we risk an endless loop (discovered during unloading Automation with memory leaks; before showing the leak message box, an infinite loop occurred because WM_PAINT kept being added to the queue and we kept discarding it)
            while (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
                ;

            // ask the user to send us a break report
            SalMessageBox(HWindow, LoadStr(IDS_SHELLEXTBREAK), SALAMANDER_TEXT_VERSION,
                          MB_OK | MB_ICONSTOP);

            // and break here
            strcpy(BugReportReasonBreak, "Some faulty shell extension destroyed our main window.");
            TaskList.FireEvent(TASKLIST_TODO_BREAK, GetCurrentProcessId());
            // freeze this thread
            // MainWindow no longer exists anyway; we would crash at the next opportunity
            while (1)
                Sleep(1000);
        }

        // notify the task list that we are exiting
        TaskList.SetProcessState(PROCESS_STATE_ENDING, NULL);

        UserMenuIconBkgndReader.EndProcessing();

        SHChangeNotifyRelease(); // we no longer accept Shell Notifications
        KillTimer(HWindow, IDT_ADDNEWMODULES);
        HANDLES(RevokeDragDrop(HWindow));
        if (Configuration.StatusArea)
            RemoveTrayIcon();
        //--- destroy child windows
        if (EditWindow != NULL)
        {
            if (EditWindow->HWindow != NULL)
                DestroyWindow(EditWindow->HWindow);
            delete EditWindow;
            EditWindow = NULL;
        }
        if (TopToolBar != NULL)
        {
            if (TopToolBar->HWindow != NULL)
                DestroyWindow(TopToolBar->HWindow);
            delete TopToolBar;
            TopToolBar = NULL;
        }
        if (PluginsBar != NULL)
        {
            if (PluginsBar->HWindow != NULL)
                DestroyWindow(PluginsBar->HWindow);
            delete PluginsBar;
            PluginsBar = NULL;
        }
        if (MiddleToolBar != NULL)
        {
            if (MiddleToolBar->HWindow != NULL)
                DestroyWindow(MiddleToolBar->HWindow);
            delete MiddleToolBar;
            MiddleToolBar = NULL;
        }
        if (UMToolBar != NULL)
        {
            if (UMToolBar->HWindow != NULL)
                DestroyWindow(UMToolBar->HWindow);
            delete UMToolBar;
            UMToolBar = NULL;
        }
        if (HPToolBar != NULL)
        {
            if (HPToolBar->HWindow != NULL)
                DestroyWindow(HPToolBar->HWindow);
            delete HPToolBar;
            HPToolBar = NULL;
        }
        if (DriveBar != NULL)
        {
            if (DriveBar->HWindow != NULL)
                DestroyWindow(DriveBar->HWindow);
            delete DriveBar;
            DriveBar = NULL;
        }
        if (DriveBar2 != NULL)
        {
            if (DriveBar2->HWindow != NULL)
                DestroyWindow(DriveBar2->HWindow);
            delete DriveBar2;
            DriveBar2 = NULL;
        }
        if (BottomToolBar != NULL)
        {
            if (BottomToolBar->HWindow != NULL)
                DestroyWindow(BottomToolBar->HWindow);
            delete BottomToolBar;
            BottomToolBar = NULL;
        }
        if (MenuBar != NULL)
        {
            if (MenuBar->HWindow != NULL)
                DestroyWindow(MenuBar->HWindow);
            delete MenuBar;
            MenuBar = NULL;
        }
        SetMessagesParent(NULL);
        PostQuitMessage(0);
        break;
    }

    case WM_USER_ICON_NOTIFY:
    {
        UINT uID = (UINT)wParam;
        if (uID != TASKBAR_ICON_ID)
            break;
        UINT uMouseMsg = (UINT)lParam;
        if (uMouseMsg == WM_LBUTTONDOWN)
        {
            if (!IsWindowVisible(HWindow))
            {
                ShowWindow(HWindow, SW_SHOW);
                if (IsIconic(HWindow))
                    ShowWindow(HWindow, SW_RESTORE);
            }
            else
            {
                SetForegroundWindow(GetLastActivePopup(HWindow));
            }
        }
        if (uMouseMsg == WM_LBUTTONDBLCLK)
        {
            if (GetActiveWindow() == HWindow)
            {
                ShowWindow(HWindow, SW_MINIMIZE);
                ShowWindow(HWindow, SW_HIDE);
            }
        }
        if (uMouseMsg == WM_RBUTTONDOWN)
        {
            /* used by the export_mnu.py script which generates salmenu.mnu for the Translator;
               keep synchronized with the InsertMenu() call below...
MENU_TEMPLATE_ITEM TaskBarIconMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_CONTEXTMENU_EXIT
  {MNTT_PE, 0
};
*/
            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, CM_EXIT, LoadStr(IDS_CONTEXTMENU_EXIT));

            POINT p;
            GetCursorPos(&p);

            DWORD cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
                                       p.x, p.y, 0, HWindow, NULL);
            DestroyMenu(hMenu);
            if (cmd != 0)
                PostMessage(HWindow, WM_COMMAND, CM_EXIT, 0);
        }
        break;
    }

#if (_MSC_VER < 1700)
    // handle messages sent from the file manager extension
    case FM_GETDRIVEINFOW:
    {
        TRACE_E("FM_GETDRIVEINFOW not implemented");
        break;
    }

    case FM_GETFILESELW:
    {
        TRACE_E("FM_GETFILESELW not implemented");
        break;
    }

    case FM_GETFILESELLFNW:
    {
        if (!GetActivePanel()->Is(ptDisk))
            return 0; // we operate only on the disk

        int index = (int)wParam;
        FMS_GETFILESELW* fs = (FMS_GETFILESELW*)lParam;
        CFilesWindow* activePanel = GetActivePanel();

        int count = activePanel->GetSelCount();
        if (count != 0)
        {
            // determine the index of the nth (index) selected item
            int totalCount = activePanel->Dirs->Count + activePanel->Files->Count;
            if (totalCount == 0 || index >= totalCount)
                return 0;
            int selectedCount = 0;
            int i;
            for (i = 0; i < totalCount; i++)
            {
                CFileData* f = (i < activePanel->Dirs->Count) ? &activePanel->Dirs->At(i) : &activePanel->Files->At(i - activePanel->Dirs->Count);
                if (f->Selected == 1)
                {
                    if (index == selectedCount)
                    {
                        index = i;
                        break;
                    }
                    selectedCount++;
                }
            }
        }
        else
        {
            index = GetActivePanel()->GetCaretIndex();
        }

        CFileData* f;
        f = (index < GetActivePanel()->Dirs->Count) ? &GetActivePanel()->Dirs->At(index) : &GetActivePanel()->Files->At(index - GetActivePanel()->Dirs->Count);

        char buff[MAX_PATH];
        strcpy(buff, GetActivePanel()->GetPath());
        if (buff[strlen(buff) - 1] != '\\')
            strcat(buff, "\\");
        strcat(buff, f->Name);
        MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, buff, -1, fs->szName, sizeof(fs->szName) / 2);
        fs->szName[sizeof(fs->szName) / 2 - 1] = 0;
        fs->ftTime = f->LastWrite;
        fs->dwSize = f->Size.LoDWord;
        fs->bAttr = (BYTE)f->Attr;
        return 0;
    }

    case FM_GETFOCUS:
    {
        return FMFOCUS_DIR;
    }

    case FM_GETSELCOUNT:
    {
        TRACE_E("FM_GETSELCOUNT not implemented");
        return 0;
    }

    case FM_GETSELCOUNTLFN:
    {
        if (!GetActivePanel()->Is(ptDisk))
            return 0; // we operate only on the disk

        CFilesWindow* activePanel = GetActivePanel();

        if (activePanel->Dirs->Count + activePanel->Files->Count == 0)
            return 0;
        int count = GetActivePanel()->GetSelCount();
        if (count == 0)
        {
            int index = GetActivePanel()->GetCaretIndex();
            if (index == 0 && GetActivePanel()->Dirs->Count > 0 &&
                strcmp(GetActivePanel()->Dirs->At(0).Name, "..") == 0)
                count = 0;
            else
                count = 1;
        }
        return count;
    }

    case FM_REFRESH_WINDOWS:
    {
        CFilesWindow* panel = GetActivePanel();
        if (panel != NULL && panel->Is(ptDisk))
        {
            //--- refresh directories that are not automatically refreshed
            // a change in the directory shown in the panel and preferably its subdirectories (who knows what the system does)
            PostChangeOnPathNotification(panel->GetPath(), TRUE);
        }
        break;
    }

    case FM_RELOAD_EXTENSIONS:
    {
        break;
    }
#endif // _MSC_VER < 1700

    default:
    {
        if (uMsg == TaskbarRestartMsg && Configuration.StatusArea)
            AddTrayIcon();
        if (TaskbarBtnCreatedMsg != 0 && uMsg == TaskbarBtnCreatedMsg)
            TaskBarList3.Init(HWindow);
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}
