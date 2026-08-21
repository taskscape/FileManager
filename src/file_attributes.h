// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include "worker.h"

// Compression/encryption attribute helpers extracted from async_copy.cpp.
// Declared here instead of ad-hoc externs so call sites cannot drift from the
// definitions.
DWORD CompressFile(char* fileName, DWORD attrs);
DWORD UncompressFile(char* fileName, DWORD attrs);
DWORD MyEncryptFile(HWND hProgressDlg, char* fileName, DWORD attrs, DWORD finalAttrs,
                    CProgressDlgData& dlgData, BOOL& cancelOper, BOOL preserveDate);
DWORD MyDecryptFile(char* fileName, DWORD attrs, BOOL preserveDate);
