// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//****************************************************************************
//
// CProgressBar
//
// Class is always allocated (CObjectOrigin origin = ooAllocated)

class CProgressBar : public CWindow
{
public:
    // hDlg is the parent window (dialog or window)
    // ctrlID is the ID of the child window
    CProgressBar(HWND hDlg, int ctrlID);
    ~CProgressBar();

    // SetProgress can be called from any thread, internally WM_USER_SETPROGRESS is sent
    // however, the progress bar thread must be running
    void SetProgress(DWORD progress, const char* text = NULL);
    void SetProgress2(const CQuadWord& progressCurrent, const CQuadWord& progressTotal,
                      const char* text = NULL);

    void SetSelfMoveTime(DWORD time);
    void SetSelfMoveSpeed(DWORD moveTime);
    void Stop();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hDC);

    void MoveBar();

protected:
    int Width, Height;
    DWORD Progress;
    CBitmap* Bitmap;     // bitmap for memDC -> paint cache
    int BarX;            // X coordinate of rectangle for unknown progress (for Progress==-1)
    BOOL MoveBarRight;   // is rectangle moving to the right?
    DWORD SelfMoveTime;  // 0: after calling SetProgress(-1) the rectangle moves only one notch (0 is the default value)
                         // more than 0: time in [ms] for which we will continue moving after calling SetProgress(-1)
    DWORD SelfMoveTicks; // stored GetTickCount() value during last call to SetSelfMoveTime()
    DWORD SelfMoveSpeed; // rectangle movement speed: value is in [ms] and indicates the time after which the rectangle moves
                         // minimum is 10ms, default value is 50ms -- thus 20 movements per second
                         // beware of low values, the animation itself can noticeably stress the processor
    BOOL TimerIsRunning; // is timer running?
    char* Text;          // if different from NULL, will be displayed instead of number
    HFONT HFont;         // font for progress bar
};

//****************************************************************************
//
// CStaticText
//
// Class is always allocated (CObjectOrigin origin = ooAllocated)

class CStaticText : public CWindow
{
public:
    // hDlg is the parent window (dialog or window)
    // ctrlID is the ID of the child window
    // flags is a combination of STF_* values (shared\spl_gui.h)
    CStaticText(HWND hDlg, int ctrlID, DWORD flags);
    ~CStaticText();

    // sets Text, returns TRUE on success and FALSE on insufficient memory
    BOOL SetText(const char* text);

    // note: returned Text can be NULL
    const char* GetText() { return Text; }

    // sets Text (if it starts or ends with a space, puts it in double quotes),
    // returns TRUE on success and FALSE on insufficient memory
    BOOL SetTextToDblQuotesIfNeeded(const char* text);

    // on some filesystems there can be a different path separator
    // must be different from '\0';
    void SetPathSeparator(char separator);

    // assigns text that will be displayed as a tooltip
    BOOL SetToolTipText(const char* text);

    // assigns window and id to which WM_USER_TTGETTEXT will be sent when tooltip is displayed
    void SetToolTip(HWND hNotifyWindow, DWORD id);

    // if set to TRUE, tooltip can be invoked by clicking on the text or
    // by pressing Up/Down/Space keys when the control has focus
    // tooltip will then be displayed just below the text and remain visible
    // default is FALSE
    void EnableHintToolTip(BOOL enable);

    //    void UpdateControl();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void PrepareForPaint();

    BOOL TextHitTest(POINT* screenCursorPos);
    int GetTextXOffset(); // based on Alignment, Width and TextWidth variables returns X offset of text
    void DrawFocus(HDC hDC);

    BOOL ToolTipAssigned();

    BOOL ShowHint();

