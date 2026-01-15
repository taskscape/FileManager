// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_fs) // so that structures are independent of set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CFileData;
class CPluginFSInterfaceAbstract;
class CSalamanderDirectoryAbstract;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CSalamanderForViewFileOnFSAbstract
//
// set of Salamander methods to support ViewFile execution in CPluginFSInterfaceAbstract,
// interface validity is limited to method to which interface is passed as parameter

class CSalamanderForViewFileOnFSAbstract
{
public:
    // finds existing file copy in disk-cache or if file copy is not yet
    // in disk-cache, reserves name for it (target file e.g. for download from FTP);
    // 'uniqueFileName' is unique name of original file (by this name
    // disk-cache is searched; full file name in Salamander
    // form should suffice - "fs-name:fs-user-part"; WARNING: name is compared "case-sensitive", if
    // plugin requires "case-insensitive", must convert all names e.g. to lowercase
    // letters - see CSalamanderGeneralAbstract::ToLowerCase); 'nameInCache' is name
    // of file copy that is placed in disk-cache (expected here is last
    // part of original file name, so later in viewer title it reminds user of
    // original file); if 'rootTmpPath' is NULL, disk cache is in Windows TEMP
    // directory, otherwise path to disk-cache is in 'rootTmpPath'; on system error returns
    // NULL (should not happen at all), otherwise returns full name of file copy in disk-cache
    // and in 'fileExists' returns TRUE if file in disk-cache exists (e.g. download from FTP
    // already happened) or FALSE if file still needs to be prepared (e.g. perform
    // its download); 'parent' is parent of error messageboxes (for example too long
    // file name)
    // WARNING: if it did not return NULL (no system error occurred), it is necessary to call
    //        FreeFileNameInCache later (for same 'uniqueFileName')
    // NOTE: if FS uses disk-cache, it should at least on plugin unload call
    //        CSalamanderGeneralAbstract::RemoveFilesFromCache("fs-name:"), otherwise its
    //        file copies will unnecessarily obstruct disk-cache
    virtual const char* WINAPI AllocFileNameInCache(HWND parent, const char* uniqueFileName, const char* nameInCache,
                                                    const char* rootTmpPath, BOOL& fileExists) = 0;

    // opens file 'fileName' from windows path in user-requested viewer (either
    // using viewer association or through View With command); 'parent' is parent of error
    // messageboxes; if 'fileLock' and 'fileLockOwner' are not NULL, they return binding to
    // opened viewer (used as parameter of FreeFileNameInCache method); returns TRUE
    // if viewer was opened
    virtual BOOL WINAPI OpenViewer(HWND parent, const char* fileName, HANDLE* fileLock,
                                   BOOL* fileLockOwner) = 0;

    // must pair with AllocFileNameInCache, called after opening viewer (or after error during
    // preparation of file copy or opening viewer); 'uniqueFileName' is unique name of
    // original file (use same string as when calling AllocFileNameInCache);
    // 'fileExists' is FALSE if file copy in disk-cache did not exist and TRUE if it
    // already existed (same value as output parameter 'fileExists' of AllocFileNameInCache method);
    // if 'fileExists' is TRUE, 'newFileOK' and 'newFileSize' are ignored, otherwise 'newFileOK' is
    // TRUE if file copy was successfully prepared (e.g. download completed successfully) and
    // 'newFileSize' is size of prepared file copy; if 'newFileOK' is FALSE,
    // 'newFileSize' is ignored; 'fileLock' and 'fileLockOwner' bind opened viewer
    // with file copies in disk-cache (after closing viewer disk-cache allows canceling file
    // copy - when copy is canceled depends on disk-cache size on disk), both
    // these parameters can be obtained when calling OpenViewer method; if viewer
    // could not be opened (or file copy could not be prepared into disk-cache or viewer
    // has no binding with disk-cache), 'fileLock' is set to NULL and 'fileLockOwner' to FALSE;
    // if 'fileExists' is TRUE (file copy existed), value of 'removeAsSoonAsPossible'
    // is ignored, otherwise: if 'removeAsSoonAsPossible' is TRUE, file copy in disk-cache
    // will not be stored longer than necessary (after closing viewer deletion happens immediately; if
    // viewer did not open at all ('fileLock' is NULL), file is not inserted into disk-cache,
    // but deleted)
    virtual void WINAPI FreeFileNameInCache(const char* uniqueFileName, BOOL fileExists, BOOL newFileOK,
                                            const CQuadWord& newFileSize, HANDLE fileLock,
                                            BOOL fileLockOwner, BOOL removeAsSoonAsPossible) = 0;
};

//
// ****************************************************************************
// CPluginFSInterfaceAbstract
//
// set of plugin methods that Salamander needs to work with file system

// type of icons in panel when listing FS (used in CPluginFSInterfaceAbstract::ListCurrentPath())
#define pitSimple 0       // simple icons for files and directories - by extension (association)
#define pitFromRegistry 1 // icons loaded from registry by file/directory extension
#define pitFromPlugin 2   // icons provided by plugin (icons obtained via CPluginDataInterfaceAbstract)

// event codes (and meaning of 'param' parameter) on FS, received by CPluginFSInterfaceAbstract::Event() method:
// CPluginFSInterfaceAbstract::TryCloseOrDetach returned TRUE, but new path could not be
// opened, so we remain on current path (FS that receives this message);
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_CLOSEORDETACHCANCELED 0

// successful attachment of new FS to panel (after path change and its listing)
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_OPENED 1

// successful addition to list of detached FS (end of "panel" FS mode, start of "detached" FS mode);
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_DETACHED 2

// successful attachment of detached FS (end of "detached" FS mode, start of "panel" FS mode);
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_ATTACHED 3

// activation of Salamander main window (when window is minimized, waits for restore/maximize,
// and only then this event is sent, so any error windows are shown above Salamander),
// comes only to FS that is in panel (not detached), if changes on FS are not monitored automatically,
// this event indicates suitable moment for refresh;
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_ACTIVATEREFRESH 4

// timeout of one of this FS's timers expired, 'param' is this timer's parameter;
// WARNING: CPluginFSInterfaceAbstract::Event() method with FSE_TIMER code is called
// from main thread after WM_TIMER message delivery to main window (so e.g. any
// modal dialog may be open), so reaction to timer should
// happen silently (don't open any windows, etc.); call to
// CPluginFSInterfaceAbstract::Event() method with FSE_TIMER code can happen immediately after
// calling CPluginInterfaceForFS::OpenFS method (if timer is added in it for
// newly created FS object)
#define FSE_TIMER 5

// path change (or refresh) just happened in this FS in panel or attachment of
// this detached FS to panel (this event is sent after path change and its
// listing); FSE_PATHCHANGED comes after each successful ListCurrentPath call
// NOTE: FSE_PATHCHANGED immediately follows all FSE_OPENED and FSE_ATTACHED
// 'param' is panel containing this FS (PANEL_LEFT or PANEL_RIGHT)
#define FSE_PATHCHANGED 6

