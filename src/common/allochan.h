// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// The global allocation handler never blocks the failing thread.  Its first
// failure releases the reserve, records fatal pressure atomically, and posts a
// pre-registered notification for the UI thread to perform recovery work.
void SetAllocEmergencyNotificationWindow(HWND window, UINT message);
BOOL IsAllocationEmergencyActive();
