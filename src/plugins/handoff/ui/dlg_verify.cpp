// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "handoff_ui.h"
#include "inspect.h"
#include "json.h"
#include "listview_util.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "spec_source.h"
#include "text_util.h"
#include "ui_thread.h"
#include "verify.h"
#include "wdialog.h"
#include "worker_bridge.h"

// Verify window IDD_HO_VERIFY (handoff-spec.md C.5.8, C.8.5): integrity of a
// package, authentication through the build record, and a rule re-check with
// the specification found beside the package, in the library, or chosen by the user.

namespace
{

const WPARAM JobVerify = 3;

struct VerifyOutput
{
    handoff::VerifyResult Result;
    std::wstring SpecText; // label for IDC_HO_VERIFY_SPEC
};

bool HasManifest(const std::wstring& folder)
{
    return !folder.empty() && !handoff::FindManifest(folder).empty();
}

// C.5.8 step 3: the build record snapshot first, then library and recent
// files with the same specification id, preferring the recorded hash.
std::unique_ptr<LoadedSpec> ResolveSpecification(const std::wstring& packageRoot, const std::wstring& chosenPath,
                                                 const handoff::BuildRecordInfo& record, int& originId)
{
    originId = IDS_VERIFY_SPEC_NONE;
    if (!chosenPath.empty())
    {
        std::unique_ptr<LoadedSpec> chosen(new LoadedSpec(LoadSpecFile(chosenPath)));
        originId = IDS_VERIFY_SPEC_FILE;
        return chosen->Read && chosen->Result.Ok ? std::move(chosen) : nullptr;
    }
    std::wstring manifestPath = handoff::FindManifest(packageRoot);
    std::vector<unsigned char> bytes;
    DWORD error = ERROR_SUCCESS;
    handoff::ParsedManifest manifest;
    std::wstring parseError;
    if (manifestPath.empty() || !handoff::ReadWholeFile(manifestPath, handoff::ManifestJsonLimits().MaxBytes, bytes, error) ||
        !handoff::ParseManifest(bytes.data(), bytes.size(), manifest, parseError))
        return nullptr;

    if (record.Found && !record.SpecText.empty())
    {
        std::unique_ptr<LoadedSpec> snapshot(
            new LoadedSpec(LoadSpecText(record.SpecPath, handoff::WideToUtf8(record.SpecText))));
        if (snapshot->Result.Ok && snapshot->Result.Model.Id == manifest.SpecId)
        {
            // The snapshot is the exact specification the build used (its hash is
            // recorded beside it); re-encoding the text must not look like an edit.
            if (!record.SpecSha256.empty())
                snapshot->Result.Model.Sha256 = record.SpecSha256;
            originId = IDS_VERIFY_SPEC_BUILDRECORD;
            return snapshot;
        }
    }
    HandoffConfig config = GetConfig();
    std::vector<std::wstring> candidates = ListSpecFiles(config.SpecLibrary);
    candidates.insert(candidates.end(), config.RecentSpecs.begin(), config.RecentSpecs.end());
    std::unique_ptr<LoadedSpec> fallback;
    for (const std::wstring& path : candidates)
    {
        std::unique_ptr<LoadedSpec> spec(new LoadedSpec(LoadSpecFile(path)));
        if (!spec->Read || !spec->Result.Ok || spec->Result.Model.Id != manifest.SpecId)
            continue;
        if (handoff::EqualsNoCase(spec->Result.Model.Sha256, manifest.SpecSha256))
        {
            originId = IDS_VERIFY_SPEC_FILE;
            return spec;
        }
        if (!fallback)
            fallback = std::move(spec);
    }
    if (fallback)
        originId = IDS_VERIFY_SPEC_FILE;
    return fallback;
}

class VerifyWindow : public WDialog
{
public:
    VerifyWindow(const std::wstring& packageRoot, int panel) : WDialog(IDD_HO_VERIFY, IDH_HANDOFF_VERIFY), PackageRoot(packageRoot), Panel(panel) {}

protected:
    BOOL OnInitDialog() override
    {
        SetSlotWindow(UiSlot::Verify, HWindow);
        SetTitle(TextF(IDS_VERIFY_TITLE, {handoff::FileNameOf(PackageRoot)}));
        SetItemText(IDC_HO_VERIFY_PATH, PackageRoot);
        HandoffConfig config = GetConfig();
        HWND list = Item(IDC_HO_FINDINGS);
        InitListView(list, false, true);
        AddColumns(list, {{IDS_COL_SEVERITY, 90}, {IDS_COL_CODE, 100}, {IDS_COL_ITEM, 260}, {IDS_COL_MESSAGE, 420}},
                   config.VerifyColumns);
        SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETRANGE32, 0, 1000);
        SetItemText(IDCANCEL, Text(IDS_CLOSE));

