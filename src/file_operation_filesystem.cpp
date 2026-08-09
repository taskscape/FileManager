// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "file_operation_filesystem.h"

namespace
{
class CWin32FileOperationFileSystem : public CFileOperationFileSystem
{
public:
    DWORD GetAttributes(const char* path) override
    {
        return SalGetFileAttributes(path);
    }

    BOOL GetDiskFreeSpace(const char* path, DWORD* sectorsPerCluster,
                          DWORD* bytesPerSector, DWORD* freeClusters,
                          DWORD* totalClusters) override
    {
        return MyGetDiskFreeSpace(path, sectorsPerCluster, bytesPerSector, freeClusters, totalClusters);
    }

    CQuadWord QueryFreeSpace(const char* path) override
    {
        return MyGetDiskFreeSpace(path);
    }
};

CWin32FileOperationFileSystem Win32FileOperationFileSystem;
CFileOperationFileSystem* TestFileOperationFileSystem = NULL;
}

CFileOperationFileSystem& FileOperationFileSystem()
{
    return TestFileOperationFileSystem != NULL ? *TestFileOperationFileSystem : Win32FileOperationFileSystem;
}

void SetFileOperationFileSystemForTests(CFileOperationFileSystem* replacement)
{
    TestFileOperationFileSystem = replacement;
}
