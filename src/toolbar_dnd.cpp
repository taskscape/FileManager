// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "cfgdlg.h"
#include "toolbar.h"

//*****************************************************************************
//
// CTBCustomizeDialog
//

// Combo order is kept in one table so display strings cannot drift from persisted pixel values.
static const int CustomizeToolbarIconSizes[] = {
    TOOLBAR_ICON_SIZE_SMALL,
    TOOLBAR_ICON_SIZE_MEDIUM,
    TOOLBAR_ICON_SIZE_LARGE,
};

CTBCustomizeDialog::CTBCustomizeDialog(CToolBar* toolBar)
    : CCommonDialog(HLanguage, IDD_CUSTOMIZETOOLBAR, IDD_CUSTOMIZETOOLBAR, toolBar->HWindow), AllItems(50, 20)
{
    CALL_STACK_MESSAGE_NONE
    ToolBar = toolBar;
    DragNotify = 0;
    DragMode = tbcdDragNone;
    DragIndex = -1;
    SelectedIconSize = IsValidToolbarIconSize(Configuration.ToolbarIconSize) ?
                           Configuration.ToolbarIconSize :
                           TOOLBAR_ICON_SIZE_SMALL;
}

CTBCustomizeDialog::~CTBCustomizeDialog()
{
    CALL_STACK_MESSAGE_NONE
    DestroyItems();
}

BOOL CTBCustomizeDialog::Execute()
{
    CALL_STACK_MESSAGE_NONE
    CCommonDialog::Execute();
    return TRUE;
}

void CTBCustomizeDialog::DestroyItems()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::DestroyItems()");
    TLBI_ITEM_INFO2* tii;
    int i;
    for (i = 0; i < AllItems.Count; i++)
    {
        tii = &AllItems[i];
        if (tii->Text != NULL)
            free(tii->Text);
        if (tii->Name != NULL)
            free(tii->Name);
    }
}

// Fills the Items array with all buttons the toolbar can contain.
BOOL CTBCustomizeDialog::EnumButtons()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::EnumButtons()");
    // vycistime pole
    DestroyItems();

    char textBuffer[1024]; // docasne buffery pro prijem retezcu
    char nameBuffer[1024];

    HWND hNotifyWnd = ToolBar->HNotifyWindow;
    TLBI_ITEM_INFO2 tii;
    tii.Mask = TLBI_MASK_STYLE; // na prvni misto seznamu zaradime separator
    tii.Style = TLBI_STYLE_SEPARATOR;
    tii.Index = -1;
    BOOL sent = TRUE;
    BOOL tryNewSend = TRUE;
    while (sent)
    {
        tii.Text = textBuffer;
        tii.TextLen = 1024;
        tii.Name = nameBuffer;
        tii.NameLen = 1024;
        textBuffer[0] = 0;
        nameBuffer[0] = 0;
        if (tii.Index != -1 && tryNewSend)
        {
            sent = SendMessage(hNotifyWnd, WM_USER_TBENUMBUTTON2, (WPARAM)ToolBar->HWindow, (LPARAM)&tii) == TRUE;
            tryNewSend = sent;
        }
        if (sent)
        {
            // naalokujeme kopie retezcu text a name
            // The notify protocol supplies terminated labels before their exact owned copies are allocated.
            tii.TextLen = static_cast<int>(strlen(tii.Text));
            tii.NameLen = static_cast<int>(strlen(tii.Name));
            char* text = (char*)malloc(tii.TextLen + 1);
            char* name = (char*)malloc(tii.NameLen + 1);
            if (tii.TextLen > 0)
                memmove(text, tii.Text, tii.TextLen);
            text[tii.TextLen] = 0;
            if (tii.NameLen > 0)
                memmove(name, tii.Name, tii.NameLen);
            name[tii.NameLen] = 0;
            tii.Text = text;
            tii.Name = name;
            // vlozime polozku do pole
            AllItems.Add(tii);
            tii.Index++;
        }
    }
    return TRUE;
}

BOOL CTBCustomizeDialog::PresentInToolBar(DWORD id)
{
    CALL_STACK_MESSAGE_NONE
    int i;
    for (i = 0; i < ToolBar->Items.Count; i++)
    {
        if (id == ToolBar->Items[i]->ID)
            return TRUE;
    }
    return FALSE;
}

