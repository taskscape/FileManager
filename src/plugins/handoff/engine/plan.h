// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Package planning (C.5.5) and build gating (C.5.6): target names and folders
// from templates, licence evidence placement, collisions, limits, free space.

#include <string>
#include <vector>

#include "review.h"

namespace handoff
{

struct PlannedFile
{
    std::wstring SourceRel;
    std::wstring TargetRel; // package-relative, '/'
    uint64_t Size = 0;
    FILETIME LastWrite = {};
    size_t FileIndex = 0;
    int CandidateIndex = -1; // -1: licence evidence
    std::wstring RuleId, Role, Format, Key, Revision;
    bool Evidence = false;
    std::vector<std::wstring> LicenceFor; // target paths of the assets this evidence covers
    bool Deferred = false;
};

struct PackagePlan
{
    std::wstring PackageName;
    bool PackageNameValid = false;
    std::vector<PlannedFile> Files;    // sorted by target path
    std::vector<std::wstring> Folders; // package-relative, parents before children
    std::vector<std::wstring> Outputs; // enabled output names (manifest.json first)
    uint64_t TotalBytes = 0;
    std::vector<Finding> Findings;     // HO-NAME-*, HO-PKG-*
};

// Expands and sanitizes package.folderName.
SanitizeResult ExpandPackageName(const Spec& spec, const Variables& variables, const SYSTEMTIME& now);

// 'freeBytes' is the free space of the staging volume, or 0 when unknown.
PackagePlan PlanPackage(const ReviewModel& model, const Spec& spec, const Variables& variables, const SYSTEMTIME& now,
                        uint64_t freeBytes, const ReviewerDecisions& decisions, const ITextCatalog& catalog);

struct Gate
{
    bool CanBuild = false;
    bool NeedsAcknowledgement = false;
    int Errors = 0, Warnings = 0, Infos = 0, Overridden = 0;
};

Gate ComputeGate(const ReviewModel& model, const PackagePlan& plan, const Spec& spec);

// The date variable drives {date:fmt}; the time of day comes from 'now'.
SYSTEMTIME TemplateDate(const Variables& variables, const SYSTEMTIME& now);

} // namespace handoff