    DWORD Flags;         // flags for control behavior
    char* Text;          // allocated text (UTF-8)
    int TextLen;         // string length in bytes
    wchar_t* TextW;      // allocated text (UTF-16 for Windows API)
    int TextLenW;        // string length in wchar_t
    char* Text2;         // allocated text containing ellipsis; used only with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS
    int Text2Len;        // length of Text2
    wchar_t* Text2W;     // wide char version of Text2
    int Text2LenW;       // length of Text2W in wchar_t
    int* AlpDX;          // array of substring lengths; used only with STF_END_ELLIPSIS or STF_PATH_ELLIPSIS
    int TextWidth;       // text width in points
    int TextHeight;      // text height in points
    int Allocated;       // size of allocated buffer 'Text' and 'AlpDX'
    int AllocatedW;      // size of allocated buffer 'TextW'
    int Width, Height;   // static dimensions
    CBitmap* Bitmap;     // cache for drawing; used only with STF_CACHED_PAINT
    HFONT HFont;         // font handle used for text rendering
    BOOL DestroyFont;    // if HFont is allocated, is TRUE, otherwise is FALSE
    BOOL ClipDraw;       // need to clip drawing because we would go outside
    BOOL Text2Draw;      // we will draw from buffer containing ellipsis
    int Alignment;       // 0=left, 1=center, 2=right
    char PathSeparator;  // path separator; default '\\'
    BOOL MouseIsTracked; // have we installed mouse tracking
    // tooltip support
    char* ToolTipText; // string that will be displayed as our tooltip
    HWND HToolTipNW;   // notification window
    DWORD ToolTipID;   // ID under which the tooltip should query for text
    BOOL HintMode;     // should we display tooltip as Hint?
    WORD UIState;      // accelerator display
};

//****************************************************************************
//
// CHyperLink
//

class CHyperLink : public CStaticText
{
public:
    // hDlg is the parent window (dialog or window)
    // ctrlID is the ID of the child window
    // flags is a combination of STF_* values (shared\spl_gui.h)
    CHyperLink(HWND hDlg, int ctrlID, DWORD flags = STF_UNDERLINE | STF_HYPERLINK_COLOR);

    void SetActionOpen(const char* file);
    void SetActionPostCommand(WORD command);
    BOOL SetActionShowHint(const char* text);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    void OnContextMenu(int x, int y);
    BOOL ExecuteIt();

protected:
    char File[MAX_PATH]; // if different from 0, passed to ShellExecute
    WORD Command;        // if different from 0, posted during action
    HWND HDialog;        // parent dialog
};

//****************************************************************************
//
// CColorRectangle
//
// draws the entire object area with Color
// combine with WS_EX_CLIENTEDGE
//

class CColorRectangle : public CWindow
{
protected:
    COLORREF Color;

public:
    CColorRectangle(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated);

    void SetColor(COLORREF color);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void PaintFace(HDC hdc);
};

//****************************************************************************
//
// CColorGraph
//

class CColorGraph : public CWindow
{
protected:
    HBRUSH Color1Light;
    HBRUSH Color1Dark;
    HBRUSH Color2Light;
    HBRUSH Color2Dark;

    RECT ClientRect;
    double UsedProc;

public:
    CColorGraph(HWND hDlg, int ctrlID, CObjectOrigin origin = ooAllocated);
    ~CColorGraph();

    void SetColor(COLORREF color1Light, COLORREF color1Dark,
                  COLORREF color2Light, COLORREF color2Dark);

    void SetUsed(double used); // used = <0, 1>

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    virtual void PaintFace(HDC hdc);
};

//****************************************************************************
//
// CBitmapButton
//

class CButton : public CWindow
{
protected:
    DWORD Flags;
    BOOL DropDownPressed;
    BOOL Checked;
    BOOL ButtonPressed;
    BOOL Pressed;
    BOOL DefPushButton;
    BOOL Captured;
    BOOL Space;
    RECT ClientRect;
    // tooltip support
    BOOL MouseIsTracked;  // have we installed mouse tracking
    char* ToolTipText;    // string that will be displayed as our tooltip
    HWND HToolTipNW;      // notification window
    DWORD ToolTipID;      // ID under which the tooltip should query for text
    DWORD DropDownUpTime; // time in [ms] when drop down was released, to protect against new press
    // XP Theme support
    BOOL Hot;
    WORD UIState; // accelerator display

public:
    CButton(HWND hDlg, int ctrlID, DWORD flags, CObjectOrigin origin = ooAllocated);
    ~CButton();

