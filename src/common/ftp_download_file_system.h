// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../operation_execution_filesystem.h"

// The FTP module supplies its own Win32 adapter; native regressions override
// only a failed phase while exercising the production publication protocol.
class CFtpDownloadFileSystem : public COperationExecutionFileSystem
{
public:
    HANDLE CreateFile(const char*, DWORD, DWORD, DWORD, DWORD) override
    { SetLastError(ERROR_NOT_SUPPORTED); return INVALID_HANDLE_VALUE; }
    BOOL WriteFile(HANDLE file, const void* data, DWORD count, DWORD* written, LPOVERLAPPED overlapped) override
    { return ::WriteFile(file, data, count, written, overlapped); }
    BOOL SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access, const FILETIME* write) override
    { return ::SetFileTime(file, creation, access, write); }
    BOOL FlushFileBuffers(HANDLE file) override { return ::FlushFileBuffers(file); }
    BOOL ReplaceFile(const char*, const char*) override { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    BOOL MoveFile(const char*, const char*) override { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS type, void* data, DWORD count) override
    { return ::SetFileInformationByHandle(file, type, data, count); }
    virtual BOOL CloseFile(HANDLE file) { return ::CloseHandle(file); }
    virtual BOOL GetSize(HANDLE file, LARGE_INTEGER& size) { return ::GetFileSizeEx(file, &size); }
    virtual BOOL Truncate(HANDLE file, ULONGLONG length)
    {
        LARGE_INTEGER position; position.QuadPart = length;
        return ::SetFilePointerEx(file, position, NULL, FILE_BEGIN) && ::SetEndOfFile(file);
    }
};
