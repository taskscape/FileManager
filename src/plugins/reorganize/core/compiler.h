// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "validate.h"

namespace reorg
{

CCompiledPlan CompilePlan(const COverlay& overlay, const CPlanDocument& plan, CIssueSink& sink);

} // namespace reorg
