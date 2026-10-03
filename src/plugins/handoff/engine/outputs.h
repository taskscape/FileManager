// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Package outputs (C.7): manifest.json (authoritative, LF, no BOM),
// manifest.csv (BOM, CRLF, formula guard), CONTENTS.txt (BOM, CRLF), and the
// internal build record written beside the package.

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "findings.h"

namespace handoff
{

struct ManifestFile
{
    std::wstring Path;
    uint64_t Bytes = 0;
    std::wstring Sha256;
    std::wstring ModifiedUtc;
    std::wstring Rule, RuleTitle, Role, Format, FormatLabel, Key, Revision;
    bool HasPdf = false;
    std::wstring PdfVersion;
    int64_t Pages = -1;                  // -1 unknown
    std::vector<std::wstring> PageSizes; // manifest tokens ("A4 portrait")
    std::vector<std::wstring> PageSizesLocalized;
    bool HasImage = false;
    uint32_t Width = 0, Height = 0;
    bool DpiKnown = false;
    double DpiX = 0, DpiY = 0;
    std::wstring ColorModel;
    std::wstring Approval; // evidence | confirmed | evidence+confirmed
    bool HasLicence = false;
    std::wstring Licence, Licensor, LicenceExpires;
    std::vector<std::wstring> LicenceEvidence;
    std::vector<std::wstring> LicenceFor;
    std::wstring Source; // working-relative; only with includeSourcePaths
};

struct ManifestOutput
{
    std::wstring Path;
    uint64_t Bytes = 0;
    std::wstring Sha256;
};

struct ManifestData
{
    std::wstring PackageName, CreatedUtc;
    std::vector<std::pair<std::wstring, std::wstring>> Variables;
    std::wstring SpecId, SpecName, SpecRevision, SpecSha256;
    std::wstring GeneratorVersion, HostVersion;
    std::wstring Status; // verified | verifiedWithWarnings
    int Errors = 0, Warnings = 0, Overrides = 0;
    std::vector<ManifestOutput> Outputs;
    std::vector<ManifestFile> Files;
    std::vector<Finding> Findings;
    uint64_t TotalBytes() const;
};

std::string ManifestJsonText(const ManifestData& data, const ITextCatalog& catalog);
std::string ManifestCsvText(const ManifestData& data);

struct ContentsOptions
{
    std::wstring Title;
    std::vector<std::wstring> Notes;
    bool GroupByRule = false;
    std::vector<std::pair<std::wstring, std::wstring>> RuleOrder; // id, title
    std::wstring DateText;
    std::wstring JsonName, CsvName; // CsvName empty when disabled
};

std::string ContentsText(const ManifestData& data, const ContentsOptions& options, const ITextCatalog& catalog);

struct BuildRecordDecision
{
    std::wstring Action, Source, RuleId, Code, Reason, User, Utc;
};

struct BuildRecordData
{
    std::wstring PackageName, PackagePath, ManifestSha256;
    std::wstring SessionId, User, Machine, StartedUtc, FinishedUtc, HostVersion, PluginVersion;
    std::wstring WorkingRoot, Scope;
    std::wstring SpecPath, SpecSha256, SpecText;
    std::vector<BuildRecordDecision> Decisions;
    std::vector<std::pair<std::wstring, std::pair<std::wstring, std::wstring>>> Files; // source -> (target, sha)
    std::vector<Finding> Findings;
};

std::string BuildRecordText(const BuildRecordData& data, const ITextCatalog& catalog);

} // namespace handoff
