// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

// Size values commonly originate at file, IPC, or network boundaries.  Keep
// the overflow test adjacent to the conversion so an invalid value cannot
// wrap into a smaller allocation or Win32 I/O request.
inline BOOL CheckedAddUInt64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (result == NULL || left > UINT64_MAX - right)
        return FALSE;
    *result = left + right;
    return TRUE;
}

inline BOOL CheckedMultiplyUInt64(uint64_t left, uint64_t right, uint64_t* result)
{
    if (result == NULL || (right != 0 && left > UINT64_MAX / right))
        return FALSE;
    *result = left * right;
    return TRUE;
}

inline BOOL CheckedAddSize(size_t left, size_t right, size_t* result)
{
    if (result == NULL || left > (size_t)-1 - right)
        return FALSE;
    *result = left + right;
    return TRUE;
}

inline BOOL CheckedMultiplySize(size_t left, size_t right, size_t* result)
{
    if (result == NULL || (right != 0 && left > (size_t)-1 / right))
        return FALSE;
    *result = left * right;
    return TRUE;
}

inline BOOL CheckedCastUInt64ToSize(uint64_t value, size_t* result)
{
    if (result == NULL || value > (size_t)-1)
        return FALSE;
    *result = (size_t)value;
    return TRUE;
}

inline BOOL CheckedCastDwordToSize(DWORD value, size_t* result)
{
    if (result == NULL)
        return FALSE;
    *result = (size_t)value;
    return TRUE;
}

inline BOOL CheckedCastUInt64ToDword(uint64_t value, DWORD* result)
{
    if (result == NULL || value > MAXDWORD)
        return FALSE;
    *result = (DWORD)value;
    return TRUE;
}

inline BOOL CheckedCastSizeToDword(size_t value, DWORD* result)
{
    if (result == NULL || value > MAXDWORD)
        return FALSE;
    *result = (DWORD)value;
    return TRUE;
}

inline BOOL CheckedAddDword(DWORD left, DWORD right, DWORD* result)
{
    uint64_t sum;
    return CheckedAddUInt64(left, right, &sum) && CheckedCastUInt64ToDword(sum, result);
}

inline BOOL CheckedMultiplyDword(DWORD left, DWORD right, DWORD* result)
{
    uint64_t product;
    return CheckedMultiplyUInt64(left, right, &product) && CheckedCastUInt64ToDword(product, result);
}

inline BOOL CheckedCastSizeToInt(size_t value, int* result)
{
    if (result == NULL || value > INT_MAX)
        return FALSE;
    *result = (int)value;
    return TRUE;
}
