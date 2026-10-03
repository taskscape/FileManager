// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "validate.h"

namespace reorg
{

struct CReportRow
{
    std::wstring Kind;
    std::wstring Original;
    std::wstring Proposed;
    std::wstring Reason;
    std::wstring Reversible;
    std::wstring IssueCodes;
    std::wstring IssueMessages;
    std::wstring Resolution;
    std::wstring Acknowledged;
};

std::string WriteReportCsv(const std::vector<CReportRow>& rows);
std::string WriteReportHtml(const std::wstring& planName, const std::wstring& planId, const std::wstring& hash, const std::vector<CReportRow>& rows);

} // namespace reorg
