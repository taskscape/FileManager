// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <time.h>
#include "allochan.h"
#include "menu.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "shellib.h"
#include "worker.h"
#include "iconpool.h"
#include "snooper.h"
#include "viewer.h"
#include "editwnd.h"
#include "find.h"
#include "zip.h"
#include "pack.h"
#include "cache.h"
#include "dialogs.h"
#include "gui.h"
#include "tasklist.h"
#include "toolbar.h"
#include "usermenu.h"
#include "execute.h"
#include "drivelst.h"

BOOL SalamanderBusy = TRUE;       // je Salamander busy?
DWORD LastSalamanderIdleTime = 0; // GetTickCount() z okamziku, kdy SalamanderBusy naposledy presel na TRUE

int PasteLinkIsRunning = 0; // if greater than zero, Past Shortcuts command is currently running in one of the panels

BOOL CannotCloseSalMainWnd = FALSE; // TRUE = nesmi dojit k zavreni hlavniho okna

DWORD MainThreadID = -1;

int MenuNewExceptionHasOccured = 0;
int FGIExceptionHasOccured = 0;
int ICExceptionHasOccured = 0;
int QCMExceptionHasOccured = 0;
int OCUExceptionHasOccured = 0;
int GTDExceptionHasOccured = 0;
int SHLExceptionHasOccured = 0;
int RelExceptionHasOccured = 0;

char DecimalSeparator[5] = "."; // "characters" (max. 4 characters) extracted from system
int DecimalSeparatorLen = 1;    // length in characters without terminating zero
char ThousandsSeparator[5] = " ";
int ThousandsSeparatorLen = 1;

BOOL WindowsXP64AndLater = FALSE;  // JRYFIXME - zrusit
BOOL WindowsVistaAndLater = FALSE; // JRYFIXME - zrusit
BOOL Windows7AndLater = FALSE;     // JRYFIXME - zrusit
BOOL Windows8AndLater = FALSE;
BOOL Windows8_1AndLater = FALSE;
BOOL Windows10AndLater = FALSE;

BOOL Windows64Bit = FALSE;

BOOL RunningAsAdmin = FALSE;

DWORD CCVerMajor = 0;
DWORD CCVerMinor = 0;

char ConfigurationName[MAX_PATH];
BOOL ConfigurationNameIgnoreIfNotExists = TRUE;

int StopRefresh = 0;

BOOL ExecCmdsOrUnloadMarkedPlugins = FALSE;
BOOL OpenPackOrUnpackDlgForMarkedPlugins = FALSE;

int StopIconRepaint = 0;
BOOL PostAllIconsRepaint = FALSE;

int StopStatusbarRepaint = 0;
BOOL PostStatusbarRepaint = FALSE;

int ChangeDirectoryAllowed = 0;
BOOL ChangeDirectoryRequest = FALSE;

BOOL SkipOneActivateRefresh = FALSE;

const char* DirColumnStr = NULL;
int DirColumnStrLen = 0;
const char* ColExtStr = NULL;
int ColExtStrLen = 0;
int TextEllipsisWidth = 0;
int TextEllipsisWidthEnv = 0;
const char* ProgDlgHoursStr = NULL;
const char* ProgDlgMinutesStr = NULL;
const char* ProgDlgSecsStr = NULL;

char FolderTypeName[80] = "";
int FolderTypeNameLen = 0;
const char* UpDirTypeName = NULL;
int UpDirTypeNameLen = 0;
const char* CommonFileTypeName = NULL;
int CommonFileTypeNameLen = 0;
const char* CommonFileTypeName2 = NULL;

char WindowsDirectory[MAX_PATH] = "";

