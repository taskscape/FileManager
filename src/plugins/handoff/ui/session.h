// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Main-thread session capture (handoff-spec.md C.3.3 step 1) and the
// cross-thread requests that must run on the main thread: focusing a name in
// a pane (CMD_INTERNAL_FOCUS) and change notifications for refreshed panes.

#include <string>
#include <vector>

struct PaneSnapshot
{
    bool IsWindowsPath = false; // PATH_TYPE_WINDOWS (disk or UNC)
    bool PathFits = true;       // false when the path could not be converted
    std::wstring Path;
    int Panel = 0; // PANEL_LEFT or PANEL_RIGHT - stays valid when the user switches panes
};

struct SessionInput
{
    PaneSnapshot Source; // active pane
    PaneSnapshot Target; // inactive pane
    std::vector<std::wstring> Selected; // names selected directly in the active pane
    std::wstring FocusedName;           // focused item of the active pane ("" for "..")
    bool FocusedIsDir = false;
    bool AlwaysOnTop = false;
};

// Restriction: main thread (panel and configuration getters).
SessionInput CaptureSession(bool withSelection);

// Any thread: queues a focus request for the main thread. False (and nothing
// queued) when the path or name exceeds what the SDK accepts unchanged.
bool RequestFocus(int panel, const std::wstring& directory, const std::wstring& name, bool onlyIfShowing);
// Main thread (CMD_INTERNAL_FOCUS): applies the most recent request.
void ProcessFocusRequests();
// Any thread: asks the host to refresh panes showing 'path'.
void NotifyPathChanged(const std::wstring& path);
