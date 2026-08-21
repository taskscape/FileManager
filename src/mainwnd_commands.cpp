// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "stswnd.h"
#include "editwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "usermenu.h"
#include "mainwnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "execute.h"
#include "cache.h"
#include "toolbar.h"
#include "salinflt.h"
#include "menu.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "zip.h"

BOOL ImageDragging = FALSE;
BOOL ImageDraggingVisible = FALSE;
BOOL ShowCaretAfterDrop = FALSE;
int ImageDraggingVisibleLevel = 0;
int ImageDragX = INT_MAX;
int ImageDragY = INT_MAX;
int ImageDragW = INT_MAX;
int ImageDragH = INT_MAX;
int ImageDragDxHotspot = INT_MAX;
int ImageDragDyHotspot = INT_MAX;

//****************************************************************************
//
// CMainWindow
//

void RecursiveFindAndCopy(char* srcPath, char* dstPath, char** fromBuf, char** toBuf, int* freeBufNames);

void CMainWindow::ClearPluginFSFromHistory(CPluginFSInterfaceAbstract* fs)
{
    DirHistory->ClearPluginFSFromHistory(fs);
}

void CMainWindow::DirHistoryAddPathUnique(int type, const char* pathOrArchiveOrFSName,
                                          const char* archivePathOrFSUserPart, HICON hIcon,
                                          CPluginFSInterfaceAbstract* pluginFS,
                                          CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (CanAddToDirHistory)
    {
        DirHistory->AddPathUnique(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, hIcon,
                                  pluginFS, curPluginFS);
        if (LeftPanel != NULL)
            LeftPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
        if (RightPanel != NULL)
            RightPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
    }
    else
    {
        if (hIcon != NULL)
            HANDLES(DestroyIcon(hIcon));
    }
}

void CMainWindow::DirHistoryRemoveActualPath(CFilesWindow* panel)
{
    if (panel->Is(ptZIPArchive))
    {
        DirHistory->RemoveActualPath(1, panel->GetZIPArchive(), panel->GetZIPPath(), NULL, NULL);
    }
    else
    {
        if (panel->Is(ptDisk))
        {
            DirHistory->RemoveActualPath(0, panel->GetPath(), NULL, NULL, NULL);
        }
        else
        {
            if (panel->Is(ptPluginFS))
            {
                char curPath[MAX_PATH];
                if (panel->GetPluginFS()->NotEmpty() && panel->GetPluginFS()->GetCurrentPath(curPath))
                {
                    DirHistory->RemoveActualPath(2, panel->GetPluginFS()->GetPluginFSName(), curPath,
                                                 panel->GetPluginFS()->GetInterface(), panel->GetPluginFS());
                }
            }
        }
    }
    if (LeftPanel != NULL)
        LeftPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
    if (RightPanel != NULL)
        RightPanel->DirectoryLine->SetHistory(DirHistory->HasPaths());
}

void CMainWindow::GetSplitRect(RECT& r)
{
    r.left = SplitPositionPix;
    r.top = TopRebarHeight;
    r.right = SplitPositionPix + MainWindow->GetSplitBarWidth();
    r.bottom = WindowHeight - EditHeight - BottomToolBarHeight;
}

void CMainWindow::GetWindowSplitRect(RECT& r)
{
    GetClientRect(HWindow, &r);
    r.top = TopRebarHeight;
    r.bottom = WindowHeight - EditHeight - BottomToolBarHeight;
}

BOOL CMainWindow::PtInChild(HWND hChild, POINT p)
{
    if (hChild == NULL)
        return FALSE;
    RECT r;
    GetWindowRect(hChild, &r);
    MapWindowPoints(NULL, HWindow, (POINT*)&r, 2);
    return PtInRect(&r, p);
}

BOOL CMainWindow::CloseDetachedFS(HWND parent, CPluginFSInterfaceEncapsulation* detachedFS)
{
    CALL_STACK_MESSAGE1("CMainWindow::CloseDetachedFS()");
    BOOL dummy; // ignored return value
    if (!detachedFS->TryCloseOrDetach(CriticalShutdown, FALSE, dummy, FSTRYCLOSE_UNLOADCLOSEDETACHEDFS) &&
        !CriticalShutdown) // test close; forceClose==TRUE only during a "critical shutdown"
    {                      // ask the user whether to close it even against the FS wishes
        char path[2 * MAX_PATH];
        strcpy(path, detachedFS->GetPluginFSName());
        strcat(path, ":");
        char* s = path + strlen(path);
        if (!detachedFS->NotEmpty() || !detachedFS->GetCurrentPath(s))
            *s = 0; // cannot obtain the user portion

        char buf[2 * MAX_PATH + 100];
        sprintf(buf, LoadStr(IDS_FSFORCECLOSE), path);
        if (SalMessageBox(parent, buf, LoadStr(IDS_QUESTION),
                          MB_YESNO | MB_ICONQUESTION) == IDYES) // user chooses "close"
        {
            detachedFS->TryCloseOrDetach(TRUE, FALSE, dummy, FSTRYCLOSE_UNLOADCLOSEDETACHEDFS);
        }
        else
            return FALSE; // user doesn't want to close the detached FS
    }

    // close the FS
    CPluginInterfaceForFSEncapsulation plugin(detachedFS->GetPluginInterfaceForFS()->GetInterface(),
                                              detachedFS->GetPluginInterfaceForFS()->GetBuiltForVersion());
    if (plugin.NotEmpty())
    {
        detachedFS->ReleaseObject(parent);
        plugin.CloseFS(detachedFS->GetInterface());
    }
    else
        TRACE_E("Unexpected situation (2) in CMainWindow::CloseDetachedFS()");

    return TRUE; // FS is closed
}

BOOL CMainWindow::CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin)
{
    CALL_STACK_MESSAGE1("CMainWindow::CanUnloadPlugin()");
    if (LeftPanel != NULL && !LeftPanel->CanUnloadPlugin(parent, plugin))
        return FALSE;
    if (RightPanel != NULL && !RightPanel->CanUnloadPlugin(parent, plugin))
        return FALSE;

    // find detached FS belonging to the plug-in 'plugin' and attempt to close them
    int i;
    for (i = DetachedFSList->Count - 1; i >= 0; i--) // iterate backwards; as we will be deleting from the array (quadratic complexity)
    {
        CPluginFSInterfaceEncapsulation* detachedFS = DetachedFSList->At(i);
        if (detachedFS->GetPluginInterface() == plugin) // belongs to plug-in 'plugin'
        {
            if (CloseDetachedFS(parent, detachedFS))
            {
                DetachedFSList->Delete(i); // remove the detached FS from DetachedFSList
                if (!DetachedFSList->IsGood())
                    DetachedFSList->ResetState();
            }
            else
                return FALSE; // unload cannot proceed (user refused to close the plug-in's detached FS)
        }
    }

    // check if data from the plugin is not in SalShExtPastedData
    // (panels leaving archives might have stored them there due to plug-in unload)
    if (!SalShExtPastedData.CanUnloadPlugin(parent, plugin))
        return FALSE; // unload cannot proceed

    return TRUE; // unload is possible; all plug-in resources were released
}

