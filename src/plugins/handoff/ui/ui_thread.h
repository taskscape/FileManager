// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Plug-in UI threads (handoff-spec.md C.3.3): every command runs its windows
// on a CThreadQueue thread with its own message loop, so the main window
// stays usable and unloading can close and join everything.

#include <functional>
#include <string>

// Window kinds that may exist at most once; a second request activates the
// existing window instead (two build sessions must never coexist).
enum class UiSlot
{
    Build,
    Verify,
    Validate,
    NewSpec,
    Count
};

// Main thread: true when the slot was free and is now reserved; otherwise the
// existing window (if already created) is brought to the foreground.
bool ClaimSlot(UiSlot slot);
void SetSlotWindow(UiSlot slot, HWND window);
void ReleaseSlot(UiSlot slot);

// Runs 'body' on a new single-threaded-apartment UI thread in ThreadQueue;
// the slot is released when the thread ends. False when the thread could not start.
bool StartUiThread(const char* name, UiSlot slot, std::function<void()> body);

// Pumps messages until the last modeless window of this thread is destroyed.
void RunModelessLoop();
void RegisterThreadDialog(HWND window, bool modeless);
void UnregisterThreadDialog(HWND window, bool modeless);

// Set while the plug-in unloads; prompts return without showing UI.
bool ShuttingDown();
void SetShuttingDown(bool value);

// Builds are tracked so a non-forced unload can be refused while one runs.
bool IsBuildRunning();
void SetBuildRunning(bool running);

// A message box that unloading can dismiss: while it is open, a thread timer
// ends it (returning IDCANCEL) as soon as the plug-in starts shutting down.
int PromptBox(HWND owner, const std::wstring& text, UINT flags);

// Wraps other nested modal UI (common file dialogs) with the same dismissal timer.
class PromptScope
{
public:
    PromptScope();
    ~PromptScope();
    PromptScope(const PromptScope&) = delete;
    PromptScope& operator=(const PromptScope&) = delete;

private:
    UINT_PTR Timer;
};
