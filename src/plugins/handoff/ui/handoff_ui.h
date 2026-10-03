// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Entry points from ExecuteMenuItem (main thread). Each one has already
// captured everything it needs from the panes; it starts a UI thread and
// returns immediately (handoff-spec.md C.3.3).

#include <string>

#include "session.h"

bool StartBuildSession(const SessionInput& input);
bool StartVerify(const SessionInput& input);
bool StartValidate(const SessionInput& input);
bool StartNewSpec(const SessionInput& input);

// Modal configuration dialog on the calling (main) thread.
void RunConfigurationDialog(HWND parent);

// Thread bodies, run on the plug-in UI threads.
void RunBuildSessionThread(const SessionInput& input);
void RunVerifyThread(const SessionInput& input);

// Modal helpers shared by several windows (UI thread).
void RunValidateDialog(HWND owner, const std::wstring& path, int focusPanel, bool alwaysOnTop);
// Returns the reason when the reviewer accepted the override.
bool RunOverrideDialog(HWND owner, const std::wstring& findingText, std::wstring& reason);