void CMainWindow::MakeFileList()
{
    CALL_STACK_MESSAGE1("CMainWindow::MakeFileList()");

    BOOL files = FALSE; // cursor is on a file or directory or there is a selection
    BOOL upDir = FALSE; // presence of ".."

    CFilesWindow* panel = GetActivePanel();

    upDir = (panel->Dirs->Count != 0 && strcmp(panel->Dirs->At(0).Name, "..") == 0);
    int caret = panel->GetCaretIndex();
    if (caret >= 0)
    {
        if (caret == 0)
        {
            if (!upDir)
                files = (panel->Dirs->Count + panel->Files->Count > 0);
            else
            {
                int count = panel->GetSelCount();
                if (count == 1)
                {
                    files = (panel->GetSel(0) == FALSE);
                }
                else
                    files = (count > 0);
            }
        }
        else
            files = TRUE;
    }

    if (!files)
        return;

    // restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);

    BeginStopRefresh(); // snooper takes a break

    CFileListDialog dlg(HWindow);
    if (dlg.Execute() == IDOK)
    {
        char fileName[MAX_PATH];

        switch (Configuration.FileListDestination)
        {
        case 0: // clipboard
        case 1: // viewer
        {
            if (!SalGetTempFileName(NULL, "MFL", fileName, _countof(fileName), TRUE))
            {
                DWORD err = GetLastError();
                char errorText[200 + 2 * MAX_PATH];
                sprintf(errorText, "%s\n\n%s", LoadStr(IDS_ERRORCREATINGTMPFILE),
                        GetErrorText(err));
                SalMessageBox(HWindow, errorText, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                fileName[0] = 0;
            }
            break;
        }

        case 2: // file
        {
            strcpy(fileName, Configuration.FileListName);
            int errTextID;
            if (!SalGetFullName(fileName, &errTextID, GetActivePanel()->Is(ptDisk) ? GetActivePanel()->GetPath() : NULL, panel->NextFocusName))
            {
                SalMessageBox(HWindow, LoadStr(errTextID), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                fileName[0] = 0;
            }
            break;
        }

        default:
        {
            TRACE_E("Unknown destination!");
            fileName[0] = 0;
        }
        }

        if (fileName[0] != 0)
        {
            BOOL append = (Configuration.FileListDestination == 2 && Configuration.FileListAppend);
            HANDLE hFile = HANDLES_Q(CreateFileUtf8(fileName, GENERIC_WRITE | GENERIC_READ,
                                                FILE_SHARE_READ, NULL,
                                                append ? OPEN_ALWAYS : CREATE_ALWAYS,
                                                FILE_FLAG_RANDOM_ACCESS,
                                                NULL));
            if (hFile != INVALID_HANDLE_VALUE)
            {
                // fill the file with data -- insert one entry for each file or directory
                BOOL deleteFile = TRUE;
                LARGE_INTEGER filePosition = {};
                // The file-list command must use a 64-bit seek so append mode cannot inherit a 32-bit sentinel failure.
                if (!SetFilePointerEx(hFile, filePosition, NULL, append ? FILE_END : FILE_BEGIN))
                {
                    DWORD err = GetLastError();
                    SalMessageBox(HWindow, GetErrorText(err), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                }
                else
                {
                    if (panel->MakeFileList(hFile))
                    {
                        panel->SetSel(FALSE, -1, TRUE);                        // force redraw
                        PostMessage(panel->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
                        deleteFile = FALSE;
                    }

                    if (!deleteFile && Configuration.FileListDestination == 0) // clipboard
                    {
                        LARGE_INTEGER fileSize;
                        // The clipboard path remains DWORD-sized, so reject a larger temporary file instead of truncating it.
                        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart > 0 &&
                            (ULONGLONG)fileSize.QuadPart <= MAXDWORD &&
                            SetFilePointerEx(hFile, filePosition, NULL, FILE_BEGIN))
                        {
                            DWORD bytesToRead = (DWORD)fileSize.QuadPart;
                            char* buff = (char*)malloc(bytesToRead);
                            if (buff != NULL)
                            {
                                DWORD read;
                                if (ReadFile(hFile, buff, bytesToRead, &read, NULL))
                                {
                                    CopyTextToClipboard(buff, bytesToRead, FALSE, NULL);
                                }
                                else
                                {
                                    DWORD err = GetLastError();
                                    SalMessageBox(HWindow, GetErrorText(err), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                                }
                                free(buff);
                            }
                            else
                                TRACE_E(LOW_MEMORY);
                        }
                    }
                }
                HANDLES(CloseHandle(hFile));

                // if the destination was the clipboard, delete the temporary file
                if (deleteFile || Configuration.FileListDestination == 0) // clipboard
                    DeleteFileUtf8(fileName);
                else
                {
                    if (Configuration.FileListDestination == 1) // viewer
                    {
                        // show the file in the internal viewer, which will delete it afterwards
                        CSalamanderPluginInternalViewerData viewerData;
                        viewerData.Size = sizeof(viewerData);
                        viewerData.FileName = fileName;
                        viewerData.Mode = 0; // text mode
                        char title[200];
                        // Viewer captions retain their fixed presentation allocation.
                        StringCchCopyNA(title, _countof(title), LoadStr(IDS_MAKEFILELIST_OUTPUT), _countof(title) - 1);
                        viewerData.Caption = title;
                        viewerData.WholeCaption = TRUE;
                        int error;
                        if (!ViewFileInPluginViewer(NULL, &viewerData, TRUE, NULL, "mfl.txt", error))
                        {
                            // delete the file even when opening fails
                        }
                    }
                }

                if (Configuration.FileListDestination == 2) // file
                {
                    //---  refresh manually refreshed directories
                    // change in the directory where the file list was created
                    CutDirectory(fileName);
                    MainWindow->PostChangeOnPathNotification(fileName, FALSE);
                }
            }
            else
            {
                DWORD err = GetLastError();
                char message[MAX_PATH + 100];
                sprintf(message, LoadStr(IDS_FILEERRORFORMAT), fileName, GetErrorText(err));
                SalMessageBox(HWindow, message, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            }
        }
    }
    EndStopRefresh(); // the snooper will start again now
}

// see description in mainwnd.h
BOOL GetNextFileFromPanel(int index, char* path, char* name, void* param)
{
    CALL_STACK_MESSAGE2("GetNextFileFromPanel(%d, , ,)", index);
    CUMDataFromPanel* data = (CUMDataFromPanel*)param;
    if (data->Count == -1) // retrieving data
    {
        BOOL upDir = (data->Window->Dirs->Count != 0 &&
                      strcmp(data->Window->Dirs->At(0).Name, "..") == 0);
        data->Count = data->Window->GetSelCount();
        if (data->Count < 0)
            data->Count = 0;
        if (data->Count == 0) // no selection -> use the focused item
        {
            index = data->Window->GetCaretIndex();
            data->Index = NULL;
            strcpy(path, data->Window->GetPath());
            if (index < 0 || index >= data->Window->Dirs->Count + data->Window->Files->Count ||
                index == 0 && upDir)
            {
                name[0] = 0; // for up-dir or for the first item of an empty panel the name will be empty...
            }
            else // copy the name for others
            {
                CFileData* f = &((index < data->Window->Dirs->Count) ? data->Window->Dirs->At(index) : data->Window->Files->At(index - data->Window->Dirs->Count));
                strcpy(name, f->Name);
            }
            return TRUE;
        }
        data->Index = new int[data->Count];
        if (data->Index == NULL)
            return FALSE; // error
        data->Window->GetSelItems(data->Count, data->Index);
    }
    if (index >= 0 && index < data->Count)
    {
        CFileData* f = &((data->Index[index] < data->Window->Dirs->Count) ? data->Window->Dirs->At(data->Index[index]) : data->Window->Files->At(data->Index[index] - data->Window->Dirs->Count));
        strcpy(path, data->Window->GetPath());
        strcpy(name, f->Name);
        return TRUE;
    }
    else
    {
        if (data->Index != NULL)
        {
            delete[] (data->Index);
            data->Index = NULL;
        }
        data->Window->SetSel(FALSE, -1, TRUE);                        // explicit redraw
        PostMessage(data->Window->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
        return FALSE;
    }
}

BOOL CheckIfCanBeExecuted(BOOL buildBat, int commandLen, int argumentsLen)
{
    /*  MEASURED LIMITS:
  Batch file:
    W2K: 2041 including executable name, spaces and parameters
    XP64/XP: 8185 including executable name, spaces and parameters
    Win7/Vista: 32776 including executable name, spaces and parameters

  ShellExecuteEx:
    XP/XP64/Vista/W2K: 2080 including executable name (without quotes), spaces and parameters
    Win7: 32764 including executable name (without quotes), spaces and parameters
*/

    // WARNING: if a .bat file is executed that runs a .exe and passes all parameters (%*), a long .exe name
    // can still trigger "too long name" error even when respecting the limit here. The limit is exceeded once
    // parameters are passed to the .exe. (I wouldn't solve this issue; it would require parsing
    // .bat files etc., which is just nonsense.)

    int cmdLineLen = commandLen + argumentsLen + 1; // +1 for the space between command and arguments
    if (buildBat)                                   // launching via a .bat file
    {
        if (WindowsVistaAndLater)
            return cmdLineLen <= 8191; // Vista/Win7: in reality it's 32776 but only 8191 works (with longer parameters, probably due to a Windows bug, characters get erased; tested on Vista and Win7)
        return cmdLineLen <= 8185;     // XP/XP64
    }
    else // launching via ShellExecuteEx
    {
        if (Windows7AndLater)
            return cmdLineLen <= 32764; // Win7
        return cmdLineLen <= 2080;      // W2K/XP/XP64/Vista
    }
}

//*****************************************************************************
//
// ExpandCommand2
//
// parent       - parent window (for error dialogs)
// cmd          - buffer for the expanded command
// cmdSize      - size of 'cmd' buffer
// args         - buffer for receiving arguments
// argsSize     - size of 'args' buffer
// buildBat     - if TRUE, arguments are placed into 'cmd'
// initDir      - buffer for the path where the execution should take place
// initDirSize  - length of initDir buffer
// item         - user-menu item
// path         - long path to the file
// longName     - long filename
// fileNameUsed - returns TRUE if a file name or path was used during argument expansion
// userMenuAdvancedData - advanced parameter`s values for User Menu: the Arguments array
// ignoreEnvVarNotFoundOrTooLong - see ExpandVarString description
//
// returns success of the operation

BOOL ExpandCommand2(HWND parent,
                    char* cmd, int cmdSize,
                    char* args, int argsSize, BOOL buildBat,
                    char* initDir, int initDirSize,
                    CUserMenuItem* item,
                    const char* path,
                    const char* longName,
                    BOOL* fileNameUsed,
                    CUserMenuAdvancedData* userMenuAdvancedData,
                    BOOL ignoreEnvVarNotFoundOrTooLong)
{
    CALL_STACK_MESSAGE5("ExpandCommand2(, , %d, , %d, , %s, %s, )",
                        cmdSize, initDirSize, path, longName);

    *fileNameUsed = FALSE;
    char command[MAX_PATH];
    if (ExpandCommand(parent, item->UMCommand, command, MAX_PATH, ignoreEnvVarNotFoundOrTooLong))
    {
        char fileName[MAX_PATH];
        if (path[0] != 0)
        {
            int l = (int)strlen(path);
            if (path[l - 1] == '\\')
                l--;
            memcpy(fileName, path, l);

            char dosName[MAX_PATH];
            if (longName[0] != 0)
            {
                if (l + 1 + strlen(longName) > MAX_PATH - 1)
                {
                    SalMessageBox(parent, LoadStr(IDS_TOOLONGNAME), LoadStr(IDS_ERRORTITLE),
                                  MB_OK | MB_ICONEXCLAMATION);
                    goto EXIT;
                }
                fileName[l++] = '\\';
                strcpy(fileName + l, longName);
                if (GetShortPathName(fileName, dosName, MAX_PATH) == 0)
                {
                    TRACE_E("GetShortPathName() failed");
                    dosName[0] = 0;
                }
            }
            else
            {
                if (l == 2 && fileName[1] == ':') // we must append '\\' after a standard root path
                {
                    fileName[l++] = '\\';
                }
                fileName[l] = 0;
                if (GetShortPathName(fileName, dosName, MAX_PATH) == 0)
                {
                    TRACE_E("GetShortPathName() failed");
                    dosName[0] = 0;
                }
                else
                {
                    SalPathAddBackslash(dosName, MAX_PATH);
                }
                SalPathAddBackslash(fileName, MAX_PATH);
            }

            char expArguments[USRMNUARGS_MAXLEN];
            if (ExpandUserMenuArguments(parent, fileName, dosName, item->Arguments, expArguments,
                                        USRMNUARGS_MAXLEN, fileNameUsed, userMenuAdvancedData,
                                        ignoreEnvVarNotFoundOrTooLong) &&
                ExpandInitDir(parent, fileName, dosName, item->InitDir, initDir, initDirSize,
                              ignoreEnvVarNotFoundOrTooLong))
            {
                int len = (int)strlen(command);
                int lArgs = (int)strlen(expArguments);
                if (CheckIfCanBeExecuted(buildBat, len, lArgs))
                {
                    if (!buildBat) // launching via ShellExecuteEx: pass arguments separately
                    {
                        if (len + 1 <= cmdSize && lArgs + 1 <= argsSize)
                        {
                            memcpy(cmd, command, len + 1);
                            memcpy(args, expArguments, lArgs + 1);
                            return TRUE;
                        }
                    }
                    else // launching via .bat: append arguments after the command
                    {
                        if (len + lArgs + 2 <= cmdSize)
                        {
                            memcpy(cmd, command, len);
                            cmd[len] = ' ';
                            memcpy(cmd + len + 1, expArguments, lArgs + 1);
                            args[0] = 0;
                            return TRUE;
                        }
                    }
                }
                SalMessageBox(parent, LoadStr(IDS_USRMNUTOOLONGCMDORARGS), LoadStr(IDS_ERRORTITLE),
                              MB_OK | MB_ICONEXCLAMATION);
            }
        }
        else
        {
            int len = (int)strlen(command);
            if (len + 1 < cmdSize)
            {
                memcpy(cmd, command, len + 1);
                args[0] = 0;
                initDir[0] = 0;
                return TRUE;
            }
            else
            {
                SalMessageBox(parent, LoadStr(IDS_USRMNUTOOLONGCMDORARGS), LoadStr(IDS_ERRORTITLE),
                              MB_OK | MB_ICONEXCLAMATION);
            }
        }
    }
EXIT:
    cmd[0] = 0;
    args[0] = 0;
    initDir[0] = 0;
    return FALSE;
}

/*
void RemoveRedundantBackslahes(char *text)
{
  if (text == NULL)
  {
    TRACE_E("Unexpected situation in RemoveRedundantBackslahes().");
    return;
  }
  if (strlen(text) < 3)
    return;
  char *s = text + 2;
  char *d = s;
  while (*s != 0) 
  {
    *d = *s;
    if (*s == '\\')
    {
      while (*s == '\\') s++;
      d++;
    }
    else
    {
      s++;
      d++;
    }
  }
  *d = 0;
}
*/

void CMainWindow::UserMenu(HWND parent, int itemIndex, UM_GetNextFileName getNextFile, void* data,
                           CUserMenuAdvancedData* userMenuAdvancedData)
{
    CALL_STACK_MESSAGE2("CMainWindow::UserMenu(%d, ,)", itemIndex);
    if (itemIndex >= 0 && itemIndex < UserMenuItems->Count)
    {
        UpdateWindow(parent);

        int errorPos1, errorPos2;
        CUserMenuValidationData userMenuValidationData;
        BOOL ok = TRUE;
        if (ValidateUserMenuArguments(parent, UserMenuItems->At(itemIndex)->Arguments, errorPos1, errorPos2,
                                      &userMenuValidationData))
        {
            if (userMenuValidationData.UsesListOfSelNames && userMenuAdvancedData->ListOfSelNames[0] == 0)
            {
                SalMessageBox(parent, LoadStr(userMenuAdvancedData->ListOfSelNamesIsEmpty ? IDS_EMPTYLISTOFSELNAMES : IDS_TOOLONGLISTOFSELNAMES),
                              LoadStr(IDS_USERMENUERROR), MB_OK | MB_ICONEXCLAMATION);
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesListOfSelFullNames && userMenuAdvancedData->ListOfSelFullNames[0] == 0)
            {
                SalMessageBox(parent, LoadStr(userMenuAdvancedData->ListOfSelFullNamesIsEmpty ? IDS_EMPTYLISTOFSELFULLNAMES : IDS_TOOLONGLISTOFSELFULLNAMES),
                              LoadStr(IDS_USERMENUERROR), MB_OK | MB_ICONEXCLAMATION);
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathLeft && userMenuAdvancedData->FullPathLeft[0] == 0)
            {
                SalMessageBox(parent, LoadStr(IDS_NOTDEFFULLPATHLEFT),
                              LoadStr(IDS_USERMENUERROR), MB_OK | MB_ICONEXCLAMATION);
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathRight && userMenuAdvancedData->FullPathRight[0] == 0)
            {
                SalMessageBox(parent, LoadStr(IDS_NOTDEFFULLPATHRIGHT),
                              LoadStr(IDS_USERMENUERROR), MB_OK | MB_ICONEXCLAMATION);
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsesFullPathInactive && userMenuAdvancedData->FullPathInactive[0] == 0)
            {
                SalMessageBox(parent, LoadStr(IDS_NOTDEFFULLPATHINACTIVE),
                              LoadStr(IDS_USERMENUERROR), MB_OK | MB_ICONEXCLAMATION);
                ok = FALSE;
            }
            if (ok && userMenuValidationData.UsedCompareType != 0)
            {
                if ((userMenuValidationData.UsedCompareType == 6 /* file-or-dir-left-right */ ||
                     userMenuValidationData.UsedCompareType == 7 /* file-or-dir-active-inactive */) &&
                    userMenuAdvancedData->CompareName1[0] == 0 &&
                    userMenuAdvancedData->CompareName2[0] == 0)
                { // we don't know if files or directories should be compared, ask the user (the name selection dialog differs for files/directories)
                    MSGBOXEX_PARAMS params;
                    memset(&params, 0, sizeof(params));
                    params.HParent = parent;
                    params.Flags = MB_YESNO | MB_ICONQUESTION | MSGBOXEX_SILENT;
                    params.Caption = LoadStr(IDS_QUESTION);
                    params.Text = LoadStr(IDS_COMPAREFILESORDIRS);
                    char aliasBtnNames[200];
                    /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   we let the message box buttons resolve hotkey collisions by pretending it's a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_MSGBOXBTN_FILES
  {MNTT_IT, IDS_MSGBOXBTN_DIRS
  {MNTT_PE, 0
};
*/
                    sprintf(aliasBtnNames, "%d\t%s\t%d\t%s",
                            DIALOG_YES, LoadStr(IDS_MSGBOXBTN_FILES),
                            DIALOG_NO, LoadStr(IDS_MSGBOXBTN_DIRS));
                    params.AliasBtnNames = aliasBtnNames;
                    userMenuAdvancedData->CompareNamesAreDirs = (SalMessageBoxEx(&params) == DIALOG_NO);
                }

                BOOL swapNames = FALSE;
                BOOL clearNames = FALSE;
                BOOL comparingFiles = TRUE;
                switch (userMenuValidationData.UsedCompareType)
                {
                case 1: // file-left-right
                {
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    if (userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 2: // file-active-inactive
                {
                    if (userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 3: // dir-left-right
                {
                    comparingFiles = FALSE;
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    if (!userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 4: // dir-active-inactive
                {
                    comparingFiles = FALSE;
                    if (!userMenuAdvancedData->CompareNamesAreDirs)
                        clearNames = TRUE;
                    break;
                }

                case 6: // file-or-dir-left-right
                {
                    comparingFiles = !userMenuAdvancedData->CompareNamesAreDirs;
                    if (userMenuAdvancedData->CompareNamesReversed)
                        swapNames = TRUE;
                    break;
                }

                case 7: // file-or-dir-active-inactive
                {
                    comparingFiles = !userMenuAdvancedData->CompareNamesAreDirs;
                    break;
                }
                }
                if (clearNames)
                {
                    userMenuAdvancedData->CompareName1[0] = 0;
                    userMenuAdvancedData->CompareName2[0] = 0;
                }
                else
                {
                    if (swapNames)
                    {
                        char swap[MAX_PATH];
                        // Compare targets are filesystem identities and must never be swapped in truncated form.
                        if (FAILED(StringCchCopyA(swap, _countof(swap), userMenuAdvancedData->CompareName1)) ||
                            FAILED(StringCchCopyA(userMenuAdvancedData->CompareName1, _countof(userMenuAdvancedData->CompareName1), userMenuAdvancedData->CompareName2)) ||
                            FAILED(StringCchCopyA(userMenuAdvancedData->CompareName2, _countof(userMenuAdvancedData->CompareName2), swap)))
                        {
                            userMenuAdvancedData->CompareName1[0] = 0;
                            userMenuAdvancedData->CompareName2[0] = 0;
                        }
                    }
                }
                if (Configuration.CnfrmShowNamesToCompare ||
                    userMenuAdvancedData->CompareName1[0] == 0 ||
                    userMenuAdvancedData->CompareName2[0] == 0)
                {
                    CCompareArgsDlg dlg(parent, comparingFiles, userMenuAdvancedData->CompareName1,
                                        userMenuAdvancedData->CompareName2, &Configuration.CnfrmShowNamesToCompare);
                    if (dlg.Execute() != IDOK)
                        ok = FALSE;
                }
            }
        }
        else
            ok = FALSE;
        if (ok)
        {
            BOOL buildBat = UserMenuItems->At(itemIndex)->ThroughShell;
            BOOL batNotEmpty = FALSE;
            char* batName;
            HANDLE file;
            char batUniqueName[50]; // we need a unique name for the batch file in the cache
            DWORD lastErr;
            CQuadWord batFileSize;

        _TRY_AGAIN:

            // Keep the existing cache-key shape while folding the 64-bit uptime past the old tick-wrap boundary.
            const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
            // The cache key has a fixed local buffer, so preserve its format with bounded output.
            _snprintf_s(batUniqueName, _countof(batUniqueName), _TRUNCATE, "Usermenu %X", (DWORD)(timeSeed ^ (timeSeed >> 32)));
            if (buildBat)
            {
                BOOL exists;
                batName = (char*)DiskCache.GetName(batUniqueName, "usermenu.bat", &exists, TRUE, NULL, FALSE, NULL, NULL);
                if (batName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
                {
                    if (!exists) // file exists -> almost impossible, handle anyway
                    {
                        Sleep(100);
                        goto _TRY_AGAIN;
                    }
                    return; // fatal error
                }
                file = HANDLES_Q(CreateFileUtf8(batName, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                            FILE_ATTRIBUTE_TEMPORARY, NULL));
                if (file == INVALID_HANDLE_VALUE)
                {
                    lastErr = GetLastError();
                    goto ERROR_LABEL;
                }
            }

            // build the .bat file
            int index;
            index = 0;
            char cmdLine[USRMNUCMDLINE_MAXLEN];
            char arguments[USRMNUARGS_MAXLEN];
            char initDir[MAX_PATH];
            char prevInitDir[MAX_PATH];
            initDir[0] = 0;
            arguments[0] = 0;
            char path[MAX_PATH], name[MAX_PATH];
            BOOL error;
            error = FALSE;
            BOOL skipErrorMessage;
            skipErrorMessage = FALSE;
            DWORD written;
            BOOL fileNameUsed;
            BOOL firstRound;
            firstRound = TRUE;
            while (getNextFile(index, path, name, data))
            {
                strcpy(prevInitDir, initDir);
                BOOL expandOK = ExpandCommand2(parent,
                                               cmdLine, USRMNUCMDLINE_MAXLEN,
                                               arguments, USRMNUARGS_MAXLEN, buildBat, // if we are running via a batch file, allow
                                               initDir, MAX_PATH,                      // arguments will be inserted into cmdLine
                                               UserMenuItems->At(itemIndex),
                                               path, name, &fileNameUsed,
                                               userMenuAdvancedData,
                                               !firstRound);
                if (!expandOK)
                {
                    error = TRUE;
                    skipErrorMessage = TRUE;
                    break;
                }
                if (expandOK && (firstRound || fileNameUsed || strcmp(initDir, prevInitDir) != 0)) // block running the same command for all items (a user mistake that happens often)
                {
                    if (buildBat) // building a .bat file
                    {
                        char initDirOEM[MAX_PATH];
                        char cmdLineOEM[USRMNUCMDLINE_MAXLEN];
                        CharToOem(initDir, initDirOEM);
                        CharToOem(cmdLine, cmdLineOEM);
                        batNotEmpty = TRUE;
                        if ((initDirOEM[0] != 0 &&
                                 (initDirOEM[1] == ':' && // "@C:"
                                  (!WriteFile(file, "@", 1, &written, NULL) ||
                                   !WriteFile(file, initDirOEM, 2, &written, NULL) ||
                                   !WriteFile(file, "\r\n", 2, &written, NULL))) ||
                             (initDirOEM[1] == ':' && // "@cd C:\\path"
                              (!WriteFile(file, "@cd \"", 5, &written, NULL) ||
                               !WriteFile(file, initDirOEM, (DWORD)strlen(initDirOEM), &written, NULL) ||
                               !WriteFile(file, "\"\r\n", 3, &written, NULL)))) ||
                            !WriteFile(file, "call ", 5, &written, NULL) ||
                            !WriteFile(file, cmdLineOEM, (DWORD)strlen(cmdLineOEM), &written, NULL) ||
                            !WriteFile(file, "\r\n", 2, &written, NULL))
                        {
                            error = TRUE;
                            break;
                        }
                    }
                    else // direct execution
                    {
                        // the original launching via CreateProcess couldn't run screen savers (*.SCR)
                        // or Control Panel items (*.cpl) and people kept complaining
                        //
                        // try executing it via ShellExecuteEx - it appears to work ;-)
                        // additionally, launch restrictions will be handled

                        // set correct default directories for individual drives
                        MainWindow->SetDefaultDirectories((initDir[0] != 0) ? initDir : NULL);

                        // to work with old configurations, remove the " character from the start and end of cmdLine
                        int cmdLen = (int)strlen(cmdLine);
                        if (cmdLen > 1 && cmdLine[0] == '\"' && cmdLine[cmdLen - 1] == '\"')
                        {
                            memmove(cmdLine, cmdLine + 1, cmdLen - 2);
                            cmdLine[cmdLen - 2] = 0;
                        }
                        // better not swallow backslashes so that we don't destroy some OLE paths
                        //RemoveRedundantBackslahes(cmdLine); // ShellExecuteEx dislikes multiple backslashes, "$(SalDir)\salamand.exe"

                        CShellExecuteWnd shellExecuteWnd;
                        SHELLEXECUTEINFO sei;
                        memset(&sei, 0, sizeof(SHELLEXECUTEINFO));
                        sei.cbSize = sizeof(SHELLEXECUTEINFO);
                        sei.hwnd = shellExecuteWnd.Create(parent, "SEW: CMainWindow::UserMenu"); // handle to any message boxes that the system might produce while executing
                        sei.lpFile = cmdLine;
                        sei.lpParameters = arguments;
                        sei.lpDirectory = (initDir[0] != 0) ? initDir : NULL;
                        sei.nShow = SW_SHOWNORMAL;

                        if (!ShellExecuteEx(&sei))
                        {
                            DWORD err = GetLastError();
                            char buff[4 * MAX_PATH];
                            if (strlen(cmdLine) > 2 * MAX_PATH) // "always false" (arguments are in 'arguments'): shorten overly long command lines for error display
                                strcpy(cmdLine + 2 * MAX_PATH - 4, "...");
                            sprintf(buff, LoadStr(IDS_EXECERROR), cmdLine, GetErrorText(err));
                            SalMessageBox(parent, buff, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            break;
                        }
                    }
                }
                index++;
                firstRound = FALSE;
                if (userMenuValidationData.MustHandleItemsAsGroup)
                    break; // in this mode, only one command is executed for all selected items
            }

            if (buildBat)
            {
                lastErr = GetLastError();
                batFileSize.Set(0, 0);
                DWORD sizeError;
                // The disk cache records the full generated batch-file size instead of a truncated DWORD result.
                SalGetFileSize(file, batFileSize, sizeError); // size is advisory here, so keep zero when the query fails.
                HANDLES(CloseHandle(file));

                DiskCache.NamePrepared(batUniqueName, batFileSize);

                if (!error) // run the .bat
                {
                    if (batNotEmpty)
                    {
                        MainWindow->SetDefaultDirectories((initDir[0] != 0) ? initDir : NULL);

                        STARTUPINFO si;
                        memset(&si, 0, sizeof(STARTUPINFO));
                        si.cb = sizeof(STARTUPINFO);
                        si.lpTitle = LoadStr(IDS_COMMANDSHELL);
                        si.dwFlags = STARTF_USESHOWWINDOW;
                        POINT p;
                        if (UserMenuItems->At(itemIndex)->UseWindow &&
                            MultiMonGetDefaultWindowPos(MainWindow->HWindow, &p))
                        {
                            // if the main window is on another monitor, we should open
                            // the new window there, preferably at the default position (as on the primary monitor)
                            si.dwFlags |= STARTF_USEPOSITION;
                            si.dwX = p.x;
                            si.dwY = p.y;
                        }
                        si.wShowWindow = (UserMenuItems->At(itemIndex)->UseWindow ? SW_SHOWNORMAL : SW_HIDE);

                        PROCESS_INFORMATION pi;

                        GetEnvironmentVariable("COMSPEC", cmdLine, USRMNUCMDLINE_MAXLEN - 20);
                        AddDoubleQuotesIfNeeded(cmdLine, USRMNUCMDLINE_MAXLEN - 10); // CreateProcess requires the name with spaces in quotes (otherwise it tries various options, see help)
                        if (!UserMenuItems->At(itemIndex)->UseWindow || UserMenuItems->At(itemIndex)->CloseShell)
                            strcat(cmdLine, " /C "); // run command and close immediately after it finishes
                        else
                            strcat(cmdLine, " /K "); // run command and keep the shell open after it finishes

                        char* s = cmdLine + strlen(cmdLine);
                        if ((s - cmdLine) + strlen(batName) < USRMNUCMDLINE_MAXLEN - 2)
                            sprintf(s, "\"%s\"", batName);
                        else
                            strcpy(cmdLine, batName);

                        if (!HANDLES(CreateProcess(NULL, cmdLine, NULL, NULL, FALSE,
                                                   CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                                                   NULL, NULL, &si, &pi)))
                        {
                            DWORD err = GetLastError();
                            char buff[4 * MAX_PATH];
                            if (strlen(cmdLine) > 2 * MAX_PATH)
                                strcpy(cmdLine + 2 * MAX_PATH, "..."); // shorten just in case (probably never needed)
                            sprintf(buff, LoadStr(IDS_EXECERROR), cmdLine, GetErrorText(err));
                            DiskCache.ReleaseName(batUniqueName, FALSE);
                            SalMessageBox(parent, buff, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                        }
                        else
                        {
                            DiskCache.AssignName(batUniqueName, pi.hProcess, TRUE, crtDirect);
                            //            HANDLES(CloseHandle(pi.hProcess));   // handled by DiskCache
                            HANDLES(CloseHandle(pi.hThread));
                        }
                    }
                    else // an empty .BAT is not worth running (in case of low memory or other crazy errors)
                    {
                        DiskCache.ReleaseName(batUniqueName, FALSE);
                    }
                }
                else
                {
                ERROR_LABEL:

                    DiskCache.ReleaseName(batUniqueName, FALSE);
                    if (!skipErrorMessage)
                    {
                        SalMessageBox(parent, GetErrorText(lastErr), LoadStr(IDS_ERRORTITLE),
                                      MB_OK | MB_ICONEXCLAMATION);
                    }
                }
            }
        }
    }
    UpdateWindow(parent);
}

void CMainWindow::SetDefaultDirectories(const char* curPath)
{
    CALL_STACK_MESSAGE2("CMainWindow::SetDefaultDirectories(%s)", curPath);
    //---  restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);
    //---  set environment variables
    char name[4] = "= :";
    const char* dir;
    char d;
    for (d = 'a'; d <= 'z'; d++)
    {
        name[1] = d;
        if (curPath != NULL && d == LowerCase[curPath[0]]) // UNC paths are ignored
            dir = curPath;
        else
            dir = DefaultDir[d - 'a'];

        if (dir[1] == ':' && dir[2] == '\\' && dir[3] == 0)
            SetEnvironmentVariable(name, NULL);
        else
            SetEnvironmentVariable(name, dir);
    }
}

BOOL CMainWindow::HandleCtrlLetter(char c)
{
    CALL_STACK_MESSAGE2("CMainWindow::HandleCtrlLetter(%u)", c);
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
    { // change drive via Shift+letter
        GetActivePanel()->ChangeDrive(c);
    }
    else // NC + Windows Ctrl+? hotkeys
    {
        WPARAM cmd;
        switch (c) // only upper-case characters reach here
        {
        case 'A':
            cmd = CM_ACTIVESELECTALL;
            break;

        case 'C': // copy
        case 'X': // cut
        {
            BOOL files = FALSE;
            if (GetActivePanel() != NULL)
            {
                if (GetActivePanel()->GetCaretIndex() == 0)
                {
                    if (0 == GetActivePanel()->Dirs->Count ||
                        strcmp(GetActivePanel()->Dirs->At(0).Name, "..") != 0)
                    {
                        files = GetActivePanel()->Dirs->Count + GetActivePanel()->Files->Count > 0;
                    }
                    else
                    {
                        int count = GetActivePanel()->GetSelCount();
                        if (count == 1)
                        {
                            int index;
                            GetActivePanel()->GetSelItems(1, &index);
                            files = index != 0;
                        }
                        else
                            files = count > 0;
                    }
                }
                else
                    files = GetActivePanel()->GetCaretIndex() > 0;
            }
            if (!files)
                return FALSE; // cut and copy cannot be performed
            cmd = (c == 'C') ? CM_CLIPCOPY : CM_CLIPCUT;
            break;
        }

        case 'D':
            cmd = CM_ACTIVEUNSELECTALL;
            break;
        case 'E':
            cmd = CM_EMAILFILES;
            break;
        case 'F':
            cmd = CM_FINDFILE;
            break;
        case 'G':
            cmd = CM_ACTIVE_CHANGEDIR;
            break;
        case 'H':
            cmd = CM_TOGGLEHIDDENFILES;
            break;
        case 'I':
            cmd = CM_LAST_PLUGIN_CMD;
            break;
        case 'K':
            cmd = CM_CONVERTFILES;
            break;
        case 'L':
            cmd = CM_DRIVEINFO;
            break;
        case 'M':
            cmd = CM_FILELIST;
            break;
        case 'N':
            cmd = CM_TOGGLEELASTICSMART;
            break;
        case 'P':
            cmd = CM_SEC_PERMISSIONS;
            break;
        case 'Q':
            cmd = CM_OCCUPIEDSPACE;
            break;
        case 'R':
            cmd = CM_ACTIVEREFRESH;
            break;
        case 'S':
            cmd = CM_CLIPPASTELINKS;
            break;
        case 'T':
            cmd = CM_AFOCUSSHORTCUT;
            break;
        case 'U':
            cmd = CM_SWAPPANELS;
            break;
        case 'V':
            cmd = CM_CLIPPASTE;
            break;
        case 'W':
            cmd = CM_RESELECT;
            break;

        default:
            return FALSE;
        }
        SendMessage(HWindow, WM_COMMAND, cmd, 0);
    }
    return TRUE;
}

void CMainWindow::ChangePanel(BOOL force)
{
    CALL_STACK_MESSAGE1("CMainWindow::ChangePanel()");

    MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit
    if (IsIconic(HWindow))
        return;

    CFilesWindow* p1 = GetActivePanel();
    CFilesWindow* p2 = GetNonActivePanel();

    BOOL change = FALSE;
    if (force || p2->CanBeFocused())
        change = TRUE;
    else
    {
        // if a panel is ZOOMed, minimize it and ZOOM the other one
        if (IsPanelZoomed(TRUE) || IsPanelZoomed(FALSE))
        {
            if (IsPanelZoomed(TRUE))
                SplitPosition = 0.0;
            else
                SplitPosition = 1.0;
            LayoutWindows();
            change = TRUE;
        }
    }

    if (change)
    {
        SetActivePanel(p2);

        // ensure the active panel header is redrawn
        if (p1->DirectoryLine != NULL)
            p1->DirectoryLine->InvalidateAndUpdate(FALSE);
        if (p2->DirectoryLine != NULL)
            p2->DirectoryLine->InvalidateAndUpdate(FALSE);

        UpdateDriveBars(); // press the correct drive in the drive bar

        //    ReleaseMenuNew();
        if (EditMode)
        {
            p1->RedrawIndex(p1->FocusedIndex);
            int i = p2->GetCaretIndex();
            i = max(i, 0);
            p2->SetCaretIndex(i, TRUE);
            p2->RedrawIndex(i);
        }
        else
        {
            if (GetFocus() != p2->GetListBoxHWND())
                SetFocus(p2->GetListBoxHWND());
            int i = p2->GetCaretIndex();
            i = max(i, 0);
            p2->SetCaretIndex(i, FALSE);
        }
        EditWindowSetDirectory();
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
        MainWindow->UpdateDefaultDir(TRUE);

        // broadcast this news to all loaded plugins
        Plugins.Event(PLUGINEVENT_PANELACTIVATED, p2 == LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
    }
}

void CMainWindow::FocusPanel(CFilesWindow* focus, BOOL testIfMainWndActive)
{
    CALL_STACK_MESSAGE2("CMainWindow::FocusPanel(, %d)", testIfMainWndActive);
    MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit

    if (!IsIconic(HWindow) && !focus->CanBeFocused())
        focus = ((focus == LeftPanel) ? RightPanel : LeftPanel);

    if (GetFocus() != focus->GetListBoxHWND())
    {
        if (!testIfMainWndActive || GetForegroundWindow() == HWindow) // focus only if main window is active (FTP plugin with non-modal Welcome Message window could focus the panel on command line shutdown -> deactivate Welcome Message)
            SetFocus(focus->GetListBoxHWND());
        else
            focus->OnSetFocus(FALSE); // simulate focus in the panel
    }

    CFilesWindow* old = GetActivePanel();
    SetActivePanel(focus);

    UpdateDriveBars(); // press the correct drive in the drive bar

    // ensure the active panel header is redrawn
    if (old != focus)
    {
        // activated a different panel, let it set its enablers
        RefreshCommandStates();
        // fixes a bug (present in 2.5b10) when users had one panel active with focus on UpDir
        // and then right-clicked a file in the passive panel and chose DELETE from the context menu
        // nothing happened because the EnablerFilesDelete enabler was FALSE (not updated for the new panel)
        // see /viewtopic.php?t=181

        // repaint the directory line of both panels
        if (old->DirectoryLine != NULL)
            old->DirectoryLine->InvalidateAndUpdate(FALSE);
        if (focus->DirectoryLine != NULL)
            focus->DirectoryLine->InvalidateAndUpdate(FALSE);
        //    ReleaseMenuNew();
        EditWindowSetDirectory();
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
        // broadcast this news to loaded plugins
        Plugins.Event(PLUGINEVENT_PANELACTIVATED, focus == LeftPanel ? PANEL_LEFT : PANEL_RIGHT);
    }
    //---  restore DefaultDir
    MainWindow->UpdateDefaultDir(TRUE);
}

void CMainWindow::ShowCommandLine()
{
    CALL_STACK_MESSAGE1("CMainWindow::ShowCommandLine()");
    if (EditWindow == NULL || EditWindow->HWindow != NULL)
        return;

    if (!EditWindow->Create(HWindow, IDC_EDITWINDOW))
        TRACE_E("Unable to create EditWindow.");
    else
    {
        LayoutWindows();
        EditWindow->RestoreContent();
        ShowWindow(EditWindow->HWindow, SW_SHOW);
        if (EditWindow->IsEnabled())
            SetFocus(EditWindow->HWindow);
        IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
    }
}

void CMainWindow::HideCommandLine(BOOL storeContent, BOOL focusPanel)
{
    if (EditWindow == NULL || EditWindow->HWindow == NULL)
        return;

    if (storeContent)
        EditWindow->StoreContent();

    DestroyWindow(EditWindow->HWindow);
    if (focusPanel)
        FocusPanel(GetActivePanel());
    LayoutWindows();
    IdleRefreshStates = TRUE; // on the next Idle, force checking of state variables
}

//****************************************************************************
//
// Image Drag functions
//

void ImageDragBegin(int width, int height, int dxHotspot, int dyHotspot)
{
    if (ImageDragging)
        TRACE_E("ImageDragging == TRUE - this should never happen");
    ImageDragW = width;
    ImageDragH = height;
    ImageDragDxHotspot = dxHotspot;
    ImageDragDyHotspot = dyHotspot;
    ImageDragging = TRUE;
}

void ImageDragEnd()
{
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = INT_MAX;
    ImageDragY = INT_MAX;
    ImageDragW = INT_MAX;
    ImageDragH = INT_MAX;
    ImageDragging = FALSE;
}

BOOL ImageDragInterfereRect(const RECT* rect)
{
    if (!ImageDraggingVisible)
        return FALSE;
    if (ImageDragX == INT_MAX || ImageDragY == INT_MAX)
    {
        TRACE_E("ImageDragX == INT_MAX || ImageDragY == INT_MAX");
        return TRUE; // just to be safe
    }
    if (ImageDragW == INT_MAX || ImageDragH == INT_MAX)
    {
        TRACE_E("ImageDragW == INT_MAX || ImageDragH == INT_MAX");
        return TRUE; // just to be safe
    }
    RECT r;
    r.left = ImageDragX - ImageDragDxHotspot;
    r.top = ImageDragY - ImageDragDyHotspot;
    r.right = r.left + ImageDragW;
    r.bottom = r.top + ImageDragH;
    RECT dstR;
    IntersectRect(&dstR, rect, &r);
    return !IsRectEmpty(&dstR);
}

void ImageDragEnter(int x, int y)
{
    CALL_STACK_MESSAGE3("ImageDragEnter(%d, %d)", x, y);
    if (ImageDraggingVisible)
        TRACE_E("ImageDraggingVisible == TRUE - this should never happen");
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = x;
    ImageDragY = y;
    ShowCaretAfterDrop = MainWindow->EditWindow->HideCaret();
    ImageList_DragEnter(MainWindow->HWindow, x - MainWindow->WindowRect.left, y - MainWindow->WindowRect.top);
    ImageDraggingVisible = TRUE;
    ImageDraggingVisibleLevel = 1;
}

void ImageDragMove(int x, int y)
{
    CALL_STACK_MESSAGE3("ImageDragMove(%d, %d)", x, y);
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDragX = x;
    ImageDragY = y;
    ImageList_DragMove(x - MainWindow->WindowRect.left, y - MainWindow->WindowRect.top);
}

void ImageDragLeave()
{
    CALL_STACK_MESSAGE1("ImageDragLeave()");
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageList_DragLeave(MainWindow->HWindow);
    ImageDraggingVisible = FALSE;
    ImageDraggingVisibleLevel = 0;
    ImageDragX = INT_MAX;
    ImageDragY = INT_MAX;
    if (ShowCaretAfterDrop)
    {
        MainWindow->EditWindow->ShowCaret();
        ShowCaretAfterDrop = FALSE;
    }
}

void ImageDragShow(BOOL show)
{
    CALL_STACK_MESSAGE2("ImageDragShow(%d)", show);
    if (!ImageDragging)
        TRACE_E("ImageDragging == FALSE - this should never happen");
    ImageDraggingVisibleLevel += show ? 1 : -1;

    if (show && ImageDraggingVisibleLevel == 1)
    {
        ImageList_DragShowNolock(TRUE);
        ImageDraggingVisible = TRUE;
    }
    if (!show && ImageDraggingVisibleLevel == 0)
    {
        ImageList_DragShowNolock(FALSE);
        ImageDraggingVisible = FALSE;
    }
}

//****************************************************************************
//
// Context Help (Shift+F1) support
//

/////////////////////////////////////////////////////////////////////////////

HWND GetParentOwner(HWND hWnd)
{
    CALL_STACK_MESSAGE_NONE
    // return parent in the Windows sense
    return (GetWindowLongPtr(hWnd, GWL_STYLE) & WS_CHILD) ? GetParent(hWnd) : GetWindow(hWnd, GW_OWNER);
}

HWND GetTopLevelParent(HWND hWindow)
{
    CALL_STACK_MESSAGE_NONE
    HWND hWndParent = hWindow;
    HWND hWndT;
    while ((hWndT = GetParentOwner(hWndParent)) != NULL)
        hWndParent = hWndT;

    return hWndParent;
}

BOOL IsDescendant(HWND hWndParent, HWND hWndChild)
{
    CALL_STACK_MESSAGE_NONE
    // helper for detecting whether child descendent of parent
    //  (works with owned popups as well)
    if (!IsWindow(hWndParent))
    {
        TRACE_E("hWndParent is not window");
        return FALSE;
    }
    if (!IsWindow(hWndChild))
    {
        TRACE_E("hWndChild is not window");
        return FALSE;
    }

    do
    {
        if (hWndParent == hWndChild)
            return TRUE;

        hWndChild = GetParentOwner(hWndChild);
    } while (hWndChild != NULL);

    return FALSE;
}

DWORD
CMainWindow::MapClientArea(POINT point)
{
    DWORD dwContext = 0;

    CMainWindowsHitTestEnum hit = HitTest(point.x, point.y);
    CToolBar* toolbar = NULL;

    switch (hit)
    {
    case mwhteMenu:
        dwContext = IDH_MENUBAR;
        break;

    case mwhteTopToolbar:
    {
        dwContext = IDH_TOPTOOLBAR;
        toolbar = TopToolBar;
        break;
    }

    case mwhtePluginsBar:
        dwContext = IDH_PLUGINSBAR;
        break;

    case mwhteMiddleToolbar:
    {
        dwContext = IDH_MIDDLETOOLBAR;
        toolbar = MiddleToolBar;
        break;
    }

    case mwhteUMToolbar:
        dwContext = IDH_UMTOOLBAR;
        break;

    case mwhteHPToolbar:
        dwContext = IDH_HPTOOLBAR;
        break;

    case mwhteDriveBar:
        dwContext = IDH_DRIVEBAR;
        break;

    case mwhteCmdLine:
        dwContext = IDH_COMMANDLINE;
        break;

    case mwhteBottomToolbar:
    {
        dwContext = IDH_BOTTOMTOOLBAR;
        toolbar = BottomToolBar;
        break;
    }

    case mwhteSplitLine:
        dwContext = IDH_SPLITBAR;
        break;

    case mwhteLeftDirLine:
    {
        dwContext = IDH_DIRECTORYLINE;

        if (LeftPanel->DirectoryLine->ToolBar != NULL &&
            LeftPanel->DirectoryLine->ToolBar->HWindow != NULL)
            toolbar = LeftPanel->DirectoryLine->ToolBar;
        break;
    }

    case mwhteRightDirLine:
    {
        dwContext = IDH_DIRECTORYLINE;

        if (RightPanel->DirectoryLine->ToolBar != NULL &&
            RightPanel->DirectoryLine->ToolBar->HWindow != NULL)
            toolbar = RightPanel->DirectoryLine->ToolBar;
        break;
    }

    case mwhteLeftHeaderLine:
    case mwhteRightHeaderLine:
        dwContext = IDH_HEADERLINE;
        break;

    case mwhteLeftStatusLine:
    case mwhteRightStatusLine:
        dwContext = IDH_INFOLINE;
        break;

    case mwhteLeftWorkingArea:
    case mwhteRightWorkingArea:
        dwContext = IDH_WORKINGAREA;
        break;
    }

    // get the ID of the button the user clicked
    if (toolbar != NULL)
    {
        POINT p;
        p = point;
        ScreenToClient(toolbar->HWindow, &p);
        int index = toolbar->HitTest(p.x, p.y);
        if (index != -1)
        {
            TLBI_ITEM_INFO2 tii;
            tii.Mask = TLBI_MASK_ID;
            if (toolbar->GetItemInfo2(index, TRUE, &tii))
                dwContext = tii.ID;
        }
    }
    return dwContext;
}

DWORD
CMainWindow::MapNonClientArea(int iHit)
{
    DWORD dwContext = 0;
    switch (iHit)
    {
        /*
    case HTBORDER:
    case HTBOTTOM:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
    case HTLEFT:
    case HTRIGHT:
    case HTTOP:
    case HTTOPLEFT:
    case HTTOPRIGHT:
    case HTCAPTION:
    case HTREDUCE:
    case HTZOOM:
*/

    case HTMINBUTTON:
    case HTMAXBUTTON:
    case HTCLOSE:
        dwContext = IDH_MINMAXCLOSEBTNS;
        break;
    }
    return dwContext;
}


void CMainWindow::UpdateDriveBars()
{
    if (DriveBar == NULL || DriveBar2 == NULL)
        return;

    if (DriveBar->HWindow == NULL)
        return;

    if (DriveBar2->HWindow == NULL)
    {
        // when there is only one drive bar it belongs to the active panel
        DriveBar->SetCheckedDrive(GetActivePanel());
    }
    else
    {
        DriveBar->SetCheckedDrive(LeftPanel);
        DriveBar2->SetCheckedDrive(RightPanel);
    }
}

void CMainWindow::CancelPanelsUI()
{
    LeftPanel->CancelUI();
    RightPanel->CancelUI();
}

BOOL CMainWindow::QuickRenameWindowActive()
{
    return (LeftPanel->IsQuickRenameActive() || RightPanel->IsQuickRenameActive());
}

BOOL CMainWindow::DoQuickRename()
{
    if (LeftPanel->IsQuickRenameActive())
        return LeftPanel->HandeQuickRenameWindowKey(VK_RETURN);
    if (RightPanel->IsQuickRenameActive())
        return RightPanel->HandeQuickRenameWindowKey(VK_RETURN);
    return TRUE; // OK
}

//
// ****************************************************************************
// LockUI
//

void CMainWindow::LockUI(BOOL lock, HWND hToolWnd, const char* lockReason)
{
    if (LockedUI && lock)
    {
        TRACE_E("CMainWindow::LockUI(): main window is already locked! Ignoring this request...");
        return;
    }
    if (!LockedUI && !lock)
    {
        TRACE_E("CMainWindow::LockUI(): main window is not locked! Ignoring this request...");
        return;
    }

    LockedUI = lock;
    if (lock)
    {
        LockedUIToolWnd = hToolWnd;
        if (lockReason != NULL)
            LockedUIReason = DupStr(lockReason);
    }
    else
    {
        LockedUIToolWnd = NULL;
        if (LockedUIReason != NULL)
            free(LockedUIReason);
    }

    if (HTopRebar != NULL)
        EnableWindow(HTopRebar, !lock);
    if (MiddleToolBar != NULL && MiddleToolBar->HWindow != NULL)
        EnableWindow(MiddleToolBar->HWindow, !lock);
    if (BottomToolBar != NULL && BottomToolBar->HWindow != NULL)
        EnableWindow(BottomToolBar->HWindow, !lock);
    LeftPanel->LockUI(lock);
    RightPanel->LockUI(lock);
}

void CMainWindow::BringLockedUIToolWnd()
{
    if (LockedUIToolWnd != NULL)
        SetWindowPos(LockedUIToolWnd, HWindow, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOSENDCHANGING | SWP_NOREDRAW);
}

// ****************************************************************************

CFilesWindow*
CMainWindow::GetPanel(int panel)
{
    switch (panel)
    {
    case PANEL_SOURCE:
        return GetActivePanel();
    case PANEL_TARGET:
        return GetNonActivePanel();
    case PANEL_LEFT:
        return LeftPanel;
    case PANEL_RIGHT:
        return RightPanel;
    default:
        TRACE_E("Invalid panel (PANEL_XXX) constant: " << panel);
        return NULL;
    }
}

void CMainWindow::PostFocusNameInPanel(int panel, const char* path, const char* name)
{
    CALL_STACK_MESSAGE4("CMainWindow::FocusNameInPanel(%d, %s, %s)", panel, path, name);
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        static char pathBackup[MAX_PATH + 200];
        static char nameBackup[MAX_PATH + 200];
        // Deferred focus messages require complete path and file-name identities.
        if (FAILED(StringCchCopyA(pathBackup, _countof(pathBackup), path)) ||
            FAILED(StringCchCopyA(nameBackup, _countof(nameBackup), name)))
            return;
        PostMessage(p->HWindow, WM_USER_FOCUSFILE, (WPARAM)nameBackup, (LPARAM)pathBackup);
    }
}

//****************************************************************************
//

//****************************************************************************
//
// CMainWindow::HandleWmCommand
//
// Dispatches all WM_COMMAND menu and toolbar commands.
// Extracted from the WindowProc switch in mainwnd_messages.cpp.
//

#include "snooper.h"
#include "shellib.h"
#include "pack.h"
#include "filesbox.h"
#include "drivelst.h"
#include "worker.h"
#include "find.h"
#include "viewer.h"
#include "gui.h"
#include "tasklist.h"
#include "jumplist.h"
#include "execlog.h"
#include "htmlhelp.h"

// Helper: invokes a shell context-menu command with temporary thread-priority reduction
// to prevent misbehaving shell extensions from hogging the CPU.
static void CMainWindowWindowProcAux(IContextMenu* menu2, CMINVOKECOMMANDINFO& ici)
{
    CALL_STACK_MESSAGE_NONE

    // temporarily lower the thread priority so a misbehaving shell extension does not hog the CPU
    HANDLE hThread = GetCurrentThread(); // pseudo-handle, no need to release
    int oldThreadPriority = GetThreadPriority(hThread);
    SetThreadPriority(hThread, THREAD_PRIORITY_NORMAL);

    __try
    {
        menu2->InvokeCommand(&ici);
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 12))
    {
        ICExceptionHasOccured++;
    }

    SetThreadPriority(hThread, oldThreadPriority);
}

LRESULT CMainWindow::HandleWmCommand(WPARAM wParam, LPARAM lParam)
{
        if (HelpMode && (HWND)lParam == NULL && LOWORD(wParam) != CM_HELP_CONTEXT)
        {
            DWORD id = LOWORD(wParam);

            if (id >= CM_PLUGINCMD_MIN && id <= CM_PLUGINCMD_MAX)
            { // command of a plugin (submenu of Plugins menu)
                if (Plugins.HelpForMenuItem(HWindow, LOWORD(wParam)))
                    return 0;
                else
                    id = CM_LAST_PLUGIN_CMD; // if the plugin has no help, show Salamander's help "Using Plugins"
            }

            // adjust ranges to their first value
            if (id > CM_USERMENU_MIN && id <= CM_USERMENU_MAX)
                id = CM_USERMENU_MIN;
            if (id > CM_DRIVEBAR_MIN && id <= CM_DRIVEBAR_MAX)
                id = CM_DRIVEBAR_MIN;
            if (id > CM_DRIVEBAR2_MIN && id <= CM_DRIVEBAR2_MAX)
                id = CM_DRIVEBAR2_MIN;
            if (id > CM_PLUGINCFG_MIN && id <= CM_PLUGINCFG_MAX)
                id = CM_PLUGINCFG_MIN;
            if (id > CM_PLUGINABOUT_MIN && id <= CM_PLUGINABOUT_MAX)
                id = CM_PLUGINABOUT_MIN;

            if (id > CM_ACTIVEMODE_1 && id <= CM_ACTIVEMODE_10)
                id = CM_ACTIVEMODE_1;
            if (id > CM_LEFTMODE_1 && id <= CM_LEFTMODE_10)
                id = CM_LEFTMODE_1;
            if (id > CM_RIGHTMODE_1 && id <= CM_RIGHTMODE_10)
                id = CM_RIGHTMODE_1;

            if (id > CM_LEFTSORTBY_MIN && id <= CM_LEFTSORTBY_MAX)
                id = CM_LEFTSORTBY_MIN;
            if (id > CM_RIGHTSORTBY_MIN && id <= CM_RIGHTSORTBY_MAX)
                id = CM_RIGHTSORTBY_MIN;

            if (id > CM_LEFTHOTPATH_MIN && id <= CM_LEFTHOTPATH_MAX)
                id = CM_LEFTHOTPATH_MIN;
            if (id > CM_RIGHTHOTPATH_MIN && id <= CM_RIGHTHOTPATH_MAX)
                id = CM_RIGHTHOTPATH_MIN;

            if (id > CM_LEFTHISTORYPATH_MIN && id <= CM_LEFTHISTORYPATH_MAX)
                id = CM_LEFTHISTORYPATH_MIN;
            if (id > CM_RIGHTHISTORYPATH_MIN && id <= CM_RIGHTHISTORYPATH_MAX)
                id = CM_RIGHTHISTORYPATH_MIN;

            if (id > CM_CODING_MIN && id <= CM_CODING_MAX)
                id = CM_CODING_MIN;

            if (id > CM_NEWMENU_MIN && id <= CM_NEWMENU_MAX)
                id = CM_NEWMENU_MIN;

            OpenHtmlHelp(NULL, HWindow, HHCDisplayContext, id, FALSE);

            return 0;
        }
        CFilesWindow* activePanel = GetActivePanel();
        if (activePanel == NULL || LeftPanel == NULL || RightPanel == NULL)
        {
            TRACE_E("activePanel == NULL || LeftPanel == NULL || RightPanel == NULL");
            return 0;
        }

        // exit quick-search mode
        if (LOWORD(wParam) != CM_ACTIVEREFRESH &&         // except refresh in the active panel
            LOWORD(wParam) != CM_LEFTREFRESH &&           // except refresh in the left panel
            LOWORD(wParam) != CM_RIGHTREFRESH &&          // except refresh in the right panel
            (HIWORD(wParam) == 0 || HIWORD(wParam) == 1)) // only from menu or accelerator
        {
            CancelPanelsUI(); // cancel QuickSearch and QuickEdit
        }

        if (LOWORD(wParam) >= CM_NEWMENU_MIN && LOWORD(wParam) <= CM_NEWMENU_MAX)
        { // command from the New menu
            if (ContextMenuNew->MenuIsAssigned() && activePanel->CheckPath(TRUE) == ERROR_SUCCESS)
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                {
                    char newMenuDetail[64];
                    _snprintf_s(newMenuDetail, _TRUNCATE, "id=%u", (unsigned)LOWORD(wParam));
                    ExecLogFeatureStart("new item", newMenuDetail);
                    CALL_STACK_MESSAGE1("CMainWindow::WindowProc::menu_new");
                    IContextMenu* menu2 = ContextMenuNew->GetMenu2();
                    menu2->AddRef(); // just in case ContextMenuNew vanishes asynchronously (message loop)
                    CShellExecuteWnd shellExecuteWnd;
                    CMINVOKECOMMANDINFO ici;
                    ici.cbSize = sizeof(CMINVOKECOMMANDINFO);
                    ici.fMask = 0;
                    ici.hwnd = shellExecuteWnd.Create(HWindow, "SEW: CMainWindow::WindowProc cmd=%d", LOWORD(wParam) - CM_NEWMENU_MIN);
                    ici.lpVerb = MAKEINTRESOURCE((LOWORD(wParam) - CM_NEWMENU_MIN));
                    ici.lpParameters = NULL;
                    ici.lpDirectory = activePanel->GetPath();
                    ici.nShow = SW_SHOWNORMAL;
                    ici.dwHotKey = 0;
                    ici.hIcon = 0;
                    activePanel->FocusFirstNewItem = TRUE; // select the newly generated file/directory

                    CMainWindowWindowProcAux(menu2, ici);
                    ExecLogFeatureResult("new item", newMenuDetail, TRUE);

                    menu2->Release();
                }
                //---  refresh directories that are not automatically refreshed
                // announce a change in the current directory (a new file or directory is most likely created there)
                MainWindow->PostChangeOnPathNotification(activePanel->GetPath(), FALSE);
            }
            else
            {
                ExecLogFeatureResult("new item", "menu unavailable", FALSE);
                TRACE_E("ContextMenuNew is not valid anymore, it is not posible to invoke menu New command.");
            }
            return 0;
        }

        if (LOWORD(wParam) >= CM_PLUGINABOUT_MIN && LOWORD(wParam) <= CM_PLUGINABOUT_MAX)
        {
            Plugins.OnPluginAbout(HWindow, LOWORD(wParam) - CM_PLUGINABOUT_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_PLUGINCFG_MIN && LOWORD(wParam) <= CM_PLUGINCFG_MAX)
        {
            Plugins.OnPluginConfiguration(HWindow, LOWORD(wParam) - CM_PLUGINCFG_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_PLUGINCMD_MIN && LOWORD(wParam) <= CM_PLUGINCMD_MAX)
        { // command from a plugin menu
            // lower the thread priority to "normal" (so operations don't burden the system)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            char pluginCmdDetail[64];
            _snprintf_s(pluginCmdDetail, _TRUNCATE, "id=%u", (unsigned)LOWORD(wParam));
            ExecLogFeatureStart("plugin command", pluginCmdDetail);
            BOOL pluginCmdResult = Plugins.ExecuteMenuItem(activePanel, HWindow, LOWORD(wParam));
            ExecLogFeatureResult("plugin command", pluginCmdDetail, pluginCmdResult);
            if (pluginCmdResult)
            {
                activePanel->StoreSelection();                               // save selection for Restore Selection command
                activePanel->SetSel(FALSE, -1, TRUE);                        // explicit redraw
                PostMessage(activePanel->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            // restoring the contents of non-automatic panels is up to plugins

            UpdateWindow(HWindow);
            return 0;
        }

        if (LOWORD(wParam) == CM_LAST_PLUGIN_CMD)
        { // Plugins/Last Command action
            // lower the thread priority to "normal" (so operations don't burden the system)
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

            ExecLogFeatureStart("plugin last command", "");
            BOOL lastCmdResult = Plugins.OnLastCommand(activePanel, HWindow);
            ExecLogFeatureResult("plugin last command", "", lastCmdResult);
            if (lastCmdResult)
            {
                activePanel->StoreSelection();                               // save selection for Restore Selection command
                activePanel->SetSel(FALSE, -1, TRUE);                        // explicit redraw
                PostMessage(activePanel->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
            }

            // raise the thread priority again, the operation has finished
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

            // restoring the contents of non-automatic panels is up to plugins

            UpdateWindow(HWindow);
            return 0;
        }

        if (LOWORD(wParam) >= CM_USERMENU_MIN && LOWORD(wParam) <= CM_USERMENU_MAX)
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command

                CUserMenuAdvancedData userMenuAdvancedData;

                char* list = userMenuAdvancedData.ListOfSelNames;
                char* listEnd = list + USRMNUARGS_MAXLEN - 1;
                BOOL smallBuf = FALSE;
                if (activePanel->SelectedCount > 0)
                {
                    int count = activePanel->Files->Count + activePanel->Dirs->Count;
                    int i;
                    for (i = 0; i < count; i++)
                    {
                        CFileData* file = (i < activePanel->Dirs->Count) ? &activePanel->Dirs->At(i) : &activePanel->Files->At(i - activePanel->Dirs->Count);
                        if (file->Selected)
                        {
                            if (list > userMenuAdvancedData.ListOfSelNames)
                            {
                                if (list < listEnd)
                                    *list++ = ' ';
                                else
                                    break;
                            }
                            if (!AddToListOfNames(&list, listEnd, file->Name, file->NameLen))
                                break;
                        }
                    }
                    if (i < count)
                        smallBuf = TRUE;
                }
                else // take the focused item
                {
                    BOOL subDir;
                    if (activePanel->Dirs->Count > 0)
                        subDir = (strcmp(activePanel->Dirs->At(0).Name, "..") == 0);
                    else
                        subDir = FALSE;
                    int index = activePanel->GetCaretIndex();
                    if (index >= 0 && index < activePanel->Files->Count + activePanel->Dirs->Count &&
                        (index != 0 || !subDir))
                    {
                        CFileData* file = (index < activePanel->Dirs->Count) ? &activePanel->Dirs->At(index) : &activePanel->Files->At(index - activePanel->Dirs->Count);
                        if (!AddToListOfNames(&list, listEnd, file->Name, file->NameLen))
                            smallBuf = TRUE;
                    }
                }
                if (smallBuf)
                {
                    userMenuAdvancedData.ListOfSelNames[0] = 0; // small buffer for the list of selected names
                    userMenuAdvancedData.ListOfSelNamesIsEmpty = FALSE;
                }
                else
                {
                    *list = 0;
                    userMenuAdvancedData.ListOfSelNamesIsEmpty = userMenuAdvancedData.ListOfSelNames[0] == 0;
                }

                char* listFull = userMenuAdvancedData.ListOfSelFullNames;
                char* listFullEnd = listFull + USRMNUARGS_MAXLEN - 1;
                smallBuf = FALSE;
                char fullName[MAX_PATH];
                if (activePanel->SelectedCount > 0)
                {
                    int count = activePanel->Files->Count + activePanel->Dirs->Count;
                    int i;
                    for (i = 0; i < count; i++)
                    {
                        CFileData* file = (i < activePanel->Dirs->Count) ? &activePanel->Dirs->At(i) : &activePanel->Files->At(i - activePanel->Dirs->Count);
                        if (file->Selected)
                        {
                            if (listFull > userMenuAdvancedData.ListOfSelFullNames)
                            {
                                if (listFull < listFullEnd)
                                    *listFull++ = ' ';
                                else
                                    break;
                            }
                            // Selected-file lists must contain complete filesystem identities.
                            if (FAILED(StringCchCopyA(fullName, _countof(fullName), activePanel->GetPath())) ||
                                !SalPathAppend(fullName, file->Name, MAX_PATH) ||
                                !AddToListOfNames(&listFull, listFullEnd, fullName, (int)strlen(fullName)))
                                break;
                        }
                    }
                    if (i < count)
                        smallBuf = TRUE;
                }
                else // take the focused item
                {
                    BOOL subDir;
                    if (activePanel->Dirs->Count > 0)
                        subDir = (strcmp(activePanel->Dirs->At(0).Name, "..") == 0);
                    else
                        subDir = FALSE;
                    int index = activePanel->GetCaretIndex();
                    if (index >= 0 && index < activePanel->Files->Count + activePanel->Dirs->Count &&
                        (index != 0 || !subDir))
                    {
                        CFileData* file = (index < activePanel->Dirs->Count) ? &activePanel->Dirs->At(index) : &activePanel->Files->At(index - activePanel->Dirs->Count);
                        // The focused-item list follows the same complete-path invariant.
                        if (FAILED(StringCchCopyA(fullName, _countof(fullName), activePanel->GetPath())) ||
                            !SalPathAppend(fullName, file->Name, MAX_PATH) ||
                            !AddToListOfNames(&listFull, listFullEnd, fullName, (int)strlen(fullName)))
                        {
                            smallBuf = TRUE;
                        }
                    }
                }
                if (smallBuf)
                {
                    userMenuAdvancedData.ListOfSelFullNames[0] = 0; // small buffer for the list of selected full names
                    userMenuAdvancedData.ListOfSelFullNamesIsEmpty = FALSE;
                }
                else
                {
                    *listFull = 0;
                    userMenuAdvancedData.ListOfSelFullNamesIsEmpty = userMenuAdvancedData.ListOfSelFullNames[0] == 0;
                }

                if (LeftPanel->Is(ptDisk))
                {
                    // User-menu paths are execution identities and must remain complete.
                    if (FAILED(StringCchCopyA(userMenuAdvancedData.FullPathLeft, _countof(userMenuAdvancedData.FullPathLeft), LeftPanel->GetPath())) ||
                        !SalPathAddBackslash(userMenuAdvancedData.FullPathLeft, MAX_PATH))
                        userMenuAdvancedData.FullPathLeft[0] = 0;
                }
                else
                    userMenuAdvancedData.FullPathLeft[0] = 0;
                if (RightPanel->Is(ptDisk))
                {
                    // User-menu paths are execution identities and must remain complete.
                    if (FAILED(StringCchCopyA(userMenuAdvancedData.FullPathRight, _countof(userMenuAdvancedData.FullPathRight), RightPanel->GetPath())) ||
                        !SalPathAddBackslash(userMenuAdvancedData.FullPathRight, MAX_PATH))
                        userMenuAdvancedData.FullPathRight[0] = 0;
                }
                else
                    userMenuAdvancedData.FullPathRight[0] = 0;
                userMenuAdvancedData.FullPathInactive = (activePanel == LeftPanel) ? userMenuAdvancedData.FullPathRight : userMenuAdvancedData.FullPathLeft;

                userMenuAdvancedData.CompareName1[0] = 0;
                userMenuAdvancedData.CompareName2[0] = 0;
                userMenuAdvancedData.CompareNamesAreDirs = FALSE;
                userMenuAdvancedData.CompareNamesReversed = FALSE;
                CFilesWindow* inactivePanel = (activePanel == LeftPanel) ? RightPanel : LeftPanel;
                CFileData* f1 = NULL;
                CFileData* f2 = NULL;
                BOOL f2FromInactPanel = FALSE;
                int focus = activePanel->GetCaretIndex();
                BOOL focusOnUpDir = (focus == 0 && activePanel->Dirs->Count > 0 &&
                                     strcmp(activePanel->Dirs->At(0).Name, "..") == 0);
                int indexes[3];
                int selCount = activePanel->GetSelItems(3, indexes); // interested in: 0-2=number selected, 3=more than two
                int tgtIndexes[2];
                int tgtSelCount = inactivePanel->Is(ptDisk) ? inactivePanel->GetSelItems(2, tgtIndexes) : 0; // interested in: 0-1=number selected, 2=more than one
                if (selCount == 2)                                                                           // two selected items in the source panel
                {
                    if ((indexes[0] < activePanel->Dirs->Count) == (indexes[1] < activePanel->Dirs->Count)) // both items are files/directories
                    {
                        f1 = (indexes[0] < activePanel->Dirs->Count) ? &activePanel->Dirs->At(indexes[0]) : &activePanel->Files->At(indexes[0] - activePanel->Dirs->Count);
                        f2 = (indexes[1] < activePanel->Dirs->Count) ? &activePanel->Dirs->At(indexes[1]) : &activePanel->Files->At(indexes[1] - activePanel->Dirs->Count);
                        userMenuAdvancedData.CompareNamesAreDirs = (indexes[0] < activePanel->Dirs->Count);
                    }
                }
                else
                {
                    if (selCount == 1) // one selected item in the source panel
                    {
                        f1 = (indexes[0] < activePanel->Dirs->Count) ? &activePanel->Dirs->At(indexes[0]) : &activePanel->Files->At(indexes[0] - activePanel->Dirs->Count);
                        userMenuAdvancedData.CompareNamesAreDirs = (indexes[0] < activePanel->Dirs->Count);
                        if (!focusOnUpDir && focus != indexes[0] && tgtSelCount != 1)
                        {
                            if ((focus < activePanel->Dirs->Count) == userMenuAdvancedData.CompareNamesAreDirs) // both items are files/directories
                            {
                                f2 = (focus < activePanel->Dirs->Count) ? &activePanel->Dirs->At(focus) : &activePanel->Files->At(focus - activePanel->Dirs->Count);
                            }
                        }
                    }
                    else
                    {
                        if (selCount == 0) // no selected item in the source panel, take the focus
                        {
                            if (!focusOnUpDir)
                            {
                                if (focus >= 0 && focus < activePanel->Dirs->Count + activePanel->Files->Count)
                                {
                                    f1 = (focus < activePanel->Dirs->Count) ? &activePanel->Dirs->At(focus) : &activePanel->Files->At(focus - activePanel->Dirs->Count);
                                    userMenuAdvancedData.CompareNamesAreDirs = (focus < activePanel->Dirs->Count);
                                }
                            }
                        }
                    }
                }
                if (f1 != NULL && f2 == NULL)
                {
                    if (tgtSelCount == 1 &&
                        (tgtIndexes[0] < inactivePanel->Dirs->Count) == userMenuAdvancedData.CompareNamesAreDirs) // both items are files/directories
                    {
                        f2 = (tgtIndexes[0] < inactivePanel->Dirs->Count) ? &inactivePanel->Dirs->At(tgtIndexes[0]) : &inactivePanel->Files->At(tgtIndexes[0] - inactivePanel->Dirs->Count);
                        f2FromInactPanel = TRUE;
                    }
                    else
                    {
                        if (inactivePanel->Is(ptDisk))
                        {
                            int c = inactivePanel->Dirs->Count + inactivePanel->Files->Count;
                            int i;
                            for (i = 0; i < c; i++)
                            {
                                CFileData* f = (i < inactivePanel->Dirs->Count) ? &inactivePanel->Dirs->At(i) : &inactivePanel->Files->At(i - inactivePanel->Dirs->Count);
                                if (f->NameLen == f1->NameLen &&
                                    StrICmp(f->Name, f1->Name) == 0)
                                {
                                    if ((i < inactivePanel->Dirs->Count) == userMenuAdvancedData.CompareNamesAreDirs) // both items are files/directories
                                    {
                                        f2 = f;
                                        f2FromInactPanel = TRUE;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
                if (f1 != NULL)
                {
                    // Comparison inputs must be complete before the filename is appended.
                    if (FAILED(StringCchCopyA(userMenuAdvancedData.CompareName1, _countof(userMenuAdvancedData.CompareName1), activePanel->GetPath())) ||
                        !SalPathAppend(userMenuAdvancedData.CompareName1, f1->Name, MAX_PATH))
                        userMenuAdvancedData.CompareName1[0] = 0;
                }
                if (f2 != NULL)
                {
                    // Comparison inputs must be complete before the filename is appended.
                    if (FAILED(StringCchCopyA(userMenuAdvancedData.CompareName2, _countof(userMenuAdvancedData.CompareName2),
                                              (f2FromInactPanel ? inactivePanel : activePanel)->GetPath())) ||
                        !SalPathAppend(userMenuAdvancedData.CompareName2, f2->Name, MAX_PATH))
                        userMenuAdvancedData.CompareName2[0] = 0;
                    else
                    {
                        if (f2FromInactPanel && inactivePanel == LeftPanel)
                            userMenuAdvancedData.CompareNamesReversed = TRUE;
                    }
                }
                if (userMenuAdvancedData.CompareName1[0] != 0 &&
                    userMenuAdvancedData.CompareName2[0] == 0 && activePanel == RightPanel)
                {
                    userMenuAdvancedData.CompareNamesReversed = TRUE;
                }

                CUMDataFromPanel data(activePanel);
                SetCurrentDirectory(activePanel->GetPath());
                UserMenu(HWindow, LOWORD(wParam) - CM_USERMENU_MIN, GetNextFileFromPanel,
                         &data, &userMenuAdvancedData);
                SetCurrentDirectoryToSystem();
            }
            return 0;
        }

        if (LOWORD(wParam) >= CM_VIEWWITH_MIN && LOWORD(wParam) <= CM_VIEWWITH_MAX)
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->OnViewFileWith(LOWORD(wParam) - CM_VIEWWITH_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_EDITWITH_MIN && LOWORD(wParam) <= CM_EDITWITH_MAX)
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->OnEditFileWith(LOWORD(wParam) - CM_EDITWITH_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_DRIVEBAR_MIN && LOWORD(wParam) <= CM_DRIVEBAR_MAX)
        {
            DriveBar->Execute(LOWORD(wParam));
            return 0;
        }

        if (LOWORD(wParam) >= CM_DRIVEBAR2_MIN && LOWORD(wParam) <= CM_DRIVEBAR2_MAX)
        {
            DriveBar2->Execute(LOWORD(wParam));
            return 0;
        }

        if (LOWORD(wParam) >= CM_ACTIVEHOTPATH_MIN && LOWORD(wParam) < CM_ACTIVEHOTPATH_MIN + HOT_PATHS_COUNT)
        {
            activePanel->GotoHotPath(LOWORD(wParam) - CM_ACTIVEHOTPATH_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_LEFTHOTPATH_MIN && LOWORD(wParam) < CM_LEFTHOTPATH_MIN + HOT_PATHS_COUNT)
        {
            LeftPanel->GotoHotPath(LOWORD(wParam) - CM_LEFTHOTPATH_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_RIGHTHOTPATH_MIN && LOWORD(wParam) < CM_RIGHTHOTPATH_MIN + HOT_PATHS_COUNT)
        {
            RightPanel->GotoHotPath(LOWORD(wParam) - CM_RIGHTHOTPATH_MIN);
            return 0;
        }

        if (LOWORD(wParam) >= CM_LEFTHISTORYPATH_MIN && LOWORD(wParam) <= CM_LEFTHISTORYPATH_MAX)
        {
            DirHistory->Execute(LOWORD(wParam) - CM_LEFTHISTORYPATH_MIN + 1, FALSE, LeftPanel, TRUE, FALSE);
            return 0;
        }

        if (LOWORD(wParam) >= CM_RIGHTHISTORYPATH_MIN && LOWORD(wParam) <= CM_RIGHTHISTORYPATH_MAX)
        {
            DirHistory->Execute(LOWORD(wParam) - CM_RIGHTHISTORYPATH_MIN + 1, FALSE, RightPanel, TRUE, FALSE);
            return 0;
        }

        if (LOWORD(wParam) >= CM_ACTIVEMODE_1 && LOWORD(wParam) <= CM_ACTIVEMODE_10)
        {
            int index = LOWORD(wParam) - CM_ACTIVEMODE_1;
            if (activePanel->IsViewTemplateValid(index))
                activePanel->SelectViewTemplate(index, TRUE, FALSE);
            return 0;
        }

        if (LOWORD(wParam) >= CM_LEFTMODE_1 && LOWORD(wParam) <= CM_LEFTMODE_10)
        {
            int index = LOWORD(wParam) - CM_LEFTMODE_1;
            if (LeftPanel->IsViewTemplateValid(index))
                LeftPanel->SelectViewTemplate(index, TRUE, FALSE);
            return 0;
        }

        if (LOWORD(wParam) >= CM_RIGHTMODE_1 && LOWORD(wParam) <= CM_RIGHTMODE_10)
        {
            int index = LOWORD(wParam) - CM_RIGHTMODE_1;
            if (RightPanel->IsViewTemplateValid(index))
                RightPanel->SelectViewTemplate(index, TRUE, FALSE);
            return 0;
        }

        if (LOWORD(wParam) >= CM_ACTIVEHOTPATH_MIN && LOWORD(wParam) < CM_ACTIVEHOTPATH_MIN + HOT_PATHS_COUNT)
        {
            activePanel->GotoHotPath(LOWORD(wParam) - CM_ACTIVEHOTPATH_MIN);
            return 0;
        }

        switch (LOWORD(wParam))
        {
        case CM_HELP_CONTEXT:
        {
            OnContextHelp();
            return 0;
        }

            /*
        case CM_HELP_KEYBOARD:
        {
          ShellExecute(HWindow, "open", "https://www.taskscape.com/salam_en/features/keyboard.html", NULL, NULL, SW_SHOWNORMAL);
          return 0;
        }
*/
        case CM_FORUM:
        {
            ShellExecute(HWindow, "open", "/", NULL, NULL, SW_SHOWNORMAL);
            return 0;
        }

        case CM_HELP_CONTENTS:
        case CM_HELP_SEARCH:
        case CM_HELP_INDEX:
        case CM_HELP_KEYBOARD:
        {
            CHtmlHelpCommand command;
            DWORD_PTR dwData = 0;
            switch (LOWORD(wParam))
            {
            case CM_HELP_CONTENTS:
            {
                OpenHtmlHelp(NULL, HWindow, HHCDisplayTOC, 0, TRUE); // we don't want two message boxes in a row
                command = HHCDisplayContext;
                dwData = IDH_INTRODUCTION;
                break;
            }

            case CM_HELP_INDEX:
            {
                command = HHCDisplayIndex;
                break;
            }

            case CM_HELP_SEARCH:
            {
                command = HHCDisplaySearch;
                break;
            }

            case CM_HELP_KEYBOARD:
            {
                command = HHCDisplayContext;
                dwData = CM_HELP_KEYBOARD;
                break;
            }
            }

            OpenHtmlHelp(NULL, HWindow, command, dwData, FALSE);

            return 0;
        }

        case CM_HELP_ABOUT:
        {
            CAboutDialog dlg(HWindow);
            dlg.Execute();
            return 0;
        }

            /*
        case CM_HELP_TIP:
        {
          BOOL openQuiet = lParam == 0xffffffff;
          if (TipOfTheDayDialog != NULL)
          {
            TipOfTheDayDialog->IncrementTipIndex();
            TipOfTheDayDialog->InvalidateTipWindow();
            SetForegroundWindow(TipOfTheDayDialog->HWindow);
          }
          else
          {
            TipOfTheDayDialog = new CTipOfTheDayDialog(openQuiet);
            if (TipOfTheDayDialog != NULL)
            {
              if (TipOfTheDayDialog->IsGood())
              {
                TipOfTheDayDialog->Create();
              }
              else
              {
                delete TipOfTheDayDialog;
                TipOfTheDayDialog = NULL;
                // the file probably does not exist - next time we won't even try at startup
                if (openQuiet)
                  Configuration.ShowTipOfTheDay = FALSE;
              }
            }
          }
          return 0;
        }
*/
        case CM_ALWAYSONTOP:
        {
            if (!Configuration.AlwaysOnTop && Configuration.CnfrmAlwaysOnTop)
            {
                BOOL dontShow = !Configuration.CnfrmAlwaysOnTop;

                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = HWindow;
                params.Flags = MSGBOXEX_OKCANCEL | MSGBOXEX_ICONINFORMATION | MSGBOXEX_SILENT | MSGBOXEX_HINT;
                params.Caption = LoadStr(IDS_INFOTITLE);
                params.Text = LoadStr(IDS_WANTALWAYSONTOP);
                params.CheckBoxText = LoadStr(IDS_DONTSHOWAGAINAT);
                params.CheckBoxValue = &dontShow;
                int ret = SalMessageBoxEx(&params);
                Configuration.CnfrmAlwaysOnTop = !dontShow;
                if (ret == IDCANCEL)
                    return 0;
            }

            Configuration.AlwaysOnTop = !Configuration.AlwaysOnTop;
            HMENU h = GetSystemMenu(HWindow, FALSE);
            if (h != NULL)
            {
                CheckMenuItem(h, CM_ALWAYSONTOP, MF_BYCOMMAND | (Configuration.AlwaysOnTop ? MF_CHECKED : MF_UNCHECKED));
            }

            SetWindowPos(HWindow,
                         Configuration.AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            return 0;
        }

        case CM_MINIMIZE:
            MinimizeApp(MainWindow->HWindow);
            return 0;

        case CM_TASKLIST:
        {
            CTaskListDialog(HWindow).Execute();
            return 0;
        }

        case CM_CLIPCOPYFULLNAME:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CopyFocusedNameToClipboard(cfnmFull);
            return 0;
        }

        case CM_CLIPCOPYNAME:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CopyFocusedNameToClipboard(cfnmShort);
            return 0;
        }

        case CM_CLIPCOPYFULLPATH:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CopyCurrentPathToClipboard();
            return 0;
        }

        case CM_CLIPCOPYUNCNAME:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CopyFocusedNameToClipboard(cfnmUNC);
            return 0;
        }

        case CM_OPEN_IN_OTHER_PANEL:
        case CM_OPEN_IN_OTHER_PANEL_ACT:
        {
            activePanel->OpenFocusedInOtherPanel(LOWORD(wParam) == CM_OPEN_IN_OTHER_PANEL_ACT);
            return 0;
        }

        case CM_PLUGINS:
        {
            BeginStopRefresh(); // snooper takes a break

            CPluginsDlg dlg(HWindow);
            dlg.Execute();
            if (dlg.GetRefreshPanels())
            {
                UpdateWindow(HWindow);

                if ((LeftPanel->Is(ptDisk) || LeftPanel->Is(ptZIPArchive)) &&
                    IsUNCPath(LeftPanel->GetPath()) &&
                    LeftPanel->DirectoryLine != NULL)
                {
                    LeftPanel->DirectoryLine->BuildHotTrackItems();
                }
                if ((RightPanel->Is(ptDisk) || RightPanel->Is(ptZIPArchive)) &&
                    IsUNCPath(RightPanel->GetPath()) &&
                    RightPanel->DirectoryLine != NULL)
                {
                    RightPanel->DirectoryLine->BuildHotTrackItems();
                }

                HANDLES(EnterCriticalSection(&TimeCounterSection));
                int t1 = MyTimeCounter++;
                int t2 = MyTimeCounter++;
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                SendMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
                SendMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t2);
            }
            if (dlg.GetRefreshPanels() || // also refresh drive bars because of the Nethood plugin (Network Neighborhood icon appears/disappears)
                dlg.GetDrivesBarChange()) // change in visibility of the FS item in the Drive bars
            {
                PostMessage(HWindow, WM_USER_DRIVES_CHANGE, 0, 0);
            }

            const char* focusPlugin = dlg.GetFocusPlugin();
            if (focusPlugin[0] != 0)
            {
                char newPath[MAX_PATH];
                // Panel focus uses a complete path and filename pair from the plugin dialog.
                if (SUCCEEDED(StringCchCopyA(newPath, _countof(newPath), focusPlugin)))
                {
                    const char* newName;
                    char* p = strrchr(newPath, '\\');
                    if (p != NULL)
                    {
                        p++;
                        *p = 0;
                        newName = focusPlugin + int(p - newPath);
                    }
                    else
                        newName = "";
                    SendMessage(GetActivePanel()->HWindow, WM_USER_FOCUSFILE, (WPARAM)newName, (LPARAM)newPath);
                }
            }

            EndStopRefresh(); // snooper starts again now
            return 0;
        }

        case CM_SAVECONFIG:
        {
            // if an exported configuration already exists, show a warning
            if (FileExists(ConfigurationName))
            {
                char buff[3000];
                _snprintf_s(buff, _TRUNCATE, LoadStr(IDS_SAVECFG_EXPFILEEXISTS), ConfigurationName);
                int ret = SalMessageBox(HWindow, buff, LoadStr(IDS_INFOTITLE),
                                        MB_ICONINFORMATION | MB_OKCANCEL);
                if (ret == IDCANCEL)
                {
                    // navigate the user to the correct directory and focus the configuration file to make it easier
                    char path[MAX_PATH];
                    char* s = strrchr(ConfigurationName, '\\');
                    if (s != NULL)
                    {
                        memcpy(path, ConfigurationName, s - ConfigurationName);
                        path[s - ConfigurationName] = 0;
                        SendMessage(activePanel->HWindow, WM_USER_FOCUSFILE, (WPARAM)(s + 1), (LPARAM)path);
                    }
                    return 0;
                }
            }
            SaveConfig();
            return 0;
        }

        case CM_EXPORTCONFIG:
        {
            int ret = SalMessageBox(HWindow, LoadStr(IDS_PREDCONFIGEXPORT),
                                    LoadStr(IDS_QUESTION), MB_YESNOCANCEL | MB_ICONQUESTION);
            if (ret == IDCANCEL)
                return 0;

            if (ret == IDYES)
            {
                SaveConfig();
            }

            char file[MAX_PATH];
            char defDir[MAX_PATH];
            strcpy(file, "config_.reg");

            BOOL clearKeyBeforeImport = TRUE;

            MSGBOXEX_PARAMS params;
            memset(&params, 0, sizeof(params));
            params.HParent = HWindow;
            params.Flags = MSGBOXEX_OK | MSGBOXEX_ICONINFORMATION | MSGBOXEX_SILENT;
            params.Caption = LoadStr(IDS_INFOTITLE);
            params.Text = LoadStr(WindowsVistaAndLater ? IDS_CONFIGEXPVISTA : IDS_CONFIGEXPUPTOXP);
            params.CheckBoxText = LoadStr(IDS_CONFIGEXPCLEARKEY);
            params.CheckBoxValue = &clearKeyBeforeImport;
            SalMessageBoxEx(&params);

            if (WindowsVistaAndLater)
            {
                if (!CreateOurPathInRoamingAPPDATA(defDir, _countof(defDir)))
                {
                    TRACE_E("CM_EXPORTCONFIG: unexpected situation: unable to get our directory under CSIDL_APPDATA");
                    return 0;
                }
            }
            else
            {
                GetModuleFileName(HInstance, defDir, MAX_PATH);
                *strrchr(defDir, '\\') = 0;
            }
            OPENFILENAME ofn;
            memset(&ofn, 0, sizeof(OPENFILENAME));
            ofn.lStructSize = sizeof(OPENFILENAME);
            ofn.hwndOwner = HWindow;
            char* s = LoadStr(IDS_REGFILTER);
            ofn.lpstrFilter = s;
            while (*s != 0) // create a double-null-terminated list
            {
                if (*s == '|')
                    *s = 0;
                s++;
            }
            ofn.nFilterIndex = 1;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrInitialDir = defDir;

            ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            ofn.lpstrDefExt = "reg";

            if (SafeGetSaveFileName(&ofn))
            {
                if (SalGetFullName(file))
                {
                    // perform the export
                    if (ExportConfiguration(HWindow, file, clearKeyBeforeImport))
                    {
                        SalMessageBox(HWindow, LoadStr(IDS_CONFIGEXPORTED), LoadStr(IDS_INFOTITLE),
                                      MB_OK | MB_ICONINFORMATION);
                    }
                    else
                        DeleteFileUtf8(file);
                }
            }
            return 0;
        }

        case CM_IMPORTCONFIG:
        {
            SalMessageBox(HWindow, LoadStr(IDS_CONFIGHOWTOIMPORT), LoadStr(IDS_INFOTITLE),
                          MB_OK | MB_ICONINFORMATION);
            return 0;
        }

        case CM_SHARES:
        {
            CSharesDialog dlg(HWindow);
            if (dlg.Execute() == IDOK)
            {
                // user chose Focus
                const char* path = dlg.GetFocusedPath();
                if (path != NULL)
                {
                    char newPath[MAX_PATH];
                    // Share focus uses a complete path and filename pair from the dialog.
                    if (FAILED(StringCchCopyA(newPath, _countof(newPath), path)))
                        break;
                    const char* newName;
                    char* p = strrchr(newPath, '\\');
                    if (p != NULL)
                    {
                        p++;
                        *p = 0;
                        newName = path + int(p - newPath);
                    }
                    else
                        newName = "";
                    SendMessage(GetActivePanel()->HWindow, WM_USER_FOCUSFILE, (WPARAM)newName, (LPARAM)newPath);
                }
            }
            break;
        }

        case CM_SKILLLEVEL:
        {
            CSkillLevelDialog dlg(HWindow, &Configuration.SkillLevel);
            if (dlg.Execute() == IDOK)
                MainMenu.SetSkillLevel(CfgSkillLevelToMenu(Configuration.SkillLevel));
            break;
        }

        case CM_CONFIGURATION:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 0, 0); // standard configuration
            break;
        }

        case CM_AUTOCONFIG:
        {
            PostMessage(HWindow, WM_USER_AUTOCONFIG, 0, 0);
            break;
        }

        case CM_LCUSTOMIZEVIEW:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 4, LeftPanel->GetViewTemplateIndex());
            return 0;
        }

        case CM_RCUSTOMIZEVIEW:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 4, RightPanel->GetViewTemplateIndex());
            return 0;
        }

        case CM_LEFTNAME:
        {
            LeftPanel->ChangeSortType(stName, TRUE);
            return 0;
        }

        case CM_LEFTEXT:
        {
            LeftPanel->ChangeSortType(stExtension, TRUE);
            return 0;
        }

        case CM_LEFTTIME:
        {
            LeftPanel->ChangeSortType(stTime, TRUE);
            return 0;
        }

        case CM_LEFTSIZE:
        {
            LeftPanel->ChangeSortType(stSize, TRUE);
            return 0;
        }

        case CM_LEFTATTR:
        {
            LeftPanel->ChangeSortType(stAttr, TRUE);
            return 0;
        }
            // change sorting in the right panel
        case CM_RIGHTNAME:
        {
            RightPanel->ChangeSortType(stName, TRUE);
            return 0;
        }

        case CM_RIGHTEXT:
        {
            RightPanel->ChangeSortType(stExtension, TRUE);
            return 0;
        }

        case CM_RIGHTTIME:
        {
            RightPanel->ChangeSortType(stTime, TRUE);
            return 0;
        }

        case CM_RIGHTSIZE:
        {
            RightPanel->ChangeSortType(stSize, TRUE);
            return 0;
        }

        case CM_RIGHTATTR:
        {
            RightPanel->ChangeSortType(stAttr, TRUE);
            return 0;
        }
            // change sorting in the current panel
        case CM_ACTIVENAME:
            activePanel->ChangeSortType(stName, TRUE);
            return 0;
        case CM_ACTIVEEXT:
            activePanel->ChangeSortType(stExtension, TRUE);
            return 0;
        case CM_ACTIVETIME:
            activePanel->ChangeSortType(stTime, TRUE);
            return 0;
        case CM_ACTIVESIZE:
            activePanel->ChangeSortType(stSize, TRUE);
            return 0;
        case CM_ACTIVEATTR:
            activePanel->ChangeSortType(stAttr, TRUE);
            return 0;

        case CM_SORTOPTIONS:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 5, 0);
            return 0;
        }

        // toggle Smart Mode (Ctrl+N)
        case CM_ACTIVE_SMARTMODE:
            ToggleSmartColumnMode(activePanel);
            return 0;
        case CM_LEFT_SMARTMODE:
            ToggleSmartColumnMode(LeftPanel);
            return 0;
        case CM_RIGHT_SMARTMODE:
            ToggleSmartColumnMode(RightPanel);
            return 0;

            // change the current drive in the left panel
        case CM_LCHANGEDRIVE:
        {
            if (activePanel != LeftPanel)
            {
                ChangePanel();
                if (GetActivePanel() != LeftPanel)
                    return 0;          // the panel cannot be activated
                UpdateWindow(HWindow); // render the focus before the menu appears
            }
            if (LeftPanel->DirectoryLine != NULL)
                LeftPanel->DirectoryLine->SetDrivePressed(TRUE);
            LeftPanel->ChangeDrive();
            if (LeftPanel->DirectoryLine != NULL)
                LeftPanel->DirectoryLine->SetDrivePressed(FALSE);
            return 0;
        }
            // change of the current drive in the right panel
        case CM_RCHANGEDRIVE:
        {
            if (activePanel != RightPanel)
            {
                ChangePanel();
                if (GetActivePanel() != RightPanel)
                    return 0;          // the panel cannot be activated
                UpdateWindow(HWindow); // render the focus before the menu appears
            }
            if (RightPanel->DirectoryLine != NULL)
                RightPanel->DirectoryLine->SetDrivePressed(TRUE);
            RightPanel->ChangeDrive();
            if (RightPanel->DirectoryLine != NULL)
                RightPanel->DirectoryLine->SetDrivePressed(FALSE);
            return 0;
        }
            // change the file filter
        case CM_LCHANGEFILTER:
        {
            LeftPanel->ChangeFilter();
            return 0;
        }

        case CM_RCHANGEFILTER:
        {
            RightPanel->ChangeFilter();
            return 0;
        }

        case CM_CHANGEFILTER:
            activePanel->ChangeFilter();
            return 0;

        case CM_ACTIVEPARENTDIR:
        {
            activePanel->CtrlPageUpOrBackspace();
            return 0;
        }

        case CM_LPARENTDIR:
        {
            LeftPanel->CtrlPageUpOrBackspace();
            return 0;
        }

        case CM_RPARENTDIR:
        {
            RightPanel->CtrlPageUpOrBackspace();
            return 0;
        }

        case CM_ACTIVEROOTDIR:
        {
            activePanel->GotoRoot();
            return 0;
        }

        case CM_LROOTDIR:
        {
            LeftPanel->GotoRoot();
            return 0;
        }

        case CM_RROOTDIR:
        {
            RightPanel->GotoRoot();
            return 0;
        }
            // enabling/diabling the left panel status line
        case CM_LEFTSTATUS:
        {
            LeftPanel->ToggleStatusLine();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            return 0;
        }
            // enabling/disabling the right panel status line
        case CM_RIGHTSTATUS:
        {
            RightPanel->ToggleStatusLine();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            return 0;
        }
            // enabling/disabling the left panel directory line
        case CM_LEFTDIRLINE:
        {
            LeftPanel->ToggleDirectoryLine();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            return 0;
        }
            // enabling/disabling the right panel directory line
        case CM_RIGHTDIRLINE:
        {
            RightPanel->ToggleDirectoryLine();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            return 0;
        }

        case CM_LEFTHEADER:
        {
            LeftPanel->ToggleHeaderLine();
            LeftPanel->HeaderLineVisible = !LeftPanel->HeaderLineVisible;
            return 0;
        }

        case CM_RIGHTHEADER:
        {
            RightPanel->ToggleHeaderLine();
            RightPanel->HeaderLineVisible = !RightPanel->HeaderLineVisible;
            return 0;
        }

        case CM_LEFTREFRESH: // refresh the left panel
        {
            LeftPanel->NextFocusName[0] = 0;
            while (SnooperSuspended)
                EndSuspendMode(); // safety catch to resume refreshing
            while (StopRefresh)
                EndStopRefresh(FALSE); // safety catch to resume refreshing
            while (StopIconRepaint)
                EndStopIconRepaint(FALSE); // safety catch to resume refreshing
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            SendMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            RebuildDriveBarsIfNeeded(FALSE, 0, FALSE, 0); // maybe the user refreshed to update the drives list?
            return 0;
        }

        case CM_RIGHTREFRESH: // refresh the right panel
        {
            RightPanel->NextFocusName[0] = 0;
            while (SnooperSuspended)
                EndSuspendMode(); // safety catch to resume refreshing
            while (StopRefresh)
                EndStopRefresh(FALSE); // safety catch to resume refreshing
            while (StopIconRepaint)
                EndStopIconRepaint(FALSE); // safety catch to resume refreshing
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            SendMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            RebuildDriveBarsIfNeeded(FALSE, 0, FALSE, 0); // maybe the user refreshed to update the drives list?
            return 0;
        }

        case CM_ACTIVEREFRESH: // refresh the right panel
        {
            activePanel->NextFocusName[0] = 0;
            while (SnooperSuspended)
                EndSuspendMode(); // safety catch to resume refreshing
            while (StopRefresh)
                EndStopRefresh(FALSE); // safety catch to resume refreshing
            while (StopIconRepaint)
                EndStopIconRepaint(FALSE); // safety catch to resume refreshing
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            SendMessage(activePanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            RebuildDriveBarsIfNeeded(FALSE, 0, FALSE, 0); // maybe the user refreshed to update the drives list?
            return 0;
        }

        case CM_ACTIVEFORWARD:
        {
            activePanel->PathHistory->Execute(1, TRUE, activePanel);
            return 0;
        }

        case CM_ACTIVEBACK:
        {
            activePanel->PathHistory->Execute(2, FALSE, activePanel);
            return 0;
        }

        case CM_LFORWARD:
        {
            LeftPanel->PathHistory->Execute(1, TRUE, LeftPanel);
            return 0;
        }

        case CM_LBACK:
        {
            LeftPanel->PathHistory->Execute(2, FALSE, LeftPanel);
            return 0;
        }

        case CM_RFORWARD:
        {
            RightPanel->PathHistory->Execute(1, TRUE, RightPanel);
            return 0;
        }

        case CM_RBACK:
        {
            RightPanel->PathHistory->Execute(2, FALSE, RightPanel);
            return 0;
        }

        case CM_REFRESHASSOC: // reload associations from the Registry
        {
            OnAssociationsChangedNotification(TRUE);
            return 0;
        }

        case CM_EMAILFILES: // emailing files and directories
        {
            if (!EnablerFilesOnDisk)
                return 0;
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->StoreSelection(); // save selection for Restore Selection command

            // if no item is selected, select the focused one and store its name
            char temporarySelected[MAX_PATH];
            activePanel->SelectFocusedItemAndGetName(temporarySelected, MAX_PATH);

            activePanel->EmailFiles();

            // if we selected an item, deselect it again
            activePanel->UnselectItemWithName(temporarySelected);

            return 0;
        }

        case CM_COPYFILES: // copy files and directories
            if (!EnablerFilesCopy)
                return 0;
        case CM_MOVEFILES: // move/rename files and directories
            if (LOWORD(wParam) == CM_MOVEFILES && !EnablerFilesMove)
                return 0;
        case CM_DELETEFILES: // delete files and directories
            if (LOWORD(wParam) == CM_DELETEFILES && !EnablerFilesDelete)
                return 0;
        case CM_OCCUPIEDSPACE: // calculate occupied disk space
            if (LOWORD(wParam) == CM_OCCUPIEDSPACE && !EnablerOccupiedSpace)
                return 0;
        case CM_CHANGECASE: // change case in names
        {
            if (LOWORD(wParam) == CM_CHANGECASE && !EnablerFilesOnDisk)
                return 0;
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->StoreSelection(); // save selection for Restore Selection command

            // if no item is selected, select the focused one and store its name
            char temporarySelected[MAX_PATH];
            activePanel->SelectFocusedItemAndGetName(temporarySelected, MAX_PATH);

            if (activePanel->Is(ptDisk)) // source is disk - all operations go here
            {
                CActionType type;
                switch (LOWORD(wParam))
                {
                case CM_COPYFILES:
                    type = atCopy;
                    break;
                case CM_MOVEFILES:
                    type = atMove;
                    break;
                case CM_DELETEFILES:
                    type = atDelete;
                    break;
                case CM_OCCUPIEDSPACE:
                    type = atCountSize;
                    break;
                case CM_CHANGECASE:
                    type = atChangeCase;
                    break;
                }

                // perform the action
                activePanel->FilesAction(type, GetNonActivePanel());
            }
            else
            {
                if (activePanel->Is(ptZIPArchive)) // source is an archive - all operations go here
                {
                    BOOL archMaybeUpdated;
                    activePanel->OfferArchiveUpdateIfNeeded(HWindow, IDS_ARCHIVECLOSEEDIT2, &archMaybeUpdated);
                    if (!archMaybeUpdated)
                    {
                        switch (LOWORD(wParam))
                        {
                        case CM_OCCUPIEDSPACE:
                            activePanel->CalculateOccupiedZIPSpace();
                            break;
                        case CM_COPYFILES:
                            activePanel->UnpackZIPArchive(GetNonActivePanel());
                            break;
                        case CM_DELETEFILES:
                            activePanel->DeleteFromZIPArchive();
                            break;
                        }
                    }
                }
                else
                {
                    if (activePanel->Is(ptPluginFS)) // source is a FS - all operations go here
                    {
                        CPluginFSActionType type;
                        switch (LOWORD(wParam))
                        {
                        case CM_COPYFILES:
                            type = fsatCopy;
                            break;
                        case CM_MOVEFILES:
                            type = fsatMove;
                            break;
                        case CM_DELETEFILES:
                            type = fsatDelete;
                            break;
                        case CM_OCCUPIEDSPACE:
                            type = fsatCountSize;
                            break;
                        }
                        activePanel->PluginFSFilesAction(type);
                    }
                }
            }

            // if we selected an item temporarily, deselect it again
            activePanel->UnselectItemWithName(temporarySelected);

            return 0;
        }

        case CM_MENU:
        {
            MenuBar->EnterMenu();
            return 0;
        }

        case CM_DIRMENU:
        {
            ShellAction(activePanel, saContextMenu, FALSE, FALSE);
            return 0;
        }

        case CM_CONTEXTMENU:
        { // panel type checks are done later in ShellAction
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->StoreSelection(); // save selection for Restore Selection command
            ShellAction(activePanel, saContextMenu, TRUE, FALSE);
            return 0;
        }

        case CM_CALCDIRSIZES:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CalculateDirSizes();
            return 0;
        }

        case CM_RENAMEFILE:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->RenameFile();
            return 0;
        }

        case CM_CHANGEATTR:
        {
            if (EnablerChangeAttrs)
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ChangeAttr();
            }
            return 0;
        }

        case CM_CONVERTFILES:
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command

                // if no item is selected, choose the one under the focus and store its name
                char temporarySelected[MAX_PATH];
                activePanel->SelectFocusedItemAndGetName(temporarySelected, MAX_PATH);

                activePanel->Convert();

                // if we selected an item temporarily, deselect it again
                activePanel->UnselectItemWithName(temporarySelected);
            }
            return 0;
        }

        case CM_COMPRESS:
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ChangeAttr(TRUE, TRUE);
            }
            return 0;
        }

        case CM_UNCOMPRESS:
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ChangeAttr(TRUE, FALSE);
            }
            return 0;
        }

        case CM_ENCRYPT:
        {
            if (activePanel->Is(ptDisk))
            {
                ExecLogFeatureStart("encrypt", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ChangeAttr(FALSE, FALSE, TRUE, TRUE);
            }
            return 0;
        }

        case CM_DECRYPT:
        {
            if (activePanel->Is(ptDisk))
            {
                ExecLogFeatureStart("decrypt", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ChangeAttr(FALSE, FALSE, TRUE, FALSE);
            }
            return 0;
        }

        case CM_PACK:
        {
            if (activePanel->Is(ptDisk))
            {
                ExecLogFeatureStart("pack", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->Pack(GetNonActivePanel());
            }
            return 0;
        }

        case CM_UNPACK:
        {
            if (activePanel->Is(ptDisk))
            {
                ExecLogFeatureStart("unpack", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->Unpack(GetNonActivePanel());
            }
            return 0;
        }

        case CM_AFOCUSSHORTCUT:
        {
            if (EnablerFileOrDirLinkOnDisk) // enabler for activePanel
            {
                //            activePanel->UserWorkedOnThisPath = TRUE; // it's just navigation, don't mark the path dirty
                activePanel->FocusShortcutTarget(activePanel);
            }
            return 0;
        }

        case CM_PROPERTIES:
        {
            if (EnablerShowProperties)
            {
                ExecLogFeatureStart("properties", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                ShellAction(activePanel, saProperties, TRUE, FALSE);
            }
            return 0;
        }

        case CM_OPEN:
        {
            ExecLogFeatureStart("open", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CtrlPageDnOrEnter(VK_RETURN);
            return 0;
        }

        case CM_VIEW:
        {
            ExecLogFeatureStart("view", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->ViewFile(NULL, FALSE, 0xFFFFFFFF, activePanel->Is(ptDisk) ? activePanel->EnumFileNamesSourceUID : -1, -1);
            return 0;
        }

        case CM_ALTVIEW:
        {
            ExecLogFeatureStart("alt view", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->ViewFile(NULL, TRUE, 0xFFFFFFFF, activePanel->Is(ptDisk) ? activePanel->EnumFileNamesSourceUID : -1, -1);
            return 0;
        }

        case CM_VIEW_WITH:
        {
            POINT menuPos;
            ExecLogFeatureStart("view with", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->GetContextMenuPos(&menuPos);
            activePanel->ViewFileWith(NULL, HWindow, &menuPos, NULL,
                                      activePanel->Is(ptDisk) ? activePanel->EnumFileNamesSourceUID : -1, -1);
            return 0;
        }

        case CM_EDIT:
        {
            if (EnablerFileOnDiskOrArchive)
            {
                ExecLogFeatureStart("edit", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                if (activePanel->Is(ptZIPArchive))
                {
                    int index = activePanel->GetCaretIndex();
                    if (index >= activePanel->Dirs->Count &&
                        index < activePanel->Dirs->Count + activePanel->Files->Count)
                    {
                        activePanel->ExecuteFromArchive(index, TRUE);
                    }
                }
                else
                    activePanel->EditFile(NULL);
            }
            return 0;
        }

        case CM_EDITNEW:
        {
            if (activePanel->Is(ptDisk))
            {
                ExecLogFeatureStart("edit new", activePanel->GetPath());
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->EditNewFile();
            }
            return 0;
        }

        case CM_EDIT_WITH:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            POINT menuPos;
            ExecLogFeatureStart("edit with", activePanel->GetPath());
            activePanel->GetContextMenuPos(&menuPos);
            if (activePanel->Is(ptDisk))
            {
                activePanel->EditFileWith(NULL, HWindow, &menuPos);
            }
            else
            {
                if (activePanel->Is(ptZIPArchive))
                {
                    int index = activePanel->GetCaretIndex();
                    if (index >= activePanel->Dirs->Count &&
                        index < activePanel->Dirs->Count + activePanel->Files->Count)
                    {
                        activePanel->ExecuteFromArchive(index, TRUE, HWindow, &menuPos);
                    }
                }
            }
            return 0;
        }

        case CM_FINDFILE:
        {
            ExecLogFeatureStart("find file", activePanel->GetPath());
            if (activePanel->Is(ptDisk)) // does Find relate to the current path? (archives and FS not yet)
            {
                activePanel->UserWorkedOnThisPath = TRUE;
            }

            activePanel->FindFile();
            return 0;
        }

        case CM_DRIVEINFO:
        {
            activePanel->DriveInfo();
            return 0;
        }

        case CM_CREATEDIR:
        {
            ExecLogFeatureStart("create dir", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->CreateDir(GetNonActivePanel());
            return 0;
        }

        case CM_ACTIVE_CHANGEDIR:
        {
            ExecLogFeatureStart("change dir", activePanel->GetPath());
            activePanel->ChangeDir();
            return 0;
        }

        case CM_LEFT_CHANGEDIR:
        {
            ExecLogFeatureStart("change dir", LeftPanel->GetPath());
            LeftPanel->ChangeDir();
            return 0;
        }

        case CM_RIGHT_CHANGEDIR:
        {
            ExecLogFeatureStart("change dir", RightPanel->GetPath());
            RightPanel->ChangeDir();
            return 0;
        }

        case CM_ACTIVE_AS_OTHER:
        {
            ExecLogFeatureStart("sync path", "active to other");
            activePanel->ChangePathToOtherPanelPath();
            return 0;
        }

        case CM_LEFT_AS_OTHER:
        {
            ExecLogFeatureStart("sync path", "left to other");
            LeftPanel->ChangePathToOtherPanelPath();
            return 0;
        }

        case CM_RIGHT_AS_OTHER:
        {
            ExecLogFeatureStart("sync path", "right to other");
            RightPanel->ChangePathToOtherPanelPath();
            return 0;
        }

        case CM_ACTIVESELECTALL:
        {
            activePanel->SelectUnselect(TRUE, TRUE, FALSE);
            return 0;
        }

        case CM_ACTIVEUNSELECTALL:
        {
            activePanel->SelectUnselect(TRUE, FALSE, FALSE);
            return 0;
        }

        case CM_ACTIVESELECT:
        {
            activePanel->SelectUnselect(FALSE, TRUE, TRUE);
            return 0;
        }

        case CM_ACTIVEUNSELECT:
        {
            activePanel->SelectUnselect(FALSE, FALSE, TRUE);
            return 0;
        }

        case CM_ACTIVEINVERTSEL:
        {
            activePanel->InvertSelection(FALSE);
            return 0;
        }

        case CM_ACTIVEINVERTSELALL:
        {
            activePanel->InvertSelection(TRUE);
            return 0;
        }

        case CM_RESELECT:
        {
            activePanel->Reselect();
            return 0;
        }

        case CM_SELECTBYFOCUSEDNAME:
        {
            activePanel->SelectUnselectByFocusedItem(TRUE, TRUE);
            return 0;
        }

        case CM_UNSELECTBYFOCUSEDNAME:
        {
            activePanel->SelectUnselectByFocusedItem(FALSE, TRUE);
            return 0;
        }

        case CM_SELECTBYFOCUSEDEXT:
        {
            activePanel->SelectUnselectByFocusedItem(TRUE, FALSE);
            return 0;
        }

        case CM_UNSELECTBYFOCUSEDEXT:
        {
            activePanel->SelectUnselectByFocusedItem(FALSE, FALSE);
            return 0;
        }

        case CM_HIDE_SELECTED_NAMES:
        {
            activePanel->ShowHideNames(1); // hide selected
            return 0;
        }

        case CM_HIDE_UNSELECTED_NAMES:
        {
            activePanel->ShowHideNames(2); // hide unselected
            return 0;
        }

        case CM_SHOW_ALL_NAME:
        {
            activePanel->ShowHideNames(0); // show all
            return 0;
        }

        case CM_STORESEL:
        {
            activePanel->StoreGlobalSelection();
            return 0;
        }

        case CM_RESTORESEL:
        {
            activePanel->RestoreGlobalSelection();
            return 0;
        }

        case CM_GOTO_PREV_SEL:
        case CM_GOTO_NEXT_SEL:
        {
            activePanel->GotoSelectedItem(LOWORD(wParam) == CM_GOTO_NEXT_SEL);
            return 0;
        }

        case CM_COMPAREDIRS:
        {
            // currently we support only ptDisk<->ptDisk, ptDisk<->ptZIPArchive and ptZIPArchive<->ptZIPArchive
            //if (LeftPanel->Is(ptPluginFS) || RightPanel->Is(ptPluginFS))
            //{
            //  SalMessageBox(HWindow, LoadStr(IDS_COMPARE_FS), LoadStr(IDS_COMPAREDIRSTITLE), MB_OK | MB_ICONINFORMATION);
            //  return 0;
            //}

            // if both panels point to the same path, exit
            char leftPath[2 * MAX_PATH];
            char rightPath[2 * MAX_PATH];
            LeftPanel->GetGeneralPath(leftPath, 2 * MAX_PATH);
            RightPanel->GetGeneralPath(rightPath, 2 * MAX_PATH);
            char compareDetail[4 * MAX_PATH];
            _snprintf_s(compareDetail, _TRUNCATE, "left=%s, right=%s", leftPath, rightPath);
            ExecLogFeatureStart("compare dirs", compareDetail);
            if (strcmp(leftPath, rightPath) == 0) // case sensitive; if this condition fails, it's fine
            {
                ExecLogFeatureResult("compare dirs", compareDetail, FALSE);
                SalMessageBox(HWindow, LoadStr(IDS_COMPARE_SAMEPATH), LoadStr(IDS_COMPAREDIRSTITLE), MB_OK | MB_ICONINFORMATION);
                return 0;
            }

            BOOL enableByDateAndTime = (LeftPanel->ValidFileData & (VALID_DATA_DATE | VALID_DATA_PL_DATE)) &&
                                       (RightPanel->ValidFileData & (VALID_DATA_DATE | VALID_DATA_PL_DATE));
            BOOL enableBySize = (LeftPanel->ValidFileData & (VALID_DATA_SIZE | VALID_DATA_PL_SIZE)) &&
                                (RightPanel->ValidFileData & (VALID_DATA_SIZE | VALID_DATA_PL_SIZE));
            BOOL enableByAttrs = (LeftPanel->ValidFileData & VALID_DATA_ATTRIBUTES) &&
                                 (RightPanel->ValidFileData & VALID_DATA_ATTRIBUTES);
            BOOL enableByContent = LeftPanel->Is(ptDisk) && RightPanel->Is(ptDisk);
            BOOL enableSubdirs = !LeftPanel->Is(ptPluginFS) && !RightPanel->Is(ptPluginFS);
            BOOL enableCompAttrsOfSubdirs = enableSubdirs && enableByAttrs;
            CCompareDirsDialog dlg(HWindow, enableByDateAndTime, enableBySize, enableByAttrs,
                                   enableByContent, enableSubdirs, enableCompAttrsOfSubdirs,
                                   LeftPanel, RightPanel);
            if (dlg.Execute() == IDOK)
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                DWORD flags = 0;
                if (enableByDateAndTime && Configuration.CompareByTime)
                    flags |= COMPARE_DIRECTORIES_BYTIME;
                if (enableBySize && Configuration.CompareBySize)
                    flags |= COMPARE_DIRECTORIES_BYSIZE;
                if (enableByContent && Configuration.CompareByContent)
                    flags |= COMPARE_DIRECTORIES_BYCONTENT;
                if (enableByAttrs && Configuration.CompareByAttr)
                    flags |= COMPARE_DIRECTORIES_BYATTR;
                if (enableSubdirs && Configuration.CompareSubdirs)
                    flags |= COMPARE_DIRECTORIES_SUBDIRS;
                else
                {
                    if (Configuration.CompareOnePanelDirs)
                    {
                        flags |= COMPARE_DIRECTORIES_ONEPANELDIRS;
                        Configuration.CompareSubdirs = FALSE; // handles case when CompareSubdirs is enabled and a compare is run for FS and the user toggles CompareOnePanelDirs - without this line, on the next open of the disk dialog, CompareSubdirs would take precedence over CompareOnePanelDirs, which isnÔÇÖt quite right...
                    }
                }
                if (enableCompAttrsOfSubdirs && Configuration.CompareSubdirsAttr)
                    flags |= COMPARE_DIRECTORIES_SUBDIRS_ATTR;
                if (Configuration.CompareIgnoreFiles)
                    flags |= COMPARE_DIRECTORIES_IGNFILENAMES;
                if ((enableSubdirs && Configuration.CompareSubdirs || Configuration.CompareOnePanelDirs) &&
                    Configuration.CompareIgnoreDirs)
                    flags |= COMPARE_DIRECTORIES_IGNDIRNAMES;
                CompareDirectories(flags);
                ExecLogFeatureResult("compare dirs", compareDetail, TRUE);
            }
            else
            {
                ExecLogFeatureResult("compare dirs", compareDetail, FALSE);
            }
            return 0;
        }

        case CM_EXIT:
        {
            PostMessage(HWindow, WM_USER_CLOSE_MAINWND, 0, 0);
            return 0;
        }

        case CM_CONNECTNET:
        {
            activePanel->ConnectNet(FALSE);
            return 0;
        }

        case CM_DISCONNECTNET:
        {
            activePanel->DisconnectNet();
            return 0;
        }

        case CM_FILEHISTORY:
        {
            if (!FileHistory->HasItem())
                return 0;
            MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit

            BeginStopRefresh(); // snooper takes a break

            RECT r;
            GetWindowRect(HWindow, &r);
            int x = r.left + (r.right - r.left) / 2;
            int y = r.top + (r.bottom - r.top) / 2;

            CMenuPopup menu;
            FileHistory->FillPopupMenu(&menu);
            DWORD cmd = menu.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_CENTERALIGN | MENU_TRACK_VCENTERALIGN,
                                   x, y, HWindow, NULL);
            if (cmd != 0)
                FileHistory->Execute(cmd);

            EndStopRefresh(); // snooper starts again now

            return 0;
        }

        case CM_DIRHISTORY:
        {
            activePanel->OpenDirHistory();
            return 0;
        }

        case CM_USERMENU:
        {
            if (activePanel->Is(ptDisk))
            {
                BeginStopRefresh(); // no refreshes needed

                MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit

                UserMenuIconBkgndReader.BeginUserMenuIconsInUse();
                CMenuPopup menu;
                FillUserMenu(&menu);
                POINT p;
                activePanel->GetContextMenuPos(&p);
                // another lock/unlock cycle (BeginUserMenuIconsInUse + EndUserMenuIconsInUse) will occur
                // in WM_USER_ENTERMENULOOP + WM_USER_LEAVEMENULOOP, but it is nested and lightweight,
                // so we ignore it and do not fight it
                menu.Track(0, p.x, p.y, HWindow, NULL);
                UserMenuIconBkgndReader.EndUserMenuIconsInUse();

                EndStopRefresh();
            }
            return 0;
        }

        case CM_OPENHOTPATHS:
        {
            BeginStopRefresh(); // no refreshes needed

            MainWindow->CancelPanelsUI(); // cancel QuickSearch and QuickEdit

            RECT r;
            GetWindowRect(GetActivePanelHWND(), &r);
            int dirHeight = GetDirectoryLineHeight();

            CMenuPopup menu;
            HotPaths.FillHotPathsMenu(&menu, CM_ACTIVEHOTPATH_MIN);
            menu.Track(0, r.left, r.top + dirHeight, HWindow, NULL);

            EndStopRefresh();
            return 0;
        }

        case CM_CUSTOMIZE_HOTPATHS:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 1, -1);
            return 0;
        }

        case CM_CUSTOMIZE_USERMENU:
        {
            PostMessage(HWindow, WM_USER_CONFIGURATION, 2, 0);
            return 0;
        }

        case CM_EDITLINE:
        {
            if (SystemPolicies.GetNoRun())
            {
                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = HWindow;
                params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
                params.Caption = LoadStr(IDS_POLICIESRESTRICTION_TITLE);
                params.Text = LoadStr(IDS_POLICIESRESTRICTION);
                params.ContextHelpId = IDH_GROUPPOLICY;
                params.HelpCallback = MessageBoxHelpCallback;
                SalMessageBoxEx(&params);
                return 0;
            }
            if (EditWindow->HWindow != NULL)
            {
                if (EditWindow->IsEnabled())
                    SetFocus(EditWindow->HWindow);
            }
            else
            {
                if (EditPermanentVisible || EditWindow->IsEnabled()) // there may be an archive in the panel
                    ShowCommandLine();
            }
            return 0;
        }

        case CM_TOGGLEEDITLINE:
        {
            if (SystemPolicies.GetNoRun())
            {
                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = HWindow;
                params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
                params.Caption = LoadStr(IDS_POLICIESRESTRICTION_TITLE);
                params.Text = LoadStr(IDS_POLICIESRESTRICTION);
                params.ContextHelpId = IDH_GROUPPOLICY;
                params.HelpCallback = MessageBoxHelpCallback;
                SalMessageBoxEx(&params);
                return 0;
            }
            EditPermanentVisible = !EditPermanentVisible;
            if (EditWindow->HWindow != NULL && !EditPermanentVisible)
                HideCommandLine();
            else if (EditWindow->HWindow == NULL)
            {
                if (EditPermanentVisible)
                {
                    ShowCommandLine();
                    if (lParam == 0)
                        SetFocus(EditWindow->HWindow);
                }
            }
            return 0;
        }

        case CM_TOGGLETOPTOOLBAR:
        {
            ToggleTopToolBar();
            //          LayoutWindows();
            break;
        }

        case CM_TOGGLEPLUGINSBAR:
        {
            TogglePluginsBar();
            break;
        }

        case CM_TOGGLEMIDDLETOOLBAR:
        {
            ToggleMiddleToolBar();
            InvalidateRect(HWindow, NULL, FALSE);
            LayoutWindows();
            break;
        }

        case CM_TOGGLEUSERMENUTOOLBAR:
        {
            ToggleUserMenuToolBar();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                                      //          LayoutWindows();
            break;
        }

        case CM_TOGGLEHOTPATHSBAR:
        {
            ToggleHotPathsBar();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                                      //          LayoutWindows();
            break;
        }

        case CM_TOGGLEDRIVEBAR:
        case CM_TOGGLEDRIVEBAR2:
        {
            ToggleDriveBar(LOWORD(wParam) == CM_TOGGLEDRIVEBAR2);
            //          LayoutWindows();
            break;
        }

        case CM_TOGGLEBOTTOMTOOLBAR:
        {
            ToggleBottomToolBar();
            IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
            LayoutWindows();
            break;
        }

        case CM_TOGGLE_UMLABELS:
        {
            UMToolBar->ToggleLabels();
            break;
        }

            //        case CM_TOGGLE_HPLABELS:
            //        {
            //          HPToolBar->ToggleLabels();
            //          break;
            //        }

        case CM_TOGGLE_GRIPS:
        {
            ToggleToolBarGrips();
            break;
        }

        case CM_CUSTOMIZETOP:
        {
            if (TopToolBar->HWindow == NULL)
            {
                ToggleTopToolBar();
                IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                LayoutWindows();
            }
            TopToolBar->Customize();
            break;
        }

        case CM_CUSTOMIZEPLUGINS:
        {
            if (PluginsBar->HWindow == NULL)
            {
                TogglePluginsBar();
                LayoutWindows();
            }
            // let the Plugins Manager open
            PostMessage(MainWindow->HWindow, WM_COMMAND, CM_PLUGINS, 0);
            break;
        }

        case CM_CUSTOMIZEMIDDLE:
        {
            if (MiddleToolBar->HWindow == NULL)
            {
                ToggleMiddleToolBar();
                IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                LayoutWindows();
            }
            MiddleToolBar->Customize();
            break;
        }

        case CM_CUSTOMIZEUM:
        {
            if (UMToolBar->HWindow == NULL)
            {
                ToggleUserMenuToolBar();
                IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                LayoutWindows();
            }
            // expand the UserMenu page and edit the item at the given index
            PostMessage(HWindow, WM_USER_CONFIGURATION, 2, 0);
            break;
        }

        case CM_CUSTOMIZEHP:
        {
            if (HPToolBar->HWindow == NULL)
            {
                ToggleHotPathsBar();
                IdleRefreshStates = TRUE; // on the next Idle, force a check of status variables
                LayoutWindows();
            }
            // let the HotPaths page expand
            PostMessage(HWindow, WM_USER_CONFIGURATION, 1, -1);
            break;
        }

        case CM_CUSTOMIZELEFT:
        {
            if (LeftPanel->DirectoryLine->HWindow == NULL)
                LeftPanel->ToggleDirectoryLine();
            if (LeftPanel->DirectoryLine->ToolBar != NULL)
                LeftPanel->DirectoryLine->ToolBar->Customize();
            break;
        }

        case CM_CUSTOMIZERIGHT:
        {
            if (RightPanel->DirectoryLine->HWindow == NULL)
                RightPanel->ToggleDirectoryLine();
            if (RightPanel->DirectoryLine->ToolBar != NULL)
                RightPanel->DirectoryLine->ToolBar->Customize();
            break;
        }

        case CM_DOSSHELL:
        {
            activePanel->UserWorkedOnThisPath = TRUE;

            char cmd[MAX_PATH];
            if (!GetEnvironmentVariable("COMSPEC", cmd, MAX_PATH))
                cmd[0] = 0;

            if (SystemPolicies.GetNoRun() ||
                (SystemPolicies.GetMyRunRestricted() && !SystemPolicies.GetMyCanRun(cmd)))
            {
                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = HWindow;
                params.Flags = MSGBOXEX_OK | MSGBOXEX_HELP | MSGBOXEX_ICONEXCLAMATION;
                params.Caption = LoadStr(IDS_POLICIESRESTRICTION_TITLE);
                params.Text = LoadStr(IDS_POLICIESRESTRICTION);
                params.ContextHelpId = IDH_GROUPPOLICY;
                params.HelpCallback = MessageBoxHelpCallback;
                SalMessageBoxEx(&params);
                return 0;
            }

            AddDoubleQuotesIfNeeded(cmd, MAX_PATH); // CreateProcess requires the name with spaces in quotes (otherwise it tries various options; see help)
            ExecLogFeatureStart("command shell", cmd);

            SetDefaultDirectories();

            STARTUPINFO si;
            memset(&si, 0, sizeof(STARTUPINFO));
            si.cb = sizeof(STARTUPINFO);
            si.lpTitle = LoadStr(IDS_COMMANDSHELL);
            // There is an undocumented flag 0x400 where we can pass the monitor handle into si.hStdOutput
            // Unfortunately it works with SOL.EXE but not with CMD.EXE, so we use the old method
            // with a dummy window
            // On W2K the flag appears as #define STARTF_HASHMONITOR 0x00000400  // same as HASSHELLDATA
            // STARTF_MONITOR was mentioned online in an article about undocumented features
            si.dwFlags = STARTF_USESHOWWINDOW;
            POINT p;
            if (MultiMonGetDefaultWindowPos(MainWindow->HWindow, &p))
            {
                // if the main window is on another monitor we should open
                // the new window there as well, preferably at the default position (same as on the primary)
                si.dwFlags |= STARTF_USEPOSITION;
                si.dwX = p.x;
                si.dwY = p.y;
                // TRACE_I("MultiMonGetDefaultWindowPos(): x = " << p.x << ", y = " << p.y);
            }
            si.wShowWindow = SW_SHOWNORMAL;

            PROCESS_INFORMATION pi;

            BOOL createProcessOk = HANDLES(CreateProcess(NULL, cmd, NULL, NULL, FALSE,
                                                         CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS, NULL,
                                                         (activePanel->Is(ptDisk) || activePanel->Is(ptZIPArchive)) ? activePanel->GetPath() : NULL, &si, &pi));
            ExecLogFeatureResult("command shell", cmd, createProcessOk);
            if (!createProcessOk)
            {
                DWORD err = GetLastError();
                SalMessageBox(HWindow, GetErrorText(err),
                              LoadStr(IDS_ERROREXECPROMPT), MB_OK | MB_ICONEXCLAMATION);
            }
            else
            {
                HANDLES(CloseHandle(pi.hProcess));
                HANDLES(CloseHandle(pi.hThread));
            }

            return 0;
        }

        case CM_FILELIST:
        {
            ExecLogFeatureStart("file list", activePanel->GetPath());
            activePanel->UserWorkedOnThisPath = TRUE;
            activePanel->StoreSelection(); // save selection for Restore Selection command
            MakeFileList();
            return 0;
        }

        case CM_OPENACTUALFOLDER:
        {
            ExecLogFeatureStart("open active folder", activePanel->GetPath());
            activePanel->OpenActiveFolder();
            return 0;
        }

        case CM_SWAPPANELS:
        {
            // swap panels
            CFilesWindow* swap = LeftPanel;
            LeftPanel = RightPanel;
            RightPanel = swap;
            // swap toolbar records
            char buff[1024];
            // Toolbar layouts are fixed persisted presentation records during the panel swap.
            StringCchCopyNA(buff, _countof(buff), Configuration.LeftToolBar, _countof(buff) - 1);
            StringCchCopyNA(Configuration.LeftToolBar, _countof(Configuration.LeftToolBar), Configuration.RightToolBar, _countof(Configuration.LeftToolBar) - 1);
            StringCchCopyNA(Configuration.RightToolBar, _countof(Configuration.RightToolBar), buff, _countof(Configuration.RightToolBar) - 1);
            // set panel variables and load the toolbars
            LeftPanel->DirectoryLine->SetLeftPanel(TRUE);
            RightPanel->DirectoryLine->SetLeftPanel(FALSE);
            // the icon must be changed in the image list
            LeftPanel->UpdateDriveIcon(FALSE);
            RightPanel->UpdateDriveIcon(FALSE);

            // if the active panel was ZOOMed, after Ctrl+U, the minimized panel would remain active
            if (GetActivePanel() == LeftPanel && IsPanelZoomed(FALSE) ||
                GetActivePanel() == RightPanel && IsPanelZoomed(TRUE))
            {
                // so activate the visible one
                ChangePanel(TRUE);
            }

            LockWindowUpdate(HWindow);
            LayoutWindows();
            LockWindowUpdate(NULL);

            // reload columns again (column widths are not swapped)
            LeftPanel->SelectViewTemplate(LeftPanel->GetViewTemplateIndex(), TRUE, FALSE);
            RightPanel->SelectViewTemplate(RightPanel->GetViewTemplateIndex(), TRUE, FALSE);

            // distribute this news among plugins as well
            Plugins.Event(PLUGINEVENT_PANELSSWAPPED, 0);

            return 0;
        }

        case CM_OPENRECYCLEBIN:
        {
            OpenSpecFolder(HWindow, CSIDL_BITBUCKET);
            return 0;
        }

        case CM_OPENCONROLPANEL:
        {
            OpenSpecFolder(HWindow, CSIDL_CONTROLS);
            return 0;
        }

        case CM_OPENDESKTOP:
        {
            OpenSpecFolder(HWindow, CSIDL_DESKTOP);
            return 0;
        }

        case CM_OPENMYCOMP:
        {
            OpenSpecFolder(HWindow, CSIDL_DRIVES);
            return 0;
        }

        case CM_OPENFONTS:
        {
            OpenSpecFolder(HWindow, CSIDL_FONTS);
            return 0;
        }

        case CM_OPENNETNEIGHBOR:
        {
            OpenSpecFolder(HWindow, CSIDL_NETWORK);
            return 0;
        }

        case CM_OPENPRINTERS:
        {
            OpenSpecFolder(HWindow, CSIDL_PRINTERS);
            return 0;
        }

        case CM_OPENDESKTOPDIR:
        {
            OpenSpecFolder(HWindow, CSIDL_DESKTOPDIRECTORY);
            return 0;
        }

        case CM_OPENPERSONAL:
        {
            OpenSpecFolder(HWindow, CSIDL_PERSONAL);
            return 0;
        }

        case CM_OPENPROGRAMS:
        {
            OpenSpecFolder(HWindow, CSIDL_PROGRAMS);
            return 0;
        }

        case CM_OPENRECENT:
        {
            OpenSpecFolder(HWindow, CSIDL_RECENT);
            return 0;
        }

        case CM_OPENSENDTO:
        {
            OpenSpecFolder(HWindow, CSIDL_SENDTO);
            return 0;
        }

        case CM_OPENSTARTMENU:
        {
            OpenSpecFolder(HWindow, CSIDL_STARTMENU);
            return 0;
        }

        case CM_OPENSTARTUP:
        {
            OpenSpecFolder(HWindow, CSIDL_STARTUP);
            return 0;
        }

        case CM_OPENTEMPLATES:
        {
            OpenSpecFolder(HWindow, CSIDL_TEMPLATES);
            return 0;
        }

        case CM_CLIPCOPY:
        {
            if (activePanel->Is(ptDisk) || activePanel->Is(ptZIPArchive))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ClipboardCopy();
            }
            return 0;
        }

        case CM_CLIPCUT:
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                activePanel->ClipboardCut();
            }
            return 0;
        }

        case CM_CLIPPASTE:
        {
            activePanel->UserWorkedOnThisPath = TRUE;
            if (!activePanel->Is(ptDisk) || !activePanel->ClipboardPaste()) // attempt to paste files to disk
            {
                if (!activePanel->Is(ptZIPArchive) && !activePanel->Is(ptPluginFS) ||
                    !activePanel->ClipboardPasteToArcOrFS(FALSE, NULL)) // attempt to paste files into an archive or the file system
                {
                    activePanel->ClipboardPastePath(); // or change the current path
                }
            }
            return 0;
        }

        case CM_CLIPPASTELINKS:
        {
            if (activePanel->Is(ptDisk))
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->ClipboardPasteLinks();
            }
            return 0;
        }

        case CM_TOGGLEELASTICSMART:
        {
            ToggleSmartColumnMode(activePanel);
            return 0;
        }

        case CM_TOGGLEHIDDENFILES:
        {
            Configuration.NotHiddenSystemFiles = !Configuration.NotHiddenSystemFiles;
            HANDLES(EnterCriticalSection(&TimeCounterSection));
            int t1 = MyTimeCounter++;
            int t2 = MyTimeCounter++;
            HANDLES(LeaveCriticalSection(&TimeCounterSection));
            SendMessage(LeftPanel->HWindow, WM_USER_REFRESH_DIR, 0, t1);
            SendMessage(RightPanel->HWindow, WM_USER_REFRESH_DIR, 0, t2);

            // distribute this news among plug-ins as well
            Plugins.Event(PLUGINEVENT_CONFIGURATIONCHANGED, 0);
            return 0;
        }

        case CM_SEC_PERMISSIONS:
        {
            if (EnablerPermissions)
            {
                activePanel->UserWorkedOnThisPath = TRUE;
                activePanel->StoreSelection(); // save selection for Restore Selection command
                ShellAction(activePanel, saPermissions, TRUE, FALSE);
            }
            return 0;
        }

        case CM_ACTIVEZOOMPANEL:
        case CM_LEFTZOOMPANEL:
        case CM_RIGHTZOOMPANEL:
        {
            if (IsPanelZoomed(TRUE) || IsPanelZoomed(FALSE))
            {
                SplitPosition = BeforeZoomSplitPosition;
                // better protect ourselves against a bad value in BeforeZoomSplitPosition
                if (IsPanelZoomed(TRUE) || IsPanelZoomed(FALSE))
                    SplitPosition = 0.5;
            }
            else
            {
                BeforeZoomSplitPosition = SplitPosition;
                if (LOWORD(wParam) == CM_ACTIVEZOOMPANEL)
                {
                    if (activePanel == LeftPanel)
                        SplitPosition = 1.0;
                    else
                        SplitPosition = 0.0;
                }
                else
                {
                    if (LOWORD(wParam) == CM_LEFTZOOMPANEL)
                        SplitPosition = 1.0;
                    else
                        SplitPosition = 0.0;
                }
            }
            LayoutWindows();
            FocusPanel(GetActivePanel());
            return 0;
        }

        case CM_FULLSCREEN:
        {
            if (IsZoomed(HWindow))
                ShowWindow(HWindow, SW_RESTORE);
            else
                ShowWindow(HWindow, SW_MAXIMIZE);
            return 0;
        }
        }
    return 0;
}
