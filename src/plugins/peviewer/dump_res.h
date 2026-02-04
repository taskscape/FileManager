// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

BOOL DumpResourceDirectory(LPVOID lpFile, DWORD fileSize, FILE* outStream);

#ifndef RT_MANIFEST
#define RT_MANIFEST MAKEINTRESOURCE(24)
#endif
