// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "fsprobe.h"

namespace reorg
{

class CSnapshot
{
public:
    std::map<std::wstring, CSnapshotItem> Items;
    std::map<std::wstring, std::vector<std::wstring>> Children;
    DWORD LastError;
    std::wstring LastErrorPath; // the directory whose capture failed, for the analysis error

    CSnapshot() : LastError(ERROR_SUCCESS) {}

    const CSnapshotItem* Find(const std::wstring& path) const;
    void Add(const CSnapshotItem& item);
    bool Capture(IFileSystemProbe& probe, const std::vector<std::wstring>& roots, bool identities, const CCancellation& cancel);
    bool CaptureDirectory(IFileSystemProbe& probe, const std::wstring& directory, bool recursive, bool identities, const CCancellation& cancel);
};

} // namespace reorg