BOOL CTBCustomizeDialog::FindIndex(DWORD id, int* index)
{
    CALL_STACK_MESSAGE_NONE
    // preskocime virtualni separator
    int i;
    for (i = 1; i < AllItems.Count; i++)
    {
        if (id == AllItems[i].ID)
        {
            *index = i;
            return TRUE;
        }
    }
    return FALSE;
}

void CTBCustomizeDialog::FillLists()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::FillLists()");
    SendMessage(HAvailableLB, WM_SETREDRAW, FALSE, 0);
    SendMessage(HCurrentLB, WM_SETREDRAW, FALSE, 0);

    SendMessage(HAvailableLB, LB_RESETCONTENT, 0, 0);
    SendMessage(HCurrentLB, LB_RESETCONTENT, 0, 0);
    int i;
    for (i = 0; i < AllItems.Count; i++)
        if (i == 0 || !PresentInToolBar(AllItems[i].ID)) // if this is not a virtual separator, check toolbar presence
        {
            int ret = (int)SendMessage(HAvailableLB, LB_ADDSTRING, 0, 1); // 1 je dumy hodnota, obchazime chybu WinXP
            if (ret != LB_ERR)
                SendMessage(HAvailableLB, LB_SETITEMDATA, ret, (LPARAM)i);
        }
    for (i = 0; i < ToolBar->Items.Count; i++)
    {
        int index;
        if (ToolBar->Items[i]->Style == TLBI_STYLE_SEPARATOR)
            index = 0; // virtualni separator
        else
        {
            if (!FindIndex(ToolBar->Items[i]->ID, &index))
            {
                TRACE_E("Button in toolbar was not found in enumerated items");
                continue;
            }
        }
        int ret = (int)SendMessage(HCurrentLB, LB_ADDSTRING, 0, 1); // 1 je dumy hodnota, obchazime chybu WinXP
        if (ret != LB_ERR)
            SendMessage(HCurrentLB, LB_SETITEMDATA, ret, (LPARAM)index);
    }
    // virtualni separator
    SendMessage(HCurrentLB, LB_ADDSTRING, 0, (LPARAM)-1);

    // nastavime selected polozky
    SendMessage(HAvailableLB, LB_SETCURSEL, 0, 0);
    SendMessage(HCurrentLB, LB_SETCURSEL, 0, 0);

    SendMessage(HAvailableLB, WM_SETREDRAW, TRUE, 0);
    SendMessage(HCurrentLB, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(HAvailableLB, NULL, FALSE);
    InvalidateRect(HCurrentLB, NULL, FALSE);
}

void CTBCustomizeDialog::EnableControls()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::EnableControls()");
    int aIndex = (int)SendMessage(HAvailableLB, LB_GETCURSEL, 0, 0);
    int cIndex = (int)SendMessage(HCurrentLB, LB_GETCURSEL, 0, 0);
    int cCount = (int)SendMessage(HCurrentLB, LB_GETCOUNT, 0, 0);
    EnableWindow(GetDlgItem(HWindow, IDB_CTB_REMOVE), cIndex < cCount - 1);
    EnableWindow(GetDlgItem(HWindow, IDB_CTB_UP), cIndex < cCount - 1 && cIndex > 0);
    EnableWindow(GetDlgItem(HWindow, IDB_CTB_DOWN), cIndex < cCount - 2);
}

