// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Working-material enumeration (C.5.2): iterative, long-path aware, bounded,
// and never following links, so reviewing cannot escape the working folder or
// hydrate online-only files.

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

#include "findings.h"
#include "progress.h"

namespace handoff
{

struct ScannedFile
{
    std::wstring Rel;  // relative to the working root, '/' separators
    std::wstring Name;
    uint64_t Size = 0;
    DWORD Attributes = 0;
    FILETIME LastWrite = {};
    bool CloudPlaceholder = false; // content not local; inspection deferred to build
};

struct ScanInput
{
    std::wstring WorkingRoot;                // absolute path
    bool SelectionOnly = false;
    std::vector<std::wstring> SelectedNames; // items directly in the working root
    size_t MaxEntries = 1000000;
    int MaxDepth = 64;
};

struct ScanResult
{
    std::vector<ScannedFile> Files;
    std::vector<Finding> Findings; // HO-SCAN-*
    bool Cancelled = false;
    bool LimitExceeded = false;
};

ScanResult ScanWorkingMaterial(const ScanInput& input, IProgress& progress);

// Cloud-file reparse tags (OneDrive and other sync providers) describe content
// placement, not links, so folders carrying them are still traversed.
bool IsCloudReparseTag(DWORD tag);

} // namespace handoff
