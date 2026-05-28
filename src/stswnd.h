// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//
// ****************************************************************************

class CMainToolBar;

enum CBorderLines
{
    blNone = 0x00,
    blTop = 0x01,
    blBottom = 0x02
};

enum CSecurityIconState
{
    sisNone = 0x00,      // icon is not displayed
    sisUnsecured = 0x01, // unlocked lock icon is displayed
    sisSecured = 0x02    // locked lock icon is displayed
};

/*
enum
{
  otStatusWindow = otLastWinLibObject
};
*/

//
// CHotTrackItem
//
// An item contains the first character index, character count, first character offset in pixels,
// and their length in pixels. A list of these items is created for the displayed path and kept
// in an array.
//
// For path "\\john\c\winnt
//
// these items are created:
//
// (0, 9,  0, length of the first nine characters) = \\john\c\
// (0, 14, 0, length of 14 characters)             = \\john\c\winnt
//
// For "DIR: 12"
//
// (0, 3, 0, length of three characters DIR)
// (5, 2, pixel offset of "12", length of two characters "12")

struct CHotTrackItem
{
    WORD Offset;       // offset of the first character in characters
    WORD Chars;        // character count
    WORD PixelsOffset; // offset of the first character in pixels
    WORD Pixels;       // their length in pixels
};

class CPanelStatusBar : public CWindow
{
public:
    CMainToolBar* ToolBar;
    CFilesWindow* FilesWindow;

protected:
    TDirectArray<CHotTrackItem> HotTrackItems;
    BOOL HotTrackItemsMeasured;

    int Border; // separator line at top/bottom
    char* Text;
    int TextLen; // number of characters at the 'Text' pointer, excluding terminator
    char* Size;
    int PathLen;          // -1 (path is the whole Text), otherwise path length in Text (the rest is the filter)
    BOOL History;         // display the arrow between text and size?
    BOOL Hidden;          // display the filter symbol?
    int HiddenFilesCount; // how many files are filtered out
    int HiddenDirsCount;  // and directories
    BOOL WholeTextVisible;

    BOOL ShowThrobber;             // TRUE if the 'progress' throbber should be shown after the text/hidden filter (independent of window existence)
    BOOL DelayedThrobber;          // TRUE if the timer for showing the throbber is already running
    DWORD DelayedThrobberShowTime; // GetTickCount() value when the delayed throbber should be shown (0 = not showing with delay)
    BOOL Throbber;                 // show the 'progress' throbber after the text/hidden filter? (TRUE only if the window exists)
    int ThrobberFrame;             // current animation frame index
    char* ThrobberTooltip;         // if NULL, it will not be displayed
    int ThrobberID;                // throbber identification number (-1 = invalid)

    CSecurityIconState Security;
    char* SecurityTooltip; // if NULL, it will not be displayed

    int Allocated;
    int* AlpDX; // array of lengths (from the zero-th to the X-th character in the string)
    BOOL Left;

    int ToolBarWidth; // current toolbar width

    int EllipsedChars; // number of omitted characters after the root; otherwise -1
    int EllipsedWidth; // length of the omitted string after the root; otherwise -1

    CHotTrackItem* HotItem;     // highlighted item
    CHotTrackItem* LastHotItem; // last highlighted item
    BOOL HotSize;               // the size item is highlighted
    BOOL HotHistory;            // the history item is highlighted
    BOOL HotZoom;               // the zoom item is highlighted
    BOOL HotHidden;             // the filter symbol is highlighted
    BOOL HotSecurity;           // the lock symbol is highlighted

    RECT TextRect;     // where we drew the text
    RECT HiddenRect;   // where we drew the filter symbol
    RECT SizeRect;     // where we drew the size text
    RECT HistoryRect;  // where we drew the history drop down
    RECT ZoomRect;     // where we drew the history drop down
    RECT ThrobberRect; // where we drew the throbber
    RECT SecurityRect; // where we drew the lock
    int MaxTextRight;
    BOOL MouseCaptured;
    BOOL RButtonDown;
    BOOL LButtonDown;
    POINT LButtonDownPoint; // where the user pressed LButton

