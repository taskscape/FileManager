// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <windows.h>
#include <crtdbg.h>
#include <strsafe.h>

#define __MODUL_MESSAGES_CPP

#ifndef MESSAGES_DISABLE

// The order here is important.
// Section names must be 8 characters or less.
// The sections with the same name before the $
// are merged into one section. The order that
// they are merged is determined by sorting
// the characters after the $.
// i_messages and i_messages_end are used to set
// boundaries so we can find the real functions
// that we need to call for initialization.

#pragma warning(disable : 4075) // we want to define module initialization order

typedef void(__cdecl* _PVFV)(void);

#pragma section(".i_msg$a", read)
__declspec(allocate(".i_msg$a")) const _PVFV i_messages = (_PVFV)1; // at the beginning of section .i_msg we place variable i_messages

#pragma section(".i_msg$z", read)
__declspec(allocate(".i_msg$z")) const _PVFV i_messages_end = (_PVFV)1; // and at the end of section .i_msg we place variable i_messages_end

void Initialize__Messages()
{
    const _PVFV* x = &i_messages;
    for (++x; x < &i_messages_end; ++x)
        if (*x != NULL)
            (*x)();
}

#pragma init_seg(".i_msg$m")

#include <ostream>
#include <stdio.h>
#ifdef _DEBUG
#include <sstream>
#endif // _DEBUG

#ifndef TRACE_ENABLE
#define __MESSAGES_STR2(x) #x
#define __MESSAGES_STR(x) __MESSAGES_STR2(x)
//#pragma message(__FILE__ "(" __MESSAGES_STR(__LINE__) "): Warning: macro TRACE_ENABLE not defined - unable to show errors")
#endif // TRACE_ENABLE

#if defined(_DEBUG) && defined(_MSC_VER) // without passing file+line to 'new' operator, list of memory leaks shows only 'crtdbg.h(552)'
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "trace.h"
#include "messages.h"
#include "thread_owner.h"

char __ResourceStringBuffer[__RESOURCE_STRING_BUFFER_SIZE] = "";
WCHAR __ResourceStringBufferW[__RESOURCE_STRING_BUFFER_SIZE] = L"";
char __SPrintFBuffer[__SPRINTF_BUFFER_SIZE] = "";
WCHAR __SPrintFBufferW[__SPRINTF_BUFFER_SIZE] = L"";
char __ErrorBuffer[__ERROR_BUFFER_SIZE] = "";
WCHAR __ErrorBufferW[__ERROR_BUFFER_SIZE] = L"";

const char* __MessagesTitle = "Message";
const WCHAR* __MessagesTitleW = L"Message";
HWND __MessagesParent = NULL;

char __MessagesTitleBuf[200];
WCHAR __MessagesTitleBufW[200];

#ifdef MULTITHREADED_MESSAGES_ENABLE

// critical section for entire module - monitor
CRITICAL_SECTION __MessagesCriticalSection;
// handle of current owning thread
DWORD __MessagesOwnerThreadID = 0;
// number of nested EnterMessagesModul calls (within current owning thread)
int __MessagesModulBlockCount = 0;

#ifdef MESSAGES_DEBUG

// call from thread that does not have access to module data and functions (did not acquire lock)
const char* __MessagesBadCall = "Incorrect call to function from modul MESSAGES.";

#endif // MESSAGES_DEBUG

#endif // MULTITHREADED_MESSAGES_ENABLE

C__Messages __Messages;
C__MessagesW __MessagesW;

#ifdef MULTITHREADED_MESSAGES_ENABLE

const char* __MessagesLowMemory = "Insufficient memory.";
const WCHAR* __MessagesLowMemoryW = L"Insufficient memory.";

//*****************************************************************************
//
// EnterMessagesModul
//

void EnterMessagesModul()
{
    EnterCriticalSection(&__MessagesCriticalSection);
    __MessagesOwnerThreadID = GetCurrentThreadId();
    __MessagesModulBlockCount++;
}

//*****************************************************************************
//
// LeaveMessagesModul
//

void LeaveMessagesModul()
{
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
        if (--__MessagesModulBlockCount == 0)
            __MessagesOwnerThreadID = 0;
        LeaveCriticalSection(&__MessagesCriticalSection);
    }
}

#endif // MULTITHREADED_MESSAGES_ENABLE

//*****************************************************************************
//
// C__Messages
//

