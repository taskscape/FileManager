// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "fsprobe.h"

namespace reorg
{
namespace
{

bool ExcludedRecovery(const std::wstring& name)
{
    return NamesEqual(name, L".reorg-recovery", false);
}

void FillFromFind(const std::wstring& directory, const WIN32_FIND_DATAW& data, CSnapshotItem& item)
{
    item = CSnapshotItem();
    item.Name = data.cFileName;
    item.ParentPath = directory;
    item.Path = JoinPath(directory, item.Name);
    item.IsDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    item.Attributes = data.dwFileAttributes;
    item.Size = ((unsigned __int64)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    ULARGE_INTEGER write;
    write.LowPart = data.ftLastWriteTime.dwLowDateTime;
    write.HighPart = data.ftLastWriteTime.dwHighDateTime;
    item.LastWrite = write.QuadPart;
    ULARGE_INTEGER created;
    created.LowPart = data.ftCreationTime.dwLowDateTime;
    created.HighPart = data.ftCreationTime.dwHighDateTime;
    item.Created = created.QuadPart;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        item.ReparseTag = data.dwReserved0;
    item.CloudPlaceholder = (data.dwFileAttributes & FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) != 0;
    item.CloudSynced = item.CloudPlaceholder || (data.dwFileAttributes & (FILE_ATTRIBUTE_PINNED | FILE_ATTRIBUTE_UNPINNED)) != 0 ||
                       (item.ReparseTag & 0xFFFF0000) == 0x90000000;
}

} // namespace

std::wstring ExtendedPath(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0)
        return path;
    if (path.rfind(L"\\\\", 0) == 0)
        return L"\\\\?\\UNC\\" + path.substr(2);
    if (path.size() >= 248)
        return L"\\\\?\\" + path;
    return path;
}

bool CWin32FileSystemProbe::Enumerate(const std::wstring& directory, std::vector<CSnapshotItem>& children, DWORD& error)
{
    children.clear();
    error = ERROR_SUCCESS;
    std::wstring pattern = ExtendedPath(directory);
    if (!pattern.empty() && pattern.back() != L'\\')
        pattern.push_back(L'\\');
    pattern += L"*";
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (find == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    do
    {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
            continue;
        if (ExcludedRecovery(data.cFileName))
            continue;
        CSnapshotItem item;
        FillFromFind(directory, data, item);
        children.push_back(item);
    } while (FindNextFileW(find, &data));
    DWORD nextError = GetLastError();
    FindClose(find);
    if (nextError != ERROR_NO_MORE_FILES && nextError != ERROR_SUCCESS)
    {
        error = nextError;
        return false;
    }
    return true;
}

bool CWin32FileSystemProbe::ReadIdentity(const std::wstring& path, CFileId& identity, DWORD& error)
{
    identity = CFileId();
    error = ERROR_SUCCESS;
    HANDLE file = CreateFileW(ExtendedPath(path).c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    FILE_ID_INFO info;
    memset(&info, 0, sizeof(info));
    BOOL idOk = GetFileInformationByHandleEx(file, FileIdInfo, &info, sizeof(info));
    BY_HANDLE_FILE_INFORMATION basic;
    memset(&basic, 0, sizeof(basic));
    BOOL basicOk = GetFileInformationByHandle(file, &basic);
    if (!idOk && !basicOk)
    {
        error = GetLastError();
        CloseHandle(file);
        return false;
    }
    identity.Valid = true;
    identity.IsDir = basicOk && (basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    identity.Attributes = basicOk ? basic.dwFileAttributes : 0;
    identity.Size = basicOk ? (((unsigned __int64)basic.nFileSizeHigh << 32) | basic.nFileSizeLow) : 0;
    if (basicOk)
    {
        ULARGE_INTEGER write;
        write.LowPart = basic.ftLastWriteTime.dwLowDateTime;
        write.HighPart = basic.ftLastWriteTime.dwHighDateTime;
        identity.LastWrite = write.QuadPart;
    }
    if (idOk)
    {
        identity.VolumeSerial = (DWORD)info.VolumeSerialNumber;
        memcpy(identity.FileId, info.FileId.Identifier, 16);
    }
    else
    {
        identity.VolumeSerial = basic.dwVolumeSerialNumber;
        memcpy(identity.FileId, &basic.nFileIndexLow, 4);
        memcpy(identity.FileId + 4, &basic.nFileIndexHigh, 4);
    }
    CloseHandle(file);
    return true;
}

bool CWin32FileSystemProbe::QueryCaseSensitive(const std::wstring& directory, bool& caseSensitive, DWORD& error)
{
    caseSensitive = false;
    error = ERROR_SUCCESS;
    HANDLE file = CreateFileW(ExtendedPath(directory).c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    FILE_CASE_SENSITIVE_INFO info;
    memset(&info, 0, sizeof(info));
    if (!GetFileInformationByHandleEx(file, FileCaseSensitiveInfo, &info, sizeof(info)))
    {
        error = GetLastError();
        CloseHandle(file);
        // Older volumes do not support the query. Case-insensitive is the Windows default.
        caseSensitive = false;
        return error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION;
    }
    caseSensitive = (info.Flags & FILE_CS_FLAG_CASE_SENSITIVE_DIR) != 0;
    CloseHandle(file);
    return true;
}

bool CWin32FileSystemProbe::GetVolume(const std::wstring& path, CVolumeInfo& info)
{
    info = CVolumeInfo();
    std::wstring root = path;
    if (path.size() >= 2 && path[1] == L':')
        root = path.substr(0, 3);
    wchar_t fs[64];
    DWORD serial = 0, maxComp = 0, flags = 0;
    if (!GetVolumeInformationW(root.c_str(), NULL, 0, &serial, &maxComp, &flags, fs, _countof(fs)))
    {
        info.Error = GetLastError();
        return false;
    }
    ULARGE_INTEGER freeBytes;
    freeBytes.QuadPart = 0;
    if (!GetDiskFreeSpaceExW(root.c_str(), &freeBytes, NULL, NULL))
    {
        info.Error = GetLastError();
        return false;
    }
    info.Ok = true;
    info.Serial = serial;
    info.FileSystem = fs;
    info.Flags = flags;
    info.MaxComponent = maxComp;
    info.ReadOnly = (flags & FILE_READ_ONLY_VOLUME) != 0;
    info.PersistentFileIds = (flags & FILE_SUPPORTS_OPEN_BY_FILE_ID) != 0;
    info.FreeBytes = freeBytes.QuadPart;
    return true;
}

bool CWin32FileSystemProbe::Exists(const std::wstring& path, bool& isDir, DWORD& attributes, DWORD& error)
{
    attributes = GetFileAttributesW(ExtendedPath(path).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        error = GetLastError();
        isDir = false;
        return false;
    }
    error = ERROR_SUCCESS;
    isDir = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return true;
}

bool CWin32FileSystemProbe::ReadFile(const std::wstring& path, unsigned __int64 maxBytes, std::vector<BYTE>& data, DWORD& error)
{
    data.clear();
    HANDLE file = CreateFileW(ExtendedPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size))
    {
        error = GetLastError();
        CloseHandle(file);
        return false;
    }
    unsigned __int64 toRead = (unsigned __int64)size.QuadPart;
    if (toRead > maxBytes)
        toRead = maxBytes;
    data.resize((size_t)toRead);
    DWORD read = 0;
    BOOL ok = toRead == 0 || ::ReadFile(file, data.data(), (DWORD)toRead, &read, NULL);
    error = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok)
        return false;
    data.resize(read);
    return true;
}

bool CWin32FileSystemProbe::WriteAtomic(const std::wstring& path, const std::string& bytes, DWORD& error)
{
    // Write a sibling, flush it, then replace. A crash before the replace leaves the previous plan intact.
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(ExtendedPath(temp).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    DWORD written = 0;
    BOOL ok = bytes.empty() || ::WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, NULL);
    if (!ok || written != bytes.size() || !FlushFileBuffers(file))
    {
        error = GetLastError();
        CloseHandle(file);
        DeleteFileW(ExtendedPath(temp).c_str());
        return false;
    }
    CloseHandle(file);
    if (!MoveFileExW(ExtendedPath(temp).c_str(), ExtendedPath(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        error = GetLastError();
        DeleteFileW(ExtendedPath(temp).c_str());
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

bool CWin32FileSystemProbe::CreateProbe(const std::wstring& directory, DWORD& error)
{
    std::wstring name = JoinPath(directory, L"~reorg-probe-" + NewGuid() + L".tmp");
    HANDLE file = CreateFileW(ExtendedPath(name).c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        error = GetLastError();
        return false;
    }
    CloseHandle(file);
    DWORD attrs = GetFileAttributesW(ExtendedPath(name).c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        error = ERROR_FILE_EXISTS;
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

void CMemoryFileSystem::AddDir(const std::wstring& path, DWORD volumeSerial, const wchar_t* fileSystem)
{
    CNode node;
    node.Item.Path = path;
    node.Item.ParentPath = ParentPath(path);
    node.Item.Name = LeafName(path);
    node.Item.IsDir = true;
    node.Item.Attributes = FILE_ATTRIBUTE_DIRECTORY;
    node.Item.VolumeSerial = volumeSerial;
    node.Item.FileSystem = fileSystem ? fileSystem : L"NTFS";
    node.Item.PersistentFileIds = true;
    node.Item.MaxComponent = 255;
    node.Item.FreeBytes = 1024ull * 1024ull * 1024ull;
    Nodes[path] = node;
}

void CMemoryFileSystem::AddFile(const std::wstring& path, unsigned __int64 size, unsigned __int64 lastWrite, DWORD attributes, DWORD volumeSerial, const BYTE fileId[16])
{
    CNode node;
    node.Item.Path = path;
    node.Item.ParentPath = ParentPath(path);
    node.Item.Name = LeafName(path);
    node.Item.IsDir = false;
    node.Item.Size = size;
    node.Item.LastWrite = lastWrite;
    node.Item.Attributes = attributes;
    node.Item.VolumeSerial = volumeSerial;
    node.Item.HasFileId = fileId != NULL;
    if (fileId)
        memcpy(node.Item.FileId, fileId, 16);
    node.Item.FileSystem = L"NTFS";
    node.Item.PersistentFileIds = true;
    Nodes[path] = node;
}

void CMemoryFileSystem::SetContent(const std::wstring& path, const std::string& content)
{
    Nodes[path].Content = content;
    Files[path] = content;
}

bool CMemoryFileSystem::Enumerate(const std::wstring& directory, std::vector<CSnapshotItem>& children, DWORD& error)
{
    children.clear();
    if (Nodes.find(directory) == Nodes.end())
    {
        error = ERROR_PATH_NOT_FOUND;
        return false;
    }
    error = ERROR_SUCCESS;
    for (std::map<std::wstring, CNode>::const_iterator it = Nodes.begin(); it != Nodes.end(); ++it)
    {
        if (PathsEqual(it->second.Item.ParentPath, directory, false) && !ExcludedRecovery(it->second.Item.Name))
            children.push_back(it->second.Item);
    }
    return true;
}

bool CMemoryFileSystem::ReadIdentity(const std::wstring& path, CFileId& identity, DWORD& error)
{
    std::map<std::wstring, CNode>::const_iterator it = Nodes.find(path);
    if (it == Nodes.end())
    {
        error = ERROR_FILE_NOT_FOUND;
        return false;
    }
    identity = CFileId();
    identity.Valid = true;
    identity.VolumeSerial = it->second.Item.VolumeSerial;
    memcpy(identity.FileId, it->second.Item.FileId, 16);
    identity.Size = it->second.Item.Size;
    identity.LastWrite = it->second.Item.LastWrite;
    identity.Attributes = it->second.Item.Attributes;
    identity.IsDir = it->second.Item.IsDir;
    error = ERROR_SUCCESS;
    return true;
}

bool CMemoryFileSystem::QueryCaseSensitive(const std::wstring& directory, bool& caseSensitive, DWORD& error)
{
    std::map<std::wstring, CNode>::const_iterator it = Nodes.find(directory);
    if (it == Nodes.end())
    {
        error = ERROR_PATH_NOT_FOUND;
        return false;
    }
    caseSensitive = it->second.CaseSensitive;
    error = ERROR_SUCCESS;
    return true;
}

bool CMemoryFileSystem::GetVolume(const std::wstring& path, CVolumeInfo& info)
{
    info = CVolumeInfo();
    std::map<std::wstring, CNode>::const_iterator it = Nodes.find(path);
    if (it == Nodes.end())
    {
        info.Error = ERROR_PATH_NOT_FOUND;
        return false;
    }
    info.Ok = true;
    info.Serial = it->second.Item.VolumeSerial;
    info.FileSystem = it->second.Item.FileSystem;
    info.MaxComponent = it->second.Item.MaxComponent;
    info.ReadOnly = it->second.Item.ReadOnlyVolume;
    info.PersistentFileIds = it->second.Item.PersistentFileIds;
    info.FreeBytes = it->second.Item.FreeBytes;
    info.Flags = info.PersistentFileIds ? FILE_SUPPORTS_OPEN_BY_FILE_ID : 0;
    return true;
}

bool CMemoryFileSystem::Exists(const std::wstring& path, bool& isDir, DWORD& attributes, DWORD& error)
{
    std::map<std::wstring, CNode>::const_iterator it = Nodes.find(path);
    if (it == Nodes.end())
    {
        error = ERROR_FILE_NOT_FOUND;
        isDir = false;
        attributes = INVALID_FILE_ATTRIBUTES;
        return false;
    }
    isDir = it->second.Item.IsDir;
    attributes = it->second.Item.Attributes;
    error = ERROR_SUCCESS;
    return true;
}

bool CMemoryFileSystem::ReadFile(const std::wstring& path, unsigned __int64 maxBytes, std::vector<BYTE>& data, DWORD& error)
{
    std::map<std::wstring, CNode>::const_iterator it = Nodes.find(path);
    if (it == Nodes.end())
    {
        error = ERROR_FILE_NOT_FOUND;
        return false;
    }
    size_t n = it->second.Content.size();
    if ((unsigned __int64)n > maxBytes)
        n = (size_t)maxBytes;
    data.assign(it->second.Content.begin(), it->second.Content.begin() + n);
    error = ERROR_SUCCESS;
    return true;
}

bool CMemoryFileSystem::WriteAtomic(const std::wstring& path, const std::string& bytes, DWORD& error)
{
    if (FailReplace)
    {
        // The temp sibling can exist, but the previous file must stay byte-for-byte intact.
        error = ERROR_ACCESS_DENIED;
        return false;
    }
    CNode& node = Nodes[path];
    node.Content = bytes;
    node.Item.Path = path;
    node.Item.Name = LeafName(path);
    node.Item.ParentPath = ParentPath(path);
    node.Item.Size = bytes.size();
    Files[path] = bytes;
    error = ERROR_SUCCESS;
    return true;
}

bool CMemoryFileSystem::CreateProbe(const std::wstring& directory, DWORD& error)
{
    if (Nodes.find(directory) == Nodes.end())
    {
        error = ERROR_PATH_NOT_FOUND;
        return false;
    }
    error = ERROR_SUCCESS;
    return true;
}

} // namespace reorg
