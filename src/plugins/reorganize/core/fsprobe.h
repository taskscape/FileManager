// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Every filesystem touch from the core goes through this probe, so tests can
// substitute an in-memory volume and the plug-in can use Win32.

#include "util.h"

namespace reorg
{

struct CVolumeInfo
{
    DWORD Serial;
    std::wstring FileSystem;
    DWORD Flags;
    DWORD MaxComponent;
    bool ReadOnly;
    bool PersistentFileIds;
    unsigned __int64 FreeBytes;
    bool Ok;
    DWORD Error;

    CVolumeInfo()
        : Serial(0), Flags(0), MaxComponent(255), ReadOnly(false), PersistentFileIds(true),
          FreeBytes(0), Ok(false), Error(0)
    {
    }
};

class IFileSystemProbe
{
public:
    virtual ~IFileSystemProbe() {}

    virtual bool Enumerate(const std::wstring& directory, std::vector<CSnapshotItem>& children, DWORD& error) = 0;
    virtual bool ReadIdentity(const std::wstring& path, CFileId& identity, DWORD& error) = 0;
    virtual bool QueryCaseSensitive(const std::wstring& directory, bool& caseSensitive, DWORD& error) = 0;
    virtual bool GetVolume(const std::wstring& path, CVolumeInfo& info) = 0;
    virtual bool Exists(const std::wstring& path, bool& isDir, DWORD& attributes, DWORD& error) = 0;
    virtual bool ReadFile(const std::wstring& path, unsigned __int64 maxBytes, std::vector<BYTE>& data, DWORD& error) = 0;
    virtual bool WriteAtomic(const std::wstring& path, const std::string& bytes, DWORD& error) = 0;
    virtual bool CreateProbe(const std::wstring& directory, DWORD& error) = 0;
};

class CWin32FileSystemProbe : public IFileSystemProbe
{
public:
    virtual bool Enumerate(const std::wstring& directory, std::vector<CSnapshotItem>& children, DWORD& error);
    virtual bool ReadIdentity(const std::wstring& path, CFileId& identity, DWORD& error);
    virtual bool QueryCaseSensitive(const std::wstring& directory, bool& caseSensitive, DWORD& error);
    virtual bool GetVolume(const std::wstring& path, CVolumeInfo& info);
    virtual bool Exists(const std::wstring& path, bool& isDir, DWORD& attributes, DWORD& error);
    virtual bool ReadFile(const std::wstring& path, unsigned __int64 maxBytes, std::vector<BYTE>& data, DWORD& error);
    virtual bool WriteAtomic(const std::wstring& path, const std::string& bytes, DWORD& error);
    virtual bool CreateProbe(const std::wstring& directory, DWORD& error);
};

class CMemoryFileSystem : public IFileSystemProbe
{
public:
    bool FailReplace;
    CMemoryFileSystem() : FailReplace(false) {}

    void AddDir(const std::wstring& path, DWORD volumeSerial, const wchar_t* fileSystem);
    void AddFile(const std::wstring& path, unsigned __int64 size, unsigned __int64 lastWrite, DWORD attributes, DWORD volumeSerial, const BYTE fileId[16]);
    void SetContent(const std::wstring& path, const std::string& content);

    virtual bool Enumerate(const std::wstring& directory, std::vector<CSnapshotItem>& children, DWORD& error);
    virtual bool ReadIdentity(const std::wstring& path, CFileId& identity, DWORD& error);
    virtual bool QueryCaseSensitive(const std::wstring& directory, bool& caseSensitive, DWORD& error);
    virtual bool GetVolume(const std::wstring& path, CVolumeInfo& info);
    virtual bool Exists(const std::wstring& path, bool& isDir, DWORD& attributes, DWORD& error);
    virtual bool ReadFile(const std::wstring& path, unsigned __int64 maxBytes, std::vector<BYTE>& data, DWORD& error);
    virtual bool WriteAtomic(const std::wstring& path, const std::string& bytes, DWORD& error);
    virtual bool CreateProbe(const std::wstring& directory, DWORD& error);

    std::map<std::wstring, std::string> Files; // exact path -> content; directories end with a sentinel stored separately
    struct CNode
    {
        CSnapshotItem Item;
        std::string Content;
        bool CaseSensitive;
        CNode() : CaseSensitive(false) {}
    };
    std::map<std::wstring, CNode> Nodes;
};

std::wstring ExtendedPath(const std::wstring& path);

} // namespace reorg
