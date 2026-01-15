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
#pragma pack(push, enter_include_spl_gen) // to make structures independent of set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CFileData;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CSalamanderGeneralAbstract
//
// generally usable Salamander methods (for all plugin types)

// message box types
#define MSGBOX_INFO 0
#define MSGBOX_ERROR 1
#define MSGBOX_EX_ERROR 2
#define MSGBOX_QUESTION 3
#define MSGBOX_EX_QUESTION 4
#define MSGBOX_WARNING 5
#define MSGBOX_EX_WARNING 6

// constants for CSalamanderGeneralAbstract::SalMessageBoxEx
#define MSGBOXEX_OK 0x00000000                // MB_OK
#define MSGBOXEX_OKCANCEL 0x00000001          // MB_OKCANCEL
#define MSGBOXEX_ABORTRETRYIGNORE 0x00000002  // MB_ABORTRETRYIGNORE
#define MSGBOXEX_YESNOCANCEL 0x00000003       // MB_YESNOCANCEL
#define MSGBOXEX_YESNO 0x00000004             // MB_YESNO
#define MSGBOXEX_RETRYCANCEL 0x00000005       // MB_RETRYCANCEL
#define MSGBOXEX_CANCELTRYCONTINUE 0x00000006 // MB_CANCELTRYCONTINUE
#define MSGBOXEX_CONTINUEABORT 0x00000007     // MB_CONTINUEABORT
#define MSGBOXEX_YESNOOKCANCEL 0x00000008

#define MSGBOXEX_ICONHAND 0x00000010        // MB_ICONHAND / MB_ICONSTOP / MB_ICONERROR
#define MSGBOXEX_ICONQUESTION 0x00000020    // MB_ICONQUESTION
#define MSGBOXEX_ICONEXCLAMATION 0x00000030 // MB_ICONEXCLAMATION / MB_ICONWARNING
#define MSGBOXEX_ICONINFORMATION 0x00000040 // MB_ICONASTERISK / MB_ICONINFORMATION

#define MSGBOXEX_DEFBUTTON1 0x00000000 // MB_DEFBUTTON1
#define MSGBOXEX_DEFBUTTON2 0x00000100 // MB_DEFBUTTON2
#define MSGBOXEX_DEFBUTTON3 0x00000200 // MB_DEFBUTTON3
#define MSGBOXEX_DEFBUTTON4 0x00000300 // MB_DEFBUTTON4

#define MSGBOXEX_HELP 0x00004000 // MB_HELP (bit mask)

#define MSGBOXEX_SETFOREGROUND 0x00010000 // MB_SETFOREGROUND (bit mask)

// altap specific
#define MSGBOXEX_SILENT 0x10000000 // messagebox will not emit any sound when opened (bit mask)
// in case of MB_YESNO messagebox allows Escape (generates IDNO); in MB_ABORTRETRYIGNORE messagebox
// allows Escape (generates IDCANCEL) (bit mask)
#define MSGBOXEX_ESCAPEENABLED 0x20000000
#define MSGBOXEX_HINT 0x40000000 // if CheckBoxText is used, separator \t will be searched and displayed as hint
// Vista: default button will have "requires elevation" state (elevated icon will be displayed)
#define MSGBOXEX_SHIELDONDEFBTN 0x80000000

#define MSGBOXEX_TYPEMASK 0x0000000F // MB_TYPEMASK
#define MSGBOXEX_ICONMASK 0x000000F0 // MB_ICONMASK
#define MSGBOXEX_DEFMASK 0x00000F00  // MB_DEFMASK
#define MSGBOXEX_MODEMASK 0x00003000 // MB_MODEMASK
#define MSGBOXEX_MISCMASK 0x0000C000 // MB_MISCMASK
#define MSGBOXEX_EXMASK 0xF0000000

// message box return values
#define DIALOG_FAIL 0x00000000 // dialog could not be opened
// individual buttons
#define DIALOG_OK 0x00000001       // IDOK
#define DIALOG_CANCEL 0x00000002   // IDCANCEL
#define DIALOG_ABORT 0x00000003    // IDABORT
#define DIALOG_RETRY 0x00000004    // IDRETRY
#define DIALOG_IGNORE 0x00000005   // IDIGNORE
#define DIALOG_YES 0x00000006      // IDYES
#define DIALOG_NO 0x00000007       // IDNO
#define DIALOG_TRYAGAIN 0x0000000a // IDTRYAGAIN
#define DIALOG_CONTINUE 0x0000000b // IDCONTINUE
// altap specific
#define DIALOG_SKIP 0x10000000
#define DIALOG_SKIPALL 0x20000000
#define DIALOG_ALL 0x30000000

typedef void(CALLBACK* MSGBOXEX_CALLBACK)(LPHELPINFO helpInfo);

struct MSGBOXEX_PARAMS
{
    HWND HParent;
    const char* Text;
    const char* Caption;
    DWORD Flags;
    HICON HIcon;
    DWORD ContextHelpId;
    MSGBOXEX_CALLBACK HelpCallback;
    const char* CheckBoxText;
    BOOL* CheckBoxValue;
    const char* AliasBtnNames;
    const char* URL;
    const char* URLText;
};

/*
HParent
  Handle to the owner window. Message box is centered to this window.
  If this parameter is NULL, the message box has no owner window.

Text
  Pointer to a null-terminated string that contains the message to be displayed.

Caption
  Pointer to a null-terminated string that contains the message box title.
  If this member is NULL, the default title "Error" is used.

Flags
  Specifies the contents and behavior of the message box.
  This parameter can be a combination of flags from the following groups of flags.

   To indicate the buttons displayed in the message box, specify one of the following values.
    MSGBOXEX_OK                   (MB_OK)
      The message box contains one push button: OK. This is the default.
      Message box can be closed using Escape and return value will be DIALOG_OK (IDOK).
    MSGBOXEX_OKCANCEL             (MB_OKCANCEL)
      The message box contains two push buttons: OK and Cancel.
    MSGBOXEX_ABORTRETRYIGNORE     (MB_ABORTRETRYIGNORE)
      The message box contains three push buttons: Abort, Retry, and Ignore.
      Message box can be closed using Escape when MSGBOXEX_ESCAPEENABLED flag is specified.
      In that case return value will be DIALOG_CANCEL (IDCANCEL).
    MSGBOXEX_YESNOCANCEL          (MB_YESNOCANCEL)
      The message box contains three push buttons: Yes, No, and Cancel.
    MSGBOXEX_YESNO                (MB_YESNO)
      The message box contains two push buttons: Yes and No.
      Message box can be closed using Escape when MSGBOXEX_ESCAPEENABLED flag is specified.
      In that case return value will be DIALOG_NO (IDNO).
    MSGBOXEX_RETRYCANCEL          (MB_RETRYCANCEL)
      The message box contains two push buttons: Retry and Cancel.
    MSGBOXEX_CANCELTRYCONTINUE    (MB_CANCELTRYCONTINUE)
      The message box contains three push buttons: Cancel, Try Again, Continue.

   To display an icon in the message box, specify one of the following values.
    MSGBOXEX_ICONHAND             (MB_ICONHAND / MB_ICONSTOP / MB_ICONERROR)
      A stop-sign icon appears in the message box.
    MSGBOXEX_ICONQUESTION         (MB_ICONQUESTION)
      A question-mark icon appears in the message box.
    MSGBOXEX_ICONEXCLAMATION      (MB_ICONEXCLAMATION / MB_ICONWARNING)
      An exclamation-point icon appears in the message box.
    MSGBOXEX_ICONINFORMATION      (MB_ICONASTERISK / MB_ICONINFORMATION)
      An icon consisting of a lowercase letter i in a circle appears in the message box.

   To indicate the default button, specify one of the following values.
    MSGBOXEX_DEFBUTTON1           (MB_DEFBUTTON1)
      The first button is the default button.
      MSGBOXEX_DEFBUTTON1 is the default unless MSGBOXEX_DEFBUTTON2, MSGBOXEX_DEFBUTTON3,
      or MSGBOXEX_DEFBUTTON4 is specified.
    MSGBOXEX_DEFBUTTON2           (MB_DEFBUTTON2)
      The second button is the default button.
    MSGBOXEX_DEFBUTTON3           (MB_DEFBUTTON3)
      The third button is the default button.
    MSGBOXEX_DEFBUTTON4           (MB_DEFBUTTON4)
      The fourth button is the default button.

   To specify other options, use one or more of the following values.
    MSGBOXEX_HELP                 (MB_HELP)
      Adds a Help button to the message box.
      When the user clicks the Help button or presses F1, the system sends a WM_HELP message to the owner
      or calls HelpCallback (see HelpCallback for details).
    MSGBOXEX_SETFOREGROUND        (MB_SETFOREGROUND)
      The message box becomes the foreground window. Internally, the system calls the SetForegroundWindow
      function for the message box.
    MSGBOXEX_SILENT
      No sound will be played when message box is displayed.
    MSGBOXEX_ESCAPEENABLED
      When MSGBOXEX_YESNO is specified, user can close message box using Escape key and DIALOG_NO (IDNO)
      will be returned. When MSGBOXEX_ABORTRETRYIGNORE is specified, user can close message box using
      Escape key and DIALOG_CANCEL (IDCANCEL) will be returned. Otherwise this option is ignored.

HIcon
  Handle to the icon to be drawn in the message box. Icon will not be destroyed when messagebox is closed.
  If this parameter is NULL, MSGBOXEX_ICONxxx style will be used.

ContextHelpId
  Identifies a help context. If a help event occurs, this value is specified in
  the HELPINFO structure that the message box sends to the owner window or callback function.

HelpCallback
  Pointer to the callback function that processes help events for the message box.
  The callback function has the following form:
    VOID CALLBACK MSGBOXEX_CALLBACK(LPHELPINFO helpInfo)
  If this member is NULL, the message box sends WM_HELP messages to the owner window
  when help events occur.

CheckBoxText
  Pointer to a null-terminated string that contains the checkbox text.
  If the MSGBOXEX_HINT flag is specified in the Flags, this text must contain HINT.
  Hint is separated from string by the TAB character. Hint is divided by the second TAB character
  on two parts. The first part is label, that will be displayed behind the check box.
  The second part is the text displayed when user clicks the hint label.

  Example: "This is text for checkbox\tHint Label\tThis text will be displayed when user click the Hint Label."
  If this member is NULL, checkbox will not be displayed.

CheckBoxValue
  Pointer to a BOOL variable contains the checkbox initial and return state (TRUE: checked, FALSE: unchecked).
  This parameter is ignored if CheckBoxText parameter is NULL. Otherwise this parameter must be set.

AliasBtnNames
  Pointer to a buffer containing pairs of id and alias strings. The last string in the
  buffer must be terminated by NULL character.

  The first string in each pair is a decimal number that specifies button ID.
  Number must be one of the DIALOG_xxx values. The second string specifies alias text
  for this button.

  First and second string in each pair are separated by TAB character.
  Pairs are separated by TAB character too.

  If this member is NULL, normal names of buttons will displayed.

  Example: sprintf(buffer, "%d\t%s\t%d\t%s", DIALOG_OK, "&Start", DIALOG_CANCEL, "E&xit");
           buffer: "1\t&Start\t2\tE&xit"

URL
  Pointer to a null-terminated string that contains the URL displayed below text.
  If this member is NULL, the URL is not displayed.

URLText
  Pointer to a null-terminated string that contains the URL text displayed below text.
  If this member is NULL, the URL is displayed instead.

*/

// panel identification
#define PANEL_SOURCE 1 // source panel (active panel)
#define PANEL_TARGET 2 // target panel (inactive panel)
#define PANEL_LEFT 3   // left panel
#define PANEL_RIGHT 4  // right panel

// path types
#define PATH_TYPE_WINDOWS 1 // Windows path ("c:\path" or UNC path)
#define PATH_TYPE_ARCHIVE 2 // path into archive (archive is located on Windows path)
#define PATH_TYPE_FS 3      // path on plugin file-system

// Only one flag can be selected from the following group.
// Defines the set of displayed buttons in various error messages.
#define BUTTONS_OK 0x00000000               // OK
#define BUTTONS_RETRYCANCEL 0x00000001      // Retry / Cancel
#define BUTTONS_SKIPCANCEL 0x00000002       // Skip / Skip all / Cancel
#define BUTTONS_RETRYSKIPCANCEL 0x00000003  // Retry / Skip / Skip all / Cancel
#define BUTTONS_YESALLSKIPCANCEL 0x00000004 // Yes / All / Skip / Skip all / Cancel
#define BUTTONS_YESNOCANCEL 0x00000005      // Yes / No / Cancel
#define BUTTONS_YESALLCANCEL 0x00000006     // Yes / All / Cancel
#define BUTTONS_MASK 0x000000FF             // internal mask, do not use
// detection whether combination has SKIP or YES button I keep here in inline form, so that
// in case of introducing new combinations it is clearly visible and we don't forget to update it
inline BOOL ButtonsContainsSkip(DWORD btn)
{
    return (btn & BUTTONS_MASK) == BUTTONS_SKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_RETRYSKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESALLSKIPCANCEL;
}
inline BOOL ButtonsContainsYes(DWORD btn)
{
    return (btn & BUTTONS_MASK) == BUTTONS_YESALLSKIPCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESNOCANCEL ||
           (btn & BUTTONS_MASK) == BUTTONS_YESALLCANCEL;
}

// error constants for CSalamanderGeneralAbstract::SalGetFullName
#define GFN_SERVERNAMEMISSING 1   // server name is missing in UNC path
#define GFN_SHARENAMEMISSING 2    // share name is missing in UNC path
#define GFN_TOOLONGPATH 3         // operation would result in too long path
#define GFN_INVALIDDRIVE 4        // for normal path (c:\) the letter is not A-Z (nor a-z)
#define GFN_INCOMLETEFILENAME 5   // relative path without specified 'curDir' -> unsolvable
#define GFN_EMPTYNAMENOTALLOWED 6 // empty string 'name'
#define GFN_PATHISINVALID 7       // cannot eliminate "..", e.g. "c:\.."

// error code for state when user interrupts CSalamanderGeneralAbstract::SalCheckPath with ESC key
#define ERROR_USER_TERMINATED -100

#define PATH_MAX_PATH 248 // limit for max. path length (full directory name), warning: the null-terminator is already included in the limit (max. string length is 247 characters)

// error constants for CSalamanderGeneralAbstract::SalParsePath:
// input was empty path and 'curPath' was NULL (empty path is replaced with current path,
// but that is not known here)
#define SPP_EMPTYPATHNOTALLOWED 1
// Windows path (normal + UNC) does not exist, is not accessible or user interrupted test
// for path accessibility (includes attempt to restore network connection)
#define SPP_WINDOWSPATHERROR 2
// Windows path starts with file name, but it is not an archive (otherwise it would be path into archive)
#define SPP_NOTARCHIVEFILE 3
// FS path - plugin FS name (fs-name - before ':' in path) is not known (no plugin
// has registered this name)
#define SPP_NOTPLUGINFS 4
// it is relative path, but current path is not known or it is FS (there it is not possible to recognize root
// and we don't know the structure of fs-user-part of path at all, so conversion to absolute path cannot be performed)
// if current path is FS ('curPathIsDiskOrArchive' is FALSE), error will not be reported to user in this case
// (further processing on the FS side that called SalParsePath method is expected)
#define SPP_INCOMLETEPATH 5

// Salamander internal color constants
#define SALCOL_FOCUS_ACTIVE_NORMAL 0 // pen colors for frame around item
#define SALCOL_FOCUS_ACTIVE_SELECTED 1
#define SALCOL_FOCUS_FG_INACTIVE_NORMAL 2
#define SALCOL_FOCUS_FG_INACTIVE_SELECTED 3
#define SALCOL_FOCUS_BK_INACTIVE_NORMAL 4
#define SALCOL_FOCUS_BK_INACTIVE_SELECTED 5
#define SALCOL_ITEM_FG_NORMAL 6 // text colors of items in panel
#define SALCOL_ITEM_FG_SELECTED 7
#define SALCOL_ITEM_FG_FOCUSED 8
#define SALCOL_ITEM_FG_FOCSEL 9
#define SALCOL_ITEM_FG_HIGHLIGHT 10
#define SALCOL_ITEM_BK_NORMAL 11 // background colors of items in panel
#define SALCOL_ITEM_BK_SELECTED 12
#define SALCOL_ITEM_BK_FOCUSED 13
#define SALCOL_ITEM_BK_FOCSEL 14
#define SALCOL_ITEM_BK_HIGHLIGHT 15
#define SALCOL_ICON_BLEND_SELECTED 16 // colors for icon blending
#define SALCOL_ICON_BLEND_FOCUSED 17
#define SALCOL_ICON_BLEND_FOCSEL 18
#define SALCOL_PROGRESS_FG_NORMAL 19 // progress bar colors
#define SALCOL_PROGRESS_FG_SELECTED 20
#define SALCOL_PROGRESS_BK_NORMAL 21
#define SALCOL_PROGRESS_BK_SELECTED 22
#define SALCOL_HOT_PANEL 23           // hot item color in panel
#define SALCOL_HOT_ACTIVE 24          //                   in active window caption
#define SALCOL_HOT_INACTIVE 25        //                   in inactive caption, statusbar,...
#define SALCOL_ACTIVE_CAPTION_FG 26   // text color in active panel title
#define SALCOL_ACTIVE_CAPTION_BK 27   // background color in active panel title
#define SALCOL_INACTIVE_CAPTION_FG 28 // text color in inactive panel title
#define SALCOL_INACTIVE_CAPTION_BK 29 // background color in inactive panel title
#define SALCOL_VIEWER_FG_NORMAL 30    // text color in internal text/hex viewer
#define SALCOL_VIEWER_BK_NORMAL 31    // background color in internal text/hex viewer
#define SALCOL_VIEWER_FG_SELECTED 32  // selected text color in internal text/hex viewer
#define SALCOL_VIEWER_BK_SELECTED 33  // selected background color in internal text/hex viewer
#define SALCOL_THUMBNAIL_NORMAL 34    // pen colors for frame around thumbnail
#define SALCOL_THUMBNAIL_SELECTED 35
#define SALCOL_THUMBNAIL_FOCUSED 36
#define SALCOL_THUMBNAIL_FOCSEL 37

// constants for reasons why CSalamanderGeneralAbstract::ChangePanelPathToXXX methods returned failure:
#define CHPPFR_SUCCESS 0 // new path is in panel, success (return value is TRUE)
// new path (or archive name) cannot be converted from relative to absolute or
// new path (or archive name) is not accessible or
// path on FS cannot be opened (plugin not available, refuses its load, refuses FS opening, fatal ChangePath error)
#define CHPPFR_INVALIDPATH 1
#define CHPPFR_INVALIDARCHIVE 2  // file is not archive or cannot be listed as archive
#define CHPPFR_CANNOTCLOSEPATH 4 // current path cannot be closed
// shortened new path is in panel,
// clarification for FS: panel contains either shortened new path or original path or shortened
// original path - original path is attempted to be restored to panel only if new path was opened
// in current FS (IsOurPath method returned TRUE for it) and if new path is not accessible
// (nor any of its subpaths)
#define CHPPFR_SHORTERPATH 5
// shortened new path is in panel; reason for shortening was that requested path was file
// name - panel contains path to file and file will be focused
#define CHPPFR_FILENAMEFOCUSED 6

// types for CSalamanderGeneralAbstract::ValidateVarString() and CSalamanderGeneralAbstract::ExpandVarString()
typedef const char*(WINAPI* FSalamanderVarStrGetValue)(HWND msgParent, void* param);
struct CSalamanderVarStrEntry
{
    const char* Name;                  // variable name in string (e.g. for string "$(name)" it is "name")
    FSalamanderVarStrGetValue Execute; // function that returns text representing the variable
};

class CSalamanderRegistryAbstract;

// callback type used when loading/saving configuration using
// CSalamanderGeneral::CallLoadOrSaveConfiguration; 'regKey' is NULL if loading
// default configuration (save is not called when 'regKey' == NULL); 'registry' is object for
// working with registry; 'param' is user parameter of function (see
// CSalamanderGeneral::CallLoadOrSaveConfiguration)
typedef void(WINAPI* FSalLoadOrSaveConfiguration)(BOOL load, HKEY regKey,
                                                  CSalamanderRegistryAbstract* registry, void* param);

// base structure for CSalamanderGeneralAbstract::ViewFileInPluginViewer (each plugin
// viewer can have this structure extended with its own parameters - structure is passed to
// CPluginInterfaceForViewerAbstract::ViewFile - parameters can be e.g. window title,
// viewer mode, offset from file beginning, selection position, etc.); WARNING !!! about structure
// packing (required is 4 bytes - see "#pragma pack(4)")
struct CSalamanderPluginViewerData
{
    // how many bytes from structure beginning are valid (for distinguishing structure versions)
    int Size;
    // name of file to be opened in viewer (do not use in method
    // CPluginInterfaceForViewerAbstract::ViewFile - file name is given by parameter 'name')
    const char* FileName;
};

// extension of CSalamanderPluginViewerData structure for internal text/hex viewer
struct CSalamanderPluginInternalViewerData : public CSalamanderPluginViewerData
{
    int Mode;            // 0 - text mode, 1 - hex mode
    const char* Caption; // NULL -> window caption contains FileName, otherwise Caption
    BOOL WholeCaption;   // has meaning if Caption != NULL. TRUE -> in viewer title
                         // only Caption string will be displayed; FALSE -> after
                         // Caption the standard " - Viewer" will be appended.
};

// constants for Salamander configuration parameter types (see CSalamanderGeneralAbstract::GetConfigParameter)
#define SALCFGTYPE_NOTFOUND 0 // parameter not found
#define SALCFGTYPE_BOOL 1     // TRUE/FALSE
#define SALCFGTYPE_INT 2      // 32-bit integer
#define SALCFGTYPE_STRING 3   // null-terminated multibyte string
#define SALCFGTYPE_LOGFONT 4  // Win32 LOGFONT structure

// Salamander configuration parameter constants (see CSalamanderGeneralAbstract::GetConfigParameter);
// comment shows parameter type (BOOL, INT, STRING), for STRING required
// buffer size for string is in parentheses
//
// general parameters
#define SALCFG_SELOPINCLUDEDIRS 1        // BOOL, select/deselect operations (num *, num +, num -) work also with directories
#define SALCFG_SAVEONEXIT 2              // BOOL, save configuration on Salamander exit
#define SALCFG_MINBEEPWHENDONE 3         // BOOL, should it beep (play sound) after end of work in inactive window?
#define SALCFG_HIDEHIDDENORSYSTEMFILES 4 // BOOL, should it hide system and/or hidden files?
#define SALCFG_ALWAYSONTOP 6             // BOOL, main window is Always On Top?
#define SALCFG_SORTUSESLOCALE 7          // BOOL, should it use regional settings when sorting?
#define SALCFG_SINGLECLICK 8             // BOOL, single click mode (single click to open file, etc.)
#define SALCFG_TOPTOOLBARVISIBLE 9       // BOOL, is top toolbar visible?
#define SALCFG_BOTTOMTOOLBARVISIBLE 10   // BOOL, is bottom toolbar visible?
#define SALCFG_USERMENUTOOLBARVISIBLE 11 // BOOL, is user-menu toolbar visible?
#define SALCFG_INFOLINECONTENT 12        // STRING (200), content of Information Line (string with parameters)
#define SALCFG_FILENAMEFORMAT 13         // INT, how to alter file name before displaying (parameter 'format' to CSalamanderGeneralAbstract::AlterFileName)
#define SALCFG_SAVEHISTORY 14            // BOOL, may history related data be stored to configuration?
#define SALCFG_ENABLECMDLINEHISTORY 15   // BOOL, is command line history enabled?
#define SALCFG_SAVECMDLINEHISTORY 16     // BOOL, may command line history be stored to configuration?
#define SALCFG_MIDDLETOOLBARVISIBLE 17   // BOOL, is middle toolbar visible?
#define SALCFG_SORTDETECTNUMBERS 18      // BOOL, should it use numerical sort for numbers contained in strings when sorting?
#define SALCFG_SORTBYEXTDIRSASFILES 19   // BOOL, should it treat dirs as files when sorting by extension? BTW, if TRUE, directories extensions are also displayed in separated Ext column. (directories have no extensions, only files have extensions, but many people have requested sort by extension and displaying extension in separated Ext column even for directories)
#define SALCFG_SIZEFORMAT 20             // INT, units for custom size columns, 0 - Bytes, 1 - KB, 2 - short (mixed B, KB, MB, GB, ...)
#define SALCFG_SELECTWHOLENAME 21        // BOOL, should be whole name selected (including extension) when entering new filename? (for dialog boxes F2:QuickRename, Alt+F5:Pack, etc)
// recycle bin parameters
#define SALCFG_USERECYCLEBIN 50   // INT, 0 - do not use, 1 - use for all, 2 - use for files matching at \
                                  //      least one of masks (see SALCFG_RECYCLEBINMASKS)
