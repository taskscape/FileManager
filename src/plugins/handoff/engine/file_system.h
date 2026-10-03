// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// File-system seam for staging and publication. Production uses Win32;
// engine tests substitute a fault-injecting double that fails any call by
// ordinal, so every failure path of the build is exercised deterministically.

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace handoff
{

class IHandoffFileSystem
{
public:
    virtual ~IHandoffFileSystem() {}
    // Paths are absolute; implementations add the long-path prefix.
    virtual HANDLE Open(const std::wstring& path, DWORD access, DWORD share, DWORD disposition, DWORD flags) = 0;
    virtual BOOL Read(HANDLE file, void* buffer, DWORD length, DWORD* read) = 0;
    virtual BOOL Write(HANDLE file, const void* buffer, DWORD length, DWORD* written) = 0;
    virtual BOOL Flush(HANDLE file) = 0;
    virtual BOOL Close(HANDLE file) = 0;
    virtual BOOL SetLastWriteTime(HANDLE file, const FILETIME& time) = 0;
    virtual BOOL GetInformation(HANDLE file, BY_HANDLE_FILE_INFORMATION& information) = 0;
    virtual BOOL SetAttributes(HANDLE file, DWORD attributes) = 0; // FileBasicInfo, timestamps unchanged
    virtual BOOL CreateFolder(const std::wstring& path) = 0;
    // Renames 'item' to 'name' inside 'directory' without replacing an existing entry.
    virtual BOOL RenameRelative(HANDLE item, HANDLE directory, const std::wstring& name) = 0;
    virtual BOOL DeleteOwnedFile(const std::wstring& path) = 0;
    virtual BOOL RemoveOwnedFolder(const std::wstring& path) = 0;
    virtual BOOL DeleteByHandle(HANDLE item) = 0; // FileDispositionInfo
    virtual DWORD GetAttributes(const std::wstring& path) = 0;
    virtual BOOL QueryFreeSpace(const std::wstring& path, uint64_t& freeBytes) = 0;
    virtual void Pause(DWORD milliseconds) = 0;
};

class Win32FileSystem : public IHandoffFileSystem
{
public:
    HANDLE Open(const std::wstring& path, DWORD access, DWORD share, DWORD disposition, DWORD flags) override;
    BOOL Read(HANDLE file, void* buffer, DWORD length, DWORD* read) override;
    BOOL Write(HANDLE file, const void* buffer, DWORD length, DWORD* written) override;
    BOOL Flush(HANDLE file) override;
    BOOL Close(HANDLE file) override;
    BOOL SetLastWriteTime(HANDLE file, const FILETIME& time) override;
    BOOL GetInformation(HANDLE file, BY_HANDLE_FILE_INFORMATION& information) override;
    BOOL SetAttributes(HANDLE file, DWORD attributes) override;
    BOOL CreateFolder(const std::wstring& path) override;
    BOOL RenameRelative(HANDLE item, HANDLE directory, const std::wstring& name) override;
    BOOL DeleteOwnedFile(const std::wstring& path) override;
    BOOL RemoveOwnedFolder(const std::wstring& path) override;
    BOOL DeleteByHandle(HANDLE item) override;
    DWORD GetAttributes(const std::wstring& path) override;
    BOOL QueryFreeSpace(const std::wstring& path, uint64_t& freeBytes) override;
    void Pause(DWORD milliseconds) override;
};

// SHA-256 through Windows CNG (handoff-spec.md D-08).
class Sha256
{
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    bool Ok() const { return Hash != nullptr; }
    void Update(const void* data, size_t length);
    std::wstring FinishHex(); // resets for reuse

private:
    void* Hash = nullptr; // BCRYPT_HASH_HANDLE
    std::vector<unsigned char> Object;
};

std::wstring Sha256Hex(const void* data, size_t length);

} // namespace handoff
