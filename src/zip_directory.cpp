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

// Globals defined in zip_progress.cpp
extern const char* STR_NONE;
extern CSalamanderDirectory GlobalEmptySalDir;
extern HWND ProgressDialogActivateDrop;

// Forward declaration for ActivateDropTarget defined in zip_utilities.cpp
void ActivateDropTarget(HWND dropTarget, HWND progressWnd);

// ****************************************************************************
// CSalamanderForOperations
//

CSalamanderForOperations::CSalamanderForOperations(CFilesWindow* panel)
{
    Panel = panel;
    FocusWnd = NULL;
    ThreadID = GetCurrentThreadId();
    Destroyed = FALSE;
}

CSalamanderForOperations::~CSalamanderForOperations()
{
    if (UnpackProgress.HWindow != NULL)
    {
        TRACE_E("Progress dialog remains opened.");
        CloseProgressDialog();
    }
    Destroyed = TRUE;
}

void CSalamanderForOperations::OpenProgressDialog(const char* title, BOOL twoProgressBars, HWND parent,
                                                  BOOL fileProgress)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::OpenProgressDialog() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::OpenProgressDialog() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow == NULL)
    {
        if (parent == NULL)
        {
            parent = MainWindow->HWindow;
            UnpackProgress.SetTaskBarList3(&MainWindow->TaskBarList3);
        }
        if (!twoProgressBars)
            UnpackProgress.Set(title, parent, CQuadWord(0, 0), fileProgress);
        else
            UnpackProgress.Set(title, parent, CQuadWord(0, 0), CQuadWord(0, 0));
        FocusWnd = GetFocus();
        EnableWindow(UnpackProgress.GetParent(), FALSE);
        UnpackProgress.Create();

        ActivateDropTarget(ProgressDialogActivateDrop, UnpackProgress.HWindow);

        ProgressDialog2 = twoProgressBars;
        PluginProgressDialog = UnpackProgress.HWindow;
    }
}

void CSalamanderForOperations::CloseProgressDialog()
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::CloseProgressDialog() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::CloseProgressDialog() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
    {
        PluginProgressDialog = NULL;
        EnableWindow(UnpackProgress.GetParent(), TRUE);
        HWND actWnd = GetForegroundWindow();
        BOOL activate = actWnd == UnpackProgress.HWindow || actWnd == UnpackProgress.GetParent();
        DestroyWindow(UnpackProgress.HWindow);
        if (activate && FocusWnd != NULL)
            SetFocus(FocusWnd);
        UnpackProgress.Init();
    }
}

void CSalamanderForOperations::ProgressSetTotalSize(const CQuadWord& totalSize1, const CQuadWord& totalSize2)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressSetTotalSize() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressSetTotalSize() after destruction!");
        return;
    }
    if (!ProgressDialog2 && totalSize2 != CQuadWord(-1, -1))
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::ProgressSetTotalSize(): progress dialog has only one progress!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.SetTotal(totalSize1, totalSize2);
}

void CSalamanderForOperations::ProgressDialogAddText(const char* txt, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressDialogAddText() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressDialogAddText() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.NewLine(txt, delayedPaint);
}

BOOL CSalamanderForOperations::ProgressAddSize(int size, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressAddSize() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressAddSize() after destruction!");
        return FALSE;
    }
    if (UnpackProgress.HWindow != NULL)
        return UnpackProgress.AddSize(size, delayedPaint) == 1;
    return TRUE;
}

BOOL CSalamanderForOperations::ProgressSetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressSetSize() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressSetSize() after destruction!");
        return FALSE;
    }
    if (!ProgressDialog2 && size2 != CQuadWord(-1, -1))
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::ProgressSetSize(): progress dialog has only one progress!");
        return FALSE;
    }
    if (UnpackProgress.HWindow != NULL)
        return UnpackProgress.SetSize(size1, size2, delayedPaint) == 1;
    return TRUE;
}

void CSalamanderForOperations::ProgressEnableCancel(BOOL enable)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::ProgressEnableCancel() only from thread ID:" << ThreadID);
        return;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::ProgressEnableCancel() after destruction!");
        return;
    }
    if (UnpackProgress.HWindow != NULL)
        UnpackProgress.EnableCancel(enable);
}

BOOL CSalamanderForOperations::MoveFiles(const char* source, const char* target, const char* remapNameFrom,
                                         const char* remapNameTo)
{
    if (ThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderForOperations::MoveFiles() only from thread ID:" << ThreadID);
        return FALSE;
    }
    if (Destroyed)
    {
        TRACE_E("You are calling CSalamanderForOperations::MoveFiles() after destruction!");
        return FALSE;
    }
    if (Panel == NULL)
    {
        TRACE_E("Incorrect call to CSalamanderForOperations::MoveFiles");
        return FALSE;
    }
    return Panel->MoveFiles(source, target, remapNameFrom, remapNameTo);
}

