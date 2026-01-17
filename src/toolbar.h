// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//*****************************************************************************
//
// CToolBarItem
//

class CToolBar;
class CTBCustomizeDialog;

class CToolBarItem
{
protected:
    DWORD Style;    // TLBI_STYLE_xxx
    DWORD State;    // TLBI_STATE_xxx
    DWORD ID;       // command id
    char* Text;     // allocated string
    int TextLen;    // length of string
    int ImageIndex; // Image index of the item. Set this member to -1 to
                    // indicate that the button does not have an image.
                    // The button layout will not include any space for
                    // a bitmap, only text.
    HICON HIcon;
    HICON HOverlay;
    DWORD CustomData; // FIXME_X64 - small for pointer, isn't it sometimes needed?
    int Width;        // width of item (computed if TLBI_STYLE_AUTOSIZE is set)

    char* Name; // name in customize dialog (valid during custimize session)

    // these values are used for optimized access to item states
    DWORD* Enabler; // Points to variable that controls item state.
                    // Non-zero value corresponds to cleared TLBI_STATE_GRAYED bit.
                    // Zero corresponds to set TLBI_STATE_GRAYED bit.

    // internal data
    int Height; // height of item
    int Offset; // position of item in whole toolbar

    WORD IconX; // placement of individual elements
    WORD TextX;
    WORD InnerX;
    WORD OutterX;

public:
    CToolBarItem();
    ~CToolBarItem();

    BOOL SetText(const char* text, int len = -1);

    friend class CToolBar;
    friend class CTBCustomizeDialog;
};

//*****************************************************************************
//
// CToolBar
//

class CBitmap;

class CToolBar : public CWindow, public CGUIToolBarAbstract
{
protected:
    TIndirectArray<CToolBarItem> Items;

    int Width; // dimensions of entire window
    int Height;
    HFONT HFont;
    int FontHeight;
    HWND HNotifyWindow; // where we will deliver notifications
    HIMAGELIST HImageList;
    HIMAGELIST HHotImageList;
    int ImageWidth; // dimensions of one image from imagelist
    int ImageHeight;
    DWORD Style;          // TLB_STYLE_xxx
    BOOL DirtyItems;      // an operation occurred that affects item layout
                          // and recalculation is needed
    CBitmap* CacheBitmap; // we draw out through this bitmap
    CBitmap* MonoBitmap;  // for grayed icons
    int CacheWidth;       // bitmap dimensions
    int CacheHeight;
    int HotIndex; // -1 = none
    int DownIndex;
    BOOL DropPressed;
    BOOL MonitorCapture;
    BOOL RelayToolTip;
    TOOLBAR_PADDING Padding;
    BOOL HasIcon;       // if it holds some icon, GetNeededSpace() function will count its height
    BOOL HasIconDirty;  // is icon presence detection needed for GetNeededSpace()?
    BOOL Customizing;   // is toolbar currently being configured
    int InserMarkIndex; // -1 = none
    BOOL InserMarkAfter;
    BOOL MouseIsTracked;  // is mouse tracked using TrackMouseEvent?
    DWORD DropDownUpTime; // time in [ms] when drop down was released, for protection against new press
    BOOL HelpMode;        // Salamander is in Shift+F1 (ctx help) mode and toolbar should highlight even disabled items under cursor

public:
    //
    // Vlastni metody
    //
    CToolBar(HWND hNotifyWindow, CObjectOrigin origin = ooAllocated);
    ~CToolBar();

    //
    // Implementace metod CGUIToolBarAbstract
    //

    virtual BOOL WINAPI CreateWnd(HWND hParent);
    virtual HWND WINAPI GetHWND() { return HWindow; }

    virtual int WINAPI GetNeededWidth(); // returns dimensions that will be needed for window
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI SetFont();
    virtual BOOL WINAPI GetItemRect(int index, RECT& r); // returns item position in screen coordinates

    virtual BOOL WINAPI CheckItem(DWORD position, BOOL byPosition, BOOL checked);
    virtual BOOL WINAPI EnableItem(DWORD position, BOOL byPosition, BOOL enabled);

    // if image list is assigned, inserts icon at corresponding position
    // normal and hot variables determine which imagelists will be affected
    virtual BOOL WINAPI ReplaceImage(DWORD position, BOOL byPosition, HICON hIcon, BOOL normal = TRUE, BOOL hot = FALSE);

    virtual int WINAPI FindItemPosition(DWORD id);

    virtual void WINAPI SetImageList(HIMAGELIST hImageList);
    virtual HIMAGELIST WINAPI GetImageList();

    virtual void WINAPI SetHotImageList(HIMAGELIST hImageList);
    virtual HIMAGELIST WINAPI GetHotImageList();

    // toolbar style
    virtual void WINAPI SetStyle(DWORD style);
    virtual DWORD WINAPI GetStyle();

    virtual BOOL WINAPI RemoveItem(DWORD position, BOOL byPosition);
    virtual void WINAPI RemoveAllItems();

    virtual int WINAPI GetItemCount() { return Items.Count; }

