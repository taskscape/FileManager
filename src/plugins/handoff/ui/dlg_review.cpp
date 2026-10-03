// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../config.h"
#include "../handoff.h"
#include "build.h"
#include "build_session.h"
#include "file_system.h"
#include "formats.h"
#include "handoff_ui.h"
#include "listview_util.h"
#include "plan.h"
#include "report_text.h"
#include "review.h"
#include "scanner.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "ui_thread.h"
#include "verify.h"
#include "wdialog.h"
#include "worker_bridge.h"

// Review window IDD_HO_REVIEW (handoff-spec.md C.8.3): scan and inspection
// on a worker, rules/items/findings, reviewer decisions, gating, and the
// build with progress, cancellation, and retry prompts.

namespace
{

const WPARAM JobScan = 1;
const WPARAM JobBuild = 2;
const UINT WM_HO_RECOMPUTE = WM_APP + 30;
const UINT WM_HO_REPLAY = WM_APP + 31;

// Item rows carry an index plus a kind tag in their LPARAM.
const LPARAM RowCandidate = 0;
const LPARAM RowExcluded = 0x10000000;
const LPARAM RowOmission = 0x20000000;
const LPARAM RowKindMask = 0x30000000;

enum ItemFilter
{
    FilterAll,
    FilterProblems,
    FilterOmissions,
    FilterNotIncluded,
    FilterSuperseded
};

struct ScanOutput
{
    handoff::ReviewModel Model;
    bool Cancelled = false;
};

struct BuildOutput
{
    handoff::BuildResult Result;
};

struct DeferredMessage
{
    UINT Message;
    WPARAM WParam;
    LPARAM LParam;
};

// Snapshot of the file a decision was made for, so re-scanning keeps
// decisions only for unchanged files (same path, size, and time).
struct DecisionFile
{
    std::wstring Rel;
    uint64_t Size = 0;
    FILETIME LastWrite = {};
};

bool IsOmission(const handoff::Finding& finding)
{
    return finding.Code == L"HO-REQ-001" || finding.Code == L"HO-REQ-002" || finding.Code == L"HO-REQ-010";
}

bool IsOpenProblem(const handoff::Finding& finding)
{
    return (finding.Sev == handoff::Severity::Error && !finding.Overridden) || finding.Sev == handoff::Severity::Warning;
}

std::wstring RoleText(const std::wstring& role)
{
    if (role == L"deliverable")
        return Text(IDS_ROLE_DELIVERABLE);
    if (role == L"source")
        return Text(IDS_ROLE_SOURCE);
    if (role == L"asset")
        return Text(IDS_ROLE_ASSET);
    if (role == L"document")
        return Text(IDS_ROLE_DOCUMENT);
    if (role == L"supporting")
        return Text(IDS_ROLE_SUPPORTING);
    return role;
}

std::wstring SeverityText(const handoff::Finding& finding)
{
    std::wstring text = handoff::SeverityLabel(finding.Sev, Catalog());
    if (finding.Overridden)
        text += L" *";
    return text;
}

std::wstring CurrentUser()
{
    std::vector<wchar_t> name(257, L'\0');
    DWORD size = (DWORD)name.size();
    std::wstring user = GetUserNameW(name.data(), &size) ? std::wstring(name.data()) : std::wstring();
    std::vector<wchar_t> domain(256, L'\0');
    DWORD length = GetEnvironmentVariableW(L"USERDOMAIN", domain.data(), (DWORD)domain.size());
    if (length > 0 && length < domain.size() && !user.empty())
        return std::wstring(domain.data()) + L"\\" + user;
    return user;
}

std::wstring MachineName()
{
    std::vector<wchar_t> name(MAX_COMPUTERNAME_LENGTH + 2, L'\0');
    DWORD size = (DWORD)name.size();
    return GetComputerNameW(name.data(), &size) ? std::wstring(name.data()) : std::wstring();
}

std::wstring NewSessionId()
{
    GUID guid;
    wchar_t text[64] = {};
    if (FAILED(CoCreateGuid(&guid)) || StringFromGUID2(guid, text, (int)_countof(text)) == 0)
        return std::wstring();
    return text;
}

class ReviewWindow : public WDialog
{
public:
    explicit ReviewWindow(const BuildSessionData& data) : WDialog(IDD_HO_REVIEW, IDH_HANDOFF_REVIEW), S(data) {}

protected:
    BOOL OnInitDialog() override
    {
        SetSlotWindow(UiSlot::Build, HWindow);
        SetTitle(TextF(IDS_REVIEW_TITLE, {PackageNameText()}));
        HandoffConfig config = GetConfig();
        ShowUnassigned = config.ShowUnassigned;

        HWND rules = Item(IDC_HO_RULES);
        InitListView(rules, false, false);
        AddColumns(rules, {{IDS_COL_RULE, 220}, {IDS_COL_ROLE, 90}, {IDS_COL_REQUIRED, 70}, {IDS_COL_SELECTED, 70},
                           {IDS_COL_EXPECTED, 70}, {IDS_COL_STATUS, 110}},
                   {});
        HWND items = Item(IDC_HO_ITEMS);
        InitListView(items, true, false);
        AddColumns(items, {{IDS_COL_SOURCE, 260}, {IDS_COL_TARGET, 260}, {IDS_COL_SIZE, 70, LVCFMT_RIGHT},
                           {IDS_COL_DETAILS, 170}, {IDS_COL_APPROVAL, 120}, {IDS_COL_LICENCE, 110}, {IDS_COL_STATUS, 110}},
                   config.ReviewColumns);
        HWND findings = Item(IDC_HO_FINDINGS);
        InitListView(findings, false, true);
        AddColumns(findings, {{IDS_COL_SEVERITY, 90}, {IDS_COL_CODE, 100}, {IDS_COL_ITEM, 240}, {IDS_COL_MESSAGE, 420}}, {});

        HWND filter = Item(IDC_HO_FILTER);
        for (int id : {IDS_FILTER_ALL, IDS_FILTER_PROBLEMS, IDS_FILTER_OMISSIONS, IDS_FILTER_NOTINCLUDED, IDS_FILTER_SUPERSEDED})
            ComboAdd(filter, Text(id), 0);
        SendMessageW(filter, CB_SETCURSEL, 0, 0);

        SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETRANGE32, 0, 1000);
        SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_FILE, PBM_SETRANGE32, 0, 1000);
        ShowItem(IDC_HO_OVERRIDE, SpecModel().AllowErrorOverride);

