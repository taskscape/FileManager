// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "handoff_ui.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "wdialog.h"

// Configuration dialog IDD_HO_CONFIG (handoff-spec.md A.5.7, C.8.9), opened
// modally from Plugins Manager on the main thread. Changes apply on OK; the
// host persists them through SaveConfiguration.

namespace
{

class ConfigDialog : public WDialog
{
public:
    ConfigDialog() : WDialog(IDD_HO_CONFIG, IDH_HANDOFF_CONFIG), Values(GetConfig()) {}

protected:
    BOOL OnInitDialog() override
    {
        SetItemText(IDC_HO_CFG_LIBRARY, Values.SpecLibrary);
        SetChecked(IDC_HO_CFG_SHOW_UNASSIGNED, Values.ShowUnassigned);
        SetChecked(IDC_HO_CFG_FOCUS_PACKAGE, Values.FocusNewPackage);
        SetChecked(IDC_HO_CFG_KEEP_PARTIAL, Values.KeepFailedStaging);
        EnableItem(IDC_HO_CFG_CLEAR_RECENT, !Values.RecentSpecs.empty());
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_CFG_LIBRARY_BROWSE:
        {
            std::wstring folder;
            bool chosen;
            {
                NestedScope nested(*this);
                chosen = BrowseFolder(HWindow, IDS_PICK_LIBRARY, ItemText(IDC_HO_CFG_LIBRARY), folder);
            }
            if (chosen)
                SetItemText(IDC_HO_CFG_LIBRARY, folder);
            return;
        }
        case IDC_HO_CFG_CLEAR_RECENT:
            ClearRecent = true;
            EnableItem(IDC_HO_CFG_CLEAR_RECENT, false);
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

    void OnOk() override
    {
        std::wstring library = handoff::Trim(ItemText(IDC_HO_CFG_LIBRARY));
        // The library is used as an absolute folder; relative text would depend on the current directory.
        if (!library.empty())
            library = handoff::StripLongPrefix(handoff::FullPathOf(library));
        bool showUnassigned = IsChecked(IDC_HO_CFG_SHOW_UNASSIGNED);
        bool focusPackage = IsChecked(IDC_HO_CFG_FOCUS_PACKAGE);
        bool keepPartial = IsChecked(IDC_HO_CFG_KEEP_PARTIAL);
        bool clearRecent = ClearRecent;
        UpdateConfig([&](HandoffConfig& config) {
            config.SpecLibrary = library;
            config.ShowUnassigned = showUnassigned;
            config.FocusNewPackage = focusPackage;
            config.KeepFailedStaging = keepPartial;
            if (clearRecent)
                config.RecentSpecs.clear();
        });
        Close(IDOK);
    }

private:
    HandoffConfig Values;
    bool ClearRecent = false;
};

} // namespace

void RunConfigurationDialog(HWND parent)
{
    // The folder picker needs COM on this thread; the host usually initialized it already.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    {
        ConfigDialog dialog;
        dialog.RunModal(parent);
    }
    if (SUCCEEDED(hr))
        CoUninitialize();
}