    // invokes configuration dialog
    virtual void WINAPI Customize();

    virtual void WINAPI SetPadding(const TOOLBAR_PADDING* padding);
    virtual void WINAPI GetPadding(TOOLBAR_PADDING* padding);

    // goes through all items and if they have 'EnablerData' pointer set
    // compares values (it points to) with actual item state.
    // If state differs, changes it.
    virtual void WINAPI UpdateItemsState();

    // if point is over some item (not over separator), returns its index.
    // otherwise returns negative number
    virtual int WINAPI HitTest(int xPos, int yPos);

    // returns TRUE if position is at item boundary; then also sets 'index'
    // to this item and variable 'after' which indicates whether it's left or
    // right side of item. If it's over some item, returns FALSE.
    // If point is not over any item, returns TRUE and sets 'index' to -1.
    virtual BOOL WINAPI InsertMarkHitTest(int xPos, int yPos, int& index, BOOL& after);

    // sets InsertMark at position index (before or after)
    // if index == -1, removes InsertMark
    virtual void WINAPI SetInsertMark(int index, BOOL after);

    // Sets the hot item in a toolbar. Returns the index of the previous hot item, or -1 if there was no hot item.
    virtual int WINAPI SetHotItem(int index);

    // screen color depth may have changed; CacheBitmap needs to be rebuilt
    virtual void WINAPI OnColorsChanged();

    virtual BOOL WINAPI InsertItem2(DWORD position, BOOL byPosition, const TLBI_ITEM_INFO2* tii);
    virtual BOOL WINAPI SetItemInfo2(DWORD position, BOOL byPosition, const TLBI_ITEM_INFO2* tii);
    virtual BOOL WINAPI GetItemInfo2(DWORD position, BOOL byPosition, TLBI_ITEM_INFO2* tii);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void DrawDropDown(HDC hDC, int x, int y, BOOL grayed);
    void DrawItem(int index);
    void DrawItem(HDC hDC, int index);
    void DrawAllItems(HDC hDC);

    void DrawInsertMark(HDC hDC);

    // returns TRUE if there is item at position; then also sets 'index'
    // otherwise returns FALSE
    // if user clicked on drop down, sets 'dropDown' to TRUE
    BOOL HitTest(int xPos, int yPos, int& index, BOOL& dropDown);

    // goes through all items and calculates their 'MinWidth' and 'XOffset'
    // is controlled by (and sets) DirtyItems
    // returns TRUE if all items were redrawn
    BOOL Refresh();

    friend class CTBCustomizeDialog;
};

//*****************************************************************************
//
// CTBCustomizeDialog
//

class CTBCustomizeDialog : public CCommonDialog
{
    enum TBCDDragMode
    {
        tbcdDragNone,
        tbcdDragAvailable,
        tbcdDragCurrent,
    };

protected:
    TDirectArray<TLBI_ITEM_INFO2> AllItems; // all available items
    CToolBar* ToolBar;
    HWND HAvailableLB;
    HWND HCurrentLB;
    DWORD DragNotify;
    TBCDDragMode DragMode;
    int DragIndex;

public:
    CTBCustomizeDialog(CToolBar* toolBar);
    ~CTBCustomizeDialog();
    BOOL Execute();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void DestroyItems();
    BOOL EnumButtons(); // fills Items array with all buttons that toolbar can hold using WM_USER_TBENUMBUTTON2 notification

    BOOL PresentInToolBar(DWORD id);      // is this command in toolbar?
    BOOL FindIndex(DWORD id, int* index); // searches for command in AllItems
    void FillLists();                     // fills both listboxes

    void EnableControls();
    void MoveItem(int srcIndex, int tgtIndex);
    void OnAdd();
    void OnRemove();
    void OnUp();
    void OnDown();
    void OnReset();
};

//*****************************************************************************
//
// CMainToolBar
//
// Configurable toolbar that carries buttons with commands. Located at the top
// of Salamander and above each panel.
//

enum CMainToolBarType
{
    mtbtTop,
    mtbtMiddle,
    mtbtLeft,
    mtbtRight,
};

class CMainToolBar : public CToolBar
{
protected:
    CMainToolBarType Type;

public:
    CMainToolBar(HWND hNotifyWindow, CMainToolBarType type, CObjectOrigin origin = ooStatic);

    BOOL Load(const char* data);
    BOOL Save(char* data);

    // need to return tooltip
    void OnGetToolTip(LPARAM lParam);
    // during configuration fills configuration dialog with items
    BOOL OnEnumButton(LPARAM lParam);
    // user pressed reset in configuration dialog - we load default configuration
    void OnReset();

    void SetType(CMainToolBarType type);

protected:
    // fills 'tii' with data for item 'tbbeIndex' and returns TRUE
    // if item is not complete (cancelled command), returns FALSE
    BOOL FillTII(int tbbeIndex, TLBI_ITEM_INFO2* tii, BOOL fillName); // 'buttonIndex' is from TBBE_xxxx family; -1 = separator
};

