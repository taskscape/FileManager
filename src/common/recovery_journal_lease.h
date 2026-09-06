// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string>
#include "../operation_execution_filesystem.h"

// One incompatible open is both the recovery claim and the stable snapshot.
// Keeping it through outcome persistence excludes live writers and other recoverers.
class CRecoveryJournalLease
{
public:
    CRecoveryJournalLease() : File(INVALID_HANDLE_VALUE), WriteError(ERROR_SUCCESS) {}
    ~CRecoveryJournalLease() { Close(); }
    BOOL Open(const WCHAR* path)
    {
        Close();
        WriteError = ERROR_SUCCESS;
        File = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        // A redirected journal is not an owned log and must never receive recovery records.
        BY_HANDLE_FILE_INFORMATION information;
        if (File != INVALID_HANDLE_VALUE && (!GetFileInformationByHandle(File, &information) ||
            (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0))
        {
            SetLastError(ERROR_INVALID_DATA); Close(); return FALSE;
        }
        return File != INVALID_HANDLE_VALUE;
    }
    HANDLE Get() const { return File; }
    DWORD GetWriteError() const { return WriteError; }
    BOOL Append(COperationExecutionFileSystem& fileSystem, const std::string& record)
    {
        if (WriteError != ERROR_SUCCESS) { SetLastError(WriteError); return FALSE; }
        if (record.empty() || record.back() != '\n' || record.size() > 64 * 1024 || record.find('\0') != std::string::npos)
            return Fail(ERROR_INVALID_DATA);
        LARGE_INTEGER end = {};
        if (!SetFilePointerEx(File, end, NULL, FILE_END)) return Fail(GetLastError());
        DWORD offset = 0;
        while (offset < record.size())
        {
            DWORD written = 0;
            if (!fileSystem.WriteFile(File, record.data() + offset, (DWORD)record.size() - offset, &written, NULL))
                return Fail(GetLastError());
            if (written == 0 || written > record.size() - offset) return Fail(ERROR_WRITE_FAULT);
            offset += written;
        }
        // A failed append/flush latches the lease: later records must not make a
        // partially written outcome look complete, or conceal the first failure.
        if (!fileSystem.FlushFileBuffers(File)) return Fail(GetLastError());
        return TRUE;
    }
    void Close()
    {
        const DWORD error = GetLastError();
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
        File = INVALID_HANDLE_VALUE;
        SetLastError(error);
    }
private:
    CRecoveryJournalLease(const CRecoveryJournalLease&);
    CRecoveryJournalLease& operator=(const CRecoveryJournalLease&);
    HANDLE File;
    DWORD WriteError;
    BOOL Fail(DWORD error)
    {
        WriteError = error == ERROR_SUCCESS ? ERROR_WRITE_FAULT : error;
        SetLastError(WriteError);
        return FALSE;
    }
};
