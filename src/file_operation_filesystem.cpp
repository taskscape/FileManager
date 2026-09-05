// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "file_operation_filesystem.h"

namespace
{
// Win32 implementation of path attribute and free-space queries used by file operations.
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

// Win32 implementation of create/write/replace/move used while executing an operation script.
class CWin32OperationExecutionFileSystem : public COperationExecutionFileSystem
{
public:
    HANDLE CreateFile(const char* path, DWORD desiredAccess, DWORD shareMode,
                      DWORD creationDisposition, DWORD flagsAndAttributes) override
    {
        return CreateFileUtf8(path, desiredAccess, shareMode, NULL, creationDisposition,
                              flagsAndAttributes, NULL);
    }

    BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytesToWrite,
                   DWORD* bytesWritten, LPOVERLAPPED overlapped) override
    {
        return ::WriteFile(file, buffer, bytesToWrite, bytesWritten, overlapped);
    }

    BOOL SetFileTime(HANDLE file, const FILETIME* creationTime,
                     const FILETIME* lastAccessTime, const FILETIME* lastWriteTime) override
    {
        return ::SetFileTime(file, creationTime, lastAccessTime, lastWriteTime);
    }

    BOOL FlushFileBuffers(HANDLE file) override
    {
        return ::FlushFileBuffers(file);
    }

    BOOL ReplaceFile(const char* replacedFileName, const char* replacementFileName) override
    {
        CStrP replacedFileNameW(ConvertAllocUtf8ToWide(replacedFileName, -1));
        CStrP replacementFileNameW(ConvertAllocUtf8ToWide(replacementFileName, -1));
        if (replacedFileNameW == NULL || replacementFileNameW == NULL)
        {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return FALSE;
        }
        return ReplaceFileW(replacedFileNameW, replacementFileNameW, NULL,
                            REPLACEFILE_WRITE_THROUGH, NULL, NULL);
    }

    BOOL MoveFile(const char* existingFileName, const char* newFileName) override
    {
        CStrP existingFileNameW(ConvertAllocUtf8ToWide(existingFileName, -1));
        CStrP newFileNameW(ConvertAllocUtf8ToWide(newFileName, -1));
        if (existingFileNameW == NULL || newFileNameW == NULL)
        {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return FALSE;
        }
        return MoveFileExW(existingFileNameW, newFileNameW,
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }

    BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS informationClass,
                                    void* information, DWORD informationSize) override
    {
        return ::SetFileInformationByHandle(file, informationClass, information, informationSize);
    }
};

CWin32OperationExecutionFileSystem DefaultOperationExecutionFileSystem;
}

CFileOperationFileSystem& FileOperationFileSystem()
{
    return TestFileOperationFileSystem != NULL ? *TestFileOperationFileSystem : Win32FileOperationFileSystem;
}

void SetFileOperationFileSystemForTests(CFileOperationFileSystem* replacement)
{
    TestFileOperationFileSystem = replacement;
}

COperationExecutionFileSystem& Win32OperationExecutionFileSystem()
{
    return DefaultOperationExecutionFileSystem;
}