//*****************************************************************************
//
// CBottomToolBar
//
// toolbar in bottom part of Salamander - contains help for F1-F12 in
// combination with Ctrl, Alt and Shift
//

enum CBottomTBStateEnum
{
    btbsNormal,
    btbsAlt,
    btbsCtrl,
    btbsShift,
    btbsCtrlShift,
    //  btbsCtrlAlt,
    btbsAltShift,
    //  btbsCtrlAltShift,
    btbsMenu,
    btbsCount
};

class CBottomToolBar : public CToolBar
{
public:
    CBottomToolBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    virtual BOOL WINAPI CreateWnd(HWND hParent);

    // called on every modifier change (Ctrl,Alt,Shift) - goes through filled
    // toolbar and sets its texts and IDs
    BOOL SetState(CBottomTBStateEnum state);

    // initializes static array from which we will then feed toolbar
    static BOOL InitDataFromResources();

    void OnGetToolTip(LPARAM lParam);

    virtual void WINAPI SetFont();

protected:
    CBottomTBStateEnum State;

    // internal function called from InitDataFromResources
    static BOOL InitDataResRow(CBottomTBStateEnum state, int textResID);

    // for each button finds longest text and sets button width accordingly
    BOOL SetMaxItemWidths();
};

//*****************************************************************************
//
// CUserMenuBar
//

class CUserMenuBar : public CToolBar
{
public:
    CUserMenuBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    // extracts items from UserMenu and loads buttons into toolbar
    BOOL CreateButtons();

    void ToggleLabels();
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    virtual void WINAPI SetInsertMark(int index, BOOL after);
    virtual int WINAPI SetHotItem(int index);

    void OnGetToolTip(LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CHotPathsBar
//

class CHotPathsBar : public CToolBar
{
public:
    CHotPathsBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);

    // extracts items from HotPaths and loads buttons into toolbar
    BOOL CreateButtons();

    void ToggleLabels();
    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    //    void SetInsertMark(int index, BOOL after);
    //    int SetHotItem(int index);

    void OnGetToolTip(LPARAM lParam);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CDriveBar
//

class CDrivesList;

class CDriveBar : public CToolBar
{
protected:
    // return values for List
    DWORD DriveType;
    DWORD_PTR DriveTypeParam;
    int PostCmd;
    void* PostCmdParam;
    BOOL FromContextMenu;
    CDrivesList* List;

    // cache: contains ?: or \\ for UNC or empty string
    char CheckedDrive[3];

public:
    // we want to display plugin icons in grayscale, so we must keep them in image lists
    HIMAGELIST HDrivesIcons;
    HIMAGELIST HDrivesIconsGray;

public:
    CDriveBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);
    ~CDriveBar();

    void DestroyImageLists();

    // throws out existing and loads new buttons;
    // if 'copyDrivesListFrom' is not NULL, drive data should be copied instead of re-acquired
    // 'copyDrivesListFrom' can also refer to the called object
    BOOL CreateDriveButtons(CDriveBar* copyDrivesListFrom);

    virtual int WINAPI GetNeededHeight();

    void OnGetToolTip(LPARAM lParam);

    // user clicked on button with command id
    void Execute(DWORD id);

    // presses icon corresponding to path; if such is not found,
    // none will be pressed; force variable disables cache
    void SetCheckedDrive(CFilesWindow* panel, BOOL force = FALSE);

    // if notification about adding/removing drive arrives, list needs to be refilled;
    // if 'copyDrivesListFrom' is not NULL, drive data should be copied instead of re-acquired
    // 'copyDrivesListFrom' can also refer to the called object
    void RebuildDrives(CDriveBar* copyDrivesListFrom = NULL);

    // need to open context menu; item is determined from GetMessagePos; returns TRUE
    // if button was hit and menu opened; otherwise returns FALSE
    BOOL OnContextMenu();

    // returns drive bit field as obtained during last List->BuildData()
    // if BuildData() has not run yet, returns 0
    // can be used for quick detection of any drive changes
    DWORD GetCachedDrivesMask();

    // returns bit field of available cloud storages as obtained during last List->BuildData()
    // if BuildData() has not run yet, returns 0
    // can be used for quick detection of any changes in cloud storages availability
    DWORD GetCachedCloudStoragesMask();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

//*****************************************************************************
//
// CPluginsBar
//

class CPluginsBar : public CToolBar
{
protected:
    // icons representing plugins, created using CPlugins::CreateIconsList
    HIMAGELIST HPluginsIcons;
    HIMAGELIST HPluginsIconsGray;

public:
    CPluginsBar(HWND hNotifyWindow, CObjectOrigin origin = ooStatic);
    ~CPluginsBar();

    void DestroyImageLists();

    // throws out existing and loads new buttons
    BOOL CreatePluginButtons();

    virtual int WINAPI GetNeededHeight();

    virtual void WINAPI Customize();

    void OnGetToolTip(LPARAM lParam);

    //  protected:
    //    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

extern void PrepareToolTipText(char* buff, BOOL stripHotKey);

extern void GetSVGIconsMainToolbar(CSVGIcon** svgIcons, int* svgIconsCount);
