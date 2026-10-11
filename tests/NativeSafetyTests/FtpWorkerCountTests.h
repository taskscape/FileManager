// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../../src/plugins/ftp/worker_count.h"
#include <stddef.h>

// Exercise the production snapshot in the same packing mode as operats.h;
// an unaligned interlocked value can fail despite otherwise correct lock order.
#pragma pack(push, 1)
struct CPackedFtpWorkerCountFixture
{
    char Prefix;
    CFTPWorkerCountSnapshot Count;
};
#pragma pack(pop)
static_assert(offsetof(CPackedFtpWorkerCountFixture, Count) % sizeof(LONG) == 0,
              "FTP worker count must remain aligned in a packed owner");
static_assert(alignof(CPackedFtpWorkerCountFixture) >= sizeof(LONG),
              "Packed FTP owners must preserve interlocked alignment");

int TestFtpWorkerCountSnapshot()
{
    CPackedFtpWorkerCountFixture fixture{};
    if (fixture.Count.Get() != 0)
        return Fail("FTP worker count did not start empty");

    CRITICAL_SECTION listLock, workerLock;
    InitializeCriticalSection(&listLock);
    InitializeCriticalSection(&workerLock);
    EnterCriticalSection(&listLock);
    fixture.Count.Publish(3);

    // Force the UI/list lock to stay held while a worker owns its lock and
    // samples the production count. The old list-locking read could not finish.
    std::promise<void> workerEntered;
    auto entered = workerEntered.get_future();
    auto sample = std::async(std::launch::async, [&]() {
        EnterCriticalSection(&workerLock);
        workerEntered.set_value();
        const int count = fixture.Count.Get();
        LeaveCriticalSection(&workerLock);
        return count;
    });
    const bool started = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    const bool finished = started && sample.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    // Always release the simulated UI lock before joining, including failure.
    LeaveCriticalSection(&listLock);
    const int sampled = sample.get();
    DeleteCriticalSection(&workerLock);
    DeleteCriticalSection(&listLock);
    if (!finished || sampled != 3)
        return Fail("FTP worker count could not be read while the UI held the list lock");

    // Removal batches and failed additions publish the actual list size, even
    // when that size is unchanged or reaches zero.
    fixture.Count.Publish(2);
    if (fixture.Count.Get() != 2)
        return Fail("FTP worker removal count was stale");
    fixture.Count.Publish(2);
    fixture.Count.Publish(0);
    return fixture.Count.Get() == 0 ? 0 : Fail("FTP shutdown count did not reach zero");
}