//
// ****************************************************************************
// CSalamanderForViewFileOnFS
//

const char*
CSalamanderForViewFileOnFS::AllocFileNameInCache(HWND parent, const char* uniqueFileName, const char* nameInCache,
                                                 const char* rootTmpPath, BOOL& fileExists)
{
    CALL_STACK_MESSAGE4("CSalamanderForViewFileOnFS::AllocFileNameInCache(, %s, %s, %s, )",
                        uniqueFileName, nameInCache, rootTmpPath);
    if (CallsCounter > 0)
    {
        TRACE_E("You are calling CSalamanderForViewFileOnFS::AllocFileNameInCache more than once! Is it O.K.?");
    }

    int errorCode;
    const char* name = DiskCache.GetName(uniqueFileName, nameInCache, &fileExists, FALSE, rootTmpPath, FALSE, NULL, &errorCode);
    if (name == NULL)
    {
        char buff[2000];
        strcpy(buff, LoadStr(IDS_VIEWFILEFAILED));
        if (errorCode == DCGNE_TOOLONGNAME)
        {
            strcat(buff, "\n");
            strcat(buff, LoadStr(IDS_VIEWFILETOOLONGNAME));
        }
        SalMessageBox(parent, buff, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
    }
    else
    {
        CallsCounter++;
    }

    return name;
}

BOOL CSalamanderForViewFileOnFS::OpenViewer(HWND parent, const char* fileName, HANDLE* fileLock,
                                            BOOL* fileLockOwner)
{
    CALL_STACK_MESSAGE2("CSalamanderForViewFileOnFS::OpenViewer(, %s, ,)", fileName);

    HANDLE lock = NULL;
    BOOL lockOwner = FALSE;

    BOOL ret = ViewFileInt(parent, fileName, AltView, HandlerID,
                           (fileLock != NULL && fileLockOwner != NULL),
                           lock, lockOwner, FALSE, -1, -1);

    if (fileLock != NULL)
        *fileLock = lock;
    if (fileLockOwner != NULL)
        *fileLockOwner = lockOwner;
    return ret;
}

void CSalamanderForViewFileOnFS::FreeFileNameInCache(const char* uniqueFileName, BOOL fileExists, BOOL newFileOK,
                                                     const CQuadWord& newFileSize, HANDLE fileLock,
                                                     BOOL fileLockOwner, BOOL removeAsSoonAsPossible)
{
    CALL_STACK_MESSAGE8("CSalamanderForViewFileOnFS::FreeFileNameInCache(%s, %d, %d, %g, 0x%p, %d, %d)",
                        uniqueFileName, fileExists, newFileOK, newFileSize.GetDouble(), fileLock,
                        fileLockOwner, removeAsSoonAsPossible);

    if (CallsCounter == 0)
    {
        TRACE_E("Unmatched call to CSalamanderForViewFileOnFS::FreeFileNameInCache!");
        return;
    }
    CallsCounter--;

    if (!fileExists) // newly downloaded copy of the file
    {
        if (newFileOK) // the download was successful
        {
            DiskCache.NamePrepared(uniqueFileName, newFileSize);
        }
        else
        {
            DiskCache.ReleaseName(uniqueFileName, FALSE); // the download failed, nothing to cache
            return;                                       // nothing else to address
        }
    }

    if (fileLock != NULL) // we have the viewer's "lock" object; link the viewer and the disk cache
    {
        DiskCache.AssignName(uniqueFileName, fileLock, fileLockOwner,
                             (fileExists || removeAsSoonAsPossible) ? crtDirect : crtCache); // for files present in the disk cache we use crtDirect, because it does not affect the "lifetime" setting (it stays as the file's author requested)
    }
    else // the viewer did not open or simply does not have a "lock" object
    {
        DiskCache.ReleaseName(uniqueFileName, !fileExists && !removeAsSoonAsPossible); // if 'removeAsSoonAsPossible' is not TRUE, at least try to keep a copy of the file in the disk cache (if it was not an existing file, we do not change its "lifetime")
    }
}

//
// ****************************************************************************
// CSalamanderDirectory
//

CSalamanderDirectory::CSalamanderDirectory(BOOL isForFS, DWORD validData, DWORD flags)
    : Dirs(10, 200), SalamDirs(10, 200), Files(10, 200)
{
    ValidData = validData;
    if (flags == -1)
        flags = isForFS ? SALDIRFLAG_IGNOREDUPDIRS : 0;
    Flags = flags;
    IsForFS = isForFS;
    AddCache = NULL;
}

CSalamanderDirectory::~CSalamanderDirectory()
{
    Clear(NULL); // plug-in data are released only in the root sal-dir
    FreeAddCache();
}

void CSalamanderDirectory::AllocAddCache()
{
    if (AddCache == NULL)
    {
        AddCache = (CSalamanderDirectoryAddCache*)malloc(sizeof(CSalamanderDirectoryAddCache));
        if (AddCache != NULL)
            ZeroMemory(AddCache, sizeof(CSalamanderDirectoryAddCache));
        // if allocating the cache fails, it is fine; we are fully functional without it
    }
}

void CSalamanderDirectory::FreeAddCache()
{
    if (AddCache != NULL)
    {
        free(AddCache);
        AddCache = NULL;
    }
}

int CSalamanderDirectory::SalDirStrCmp(const char* s1, const char* s2)
{
    if (Flags & SALDIRFLAG_CASESENSITIVE)
        return strcmp(s1, s2);
    else
        return StrICmp(s1, s2);
}

int CSalamanderDirectory::SalDirStrCmpEx(const char* s1, int l1, const char* s2, int l2)
{
    if (Flags & SALDIRFLAG_CASESENSITIVE)
        return StrCmpEx(s1, l1, s2, l2);
    else
        return StrICmpEx(s1, l1, s2, l2);
}

void CSalamanderDirectory::Clear(CPluginDataInterfaceAbstract* pluginData)
{
    if (pluginData != NULL) // release plug-in-specific data
    {
        CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
        BOOL releaseFiles = plugin.CallReleaseForFiles();
        BOOL releaseDirs = plugin.CallReleaseForDirs();
        if (releaseFiles || releaseDirs)
        {
            ReleasePluginData(plugin, releaseFiles, releaseDirs);
        }
    }
    int i;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            delete salDir;
    }
    SalamDirs.DestroyMembers();
    Dirs.DestroyMembers();
    Files.DestroyMembers();
    if (AddCache != NULL)
    {
        AddCache->PathLen = 0;
        AddCache->Path[0] = 0;
        AddCache->Dir = NULL;
    }
    ValidData = VALID_DATA_ALL_FS_ARC;
    Flags = IsForFS ? SALDIRFLAG_IGNOREDUPDIRS : 0;
}