// to ensure escape from removed drives to fixed drive (after ejecting device - USB flash disk, etc.)
BOOL ChangeLeftPanelToFixedWhenIdleInProgress = FALSE; // TRUE = path is currently being changed, setting ChangeLeftPanelToFixedWhenIdle to TRUE is unnecessary
BOOL ChangeLeftPanelToFixedWhenIdle = FALSE;
BOOL ChangeRightPanelToFixedWhenIdleInProgress = FALSE; // TRUE = path is currently being changed, setting ChangeRightPanelToFixedWhenIdle to TRUE is unnecessary
BOOL ChangeRightPanelToFixedWhenIdle = FALSE;
BOOL OpenCfgToChangeIfPathIsInaccessibleGoTo = FALSE; // TRUE = in idle opens configuration on Drives and focuses "If path in panel is inaccessible, go to:"

char IsSLGIncomplete[ISSLGINCOMPLETE_SIZE]; // if string is empty, SLG is completely translated; otherwise contains URL to forum section for the given language

UINT TaskbarBtnCreatedMsg = 0;

// ****************************************************************************

CMainWindowLock MainWindowCS;
BOOL CanDestroyMainWindow = FALSE;
CMainWindow* MainWindow = NULL;
CFilesWindow* DropSourcePanel = NULL;
BOOL OurClipDataObject = FALSE;
const char* SALCF_IDATAOBJECT = "SalIDataObject";
const char* SALCF_FAKE_REALPATH = "SalFakeRealPath";
const char* SALCF_FAKE_SRCTYPE = "SalFakeSrcType";
const char* SALCF_FAKE_SRCFSPATH = "SalFakeSrcFSPath";

const char* MAINWINDOW_NAME = "Open Salamander";
const char* CMAINWINDOW_CLASSNAME = "SalamanderMainWindowVer25";
const char* SAVEBITS_CLASSNAME = "SalamanderSaveBits";
const char* SHELLEXECUTE_CLASSNAME = "SalamanderShellExecute";

CAssociations Associations; // associations loaded from registry
CShares Shares;

char DefaultDir['Z' - 'A' + 1][MAX_PATH];

HACCEL AccelTable1 = NULL;
HACCEL AccelTable2 = NULL;

HINSTANCE NtDLL = NULL;             // handle to ntdll.dll
HINSTANCE Shell32DLL = NULL;        // handle to shell32.dll (icons)
HINSTANCE ImageResDLL = NULL;       // handle to imageres.dll (icons - Vista)
HINSTANCE User32DLL = NULL;         // handle to user32.dll (DisableProcessWindowsGhosting)
HINSTANCE HLanguage = NULL;         // handle to language-dependent resources (.SPL file)
char CurrentHelpDir[MAX_PATH] = ""; // after first help usage, contains path to help directory (location of all .chm files)
WORD LanguageID = 0;                // language-id of .SPL file

char OpenReadmeInNotepad[MAX_PATH]; // used only when launched from installer: name of file to open in notepad during IDLE (launch notepad)

BOOL UseCustomPanelFont = FALSE;
HFONT Font = NULL;
HFONT FontUL = NULL;
LOGFONT LogFont;
int FontCharHeight = 0;

HFONT EnvFont = NULL;
HFONT EnvFontUL = NULL;
//LOGFONT EnvLogFont;
int EnvFontCharHeight = 0;
HFONT TooltipFont = NULL;

HBRUSH HNormalBkBrush = NULL;
HBRUSH HFocusedBkBrush = NULL;
HBRUSH HSelectedBkBrush = NULL;
HBRUSH HFocSelBkBrush = NULL;
HBRUSH HDialogBrush = NULL;
HBRUSH HButtonTextBrush = NULL;
HBRUSH HDitherBrush = NULL;
HBRUSH HActiveCaptionBrush = NULL;
HBRUSH HInactiveCaptionBrush = NULL;

HBRUSH HMenuSelectedBkBrush = NULL;
HBRUSH HMenuSelectedTextBrush = NULL;
HBRUSH HMenuHilightBrush = NULL;
HBRUSH HMenuGrayTextBrush = NULL;

