// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "ui_thread.h"
#include "wdialog.h"

WDialog::WDialog(int templateId, int helpId) : TemplateId(templateId), HelpId(helpId)
{
}

INT_PTR WDialog::RunModal(HWND owner)
{
    Modal = true;
    return DialogBoxParamW(HLanguage, MAKEINTRESOURCEW(TemplateId), owner, Proc, (LPARAM)this);
}

bool WDialog::CreateModeless(HWND owner)
{
    Modal = false;
    bool initReached = false;
    InitReached = &initReached;
    HWND window = CreateDialogParamW(HLanguage, MAKEINTRESOURCEW(TemplateId), owner, Proc, (LPARAM)this);
    if (window == NULL)
    {
        // A window destroyed during WM_INITDIALOG already deleted this object.
        if (!initReached)
            delete this;
        return false;
    }
    InitReached = nullptr;
    ShowWindow(window, SW_SHOW);
    return true;
}

void WDialog::OnCommand(int id, int code, HWND control)
{
    switch (id)
    {
    case IDOK:
        OnOk();
        break;
    case IDCANCEL:
        OnCloseRequest();
        break;
    case IDHELP:
        OpenHelpFrom(HWindow, HelpId);
        break;
    }
}

void WDialog::Close(INT_PTR result)
{
    if (Modal)
        EndDialog(HWindow, result);
    else
        DestroyWindow(HWindow);
}

int WDialog::Prompt(const std::wstring& text, UINT flags)
{
    NestedScope nested(*this);
    return PromptBox(HWindow, text, flags);
}

void WDialog::EndNested()
{
    PromptDepth--;
    if (PromptDepth != 0 || HWindow == NULL)
        return;
    if (ClosePending)
    {
        // The close arrived while nested UI was open; apply it once the caller has finished.
        ClosePending = false;
        PostMessageW(HWindow, WM_CLOSE, 0, 0);
    }
    OnNestedEnded();
}

std::wstring WDialog::ItemText(int id) const
{
    HWND item = Item(id);
    int length = item != NULL ? GetWindowTextLengthW(item) : 0;
    if (length <= 0)
        return std::wstring();
    std::wstring text((size_t)length + 1, L'\0');
    int copied = GetWindowTextW(item, &text[0], length + 1);
    text.resize((size_t)(copied > 0 ? copied : 0));
    return text;
}

void WDialog::SetItemText(int id, const std::wstring& text)
{
    SetDlgItemTextW(HWindow, id, text.c_str());
}

void WDialog::EnableItem(int id, bool enable)
{
    HWND item = Item(id);
    if (item == NULL)
        return;
    // Disabling the focused control would strand keyboard focus.
    if (!enable && GetFocus() == item)
        SendMessageW(HWindow, WM_NEXTDLGCTL, 0, FALSE);
    EnableWindow(item, enable ? TRUE : FALSE);
}

void WDialog::ShowItem(int id, bool show)
{
    HWND item = Item(id);
    if (item != NULL)
        ShowWindow(item, show ? SW_SHOW : SW_HIDE);
}

bool WDialog::IsChecked(int id) const
{
    return IsDlgButtonChecked(HWindow, id) == BST_CHECKED;
}

void WDialog::SetChecked(int id, bool checked)
{
    CheckDlgButton(HWindow, id, checked ? BST_CHECKED : BST_UNCHECKED);
}

void WDialog::SetTitle(const std::wstring& title)
{
    SetWindowTextW(HWindow, title.c_str());
}

void WDialog::FocusItem(int id)
{
    HWND item = Item(id);
    if (item != NULL)
        SendMessageW(HWindow, WM_NEXTDLGCTL, (WPARAM)item, TRUE);
}

void WDialog::Anchor(int id, unsigned flags)
{
    HWND item = Item(id);
    if (item == NULL)
        return;
    RECT rect;
    GetWindowRect(item, &rect);
    MapWindowPoints(NULL, HWindow, (POINT*)&rect, 2);
    Anchors.push_back(AnchorItem{id, rect, flags});
}

void WDialog::EnableResizing()
{
    RECT client, window;
    GetClientRect(HWindow, &client);
    GetWindowRect(HWindow, &window);
    InitialClient.cx = client.right;
    InitialClient.cy = client.bottom;
    // The template size is the minimum, so controls never overlap.
    MinTrack.cx = window.right - window.left;
    MinTrack.cy = window.bottom - window.top;
    Resizable = true;
}

