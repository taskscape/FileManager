// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// State handed from the build session dialog to the review window: the two
// locations, the scope, the loaded specification, and the variable values.

#include <map>
#include <string>
#include <vector>

#include "build.h"
#include "session.h"
#include "spec_source.h"

struct BuildSessionData
{
    std::wstring WorkingRoot, StagingRoot;
    int WorkingPanel = 0, StagingPanel = 0; // PANEL_LEFT / PANEL_RIGHT
    bool SelectionOnly = false;
    std::vector<std::wstring> Selected;
    LoadedSpec Spec;
    std::map<std::wstring, std::wstring> Values;
    bool AlwaysOnTop = false;
    // Local time captured once per session, so the previewed package name and
    // the built one cannot differ when the session crosses midnight.
    SYSTEMTIME Now = {};
};

// Creates the review window on the current UI thread (it starts scanning at once).
bool OpenReviewWindow(const BuildSessionData& data);

// Result dialog IDD_HO_RESULT, modal to the review window. 'result' is null
// when the build ended unexpectedly. Returns true when the package was published.
bool RunResultDialog(HWND owner, const BuildSessionData& session, const handoff::BuildResult* result, int fileCount,
                     uint64_t totalBytes);