HPEN HActiveNormalPen = NULL; // pens for frame around item
HPEN HActiveSelectedPen = NULL;
HPEN HInactiveNormalPen = NULL;
HPEN HInactiveSelectedPen = NULL;

HPEN HThumbnailNormalPen = NULL; // pens for frame around thumbnail
HPEN HThumbnailFucsedPen = NULL;
HPEN HThumbnailSelectedPen = NULL;
HPEN HThumbnailFocSelPen = NULL;

HPEN BtnShadowPen = NULL;
HPEN BtnHilightPen = NULL;
HPEN Btn3DLightPen = NULL;
HPEN BtnFacePen = NULL;
HPEN WndFramePen = NULL;
HPEN WndPen = NULL;
HBITMAP HFilter = NULL;
HBITMAP HHeaderSort = NULL;

HIMAGELIST HFindSymbolsImageList = NULL;
HIMAGELIST HMenuMarkImageList = NULL;
// Menu commands retain compact rows independently of the configurable toolbar size.
HIMAGELIST HGrayMenuImageList = NULL;
HIMAGELIST HHotMenuImageList = NULL;
HIMAGELIST HGrayToolBarImageList = NULL;
HIMAGELIST HHotToolBarImageList = NULL;
HIMAGELIST HBottomTBImageList = NULL;
HIMAGELIST HHotBottomTBImageList = NULL;

CBitmap ItemBitmap;

HBITMAP HUpDownBitmap = NULL;
HBITMAP HZoomBitmap = NULL;

//HBITMAP HWorkerBitmap = NULL;

HCURSOR HHelpCursor = NULL;

int SystemDPI = 96; // System DPI (96 = 100% scale). Per-monitor DPI is supported via GetDpiForWindow().
// Salamander uses per-monitor DPI awareness on Windows 8.1+, but SystemDPI is maintained for compatibility.
int IconSizes[] = {16, 32, 48};
int IconLRFlags = 0;
HICON HSharedOverlays[] = {0};
HICON HShortcutOverlays[] = {0};
HICON HSlowFileOverlays[] = {0};
CIconList* SimpleIconLists[] = {0};
CIconList* ThrobberFrames = NULL;
CIconList* LockFrames = NULL; // for simplicity declare and load as throbber

HICON HGroupIcon = NULL;
HICON HFavoritIcon = NULL;
HICON HSlowFileIcon = NULL;

RGBQUAD ColorTable[256] = {0};

DWORD MouseHoverTime = 0;

SYSTEMTIME SalamanderStartSystemTime = {0}; // Salamander start time (GetSystemTime)

BOOL WaitForESCReleaseBeforeTestingESC = FALSE; // should we wait for ESC release before starting to browse path in panel?

int SPACE_WIDTH = 10;

const char* LOW_MEMORY = "Low memory.";

BOOL DragFullWindows = TRUE;

CWindowQueue ViewerWindowQueue("Internal Viewers");

CFindSetDialog GlobalFindDialog(NULL /* ignored */, 0 /* ignored */, 0 /* ignored */);

CNames GlobalSelection;
CDirectorySizeCache DirectorySizesHolder;

HWND PluginProgressDialog = NULL;
HWND PluginMsgBoxParent = NULL;

BOOL CriticalShutdown = FALSE;

HANDLE SalOpenFileMapping = NULL;
void* SalOpenSharedMem = NULL;

// mutex pro synchronizaci load/save do Registry (dva procesy najednou nemuzou, ma to neblahe vysledky)
CLoadSaveToRegistryMutex LoadSaveToRegistryMutex;

BOOL IsNotAlphaNorNum[256]; // array of TRUE/FALSE for characters (TRUE = not a letter or digit)
BOOL IsAlpha[256];          // array of TRUE/FALSE for characters (TRUE = letter)