        Anchor(IDC_HO_STATUS_BANNER, AnchorTopStretch);
        Anchor(IDC_HO_LABEL_RULES, AnchorTopLeft);
        Anchor(IDC_HO_RULES, AnchorTopStretch);
        Anchor(IDC_HO_LABEL_ITEMS, AnchorTopLeft);
        Anchor(IDC_HO_FILTER, AnchorTopRight);
        Anchor(IDC_HO_ITEMS, AnchorAll);
        Anchor(IDC_HO_LABEL_FINDINGS, AnchorBottomLeft);
        Anchor(IDC_HO_FINDINGS, AnchorBottomStretch);
        for (int id : {IDC_HO_APPROVE, IDC_HO_APPROVE_ALL, IDC_HO_SHOW_IN_PANEL, IDC_HO_OVERRIDE, IDC_HO_RESCAN, IDC_HO_SAVE_REPORT})
            Anchor(id, AnchorBottomLeft);
        Anchor(IDC_HO_ACK_WARNINGS, AnchorBottomLeft);
        Anchor(IDC_HO_PROGRESS_TEXT, AnchorBottomStretch);
        Anchor(IDC_HO_PROGRESS_TOTAL, AnchorBottomStretch);
        Anchor(IDC_HO_PROGRESS_FILE, AnchorBottomStretch);
        Anchor(IDOK, AnchorBottomRight);
        Anchor(IDCANCEL, AnchorBottomRight);
        Anchor(IDHELP, AnchorBottomRight);
        EnableResizing();
        RestorePlacement(config.ReviewPlacement);

        StartScan();
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_FILTER:
            if (code == CBN_SELCHANGE)
            {
                Filter = (int)SendDlgItemMessageW(HWindow, IDC_HO_FILTER, CB_GETCURSEL, 0, 0);
                FillItems();
            }
            return;
        case IDC_HO_APPROVE:
            Approve(false);
            return;
        case IDC_HO_APPROVE_ALL:
            Approve(true);
            return;
        case IDC_HO_SHOW_IN_PANEL:
            ShowSelectedInPanel();
            return;
        case IDC_HO_OVERRIDE:
            OverrideSelected();
            return;
        case IDC_HO_RESCAN:
            if (State == Busy::None)
                StartScan();
            return;
        case IDC_HO_SAVE_REPORT:
            SaveReviewReport();
            return;
        case IDC_HO_ACK_WARNINGS:
            UpdateGate();
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

    LRESULT OnNotify(int id, NMHDR* header) override
    {
        if (id == IDC_HO_RULES && header->code == LVN_ITEMCHANGED && !Populating)
        {
            NMLISTVIEW* change = (NMLISTVIEW*)header;
            if ((change->uChanged & LVIF_STATE) != 0 && (change->uNewState & LVIS_SELECTED) != 0 &&
                (change->uOldState & LVIS_SELECTED) == 0)
            {
                RuleFilter = (int)RowParam(Item(IDC_HO_RULES), change->iItem);
                FillItems();
                FillFindings();
                UpdateButtons();
            }
        }
        else if (id == IDC_HO_ITEMS && header->code == LVN_ITEMCHANGING && !Populating)
        {
            NMLISTVIEW* change = (NMLISTVIEW*)header;
            if ((change->uChanged & LVIF_STATE) != 0 && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) != 0)
            {
                // Only included-or-excluded candidates have a check box, and never while work runs.
                const handoff::Candidate* candidate = CandidateOfParam(change->lParam);
                if (State != Busy::None || candidate == nullptr || candidate->Superseded)
                    return TRUE;
            }
        }
        else if (id == IDC_HO_ITEMS && header->code == LVN_ITEMCHANGED && !Populating)
        {
            NMLISTVIEW* change = (NMLISTVIEW*)header;
            if ((change->uChanged & LVIF_STATE) != 0 && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) != 0 &&
                (change->uOldState & LVIS_STATEIMAGEMASK) != 0)
                ToggleInclude(change->lParam, (change->uNewState & LVIS_STATEIMAGEMASK) == INDEXTOSTATEIMAGEMASK(2));
            else if ((change->uChanged & LVIF_STATE) != 0 && ((change->uNewState ^ change->uOldState) & LVIS_SELECTED) != 0)
                UpdateButtons();
        }
        else if (id == IDC_HO_ITEMS && header->code == NM_DBLCLK)
            ShowSelectedInPanel();
        else if (id == IDC_HO_FINDINGS && header->code == NM_DBLCLK)
            FocusFindingItem();
        else if (id == IDC_HO_FINDINGS && header->code == LVN_ITEMCHANGED)
            UpdateButtons();
        return 0;
    }

    bool OnMessage(UINT message, WPARAM wParam, LPARAM lParam, INT_PTR& result) override
    {
        switch (message)
        {
        case WM_HO_PROGRESS:
        {
            std::unique_ptr<ProgressMessage> progress((ProgressMessage*)lParam);
            ShowProgress(*progress);
            result = TRUE;
            return true;
        }
        case WM_HO_DONE:
        case WM_HO_RETRY:
            // Completion and retry prompts may destroy this window or open UI;
            // while nested UI is open they wait until it closes.
            if (InNested())
                Deferred.push_back(DeferredMessage{message, wParam, lParam});
            else if (message == WM_HO_DONE)
                OnDone(wParam, lParam);
            else
                OnRetry((RetryMessage*)lParam);
            result = TRUE;
            return true;
        case WM_HO_REPLAY:
            if (!InNested() && !Deferred.empty())
            {
                DeferredMessage next = Deferred.front();
                Deferred.erase(Deferred.begin());
                if (!Deferred.empty())
                    PostMessageW(HWindow, WM_HO_REPLAY, 0, 0);
                if (next.Message == WM_HO_DONE)
                    OnDone(next.WParam, next.LParam);
                else
                    OnRetry((RetryMessage*)next.LParam);
            }
            result = TRUE;
            return true;
        case WM_HO_RECOMPUTE:
            if (State == Busy::None && HaveModel)
                Recompute();
            result = TRUE;
            return true;
        }
        return false;
    }

    void OnNestedEnded() override
    {
        if (!Deferred.empty())
            PostMessageW(HWindow, WM_HO_REPLAY, 0, 0);
    }

    void OnOk() override
    {
        // Enter on a list means "Show in panel" (C.8.3), not "Build".
        HWND focus = GetFocus();
        if (focus == Item(IDC_HO_ITEMS))
        {
            ShowSelectedInPanel();
            return;
        }
        if (focus == Item(IDC_HO_FINDINGS))
        {
            FocusFindingItem();
            return;
        }
        if (BuildAllowed())
            StartBuild();
    }

    void OnCloseRequest() override
    {
        if (State != Busy::None)
        {
            if (!ShuttingDown())
            {
                int question = State == Busy::Build ? IDS_CONFIRM_CANCEL_BUILD : IDS_CONFIRM_CANCEL_SCAN;
                if (Prompt(Text(question), MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                if (State == Busy::None)
                {
                    // The work finished while the question was open.
                    DestroyWindow(HWindow);
                    return;
                }
            }
            CloseWhenIdle = true;
            Worker->RequestCancel();
            SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_BANNER_STOPPED));
            // A scan has no side effects, and during unload the build worker cleans
            // up on its own, so those close at once; otherwise the window waits until
            // the cancelled build has removed what it created.
            if (State == Busy::Scan || ShuttingDown())
                DestroyWindow(HWindow);
            return;
        }
        DestroyWindow(HWindow);
    }

    void OnDestroyed() override
    {
        UpdateConfig([this](HandoffConfig& config) {
            config.ReviewPlacement = Placement();
            config.ReviewColumns = SaveColumnWidths(Item(IDC_HO_ITEMS));
        });
        if (Worker)
            Worker->Detach();
        for (const DeferredMessage& message : Deferred)
            Dispose(message.Message, message.WParam, message.LParam);
        Deferred.clear();
        // Payloads already queued for this window would be lost with it.
        MSG msg;
        while (PeekMessageW(&msg, HWindow, WM_HO_PROGRESS, WM_HO_RETRY, PM_REMOVE))
            Dispose(msg.message, msg.wParam, msg.lParam);
        if (State == Busy::Build)
            SetBuildRunning(false);
    }

