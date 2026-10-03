// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The review model (C.5.3-C.5.6): candidates selected by rules, inspection
// facts, evidence, reviewer decisions, and the findings that gate a build.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "csv.h"
#include "findings.h"
#include "inspect.h"
#include "progress.h"
#include "scanner.h"
#include "spec.h"

namespace handoff
{

typedef std::map<std::wstring, std::wstring> Variables;

struct Candidate
{
    size_t FileIndex = 0; // into ReviewModel::Files
    int RuleIndex = -1;
    std::wstring Rel, Name;
    bool Superseded = false;
    std::wstring SupersededBy; // revision that replaced this file
    std::wstring Revision;     // latestRevision capture
    bool RevisionParsed = false;
    std::wstring Key;          // expect.keyRegex capture
    std::wstring RelDir;       // below the matching include glob's static prefix

    // Inspection (C.5.4).
    bool Inspected = false;
    bool Deferred = false; // cloud placeholder: content checks happen at build
    std::wstring Format = L"unknown";
    bool FormatKnown = false;
    bool SignatureOk = true;
    DWORD ReadError = ERROR_SUCCESS;
    bool HasNamedStreams = false;
    bool PdfInspected = false;
    PdfStructure Pdf;
    bool PagesInspected = false;
    PdfPages Pages;
    bool ImageInspected = false;
    ImageFacts Image;

    // Evidence (C.4.8, C.4.9).
    bool ApprovalEvidence = false;
    bool LicenceFound = false;
    std::wstring Licence, Licensor, LicenceExpires, LicenceScope;
    std::vector<size_t> LicenceEvidence; // file indexes of sidecar evidence

    // Effective state after reviewer decisions (Evaluate).
    bool Included = true;
    bool Approved = false;
    std::vector<Finding> Findings;
};

struct ExcludedFile
{
    size_t FileIndex = 0;
    std::wstring Code; // HO-SEL-003 not included, HO-CONT-001 forbidden
};

struct RuleSummary
{
    size_t Selected = 0;
    size_t Expected = 0;
    bool HasExpected = false;
    int Errors = 0, Warnings = 0;
    bool Missing = false;
};

struct RegisterEntry
{
    bool Ok = false;
    CsvTable Table;
    std::wstring Error;
};

struct DecisionRecord
{
    std::wstring Action; // approve | exclude | include | override
    std::wstring Source; // working-relative path
    std::wstring RuleId;
    std::wstring Code;
    std::wstring Reason;
    std::wstring User;
    std::wstring Utc;
};

struct ReviewerDecisions
{
    std::set<std::wstring> Excluded;                 // Key(rel, rule)
    std::set<std::wstring> Approved;                 // Key(rel, rule)
    std::map<std::wstring, std::wstring> Overrides;  // OverrideKey(finding) -> reason
    std::vector<DecisionRecord> Log;

    static std::wstring Key(const std::wstring& rel, const std::wstring& ruleId);
    static std::wstring OverrideKey(const Finding& finding);
};

struct ReviewModel
{
    std::wstring WorkingRoot;
    std::vector<ScannedFile> Files;
    std::vector<Candidate> Candidates;
    std::vector<ExcludedFile> Excluded;
    std::vector<Finding> ScanFindings;
    std::vector<Finding> SelectionFindings;          // HO-SEL-002/007/008 found while matching
    std::map<std::wstring, RegisterEntry> Registers; // by working-relative path
    std::set<size_t> SupportFiles;                   // registers and evidence; never "not included"
    std::vector<std::vector<std::wstring>> ExpectedKeys; // per rule, empty when no expectation

    // Evaluate() output.
    std::vector<Finding> Findings;
    std::vector<RuleSummary> Rules;
};

// Selection, revisions, keys, registers, and evidence lookup (no content reads
// besides registers).
ReviewModel Assemble(const Spec& spec, const ScanResult& scan, const std::wstring& workingRoot);

// Reads format heads and inspects PDFs and raster images of non-superseded
// candidates; 'pdf' and 'images' may be null in tests or when unavailable.
void InspectCandidates(ReviewModel& model, const Spec& spec, IImageInspector* images, IPdfPageInspector* pdf,
                       IProgress& progress);

// Inspects one file (used again at build time for deferred placeholders).
void InspectOne(Candidate& candidate, const std::wstring& fullPath, const Spec& spec, IImageInspector* images,
                IPdfPageInspector* pdf);

// Recomputes every finding from the model, the specification, and reviewer decisions.
void Evaluate(ReviewModel& model, const Spec& spec, const ReviewerDecisions& decisions, const SYSTEMTIME& today,
              const ITextCatalog& catalog);

// Applies severity overrides (top-level, then rule) and reviewer overrides,
// appending non-"off" findings to 'destination'.
void ResolveFindings(std::vector<Finding>& findings, const Spec& spec, const RuleSpec* rule,
                     const ReviewerDecisions& decisions, std::vector<Finding>& destination);

// Content checks for one inspected candidate (shared by Evaluate and the build).
std::vector<Finding> CheckCandidateContent(const Candidate& candidate, const ScannedFile& file, const Spec& spec,
                                           const ITextCatalog& catalog);

// Variables with built-ins (date, specId, specName, specRevision).
Variables BuildVariables(const Spec& spec, const std::map<std::wstring, std::wstring>& values, const SYSTEMTIME& date);
// HO-SES-004 findings for missing or invalid variable values.
std::vector<Finding> ValidateVariables(const Spec& spec, const Variables& values);

std::wstring RangeText(const IntRange& range, const ITextCatalog& catalog, const std::wstring& unit = L"");
std::wstring PageSizeText(double widthMm, double heightMm, const ITextCatalog* catalog);

} // namespace handoff