C__Messages::C__Messages() : MessagesStrStream(&MessagesStringBuf)
{
#ifdef _DEBUG
    // new streams use locales internally, which have individual
    // "facets" implemented using lazy creation - they are allocated on heap
    // when needed, that is when someone sends something to stream that has
    // formatting dependent on locale rules, such as number, date,
    // or boolean. These "facets" are then deallocated on exit
    // of program with compiler priority, i.e. after our memory leak check.
    // So if someone uses stream to output anything localizable,
    // our debug heap will start reporting memory leaks, even though there are none. To
    // prevent this, we force locales to create all "facets" now, while
    // we are not yet monitoring heap.
    // For now we only use output stream and only with strings (without conversion)
    // and numbers. So sending a number to stringstream should suffice. If
    // in the future we start using streams more and debug heap starts reporting
    // leaks, we will have to add more input/output here.
    std::stringstream s;
    s << 1;
#endif // _DEBUG
#ifdef MULTITHREADED_MESSAGES_ENABLE
    InitializeCriticalSection(&__MessagesCriticalSection);
}

C__Messages::~C__Messages()
{
    DeleteCriticalSection(&__MessagesCriticalSection);
#endif // MULTITHREADED_MESSAGES_ENABLE
}

struct C__MessageBoxData
{
    const char* Text;
    const char* Caption;
    UINT Type;
    int Return;
};

int CALLBACK __MessagesMessageBoxThreadF(C__MessageBoxData* data)
{ // must not wait for response from calling thread, because it will not respond
    // therefore parent==NULL -> no window disabling etc.
    data->Return = MessageBoxA(NULL, data->Text, data->Caption, data->Type | MB_SETFOREGROUND);
    return 0;
}

static DWORD WINAPI MessagesMessageBoxOwnedThreadF(void* parameter, HANDLE stopEvent)
{
    // MessageBox has no cancellation API; ownership still keeps the caller's stack data valid until dismissal.
    UNREFERENCED_PARAMETER(stopEvent);
    return (DWORD)__MessagesMessageBoxThreadF((C__MessageBoxData*)parameter);
}

int C__Messages::MessageBoxT(const char* lpCaption, UINT uType)
{
    C__MessageBoxData data;
    data.Caption = lpCaption;
    data.Type = uType;
    data.Return = 0;

    MessagesStrStream.flush(); // flush to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    data.Text = MessagesStringBuf.c_str();
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    int len = (int)MessagesStringBuf.length() + 1;
    // The copied text is private to this call, so use the process heap instead of a movable-memory API.
    char* message = (char*)HeapAlloc(GetProcessHeap(), 0, len); // backup of text
    if (message != NULL)
    {
        memcpy((char*)message, MessagesStringBuf.c_str(), len); // it's FIXED -> HANDLE==PTR
        data.Text = message;
    }
    else
        data.Text = __MessagesLowMemory;
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads and message loops can start interfering
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    // Keep the stack-owned message data alive through a named owner rather than an unowned raw handle.
    CThreadOwner messageBoxOwner;
    if (messageBoxOwner.Start(MessagesMessageBoxOwnedThreadF, &data, "AnsiMessageBox"))
    {
        messageBoxOwner.StopAndJoin(INFINITE); // wait until user dismisses it
    }
    else
        TRACE_E("Unable to show MessageBox: " << data.Caption << ": " << data.Text);

#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (message != NULL)
        HeapFree(GetProcessHeap(), 0, message);
#endif // MULTITHREADED_MESSAGES_ENABLE

    return data.Return;
}

int C__Messages::MessageBox(HWND hWnd, const char* lpCaption, UINT uType)
{
    int ret;
    MessagesStrStream.flush(); // flush to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = ::MessageBoxA(hWnd, MessagesStringBuf.c_str(), lpCaption, uType);
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    size_t len = MessagesStringBuf.length() + 1;
    // The copied text is private to this call, so use the process heap instead of a movable-memory API.
    char* message = (char*)HeapAlloc(GetProcessHeap(), 0, len); // backup of text
    char* txt;
    txt = message;
    if (txt != NULL)
        memcpy(txt, MessagesStringBuf.c_str(), len);
    else
        txt = (char*)__MessagesLowMemory;
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads and message loops can start interfering

    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = ::MessageBoxA(hWnd, txt, lpCaption, uType);

    if (message != NULL)
        HeapFree(GetProcessHeap(), 0, message);
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    return ret;
}

//*****************************************************************************
//
// C__MessagesW
//

