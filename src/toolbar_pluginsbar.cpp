// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "mainwnd.h"
#include "plugins.h"
#include "toolbar.h"

#include <strsafe.h>

//*****************************************************************************
//
// CPluginsBar
//

CPluginsBar::CPluginsBar(HWND hNotifyWindow, CObjectOrigin origin)
    : CToolBar(hNotifyWindow, origin)
{
    CALL_STACK_MESSAGE_NONE
    HPluginsIcons = NULL;
    HPluginsIconsGray = NULL;
}

CPluginsBar::~CPluginsBar()
{
    DestroyImageLists();
}

void CPluginsBar::DestroyImageLists()
{
    if (HPluginsIcons != NULL)
    {
        ImageList_Destroy(HPluginsIcons);
        HPluginsIcons = NULL;
    }
    if (HPluginsIconsGray != NULL)
    {
        ImageList_Destroy(HPluginsIconsGray);
        HPluginsIconsGray = NULL;
    }
}

BOOL CPluginsBar::CreatePluginButtons()
{
    CALL_STACK_MESSAGE1("CPluginsBar::CreateButtons()");
    if (HWindow == NULL)
        return FALSE;

    RemoveAllItems();

    SetStyle(TLB_STYLE_IMAGE /*| TLB_STYLE_TEXT*/);

    DestroyImageLists();

    // Full-color identities and native-size rasterization match the main and drive toolbars.
    int iconSize = GetToolbarIconSizeForSystemDPI();
    HPluginsIcons = Plugins.CreateIconsList(FALSE, iconSize);
    HPluginsIconsGray = Plugins.CreateIconsList(FALSE, iconSize);

    SetImageList(HPluginsIconsGray);
    SetHotImageList(HPluginsIcons);
    ApplyConfiguredIconSpacing(); // Keep padding proportional at every configured size.

    Plugins.InitPluginsBar(this);
    /*
  TLBI_ITEM_INFO2 tii;
  int i;
  for (i = 0; i < Order.GetCount(); i++)
  {
    CPluginData *plugin = Plugins.Get(i);
    if (plugin == NULL || plugin->MenuItems.Count == 0) 
      continue;

    tii.Mask = TLBI_MASK_STYLE | TLBI_MASK_IMAGEINDEX | TLBI_MASK_ID;
    tii.Style = TLBI_STYLE_WHOLEDROPDOWN | TLBI_STYLE_DROPDOWN;
    tii.ImageIndex = i;
    tii.ID = CM_PLUGINCMD_MIN + i; // do mainwnd3 prijde jako WM_USER_TBDROPDOWN
    InsertItem2(0xFFFFFFFF, TRUE, &tii);
  }
  */

    return TRUE;
}

int CPluginsBar::GetNeededHeight()
{
    CALL_STACK_MESSAGE_NONE
    // i v pripade, ze nedrzime zadnou ikonu budeem vracet spravnou vysku
    int height = CToolBar::GetNeededHeight();
    // Empty bars reserve the same icon height as populated bars.
    int iconSize = GetToolbarIconSizeForSystemDPI();
    int minH = 3 + iconSize + 3;
    if (height < minH)
        height = minH;
    return height;
}

void CPluginsBar::Customize()
{
    CALL_STACK_MESSAGE_NONE
    // show Plugins window
    PostMessage(MainWindow->HWindow, WM_COMMAND, CM_CUSTOMIZEPLUGINS, 0);
}

void CPluginsBar::OnGetToolTip(LPARAM lParam)
{
    CALL_STACK_MESSAGE2("CPluginsBar::OnGetToolTip(0x%IX)", lParam);
    TOOLBAR_TOOLTIP* tt = (TOOLBAR_TOOLTIP*)lParam;

    int index = tt->ID - CM_PLUGINCMD_MIN;
    tt->Buffer[0] = 0;
    CPluginData* plugin = Plugins.Get(index);
    if (plugin != NULL)
    {
        // The toolbar owns a fixed reply buffer, so do not truncate a plug-in name into a tooltip.
        if (FAILED(StringCchCopyA(tt->Buffer, TOOLTIP_TEXT_MAX, plugin->Name)))
            tt->Buffer[0] = 0;
    }
}
