// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "build.h"
#include "build_session.h"
#include "handoff_ui.h"
#include "listview_util.h"
#include "plan.h"
#include "report_text.h"
#include "review.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "ui_thread.h"
#include "wdialog.h"

// Build session dialog IDD_HO_SESSION (handoff-spec.md C.8.2): locations,
// scope, specification, variables, and the live package-name preview.

namespace
{

const UINT WM_HO_DISCOVER = WM_APP + 20;

struct VariableRow
{
    std::wstring Name, Label;
    bool Required = false;
    std::vector<std::wstring> Choices;
};

class SessionDialog : public WDialog
{
public:
    explicit SessionDialog(const SessionInput& input) : WDialog(IDD_HO_SESSION, IDH_HANDOFF_BUILD), Input(input)
    {
        Data.WorkingRoot = input.Source.Path;
        Data.StagingRoot = input.Target.Path;
        Data.WorkingPanel = input.Source.Panel;
        Data.StagingPanel = input.Target.Panel;
        Data.Selected = input.Selected;
        Data.AlwaysOnTop = input.AlwaysOnTop;
        GetLocalTime(&Data.Now);
        Data.Values[L"date"] = handoff::FormatDate(Data.Now, L"yyyy-MM-dd");
    }

protected:
    BOOL OnInitDialog() override
    {
        SetSlotWindow(UiSlot::Build, HWindow);
        HWND variables = Item(IDC_HO_VARIABLES);
        InitListView(variables, false, false);
        AddColumns(variables, {{IDS_COL_VARIABLE, 150}, {IDS_COL_VALUE, 170}, {IDS_COL_REQUIRED, 70}}, {});
        ShowItem(IDC_HO_VARIABLE_CHOICE, false);
        EnableItem(IDC_HO_VARIABLE_VALUE, false);
        SetChecked(IDC_HO_SCOPE_ALL, true);
        ShowPaths();
        EnableItem(IDOK, false);
        // Discovery reads folders; it runs after the window is shown so it appears at once.
        PostMessageW(HWindow, WM_HO_DISCOVER, 1, 0);
        return TRUE;
    }

