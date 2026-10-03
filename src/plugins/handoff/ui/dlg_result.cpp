// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "build_session.h"
#include "listview_util.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "verify.h"
#include "wdialog.h"

// Result dialog IDD_HO_RESULT (handoff-spec.md C.8.4).

namespace
{

class ResultDialog : public WDialog
{
public:
    ResultDialog(const BuildSessionData& session, const handoff::BuildResult* result, int fileCount, uint64_t totalBytes)
        : WDialog(IDD_HO_RESULT, IDH_HANDOFF_BUILD), Session(session), Result(result), FileCount(fileCount),
          TotalBytes(totalBytes)
    {
        if (Result != nullptr)
        {
            Findings = Result->Manifest.Findings;
            Findings.insert(Findings.end(), Result->Findings.begin(), Result->Findings.end());
        }
    }

    bool Published() const { return Result != nullptr && Result->Outcome == handoff::BuildOutcome::Published; }

protected:
    BOOL OnInitDialog() override
    {
        const handoff::ITextCatalog& catalog = Catalog();
        bool warnings = false;
        for (const handoff::Finding& finding : Findings)
            warnings = warnings || finding.Sev == handoff::Severity::Warning;
        handoff::Label outcome = !Published() ? handoff::Label::OUTCOME_NOT_BUILT
                                 : warnings   ? handoff::Label::OUTCOME_VERIFIED_WARNINGS
                                              : handoff::Label::OUTCOME_VERIFIED;
        OutcomeText = handoff::LabelText(catalog, outcome);
        SetItemText(IDC_HO_RESULT_TEXT, OutcomeText);
        LPCWSTR icon = !Published() ? HO_IDI_ERROR : warnings ? HO_IDI_WARNING : HO_IDI_INFORMATION;
        HICON image = (HICON)LoadImageW(NULL, icon, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
        SendDlgItemMessageW(HWindow, IDC_HO_RESULT_ICON, STM_SETICON, (WPARAM)image, 0);

        std::wstring path, totals;
        if (Result == nullptr)
            totals = Text(IDS_RESULT_FAILED_UNEXPECTED);
        else if (Published())
        {
            path = Result->PackagePath;
            totals = TextF(IDS_RESULT_TOTALS, {std::to_wstring(Result->Manifest.Files.size()),
                                               handoff::FormatSize(Result->Manifest.TotalBytes())});
        }
        else if (!Result->PartialPath.empty())
        {
            path = Result->PartialPath;
            totals = TextF(IDS_RESULT_KEPT, {Result->PartialPath});
        }
        else
        {
            path = Session.StagingRoot;
            totals = Text(IDS_RESULT_REMOVED);
        }
        SetItemText(IDC_HO_RESULT_PATH, handoff::StripLongPrefix(path));
        SetItemText(IDC_HO_RESULT_TOTALS, totals);

        HWND list = Item(IDC_HO_FINDINGS);
        InitListView(list, false, true);
        AddColumns(list, {{IDS_COL_SEVERITY, 90}, {IDS_COL_CODE, 100}, {IDS_COL_ITEM, 200}, {IDS_COL_MESSAGE, 360}}, {});
        for (const handoff::Finding& finding : Findings)
        {
            int row = AddRow(list, handoff::SeverityLabel(finding.Sev, catalog), 0, SeverityImage(finding.Sev));
            SetCell(list, row, 1, finding.Code);
            SetCell(list, row, 2, finding.Item);
            SetCell(list, row, 3, handoff::FindingMessage(finding, catalog));
        }
        EnableItem(IDC_HO_FOCUS_PACKAGE, Published());
        SetItemText(IDOK, Text(IDS_CLOSE));
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        switch (id)
        {
        case IDC_HO_FOCUS_PACKAGE:
            if (Published() && !RequestFocus(Session.StagingPanel, Session.StagingRoot,
                                             handoff::FileNameOf(Result->PackagePath), false))
                Prompt(Text(IDS_FOCUS_TOO_LONG), MB_OK | MB_ICONINFORMATION);
            return;
        case IDC_HO_SAVE_REPORT:
            SaveResultReport();
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

private:
    const BuildSessionData& Session;
    const handoff::BuildResult* Result;
    int FileCount;
    uint64_t TotalBytes;
    std::vector<handoff::Finding> Findings;
    std::wstring OutcomeText;

    void SaveResultReport()
    {
        const handoff::ITextCatalog& catalog = Catalog();
        std::wstring name = Result != nullptr ? handoff::FileNameOf(Result->PackagePath) : std::wstring();
        if (name.empty())
            name = L"handoff";
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        std::string bytes = handoff::ReportText(
            handoff::LabelText(catalog, handoff::Label::REPORT_REVIEW_TITLE),
            {{handoff::LabelText(catalog, handoff::Label::REPORT_PACKAGE), ItemText(IDC_HO_RESULT_PATH)},
             {handoff::LabelText(catalog, handoff::Label::REPORT_SPEC), Session.Spec.Result.Model.Name + L" " +
                                                                           Session.Spec.Result.Model.Revision},
             {handoff::LabelText(catalog, handoff::Label::REPORT_STATUS), OutcomeText},
             {handoff::LabelText(catalog, handoff::Label::REPORT_CREATED), handoff::FormatIsoUtc(now)}},
            {{handoff::LabelText(catalog, handoff::Label::REPORT_FINDINGS), FindingLines(Findings)}});
        NestedScope nested(*this);
        // A published package must stay exactly as verified, so the report never goes inside it.
        SaveReport(HWindow, name + L"-build.txt", bytes, Published() ? Result->PackagePath : std::wstring());
    }
};

} // namespace

bool RunResultDialog(HWND owner, const BuildSessionData& session, const handoff::BuildResult* result, int fileCount,
                     uint64_t totalBytes)
{
    ResultDialog dialog(session, result, fileCount, totalBytes);
    dialog.SetAlwaysOnTop(session.AlwaysOnTop);
    dialog.RunModal(owner);
    return dialog.Published();
}
