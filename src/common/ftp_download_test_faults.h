// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ftp_download_file_system.h"
#include "monotonic_time.h"
#include <string>
#include <vector>

// Deterministic product regressions require all three sandbox guards and a
// one-use arm file. Ordinary downloads never pause or inject storage failures.
class CFtpDownloadTestFaults : public CFtpDownloadFileSystem
{
public:
    explicit CFtpDownloadTestFaults(HANDLE stage) : Stage(stage)
    {
        Claim(NULL);
        if (Phase.empty()) return;
        Marker(L".ftp-reliability.entered", "entered");
        if (Phase == L"pause")
        {
            const auto started = CMonotonicClock::Now();
            while (GetFileAttributesW((Root + L"\\.ftp-reliability.release").c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                if (CMonotonicClock::Elapsed(started, CMonotonicClock::Now()) >= 20000)
                { PauseTimedOut = TRUE; break; }
                Sleep(10);
            }
        }
    }
    void Completed(DWORD error)
    { if (!Phase.empty()) Marker(L".ftp-reliability.completed", std::to_string(error)); }
    static BOOL RejectAdmission(const WCHAR* phase)
    {
        CFtpDownloadTestFaults fault;
        if (!fault.Claim(phase)) return FALSE;
        fault.Marker(L".ftp-reliability.entered", "admission");
        fault.Completed(ERROR_NOT_ENOUGH_MEMORY);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return TRUE;
    }
    BOOL GetSize(HANDLE file, LARGE_INTEGER& size) override
    {
        if (PauseTimedOut) { SetLastError(ERROR_TIMEOUT); return FALSE; }
        return CFtpDownloadFileSystem::GetSize(file, size);
    }
    BOOL FlushFileBuffers(HANDLE file) override
    {
        if (file == Stage && Phase == L"flush") { SetLastError(ERROR_DISK_FULL); return FALSE; }
        return CFtpDownloadFileSystem::FlushFileBuffers(file);
    }
    BOOL SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access, const FILETIME* write) override
    {
        if (Phase == L"metadata") { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        return CFtpDownloadFileSystem::SetFileTime(file, creation, access, write);
    }
    BOOL RenameFileByHandle(HANDLE file, HANDLE directory, const WCHAR* name) override
    {
        if (Phase == L"commit" && ++Renames == 2) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        return CFtpDownloadFileSystem::RenameFileByHandle(file, directory, name);
    }
    BOOL CloseFile(HANDLE file) override
    {
        const BOOL result = CFtpDownloadFileSystem::CloseFile(file);
        // The injected error remains unknown to the caller even though the real
        // test handle is closed, preventing leaks or retries of a recycled value.
        if (file == Stage && Phase == L"close") { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return result;
    }
private:
    CFtpDownloadTestFaults() = default;
    HANDLE Stage = INVALID_HANDLE_VALUE;
    std::wstring Root, Phase;
    BOOL PauseTimedOut = FALSE;
    int Renames = 0;
    static std::wstring Environment(const WCHAR* name)
    {
        const DWORD count = GetEnvironmentVariableW(name, NULL, 0);
        if (count == 0 || count > 32768) return {};
        std::vector<WCHAR> value(count);
        const DWORD copied = GetEnvironmentVariableW(name, value.data(), count);
        return copied != 0 && copied < count ? std::wstring(value.data(), copied) : std::wstring();
    }
    BOOL Claim(const WCHAR* required)
    {
        if (Environment(L"FILEMANAGER_UI_ISOLATED") != L"1" ||
            Environment(L"FILEMANAGER_UI_CONFIG_ROOT") != L"Software\\Open Salamander\\6.0-filemanager-testdata") return FALSE;
        const std::wstring requested = Environment(L"FILEMANAGER_UI_FTP_FAULT");
        if (requested != L"pause" && requested != L"flush" && requested != L"metadata" &&
            requested != L"commit" && requested != L"close" && requested != L"admission" &&
            requested != L"close-admission") return FALSE;
        if (required != NULL && requested != required) return FALSE;
        if (required == NULL && (requested == L"admission" || requested == L"close-admission")) return FALSE;
        Root = Environment(L"FILEMANAGER_UI_TESTDATA_ROOT");
        while (!Root.empty() && (Root.back() == L'\\' || Root.back() == L'/')) Root.pop_back();
        const size_t separator = Root.rfind(L'\\');
        if (Root.size() < 3 || Root[1] != L':' || Root[2] != L'\\' || separator == std::wstring::npos ||
            _wcsicmp(Root.substr(separator + 1).c_str(), L"filemanager-testdata") != 0) return FALSE;
        const std::wstring arm = Root + L"\\.ftp-reliability.arm";
        HANDLE file = CreateFileW(arm.c_str(), GENERIC_READ | DELETE, 0, NULL, OPEN_EXISTING,
                                  FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE) return FALSE;
        BY_HANDLE_FILE_INFORMATION information;
        const BOOL owned = GetFileInformationByHandle(file, &information) &&
            (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
        CloseHandle(file);
        if (owned) Phase = requested;
        return owned;
    }
    void Marker(const WCHAR* leaf, const std::string& contents)
    {
        HANDLE file = CreateFileW((Root + L"\\" + leaf).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  NULL, CREATE_NEW, FILE_FLAG_WRITE_THROUGH, NULL);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written;
        WriteFile(file, contents.data(), (DWORD)contents.size(), &written, NULL);
        CloseHandle(file);
    }
};
