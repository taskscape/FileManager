// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_base) // so that structures are independent of the set alignment
#pragma pack(4)
#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression
#endif                    // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

// in debug version we will test if source and target memory do not overlap (for memcpy they must not overlap)
#if defined(_DEBUG) && defined(TRACE_ENABLE)
#define memcpy _sal_safe_memcpy
#ifdef __cplusplus
extern "C"
{
#endif
    void* _sal_safe_memcpy(void* dest, const void* src, size_t count);
#ifdef __cplusplus
}
#endif
#endif // defined(_DEBUG) && defined(TRACE_ENABLE)

// following functions do not crash when working with invalid memory (even when working with NULL):
// lstrcpy, lstrcpyn, lstrlen and lstrcat (these are defined with suffix A or W, therefore
// we do not redefine them directly), in order to make debugging errors easier we need them to crash,
// because otherwise error is found later in a place where it may not be clear what
// caused it
#define lstrcpyA _sal_lstrcpyA
#define lstrcpyW _sal_lstrcpyW
#define lstrcpynA _sal_lstrcpynA
#define lstrcpynW _sal_lstrcpynW
#define lstrlenA _sal_lstrlenA
#define lstrlenW _sal_lstrlenW
#define lstrcatA _sal_lstrcatA
#define lstrcatW _sal_lstrcatW
#ifdef __cplusplus
extern "C"
{
#endif
    LPSTR _sal_lstrcpyA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcpyW(LPWSTR lpString1, LPCWSTR lpString2);
    LPSTR _sal_lstrcpynA(LPSTR lpString1, LPCSTR lpString2, int iMaxLength);
    LPWSTR _sal_lstrcpynW(LPWSTR lpString1, LPCWSTR lpString2, int iMaxLength);
    int _sal_lstrlenA(LPCSTR lpString);
    int _sal_lstrlenW(LPCWSTR lpString);
    LPSTR _sal_lstrcatA(LPSTR lpString1, LPCSTR lpString2);
    LPWSTR _sal_lstrcatW(LPWSTR lpString1, LPCWSTR lpString2);
#ifdef __cplusplus
}
#endif

// the original SDK that was part of VC6 had the value defined as 0x00000040 (year 1998, when the attribute was not yet used, it was introduced with W2K)
#if (FILE_ATTRIBUTE_ENCRYPTED != 0x00004000)
#pragma message(__FILE__ " ERROR: FILE_ATTRIBUTE_ENCRYPTED != 0x00004000. You have to install latest version of Microsoft SDK. This value has changed!")
#endif

class CSalamanderGeneralAbstract;
class CPluginDataInterfaceAbstract;
class CPluginInterfaceForArchiverAbstract;
class CPluginInterfaceForViewerAbstract;
class CPluginInterfaceForMenuExtAbstract;
class CPluginInterfaceForFSAbstract;
class CPluginInterfaceForThumbLoaderAbstract;
class CSalamanderGUIAbstract;
class CSalamanderSafeFileAbstract;
class CGUIIconListAbstract;

//
// ****************************************************************************
// CSalamanderDebugAbstract
//
// set of Salamander methods used for finding errors in debug and release versions

// CALLSTK_MEASURETIMES macro - enables time measurement spent preparing call-stack messages (ratio is measured against
//                              total function execution time)
//                              WARNING: must also be enabled for each plugin separately
// CALLSTK_DISABLEMEASURETIMES macro - suppresses time measurement spent preparing call-stack messages in DEBUG/SDK/PB version

#if (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
struct CCallStackMsgContext
{
    DWORD PushesCounterStart;                      // start state of Push counter called in this thread
    LARGE_INTEGER PushPerfTimeCounterStart;        // start state of time counter spent in Push methods called in this thread
    LARGE_INTEGER IgnoredPushPerfTimeCounterStart; // start state of time counter spent in unmeasured (ignored) Push methods called in this thread
    LARGE_INTEGER StartTime;                       // "time" of Push of this call-stack macro
    DWORD_PTR PushCallerAddress;                   // address of CALL_STACK_MESSAGE macro (address of Push)
};
#else  // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)
struct CCallStackMsgContext;
#endif // (defined(_DEBUG) || defined(CALLSTK_MEASURETIMES)) && !defined(CALLSTK_DISABLEMEASURETIMES)

class CSalamanderDebugAbstract
{
public:
    // outputs 'file'+'line'+'str' TRACE_I to TRACE SERVER - only in DEBUG/SDK/PB version of Salamander
    virtual void WINAPI TraceI(const char* file, int line, const char* str) = 0;
    virtual void WINAPI TraceIW(const WCHAR* file, int line, const WCHAR* str) = 0;

    // outputs 'file'+'line'+'str' TRACE_E to TRACE SERVER - only in DEBUG/SDK/PB version of Salamander
    virtual void WINAPI TraceE(const char* file, int line, const char* str) = 0;
    virtual void WINAPI TraceEW(const WCHAR* file, int line, const WCHAR* str) = 0;

    // registers new thread with TRACE (assigns Unique ID), 'thread'+'tid' returns
    // _beginthreadex and CreateThread, optional (UID is then -1)
    virtual void WINAPI TraceAttachThread(HANDLE thread, unsigned tid) = 0;

    // sets name of active thread for TRACE, optional (thread is marked as "unknown")
    // WARNING: requires thread registration with TRACE (see TraceAttachThread), otherwise does nothing
    virtual void WINAPI TraceSetThreadName(const char* name) = 0;
    virtual void WINAPI TraceSetThreadNameW(const WCHAR* name) = 0;

    // introduces things needed for CALL-STACK methods into the thread (see Push and Pop below),
    // in all called plugin methods it is possible to use CALL_STACK methods directly,
    // this method is used only for new plugin threads,
    // executes function 'threadBody' with parameter 'param', returns result of function 'threadBody'
    virtual unsigned WINAPI CallWithCallStack(unsigned(WINAPI* threadBody)(void*), void* param) = 0;

    // stores message on CALL-STACK ('format'+'args' see vsprintf), when application crashes
    // the CALL-STACK content is written to Bug Report window reporting the application crash
    virtual void WINAPI Push(const char* format, va_list args, CCallStackMsgContext* callStackMsgContext,
                             BOOL doNotMeasureTimes) = 0;

    // removes last message from CALL-STACK, call must be paired with Push
    virtual void WINAPI Pop(CCallStackMsgContext* callStackMsgContext) = 0;

    // sets name of active thread for VC debugger
    virtual void WINAPI SetThreadNameInVC(const char* name) = 0;

    // calls TraceSetThreadName and SetThreadNameInVC for 'name' (description see these two methods)
    virtual void WINAPI SetThreadNameInVCAndTrace(const char* name) = 0;

