// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// structure passed to the compression thread, used to transfer input/output parameters
struct CCompressParams
{
    BOOL Result;                     // TRUE if the operation completed successfully, otherwise FALSE
    DWORD CorrelationId;              // identifies one owned crash-compression attempt in diagnostics
    char ErrorMessage[2 * MAX_PATH]; // if Result is FALSE, contains the error description
};

BOOL StartCompressThread(CCompressParams* params);
BOOL IsCompressThreadRunning();
void StopCompressThread();
