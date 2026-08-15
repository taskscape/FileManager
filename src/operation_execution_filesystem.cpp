// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "operation_execution_filesystem.h"

// The production implementation stays in the filesystem adapter translation
// unit; this small indirection lets the native test link the replacement seam.
COperationExecutionFileSystem& Win32OperationExecutionFileSystem();

namespace
{
COperationExecutionFileSystem* TestOperationExecutionFileSystem = NULL;
}

COperationExecutionFileSystem& OperationExecutionFileSystem()
{
    return TestOperationExecutionFileSystem != NULL ? *TestOperationExecutionFileSystem : Win32OperationExecutionFileSystem();
}

void SetOperationExecutionFileSystemForTests(COperationExecutionFileSystem* replacement)
{
    TestOperationExecutionFileSystem = replacement;
}
