// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "sdk_strings.h"
#include "ui_thread.h"

namespace
{

struct SlotState
{
    bool Claimed = false;
    HWND Window = NULL;
};

std::mutex SlotLock;
SlotState Slots[(int)UiSlot::Count];
std::atomic<bool> ShuttingDownFlag{false};
std::atomic<int> BuildsRunning{0};

// Dialogs of the current UI thread: the modeless count ends the message loop,
// and the set lets IsDialogMessage and the prompt timer tell plug-in dialogs
// from message boxes and common dialogs.
thread_local std::set<HWND>* ThreadDialogs = nullptr;
thread_local int ThreadModelessCount = 0;

bool IsThreadDialog(HWND window)
{
    return ThreadDialogs != nullptr && ThreadDialogs->count(window) != 0;
}

BOOL CALLBACK EndForeignDialog(HWND window, LPARAM)
{
    wchar_t className[16] = {};
    if (GetClassNameW(window, className, (int)_countof(className)) > 0 && wcscmp(className, L"#32770") == 0 &&
        !IsThreadDialog(window) && IsWindowEnabled(window))
        EndDialog(window, IDCANCEL);
    return TRUE;
}

VOID CALLBACK PromptTimerProc(HWND, UINT, UINT_PTR, DWORD)
{
    // Runs inside the nested loop of the message box or file dialog; plug-in
    // dialogs receive WM_CLOSE from ModelessQueue instead.
    if (ShuttingDownFlag)
        EnumThreadWindows(GetCurrentThreadId(), EndForeignDialog, 0);
}

class UiThread : public CThread
{
public:
    UiThread(const char* name, UiSlot slot, std::function<void()> work)
        : CThread(name), Slot(slot), Work(std::move(work))
    {
    }

    unsigned Body() override
    {
        CALL_STACK_MESSAGE1("Handoff UiThread::Body()");
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        std::set<HWND> dialogs;
        ThreadDialogs = &dialogs;
        try
        {
            Work();
        }
        catch (...)
        {
            // An allocation failure ends this session's windows; the host keeps running.
            TRACE_E("Handoff UI thread ended by an exception");
        }
        ThreadDialogs = nullptr;
        if (SUCCEEDED(hr))
            CoUninitialize();
        ReleaseSlot(Slot);
        return 0;
    }

private:
    UiSlot Slot;
    std::function<void()> Work;
};

} // namespace

bool ClaimSlot(UiSlot slot)
{
    HWND existing = NULL;
    {
        std::lock_guard<std::mutex> lock(SlotLock);
        SlotState& state = Slots[(int)slot];
        if (!state.Claimed)
        {
            state.Claimed = true;
            state.Window = NULL;
            return true;
        }
        existing = state.Window;
    }
    if (existing != NULL && IsWindow(existing))
    {
        // Asynchronous so the main thread never waits for a busy UI thread.
        if (IsIconic(existing))
            ShowWindowAsync(existing, SW_RESTORE);
        SetForegroundWindow(existing);
    }
    return false;
}

void SetSlotWindow(UiSlot slot, HWND window)
{
    std::lock_guard<std::mutex> lock(SlotLock);
    Slots[(int)slot].Window = window;
}

void ReleaseSlot(UiSlot slot)
{
    std::lock_guard<std::mutex> lock(SlotLock);
    Slots[(int)slot] = SlotState();
}

bool StartUiThread(const char* name, UiSlot slot, std::function<void()> body)
{
    UiThread* thread = new UiThread(name, slot, std::move(body));
    if (thread->Create(ThreadQueue) != NULL)
        return true; // the thread object deletes itself when the thread ends
    delete thread;
    ReleaseSlot(slot);
    return false;
}

void RegisterThreadDialog(HWND window, bool modeless)
{
    if (ThreadDialogs != nullptr)
        ThreadDialogs->insert(window);
    if (modeless)
        ThreadModelessCount++;
}

void UnregisterThreadDialog(HWND window, bool modeless)
{
    if (ThreadDialogs != nullptr)
        ThreadDialogs->erase(window);
    if (modeless && ThreadModelessCount > 0)
        ThreadModelessCount--;
}

void RunModelessLoop()
{
    MSG msg;
    while (ThreadModelessCount > 0)
    {
        BOOL result = GetMessageW(&msg, NULL, 0, 0);
        if (result == 0 || result == -1)
            break;
        HWND root = msg.hwnd != NULL ? GetAncestor(msg.hwnd, GA_ROOT) : NULL;
        if (root != NULL && IsThreadDialog(root) && IsDialogMessageW(root, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool ShuttingDown()
{
    return ShuttingDownFlag;
}

void SetShuttingDown(bool value)
{
    ShuttingDownFlag = value;
}

bool IsBuildRunning()
{
    return BuildsRunning > 0;
}

void SetBuildRunning(bool running)
{
    if (running)
        BuildsRunning++;
    else if (BuildsRunning > 0)
        BuildsRunning--;
}

PromptScope::PromptScope()
{
    Timer = SetTimer(NULL, 0, 200, PromptTimerProc);
}

PromptScope::~PromptScope()
{
    if (Timer != 0)
        KillTimer(NULL, Timer);
}

int PromptBox(HWND owner, const std::wstring& text, UINT flags)
{
    if (ShuttingDown())
        return IDCANCEL;
    PromptScope scope;
    return MessageBoxW(owner, text.c_str(), Text(IDS_TITLE).c_str(), flags);
}
