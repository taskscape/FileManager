// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "plan.h"
#include "text_util.h"

#include <algorithm>
#include <map>

namespace handoff
{

SYSTEMTIME TemplateDate(const Variables& variables, const SYSTEMTIME& now)
{
    SYSTEMTIME date = now;
    auto it = variables.find(L"date");
    SYSTEMTIME parsed = {};
    if (it != variables.end() && ParseIsoDate(it->second, parsed))
    {
        date.wYear = parsed.wYear;
        date.wMonth = parsed.wMonth;
        date.wDay = parsed.wDay;
    }
    return date;
}

SanitizeResult ExpandPackageName(const Spec& spec, const Variables& variables, const SYSTEMTIME& now)
{
    TemplateContext context;
    context.Variables = &variables;
    context.Date = TemplateDate(variables, now);
    return SanitizeName(spec.Package.FolderName.Expand(context), spec.Naming, false);
}

namespace
{

struct FolderResult
{
    std::vector<std::wstring> Segments;
    bool Changed = false, Truncated = false, Invalid = false, Reserved = false;
};

FolderResult SanitizeFolder(const std::wstring& raw, const NamingPolicy& naming)
{
    FolderResult result;
    for (const std::wstring& segment : Split(ToSlashes(raw), L'/'))
    {
        if (segment.empty())
            continue; // an empty variable must not create "//"
        SanitizeResult clean = SanitizeName(segment, naming, false);
        result.Changed = result.Changed || clean.Changed;
        result.Truncated = result.Truncated || clean.Truncated;
        result.Invalid = result.Invalid || clean.Invalid;
        result.Reserved = result.Reserved || clean.Reserved;
        if (!clean.Invalid)
            result.Segments.push_back(clean.Value);
    }
    return result;
}

void AddFinding(std::vector<Finding>& raw, const wchar_t* code, const std::wstring& item, std::vector<std::wstring> args,
                const std::wstring& ruleId)
{
    Finding finding = MakeFinding(code, item, std::move(args));
    finding.RuleId = ruleId;
    raw.push_back(finding);
}

} // namespace

PackagePlan PlanPackage(const ReviewModel& model, const Spec& spec, const Variables& variables, const SYSTEMTIME& now,
                        uint64_t freeBytes, const ReviewerDecisions& decisions, const ITextCatalog& catalog)
{
    PackagePlan plan;
    SanitizeResult packageName = ExpandPackageName(spec, variables, now);
    plan.PackageName = packageName.Value;
    plan.PackageNameValid = !packageName.Invalid && !packageName.Reserved;
    SYSTEMTIME date = TemplateDate(variables, now);
    std::vector<std::vector<Finding>> raw(spec.Rules.size() + 1); // last slot: package-wide

    // Pass 1: folders, so {seq} can number files per target folder.
    struct Pending
    {
        size_t Candidate;
        std::wstring Folder;
    };
    std::vector<Pending> pending;
    for (size_t c = 0; c < model.Candidates.size(); c++)
    {
        const Candidate& candidate = model.Candidates[c];
        if (!candidate.Included)
            continue;
        const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
        TemplateContext context;
        context.Variables = &variables;
        context.Date = date;
        context.HasFile = true;
        context.Stem = StemOf(candidate.Name);
        context.Ext = ExtensionOf(candidate.Name);
        context.Name = candidate.Name;
        context.Key = candidate.Key;
        context.Rev = candidate.Revision;
        context.Rule = rule.Id;
        context.RelDir = candidate.RelDir;
        std::wstring rawFolder = rule.Target.Folder.Expand(context);
        if (rule.Target.KeepSubfolders && !candidate.RelDir.empty())
            rawFolder = rawFolder.empty() ? candidate.RelDir : rawFolder + L"/" + candidate.RelDir;
        FolderResult folder = SanitizeFolder(rawFolder, spec.Naming);
        if (folder.Invalid)
            AddFinding(raw[(size_t)candidate.RuleIndex], L"HO-NAME-011", candidate.Rel, {}, rule.Id);
        if (folder.Reserved)
            AddFinding(raw[(size_t)candidate.RuleIndex], L"HO-NAME-014", candidate.Rel, {rawFolder}, rule.Id);
        pending.push_back(Pending{c, Join(folder.Segments, L"/")});
    }
    std::stable_sort(pending.begin(), pending.end(), [&](const Pending& a, const Pending& b) {
        return CompareNoCase(model.Candidates[a.Candidate].Rel, model.Candidates[b.Candidate].Rel) < 0;
    });
    std::map<std::wstring, int> sequence;

    for (const Pending& item : pending)
    {
        const Candidate& candidate = model.Candidates[item.Candidate];
        const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
        std::vector<Finding>& findings = raw[(size_t)candidate.RuleIndex];
        TemplateContext context;
        context.Variables = &variables;
        context.Date = date;
        context.HasFile = true;
        context.Stem = StemOf(candidate.Name);
        context.Ext = ExtensionOf(candidate.Name);
        context.Name = candidate.Name;
        context.Key = candidate.Key;
        context.Rev = candidate.Revision;
        context.Rule = rule.Id;
        context.RelDir = candidate.RelDir;
        context.Seq = ++sequence[FoldKey(item.Folder)];
        std::wstring rawName = rule.Target.Name.Expand(context);
        SanitizeResult name = SanitizeName(rawName, spec.Naming, true);
        if (name.Invalid)
        {
            AddFinding(findings, L"HO-NAME-011", candidate.Rel, {}, rule.Id);
            continue;
        }
        if (name.Reserved)
            AddFinding(findings, L"HO-NAME-014", candidate.Rel, {name.Value}, rule.Id);
        if (name.Truncated)
            AddFinding(findings, L"HO-NAME-015", candidate.Rel, {NumberText(spec.Naming.MaxNameLength)}, rule.Id);
        PlannedFile file;
        file.SourceRel = candidate.Rel;
        file.TargetRel = RelJoin(item.Folder, name.Value);
        if (name.Changed)
            AddFinding(findings, L"HO-NAME-010", candidate.Rel, {rawName, name.Value}, rule.Id);
        const ScannedFile& scanned = model.Files[candidate.FileIndex];
        file.Size = scanned.Size;
        file.LastWrite = scanned.LastWrite;
        file.FileIndex = candidate.FileIndex;
        file.CandidateIndex = (int)item.Candidate;
        file.RuleId = rule.Id;
        file.Role = rule.Role;
        file.Format = candidate.Format;
        file.Key = candidate.Key;
        file.Revision = candidate.Revision;
        file.Deferred = candidate.Deferred;
        plan.Files.push_back(file);
    }

    // Licence evidence travels with its assets; one file is copied once (C.4.9).
    std::map<size_t, size_t> evidenceIndex; // file index -> plan index
    size_t assetCount = plan.Files.size();
    for (size_t f = 0; f < assetCount; f++)
    {
        const PlannedFile asset = plan.Files[f];
        const Candidate& candidate = model.Candidates[(size_t)asset.CandidateIndex];
        const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
        if (!rule.License.Present || !rule.License.IncludeEvidence)
            continue;
        for (size_t evidence : candidate.LicenceEvidence)
        {
            auto existing = evidenceIndex.find(evidence);
            if (existing != evidenceIndex.end())
            {
                plan.Files[existing->second].LicenceFor.push_back(asset.TargetRel);
                continue;
            }
            std::wstring folder = ParentOf(asset.TargetRel);
            if (rule.License.HasEvidenceFolder)
            {
                TemplateContext context;
                context.Variables = &variables;
                context.Date = date;
                folder = Join(SanitizeFolder(rule.License.EvidenceFolder.Expand(context), spec.Naming).Segments, L"/");
            }
            const ScannedFile& scanned = model.Files[evidence];
            SanitizeResult name = SanitizeName(scanned.Name, spec.Naming, true);
            if (name.Invalid)
            {
                AddFinding(raw[(size_t)candidate.RuleIndex], L"HO-NAME-011", scanned.Rel, {}, rule.Id);
                continue;
            }
            PlannedFile file;
            file.SourceRel = scanned.Rel;
            file.TargetRel = RelJoin(folder, name.Value);
            file.Size = scanned.Size;
            file.LastWrite = scanned.LastWrite;
            file.FileIndex = evidence;
            file.RuleId = rule.Id;
            file.Role = L"licence-evidence";
            std::vector<unsigned char> head;
            DWORD error = ERROR_SUCCESS;
            FormatDetection detection;
            if (!scanned.CloudPlaceholder &&
                ReadHead(PathJoin(model.WorkingRoot, ToBackslashes(scanned.Rel)), FormatHeadBytes, head, error))
                detection = DetectFormat(scanned.Name, head.data(), head.size(), spec.CustomFormats);
            file.Format = detection.Known ? detection.Id : L"unknown";
            file.Evidence = true;
            file.Deferred = scanned.CloudPlaceholder;
            file.LicenceFor.push_back(asset.TargetRel);
            evidenceIndex[evidence] = plan.Files.size();
            plan.Files.push_back(file);
        }
    }

    std::sort(plan.Files.begin(), plan.Files.end(),
              [](const PlannedFile& a, const PlannedFile& b) { return CompareNoCase(a.TargetRel, b.TargetRel) < 0; });

    plan.Outputs.push_back(spec.Package.ManifestJson);
    if (!spec.Package.ManifestCsv.empty())
        plan.Outputs.push_back(spec.Package.ManifestCsv);
    if (!spec.Package.Contents.empty())
        plan.Outputs.push_back(spec.Package.Contents);

    // Collisions after NFC and case folding, including file-versus-folder and outputs (C.5.5).
    std::map<std::wstring, std::vector<size_t>> byTarget;
    std::map<std::wstring, std::wstring> folders;
    for (size_t f = 0; f < plan.Files.size(); f++)
    {
        byTarget[FoldKey(plan.Files[f].TargetRel)].push_back(f);
        std::wstring folder = ParentOf(plan.Files[f].TargetRel);
        while (!folder.empty())
        {
            folders[FoldKey(folder)] = folder;
            folder = ParentOf(folder);
        }
    }
    for (const auto& entry : byTarget)
    {
        bool outputClash = false;
        for (const std::wstring& output : plan.Outputs)
            outputClash = outputClash || FoldKey(output) == entry.first;
        bool folderClash = folders.count(entry.first) != 0;
        if (entry.second.size() < 2 && !outputClash && !folderClash)
            continue;
        for (size_t f : entry.second)
        {
            const PlannedFile& file = plan.Files[f];
            size_t slot = spec.Rules.size();
            for (size_t r = 0; r < spec.Rules.size(); r++)
                if (spec.Rules[r].Id == file.RuleId)
                    slot = r;
            AddFinding(raw[slot], L"HO-NAME-012", file.SourceRel, {file.TargetRel}, file.RuleId);
        }
    }
    for (const auto& entry : folders)
        plan.Folders.push_back(entry.second);
    std::sort(plan.Folders.begin(), plan.Folders.end(), [](const std::wstring& a, const std::wstring& b) {
        size_t depthA = std::count(a.begin(), a.end(), L'/'), depthB = std::count(b.begin(), b.end(), L'/');
        return depthA != depthB ? depthA < depthB : CompareNoCase(a, b) < 0;
    });

    for (const PlannedFile& file : plan.Files)
    {
        plan.TotalBytes += file.Size;
        if ((int64_t)file.TargetRel.size() > spec.Package.MaxRelativePathLength)
        {
            size_t slot = spec.Rules.size();
            for (size_t r = 0; r < spec.Rules.size(); r++)
                if (spec.Rules[r].Id == file.RuleId)
                    slot = r;
            AddFinding(raw[slot], L"HO-NAME-013", file.SourceRel,
                       {NumberText((int64_t)file.TargetRel.size()), NumberText(spec.Package.MaxRelativePathLength)}, file.RuleId);
        }
    }
    std::vector<Finding>& package = raw[spec.Rules.size()];
    if ((int64_t)plan.Files.size() > spec.Package.MaxFiles)
        AddFinding(package, L"HO-PKG-001", L"", {NumberText((int64_t)plan.Files.size()), NumberText(spec.Package.MaxFiles)}, L"");
    if (spec.Package.HasMaxTotalBytes && plan.TotalBytes > spec.Package.MaxTotalBytes)
        AddFinding(package, L"HO-PKG-002", L"", {FormatSize(plan.TotalBytes), FormatSize(spec.Package.MaxTotalBytes)}, L"");
    uint64_t needed = plan.TotalBytes + plan.TotalBytes / 20 + 16 * 1024 * 1024;
    if (freeBytes != 0 && freeBytes < needed)
        AddFinding(package, L"HO-PKG-003", L"", {FormatSize(needed)}, L"");
    (void)catalog;

    for (size_t slot = 0; slot < raw.size(); slot++)
    {
        const RuleSpec* rule = slot < spec.Rules.size() ? &spec.Rules[slot] : nullptr;
        ResolveFindings(raw[slot], spec, rule, decisions, plan.Findings);
    }
    return plan;
}

Gate ComputeGate(const ReviewModel& model, const PackagePlan& plan, const Spec& spec)
{
    Gate gate;
    auto count = [&](const std::vector<Finding>& findings) {
        for (const Finding& finding : findings)
        {
            if (finding.Sev == Severity::Error)
            {
                if (finding.Overridden)
                    gate.Overridden++;
                else
                    gate.Errors++;
            }
            else if (finding.Sev == Severity::Warning)
                gate.Warnings++;
            else if (finding.Sev == Severity::Info)
                gate.Infos++;
        }
    };
    count(model.Findings);
    count(plan.Findings);
    // A package with nothing to deliver is never built.
    gate.CanBuild = gate.Errors == 0 && plan.PackageNameValid && !plan.Files.empty();
    gate.NeedsAcknowledgement = gate.Warnings > 0 && spec.RequireWarningAcknowledgement;
    return gate;
}

} // namespace handoff
