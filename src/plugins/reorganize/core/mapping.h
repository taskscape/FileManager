// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "plan.h"

namespace reorg
{

struct CMappingRow
{
    int Row;
    bool Accepted;
    std::wstring Source;
    std::wstring DestinationDir;
    std::wstring NewName;
    std::wstring Note;
    std::wstring Error;
};

struct CMappingResult
{
    std::vector<CMappingRow> Rows;
};

CMappingResult ParseMapping(const std::string& csv, const std::wstring& scopeRoot, const std::wstring& destinationRoot, const CSnapshot& snapshot);

} // namespace reorg