    // If we are not already connected to Trace Server, tries to establish connection (server
    // must be running). SDK version of Salamander only (including Preview Builds): if server
    // autostart is enabled and server is not running (e.g. user terminated it), tries to start it
    // before connecting.
    virtual void WINAPI TraceConnectToServer() = 0;

    // called for modules in which memory leaks may be reported, if memory leaks are detected,
    // all such registered modules are loaded "as image" (without module init) (during memory leak
    // check these modules are already unloaded), and only then memory leaks are listed = .cpp module
    // names are visible instead of "#File Error#"
    // can be called from any thread
    virtual void WINAPI AddModuleWithPossibleMemoryLeaks(const char* fileName) = 0;
};

//
// ****************************************************************************
// CSalamanderRegistryAbstract
//
// set of Salamander methods for working with system registry,
// used in CPluginInterfaceAbstract::LoadConfiguration
// and CPluginInterfaceAbstract::SaveConfiguration

class CSalamanderRegistryAbstract
{
public:
    // clears key 'key' of all subkeys and values, returns success
    virtual BOOL WINAPI ClearKey(HKEY key) = 0;

    // creates or opens existing subkey 'name' of key 'key', returns 'createdKey' and success;
    // obtained key ('createdKey') must be closed by calling CloseKey
    virtual BOOL WINAPI CreateKey(HKEY key, const char* name, HKEY& createdKey) = 0;

    // opens existing subkey 'name' of key 'key', returns 'openedKey' and success
    // obtained key ('openedKey') must be closed by calling CloseKey
    virtual BOOL WINAPI OpenKey(HKEY key, const char* name, HKEY& openedKey) = 0;

    // closes key opened via OpenKey or CreateKey
    virtual void WINAPI CloseKey(HKEY key) = 0;

    // deletes subkey 'name' of key 'key', returns success
    virtual BOOL WINAPI DeleteKey(HKEY key, const char* name) = 0;

    // loads value 'name'+'type'+'buffer'+'bufferSize' from key 'key', returns success
    virtual BOOL WINAPI GetValue(HKEY key, const char* name, DWORD type, void* buffer, DWORD bufferSize) = 0;

    // saves value 'name'+'type'+'data'+'dataSize' to key 'key', for strings it is possible
    // to specify 'dataSize' == -1 -> calculation of string length using strlen function,
    // returns success
    virtual BOOL WINAPI SetValue(HKEY key, const char* name, DWORD type, const void* data, DWORD dataSize) = 0;

    // deletes value 'name' of key 'key', returns success
    virtual BOOL WINAPI DeleteValue(HKEY key, const char* name) = 0;

    // extracts into 'bufferSize' required size for value 'name'+'type' from key 'key', returns success
    virtual BOOL WINAPI GetSize(HKEY key, const char* name, DWORD type, DWORD& bufferSize) = 0;
};

//
// ****************************************************************************
// CSalamanderConnectAbstract
//
// set of Salamander methods for connecting plugin to Salamander
// (custom pack/unpack + panel archiver view/edit + file viewer + menu-items)

// constants for CSalamanderConnectAbstract::AddMenuItem
#define MENU_EVENT_TRUE 0x0001                    // occurs always
#define MENU_EVENT_DISK 0x0002                    // source is windows directory ("c:\path" or UNC)
#define MENU_EVENT_THIS_PLUGIN_ARCH 0x0004        // source is archive of this plugin
#define MENU_EVENT_THIS_PLUGIN_FS 0x0008          // source is file-system of this plugin
#define MENU_EVENT_FILE_FOCUSED 0x0010            // focus is on file
#define MENU_EVENT_DIR_FOCUSED 0x0020             // focus is on directory
#define MENU_EVENT_UPDIR_FOCUSED 0x0040           // focus is on ".."
#define MENU_EVENT_FILES_SELECTED 0x0080          // files are selected
#define MENU_EVENT_DIRS_SELECTED 0x0100           // directories are selected
#define MENU_EVENT_TARGET_DISK 0x0200             // target is windows directory ("c:\path" or UNC)
#define MENU_EVENT_TARGET_THIS_PLUGIN_ARCH 0x0400 // target is archive of this plugin
#define MENU_EVENT_TARGET_THIS_PLUGIN_FS 0x0800   // target is file-system of this plugin
#define MENU_EVENT_SUBDIR 0x1000                  // directory is not root (contains "..")
// focus is on file for which this plugin provides "panel archiver view" or "panel archiver edit"
#define MENU_EVENT_ARCHIVE_FOCUSED 0x2000
// only 0x4000 is available (masks are composed both into DWORD and before that they are masked with 0x7FFF)

// determines for which user the item is intended
#define MENU_SKILLLEVEL_BEGINNER 0x0001     // intended for most important menu items, for beginners
#define MENU_SKILLLEVEL_INTERMEDIATE 0x0002 // also set for less frequently used commands; for more advanced users
#define MENU_SKILLLEVEL_ADVANCED 0x0004     // set for all commands (experts should have everything in menu)
#define MENU_SKILLLEVEL_ALL 0x0007          // helper constant combining all previous ones

// macro for preparing 'HotKey' for AddMenuItem()
// LOWORD - hot key (virtual key + modifiers) (LOBYTE - virtual key, HIBYTE - modifiers)
// mods: combination of HOTKEYF_CONTROL, HOTKEYF_SHIFT, HOTKEYF_ALT
// examples: SALHOTKEY('A', HOTKEYF_CONTROL | HOTKEYF_SHIFT), SALHOTKEY(VK_F1, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_EXT)
//#define SALHOTKEY(vk,mods,cst) ((DWORD)(((BYTE)(vk)|((WORD)((BYTE)(mods))<<8))|(((DWORD)(BYTE)(cst))<<16)))
#define SALHOTKEY(vk, mods) ((DWORD)(((BYTE)(vk) | ((WORD)((BYTE)(mods)) << 8))))

// macro for preparing 'hotKey' for AddMenuItem()
// tells Salamander that menu item will contain hot key (separated by '\t' character)
// Salamander will not complain via TRACE_E and will display hot key in Plugins menu
// WARNING: this is not a hot key that Salamander would deliver to plugin, it is really only a label
// if user assigns own hot key to this command in Plugin Manager, hint will be suppressed
#define SALHOTKEY_HINT ((DWORD)0x00020000)