void CSalamanderDirectory::SetValidData(DWORD validData)
{
    if (ValidData != validData)
    {
        ValidData = validData;
        int i;
        for (i = 0; i < SalamDirs.Count; i++)
        {
            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL)
                salDir->SetValidData(validData);
        }
    }
}

void CSalamanderDirectory::SetFlags(DWORD flags)
{
    if (Flags != flags)
    {
        Flags = flags;
        int i;
        for (i = 0; i < SalamDirs.Count; i++)
        {
            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL)
                salDir->SetFlags(flags);
        }
    }
}

CSalamanderDirectory*
CSalamanderDirectory::AllocSalamDir(int index)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

        if (index < 0 || index >= SalamDirs.Count || SalamDirs[index] != NULL)
    {
        TRACE_E("Unexpected error in CSalamanderDirectory::AllocSalamDir().");
        return NULL;
    }
    CSalamanderDirectory* dir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (dir == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return NULL;
    }
    SalamDirs[index] = dir;
    return dir;
}

// ***************************************************************************
// FindDir:
//
// 'path' - input: path in the archive (relative to this directory)
// 's' - output: points past the first name in the path 'path'
// 'i' - output: index of the found subdirectory (which should continue processing the path 's')
// 'file' - input: if the directory must be created, where to copy data from
// 'pluginData' - input: interface for creating plug-in-specific data for the new directory (if needed)
// 'archivePath' - input: full path in the archive ('path' and 's' both point into it)

