// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "handles.h"

// Keeps the existing debug handle accounting while making local kernel-handle
// ownership survive every return path in newly touched code.
class CScopedKernelHandle
{
public:
    CScopedKernelHandle() : Handle(INVALID_HANDLE_VALUE)
    {
    }

    explicit CScopedKernelHandle(HANDLE handle) : Handle(handle)
    {
    }

    ~CScopedKernelHandle()
    {
        // Destruction must not replace the failure code a caller is returning.
        const DWORD error = GetLastError();
        Close(NULL);
        SetLastError(error);
    }

private:
    CScopedKernelHandle(const CScopedKernelHandle&);
    CScopedKernelHandle& operator=(const CScopedKernelHandle&);

public:
    BOOL IsValid() const
    {
        return Handle != INVALID_HANDLE_VALUE && Handle != NULL;
    }

    HANDLE Get() const
    {
        return Handle;
    }

    // Reset makes replacement ownership explicit and closes any prior owner.
    void Reset(HANDLE handle = INVALID_HANDLE_VALUE)
    {
        Close(NULL);
        Handle = handle;
    }

    // Release is the only way to pass ownership back to a legacy raw-HANDLE API.
    HANDLE Release()
    {
        HANDLE handle = Handle;
        Handle = INVALID_HANDLE_VALUE;
        return handle;
    }

    // Some legacy result contracts report a close failure; callers can preserve
    // that behaviour without reinstating a raw cleanup ladder.
    BOOL Close(DWORD* error)
    {
        if (!IsValid())
            return TRUE;

        HANDLE handle = Handle;
        Handle = INVALID_HANDLE_VALUE;
        if (!HANDLES(CloseHandle(handle)))
        {
            if (error != NULL)
                *error = GetLastError();
            return FALSE;
        }
        return TRUE;
    }

private:
    HANDLE Handle;
};

// Find-first handles must be released with FindClose rather than CloseHandle;
// this wrapper keeps the debug handle accounting while scoping that ownership.
class CScopedFindHandle
{
public:
    CScopedFindHandle() : Handle(INVALID_HANDLE_VALUE)
    {
    }

    explicit CScopedFindHandle(HANDLE handle) : Handle(handle)
    {
    }

    ~CScopedFindHandle()
    {
        // Destruction must not replace the failure code a caller is returning.
        const DWORD error = GetLastError();
        Close();
        SetLastError(error);
    }

    CScopedFindHandle(const CScopedFindHandle&);
    CScopedFindHandle& operator=(const CScopedFindHandle&);

    BOOL IsValid() const
    {
        return Handle != INVALID_HANDLE_VALUE && Handle != NULL;
    }

    HANDLE Get() const
    {
        return Handle;
    }

    void Close()
    {
        if (IsValid())
        {
            HANDLES(FindClose(Handle));
            Handle = INVALID_HANDLE_VALUE;
        }
    }

private:
    HANDLE Handle;
};
