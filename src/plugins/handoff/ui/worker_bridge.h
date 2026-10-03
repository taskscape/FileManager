// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Worker threads for scans, builds, and verification (handoff-spec.md C.3.3
// step 4). Workers never touch windows: they post heap payloads that the
// receiving window deletes, and a payload whose window is gone is deleted by
// the worker. Cancellation combines the window's request with the queue's
// stop event, so closing a window or unloading the plug-in both stop work.

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "build.h"

const UINT WM_HO_PROGRESS = WM_APP + 1; // lParam: ProgressMessage*
const UINT WM_HO_DONE = WM_APP + 2;     // wParam: job id, lParam: job-specific payload (may be null after a failure)
const UINT WM_HO_RETRY = WM_APP + 3;    // lParam: RetryMessage*

class WorkerContext;

class WorkerControl
{
public:
    explicit WorkerControl(HWND target);
    ~WorkerControl();
    WorkerControl(const WorkerControl&) = delete;
    WorkerControl& operator=(const WorkerControl&) = delete;

    void RequestCancel();
    bool CancelRequested() const { return Cancelled; }
    HANDLE CancelEvent() const { return Event; }
    // Called by the window when it is destroyed; later posts fail and cancel.
    void Detach();
    bool Post(UINT message, WPARAM wParam, LPARAM lParam);
    bool Running() const { return IsRunning; }

private:
    friend DWORD WINAPI WorkerMain(void* parameter, HANDLE stopEvent);
    friend bool StartWorker(const std::shared_ptr<WorkerControl>& control, WPARAM jobId,
                            std::function<void(WorkerContext&)> body);
    std::atomic<bool> Cancelled{false};
    std::atomic<bool> IsRunning{false};
    std::atomic<HWND> Target;
    HANDLE Event;
};

class WorkerContext
{
public:
    WorkerContext(std::shared_ptr<WorkerControl> control, HANDLE stopEvent) : Control(std::move(control)), StopEvent(stopEvent) {}
    bool Stopped() const;
    WorkerControl& Ctl() { return *Control; }
    HANDLE Stop() const { return StopEvent; }
    // Posts 'payload' (owned by the message on success, deleted here on failure).
    template <class T> void Deliver(UINT message, WPARAM wParam, T* payload)
    {
        if (!Control->Post(message, wParam, (LPARAM)payload))
            delete payload;
    }

private:
    std::shared_ptr<WorkerControl> Control;
    HANDLE StopEvent;
};

// Starts 'body' on a ThreadQueue worker in the multithreaded apartment (WIC,
// Windows.Data.Pdf). When 'body' throws, WM_HO_DONE is posted with a null payload.
bool StartWorker(const std::shared_ptr<WorkerControl>& control, WPARAM jobId, std::function<void(WorkerContext&)> body);

// Requests cancellation of every running worker (plug-in unload).
void CancelAllWorkers();

struct ProgressMessage
{
    handoff::Phase Phase = handoff::Phase::Scan;
    uint64_t Done = 0, Total = 0;
    std::wstring Item;
};

// Shared by the waiting worker and the answering window, so a late answer
// after cancellation never signals a closed handle.
struct RetryState
{
    std::atomic<int> Answer{0}; // 0 pending, 1 retry, 2 cancel
    HANDLE Answered;
    RetryState();
    ~RetryState();
    RetryState(const RetryState&) = delete;
    RetryState& operator=(const RetryState&) = delete;
};

struct RetryMessage
{
    std::wstring Item;
    DWORD Error = 0;
    std::shared_ptr<RetryState> State;
    void Reply(bool retry);
};

// IProgress/IBuildCallbacks for workers: throttled progress (at most ten
// updates per second) and retry prompts answered on the window's thread.
class WorkerProgress : public handoff::IBuildCallbacks
{
public:
    explicit WorkerProgress(WorkerContext& context) : Context(context) {}
    void Report(handoff::Phase phase, uint64_t done, uint64_t total, const std::wstring& item) override;
    bool StopRequested() override { return Context.Stopped(); }
    bool AskRetry(const std::wstring& item, DWORD error) override;

private:
    WorkerContext& Context;
    ULONGLONG LastPost = 0;
    handoff::Phase LastPhase = handoff::Phase::Scan;
    bool Posted = false;
};
