// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "allochan.h"

#include <windows.h>
#include <new.h>

#ifndef ALLOCHAN_DISABLE

#ifndef SAFE_ALLOC
#pragma message(__FILE__ "(" __TRACE_STR(__LINE__) "): Maybe Error: Macro SAFE_ALLOC is not defined! You should define this macro when using allochan.* module to optimize other modules.")
#endif // SAFE_ALLOC

// The order here is important.
// Section names must be 8 characters or less.
// The sections with the same name before the $
// are merged into one section. The order that
// they are merged is determined by sorting
// the characters after the $.
// i_allochan and i_allochan_end are used to set
// boundaries so we can find the real functions
// that we need to call for initialization.

#pragma warning(disable : 4075) // we want to define module initialization order

typedef void(__cdecl* _PVFV)(void);

#pragma section(".i_alc$a", read)
__declspec(allocate(".i_alc$a")) const _PVFV i_allochan = (_PVFV)1; // at the beginning of .i_alc section we place variable i_allochan

#pragma section(".i_alc$z", read)
__declspec(allocate(".i_alc$z")) const _PVFV i_allochan_end = (_PVFV)1; // and at the end of .i_alc section we place variable i_allochan_end

void Initialize__Allochan()
{
    const _PVFV* x = &i_allochan;
    for (++x; x < &i_allochan_end; ++x)
        if (*x != NULL)
            (*x)();
}

#pragma init_seg(".i_alc$m")

const SIZE_T AllocEmergencyReserveSize = 64 * 1024;
volatile PVOID AllocEmergencyReserve = NULL;
volatile LONG AllocEmergencyActive = FALSE;
volatile PVOID AllocEmergencyNotificationWindow = NULL;
volatile LONG AllocEmergencyNotificationMessage = 0;

void NotifyAllocationEmergency()
{
    HWND window = (HWND)InterlockedCompareExchangePointer((PVOID volatile*)&AllocEmergencyNotificationWindow, NULL, NULL);
    UINT message = (UINT)InterlockedCompareExchange(&AllocEmergencyNotificationMessage, 0, 0);
    if (window != NULL && message != 0)
        PostMessage(window, message, 0, 0);
}

void ActivateAllocationEmergency()
{
    if (InterlockedCompareExchange(&AllocEmergencyActive, TRUE, FALSE) != FALSE)
        return;

    // The only recovery performed on the failing thread is releasing the
    // precommitted reserve; all stateful work is deferred to the UI message.
    PVOID reserve = InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyReserve, NULL);
    if (reserve != NULL)
        HeapFree(GetProcessHeap(), 0, reserve);
    NotifyAllocationEmergency();
}

// Installs the CRT new-handler and an emergency heap reserve used when allocations fail.
class C__AllocHandlerInit
{
public:
    static int TaskscapeLtdNewHandler(size_t size);

    C__AllocHandlerInit()
    {
        // Allocate outside the CRT allocator so its failure cannot recurse into this handler.
        AllocEmergencyReserve = HeapAlloc(GetProcessHeap(), 0, AllocEmergencyReserveSize);
        OldNewHandler = _set_new_handler(TaskscapeLtdNewHandler); // operator new should call our new-handler on insufficient memory
        OldNewMode = _set_new_mode(1);                     // malloc should call our new-handler on insufficient memory
    }
    ~C__AllocHandlerInit()
    {
        _set_new_mode(OldNewMode);
        _set_new_handler(OldNewHandler);
        PVOID reserve = InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyReserve, NULL);
        if (reserve != NULL)
            HeapFree(GetProcessHeap(), 0, reserve);
    }

private:
    _PNH OldNewHandler;
    int OldNewMode;
} __AllocHandlerInit;

void SetAllocEmergencyNotificationWindow(HWND window, UINT message)
{
    // A notification registered after an early OOM is posted once the UI can
    // safely persist recovery state and begin the controlled shutdown.
    InterlockedExchange(&AllocEmergencyNotificationMessage, (LONG)message);
    InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyNotificationWindow, window);
    if (window != NULL && message != 0 && IsAllocationEmergencyActive())
        PostMessage(window, message, 0, 0);
}

BOOL IsAllocationEmergencyActive()
{
    return InterlockedCompareExchange(&AllocEmergencyActive, FALSE, FALSE) != FALSE;
}

int C__AllocHandlerInit::TaskscapeLtdNewHandler(size_t)
{
    ActivateAllocationEmergency();
    // Returning zero propagates the failure immediately instead of retrying
    // while the allocator's caller may still own unrelated subsystem locks.
    return 0;
}

#endif // ALLOCHAN_DISABLE

