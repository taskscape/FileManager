// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "unicode_text_layout.h"
#include <shobjidl.h>

inline bool ShellLinkTargetUtf8(IShellLinkW* link, char* target, size_t capacity)
{
    if (link == NULL || target == NULL || capacity == 0 || capacity > INT_MAX)
        return false;
    try
    {
        // COM returns UTF-16 targets; reject oversized UTF-8 results before touching the caller's cached path.
        // IShellLink may silently truncate to its input capacity; always retrieve at least the legacy full path first.
        size_t wideCapacity = capacity < MAX_PATH ? MAX_PATH : capacity;
        std::wstring wide(wideCapacity, L'\0');
        std::string utf8;
        if (link->GetPath(&wide[0], static_cast<int>(wideCapacity), NULL, SLGP_UNCPRIORITY) != S_OK ||
            !WideTextToUtf8(wide.c_str(), -1, utf8) || utf8.size() >= capacity)
            return false;
        memcpy(target, utf8.c_str(), utf8.size() + 1);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }
}