        Anchor(IDC_HO_VERIFY_PATH, AnchorTopStretch);
        Anchor(IDC_HO_VERIFY_SPEC, AnchorTopStretch);
        Anchor(IDC_HO_VERIFY_SPEC_BROWSE, AnchorTopRight);
        Anchor(IDC_HO_STATUS_BANNER, AnchorTopStretch);
        Anchor(IDC_HO_FINDINGS, AnchorAll);
        Anchor(IDC_HO_PROGRESS_TEXT, AnchorBottomStretch);
        Anchor(IDC_HO_PROGRESS_TOTAL, AnchorBottomStretch);
        for (int id : {IDC_HO_SHOW_IN_PANEL, IDC_HO_SAVE_REPORT, IDC_HO_VERIFY_AGAIN})
            Anchor(id, AnchorBottomLeft);
        Anchor(IDCANCEL, AnchorBottomRight);
        Anchor(IDHELP, AnchorBottomRight);
        EnableResizing();
        RestorePlacement(config.VerifyPlacement);
        StartVerify();
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_VERIFY_SPEC_BROWSE:
        {
            std::wstring path;
            bool chosen;
            {
                NestedScope nested(*this);
                chosen = BrowseOpenFile(HWindow, IDS_PICK_SPEC, IDS_FILTER_SPECS, L"*.handoff.json", std::wstring(), path);
            }
            if (chosen && !Running())
            {
                ChosenSpec = path;
                StartVerify();
            }
            return;
        }
        case IDC_HO_VERIFY_AGAIN:
            if (!Running())
                StartVerify();
            return;
        case IDC_HO_SHOW_IN_PANEL:
            ShowSelected();
            return;
        case IDC_HO_SAVE_REPORT:
            SaveVerifyReport();
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

    LRESULT OnNotify(int id, NMHDR* header) override
    {
        if (id == IDC_HO_FINDINGS && header->code == NM_DBLCLK)
            ShowSelected();
        else if (id == IDC_HO_FINDINGS && header->code == LVN_ITEMCHANGED)
            UpdateButtons();
        return 0;
    }

    bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam, INT_PTR& result) override
    {
        if (message == WM_HO_PROGRESS)
        {
            std::unique_ptr<ProgressMessage> progress((ProgressMessage*)lParam);
            if (progress->Total > 0)
                SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS,
                                    (WPARAM)((progress->Done >= progress->Total ? progress->Total : progress->Done) * 1000 / progress->Total), 0);
            SetItemText(IDC_HO_PROGRESS_TEXT, progress->Item);
            result = TRUE;
            return true;
        }
        if (message == WM_HO_DONE && wParam == JobVerify)
        {
            std::unique_ptr<VerifyOutput> output((VerifyOutput*)lParam);
            OnVerifyDone(output.get());
            result = TRUE;
            return true;
        }
        return false;
    }

    void OnOk() override
    {
        // The window has no OK button: Enter on the findings list shows the item,
        // and elsewhere it does nothing rather than closing the window.
        if (GetFocus() == Item(IDC_HO_FINDINGS))
            ShowSelected();
    }

    void OnCloseRequest() override
    {
        // Verification only reads, so closing simply cancels the worker.
        if (Worker)
            Worker->RequestCancel();
        DestroyWindow(HWindow);
    }

    void OnDestroyed() override
    {
        UpdateConfig([this](HandoffConfig& config) {
            config.VerifyPlacement = Placement();
            config.VerifyColumns = SaveColumnWidths(Item(IDC_HO_FINDINGS));
        });
        if (Worker)
            Worker->Detach();
        MSG msg;
        while (PeekMessageW(&msg, HWindow, WM_HO_PROGRESS, WM_HO_DONE, PM_REMOVE))
        {
            if (msg.message == WM_HO_PROGRESS)
                delete (ProgressMessage*)msg.lParam;
            else
                delete (VerifyOutput*)msg.lParam;
        }
    }

