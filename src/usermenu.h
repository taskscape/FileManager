// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define USRMNUARGS_MAXLEN 32772    // buffer size (+1 against maximum string length) (=32776 (Vista/Win7 through .bat) - 5 ("C:\\a ") + 1)
#define USRMNUCMDLINE_MAXLEN 32777 // buffer size (+1 against maximum string length)

//****************************************************************************
//
// CUserMenuIconBkgndReader
//

struct CUserMenuIconData
{
    char FileName[MAX_PATH];  // file name from which to read the icon at index IconIndex (via ExtractIconEx())
    DWORD IconIndex;          // see comment for FileName
    char UMCommand[MAX_PATH]; // file name from which to read the icon (via GetFileOrPathIconAux())

    HICON LoadedIcon; // NULL = icon not loaded, otherwise loaded icon handle

    CUserMenuIconData(const char* fileName, DWORD iconIndex, const char* umCommand);
    ~CUserMenuIconData();

    void Clear();
};

// Array of user-menu icon load requests, tagged with the background reader thread that owns them.
class CUserMenuIconDataArr : public TIndirectArray<CUserMenuIconData>
{
protected:
    DWORD IRThreadID; // unique thread ID for loading these icons

public:
    CUserMenuIconDataArr() : TIndirectArray<CUserMenuIconData>(50, 50) { IRThreadID = 0; }

    void SetIRThreadID(DWORD id) { IRThreadID = id; }
    DWORD GetIRThreadID() { return IRThreadID; }

    HICON GiveIconForUMI(const char* fileName, DWORD iconIndex, const char* umCommand);
};

// Background thread that extracts User Menu icons so the configuration dialog stays responsive.
class CUserMenuIconBkgndReader
{
protected:
    BOOL SysColorsChanged; // helper variable for detecting system color changes since opening the cfg dialog

    CRITICAL_SECTION CS;       // section for access to object data
    DWORD IconReaderThreadUID; // generator of unique thread IDs for reading icons
    BOOL CurIRThreadIDIsValid; // TRUE = thread is running and CurIRThreadID is valid
    DWORD CurIRThreadID;       // unique thread ID (see IconReaderThreadUID), in which icons for the current user-menu version are read
    BOOL AlreadyStopped;       // TRUE = no more icon reading, main window has closed/is closing

    int UserMenuIconsInUse;                            // > 0: icons from user menu are currently in an open menu, cannot update to new icons immediately; can be at most 2 (Salam cfg + Find: user menu)
    CUserMenuIconDataArr* UserMenuIIU_BkgndReaderData; // storage for new icons when UserMenuIconsInUse > 0
    DWORD UserMenuIIU_ThreadID;                        // thread ID storage (for checking data freshness) when UserMenuIconsInUse > 0

public:
    CUserMenuIconBkgndReader();
    ~CUserMenuIconBkgndReader();

    // main window is closing = we no longer want to receive any user menu icon data
    void EndProcessing();

    // CAUTION: 'bkgndReaderData' is deallocated inside.
    void StartBkgndReadingIcons(CUserMenuIconDataArr* bkgndReaderData);

    BOOL IsCurrentIRThreadID(DWORD threadID);

    BOOL IsReadingIcons();

    // CAUTION: after calling this function, this object is responsible for releasing 'bkgndReaderData'.
    void ReadingFinished(DWORD threadID, CUserMenuIconDataArr* bkgndReaderData);

    // Enter/leave section where icons from user menu are used and therefore cannot be
    // updated while this section is running (mainly opening user menu).
    void BeginUserMenuIconsInUse();
    void EndUserMenuIconsInUse();

    // If icons were loaded for an already obsolete user menu, returns FALSE, otherwise:
    // if icons are currently in an open menu (see UserMenuIconsInUse), returns FALSE;
    // if icons are not currently in an open menu, returns TRUE and CAUTION: does not leave CS,
    // so access from other threads is blocked (mainly access to user menu from the Find thread);
    // LeaveCSAfterUMIconsUpdate() is used to leave CS after updating icons.
    BOOL EnterCSIfCanUpdateUMIcons(CUserMenuIconDataArr** bkgndReaderData, DWORD threadID);
    void LeaveCSAfterUMIconsUpdate();

    void ResetSysColorsChanged() { SysColorsChanged = FALSE; }
    void SetSysColorsChanged() { SysColorsChanged = TRUE; }
    BOOL HasSysColorsChanged() { return SysColorsChanged; }
};

extern CUserMenuIconBkgndReader UserMenuIconBkgndReader;

//****************************************************************************
//
// CUserMenuItem
//

enum CUserMenuItemType
{
    umitItem,         // classic item
    umitSubmenuBegin, // marks popup start
    umitSubmenuEnd,   // marks popup end
    umitSeparator     // marks popup end
};

struct CUserMenuItem
{
    char *ItemName,
        *UMCommand,
        *Arguments,
        *InitDir,
        *Icon;

    int ThroughShell,
        CloseShell,
        UseWindow,
        ShowInToolbar;

    CUserMenuItemType Type;

    HICON UMIcon;

    CUserMenuItem(char* name, char* umCommand, char* arguments, char* initDir, char* icon,
                  int throughShell, int closeShell, int useWindow, int showInToolbar,
                  CUserMenuItemType type, CUserMenuIconDataArr* bkgndReaderData);

    CUserMenuItem();

    CUserMenuItem(CUserMenuItem& item, CUserMenuIconDataArr* bkgndReaderData);

    ~CUserMenuItem();

    // Tries to get the icon handle in this order:
    // a) Icon variable
    // b) SHGetFileInfo
    // c) take default from the system
    // background icon reading: if 'bkgndReaderData' is NULL, read immediately, otherwise icons
    // are read in the background - if 'getIconsFromReader' is FALSE, collect what to load into
    // 'bkgndReaderData'; if TRUE, icons are already loaded and we only take handles of loaded
    // icons from 'bkgndReaderData'.
    BOOL GetIconHandle(CUserMenuIconDataArr* bkgndReaderData, BOOL getIconsFromReader);

    // Searches ItemName for & and returns HotKey and TRUE when found.
    BOOL GetHotKey(char* key);

    BOOL Set(char* name, char* umCommand, char* arguments, char* initDir, char* icon);
    void SetType(CUserMenuItemType type);
    BOOL IsGood() { return ItemName != NULL && UMCommand != NULL &&
                           Arguments != NULL && InitDir != NULL && Icon != NULL; }
};

//****************************************************************************
//
// CUserMenuItems
//

class CUserMenuItems : public TIndirectArray<CUserMenuItem>
{
public:
    CUserMenuItems(DWORD base, DWORD delta, CDeleteType dt = dtDelete)
        : TIndirectArray<CUserMenuItem>(base, delta, dt) {}

    // Copies the list from 'source'.
    BOOL LoadUMI(CUserMenuItems& source, BOOL readNewIconsOnBkgnd);

    // Searches for the last (closing) item of the submenu addressed by variable 'index'.
    // If the terminator is not found, returns -1.
    int GetSubmenuEndIndex(int index);
};