    // assigns text that will be displayed as a tooltip
    BOOL SetToolTipText(const char* text);

    // assigns window and id to which WM_USER_TTGETTEXT will be sent when tooltip is displayed
    void SetToolTip(HWND hNotifyWindow, DWORD id);

    DWORD GetFlags();
    void SetFlags(DWORD flags, BOOL updateWindow);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    virtual void PaintFace(HDC hdc, const RECT* rect, BOOL enabled);

    int HitTest(LPARAM lParam); // returns 0: nowhere; 1: button; 2: drop down
    void PaintFrame(HDC hDC, const RECT* r, BOOL down);
    void PaintDrop(HDC hDC, const RECT* r, BOOL enabled);
    int GetDropPartWidth();

    void RePaint();
    void NotifyParent(WORD notify);

    BOOL ToolTipAssigned();
};

//****************************************************************************
//
// CColorArrowButton
//
// background with text, followed by an arrow - used for menu expansion
//

class CColorArrowButton : public CButton
{
protected:
    COLORREF TextColor;
    COLORREF BkgndColor;
    BOOL ShowArrow;

public:
    CColorArrowButton(HWND hDlg, int ctrlID, BOOL showArrow, CObjectOrigin origin = ooAllocated);

    void SetColor(COLORREF textColor, COLORREF bkgndColor);
    //    void     SetColor(COLORREF color);

    void SetTextColor(COLORREF textColor);
    void SetBkgndColor(COLORREF bkgndColor);

    COLORREF GetTextColor() { return TextColor; }
    COLORREF GetBkgndColor() { return BkgndColor; }

protected:
    virtual void PaintFace(HDC hdc, const RECT* rect, BOOL enabled);
};

//****************************************************************************
//
// CToolbarHeader
//

//#define TOOLBARHDR_USE_SVG

class CToolBar;

class CToolbarHeader : public CWindow
{
protected:
    CToolBar* ToolBar;
#ifdef TOOLBARHDR_USE_SVG
    HIMAGELIST HEnabledImageList;
    HIMAGELIST HDisabledImageList;
#else
    HIMAGELIST HHotImageList;
    HIMAGELIST HGrayImageList;
#endif
    DWORD ButtonMask;   // used buttons
    HWND HNotifyWindow; // where I send commands
    WORD UIState;       // accelerator display

public:
    CToolbarHeader(HWND hDlg, int ctrlID, HWND hAlignWindow, DWORD buttonMask);

    void EnableToolbar(DWORD enableMask);
    void CheckToolbar(DWORD checkMask);
    void SetNotifyWindow(HWND hWnd) { HNotifyWindow = hWnd; }

protected:
#ifdef TOOLBARHDR_USE_SVG
    void CreateImageLists(HIMAGELIST* enabled, HIMAGELIST* disabled);
#endif

    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void OnPaint(HDC hDC, BOOL hideAccel, BOOL prefixOnly);
};

//****************************************************************************
//
// CAnimate
//