void CTBCustomizeDialog::OnAdd()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::OnAdd()");
    int aCount = (int)SendMessage(HAvailableLB, LB_GETCOUNT, 0, 0);
    int aIndex = (int)SendMessage(HAvailableLB, LB_GETCURSEL, 0, 0);
    int cIndex = (int)SendMessage(HCurrentLB, LB_GETCURSEL, 0, 0);
    int data = (int)SendMessage(HAvailableLB, LB_GETITEMDATA, aIndex, 0);
    if (ToolBar->InsertItem2(cIndex, TRUE, &AllItems[data]))
    {
        SendMessage(HAvailableLB, WM_SETREDRAW, FALSE, 0);
        if (aIndex > 0)
        {
            SendMessage(HAvailableLB, LB_DELETESTRING, aIndex, 0);
            if (aIndex >= aCount - 1)
                aIndex = aCount - 2;
        }
        // fix: if the list was scrolled down and items were removed from the bottom,
        // zmensoval se scrollbar, ale neupravoval se topindex
        SendMessage(HAvailableLB, LB_SETCURSEL, 0, 0);
        SendMessage(HAvailableLB, LB_SETCURSEL, aIndex, 0);
        SendMessage(HAvailableLB, WM_SETREDRAW, TRUE, 0);
        int ret = (int)SendMessage(HCurrentLB, LB_INSERTSTRING, cIndex, 1); // 1 je dumy hodnota, obchazime chybu WinXP
        if (ret != LB_ERR)
            SendMessage(HCurrentLB, LB_SETITEMDATA, ret, data);
        SendMessage(HCurrentLB, LB_SETCURSEL, cIndex + 1, 0);
        SendMessage(ToolBar->HNotifyWindow, WM_USER_TBCHANGED, (WPARAM)ToolBar->HWindow, 0);
    }
    EnableControls();
}

void CTBCustomizeDialog::OnRemove()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::OnRemove()");
    int cIndex = (int)SendMessage(HCurrentLB, LB_GETCURSEL, 0, 0);
    int cCount = (int)SendMessage(HCurrentLB, LB_GETCOUNT, 0, 0);
    if (cIndex < 0 || cIndex >= cCount - 1)
        return;
    if (ToolBar->RemoveItem(cIndex, TRUE))
    {
        int data = (int)SendMessage(HCurrentLB, LB_GETITEMDATA, cIndex, 0);
        SendMessage(HCurrentLB, WM_SETREDRAW, FALSE, 0);
        SendMessage(HCurrentLB, LB_DELETESTRING, cIndex, 0);
        // fix: if the list was scrolled down and items were removed from the bottom,
        // zmensoval se scrollbar, ale neupravoval se topindex
        SendMessage(HCurrentLB, LB_SETCURSEL, 0, 0);
        SendMessage(HCurrentLB, LB_SETCURSEL, cIndex, 0);
        SendMessage(HCurrentLB, WM_SETREDRAW, TRUE, 0);
        int index = 0;
        if (data != 0) // separator
        {
            int aCount = (int)SendMessage(HAvailableLB, LB_GETCOUNT, 0, 0);
            for (index = 0; index < aCount; index++)
            {
                int tmp = (int)SendMessage(HAvailableLB, LB_GETITEMDATA, index, 0);
                if (tmp > data)
                    break;
            }
            int ret = (int)SendMessage(HAvailableLB, LB_INSERTSTRING, index, 1); // 1 je dumy hodnota, obchazime chybu WinXP
            if (ret != LB_ERR)
                SendMessage(HAvailableLB, LB_SETITEMDATA, ret, data);
        }
        SendMessage(HAvailableLB, LB_SETCURSEL, index, 0);
        SendMessage(ToolBar->HNotifyWindow, WM_USER_TBCHANGED, (WPARAM)ToolBar->HWindow, 0);
    }
    EnableControls();
}

void CTBCustomizeDialog::MoveItem(int srcIndex, int tgtIndex)
{
    CALL_STACK_MESSAGE3("CTBCustomizeDialog::MoveItem(%d, %d)", srcIndex, tgtIndex);
    int data = (int)SendMessage(HCurrentLB, LB_GETITEMDATA, srcIndex, 0);
    int delta = 0;
    if (srcIndex < tgtIndex)
        delta = 1;
    ToolBar->RemoveItem(srcIndex, TRUE);
    ToolBar->InsertItem2(tgtIndex - delta, TRUE, &AllItems[data]);

    SendMessage(HCurrentLB, LB_DELETESTRING, srcIndex, 0);
    int ret = (int)SendMessage(HCurrentLB, LB_INSERTSTRING, tgtIndex - delta, 1); // 1 je dumy hodnota, obchazime chybu WinXP
    if (ret != LB_ERR)
        SendMessage(HCurrentLB, LB_SETITEMDATA, ret, data);
    SendMessage(HCurrentLB, LB_SETCURSEL, tgtIndex - delta, 0);
    SendMessage(ToolBar->HNotifyWindow, WM_USER_TBCHANGED, (WPARAM)ToolBar->HWindow, 0);
    EnableControls();
}