BOOL CSalamanderDirectory::FindDir(const char* path, const char*& s, int& i, const CFileData& file,
                                   CPluginDataInterfaceAbstract* pluginData, const char* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        //  CALL_STACK_MESSAGE2("CSalamanderDirectory::FindDir(%s, , , ,)", path);
        s = path;
    while (*s != 0 && *s != '\\')
        s++;

    for (i = 0; i < Dirs.Count; i++)
    {
        if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
            break;
    }
    if (i == Dirs.Count) // we must create it
    {
        CFileData data;
        //--- name
        data.Name = (char*)malloc((s - path) + 1); // allocation
        if (data.Name == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        memcpy(data.Name, path, s - path); // copy of the text
        data.Name[s - path] = 0;
        data.NameLen = s - path;
        //--- extension
        if (!Configuration.SortDirsByExt)
            data.Ext = data.Name + data.NameLen; // directories have no extensions
        else
        {
            const char* ss = s;
            while (--ss >= path && *ss != '.')
                ;
            if (ss >= path)
                data.Ext = data.Name + (ss - path + 1); // ".cvspass" is an extension in Windows...
                                                        //      if (ss > path) data.Ext = data.Name + (ss - path + 1);
            else
                data.Ext = data.Name + data.NameLen;
        }
        //--- other fields
        data.Size = CQuadWord(0, 0);
        data.Attr = 0;
        data.LastWrite = file.LastWrite; // take the date from the first file in the directory
        data.DosName = NULL;
        data.PluginData = 0;
        data.Hidden = 0;
        data.IsLink = 0;
        data.IsOffline = 0;
        // private Salamander data
        data.Association = 0;
        data.Selected = 0;
        data.Shared = 0;
        data.Archive = 0;
        data.SizeValid = 0;
        data.Dirty = 0; // optional, kept only for formality
        data.CutToClip = 0;
        data.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
        data.IconOverlayDone = 0;

        if (pluginData != NULL) // let the plug-in add its specific data
        {
            char arcPath[MAX_PATH]; // name of the added directory inside the archive
            memcpy(arcPath, archivePath, s - archivePath);
            arcPath[s - archivePath] = 0;
            CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
            if (!plugin.GetFileDataForNewDir(arcPath, data)) // cannot add the plug-in data
            {
                free(data.Name);
                return FALSE;
            }
        }

        Dirs.Add(data);
        if (!Dirs.IsGood())
        {
            Dirs.ResetState();
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(data, TRUE);
            }
            free(data.Name);
            return FALSE;
        }
        //--- adding the Salamander directory corresponding to the new directory
        /*
    CSalamanderDirectory *dir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (dir != NULL) SalamDirs.Add((DWORD)dir);
    else TRACE_E(LOW_MEMORY);
    if (dir == NULL || !SalamDirs.IsGood())
    {
      if (dir != NULL) delete dir;
      SalamDirs.ResetState();
      if (pluginData != NULL)   // release plug-in-specific data
      {
        CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
        if (plugin.CallReleaseForDirs()) plugin.ReleasePluginData2(Dirs[Dirs.Count - 1], TRUE);
      }
      Dirs.Delete(Dirs.Count - 1);
      return FALSE;
    }
*/
        SalamDirs.Add(NULL); // add NULL (the object will be allocated the first time it is needed)
        if (!SalamDirs.IsGood())
        {
            SalamDirs.ResetState();
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(Dirs[Dirs.Count - 1], TRUE);
            }
            Dirs.Delete(Dirs.Count - 1);
            if (!Dirs.IsGood())
                Dirs.ResetState();
            return FALSE;
        }
    }
    return TRUE;
}

BOOL CSalamanderDirectory::AddFile(const char* path, CFileData& file, CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

        int pathLen = 0;
    if (path != NULL && ((pathLen = (int)strlen(path)) > MAX_PATH - 5 || file.NameLen > MAX_PATH - 5))
    {
        TRACE_E("Too long path or file name!");
        return FALSE;
    }

    //  TRACE_I("AddFile path="<<path<<" file="<<file.Name);

    // zero out variables that the plugin does not define
    if ((ValidData & VALID_DATA_EXTENSION) == 0)
        file.Ext = file.Name + file.NameLen;
    if ((ValidData & VALID_DATA_DOSNAME) == 0)
        file.DosName = NULL;
    if ((ValidData & VALID_DATA_SIZE) == 0)
        file.Size = CQuadWord(0, 0);
    if ((ValidData & VALID_DATA_DATE) == 0 || (ValidData & VALID_DATA_TIME) == 0)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if ((ValidData & (VALID_DATA_DATE | VALID_DATA_TIME)) == 0 ||
            FileTimeToLocalFileTime(&file.LastWrite, &ft) &&
                FileTimeToSystemTime(&ft, &st))
        {
            if ((ValidData & VALID_DATA_DATE) == 0) // missing date
            {
                st.wYear = 1602;
                st.wMonth = 1;
                st.wDay = 1;
                st.wDayOfWeek = 2;
            }
            if ((ValidData & VALID_DATA_TIME) == 0) // missing time
            {
                st.wHour = 0;
                st.wMinute = 0;
                st.wSecond = 0;
                st.wMilliseconds = 0;
            }
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &file.LastWrite);
        }
        else // invalid file.LastWrite
        {
            TRACE_E("CSalamanderDirectory::AddFile(): invalid file.LastWrite!");
            file.LastWrite.dwLowDateTime = 0;
            file.LastWrite.dwHighDateTime = 0;
        }
    }
    if ((ValidData & VALID_DATA_ATTRIBUTES) == 0)
        file.Attr = 0;
    if ((ValidData & VALID_DATA_HIDDEN) == 0)
        file.Hidden = 0;
    if ((ValidData & VALID_DATA_ISLINK) == 0)
        file.IsLink = 0;
    if ((ValidData & VALID_DATA_ISOFFLINE) == 0)
        file.IsOffline = 0;
    if ((ValidData & VALID_DATA_ICONOVERLAY) == 0)
        file.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    file.Association = 0;
    file.Selected = 0;
    file.Shared = 0;
    file.Archive = 0;
    file.SizeValid = 0;
    file.Dirty = 0; // optional, kept only for formality
    file.CutToClip = 0;
    file.IconOverlayDone = 0;

    // if we have the path cached from the previous addition, we can insert the file right into its place
    if (path != NULL && AddCache != NULL && pathLen > 0 &&
        pathLen == AddCache->PathLen && memcmp(path, AddCache->Path, pathLen) == 0)
    {
        // the cache already held our path, so we can insert the file immediately
        AddCache->Dir->Files.Add(file);
        if (!AddCache->Dir->Files.IsGood())
        {
            AddCache->Dir->Files.ResetState();
            return FALSE;
        }
        return TRUE;
    }

    CSalamanderDirectory* ret = AddFileInt(path, file, pluginData, path);

    // if the insertion succeeded and the cache is used, remember the path
    if (ret != NULL && AddCache != NULL && pathLen > 0)
    {
        AddCache->PathLen = pathLen;
        memcpy(AddCache->Path, path, pathLen);
        AddCache->Dir = ret;
    }

    return ret != NULL;
}