void WDialog::Layout()
{
    if (!Resizable || Anchors.empty())
        return;
    RECT client;
    GetClientRect(HWindow, &client);
    int dx = client.right - InitialClient.cx;
    int dy = client.bottom - InitialClient.cy;
    HDWP defer = BeginDeferWindowPos((int)Anchors.size());
    for (const AnchorItem& anchor : Anchors)
    {
        RECT rect = anchor.Initial;
        if ((anchor.Flags & AnchorRight) != 0)
        {
            rect.right += dx;
            if ((anchor.Flags & AnchorLeft) == 0)
                rect.left += dx;
        }
        if ((anchor.Flags & AnchorBottom) != 0)
        {
            rect.bottom += dy;
            if ((anchor.Flags & AnchorTop) == 0)
                rect.top += dy;
        }
        HWND item = Item(anchor.Id);
        if (item != NULL && defer != NULL)
            defer = DeferWindowPos(defer, item, NULL, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (defer != NULL)
        EndDeferWindowPos(defer);
    InvalidateRect(HWindow, NULL, TRUE);
}

std::vector<unsigned char> WDialog::Placement() const
{
    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (!GetWindowPlacement(HWindow, &placement))
        return std::vector<unsigned char>();
    const unsigned char* bytes = (const unsigned char*)&placement;
    return std::vector<unsigned char>(bytes, bytes + sizeof(placement));
}

void WDialog::RestorePlacement(const std::vector<unsigned char>& bytes)
{
    if (bytes.size() != sizeof(WINDOWPLACEMENT))
        return;
    WINDOWPLACEMENT placement;
    memcpy(&placement, bytes.data(), sizeof(placement));
    placement.length = sizeof(placement);
    // Restored only when the saved position is on a monitor that exists now (C.8.1).
    if (MonitorFromRect(&placement.rcNormalPosition, MONITOR_DEFAULTTONULL) == NULL)
        return;
    if (placement.rcNormalPosition.right - placement.rcNormalPosition.left < MinTrack.cx ||
        placement.rcNormalPosition.bottom - placement.rcNormalPosition.top < MinTrack.cy)
        return;
    placement.showCmd = placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    placement.flags = 0;
    SetWindowPlacement(HWindow, &placement);
}

INT_PTR CALLBACK WDialog::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    WDialog* dialog;
    if (message == WM_INITDIALOG)
    {
        dialog = (WDialog*)lParam;
        dialog->HWindow = window;
        SetWindowLongPtrW(window, DWLP_USER, (LONG_PTR)dialog);
        if (dialog->InitReached != nullptr)
            *dialog->InitReached = true;
    }
    else
        dialog = (WDialog*)GetWindowLongPtrW(window, DWLP_USER);
    if (dialog == NULL)
        return FALSE;
    INT_PTR result = dialog->Handle(message, wParam, lParam);
    if (message == WM_NCDESTROY)
    {
        SetWindowLongPtrW(window, DWLP_USER, 0);
        UnregisterThreadDialog(window, !dialog->Modal);
        dialog->HWindow = NULL;
        if (!dialog->Modal)
            delete dialog;
    }
    return result;
}

INT_PTR WDialog::Handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    INT_PTR result = 0;
    if (message != WM_INITDIALOG && OnMessage(message, wParam, lParam, result))
        return result;
    switch (message)
    {
    case WM_INITDIALOG:
    {
        // Registered so plug-in unload can close it (WM_CLOSE) and prompts can tell it apart.
        ModelessQueue.Add(new CWindowQueueItem(HWindow));
        RegisterThreadDialog(HWindow, !Modal);
        if (AlwaysOnTop)
            SetWindowPos(HWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return OnInitDialog();
    }
    case WM_COMMAND:
        OnCommand(LOWORD(wParam), HIWORD(wParam), (HWND)lParam);
        return TRUE;
    case WM_NOTIFY:
    {
        NMHDR* header = (NMHDR*)lParam;
        LRESULT notifyResult = OnNotify((int)header->idFrom, header);
        SetWindowLongPtrW(HWindow, DWLP_MSGRESULT, notifyResult);
        return TRUE;
    }
    case WM_CLOSE:
        if (PromptDepth > 0)
            ClosePending = true;
        else
            OnCloseRequest();
        return TRUE;
    case WM_HELP:
        OpenHelpFrom(HWindow, HelpId);
        return TRUE;
    case WM_SIZE:
        Layout();
        return FALSE;
    case WM_GETMINMAXINFO:
        if (Resizable)
        {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = MinTrack.cx;
            info->ptMinTrackSize.y = MinTrack.cy;
            return TRUE;
        }
        return FALSE;
    case WM_DESTROY:
        ModelessQueue.Remove(HWindow);
        OnDestroyed();
        return FALSE;
    }
    return FALSE;
}

void OpenHelpFrom(HWND window, int helpId)
{
    if (helpId != 0)
        OpenHelpTopic(window, helpId);
}