C__MessagesW::C__MessagesW() : MessagesStrStream(&MessagesStringBuf)
{
#ifdef _DEBUG
    // new streams use locales internally, which have individual
    // "facets" implemented using lazy creation - they are allocated on heap
    // when needed, that is when someone sends something to stream that has
    // formatting dependent on locale rules, such as number, date,
    // or boolean. These "facets" are then deallocated on exit
    // of program with compiler priority, i.e. after our memory leak check.
    // So if someone uses stream to output anything localizable,
    // our debug heap will start reporting memory leaks, even though there are none. To
    // prevent this, we force locales to create all "facets" now, while
    // we are not yet monitoring heap.
    // For now we only use output stream and only with strings (without conversion)
    // and numbers. So sending a number to stringstream should suffice. If
    // in the future we start using streams more and debug heap starts reporting
    // leaks, we will have to add more input/output here.
    std::wstringstream s;
    s << 1;
#endif // _DEBUG
}

struct C__MessageBoxDataW
{
    const WCHAR* Text;
    const WCHAR* Caption;
    UINT Type;
    int Return;
};

int CALLBACK __MessagesWMessageBoxThreadF(C__MessageBoxDataW* data)
{ // must not wait for response from calling thread, because it will not respond
    // therefore parent==NULL -> no window disabling etc.
    data->Return = MessageBoxW(NULL, data->Text, data->Caption, data->Type | MB_SETFOREGROUND);
    return 0;
}

static DWORD WINAPI MessagesWMessageBoxOwnedThreadF(void* parameter, HANDLE stopEvent)
{
    // MessageBoxW has no cancellation API; the owner protects its stack-backed input for the whole modal call.
    UNREFERENCED_PARAMETER(stopEvent);
    return (DWORD)__MessagesWMessageBoxThreadF((C__MessageBoxDataW*)parameter);
}

int C__MessagesW::MessageBoxT(const WCHAR* lpCaption, UINT uType)
{
    C__MessageBoxDataW data;
    data.Caption = lpCaption;
    data.Type = uType;
    data.Return = 0;

    MessagesStrStream.flush(); // flush to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    data.Text = MessagesStringBuf.c_str();
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    int len = (int)MessagesStringBuf.length() + 1;
    // The copied text is private to this call, so use the process heap instead of a movable-memory API.
    WCHAR* message = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * len); // backup of text
    if (message != NULL)
    {
        memcpy((WCHAR*)message, MessagesStringBuf.c_str(), sizeof(WCHAR) * len); // it's FIXED -> HANDLE==PTR
        data.Text = message;
    }
    else
        data.Text = __MessagesLowMemoryW;
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads and message loops can start interfering
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    // Keep the stack-owned Unicode message data alive through the standard worker owner.
    CThreadOwner messageBoxOwner;
    if (messageBoxOwner.Start(MessagesWMessageBoxOwnedThreadF, &data, "UnicodeMessageBox"))
    {
        messageBoxOwner.StopAndJoin(INFINITE); // wait until user dismisses it
    }
    else
        TRACE_EW(L"Unable to show MessageBox: " << data.Caption << L": " << data.Text);

#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (message != NULL)
        HeapFree(GetProcessHeap(), 0, message);
#endif // MULTITHREADED_MESSAGES_ENABLE

    return data.Return;
}

int C__MessagesW::MessageBox(HWND hWnd, const WCHAR* lpCaption, UINT uType)
{
    int ret;
    MessagesStrStream.flush(); // flush to buffer (in lpText)

#ifndef MULTITHREADED_MESSAGES_ENABLE
    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = ::MessageBoxW(hWnd, MessagesStringBuf.c_str(), lpCaption, uType);
    MessagesStringBuf.erase(); // preparation for next message
#else                          // MULTITHREADED_MESSAGES_ENABLE
    size_t len = MessagesStringBuf.length() + 1;
    // The copied text is private to this call, so use the process heap instead of a movable-memory API.
    WCHAR* message = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * len); // backup of text
    WCHAR* txt;
    txt = message;
    if (txt != NULL)
        memcpy(txt, MessagesStringBuf.c_str(), sizeof(WCHAR) * len);
    else
        txt = (WCHAR*)__MessagesLowMemoryW;
    MessagesStringBuf.erase(); // preparation for next message
    LeaveMessagesModul();      // now other threads and message loops can start interfering

    if (!IsWindow(hWnd))
        hWnd = NULL;
    ret = ::MessageBoxW(hWnd, txt, lpCaption, uType);

    if (message != NULL)
        HeapFree(GetProcessHeap(), 0, message);
#endif                         // MULTITHREADED_MESSAGES_ENABLE

    return ret;
}

//*****************************************************************************
//
// rsc
//