BOOL CSalamanderDirectory::AddDir(const char* path, CFileData& dir, CPluginDataInterfaceAbstract* pluginData)
{
    CALL_STACK_MESSAGE_NONE // time-critical method

        if (path != NULL && (strlen(path) > MAX_PATH - 5 || dir.NameLen > MAX_PATH - 5))
    {
        TRACE_E("Too long path or file name!");
        return FALSE;
    }

    //  TRACE_I("AddDir path="<<path<<" dir="<<dir.Name);

    // zero out variables that the plugin does not define
    if ((ValidData & VALID_DATA_EXTENSION) == 0)
        dir.Ext = dir.Name + dir.NameLen;
    if ((ValidData & VALID_DATA_DOSNAME) == 0)
        dir.DosName = NULL;
    if ((ValidData & VALID_DATA_SIZE) == 0)
        dir.Size = CQuadWord(0, 0);
    if ((ValidData & VALID_DATA_DATE) == 0 || (ValidData & VALID_DATA_TIME) == 0)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if ((ValidData & (VALID_DATA_DATE | VALID_DATA_TIME)) == 0 ||
            FileTimeToLocalFileTime(&dir.LastWrite, &ft) &&
                FileTimeToSystemTime(&ft, &st))
        {
            if ((ValidData & VALID_DATA_DATE) == 0) // missing date
            {
                st.wYear = 1602;
                st.wMonth = 1;
                st.wDay = 1;
                st.wDayOfWeek = 2;
            }
            if ((ValidData & VALID_DATA_TIME) == 0) // missing time
            {
                st.wHour = 0;
                st.wMinute = 0;
                st.wSecond = 0;
                st.wMilliseconds = 0;
            }
            SystemTimeToFileTime(&st, &ft);
            LocalFileTimeToFileTime(&ft, &dir.LastWrite);
        }
        else // invalid dir.LastWrite
        {
            TRACE_E("CSalamanderDirectory::AddDir(): invalid dir.LastWrite!");
            dir.LastWrite.dwLowDateTime = 0;
            dir.LastWrite.dwHighDateTime = 0;
        }
    }
    if ((ValidData & VALID_DATA_ATTRIBUTES) == 0)
        dir.Attr = 0;
    if ((ValidData & VALID_DATA_HIDDEN) == 0)
        dir.Hidden = 0;
    if ((ValidData & VALID_DATA_ISLINK) == 0)
        dir.IsLink = 0;
    if ((ValidData & VALID_DATA_ISOFFLINE) == 0)
        dir.IsOffline = 0;
    if ((ValidData & VALID_DATA_ICONOVERLAY) == 0)
        dir.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;

    dir.Association = 0;
    dir.Selected = 0;
    dir.Shared = 0;
    dir.Archive = 0;
    dir.SizeValid = 0;
    dir.Dirty = 0; // optional, kept only for formality
    dir.CutToClip = 0;
    dir.IconOverlayDone = 0;

    return AddDirInt(path, dir, pluginData, path) != NULL;
}

int CSalamanderDirectory::GetFilesCount() const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        return Files.Count;
}

int CSalamanderDirectory::GetDirsCount() const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        return Dirs.Count;
}

CFileData const*
CSalamanderDirectory::GetFile(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < Files.Count) return &(*((CFilesArray*)&Files))[i];
    else return NULL;
}