void CTBCustomizeDialog::OnUp()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::OnUp()");
    int cIndex = (int)SendMessage(HCurrentLB, LB_GETCURSEL, 0, 0);
    int cCount = (int)SendMessage(HCurrentLB, LB_GETCOUNT, 0, 0);
    if (cIndex >= cCount - 1 || cIndex <= 0)
        return;
    MoveItem(cIndex, cIndex - 1);
}

void CTBCustomizeDialog::OnDown()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::OnDown()");
    int cIndex = (int)SendMessage(HCurrentLB, LB_GETCURSEL, 0, 0);
    int cCount = (int)SendMessage(HCurrentLB, LB_GETCOUNT, 0, 0);
    if (cIndex >= cCount - 2)
        return;
    MoveItem(cIndex, cIndex + 2);
}

void CTBCustomizeDialog::OnReset()
{
    CALL_STACK_MESSAGE1("CTBCustomizeDialog::OnReset()");
    SendMessage(ToolBar->HNotifyWindow, WM_USER_TBRESET, (WPARAM)ToolBar->HWindow, 0);
    FillLists();
    SendMessage(ToolBar->HNotifyWindow, WM_USER_TBCHANGED, (WPARAM)ToolBar->HWindow, 0);
}

INT_PTR
CTBCustomizeDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CTBCustomizeDialog::DialogProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    if (DragNotify != 0 && DragNotify == uMsg)
    {
        DRAGLISTINFO* dli = (DRAGLISTINFO*)lParam;
        switch (dli->uNotification)
        {
        case DL_BEGINDRAG:
        {
            if (dli->hWnd == HAvailableLB)
                DragMode = tbcdDragAvailable;
            else
                DragMode = tbcdDragCurrent;

            DragIndex = LBItemFromPt(dli->hWnd, dli->ptCursor, FALSE);
            if (DragMode == tbcdDragCurrent && DragIndex >= ToolBar->Items.Count)
            {
                // virtualni separator nenechame tahnout
                DragMode = tbcdDragNone;
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, FALSE);
            }
            else
                SetWindowLongPtr(HWindow, DWLP_MSGRESULT, TRUE);
            break;
        }

        case DL_DRAGGING:
        {
            HWND hWindow = WindowFromPoint(dli->ptCursor);
            int index = LBItemFromPt(HCurrentLB, dli->ptCursor, TRUE);
            if (DragMode == tbcdDragAvailable)
            {
                if (index != -1)
                    SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(DragIndex == 0 ? IDC_TOOLBAR_ADD : IDC_TOOLBAR_MOVE)));
                else
                    SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(IDC_TOOLBAR_DEL)));
                DrawInsert(HWindow, HCurrentLB, index);
            }
            if (DragMode == tbcdDragCurrent)
            {
                if (index != -1 || hWindow == HAvailableLB)
                    SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(IDC_TOOLBAR_MOVE)));
                else
                    SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(IDC_TOOLBAR_DEL)));
                DrawInsert(HWindow, HCurrentLB, index);
            }
            SetWindowLongPtr(HWindow, DWLP_MSGRESULT, 0); // prevent cursor change
            break;
        }

        case DL_DROPPED:
        {
            DrawInsert(HWindow, HCurrentLB, -1);
            int index = LBItemFromPt(HCurrentLB, dli->ptCursor, FALSE);
            if (DragMode == tbcdDragAvailable && index != -1)
            {
                SendMessage(HCurrentLB, LB_SETCURSEL, index, 0);
                OnAdd();
                SendMessage(HCurrentLB, LB_SETCURSEL, index, 0);
            }
            if (DragMode == tbcdDragCurrent)
            {
                HWND hWindow = WindowFromPoint(dli->ptCursor);
                if (hWindow == HAvailableLB)
                    OnRemove();
                else if (index != -1)
                {
                    if (DragIndex != index && index != DragIndex + 1)
                        MoveItem(DragIndex, index);
                }
            }
            DragMode = tbcdDragNone;
            break;
        }

        case DL_CANCELDRAG:
        {
            DrawInsert(HWindow, HCurrentLB, -1);
            DragMode = tbcdDragNone;
            break;
        }
        }
        return TRUE;
    }

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        HAvailableLB = GetDlgItem(HWindow, IDL_CTB_AVAILABLE_ITEMS);
        HCurrentLB = GetDlgItem(HWindow, IDL_CTB_CURRENT_ITEMS);
        MakeDragList(HAvailableLB);
        MakeDragList(HCurrentLB);
        DragNotify = RegisterWindowMessage(DRAGLISTMSGSTRING);

        // Populate localized choices while retaining logical pixel values in the fixed table above.
        HWND hIconSize = GetDlgItem(HWindow, IDC_CTB_ICON_SIZE);
        int sizeStringIDs[] = {
            IDS_TOOLBAR_ICON_SIZE_SMALL,
            IDS_TOOLBAR_ICON_SIZE_MEDIUM,
            IDS_TOOLBAR_ICON_SIZE_LARGE,
        };
        int selectedSizeIndex = 0;
        for (int i = 0; i < _countof(CustomizeToolbarIconSizes); i++)
        {
            SendMessage(hIconSize, CB_ADDSTRING, 0, (LPARAM)LoadStr(sizeStringIDs[i]));
            if (CustomizeToolbarIconSizes[i] == SelectedIconSize)
                selectedSizeIndex = i;
        }
        SendMessage(hIconSize, CB_SETCURSEL, selectedSizeIndex, 0);

        // vytahneme tlacitka
        EnumButtons();
        FillLists();
        EnableControls();

        // Give Customize Toolbar previews the same proportional breathing room as live buttons.
        int iconSize = max(GetIconSizeForSystemDPI(ICONSIZE_16), ToolBar->ImageHeight);
        int edgeSpacing = max(1, (iconSize + 3) / 4);
        int minHeight = iconSize + 2 * edgeSpacing;
        HFONT hFont = (HFONT)SendMessage(HAvailableLB, WM_GETFONT, 0, 0);
        HDC hDC = HANDLES(GetDC(HWindow));
        SelectObject(hDC, hFont);
        TEXTMETRIC tm;
        HFONT hOldFont = (HFONT)SelectObject(hDC, hFont);
        GetTextMetrics(hDC, &tm);
        minHeight = max(tm.tmHeight + 2 * edgeSpacing, minHeight);
        SelectObject(hDC, hOldFont);
        HANDLES(ReleaseDC(HWindow, hDC));
        // nastavime oba listboxy
        SendMessage(HAvailableLB, LB_SETITEMHEIGHT, 0, minHeight);
        SendMessage(HCurrentLB, LB_SETITEMHEIGHT, 0, minHeight);
        break;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_CTB_ICON_SIZE && HIWORD(wParam) == CBN_SELCHANGE)
        {
            int selectedIndex = (int)SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
            // Ignore an invalid transient selection instead of corrupting the persisted preference.
            if (selectedIndex >= 0 && selectedIndex < _countof(CustomizeToolbarIconSizes))
                SelectedIconSize = CustomizeToolbarIconSizes[selectedIndex];
            return 0;
        }
        if (HIWORD(wParam) == LBN_SELCHANGE)
        {
            EnableControls();
            return 0;
        }
        if (HIWORD(wParam) == LBN_DBLCLK)
        {
            if ((HWND)lParam == HCurrentLB)
                OnRemove();
            if ((HWND)lParam == HAvailableLB)
                OnAdd();
            return 0;
        }
        switch (LOWORD(wParam))
        {
        case IDB_CTB_UP:
            OnUp();
            return 0;
        case IDB_CTB_DOWN:
            OnDown();
            return 0;
        case IDB_CTB_ADD:
            OnAdd();
            return 0;
        case IDB_CTB_REMOVE:
            OnRemove();
            return 0;
        case IDB_CTB_RESET:
            OnReset();
            return 0;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        HDC hDC = dis->hDC;
        RECT r = dis->rcItem;
        int index = (int)dis->itemData;
        BOOL separator = (index == 0 || index == -1);
        BOOL selected = (dis->itemState & ODS_SELECTED);
        //      BOOL focused = (dis->itemState & ODS_FOCUS);
        BOOL focused = selected && GetFocus() == GetDlgItem(HWindow, dis->CtlID);

        if (selected && focused)
            FillRect(hDC, &r, (HBRUSH)(COLOR_HIGHLIGHT + 1));
        else
            FillRect(hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));

        const char* text;
        if (separator)
            text = LoadStr(IDS_SEPARATOR);
        else
            text = AllItems[index].Name;

        // Direct HICON items are scaled to the same dimensions as image-list-backed commands.
        int iconSize = ToolBar->ImageHeight > 0 ? ToolBar->ImageHeight : GetToolbarIconSizeForSystemDPI();
        int edgeSpacing = max(1, (iconSize + 3) / 4);
        int imageWidth = max(iconSize, ToolBar->ImageWidth) + 2 * edgeSpacing;
        int iconY = r.top + max(0, (r.bottom - r.top - iconSize) / 2);
        if (!separator)
        {
            if ((AllItems[index].Mask & TLBI_MASK_ICON) && AllItems[index].HIcon != NULL)
                DrawIconEx(hDC, r.left + edgeSpacing, iconY, AllItems[index].HIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
            if ((AllItems[index].Mask & TLBI_MASK_IMAGEINDEX) && AllItems[index].ImageIndex != -1)
                ImageList_Draw(ToolBar->HImageList, AllItems[index].ImageIndex, hDC, r.left + edgeSpacing, iconY, ILD_TRANSPARENT);
        }

        r.left += imageWidth;
        int normalColor = index == -1 ? COLOR_GRAYTEXT : COLOR_WINDOWTEXT;
        SetTextColor(hDC, GetSysColor(selected && focused ? COLOR_HIGHLIGHTTEXT : normalColor));
        SetBkMode(hDC, TRANSPARENT);
        DrawText(hDC, text, -1, &r, DT_SINGLELINE | DT_LEFT | DT_VCENTER);
        r.left -= imageWidth;
        if (selected && !focused)
        {
            SelectObject(hDC, HANDLES(GetStockObject(NULL_BRUSH)));
            HPEN hPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_HIGHLIGHT)));
            HPEN hOldPen = (HPEN)SelectObject(hDC, hPen);
            Rectangle(hDC, r.left, r.top, r.right, r.bottom);
            SelectObject(hDC, hOldPen);
            HANDLES(DeleteObject(hPen));
        }
        else
        {
            if (focused)
            {
                SetTextColor(hDC, RGB(0, 0, 0));
                DrawFocusRect(hDC, &r);
            }
        }
        return TRUE;
    }
    }
    return CCommonDialog::DialogProc(uMsg, wParam, lParam);
}

