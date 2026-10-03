// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Plug-in configuration (handoff-spec.md C.9). The host loads and saves it on
// the main thread; UI threads read snapshots and apply changes under a lock,
// and the host persists them at its next configuration save.

#include <functional>
#include <string>
#include <vector>

struct HandoffConfig
{
    std::wstring SpecLibrary;
    std::vector<std::wstring> RecentSpecs; // most recent first, at most kMaxRecentSpecs
    bool ShowUnassigned = true;
    bool FocusNewPackage = true;
    bool KeepFailedStaging = false;
    std::vector<unsigned char> ReviewPlacement, VerifyPlacement; // WINDOWPLACEMENT bytes
    std::vector<unsigned char> ReviewColumns, VerifyColumns;     // column widths in DIP (int32 each)
};

const size_t kMaxRecentSpecs = 10;

HandoffConfig GetConfig();
void UpdateConfig(const std::function<void(HandoffConfig&)>& change);
void AddRecentSpec(const std::wstring& path);

void LoadHandoffConfig(HKEY regKey, CSalamanderRegistryAbstract* registry);
void SaveHandoffConfig(HKEY regKey, CSalamanderRegistryAbstract* registry);