const char* rsc(int resID)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        if (LoadStringA(HInstance, resID, __ResourceStringBuffer,
                        __RESOURCE_STRING_BUFFER_SIZE) == 0)
        {
            TRACE_E("Unable to load string from resource (resource ID is " << resID << ")");
            __ResourceStringBuffer[0] = 0;
        }
        return __ResourceStringBuffer;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* rscW(int resID)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        if (LoadStringW(HInstance, resID, __ResourceStringBufferW,
                        __RESOURCE_STRING_BUFFER_SIZE) == 0)
        {
            TRACE_E("Unable to load string from resource (resource ID is " << resID << ")");
            __ResourceStringBufferW[0] = 0;
        }
        return __ResourceStringBufferW;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// spf
//

const char* spf(const char* formatString, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        va_list params;
        va_start(params, formatString);
        _vsnprintf_s(__SPrintFBuffer, _TRUNCATE, formatString, params);
        va_end(params);
        return __SPrintFBuffer;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* spfW(const WCHAR* formatString, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        va_list params;
        va_start(params, formatString);
        _vsnwprintf_s(__SPrintFBufferW, _TRUNCATE, formatString, params);
        va_end(params);
        return __SPrintFBufferW;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// spf
//

const char* spf(int formatStringResID, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        va_list params;
        va_start(params, formatStringResID);
        _vsnprintf_s(__SPrintFBuffer, _TRUNCATE, rsc(formatStringResID), params);
        va_end(params);
        return __SPrintFBuffer;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* spfW(int formatStringResID, ...)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        va_list params;
        va_start(params, formatStringResID);
        _vsnwprintf_s(__SPrintFBufferW, _TRUNCATE, rscW(formatStringResID), params);
        va_end(params);
        return __SPrintFBufferW;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// err
//

const char* err(DWORD error)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        // Keep the error prefix bounded before appending the system message.
        _snprintf_s(__ErrorBuffer, _countof(__ErrorBuffer), _TRUNCATE, "(%d) ", error);
        int len = (int)strlen(__ErrorBuffer);
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL,
                       error,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       __ErrorBuffer + len,
                       __ERROR_BUFFER_SIZE - len,
                       NULL);
        return __ErrorBuffer;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

const WCHAR* errW(DWORD error)
{
#ifdef MULTITHREADED_MESSAGES_ENABLE
    if (__MessagesModulBlockCount > 0 &&
        __MessagesOwnerThreadID == GetCurrentThreadId())
    {
#endif // MULTITHREADED_MESSAGES_ENABLE
        // Keep the error prefix bounded before appending the system message.
        _snwprintf_s(__ErrorBufferW, _countof(__ErrorBufferW), _TRUNCATE, L"(%d) ", error);
        int len = (int)wcslen(__ErrorBufferW);
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
                       NULL,
                       error,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       __ErrorBufferW + len,
                       __ERROR_BUFFER_SIZE - len,
                       NULL);
        return __ErrorBufferW;
#ifdef MULTITHREADED_MESSAGES_ENABLE
    }
    else
    {
#ifdef MESSAGES_DEBUG
        TRACE_E(__MessagesBadCall);
#endif // MESSAGES_DEBUG
        return NULL;
    }
#endif // MULTITHREADED_MESSAGES_ENABLE
}

//*****************************************************************************
//
// SetMessagesTitle
//

void SetMessagesTitle(const char* title)
{
    // Message titles are bounded presentation fields; retain their explicit clip before conversion.
    StringCchCopyNA(__MessagesTitleBuf, _countof(__MessagesTitleBuf), title, _countof(__MessagesTitleBuf) - 1);
    __MessagesTitle = __MessagesTitleBuf;
    MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, __MessagesTitleBuf, -1,
                        __MessagesTitleBufW, _countof(__MessagesTitleBufW));
    __MessagesTitleBufW[_countof(__MessagesTitleBufW) - 1] = 0;
    __MessagesTitleW = __MessagesTitleBufW;
}

void SetMessagesTitleW(const WCHAR* title)
{
    // Keep the Unicode title within the shared presentation buffer before ANSI conversion.
    StringCchCopyNW(__MessagesTitleBufW, _countof(__MessagesTitleBufW), title, _countof(__MessagesTitleBufW) - 1);
    __MessagesTitleW = __MessagesTitleBufW;
    WideCharToMultiByte(CP_ACP, 0, __MessagesTitleBufW, -1,
                        __MessagesTitleBuf, _countof(__MessagesTitleBuf), NULL, NULL);
    __MessagesTitleBuf[_countof(__MessagesTitleBuf) - 1] = 0;
    __MessagesTitle = __MessagesTitleBuf;
}

//*****************************************************************************
//
// SetMessagesParent
//

void SetMessagesParent(HWND parent)
{
    __MessagesParent = parent;
}

#endif // MESSAGES_DISABLE
