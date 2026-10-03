// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Typed model of a delivery specification (*.handoff.json, C.4) with all
// defaults applied, plus the validator that reports HO-SPEC-* findings with
// line, column, and JSON Pointer.

#include <stdint.h>
#include <string>
#include <vector>

#include "findings.h"
#include "formats.h"
#include "json.h"
#include "name_template.h"
#include "patterns.h"

namespace handoff
{

struct IntRange
{
    bool HasMin = false, HasMax = false;
    int64_t Min = 0, Max = 0;
    bool Contains(int64_t value) const { return (!HasMin || value >= Min) && (!HasMax || value <= Max); }
    bool IsSet() const { return HasMin || HasMax; }
};

struct VariableDef
{
    std::wstring Name, Label, Default;
    bool Required = false;
    SafeRegex Pattern;
    std::vector<std::wstring> Choices;
};

struct RegisterCsvSpec
{
    bool Present = false;
    std::wstring Path; // working-root relative, '/'
    // expect: Column/WhereColumn/WhereEquals; approval: FileColumn/StatusColumn/ApprovedValues;
    // licence: FileColumn/LicenceColumn/LicensorColumn/ExpiresColumn/ScopeColumn/AllowedScopes
    std::wstring Column, WhereColumn, WhereEquals;
    std::wstring FileColumn, StatusColumn, LicenceColumn, LicensorColumn, ExpiresColumn, ScopeColumn;
    std::vector<std::wstring> ApprovedValues, AllowedScopes;
};

struct LatestRevisionSpec
{
    bool Present = false;
    SafeRegex Regex;
    bool PerFolder = false; // scope "folder"
    std::vector<std::wstring> PrefixOrder;
};

struct SelectSpec
{
    std::vector<Glob> Include, Exclude;
    SafeRegex NameRegex, PathRegex;
    bool Shared = false;
    LatestRevisionSpec Latest;
};

struct ExpectSpec
{
    bool Present = false;
    SafeRegex KeyRegex;
    std::vector<std::wstring> Keys;
    RegisterCsvSpec Register;
    bool AllowUnexpected = false;
};

struct PageSizeSpec
{
    std::wstring Name; // empty for explicit sizes
    double WidthMm = 0, HeightMm = 0;
};

struct PdfSpec
{
    bool Present = false;
    std::wstring VersionMin, VersionMax;
    IntRange Pages;
    std::vector<PageSizeSpec> PageSizes;
    std::wstring Orientation = L"any";
    double ToleranceMm = 2.0;
    bool AllPagesSameSize = false;
    bool AllowPasswordProtected = false;
};

struct ImageSpec
{
    bool Present = false;
    IntRange Width, Height, LongEdge, ShortEdge, BitDepth, Frames;
    std::vector<std::pair<int64_t, int64_t>> Exact;
    bool HasAspect = false;
    double Aspect = 0, AspectTolerance = 0.01;
    std::wstring AspectText;
    bool HasDpiMin = false;
    double DpiMin = 0;
    std::vector<std::wstring> ColorModels;
    std::wstring Alpha = L"any";
    bool ForbidGps = false;
};

struct ApprovalSpec
{
    std::wstring Mode = L"none"; // none | evidence | confirm | evidenceAndConfirm
    SafeRegex PathRegex, NameRegex;
    std::vector<std::wstring> Sidecar;
    RegisterCsvSpec Register;
    bool NeedsEvidence() const { return Mode == L"evidence" || Mode == L"evidenceAndConfirm"; }
    bool NeedsConfirm() const { return Mode == L"confirm" || Mode == L"evidenceAndConfirm"; }
};

struct LicenseSpec
{
    bool Present = false;
    bool Required = false;
    std::vector<std::wstring> Sidecar;
    RegisterCsvSpec Register;
    bool IncludeEvidence = false;
    NameTemplate EvidenceFolder;
    bool HasEvidenceFolder = false;
    int ExpiryWarningDays = 30;
};

struct TargetSpec
{
    NameTemplate Folder;
    NameTemplate Name;
    bool KeepSubfolders = false;
};

struct RuleSpec
{
    std::wstring Id, Title, Description, Role = L"deliverable";
    bool Required = false;
    IntRange Count;
    SelectSpec Select;
    ExpectSpec Expect;
    std::vector<std::wstring> Formats;
    bool HasFileSizeMin = false, HasFileSizeMax = false;
    uint64_t FileSizeMin = 0, FileSizeMax = 0;
    PdfSpec Pdf;
    ImageSpec Image;
    ApprovalSpec Approval;
    LicenseSpec License;
    TargetSpec Target;
    SeverityMap Severity;
};

struct PackageSpec
{
    NameTemplate FolderName;
    std::wstring ManifestJson = L"manifest.json";
    std::wstring ManifestCsv = L"manifest.csv"; // empty: disabled
    std::wstring Contents = L"CONTENTS.txt";    // empty: disabled
    NameTemplate ContentsTitle;
    bool GroupByRule = false;
    std::vector<NameTemplate> Notes;
    int64_t MaxFiles = 10000;
    bool HasMaxTotalBytes = false;
    uint64_t MaxTotalBytes = 0;
    int64_t MaxRelativePathLength = 200;
    bool PreserveModifiedTime = true;
    bool BuildRecordBeside = true;
    bool IncludeSourcePaths = false;
};

struct ContentSpec
{
    bool HasAllowedFormats = false;
    std::vector<std::wstring> AllowedFormats;
    std::vector<Glob> Forbid;
    bool HiddenAllowed = false, SystemAllowed = false;
    std::wstring EmptyFiles = L"forbid"; // forbid | warn | allow
    bool HasMaxFileSize = false;
    uint64_t MaxFileSize = 0;
    std::wstring Duplicates = L"warn";   // warn | forbid | allow
    std::wstring Unassigned = L"info";   // info | warn | off
};

struct Spec
{
    int64_t Version = 1;
    std::wstring Id, Name, Revision, Description;
    std::vector<VariableDef> Variables;
    PackageSpec Package;
    NamingPolicy Naming;
    ContentSpec Content;
    std::vector<FormatDef> CustomFormats;
    std::vector<RuleSpec> Rules;
    SeverityMap Severity;
    bool AllowErrorOverride = false;
    bool RequireWarningAcknowledgement = true;
    std::wstring Sha256; // of the exact file bytes, filled by the loader

    const RuleSpec* FindRule(const std::wstring& id) const;
};

struct SpecLoadResult
{
    bool Ok = false; // no error-severity findings
    Spec Model;
    std::vector<Finding> Findings; // HO-SPEC-* with Line/Column/Pointer
};

// Parses and validates specification bytes (UTF-8 JSON). The catalogue localizes
// composed fragments such as "did you mean" hints.
SpecLoadResult ParseSpecification(const unsigned char* data, size_t length);
SpecLoadResult ParseSpecification(const unsigned char* data, size_t length, const ITextCatalog& catalog);

// Named page sizes in millimetres (portrait); false when unknown.
bool LookupPageSize(const std::wstring& name, double& widthMm, double& heightMm);
// Named page size for a measured page (within 'toleranceMm'), "" when none.
std::wstring NamePageSize(double widthMm, double heightMm, double toleranceMm, bool* landscape);

const std::vector<std::wstring>& KnownTopLevelMembers();

} // namespace handoff