// default user's charset for fonts; on W2K+ DEFAULT_CHARSET would be enough
//
// On WinXP, regional settings can choose Czech as the default, for example,
// without installing Czech fonts on the Advanced tab. Then, when constructing
// a font with UserCharset encoding, the operating system returns a font with a
// completely different face name, mainly to get the required encoding. Therefore
// it is IMPORTANT to choose the lfPitchAndFamily variable correctly when specifying
// font parameters, where FF_SWISS and FF_ROMAN fonts (sans-serif/serif) can be selected.
int UserCharset = DEFAULT_CHARSET;

DWORD AllocationGranularity = 1; // allocation granularity (needed for using memory-mapped files)

#ifdef USE_BETA_EXPIRATION_DATE

// urcuje prvni den, kdy uz tato beta/PB verze nepobezi
// beta/PB version 4.0 beta 1 will run only until February 1, 2020
//                                 YEAR  MONTH DAY
SYSTEMTIME BETA_EXPIRATION_DATE = {2020, 2, 0, 1, 0, 0, 0, 0};
#endif // USE_BETA_EXPIRATION_DATE

//******************************************************************************
//
// Rizeni Idle processingu (CMainWindow::OnEnterIdle)
//

BOOL IdleRefreshStates = TRUE;  // na uvod nechame nastavit promenne
BOOL IdleForceRefresh = FALSE;  // vyradi cache Enabler*
BOOL IdleCheckClipboard = TRUE; // koukneme taky na clipboard

DWORD EnablerUpDir = FALSE;
DWORD EnablerRootDir = FALSE;
DWORD EnablerForward = FALSE;
DWORD EnablerBackward = FALSE;
DWORD EnablerFileOnDisk = FALSE;
DWORD EnablerLeftFileOnDisk = FALSE;
DWORD EnablerRightFileOnDisk = FALSE;
DWORD EnablerFileOnDiskOrArchive = FALSE;
DWORD EnablerFileOrDirLinkOnDisk = FALSE;
DWORD EnablerFiles = FALSE;
DWORD EnablerFilesOnDisk = FALSE;
DWORD EnablerFilesOnDiskCompress = FALSE;
DWORD EnablerFilesOnDiskEncrypt = FALSE;
DWORD EnablerFilesOnDiskOrArchive = FALSE;
DWORD EnablerOccupiedSpace = FALSE;
DWORD EnablerFilesCopy = FALSE;
DWORD EnablerFilesMove = FALSE;
DWORD EnablerFilesDelete = FALSE;
DWORD EnablerFileDir = FALSE;
DWORD EnablerFileDirANDSelected = FALSE;
DWORD EnablerQuickRename = FALSE;
DWORD EnablerOnDisk = FALSE;
DWORD EnablerCalcDirSizes = FALSE;
DWORD EnablerPasteFiles = FALSE;
DWORD EnablerPastePath = FALSE;
DWORD EnablerPasteLinks = FALSE;
DWORD EnablerPasteSimpleFiles = FALSE;
DWORD EnablerPasteDefEffect = FALSE;
DWORD EnablerPasteFilesToArcOrFS = FALSE;
DWORD EnablerPaste = FALSE;
DWORD EnablerPasteLinksOnDisk = FALSE;
DWORD EnablerSelected = FALSE;
DWORD EnablerUnselected = FALSE;
DWORD EnablerHiddenNames = FALSE;
DWORD EnablerSelectionStored = FALSE;
DWORD EnablerGlobalSelStored = FALSE;
DWORD EnablerSelGotoPrev = FALSE;
DWORD EnablerSelGotoNext = FALSE;
DWORD EnablerLeftUpDir = FALSE;
DWORD EnablerRightUpDir = FALSE;
DWORD EnablerLeftRootDir = FALSE;
DWORD EnablerRightRootDir = FALSE;
DWORD EnablerLeftForward = FALSE;
DWORD EnablerRightForward = FALSE;
DWORD EnablerLeftBackward = FALSE;
DWORD EnablerRightBackward = FALSE;
DWORD EnablerFileHistory = FALSE;
DWORD EnablerDirHistory = FALSE;
DWORD EnablerCustomizeLeftView = FALSE;
DWORD EnablerCustomizeRightView = FALSE;
DWORD EnablerDriveInfo = FALSE;
DWORD EnablerCreateDir = FALSE;
DWORD EnablerViewFile = FALSE;
DWORD EnablerChangeAttrs = FALSE;
DWORD EnablerShowProperties = FALSE;
DWORD EnablerItemsContextMenu = FALSE;
DWORD EnablerOpenActiveFolder = FALSE;
DWORD EnablerPermissions = FALSE;

