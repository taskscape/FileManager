// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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

// Execution has a separate seam from planning.  It is intentionally limited to
// the durable copy/move boundaries so a native test can make one Win32 call
// fail without changing worker, dialog, or operation-script behaviour.
class COperationExecutionFileSystem
{
public:
    virtual ~COperationExecutionFileSystem() {}

    virtual HANDLE CreateFile(const char* path, DWORD desiredAccess, DWORD shareMode,
                              DWORD creationDisposition, DWORD flagsAndAttributes) = 0;
    virtual BOOL WriteFile(HANDLE file, const void* buffer, DWORD bytesToWrite,
                           DWORD* bytesWritten, LPOVERLAPPED overlapped) = 0;
    virtual BOOL SetFileTime(HANDLE file, const FILETIME* creationTime,
                             const FILETIME* lastAccessTime, const FILETIME* lastWriteTime) = 0;
    virtual BOOL FlushFileBuffers(HANDLE file) = 0;
    virtual BOOL ReplaceFile(const char* replacedFileName, const char* replacementFileName) = 0;
    virtual BOOL MoveFile(const char* existingFileName, const char* newFileName) = 0;
    virtual BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS informationClass,
                                            void* information, DWORD informationSize) = 0;
};

COperationExecutionFileSystem& OperationExecutionFileSystem();

// Intended for a single-threaded native fault-injection test.  A fake can fail
// any selected call deterministically and must be restored before destruction.
void SetOperationExecutionFileSystemForTests(COperationExecutionFileSystem* replacement);
