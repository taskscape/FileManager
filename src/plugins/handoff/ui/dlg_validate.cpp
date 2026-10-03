// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "handoff_ui.h"
#include "listview_util.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "spec_source.h"
#include "text_util.h"
#include "ui_thread.h"
#include "wdialog.h"

// Validate specification dialog IDD_HO_VALIDATE (handoff-spec.md C.8.7):
// every HO-SPEC finding with line and column, re-validation after editing,
// and copying the findings as text.

namespace
{

class ValidateDialog : public WDialog
{
public:
    explicit ValidateDialog(const std::wstring& path) : WDialog(IDD_HO_VALIDATE, IDH_HANDOFF_VALIDATE), Path(path) {}

protected:
    BOOL OnInitDialog() override
    {
        // Opened from the menu it is its own top-level window; a second request activates it.
        if (GetWindow(HWindow, GW_OWNER) == NULL)
            SetSlotWindow(UiSlot::Validate, HWindow);
        HWND list = Item(IDC_HO_FINDINGS);
        InitListView(list, false, true);
        AddColumns(list, {{IDS_COL_LINE, 60, LVCFMT_RIGHT}, {IDS_COL_COLUMN, 60, LVCFMT_RIGHT}, {IDS_COL_CODE, 100},
                          {IDS_COL_MESSAGE, 420}},
                   {});
        SetItemText(IDOK, Text(IDS_CLOSE));
        Validate();
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_VALIDATE_BROWSE:
        {
            std::wstring path;
            bool chosen;
            {
                NestedScope nested(*this);
                chosen = BrowseOpenFile(HWindow, IDS_PICK_SPEC, IDS_FILTER_SPECS, L"*.handoff.json",
                                        Path.empty() ? std::wstring() : handoff::ParentOf(Path), path);
            }
            if (chosen)
            {
                Path = path;
                Validate();
            }
            return;
        }
        case IDC_HO_VALIDATE_AGAIN:
            Validate();
            return;
        case IDC_HO_COPY:
            CopyTextToClipboard(HWindow, ItemText(IDC_HO_VALIDATE_PATH) + L"\r\n" + ListAsText(Item(IDC_HO_FINDINGS)));
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

private:
    std::wstring Path;

    void Validate()
    {
        SetItemText(IDC_HO_VALIDATE_PATH, Path);
        EnableItem(IDC_HO_VALIDATE_AGAIN, !Path.empty());
        HWND list = Item(IDC_HO_FINDINGS);
        ListView_DeleteAllItems(list);
        if (Path.empty())
            return;
        LoadedSpec spec = LoadSpecFile(Path);
        if (!spec.Read)
        {
            AddRow(list, std::wstring(), 0, SeverityImage(handoff::Severity::Error));
            SetCell(list, 0, 3, TextF(IDS_READ_FAILED, {handoff::FileNameOf(Path), handoff::Win32ErrorText(spec.Error)}));
            return;
        }
        if (spec.Result.Findings.empty())
        {
            int row = AddRow(list, std::wstring(), 0, SeverityImage(handoff::Severity::Info));
            SetCell(list, row, 3, Text(IDS_VALIDATE_OK));
            return;
        }
        for (const handoff::Finding& finding : spec.Result.Findings)
        {
            int row = AddRow(list, finding.Line > 0 ? std::to_wstring(finding.Line) : std::wstring(), 0, SeverityImage(finding.Sev));
            SetCell(list, row, 1, finding.Column > 0 ? std::to_wstring(finding.Column) : std::wstring());
            SetCell(list, row, 2, finding.Code);
            std::wstring message = handoff::FindingMessage(finding, Catalog());
            if (!finding.Pointer.empty())
                message += L" [" + finding.Pointer + L"]";
            SetCell(list, row, 3, message);
        }
    }
};

} // namespace

void RunValidateDialog(HWND owner, const std::wstring& path, int, bool alwaysOnTop)
{
    ValidateDialog dialog(path);
    dialog.SetAlwaysOnTop(alwaysOnTop);
    dialog.RunModal(owner);
}

bool StartValidate(const SessionInput& input)
{
    // The focused *.handoff.json of a disk pane is validated directly; otherwise the user browses.
    std::wstring path;
    if (input.Source.IsWindowsPath && input.Source.PathFits && !input.FocusedIsDir &&
        handoff::EndsWithNoCase(input.FocusedName, L".handoff.json"))
        path = handoff::PathJoin(input.Source.Path, input.FocusedName);
    if (!ClaimSlot(UiSlot::Validate))
        return true;
    bool alwaysOnTop = input.AlwaysOnTop;
    int panel = input.Source.Panel;
    return StartUiThread("Handoff Validate", UiSlot::Validate, [path, panel, alwaysOnTop]() {
        std::wstring chosen = path;
        if (chosen.empty() && !BrowseOpenFile(NULL, IDS_PICK_SPEC, IDS_FILTER_SPECS, L"*.handoff.json", std::wstring(), chosen))
            return;
        RunValidateDialog(NULL, chosen, panel, alwaysOnTop);
    });
}
