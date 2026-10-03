// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Progress and cancellation seam between engine workers and the host UI.

#include <stdint.h>
#include <string>

namespace handoff
{

enum class Phase
{
    Scan,
    Inspect,
    Copy,
    Verify,
    Publish
};

class IProgress
{
public:
    virtual ~IProgress() {}
    // Called often; implementations throttle UI updates themselves.
    virtual void Report(Phase phase, uint64_t done, uint64_t total, const std::wstring& item) = 0;
    // Backed by the worker's stop event; checked between files and data chunks.
    virtual bool StopRequested() = 0;
};

class NullProgress : public IProgress
{
public:
    void Report(Phase, uint64_t, uint64_t, const std::wstring&) override {}
    bool StopRequested() override { return false; }
};

} // namespace handoff