class CSalamanderConnectAbstract
{
public:
    // adds plugin to list for "custom archiver pack",
    // 'title' is name of custom packer for user, 'defaultExtension' is standard extension
    // for new archives, if it is not upgrade of "custom pack" (or addition of whole plugin) and
    // 'update' is FALSE, call is ignored; if 'update' is TRUE, settings are overwritten with
    // new values 'title' and 'defaultExtension' - prevention against repeated 'update'==TRUE
    // (constant overwriting of settings) is necessary
    virtual void WINAPI AddCustomPacker(const char* title, const char* defaultExtension, BOOL update) = 0;

    // adds plugin to list for "custom archiver unpack",
    // 'title' is name of custom unpacker for user, 'masks' are archive file masks (they are used
    // to find what to unpack given archive with, separator is ';' (escape sequence for ';' is
    // ";;") and classic wildcards '*' and '?' plus '#' for '0'..'9' are used), if it is not upgrade
    // of "custom unpack" (or addition of whole plugin) and 'update' is FALSE call is ignored;
    // if 'update' is TRUE, settings are overwritten with new values 'title' and 'masks' - prevention
    // against repeated 'update'==TRUE (constant overwriting of settings) is necessary
    virtual void WINAPI AddCustomUnpacker(const char* title, const char* masks, BOOL update) = 0;

    // adds plugin to list for "panel archiver view/edit",
    // 'extensions' are archive extensions to be processed by this plugin
    // (separator is ';' (here ';' has no escape sequence) and wildcard '#' is used for
    // '0'..'9'), if 'edit' is TRUE, this plugin handles "panel archiver view/edit", otherwise only
    // "panel archiver view", if it is not upgrade of "panel archiver view/edit" (or addition
    // of whole plugin) and 'updateExts' is FALSE call is ignored; if 'updateExts' is TRUE,
    // it is addition of new archive extensions (ensuring presence of all extensions from 'extensions') - prevention
    // against repeated 'updateExts'==TRUE (constant reviving of extensions from 'extensions') is necessary
    virtual void WINAPI AddPanelArchiver(const char* extensions, BOOL edit, BOOL updateExts) = 0;

    // removes extension from list for "panel archiver view/edit" (only from items related to
    // this plugin), 'extension' is archive extension (single one; wildcard '#' is used for '0'..'9'),
    // prevention against repeated calls (constant deleting of 'extension') is necessary
    virtual void WINAPI ForceRemovePanelArchiver(const char* extension) = 0;

    // adds plugin to list for "file viewer",
    // 'masks' are viewer extensions to be processed by this plugin
    // (separator is ';' (escape sequence for ';' is ";;") and wildcards '*' and '?' are used,
    // if possible do not use spaces + character '|' is forbidden (inverse masks are not allowed)),
    // if it is not upgrade of "file viewer" (or addition of whole plugin) and 'force' is FALSE,
    // call is ignored; if 'force' is TRUE, adds 'masks' always (if they are not already on
    // list) - prevention against repeated 'force'==TRUE (constant adding of 'masks') is necessary
    virtual void WINAPI AddViewer(const char* masks, BOOL force) = 0;

    // removes mask from list for "file viewer" (only from items related to this plugin),
    // 'mask' is viewer extension (single one; wildcards '*' and '?' are used), prevention
    // against repeated calls (constant deleting of 'mask') is necessary
    virtual void WINAPI ForceRemoveViewer(const char* mask) = 0;

    // adds items to Plugins/"plugin name" menu in Salamander, 'iconIndex' is index
    // of item icon (-1=no icon; bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons; ignored for separator), 'name' is
    // item name (max. MAX_PATH - 1 characters) or NULL if it is separator (parameters
    // 'state_or'+'state_and' have no meaning in this case); 'hotKey' is hot key
    // of item obtained using SALHOTKEY macro; 'name' can contain hint for hot key,
    // separated by '\t' character, in variable 'hotKey' constant SALHOTKEY_HINT must be
    // assigned in this case, see comment for SALHOTKEY_HINT for more; 'id' is unique identification
    // number of item within plugin (for separator has meaning only if 'callGetState' is TRUE),
    // if 'callGetState' is TRUE, method CPluginInterfaceForMenuExtAbstract::GetMenuItemState
    // is called to determine item state (for separator only state MENU_ITEM_STATE_HIDDEN has meaning,
    // others are ignored), otherwise 'state_or'+'state_and' are used to calculate item state (enabled/disabled)
    // - when calculating item state, mask ('eventMask') is first assembled by logically adding
    // all events that occurred (events see MENU_EVENT_XXX), item will be "enable" if following
    // expression is TRUE:
    //   ('eventMask' & 'state_or') != 0 && ('eventMask' & 'state_and') == 'state_and',
    // parameter 'skillLevel' determines which user levels will display item (or separator);
    // value contains one or more (ORed) MENU_SKILLLEVEL_XXX constants;
    // menu items are updated on each plugin load (possible item change based on configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddMenuItem
    virtual void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey, int id, BOOL callGetState,
                                    DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // adds submenu to Plugins/"plugin name" menu in Salamander, 'iconIndex'
    // is index of submenu icon (-1=no icon; bitmap with icons specification
    // see CSalamanderConnectAbstract::SetBitmapWithIcons), 'name' is submenu
    // name (max. MAX_PATH - 1 characters), 'id' is unique identification number of menu
    // item within plugin (for submenu has meaning only if 'callGetState' is TRUE),
    // if 'callGetState' is TRUE, method CPluginInterfaceForMenuExtAbstract::GetMenuItemState
    // is called to determine submenu state (only states MENU_ITEM_STATE_ENABLED and
    // MENU_ITEM_STATE_HIDDEN have meaning, others are ignored), otherwise
    // 'state_or'+'state_and' are used to calculate item state (enabled/disabled) - calculation
    // of item state see CSalamanderConnectAbstract::AddMenuItem(), parameter 'skillLevel'
    // determines which user levels will display submenu, value contains one or
    // more (ORed) MENU_SKILLLEVEL_XXX constants, submenu is terminated by calling
    // CSalamanderConnectAbstract::AddSubmenuEnd();
    // menu items are updated on each plugin load (possible item change based on configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddSubmenuStart
    virtual void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id, BOOL callGetState,
                                        DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // terminates submenu in Plugins/"plugin name" menu in Salamander, next items will be
    // added to higher (parent) menu level;
    // menu items are updated on each plugin load (possible item change based on configuration)
    // WARNING: for "dynamic menu extension" use CSalamanderBuildMenuAbstract::AddSubmenuEnd
    virtual void WINAPI AddSubmenuEnd() = 0;

