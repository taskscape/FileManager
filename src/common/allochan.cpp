// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// Keep the emergency callback declarations synchronized with this allocator implementation.
#include "allochan.h"

#include <windows.h>
#include <ostream>
#include <tchar.h>
#include <new.h>

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"

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
volatile PVOID AllocEmergencyRecoveryCallback = NULL;
volatile PVOID AllocEmergencyExitWindow = NULL;
volatile LONG AllocEmergencyExitMessage = 0;

void NotifyAllocationEmergency()
{
    TAllocEmergencyRecoveryCallback callback = (TAllocEmergencyRecoveryCallback)InterlockedCompareExchangePointer(
        (PVOID volatile*)&AllocEmergencyRecoveryCallback, NULL, NULL);
    if (callback != NULL)
        callback();

    HWND window = (HWND)InterlockedCompareExchangePointer((PVOID volatile*)&AllocEmergencyExitWindow, NULL, NULL);
    UINT message = (UINT)InterlockedCompareExchange(&AllocEmergencyExitMessage, 0, 0);
    if (window != NULL && message != 0)
        PostMessage(window, message, 0, 0);
}

void ActivateAllocationEmergency()
{
    if (InterlockedCompareExchange(&AllocEmergencyActive, TRUE, FALSE) != FALSE)
        return;

    // Free the precommitted reserve before touching recovery code because the
    // failing allocation may otherwise leave no memory for its last-safe state.
    PVOID reserve = InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyReserve, NULL);
    if (reserve != NULL)
        HeapFree(GetProcessHeap(), 0, reserve);
    NotifyAllocationEmergency();
}

class C__AllocHandlerInit
{
public:
    static int TaskscapeLtdNewHandler(size_t size);

    C__AllocHandlerInit()
    {
        // Allocate outside the CRT allocator so its failure cannot recurse into this handler.
        AllocEmergencyReserve = HeapAlloc(GetProcessHeap(), 0, AllocEmergencyReserveSize);
        InitializeCriticalSection(&CriticalSection);
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
        DeleteCriticalSection(&CriticalSection);
    }

private:
    CRITICAL_SECTION CriticalSection;
    _PNH OldNewHandler;
    int OldNewMode;
} __AllocHandlerInit;

TCHAR __AllocHandlerMessage[500] = _T("Insufficient memory to allocate %Iu bytes. Try to release some memory (e.g. ")
                                   _T("close some running application) and click Retry. If it does not help, you can ")
                                   _T("click Ignore to pass memory allocation error to this application or click Abort ")
                                   _T("to terminate this application.");
TCHAR __AllocHandlerTitle[200] = _T("Error");
TCHAR __AllocHandlerWarningIgnore[500] = _T("Do you really want to pass memory allocation error to this application?\n\n")
                                         _T("WARNING: Application may crash and then all unsaved data will be lost!\n")
                                         _T("HINT: We recommend to risk this action only if the application is trying to ")
                                         _T("allocate extra large block of memory (i.e. more than 500 MB).");
TCHAR __AllocHandlerWarningAbort[200] = _T("Do you really want to terminate this application?\n\nWARNING: All unsaved data will be lost!");

void SetAllocHandlerMessage(const TCHAR* message, const TCHAR* title, const TCHAR* warningIgnore, const TCHAR* warningAbort)
{
    if (message != NULL)
        lstrcpyn(__AllocHandlerMessage, message, 500);
    if (title != NULL)
        lstrcpyn(__AllocHandlerTitle, title, 200);
    if (warningIgnore != NULL)
        lstrcpyn(__AllocHandlerWarningIgnore, warningIgnore, 500);
    if (warningAbort != NULL)
        lstrcpyn(__AllocHandlerWarningAbort, warningAbort, 200);
}

void SetAllocEmergencyRecoveryCallback(TAllocEmergencyRecoveryCallback callback)
{
    // Late registration is required during startup: an earlier OOM must still
    // obtain its minimal recovery record once the operation subsystem is ready.
    InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyRecoveryCallback, (PVOID)callback);
    if (callback != NULL && IsAllocationEmergencyActive())
        callback();
}

void SetAllocEmergencyExitWindow(HWND window, UINT message)
{
    // Do not lose an OOM raised before the main HWND existed; notify it on registration.
    InterlockedExchange(&AllocEmergencyExitMessage, (LONG)message);
    InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyExitWindow, window);
    if (window != NULL && message != 0 && IsAllocationEmergencyActive())
        PostMessage(window, message, 0, 0);
}

BOOL IsAllocationEmergencyActive()
{
    return InterlockedCompareExchange(&AllocEmergencyActive, FALSE, FALSE) != FALSE;
}

int C__AllocHandlerInit::TaskscapeLtdNewHandler(size_t size)
{
    ActivateAllocationEmergency();
    TRACE_ET(_T("TaskscapeLtdNewHandler: not enough memory to allocate ") << size << _T(" bytes!"));
    int ret = 1;
    int ti = GetTickCount();
    EnterCriticalSection(&__AllocHandlerInit.CriticalSection);
    if (GetTickCount() - ti <= 500) // we will show message-box only if we didn't force the user to solve the same problem in another thread a moment ago
    {
        TCHAR buf[550];
        _sntprintf_s(buf, _countof(buf) - 1, __AllocHandlerMessage, size);
        int res;
        do
        {
            res = MessageBox(NULL, buf, __AllocHandlerTitle, MB_ICONERROR | MB_TASKMODAL | MB_ABORTRETRYIGNORE | MB_DEFBUTTON2);
            if (res == 0)
            {
                TRACE_ET(_T("TaskscapeLtdNewHandler: unable to open message-box!"));
                Sleep(1000); // let the machine rest and try to show msgbox again
            }
        } while (res == 0);
        if (res == IDABORT) // terminate
        {
            do
            {
                res = MessageBox(NULL, __AllocHandlerWarningAbort, __AllocHandlerTitle, MB_ICONQUESTION | MB_TASKMODAL | MB_YESNO | MB_DEFBUTTON2);
                if (res == 0)
                {
                    TRACE_ET(_T("TaskscapeLtdNewHandler: unable to open message-box with abort-warning!"));
                    Sleep(1000); // let the machine rest and try to show msgbox again
                }
            } while (res == 0);
            if (res == IDYES)
                TerminateProcess(GetCurrentProcess(), 777); // harder exit (ExitProcess still calls something)
        }
        else
        {
            if (res == IDIGNORE) // user wants to pass allocation problem to application (return NULL), places where large blocks are allocated should be protected (otherwise it will crash on NULL access)
            {
                do
                {
                    res = MessageBox(NULL, __AllocHandlerWarningIgnore, __AllocHandlerTitle, MB_ICONQUESTION | MB_TASKMODAL | MB_YESNO | MB_DEFBUTTON2);
                    if (res == 0)
                    {
                        TRACE_ET(_T("TaskscapeLtdNewHandler: unable to open message-box with ignore-warning!"));
                        Sleep(1000); // let the machine rest and try to show msgbox again
                    }
                } while (res == 0);
                if (res == IDYES)
                    ret = 0; // returning NULL to application
            }
        }
    }
    LeaveCriticalSection(&__AllocHandlerInit.CriticalSection);
    return ret; // retry or NULL
}

#endif // ALLOCHAN_DISABLE