// constants indicating reason for calling CPluginFSInterfaceAbstract::TryCloseOrDetach();
// in parentheses are always stated possible values of forceClose ("FALSE->TRUE" means "first
// try without force, if FS refuses, ask user and possibly do it with force") and canDetach:
//
// (FALSE, TRUE) when changing path outside FS opened in panel
#define FSTRYCLOSE_CHANGEPATH 1
// (FALSE->TRUE, FALSE) for FS opened in panel during plugin unload (user wants unload +
// closing Salamander + before removing plugin + unload at plugin's request)
#define FSTRYCLOSE_UNLOADCLOSEFS 2
// (FALSE, TRUE) when changing path or refresh (Ctrl+R) of FS opened in panel, it was found that
// no path on FS is accessible anymore - Salamander tries to change path in panel
// to fixed-drive (if FS doesn't allow it, FS remains in panel without files and directories)
#define FSTRYCLOSE_CHANGEPATHFAILURE 3
// (FALSE, FALSE) when attaching detached FS back to panel, it was found that no path
// on this FS is accessible anymore - Salamander tries to close this detached FS (if FS refuses,
// it remains on list of detached FS - e.g. in Alt+F1/F2 menu)
#define FSTRYCLOSE_ATTACHFAILURE 4
// (FALSE->TRUE, FALSE) for detached FS during plugin unload (user wants unload +
// closing Salamander + before removing plugin + unload at plugin's request)
#define FSTRYCLOSE_UNLOADCLOSEDETACHEDFS 5
// (FALSE, FALSE) plugin called CSalamanderGeneral::CloseDetachedFS() for detached FS
#define FSTRYCLOSE_PLUGINCLOSEDETACHEDFS 6

// flags indicating which file-system services plugin provides - which
// CPluginFSInterfaceAbstract methods are defined):
// copy from FS (F5 on FS)
#define FS_SERVICE_COPYFROMFS 0x00000001
// move from FS + rename on FS (F6 on FS)
#define FS_SERVICE_MOVEFROMFS 0x00000002
// copy from disk to FS (F5 on disk)
#define FS_SERVICE_COPYFROMDISKTOFS 0x00000004
// move from disk to FS (F6 on disk)
#define FS_SERVICE_MOVEFROMDISKTOFS 0x00000008
// delete on FS (F8)
#define FS_SERVICE_DELETE 0x00000010
// quick rename on FS (F2)
#define FS_SERVICE_QUICKRENAME 0x00000020
// view from FS (F3)
#define FS_SERVICE_VIEWFILE 0x00000040
// edit from FS (F4)
#define FS_SERVICE_EDITFILE 0x00000080
// edit new file from FS (Shift+F4)
#define FS_SERVICE_EDITNEWFILE 0x00000100
// change attributes on FS (Ctrl+F2)
#define FS_SERVICE_CHANGEATTRS 0x00000200
// create directory on FS (F7)
#define FS_SERVICE_CREATEDIR 0x00000400
// show info about FS (Ctrl+F1)
#define FS_SERVICE_SHOWINFO 0x00000800
// show properties on FS (Alt+Enter)
#define FS_SERVICE_SHOWPROPERTIES 0x00001000
// calculate occupied space on FS (Alt+F10 + Ctrl+Shift+F10 + calc. needed space + spacebar key in panel)
#define FS_SERVICE_CALCULATEOCCUPIEDSPACE 0x00002000
// command line for FS (otherwise command line is disabled)
#define FS_SERVICE_COMMANDLINE 0x00008000
// get free space on FS (number in directory line)
#define FS_SERVICE_GETFREESPACE 0x00010000
// get icon of FS (icon in directory line or Disconnect dialog)
#define FS_SERVICE_GETFSICON 0x00020000
// get next directory-line FS hot-path (for shortening of current FS path in panel)
#define FS_SERVICE_GETNEXTDIRLINEHOTPATH 0x00040000
// context menu on FS (Shift+F10)
#define FS_SERVICE_CONTEXTMENU 0x00080000
// get item for change drive menu or Disconnect dialog (item for active/detached FS in Alt+F1/F2 or Disconnect dialog)
#define FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM 0x00100000
// accepts change on path notifications from Salamander (see PostChangeOnPathNotification)
#define FS_SERVICE_ACCEPTSCHANGENOTIF 0x00200000
// get path for main-window title (text in window caption) (see Configuration/Appearance/Display current path...)
// if it's not defined, full path is displayed in window caption in all display modes
#define FS_SERVICE_GETPATHFORMAINWNDTITLE 0x00400000
// Find (Alt+F7 on FS) - if it's not defined, standard Find Files and Directories dialog
// is opened even if FS is opened in panel
#define FS_SERVICE_OPENFINDDLG 0x00800000
// open active folder (Shift+F3)
#define FS_SERVICE_OPENACTIVEFOLDER 0x01000000
// show security information (click on security icon in Directory Line, see CSalamanderGeneralAbstract::ShowSecurityIcon)
#define FS_SERVICE_SHOWSECURITYINFO 0x02000000

// missing: Change Case, Convert, Properties, Make File List

// context menu types for CPluginFSInterfaceAbstract::ContextMenu() method
#define fscmItemsInPanel 0 // context menu for items in panel (selected/focused files and directories)
#define fscmPathInPanel 1  // context menu for current path in panel
#define fscmPanel 2        // context menu for panel

#define SALCMDLINE_MAXLEN 8192 // maximum length of command from Salamander command line

class CPluginFSInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginFSInterfaceEncapsulation)
    friend class CPluginFSInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns user-part of current path in this FS, 'userPart' is buffer of MAX_PATH size
    // for path, returns success
    virtual BOOL WINAPI GetCurrentPath(char* userPart) = 0;

    // returns user-part of full name of file/directory/up-dir 'file' ('isDir' is 0/1/2) on current
    // path in this FS; for up-dir directory (first in directory list and also named ".."),
    // 'isDir'==2 and method should return current path shortened by last component; 'buf'
    // is buffer of 'bufSize' size for resulting full name, returns success
    virtual BOOL WINAPI GetFullName(CFileData& file, int isDir, char* buf, int bufSize) = 0;

    // returns absolute path (including fs-name) corresponding to relative path 'path' on this FS;
    // returns FALSE if this method is not implemented (other return values are then ignored);
    // 'parent' is parent of possible messageboxes; 'fsName' is current FS name; 'path' is buffer
    // of 'pathSize' characters size, on input it contains relative path on FS, on output it contains
    // corresponding absolute path on FS; in 'success' returns TRUE if path was successfully translated
    // (string in 'path' should be used - otherwise ignored) - path change follows (if
    // it's path to this FS, ChangePath() is called); if it returns FALSE in 'success', it is assumed
    // that user has already seen error message
    virtual BOOL WINAPI GetFullFSPath(HWND parent, const char* fsName, char* path, int pathSize,
                                      BOOL& success) = 0;

    // returns user-part of root of current path in this FS, 'userPart' is buffer of MAX_PATH size
    // for path (used in "goto root" function), returns success
    virtual BOOL WINAPI GetRootPath(char* userPart) = 0;

    // compares current path in this FS and path specified via 'fsNameIndex' and 'userPart'
    // (FS name in path is from this plugin and is given by index 'fsNameIndex'), returns TRUE
    // if paths are identical; 'currentFSNameIndex' is index of current FS name
    virtual BOOL WINAPI IsCurrentPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) = 0;

    // returns TRUE if path is from this FS (which means Salamander can pass path
    // to ChangePath of this FS); path is always to one of FS from this plugin (e.g. windows
    // paths and paths to archives don't come here at all); 'fsNameIndex' is index of FS name
    // in path (index is zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData,
    // for other fs-names index is returned by CSalamanderPluginEntryAbstract::AddFSName); user-part
    // of path is 'userPart'; 'currentFSNameIndex' is index of current FS name
    virtual BOOL WINAPI IsOurPath(int currentFSNameIndex, int fsNameIndex, const char* userPart) = 0;

    // changes current path in this FS to path specified via 'fsName' and 'userPart' (exactly
    // or to nearest accessible subpath of 'userPart' - see value of 'mode'); in case
    // path is shortened because it's path to file (suspicion that it could be
    // path to file suffices - after listing path it's verified whether file exists, possibly
    // error is shown to user) and 'cutFileName' is not NULL (possible only in 'mode' 3), returns
    // in buffer 'cutFileName' (of MAX_PATH characters size) name of this file (without path),
    // otherwise in buffer 'cutFileName' returns empty string; 'currentFSNameIndex' is index
    // of current FS name; 'fsName' is buffer of MAX_PATH size, on input it contains
    // FS name in path, which is from this plugin (but doesn't have to match current FS name
    // in this object, it's enough when IsOurPath() returns TRUE for it), on output 'fsName' contains
    // current FS name in this object (must be from this plugin); 'fsNameIndex' is index
    // of FS name 'fsName' in plugin (for easier detection which FS name it is); if
    // 'pathWasCut' is not NULL, it returns TRUE in it if path was shortened; Salamander
    // uses 'cutFileName' and 'pathWasCut' for Change Directory command (Shift+F7) when entering
    // file name - this file is focused; if 'forceRefresh' is TRUE, it's
    // hard refresh (Ctrl+R) and plugin should change path without using cache information
    // (it's necessary to verify whether new path exists); 'mode' is path change mode:
    //   1 (refresh path) - shortens path if needed; don't report path non-existence (shorten
    //                      without reporting), report file instead of path, path inaccessibility and other errors
    //   2 (calling ChangePanelPathToPluginFS, back/forward in history, etc.) - shortens path
    //                      if needed; report all path errors (file
    //                      instead of path, non-existence, inaccessibility and others)
    //   3 (change-dir command) - shortens path only if it's file or path cannot be listed
    //                      (ListCurrentPath returns FALSE for it); don't report file instead of path
    //                      (shorten without reporting and return file name), report all other
    //                      path errors (non-existence, inaccessibility and others)
    // if 'mode' is 1 or 2, returns FALSE only if no path on this FS is accessible
    // (e.g. on connection failure); if 'mode' is 3, returns FALSE if
    // requested path or file is not accessible (path shortening happens only in case it's file);
    // in case FS opening is time-consuming (e.g. connecting to FTP server) and 'mode'
    // is 3, it's possible to adjust behavior like for archives - shorten path if needed and return FALSE
    // only if no path on FS is accessible, error reporting doesn't change
    virtual BOOL WINAPI ChangePath(int currentFSNameIndex, char* fsName, int fsNameIndex,
                                   const char* userPart, char* cutFileName, BOOL* pathWasCut,
                                   BOOL forceRefresh, int mode) = 0;

    // loads files and directories from current path, stores them in object 'dir' (to path NULL or
    // "", files and directories on other paths are ignored; if directory with name
    // ".." is added, it's drawn as "up-dir" symbol; file and directory names are completely
    // plugin-dependent, Salamander only displays them); Salamander finds content
    // of plugin-added columns using 'pluginData' interface (if plugin doesn't add columns
    // and has no custom icons, returns 'pluginData'==NULL); in 'iconsType' returns requested method
    // of obtaining file and directory icons for panel, pitFromPlugin degrades to pitSimple if
    // 'pluginData' is NULL (without 'pluginData' pitFromPlugin cannot be ensured); if 'forceRefresh'
    // is TRUE, it's hard refresh (Ctrl+R) and plugin should load files and directories without using
    // cache; returns TRUE on successful loading, if it returns FALSE it's error and
    // ChangePath will be called on current path, it's expected that ChangePath selects accessible subpath
    // or returns FALSE, after successful ChangePath call ListCurrentPath will be called again;
    // if it returns FALSE, return value of 'pluginData' is ignored (data in 'dir' needs to be
    // freed using 'dir.Clear(pluginData)', otherwise only Salamander part of data is freed);
    virtual BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* dir,
                                        CPluginDataInterfaceAbstract*& pluginData,
                                        int& iconsType, BOOL forceRefresh) = 0;

    // preparation of FS for closing/detaching from panel or closing detached FS; if 'forceClose'
    // is TRUE, FS will be closed regardless of return values, action was forced by user or
    // critical shutdown is in progress (more see CSalamanderGeneralAbstract::IsCriticalShutdown), anyway
    // there's no point asking user anything, FS should be closed immediately (don't open any windows);
    // if 'forceClose' is FALSE, FS can be closed or detached ('canDetach' TRUE) or just
    // closed ('canDetach' FALSE); in 'detach' returns TRUE if it wants to just detach, FALSE means
    // close; 'reason' contains reason for calling this method (one of FSTRYCLOSE_XXX); returns TRUE
    // if can close/detach, otherwise returns FALSE
    virtual BOOL WINAPI TryCloseOrDetach(BOOL forceClose, BOOL canDetach, BOOL& detach, int reason) = 0;

    // reception of event on this FS, see event codes FSE_XXX; 'param' is event parameter
    virtual void WINAPI Event(int event, DWORD param) = 0;

    // release of all FS resources except listing data (during this method call listing
    // can still be displayed in panel); called just before canceling listing in panel
    // (listing is canceled only for active FS, detached FS don't have listing) and CloseFS for this FS;
    // 'parent' is parent of possible messageboxes, if critical shutdown is in progress (more see
    // CSalamanderGeneralAbstract::IsCriticalShutdown), don't display any windows
    virtual void WINAPI ReleaseObject(HWND parent) = 0;

    // getting set of supported FS services (see constants FS_SERVICE_XXX); returns logical
    // sum of constants; called after opening this FS (see CPluginInterfaceForFSAbstract::OpenFS),
    // and then after each call to ChangePath and ListCurrentPath of this FS
    virtual DWORD WINAPI GetSupportedServices() = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETCHANGEDRIVEORDISCONNECTITEM:
    // getting item for this FS (active or detached) into Change Drive menu (Alt+F1/F2)
    // or Disconnect dialog (hotkey: F12; possible disconnect of this FS is handled by
    // CPluginInterfaceForFSAbstract::DisconnectFS method; if GetChangeDriveOrDisconnectItem returns
    // FALSE and FS is in panel, item is added with icon obtained via GetFSIcon and root path);
    // if return value is TRUE, item is added with icon 'icon' and text 'title';
    // 'fsName' is current FS name; if 'icon' is NULL, item has no icon; if
    // 'destroyIcon' is TRUE and 'icon' is not NULL, 'icon' is freed after use via Win32 API
    // function DestroyIcon; 'title' is text allocated on Salamander's heap and can contain
    // up to three columns mutually separated by '\t' (see Alt+F1/F2 menu), in Disconnect dialog
    // only second column is used; if return value is FALSE, return values
    // 'title', 'icon' and 'destroyIcon' are ignored (item is not added)
    virtual BOOL WINAPI GetChangeDriveOrDisconnectItem(const char* fsName, char*& title,
                                                       HICON& icon, BOOL& destroyIcon) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETFSICON:
    // getting FS icon for directory-line toolbar or possibly for Disconnect dialog (F12);
    // icon for Disconnect dialog is obtained here only if for this FS the
    // GetChangeDriveOrDisconnectItem method doesn't return item (e.g. RegEdit and WMobile);
    // returns icon or NULL if standard icon should be used; if 'destroyIcon' is TRUE
    // and returns icon (not NULL), returned icon is freed after use via Win32 API
    // function DestroyIcon
    // Warning: if icon resource is loaded using LoadIcon in 16x16 dimensions, LoadIcon returns
    //        32x32 icon. When subsequently drawing it to 16x16, colored
    //        contours appear around icon. Conversion 16->32->16 can be avoided using LoadImage:
    //        (HICON)LoadImage(DLLInstance, MAKEINTRESOURCE(id), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    //
    // in this method no windows must be displayed (panel content is not consistent, messages
    // must not be distributed - redraws, etc.)
    virtual HICON WINAPI GetFSIcon(BOOL& destroyIcon) = 0;

    // returns requested drop-effect for drag&drop operation from FS (can be this FS too) to this FS;
    // 'srcFSPath' is source path; 'tgtFSPath' is target path (is from this FS); 'allowedEffects'
    // contains allowed drop-effects; 'keyState' is key state (combination of flags MK_CONTROL,
    // MK_SHIFT, MK_ALT, MK_BUTTON, MK_LBUTTON, MK_MBUTTON and MK_RBUTTON, see IDropTarget::Drop);
    // 'dropEffect' contains recommended drop-effects (equal to 'allowedEffects' or limited to
    // DROPEFFECT_COPY or DROPEFFECT_MOVE if user holds Ctrl or Shift keys) and
    // returns in it chosen drop-effect (DROPEFFECT_COPY, DROPEFFECT_MOVE or DROPEFFECT_NONE);
    // if method doesn't change 'dropEffect' and it contains more effects, preferential selection
    // of Copy operation is performed
    virtual void WINAPI GetDropEffect(const char* srcFSPath, const char* tgtFSPath,
                                      DWORD allowedEffects, DWORD keyState,
                                      DWORD* dropEffect) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETFREESPACE:
    // returns in 'retValue' (must not be NULL) size of free space on FS (displayed
    // on right side of directory-line); if free space cannot be determined, returns
    // CQuadWord(-1, -1) (value is not displayed)
    virtual void WINAPI GetFSFreeSpace(CQuadWord* retValue) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETNEXTDIRLINEHOTPATH:
    // finding delimiter points in Directory Line text (for path shortening using mouse - hot-tracking);
    // 'text' is text in Directory Line (path + possibly filter); 'pathLen' is length of path in 'text'
    // (rest is filter); 'offset' is offset of character from which to search for delimiter point; returns TRUE
    // if next delimiter point exists, returns its position in 'offset'; returns FALSE if no more
    // delimiter points exist (end of text is not considered delimiter point)
    virtual BOOL WINAPI GetNextDirectoryLineHotPath(const char* text, int pathLen, int& offset) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETNEXTDIRLINEHOTPATH:
    // adjustment of shortened path text to be displayed in panel (Directory Line - path shortening
    // using mouse - hot-tracking); used if hot-text from Directory Line doesn't correspond
    // exactly to path (e.g. it's missing closing bracket - VMS paths on FTP - "[DIR1.DIR2.DIR3]");
    // 'path' is in/out buffer with path (buffer size is 'pathBufSize')
    virtual void WINAPI CompleteDirectoryLineHotPath(char* path, int pathBufSize) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_GETPATHFORMAINWNDTITLE:
    // getting text that will be displayed in main window title if display of
    // current path in main window title is enabled (see Configuration/Appearance/Display current
    // path...); 'fsName' is current FS name; if 'mode' is 1, it's
    // "Directory Name Only" mode (should display only current directory name - last
    // path component); if 'mode' is 2, it's "Shortened Path" mode (should display
    // shortened form of path - root (incl. path separator) + "..." + path
    // separator + last path component); 'buf' is buffer of 'bufSize' size for
    // resulting text; returns TRUE if it returns requested text; returns FALSE if
    // text should be created based on delimiter point information obtained via
    // GetNextDirectoryLineHotPath() method
    // NOTE: if GetSupportedServices() doesn't return also FS_SERVICE_GETPATHFORMAINWNDTITLE,
    //           full path on FS is displayed in main window title in all title
    //           display modes (even in "Directory Name Only" and "Shortened Path")
    virtual BOOL WINAPI GetPathForMainWindowTitle(const char* fsName, int mode, char* buf, int bufSize) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_SHOWINFO:
    // displays dialog with information about FS (free space, capacity, name, options, etc.);
    // 'fsName' is current FS name; 'parent' is suggested parent of displayed dialog
    virtual void WINAPI ShowInfoDialog(const char* fsName, HWND parent) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_COMMANDLINE:
    // executes command for FS in active panel from command line under panels; returns FALSE on error
    // (command is not inserted into command line history and other return values are ignored);
    // returns TRUE on successful command execution (warning: command results don't matter - important
    // is only whether it was executed (e.g. for FTP it's about whether it was successfully delivered to server));
    // 'parent' is suggested parent of possibly displayed dialogs; 'command' is buffer
    // of SALCMDLINE_MAXLEN+1 size, which on input contains executed command (actual
    // maximum command length depends on Windows version and COMSPEC environment variable content)
    // and on output new command line content (usually just cleared to empty string);
    // 'selFrom' and 'selTo' return selection position in new command line content (if they match,
    // just places cursor; if output is empty line, these values are ignored)
    // WARNING: this method should not directly change path in panel - risk of FS closure on path error
    //        (this pointer would cease to exist for the method)
    virtual BOOL WINAPI ExecuteCommandLine(HWND parent, char* command, int& selFrom, int& selTo) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_QUICKRENAME:
    // quick rename of file or directory ('isDir' is FALSE/TRUE) 'file' on FS;
    // allows opening custom dialog for quick rename (parameter 'mode' is 1)
    // or using standard dialog (when 'mode'==1 returns FALSE and 'cancel' also FALSE,
    // then Salamander opens standard dialog and passes obtained new name in 'newName' at
    // next QuickRename call with 'mode'==2); 'fsName' is current FS name; 'parent' is
    // suggested parent of possibly displayed dialogs; 'newName' is new name if
    // 'mode'==2; if returns TRUE, in 'newName' returns new name (max. MAX_PATH characters;
    // not full name, just item name in panel) - Salamander tries to focus it after
    // refresh (refresh is handled by FS itself, for example using
    // CSalamanderGeneralAbstract::PostRefreshPanelFS method); if returns FALSE and 'mode'==2,
    // returns in 'newName' incorrect new name (possibly modified in some way - e.g.
    // operation mask may already be applied) if user wants to cancel operation, returns
    // 'cancel' TRUE; if 'cancel' returns FALSE, method returns TRUE on successful completion
    // of operation, if returns FALSE at 'mode'==1, standard dialog for
    // quick rename should be opened, if returns FALSE at 'mode'==2, it's operation error (incorrect
    // new name is returned in 'newName' - standard dialog opens again and user
    // can correct incorrect name)
    virtual BOOL WINAPI QuickRename(const char* fsName, int mode, HWND parent, CFileData& file,
                                    BOOL isDir, char* newName, BOOL& cancel) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_ACCEPTSCHANGENOTIF:
    // reception of information about change on path 'path' (if 'includingSubdirs' is TRUE,
    // it includes also change in subdirectories of path 'path'); this method should decide
    // whether refresh of this FS is needed (for example using
    // CSalamanderGeneralAbstract::PostRefreshPanelFS method); applies to both active FS and
    // detached FS; 'fsName' is current FS name
    // NOTE: for plugin as a whole there exists method
    //           CPluginInterfaceAbstract::AcceptChangeOnPathNotification()
    virtual void WINAPI AcceptChangeOnPathNotification(const char* fsName, const char* path,
                                                       BOOL includingSubdirs) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_CREATEDIR:
    // creation of new directory on FS; allows opening custom dialog for directory
    // creation (parameter 'mode' is 1) or using standard dialog (when 'mode'==1 returns
    // FALSE and 'cancel' also FALSE, then Salamander opens standard dialog and passes obtained directory
    // name in 'newName' at next CreateDir call with 'mode'==2);
    // 'fsName' is current FS name; 'parent' is suggested parent of possibly displayed
    // dialogs; 'newName' is name of new directory if 'mode'==2; if returns TRUE,
    // in 'newName' returns name of new directory (max. 2 * MAX_PATH characters; not full name,
    // just item name in panel) - Salamander tries to focus it after refresh (refresh
    // is handled by FS itself, for example using CSalamanderGeneralAbstract::PostRefreshPanelFS method);
    // if returns FALSE and 'mode'==2, returns in 'newName' incorrect directory name (max. 2 * MAX_PATH
    // characters, possibly converted to absolute form); if user wants to cancel operation,
    // returns 'cancel' TRUE; if 'cancel' returns FALSE, method returns TRUE on successful completion
    // of operation, if returns FALSE at 'mode'==1, standard dialog for directory
    // creation should be opened, if returns FALSE at 'mode'==2, it's operation error (incorrect directory
    // name is returned in 'newName' - standard dialog opens again and user
    // can correct incorrect name)
    virtual BOOL WINAPI CreateDir(const char* fsName, int mode, HWND parent,
                                  char* newName, BOOL& cancel) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_VIEWFILE:
    // viewing file (directories cannot be viewed via View function) 'file' on current path
    // on FS; 'fsName' is current FS name; 'parent' is parent of possible error
    // messageboxes; 'salamander' is set of methods from Salamander needed for implementation
    // of viewing with caching
    virtual void WINAPI ViewFile(const char* fsName, HWND parent,
                                 CSalamanderForViewFileOnFSAbstract* salamander,
                                 CFileData& file) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_DELETE:
    // deleting files and directories selected in panel; allows opening custom dialog with question
    // about deletion (parameter 'mode' is 1; whether question should or shouldn't be displayed depends on value
    // of SALCFG_CNFRMFILEDIRDEL - TRUE means user wants to confirm deletion)
    // or using standard question (when 'mode'==1 returns FALSE and 'cancelOrError' also FALSE,
    // then Salamander opens standard question (if SALCFG_CNFRMFILEDIRDEL is TRUE)
    // and in case of positive answer calls Delete again with 'mode'==2); 'fsName' is current FS name;
    // 'parent' is suggested parent of possibly displayed dialogs; 'panel' identifies panel
    // (PANEL_LEFT or PANEL_RIGHT) in which FS is opened (from this panel
    // files/directories to be deleted are obtained); 'selectedFiles' + 'selectedDirs' - number of selected
    // files and directories, if both values are zero, file/directory under cursor is deleted
    // (focus), before calling Delete method either files and directories are selected or at least
    // focus is on file/directory, so there's always something to work with (no additional tests needed);
    // if returns TRUE and 'cancelOrError' is FALSE, operation completed correctly and selected
    // files/directories should be deselected (if they survived deletion); if user wants to cancel
    // operation or error occurs, 'cancelOrError' returns TRUE and files/directories
    // are not deselected; if returns FALSE at 'mode'==1 and 'cancelOrError' is FALSE,
    // standard deletion question should be opened
    virtual BOOL WINAPI Delete(const char* fsName, int mode, HWND parent, int panel,
                               int selectedFiles, int selectedDirs, BOOL& cancelOrError) = 0;

    // copy/move from FS (parameter 'copy' is TRUE/FALSE), further text talks only about
    // copy, but everything applies identically to move; 'copy' can be TRUE (copy) only
    // if GetSupportedServices() returns also FS_SERVICE_COPYFROMFS; 'copy' can be FALSE
    // (move or rename) only if GetSupportedServices() returns also FS_SERVICE_MOVEFROMFS;
    //
    // copying files and directories (from FS) selected in panel; allows opening custom dialog for
    // specifying copy target (parameter 'mode' is 1) or using standard dialog (returns FALSE
    // and 'cancelOrHandlePath' also FALSE, then Salamander opens standard dialog and passes obtained target
    // path in 'targetPath' at next CopyOrMoveFromFS call with 'mode'==2); when 'mode'==2
    // 'targetPath' is exactly string entered by user (CopyOrMoveFromFS can analyze it
    // its own way); if CopyOrMoveFromFS supports only windows target paths (or cannot
    // process user-entered path - e.g. leads to different FS or to archive), can use
    // standard path processing in Salamander (currently can process only windows paths,
    // in time may process also FS and archive paths via TEMP directory using sequence of several basic
    // operations) - returns FALSE, 'cancelOrHandlePath' TRUE and 'operationMask' TRUE/FALSE
    // (supports/doesn't support operation masks - if doesn't support and path contains mask, error
    // message is displayed), then Salamander processes path returned in 'targetPath' (currently only splitting
    // windows path into existing part, non-existing part and possibly mask; also allows creating
    // subdirectories from non-existing part) and if path is OK, calls CopyOrMoveFromFS again
    // with 'mode'==3 and in 'targetPath' with target path and possibly operation mask (two strings
    // mutually separated by zero; no mask -> two zeros at end of string), if there's any
    // error in path, calls CopyOrMoveFromFS again with 'mode'==4 in 'targetPath' with corrected erroneous target
    // path (error was already reported to user; user should get chance to correct path;
    // "." and ".." etc. may have been removed from path);
    //
    // if user specifies operation via drag&drop (drops files/directories from FS to same panel
    // or to different drop-target), 'mode'==5 and in 'targetPath' is target path of operation (can
    // be windows path, FS path and in future can count also with paths to archives),
    // 'targetPath' is terminated with two zeros (for compatibility with 'mode'==3); 'dropTarget' is
    // in this case drop-target window (used for drop-target reactivation after opening
    // operation progress-window, see CSalamanderGeneralAbstract::ActivateDropTarget); at 'mode'==5 only
    // return value TRUE makes sense;
    //
    // 'fsName' is current FS name; 'parent' is suggested parent of possibly displayed dialogs;
    // 'panel' identifies panel (PANEL_LEFT or PANEL_RIGHT) in which FS is opened (from this
    // panel files/directories to be copied are obtained);
    // 'selectedFiles' + 'selectedDirs' - number of selected files and directories, if both
    // values are zero, file/directory under cursor (focus) is copied, before calling
    // CopyOrMoveFromFS method either files and directories are selected or at least focus
    // is on file/directory, so there's always something to work with (no additional tests
    // needed); on input 'targetPath' at 'mode'==1 contains suggested target path
    // (only windows paths without mask or empty string), at 'mode'==2 contains string of
    // target path entered by user in standard dialog, at 'mode'==3 contains target
    // path and mask (separated by zero), at 'mode'==4 contains erroneous target path, at 'mode'==5
    // contains target path (windows, FS or to archive) terminated with two zeros; if
    // method returns FALSE, 'targetPath' contains on output (buffer 2 * MAX_PATH characters) when
    // 'cancelOrHandlePath'==FALSE suggested target path for standard dialog and when
    // 'cancelOrHandlePath'==TRUE target path string for processing; if method returns TRUE and
    // 'cancelOrHandlePath' FALSE, 'targetPath' contains name of item to be focused
    // in source panel (buffer 2 * MAX_PATH characters; not full name, just item name in panel;
    // if empty string, focus remains unchanged); 'dropTarget' is not NULL only in case of
    // specifying operation path via drag&drop (see description above)
    //
    // if returns TRUE and 'cancelOrHandlePath' is FALSE, operation completed correctly and selected
    // files/directories should be deselected; if user wants to cancel operation or
    // error occurred, method returns TRUE and 'cancelOrHandlePath' TRUE, in both cases files/
    // directories are not deselected; if returns FALSE, standard dialog should be opened ('cancelOrHandlePath'
    // is FALSE) or path should be processed standard way ('cancelOrHandlePath' is TRUE)
    //
    // NOTE: if option to copy/move to path in target panel is offered,
    //           it's necessary to call CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath for target
    //           panel, otherwise path in this panel will not be inserted into list of working
    //           directories - List of Working Directories (Alt+F12)
    virtual BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                         int panel, int selectedFiles, int selectedDirs,
                                         char* targetPath, BOOL& operationMask,
                                         BOOL& cancelOrHandlePath, HWND dropTarget) = 0;

    // copy/move from windows path to FS (parameter 'copy' is TRUE/FALSE), further text talks only
    // about copy, but everything applies identically to move; 'copy' can be TRUE (copy)
    // only if GetSupportedServices() returns also FS_SERVICE_COPYFROMDISKTOFS; 'copy' can be FALSE
    // (move or rename) only if GetSupportedServices() returns also FS_SERVICE_MOVEFROMDISKTOFS;
    //
    // copying selected (in panel or elsewhere) files and directories to FS; at 'mode'==1 allows
    // preparing target path text for user into standard copy dialog, it's situation
    // when in source panel (panel where Copy command is executed (F5 key)) is windows
    // path and in target panel is this FS; at 'mode'==2 and 'mode'==3 plugin can perform copy operation or
    // report one of two errors: "error in path" (e.g. contains invalid characters or doesn't exist)
    // and "requested operation cannot be performed in this FS" (e.g. it's FTP, but opened path
    // in this FS differs from target path (e.g. for FTP different FTP server) - need to open
    // different/new FS; this error cannot be reported by newly opened FS);
    // WARNING: this method can be called for any target FS path of this plugin (so it can
    //        be also path with different FS name of this plugin)
    //
    // 'fsName' is current FS name; 'parent' is suggested parent of possibly displayed
    // dialogs; 'sourcePath' is source windows path (all selected files and directories
    // are addressed relative to it), at 'mode'==1 is NULL; selected files and directories
    // are specified by enumeration function 'next' whose parameter is 'nextParam', at 'mode'==1
    // are NULL; 'sourceFiles' + 'sourceDirs' - number of selected files and directories (sum
    // is always non-zero); 'targetPath' is in/out buffer min. 2 * MAX_PATH characters for target
    // path; at 'mode'==1 'targetPath' is on input current path on this FS and on output target
    // path for standard copy dialog; at 'mode'==2 'targetPath' is on input user-
    // entered target path (without modifications, including mask, etc.) and on output is ignored except case when
    // method returns FALSE (error) and 'invalidPathOrCancel' TRUE (error in path), in this case on
    // output is corrected target path (e.g. removed "." and ".."), which user will correct
    // in standard copy dialog; at 'mode'==3 'targetPath' is on input drag&drop-
    // entered target path and on output is ignored; if 'invalidPathOrCancel' is not NULL (only 'mode'==2
    // and 'mode'==3), it returns TRUE if path is incorrectly entered (contains invalid characters or
    // doesn't exist, etc.) or operation was canceled - error/cancel message is displayed
    // before this method finishes
    //
    // at 'mode'==1 method returns TRUE on success, if it returns FALSE, empty string is used as target path
    // for standard copy dialog; if method returns FALSE at 'mode'==2 and 'mode'==3,
    // different FS should be searched for operation processing (if 'invalidPathOrCancel' is FALSE) or
    // user should correct target path (if 'invalidPathOrCancel' is TRUE); if method returns TRUE
    // at 'mode'==2 or 'mode'==3, operation completed and selected files and directories should be deselected
    // (if 'invalidPathOrCancel' is FALSE) or error/operation cancellation occurred and selected files
    // and directories should not be deselected (if 'invalidPathOrCancel' is TRUE)
    //
    // WARNING: CopyOrMoveFromDiskToFS method can be called in three situations:
    //        - this FS is active in panel
    //        - this FS is detached
    //        - this FS was just created (by OpenFS call) and after method finishes is immediately destroyed
    //          (by CloseFS call) - no other method was called from it (not even ChangePath)
    virtual BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char* fsName, HWND parent,
                                               const char* sourcePath, SalEnumSelection2 next,
                                               void* nextParam, int sourceFiles, int sourceDirs,
                                               char* targetPath, BOOL* invalidPathOrCancel) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_CHANGEATTRS:
    // change attributes of files and directories selected in panel; dialog for specifying attribute changes
    // is specific to each plugin;
    // 'fsName' is current FS name; 'parent' is suggested parent of custom dialog; 'panel'
    // identifies panel (PANEL_LEFT or PANEL_RIGHT) in which FS is opened (from this
    // panel files/directories to work with are obtained);
    // 'selectedFiles' + 'selectedDirs' - number of selected files and directories,
    // if both values are zero, file/directory under cursor is used
    // (focus), before calling ChangeAttributes method either files and directories are selected or
    // at least focus is on file/directory, so there's always something to work with (no additional tests
    // needed); if returns TRUE, operation completed correctly and selected files/directories
    // should be deselected; if user wants to cancel operation or error occurs, method returns
    // FALSE and files/directories are not deselected
    virtual BOOL WINAPI ChangeAttributes(const char* fsName, HWND parent, int panel,
                                         int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_SHOWPROPERTIES:
    // display window with properties of files and directories selected in panel; properties window
    // is specific to each plugin;
    // 'fsName' is current FS name; 'parent' is suggested parent of custom window
    // (Windows properties window is non-modal - warning: non-modal window must
    // have its own thread); 'panel' identifies panel (PANEL_LEFT or PANEL_RIGHT)
    // in which FS is opened (from this panel files/directories
    // to work with are obtained); 'selectedFiles' + 'selectedDirs' - number of selected
    // files and directories, if both values are zero, file/directory under
    // cursor (focus) is used, before calling ShowProperties method either
    // files and directories are selected or at least focus is on file/directory, so there's always
    // something to work with (no additional tests needed)
    virtual void WINAPI ShowProperties(const char* fsName, HWND parent, int panel,
                                       int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_CONTEXTMENU:
    // display context menu for files and directories selected in panel (right-click
    // on items in panel) or for current path in panel (right-click
    // on change-drive button in panel toolbar) or for panel (right-click
    // behind items in panel); each plugin has its own context menu;
    //
    // 'fsName' is current FS name; 'parent' is suggested parent of context menu;
    // 'menuX' + 'menuY' are suggested coordinates of upper left corner of context menu;
    // 'type' is type of context menu (see descriptions of fscmXXX constants); 'panel'
    // identifies panel (PANEL_LEFT or PANEL_RIGHT) for which context
    // menu should be opened (from this panel files/directories/path are obtained
    // to work with); when 'type'==fscmItemsInPanel, 'selectedFiles' + 'selectedDirs'
    // is number of selected files and directories, if both values are zero, work with
    // file/directory under cursor (focus), before calling ContextMenu method either
    // files and directories are selected (and were clicked on) or at least focus is on
    // file/directory (not on updir), so there's always something to work with (no additional tests
    // needed); if 'type'!=fscmItemsInPanel, 'selectedFiles' + 'selectedDirs'
    // are always set to zero (ignored)
    virtual void WINAPI ContextMenu(const char* fsName, HWND parent, int menuX, int menuY, int type,
                                    int panel, int selectedFiles, int selectedDirs) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_CONTEXTMENU:
    // if FS is opened in panel and one of messages WM_INITPOPUP, WM_DRAWITEM,
    // WM_MENUCHAR or WM_MEASUREITEM arrives, Salamander calls HandleMenuMsg to allow plugin
    // to work with IContextMenu2 and IContextMenu3
    // plugin returns TRUE if it processed message and FALSE otherwise
    virtual BOOL WINAPI HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_OPENFINDDLG:
    // opening Find dialog for FS in panel; 'fsName' is current FS name; 'panel' identifies
    // panel (PANEL_LEFT or PANEL_RIGHT) for which Find dialog should be opened (from this panel
    // usually path for searching is obtained); returns TRUE on successful opening of Find dialog;
    // if returns FALSE, Salamander opens standard Find Files and Directories dialog
    virtual BOOL WINAPI OpenFindDialog(const char* fsName, int panel) = 0;

    // only if GetSupportedServices() returns also FS_SERVICE_OPENACTIVEFOLDER:
    // opens Explorer window for current path in panel
    // 'fsName' is current FS name; 'parent' is suggested parent of displayed dialog
    virtual void WINAPI OpenActiveFolder(const char* fsName, HWND parent) = 0;

    // only if GetSupportedServices() returns FS_SERVICE_MOVEFROMFS or FS_SERVICE_COPYFROMFS:
    // allows influencing allowed drop-effects during drag&drop from this FS; if 'allowedEffects' is not
    // NULL, contains on input currently allowed drop-effects (combination of DROPEFFECT_MOVE and DROPEFFECT_COPY),
    // on output contains drop-effects allowed by this FS (effects should only be reduced);
    // 'mode' is 0 at call that immediately precedes drag&drop operation start, effects returned
    // in 'allowedEffects' are used for DoDragDrop call (concerns entire drag&drop operation);
    // 'mode' is 1 during mouse dragging over FS from this process (can be this FS or FS from other
    // panel); at 'mode' 1 in 'tgtFSPath' is target path that will be used if drop occurs,
    // otherwise 'tgtFSPath' is NULL; 'mode' is 2 at call that immediately follows completion of
    // drag&drop operation (successful or unsuccessful)
    virtual void WINAPI GetAllowedDropEffects(int mode, const char* tgtFSPath, DWORD* allowedEffects) = 0;

    // allows plugin to change standard message "There are no items in this panel." displayed
    // in situation when there's no item in panel (file/directory/up-dir); returns FALSE if
    // standard message should be used (return value of 'textBuf' is then ignored); returns TRUE
    // if plugin returns its alternative in 'textBuf' (buffer of 'textBufSize' characters size)
    // to this message
    virtual BOOL WINAPI GetNoItemsInPanelText(char* textBuf, int textBufSize) = 0;

    // only if GetSupportedServices() returns FS_SERVICE_SHOWSECURITYINFO:
    // user clicked on security icon (see CSalamanderGeneralAbstract::ShowSecurityIcon;
    // e.g. FTPS displays dialog with server certificate); 'parent' is suggested parent of dialog
    virtual void WINAPI ShowSecurityInfo(HWND parent) = 0;

    /* remains to complete:
// calculate occupied space on FS (Alt+F10 + Ctrl+Shift+F10 + calc. needed space + spacebar key in panel)
#define FS_SERVICE_CALCULATEOCCUPIEDSPACE
// edit from FS (F4)
#define FS_SERVICE_EDITFILE
// edit new file from FS (Shift+F4)
#define FS_SERVICE_EDITNEWFILE
*/
};

//
// ****************************************************************************
// CPluginInterfaceForFSAbstract
//

class CPluginInterfaceForFSAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForFSEncapsulation)
    friend class CPluginInterfaceForFSEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "file system"; called to open FS; 'fsName' is name of FS
    // to be opened; 'fsNameIndex' is index of FS name to be opened
    // (index is zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData,
    // for other fs-names index is returned by CSalamanderPluginEntryAbstract::AddFSName);
    // returns pointer to opened FS interface CPluginFSInterfaceAbstract or
    // NULL on error
    virtual CPluginFSInterfaceAbstract* WINAPI OpenFS(const char* fsName, int fsNameIndex) = 0;

    // function for "file system", called to close FS, 'fs' is pointer to
    // opened FS interface, after this call interface 'fs' is already
    // considered invalid in Salamander and will not be used further (function pairs with OpenFS)
    // WARNING: in this method no window or dialog must be opened
    //        (windows can be opened in CPluginFSInterfaceAbstract::ReleaseObject method)
    virtual void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) = 0;

    // execution of command on item for FS in Change Drive menu or in Drive bars
    // (its addition see CSalamanderConnectAbstract::SetChangeDriveMenuItem);
    // 'panel' identifies panel we should work with - for command from Change Drive
    // menu 'panel' is always PANEL_SOURCE (this menu can be opened only for active
    // panel), for command from Drive bar can be PANEL_LEFT or PANEL_RIGHT (if
    // two Drive bars are enabled, we can work also with inactive panel)
    virtual void WINAPI ExecuteChangeDriveMenuItem(int panel) = 0;

    // opening context menu on item for FS in Change Drive menu or in Drive
    // bars or for active/detached FS in Change Drive menu; 'parent' is parent
    // of context menu; 'x' and 'y' are coordinates for opening context menu
    // (place of right mouse button click or suggested coordinates at Shift+F10);
    // if 'pluginFS' is NULL it's item for FS, otherwise 'pluginFS' is interface
    // of active/detached FS ('isDetachedFS' is FALSE/TRUE); if 'pluginFS' is not
    // NULL, in 'pluginFSName' is name of FS opened in 'pluginFS' (otherwise
    // 'pluginFSName' is NULL) and in 'pluginFSNameIndex' index of FS name opened
    // in 'pluginFS' (for easier detection which FS name it is; otherwise
    // 'pluginFSNameIndex' is -1); if returns FALSE, other return values are
    // ignored, otherwise they have this meaning: in 'refreshMenu' returns TRUE if
    // Change Drive menu refresh should be performed (for Drive bars ignored, because
    // active/detached FS are not shown on them); in 'closeMenu' returns TRUE if
    // Change Drive menu should be closed (for Drive bars nothing to close); if 'closeMenu'
    // returns TRUE and 'postCmd' is not zero, after closing Change Drive menu (for Drive bars
    // immediately) ExecuteChangeDrivePostCommand is called with parameters 'postCmd'
    // and 'postCmdParam'; 'panel' identifies panel we should work with - for
    // context menu in Change Drive menu 'panel' is always PANEL_SOURCE (this menu
    // can be opened only for active panel), for context menu in Drive bars
    // can be PANEL_LEFT or PANEL_RIGHT (if two Drive bars are enabled, we can
    // work also with inactive panel)
    virtual BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND parent, int panel, int x, int y,
                                                       CPluginFSInterfaceAbstract* pluginFS,
                                                       const char* pluginFSName, int pluginFSNameIndex,
                                                       BOOL isDetachedFS, BOOL& refreshMenu,
                                                       BOOL& closeMenu, int& postCmd, void*& postCmdParam) = 0;

    // execution of command from context menu on item for FS or for active/detached FS in
    // Change Drive menu after closing Change Drive menu or execution of command from context
    // menu on item for FS in Drive bars (only for compatibility with Change Drive menu); called
    // as reaction to return values 'closeMenu' (TRUE), 'postCmd' and 'postCmdParam' of
    // ChangeDriveMenuItemContextMenu after closing Change Drive menu (for Drive bars immediately);
    // 'panel' identifies panel we should work with - for context menu in Change Drive
    // menu 'panel' is always PANEL_SOURCE (this menu can be opened only for active panel),
    // for context menu in Drive bars can be PANEL_LEFT or PANEL_RIGHT (if
    // two Drive bars are enabled, we can work also with inactive panel)
    virtual void WINAPI ExecuteChangeDrivePostCommand(int panel, int postCmd, void* postCmdParam) = 0;

    // executes item in panel with opened FS (e.g. reaction to Enter key in panel;
    // for subdirectory/up-dir (it's up-dir if name is ".." and also is first directory)
    // path change is expected, for file opening file copy on disk with possibility that
    // changes are loaded back to FS); execution cannot be done in FS interface method, because
    // path change methods cannot be called there (FS closing can happen in them);
    // 'panel' specifies panel in which execution happens (PANEL_LEFT or PANEL_RIGHT);
    // 'pluginFS' is interface of FS opened in panel; 'pluginFSName' is name of FS opened
    // in panel; 'pluginFSNameIndex' is index of FS name opened in panel (for easier detection
    // which FS name it is); 'file' is executed file/directory/up-dir ('isDir' is 0/1/2);
    // WARNING: calling method for path change in panel can invalidate 'pluginFS' (after FS closing)
    //        and 'file'+'isDir' (listing change in panel -> cancellation of original listing items)
    // NOTE: if file is executed or worked with in other way (e.g. downloaded),
    //           it's necessary to call CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath for panel
    //           'panel', otherwise path in this panel will not be inserted into list of working
    //           directories - List of Working Directories (Alt+F12)
    virtual void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS,
                                    const char* pluginFSName, int pluginFSNameIndex,
                                    CFileData& file, int isDir) = 0;

    // performs disconnect of FS, which user requested in Disconnect dialog; 'parent' is
    // parent of possible messageboxes (still opened Disconnect dialog);
    // disconnect cannot be done in FS interface method, because FS should be destroyed;
    // 'isInPanel' is TRUE if FS is in panel, then 'panel' specifies in which panel
    // (PANEL_LEFT or PANEL_RIGHT); 'isInPanel' is FALSE if FS is detached, then
    // 'panel' is 0; 'pluginFS' is FS interface; 'pluginFSName' is FS name; 'pluginFSNameIndex'
    // is index of FS name (for easier detection which FS name it is); method returns FALSE
    // if disconnect failed and Disconnect dialog should remain open (its content
    // is refreshed to show previous successful disconnects)
    virtual BOOL WINAPI DisconnectFS(HWND parent, BOOL isInPanel, int panel,
                                     CPluginFSInterfaceAbstract* pluginFS,
                                     const char* pluginFSName, int pluginFSNameIndex) = 0;

    // converts user-part of path in buffer 'fsUserPart' (MAX_PATH characters size) from external
    // to internal format (e.g. for FTP: internal format = paths as server works with them,
    // external format = URL format = paths contain hex-escape-sequences (e.g. "%20" = " "))
    virtual void WINAPI ConvertPathToInternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // converts user-part of path in buffer 'fsUserPart' (MAX_PATH characters size) from internal
    // to external format
    virtual void WINAPI ConvertPathToExternal(const char* fsName, int fsNameIndex,
                                              char* fsUserPart) = 0;

    // this method is called only for plugin that serves as replacement for Network item
    // in Change Drive menu and in Drive bars (see CSalamanderGeneralAbstract::SetPluginIsNethood()):
    // by calling this method Salamander informs plugin that user changes path from root of UNC
    // path "\\server\share" via up-dir symbol ("..") to plugin FS to path with
    // user-part "\\server" in panel 'panel' (PANEL_LEFT or PANEL_RIGHT); purpose of this method:
    // plugin should without waiting list on this path at least this one share, so
    // its focus can happen in panel (which is normal behavior when changing path via up-dir)
    virtual void WINAPI EnsureShareExistsOnServer(int panel, const char* server, const char* share) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_fs)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__