    // sets item for FS in Change Drive menu and in Drive bars; 'title' is its text,
    // 'iconIndex' is index of its icon (-1=no icon; bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons), 'title' can contain up to three columns
    // separated by '\t' (see Alt+F1/F2 menu); item visibility can be set
    // from Plugins Manager or directly from plugin using method
    // CSalamanderGeneralAbstract::SetChangeDriveMenuItemVisibility
    virtual void WINAPI SetChangeDriveMenuItem(const char* title, int iconIndex) = 0;

    // informs Salamander that plugin can load thumbnails from files matching
    // group mask 'masks' (separator is ';' (escape sequence for ';' is ";;") and wildcards
    // '*' and '?' are used); for loading thumbnail
    // CPluginInterfaceForThumbLoaderAbstract::LoadThumbnail is called
    virtual void WINAPI SetThumbnailLoader(const char* masks) = 0;

    // sets bitmap with plugin icons; Salamander copies bitmap content to internal
    // structures, plugin is responsible for bitmap destruction (from Salamander side
    // bitmap is used only during this function); number of icons is derived from
    // bitmap width, icons are always 16x16 pixels; transparent part of icons is magenta
    // color (RGB(255,0,255)), bitmap color depth can be 4 or 8 bits (16 or 256
    // colors), ideal is to have both color variants prepared and select from them according
    // to result of method CSalamanderGeneralAbstract::CanUse256ColorsBitmap()
    // WARNING: this method is obsolete, does not support alpha transparency, use
    //        SetIconListForGUI() instead
    virtual void WINAPI SetBitmapWithIcons(HBITMAP bitmap) = 0;

    // sets index of plugin icon, which is used for plugin in Plugins/Plugins Manager window,
    // in Help/About Plugin menu and possibly also for plugin submenu in Plugins menu (details
    // see CSalamanderConnectAbstract::SetPluginMenuAndToolbarIcon()); if plugin does not call this
    // method, standard Salamander icon for plugin is used; 'iconIndex'
    // is icon index to be set (bitmap with icons specification see
    // CSalamanderConnectAbstract::SetBitmapWithIcons)
    virtual void WINAPI SetPluginIcon(int iconIndex) = 0;

    // sets index of icon for plugin submenu, which is used for plugin submenu
    // in Plugins menu and possibly also in top toolbar for drop-down button serving
    // to display plugin submenu; if plugin does not call this method, plugin icon
    // is used for plugin submenu in Plugins menu (setting see
    // CSalamanderConnectAbstract::SetPluginIcon) and button for plugin will not appear
    // in top toolbar; 'iconIndex' is icon index to be set (-1=plugin icon should be used,
    // see CSalamanderConnectAbstract::SetPluginIcon(); bitmap with icons specification
    // see CSalamanderConnectAbstract::SetBitmapWithIcons);
    virtual void WINAPI SetPluginMenuAndToolbarIcon(int iconIndex) = 0;

    // sets bitmap with plugin icons; bitmap must be allocated by calling
    // CSalamanderGUIAbstract::CreateIconList() and subsequently created and filled using
    // CGUIIconListAbstract interface methods; icon dimensions must be 16x16 pixels;
    // Salamander takes over bitmap object into its management, plugin must not destroy it
    // after calling this function; bitmap is stored in Salamander configuration
    // so that icons can be used on next launch without plugin load, therefore insert
    // only necessary icons into it
    virtual void WINAPI SetIconListForGUI(CGUIIconListAbstract* iconList) = 0;
};

//
// ****************************************************************************
// CDynamicString
//
// dynamic string: reallocates as needed

class CDynamicString
{
public:
    // returns TRUE if string 'str' of length 'len' was successfully added; if 'len' is -1,
    // 'len' is determined as "strlen(str)" (addition without terminating zero); if 'len' is -2,
    // 'len' is determined as "strlen(str)+1" (addition including terminating zero)
    virtual BOOL WINAPI Add(const char* str, int len = -1) = 0;
};

//
// ****************************************************************************
// CPluginInterfaceAbstract
//
// set of plugin methods that Salamander needs to work with plugin
//
// For better clarity separate parts are for:
// archivers - see CPluginInterfaceForArchiverAbstract,
// viewers - see CPluginInterfaceForViewerAbstract,
// menu extension - see CPluginInterfaceForMenuExtAbstract,
// file-systems - see CPluginInterfaceForFSAbstract,
// thumbnail loaders - see CPluginInterfaceForThumbLoaderAbstract.
// Parts are connected to CPluginInterfaceAbstract through CPluginInterfaceAbstract::GetInterfaceForXXX

// flags indicating which functions plugin provides (which methods of CPluginInterfaceAbstract
// descendant are actually implemented in plugin):
#define FUNCTION_PANELARCHIVERVIEW 0x0001     // methods for "panel archiver view"
#define FUNCTION_PANELARCHIVEREDIT 0x0002     // methods for "panel archiver edit"
#define FUNCTION_CUSTOMARCHIVERPACK 0x0004    // methods for "custom archiver pack"
#define FUNCTION_CUSTOMARCHIVERUNPACK 0x0008  // methods for "custom archiver unpack"
#define FUNCTION_CONFIGURATION 0x0010         // Configuration method
#define FUNCTION_LOADSAVECONFIGURATION 0x0020 // methods for "load/save configuration"
#define FUNCTION_VIEWER 0x0040                // methods for "file viewer"
#define FUNCTION_FILESYSTEM 0x0080            // methods for "file system"
#define FUNCTION_DYNAMICMENUEXT 0x0100        // methods for "dynamic menu extension"

// codes of various events (and meaning of parameter 'param'), received by method CPluginInterfaceAbstract::Event():
// colors have changed (due to system colors change / WM_SYSCOLORCHANGE or due to configuration change); plugin can
// retrieve new versions of Salamander colors via CSalamanderGeneralAbstract::GetCurrentColor;
// if plugin has file-system with icons of type pitFromPlugin, it should recolor background of image-list
// with simple icons to color SALCOL_ITEM_BK_NORMAL; 'param' is ignored here
#define PLUGINEVENT_COLORSCHANGED 0

// Salamander configuration has changed; plugin can retrieve new versions of Salamander
// configuration parameters via CSalamanderGeneralAbstract::GetConfigParameter;
// 'param' is ignored here
#define PLUGINEVENT_CONFIGURATIONCHANGED 1

// left and right panel have been swapped (Swap Panels - Ctrl+U)
// 'param' is ignored here
#define PLUGINEVENT_PANELSSWAPPED 2

// active panel has changed (switching between panels)
// 'param' is PANEL_LEFT or PANEL_RIGHT - indicates activated panel
#define PLUGINEVENT_PANELACTIVATED 3

// Salamander received WM_SETTINGCHANGE and based on it regenerated fonts for toolbars.
// Then sent this event to all plugins so they have opportunity to call SetFont()
// method on their toolbars;
// 'param' is ignored here
#define PLUGINEVENT_SETTINGCHANGE 4

