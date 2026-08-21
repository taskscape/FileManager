// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

// Teardown of the whole application, called once after the main message loop
// ends; see app_shutdown.cpp.
void ShutdownSalamander();

// Releases the preloaded resource strings created by InitPreloadedStrings();
// defined next to the shutdown sequence because it is pure cleanup.
void ReleasePreloadedStrings();