CFileData const*
CSalamanderDirectory::GetDir(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < Dirs.Count) return &(*((CFilesArray*)&Dirs))[i];
    else return NULL;
}

CSalamanderDirectoryAbstract const*
CSalamanderDirectory::GetSalDir(int i) const
{
    CALL_STACK_MESSAGE_NONE // time-critical method
        if (i >= 0 && i < SalamDirs.Count)
    {
        CSalamanderDirectoryAbstract const* salDir = (CSalamanderDirectoryAbstract const*)(*((TDirectArray<CSalamanderDirectory*>*)&SalamDirs))[i];
        if (salDir == NULL)
            salDir = &GlobalEmptySalDir; // it's an empty directory - return the global empty directory
        return salDir;
    }
    else return NULL;
}

CSalamanderDirectory*
CSalamanderDirectory::AddFileInt(const char* path, CFileData& file,
                                 CPluginDataInterfaceAbstract* pluginData, const char* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method; in addition, path may be NULL
                            //  CALL_STACK_MESSAGE3("CSalamanderDirectory::AddFileInt(%s, , , %s)", path, archivePath);

        if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // not this directory; find the subdirectory
        {
            const char* s;
            int i;
            if (!FindDir(path, s, i, file, pluginData, archivePath))
                return NULL;

            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL ||                    // already allocated
                (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
            {
                return salDir->AddFileInt(s, file, pluginData, archivePath);
            }
            else
                return NULL;
        }
    }

    // note: if AddCache applies, the item is added directly in AddFile
    Files.Add(file);
    if (!Files.IsGood())
    {
        Files.ResetState();
        return NULL;
    }
    return this;
}

CSalamanderDirectory*
CSalamanderDirectory::AddDirInt(const char* path, CFileData& dir,
                                CPluginDataInterfaceAbstract* pluginData, const char* archivePath)
{
    CALL_STACK_MESSAGE_NONE // time-critical method; in addition, path may be NULL
                            //  CALL_STACK_MESSAGE3("CSalamanderDirectory::AddDirInt(%s, , , %s)", path, archivePath);

        if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // not this directory; find the subdirectory
        {
            const char* s;
            int i;
            if (!FindDir(path, s, i, dir, pluginData, archivePath))
                return NULL;

            CSalamanderDirectory* salDir = SalamDirs[i];
            if (salDir != NULL ||                    // already allocated
                (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
            {
                return salDir->AddDirInt(s, dir, pluginData, archivePath);
            }
            else
                return NULL;
        }
    }

    BOOL newDir = TRUE;
    if ((Flags & SALDIRFLAG_IGNOREDUPDIRS) == 0) // if we should test for duplicate directories
    {
        int i;
        for (i = 0; i < Dirs.Count; i++)
        {
            if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, dir.Name, dir.NameLen) == 0)
                break;
        }
        newDir = (i == Dirs.Count); // not created yet
        if (!newDir)                // updating existing data
        {
            if (pluginData != NULL) // release plug-in-specific data
            {
                CPluginDataInterfaceEncapsulation plugin(pluginData, STR_NONE, STR_NONE, NULL, 0);
                if (plugin.CallReleaseForDirs())
                    plugin.ReleasePluginData2(Dirs[i], TRUE);
            }

            if (Dirs[i].Name != NULL)
                free(Dirs[i].Name);
            Dirs[i].Name = dir.Name; // rather take the new name (for possible data after '\0' in the string)
            Dirs[i].Ext = dir.Ext;
            Dirs[i].Size = dir.Size;
            Dirs[i].Attr = dir.Attr;
            Dirs[i].LastWrite = dir.LastWrite;
            if (Dirs[i].DosName != NULL)
                free(Dirs[i].DosName);
            Dirs[i].DosName = dir.DosName;
            Dirs[i].PluginData = dir.PluginData;
            // Dirs[i].NameLen should be the same as dir.NameLen
            Dirs[i].Hidden = dir.Hidden;
            Dirs[i].IsLink = dir.IsLink;
            Dirs[i].IsOffline = dir.IsOffline;
            // the remainder of Dirs[i] should be zeroed just like the rest of dir
        }
    }
    if (newDir)
    {
        //--- adding the Salamander directory corresponding to the new directory
        /*
    CSalamanderDirectory *SalamDir = new CSalamanderDirectory(IsForFS, ValidData, Flags);
    if (SalamDir != NULL) SalamDirs.Add((DWORD)SalamDir);
    else TRACE_E(LOW_MEMORY);
    if (SalamDir == NULL || !SalamDirs.IsGood())
    {
      if (SalamDir != NULL) delete SalamDir;
      SalamDirs.ResetState();
      return FALSE;
    }

    Dirs.Add(dir);
    if (!Dirs.IsGood())
    {
      Dirs.ResetState();
      SalamDirs.Delete(SalamDirs.Count - 1);
      delete SalamDir;
      return FALSE;
    }
*/
        if (IsForFS && dir.NameLen == 2 && dir.Name[0] == '.' && dir.Name[1] == '.')
        {
            CFileData* firstDir = Dirs.Count > 0 ? &Dirs[0] : NULL;
            if (firstDir != NULL && firstDir->NameLen == 2 &&
                firstDir->Name[0] == '.' && firstDir->Name[1] == '.')
            { // an up-directory is already present
                TRACE_E("CSalamanderDirectory::AddFile(): you can add up-dir (\"..\") at most once!");
                return NULL;
            }
            SalamDirs.Insert(0, NULL); // add NULL (the object will be allocated the first time it is needed)
            if (!SalamDirs.IsGood())
            {
                SalamDirs.ResetState();
                return NULL;
            }

            Dirs.Insert(0, dir);
            if (!Dirs.IsGood())
            {
                Dirs.ResetState();
                SalamDirs.Delete(0);
                if (!SalamDirs.IsGood())
                    SalamDirs.ResetState();
                return NULL;
            }
        }
        else
        {
            SalamDirs.Add(NULL); // add NULL (the object will be allocated the first time it is needed)
            if (!SalamDirs.IsGood())
            {
                SalamDirs.ResetState();
                return NULL;
            }

            Dirs.Add(dir);
            if (!Dirs.IsGood())
            {
                Dirs.ResetState();
                SalamDirs.Delete(SalamDirs.Count - 1);
                if (!SalamDirs.IsGood())
                    SalamDirs.ResetState();
                return NULL;
            }
        }
    }
    return this;
}

