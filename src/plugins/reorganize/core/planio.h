// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "plan.h"
#include "json.h"

namespace reorg
{

struct CPlanIoResult
{
    bool Ok;
    std::wstring Error;
    int Line;
    int Column;
    CPlanIoResult() : Ok(false), Line(1), Column(1) {}
};

CPlanIoResult LoadPlanJson(const std::string& utf8, CPlanDocument& plan);
std::string SavePlanJson(const CPlanDocument& plan);
CPlanIoResult SavePlanFile(IFileSystemProbe& probe, const std::wstring& path, CPlanDocument& plan);

} // namespace reorg