COLORREF* CurrentColors = SalamanderColors;

COLORREF UserColors[NUMBER_OF_COLORS];

SALCOLOR ViewerColors[NUMBER_OF_VIEWERCOLORS] =
    {
        RGBF(0, 0, 0, SCF_DEFAULT),       // VIEWER_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // VIEWER_BK_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // VIEWER_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT),       // VIEWER_BK_SELECTED
};

COLORREF SalamanderColors[NUMBER_OF_COLORS] =
    {
        // barvy pera pro ramecek kolem polozky
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_NORMAL
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_SELECTED
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_NORMAL
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_SELECTED

        // barvy textu polozek v panelu
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_NORMAL
        RGBF(255, 0, 0, 0),         // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 0, 0, 0),         // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // barvy pozadi polozek v panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_SELECTED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCUSED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT),       // ITEM_BK_HIGHLIGHT

        // barvy pro blend ikonek
        RGBF(255, 128, 128, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),           // ICON_BLEND_FOCUSED
        RGBF(255, 0, 0, 0),               // ICON_BLEND_FOCSEL

        // barvy progress bary
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // barvy hot polozek
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // barvy titulku panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // barvy pera pro ramecek kolem thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 0, 0, 0),       // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 0, 0, 0),     // THUMBNAIL_FRAME_SELECTED
        RGBF(128, 0, 0, 0),     // THUMBNAIL_FRAME_FOCSEL
};

COLORREF ExplorerColors[NUMBER_OF_COLORS] =
    {
        // barvy pera pro ramecek kolem polozky
        RGBF(0, 0, 0, SCF_DEFAULT),       // FOCUS_ACTIVE_NORMAL
        RGBF(255, 255, 0, 0),             // FOCUS_ACTIVE_SELECTED
        RGBF(128, 128, 128, 0),           // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),               // FOCUS_FG_INACTIVE_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // FOCUS_BK_INACTIVE_NORMAL
        RGBF(255, 255, 0, 0),             // FOCUS_BK_INACTIVE_SELECTED

        // barvy textu polozek v panelu
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_NORMAL
        RGBF(255, 255, 255, 0),     // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 255, 0),     // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // barvy pozadi polozek v panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ITEM_BK_NORMAL
        RGBF(0, 0, 128, 0),               // ITEM_BK_SELECTED
        RGBF(232, 232, 232, 0),           // ITEM_BK_FOCUSED
        RGBF(0, 0, 128, 0),               // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT),       // ITEM_BK_HIGHLIGHT

        // barvy pro blend ikonek
        RGBF(0, 0, 128, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),       // ICON_BLEND_FOCUSED
        RGBF(0, 0, 128, 0),           // ICON_BLEND_FOCSEL

        // barvy progress bary
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // barvy hot polozek
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // barvy titulku panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // barvy pera pro ramecek kolem thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_FOCUSED
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_SELECTED
        RGBF(0, 0, 128, 0),     // THUMBNAIL_FRAME_FOCSEL
};