extern int DeltaForTotalCount(int total);

void CSalamanderDirectory::SetApproximateCount(int files, int dirs)
{
    CALL_STACK_MESSAGE3("CSalamanderDirectory::SetApproximateCount(%d, %d)", files, dirs);
    if (files > 1)
    {
        if (Files.Count == 0)
            Files.SetDelta(DeltaForTotalCount(files));
        else
            TRACE_E("CSalamanderDirectory::SetApproximateCount() Files.Count = " << Files.Count);
    }
    if (dirs > 1)
    {
        if (Dirs.Count == 0)
            Dirs.SetDelta(DeltaForTotalCount(dirs));
        else
            TRACE_E("CSalamanderDirectory::SetApproximateCount() Dirs.Count = " << Dirs.Count);
    }
}

void CSalamanderDirectory::ReleasePluginData(CPluginDataInterfaceEncapsulation& pluginData,
                                             BOOL releaseFiles, BOOL releaseDirs)
{
    SLOW_CALL_STACK_MESSAGE3("CSalamanderDirectory::ReleasePluginData(, %d, %d)",
                             releaseFiles, releaseDirs);
    if (releaseFiles)
        pluginData.ReleaseFilesOrDirs(&Files, FALSE);
    if (releaseDirs)
        pluginData.ReleaseFilesOrDirs(&Dirs, TRUE);
    int i;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            salDir->ReleasePluginData(pluginData, releaseFiles, releaseDirs);
    }
}

CFilesArray*
CSalamanderDirectory::GetDirs(const char* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetDirs(%s)", path);
    if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const char* s = path;
            while (*s != 0 && *s != '\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL ||                    // already allocated
                        (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                    {
                        return salDir->GetDirs(s);
                    }
                    else
                        return NULL; // low memory error (as if the directory did not exist)
                }
            }
        }
        else
            return &Dirs;
    }
    return NULL;
}

CFilesArray*
CSalamanderDirectory::GetFiles(const char* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetFiles(%s)", path);
    if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const char* s = path;
            while (*s != 0 && *s != '\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL ||                    // already allocated
                        (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                    {
                        return salDir->GetFiles(s);
                    }
                    else
                        return NULL; // low memory error (as if the directory did not exist)
                }
            }
        }
        else
            return &Files;
    }
    return NULL;
}

const CFileData*
CSalamanderDirectory::GetUpperDir(const char* path)
{
    CALL_STACK_MESSAGE2("CSalamanderDirectory::GetUpperDir(%s)", path);
    if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const char* s = path;
            while (*s != 0 && *s != '\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    if (*s == 0 || *(s + 1) == 0)
                        return &Dirs[i]; // the last path component = the requested parent directory
                    else
                    {
                        CSalamanderDirectory* salDir = SalamDirs[i];
                        if (salDir != NULL ||                    // already allocated
                            (salDir = AllocSalamDir(i)) != NULL) // or succeeded in allocating a new object
                        {
                            return salDir->GetUpperDir(s);
                        }
                        else
                            return NULL; // low memory error (as if the directory did not exist)
                    }
                }
            }
        }
        else
            return NULL; // for root return NULL
    }
    return NULL; // for root and unknown paths return NULL
}

