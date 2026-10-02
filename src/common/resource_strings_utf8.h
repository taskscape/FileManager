// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Resource strings are UTF-16; application char strings must remain UTF-8 regardless of the host ANSI code page.
// A null destination queries the required byte count without the terminator; failed copies leave an empty string.
inline int LoadStringUtf8(HINSTANCE instance, int resourceId, char* destination, int capacity)
{
    if (destination != NULL && capacity > 0)
        destination[0] = 0;
    const WCHAR* resource = NULL;
    // The resource view is counted and need not be terminated, so never convert it with a length of -1.
    const int characters = LoadStringW(instance, resourceId, reinterpret_cast<LPWSTR>(&resource), 0);
    if (characters <= 0)
        return 0;
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, resource, characters, NULL, 0, NULL, NULL);
    if (bytes <= 0 || destination == NULL)
        return bytes;
    // Refuse partial strings, including a cut through a multibyte Polish character.
    if (capacity <= bytes || WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, resource, characters,
                                               destination, bytes, NULL, NULL) != bytes)
        return 0;
    destination[bytes] = 0;
    return bytes;
}