void CToolBar::Customize()
{
    CALL_STACK_MESSAGE1("CToolBar::Customize()");
    if (!(Style & TLB_STYLE_ADJUSTABLE))
        return;

    // posleme notifikaci o zacatku konfigurace
    SendMessage(HNotifyWindow, WM_USER_TBBEGINADJUST, (WPARAM)HWindow, 0);

    // behem konfigurace jsou vsechna tlacitka enabled
    Customizing = TRUE;
    InvalidateRect(HWindow, NULL, FALSE);
    UpdateWindow(HWindow);

    // provedeme konfiguraci
    CTBCustomizeDialog dialog(this);
    dialog.Execute();

    // Commit the staged size only after the dialog stops drawing from the current image list.
    BOOL iconSizeChanged = Configuration.ToolbarIconSize != dialog.GetSelectedIconSize();
    if (iconSizeChanged)
        Configuration.ToolbarIconSize = dialog.GetSelectedIconSize();

    // vratime se k puvodnimu nastaveni tlacitek
    Customizing = FALSE;
    InvalidateRect(HWindow, NULL, FALSE);

    // posleme notifikaci o ukonceni konfigurace
    SendMessage(HNotifyWindow, WM_USER_TBENDADJUST, (WPARAM)HWindow, 0);

    if (iconSizeChanged)
    {
        // Rebuild full-color SVG image lists and reattach them to every open toolbar.
        ColorsChanged(TRUE, FALSE, FALSE);
    }
}
