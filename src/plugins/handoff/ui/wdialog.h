// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Unicode dialog base for the Delivery Handoff windows. WinLib's CDialog
// creates ANSI dialogs, whose edit and list controls cannot hold characters
// outside the active code page; package, specification, and file names here
// are arbitrary Unicode, so these windows use the W dialog APIs directly
// (recorded as a deviation in handoff-spec.md C.13).

#include <string>
#include <vector>

enum AnchorFlags : unsigned
{
    AnchorLeft = 1,
    AnchorTop = 2,
    AnchorRight = 4,
    AnchorBottom = 8,
    AnchorTopLeft = AnchorLeft | AnchorTop,
    AnchorTopRight = AnchorRight | AnchorTop,
    AnchorTopStretch = AnchorLeft | AnchorRight | AnchorTop,
    AnchorBottomLeft = AnchorLeft | AnchorBottom,
    AnchorBottomRight = AnchorRight | AnchorBottom,
    AnchorBottomStretch = AnchorLeft | AnchorRight | AnchorBottom,
    AnchorAll = AnchorLeft | AnchorTop | AnchorRight | AnchorBottom
};

class WDialog
{
public:
    WDialog(int templateId, int helpId);
    virtual ~WDialog() {}
    WDialog(const WDialog&) = delete;
    WDialog& operator=(const WDialog&) = delete;

    // Modal: DialogBoxParamW on the calling thread.
    INT_PTR RunModal(HWND owner);
    // Modeless: on success the object deletes itself when its window is
    // destroyed; on failure it is deleted before returning false.
    bool CreateModeless(HWND owner);

    HWND Window() const { return HWindow; }
    void SetAlwaysOnTop(bool value) { AlwaysOnTop = value; }

protected:
    HWND HWindow = NULL;
    int TemplateId;
    int HelpId;
    bool Modal = false;
    bool AlwaysOnTop = false;

    virtual BOOL OnInitDialog() { return TRUE; }
    virtual void OnCommand(int id, int code, HWND control);
    virtual LRESULT OnNotify(int id, NMHDR* header) { return 0; }
    // Returns true when handled; 'result' becomes the dialog procedure's return value.
    virtual bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam, INT_PTR& result) { return false; }
    virtual void OnOk() { Close(IDOK); }
    // IDCANCEL, Esc, and WM_CLOSE (including plug-in unload).
    virtual void OnCloseRequest() { Close(IDCANCEL); }
    virtual void OnDestroyed() {}
    // Called when the last NestedScope ends; windows replay work they deferred meanwhile.
    virtual void OnNestedEnded() {}
    bool InNested() const { return PromptDepth > 0; }

    void Close(INT_PTR result);
    // Message box owned by this window; a close requested meanwhile is applied afterwards.
    int Prompt(const std::wstring& text, UINT flags);

    // Guards any nested modal UI (child dialogs, file dialogs): WM_CLOSE that
    // arrives meanwhile is deferred until the scope ends, so this object is
    // never destroyed underneath the code that opened the nested UI.
    class NestedScope
    {
    public:
        explicit NestedScope(WDialog& dialog) : Dialog(dialog) { Dialog.PromptDepth++; }
        ~NestedScope() { Dialog.EndNested(); }
        NestedScope(const NestedScope&) = delete;
        NestedScope& operator=(const NestedScope&) = delete;

    private:
        WDialog& Dialog;
    };

    HWND Item(int id) const { return GetDlgItem(HWindow, id); }
    std::wstring ItemText(int id) const;
    void SetItemText(int id, const std::wstring& text);
    void EnableItem(int id, bool enable);
    void ShowItem(int id, bool show);
    bool IsChecked(int id) const;
    void SetChecked(int id, bool checked);
    void SetTitle(const std::wstring& title);
    void FocusItem(int id);

    // Resizable windows: anchors are recorded relative to the template layout.
    void Anchor(int id, unsigned flags);
    void EnableResizing();
    std::vector<unsigned char> Placement() const;
    void RestorePlacement(const std::vector<unsigned char>& bytes);

private:
    struct AnchorItem
    {
        int Id;
        RECT Initial;
        unsigned Flags;
    };
    std::vector<AnchorItem> Anchors;
    SIZE InitialClient = {};
    SIZE MinTrack = {};
    bool Resizable = false;
    bool ClosePending = false;
    int PromptDepth = 0;
    bool* InitReached = nullptr;

    static INT_PTR CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    INT_PTR Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void Layout();
    void EndNested();
};

// Help topics open from any thread through the host's HTML Help.
void OpenHelpFrom(HWND window, int helpId);
