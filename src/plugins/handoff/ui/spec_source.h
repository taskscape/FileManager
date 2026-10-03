// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Where specifications come from (handoff-spec.md A.5.3) and how they are
// read: the nearest .handoff folder above the working root, the configured
// library folder, then the recent list; each file is parsed with the bounded
// validator and hashed exactly as stored.

#include <string>
#include <vector>

#include "spec.h"

struct SpecChoice
{
    std::wstring Path;
    int OriginTextId = 0; // IDS_SPEC_ORIGIN_*
};

// Disk access: call on a plug-in UI thread or worker, never the main thread.
// Recent entries whose files no longer exist are pruned from the configuration.
std::vector<SpecChoice> DiscoverSpecs(const std::wstring& workingRoot);

struct LoadedSpec
{
    bool Read = false; // the file could be read (findings may still contain errors)
    DWORD Error = ERROR_SUCCESS;
    std::wstring Path;
    std::vector<unsigned char> Bytes;
    std::wstring Text; // UTF-8 decoded for the build record
    handoff::SpecLoadResult Result;
};

LoadedSpec LoadSpecFile(const std::wstring& path);
LoadedSpec LoadSpecText(const std::wstring& path, const std::string& utf8);

// *.handoff.json files directly inside 'folder'.
std::vector<std::wstring> ListSpecFiles(const std::wstring& folder);

// "Client delivery: approved PDFs, ... \n 4 rules · revision 2026.1" or the first error.
std::wstring SpecSummary(const LoadedSpec& spec);