#define SALCFG_RECYCLEBINMASKS 51 // STRING (MAX_PATH), masks for SALCFG_USERECYCLEBIN==2
// time resolution of file compare (used in command Compare Directories)
#define SALCFG_COMPDIRSUSETIMERES 60 // BOOL, should it use time resolution? (FALSE==exact match)
#define SALCFG_COMPDIRTIMERES 61     // INT, time resolution for file compare (from 0 to 3600 second)
// confirmations
#define SALCFG_CNFRMFILEDIRDEL 70 // BOOL, files or directories delete
#define SALCFG_CNFRMNEDIRDEL 71   // BOOL, non-empty directory delete
#define SALCFG_CNFRMFILEOVER 72   // BOOL, file overwrite
#define SALCFG_CNFRMSHFILEDEL 73  // BOOL, system or hidden file delete
#define SALCFG_CNFRMSHDIRDEL 74   // BOOL, system or hidden directory delete
#define SALCFG_CNFRMSHFILEOVER 75 // BOOL, system or hidden file overwrite
#define SALCFG_CNFRMCREATEPATH 76 // BOOL, show "do you want to create target path?" in Copy/Move operations
#define SALCFG_CNFRMDIROVER 77    // BOOL, directory overwrite (copy/move selected directory: ask user if directory already exists on target path - standard behaviour is to join contents of both directories)
// drive specific settings
#define SALCFG_DRVSPECFLOPPYMON 88         // BOOL, floppy disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECFLOPPYSIM 89         // BOOL, floppy disks - use simple icons
#define SALCFG_DRVSPECREMOVABLEMON 90      // BOOL, removable disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECREMOVABLESIM 91      // BOOL, removable disks - use simple icons
#define SALCFG_DRVSPECFIXEDMON 92          // BOOL, fixed disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECFIXEDSIMPLE 93       // BOOL, fixed disks - use simple icons
#define SALCFG_DRVSPECREMOTEMON 94         // BOOL, remote (network) disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECREMOTESIMPLE 95      // BOOL, remote (network) disks - use simple icons
#define SALCFG_DRVSPECREMOTEDONOTREF 96    // BOOL, remote (network) disks - do not refresh on activation of Salamander
#define SALCFG_DRVSPECCDROMMON 97          // BOOL, CDROM disks - use automatic refresh (changes monitoring)
#define SALCFG_DRVSPECCDROMSIMPLE 98       // BOOL, CDROM disks - use simple icons
#define SALCFG_IFPATHISINACCESSIBLEGOTO 99 // STRING (MAX_PATH), path where to go if path in panel is inaccessible
// internal text/hex viewer
#define SALCFG_VIEWEREOLCRLF 120          // BOOL, accept CR-LF ("\r\n") line ends?
#define SALCFG_VIEWEREOLCR 121            // BOOL, accept CR ("\r") line ends?
#define SALCFG_VIEWEREOLLF 122            // BOOL, accept LF ("\n") line ends?
#define SALCFG_VIEWEREOLNULL 123          // BOOL, accept NULL ("\0") line ends?
#define SALCFG_VIEWERTABSIZE 124          // INT, size of tab ("\t") character in spaces
#define SALCFG_VIEWERSAVEPOSITION 125     // BOOL, TRUE = save position of viewer window, FALSE = always use position of main window
#define SALCFG_VIEWERFONT 126             // LOGFONT, viewer font
#define SALCFG_VIEWERWRAPTEXT 127         // BOOL, wrap text (divide long text line to more lines)
#define SALCFG_AUTOCOPYSELTOCLIPBOARD 128 // BOOL, TRUE = when user selects some text, this text is instantly copied to the cliboard
// archivers
#define SALCFG_ARCOTHERPANELFORPACK 140    // BOOL, should it pack to other panel path?
#define SALCFG_ARCOTHERPANELFORUNPACK 141  // BOOL, should it unpack to other panel path?
#define SALCFG_ARCSUBDIRBYARCFORUNPACK 142 // BOOL, should it unpack to subdirectory named by archive?
#define SALCFG_ARCUSESIMPLEICONS 143       // BOOL, should it use simple icons in archives?

// callback type used in CSalamanderGeneral::SalSplitGeneralPath method
typedef BOOL(WINAPI* SGP_IsTheSamePathF)(const char* path1, const char* path2);

// callback type used in CSalamanderGeneralAbstract::CallPluginOperationFromDisk method
// 'sourcePath' is source path on disk (other paths are relative to it);
// selected files/directories are given to enumeration function 'next' whose parameter is
// 'nextParam'; 'param' is parameter passed to CallPluginOperationFromDisk as 'param'
typedef void(WINAPI* SalPluginOperationFromDisk)(const char* sourcePath, SalEnumSelection2 next,
                                                 void* nextParam, void* param);

// flags for text search algorithms (CSalamanderBMSearchData and CSalamanderREGEXPSearchData);
// flags can be logically combined
#define SASF_CASESENSITIVE 0x01 // case is important (if not set, search is case-insensitive)
#define SASF_FORWARD 0x02       // search forward (if not set, search backward)

// icons for GetSalamanderIcon
#define SALICON_EXECUTABLE 1    // exe/bat/pif/com
#define SALICON_DIRECTORY 2     // dir
#define SALICON_NONASSOCIATED 3 // non-associated file
#define SALICON_ASSOCIATED 4    // associated file
#define SALICON_UPDIR 5         // up-dir ".."
#define SALICON_ARCHIVE 6       // archive

// icon sizes for GetSalamanderIcon
#define SALICONSIZE_16 1 // 16x16
#define SALICONSIZE_32 2 // 32x32
#define SALICONSIZE_48 3 // 48x48

// interface for Boyer-Moore algorithm object for text searching
// WARNING: each allocated object can only be used within one thread
// (it doesn't have to be the main thread, different objects can use different threads)
class CSalamanderBMSearchData
{
public:
    // set pattern; 'pattern' is null-terminated pattern text; 'flags' are algorithm
    // flags (see SASF_XXX constants)
    virtual void WINAPI Set(const char* pattern, WORD flags) = 0;

    // set pattern; 'pattern' is binary pattern of length 'length' (buffer 'pattern' must
    // have length at least ('length' + 1) characters - only for compatibility with text patterns);
    // 'flags' are algorithm flags (see SASF_XXX constants)
    virtual void WINAPI Set(const char* pattern, const int length, WORD flags) = 0;

    // set algorithm flags; 'flags' are algorithm flags (see SASF_XXX constants)
    virtual void WINAPI SetFlags(WORD flags) = 0;

    // returns pattern length (usable only after successful call to Set method)
    virtual int WINAPI GetLength() const = 0;

    // returns pattern (usable only after successful call to Set method)
    virtual const char* WINAPI GetPattern() const = 0;

    // returns TRUE if it is possible to start searching (pattern and flags were successfully set,
    // failure threatens only with empty pattern)
    virtual BOOL WINAPI IsGood() const = 0;

    // search for pattern in text 'text' of length 'length' from offset 'start' forward;
    // returns offset of found pattern or -1 if pattern was not found;
    // WARNING: algorithm must have SASF_FORWARD flag set
    virtual int WINAPI SearchForward(const char* text, int length, int start) = 0;

    // search for pattern in text 'text' of length 'length' backward (starts searching at text end);
    // returns offset of found pattern or -1 if pattern was not found;
    // WARNING: algorithm must not have SASF_FORWARD flag set
    virtual int WINAPI SearchBackward(const char* text, int length) = 0;
};

// interface for algorithm object for searching using regular expressions in text
// WARNING: each allocated object can only be used within one thread
// (it doesn't have to be the main thread, different objects can use different threads)
class CSalamanderREGEXPSearchData
{
public:
    // set regular expression; 'pattern' is null-terminated regular expression text; 'flags'
    // are algorithm flags (see SASF_XXX constants); on error returns FALSE and error description
    // can be obtained by calling GetLastErrorText method
    virtual BOOL WINAPI Set(const char* pattern, WORD flags) = 0;

    // set algorithm flags; 'flags' are algorithm flags (see SASF_XXX constants);
    // on error returns FALSE and error description can be obtained by calling GetLastErrorText method
    virtual BOOL WINAPI SetFlags(WORD flags) = 0;

    // returns error text that occurred in last Set or SetFlags call (can be NULL)
    virtual const char* WINAPI GetLastErrorText() const = 0;

    // returns regular expression text (usable only after successful call to Set method)
    virtual const char* WINAPI GetPattern() const = 0;

    // set text line (line is from 'start' to 'end', 'end' points after last line character),
    // in which to search; always returns TRUE
    virtual BOOL WINAPI SetLine(const char* start, const char* end) = 0;

    // search for substring matching regular expression in line set by SetLine method;
    // searches from offset 'start' forward; returns offset of found substring and its length
    // (in 'foundLen') or -1 if substring was not found;
    // WARNING: algorithm must have SASF_FORWARD flag set
    virtual int WINAPI SearchForward(int start, int& foundLen) = 0;

    // search for substring matching regular expression in line set by SetLine method;
    // searches backward (starts searching at end of text of length 'length' from line beginning);
    // returns offset of found substring and its length (in 'foundLen') or -1 if substring
    // was not found;
    // WARNING: algorithm must not have SASF_FORWARD flag set
    virtual int WINAPI SearchBackward(int length, int& foundLen) = 0;
};

// Salamander command types used in CSalamanderGeneralAbstract::EnumSalamanderCommands method
#define sctyUnknown 0
#define sctyForFocusedFile 1                 // only for focused file (e.g. View)
#define sctyForFocusedFileOrDirectory 2      // for focused file or directory (e.g. Open)
#define sctyForSelectedFilesAndDirectories 3 // for selected/focused files and directories (e.g. Copy)
#define sctyForCurrentPath 4                 // for current path in panel (e.g. Create Directory)
#define sctyForConnectedDrivesAndFS 5        // for connected drives and FS (e.g. Disconnect)

// Salamander commands used in CSalamanderGeneralAbstract::EnumSalamanderCommands
// and CSalamanderGeneralAbstract::PostSalamanderCommand methods
// (WARNING: only interval <0, 499> is reserved for command numbers)
#define SALCMD_VIEW 0     // view (F3 key in panel)
#define SALCMD_ALTVIEW 1  // alternate view (Alt+F3 key in panel)
#define SALCMD_VIEWWITH 2 // view with (Ctrl+Shift+F3 key in panel)
#define SALCMD_EDIT 3     // edit (F4 key in panel)
#define SALCMD_EDITWITH 4 // edit with (Ctrl+Shift+F4 key in panel)

#define SALCMD_OPEN 20        // open (Enter key in panel)
#define SALCMD_QUICKRENAME 21 // quick rename (F2 key in panel)

#define SALCMD_COPY 40          // copy (F5 key in panel)
#define SALCMD_MOVE 41          // move/rename (F6 key in panel)
#define SALCMD_EMAIL 42         // email (Ctrl+E key in panel)
#define SALCMD_DELETE 43        // delete (Delete key in panel)
#define SALCMD_PROPERTIES 44    // show properties (Alt+Enter key in panel)
#define SALCMD_CHANGECASE 45    // change case (Ctrl+F7 key in panel)
#define SALCMD_CHANGEATTRS 46   // change attributes (Ctrl+F2 key in panel)
#define SALCMD_OCCUPIEDSPACE 47 // calculate occupied space (Alt+F10 key in panel)

#define SALCMD_EDITNEWFILE 70     // edit new file (Shift+F4 key in panel)
#define SALCMD_REFRESH 71         // refresh (Ctrl+R key in panel)
#define SALCMD_CREATEDIRECTORY 72 // create directory (F7 key in panel)
#define SALCMD_DRIVEINFO 73       // drive info (Ctrl+F1 key in panel)
#define SALCMD_CALCDIRSIZES 74    // calculate directory sizes (Ctrl+Shift+F10 key in panel)

#define SALCMD_DISCONNECT 90 // disconnect (network drive or plugin-fs) (F12 key in panel)

#define MAX_GROUPMASK 1001 // max. number of characters (including terminating zero) in group mask

// identifiers of shared histories (recently used values in comboboxes) for
// CSalamanderGeneral::GetStdHistoryValues()
#define SALHIST_QUICKRENAME 1 // names in Quick Rename dialog (F2)
#define SALHIST_COPYMOVETGT 2 // target paths in Copy/Move dialog (F5/F6)
#define SALHIST_CREATEDIR 3   // directory names in Create Directory dialog (F7)
#define SALHIST_CHANGEDIR 4   // paths in Change Directory dialog (Shift+F7)
#define SALHIST_EDITNEW 5     // names in Edit New dialog (Shift+F4)
#define SALHIST_CONVERT 6     // names in Convert dialog (Ctrl+K)

// interface for object working with file mask groups
// WARNING: object methods are not synchronized, so they can only be used
//        within one thread (doesn't have to be main thread) or plugin must
//        ensure their synchronization (no "write" operation during
//        execution of another method; "write"=SetMasksString+PrepareMasks;
//        "read" operations can be executed from multiple threads at once; "read"=GetMasksString+
//        AgreeMasks)
//
// Object lifecycle:
//   1) Allocate using CSalamanderGeneralAbstract::AllocSalamanderMaskGroup method
//   2) Pass mask group in SetMasksString method.
//   3) Call PrepareMasks to build internal data; in case of failure
//      display error position and after mask correction return to step (3)
//   4) Freely call AgreeMasks to find out if name matches mask group.
//   5) After possible call to SetMasksString continue from (3)
//   6) Destroy object using CSalamanderGeneralAbstract::FreeSalamanderMaskGroup method
//
// Mask:
//   '?' - any character
//   '*' - any string (even empty)
//   '#' - any digit (only if 'extendedMode'==TRUE)
//
//   Examples:
//     *     - all names
//     *.*   - all names
//     *.exe - names with "exe" extension
//     *.t?? - names with extension starting with 't' and containing two more arbitrary characters
//     *.r## - names with extension starting with 'r' and containing two more arbitrary digits
//
class CSalamanderMaskGroup
{
public:
    // set masks string (masks are separated by ';' (escape sequence for ';' is ";;"));
    // 'masks' is masks string (max. length including terminating zero is MAX_GROUPMASK)
    // if 'extendedMode' is TRUE, character '#' matches any digit ('0'-'9')
    // character '|' can be used as separator; following masks (again separated by ';')
    // will be evaluated inversely, so if name matches them,
    // AgreeMasks returns FALSE; character '|' can be at string beginning
    //
    //   Examples:
    //     *.txt;*.cpp - all names with txt or cpp extension
    //     *.h*|*.html - all names with extension starting with 'h', but not names with "html" extension
    //     |*.txt      - all names with extension other than "txt"
    virtual void WINAPI SetMasksString(const char* masks, BOOL extendedMode) = 0;

    // returns masks string; 'buffer' is buffer of length at least MAX_GROUPMASK
    virtual void WINAPI GetMasksString(char* buffer) = 0;

    // returns 'extendedMode' set in SetMasksString method
    virtual BOOL WINAPI GetExtendedMode() = 0;

    // working with file masks: ('?' any char, '*' any string - even empty, if
    //  'extendedMode' in SetMasksString method was TRUE, '#' any digit - '0'..'9'):
    // 1) convert masks to simpler format; 'errorPos' returns error position in masks string;
    //    returns TRUE if no error occurred (returns FALSE -> 'errorPos' is set)
    virtual BOOL WINAPI PrepareMasks(int& errorPos) = 0;
    // 2) we can use converted masks to test if file 'filename' matches any of them;
    //    'fileExt' points either to end of 'fileName' or to extension (if exists), 'fileExt'
    //    can be NULL (extension is searched according to std. rules); returns TRUE if file
    //    matches at least one mask
    virtual BOOL WINAPI AgreeMasks(const char* fileName, const char* fileExt) = 0;
};

// interface for MD5 calculation object
//
// Object lifecycle:
//
//   1) Allocate using CSalamanderGeneralAbstract::AllocSalamanderMD5 method
//   2) Gradually call Update() method for data for which we want to calculate MD5
//   3) Call Finalize() method
//   4) Retrieve calculated MD5 using GetDigest() method
//   5) If we want to reuse object, call Init() method
//      (called automatically in step (1)) and go to step (2)
//   6) Destroy object using CSalamanderGeneralAbstract::FreeSalamanderMD5 method
//
class CSalamanderMD5
{
public:
    // object initialization, called automatically in constructor
    // method is published for multiple use of allocated object
    virtual void WINAPI Init() = 0;

    // updates internal object state based on data block specified by 'input' variable,
    // 'input_length' specifies buffer size in bytes
    virtual void WINAPI Update(const void* input, DWORD input_length) = 0;

    // prepares MD5 for retrieval using GetDigest method
    // after calling Finalize method only GetDigest() and Init() methods can be called
    virtual void WINAPI Finalize() = 0;

    // retrieves MD5, 'dest' must point to buffer of size 16 bytes
    // method can only be called after calling Finalize method
    virtual void WINAPI GetDigest(void* dest) = 0;
};

#define SALPNG_GETALPHA 0x00000002    // when creating DIB alpha channel will also be set (otherwise will be 0)
#define SALPNG_PREMULTIPLE 0x00000004 // has meaning if SALPNG_GETALPHA is set; premultiplies RGB components so that AlphaBlend() can be called on bitmap with BLENDFUNCTION::AlphaFormat==AC_SRC_ALPHA

class CSalamanderPNGAbstract
{
public:
    // creates bitmap from PNG resource; 'hInstance' and 'lpBitmapName' specify resource,
    // 'flags' contains 0 or bits from SALPNG_xxx family
    // on success returns bitmap handle, otherwise NULL
    // plugin is responsible for bitmap destruction by calling DeleteObject()
    // can be called from any thread
    virtual HBITMAP WINAPI LoadPNGBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName, DWORD flags, COLORREF unused) = 0;

    // creates bitmap from PNG given in memory; 'rawPNG' is pointer to memory containing PNG
    // (for example loaded from file) and 'rawPNGSize' specifies size of memory occupied by PNG in bytes,
    // 'flags' contains 0 or bits from SALPNG_xxx family
    // on success returns bitmap handle, otherwise NULL
    // plugin is responsible for bitmap destruction by calling DeleteObject()
    // can be called from any thread
    virtual HBITMAP WINAPI LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags, COLORREF unused) = 0;

    // note 1: loaded PNG should be compressed using PNGSlim, see https://forum.altap.cz/viewtopic.php?f=15&t=3278
    // note 2: example of direct access to DIB data see Demoplugin, AlphaBlend function
    // note 3: supported are non-interlaced PNG of type Greyscale, Greyscale with alpha, Truecolour, Truecolour with alpha, Indexed-colour
    //             condition is 8 bits per channel
};

// all methods can only be called from main thread
class CSalamanderPasswordManagerAbstract
{
public:
    // returns TRUE if user has set master password in Salamander configuration, otherwise returns FALSE
    // (unrelated to whether MP was entered in this session)
    virtual BOOL WINAPI IsUsingMasterPassword() = 0;

    // returns TRUE if user entered correct master password in this Salamander session, otherwise returns FALSE
    virtual BOOL WINAPI IsMasterPasswordSet() = 0;

    // displays window with parent 'hParent' asking for master password entry
    // returns TRUE if correct MP was entered, otherwise returns FALSE
    // asks even if master password was already entered in this session, see IsMasterPasswordSet()
    // if user doesn't use master password, returns FALSE, see IsUsingMasterPassword()
    virtual BOOL WINAPI AskForMasterPassword(HWND hParent) = 0;

    // reads 'plainPassword' terminated by zero and based on 'encrypt' variable either encrypts it (if TRUE) using AES or
    // only scrambles it (if FALSE); stores allocated result to 'encryptedPassword' and returns its size in
    // 'encryptedPasswordSize' variable; returns TRUE on success, otherwise FALSE
    // if 'encrypt'==TRUE, caller must ensure master password is entered before calling function, see AskForMasterPassword()
    // note: returned 'encryptedPassword' is allocated on Salamander heap; if plugin doesn't use salrtl, buffer must be freed
    // using SalamanderGeneral->Free(), otherwise just call free();
    virtual BOOL WINAPI EncryptPassword(const char* plainPassword, BYTE** encryptedPassword, int* encryptedPasswordSize, BOOL encrypt) = 0;

    // reads 'encryptedPassword' of size 'encryptedPasswordSize' and converts it to plain password, which returns
    // in allocated buffer 'plainPassword'; returns TRUE on success, otherwise FALSE
    // note: returned 'plainPassword' is allocated on Salamander heap; if plugin doesn't use salrtl, buffer must be freed
    // using SalamanderGeneral->Free(), otherwise just call free();
    virtual BOOL WINAPI DecryptPassword(const BYTE* encryptedPassword, int encryptedPasswordSize, char** plainPassword) = 0;

    // returns TRUE if 'encyptedPassword' of length 'encyptedPasswordSize' is encrypted using AES; otherwise returns FALSE
    virtual BOOL WINAPI IsPasswordEncrypted(const BYTE* encyptedPassword, int encyptedPasswordSize) = 0;
};

// modes for CSalamanderGeneralAbstract::ExpandPluralFilesDirs method
#define epfdmNormal 0   // XXX files and YYY directories
#define epfdmSelected 1 // XXX selected files and YYY selected directories
#define epfdmHidden 2   // XXX hidden files and YYY hidden directories

// commands for HTML help: see CSalamanderGeneralAbstract::OpenHtmlHelp method
enum CHtmlHelpCommand
{
    HHCDisplayTOC,     // see HH_DISPLAY_TOC: dwData = 0 (no topic) or: pointer to a topic within a compiled help file
    HHCDisplayIndex,   // see HH_DISPLAY_INDEX: dwData = 0 (no keyword) or: keyword to select in the index (.hhk) file
    HHCDisplaySearch,  // see HH_DISPLAY_SEARCH: dwData = 0 (empty search) or: pointer to an HH_FTS_QUERY structure
    HHCDisplayContext, // see HH_HELP_CONTEXT: dwData = numeric ID of the topic to display
};

// serves as parameter for OpenHtmlHelpForSalamander when command==HHCDisplayContext
#define HTMLHELP_SALID_PWDMANAGER 1 // displays help for Password Manager

class CPluginFSInterfaceAbstract;

class CSalamanderZLIBAbstract;

class CSalamanderBZIP2Abstract;

class CSalamanderCryptAbstract;

class CSalamanderGeneralAbstract
{
public:
    // displays message-box with specified text and title, message-box parent is HWND
    // returned by GetMsgBoxParent() method (see below); uses SalMessageBox (see below)
    // type = MSGBOX_INFO        - information (ok)
    // type = MSGBOX_ERROR       - error message (ok)
    // type = MSGBOX_EX_ERROR    - error message (ok/cancel) - returns IDOK, IDCANCEL
    // type = MSGBOX_QUESTION    - question (yes/no) - returns IDYES, IDNO
    // type = MSGBOX_EX_QUESTION - question (yes/no/cancel) - returns IDYES, IDNO, IDCANCEL
    // type = MSGBOX_WARNING     - warning (ok)
    // type = MSGBOX_EX_WARNING  - warning (yes/no/cancel) - returns IDYES, IDNO, IDCANCEL
    // on error returns 0
    // restriction: main thread
    virtual int WINAPI ShowMessageBox(const char* text, const char* title, int type) = 0;

    // SalMessageBox and SalMessageBoxEx create, display and after selecting one of buttons
    // close message box. Message box can contain user defined title, message,
    // buttons, icon, checkbox with some text.
    //
    // If 'hParent' is not currently foreground window (msgbox in inactive application), before
    // msgbox is displayed FlashWindow(mainwnd, TRUE) is called and after msgbox is closed
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is in chain of parents of window 'hParent' the one
    // that no longer has parent (typically Salamander main window).
    //
    // SalMessageBox fills MSGBOXEX_PARAMS structure (hParent->HParent, lpText->Text,
    // lpCaption->Caption and uType->Flags; other structure fields are zeroed) and calls
    // SalMessageBoxEx, so we will further describe only SalMessageBoxEx.
    //
    // SalMessageBoxEx tries to behave as much as possible like Windows API functions
    // MessageBox and MessageBoxIndirect. Differences are:
    //   - message box is centered to hParent (if it's child window, non-child parent is found)
    //   - in case of MB_YESNO/MB_ABORTRETRYIGNORE message box it's possible to enable
    //     closing window with Escape key or by clicking cross in title (flag
    //     MSGBOXEX_ESCAPEENABLED); return value will then be IDNO/IDCANCEL
    //   - beep can be suppressed (flag MSGBOXEX_SILENT)
    //
    // Comment for uType see comment for MSGBOXEX_PARAMS::Flags
    //
    // Return Values
    //    DIALOG_FAIL       (0)            The function fails.
    //    DIALOG_OK         (IDOK)         'OK' button was selected.
    //    DIALOG_CANCEL     (IDCANCEL)     'Cancel' button was selected.
    //    DIALOG_ABORT      (IDABORT)      'Abort' button was selected.
    //    DIALOG_RETRY      (IDRETRY)      'Retry' button was selected.
    //    DIALOG_IGNORE     (IDIGNORE)     'Ignore' button was selected.
    //    DIALOG_YES        (IDYES)        'Yes' button was selected.
    //    DIALOG_NO         (IDNO)         'No' button was selected.
    //    DIALOG_TRYAGAIN   (IDTRYAGAIN)   'Try Again' button was selected.
    //    DIALOG_CONTINUE   (IDCONTINUE)   'Continue' button was selected.
    //    DIALOG_SKIP                      'Skip' button was selected.
    //    DIALOG_SKIPALL                   'Skip All' button was selected.
    //    DIALOG_ALL                       'All' button was selected.
    //
    // SalMessageBox and SalMessageBoxEx can be called from any thread
    virtual int WINAPI SalMessageBox(HWND hParent, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType) = 0;
    virtual int WINAPI SalMessageBoxEx(const MSGBOXEX_PARAMS* params) = 0;

