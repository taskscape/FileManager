// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// Execution remains separate from planning so deterministic native tests can
// fail one durable I/O phase without changing the operation plan or UI flow.
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

// A native test owns the replacement and restores NULL before destruction so
// production threads can never retain a pointer to a finished test fake.
void SetOperationExecutionFileSystemForTests(COperationExecutionFileSystem* replacement);