// event codes in Password Manager, received by method CPluginInterfaceAbstract::PasswordManagerEvent():
#define PME_MASTERPASSWORDCREATED 1 // user created master password (passwords need to be encrypted)
#define PME_MASTERPASSWORDCHANGED 2 // user changed master password (passwords need to be decrypted and then encrypted again)
#define PME_MASTERPASSWORDREMOVED 3 // user removed master password (passwords need to be decrypted)

class CPluginInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // ochrana proti nespravnemu primemu volani metod (viz CPluginInterfaceEncapsulation)
    friend class CPluginInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // called as reaction to About button in Plugins window or command from Help/About Plugins menu
    virtual void WINAPI About(HWND parent) = 0;

    // called before plugin unload (naturally only if SalamanderPluginEntry returned
    // this object and not NULL), returns TRUE if unload can proceed,
    // 'parent' is parent of messagebox, 'force' is TRUE if return value is not taken into account,
    // if returns TRUE, this object and all others obtained from it will not be used
    // anymore and plugin unload will occur; if critical shutdown is in progress (see
    // CSalamanderGeneralAbstract::IsCriticalShutdown), it makes no sense to ask user anything
    // (do not open any windows anymore)
    // WARNING!!! It is necessary to terminate all plugin threads (if Release returns TRUE,
    // FreeLibrary is called on plugin .SPL => plugin code is unmapped from memory => threads
    // then have nothing to execute => usually neither bug-report nor Windows exception info appears)
    virtual BOOL WINAPI Release(HWND parent, BOOL force) = 0;

    // function for loading default configuration and for "load/save configuration" (load from plugin's
    // private key in registry), 'parent' is parent of messagebox, if 'regKey' == NULL, it is
    // default configuration, 'registry' is object for working with registry, this method is always called
    // after SalamanderPluginEntry and before other calls (load from private key is called if
    // this function is provided by plugin and key exists in registry, otherwise only load of default
    // configuration is called)
    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) = 0;

    // function for "load/save configuration", called to save plugin configuration to its private
    // key in registry, 'parent' is parent of messagebox, 'registry' is object for working with registry,
    // if Salamander saves configuration, it also calls this method (if provided by plugin); Salamander
    // also offers saving plugin configuration on its unload (e.g. manually from Plugins Manager),
    // in this case saving is performed only if Salamander key exists in registry
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry) = 0;

    // called as reaction to Configurate button in Plugins window
    virtual void WINAPI Configuration(HWND parent) = 0;

    // called for connecting plugin to Salamander, called after LoadConfiguration,
    // 'parent' is parent of messagebox, 'salamander' is set of methods for connecting plugin

    /*  RULES FOR IMPLEMENTING CONNECT METHOD
        (plugins must have stored configuration version - see DEMOPLUGin,
         variable ConfigVersion and constant CURRENT_CONFIG_VERSION; below is
         illustrative EXAMPLE of adding extension "dmp2" to DEMOPLUGin):

      -with each change it is necessary to increase configuration version number - CURRENT_CONFIG_VERSION
       (in first version of Connect method CURRENT_CONFIG_VERSION=1)
      -into basic part (before conditions "if (ConfigVersion < YYY)"):
        -write code for plugin installation (very first plugin load):
         see CSalamanderConnectAbstract methods
        -during upgrades it is necessary to update extension lists for installation for "custom archiver
         unpack" (AddCustomUnpacker), "panel archiver view/edit" (AddPanelArchiver),
         "file viewer" (AddViewer), menu items (AddMenuItem), etc.
        -for AddPanelArchiver and AddViewer calls leave 'updateExts' and 'force' FALSE
         (otherwise we would force user not only new but also old extensions which maybe
         were manually deleted)
        -for AddCustomPacker/AddCustomUnpacker calls put condition in 'update' parameter
         "ConfigVersion < XXX", where XXX is number of last version where extensions
         for custom packers/unpackers were changed (both calls need to be considered separately;
         here for simplicity we force all extensions on user, if they deleted or added some,
         they are out of luck, they will have to do it manually again)
        -AddMenuItem, SetChangeDriveMenuItem and SetThumbnailLoader work the same on each plugin load
         (installation/upgrades do not differ - always starting from scratch)
      -only for upgrades: into part for upgrades (after basic part):
        -add condition "if (ConfigVersion < XXX)", where XXX is new value
         of constant CURRENT_CONFIG_VERSION + add comment for this version;
         in body of this condition we call:
          -if extensions were added for "panel archiver", call
           "AddPanelArchiver(PPP, EEE, TRUE)", where PPP are only new extensions separated
           by semicolon and EEE is TRUE/FALSE ("panel view+edit"/"only panel view")
          -if extensions were added for "viewer", call "AddViewer(PPP, TRUE)",
           where PPP are only new extensions separated by semicolon
          -if some old extensions for "viewer" should be deleted, call
           "ForceRemoveViewer(PPP)" for each such extension PPP
          -if some old extensions for "panel archiver" should be deleted, call
           "ForceRemovePanelArchiver(PPP)" for each such extension PPP

      CHECK: after these changes I recommend testing if it works as it should,
                it is enough to compile plugin and try to load it into Salam, automatic
                upgrade from previous version should occur (without need to
                remove and add plugin):
                -see menu Options/Configuration:
                  -Viewers are on Viewers page: you will find added extensions,
                   verify that removed extensions no longer exist
                  -Panel Archivers are on Archives Associations in Panels page:
                   you will find added extensions
                  -Custom Unpackers are on Unackers in Unpack Dialog Box page:
                   find your plugin and check if mask list is OK
                -check new appearance of plugin submenu (in Plugins menu)
                -check new appearance of Change Drive menu (Alt+F1/F2)
                -check in Plugins Manager (in Plugins menu) thumbnail masks:
                 focus your plugin, then check editbox "Thumbnails"
              +finally you can also try to remove and add plugin to see if
               plugin "installation" works: check see all previous points

      NOTE: when adding extensions for "panel archiver" it is also necessary to add
                extension list to 'extensions' parameter of SetBasicPluginData method

      EXAMPLE OF ADDING EXTENSION "dmp2" TO VIEWER AND ARCHIVER:
        (lines starting with "-" were removed, lines starting with "+" added,
         symbol "=====" at line beginning indicates interruption of continuous code section)
        Summary of changes:
          -configuration version increased from 2 to 3:
            -added comment for version 3
            -increased CURRENT_CONFIG_VERSION to 3
          -adding extension "dmp2" to 'extensions' parameter of SetBasicPluginData
           (because we are adding extension "dmp2" for "panel archiver")
          -adding mask "*.dmp2" to AddCustomUnpacker + increasing version from 1 to 3
           in condition (because we are adding extension "dmp2" for "custom unpacker")
          -adding extension "dmp2" to AddPanelArchiver (because we are adding extension
           "dmp2" for "panel archiver")
          -adding mask "*.dmp2" to AddViewer (because we are adding extension "dmp2"
           for "viewer")
          -adding condition for upgrade to version 3 + comment for this upgrade,
           condition body:
            -calling AddPanelArchiver for extension "dmp2" with 'updateExts' TRUE
             (because we are adding extension "dmp2" for "panel archiver")
            -calling AddViewer for mask "*.dmp2" with 'force' TRUE (because
             we are adding extension "dmp2" for "viewer")
=====
  // ConfigVersion: 0 - no configuration was loaded from Registry (this is plugin installation),
  //                1 - first configuration version
  //                2 - second configuration version (some values added to configuration)
+ //                3 - third configuration version (adding extension "dmp2")

  int ConfigVersion = 0;
- #define CURRENT_CONFIG_VERSION 2
+ #define CURRENT_CONFIG_VERSION 3
  const char *CONFIG_VERSION = "Version";
=====
  // set basic plugin information
  salamander->SetBasicPluginData("Salamander Demo Plugin",
                                 FUNCTION_PANELARCHIVERVIEW | FUNCTION_PANELARCHIVEREDIT |
                                 FUNCTION_CUSTOMARCHIVERPACK | FUNCTION_CUSTOMARCHIVERUNPACK |
                                 FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION |
                                 FUNCTION_VIEWER | FUNCTION_FILESYSTEM,
                                 "2.0",
                                 "Copyright © 1999-2023 Open Salamander Authors",
                                 "This plugin should help you to make your own plugins.",
-                                "DEMOPLUG", "dmp", "dfs");
+                                "DEMOPLUG", "dmp;dmp2", "dfs");
=====
  void WINAPI
  CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract *salamander)
  {
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    // basic part:
    salamander->AddCustomPacker("DEMOPLUG (Plugin)", "dmp", FALSE);
-   salamander->AddCustomUnpacker("DEMOPLUG (Plugin)", "*.dmp", ConfigVersion < 1);
+   salamander->AddCustomUnpacker("DEMOPLUG (Plugin)", "*.dmp;*.dmp2", ConfigVersion < 3);
-   salamander->AddPanelArchiver("dmp", TRUE, FALSE);
+   salamander->AddPanelArchiver("dmp;dmp2", TRUE, FALSE);
-   salamander->AddViewer("*.dmp", FALSE);
+   salamander->AddViewer("*.dmp;*.dmp2", FALSE);
===== (I skipped adding menu items, setting icons and thumbnail masks)
    // part for upgrades:
+   if (ConfigVersion < 3)   // version 3: adding extension "dmp2"
+   {
+     salamander->AddPanelArchiver("dmp2", TRUE, TRUE);
+     salamander->AddViewer("*.dmp2", TRUE);
+   }
  }
=====
    */
    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander) = 0;

    // releases interface 'pluginData' that Salamander obtained from plugin by calling
    // CPluginInterfaceForArchiverAbstract::ListArchive or
    // CPluginFSInterfaceAbstract::ListCurrentPath; before this call
    // file and directory data (CFileData::PluginData) are released using methods of
    // CPluginDataInterfaceAbstract
    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) = 0;

    // returns archiver interface; plugin must return this interface if it has
    // at least one of following functions (see SetBasicPluginData): FUNCTION_PANELARCHIVERVIEW,
    // FUNCTION_PANELARCHIVEREDIT, FUNCTION_CUSTOMARCHIVERPACK and/or FUNCTION_CUSTOMARCHIVERUNPACK;
    // if plugin does not contain archiver, returns NULL
    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() = 0;

    // returns viewer interface; plugin must return this interface if it has function
    // (see SetBasicPluginData) FUNCTION_VIEWER; if plugin does not contain viewer, returns NULL
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() = 0;

    // returns menu extension interface; plugin must return this interface if it adds
    // items to menu (see CSalamanderConnectAbstract::AddMenuItem) or if it has
    // function (see SetBasicPluginData) FUNCTION_DYNAMICMENUEXT; otherwise returns NULL
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() = 0;

    // returns file-system interface; plugin must return this interface if it has function
    // (see SetBasicPluginData) FUNCTION_FILESYSTEM; if plugin does not contain file-system, returns NULL
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() = 0;

    // returns thumbnail loader interface; plugin must return this interface if it informed
    // Salamander that it can load thumbnails (see CSalamanderConnectAbstract::SetThumbnailLoader);
    // if plugin cannot load thumbnails, returns NULL
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() = 0;

    // receives various events, see event codes PLUGINEVENT_XXX; called only if plugin
    // is loaded; 'param' is event parameter
    // WARNING: can be called anytime after plugin entry-point completion (SalamanderPluginEntry)
    virtual void WINAPI Event(int event, DWORD param) = 0;

    // user wishes all histories to be deleted (started Clear History from configuration
    // from History page); history here means everything that is created automatically from user-entered
    // values (e.g. list of texts executed in command line, list of current paths on
    // individual drives, etc.); this does not include lists created by user - e.g. hot-paths, user-menu,
    // etc.; 'parent' is parent of possible messageboxes; after saving configuration history must not
    // remain in registry; if plugin has open windows containing histories (comboboxes), it must
    // clear histories there too
    virtual void WINAPI ClearHistory(HWND parent) = 0;

    // receives information about change on path 'path' (if 'includingSubdirs' is TRUE, then
    // it includes change in subdirectories of path 'path'); this method can be used e.g.
    // for invalidating/clearing cache of files/directories; NOTE: for plugin file-systems (FS)
    // there is method CPluginFSInterfaceAbstract::AcceptChangeOnPathNotification()
    virtual void WINAPI AcceptChangeOnPathNotification(const char* path, BOOL includingSubdirs) = 0;

    // this method is called only for plugin that uses Password Manager (see
    // CSalamanderGeneralAbstract::SetPluginUsesPasswordManager()):
    // informs plugin about changes in Password Manager; 'parent' is parent of possible
    // messageboxes/dialogs; 'event' contains event, see PME_XXX
    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) = 0;
};

