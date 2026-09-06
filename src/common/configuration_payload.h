// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// One save spans the UI and registry worker. A process-local first-error latch
// therefore follows the serialized transaction rather than a thread-local scope.
class CConfigurationPayloadStatus
{
public:
    void Begin()
    { InterlockedExchange(&Failure, ERROR_SUCCESS); InterlockedExchange(&Active, 1); }
    BOOL IsActive() const { return InterlockedCompareExchange(&Active, 0, 0) != 0; }
    LONG Error() const { return InterlockedCompareExchange(&Failure, 0, 0); }
    LONG End() { InterlockedExchange(&Active, 0); return Error(); }
    void Record(LONG error, BOOL missingIsSuccess = FALSE)
    {
        if (error != ERROR_SUCCESS && !(missingIsSuccess && error == ERROR_FILE_NOT_FOUND) && IsActive())
            InterlockedCompareExchange(&Failure, error, ERROR_SUCCESS);
    }
private:
    mutable volatile LONG Active = 0, Failure = ERROR_SUCCESS;
};

struct CConfigurationRequiredField { const char* Name; DWORD Type; };

// The manifest count is the writer's intended collection size. Counting only
// what reached the registry would legitimize missing optional entries.
inline BOOL ValidateConfigurationCollection(HKEY collection, DWORD expectedCount,
    const CConfigurationRequiredField* fields, size_t fieldCount)
{
    DWORD actualCount = 0;
    if (RegQueryInfoKeyA(collection, NULL, NULL, NULL, &actualCount, NULL, NULL,
                         NULL, NULL, NULL, NULL, NULL) != ERROR_SUCCESS || actualCount != expectedCount)
        return FALSE;
    for (DWORD index = 0; index < expectedCount; ++index)
    {
        const std::string name = std::to_string((ULONGLONG)index + 1);
        HKEY child = NULL;
        if (RegOpenKeyExA(collection, name.c_str(), 0, KEY_QUERY_VALUE, &child) != ERROR_SUCCESS)
            return FALSE;
        BOOL valid = TRUE;
        for (size_t field = 0; field < fieldCount && valid; ++field)
        {
            DWORD type = 0, length = 0;
            valid = RegQueryValueExA(child, fields[field].Name, NULL, &type, NULL, &length) == ERROR_SUCCESS &&
                    type == fields[field].Type;
            if (valid && type == REG_DWORD) valid = length == sizeof(DWORD);
            if (valid && type == REG_SZ)
            {
                // Bound inspection of malformed persisted text before allocating.
                valid = length != 0 && length <= 1024 * 1024;
                if (valid)
                {
                    try
                    {
                        std::vector<BYTE> value(length);
                        const DWORD expectedLength = length;
                        valid = RegQueryValueExA(child, fields[field].Name, NULL, &type, value.data(), &length) == ERROR_SUCCESS &&
                                type == REG_SZ && length == expectedLength && value[length - 1] == 0;
                    }
                    catch (const std::bad_alloc&) { valid = FALSE; }
                }
            }
        }
        RegCloseKey(child);
        if (!valid) return FALSE;
    }
    return TRUE;
}
