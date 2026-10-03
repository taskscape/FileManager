// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "file_system.h"
#include "handoff_ui.h"
#include "listview_util.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "ui_thread.h"
#include "wdialog.h"

// New specification dialog IDD_HO_NEWSPEC (handoff-spec.md C.8.6): writes an
// embedded template (C.4.14) with CREATE_NEW, so an existing file is never replaced.

namespace
{

struct TemplateInfo
{
    int ResourceId;
    int TitleId;
    int DescriptionId;
    const wchar_t* FileName;
};

const TemplateInfo Templates[] = {
    {IDR_TEMPLATE_DESIGN, IDS_TPL_DESIGN, IDS_TPL_DESIGN_DESC, L"design-studio.handoff.json"},
    {IDR_TEMPLATE_ARCHITECTURE, IDS_TPL_ARCH, IDS_TPL_ARCH_DESC, L"architecture-issue.handoff.json"},
    {IDR_TEMPLATE_CONSULTANCY, IDS_TPL_CONSULT, IDS_TPL_CONSULT_DESC, L"consultancy-report.handoff.json"},
    {IDR_TEMPLATE_EXAMPLE, IDS_TPL_EXAMPLE, IDS_TPL_EXAMPLE_DESC, L"client-delivery-example.handoff.json"},
};

bool TemplateBytes(int resourceId, const void*& data, DWORD& size)
{
    HRSRC resource = FindResourceW(DLLInstance, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10)); // RT_RCDATA
    HGLOBAL loaded = resource != NULL ? LoadResource(DLLInstance, resource) : NULL;
    data = loaded != NULL ? LockResource(loaded) : NULL;
    size = resource != NULL ? SizeofResource(DLLInstance, resource) : 0;
    return data != NULL && size > 0;
}

class NewSpecDialog : public WDialog
{
public:
    NewSpecDialog(const std::wstring& folder, int panel) : WDialog(IDD_HO_NEWSPEC, IDH_HANDOFF_NEWSPEC), Folder(folder), Panel(panel) {}

protected:
    BOOL OnInitDialog() override
    {
        SetSlotWindow(UiSlot::NewSpec, HWindow);
        HWND list = Item(IDC_HO_TEMPLATES);
        for (size_t i = 0; i < _countof(Templates); i++)
        {
            int index = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)Text(Templates[i].TitleId).c_str());
            SendMessageW(list, LB_SETITEMDATA, index, (LPARAM)i);
        }
        // The example template implements the quick-start request, so it is the default.
        SendMessageW(list, LB_SETCURSEL, _countof(Templates) - 1, 0);
        TemplateChanged();
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_TEMPLATES:
            if (code == LBN_SELCHANGE)
                TemplateChanged();
            return;
        case IDC_HO_NEWSPEC_BROWSE:
        {
            std::wstring current = ItemText(IDC_HO_NEWSPEC_PATH);
            std::wstring path;
            bool chosen;
            {
                NestedScope nested(*this);
                chosen = BrowseSaveFile(HWindow, IDS_FILTER_SPECS, L"*.handoff.json", L"json",
                                        handoff::FileNameOf(current), handoff::ParentOf(current), path);
            }
            if (chosen)
            {
                // The save dialog may append only ".json"; specifications keep their double extension.
                if (!handoff::EndsWithNoCase(path, L".handoff.json"))
                {
                    if (handoff::EndsWithNoCase(path, L".json"))
                        path.resize(path.size() - 5);
                    path += L".handoff.json";
                }
                SetItemText(IDC_HO_NEWSPEC_PATH, path);
            }
            return;
        }
        }
        WDialog::OnCommand(id, code, control);
    }

    void OnOk() override
    {
        size_t index = SelectedTemplate();
        std::wstring path = handoff::Trim(ItemText(IDC_HO_NEWSPEC_PATH));
        if (index >= _countof(Templates) || path.empty())
            return;
        path = handoff::FullPathOf(path);
        const void* data = NULL;
        DWORD size = 0;
        if (!TemplateBytes(Templates[index].ResourceId, data, size))
        {
            Prompt(TextF(IDS_NEWSPEC_FAILED, {path, handoff::Win32ErrorText(ERROR_RESOURCE_DATA_NOT_FOUND)}), MB_OK | MB_ICONERROR);
            return;
        }
        // Only the specification folder itself is created on demand (".handoff").
        std::wstring parent = handoff::ParentOf(path);
        if (handoff::EqualsNoCase(handoff::FileNameOf(parent), L".handoff") &&
            GetFileAttributesW(handoff::LongPath(parent).c_str()) == INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryW(handoff::LongPath(parent).c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            Prompt(TextF(IDS_NEWSPEC_FAILED, {path, handoff::Win32ErrorText(GetLastError())}), MB_OK | MB_ICONERROR);
            return;
        }
        HANDLE file = CreateFileW(handoff::LongPath(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                Prompt(TextF(IDS_NEWSPEC_EXISTS, {path}), MB_OK | MB_ICONWARNING);
            else
                Prompt(TextF(IDS_NEWSPEC_FAILED, {path, handoff::Win32ErrorText(error)}), MB_OK | MB_ICONERROR);
            return;
        }
        DWORD written = 0;
        BOOL ok = WriteFile(file, data, size, &written, NULL) && written == size;
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        if (!CloseHandle(file) && ok)
        {
            ok = FALSE;
            error = GetLastError();
        }
        if (!ok)
        {
            // The incomplete file was created by this dialog a moment ago, so removing it is
            // safe; deletion goes through the engine's single owned-delete path (T8.4).
            handoff::Win32FileSystem().DeleteOwnedFile(path);
            Prompt(TextF(IDS_NEWSPEC_FAILED, {path, handoff::Win32ErrorText(error)}), MB_OK | MB_ICONERROR);
            return;
        }
        AddRecentSpec(path);
        NotifyPathChanged(parent);
        RequestFocus(Panel, parent, handoff::FileNameOf(path), false);
        Close(IDOK);
    }

private:
    std::wstring Folder;
    int Panel;

    size_t SelectedTemplate() const
    {
        HWND list = Item(IDC_HO_TEMPLATES);
        int index = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
        if (index < 0)
            return _countof(Templates);
        return (size_t)SendMessageW(list, LB_GETITEMDATA, index, 0);
    }

    void TemplateChanged()
    {
        size_t index = SelectedTemplate();
        if (index >= _countof(Templates))
            return;
        SetItemText(IDC_HO_TEMPLATE_DESC, Text(Templates[index].DescriptionId));
        SetItemText(IDC_HO_NEWSPEC_PATH,
                    handoff::PathJoin(handoff::PathJoin(Folder, L".handoff"), Templates[index].FileName));
    }
};

} // namespace

bool StartNewSpec(const SessionInput& input)
{
    if (!input.Source.IsWindowsPath || !input.Source.PathFits)
        return false;
    if (!ClaimSlot(UiSlot::NewSpec))
        return true;
    std::wstring folder = input.Source.Path;
    int panel = input.Source.Panel;
    bool alwaysOnTop = input.AlwaysOnTop;
    return StartUiThread("Handoff New Specification", UiSlot::NewSpec, [folder, panel, alwaysOnTop]() {
        NewSpecDialog dialog(folder, panel);
        dialog.SetAlwaysOnTop(alwaysOnTop);
        dialog.RunModal(NULL);
    });
}