private:
    std::wstring PackageRoot;
    int Panel;
    std::wstring ChosenSpec;
    std::shared_ptr<WorkerControl> Worker;
    bool HaveResult = false;
    handoff::VerifyResult Result;
    std::wstring SpecLabel;

    // Set on start and cleared by the completion message, so buttons never
    // depend on the moment the worker thread itself exits.
    bool Busy = false;
    bool Running() const { return Busy; }

    void StartVerify()
    {
        Worker = std::make_shared<WorkerControl>(HWindow);
        Busy = true;
        HaveResult = false;
        ListView_DeleteAllItems(Item(IDC_HO_FINDINGS));
        SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_BANNER_VERIFYING));
        SetItemText(IDC_HO_VERIFY_SPEC, std::wstring());
        ShowItem(IDC_HO_PROGRESS_TOTAL, true);
        ShowItem(IDC_HO_PROGRESS_TEXT, true);
        SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS, 0, 0);
        std::wstring root = PackageRoot, chosen = ChosenSpec;
        bool started = StartWorker(Worker, JobVerify, [root, chosen](WorkerContext& context) {
            WorkerProgress progress(context);
            std::unique_ptr<VerifyOutput> output(new VerifyOutput);
            handoff::VerifyInput input;
            input.PackageRoot = root;
            input.BuildRecord = handoff::ReadBuildRecordBeside(root);
            int originId = IDS_VERIFY_SPEC_NONE;
            std::unique_ptr<LoadedSpec> spec = ResolveSpecification(root, chosen, input.BuildRecord, originId);
            if (spec)
            {
                input.SpecModel = &spec->Result.Model;
                input.SpecOrigin = spec->Path;
            }
            std::unique_ptr<handoff::IImageInspector> images = handoff::CreateWicImageInspector();
            std::unique_ptr<handoff::IPdfPageInspector> pdf = handoff::CreateWinRtPdfPageInspector();
            output->Result = handoff::VerifyPackage(input, progress, images.get(), pdf.get(), Catalog());
            std::wstring label = spec ? TextF(originId, {handoff::FileNameOf(spec->Path)}) : Text(IDS_VERIFY_SPEC_NONE);
            if (spec)
            {
                bool changed = !handoff::EqualsNoCase(spec->Result.Model.Sha256, output->Result.Manifest.SpecSha256);
                label += L" — " + Text(changed ? IDS_VERIFY_SPEC_CHANGED : IDS_VERIFY_SPEC_MATCH);
            }
            if (output->Result.ManifestOk && !output->Result.ManifestAuthenticated && !input.BuildRecord.Found)
                label += L"\r\n" + handoff::LabelText(Catalog(), handoff::Label::MANIFEST_UNAUTHENTICATED);
            output->SpecText = label;
            context.Deliver(WM_HO_DONE, JobVerify, output.release());
        });
        if (!started)
        {
            Busy = false;
            SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_ERR_START));
            ShowItem(IDC_HO_PROGRESS_TOTAL, false);
            ShowItem(IDC_HO_PROGRESS_TEXT, false);
        }
        UpdateButtons();
    }

    void OnVerifyDone(VerifyOutput* output)
    {
        Busy = false;
        ShowItem(IDC_HO_PROGRESS_TOTAL, false);
        ShowItem(IDC_HO_PROGRESS_TEXT, false);
        if (output == nullptr)
        {
            SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_RESULT_FAILED_UNEXPECTED));
            UpdateButtons();
            return;
        }
        Result = output->Result;
        HaveResult = true;
        SpecLabel = output->SpecText;
        SetItemText(IDC_HO_VERIFY_SPEC, SpecLabel);
        const handoff::ITextCatalog& catalog = Catalog();
        handoff::Label outcome = Result.Status == L"verified"               ? handoff::Label::OUTCOME_VERIFIED
                                 : Result.Status == L"verifiedWithWarnings" ? handoff::Label::OUTCOME_VERIFIED_WARNINGS
                                                                            : handoff::Label::OUTCOME_FAILED;
        int errors = 0, warnings = 0, infos = 0;
        for (const handoff::Finding& finding : Result.Findings)
        {
            if (finding.Sev == handoff::Severity::Error)
                errors++;
            else if (finding.Sev == handoff::Severity::Warning)
                warnings++;
            else
                infos++;
        }
        SetItemText(IDC_HO_STATUS_BANNER,
                    handoff::LabelText(catalog, outcome) + L" — " +
                        handoff::LabelText(catalog, handoff::Label::REPORT_COUNTS,
                                           {std::to_wstring(errors), std::to_wstring(warnings), std::to_wstring(infos)}));
        HWND list = Item(IDC_HO_FINDINGS);
        for (size_t i = 0; i < Result.Findings.size(); i++)
        {
            const handoff::Finding& finding = Result.Findings[i];
            int row = AddRow(list, handoff::SeverityLabel(finding.Sev, catalog), (LPARAM)i, SeverityImage(finding.Sev));
            SetCell(list, row, 1, finding.Code);
            SetCell(list, row, 2, finding.Item);
            SetCell(list, row, 3, handoff::FindingMessage(finding, catalog));
        }
        UpdateButtons();
    }

    std::wstring SelectedItemPath() const
    {
        HWND list = Item(IDC_HO_FINDINGS);
        int row = FocusedOrSelectedRow(list);
        if (row < 0 || !HaveResult)
            return std::wstring();
        LPARAM index = RowParam(list, row);
        if (index < 0 || (size_t)index >= Result.Findings.size() || Result.Findings[(size_t)index].Item.empty())
            return std::wstring();
        return handoff::PathJoin(PackageRoot, handoff::ToBackslashes(Result.Findings[(size_t)index].Item));
    }

    void UpdateButtons()
    {
        bool idle = !Running();
        EnableItem(IDC_HO_SHOW_IN_PANEL, !SelectedItemPath().empty());
        EnableItem(IDC_HO_SAVE_REPORT, idle && HaveResult);
        EnableItem(IDC_HO_VERIFY_AGAIN, idle);
        EnableItem(IDC_HO_VERIFY_SPEC_BROWSE, idle);
    }

    void ShowSelected()
    {
        std::wstring path = SelectedItemPath();
        if (path.empty())
            return;
        // A package file that no longer exists is focused by its folder only.
        bool exists = GetFileAttributesW(handoff::LongPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
        std::wstring folder = handoff::ParentOf(path);
        if (!RequestFocus(Panel, folder, exists ? handoff::FileNameOf(path) : std::wstring(), false))
            Prompt(Text(IDS_FOCUS_TOO_LONG), MB_OK | MB_ICONINFORMATION);
    }

    void SaveVerifyReport()
    {
        if (!HaveResult)
            return;
        const handoff::ITextCatalog& catalog = Catalog();
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        std::string bytes = handoff::ReportText(
            handoff::LabelText(catalog, handoff::Label::REPORT_VERIFY_TITLE),
            {{handoff::LabelText(catalog, handoff::Label::REPORT_PACKAGE), PackageRoot},
             {handoff::LabelText(catalog, handoff::Label::REPORT_SPEC), SpecLabel},
             {handoff::LabelText(catalog, handoff::Label::REPORT_STATUS), ItemText(IDC_HO_STATUS_BANNER)},
             {handoff::LabelText(catalog, handoff::Label::REPORT_CREATED), handoff::FormatIsoUtc(now)}},
            {{handoff::LabelText(catalog, handoff::Label::REPORT_FINDINGS), FindingLines(Result.Findings)}});
        NestedScope nested(*this);
        // Never into the package: writing there would itself make verification fail.
        SaveReport(HWindow, handoff::FileNameOf(PackageRoot) + L"-verification.txt", bytes, PackageRoot);
    }
};

} // namespace

bool StartVerify(const SessionInput& input)
{
    if (!input.Source.IsWindowsPath || !input.Source.PathFits)
        return false;
    if (!ClaimSlot(UiSlot::Verify))
        return true;
    return StartUiThread("Handoff Verify", UiSlot::Verify, [input]() { RunVerifyThread(input); });
}

void RunVerifyThread(const SessionInput& input)
{
    // C.5.8 step 1: the focused folder with a manifest, else the current folder, else ask.
    std::wstring root;
    if (input.FocusedIsDir && !input.FocusedName.empty())
    {
        std::wstring focused = handoff::PathJoin(input.Source.Path, input.FocusedName);
        if (HasManifest(focused))
            root = focused;
    }
    if (root.empty() && HasManifest(input.Source.Path))
        root = input.Source.Path;
    if (root.empty() && !BrowseFolder(NULL, IDS_PICK_PACKAGE, input.Source.Path, root))
        return;
    VerifyWindow* window = new VerifyWindow(handoff::StripLongPrefix(handoff::FullPathOf(root)), input.Source.Panel);
    window->SetAlwaysOnTop(input.AlwaysOnTop);
    if (window->CreateModeless(NULL))
        RunModelessLoop();
}
