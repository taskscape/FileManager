// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "snapshot.h"

namespace reorg
{

const CSnapshotItem* CSnapshot::Find(const std::wstring& path) const
{
    std::map<std::wstring, CSnapshotItem>::const_iterator it = Items.find(path);
    if (it != Items.end())
        return &it->second;
    for (std::map<std::wstring, CSnapshotItem>::const_iterator scan = Items.begin(); scan != Items.end(); ++scan)
    {
        if (PathsEqual(scan->first, path, false))
            return &scan->second;
    }
    return NULL;
}

void CSnapshot::Add(const CSnapshotItem& item)
{
    Items[item.Path] = item;
    if (!item.ParentPath.empty())
        Children[item.ParentPath].push_back(item.Path);
}

bool CSnapshot::CaptureDirectory(IFileSystemProbe& probe, const std::wstring& directory, bool recursive, bool identities, const CCancellation& cancel)
{
    if (cancel.IsCancelled())
        return false;
    std::wstring normalized;
    std::wstring errorText;
    if (!NormalizePath(directory, normalized, errorText))
    {
        LastError = ERROR_INVALID_NAME;
        return false;
    }
    if (Items.find(normalized) == Items.end())
    {
        CSnapshotItem self;
        self.Path = normalized;
        self.ParentPath = ParentPath(normalized);
        self.Name = LeafName(normalized);
        self.IsDir = true;
        self.Attributes = FILE_ATTRIBUTE_DIRECTORY;
        CVolumeInfo volume;
        if (probe.GetVolume(normalized, volume) && volume.Ok)
        {
            self.VolumeSerial = volume.Serial;
            self.FileSystem = volume.FileSystem;
            self.PersistentFileIds = volume.PersistentFileIds;
            self.ReadOnlyVolume = volume.ReadOnly;
            self.MaxComponent = volume.MaxComponent;
            self.FreeBytes = volume.FreeBytes;
        }
        bool caseSensitive = false;
        DWORD caseError = 0;
        if (probe.QueryCaseSensitive(normalized, caseSensitive, caseError))
        {
            self.CaseSensitive = caseSensitive;
            self.CaseSensitiveKnown = true;
        }
        Add(self);
    }
    std::vector<CSnapshotItem> children;
    DWORD error = 0;
    if (!probe.Enumerate(normalized, children, error))
    {
        LastError = error;
        return false;
    }
    for (size_t i = 0; i < children.size(); ++i)
    {
        if (cancel.IsCancelled())
            return false;
        CSnapshotItem item = children[i];
        const CSnapshotItem* parent = Find(normalized);
        if (parent)
        {
            item.VolumeSerial = parent->VolumeSerial;
            item.FileSystem = parent->FileSystem;
            item.PersistentFileIds = parent->PersistentFileIds;
            item.ReadOnlyVolume = parent->ReadOnlyVolume;
            item.MaxComponent = parent->MaxComponent;
            item.FreeBytes = parent->FreeBytes;
        }
        if (identities)
        {
            CFileId id;
            DWORD idError = 0;
            if (probe.ReadIdentity(item.Path, id, idError) && id.Valid)
            {
                item.HasFileId = true;
                memcpy(item.FileId, id.FileId, 16);
                item.VolumeSerial = id.VolumeSerial;
            }
        }
        if (item.IsDir)
        {
            bool caseSensitive = false;
            DWORD caseError = 0;
            if (probe.QueryCaseSensitive(item.Path, caseSensitive, caseError))
            {
                item.CaseSensitive = caseSensitive;
                item.CaseSensitiveKnown = true;
            }
        }
        Add(item);
        if (recursive && item.IsDir && item.ReparseTag == 0)
        {
            if (!CaptureDirectory(probe, item.Path, true, identities, cancel))
                return false;
        }
    }
    return true;
}

bool CSnapshot::Capture(IFileSystemProbe& probe, const std::vector<std::wstring>& roots, bool identities, const CCancellation& cancel)
{
    for (size_t i = 0; i < roots.size(); ++i)
    {
        if (!CaptureDirectory(probe, roots[i], true, identities, cancel))
            return false;
    }
    return true;
}

} // namespace reorg