//
// ****************************************************************************
// CSalamanderPluginEntryAbstract
//
// set of Salamander methods used in SalamanderPluginEntry

// flags informing about reason for plugin load (see method CSalamanderPluginEntryAbstract::GetLoadInformation)
#define LOADINFO_INSTALL 0x0001          // first plugin load (installation into Salamander)
#define LOADINFO_NEWSALAMANDERVER 0x0002 // new version of Salamander (reinstallation of all plugins from \
                                         // plugins subdirectory), loads all plugins (possible \
                                         // upgrade of all)
#define LOADINFO_NEWPLUGINSVER 0x0004    // change in plugins.ver file (plugin installation/upgrade), \
                                         // for simplicity loads all plugins (possible upgrade \
                                         // of all)
#define LOADINFO_LOADONSTART 0x0008      // load occurred because "load on start" flag was found

class CSalamanderPluginEntryAbstract
{
public:
    // returns Salamander version, see spl_vers.h, constants LAST_VERSION_OF_SALAMANDER and REQUIRE_LAST_VERSION_OF_SALAMANDER
    virtual int WINAPI GetVersion() = 0;

    // returns Salamander "parent" window (parent for messageboxes)
    virtual HWND WINAPI GetParentWindow() = 0;

    // returns pointer to interface for Salamander debug functions,
    // interface is valid for entire plugin lifetime (not only within
    // "SalamanderPluginEntry" function) and it is only reference, so it is not released
    virtual CSalamanderDebugAbstract* WINAPI GetSalamanderDebug() = 0;

