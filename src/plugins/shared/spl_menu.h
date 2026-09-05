// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_menu) // keep structures independent of the current alignment setting
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

class CSalamanderForOperationsAbstract;

//
// ****************************************************************************
// CSalamanderBuildMenuAbstract
//
// set of Salamander methods for building plugin menu
//
// subset of CSalamanderConnectAbstract methods; methods behave the same way,
// same constants are used, description see CSalamanderConnectAbstract

class CSalamanderBuildMenuAbstract
{
public:
    // icons are specified using CSalamanderBuildMenuAbstract::SetIconListForMenu, rest of the
    // description see CSalamanderConnectAbstract::AddMenuItem
    virtual void WINAPI AddMenuItem(int iconIndex, const char* name, DWORD hotKey, int id, BOOL callGetState,
                                    DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // icons are specified using CSalamanderBuildMenuAbstract::SetIconListForMenu, rest of the
    // description see CSalamanderConnectAbstract::AddSubmenuStart
    virtual void WINAPI AddSubmenuStart(int iconIndex, const char* name, int id, BOOL callGetState,
                                        DWORD state_or, DWORD state_and, DWORD skillLevel) = 0;

    // description see CSalamanderConnectAbstract::AddSubmenuEnd
    virtual void WINAPI AddSubmenuEnd() = 0;

    // sets bitmap with plugin icons for menu; bitmap must be allocated by calling
    // CSalamanderGUIAbstract::CreateIconList(), then created and filled using
    // methods of CGUIIconListAbstract interface; icon dimensions must be 16x16 pixels;
    // Salamander takes ownership of the bitmap object; after calling
    // must not destroy this function; Salamander only holds it in memory, it is not saved anywhere
    virtual void WINAPI SetIconListForMenu(CGUIIconListAbstract* iconList) = 0;
};

//
// ****************************************************************************
// CPluginInterfaceForMenuExtAbstract
//

// flags for menu item states (for menu extension plugins)
#define MENU_ITEM_STATE_ENABLED 0x01 // enabled; without this flag the item is disabled
#define MENU_ITEM_STATE_CHECKED 0x02 // before item is "check" or "radio" mark
#define MENU_ITEM_STATE_RADIO 0x04   // ignored without MENU_ITEM_STATE_CHECKED, \
                                     // "radio" mark; without this flag it is a "check" mark
#define MENU_ITEM_STATE_HIDDEN 0x08  // item must not appear in the menu at all

// Plugin API for Plugins-menu commands and their enabled/checked state.
class CPluginInterfaceForMenuExtAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForMenuExtEncapsulation)
    friend class CPluginInterfaceForMenuExtEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns the state of the menu item with identifier 'id'; return value is a combination
    // of flags (see MENU_ITEM_STATE_XXX); 'eventMask' see CSalamanderConnectAbstract::AddMenuItem
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) = 0;

    // executes the menu command with identifier 'id', 'eventMask' see
    // CSalamanderConnectAbstract::AddMenuItem, 'salamander' is the set of Salamander
    // methods usable for performing operations (CAUTION: it can be NULL, see the
    // description of CSalamanderGeneralAbstract::PostMenuExtCommand), 'parent' is the
    // message box parent; returns TRUE if selection in the panel should be cleared
    // (Cancel was not used, Skip could have been used), otherwise returns FALSE
    // (selection is not cleared);
    // CAUTION: If the command causes changes on some path (disk/FS), it should use
    //          CSalamanderGeneralAbstract::PostChangeOnPathNotification to inform
    //          panels without automatic refresh and open FS instances (active and detached)
    // NOTE: if the command works with files/directories from the path in the current panel
    //       or directly with that path, it must call
    //       CSalamanderGeneralAbstract::SetUserWorkedOnPanelPath for the current panel,
    //       otherwise the path in this panel will not be inserted into the List of Working
    //       Directories (Alt+F12)
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent,
                                        int id, DWORD eventMask) = 0;

    // displays help for the menu command with identifier 'id' (user presses Shift+F1,
    // finds this plugin's menu in the Plugins menu, and selects a command from it),
    // 'parent' is the message box parent; returns TRUE if some help was displayed,
    // otherwise the "Using Plugins" chapter from Salamander help is displayed
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id) = 0;

    // function for "dynamic menu extension", called only if FUNCTION_DYNAMICMENUEXT is passed
    // to SetBasicPluginData; builds the plugin menu on plugin load and then again immediately
    // before opening it from the Plugins menu or Plugin bar (also before opening the Keyboard
    // Shortcuts window from Plugins Manager); commands in the new menu should have the same IDs
    // as in the old one so user-assigned hotkeys remain assigned and so they can possibly work
    // as the last used command (see Plugins / Last Command); 'parent' is the message box parent,
    // 'salamander' is the set of methods for building the menu
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_menu)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
