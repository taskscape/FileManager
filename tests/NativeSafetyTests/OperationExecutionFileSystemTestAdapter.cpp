// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "../../src/operation_execution_filesystem.h"

namespace
{
class CUnreachableDefaultOperationExecutionFileSystem : public COperationExecutionFileSystem
{
public:
    HANDLE CreateFile(const char*, DWORD, DWORD, DWORD, DWORD) override { return INVALID_HANDLE_VALUE; }
    BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, LPOVERLAPPED) override { return FALSE; }
    BOOL SetFileTime(HANDLE, const FILETIME*, const FILETIME*, const FILETIME*) override { return FALSE; }
    BOOL FlushFileBuffers(HANDLE) override { return FALSE; }
    BOOL ReplaceFile(const char*, const char*) override { return FALSE; }
    BOOL MoveFile(const char*, const char*) override { return FALSE; }
    BOOL SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, void*, DWORD) override { return FALSE; }
};
}

COperationExecutionFileSystem& Win32OperationExecutionFileSystem()
{
    // The native test must install a fake before any operation, so a default
    // that fails closed makes accidental production-I/O use immediately visible.
    static CUnreachableDefaultOperationExecutionFileSystem defaultFileSystem;
    return defaultFileSystem;
}