    // sets basic data about plugin (data that Salamander remembers about plugin along with DLL file name),
    // must be called, otherwise plugin cannot be connected;
    // 'pluginName' is plugin name; 'functions' contains ORed all functions that plugin
    // supports (see constants FUNCTION_XXX); 'version'+'copyright'+'description' are data for
    // user displayed in Plugins window; 'regKeyName' is proposed name of private key
    // for configuration storage in registry (ignored without FUNCTION_LOADSAVECONFIGURATION);
    // 'extensions' are basic extensions (e.g. only "ARJ"; "A01", etc. not anymore) of processed
    // archives separated by ';' (here ';' has no escape sequence) - Salamander uses these extensions
    // only when searching for replacement for removed panel archivers (occurs when removing plugin;
    // solving problem "what will now handle extension XXX, when original associated archiver
    // was removed as part of plugin PPP?") (ignored without FUNCTION_PANELARCHIVERVIEW and without FUNCTION_PANELARCHIVEREDIT);
    // 'fsName' is proposed name (obtaining assigned name is done using
    // CSalamanderGeneralAbstract::GetPluginFSName) of file system (ignored without FUNCTION_FILESYSTEM,
    // allowed characters are 'a-zA-Z0-9_+-', min. length 2 characters), if plugin needs
    // more file system names, it can use method CSalamanderPluginEntryAbstract::AddFSName;
    // returns TRUE on successful data acceptance
    virtual BOOL WINAPI SetBasicPluginData(const char* pluginName, DWORD functions,
                                           const char* version, const char* copyright,
                                           const char* description, const char* regKeyName = NULL,
                                           const char* extensions = NULL, const char* fsName = NULL) = 0;

    // returns pointer to interface for generally usable Salamander functions,
    // interface is valid for entire plugin lifetime (not only within
    // "SalamanderPluginEntry" function) and it is only reference, so it is not released
    virtual CSalamanderGeneralAbstract* WINAPI GetSalamanderGeneral() = 0;

    // returns information associated with plugin load; information is returned in DWORD value
    // as logical sum of LOADINFO_XXX flags (for testing flag presence use
    // condition: (GetLoadInformation() & LOADINFO_XXX) != 0)
    virtual DWORD WINAPI GetLoadInformation() = 0;

    // loads module with language-dependent resources (SLG file); always tries to load module
    // of same language as Salamander is currently running in, if it does not find such module (or
    // version does not match), it lets user select alternative module (if more than one
    // alternative exists + if it does not have saved user selection from previous plugin load);
    // if it does not find any module, returns NULL -> plugin should terminate;
    // 'parent' is parent of messageboxes with errors and dialog for selecting alternative
    // language module; 'pluginName' is plugin name (so user knows which plugin
    // it is about in error message or when selecting alternative language module)
    // WARNING: this method can be called only once; obtained language module handle
    //        is released automatically on plugin unload
    virtual HINSTANCE WINAPI LoadLanguageModule(HWND parent, const char* pluginName) = 0;

    // returns ID of current language selected for Salamander environment (e.g. english.slg =
    // English (US) = 0x409, czech.slg = Czech = 0x405)
    virtual WORD WINAPI GetCurrentSalamanderLanguageID() = 0;

    // returns pointer to interface providing modified Windows controls used
    // in Salamander, interface is valid for entire plugin lifetime (not only
    // within "SalamanderPluginEntry" function) and it is only reference, so it is not released
    virtual CSalamanderGUIAbstract* WINAPI GetSalamanderGUI() = 0;

    // returns pointer to interface for comfortable work with files,
    // interface is valid for entire plugin lifetime (not only within
    // "SalamanderPluginEntry" function) and it is only reference, so it is not released
    virtual CSalamanderSafeFileAbstract* WINAPI GetSalamanderSafeFile() = 0;

    // sets URL to be displayed in Plugins Manager window as plugin home-page;
    // Salamander maintains value until next plugin load (URL is displayed even for
    // not loaded plugins); on each plugin load URL must be set again, otherwise
    // no URL will be displayed (defense against holding invalid home-page URL)
    virtual void WINAPI SetPluginHomePageURL(const char* url) = 0;

    // adds another file system name; without FUNCTION_FILESYSTEM in 'functions' parameter
    // when calling SetBasicPluginData method, this method always returns only error;
    // 'fsName' is proposed name (obtaining assigned name is done using
    // CSalamanderGeneralAbstract::GetPluginFSName) of file system (allowed characters are
    // 'a-zA-Z0-9_+-', min. length 2 characters); in 'newFSNameIndex' (must not be NULL)
    // index of newly added file system name is returned; returns TRUE on success;
    // returns FALSE on fatal error - in this case 'newFSNameIndex' is ignored
    // limitation: must not be called before SetBasicPluginData method
    virtual BOOL WINAPI AddFSName(const char* fsName, int* newFSNameIndex) = 0;
};

//
// ****************************************************************************
// FSalamanderPluginEntry
//
// Open Salamander 1.6 or Later Plugin Entry Point Function Type,
// plugin exports this function as "SalamanderPluginEntry" and Salamander calls it
// for plugin connection at plugin load time
// returns plugin interface on successful connection, otherwise NULL,
// plugin interface is released by calling its Release method before plugin unload

typedef CPluginInterfaceAbstract*(WINAPI* FSalamanderPluginEntry)(CSalamanderPluginEntryAbstract* salamander);