    // returns HWND of suitable parent for opened message-boxes (or other modal windows),
    // it's main window, progress-dialog, Plugins/Plugins dialog or other modal window
    // opened to main window
    // restriction: main thread, returned HWND is always from main thread
    virtual HWND WINAPI GetMsgBoxParent() = 0;

    // returns handle of Salamander main window
    // can be called from any thread
    virtual HWND WINAPI GetMainWindowHWND() = 0;

    // restores focus in panel or in command line (depending on what was last activated); this
    // call is needed if plugin disables/enables Salamander main window (situations arise
    // when disabled main window is activated - focus cannot be set in disabled window -
    // after enabling main window focus needs to be restored with this method)
    virtual void WINAPI RestoreFocusInSourcePanel() = 0;

    // frequently used dialogs, dialog parent 'parent', return values DIALOG_XXX;
    // if 'parent' is not currently foreground window (dialog in inactive application), before
    // dialog is displayed FlashWindow(mainwnd, TRUE) is called and after dialog is closed
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is in chain of parents of window 'parent' the one
    // that no longer has parent (typically Salamander main window)
    // ERROR: filename+error+title (if 'title' == NULL, std. title "Error" is used)
    //
    // Variable 'flags' specifies displayed buttons, for DialogError one of these values can be used:
    // BUTTONS_OK               // OK                                    (old DialogError3)
    // BUTTONS_RETRYCANCEL      // Retry / Cancel                        (old DialogError4)
    // BUTTONS_SKIPCANCEL       // Skip / Skip all / Cancel              (old DialogError2)
    // BUTTONS_RETRYSKIPCANCEL  // Retry / Skip / Skip all / Cancel      (old DialogError)
    //
    // all can be called from any thread
    virtual int WINAPI DialogError(HWND parent, DWORD flags, const char* fileName, const char* error, const char* title) = 0;

    // CONFIRM FILE OVERWRITE: filename1+filedata1+filename2+filedata2
    // Variable 'flags' specifies displayed buttons, for DialogOverwrite one of these values can be used:
    // BUTTONS_YESALLSKIPCANCEL // Yes / All / Skip / Skip all / Cancel  (old DialogOverwrite)
    // BUTTONS_YESNOCANCEL      // Yes / No / Cancel                     (old DialogOverwrite2)
    virtual int WINAPI DialogOverwrite(HWND parent, DWORD flags, const char* fileName1, const char* fileData1,
                                       const char* fileName2, const char* fileData2) = 0;

    // QUESTION: filename+question+title (if 'title' == NULL, std. title "Question" is used)
    // Variable 'flags' specifies displayed buttons, for DialogQuestion one of these values can be used:
    // BUTTONS_YESALLSKIPCANCEL // Yes / All / Skip / Skip all / Cancel  (old DialogQuestion)
    // BUTTONS_YESNOCANCEL      // Yes / No / Cancel                     (old DialogQuestion2)
    // BUTTONS_YESALLCANCEL     // Yes / All / Cancel                    (old DialogQuestion3)
    virtual int WINAPI DialogQuestion(HWND parent, DWORD flags, const char* fileName,
                                      const char* question, const char* title) = 0;

    // if path 'dir' doesn't exist, allows to create it (asks user; optionally creates
    // multiple directories at end of path); if path exists or is successfully created returns TRUE;
    // if path doesn't exist and 'quiet' is TRUE, doesn't ask user whether to create
    // path 'dir'; if 'errBuf' is NULL, shows errors in windows; if 'errBuf' is not NULL,
    // puts error description to buffer 'errBuf' of size 'errBufSize' (no error windows are
    // opened); all opened windows have 'parent' as parent, if 'parent' is NULL,
    // Salamander main window is used; if 'firstCreatedDir' is not NULL, it's buffer
    // of size MAX_PATH for storing full name of first created directory on path
    // 'dir' (returns empty string if path 'dir' already exists); if 'manualCrDir' is TRUE,
    // doesn't allow creating directory with space at beginning of name (Windows doesn't mind, but it's
    // potentially dangerous, e.g. Explorer also doesn't allow it)
    // can be called from any thread
    virtual BOOL WINAPI CheckAndCreateDirectory(const char* dir, HWND parent = NULL, BOOL quiet = TRUE,
                                                char* errBuf = NULL, int errBufSize = 0,
                                                char* firstCreatedDir = NULL, BOOL manualCrDir = FALSE) = 0;

