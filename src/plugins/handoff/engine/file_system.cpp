// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "file_system.h"
#include "text_util.h"

#include <bcrypt.h>
#include <vector>

#include "common/relative_file_operations.h"

namespace handoff
{

HANDLE Win32FileSystem::Open(const std::wstring& path, DWORD access, DWORD share, DWORD disposition, DWORD flags)
{
    return CreateFileW(LongPath(path).c_str(), access, share, NULL, disposition, flags, NULL);
}

BOOL Win32FileSystem::Read(HANDLE file, void* buffer, DWORD length, DWORD* read)
{
    return ReadFile(file, buffer, length, read, NULL);
}

BOOL Win32FileSystem::Write(HANDLE file, const void* buffer, DWORD length, DWORD* written)
{
    return WriteFile(file, buffer, length, written, NULL);
}

BOOL Win32FileSystem::Flush(HANDLE file) { return FlushFileBuffers(file); }
BOOL Win32FileSystem::Close(HANDLE file) { return CloseHandle(file); }

BOOL Win32FileSystem::SetLastWriteTime(HANDLE file, const FILETIME& time)
{
    return SetFileTime(file, NULL, NULL, &time);
}

BOOL Win32FileSystem::GetInformation(HANDLE file, BY_HANDLE_FILE_INFORMATION& information)
{
    return GetFileInformationByHandle(file, &information);
}

BOOL Win32FileSystem::SetAttributes(HANDLE file, DWORD attributes)
{
    FILE_BASIC_INFO info = {};
    info.FileAttributes = attributes; // zero timestamps mean "unchanged"
    return SetFileInformationByHandle(file, FileBasicInfo, &info, sizeof(info));
}

BOOL Win32FileSystem::CreateFolder(const std::wstring& path)
{
    return CreateDirectoryW(LongPath(path).c_str(), NULL);
}

BOOL Win32FileSystem::RenameRelative(HANDLE item, HANDLE directory, const std::wstring& name)
{
    // Shared with the FTP plug-in: handle-bound rename that never replaces (D-07).
    return RenameRelativePublicationFile(item, directory, name.c_str());
}

BOOL Win32FileSystem::DeleteOwnedFile(const std::wstring& path)
{
    return DeleteFileW(LongPath(path).c_str());
}

BOOL Win32FileSystem::RemoveOwnedFolder(const std::wstring& path)
{
    return RemoveDirectoryW(LongPath(path).c_str());
}

BOOL Win32FileSystem::DeleteByHandle(HANDLE item)
{
    FILE_DISPOSITION_INFO info = {};
    info.DeleteFile = TRUE;
    return SetFileInformationByHandle(item, FileDispositionInfo, &info, sizeof(info));
}

DWORD Win32FileSystem::GetAttributes(const std::wstring& path)
{
    return GetFileAttributesW(LongPath(path).c_str());
}

BOOL Win32FileSystem::QueryFreeSpace(const std::wstring& path, uint64_t& freeBytes)
{
    ULARGE_INTEGER available;
    std::wstring folder = LongPath(path);
    if (folder.back() != L'\\')
        folder += L'\\';
    if (!GetDiskFreeSpaceExW(folder.c_str(), &available, NULL, NULL))
        return FALSE;
    freeBytes = available.QuadPart;
    return TRUE;
}

void Win32FileSystem::Pause(DWORD milliseconds)
{
    Sleep(milliseconds);
}

// ---------------------------------------------------------------------------
// SHA-256

namespace
{
struct Provider
{
    BCRYPT_ALG_HANDLE Handle = nullptr;
    DWORD ObjectLength = 0;
    Provider()
    {
        // One reusable-hash provider per process; algorithm handles are thread-safe.
        if (BCryptOpenAlgorithmProvider(&Handle, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_HASH_REUSABLE_FLAG) != 0)
        {
            Handle = nullptr;
            return;
        }
        ULONG written = 0;
        BCryptGetProperty(Handle, BCRYPT_OBJECT_LENGTH, (PUCHAR)&ObjectLength, sizeof(ObjectLength), &written, 0);
    }
    ~Provider()
    {
        if (Handle != nullptr)
            BCryptCloseAlgorithmProvider(Handle, 0);
    }
};

Provider& Sha256Provider()
{
    static Provider provider;
    return provider;
}
} // namespace

Sha256::Sha256()
{
    Provider& provider = Sha256Provider();
    if (provider.Handle == nullptr)
        return;
    Object.resize(provider.ObjectLength);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(provider.Handle, &hash, Object.data(), (ULONG)Object.size(), NULL, 0, BCRYPT_HASH_REUSABLE_FLAG) == 0)
        Hash = hash;
}

Sha256::~Sha256()
{
    if (Hash != nullptr)
        BCryptDestroyHash((BCRYPT_HASH_HANDLE)Hash);
}

void Sha256::Update(const void* data, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)data;
    while (Hash != nullptr && length > 0)
    {
        ULONG chunk = length > 0x40000000 ? 0x40000000 : (ULONG)length;
        BCryptHashData((BCRYPT_HASH_HANDLE)Hash, (PUCHAR)bytes, chunk, 0);
        bytes += chunk;
        length -= chunk;
    }
}

std::wstring Sha256::FinishHex()
{
    unsigned char digest[32];
    if (Hash == nullptr || BCryptFinishHash((BCRYPT_HASH_HANDLE)Hash, digest, sizeof(digest), 0) != 0)
        return std::wstring();
    return ToHex(digest, sizeof(digest));
}

std::wstring Sha256Hex(const void* data, size_t length)
{
    Sha256 hash;
    hash.Update(data, length);
    return hash.FinishHex();
}

} // namespace handoff