//
// ****************************************************************************
// FSalamanderPluginGetReqVer
//
// Open Salamander 2.5 Beta 2 or Later Plugin Get Required Version of Salamander Function Type,
// plugin exports this function as "SalamanderPluginGetReqVer" and Salamander calls it
// as first plugin function (before "SalamanderPluginGetSDKVer" and "SalamanderPluginEntry")
// at plugin load time;
// returns Salamander version for which plugin is built (oldest version into which plugin can be loaded)

typedef int(WINAPI* FSalamanderPluginGetReqVer)();

//
// ****************************************************************************
// FSalamanderPluginGetSDKVer
//
// Open Salamander 2.52 beta 2 (PB 22) or Later Plugin Get SDK Version Function Type,
// plugin optionally exports this function as "SalamanderPluginGetSDKVer" and Salamander
// tries to call it as second plugin function (before "SalamanderPluginEntry")
// at plugin load time;
// returns SDK version used for plugin build (informs Salamander which methods
// plugin provides); exporting "SalamanderPluginGetSDKVer" makes sense only if
// "SalamanderPluginGetReqVer" returns number smaller than LAST_VERSION_OF_SALAMANDER; it is recommended
// to return LAST_VERSION_OF_SALAMANDER directly

typedef int(WINAPI* FSalamanderPluginGetSDKVer)();

// ****************************************************************************
// SalIsWindowsVersionOrGreater
//
// Based on SDK 8.1 VersionHelpers.h
// Indicates if the current OS version matches, or is greater than, the provided
// version information. This function is useful in confirming a version of Windows
// Server that doesn't share a version number with a client release.
// http://msdn.microsoft.com/en-us/library/windows/desktop/dn424964%28v=vs.85%29.aspx
//

#ifdef __BORLANDC__
inline void* SecureZeroMemory(void* ptr, int cnt)
{
    char* vptr = (char*)ptr;
    while (cnt)
    {
        *vptr++ = 0;
        cnt--;
    }
    return ptr;
}
#endif // __BORLANDC__

inline BOOL SalIsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi;
    DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
                                                                                                   VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                                               VER_MINORVERSION, VER_GREATER_EQUAL),
                                                           VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    SecureZeroMemory(&osvi, sizeof(osvi)); // replacement for memset (does not require RTL)
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion = wMajorVersion;
    osvi.dwMinorVersion = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

// Find Windows version using bisection method and VerifyVersionInfo.
// Author:   M1xA, www.m1xa.com
// Licence:  MIT
// Version:  1.0
// https://bitbucket.org/AnyCPU/findversion/src/ebdec778fdbcdee67ac9a4d520239e134e047d8d/include/findversion.h?at=default
// Tested on: Windows 2000 .. Windows 8.1.
//
// WARNING: This function is ***SLOW_HACK***, use SalIsWindowsVersionOrGreater() instead (if you can).

#define M1xA_FV_EQUAL 0
#define M1xA_FV_LESS -1
#define M1xA_FV_GREAT 1
#define M1xA_FV_MIN_VALUE 0
#define M1xA_FV_MINOR_VERSION_MAX_VALUE 16
inline int M1xA_testValue(OSVERSIONINFOEX* value, DWORD verPart, DWORDLONG eq, DWORDLONG gt)
{
    if (VerifyVersionInfo(value, verPart, eq) == FALSE)
    {
        if (VerifyVersionInfo(value, verPart, gt) == TRUE)
            return M1xA_FV_GREAT;
        return M1xA_FV_LESS;
    }
    else
        return M1xA_FV_EQUAL;
}

#define M1xA_findPartTemplate(T) \
    inline BOOL M1xA_findPart##T(T* part, DWORD partType, OSVERSIONINFOEX* ret, T a, T b) \
    { \
        int funx = M1xA_FV_EQUAL; \
\
        DWORDLONG const eq = VerSetConditionMask(0, partType, VER_EQUAL); \
        DWORDLONG const gt = VerSetConditionMask(0, partType, VER_GREATER); \
\
        T* p = part; \
\
        *p = (T)((a + b) / 2); \
\
        while ((funx = M1xA_testValue(ret, partType, eq, gt)) != M1xA_FV_EQUAL) \
        { \
            switch (funx) \
            { \
            case M1xA_FV_GREAT: \
                a = *p; \
                break; \
            case M1xA_FV_LESS: \
                b = *p; \
                break; \
            } \
\
            *p = (T)((a + b) / 2); \
\
            if (*p == a) \
            { \
                if (M1xA_testValue(ret, partType, eq, gt) == M1xA_FV_EQUAL) \
                    return TRUE; \
\
                *p = b; \
\
                if (M1xA_testValue(ret, partType, eq, gt) == M1xA_FV_EQUAL) \
                    return TRUE; \
\
                a = 0; \
                b = 0; \
                *p = 0; \
            } \
\
            if (a == b) \
            { \
                *p = 0; \
                return FALSE; \
            } \
        } \
\
        return TRUE; \
    }
M1xA_findPartTemplate(DWORD)
    M1xA_findPartTemplate(WORD)
        M1xA_findPartTemplate(BYTE)

            inline BOOL SalGetVersionEx(OSVERSIONINFOEX* osVer, BOOL versionOnly)
{
    BOOL ret = TRUE;
    ZeroMemory(osVer, sizeof(OSVERSIONINFOEX));
    osVer->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (!versionOnly)
    {
        ret &= M1xA_findPartDWORD(&osVer->dwPlatformId, VER_PLATFORMID, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
    }
    ret &= M1xA_findPartDWORD(&osVer->dwMajorVersion, VER_MAJORVERSION, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
    ret &= M1xA_findPartDWORD(&osVer->dwMinorVersion, VER_MINORVERSION, osVer, M1xA_FV_MIN_VALUE, M1xA_FV_MINOR_VERSION_MAX_VALUE);
    if (!versionOnly)
    {
        ret &= M1xA_findPartDWORD(&osVer->dwBuildNumber, VER_BUILDNUMBER, osVer, M1xA_FV_MIN_VALUE, MAXDWORD);
        ret &= M1xA_findPartWORD(&osVer->wServicePackMajor, VER_SERVICEPACKMAJOR, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartWORD(&osVer->wServicePackMinor, VER_SERVICEPACKMINOR, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartWORD(&osVer->wSuiteMask, VER_SUITENAME, osVer, M1xA_FV_MIN_VALUE, MAXWORD);
        ret &= M1xA_findPartBYTE(&osVer->wProductType, VER_PRODUCT_TYPE, osVer, M1xA_FV_MIN_VALUE, MAXBYTE);
    }
    return ret;
}

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_base)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