private:
    enum class Busy
    {
        None,
        Scan,
        Build
    };

    BuildSessionData S;
    std::shared_ptr<WorkerControl> Worker;
    Busy State = Busy::None;
    bool CloseWhenIdle = false;
    bool HaveModel = false;
    bool ShowUnassigned = true;
    handoff::ReviewModel Model;
    handoff::ReviewerDecisions Decisions;
    std::map<std::wstring, DecisionFile> DecisionFiles; // decision key -> file snapshot
    handoff::Variables Vars;
    handoff::PackagePlan Plan;
    handoff::Gate GateState;
    std::vector<handoff::Finding> AllFindings;
    std::vector<size_t> ShownFindings; // findings list row -> AllFindings index
    std::map<size_t, std::wstring> TargetByCandidate;
    std::map<std::wstring, std::pair<uint64_t, uint64_t>> Offsets; // target -> (start byte, size)
    std::map<std::wstring, size_t> OrdinalByTarget;
    std::vector<DeferredMessage> Deferred;
    int RuleFilter = -1;
    int Filter = FilterAll;
    bool Populating = false;

    const handoff::Spec& SpecModel() const { return S.Spec.Result.Model; }

    std::wstring PackageNameText() const
    {
        handoff::Variables variables = handoff::BuildVariables(SpecModel(), S.Values, S.Now);
        return handoff::ExpandPackageName(SpecModel(), variables, S.Now).Value;
    }

    static void Dispose(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (lParam == 0)
            return;
        if (message == WM_HO_PROGRESS)
            delete (ProgressMessage*)lParam;
        else if (message == WM_HO_RETRY)
        {
            // An unanswered prompt cancels the build rather than leaving the worker waiting.
            RetryMessage* retry = (RetryMessage*)lParam;
            retry->Reply(false);
            delete retry;
        }
        else if (message == WM_HO_DONE && wParam == JobScan)
            delete (ScanOutput*)lParam;
        else if (message == WM_HO_DONE && wParam == JobBuild)
            delete (BuildOutput*)lParam;
    }

    // ---- scanning --------------------------------------------------------

    void StartScan()
    {
        Worker = std::make_shared<WorkerControl>(HWindow);
        State = Busy::Scan;
        CloseWhenIdle = false;
        handoff::ScanInput input;
        input.WorkingRoot = S.WorkingRoot;
        input.SelectionOnly = S.SelectionOnly;
        input.SelectedNames = S.Selected;
        std::shared_ptr<handoff::Spec> spec = std::make_shared<handoff::Spec>(SpecModel());
        SetItemText(IDC_HO_STATUS_BANNER, TextF(IDS_BANNER_SCANNING, {L"0"}));
        UpdateBusyUi();
        bool started = StartWorker(Worker, JobScan, [input, spec](WorkerContext& context) {
            WorkerProgress progress(context);
            std::unique_ptr<ScanOutput> output(new ScanOutput);
            handoff::ScanResult scan = handoff::ScanWorkingMaterial(input, progress);
            output->Cancelled = scan.Cancelled || context.Stopped();
            if (!output->Cancelled)
            {
                output->Model = handoff::Assemble(*spec, scan, input.WorkingRoot);
                std::unique_ptr<handoff::IImageInspector> images = handoff::CreateWicImageInspector();
                std::unique_ptr<handoff::IPdfPageInspector> pdf = handoff::CreateWinRtPdfPageInspector();
                handoff::InspectCandidates(output->Model, *spec, images.get(), pdf.get(), progress);
                output->Cancelled = context.Stopped();
            }
            context.Deliver(WM_HO_DONE, JobScan, output.release());
        });
        if (!started)
        {
            State = Busy::None;
            Prompt(Text(IDS_ERR_START), MB_OK | MB_ICONERROR);
            UpdateBusyUi();
        }
    }

    void OnDone(WPARAM job, LPARAM payload)
    {
        if (job == JobScan)
            OnScanDone((ScanOutput*)payload);
        else if (job == JobBuild)
            OnBuildDone((BuildOutput*)payload);
    }

    void OnScanDone(ScanOutput* raw)
    {
        std::unique_ptr<ScanOutput> output(raw);
        State = Busy::None;
        if (CloseWhenIdle)
        {
            DestroyWindow(HWindow);
            return;
        }
        if (!output || output->Cancelled)
        {
            // A cancelled re-scan leaves no trustworthy model; building needs a complete scan.
            HaveModel = false;
            ClearLists();
            SetItemText(IDC_HO_STATUS_BANNER, Text(output ? IDS_BANNER_STOPPED : IDS_RESULT_FAILED_UNEXPECTED));
            UpdateBusyUi();
            return;
        }
        PruneDecisions(output->Model);
        Model = std::move(output->Model);
        HaveModel = true;
        Recompute();
        UpdateBusyUi();
    }

    void PruneDecisions(const handoff::ReviewModel& next)
    {
        std::map<std::wstring, const handoff::ScannedFile*> byRel;
        for (const handoff::ScannedFile& file : next.Files)
            byRel[handoff::FoldKey(file.Rel)] = &file;
        for (auto it = DecisionFiles.begin(); it != DecisionFiles.end();)
        {
            auto found = byRel.find(handoff::FoldKey(it->second.Rel));
            bool unchanged = found != byRel.end() && found->second->Size == it->second.Size &&
                             CompareFileTime(&found->second->LastWrite, &it->second.LastWrite) == 0;
            if (unchanged)
            {
                ++it;
                continue;
            }
            Decisions.Excluded.erase(it->first);
            Decisions.Approved.erase(it->first);
            Decisions.Overrides.erase(it->first);
            it = DecisionFiles.erase(it);
        }
    }

    void RememberFile(const std::wstring& key, const std::wstring& rel)
    {
        for (const handoff::ScannedFile& file : Model.Files)
            if (handoff::EqualsNoCase(file.Rel, rel))
            {
                DecisionFiles[key] = DecisionFile{file.Rel, file.Size, file.LastWrite};
                return;
            }
        DecisionFiles[key] = DecisionFile{rel, 0, {}};
    }

    void Log(const wchar_t* action, const std::wstring& source, const std::wstring& ruleId, const std::wstring& code = L"",
             const std::wstring& reason = L"")
    {
        handoff::DecisionRecord record;
        record.Action = action;
        record.Source = source;
        record.RuleId = ruleId;
        record.Code = code;
        record.Reason = reason;
        record.User = CurrentUser();
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        record.Utc = handoff::FormatIsoUtc(now);
        Decisions.Log.push_back(record);
    }

    // ---- model -> lists --------------------------------------------------

    void Recompute()
    {
        const handoff::Spec& spec = SpecModel();
        handoff::Evaluate(Model, spec, Decisions, S.Now, Catalog());
        Vars = handoff::BuildVariables(spec, S.Values, S.Now);
        uint64_t freeBytes = 0;
        handoff::Win32FileSystem fs;
        if (!fs.QueryFreeSpace(S.StagingRoot, freeBytes))
            freeBytes = 0;
        Plan = handoff::PlanPackage(Model, spec, Vars, S.Now, freeBytes, Decisions, Catalog());
        GateState = handoff::ComputeGate(Model, Plan, spec);
        AllFindings = Model.Findings;
        AllFindings.insert(AllFindings.end(), Plan.Findings.begin(), Plan.Findings.end());
        TargetByCandidate.clear();
        Offsets.clear();
        OrdinalByTarget.clear();
        uint64_t offset = 0;
        for (const handoff::PlannedFile& file : Plan.Files)
        {
            if (file.CandidateIndex >= 0)
                TargetByCandidate[(size_t)file.CandidateIndex] = file.TargetRel;
            Offsets[file.TargetRel] = std::make_pair(offset, file.Size);
            size_t ordinal = OrdinalByTarget.size() + 1;
            OrdinalByTarget[file.TargetRel] = ordinal;
            offset += file.Size;
        }
        // Any change invalidates an earlier acknowledgement (C.5.6).
        SetChecked(IDC_HO_ACK_WARNINGS, false);
        FillRules();
        FillItems();
        FillFindings();
        UpdateGate();
    }

    void ClearLists()
    {
        Populating = true;
        ListView_DeleteAllItems(Item(IDC_HO_RULES));
        ListView_DeleteAllItems(Item(IDC_HO_ITEMS));
        ListView_DeleteAllItems(Item(IDC_HO_FINDINGS));
        Populating = false;
        ShownFindings.clear();
    }

    void FillRules()
    {
        HWND list = Item(IDC_HO_RULES);
        Populating = true;
        ListView_DeleteAllItems(list);
        AddRow(list, Text(IDS_ALL_RULES), (LPARAM)-1);
        const handoff::Spec& spec = SpecModel();
        for (size_t r = 0; r < spec.Rules.size(); r++)
        {
            const handoff::RuleSpec& rule = spec.Rules[r];
            const handoff::RuleSummary& summary = Model.Rules[r];
            int row = AddRow(list, rule.Title.empty() ? rule.Id : rule.Title, (LPARAM)r);
            SetCell(list, row, 1, RoleText(rule.Role));
            SetCell(list, row, 2, rule.Required ? Text(IDS_YES) : std::wstring());
            SetCell(list, row, 3, std::to_wstring(summary.Selected));
            SetCell(list, row, 4, summary.HasExpected ? std::to_wstring(summary.Expected) : std::wstring());
            std::wstring status;
            if (summary.Missing)
                status = Text(IDS_STATUS_MISSING);
            else if (summary.Errors > 0)
                status = TextF(IDS_STATUS_ERRORS, {std::to_wstring(summary.Errors)});
            else if (summary.Warnings > 0)
                status = TextF(IDS_STATUS_WARNINGS, {std::to_wstring(summary.Warnings)});
            else
                status = Text(IDS_STATUS_OK);
            SetCell(list, row, 5, status);
        }
        int select = FindRowByParam(list, (LPARAM)RuleFilter);
        SelectRow(list, select >= 0 ? select : 0);
        Populating = false;
    }

    const handoff::Candidate* CandidateOfParam(LPARAM param) const
    {
        if ((param & RowKindMask) != RowCandidate || param < 0)
            return nullptr;
        size_t index = (size_t)param;
        return index < Model.Candidates.size() ? &Model.Candidates[index] : nullptr;
    }

    std::wstring Details(const handoff::Candidate& candidate) const
    {
        const handoff::ITextCatalog& catalog = Catalog();
        std::wstring format = handoff::FormatLabel(SpecModel().CustomFormats, candidate.Format);
        if (candidate.PagesInspected && candidate.Pages.Ok)
        {
            std::vector<std::wstring> sizes;
            for (const auto& size : candidate.Pages.SizesMm)
            {
                std::wstring text = handoff::PageSizeText(size.first, size.second, &catalog);
                if (std::find(sizes.begin(), sizes.end(), text) == sizes.end())
                    sizes.push_back(text);
                if (sizes.size() >= 3)
                    break;
            }
            std::wstring pages = std::to_wstring(candidate.Pages.SizesMm.size());
            return sizes.empty() ? handoff::LabelText(catalog, handoff::Label::CT_PDF, {pages})
                                 : handoff::LabelText(catalog, handoff::Label::CT_PDF_SIZE, {pages, handoff::Join(sizes, L", ")});
        }
        if (candidate.ImageInspected && candidate.Image.Decoded)
        {
            std::wstring text = handoff::LabelText(catalog, handoff::Label::CT_PIXELS,
                                                   {format, std::to_wstring(candidate.Image.Width), std::to_wstring(candidate.Image.Height)});
            if (candidate.Image.DpiKnown)
                text += L", " + handoff::DecimalText(candidate.Image.DpiX, 0) + L" dpi";
            return text;
        }
        return format;
    }

    std::wstring ApprovalText(const handoff::Candidate& candidate) const
    {
        const handoff::ApprovalSpec& approval = SpecModel().Rules[(size_t)candidate.RuleIndex].Approval;
        std::vector<std::wstring> parts;
        if (approval.NeedsEvidence())
            parts.push_back(Text(candidate.ApprovalEvidence ? IDS_APPROVAL_EVIDENCE : IDS_APPROVAL_MISSING));
        if (approval.NeedsConfirm())
            parts.push_back(Text(candidate.Approved ? IDS_APPROVAL_APPROVED : IDS_APPROVAL_AWAITING));
        return handoff::Join(parts, L", ");
    }

    std::wstring LicenceText(const handoff::Candidate& candidate) const
    {
        const handoff::LicenseSpec& licence = SpecModel().Rules[(size_t)candidate.RuleIndex].License;
        if (!licence.Present)
            return std::wstring();
        if (candidate.LicenceFound || !candidate.LicenceEvidence.empty())
            return TextF(IDS_LICENCE_FOUND, {candidate.Licence});
        return Text(IDS_LICENCE_MISSING);
    }

    std::wstring CandidateStatus(const handoff::Candidate& candidate) const
    {
        if (candidate.Superseded)
            return TextF(IDS_STATUS_SUPERSEDED, {candidate.SupersededBy});
        if (!candidate.Included)
            return Text(IDS_STATUS_EXCLUDED);
        int errors = 0, warnings = 0;
        for (const handoff::Finding& finding : candidate.Findings)
        {
            if (finding.Sev == handoff::Severity::Error && !finding.Overridden)
                errors++;
            else if (finding.Sev == handoff::Severity::Warning)
                warnings++;
        }
        if (errors > 0)
            return TextF(IDS_STATUS_ERRORS, {std::to_wstring(errors)});
        if (warnings > 0)
            return TextF(IDS_STATUS_WARNINGS, {std::to_wstring(warnings)});
        if (candidate.Deferred && !candidate.Inspected)
            return Text(IDS_STATUS_ONLINE);
        return Text(IDS_STATUS_INCLUDED);
    }

    static void SetCheckBox(HWND list, int row, int state)
    {
        // State image 0 hides the check box; 1 unchecked; 2 checked.
        ListView_SetItemState(list, row, INDEXTOSTATEIMAGEMASK(state), LVIS_STATEIMAGEMASK);
    }

    void FillItems()
    {
        HWND list = Item(IDC_HO_ITEMS);
        LPARAM keep = -1;
        int focused = FocusedOrSelectedRow(list);
        if (focused >= 0)
            keep = RowParam(list, focused);
        Populating = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        if (HaveModel)
        {
            for (size_t i = 0; i < Model.Candidates.size(); i++)
            {
                const handoff::Candidate& candidate = Model.Candidates[i];
                if (RuleFilter >= 0 && candidate.RuleIndex != RuleFilter)
                    continue;
                bool problem = false;
                for (const handoff::Finding& finding : candidate.Findings)
                    problem = problem || IsOpenProblem(finding);
                bool show = Filter == FilterAll || (Filter == FilterProblems && problem) ||
                            (Filter == FilterSuperseded && candidate.Superseded);
                if (!show)
                    continue;
                const handoff::ScannedFile& file = Model.Files[candidate.FileIndex];
                int row = AddRow(list, candidate.Rel, (LPARAM)i | RowCandidate);
                auto target = TargetByCandidate.find(i);
                SetCell(list, row, 1, target != TargetByCandidate.end() ? target->second : std::wstring());
                SetCell(list, row, 2, handoff::FormatSize(file.Size));
                SetCell(list, row, 3, Details(candidate));
                SetCell(list, row, 4, ApprovalText(candidate));
                SetCell(list, row, 5, LicenceText(candidate));
                SetCell(list, row, 6, CandidateStatus(candidate));
                SetCheckBox(list, row, candidate.Superseded ? 0 : candidate.Included ? 2 : 1);
            }
            if (RuleFilter < 0)
            {
                for (size_t j = 0; j < Model.Excluded.size(); j++)
                {
                    const handoff::ExcludedFile& excluded = Model.Excluded[j];
                    bool forbidden = excluded.Code == L"HO-CONT-001";
                    bool show = (Filter == FilterAll && (ShowUnassigned || forbidden)) || Filter == FilterNotIncluded ||
                                (Filter == FilterProblems && forbidden);
                    if (!show)
                        continue;
                    const handoff::ScannedFile& file = Model.Files[excluded.FileIndex];
                    int row = AddRow(list, file.Rel, (LPARAM)j | RowExcluded);
                    SetCell(list, row, 2, handoff::FormatSize(file.Size));
                    SetCell(list, row, 6, Text(forbidden ? IDS_STATUS_FORBIDDEN : IDS_STATUS_NOTINCLUDED));
                    SetCheckBox(list, row, 0);
                }
            }
            for (size_t k = 0; k < AllFindings.size(); k++)
            {
                const handoff::Finding& finding = AllFindings[k];
                if (!IsOmission(finding) || (Filter != FilterAll && Filter != FilterProblems && Filter != FilterOmissions))
                    continue;
                if (RuleFilter >= 0 && finding.RuleId != SpecModel().Rules[(size_t)RuleFilter].Id)
                    continue;
                int row = AddRow(list, handoff::FindingMessage(finding, Catalog()), (LPARAM)k | RowOmission);
                SetCell(list, row, 6, Text(IDS_STATUS_MISSING));
                SetCheckBox(list, row, 0);
            }
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        int restore = keep >= 0 ? FindRowByParam(list, keep) : -1;
        if (restore >= 0)
            SelectRow(list, restore);
        Populating = false;
        UpdateButtons();
    }

    void FillFindings()
    {
        HWND list = Item(IDC_HO_FINDINGS);
        Populating = true;
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        ShownFindings.clear();
        std::wstring ruleId = RuleFilter >= 0 ? SpecModel().Rules[(size_t)RuleFilter].Id : std::wstring();
        for (size_t i = 0; i < AllFindings.size(); i++)
        {
            const handoff::Finding& finding = AllFindings[i];
            if (!ruleId.empty() && finding.RuleId != ruleId)
                continue;
            int row = AddRow(list, SeverityText(finding), (LPARAM)ShownFindings.size(), SeverityImage(finding.Sev));
            SetCell(list, row, 1, finding.Code);
            SetCell(list, row, 2, finding.Item);
            SetCell(list, row, 3, handoff::FindingMessage(finding, Catalog()));
            ShownFindings.push_back(i);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        Populating = false;
    }

    // ---- gating, buttons, progress --------------------------------------

    bool BuildAllowed() const
    {
        if (State != Busy::None || !HaveModel || !GateState.CanBuild)
            return false;
        return !GateState.NeedsAcknowledgement || IsChecked(IDC_HO_ACK_WARNINGS);
    }

    void UpdateGate()
    {
        if (State == Busy::None && HaveModel)
        {
            if (Plan.Files.empty() && GateState.Errors == 0)
                SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_BANNER_NOTHING));
            else if (GateState.Errors > 0 || GateState.Warnings > 0)
                SetItemText(IDC_HO_STATUS_BANNER, TextF(IDS_BANNER_COUNTS, {std::to_wstring(GateState.Errors), std::to_wstring(GateState.Warnings)}));
            else
                SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_BANNER_READY));
        }
        ShowItem(IDC_HO_ACK_WARNINGS, HaveModel && GateState.NeedsAcknowledgement);
        EnableItem(IDC_HO_ACK_WARNINGS, State == Busy::None);
        EnableItem(IDOK, BuildAllowed());
        UpdateButtons();
    }

    void UpdateBusyUi()
    {
        bool busy = State != Busy::None;
        ShowItem(IDC_HO_PROGRESS_TOTAL, busy);
        ShowItem(IDC_HO_PROGRESS_FILE, State == Busy::Build);
        ShowItem(IDC_HO_PROGRESS_TEXT, busy);
        if (busy)
        {
            SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS, 0, 0);
            SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_FILE, PBM_SETPOS, 0, 0);
            SetItemText(IDC_HO_PROGRESS_TEXT, std::wstring());
        }
        SetItemText(IDCANCEL, Text(busy ? IDS_CANCEL : IDS_CLOSE));
        EnableItem(IDC_HO_RESCAN, !busy);
        EnableItem(IDC_HO_FILTER, true);
        UpdateGate();
    }

    const handoff::Finding* SelectedFinding() const
    {
        HWND list = Item(IDC_HO_FINDINGS);
        int row = FocusedOrSelectedRow(list);
        if (row < 0)
            return nullptr;
        LPARAM index = RowParam(list, row);
        if (index < 0 || (size_t)index >= ShownFindings.size())
            return nullptr;
        return &AllFindings[ShownFindings[(size_t)index]];
    }

    bool CanOverride(const handoff::Finding* finding) const
    {
        if (finding == nullptr || !SpecModel().AllowErrorOverride || finding->Sev != handoff::Severity::Error || finding->Overridden)
            return false;
        const handoff::CodeInfo* info = handoff::FindCode(finding->Code);
        return info != nullptr && !info->Locked;
    }

    int SelectedRuleForApproval() const
    {
        if (RuleFilter >= 0)
            return RuleFilter;
        HWND list = Item(IDC_HO_ITEMS);
        int row = FocusedOrSelectedRow(list);
        const handoff::Candidate* candidate = row >= 0 ? CandidateOfParam(RowParam(list, row)) : nullptr;
        return candidate != nullptr ? candidate->RuleIndex : -1;
    }

    void UpdateButtons()
    {
        bool idle = State == Busy::None && HaveModel;
        HWND list = Item(IDC_HO_ITEMS);
        bool approvable = false, showable = false;
        for (int row : SelectedRows(list))
        {
            LPARAM param = RowParam(list, row);
            const handoff::Candidate* candidate = CandidateOfParam(param);
            showable = showable || candidate != nullptr || (param & RowKindMask) == RowExcluded;
            if (candidate != nullptr && candidate->Included && !candidate->Approved &&
                SpecModel().Rules[(size_t)candidate->RuleIndex].Approval.NeedsConfirm())
                approvable = true;
        }
        int rule = SelectedRuleForApproval();
        bool ruleApprovable = rule >= 0 && SpecModel().Rules[(size_t)rule].Approval.NeedsConfirm();
        EnableItem(IDC_HO_APPROVE, idle && approvable);
        EnableItem(IDC_HO_APPROVE_ALL, idle && ruleApprovable);
        EnableItem(IDC_HO_SHOW_IN_PANEL, HaveModel && showable);
        EnableItem(IDC_HO_OVERRIDE, idle && CanOverride(SelectedFinding()));
        EnableItem(IDC_HO_SAVE_REPORT, HaveModel && State == Busy::None);
    }

    void ShowProgress(const ProgressMessage& progress)
    {
        if (State == Busy::None)
            return;
        auto permille = [](uint64_t done, uint64_t total) -> int {
            if (total == 0)
                return 0;
            return (int)((done >= total ? total : done) * 1000 / total);
        };
        switch (progress.Phase)
        {
        case handoff::Phase::Scan:
            SetItemText(IDC_HO_STATUS_BANNER, TextF(IDS_BANNER_SCANNING, {handoff::NumberText((int64_t)progress.Done)}));
            break;
        case handoff::Phase::Inspect:
            SetItemText(IDC_HO_STATUS_BANNER, TextF(IDS_BANNER_INSPECTING, {handoff::NumberText((int64_t)progress.Done),
                                                                            handoff::NumberText((int64_t)progress.Total)}));
            SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS, permille(progress.Done, progress.Total), 0);
            SetItemText(IDC_HO_PROGRESS_TEXT, progress.Item);
            break;
        case handoff::Phase::Copy:
        case handoff::Phase::Verify:
        {
            auto ordinal = OrdinalByTarget.find(progress.Item);
            size_t index = ordinal != OrdinalByTarget.end() ? ordinal->second : 0;
            SetItemText(IDC_HO_STATUS_BANNER,
                        TextF(IDS_BANNER_BUILDING, {std::to_wstring(index), std::to_wstring(Plan.Files.size())}));
            SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS, permille(progress.Done, progress.Total), 0);
            auto offset = Offsets.find(progress.Item);
            if (offset != Offsets.end())
            {
                uint64_t start = offset->second.first, size = offset->second.second;
                uint64_t done = progress.Done > start ? progress.Done - start : 0;
                SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_FILE, PBM_SETPOS, size == 0 ? 1000 : permille(done, size), 0);
            }
            SetItemText(IDC_HO_PROGRESS_TEXT, progress.Item);
            break;
        }
        case handoff::Phase::Publish:
            SetItemText(IDC_HO_STATUS_BANNER, Text(IDS_BANNER_PUBLISHING));
            SendDlgItemMessageW(HWindow, IDC_HO_PROGRESS_TOTAL, PBM_SETPOS, 1000, 0);
            SetItemText(IDC_HO_PROGRESS_TEXT, progress.Item);
            break;
        }
    }

    // ---- reviewer actions ------------------------------------------------

    void ToggleInclude(LPARAM param, bool include)
    {
        const handoff::Candidate* candidate = CandidateOfParam(param);
        if (candidate == nullptr)
            return;
        const std::wstring& ruleId = SpecModel().Rules[(size_t)candidate->RuleIndex].Id;
        std::wstring key = handoff::ReviewerDecisions::Key(candidate->Rel, ruleId);
        if (include)
            Decisions.Excluded.erase(key);
        else
            Decisions.Excluded.insert(key);
        RememberFile(key, candidate->Rel);
        Log(include ? L"include" : L"exclude", candidate->Rel, ruleId);
        // The list cannot be rebuilt from inside its own notification.
        PostMessageW(HWindow, WM_HO_RECOMPUTE, 0, 0);
    }

    void Approve(bool wholeRule)
    {
        if (State != Busy::None || !HaveModel)
            return;
        std::vector<size_t> indexes;
        if (wholeRule)
        {
            int rule = SelectedRuleForApproval();
            for (size_t i = 0; i < Model.Candidates.size(); i++)
                if (Model.Candidates[i].RuleIndex == rule && Model.Candidates[i].Included)
                    indexes.push_back(i);
        }
        else
        {
            HWND list = Item(IDC_HO_ITEMS);
            for (int row : SelectedRows(list))
            {
                LPARAM param = RowParam(list, row);
                if (CandidateOfParam(param) != nullptr)
                    indexes.push_back((size_t)param);
            }
        }
        bool changed = false;
        for (size_t i : indexes)
        {
            const handoff::Candidate& candidate = Model.Candidates[i];
            const handoff::RuleSpec& rule = SpecModel().Rules[(size_t)candidate.RuleIndex];
            if (!rule.Approval.NeedsConfirm() || !candidate.Included || candidate.Approved)
                continue;
            std::wstring key = handoff::ReviewerDecisions::Key(candidate.Rel, rule.Id);
            Decisions.Approved.insert(key);
            RememberFile(key, candidate.Rel);
            Log(L"approve", candidate.Rel, rule.Id);
            changed = true;
        }
        if (changed)
            Recompute();
    }

    std::wstring RelOfRow(int row) const
    {
        HWND list = Item(IDC_HO_ITEMS);
        LPARAM param = RowParam(list, row);
        if (const handoff::Candidate* candidate = CandidateOfParam(param))
            return candidate->Rel;
        if ((param & RowKindMask) == RowExcluded)
        {
            size_t index = (size_t)(param & ~RowKindMask);
            if (index < Model.Excluded.size())
                return Model.Files[Model.Excluded[index].FileIndex].Rel;
        }
        return std::wstring();
    }

    void FocusWorkingFile(const std::wstring& rel)
    {
        if (rel.empty())
            return;
        std::wstring full = handoff::PathJoin(S.WorkingRoot, handoff::ToBackslashes(rel));
        if (!RequestFocus(S.WorkingPanel, handoff::ParentOf(full), handoff::FileNameOf(full), false))
            Prompt(Text(IDS_FOCUS_TOO_LONG), MB_OK | MB_ICONINFORMATION);
    }

    void ShowSelectedInPanel()
    {
        int row = FocusedOrSelectedRow(Item(IDC_HO_ITEMS));
        if (row >= 0)
            FocusWorkingFile(RelOfRow(row));
    }

    void FocusFindingItem()
    {
        const handoff::Finding* finding = SelectedFinding();
        if (finding == nullptr || finding->Item.empty())
            return;
        std::wstring item = finding->Item;
        // Select the matching row so keyboard users land on the item, then focus it in the pane.
        HWND list = Item(IDC_HO_ITEMS);
        int rows = ListView_GetItemCount(list);
        for (int row = 0; row < rows; row++)
            if (handoff::EqualsNoCase(RelOfRow(row), item))
            {
                SelectRow(list, row);
                break;
            }
        bool known = false;
        for (const handoff::ScannedFile& file : Model.Files)
            known = known || handoff::EqualsNoCase(file.Rel, item);
        if (known)
            FocusWorkingFile(item);
    }

    void OverrideSelected()
    {
        const handoff::Finding* selected = SelectedFinding();
        if (!CanOverride(selected) || State != Busy::None)
            return;
        handoff::Finding finding = *selected;
        std::wstring text = finding.Code + L"  " + finding.Item + L"\r\n" + handoff::FindingMessage(finding, Catalog());
        std::wstring reason;
        bool accepted;
        {
            NestedScope nested(*this);
            accepted = RunOverrideDialog(HWindow, text, reason);
        }
        if (!accepted)
            return;
        std::wstring key = handoff::ReviewerDecisions::OverrideKey(finding);
        Decisions.Overrides[key] = reason;
        if (!finding.Item.empty())
            RememberFile(key, finding.Item);
        Log(L"override", finding.Item, finding.RuleId, finding.Code, reason);
        Recompute();
    }

    void SaveReviewReport()
    {
        if (!HaveModel)
            return;
        const handoff::ITextCatalog& catalog = Catalog();
        std::vector<std::wstring> rules, items;
        for (size_t r = 0; r < SpecModel().Rules.size(); r++)
        {
            const handoff::RuleSpec& rule = SpecModel().Rules[r];
            const handoff::RuleSummary& summary = Model.Rules[r];
            rules.push_back(rule.Title + L" (" + rule.Id + L"): " + std::to_wstring(summary.Selected) +
                            (summary.HasExpected ? L" / " + std::to_wstring(summary.Expected) : std::wstring()));
        }
        for (size_t i = 0; i < Model.Candidates.size(); i++)
        {
            const handoff::Candidate& candidate = Model.Candidates[i];
            auto target = TargetByCandidate.find(i);
            items.push_back(candidate.Rel + (target != TargetByCandidate.end() ? L" -> " + target->second : std::wstring()) +
                            L"  [" + CandidateStatus(candidate) + L"]");
        }
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        std::wstring status;
        HWND banner = Item(IDC_HO_STATUS_BANNER);
        if (banner != NULL)
            status = ItemText(IDC_HO_STATUS_BANNER);
        std::string bytes = handoff::ReportText(
            handoff::LabelText(catalog, handoff::Label::REPORT_REVIEW_TITLE),
            {{handoff::LabelText(catalog, handoff::Label::REPORT_PACKAGE), Plan.PackageName},
             {handoff::LabelText(catalog, handoff::Label::REPORT_SPEC), SpecModel().Name + L" " + SpecModel().Revision},
             {handoff::LabelText(catalog, handoff::Label::REPORT_STATUS), status},
             {handoff::LabelText(catalog, handoff::Label::REPORT_CREATED), handoff::FormatIsoUtc(now)}},
            {{handoff::LabelText(catalog, handoff::Label::REPORT_RULES), rules},
             {handoff::LabelText(catalog, handoff::Label::REPORT_ITEMS), items},
             {handoff::LabelText(catalog, handoff::Label::REPORT_FINDINGS), FindingLines(AllFindings)}});
        NestedScope nested(*this);
        SaveReport(HWindow, Plan.PackageName + L"-review.txt", bytes, std::wstring());
    }

    // ---- building --------------------------------------------------------

    void StartBuild()
    {
        std::shared_ptr<handoff::BuildInput> input = std::make_shared<handoff::BuildInput>();
        input->SpecModel = SpecModel();
        input->Model = Model;
        input->Plan = Plan;
        input->Decisions = Decisions;
        input->Values = Vars;
        input->StagingRoot = S.StagingRoot;
        input->Now = S.Now;
        GetSystemTimeAsFileTime(&input->NowUtc);
        input->SessionId = NewSessionId();
        input->User = CurrentUser();
        input->Machine = MachineName();
        input->HostVersion = SdkToWide(VERSINFO_SALAMANDER_VERSION);
        input->PluginVersion = SdkToWide(VERSINFO_VERSION_NO_PLATFORM);
        input->SpecPath = S.Spec.Path;
        input->SpecText = S.Spec.Text;
        input->Scope = S.SelectionOnly ? L"selection" : L"all";
        input->KeepFailedPartial = GetConfig().KeepFailedStaging;
        if (input->SessionId.empty())
        {
            Prompt(Text(IDS_ERR_START), MB_OK | MB_ICONERROR);
            return;
        }
        Worker = std::make_shared<WorkerControl>(HWindow);
        State = Busy::Build;
        CloseWhenIdle = false;
        SetBuildRunning(true);
        UpdateBusyUi();
        SetItemText(IDC_HO_STATUS_BANNER, TextF(IDS_BANNER_BUILDING, {L"0", std::to_wstring(Plan.Files.size())}));
        bool started = StartWorker(Worker, JobBuild, [input](WorkerContext& context) {
            WorkerProgress progress(context);
            handoff::Win32FileSystem fs;
            std::unique_ptr<handoff::IImageInspector> images = handoff::CreateWicImageInspector();
            std::unique_ptr<handoff::IPdfPageInspector> pdf = handoff::CreateWinRtPdfPageInspector();
            std::unique_ptr<BuildOutput> output(new BuildOutput);
            output->Result = handoff::BuildPackage(*input, fs, progress, images.get(), pdf.get(), Catalog());
            context.Deliver(WM_HO_DONE, JobBuild, output.release());
        });
        if (!started)
        {
            State = Busy::None;
            SetBuildRunning(false);
            Prompt(Text(IDS_ERR_START), MB_OK | MB_ICONERROR);
            UpdateBusyUi();
        }
    }

    void OnRetry(RetryMessage* raw)
    {
        std::unique_ptr<RetryMessage> retry(raw);
        bool again = !CloseWhenIdle &&
                     Prompt(TextF(IDS_RETRY_PROMPT, {retry->Item, handoff::Win32ErrorText(retry->Error)}),
                            MB_RETRYCANCEL | MB_ICONWARNING) == IDRETRY;
        retry->Reply(again);
    }

    void OnBuildDone(BuildOutput* raw)
    {
        std::unique_ptr<BuildOutput> output(raw);
        State = Busy::None;
        SetBuildRunning(false);
        if (output)
        {
            const handoff::BuildResult& result = output->Result;
            // The staging pane shows the new package (or nothing changed visibly for a hidden partial).
            NotifyPathChanged(S.StagingRoot);
            if (result.Outcome == handoff::BuildOutcome::Published && GetConfig().FocusNewPackage)
                RequestFocus(S.StagingPanel, S.StagingRoot, handoff::FileNameOf(result.PackagePath), true);
        }
        if (CloseWhenIdle)
        {
            DestroyWindow(HWindow);
            return;
        }
        UpdateBusyUi();
        bool published;
        {
            NestedScope nested(*this);
            published = RunResultDialog(HWindow, S, output ? &output->Result : nullptr, (int)Plan.Files.size(), Plan.TotalBytes);
        }
        // Closing the result after a successful build also closes the review (C.8.4).
        if (published)
            DestroyWindow(HWindow);
    }
};

} // namespace

bool OpenReviewWindow(const BuildSessionData& data)
{
    ReviewWindow* window = new ReviewWindow(data);
    window->SetAlwaysOnTop(data.AlwaysOnTop);
    return window->CreateModeless(NULL);
}