CQuadWord
CSalamanderDirectory::GetSize(int* dirsCount, int* filesCount, TDirectArray<CQuadWord>* sizes)
{
    CALL_STACK_MESSAGE1("CSalamanderDirectory::GetSize(,)");
    CQuadWord size(0, 0);
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        size += Files[i].Size;
        if (sizes != NULL)
            sizes->Add(Files[i].Size); // addition failure is handled at the level of the output dialog
    }
    if (filesCount != NULL)
        *filesCount += Files.Count;
    for (i = 0; i < SalamDirs.Count; i++)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir != NULL)
            size += salDir->GetSize(dirsCount, filesCount, sizes);
    }
    if (dirsCount != NULL)
        *dirsCount += SalamDirs.Count;
    return size;
}

CQuadWord
CSalamanderDirectory::GetDirSize(const char* path, const char* dirName, int* dirsCount,
                                 int* filesCount, TDirectArray<CQuadWord>* sizes)
{
    CALL_STACK_MESSAGE3("CSalamanderDirectory::GetDirSize(%s, %s, , ,)", path, dirName);
    if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const char* s = path;
            while (*s != 0 && *s != '\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetDirSize(s, dirName, dirsCount, filesCount, sizes);
                    else
                        return CQuadWord(0, 0); // contains nothing; otherwise it would already be allocated
                }
            }
        }
        else
        {
            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmp(Dirs[i].Name, dirName) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetSize(dirsCount, filesCount, sizes);
                    else
                        return CQuadWord(0, 0); // contains nothing; otherwise it would already be allocated
                }
            }
            TRACE_E("Incorrect call to CSalamanderDirectory::GetDirSize() - directory does not exist!");
            return CQuadWord(0, 0); // not found
        }
    }
    return CQuadWord(0, 0);
}

CSalamanderDirectory*
CSalamanderDirectory::GetSalamanderDir(const char* path, BOOL readOnly)
{
    CALL_STACK_MESSAGE_NONE
    // CALL_STACK_MESSAGE3("CSalamanderDirectory::GetSalamanderDir(%s, %d)", path, readOnly);
    if (path != NULL)
    {
        if (*path == '\\')
            path++;
        if (*path != 0) // some subdirectory
        {
            const char* s = path;
            while (*s != 0 && *s != '\\')
                s++;

            int i;
            for (i = 0; i < Dirs.Count; i++)
            {
                if (SalDirStrCmpEx(Dirs[i].Name, Dirs[i].NameLen, path, (int)(s - path)) == 0)
                {
                    CSalamanderDirectory* salDir = SalamDirs[i];
                    if (salDir != NULL)
                        return salDir->GetSalamanderDir(s, readOnly);
                    else // an empty directory
                    {
                        if (readOnly)
                            return &GlobalEmptySalDir; // read-only - return the global empty directory
                        else                           // for writing
                        {
                            if ((salDir = AllocSalamDir(i)) != NULL) // we must allocate a new object
                            {
                                return salDir->GetSalamanderDir(s, readOnly);
                            }
                            else
                                return NULL; // allocation error
                        }
                    }
                }
            }
        }
        else
            return this;
    }
    return NULL;
}

CSalamanderDirectory*
CSalamanderDirectory::GetSalamanderDir(int i)
{
    if (i >= 0 && i < SalamDirs.Count)
    {
        CSalamanderDirectory* salDir = SalamDirs[i];
        if (salDir == NULL)
            salDir = &GlobalEmptySalDir; // it's an empty directory - return the global empty directory
        return salDir;
    }
    else
        return NULL;
}

int CSalamanderDirectory::GetIndex(const char* dir)
{
    if (dir != NULL)
    {
        int i;
        for (i = 0; i < Dirs.Count; i++)
        {
            if (SalDirStrCmp(Dirs[i].Name, dir) == 0)
                return i;
        }
    }
    return -1; // not found
}

// ****************************************************************************

BOOL TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize, const char* messageTitle)
{
    CQuadWord freeSpace = MyGetDiskFreeSpace(path);
    if (freeSpace != CQuadWord(-1, -1) && freeSpace < totalSize)
    {
        char buf1[50];
        char buf2[50];
        char buf3[200];
        sprintf(buf3, LoadStr(IDS_NOTENOUGHSPACE),
                NumberToStr(buf1, totalSize),
                NumberToStr(buf2, freeSpace));
        return SalMessageBox(parent, buf3, messageTitle, MB_YESNO | MB_ICONQUESTION | MSGBOXEX_ESCAPEENABLED) == IDYES;
    }
    return TRUE;
}
