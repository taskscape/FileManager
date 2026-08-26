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

// TRUE after the background check found a release newer than this executable;
// read on the UI thread only (worker stores it before posting the result message)
BOOL IsUpdateAvailable();

// starts the one-shot asynchronous check for a newer GitHub release; the given
// window receives WM_USER_UPDATE_CHECK_DONE when the attempt finishes, no
// matter whether it succeeded or was skipped (e.g. offline)
void StartUpdateCheck(HWND hNotifyWindow);
