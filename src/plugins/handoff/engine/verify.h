// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Package verification (C.5.8): integrity of every listed file, unlisted
// content, manifest authentication against the build record, and a re-check
// of the specification's rules against the package itself.

#include <map>
#include <string>
#include <vector>

#include "findings.h"
#include "inspect.h"
#include "outputs.h"
#include "progress.h"
#include "spec.h"

namespace handoff
{

struct ManifestEntry
{
    std::wstring Path, Sha256, Rule, Role, Format, Key, Approval;
    uint64_t Bytes = 0;
};

struct ParsedManifest
{
    std::wstring PackageName, CreatedUtc;
    std::wstring SpecId, SpecName, SpecRevision, SpecSha256;
    std::vector<ManifestOutput> Outputs;
    std::vector<ManifestEntry> Files;
};

// Strict reader for "handoffManifest": 1 documents (C.7.1).
bool ParseManifest(const unsigned char* data, size_t length, ParsedManifest& manifest, std::wstring& error);
// manifest.json, or the single root *.json (at most 10 checked) declaring handoffManifest 1; empty when none.
std::wstring FindManifest(const std::wstring& packageRoot);

struct BuildRecordInfo
{
    bool Found = false;
    std::wstring Path;
    std::wstring ManifestSha256, SpecSha256, SpecPath, SpecText;
};

// Reads "<parent>\<package folder name>.handoff-build.json" beside the package.
BuildRecordInfo ReadBuildRecordBeside(const std::wstring& packageRoot);

struct VerifyInput
{
    std::wstring PackageRoot;
    const Spec* SpecModel = nullptr; // null when no specification was found
    std::wstring SpecOrigin;
    BuildRecordInfo BuildRecord;
};

struct VerifyResult
{
    bool ManifestOk = false;
    std::wstring ManifestPath;
    ParsedManifest Manifest;
    bool ManifestAuthenticated = false;
    size_t FilesChecked = 0;
    std::vector<Finding> Findings;
    std::wstring Status; // verified | verifiedWithWarnings | failed
};

VerifyResult VerifyPackage(const VerifyInput& input, IProgress& progress, IImageInspector* images, IPdfPageInspector* pdf,
                           const ITextCatalog& catalog);

// Plain-text reports (UTF-8 with BOM, CRLF) for "Save report..." (T6.9).
std::string ReportText(const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& header,
                       const std::vector<std::pair<std::wstring, std::vector<std::wstring>>>& sections);
std::wstring SeverityLabel(Severity severity, const ITextCatalog& catalog);
std::wstring FindingLine(const Finding& finding, const ITextCatalog& catalog);

} // namespace handoff