    int Height;
    int Width; // dimensions

    BOOL NeedToInvalidate; // for SetAutomatic() - a change occurred, do we need to repaint?

    DWORD* SubTexts;     // DWORD array: LOWORD position, HIWORD length
    DWORD SubTextsCount; // number of items in the SubTexts array

    IDropTarget* IDropTargetPtr;

public:
    CPanelStatusBar(CFilesWindow* filesWindow, int border, CObjectOrigin origin = ooAllocated);
    ~CPanelStatusBar();

    BOOL SetSubTexts(DWORD* subTexts, DWORD subTextsCount);
    // Sets text 'text' into the status line, 'pathLen' determines the path length (the rest is the filter),
    // if 'pathLen' is not used (the path is the complete 'text'), it is equal to -1.
    BOOL SetText(const char* text, int pathLen = -1);

    // Builds the HotTrackItems array: for disks and archivers based on backslashes,
    // and asks the plugin for FS.
    void BuildHotTrackItems();

    void GetHotText(char* buffer, int bufSize);

    void DestroyWindow();

    int GetToolBarWidth() { return ToolBarWidth; }

    int GetNeededHeight();
    void SetSize(const CQuadWord& size);
    void SetHidden(int hiddenFiles, int hiddenDirs);
    void SetHistory(BOOL history);
    void SetThrobber(BOOL show, int delay = 0, BOOL calledFromDestroyWindow = FALSE); // call only from the main (GUI) thread, like other object methods
    // Sets text displayed as a tooltip when hovering over the throbber; the object makes a copy.
    // If NULL, the tooltip will not be displayed.
    void SetThrobberTooltip(const char* throbberTooltip);
    int ChangeThrobberID(); // changes ThrobberID and returns its new value
    BOOL IsThrobberVisible(int throbberID) { return ShowThrobber && ThrobberID == throbberID; }
    void HideThrobberAndSecurityIcon();

    void SetSecurity(CSecurityIconState iconState);
    void SetSecurityTooltip(const char* tooltip);

    void InvalidateIfNeeded();

    void LayoutWindow();
    void Paint(HDC hdc, BOOL highlightText = FALSE, BOOL highlightHotTrackOnly = FALSE);
    void Repaint(BOOL flashText = FALSE, BOOL hotTrackOnly = FALSE);
    void InvalidateAndUpdate(BOOL update); // can also be called for HWindow == NULL
    void FlashText(BOOL hotTrackOnly = FALSE);

    BOOL FindHotTrackItem(int xPos, int& index);

    void SetLeftPanel(BOOL left);
    BOOL ToggleToolBar();

    BOOL IsLeft() { return Left; }

    BOOL SetDriveIcon(HICON hIcon);     // icon is copied into the image list - caller code must ensure destruction
    void SetDrivePressed(BOOL pressed); // presses the drive icon

    BOOL GetTextFrameRect(RECT* r);   // returns the rectangle around the text in screen coordinates
    BOOL GetFilterFrameRect(RECT* r); // returns the rectangle around the filter symbol in screen coordinates

    // The screen color depth may have changed; CacheBitmap must be rebuilt.
    void OnColorsChanged();

    void SetFont();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void RegisterDragDrop();
    void RevokeDragDrop();

    // Creates an image list with one item used to display drag progress.
    // After dragging ends, this image list must be released.
    // The input is a point for which dxHotspot and dyHotspot offsets are computed.
    HIMAGELIST CreateDragImage(const char* text, int& dxHotspot, int& dyHotspot, int& imgWidth, int& imgHeight);

    void PaintThrobber(HDC hDC);
    //    void RepaintThrobber();

    void PaintSecurity(HDC hDC);
};
