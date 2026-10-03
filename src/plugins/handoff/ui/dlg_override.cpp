// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "handoff_ui.h"
#include "text_util.h"
#include "wdialog.h"

// Override dialog IDD_HO_OVERRIDE (handoff-spec.md C.8.8): accepting an
// overridable error requires a recorded reason of at least ten characters.

namespace
{

const size_t kMinimumReason = 10;

class OverrideDialog : public WDialog
{
public:
    explicit OverrideDialog(const std::wstring& findingText) : WDialog(IDD_HO_OVERRIDE, IDH_HANDOFF_REVIEW), FindingText(findingText) {}

    std::wstring Reason;

protected:
    BOOL OnInitDialog() override
    {
        SetItemText(IDC_HO_OVERRIDE_FINDING, FindingText);
        SendDlgItemMessageW(HWindow, IDC_HO_OVERRIDE_REASON, EM_LIMITTEXT, 1000, 0);
        EnableItem(IDOK, false);
        return TRUE;
    }

    void OnCommand(int id, int code, HWND control) override
    {
        if (id == IDC_HO_OVERRIDE_REASON && code == EN_CHANGE)
        {
            EnableItem(IDOK, handoff::Trim(ItemText(IDC_HO_OVERRIDE_REASON)).size() >= kMinimumReason);
            return;
        }
        WDialog::OnCommand(id, code, control);
    }

    void OnOk() override
    {
        Reason = handoff::Trim(ItemText(IDC_HO_OVERRIDE_REASON));
        if (Reason.size() >= kMinimumReason)
            Close(IDOK);
    }

private:
    std::wstring FindingText;
};

} // namespace

bool RunOverrideDialog(HWND owner, const std::wstring& findingText, std::wstring& reason)
{
    OverrideDialog dialog(findingText);
    if (dialog.RunModal(owner) != IDOK)
        return false;
    reason = dialog.Reason;
    return true;
}
