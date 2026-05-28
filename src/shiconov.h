// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

void InitShellIconOverlays();
void ReleaseShellIconOverlays();

struct CSQLite3DynLoadBase
{
    BOOL OK; // TRUE if SQLite3 is loaded successfully and ready to use
    HINSTANCE SQLite3DLL;

    CSQLite3DynLoadBase()
    {
        OK = FALSE;
        SQLite3DLL = NULL;
    }
    ~CSQLite3DynLoadBase()
    {
        if (SQLite3DLL != NULL)
            HANDLES(FreeLibrary(SQLite3DLL));
    }
};

struct CShellIconOverlayItem
{
    char IconOverlayName[MAX_PATH];          // key name under HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers
    IShellIconOverlayIdentifier* Identifier; // IShellIconOverlayIdentifier object, CAUTION: can be used only in the main thread
    CLSID IconOverlayIdCLSID;                // CLSID of the corresponding IShellIconOverlayIdentifier object
    int Priority;                            // priority of this icon-overlay (0-100, highest priority is zero)
    HICON IconOverlay[ICONSIZE_COUNT];       // icon-overlay in all sizes
    BOOL GoogleDriveOverlay;                 // TRUE = Google Drive handler (pada jim to, resime extra synchronizaci)

    void Cleanup();

    CShellIconOverlayItem();
    ~CShellIconOverlayItem();
};

class CShellIconOverlays
{
protected:
    TIndirectArray<CShellIconOverlayItem> Overlays; // priority-sorted list of icon-overlays
    CRITICAL_SECTION GD_CS;                         // for GoogleDrive, mutually exclude IsMemberOf calls from both icon-readers (otherwise it crashes and corrupts heap)
    BOOL GetGDAlreadyCalled;                        // TRUE = already checked where the Google Drive folder is
    char GoogleDrivePath[MAX_PATH];                 // Google Drive folder (do not call their handler elsewhere; it is very slow and crashes without added synchronization)
    BOOL GoogleDrivePathIsFromCfg;                  // is the Google Drive folder taken from Google Drive configuration (FALSE = may be only default + Google Drive may not be installed at all)
    BOOL GoogleDrivePathExists;                     // does the Google Drive folder exist on disk?

public:
    CShellIconOverlays() : Overlays(1, 5)
    {
        HANDLES(InitializeCriticalSection(&GD_CS));
        GoogleDrivePath[0] = 0;
        GetGDAlreadyCalled = FALSE;
        GoogleDrivePathIsFromCfg = FALSE;
        GoogleDrivePathExists = FALSE;
    }
    ~CShellIconOverlays() { HANDLES(DeleteCriticalSection(&GD_CS)); }

    // Adds 'item' to the array (previously sorted incorrectly by 'priority').
    BOOL Add(CShellIconOverlayItem* item /*, int priority*/);

    // Releases all icon-overlays.
    void Release() { Overlays.Destroy(); }

    // Allocates an array of IShellIconOverlayIdentifier objects for the calling thread (we use COM in
    // STA threading model, so the object must be created and used in only one thread).
    IShellIconOverlayIdentifier** CreateIconReadersIconOverlayIds();

    // Releases the array of IShellIconOverlayIdentifier objects.
    void ReleaseIconReadersIconOverlayIds(IShellIconOverlayIdentifier** iconReadersIconOverlayIds);

    // Returns icon-overlay index for file/directory "wPath+name".
    DWORD GetIconOverlayIndex(WCHAR* wPath, WCHAR* wName, char* aPath, char* aName, char* name,
                              DWORD fileAttrs, int minPriority,
                              IShellIconOverlayIdentifier** iconReadersIconOverlayIds,
                              BOOL isGoogleDrivePath);

    HICON GetIconOverlay(int iconOverlayIndex, CIconSizeEnum iconSize)
    {
        return Overlays[iconOverlayIndex]->IconOverlay[iconSize];
    }

    // Called when display color depth changes, all overlay icons must be loaded again.
    // CAUTION: can be called only from the main thread.
    void ColorsChanged();

    // If we have not done it yet, find where Google Drive lives; 'sqlite3_Dyn_InOut'
    // serves as a cache for sqlite.dll (if it is already loaded, use it + if it is loaded
    // in this function, return it).
    void InitGoogleDrivePath(CSQLite3DynLoadBase** sqlite3_Dyn_InOut, BOOL debugTestOverlays);

    BOOL HasGoogleDrivePath();

    BOOL GetPathForGoogleDrive(char* path, int pathLen)
    {
        strcpy_s(path, pathLen, GoogleDrivePath);
        return GoogleDrivePath[0] != 0;
    }

    void SetGoogleDrivePath(const char* path, BOOL pathIsFromConfig)
    {
        strcpy_s(GoogleDrivePath, path);
        GoogleDrivePathIsFromCfg = pathIsFromConfig;
        GoogleDrivePathExists = FALSE;
    }

    BOOL IsGoogleDrivePath(const char* path) { return GoogleDrivePath[0] != 0 && SalPathIsPrefix(GoogleDrivePath, path); }
};

struct CShellIconOverlayItem2 // only list of icon overlay handlers (for configuration dialog, Icon Overlays page)
{
    char IconOverlayName[MAX_PATH];  // key name under HKEY_LOCAL_MACHINE\Software\Microsoft\Windows\CurrentVersion\Explorer\ShellIconOverlayIdentifiers
    char IconOverlayDescr[MAX_PATH]; // description of icon overlay handler COM object
};

extern CShellIconOverlays ShellIconOverlays;                           // array of all available icon-overlays
extern TIndirectArray<CShellIconOverlayItem2> ListOfShellIconOverlays; // list of all icon overlay handlers
