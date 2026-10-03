// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "worker_bridge.h"

namespace
{

std::mutex RegistryLock;
std::vector<std::weak_ptr<WorkerControl>> Registry;

void Register(const std::shared_ptr<WorkerControl>& control)
{
    std::lock_guard<std::mutex> lock(RegistryLock);
    Registry.erase(std::remove_if(Registry.begin(), Registry.end(),
                                  [](const std::weak_ptr<WorkerControl>& item) { return item.expired(); }),
                   Registry.end());
    Registry.push_back(control);
}

} // namespace

struct WorkerLaunch
{
    std::shared_ptr<WorkerControl> Control;
    WPARAM JobId = 0;
    std::function<void(WorkerContext&)> Body;
};

WorkerControl::WorkerControl(HWND target) : Target(target)
{
    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
}

WorkerControl::~WorkerControl()
{
    if (Event != NULL)
        CloseHandle(Event);
}

void WorkerControl::RequestCancel()
{
    Cancelled = true;
    if (Event != NULL)
        SetEvent(Event);
}

void WorkerControl::Detach()
{
    Target = NULL;
    RequestCancel();
}

bool WorkerControl::Post(UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND target = Target;
    return target != NULL && PostMessageW(target, message, wParam, lParam) != FALSE;
}

bool WorkerContext::Stopped() const
{
    return Control->CancelRequested() || (StopEvent != NULL && WaitForSingleObject(StopEvent, 0) == WAIT_OBJECT_0);
}

DWORD WINAPI WorkerMain(void* parameter, HANDLE stopEvent)
{
    std::unique_ptr<WorkerLaunch> launch((WorkerLaunch*)parameter);
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    WorkerContext context(launch->Control, stopEvent);
    bool completed = false;
    try
    {
        launch->Body(context);
        completed = true;
    }
    catch (...)
    {
        // The window must still leave its busy state; a null payload reports the failure.
        TRACE_E("Handoff worker ended by an exception");
    }
    if (!completed)
        launch->Control->Post(WM_HO_DONE, launch->JobId, 0);
    if (SUCCEEDED(hr))
        CoUninitialize();
    launch->Control->IsRunning = false;
    return 0;
}

bool StartWorker(const std::shared_ptr<WorkerControl>& control, WPARAM jobId, std::function<void(WorkerContext&)> body)
{
    if (control->Event == NULL)
        return false;
    WorkerLaunch* launch = new WorkerLaunch;
    launch->Control = control;
    launch->JobId = jobId;
    launch->Body = std::move(body);
    control->IsRunning = true;
    Register(control);
    if (ThreadQueue.StartThread(WorkerMain, launch) != NULL)
        return true;
    control->IsRunning = false;
    delete launch;
    return false;
}

void CancelAllWorkers()
{
    std::lock_guard<std::mutex> lock(RegistryLock);
    for (const std::weak_ptr<WorkerControl>& item : Registry)
    {
        std::shared_ptr<WorkerControl> control = item.lock();
        if (control)
            control->RequestCancel();
    }
}

RetryState::RetryState()
{
    Answered = CreateEventW(NULL, TRUE, FALSE, NULL);
}

RetryState::~RetryState()
{
    if (Answered != NULL)
        CloseHandle(Answered);
}

void RetryMessage::Reply(bool retry)
{
    State->Answer = retry ? 1 : 2;
    if (State->Answered != NULL)
        SetEvent(State->Answered);
}

void WorkerProgress::Report(handoff::Phase phase, uint64_t done, uint64_t total, const std::wstring& item)
{
    // At most ten updates per second, but phase changes and completion always get through.
    ULONGLONG now = GetTickCount64();
    bool final = total != 0 && done >= total;
    if (Posted && phase == LastPhase && !final && now - LastPost < 100)
        return;
    Posted = true;
    LastPost = now;
    LastPhase = phase;
    ProgressMessage* message = new ProgressMessage;
    message->Phase = phase;
    message->Done = done;
    message->Total = total;
    message->Item = item;
    Context.Deliver(WM_HO_PROGRESS, 0, message);
}

bool WorkerProgress::AskRetry(const std::wstring& item, DWORD error)
{
    if (Context.Stopped())
        return false;
    std::shared_ptr<RetryState> state = std::make_shared<RetryState>();
    if (state->Answered == NULL)
        return false;
    RetryMessage* message = new RetryMessage;
    message->Item = item;
    message->Error = error;
    message->State = state;
    if (!Context.Ctl().Post(WM_HO_RETRY, 0, (LPARAM)message))
    {
        delete message;
        return false;
    }
    // No lock is held while waiting; a closed window or an unload wakes the worker as "cancel".
    HANDLE handles[3] = {state->Answered, Context.Ctl().CancelEvent(), Context.Stop()};
    DWORD count = Context.Stop() != NULL ? 3 : 2;
    DWORD wait = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
    return wait == WAIT_OBJECT_0 && state->Answer == 1;
}
