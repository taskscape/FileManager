// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Staging and publication (C.5.7). A package is built in a hidden partial
// sibling folder whose handle is retained (no FILE_SHARE_DELETE) until it is
// verified and renamed by handle without replacement; a failed or cancelled
// build removes only what this session created.

#include <string>
#include <vector>

#include "file_system.h"
#include "outputs.h"
#include "plan.h"

namespace handoff
{

class IBuildCallbacks : public IProgress
{
public:
    // A source could not be read or a target written; true retries the file
    // from the start, false cancels the build. Called on the worker thread.
    virtual bool AskRetry(const std::wstring& item, DWORD error) = 0;
};

struct BuildInput
{
    // Snapshots owned by the worker, so the review window can keep changing its own copy.
    Spec SpecModel;
    ReviewModel Model;
    PackagePlan Plan;
    ReviewerDecisions Decisions;
    Variables Values;
    std::wstring StagingRoot;
    SYSTEMTIME Now = {}; // local time for templates
    FILETIME NowUtc = {};
    std::wstring SessionId; // GUID text
    std::wstring User, Machine, HostVersion, PluginVersion;
    std::wstring SpecPath, SpecText, Scope;
    bool KeepFailedPartial = false;
};

enum class BuildOutcome
{
    Published,
    Retained,  // complete and verified, but publication was blocked or the name was taken
    NotBuilt,
    Cancelled
};

struct BuildResult
{
    BuildOutcome Outcome = BuildOutcome::NotBuilt;
    std::wstring PackagePath;
    std::wstring PartialPath; // set when a partial folder remains
    std::wstring BuildRecordPath;
    bool BuildRecordWritten = false;
    std::wstring ManifestSha256;
    ManifestData Manifest;
    std::vector<Finding> Findings; // HO-BUILD-* and other build-time findings
};

BuildResult BuildPackage(BuildInput& input, IHandoffFileSystem& fs, IBuildCallbacks& callbacks, IImageInspector* images,
                         IPdfPageInspector* pdf, const ITextCatalog& catalog);

std::wstring PartialFolderName(const std::wstring& packageName, const std::wstring& sessionId);
const wchar_t* PartialMarkerName();

struct StalePartial
{
    std::wstring Path;
    bool Live = false; // held by a running build
};

// Partial folders (with a marker) in the staging location.
std::vector<StalePartial> FindPartialFolders(const std::wstring& stagingRoot, IHandoffFileSystem& fs);
// Deletes a non-live partial folder tree; links inside are removed as links, never followed.
bool RemovePartialFolder(const std::wstring& path, IHandoffFileSystem& fs, std::wstring& failedPath);

} // namespace handoff