/*   Preliminary version of help for plugin interface

  Opening, changing, listing and refresh of path:
    - for opening path in new FS ChangePath is called (first ChangePath call is always for path opening)
    - for path change ChangePath is called (second and all further ChangePath calls are path changes)
    - on fatal error ChangePath returns FALSE (FS path will not open in panel, if it was
      path change, then ChangePath will be called for original path, if that also fails,
      transition to fixed-drive path happens)
    - if ChangePath returns TRUE (success) and path was not shortened to original (whose
      listing is currently loaded), ListCurrentPath is called to get new listing
    - after successful listing ListCurrentPath returns TRUE
    - on fatal error ListCurrentPath returns FALSE and subsequent ChangePath call
      must also return FALSE
    - if current path cannot be listed, ListCurrentPath returns FALSE and subsequent
      ChangePath call must change path and return TRUE (ListCurrentPath is called again), if
      path cannot be changed anymore (root, etc.), ChangePath also returns FALSE (FS path will not open in panel,
      if it was path change, then ChangePath will be called for original path, if that also
      fails, transition to fixed-drive path happens)
    - path refresh (Ctrl+R) behaves same as path change to current path (path
      may not change at all or may be shortened or in case of fatal error changed to
      fixed-drive); during path refresh parameter 'forceRefresh' is TRUE for all calls of
      ChangePath and ListCurrentPath methods (FS must not use any
      cache for path change or listing loading - user doesn't want to use cache);

  During history navigation (back/forward) FS interface in which listing of
  FS path ('fsName':'fsUserPart') will happen, is obtained by first possible method from following:
    - FS interface in which path was last opened has not been closed yet
      and is among detached or is active in panel (is not active in second panel)
    - active FS interface in panel ('currentFSName') is from same plugin as
      'fsName' and returns TRUE for IsOurPath('currentFSName', 'fsName', 'fsUserPart')
    - first of detached FS interfaces ('currentFSName') that is from same
      plugin as 'fsName' and returns TRUE for IsOurPath('currentFSName', 'fsName',
      'fsUserPart')
    - new FS interface
*/