    bool OnMessage(UINT message, WPARAM wParam, LPARAM, INT_PTR& result) override
    {
        if (message != WM_HO_DISCOVER)
            return false;
        Discover();
        if (wParam != 0)
            OfferStalePartialCleanup();
        result = TRUE;
        return true;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_SWAP:
            Swap();
            return;
        case IDC_HO_SCOPE_ALL:
        case IDC_HO_SCOPE_SELECTION:
            return;
        case IDC_HO_SPEC_COMBO:
            if (code == CBN_SELCHANGE)
                LoadSelected();
            return;
        case IDC_HO_SPEC_BROWSE:
            Browse();
            return;
        case IDC_HO_SPEC_VALIDATE:
            if (!Loaded.Path.empty())
            {
                {
                    NestedScope nested(*this);
                    RunValidateDialog(HWindow, Loaded.Path, Data.WorkingPanel, Data.AlwaysOnTop);
                }
                // The user may have corrected the file meanwhile.
                LoadSelected();
            }
            return;
        case IDC_HO_SPEC_SHOW:
            if (!Loaded.Path.empty() &&
                !RequestFocus(Data.WorkingPanel, handoff::ParentOf(Loaded.Path), handoff::FileNameOf(Loaded.Path), false))
                Prompt(Text(IDS_FOCUS_TOO_LONG), MB_OK | MB_ICONINFORMATION);
            return;
        case IDC_HO_VARIABLE_VALUE:
            if (code == EN_CHANGE && !Updating)
                StoreValue(ItemText(IDC_HO_VARIABLE_VALUE));
            return;
        case IDC_HO_VARIABLE_CHOICE:
            if (code == CBN_SELCHANGE && !Updating)
            {
                HWND combo = Item(IDC_HO_VARIABLE_CHOICE);
                int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
                StoreValue(index >= 0 ? ComboItemText(combo, index) : std::wstring());
            }
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

    LRESULT OnNotify(int id, NMHDR* header) override
    {
        if (id == IDC_HO_VARIABLES && header->code == LVN_ITEMCHANGED)
        {
            NMLISTVIEW* change = (NMLISTVIEW*)header;
            if ((change->uChanged & LVIF_STATE) != 0 && (change->uNewState & LVIS_SELECTED) != 0 &&
                (change->uOldState & LVIS_SELECTED) == 0)
                BindRow(change->iItem);
        }
        return 0;
    }

    void OnOk() override
    {
        Revalidate();
        if (!CanScan)
            return;
        Data.SelectionOnly = IsChecked(IDC_HO_SCOPE_SELECTION);
        Data.Spec = Loaded;
        AddRecentSpec(Loaded.Path);
        // The review window takes over the build slot; this dialog closes once it exists.
        if (OpenReviewWindow(Data))
            DestroyWindow(HWindow);
    }

private:
    SessionInput Input;
    BuildSessionData Data;
    std::vector<SpecChoice> Choices;
    LoadedSpec Loaded;
    std::vector<VariableRow> Rows;
    int CurrentRow = -1;
    bool Updating = false;
    bool CanScan = false;

    bool Contained() const
    {
        std::wstring working = handoff::FullPathOf(Data.WorkingRoot);
        std::wstring staging = handoff::FullPathOf(Data.StagingRoot);
        return handoff::IsPathInsideOrEqual(working, staging) || handoff::IsPathInsideOrEqual(staging, working);
    }

    void ShowPaths()
    {
        SetItemText(IDC_HO_WORKING_PATH, Data.WorkingRoot);
        SetItemText(IDC_HO_STAGING_PATH, Data.StagingRoot);
        // The selection belongs to the pane it was made in; after a swap it no longer applies.
        bool selection = !Data.Selected.empty();
        SetItemText(IDC_HO_SCOPE_SELECTION, TextF(IDS_SELECTION_COUNT, {std::to_wstring(Data.Selected.size())}));
        EnableItem(IDC_HO_SCOPE_SELECTION, selection);
        if (!selection)
        {
            SetChecked(IDC_HO_SCOPE_SELECTION, false);
            SetChecked(IDC_HO_SCOPE_ALL, true);
        }
    }

    void Swap()
    {
        std::swap(Data.WorkingRoot, Data.StagingRoot);
        std::swap(Data.WorkingPanel, Data.StagingPanel);
        Data.Selected.clear();
        ShowPaths();
        Discover();
        OfferStalePartialCleanup();
    }

    void Discover()
    {
        std::wstring previous = Loaded.Path;
        Choices = DiscoverSpecs(Data.WorkingRoot);
        FillSpecCombo(previous);
    }

    void FillSpecCombo(const std::wstring& preferred)
    {
        HWND combo = Item(IDC_HO_SPEC_COMBO);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        int select = Choices.empty() ? -1 : 0;
        for (size_t i = 0; i < Choices.size(); i++)
        {
            ComboAdd(combo, handoff::FileNameOf(Choices[i].Path) + L" (" + Text(Choices[i].OriginTextId) + L")", (LPARAM)i);
            if (!preferred.empty() && handoff::EqualsNoCase(Choices[i].Path, preferred))
                select = (int)i;
        }
        SendMessageW(combo, CB_SETCURSEL, select, 0);
        LoadSelected();
    }

    void Browse()
    {
        std::wstring path;
        {
            NestedScope nested(*this);
            if (!BrowseOpenFile(HWindow, IDS_PICK_SPEC, IDS_FILTER_SPECS, L"*.handoff.json", Data.WorkingRoot, path))
                return;
        }
        bool known = false;
        for (const SpecChoice& choice : Choices)
            known = known || handoff::EqualsNoCase(choice.Path, path);
        if (!known)
            Choices.insert(Choices.begin(), SpecChoice{path, IDS_SPEC_ORIGIN_BROWSED});
        FillSpecCombo(path);
    }

    void LoadSelected()
    {
        HWND combo = Item(IDC_HO_SPEC_COMBO);
        int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (index < 0 || (size_t)index >= Choices.size())
        {
            Loaded = LoadedSpec();
            SetItemText(IDC_HO_SPEC_SUMMARY, Text(IDS_NO_SPEC));
        }
        else
        {
            Loaded = LoadSpecFile(Choices[(size_t)index].Path);
            SetItemText(IDC_HO_SPEC_SUMMARY, SpecSummary(Loaded));
        }
        bool haveFile = !Loaded.Path.empty();
        EnableItem(IDC_HO_SPEC_VALIDATE, haveFile);
        EnableItem(IDC_HO_SPEC_SHOW, haveFile);
        FillVariables();
        Revalidate();
    }

    bool SpecUsable() const
    {
        return Loaded.Read && Loaded.Result.Ok;
    }

    void FillVariables()
    {
        Rows.clear();
        if (SpecUsable())
        {
            for (const handoff::VariableDef& def : Loaded.Result.Model.Variables)
            {
                VariableRow row;
                row.Name = def.Name;
                row.Label = def.Label.empty() ? def.Name : def.Label;
                row.Required = def.Required;
                row.Choices = def.Choices;
                Rows.push_back(row);
                // Values typed earlier in this session survive switching specifications.
                if (Data.Values.find(def.Name) == Data.Values.end())
                    Data.Values[def.Name] = def.Default;
            }
        }
        VariableRow date;
        date.Name = L"date";
        date.Label = Text(IDS_VARIABLE_DATE);
        Rows.push_back(date);

        HWND list = Item(IDC_HO_VARIABLES);
        Updating = true;
        ListView_DeleteAllItems(list);
        for (size_t i = 0; i < Rows.size(); i++)
        {
            int row = AddRow(list, Rows[i].Label, (LPARAM)i);
            SetCell(list, row, 1, Data.Values[Rows[i].Name]);
            SetCell(list, row, 2, Rows[i].Required ? Text(IDS_YES) : std::wstring());
        }
        Updating = false;
        CurrentRow = -1;
        SelectRow(list, 0);
        BindRow(0);
    }

    void BindRow(int row)
    {
        if (row < 0 || (size_t)row >= Rows.size())
            return;
        CurrentRow = row;
        const VariableRow& variable = Rows[(size_t)row];
        const std::wstring& value = Data.Values[variable.Name];
        Updating = true;
        bool choices = !variable.Choices.empty();
        ShowItem(IDC_HO_VARIABLE_VALUE, !choices);
        ShowItem(IDC_HO_VARIABLE_CHOICE, choices);
        if (choices)
        {
            HWND combo = Item(IDC_HO_VARIABLE_CHOICE);
            SendMessageW(combo, CB_RESETCONTENT, 0, 0);
            int select = -1;
            for (size_t i = 0; i < variable.Choices.size(); i++)
            {
                ComboAdd(combo, variable.Choices[i], (LPARAM)i);
                if (variable.Choices[i] == value)
                    select = (int)i;
            }
            SendMessageW(combo, CB_SETCURSEL, select, 0);
            EnableItem(IDC_HO_VARIABLE_CHOICE, true);
        }
        else
        {
            SetItemText(IDC_HO_VARIABLE_VALUE, value);
            EnableItem(IDC_HO_VARIABLE_VALUE, true);
        }
        Updating = false;
    }

    void StoreValue(const std::wstring& value)
    {
        if (CurrentRow < 0 || (size_t)CurrentRow >= Rows.size())
            return;
        Data.Values[Rows[(size_t)CurrentRow].Name] = value;
        SetCell(Item(IDC_HO_VARIABLES), CurrentRow, 1, value);
        Revalidate();
    }

    void Revalidate()
    {
        CanScan = false;
        std::wstring name, status;
        if (Contained())
            status = handoff::FindingMessage(handoff::MakeFinding(L"HO-SES-003", L""), Catalog());
        else if (!SpecUsable())
            status = Loaded.Path.empty() ? Text(IDS_NO_SPEC) : SpecSummary(Loaded);
        else
        {
            const handoff::Spec& spec = Loaded.Result.Model;
            handoff::Variables variables = handoff::BuildVariables(spec, Data.Values, Data.Now);
            std::vector<handoff::Finding> problems = handoff::ValidateVariables(spec, variables);
            handoff::SanitizeResult package = handoff::ExpandPackageName(spec, variables, Data.Now);
            name = package.Value;
            if (!problems.empty())
                status = handoff::FindingMessage(problems.front(), Catalog());
            else if (package.Invalid || package.Reserved || package.Value.empty())
                status = Text(IDS_PACKAGE_INVALID);
            else if (GetFileAttributesW(handoff::LongPath(handoff::PathJoin(Data.StagingRoot, package.Value)).c_str()) !=
                     INVALID_FILE_ATTRIBUTES)
                status = handoff::FindingMessage(handoff::MakeFinding(L"HO-SES-005", L"", {package.Value}), Catalog());
            else
            {
                status = Text(IDS_PACKAGE_AVAILABLE);
                CanScan = true;
            }
        }
        SetItemText(IDC_HO_PACKAGE_NAME, name);
        SetItemText(IDC_HO_PACKAGE_STATUS, status);
        EnableItem(IDOK, CanScan);
    }

    void OfferStalePartialCleanup()
    {
        // C.5.1 step 4: only folders no running build holds open are offered for removal.
        handoff::Win32FileSystem fs;
        std::vector<std::wstring> stale;
        for (const handoff::StalePartial& partial : handoff::FindPartialFolders(Data.StagingRoot, fs))
            if (!partial.Live)
                stale.push_back(partial.Path);
        if (stale.empty())
            return;
        std::wstring names;
        for (size_t i = 0; i < stale.size() && i < 10; i++)
            names += L"\r\n" + handoff::FileNameOf(stale[i]);
        if (Prompt(TextF(IDS_STALE_PARTIALS, {std::to_wstring(stale.size()), names}), MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        for (const std::wstring& path : stale)
        {
            std::wstring failed;
            if (!handoff::RemovePartialFolder(path, fs, failed))
                Prompt(TextF(IDS_STALE_REMOVE_FAILED, {failed.empty() ? path : failed}), MB_OK | MB_ICONWARNING);
        }
        NotifyPathChanged(Data.StagingRoot);
        Revalidate();
    }
};

} // namespace

bool StartBuildSession(const SessionInput& input)
{
    if (!ClaimSlot(UiSlot::Build))
        return true; // the existing session was brought to the foreground
    return StartUiThread("Handoff Build Session", UiSlot::Build, [input]() { RunBuildSessionThread(input); });
}

void RunBuildSessionThread(const SessionInput& input)
{
    SessionDialog* dialog = new SessionDialog(input);
    dialog->SetAlwaysOnTop(input.AlwaysOnTop);
    if (dialog->CreateModeless(NULL))
        RunModelessLoop();
}