    // checks free space on path and if it's not >= totalSize asks if user wants to continue;
    // query window has 'parent' as parent, returns TRUE if there's enough space or if user answered
    // "continue"; if 'parent' is not currently foreground window (dialog in inactive application), before
    // dialog is displayed FlashWindow(mainwnd, TRUE) is called and after dialog is closed
    // FlashWindow(mainwnd, FALSE) is called, mainwnd is in chain of parents of window 'parent' the one
    // that no longer has parent (typically Salamander main window)
    // 'messageTitle' will be displayed in messagebox title with question and should be
    // name of plugin that called the method
    // can be called from any thread
    virtual BOOL WINAPI TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize,
                                      const char* messageTitle) = 0;

    // returns in 'retValue' (must not be NULL) free space on given path (so far most correct
    // information that can be obtained from Windows, on NT/W2K/XP/Vista can work with reparse points
    // and substs (Salamander 2.5 works only with junction-points)); 'path' is path on
    // which we check free space (doesn't have to be root); if 'total' is not NULL, returns in it
    // total disk size, if error occurs, returns CQuadWord(-1, -1)
    // can be called from any thread
    virtual void WINAPI GetDiskFreeSpace(CQuadWord* retValue, const char* path, CQuadWord* total) = 0;

    // own clone of Windows GetDiskFreeSpace: can determine correct data for paths containing
    // substs and reparse points under Windows 2000/XP/Vista/7 (Salamander 2.5 works only
    // with junction-points); 'path' is path on which we check free space; other parameters
    // correspond to standard Win32 API function GetDiskFreeSpace
    //
    // WARNING: do not use return values 'lpNumberOfFreeClusters' and 'lpTotalNumberOfClusters', because
    //        for larger disks they contain nonsense (DWORD may not be enough for total cluster count),
    //        solve via previous GetDiskFreeSpace method, which returns 64-bit numbers
    //
    // can be called from any thread
    virtual BOOL WINAPI SalGetDiskFreeSpace(const char* path, LPDWORD lpSectorsPerCluster,
                                            LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                                            LPDWORD lpTotalNumberOfClusters) = 0;

    // own clone of Windows GetVolumeInformation: can determine correct data also for
    // paths containing substs and reparse points under Windows 2000/XP/Vista (Salamander 2.5
    // works only with junction-points); 'path' is path for which we query information;
    // in 'rootOrCurReparsePoint' (if not NULL, must be at least MAX_PATH
    // characters large buffer) returns root directory or current (last) local reparse
    // point on path 'path' (Salamander 2.5 returns path for which it succeeded to determine
    // information or at least root directory); other parameters correspond to standard Win32 API
    // function GetVolumeInformation
    // can be called from any thread
    virtual BOOL WINAPI SalGetVolumeInformation(const char* path, char* rootOrCurReparsePoint, LPTSTR lpVolumeNameBuffer,
                                                DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
                                                LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                                                LPTSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize) = 0;

    // own clone of Windows GetDriveType: can determine correct data also for paths
    // containing substs and reparse points under Windows 2000/XP/Vista (Salamander 2.5
    // works only with junction-points); 'path' is path whose type we determine
    // can be called from any thread
    virtual UINT WINAPI SalGetDriveType(const char* path) = 0;

    // because Windows GetTempFileName doesn't work, we wrote our own clone:
    // creates file/directory (according to 'file') on path 'path' (NULL -> Windows TEMP dir),
    // with prefix 'prefix', returns name of created file in 'tmpName' (min. size MAX_PATH),
    // returns success (on failure returns in 'err' (if not NULL) Windows error code)
    // can be called from any thread
    virtual BOOL WINAPI SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file, DWORD* err) = 0;

    // removal of directory including its contents (SHFileOperation is terribly slow)
    // can be called from any thread
    virtual void WINAPI RemoveTemporaryDir(const char* dir) = 0;

    // because Windows version of MoveFile doesn't handle renaming file with read-only attribute on Novell,
    // we wrote our own (if error occurs at MoveFile, tries to drop read-only, perform operation,
    // and then set it again); returns success (on failure returns in 'err' (if not NULL) Windows error code)
    // can be called from any thread
    virtual BOOL WINAPI SalMoveFile(const char* srcName, const char* destName, DWORD* err) = 0;

    // variant of Windows version GetFileSize (has simpler error handling); 'file' is opened
    // file for GetFileSize() call; in 'size' returns obtained file size; returns success,
    // on FALSE (error) 'err' contains Windows error code and 'size' is zero;
    // NOTE: there's variant SalGetFileSize2() that works with full file name
    // can be called from any thread
    virtual BOOL WINAPI SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err) = 0;

    // opens file/directory 'name' on path 'path'; follows Windows associations, opens
    // via Open item in context menu (can also use salopen.exe, depends on configuration);
    // before execution sets current directories on local drives according to panel;
    // 'parent' is parent of potential windows (e.g. when opening unassociated file)
    // restriction: main thread (otherwise salopen.exe wouldn't work - uses one shared memory)
    virtual void WINAPI ExecuteAssociation(HWND parent, const char* path, const char* name) = 0;

    // opens browse dialog in which user selects path; 'parent' is parent of browse dialog;
    // 'hCenterWindow' - window to which dialog will be centered; 'title' is title of browse dialog;
    // 'comment' is comment in browse dialog; 'path' is buffer for resulting path (min. MAX_PATH
    // characters); if 'onlyNet' is TRUE, only network paths can be browsed (otherwise unrestricted); if
    // 'initDir' is not NULL, contains path on which browse dialog should open; returns TRUE if there is
    // new selected path in 'path'
    // WARNING: if called outside main thread, COM must be initialized first (perhaps better entire
    //          OLE - see CoInitialize or OLEInitialize)
    // can be called from any thread
    virtual BOOL WINAPI GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title,
                                           const char* comment, char* path, BOOL onlyNet,
                                           const char* initDir) = 0;

    // working with file masks: ('?' any character, '*' any string - even empty)
    // all can be called from any thread
    // 1) convert mask to simpler format (src -> buffer mask - min. size
    //    of buffer 'mask' is (strlen(src) + 1))
    virtual void WINAPI PrepareMask(char* mask, const char* src) = 0;
    // 2) we can use converted mask to test whether file filename matches it,
    //    hasExtension = TRUE if file has extension
    //    returns TRUE if file matches mask
    virtual BOOL WINAPI AgreeMask(const char* filename, const char* mask, BOOL hasExtension) = 0;
    // 3) unmodified mask (don't call PrepareMask for it) can be used to create name according to
    //    given name and mask ("a.txt" + "*.cpp" -> "a.cpp" etc.),
    //    buffer should be at least strlen(name)+strlen(mask) (2*MAX_PATH is suitable)
    //    returns created name (pointer 'buffer')
    virtual char* WINAPI MaskName(char* buffer, int bufSize, const char* name, const char* mask) = 0;

    // working with extended file masks: ('?' any character, '*' any string - even empty,
    // '#' any digit - '0'..'9')
    // all can be called from any thread
    // 1) convert mask to simpler format (src -> buffer mask - min. length strlen(src) + 1)
    virtual void WINAPI PrepareExtMask(char* mask, const char* src) = 0;
    // 2) we can use converted mask to test whether file filename matches it,
    //    hasExtension = TRUE if file has extension
    //    returns TRUE if file matches mask
    virtual BOOL WINAPI AgreeExtMask(const char* filename, const char* mask, BOOL hasExtension) = 0;

    // allocates new object for working with group of file masks
    // can be called from any thread
    virtual CSalamanderMaskGroup* WINAPI AllocSalamanderMaskGroup() = 0;

    // frees object for working with group of file masks (obtained by AllocSalamanderMaskGroup method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderMaskGroup(CSalamanderMaskGroup* maskGroup) = 0;

    // memory allocation on Salamander's heap (when using salrtl9.dll unnecessary - classic malloc suffices);
    // when memory is insufficient, message with buttons Retry (another allocation attempt),
    // Abort (after another question terminates application) and Ignore (let allocation error into application - after
    // warning user that application may crash, Alloc returns NULL;
    // checking for NULL makes sense probably only for large memory blocks, e.g. more than 500 MB, where there is risk
    // that allocation won't be possible due to address space fragmentation by loaded DLL libraries);
    // NOTE: Realloc() was added later, it is lower in this module
    // can be called from any thread
    virtual void* WINAPI Alloc(int size) = 0;
    // memory deallocation from Salamander's heap (when using salrtl9.dll unnecessary - classic free suffices)
    // can be called from any thread
    virtual void WINAPI Free(void* ptr) = 0;

    // string duplication - memory allocation (on Salamander's heap - heap accessible via salrtl9.dll)
    // + string copy; when 'str'==NULL returns NULL;
    // can be called from any thread
    virtual char* WINAPI DupStr(const char* str) = 0;

    // returns mapping table for lowercase and uppercase letters (array of 256 characters - lowercase/uppercase letter at
    // index of queried letter); if 'lowerCase' is not NULL, table of lowercase letters is returned in it;
    // if 'upperCase' is not NULL, table of uppercase letters is returned in it
    // can be called from any thread
    virtual void WINAPI GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase) = 0;

    // conversion of string 'str' to lowercase/uppercase; unlike ANSI C tolower/toupper works
    // directly with string and supports not only characters 'A' to 'Z' (conversion to lowercase is done via
    // array initialized with Win32 API function CharLower)
    virtual void WINAPI ToLowerCase(char* str) = 0;
    virtual void WINAPI ToUpperCase(char* str) = 0;

    //*****************************************************************************
    //
    // StrCmpEx
    //
    // Function compares two substrings.
    // If the two substrings are of different lengths, they are compared up to the
    // length of the shortest one. If they are equal to that point, then the return
    // value will indicate that the longer string is greater.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   l1    : compared length of s1 (must be less or equal to strlen(s1))
    //   l2    : compared length of s2 (must be less or equal to strlen(s1))
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrCmpEx(const char* s1, int l1, const char* s2, int l2) = 0;

    //*****************************************************************************
    //
    // StrICpy
    //
    // Function copies characters from source to destination. Upper case letters are mapped to
    // lower case using LowerCase array (filled using CharLower Win32 API call).
    //
    // Parameters
    //   dest: pointer to the destination string
    //   src: pointer to the null-terminated source string
    //
    // Return Values
    //   The StrICpy returns the number of bytes stored in buffer, not counting
    //   the terminating null character.
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICpy(char* dest, const char* src) = 0;

    //*****************************************************************************
    //
    // StrICmp
    //
    // Function compares two strings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    //
    // Parameters
    //   s1, s2: null-terminated strings to compare
    //
    // Return Values
    //   -1 if s1 < s2 (if string pointed to by s1 is less than the string pointed to by s2)
    //    0 if s1 = s2 (if the strings are equal)
    //   +1 if s1 > s2 (if string pointed to by s1 is greater than the string pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICmp(const char* s1, const char* s2) = 0;

    //*****************************************************************************
    //
    // StrICmpEx
    //
    // Function compares two substrings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    // If the two substrings are of different lengths, they are compared up to the
    // length of the shortest one. If they are equal to that point, then the return
    // value will indicate that the longer string is greater.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   l1    : compared length of s1 (must be less or equal to strlen(s1))
    //   l2    : compared length of s2 (must be less or equal to strlen(s2))
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrICmpEx(const char* s1, int l1, const char* s2, int l2) = 0;

    //*****************************************************************************
    //
    // StrNICmp
    //
    // Function compares two strings. The comparison is not case sensitive and ignores
    // regional settings. For the purposes of the comparison, all characters are converted
    // to lower case using LowerCase array (filled using CharLower Win32 API call).
    // The comparison stops after: (1) a difference between the strings is found,
    // (2) the end of the string is reached, or (3) n characters have been compared.
    //
    // Parameters
    //   s1, s2: strings to compare
    //   n:      maximum length to compare
    //
    // Return Values
    //   -1 if s1 < s2 (if substring pointed to by s1 is less than the substring pointed to by s2)
    //    0 if s1 = s2 (if the substrings are equal)
    //   +1 if s1 > s2 (if substring pointed to by s1 is greater than the substring pointed to by s2)
    //
    // Method can be called from any thread.
    virtual int WINAPI StrNICmp(const char* s1, const char* s2, int n) = 0;

    //*****************************************************************************
    //
    // MemICmp
    //
    // Compares n bytes of the two blocks of memory stored at buf1 and buf2.
    // Characters are converted to lowercase before comparing (not permanently;
    // using LowerCase array which was filled using CharLower Win32 API call),
    // so case is ignored in comparation.
    //
    // Parameters
    //   buf1, buf2: memory buffers to compare
    //   n:          maximum length to compare
    //
    // Return Values
    //   -1 if buf1 < buf2 (if buffer pointed to by buf1 is less than the buffer pointed to by buf2)
    //    0 if buf1 = buf2 (if the buffers are equal)
    //   +1 if buf1 > buf2 (if buffer pointed to by buf1 is greater than the buffer pointed to by buf2)
    //
    // Method can be called from any thread.
    virtual int WINAPI MemICmp(const void* buf1, const void* buf2, int n) = 0;

    // comparison of two strings 's1' and 's2' ignoring letter case (ignore-case),
    // if SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to Windows regional settings,
    // otherwise compares same as CSalamanderGeneral::StrICmp, if SALCFG_SORTDETECTNUMBERS is
    // TRUE, uses numerical sorting for numbers contained in strings
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrICmp(const char* s1, const char* s2) = 0;

    // comparison of two strings 's1' and 's2' (of lengths 'l1' and 'l2') ignoring letter
    // case (ignore-case), if SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to
    // Windows regional settings, otherwise compares same as CSalamanderGeneral::StrICmp,
    // if SALCFG_SORTDETECTNUMBERS is TRUE, uses numerical sorting for numbers contained
    // in strings; in 'numericalyEqual' (if not NULL) returns TRUE if strings are
    // numerically equal (e.g. "a01" and "a1"), is automatically TRUE if strings are equal
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrICmpEx(const char* s1, int l1, const char* s2, int l2,
                                       BOOL* numericalyEqual) = 0;

    // comparison (case-sensitive) of two strings 's1' and 's2', if SALCFG_SORTUSESLOCALE is TRUE,
    // uses sorting according to Windows regional settings, otherwise compares same as
    // standard C library function strcmp, if SALCFG_SORTDETECTNUMBERS is TRUE, uses
    // numerical sorting for numbers contained in strings
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrCmp(const char* s1, const char* s2) = 0;

    // comparison (case-sensitive) of two strings 's1' and 's2' (of lengths 'l1' and 'l2'), if
    // SALCFG_SORTUSESLOCALE is TRUE, uses sorting according to Windows regional settings,
    // otherwise compares same as standard C library function strcmp, if
    // SALCFG_SORTDETECTNUMBERS is TRUE, uses numerical sorting for numbers contained in strings;
    // in 'numericalyEqual' (if not NULL) returns TRUE if strings are numerically equal
    // (e.g. "a01" and "a1"), is automatically TRUE if strings are equal
    // returns <0 ('s1' < 's2'), ==0 ('s1' == 's2'), >0 ('s1' > 's2')
    virtual int WINAPI RegSetStrCmpEx(const char* s1, int l1, const char* s2, int l2,
                                      BOOL* numericalyEqual) = 0;

    // returns path in panel; 'panel' is one of PANEL_XXX; 'buffer' is buffer for path (can
    // be NULL too); 'bufferSize' is size of buffer 'buffer' (if 'buffer' is NULL, must be
    // zero); 'type' if not NULL points to variable where path type will be stored
    // (see PATH_TYPE_XXX); if it's an archive and 'archiveOrFS' is not NULL and 'buffer' is not NULL,
    // returns 'archiveOrFS' set in 'buffer' at position after archive file;
    // if it's a file-system and 'archiveOrFS' is not NULL and 'buffer' is not NULL, returns
    // 'archiveOrFS' set in 'buffer' at ':' after file-system name (after ':' is user-part
    // of file-system path); if 'convertFSPathToExternal' is TRUE and in panel is path on FS,
    // plugin whose path it is (according to fs-name) will be found and its
    // CPluginInterfaceForFSAbstract::ConvertPathToExternal() will be called; returns success (if not
    // 'bufferSize'==0, it is also considered failure when path doesn't fit into buffer
    // 'buffer')
    // restriction: main thread
    virtual BOOL WINAPI GetPanelPath(int panel, char* buffer, int bufferSize, int* type,
                                     char** archiveOrFS, BOOL convertFSPathToExternal = FALSE) = 0;

    // returns last visited Windows path in panel, useful for returns from FS (nicer than
    // going directly to fixed-drive); 'panel' is one of PANEL_XXX; 'buffer' is buffer for path;
    // 'bufferSize' is size of buffer 'buffer'; returns success
    // restriction: main thread
    virtual BOOL WINAPI GetLastWindowsPanelPath(int panel, char* buffer, int bufferSize) = 0;

    // returns FS name assigned "for lifetime" to plugin by Salamander (according to proposal from SetBasicPluginData);
    // 'buf' is buffer of size at least MAX_PATH characters; 'fsNameIndex' is index of fs-name (index is
    // zero for fs-name specified in CSalamanderPluginEntryAbstract::SetBasicPluginData, for other
    // fs-names index is returned by CSalamanderPluginEntryAbstract::AddFSName)
    // restriction: main thread (otherwise changes in plugin configuration may occur during call),
    // in entry-point can be called only after SetBasicPluginData, before it may not be known
    virtual void WINAPI GetPluginFSName(char* buf, int fsNameIndex) = 0;

    // returns interface of plugin file-system (FS) opened in panel 'panel' (one of PANEL_XXX);
    // if no FS is opened in panel or it's FS of another plugin (doesn't belong to calling plugin), method
    // returns NULL (cannot work with object of another plugin, its structure is unknown)
    // restriction: main thread
    virtual CPluginFSInterfaceAbstract* WINAPI GetPanelPluginFS(int panel) = 0;

    // returns plugin data interface of panel listing (can be NULL too), 'panel' is one of PANEL_XXX;
    // if plugin data interface exists, but doesn't belong to this (calling) plugin, method
    // returns NULL (cannot work with object of another plugin, its structure is unknown)
    // restriction: main thread
    virtual CPluginDataInterfaceAbstract* WINAPI GetPanelPluginData(int panel) = 0;

    // returns focused panel item (file/directory/updir("..")), 'panel' is one of PANEL_XXX,
    // returns NULL (no item in panel) or data of focused item; if 'isDir' is not NULL,
    // returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data is read-only
    // restriction: main thread
    virtual const CFileData* WINAPI GetPanelFocusedItem(int panel, BOOL* isDir) = 0;

    // returns panel items sequentially (first directories, then files), 'panel' is one of PANEL_XXX,
    // 'index' is input/output variable, points to int which is 0 at first call,
    // value for next call is stored by function upon return (usage: zero at start, then
    // don't modify), returns NULL (no more items) or data of next (or first) item;
    // if 'isDir' is not NULL, returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data is read-only
    // restriction: main thread
    virtual const CFileData* WINAPI GetPanelItem(int panel, int* index, BOOL* isDir) = 0;

    // returns selected panel items sequentially (first directories, then files), 'panel' is one of
    // PANEL_XXX, 'index' is input/output variable, points to int which is 0 at first call,
    // value for next call is stored by function upon return (usage: zero at start, then
    // don't modify), returns NULL (no more items) or data of next (or first) item;
    // if 'isDir' is not NULL, returns FALSE in it if it's a file (otherwise it's a directory or updir)
    // WARNING: returned item data is read-only
    // restriction: main thread
    virtual const CFileData* WINAPI GetPanelSelectedItem(int panel, int* index, BOOL* isDir) = 0;

    // finds out how many files and directories are selected in panel; 'panel' is one of PANEL_XXX;
    // if 'selectedFiles' is not NULL, returns count of selected files in it; if 'selectedDirs' is
    // not NULL, returns count of selected directories in it; returns TRUE if at least one
    // file or directory is selected or if focus is on file or directory (if there is something
    // to work with - focus is not on up-dir)
    // restriction: main thread (otherwise panel contents may change)
    virtual BOOL WINAPI GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs) = 0;

    // returns top-index of listbox in panel; 'panel' is one of PANEL_XXX
    // restriction: main thread (otherwise panel contents may change)
    virtual int WINAPI GetPanelTopIndex(int panel) = 0;

    // informs Salamander's main window that viewer window is being deactivated, if
    // main window will be activated immediately and in panels there will be non-automatically refreshed
    // disks, their refresh won't occur (viewers don't change disk contents), optional (will result
    // in possibly unnecessary refresh)
    // can be called from any thread
    virtual void WINAPI SkipOneActivateRefresh() = 0;

    // selects/unselects panel item, 'file' is pointer to changed item obtained by previous
    // "get-item" call (methods GetPanelFocusedItem, GetPanelItem and GetPanelSelectedItem);
    // it's necessary that plugin hasn't been left since "get-item" call and call occurred in main
    // thread (to prevent panel refresh - pointer invalidation); 'panel' must be same
    // as parameter 'panel' of corresponding "get-item" call; if 'select' is TRUE selection occurs,
    // otherwise unselection occurs; after last call it's necessary to use RepaintChangedItems('panel') for
    // panel redraw
    // restriction: main thread
    virtual void WINAPI SelectPanelItem(int panel, const CFileData* file, BOOL select) = 0;

    // redraws panel items where changes occurred (selection); 'panel' is
    // one of PANEL_XXX
    // restriction: main thread
    virtual void WINAPI RepaintChangedItems(int panel) = 0;

    // selects/unselects all items in panel, 'panel' is one of PANEL_XXX; if 'select' is
    // TRUE selection occurs, otherwise unselection occurs; if 'repaint' is TRUE all
    // changed items in panel are redrawn, otherwise redraw doesn't occur (can call RepaintChangedItems later)
    // restriction: main thread
    virtual void WINAPI SelectAllPanelItems(int panel, BOOL select, BOOL repaint) = 0;

    // sets focus in panel, 'file' is pointer to focused item obtained by previous
    // "get-item" call (methods GetPanelFocusedItem, GetPanelItem and GetPanelSelectedItem);
    // it's necessary that plugin hasn't been left since "get-item" call and call occurred in main
    // thread (to prevent panel refresh - pointer invalidation); 'panel' must be same
    // as parameter 'panel' of corresponding "get-item" call; if 'partVis' is TRUE and item will be
    // visible only partially, panel won't be scrolled on focus, with FALSE scrolls panel
    // so that entire item is visible
    // restriction: main thread
    virtual void WINAPI SetPanelFocusedItem(int panel, const CFileData* file, BOOL partVis) = 0;

    // finds out if filter is used in panel and if it is, gets string with masks
    // of this filter; 'panel' indicates panel we are interested in (one of PANEL_XXX);
    // 'masks' is buffer for filter masks of size at least 'masksBufSize' bytes (recommended
    // size is MAX_GROUPMASK); returns TRUE if filter is used and buffer 'masks' is
    // large enough; returns FALSE if filter is not used or string of masks didn't fit
    // into 'masks'
    // restriction: main thread
    virtual BOOL WINAPI GetFilterFromPanel(int panel, char* masks, int masksBufSize) = 0;

    // returns position of source panel (is it left or right?), returns PANEL_LEFT or PANEL_RIGHT
    // restriction: main thread
    virtual int WINAPI GetSourcePanel() = 0;

    // finds out in which panel 'pluginFS' is opened; if it's not in any panel,
    // returns FALSE; if returns TRUE, panel number is in 'panel' (PANEL_LEFT or PANEL_RIGHT)
    // restriction: main thread (otherwise panel contents may change)
    virtual BOOL WINAPI GetPanelWithPluginFS(CPluginFSInterfaceAbstract* pluginFS, int& panel) = 0;

    // activates second panel (like TAB key); panels marked via PANEL_SOURCE and PANEL_TARGET
    // are naturally swapped by this
    // restriction: main thread
    virtual void WINAPI ChangePanel() = 0;

    // conversion of number to "more readable" string (space after every three digits), returns string in
    // 'buffer' (min. size 50 bytes), returns 'buffer'
    // can be called from any thread
    virtual char* WINAPI NumberToStr(char* buffer, const CQuadWord& number) = 0;

    // prints disk space size into 'buf' (min. buffer size is 100 bytes),
    // mode==0 "1.23 MB", mode==1 "1 230 000 bytes, 1.23 MB", mode==2 "1 230 000 bytes",
    // mode==3 "12 KB" (always in whole kilobytes), mode==4 (like mode==0, but always
    // at least 3 significant digits, e.g. "2.00 MB")
    // returns 'buf'
    // can be called from any thread
    virtual char* WINAPI PrintDiskSize(char* buf, const CQuadWord& size, int mode) = 0;

    // converts number of seconds to string ("5 sec", "1 hr 34 min", etc.); 'buf' is
    // buffer for resulting text, must be at least 100 characters; 'secs' is number of seconds;
    // returns 'buf'
    // can be called from any thread
    virtual char* WINAPI PrintTimeLeft(char* buf, const CQuadWord& secs) = 0;

    // compares root of normal (c:\path) and UNC (\\server\share\path) paths, returns TRUE if root is same
    // can be called from any thread
    virtual BOOL WINAPI HasTheSameRootPath(const char* path1, const char* path2) = 0;

    // Returns number of characters of common path. On normal path root must be terminated with backslash,
    // otherwise function returns 0. ("C:\"+"C:"->0, "C:\A\B"+"C:\"->3, "C:\A\B\"+"C:\A"->4,
    // "C:\AA\BB"+"C:\AA\CC"->5)
    // Works for normal and UNC paths.
    virtual int WINAPI CommonPrefixLength(const char* path1, const char* path2) = 0;

    // Returns TRUE if path 'prefix' is base of path 'path'. Otherwise returns FALSE.
    // "C:\aa","C:\Aa\BB"->TRUE
    // "C:\aa","C:\aaa"->FALSE
    // "C:\aa\","C:\Aa"->TRUE
    // "\\server\share","\\server\share\aaa"->TRUE
    // Works for normal and UNC paths.
    virtual BOOL WINAPI PathIsPrefix(const char* prefix, const char* path) = 0;

    // compares two normal (c:\path) and UNC (\\server\share\path) paths, ignores lowercase/uppercase,
    // also ignores one backslash at beginning and end of paths, returns TRUE if paths are same
    // can be called from any thread
    virtual BOOL WINAPI IsTheSamePath(const char* path1, const char* path2) = 0;

    // gets root path from normal (c:\path) and UNC (\\server\share\path) path 'path', returns in 'root'
    // path in format 'c:\' or '\\server\share\' (min. size of 'root' buffer is MAX_PATH),
    // returns number of characters of root path (without null-terminator); with UNC root path longer than MAX_PATH
    // truncation to MAX_PATH-2 characters occurs and backslash is added (anyway it's not 100% root path)
    // can be called from any thread
    virtual int WINAPI GetRootPath(char* root, const char* path) = 0;

    // shortens normal (c:\path) and UNC (\\server\share\path) path by last directory
    // (cutting at last backslash - in cut path backslash remains at end
    // only for 'c:\'), 'path' is in/out buffer (min. size strlen(path)+2 bytes),
    // in 'cutDir' (if not NULL) pointer is returned (into buffer 'path' after 1st null-terminator)
    // to last directory (cut part), this method replaces PathRemoveFileSpec,
    // returns TRUE if shortening occurred (it wasn't root path)
    // can be called from any thread
    virtual BOOL WINAPI CutDirectory(char* path, char** cutDir = NULL) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // joins 'path' and 'name' into 'path', ensures joining with backslash, 'path' is buffer of at least
    // 'pathSize' characters, returns TRUE if 'name' fit after 'path'; if 'path' or 'name' is
    // empty, joining (initial/terminal) backslash won't be present (e.g. "c:\" + "" -> "c:")
    // can be called from any thread
    virtual BOOL WINAPI SalPathAppend(char* path, const char* name, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if 'path' doesn't end with backslash yet, adds it to end of 'path'; 'path' is buffer
    // of at least 'pathSize' characters; returns TRUE if backslash fit after 'path'; if 'path' is
    // empty, backslash won't be added
    // can be called from any thread
    virtual BOOL WINAPI SalPathAddBackslash(char* path, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if there is backslash at end of 'path', removes it
    // can be called from any thread
    virtual void WINAPI SalPathRemoveBackslash(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // converts full name to name only ("c:\path\file" -> "file")
    // can be called from any thread
    virtual void WINAPI SalPathStripPath(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if name has extension, removes it
    // can be called from any thread
    virtual void WINAPI SalPathRemoveExtension(char* path) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // if name 'path' doesn't have extension yet, adds extension 'extension' (e.g. ".txt"),
    // 'path' is buffer of at least 'pathSize' characters, returns FALSE if buffer 'path' doesn't suffice
    // for resulting path
    // can be called from any thread
    virtual BOOL WINAPI SalPathAddExtension(char* path, const char* extension, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // changes/adds extension 'extension' (e.g. ".txt") in name 'path', 'path' is buffer
    // of at least 'pathSize' characters, returns FALSE if buffer 'path' doesn't suffice for resulting path
    // can be called from any thread
    virtual BOOL WINAPI SalPathRenameExtension(char* path, const char* extension, int pathSize) = 0;

    // works with normal (c:\path) and UNC (\\server\share\path) paths,
    // returns pointer into 'path' to file/directory name (ignores backslash at end of 'path'),
    // if name doesn't contain other backslashes than at end of string, returns 'path'
    // can be called from any thread
    virtual const char* WINAPI SalPathFindFileName(const char* path) = 0;

    // modifies relative or absolute normal (c:\path) and UNC (\\server\share\path) path
    // to absolute without '.', '..' and trailing backslashes (except type "c:\"); if 'curDir' is NULL,
    // relative paths of type "\path" and "path" return error (indeterminable), otherwise 'curDir' is valid
    // modified current path (UNC and normal); current paths of other drives (besides
    // 'curDir' + only normal, not UNC) are in Salamander's array DefaultDir (before using
    // it's good to call method SalUpdateDefaultDir); 'name' - in/out path buffer of at least 'nameBufSize'
    // characters; if 'nextFocus' is not NULL and given relative path doesn't contain backslash, performs
    // strcpy(nextFocus, name); returns TRUE - name 'name' is ready for use, otherwise if
    // 'errTextID' is not NULL contains error (constants GFN_XXX - text can be obtained via GetGFNErrorText)
    // WARNING: before using it's good to call method SalUpdateDefaultDir
    // restriction: main thread (otherwise changes to DefaultDir may occur in main thread)
    virtual BOOL WINAPI SalGetFullName(char* name, int* errTextID = NULL, const char* curDir = NULL,
                                       char* nextFocus = NULL, int nameBufSize = MAX_PATH) = 0;

    // refreshes Salamander's array DefaultDir according to paths in panels, if 'activePrefered' is TRUE,
    // path in active panel will have priority (is written later to DefaultDir), otherwise
    // path in inactive panel has priority
    // restriction: main thread (otherwise changes to DefaultDir may occur in main thread)
    virtual void WINAPI SalUpdateDefaultDir(BOOL activePrefered) = 0;

    // returns text representation of GFN_XXX error constant; returns 'buf' (so GetGFNErrorText
    // can be used directly as a function parameter)
    // can be called from any thread
    virtual char* WINAPI GetGFNErrorText(int GFN, char* buf, int bufSize) = 0;

    // returns text representation of system error (ERROR_XXX) in buffer 'buf' of size 'bufSize';
    // returns 'buf' (so GetErrorText can be used directly as a function parameter); 'buf' can be NULL or
    // 'bufSize' 0, in which case it returns text in internal buffer (text may change due to internal
    // buffer changes caused by subsequent GetErrorText calls from other plugins or Salamander;
    // buffer is dimensioned for at least 10 texts before overwriting occurs; if you need to use the text
    // later, we recommend copying it to a local buffer of size MAX_PATH + 20)
    // can be called from any thread
    virtual char* WINAPI GetErrorText(int err, char* buf = NULL, int bufSize = 0) = 0;

    // returns internal Salamander color, 'color' is color constant (see SALCOL_XXX)
    // can be called from any thread
    virtual COLORREF WINAPI GetCurrentColor(int color) = 0;

    // ensures activation of Salamander's main window + focus of file/directory 'name' on path
    // 'path' in panel 'panel'; optionally changes path in panel (if necessary); 'panel' is one of
    // PANEL_XXX; 'path' is any path (windows (disk), on FS or into archive);
    // 'name' can be empty string if nothing should be focused;
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual void WINAPI FocusNameInPanel(int panel, const char* path, const char* name) = 0;

    // changes path in panel - input can be absolute or relative UNC (\\server\share\path)
    // or normal (c:\path) path, both windows (disk), to archive or path
    // on FS (absolute/relative is handled by plugin directly); if input is path to file,
    // this file will be focused; if suggestedTopIndex is not -1, top-index will be set
    // in panel; if suggestedFocusName is not NULL, it will try to find (ignore-case) and focus
    // item with same name; if 'failReason' is not NULL, it is set to one of constants
    // CHPPFR_XXX (informs about method result); if 'convertFSPathToInternal' is TRUE and it's
    // a path on FS, plugin whose path it is (by fs-name) is found and its
    // CPluginInterfaceForFSAbstract::ConvertPathToInternal() is called; returns TRUE if
    // requested path was successfully listed;
    // NOTE: when FS path is specified, attempt is made to open path in this order: in FS
    // in panel, in detached FS, or in new FS (for FS from panel and detached FS it checks
    // if plugin-fs-name matches and if FS method IsOurPath returns TRUE for specified path);
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPath(int panel, const char* path, int* failReason = NULL,
                                        int suggestedTopIndex = -1,
                                        const char* suggestedFocusName = NULL,
                                        BOOL convertFSPathToInternal = TRUE) = 0;

    // changes path in panel to relative or absolute UNC (\\server\share\path) or normal (c:\path)
    // path, if new path is not available, tries to succeed with shortened paths; if changing
    // path within one disk (including archives on this disk) and accessible path cannot be found
    // on disk, changes path to root of first local fixed drive (good chance of success,
    // panel won't remain empty); when translating relative path to absolute, path in
    // panel 'panel' is preferred (only if it's path to disk (including to archive), otherwise not used); 'panel' is
    // one of PANEL_XXX; 'path' is new path; if 'suggestedTopIndex' is not -1, it will be set as
    // top-index in panel (only for new path, not set on shortened (changed) path); if
    // 'suggestedFocusName' is not NULL, it will try to find (ignore-case) and focus item with same name
    // (only for new path, not performed on shortened (changed) path); if 'failReason' is not NULL,
    // it is set to one of constants CHPPFR_XXX (informs about method result); returns TRUE if
    // requested path was successfully listed (not shortened/not changed)
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToDisk(int panel, const char* path, int* failReason = NULL,
                                              int suggestedTopIndex = -1,
                                              const char* suggestedFocusName = NULL) = 0;

    // changes path in panel to archive, 'archiv' is relative or absolute UNC
    // (\\server\share\path\file) or normal (c:\path\file) archive name, 'archivePath' is path
    // inside archive, if new path in archive is not available, tries to succeed with shortened paths;
    // when translating relative path to absolute, path in panel 'panel' is preferred
    // (only if it's path to disk (including to archive), otherwise not used); 'panel' is one of PANEL_XXX;
    // if 'suggestedTopIndex' is not -1, it will be set as top-index in panel (only for new
    // path, not set on shortened (changed) path); if 'suggestedFocusName' is not NULL,
    // it will try to find (ignore-case) and focus item with same name (only for new path,
    // not performed on shortened (changed) path); if 'forceUpdate' is TRUE and changing path
    // inside archive 'archive' (archive is already open in panel), test for archive file change
    // is performed (size & time check) and in case of change archive is closed (risk of update of edited
    // files) and listed again or if file ceased to exist, path change to disk is performed
    // (archive closure; if disk path is not accessible, changes path to root of first local
    // fixed drive); if 'forceUpdate' is FALSE, path changes inside archive 'archive' are performed without
    // archive file check; if 'failReason' is not NULL, it is set to one of constants
    // CHPPFR_XXX (informs about method result); returns TRUE if requested path was
    // successfully listed (not shortened/not changed)
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToArchive(int panel, const char* archive, const char* archivePath,
                                                 int* failReason = NULL, int suggestedTopIndex = -1,
                                                 const char* suggestedFocusName = NULL,
                                                 BOOL forceUpdate = FALSE) = 0;

    // changes path in panel to plugin FS, 'fsName' is FS name (see GetPluginFSName; doesn't have to
    // be from this plugin), 'fsUserPart' is path within FS; if new path in FS is not
    // available, tries to succeed with shortened paths (repeated calls to ChangePath and ListCurrentPath,
    // see CPluginFSInterfaceAbstract); if changing path within current FS (see
    // CPluginFSInterfaceAbstract::IsOurPath) and accessible path cannot be found from new path,
    // tries to find accessible path from original (current) path, and if that also fails,
    // changes path to root of first local fixed drive (good chance of success, panel won't remain empty);
    // 'panel' is one of PANEL_XXX; if 'suggestedTopIndex' is not -1, it will be set as top-index
    // in panel (only for new path, not set on shortened (changed) path); if
    // 'suggestedFocusName' is not NULL, it will try to find (ignore-case) and focus item with same name
    // (only for new path, not performed on shortened (changed) path); if 'forceUpdate' is TRUE,
    // case of changing path to current path in panel is not optimized (path is normally listed)
    // (either new path matches current path directly or was shortened to it by first ChangePath
    // call); if 'convertPathToInternal' is TRUE, plugin is found by 'fsName' and its
    // method CPluginInterfaceForFSAbstract::ConvertPathToInternal() is called for 'fsUserPart';
    // if 'failReason' is not NULL, it is set to one of constants CHPPFR_XXX (informs
    // about method result); returns TRUE if requested path was successfully listed
    // (not shortened/not changed)
    // NOTE: if you need FS path to be tried to open in detached FS too, use method
    // ChangePanelPath (ChangePanelPathToPluginFS ignores detached FS - works only with FS open
    // in panel or opens new FS);
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToPluginFS(int panel, const char* fsName, const char* fsUserPart,
                                                  int* failReason = NULL, int suggestedTopIndex = -1,
                                                  const char* suggestedFocusName = NULL,
                                                  BOOL forceUpdate = FALSE,
                                                  BOOL convertPathToInternal = FALSE) = 0;

    // changes path in panel to detached plugin FS (see FSE_DETACHED/FSE_ATTACHED),
    // 'detachedFS' is detached plugin FS; if current path in detached FS is not available,
    // tries to succeed with shortened paths (repeated calls to ChangePath and ListCurrentPath, see
    // CPluginFSInterfaceAbstract); 'panel' is one of PANEL_XXX; if 'suggestedTopIndex' is not -1,
    // it will be set as top-index in panel (only for new path, not set on shortened (changed) path
    // ); if 'suggestedFocusName' is not NULL, it will try to find (ignore-case) and focus
    // item with same name (only for new path, not performed on shortened (changed) path);
    // if 'failReason' is not NULL, it is set to one of constants CHPPFR_XXX (informs about method
    // result); returns TRUE if requested path was successfully listed (not shortened/not changed)
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToDetachedFS(int panel, CPluginFSInterfaceAbstract* detachedFS,
                                                    int* failReason = NULL, int suggestedTopIndex = -1,
                                                    const char* suggestedFocusName = NULL) = 0;

    // changes path in panel to root of first local fixed drive, this is almost certain change
    // of current path in panel; 'panel' is one of PANEL_XXX; if 'failReason' is not NULL,
    // it is set to one of constants CHPPFR_XXX (informs about method result); returns
    // TRUE if root of first local fixed drive was successfully listed
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (risk of e.g. closing FS opened in panel - 'this' might cease to exist for the method)
    virtual BOOL WINAPI ChangePanelPathToFixedDrive(int panel, int* failReason = NULL) = 0;

    // performs path refresh in panel (reloads listing and transfers selection, icons, focus, etc.
    // to new panel content); disk and FS paths are always reloaded, archive paths
    // are reloaded only if archive file changed (size & time check) or if
    // 'forceRefresh' is TRUE; thumbnails on disk paths are reloaded only when file
    // size changes or last write date/time changes or if
    // 'forceRefresh' is TRUE; 'panel' is one of PANEL_XXX; if 'focusFirstNewItem' is TRUE and
    // only one item was added to panel, this new item will be focused (used e.g.
    // to focus newly created file/directory)
    // restriction: main thread and moreover only outside methods CPluginFSInterfaceAbstract and
    // CPluginDataInterfaceAbstract (risk of e.g. closing FS opened in panel - 'this' might
    // cease to exist for the method)
    virtual void WINAPI RefreshPanelPath(int panel, BOOL forceRefresh = FALSE,
                                         BOOL focusFirstNewItem = FALSE) = 0;

    // posts message to panel that path refresh should be performed (reloads listing and
    // transfers selection, icons, focus, etc. to new panel content); refresh is performed when
    // Salamander's main window is activated (when suspend-mode ends); disk
    // and FS paths are always reloaded, archive paths are reloaded only if archive
    // file changed (size & time check); 'panel' is one of PANEL_XXX; if
    // 'focusFirstNewItem' is TRUE and only one item was added to panel, this
    // new item will be focused (used e.g. to focus newly created file/directory)
    // can be called from any thread (if main thread is not running code inside plugin,
    // refresh will occur as soon as possible, otherwise refresh will wait at least until
    // main thread exits plugin)
    virtual void WINAPI PostRefreshPanelPath(int panel, BOOL focusFirstNewItem = FALSE) = 0;

    // posts message to panel with active FS 'modifiedFS' that
    // path refresh should be performed (reloads listing and transfers selection, icons, focus, etc. to
    // new panel content); refresh is performed when Salamander's main window is activated
    // (when suspend-mode ends); FS path is always reloaded; if 'modifiedFS' is not in any
    // panel, nothing is done; if 'focusFirstNewItem' is TRUE and only one item was added
    // to panel, this new item will be focused (used e.g. to focus newly created
    // file/directory);
    // NOTE: there is also PostRefreshPanelFS2, which returns TRUE if refresh was performed,
    // FALSE if 'modifiedFS' was not found in any panel;
    // can be called from any thread (if main thread is not running code inside plugin,
    // refresh will occur as soon as possible, otherwise refresh will wait at least until
    // main thread exits plugin)
    virtual void WINAPI PostRefreshPanelFS(CPluginFSInterfaceAbstract* modifiedFS,
                                           BOOL focusFirstNewItem = FALSE) = 0;

    // closes detached plugin FS (if it allows, see CPluginFSInterfaceAbstract::TryCloseOrDetach),
    // 'detachedFS' is detached plugin FS; returns TRUE on success (FALSE means detached
    // plugin FS was not closed); 'parent' is parent of possible messageboxes (so far can be opened only by
    // CPluginFSInterfaceAbstract::ReleaseObject)
    // Note: plugin FS opened in panel is closed e.g. using ChangePanelPathToRescuePathOrFixedDrive
    // restriction: main thread + outside methods CPluginFSInterfaceAbstract (attempting to close
    // detached FS - 'this' might cease to exist for the method)
    virtual BOOL WINAPI CloseDetachedFS(HWND parent, CPluginFSInterfaceAbstract* detachedFS) = 0;

    // doubles '&' - useful for paths displayed in menu ('&&' is displayed as '&');
    // 'buffer' is input/output string, 'bufferSize' is size of 'buffer' in bytes;
    // returns TRUE if doubling did not cause loss of characters from end of string (buffer was large
    // enough)
    // can be called from any thread
    virtual BOOL WINAPI DuplicateAmpersands(char* buffer, int bufferSize) = 0;

    // removes '&' from text; if it finds pair "&&", replaces it with single '&' character
    // can be called from any thread
    virtual void WINAPI RemoveAmpersands(char* text) = 0;

    // ValidateVarString and ExpandVarString:
    // methods for validating and expanding strings with variables in form "$(var_name)", "$(var_name:num)"
    // (num is variable width, it's numeric value from 1 to 9999), "$(var_name:max)" ("max" is
    // symbol that indicates variable width is determined by value in 'maxVarWidths' array, details
    // in ExpandVarString) and "$[env_var]" (expands environment variable value); used when
    // user can specify string format (such as in info-line) example string with variables:
    // "$(files) files and $(dirs) directories" - variables 'files' and 'dirs';
    // source code for use in info-line (without variables in form "$(varname:max)") is in DEMOPLUG
    //
    // checks syntax of 'varText' (string with variables), returns FALSE if it finds error, its
    // position is placed in 'errorPos1' (offset of error start) and 'errorPos2' (offset of error end
    // ); 'variables' is array of CSalamanderVarStrEntry structures, which is terminated by structure with
    // Name==NULL; 'msgParent' is parent of message-box with errors, if NULL, errors are not displayed
    virtual BOOL WINAPI ValidateVarString(HWND msgParent, const char* varText, int& errorPos1, int& errorPos2,
                                          const CSalamanderVarStrEntry* variables) = 0;
    //
    // fills 'buffer' with result of expansion of 'varText' (string with variables), returns FALSE if
    // 'buffer' is small (assumes string validation via ValidateVarString, otherwise
    // returns FALSE also on syntax error) or user clicked Cancel on environment-variable error
    // (not found or too large); 'bufferLen' is size of buffer 'buffer';
    // 'variables' is array of CSalamanderVarStrEntry structures, which is terminated by structure
    // with Name==NULL; 'param' is pointer passed to CSalamanderVarStrEntry::Execute
    // when expanding found variable; 'msgParent' is parent of message-box with errors, if NULL,
    // errors are not displayed; if 'ignoreEnvVarNotFoundOrTooLong' is TRUE, errors of
    // environment-variable (not found or too large) are ignored, if FALSE, messagebox
    // with error is displayed; if 'varPlacements' is not NULL, it points to DWORD array with '*varPlacementsCount' items,
    // which will be filled with DWORDs composed of variable position in output buffer (lower WORD)
    // and variable character count (upper WORD); if 'varPlacementsCount' is not NULL, it returns count of
    // filled items in 'varPlacements' array (actually number of variables in input
    // string);
    // if this method is used only for string expansion for one 'param' value,
    // 'detectMaxVarWidths' should be set to FALSE, 'maxVarWidths' to NULL and 'maxVarWidthsCount'
    // to 0; however if this method is used for repeated string expansion for certain
    // set of 'param' values (e.g. in Make File List it's row expansion successively for all
    // selected files and directories), it makes sense to use variables in form "$(varname:max)",
    // for these variables width is determined as largest width of expanded variable within entire
    // set of values; measurement of largest width of expanded variable is performed in first cycle
    // (for all values in set) of ExpandVarString calls, in first cycle parameter
    // 'detectMaxVarWidths' has value TRUE and 'maxVarWidths' array with 'maxVarWidthsCount' items
    // is pre-zeroed (serves for storing maxima between individual ExpandVarString calls);
    // actual expansion then occurs in second cycle (for all values in set) of
    // ExpandVarString calls, in second cycle parameter 'detectMaxVarWidths' has value FALSE and
    // 'maxVarWidths' array with 'maxVarWidthsCount' items contains pre-calculated largest widths
    // (from first cycle)
    virtual BOOL WINAPI ExpandVarString(HWND msgParent, const char* varText, char* buffer, int bufferLen,
                                        const CSalamanderVarStrEntry* variables, void* param,
                                        BOOL ignoreEnvVarNotFoundOrTooLong = FALSE,
                                        DWORD* varPlacements = NULL, int* varPlacementsCount = NULL,
                                        BOOL detectMaxVarWidths = FALSE, int* maxVarWidths = NULL,
                                        int maxVarWidthsCount = 0) = 0;

    // sets flag load-on-salamander-start (load plugin on Salamander start?) of plugin;
    // 'start' is new flag value; returns old flag value; if SetFlagLoadOnSalamanderStart was never called
    // , flag is set to FALSE (plugin is not loaded on start, but
    // only when needed)
    // restriction: main thread (otherwise changes in plugin configuration may occur during call)
    virtual BOOL WINAPI SetFlagLoadOnSalamanderStart(BOOL start) = 0;

    // sets flag for calling plugin that it should unload at next possible opportunity
    // (once all posted menu commands are executed (see PostMenuExtCommand), there are no
    // messages in main thread message-queue and Salamander is not "busy");
    // WARNING: if called from thread other than main, unload request may occur (runs
    // in main thread) even before PostUnloadThisPlugin finishes (more information about
    // unload - see CPluginInterfaceAbstract::Release)
    // can be called from any thread (but only after plugin entry-point finishes, while
    // entry-point is running, method can only be called from main thread)
    virtual void WINAPI PostUnloadThisPlugin() = 0;

    // returns Salamander modules successively (executable file and .spl files of installed
    // plugins, all including versions); 'index' is input/output variable, points to int,
    // which is 0 on first call, value for next call is stored by function on return
    // (usage: zero at start, then don't change); 'module' is buffer for module name
    // (min. size MAX_PATH characters); 'version' is buffer for module version (min. size
    // MAX_PATH characters); returns FALSE if 'module' + 'version' is not filled and there is no
    // more module, returns TRUE if 'module' + 'version' contains next module
    // restriction: main thread (otherwise changes in plugin configuration may occur during
    // call - add/remove)
    virtual BOOL WINAPI EnumInstalledModules(int* index, char* module, char* version) = 0;

    // calls 'loadOrSaveFunc' for load or save configuration; if 'load' is TRUE, it's configuration
    // load, if plugin supports "load/save configuration" and at time of call
    // plugin's private registry key exists, 'loadOrSaveFunc' is called for this key, otherwise
    // default configuration load is called ('regKey' parameter of 'loadOrSaveFunc' function is NULL);
    // if 'load' is FALSE, it's configuration save, 'loadOrSaveFunc' is called only if
    // plugin supports "load/save configuration" and at time of call Salamander's key exists;
    // 'param' is user parameter and is passed to 'loadOrSaveFunc'
    // restriction: main thread, in entry-point can be called only after SetBasicPluginData,
    // before that it may not be known if support for "load/save configuration" exists and name
    // of private registry key
    virtual void WINAPI CallLoadOrSaveConfiguration(BOOL load, FSalLoadOrSaveConfiguration loadOrSaveFunc,
                                                    void* param) = 0;

    // saves 'text' of length 'textLen' (-1 means "use strlen") to clipboard both as multibyte,
    // and Unicode (otherwise e.g. Notepad doesn't handle Czech), on success can (if 'echo' is TRUE)
    // display message "Text was successfully copied to clipboard." (messagebox parent will be
    // 'echoParent'); returns TRUE on success
    // can be called from any thread
    virtual BOOL WINAPI CopyTextToClipboard(const char* text, int textLen, BOOL showEcho, HWND echoParent) = 0;

    // saves unicode 'text' of length 'textLen' (-1 means "use wcslen") to clipboard both as
    // unicode and multibyte (otherwise e.g. MSVC6.0 doesn't handle Czech), on success can (if
    // 'echo' is TRUE) display message "Text was successfully copied to clipboard." (messagebox parent
    // will be 'echoParent'); returns TRUE on success
    // can be called from any thread
    virtual BOOL WINAPI CopyTextToClipboardW(const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent) = 0;

    // executes menu command with identification number 'id' in main thread (calls
    // CPluginInterfaceForMenuExtAbstract::ExecuteMenuItem(salamander, main-window-hwnd, 'id', 0),
    // 'salamander' is NULL if 'waitForSalIdle' is FALSE, otherwise contains pointer to valid
    // set of usable Salamander methods for performing operations; return value
    // is ignored); if 'waitForSalIdle' is FALSE, message posted to
    // main window is used for execution (this message is delivered by every running message-loop in main thread - including modal
    // dialogs/messageboxes, including those opened by plugin), so multiple entry
    // to plugin is possible; if 'waitForSalIdle' is TRUE, 'id' is limited to interval <0, 999999> and
    // command is executed once there are no messages in main thread message-queue and Salamander
    // is not "busy" (no modal dialog is open and no message is being processed);
    // WARNING: if called from thread other than main, menu command may execute
    // (runs in main thread) even before PostMenuExtCommand finishes
    // can be called from any thread and if 'waitForSalIdle' is FALSE, must wait until after calling
    // CPluginInterfaceAbstract::GetInterfaceForMenuExt (called after entry-point from main thread)
    virtual void WINAPI PostMenuExtCommand(int id, BOOL waitForSalIdle) = 0;

    // determines if there is good chance (cannot be determined for certain) that Salamander in next few
    // moments will not be "busy" (no modal dialog will be open and no message will be processed
    // ) - in this case returns TRUE (otherwise FALSE); if 'lastIdleTime' is not NULL,
    // it returns GetTickCount() from moment of last transition from "idle" to "busy" state;
    // can be used e.g. as prediction for command delivery posted using
    // CSalamanderGeneralAbstract::PostMenuExtCommand with parameter 'waitForSalIdle'==TRUE;
    // can be called from any thread
    virtual BOOL WINAPI SalamanderIsNotBusy(DWORD* lastIdleTime) = 0;

    // sets message that Bug Report dialog should display if crash occurs inside plugin
    // (inside plugin = at least one call-stack-message saved from plugin) and allows redefining
    // standard bug-report email address (support@altap.cz); 'message' is new message
    // (NULL means "no message"); 'email' is new email address (NULL means "use
    // standard"; max. email length is 100 characters); this method can be called repeatedly, original
    // message and email are overwritten; Salamander doesn't remember message or
    // email for next run, so this method must always be called again when loading plugin (preferably in entry-point)
    //
    // restriction: main thread (otherwise changes in plugin configuration may occur during call)
    virtual void WINAPI SetPluginBugReportInfo(const char* message, const char* email) = 0;

    // determines if plugin is installed (however doesn't determine if it can be loaded - whether
    // user e.g. deleted it only from disk); 'pluginSPL' identifies plugin - it's required
    // ending part of full path to plugin .SPL file (e.g. "ieviewer\\ieviewer.spl" identifies
    // IEViewer supplied with Salamander); returns TRUE if plugin is installed
    // restriction: main thread (otherwise changes in plugin configuration may occur during call)
    virtual BOOL WINAPI IsPluginInstalled(const char* pluginSPL) = 0;

    // opens file in viewer implemented in plugin or internal text/hex viewer;
    // if 'pluginSPL' is NULL, internal text/hex viewer should be used, otherwise identifies plugin
    // viewer - it's required ending part of full path to plugin .SPL file (e.g.
    // "ieviewer\\ieviewer.spl" identifies IEViewer supplied with Salamander); 'pluginData'
    // is data structure containing name of viewed file and optionally also contains extended
    // viewer parameters (see description of CSalamanderPluginViewerData); if 'useCache' is FALSE
    // 'rootTmpPath' and 'fileNameInCache' are ignored and only file opening in viewer occurs;
    // if 'useCache' is TRUE, file will first be moved to disk cache under file name
    // 'fileNameInCache' (name without path), then opened in viewer and after viewer closes will be
    // removed from disk cache, if file 'pluginData->FileName' is on same disk as
    // disk cache, move will be immediate, otherwise copying between volumes occurs, which may
    // take longer time, but no progress is shown (if 'rootTmpPath' is NULL, disk
    // cache is in Windows TEMP directory, otherwise path to disk cache is in 'rootTmpPath'; for move
    // to disk cache SalMoveFile is used), ideal is using SalGetTempFileName
    // with parameter 'path' equal to 'rootTmpPath'; returns TRUE on successful file opening in
    // viewer; returns FALSE if error occurs during opening (specific reason is stored in
    // 'error' - 0 - success, 1 - cannot load plugin, 2 - ViewFile from plugin returned
    // error, 3 - cannot move file to disk cache), if 'useCache' is TRUE,
    // file is removed from disk (as after viewer closes)
    // restriction: main thread (otherwise changes in plugin configuration may occur during call),
    // moreover cannot be called from entry-point (plugin load is not reentrant)
    virtual BOOL WINAPI ViewFileInPluginViewer(const char* pluginSPL,
                                               CSalamanderPluginViewerData* pluginData,
                                               BOOL useCache, const char* rootTmpPath,
                                               const char* fileNameInCache, int& error) = 0;

    // informs Salamander as soon as possible, then all loaded plugins and then all open
    // FS (in panels and detached) about change on path 'path' (disk or FS path); important for
    // paths where changes cannot be monitored automatically (see FindFirstChangeNotification) or
    // user disabled this monitoring (auto-refresh), for FS the plugin ensures change monitoring
    // itself; change notification occurs as soon as possible (if main thread is not running code
    // inside plugin, refresh occurs after message delivery to main window and possibly after
    // re-enabling refresh (after closing dialog, etc.), otherwise refresh waits at least until
    // moment when main thread exits plugin); 'includingSubdirs' is TRUE if change can
    // manifest in subdirectories of 'path' as well;
    // WARNING: if called from thread other than main, change notification can occur
    // (runs in main thread) even before PostChangeOnPathNotification finishes
    // can be called from any thread
    virtual void WINAPI PostChangeOnPathNotification(const char* path, BOOL includingSubdirs) = 0;

    // tries access to windows path 'path' (normal or UNC), runs in worker thread, thus
    // allows interrupting test with ESC key (after certain time displays window with ESC message)
    // 'echo' TRUE means enabled error message output (if path is not accessible);
    // 'err' different from ERROR_SUCCESS in combination with 'echo' TRUE only displays error (path
    // is no longer accessed); 'parent' is parent of messagebox; returns ERROR_SUCCESS if
    // path is okay, otherwise returns standard windows error code or ERROR_USER_TERMINATED
    // if user used ESC key to interrupt test
    // restriction: main thread (repeated call is not possible and main thread uses this method)
    virtual DWORD WINAPI SalCheckPath(BOOL echo, const char* path, DWORD err, HWND parent) = 0;

    // tries if windows path 'path' is accessible, possibly restores network connections (if it's
    // a normal path, tries to revive remembered network connection, if it's UNC path, allows login
    // with new username and password); returns TRUE if path is accessible; 'parent' is parent
    // of messagebox and dialog; 'tryNet' is TRUE if it makes sense to try restoring network connections
    // (with FALSE degrades to SalCheckPath; is here only for optimization possibility)
    // restriction: main thread (repeated call is not possible and main thread uses this method)
    virtual BOOL WINAPI SalCheckAndRestorePath(HWND parent, const char* path, BOOL tryNet) = 0;

    // more complex variant of SalCheckAndRestorePath method; tries if windows path 'path' is
    // accessible, possibly shortens it; if 'tryNet' is TRUE, tries to restore network connection and sets
    // 'tryNet' to FALSE (if it's a normal path, tries to revive remembered network connection, if
    // it's UNC path, allows login with new username and password); if 'donotReconnect' is
    // TRUE, only error is determined, connection restoration is not performed; returns 'err' (windows error code of
    // current path), 'lastErr' (error code leading to path shortening), 'pathInvalid' (TRUE if
    // network connection restoration was attempted without success), 'cut' (TRUE if resulting path is shortened);
    // 'parent' is parent of messagebox; returns TRUE if resulting path 'path' is accessible
    // restriction: main thread (repeated call is not possible and main thread uses this method)
    virtual BOOL WINAPI SalCheckAndRestorePathWithCut(HWND parent, char* path, BOOL& tryNet, DWORD& err,
                                                      DWORD& lastErr, BOOL& pathInvalid, BOOL& cut,
                                                      BOOL donotReconnect) = 0;

    // recognizes what type of path (FS/windows/archive) it is and handles splitting into
    // its parts (for FS it's fs-name and fs-user-part, for archive it's path-to-archive and
    // path-in-archive, for windows paths it's existing part and remainder of path), for FS paths
    // nothing is checked, for windows (normal + UNC) paths it checks how far path exists
    // (possibly restores network connection), for archive it checks existence of archive file
    // (archive recognition by extension);
    // 'path' is full or relative path (buffer min. 'pathBufSize' chars; for relative paths
    // current path 'curPath' (if not NULL) is considered as base for evaluating full path;
    // 'curPathIsDiskOrArchive' is TRUE if 'curPath' is windows or archive path;
    // if current path is archive, 'curArchivePath' contains archive name, otherwise is NULL),
    // resulting full path is stored in 'path' (must be min. 'pathBufSize' chars); returns TRUE on
    // successful recognition, then 'type' is path type (see PATH_TYPE_XXX) and 'secondPart' is set:
    // - in 'path' to position after existing path (after '\\' or at end of string; if
    //   file exists in path, points after path to this file) (windows path type), WARNING: does not handle
    //   length of returned path part (entire path may be longer than MAX_PATH)
    // - after archive file (archive path type), WARNING: does not handle archive path length (may be
    //   longer than MAX_PATH)
    // - after ':' after file-system name - user-part of file-system path (FS path type), WARNING: does not handle
    //   user-part path length (may be longer than MAX_PATH);
    // if returns TRUE, 'isDir' is also set to:
    // - TRUE if existing path part is directory, FALSE == file (windows path type)
    // - FALSE for archive and FS path types;
    // if returns FALSE, error was displayed to user (except one exception - see SPP_INCOMLETEPATH description),
    // which occurred during recognition (if 'error' is not NULL, one of SPP_XXX constants is returned in it);
    // 'errorTitle' is messagebox title for error; if 'nextFocus' != NULL and windows/archive
    // path does not contain '\\' or only ends with '\\', path is copied to 'nextFocus' (see SalGetFullName);
    // WARNING: uses SalGetFullName, so it's good to call method
    //        CSalamanderGeneralAbstract::SalUpdateDefaultDir beforehand
    // restriction: main thread (repeated call is not possible and main thread uses this method)
    virtual BOOL WINAPI SalParsePath(HWND parent, char* path, int& type, BOOL& isDir, char*& secondPart,
                                     const char* errorTitle, char* nextFocus, BOOL curPathIsDiskOrArchive,
                                     const char* curPath, const char* curArchivePath, int* error,
                                     int pathBufSize) = 0;

    // gets existing part and operation mask from windows target path; allows creating any non-existing part;
    // on success returns TRUE and existing windows target path (in 'path')
    // and found operation mask (in 'mask' - points to 'path' buffer, but path and mask are separated by
    // zero; if mask is not in path, automatically creates mask "*.*"); 'parent' - parent of any
    // messageboxes; 'title' + 'errorTitle' are messagebox titles for information + error; 'selCount' is
    // count of selected files and directories; 'path' is on input target path to process, on output
    // (at least 2 * MAX_PATH chars) existing target path; 'secondPart' points into 'path' to position
    // after existing path (after '\\' or to end of string; if file exists in path, points after path
    // to this file); 'pathIsDir' is TRUE/FALSE if existing path part is directory/file;
    // 'backslashAtEnd' is TRUE if there was backslash at end of 'path' before performing "parse" (e.g.
    // SalParsePath removes such backslash); 'dirName' + 'curDiskPath' are not NULL if
    // max. one file/directory is selected (its name without path is in 'dirName'; if nothing is selected, takes
    // focus) and current path is windows (path is in 'curDiskPath'); 'mask' is on output
    // pointer to operation mask in 'path' buffer; if there is error in path, method returns FALSE,
    // problem was already reported to user
    // can be called from any thread
    virtual BOOL WINAPI SalSplitWindowsPath(HWND parent, const char* title, const char* errorTitle,
                                            int selCount, char* path, char* secondPart, BOOL pathIsDir,
                                            BOOL backslashAtEnd, const char* dirName,
                                            const char* curDiskPath, char*& mask) = 0;

    // gets existing part and operation mask from target path; recognizes any non-existing part; on
    // success returns TRUE, relative path to create (in 'newDirs'), existing target path (in 'path';
    // existing only assuming creation of relative path 'newDirs') and found operation mask
    // (in 'mask' - points to 'path' buffer, but path and mask are separated by zero; if mask is not in path,
    // automatically creates mask "*.*"); 'parent' - parent of any messageboxes;
    // 'title' + 'errorTitle' are messagebox titles for information + error; 'selCount' is count of selected
    // files and directories; 'path' is on input target path to process, on output (at least 2 * MAX_PATH
    // chars) existing target path (always ends with backslash); 'afterRoot' points into 'path' after path root
    // (after '\\' or to end of string); 'secondPart' points into 'path' to position after existing path (after
    // '\\' or to end of string; if file exists in path, points after path to this file);
    // 'pathIsDir' is TRUE/FALSE if existing path part is directory/file; 'backslashAtEnd' is
    // TRUE if there was backslash at end of 'path' before performing "parse" (e.g. SalParsePath removes such
    // backslash); 'dirName' + 'curPath' are not NULL if max. one file/directory is selected
    // (its name without path is in 'dirName'; its path is in 'curPath'; if nothing is selected, takes
    // focus); 'mask' is on output pointer to operation mask in 'path' buffer; if 'newDirs' is not NULL,
    // it's a buffer (of size at least MAX_PATH) for relative path (relative to existing path
    // in 'path'), which must be created (user agrees with creation, same query was used as
    // for copying from disk to disk; empty string = create nothing); if 'newDirs' is NULL and
    // some relative path needs to be created, only error is displayed; 'isTheSamePathF' is function for
    // comparing two paths (needed only if 'curPath' is not NULL), if NULL, IsTheSamePath is used;
    // if there is error in path, method returns FALSE, problem was already reported to user
    // can be called from any thread
    virtual BOOL WINAPI SalSplitGeneralPath(HWND parent, const char* title, const char* errorTitle,
                                            int selCount, char* path, char* afterRoot, char* secondPart,
                                            BOOL pathIsDir, BOOL backslashAtEnd, const char* dirName,
                                            const char* curPath, char*& mask, char* newDirs,
                                            SGP_IsTheSamePathF isTheSamePathF) = 0;

    // removes ".." (skips ".." together with one subdirectory to the left) and "." (skips only ".")
    // from path; condition is backslash as subdirectory separator; 'afterRoot' points after root
    // of processed path (path changes occur only after 'afterRoot'); returns TRUE if modifications
    // succeeded, FALSE if ".." cannot be removed (root is already on the left)
    // can be called from any thread
    virtual BOOL WINAPI SalRemovePointsFromPath(char* afterRoot) = 0;

    // returns parameter from Salamander configuration; 'paramID' identifies which parameter it is
    // (see SALCFG_XXX constants); 'buffer' points to buffer where parameter
    // data will be copied, buffer size is 'bufferSize'; if 'type' is not NULL, one of
    // SALCFGTYPE_XXX constants or SALCFGTYPE_NOTFOUND is returned in it (if parameter with
    // 'paramID' was not found); returns TRUE if 'paramID' is valid and also if
    // configuration parameter value fits in 'buffer'
    // note: changes in Salamander configuration are reported via event
    //       PLUGINEVENT_CONFIGURATIONCHANGED (see method CPluginInterfaceAbstract::Event)
    // restriction: main thread, configuration changes occur only in main thread (contains no other
    //          synchronization)
    virtual BOOL WINAPI GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type) = 0;

    // changes letter case in file name (name without path); 'tgtName' is buffer for result
    // (size is min. for storing 'srcName' string); 'srcName' is file name (is written to,
    // but is always restored before returning from method); 'format' is result format (1 - capital initial
    // letters of words, 2 - complete lowercase, 3 - complete uppercase, 4 - no changes, 5 - if
    // DOS name (8.3) -> capital initial letters of words, 6 - file lowercase, directory uppercase,
    // 7 - capital initial letters in name and lowercase in extension);
    // 'changedParts' determines which parts of name should be changed (0 - changes name and extension, 1 - changes only
    // name (possible only with format == 1, 2, 3, 4), 2 - changes only extension (possible only with
    // format == 1, 2, 3, 4)); 'isDir' is TRUE if it's directory name
    // can be called from any thread
    virtual void WINAPI AlterFileName(char* tgtName, char* srcName, int format, int changedParts,
                                      BOOL isDir) = 0;

    // shows/hides message in window in its own thread (does not drain message-queue); shows
    // only one message at a time, repeated call reports error to TRACE (not fatal);
    // NOTE: used in SalCheckPath and other routines, so collision can occur between
    //       window opening requests (not fatal, just won't display)
    // everything can be called from any thread (but window must be serviced only
    // from one thread - cannot show from one thread and hide from another)
    //
    // opens window with text 'message' with delay 'delay' (in ms), only if 'hForegroundWnd' is NULL
    // or identifies foreground window
    // 'message' can be multi-line; individual lines are separated by '\n' character
    // 'caption' can be NULL: then "Open Salamander" will be used
    // 'showCloseButton' specifies whether window will contain Close button; equivalent to Escape key
    virtual void WINAPI CreateSafeWaitWindow(const char* message, const char* caption,
                                             int delay, BOOL showCloseButton, HWND hForegroundWnd) = 0;
    // closes window
    virtual void WINAPI DestroySafeWaitWindow() = 0;
    // hiding/showing window (if open); call as reaction to WM_ACTIVATE from hForegroundWnd window:
    //    case WM_ACTIVATE:
    //    {
    //      ShowSafeWaitWindow(LOWORD(wParam) != WA_INACTIVE);
    //      break;
    //    }
    // If thread (from which window was created) is busy, message
    // distribution does not occur, so WM_ACTIVATE is not delivered when clicking
    // on another application. Messages are delivered at moment of showing messagebox,
    // which is exactly what we need: we temporarily hide and later (after closing
    // messagebox and activating hForegroundWnd window) show again.
    virtual void WINAPI ShowSafeWaitWindow(BOOL show) = 0;
    // after calling CreateSafeWaitWindow or ShowSafeWaitWindow function returns FALSE until
    // user clicks with mouse on Close button (if displayed); then returns TRUE
    virtual BOOL WINAPI GetSafeWaitWindowClosePressed() = 0;
    // serves for additional text change in window
    // WARNING: no window re-layout occurs and if text expands significantly
    // it will be clipped; use for example for countdown: 60s, 55s, 50s, ...
    virtual void WINAPI SetSafeWaitWindowText(const char* message) = 0;

    // finds existing file copy in disk-cache and locks it (prevents its deletion); 'uniqueFileName'
    // is unique name of original file (disk-cache is searched by this name; full file name in Salamander
    // form should suffice - "fs-name:fs-user-part"; WARNING: name is compared
    // "case-sensitive", if plugin requires "case-insensitive", it must convert all names
    // e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); in 'tmpName'
    // pointer is returned (valid until file copy deletion from disk-cache) to full name of file copy,
    // which is located in temporary directory; 'fileLock' is file copy lock, it's a system event
    // in nonsignaled state, which transitions to signaled state after processing file copy (must
    // use UnlockFileInCache method; plugin signals that copy in disk-cache can now be deleted);
    // if copy was not found returns FALSE and 'tmpName' NULL (otherwise returns TRUE)
    // can be called from any thread
    virtual BOOL WINAPI GetFileFromCache(const char* uniqueFileName, const char*& tmpName, HANDLE fileLock) = 0;

    // unlocks file copy lock in disk-cache (sets 'fileLock' to signaled state, requests
    // disk-cache to perform lock check, and then sets 'fileLock' back to nonsignaled state);
    // if it was last lock, copy may be deleted, when copy deletion occurs depends
    // on disk-cache size on disk; lock can be used for multiple file copies (lock
    // must be of "manual reset" type, otherwise after unlocking first copy lock is set to
    // nonsignaled state and unlocking ends), in this case unlocking occurs for all copies
    // can be called from any thread
    virtual void WINAPI UnlockFileInCache(HANDLE fileLock) = 0;

    // inserts (moves) file copy to disk-cache (inserted copy is not locked, so it can be deleted at any time);
    // 'uniqueFileName' is unique name of original file (disk-cache is searched by this
    // name; full file name in Salamander form should suffice - "fs-name:fs-user-part"; WARNING: name
    // is compared "case-sensitive", if plugin requires "case-insensitive", it must convert all names
    // e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); 'nameInCache' is name of file copy
    // which will be placed in temporary directory (last part of original file name is expected here,
    // to later remind user of original file); 'newFileName' is full name of stored file copy,
    // which will be moved to disk-cache under name 'nameInCache', must be located on same disk
    // as disk cache (if 'rootTmpPath' is NULL, disk cache is in Windows TEMP directory, otherwise
    // path to disk-cache is in 'rootTmpPath'; for renaming to disk cache via Win32 API function
    // MoveFile); 'newFileName' is ideally obtained by calling SalGetTempFileName with 'path' parameter equal to
    // 'rootTmpPath'); in 'newFileSize' is size of stored file copy; returns TRUE on success
    // (file was moved to disk-cache - disappeared from original location on disk), returns FALSE on
    // internal error or if file is already in disk-cache (if 'alreadyExists' is not NULL, returns
    // TRUE in it if file is already in disk-cache)
    // NOTE: if plugin uses disk-cache, it should at least on plugin unload call
    //       CSalamanderGeneralAbstract::RemoveFilesFromCache("fs-name:"), otherwise its
    //       file copies will unnecessarily linger in disk-cache
    // can be called from any thread
    virtual BOOL WINAPI MoveFileToCache(const char* uniqueFileName, const char* nameInCache,
                                        const char* rootTmpPath, const char* newFileName,
                                        const CQuadWord& newFileSize, BOOL* alreadyExists) = 0;

    // removes file copy from disk-cache, whose unique name is 'uniqueFileName' (WARNING: name
    // is compared "case-sensitive", if plugin requires "case-insensitive", it must convert all names
    // e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase); if copy
    // is still in use, it will be removed when possible (when viewers are closed), in any case
    // disk-cache will no longer provide it as valid file copy (is marked as out-of-date)
    // can be called from any thread
    virtual void WINAPI RemoveOneFileFromCache(const char* uniqueFileName) = 0;

    // removes from disk-cache all file copies whose unique names start with 'fileNamesRoot'
    // (used when closing file-system, when it's no longer desirable to cache downloaded copies
    // of files; WARNING: names are compared "case-sensitive", if plugin requires "case-insensitive",
    // it must convert all names e.g. to lowercase - see CSalamanderGeneralAbstract::ToLowerCase);
    // if file copies are still in use, they will be removed when possible (when unlocked
    // e.g. after closing viewer), in any case disk-cache will no longer provide them as valid
    // file copies (are marked as out-of-date)
    // can be called from any thread
    virtual void WINAPI RemoveFilesFromCache(const char* fileNamesRoot) = 0;

    // returns conversion tables successively (loaded from file convert\XXX\convert.cfg
    // in Salamander installation - XXX is currently used conversion tables directory);
    // 'parent' is parent of messagebox (if NULL, parent is main window);
    // 'index' is input/output variable, points to int, which is 0 on first call,
    // value for next call is stored by function on return (usage: zero at start, then
    // don't change); returns FALSE, if no more table exists; if returns TRUE, contains
    // 'name' (if not NULL) reference to conversion name (may contain '&' - underlined character in menu) or NULL
    // if it's separator and 'table' (if not NULL) reference to 256-byte conversion table or NULL
    // if it's separator; references 'name' and 'table' are valid for entire Salamander runtime (no
    // need to copy content)
    // WARNING: use 'table' pointer this way (necessary cast to "unsigned"):
    //        *s = table[(unsigned char)*s]
    // can be called from any thread
    virtual BOOL WINAPI EnumConversionTables(HWND parent, int* index, const char** name, const char** table) = 0;

    // returns conversion table 'table' (buffer min. 256 chars) for conversion 'conversion' (conversion name
    // see file convert\XXX\convert.cfg in Salamander installation, e.g. "ISO-8859-2 - CP1250";
    // characters <= ' ' and '-' and '&' in name play no role in search; searches case-insensitive);
    // 'parent' is parent of messagebox (if NULL, parent is main window); returns TRUE
    // if conversion was found (otherwise 'table' content is not valid);
    // WARNING: use this way (necessary cast to "unsigned"): *s = table[(unsigned char)*s]
    // can be called from any thread
    virtual BOOL WINAPI GetConversionTable(HWND parent, char* table, const char* conversion) = 0;

    // returns name of code page used in Windows in this region (drawn from convert\XXX\convert.cfg
    // in Salamander installation); it's normally displayable encoding, so it's used when
    // it's necessary to display text that was created in different code page (here specified as
    // "target" encoding when searching for conversion table, see GetConversionTable method);
    // 'parent' is parent of messagebox (if NULL, parent is main window); 'codePage' is buffer
    // (min. 101 chars) for code page name (if this name is not defined in convert\XXX\convert.cfg file,
    // empty string is returned in buffer)
    // can be called from any thread
    virtual void WINAPI GetWindowsCodePage(HWND parent, char* codePage) = 0;

    // determines from buffer 'pattern' of length 'patternLen' (e.g. first 10000 chars) if it's
    // text (code page exists in which it contains only allowed characters - displayable
    // and control) and if it's text, also determines its code page (most probable);
    // 'parent' is parent of messagebox (if NULL, parent is main window); if 'forceText' is
    // TRUE, check for invalid characters is not performed (used if 'pattern' contains
    // text); if 'isText' is not NULL, TRUE is returned in it if it's text; if 'codePage' is not
    // NULL, it's buffer (min. 101 chars) for code page name (most probable)
    // can be called from any thread
    virtual void WINAPI RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText,
                                          BOOL* isText, char* codePage) = 0;

    // determines from buffer 'text' of length 'textLen' if it's ANSI text (contains (in ANSI
    // character set) only allowed characters - displayable and control); decides without context
    // (doesn't matter number of characters or how they follow each other - tested text can be divided
    // into arbitrary parts and tested progressively); returns TRUE if it's ANSI text (otherwise
    // content of buffer 'text' is binary)
    // can be called from any thread
    virtual BOOL WINAPI IsANSIText(const char* text, int textLen) = 0;

    // calls function 'callback' with parameters 'param' and function for getting selected
    // files/directories (see definition of type SalPluginOperationFromDisk) from panel 'panel'
    // (windows path must be open in panel); 'panel' is one of PANEL_XXX
    // restriction: main thread
    virtual void WINAPI CallPluginOperationFromDisk(int panel, SalPluginOperationFromDisk callback,
                                                    void* param) = 0;

    // returns standard charset that user has set (part of regional
    // settings); fonts must be constructed with this charset, otherwise
    // texts may not be readable (if text is in standard code page, see Win32 API function
    // GetLocaleInfo(LOCALE_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, ...))
    // can be called from any thread
    virtual BYTE WINAPI GetUserDefaultCharset() = 0;

    // allocates new Boyer-Moore search algorithm object
    // can be called from any thread
    virtual CSalamanderBMSearchData* WINAPI AllocSalamanderBMSearchData() = 0;

    // frees Boyer-Moore search algorithm object (obtained by AllocSalamanderBMSearchData method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderBMSearchData(CSalamanderBMSearchData* data) = 0;

    // allocates new regular expression search algorithm object
    // can be called from any thread
    virtual CSalamanderREGEXPSearchData* WINAPI AllocSalamanderREGEXPSearchData() = 0;

    // frees regular expression search algorithm object (obtained by
    // AllocSalamanderREGEXPSearchData method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderREGEXPSearchData(CSalamanderREGEXPSearchData* data) = 0;

    // returns Salamander commands successively (proceeds in order of SALCMD_XXX constant definitions);
    // 'index' is input/output variable, points to int, which is 0 on first call,
    // value for next call is stored by function on return (usage: zero at start, then
    // don't change); returns FALSE, if no more command exists; if returns TRUE, contains
    // 'salCmd' (if not NULL) Salamander command number (see SALCMD_XXX constants; numbers have
    // reserved interval 0 to 499, so if Salamander commands should be in menu together with
    // other commands, it's no problem to create mutually non-overlapping sets of command values
    // e.g. by shifting all values by chosen number - example see DEMOPLUGin -
    // CPluginFSInterface::ContextMenu), 'nameBuf' (buffer of size 'nameBufSize' bytes)
    // contains command name (name is prepared for use in menu - has doubled ampersands,
    // underlined characters marked by ampersands and after '\t' has keyboard shortcut descriptions), 'enabled'
    // (if not NULL) contains command state (TRUE/FALSE if enabled/disabled), 'type'
    // (if not NULL) contains command type (see description of sctyXXX constants)
    // can be called from any thread
    virtual BOOL WINAPI EnumSalamanderCommands(int* index, int* salCmd, char* nameBuf, int nameBufSize,
                                               BOOL* enabled, int* type) = 0;

    // returns Salamander command with number 'salCmd' (see SALCMD_XXX constants);
    // returns FALSE, if such command doesn't exist; if returns TRUE, contains
    // 'nameBuf' (buffer of size 'nameBufSize' bytes) command name (name is prepared for
    // use in menu - has doubled ampersands, underlined characters marked by ampersands and after '\t' has
    // keyboard shortcut descriptions), 'enabled' (if not NULL) contains command state (TRUE/FALSE
    // if enabled/disabled), 'type' (if not NULL) contains command type (see description of
    // sctyXXX constants)
    // can be called from any thread
    virtual BOOL WINAPI GetSalamanderCommand(int salCmd, char* nameBuf, int nameBufSize, BOOL* enabled,
                                             int* type) = 0;

    // sets flag for calling plugin that Salamander command with number 'salCmd' should be executed
    // at next possible opportunity (once there are no messages in main thread message-queue
    // and Salamander is not "busy" (no modal dialog is open and no
    // message is being processed));
    // WARNING: if called from thread other than main, Salamander command may execute
    // (runs in main thread) even before PostSalamanderCommand finishes
    // can be called from any thread
    virtual void WINAPI PostSalamanderCommand(int salCmd) = 0;

    // sets flag "user worked with current path" in panel 'panel' (this flag
    // is used when filling list of working directories - List Of Working Directories (Alt+F12));
    // 'panel' is one of PANEL_XXX
    // restriction: main thread
    virtual void WINAPI SetUserWorkedOnPanelPath(int panel) = 0;

    // in panel 'panel' (one of PANEL_XXX constants) stores Selected names
    // in special array, from which user can restore selection with Edit/Restore Selection command
    // used for commands that clear current selection, so user has option
    // to return to it and perform another operation
    // restriction: main thread
    virtual void WINAPI StoreSelectionOnPanelPath(int panel) = 0;

    //
    // UpdateCrc32
    //   Updates CRC-32 (32-bit Cyclic Redundancy Check) with specified array of bytes.
    //
    // Parameters
    //   'buffer'
    //      [in] Pointer to the starting address of the block of memory to update 'crcVal' with.
    //
    //   'count'
    //      [in] Size, in bytes, of the block of memory to update 'crcVal' with.
    //
    //   'crcVal'
    //      [in] Initial crc value. Set this value to zero to calculate CRC-32 of the 'buffer'.
    //
    // Return Values
    //   Returns updated CRC-32 value.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual DWORD WINAPI UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal) = 0;

    // allocates new object for MD5 calculation
    // can be called from any thread
    virtual CSalamanderMD5* WINAPI AllocSalamanderMD5() = 0;

    // frees object for MD5 calculation (obtained by AllocSalamanderMD5 method)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderMD5(CSalamanderMD5* md5) = 0;

    // Finds pairs of '<' '>' in text, removes them from buffer and adds references to
    // their content to 'varPlacements'. 'varPlacements' is DWORD array with '*varPlacementsCount'
    // items, DWORDs are always composed of reference position in output buffer (lower WORD)
    // and reference character count (upper WORD). Strings "\<", "\>", "\\" are interpreted
    // as escape sequences and will be replaced by characters '<', '>' and '\\'.
    // Returns TRUE on success, otherwise FALSE; always sets 'varPlacementsCount' to
    // count of processed variables.
    // can be called from any thread
    virtual BOOL WINAPI LookForSubTexts(char* text, DWORD* varPlacements, int* varPlacementsCount) = 0;

    // waits (maximum 0.2 seconds) for ESC key release; used if plugin contains
    // actions that are interrupted by ESC key (ESC key monitoring via
    // GetAsyncKeyState(VK_ESCAPE)) - prevents interruption of next action monitoring ESC key
    // immediately after pressing ESC in dialog/messagebox
    // can be called from any thread
    virtual void WINAPI WaitForESCRelease() = 0;

    //
    // GetMouseWheelScrollLines
    //   An OS independent method to retrieve the number of wheel scroll lines.
    //
    // Return Values
    //   Number of scroll lines where WHEEL_PAGESCROLL (0xffffffff) indicates to scroll a page at a time.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual DWORD WINAPI GetMouseWheelScrollLines() = 0;

    //
    // GetTopVisibleParent
    //   Retrieves the visible root window by walking the chain of parent windows
    //   returned by GetParent.
    //
    // Parameters
    //   'hParent'
    //      [in] Handle to the window whose parent window handle is to be retrieved.
    //
    // Return Values
    //   The return value is the handle to the top Popup or Overlapped visible parent window.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual HWND WINAPI GetTopVisibleParent(HWND hParent) = 0;

    //
    // MultiMonGetDefaultWindowPos
    //   Retrieves the default position of the upper-left corner for a newly created window
    //   on the display monitor that has the largest area of intersection with the bounding
    //   rectangle of a specified window.
    //
    // Parameters
    //   'hByWnd'
    //      [in] Handle to the window of interest.
    //
    //   'p'
    //      [out] Pointer to a POINT structure that receives the virtual-screen coordinates
    //      of the upper-left corner for the window that would be created with CreateWindow
    //      with CW_USEDEFAULT in the 'x' parameter. Note that if the monitor is not the
    //      primary display monitor, some of the point's coordinates may be negative values.
    //
    // Return Values
    //   If the default window position lies on the primary monitor or some error occured,
    //   the return value is FALSE and you should use CreateWindow with CW_USEDEFAULT in
    //   the 'x' parameter.
    //
    //   Otherwise the return value is TRUE and coordinates from 'p' structure should be used
    //   in the CreateWindow 'x' and 'y' parameters.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p) = 0;

    //
    // MultiMonGetClipRectByRect
    //   Retrieves the bounding rectangle of the display monitor that has the largest
    //   area of intersection with a specified rectangle.
    //
    // Parameters
    //   'rect'
    //      [in] Pointer to a RECT structure that specifies the rectangle of interest
    //      in virtual-screen coordinates.
    //
    //   'workClipRect'
    //      [out] A RECT structure that specifies the work area rectangle of the
    //      display monitor, expressed in virtual-screen coordinates. Note that
    //      if the monitor is not the primary display monitor, some of the rectangle's
    //      coordinates may be negative values.
    //
    //   'monitorClipRect'
    //      [out] A RECT structure that specifies the display monitor rectangle,
    //      expressed in virtual-screen coordinates. Note that if the monitor is
    //      not the primary display monitor, some of the rectangle's coordinates
    //      may be negative values. This parameter can be NULL.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect) = 0;

    //
    // MultiMonGetClipRectByWindow
    //   Retrieves the bounding rectangle of the display monitor that has the largest
    //   area of intersection with the bounding rectangle of a specified window.
    //
    // Parameters
    //   'hByWnd'
    //      [in] Handle to the window of the interest. If this parameter is NULL,
    //      or window is not visible or is iconic, monitor with the currently active window
    //      from the same application will be used. Otherwise primary monitor will be used.
    //
    //   'workClipRect'
    //      [out] A RECT structure that specifies the work area rectangle of the
    //      display monitor, expressed in virtual-screen coordinates. Note that
    //      if the monitor is not the primary display monitor, some of the rectangle's
    //      coordinates may be negative values.
    //
    //   'monitorClipRect'
    //      [out] A RECT structure that specifies the display monitor rectangle,
    //      expressed in virtual-screen coordinates. Note that if the monitor is
    //      not the primary display monitor, some of the rectangle's coordinates
    //      may be negative values. This parameter can be NULL.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect) = 0;

    //
    // MultiMonCenterWindow
    //   Centers the window against a specified window or monitor.
    //
    // Parameters
    //   'hWindow'
    //      [in] Handle to the window whose parent window handle is to be retrieved.
    //
    //   'hByWnd'
    //      [in] Handle to the window against which to center. If this parameter is NULL,
    //      or window is not visible or is iconic, the method will center 'hWindow' against
    //      the working area of the monitor. Monitor with the currently active window
    //      from the same application will be used. Otherwise primary monitor will be used.
    //
    //   'findTopWindow'
    //      [in] If this parameter is TRUE, non child visible window will be used by walking
    //      the chain of parent windows of 'hByWnd' as the window against which to center.
    //
    //      If this parameter is FALSE, 'hByWnd' will be to the window against which to center.
    //
    // Remarks
    //   If centered window gets over working area of the monitor, the method positions
    //   the window to be whole visible.
    //
    //   Method can be called from any thread.
    //
    virtual void WINAPI MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow) = 0;

    //
    // MultiMonEnsureRectVisible
    //   Ensures that specified rectangle is either entirely or partially visible,
    //   adjusting the coordinates if necessary. All monitors are considered.
    //
    // Parameters
    //   'rect'
    //      [in/out] Pointer to the RECT structure that contain the coordinated to be
    //      adjusted. The rectangle is presumed to be in virtual-screen coordinates.
    //
    //   'partialOK'
    //      [in] Value specifying whether the rectangle must be entirely visible.
    //      If this parameter is TRUE, no moving occurs if the item is at least
    //      partially visible.
    //
    // Return Values
    //   If the rectangle is adjusted, the return value is TRUE.
    //
    //   If the rectangle is not adjusted, the return value is FALSE.
    //
    // Remarks
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK) = 0;

    //
    // InstallWordBreakProc
    //   Installs special word break procedure to the specified window. This procedure
    //   is inteded for easier cursor movevement in the single line edit controls.
    //   Delimiters '\\', '/', ' ', ';', ',', and '.' are used as cursor stops when user
    //   navigates using Ctrl+Left or Ctrl+Right keys.
    //   You can use Ctrl+Backspace to delete one word.
    //
    // Parameters
    //   'hWindow'
    //      [in] Handle to the window or control where word break proc is to be isntalled.
    //      Window may be either edit or combo box with edit control.
    //
    // Return Values
    //   The return value is TRUE if the word break proc is installed. It is FALSE if the
    //   window is neither edit nor combo box with edit control, some error occured, or
    //   this special word break proc is not supported on your OS.
    //
    // Remarks
    //   You needn't uninstall word break procedure before window is destroyed.
    //
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI InstallWordBreakProc(HWND hWindow) = 0;

    // Salamander 3 or newer: returns TRUE, if this instance of Altap
    // Salamander was first started (at instance startup time other running
    // instances of version 3 or newer are searched for);
    //
    // Notes about different SID / Session / Integrity Level (doesn't apply to Salamander 2.5 and 2.51):
    // function returns TRUE even if instance of Salamander is already running
    // under different SID; session and integrity level don't matter, so if instance
    // of Salamander is already running on different session, or with different integrity level, but
    // with matching SID, newly started instance returns FALSE
    //
    // can be called from any thread
    virtual BOOL WINAPI IsFirstInstance3OrLater() = 0;

    // support for parameter dependent strings (dealing with singles/plurals);
    // 'format' is format string for resulting string - its description follows;
    // resulting string is copied to 'buffer' buffer which size is 'bufferSize' bytes;
    // 'parametersArray' is array of parameters; 'parametersCount' is count of
    // these parameters; returns length of the resulting string
    //
    // format string description:
    //   - each format string starts with signature "{!}"
    //   - format string can contain following escape sequences (it allows to use special
    //     character without its special meaning): "\\" = "\", "\{" = "{", "\}" = "}",
    //     "\:" = ":", and "\|" = "|" (do not forget to double backslashes when writing C++
    //     strings, this applies only to format strings placed directly in C++ source code)
    //   - text which is not placed in curly brackets goes directly to resulting string
    //     (only escape sequences are handled)
    //   - parameter dependent text is placed in curly brackets
    //   - each parameter dependent text uses one parameter from 'parametersArray'
    //     (it is 64-bit unsigned int)
    //   - parameter dependent text contains more variants of resulting text, which variant
    //     is used depends on parameter value, more precisely to which defined interval the
    //     value belongs
    //   - variants of resulting text and interval bounds are separated by "|" character
    //   - first interval is from 0 to first interval bound
    //   - last interval is from last interval bound plus one to infinity (2^64-1)
    //   - parameter dependent text "{}" is used to skip one parameter from 'parametersArray'
    //     (nothing goes to resulting string)
    //   - you can also specify index of parameter to use for parameter dependent text,
    //     just place its index (from one to number of parameters) to the beginning of
    //     parameter dependent text and follow it by ':' character
    //   - if you don't specify index of parameter to use, it is assigned automatically
    //     (starting from one to number of parameters)
    //   - if you specify index of parameter to use, the next index which is assigned
    //     automatically is not affected,
    //     e.g. in "{!}%d file{2:s|0||1|s} and %d director{y|1|ies}" the first parameter
    //     dependent text uses parameter with index 2 and second uses parameter with index 1
    //   - you can use any number of parameter dependent texts with specified index
    //     of parameter to use
    //
    // examples of format strings:
    //   - "{!}director{y|1|ies}": for parameter values from 0 to 1 resulting string will be
    //     "directory" and for parameter values from 2 to infinity (2^64-1) resulting string
    //     will be "directories"
    //   - "{!}%d soubor{u|0||1|y|4|u} a %d adresar{u|0||1|e|4|u}": it needs two parameters
    //     because there are two dependent texts in curly brackets, resulting string for
    //     choosen pairs of parameters (I believe it is not needed to show all possible variants):
    //       0, 0: "%d souboru a %d adresaru"
    //       1, 12: "%d soubor a %d adresaru"
    //       3, 4: "%d soubory a %d adresare"
    //       13, 1: "%d souboru a %d adresar"
    //
    // method can be called from any thread
    virtual int WINAPI ExpandPluralString(char* buffer, int bufferSize, const char* format,
                                          int parametersCount, const CQuadWord* parametersArray) = 0;

    // in current language version of Salamander prepares string "XXX (selected/hidden)
    // files and YYY (selected/hidden) directories"; if XXX (value of parameter 'files')
    // or YYY (value of parameter 'dirs') is zero, corresponding part of string is omitted (both
    // parameters simultaneously zero is not considered); use of "selected" and "hidden" depends
    // on mode 'mode' - see description of epfdmXXX constants; resulting text
    // is returned in buffer 'buffer' of size 'bufferSize' bytes; returns length of resulting
    // text; 'forDlgCaption' is TRUE/FALSE if text is/is not intended for dialog caption
    // (in English requires capital initial letters)
    // can be called from any thread
    virtual int WINAPI ExpandPluralFilesDirs(char* buffer, int bufferSize, int files, int dirs,
                                             int mode, BOOL forDlgCaption) = 0;

    // in current language version of Salamander prepares string "BBB bytes in XXX selected
    // files and YYY selected directories"; BBB is value of parameter 'selectedBytes';
    // if XXX (value of parameter 'files') or YYY (value of parameter 'dirs') is zero,
    // corresponding part of string is omitted (both parameters simultaneously zero is not considered);
    // if 'useSubTexts' is TRUE, BBB is enclosed in '<' and '>', so BBB can be
    // further processed on info-line (see CSalamanderGeneralAbstract::LookForSubTexts method and
    // CPluginDataInterfaceAbstract::GetInfoLineContent); resulting text is returned in buffer
    // 'buffer' of size 'bufferSize' bytes; returns length of resulting text
    // can be called from any thread
    virtual int WINAPI ExpandPluralBytesFilesDirs(char* buffer, int bufferSize,
                                                  const CQuadWord& selectedBytes, int files, int dirs,
                                                  BOOL useSubTexts) = 0;

    // returns string describing what is being worked with (e.g. "file "test.txt"" or "directory "test""
    // or "3 files and 1 directory"); 'sourceDescr' is buffer for result of size
    // at least 'sourceDescrSize'; 'panel' describes source panel of operation (one of PANEL_XXX or -1
    // if operation has no source panel (e.g. CPluginFSInterfaceAbstract::CopyOrMoveFromDiskToFS));
    // 'selectedFiles'+'selectedDirs' - if operation has source panel, here is count of selected
    // files and directories in source panel, if both values are zero, works with
    // file/directory under cursor (focus); 'selectedFiles'+'selectedDirs' - if operation has no
    // source panel, here is count of files/directories that operation works with;
    // 'fileOrDirName'+'isDir' - used only if operation has no source panel and if
    // 'selectedFiles + selectedDirs == 1', here is name of file/directory and whether it's file
    // or directory ('isDir' is FALSE or TRUE); 'forDlgCaption' is TRUE/FALSE if text is/is not
    // intended for dialog caption (in English requires capital initial letters)
    // restriction: main thread (may work with panel)
    virtual void WINAPI GetCommonFSOperSourceDescr(char* sourceDescr, int sourceDescrSize,
                                                   int panel, int selectedFiles, int selectedDirs,
                                                   const char* fileOrDirName, BOOL isDir,
                                                   BOOL forDlgCaption) = 0;

    // copies string 'srcStr' after string 'dstStr' (after its terminating zero);
    // 'dstStr' is buffer of size 'dstBufSize' (must be at least 2);
    // if both strings don't fit in buffer, they are shortened (always so that
    // maximum characters from both strings fit)
    // can be called from any thread
    virtual void WINAPI AddStrToStr(char* dstStr, int dstBufSize, const char* srcStr) = 0;

    // determines if string 'fileNameComponent' can be used as component
    // of name on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
    // + null-terminator), empty string, strings of '.' characters, strings of white-spaces,
    // characters "*?\\/<>|\":" and simple names like "prn" and "prn  .txt")
    // can be called from any thread
    virtual BOOL WINAPI SalIsValidFileNameComponent(const char* fileNameComponent) = 0;

    // transforms string 'fileNameComponent' so it can be used as component
    // of name on Windows filesystem (handles strings longer than MAX_PATH-4 (4 = "C:\"
    // + null-terminator), handles empty string, strings of '.' characters, strings of
    // white-spaces, replaces characters "*?\\/<>|\":" with '_' + simple names like "prn"
    // and "prn  .txt" adds '_' at end of name); 'fileNameComponent' must be
    // expandable by at least one character (however maximum of MAX_PATH bytes
    // from 'fileNameComponent' is used)
    // can be called from any thread
    virtual void WINAPI SalMakeValidFileNameComponent(char* fileNameComponent) = 0;

    // returns TRUE if enumeration source is panel, in 'panel' then returns PANEL_LEFT or
    // PANEL_RIGHT; if enumeration source was not found or it's Find window, returns FALSE;
    // 'srcUID' is unique identifier of source (passed as parameter when opening
    // viewer or can be obtained by calling GetPanelEnumFilesParams)
    // can be called from any thread
    virtual BOOL WINAPI IsFileEnumSourcePanel(int srcUID, int* panel) = 0;

    // returns next file name for viewer from source (left/right panel or Find windows);
    // 'srcUID' is unique identifier of source (passed as parameter when opening
    // viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex'
    // (must not be NULL) is IN/OUT parameter, which plugin should change only if it wants to return
    // first file name, in this case set 'lastFileIndex' to -1; initial
    // value of 'lastFileIndex' is passed as parameter both when opening viewer and
    // when calling GetPanelEnumFilesParams; 'lastFileName' is full name of current file
    // (empty string if not known, e.g. if 'lastFileIndex' is -1); if
    // 'preferSelected' is TRUE and at least one name is selected, selected names will be returned;
    // if 'onlyAssociatedExtensions' is TRUE, returns only files with extension associated with
    // this plugin's viewer (F3 on this file would attempt to open this plugin's
    // viewer + ignores possible shadowing by another plugin's viewer); 'fileName' is buffer
    // for obtained name (size at least MAX_PATH); returns TRUE if name was
    // obtained; returns FALSE on error: no more file names in source (if
    // 'noMoreFiles' is not NULL, TRUE is returned in it), source is busy (not processing messages;
    // if 'srcBusy' is not NULL, TRUE is returned in it), otherwise source ceased to exist (path
    // change in panel, etc.);
    // can be called from any thread; WARNING: use from main thread makes no sense
    // (Salamander is busy when calling plugin method, so always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                 BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                 char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) = 0;

    // returns the previous file name for the viewer from the source (left/right panel or Find);
    // 'srcUID' is the unique identifier of the source (passed as a parameter when opening
    // of the viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex' (must
    // not be NULL) is an IN/OUT parameter, which the plugin should change only if it wants to return
    // the last file name, in which case set 'lastFileIndex' to -1; the initial value of
    // 'lastFileIndex' is passed as a parameter both when opening the viewer and when calling
    // GetPanelEnumFilesParams; 'lastFileName' is the full name of the current file (empty
    // string if not known, e.g., if 'lastFileIndex' is -1); if 'preferSelected' is
    // TRUE and at least one name is selected, selected names will be returned; if
    // 'onlyAssociatedExtensions' is TRUE, returns only files with extension associated with the viewer
    // of this plugin (F3 on this file would try to open the viewer of this
    // plugin + ignores possible shadowing by another plugin's viewer); 'fileName' is a buffer
    // for the obtained name (size at least MAX_PATH); returns TRUE if the name is successfully
    // obtained; returns FALSE on error: no previous file name exists in the source (if
    // 'noMoreFiles' is not NULL, TRUE is returned in it), the source is busy (not processing messages;
    // if 'srcBusy' is not NULL, TRUE is returned in it), otherwise the source no longer exists (path change
    // in the panel, etc.)
    // can be called from any thread; WARNING: use from the main thread makes no sense
    // (Salamander is busy when calling the plugin method, so it always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                     BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                     char* fileName, BOOL* noMoreFiles, BOOL* srcBusy) = 0;

    // determines whether the current file from the viewer is selected in the source (left/right
    // panel or Find); 'srcUID' is the unique identifier of the source (passed as a parameter
    // when opening the viewer or can be obtained by calling GetPanelEnumFilesParams); 'lastFileIndex'
    // is a parameter that the plugin should not change, the initial value of 'lastFileIndex' is passed
    // as a parameter both when opening the viewer and when calling GetPanelEnumFilesParams;
    // 'lastFileName' is the full name of the current file; returns TRUE if it was possible to determine
    // whether the current file is selected, the result is in 'isFileSelected' (must not be NULL);
    // returns FALSE on error: the source no longer exists (path change in the panel, etc.) or the file
    // 'lastFileName' is no longer in the source (for these two errors, if 'srcBusy' is not NULL,
    // FALSE is returned in it), the source is busy (not processing messages; for this error,
    // if 'srcBusy' is not NULL, TRUE is returned in it)
    // can be called from any thread; WARNING: use from the main thread makes no sense
    // (Salamander is busy when calling the plugin method, so it always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI IsFileNameForViewerSelected(int srcUID, int lastFileIndex,
                                                    const char* lastFileName,
                                                    BOOL* isFileSelected, BOOL* srcBusy) = 0;

    // sets the selection on the current file from the viewer in the source (left/right
    // panel or Find); 'srcUID' is the unique identifier of the source (passed as a parameter
    // when opening the viewer or can be obtained by calling GetPanelEnumFilesParams);
    // 'lastFileIndex' is a parameter that the plugin should not change, the initial value of
    // 'lastFileIndex' is passed as a parameter both when opening the viewer and when calling
    // GetPanelEnumFilesParams; 'lastFileName' is the full name of the current file; 'select'
    // is TRUE/FALSE if the current file should be selected/deselected; returns TRUE on success;
    // returns FALSE on error: the source no longer exists (path change in the panel, etc.) or
    // the file 'lastFileName' is no longer in the source (for these two errors, if 'srcBusy' is
    // not NULL, FALSE is returned in it), the source is busy (not processing messages; for this
    // error, if 'srcBusy' is not NULL, TRUE is returned in it)
    // can be called from any thread; WARNING: use from the main thread makes no sense
    // (Salamander is busy when calling the plugin method, so it always returns FALSE + TRUE
    // in 'srcBusy')
    virtual BOOL WINAPI SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex,
                                                        const char* lastFileName, BOOL select,
                                                        BOOL* srcBusy) = 0;

    // returns a reference to the shared history (recently used values) of the selected combobox;
    // it is an array of allocated strings; the array has a fixed number of strings, which is returned
    // in 'historyItemsCount' (must not be NULL); a reference to the array is returned in 'historyArr'
    // (must not be NULL); 'historyID' (one of SALHIST_XXX) determines which shared history to return
    // a reference to
    // limitation: main thread (shared histories cannot be used in another thread, access
    // to them is not synchronized in any way)
    virtual BOOL WINAPI GetStdHistoryValues(int historyID, char*** historyArr, int* historyItemsCount) = 0;

    // adds an allocated copy of the new value 'value' to the shared history ('historyArr'+'historyItemsCount');
    // if 'caseSensitiveValue' is TRUE, the value (string) is searched for
    // in the history array using case-sensitive comparison (FALSE = case-insensitive comparison),
    // the found value is only moved to the first position in the history array
    // limitation: main thread (shared histories cannot be used in another thread, access
    // to them is not synchronized in any way)
    // NOTE: if used for non-shared histories, can be called from any thread
    virtual void WINAPI AddValueToStdHistoryValues(char** historyArr, int historyItemsCount,
                                                   const char* value, BOOL caseSensitiveValue) = 0;

    // adds texts from the shared history ('historyArr'+'historyItemsCount') to the combobox ('combo');
    // before adding, resets the contents of the combobox (see CB_RESETCONTENT)
    // limitation: main thread (shared histories cannot be used in another thread, access
    // to them is not synchronized in any way)
    // NOTE: if used for non-shared histories, can be called from any thread
    virtual void WINAPI LoadComboFromStdHistoryValues(HWND combo, char** historyArr, int historyItemsCount) = 0;

    // determines the color depth of the current display and returns TRUE if it is more than 8-bit (256 colors)
    // can be called from any thread
    virtual BOOL WINAPI CanUse256ColorsBitmap() = 0;

    // checks if the enabled-root-parent of window 'parent' is the foreground window, if not,
    // FlashWindow(root-parent of window 'parent', TRUE) is performed and the root-parent of window 'parent' is returned,
    // otherwise NULL is returned
    // USAGE:
    //    HWND mainWnd = GetWndToFlash(parent);
    //    CDlg(parent).Execute();
    //    if (mainWnd != NULL) FlashWindow(mainWnd, FALSE);  // under W2K+ probably no longer needed: flashing must be removed manually
    // can be called from any thread
    virtual HWND WINAPI GetWndToFlash(HWND parent) = 0;

    // performs reactivation of the drop-target (after dropping in drag&drop) after opening our progress
    // window (which activates upon opening, thereby deactivating the drop-target); if 'dropTarget' is
    // not NULL and is not a panel in this Salamander, activates 'progressWnd' and subsequently
    // activates the most distant enabled ancestor of 'dropTarget' (this combination gets rid of the activated
    // state without an active application, which otherwise occasionally occurs)
    // can be called from any thread
    virtual void WINAPI ActivateDropTarget(HWND dropTarget, HWND progressWnd) = 0;

    // schedules opening of the Pack dialog with the selected packer from this plugin (see
    // CSalamanderConnectAbstract::AddCustomPacker), if the packer from this plugin
    // does not exist (e.g., because the user deleted it), an error message is displayed
    // to the user; the dialog opens when there are no messages in the main thread message-queue
    // and Salamander is not "busy" (no modal dialog is open
    // and no message is being processed); repeated calls to this method before
    // opening the Pack dialog only result in changing the 'delFilesAfterPacking' parameter;
    // 'delFilesAfterPacking' affects the "Delete files after packing" checkbox
    // in the Pack dialog: 0=default, 1=checked, 2=unchecked
    // limitation: main thread
    virtual void WINAPI PostOpenPackDlgForThisPlugin(int delFilesAfterPacking) = 0;

    // schedules opening of the Unpack dialog with the selected unpacker from this plugin (see
    // CSalamanderConnectAbstract::AddCustomUnpacker), if the unpacker from this plugin
    // does not exist (e.g., because the user deleted it), an error message is displayed
    // to the user; the dialog opens when there are no messages in the main thread message-queue
    // and Salamander is not "busy" (no modal dialog is open
    // and no message is being processed); repeated calls to this method before
    // opening the Unpack dialog only result in changing the 'unpackMask' parameter;
    // 'unpackMask' affects the "Unpack files" mask: NULL=default, otherwise the text of the mask
    // limitation: main thread
    virtual void WINAPI PostOpenUnpackDlgForThisPlugin(const char* unpackMask) = 0;

    // creates a file with the name 'fileName' via a classic Win32 API call
    // CreateFile (lpSecurityAttributes==NULL, dwCreationDisposition==CREATE_NEW,
    // hTemplateFile==NULL); this method resolves collision of 'fileName' with the DOS name
    // of an already existing file/directory (only if it is not also a collision with the long
    // name of the file/directory) - ensures a change of the DOS name so that the file with
    // the name 'fileName' can be created (method: temporarily renames the conflicting
    // file/directory to another name and after creating 'fileName' renames it back);
    // returns the file handle or INVALID_HANDLE_VALUE on error (returns in 'err'
    // (if not NULL) the Windows error code)
    // can be called from any thread
    virtual HANDLE WINAPI SalCreateFileEx(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                          DWORD flagsAndAttributes, DWORD* err) = 0;

    // creates a directory with the name 'name' via a classic Win32 API call
    // CreateDirectory(lpSecurityAttributes==NULL); this method resolves collision of 'name'
    // with the DOS name of an already existing file/directory (only if it is not also a
    // collision with the long name of the file/directory) - ensures a change of the DOS name
    // so that the directory with the name 'name' can be created (method: temporarily renames
    // the conflicting file/directory to another name and after creating 'name'
    // renames it back); also handles names ending with spaces (can create them, unlike
    // CreateDirectory, which trims spaces without warning and thus actually creates
    // a different directory); returns TRUE on success, FALSE on error (returns in 'err'
    // (if not NULL) the Windows error code)
    // can be called from any thread
    virtual BOOL WINAPI SalCreateDirectoryEx(const char* name, DWORD* err) = 0;

    // allows disconnecting/connecting change monitoring (only for Windows paths and archive paths)
    // on paths viewed in one of the panels; purpose: if your code (disk formatting,
    // disk shredding, etc.) is hindered by the fact that the panel has an open handle
    // "ChangeNotification" for the path, you can temporarily disconnect it with this method (after reconnecting,
    // a refresh is triggered for the path in the panel); 'panel' is one of PANEL_XXX; 'stopMonitoring'
    // is TRUE/FALSE (disconnect/connect)
    // limitation: main thread
    virtual void WINAPI PanelStopMonitoring(int panel, BOOL stopMonitoring) = 0;

    // allocates a new CSalamanderDirectory object for working with files/directories in an archive or
    // file-system; if 'isForFS' is TRUE, the object is preset for use with a file-system,
    // otherwise the object is preset for use with an archive (default object flags differ
    // for archive and file-system, see method CSalamanderDirectoryAbstract::SetFlags)
    // can be called from any thread
    virtual CSalamanderDirectoryAbstract* WINAPI AllocSalamanderDirectory(BOOL isForFS) = 0;

    // frees a CSalamanderDirectory object (obtained by the AllocSalamanderDirectory method,
    // WARNING: must not be called for any other CSalamanderDirectoryAbstract pointer)
    // can be called from any thread
    virtual void WINAPI FreeSalamanderDirectory(CSalamanderDirectoryAbstract* salDir) = 0;

    // adds a new timer for a plugin FS object; when the timer times out, the method
    // CPluginFSInterfaceAbstract::Event() of the plugin FS object 'timerOwner' is called with parameters
    // FSE_TIMER and 'timerParam'; 'timeout' is the timer timeout from its addition (in milliseconds,
    // must be >= 0); the timer is canceled at the moment of its timeout (before calling
    // CPluginFSInterfaceAbstract::Event()) or when closing the plugin FS object;
    // returns TRUE if the timer was successfully added
    // limitation: main thread
    virtual BOOL WINAPI AddPluginFSTimer(int timeout, CPluginFSInterfaceAbstract* timerOwner,
                                         DWORD timerParam) = 0;

    // cancels either all timers of the plugin FS object 'timerOwner' (if 'allTimers' is TRUE)
    // or only all timers with parameter equal to 'timerParam' (if 'allTimers' is FALSE);
    // returns the number of canceled timers
    // limitation: main thread
    virtual int WINAPI KillPluginFSTimer(CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers,
                                         DWORD timerParam) = 0;

    // determines the visibility of the item for FS in the Change Drive menu and Drive bars; returns TRUE
    // if the item is visible, otherwise returns FALSE
    // limitation: main thread (otherwise changes in plugin configuration may occur during the call)
    virtual BOOL WINAPI GetChangeDriveMenuItemVisibility() = 0;

    // sets the visibility of the item for FS in the Change Drive menu and Drive bars; use
    // only during plugin installation (otherwise there is a risk of overriding user-selected visibility);
    // 'visible' is TRUE if the item should be visible
    // limitation: main thread (otherwise changes in plugin configuration may occur during the call)
    virtual void WINAPI SetChangeDriveMenuItemVisibility(BOOL visible) = 0;

    // Sets a breakpoint on the x-th COM/OLE allocation. Used to track down COM/OLE leaks.
    // In the release version of Salamander, it does nothing. The debug version of Salamander displays
    // a list of COM/OLE leaks to the debugger's Debug window and to the Trace Server upon termination.
    // In square brackets is the allocation order, which we pass as 'alloc' to the
    // OleSpySetBreak call. Can be called from any thread.
    virtual void WINAPI OleSpySetBreak(int alloc) = 0;

    // Returns copies of icons that Salamander uses in panels. 'icon' specifies the icon and is
    // one of the SALICON_xxx values. 'iconSize' specifies what size the returned icon should have
    // and is one of the SALICONSIZE_xxx values.
    // On success, returns a handle to the created icon. The plugin must ensure destruction
    // of the icon by calling the DestroyIcon API. On failure, returns NULL.
    // limitation: main thread
    virtual HICON WINAPI GetSalamanderIcon(int icon, int iconSize) = 0;

    // GetFileIcon
    //   Function retrieves handle to large or small icon from the specified object,
    //   such as a file, a folder, a directory, or a drive root.
    //
    // Parameters
    //   'path'
    //      [in] Pointer to a null-terminated string that contains the path and file
    //      name. If the 'pathIsPIDL' parameter is TRUE, this parameter must be the
    //      address of an ITEMIDLIST (PIDL) structure that contains the list of item
    //      identifiers that uniquely identify the file within the Shell's namespace.
    //      The PIDL must be a fully qualified PIDL. Relative PIDLs are not allowed.
    //
    //   'pathIsPIDL'
    //      [in] Indicate that 'path' is the address of an ITEMIDLIST structure rather
    //      than a path name.
    //
    //   'hIcon'
    //      [out] Pointer to icon handle that receive handle to the icon extracted
    //      from the object.
    //
    //   'iconSize'
    //      [in] Required size of icon. SALICONSIZE_xxx
    //
    //   'fallbackToDefIcon'
    //      [in] Value specifying whether the default (simple) icon should be used if
    //      the icon of the specified object is not available. If this parameter is
    //      TRUE, function tries to return the default (simple) icon in this situation.
    //      Otherwise, it returns no icon (return value is FALSE).
    //
    //   'defIconIsDir'
    //      [in] Specifies whether the default (simple) icon for 'path' is icon of
    //      directory. This parameter is ignored unless 'fallbackToDefIcon' is TRUE.
    //
    // Return Values
    //   Returns TRUE if successful, or FALSE otherwise.
    //
    // Remarks
    //   You are responsible for freeing returned icons with DestroyIcon when you
    //   no longer need them.
    //
    //   You must initialize COM with CoInitialize or OLEInitialize prior to
    //   calling GetFileIcon.
    //
    //   Method can be called from any thread.
    //
    virtual BOOL WINAPI GetFileIcon(const char* path, BOOL pathIsPIDL,
                                    HICON* hIcon, int iconSize, BOOL fallbackToDefIcon,
                                    BOOL defIconIsDir) = 0;

    // FileExists
    //   Function checks the existence of a file. It returns TRUE if the specified
    //   file exists. If the file does not exist, it returns 0. FileExists only checks
    //   the existence of files, directories are ignored.
    // can be called from any thread
    virtual BOOL WINAPI FileExists(const char* fileName) = 0;

    // changes the path in the panel to the last known disk path, if it is not accessible,
    // then a change is made to the user-selected "rescue" path (see
    // SALCFG_IFPATHISINACCESSIBLEGOTO) and if that also fails, then to the root of the first local
    // fixed drive (Salamander 2.5 and 2.51 only change to the root of the first local fixed drive);
    // used to close the file-system in the panel (disconnect); 'parent' is the parent of any
    // messageboxes; 'panel' is one of PANEL_XXX
    // limitation: main thread + outside methods CPluginFSInterfaceAbstract and CPluginDataInterfaceAbstract
    // (there is a risk of closing the FS open in the panel - 'this' could cease to exist for the method)
    virtual void WINAPI DisconnectFSFromPanel(HWND parent, int panel) = 0;

    // returns TRUE if the file name 'name' is associated in Archives Associations in Panels
    // with the calling plugin
    // 'name' must be only the file name, not with a full or relative path
    // limitation: main thread
    virtual BOOL WINAPI IsArchiveHandledByThisPlugin(const char* name) = 0;

    // serves as the LR_xxx parameter for the LoadImage() API function
    // if the user does not have hi-color icons enabled in desktop configuration,
    // returns LR_VGACOLOR to prevent incorrect loading of the multicolor version of the icon
    // otherwise returns 0 (LR_DEFAULTCOLOR); the function result can be ORed with other LR_xxx flags
    // can be called from any thread
    virtual DWORD WINAPI GetIconLRFlags() = 0;

    // determines from the file extension whether it is a link ("lnk", "pif" or "url"); 'fileExtension'
    // is the file extension (pointer after the dot), must not be NULL; returns 1 if it is a link, otherwise
    // returns 0; NOTE: used to populate CFileData::IsLink
    // can be called from any thread
    virtual int WINAPI IsFileLink(const char* fileExtension) = 0;

    // returns ILC_COLOR??? according to Windows version - tuned for using imagelists in listviews
    // typical usage: ImageList_Create(16, 16, ILC_MASK | GetImageListColorFlags(), ???, ???)
    // can be called from any thread
    virtual DWORD WINAPI GetImageListColorFlags() = 0;

    // "safe" version of GetOpenFileName()/GetSaveFileName() handles the situation when the provided path
    // in OPENFILENAME::lpstrFile is not valid (for example z:\); in this case the std. API version
    // of the function does not open a window and silently returns FALSE and CommDlgExtendedError() returns FNERR_INVALIDFILENAME.
    // The following two functions in this case call the API once more, but with a "safely"
    // existing path (Documents, or Desktop).
    virtual BOOL WINAPI SafeGetOpenFileName(LPOPENFILENAME lpofn) = 0;
    virtual BOOL WINAPI SafeGetSaveFileName(LPOPENFILENAME lpofn) = 0;

    // plugin must provide Salamander with the name of its .chm file before using OpenHtmlHelp()
    // without the path (e.g., "demoplug.chm")
    // can be called from any thread, but it is necessary to exclude simultaneous calls with OpenHtmlHelp()
    virtual void WINAPI SetHelpFileName(const char* chmName) = 0;

    // opens the plugin's HTML help, the help language (directory with .chm files) is selected as follows:
    // -directory obtained from the current .slg file of Salamander (see SLGHelpDir in shared\versinfo.rc)
    // -HELP\ENGLISH\*.chm
    // -first found subdirectory in the HELP subdirectory
    // plugin must call SetHelpFileName() before using OpenHtmlHelp(); 'parent' is the parent
    // of the messagebox with error; 'command' is the HTML help command, see HHCDisplayXXX; 'dwData' is the parameter
    // of the HTML help command, see HHCDisplayXXX
    // can be called from any thread
    // note: for displaying Salamander's help see OpenHtmlHelpForSalamander
    virtual BOOL WINAPI OpenHtmlHelp(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData,
                                     BOOL quiet) = 0;

    // returns TRUE if paths 'path1' and 'path2' are from the same volume; in 'resIsOnlyEstimation'
    // (if not NULL) returns TRUE if the result is uncertain (certain only in case of path match or
    // if it is possible to obtain the "volume name" (volume GUID) for both paths, which is only applicable for
    // local paths under W2K or newer from the NT series)
    // can be called from any thread
    virtual BOOL WINAPI PathsAreOnTheSameVolume(const char* path1, const char* path2,
                                                BOOL* resIsOnlyEstimation) = 0;

    // reallocation of memory on Salamander's heap (unnecessary when using salrtl9.dll - classic realloc is sufficient);
    // on insufficient memory, displays a message to the user with Retry and Cancel buttons (after another prompt
    // terminates the application)
    // can be called from any thread
    virtual void* WINAPI Realloc(void* ptr, int size) = 0;

    // returns in 'enumFilesSourceUID' (must not be NULL) the unique identifier of the source for panel
    // 'panel' (one of PANEL_XXX), used in viewers when enumerating files
    // from the panel (see parameter 'srcUID' e.g. in method GetNextFileNameForViewer), this
    // identifier changes e.g. when the path in the panel changes; if 'enumFilesCurrentIndex' is not
    // NULL, the index of the focused file is returned in it (if there is no focused file, -1 is returned);
    // limitation: main thread (otherwise the panel content may change)
    virtual void WINAPI GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID,
                                                int* enumFilesCurrentIndex) = 0;

    // posts a message to the panel with active FS 'modifiedFS' that a
    // path refresh should be performed (reloads the listing and transfers selection, icons, focus, etc. to
    // the new panel content); the refresh is performed when the main window of Salamander is activated
    // (when suspend-mode ends); the FS path is always reloaded; if 'modifiedFS' is not in any
    // panel, nothing is done; if 'focusFirstNewItem' is TRUE and only one
    // item was added to the panel, the focus goes to this new item (used e.g. for focusing newly created
    // files/directories); returns TRUE if the refresh was performed, FALSE if 'modifiedFS' was not
    // found in any panel
    // can be called from any thread (if the main thread is not running code inside the plugin,
    // the refresh will occur as soon as possible, otherwise the refresh will wait at least until the main
    // thread exits the plugin)
    virtual BOOL WINAPI PostRefreshPanelFS2(CPluginFSInterfaceAbstract* modifiedFS,
                                            BOOL focusFirstNewItem = FALSE) = 0;

    // loads text with ID 'resID' from the resources of module 'module'; returns text in an internal buffer (there is a risk of
    // text change due to internal buffer change caused by other LoadStr calls from other
    // plugins or Salamander; the buffer is 10000 characters large, overwriting is only a risk after it is
    // filled (used cyclically); if you need to use the text later, we recommend
    // copying it to a local buffer); if 'module' is NULL or 'resID' is not in the module,
    // the text "ERROR LOADING STRING" is returned (and debug/SDK version outputs TRACE_E)
    // can be called from any thread
    virtual char* WINAPI LoadStr(HINSTANCE module, int resID) = 0;

    // loads text with ID 'resID' from the resources of module 'module'; returns text in an internal buffer (there is a risk of
    // text change due to internal buffer change caused by other LoadStrW calls from other
    // plugins or Salamander; the buffer is 10000 characters large, overwriting is only a risk after it is
    // filled (used cyclically); if you need to use the text later, we recommend
    // copying it to a local buffer); if 'module' is NULL or 'resID' is not in the module,
    // the text L"ERROR LOADING WIDE STRING" is returned (and debug/SDK version outputs TRACE_E)
    // can be called from any thread
    virtual WCHAR* WINAPI LoadStrW(HINSTANCE module, int resID) = 0;

    // zmena cesty v panelu na uzivatelem zvolenou "zachranou" cestu (viz
    // SALCFG_IFPATHISINACCESSIBLEGOTO) a pokud i ta selze, tak na root prvniho lokalniho fixed
    // drivu, jde o temer jistou zmenu aktualni cesty v panelu; 'panel' je jeden z PANEL_XXX;
    // neni-li 'failReason' NULL, nastavuje se na jednu z konstant CHPPFR_XXX (informuje o vysledku
    // metody); vraci TRUE pokud se zmena cesty podarila (na "zachranou" nebo fixed drive)
    // omezeni: hlavni thread + mimo metody CPluginFSInterfaceAbstract a CPluginDataInterfaceAbstract
    // (hrozi napr. zavreni FS otevreneho v panelu - metode by mohl prestat existovat 'this')
    virtual BOOL WINAPI ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason = NULL) = 0;

    // prihlasi plugin jako nahradu za Network polozku v Change Drive menu a v Drive barach,
    // plugin musi pridavat do Salamandera file-system, na kterem se pak oteviraji nekompletni
    // UNC cesty ("\\" a "\\server") z prikazu Change Directory a na ktery se odchazi
    // pres symbol up-diru ("..") z rootu UNC cest;
    // omezeni: volat jen z entry-pointu pluginu a to az po SetBasicPluginData
    virtual void WINAPI SetPluginIsNethood() = 0;

    // otevre systemove kontextove menu pro oznacene polozky nebo fokuslou polozku na sitove ceste
    // ('forItems' je TRUE) nebo pro sitovou cestu ('forItems' je FALSE), vybrany prikaz z menu
    // take provede; menu se ziskava prochazenim slozky CSIDL_NETWORK; 'parent' je navrzeny parent
    // kontextoveho menu; 'panel' identifikuje panel (PANEL_LEFT nebo PANEL_RIGHT), pro ktery se
    // ma kontextove menu otevrit (z tohoto panelu se ziskavaji fokusle/oznacene soubory/adresare,
    // se kterymi se pracuje); 'menuX' + 'menuY' jsou navrzene souradnice leveho horniho rohu
    // kontextoveho menu; 'netPath' je sitova cesta, povolene jsou jen "\\" a "\\server"; neni-li
    // 'newlyMappedDrive' NULL, vraci se v nem pismenko ('A' az 'Z') nove namapovaneho disku (pres
    // prikaz Map Network Drive z kontextoveho menu), pokud se v nem vraci nula, k zadnemu novemu
    // mapovani nedoslo
    // omezeni: hlavni thread
    virtual void WINAPI OpenNetworkContextMenu(HWND parent, int panel, BOOL forItems, int menuX,
                                               int menuY, const char* netPath,
                                               char* newlyMappedDrive) = 0;

    // zdvojuje '\\' - hodi se pro texty, ktere posilame do LookForSubTexts, ktera '\\\\'
    // zase zredukuje na '\\'; 'buffer' je vstupne/vystupni retezec, 'bufferSize' je velikost
    // 'buffer' v bytech; vraci TRUE pokud zdvojenim nedoslo ke ztrate znaku z konce retezce
    // (buffer byl dost veliky)
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI DuplicateBackslashes(char* buffer, int bufferSize) = 0;

    // ukaze v panelu 'panel' throbber (animace informujici uzivatele o aktivite souvisejici
    // s panelem, napr. "nacitam data ze site") se zpozdenim 'delay' (v ms); 'panel' je jeden
    // z PANEL_XXX; neni-li 'tooltip' NULL, jde o text, ktery se ukaze po najeti mysi na
    // throbber (je-li NULL, zadny text se neukazuje); pokud je uz v panelu throbber zobrazeny
    // nebo ceka na zobrazeni, zmeni se jeho identifikacni cislo a tooltip (je-li zobrazeny,
    // 'delay' se ignoruje, ceka-li na zobrazeni, nastavi se nove zpozdeni podle 'delay');
    // vraci identifikacni cislo throbberu (nikdy neni -1, tedy -1 je mozne pouzit jako
    // prazdnou hodnotu + vsechna vracena cisla jsou unikatni, presneji receno opakovat se
    // zacnou po nerealnych 2^32 zobrazeni throbberu);
    // POZNAMKA: vhodne misto pro zobrazeni throbberu pro FS je prijem udalosti FSE_PATHCHANGED,
    // to uz je FS v panelu (jestli se ma nebo nema throbber zobrazit se muze urcit predem
    // v ChangePath nebo ListCurrentPath)
    // omezeni: hlavni thread
    virtual int WINAPI StartThrobber(int panel, const char* tooltip, int delay) = 0;

    // schova throbber s identifikacnim cislem 'id'; vraci TRUE pokud dojde ke schovani
    // throbberu; vraci FALSE pokud se jiz tento throbber schoval nebo se pres nej ukazal
    // jiny throbber;
    // POZNAMKA: throbber se automaticky schovava tesne pred zmenou cesty v panelu nebo
    // pred refreshem (u FS to znamena tesne po uspesnem volani ListCurrentPath, u archivu
    // je to po otevreni a vylistovani archivu, u disku je to po overeni pristupnosti cesty)
    // omezeni: hlavni thread
    virtual BOOL WINAPI StopThrobber(int id) = 0;

    // ukaze v panelu 'panel' ikonu zabezpeceni (zamknuty nebo odemknuty zamek, napr. u FTPS informuje
    // uzivatele o tom, ze je spojeni se serverem zabezpecene pomoci SSL a identita serveru je
    // overena (zamknuty zamek) nebo overena neni (odemknuty zamek)); 'panel' je jeden z PANEL_XXX;
    // je-li 'showIcon' TRUE, ikona se ukaze, jinak se schova; 'isLocked' urcuje, jestli jde
    // o zamknuty (TRUE) nebo odemknuty (FALSE) zamek; neni-li 'tooltip' NULL, jde o text, ktery se
    // ukaze po najeti mysi na ikonu (je-li NULL, zadny text se neukazuje); pokud se ma po kliknuti
    // na ikone zabezpeceni provest nejaka akce (napr. u FTPS se zobrazuje dialog s certifikatem
    // serveru), je nutne ji pridat do metody CPluginFSInterfaceAbstract::ShowSecurityInfo file-systemu
    // zobrazeneho v panelu;
    // POZNAMKA: vhodne misto pro zobrazeni ikony zabezpeceni pro FS je prijem udalosti
    // FSE_PATHCHANGED, to uz je FS v panelu (jestli se ma nebo nema ikona zobrazit se muze urcit
    // predem v ChangePath nebo ListCurrentPath)
    // POZNAMKA: ikona zabezpeceni se automaticky schovava tesne pred zmenou cesty v panelu nebo
    // pred refreshem (u FS to znamena tesne po uspesnem volani ListCurrentPath, u archivu
    // je to po otevreni a vylistovani archivu, u disku je to po overeni pristupnosti cesty)
    // omezeni: hlavni thread
    virtual void WINAPI ShowSecurityIcon(int panel, BOOL showIcon, BOOL isLocked,
                                         const char* tooltip) = 0;

    // odstrani aktualni cestu v panelu z historie adresaru zobrazenych v panelu (Alt+Left/Right)
    // a ze seznamu pracovnich cest (Alt+F12); pouziva se pro zneviditelneni prechodnych cest,
    // napr. "net:\Entire Network\Microsoft Windows Network\WORKGROUP\server\share" automaticky
    // prechazi na "\\server\share" a je nezadouci, aby se tento prechod delal pri pohybu v historii
    // omezeni: hlavni thread
    virtual void WINAPI RemoveCurrentPathFromHistory(int panel) = 0;

    // vracit TRUE, pokud je aktualni uzivatel clenem skupiny Administrators, jinak vraci FALSE
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI IsUserAdmin() = 0;

    // vraci TRUE, pokud Salamander bezi na vzdalene plose (RemoteDesktop), jinak vraci FALSE
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI IsRemoteSession() = 0;

    // ekvivalent volani WNetAddConnection2(lpNetResource, NULL, NULL, CONNECT_INTERACTIVE);
    // vyhodou je podrobnejsi zobrazeni chybovych stavu (napr. ze expirovalo heslo,
    // ze je spatne zadane heslo nebo jmeno, ze je potreba zmenit heslo, atd.)
    // mozne volat z libovolneho threadu
    virtual DWORD WINAPI SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource) = 0;

    //
    // GetMouseWheelScrollChars
    //   An OS independent method to retrieve the number of wheel scroll chars.
    //
    // Return Values
    //   Number of scroll characters where WHEEL_PAGESCROLL (0xffffffff) indicates to scroll a page at a time.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual DWORD WINAPI GetMouseWheelScrollChars() = 0;

    //
    // GetSalamanderZLIB
    //   Provides simplified interface to the ZLIB library provided by Salamander,
    //   for details see spl_zlib.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderZLIBAbstract* WINAPI GetSalamanderZLIB() = 0;

    //
    // GetSalamanderPNG
    //   Provides interface to the PNG library provided by Salamander.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderPNGAbstract* WINAPI GetSalamanderPNG() = 0;

    //
    // GetSalamanderCrypt
    //   Provides interface to encryption libraries provided by Salamander,
    //   for details see spl_crypt.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderCryptAbstract* WINAPI GetSalamanderCrypt() = 0;

    // informuje Salamandera o tom, ze plugin pouziva Password Manager a tedy Salamander ma
    // pluginu hlasit zavedeni/zmenu/zruseni master passwordu (viz
    // CPluginInterfaceAbstract::PasswordManagerEvent)
    // omezeni: volat jen z entry-pointu pluginu a to az po SetBasicPluginData
    virtual void WINAPI SetPluginUsesPasswordManager() = 0;

    //
    // GetSalamanderPasswordManager
    //   Provides interface to Password Manager provided by Salamander.
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual CSalamanderPasswordManagerAbstract* WINAPI GetSalamanderPasswordManager() = 0;

    // otevre HTML help samotneho Salamanadera (misto help pluginu, ktery se otevira pomoci OpenHtmlHelp()),
    // jazyk helpu (adresar s .chm soubory) vybira takto:
    // -adresar ziskany z aktualniho .slg souboru Salamandera (viz SLGHelpDir v shared\versinfo.rc)
    // -HELP\ENGLISH\*.chm
    // -prvni nalezeny podadresar v podadresari HELP
    // 'parent' je parent messageboxu s chybou; 'command' je prikaz HTML helpu, viz HHCDisplayXXX;
    // 'dwData' je parametr prikazu HTML helpu, viz HHCDisplayXXX; pokud je command==HHCDisplayContext,
    // musi byt hodnota 'dwData' z rodiny konstant HTMLHELP_SALID_XXX
    // lze volat z libovolneho threadu
    virtual BOOL WINAPI OpenHtmlHelpForSalamander(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet) = 0;

    //
    // GetSalamanderBZIP2
    //   Provides simplified interface to the BZIP2 library provided by Salamander,
    //   for details see spl_bzip2.h.
    //
    // Remarks
    //   Method can be called from any thread.
    virtual CSalamanderBZIP2Abstract* WINAPI GetSalamanderBZIP2() = 0;

    //
    // GetFocusedItemMenuPos
    //   Returns point (in screen coordinates) where the context menu for focused item in the
    //   active panel should be displayed. The upper left corner of the panel is returned when
    //   focused item is not visible
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual void WINAPI GetFocusedItemMenuPos(POINT* pos) = 0;

    //
    // LockMainWindow
    //   Locks main window to pretend it is disabled. Main windows is still able to receive focus
    //   in the locked state. Set 'lock' to TRUE to lock main window and to FALSE to revert it back
    //   to normal state. 'hToolWnd' is reserverd parameter, set it to NULL. 'lockReason' is (optional,
    //   can be NULL) describes the reason for main window locked state. It will be displayed during
    //   attempt to close locked main window; content of string is copied to internal structure
    //   so buffer can be deallocated after return from LockMainWindow().
    //
    // Remarks
    //   Method can be called from main thread only.
    virtual void WINAPI LockMainWindow(BOOL lock, HWND hToolWnd, const char* lockReason) = 0;

    // jen pro pluginy "dynamic menu extension" (viz FUNCTION_DYNAMICMENUEXT):
    // nastavi volajicimu pluginu priznak, ze se ma pri nejblizsi mozne prilezitosti
    // (jakmile nebudou v message-queue hl. threadu zadne message a Salamander nebude
    // "busy" (nebude otevreny zadny modalni dialog a nebude se zpracovavat zadna zprava))
    // znovu sestavit menu volanim metody CPluginInterfaceForMenuExtAbstract::BuildMenu;
    // POZOR: pokud se vola z jineho nez hlavniho threadu, muze dojit k volani BuildMenu
    // (probiha v hlavnim threadu) dokonce drive nez skonci PostPluginMenuChanged
    // mozne volat z libovolneho threadu
    virtual void WINAPI PostPluginMenuChanged() = 0;

    //
    // GetMenuItemHotKey
    //   Search through plugin's menu items added with AddMenuItem() for item with 'id'.
    //   When such item is found, its 'hotKey' and 'hotKeyText' (up to 'hotKeyTextSize' characters)
    //   is set. Both 'hotKey' and 'hotKeyText' could be NULL.
    //   Returns TRUE when item with 'id' is found, otherwise returns FALSE.
    //
    //   Remarks
    //   Method can be called from main thread only.
    virtual BOOL WINAPI GetMenuItemHotKey(int id, WORD* hotKey, char* hotKeyText, int hotKeyTextSize) = 0;

    // nase varianty funkci RegQueryValue a RegQueryValueEx, narozdil od API variant
    // zajistuji pridani null-terminatoru pro typy REG_SZ, REG_MULTI_SZ a REG_EXPAND_SZ
    // POZOR: pri zjistovani potrebne velikosti bufferu vraci o jeden nebo dva (dva
    //        jen u REG_MULTI_SZ) znaky vic pro pripad, ze by string bylo potreba
    //        zakoncit nulou/nulami
    // mozne volat z libovolneho threadu
    virtual LONG WINAPI SalRegQueryValue(HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData) = 0;
    virtual LONG WINAPI SalRegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                           LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData) = 0;

    // protoze windowsova verze GetFileAttributes neumi pracovat se jmeny koncicimi mezerou,
    // napsali jsme si vlastni (u techto jmen pridava backslash na konec, cimz uz pak
    // GetFileAttributes funguje spravne, ovsem jen pro adresare, pro soubory s mezerou na
    // konci reseni nemame, ale aspon se to nezjistuje od jineho souboru - windowsova verze
    // orizne mezery a pracuje tak s jinym souborem/adresarem)
    // mozne volat z libovolneho threadu
    virtual DWORD WINAPI SalGetFileAttributes(const char* fileName) = 0;

    // zatim neexistuje Win32 API pro detekci SSD, takze se jejich detekovani provadi heuristikou
    // na zaklade dotazu na podporu pro TRIM, StorageDeviceSeekPenaltyProperty, atd
    // funkce vraci TRUE, pokud disk na ceste 'path' vypada jako SSD; FALSE jindy
    // vysledek neni 100%, lide hlasi nefunkcnost algoritmu napriklad na SSD PCIe kartach:
    // http://stackoverflow.com/questions/23363115/detecting-ssd-in-windows/33359142#33359142
    // umi zjistit korektni udaje i pro cesty obsahujici substy a reparse pointy pod Windows
    // 2000/XP/Vista (Salamander 2.5 pracuje jen s junction-pointy); 'path' je cesta, pro kterou
    // zjistujeme informace; pokud cesta vede pres sitovou cestu, tise vraci FALSE
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI IsPathOnSSD(const char* path) = 0;

    // vraci TRUE, pokud jde o UNC cestu (detekuje oba formaty: \\server\share i \\?\UNC\server\share)
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI IsUNCPath(const char* path) = 0;

    // nahradi substy v ceste 'resPath' jejich cilovymi cestami (prevod na cestu bez SUBST drive-letters);
    // 'resPath' musi ukazovat na buffer o minimalne 'MAX_PATH' znacich
    // vraci TRUE pri uspechu, FALSE pri chybe
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI ResolveSubsts(char* resPath) = 0;

    // volat jen pro cesty 'path', jejichz root (po odstraneni substu) je DRIVE_FIXED (jinde nema smysl hledat
    // reparse pointy); hledame cestu bez reparse pointu, vedouci na stejny svazek jako 'path'; pro cestu
    // obsahujici symlink vedouci na sitovou cestu (UNC nebo mapovanou) vracime jen root teto sitove cesty
    // (ani Vista neumi delat s reparse pointy na sitovych cestach, takze to je nejspis zbytecne drazdit);
    // pokud takova cesta neexistuje z duvodu, ze aktualni (posledni) lokalni reparse point je volume mount
    // point (nebo neznamy typ reparse pointu), vracime cestu k tomuto volume mount pointu (nebo reparse
    // pointu neznameho typu); pokud cesta obsahuje vice nez 50 reparse pointu (nejspis nekonecny cyklus),
    // vracime puvodni cestu;
    //
    // 'resPath' je buffer pro vysledek o velikosti MAX_PATH; 'path' je puvodni cesta; v 'cutResPathIsPossible'
    // (nesmi byt NULL) vracime FALSE pokud vysledna cesta v 'resPath' obsahuje na konci reparse point (volume
    // mount point nebo neznamy typ reparse pointu) a tudiz ji nesmime zkracovat (dostali bysme se tim nejspis
    // na jiny svazek); je-li 'rootOrCurReparsePointSet' neNULLove a obsahuje-li FALSE a na puvodni ceste je
    // aspon jeden lokalni reparse point (reparse pointy na sitove casti cesty ignorujeme), vracime v teto
    // promenne TRUE + v 'rootOrCurReparsePoint' (neni-li NULL) vracime plnou cestu k aktualnimu (poslednimu
    // lokalnimu) reparse pointu (pozor, ne kam vede); cilovou cestu aktualniho reparse pointu (jen je-li to
    // junction nebo symlink) vracime v 'junctionOrSymlinkTgt' (neni-li NULL) + typ vracime v 'linkType':
    // 2 (JUNCTION POINT), 3 (SYMBOLIC LINK); v 'netPath' (neni-li NULL) vracime sitovou cestu, na kterou
    // vede aktualni (posledni) lokalni symlink v ceste - v teto situaci se root sitove cesty vraci v 'resPath'
    // mozne volat z libovolneho threadu
    virtual void WINAPI ResolveLocalPathWithReparsePoints(char* resPath, const char* path,
                                                          BOOL* cutResPathIsPossible,
                                                          BOOL* rootOrCurReparsePointSet,
                                                          char* rootOrCurReparsePoint,
                                                          char* junctionOrSymlinkTgt, int* linkType,
                                                          char* netPath) = 0;

    // Provede resolve substu a reparse pointu pro cestu 'path', nasledne se pro mount-point cesty
    // (pokud chybi tak pro root cesty) pokusi ziskat GUID path. Pri neuspechu vrati FALSE. Pri
    // uspechu, vrati TRUE a nastavi 'mountPoint' a 'guidPath' (pokud jsou ruzne od NULL, musi
    // odkazovat na buffery o velikosti minimalne MAX_PATH; retezce budou zakonceny zpetnym lomitkem).
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI GetResolvedPathMountPointAndGUID(const char* path, char* mountPoint, char* guidPath) = 0;

    // nahradi v retezci posledni znak '.' decimalnim separatorem ziskanym ze systemu LOCALE_SDECIMAL
    // delka retezce muze narust, protoze separator muze mit podle MSDN az 4 znaky
    // vraci TRUE, pokud byl buffer dostatecne veliky a operaci se povedlo dokoncit, jinak vraci FALSE
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI PointToLocalDecimalSeparator(char* buffer, int bufferSize) = 0;

    // nastavi pro tento plugin pole icon-overlays; po nastaveni muze plugin v listingach vracet
    // index icon-overlaye (viz CFileData::IconOverlayIndex), ktery se ma zobrazit pres ikonu
    // polozky listingu, takto je mozne pouzit az 15 icon-overlays (indexy 0 az 14, protoze
    // index 15=ICONOVERLAYINDEX_NOTUSED aneb: nezobrazuj icon-overlay); 'iconOverlaysCount'
    // je pocet icon-overlays pro plugin; pole 'iconOverlays' obsahuje pro kazdy icon-overlay
    // postupne vsechny velikosti ikon: SALICONSIZE_16, SALICONSIZE_32 a SALICONSIZE_48 - tedy
    // v poli 'iconOverlays' je 3 * 'iconOverlaysCount' ikon; uvolneni ikon v poli 'iconOverlays'
    // zajisti Salamander (volani DestroyIcon()), samotne pole je vec volajiciho, pokud v poli
    // budou nejake NULL (napr. nezdaril se load ikony), funkce selze, ale platne ikony z pole
    // uvolni; pri zmene barev v systemu by mel plugin icon-overlays znovu nacist a znovu nastavit
    // touto funkci, idealni je reakce na PLUGINEVENT_COLORSCHANGED ve funkci
    // CPluginInterfaceAbstract::Event()
    // POZOR: pred Windows XP (ve W2K) je velikost ikony SALICONSIZE_48 jen 32 bodu!
    // omezeni: hlavni thread
    virtual void WINAPI SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays) = 0;

    // popis viz SalGetFileSize(), prvni rozdil je, ze se soubor zadava plnou cestou;
    // druhy je, ze 'err' muze byt NULL, pokud nestojime o kod chyby;
    virtual BOOL WINAPI SalGetFileSize2(const char* fileName, CQuadWord& size, DWORD* err) = 0;

    // zjisti velikost souboru, na ktery vede symlink 'fileName'; velikost vraci v 'size';
    // 'ignoreAll' je in + out, je-li TRUE vsechny chyby se ignoruji (pred akci je treba
    // priradit FALSE, jinak se okno s chybou vubec nezobrazi, pak uz nemenit);
    // pri chybe zobrazi standardni okno s dotazem Retry / Ignore / Ignore All / Cancel
    // s parentem 'parent'; pokud velikost uspesne zjisti, vraci TRUE; pri chybe a stisku
    // tlacitka Ignore / Igore All v okne s chybou, vraci FALSE a v 'cancel' vraci FALSE;
    // je-li 'ignoreAll' TRUE, okno se neukaze, na tlacitko se neceka, chova se jako by
    // uzivatel stiskl Ignore; pri chybe a stisku Cancel v okne s chybou vraci FALSE a
    // v 'cancel' vraci TRUE;
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI GetLinkTgtFileSize(HWND parent, const char* fileName, CQuadWord* size,
                                           BOOL* cancel, BOOL* ignoreAll) = 0;

    // smaze link na adresar (junction point, symbolic link, mount point); pri uspechu
    // vraci TRUE; pri chybe vraci FALSE a neni-li 'err' NULL, vraci kod chyby v 'err'
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI DeleteDirLink(const char* name, DWORD* err) = 0;

    // pokud ma soubor/adresar 'name' read-only atribut, pokusime se ho vypnout
    // (duvod: napr. aby sel smazat pres DeleteFile); pokud uz mame atributy 'name'
    // nactene, predame je v 'attr', je-li 'attr' -1, ctou se atributy 'name' z disku;
    // vraci TRUE pokud se provede pokus o zmenu atributu (uspech se nekontroluje);
    // POZNAMKA: vypina jen read-only atribut, aby v pripade vice hardlinku nedoslo
    // k zbytecne velke zmene atributu na zbyvajicich hardlinkach souboru (atributy
    // vsechny hardlinky sdili)
    // mozne volat z libovolneho threadu
    virtual BOOL WINAPI ClearReadOnlyAttr(const char* name, DWORD attr = -1) = 0;

    // zjisti, jestli prave probiha critical shutdown (nebo log off), pokud ano, vraci TRUE;
    // pri tomto shutdownu mame cas jen 5s na ulozeni konfigurace celeho programu
    // vcetne pluginu, takze casove narocnejsi operace musime vynechat, po uplynuti
    // 5s system nas process nasilne ukonci, vice viz WM_ENDSESSION, flag ENDSESSION_CRITICAL,
    // je to Vista+
    virtual BOOL WINAPI IsCriticalShutdown() = 0;

    // projde v threadu 'tid' (0 = aktualni) vsechna okna (EnumThreadWindows) a vsem enablovanym
    // a viditelnym dialogum (class name "#32770") vlastnenym oknem 'parent' postne WM_CLOSE;
    // pouziva se pri critical shutdown k odblokovani okna/dialogu, nad kterym jsou otevrene
    // modalni dialogy, hrozi-li vice vrstev, je nutne volat opakovane
    virtual void WINAPI CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid = 0) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_gen)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
