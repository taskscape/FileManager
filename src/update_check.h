// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

// opens the GitHub releases page in the default browser (target of the
// Help > Download update menu item)
#define SALAMANDER_RELEASES_URL "https://github.com/taskscape/FileManager/releases"

// menu enabler for Help > Download update: zero keeps the item disabled,
// non-zero enables it once a newer published release is confirmed
extern DWORD EnablerUpdateAvailable;

// TRUE after the most recent successful check found a release newer than this
// executable. A failed refresh deliberately preserves a previous positive
// result, so users never lose a confirmed download route because of a timeout.
BOOL IsUpdateAvailable();

// Starts or joins the process-wide check for a newer GitHub release. Callers
// receive notifyMessage on notifyWindow when this shared request completes.
// A non-forced request reuses a completed result; a manual forced request may
// retry after an offline or stale automatic check.
BOOL RequestApplicationUpdateCheck(HWND notifyWindow, UINT notifyMessage, BOOL force);

// Returns one of CSalamanderApplicationUpdateState from spl_gen.h. Keeping the
// state in the host lets the Help menu and CheckVer describe the same request.
int GetApplicationUpdateState();

// Compatibility wrapper for startup code. New callers should use the
// coordinator entry point above so they can coalesce with CheckVer requests.
void StartUpdateCheck(HWND hNotifyWindow);