COLORREF NortonColors[NUMBER_OF_COLORS] =
    {
        // barvy pera pro ramecek kolem polozky
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_SELECTED
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_SELECTED
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_SELECTED

        // barvy textu polozek v panelu
        RGBF(0, 255, 255, 0),       // ITEM_FG_NORMAL
        RGBF(255, 255, 0, 0),       // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 0, 0),       // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // barvy pozadi polozek v panelu
        RGBF(0, 0, 128, 0),         // ITEM_BK_NORMAL
        RGBF(0, 0, 128, 0),         // ITEM_BK_SELECTED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCUSED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_BK_HIGHLIGHT

        // barvy pro blend ikonek
        RGBF(255, 255, 0, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),         // ICON_BLEND_FOCUSED
        RGBF(255, 255, 0, 0),           // ICON_BLEND_FOCSEL

        // barvy progress bary
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // barvy hot polozek
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(128, 128, 128, SCF_DEFAULT), // HOT_INACTIVE

        // barvy titulku panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // barvy pera pro ramecek kolem thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 128, 128, 0),   // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_SELECTED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_FOCSEL
};

COLORREF NavigatorColors[NUMBER_OF_COLORS] =
    {
        // barvy pera pro ramecek kolem polozky
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_ACTIVE_SELECTED
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_NORMAL
        RGBF(0, 128, 128, 0), // FOCUS_FG_INACTIVE_SELECTED
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_NORMAL
        RGBF(0, 0, 128, 0),   // FOCUS_BK_INACTIVE_SELECTED

        // barvy textu polozek v panelu
        RGBF(255, 255, 255, 0),     // ITEM_FG_NORMAL
        RGBF(255, 255, 0, 0),       // ITEM_FG_SELECTED
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_FOCUSED
        RGBF(255, 255, 0, 0),       // ITEM_FG_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_FG_HIGHLIGHT

        // barvy pozadi polozek v panelu
        RGBF(80, 80, 80, 0),        // ITEM_BK_NORMAL
        RGBF(80, 80, 80, 0),        // ITEM_BK_SELECTED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCUSED
        RGBF(0, 128, 128, 0),       // ITEM_BK_FOCSEL
        RGBF(0, 0, 0, SCF_DEFAULT), // ITEM_BK_HIGHLIGHT

        // barvy pro blend ikonek
        RGBF(255, 255, 0, SCF_DEFAULT), // ICON_BLEND_SELECTED
        RGBF(128, 128, 128, 0),         // ICON_BLEND_FOCUSED
        RGBF(255, 255, 0, 0),           // ICON_BLEND_FOCSEL

        // barvy progress bary
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_FG_NORMAL
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_FG_SELECTED
        RGBF(255, 255, 255, SCF_DEFAULT), // PROGRESS_BK_NORMAL
        RGBF(0, 0, 192, SCF_DEFAULT),     // PROGRESS_BK_SELECTED

        // barvy hot polozek
        RGBF(0, 0, 255, SCF_DEFAULT),     // HOT_PANEL
        RGBF(173, 182, 205, SCF_DEFAULT), // HOT_ACTIVE
        RGBF(212, 212, 212, SCF_DEFAULT), // HOT_INACTIVE

        // barvy titulku panelu
        RGBF(255, 255, 255, SCF_DEFAULT), // ACTIVE_CAPTION_FG
        RGBF(0, 0, 128, SCF_DEFAULT),     // ACTIVE_CAPTION_BK
        RGBF(255, 255, 255, SCF_DEFAULT), // INACTIVE_CAPTION_FG
        RGBF(128, 128, 128, SCF_DEFAULT), // INACTIVE_CAPTION_BK

        // barvy pera pro ramecek kolem thumbnails
        RGBF(192, 192, 192, 0), // THUMBNAIL_FRAME_NORMAL
        RGBF(0, 128, 128, 0),   // THUMBNAIL_FRAME_FOCUSED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_SELECTED
        RGBF(255, 255, 0, 0),   // THUMBNAIL_FRAME_FOCSEL
};

COLORREF CustomColors[NUMBER_OF_CUSTOMCOLORS] =
    {
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
        RGB(255, 255, 255),
};

//*****************************************************************************
// CRC32 tables are defined in app_entry.cpp alongside UpdateCrc32()