/*
class CAnimate: public CWindow
{
  protected:
    HBITMAP          HBitmap;             // bitmapa ze ktere tahame jednotliva policka animace
    int              FramesCount;         // pocet policek v bitmape
    int              FirstLoopFrame;      // pokud jedeme ve smycce, z konce prechazime na toto policko
    SIZE             FrameSize;           // rozmer policka v bodech
    CRITICAL_SECTION GDICriticalSection;  // kriticka sekce pro pristup ke GDI prostredkum
    CRITICAL_SECTION DataCriticalSection; // kriticka sekce pro pristup k datum
    HANDLE           HThread;
    HANDLE           HRunEvent;           // pokud je signed, animacni thread bezi
    HANDLE           HTerminateEvent;     // pokud je signed, thread se ukonci
    COLORREF         BkColor;

    // ridici promenne, prijdou ke slovu kdyz HRunEvent signed
    BOOL             SleepThread;         // thread se ma uspat, HRunEvent bude resetnut

    int              CurrentFrame;        // zero-based index prave zobrazeneho policka
    int              NestedCount;
    BOOL             MouseIsTracked;      // instalovali jsme hlidani opusteni mysi

  public:
    // 'hBitmap'          je bitmapa ze ktere vykreslujeme policka pri animaci; 
    //                    policka musi byt pod sebou a musi mit konstantni vysku
    // 'framesCount'      udava celkovy pocet policek v bitmape
    // 'firstLoopFrame'   zero-based index policka, kam se pri cyklicke
    //                    animaci vracime po dosazeni konce
    CAnimate(HBITMAP hBitmap, int framesCount, int firstLoopFrame, COLORREF bkColor, CObjectOrigin origin = ooAllocated);
    BOOL IsGood();                // dopadnul dobre konstruktor?

    void Start();                 // pokud neanimujeme, zacneme
    void Stop();                  // zastavi animaci a zobrazi uvodni policko
    void GetFrameSize(SIZE *sz);  // vraci rozmer v bodech potrebny pro zobrazeni policka

  protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);

    void Paint(HDC hdc = NULL);   // zobraz soucasne policko; pokud je hdc NULL, vytahni si DC okna
    void FirstFrame();            // nastav Frame na uvodni policko
    void NextFrame();             // nastav Frame na dalsi policko; preskakuj uvodni sekvenci

    // tela threadu
    static unsigned ThreadF(void *param);
    static unsigned AuxThreadEH(void *param);
    static DWORD WINAPI AuxThreadF(void *param);

    // ThreadF bude friend, aby mohl pristupovat na nase data
    friend static unsigned ThreadF(void *param);
};
*/

//
//  ****************************************************************************
// ChangeToArrowButton
//

BOOL ChangeToArrowButton(HWND hParent, int ctrlID);

//
//  ****************************************************************************
// ChangeToIconButton
//

BOOL ChangeToIconButton(HWND hParent, int ctrlID, int iconID);

//
//  ****************************************************************************
// VerticalAlignChildToChild
//
// used for aligning "browse" button after editline / combobox (in resource workshop it's difficult to hit the button after combobox)
// adjusts the size and position of child window 'alignID' so that it sits at the same height (and is the same height) as child 'toID'
void VerticalAlignChildToChild(HWND hParent, int alignID, int toID);

//
//  ****************************************************************************
// CondenseStaticTexts
//
// moves static texts down so that they will closely follow each other - the distance between them will be
// the width of a space in the dialog font; 'staticsArr' is an array of static IDs terminated with zero
void CondenseStaticTexts(HWND hWindow, int* staticsArr);

//
//  ****************************************************************************
// ArrangeHorizontalLines
//
// finds horizontal lines and extends them from the right to the text they follow
// additionally finds checkboxes and radioboxes that form labels for groupboxes and shortens
// them according to their text and current font in the dialog (eliminates unnecessary
// spaces created due to different screen DPI)
void ArrangeHorizontalLines(HWND hWindow);

//
//  ****************************************************************************
// GetWindowFontHeight
//
// gets current font for hWindow and returns its height
int GetWindowFontHeight(HWND hWindow);

//
//  ****************************************************************************
// CreateCheckboxImagelist
//
// creates imagelist containing two checkbox states (unchecked and checked)
// and returns its handle; 'itemSize' is the width and height of one item in points
HIMAGELIST CreateCheckboxImagelist(int itemSize);

//
//  ****************************************************************************
// SalLoadIcon
//
// loads icon specified by 'hInst' and 'iconName', returns its handle or NULL in case of
// error; 'iconSize' specifies the desired icon size; function is High DPI ready
// and returns its handle; 'itemSize' is the width and height of one item in points
//
// Note: old API LoadIcon() cannot handle larger icon sizes, so we introduce this
// function which reads icons using the new LoadIconWithScaleDown()
HICON SalLoadIcon(HINSTANCE hInst, LPCTSTR iconName, CIconSizeEnum iconSize);
