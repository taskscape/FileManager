// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "operation_execution_filesystem.h"

// Planning needs only filesystem facts.  Keep this deliberately narrow: the
// adapter does not create, delete, copy, or mutate anything, and test code can
// replace it while characterizing the generated COperationPlan.
class CFileOperationFileSystem
{
public:
    virtual ~CFileOperationFileSystem() {}

    virtual DWORD GetAttributes(const char* path) = 0;
    virtual BOOL GetDiskFreeSpace(const char* path, DWORD* sectorsPerCluster,
                                  DWORD* bytesPerSector, DWORD* freeClusters,
                                  DWORD* totalClusters) = 0;
    virtual CQuadWord QueryFreeSpace(const char* path) = 0;
};

CFileOperationFileSystem& FileOperationFileSystem();

// Intended for a single-threaded characterization test during planning.  The
// caller owns the replacement and must restore NULL before it is destroyed.
void SetFileOperationFileSystemForTests(CFileOperationFileSystem* replacement);
