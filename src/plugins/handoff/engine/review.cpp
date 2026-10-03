// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "review.h"
#include "text_util.h"

#include <algorithm>
#include <math.h>

namespace handoff
{

std::wstring ReviewerDecisions::Key(const std::wstring& rel, const std::wstring& ruleId)
{
    return FoldKey(rel) + L"|" + ruleId;
}

std::wstring ReviewerDecisions::OverrideKey(const Finding& finding)
{
    return finding.Code + L"|" + FoldKey(finding.Item) + L"|" + finding.RuleId;
}

std::wstring RangeText(const IntRange& range, const ITextCatalog& catalog, const std::wstring& unit)
{
    std::wstring min = NumberText(range.Min) + unit, max = NumberText(range.Max) + unit;
    if (range.HasMin && range.HasMax)
        return range.Min == range.Max ? min : LabelText(catalog, Label::RANGE_BETWEEN, {min, max});
    if (range.HasMin)
        return LabelText(catalog, Label::RANGE_MIN, {min});
    if (range.HasMax)
        return LabelText(catalog, Label::RANGE_MAX, {max});
    return std::wstring();
}

std::wstring PageSizeText(double widthMm, double heightMm, const ITextCatalog* catalog)
{
    bool landscape = false;
    std::wstring name = NamePageSize(widthMm, heightMm, 2.0, &landscape);
    std::wstring orientation;
    if (catalog != nullptr)
        orientation = LabelText(*catalog, landscape ? Label::LANDSCAPE : Label::PORTRAIT);
    else
        orientation = landscape ? L"landscape" : L"portrait"; // manifests keep English tokens
    if (!name.empty())
        return name + L" " + orientation;
    return DecimalText(widthMm, 1) + L" × " + DecimalText(heightMm, 1) + L" mm";
}

namespace
{

struct FileIndex
{
    std::map<std::wstring, size_t> ByPath;                 // FoldKey(rel)
    std::map<std::wstring, std::vector<size_t>> ByFolder; // FoldKey(parent rel)
};

FileIndex BuildIndex(const std::vector<ScannedFile>& files)
{
    FileIndex index;
    for (size_t i = 0; i < files.size(); i++)
    {
        index.ByPath[FoldKey(files[i].Rel)] = i;
        index.ByFolder[FoldKey(ParentOf(files[i].Rel))].push_back(i);
    }
    return index;
}

// Splits "C01" into ("C", "01"); revision values without letters have an empty prefix.
void SplitRevision(const std::wstring& value, std::wstring& prefix, std::wstring& rest)
{
    size_t i = 0;
    while (i < value.size() && iswalpha(value[i]))
        i++;
    prefix = value.substr(0, i);
    rest = value.substr(i);
}

bool AllDigits(const std::wstring& text)
{
    return !text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos;
}

int CompareNatural(const std::wstring& a, const std::wstring& b)
{
    if (AllDigits(a) && AllDigits(b))
    {
        std::wstring x = a.substr((std::min)(a.find_first_not_of(L'0'), a.size()));
        std::wstring y = b.substr((std::min)(b.find_first_not_of(L'0'), b.size()));
        if (x.size() != y.size())
            return x.size() < y.size() ? -1 : 1;
        return x.compare(y) < 0 ? -1 : (x == y ? 0 : 1);
    }
    // Revision letters: A < B < ... < Z < AA (shorter sorts first).
    std::wstring x = UpperInvariant(a), y = UpperInvariant(b);
    if (x.size() != y.size())
        return x.size() < y.size() ? -1 : 1;
    return x.compare(y) < 0 ? -1 : (x == y ? 0 : 1);
}

// <0 when 'a' is older than 'b'. 'listed' reports whether a prefix is in prefixOrder.
int CompareRevision(const std::wstring& a, const std::wstring& b, const std::vector<std::wstring>& prefixOrder)
{
    if (prefixOrder.empty())
        return CompareNatural(a, b);
    std::wstring pa, ra, pb, rb;
    SplitRevision(a, pa, ra);
    SplitRevision(b, pb, rb);
    auto rank = [&](const std::wstring& prefix) {
        for (size_t i = 0; i < prefixOrder.size(); i++)
            if (EqualsNoCase(prefixOrder[i], prefix))
                return (int)i;
        return -1; // unlisted prefixes rank lowest
    };
    int rankA = rank(pa), rankB = rank(pb);
    if (rankA != rankB)
        return rankA < rankB ? -1 : 1;
    return CompareNatural(ra, rb);
}

bool PrefixListed(const std::wstring& value, const std::vector<std::wstring>& prefixOrder)
{
    if (prefixOrder.empty())
        return true;
    std::wstring prefix, rest;
    SplitRevision(value, prefix, rest);
    for (const std::wstring& candidate : prefixOrder)
        if (EqualsNoCase(candidate, prefix))
            return true;
    return false;
}

std::wstring FullPath(const std::wstring& root, const std::wstring& rel)
{
    return PathJoin(root, ToBackslashes(rel));
}

void LoadRegister(ReviewModel& model, const FileIndex& index, const RegisterCsvSpec& spec)
{
    if (!spec.Present || model.Registers.count(spec.Path) != 0)
        return;
    RegisterEntry& entry = model.Registers[spec.Path];
    auto found = index.ByPath.find(FoldKey(spec.Path));
    if (found != index.ByPath.end())
        model.SupportFiles.insert(found->second);
    std::vector<unsigned char> data;
    DWORD error = ERROR_SUCCESS;
    if (!ReadWholeFile(FullPath(model.WorkingRoot, spec.Path), 4 * 1024 * 1024, data, error))
    {
        entry.Error = spec.Path + L": " + Win32ErrorText(error);
        return;
    }
    std::wstring parseError;
    if (!ParseCsv(data.data(), data.size(), entry.Table, parseError))
    {
        entry.Error = spec.Path + L": " + parseError;
        return;
    }
    entry.Ok = true;
}

// Register rows name files by working-relative path, or by file name when the cell has no '/'.
bool RowNamesFile(const std::wstring& cell, const Candidate& candidate)
{
    std::wstring value = Trim(ToSlashes(cell));
    if (value.empty())
        return false;
    if (value.find(L'/') != std::wstring::npos)
        return EqualsNoCase(value, candidate.Rel);
    return EqualsNoCase(value, candidate.Name);
}

// Resolves sidecar patterns relative to the candidate's folder against scanned files.
std::vector<size_t> FindSidecars(const std::vector<std::wstring>& patterns, const Candidate& candidate,
                                 const std::vector<ScannedFile>& files, const FileIndex& index)
{
    std::vector<size_t> found;
    std::wstring folder = ParentOf(candidate.Rel);
    for (const std::wstring& pattern : patterns)
    {
        std::wstring expanded = ReplaceAll(ReplaceAll(pattern, L"{stem}", StemOf(candidate.Name)), L"{name}", candidate.Name);
        std::wstring rel = RelJoin(folder, ToSlashes(expanded));
        if (expanded.find_first_of(L"*?[") == std::wstring::npos)
        {
            auto it = index.ByPath.find(FoldKey(rel));
            if (it != index.ByPath.end() && it->second != candidate.FileIndex)
                found.push_back(it->second);
            continue;
        }
        Glob glob;
        std::wstring error;
        if (!glob.Compile(rel, error))
            continue;
        auto bucket = index.ByFolder.find(FoldKey(ParentOf(rel)));
        if (bucket == index.ByFolder.end())
            continue;
        for (size_t fileIndex : bucket->second)
            if (fileIndex != candidate.FileIndex && glob.Matches(files[fileIndex].Rel))
                found.push_back(fileIndex);
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

void AddSelectionOutcome(ReviewModel& model, RegexOutcome outcome, const SafeRegex& regex, const std::wstring& rel,
                         const std::wstring& ruleId)
{
    if (outcome == RegexOutcome::TooLong)
    {
        Finding finding = MakeFinding(L"HO-SEL-007", rel);
        finding.RuleId = ruleId;
        model.SelectionFindings.push_back(finding);
    }
    else if (outcome == RegexOutcome::TooComplex)
    {
        Finding finding = MakeFinding(L"HO-SEL-008", rel, {regex.Pattern()});
        finding.RuleId = ruleId;
        model.SelectionFindings.push_back(finding);
    }
}

} // namespace

ReviewModel Assemble(const Spec& spec, const ScanResult& scan, const std::wstring& workingRoot)
{
    ReviewModel model;
    model.WorkingRoot = workingRoot;
    model.Files = scan.Files;
    model.ScanFindings = scan.Findings;
    model.ExpectedKeys.resize(spec.Rules.size());
    FileIndex index = BuildIndex(model.Files);

    // Forbidden content is never a candidate (C.4.11).
    std::vector<char> forbidden(model.Files.size(), 0);
    for (size_t i = 0; i < model.Files.size(); i++)
    {
        for (const Glob& glob : spec.Content.Forbid)
        {
            if (glob.Matches(model.Files[i].Rel))
            {
                forbidden[i] = 1;
                model.Excluded.push_back(ExcludedFile{i, L"HO-CONT-001"});
                break;
            }
        }
    }

    for (const RuleSpec& rule : spec.Rules)
    {
        LoadRegister(model, index, rule.Expect.Register);
        LoadRegister(model, index, rule.Approval.Register);
        LoadRegister(model, index, rule.License.Register);
    }

    // Rule matching in order; the first matching rule owns a file unless a later rule is 'shared'.
    std::vector<int> owner(model.Files.size(), -1);
    for (size_t r = 0; r < spec.Rules.size(); r++)
    {
        const RuleSpec& rule = spec.Rules[r];
        for (size_t i = 0; i < model.Files.size(); i++)
        {
            if (forbidden[i])
                continue;
            const ScannedFile& file = model.Files[i];
            const Glob* matched = nullptr;
            for (const Glob& glob : rule.Select.Include)
            {
                if (glob.Matches(file.Rel))
                {
                    matched = &glob;
                    break;
                }
            }
            if (matched == nullptr)
                continue;
            bool excluded = false;
            for (const Glob& glob : rule.Select.Exclude)
                excluded = excluded || glob.Matches(file.Rel);
            if (excluded)
                continue;
            if (rule.Select.NameRegex.IsSet())
            {
                RegexOutcome outcome = rule.Select.NameRegex.FullMatch(file.Name);
                AddSelectionOutcome(model, outcome, rule.Select.NameRegex, file.Rel, rule.Id);
                if (outcome != RegexOutcome::Match)
                    continue;
            }
            if (rule.Select.PathRegex.IsSet())
            {
                RegexOutcome outcome = rule.Select.PathRegex.FullMatch(file.Rel);
                AddSelectionOutcome(model, outcome, rule.Select.PathRegex, file.Rel, rule.Id);
                if (outcome != RegexOutcome::Match)
                    continue;
            }
            if (owner[i] >= 0 && !rule.Select.Shared)
            {
                Finding finding = MakeFinding(L"HO-SEL-002", file.Rel, {rule.Title, spec.Rules[(size_t)owner[i]].Title});
                finding.RuleId = rule.Id;
                model.SelectionFindings.push_back(finding);
                continue;
            }
            if (owner[i] < 0)
                owner[i] = (int)r;
            Candidate candidate;
            candidate.FileIndex = i;
            candidate.RuleIndex = (int)r;
            candidate.Rel = file.Rel;
            candidate.Name = file.Name;
            candidate.RelDir = RelativeBelowPrefix(file.Rel, matched->StaticPrefix());
            candidate.Deferred = file.CloudPlaceholder;
            model.Candidates.push_back(candidate);
        }
    }

    // Latest revision per document (C.4.6.3).
    for (size_t r = 0; r < spec.Rules.size(); r++)
    {
        const RuleSpec& rule = spec.Rules[r];
        if (!rule.Select.Latest.Present)
            continue;
        std::map<std::wstring, std::vector<size_t>> groups;
        for (size_t c = 0; c < model.Candidates.size(); c++)
        {
            Candidate& candidate = model.Candidates[c];
            if (candidate.RuleIndex != (int)r)
                continue;
            std::wstring stem = StemOf(candidate.Name), revision;
            size_t position = 0, length = 0;
            RegexOutcome outcome = rule.Select.Latest.Regex.Search(stem, &revision, &position, &length);
            AddSelectionOutcome(model, outcome, rule.Select.Latest.Regex, candidate.Rel, rule.Id);
            std::wstring documentKey;
            if (outcome == RegexOutcome::Match && !revision.empty())
            {
                candidate.Revision = revision;
                candidate.RevisionParsed = PrefixListed(revision, rule.Select.Latest.PrefixOrder);
                documentKey = stem.substr(0, position) + stem.substr(position + length);
            }
            else
                documentKey = stem; // kept, reported as HO-SEL-006
            // Different formats of one document (PDF and DWG) are separate documents.
            documentKey = FoldKey(documentKey + ExtensionOf(candidate.Name));
            if (rule.Select.Latest.PerFolder)
                documentKey = FoldKey(ParentOf(candidate.Rel)) + L"|" + documentKey;
            groups[documentKey].push_back(c);
        }
        for (auto& group : groups)
        {
            size_t newest = group.second[0];
            for (size_t c : group.second)
            {
                const Candidate& candidate = model.Candidates[c];
                if (candidate.Revision.empty())
                    continue;
                const Candidate& best = model.Candidates[newest];
                if (best.Revision.empty() ||
                    CompareRevision(best.Revision, candidate.Revision, rule.Select.Latest.PrefixOrder) < 0)
                    newest = c;
            }
            for (size_t c : group.second)
            {
                Candidate& candidate = model.Candidates[c];
                if (c != newest && !candidate.Revision.empty() && !model.Candidates[newest].Revision.empty())
                {
                    candidate.Superseded = true;
                    candidate.SupersededBy = model.Candidates[newest].Revision;
                }
            }
        }
    }

    // Expected items: keys from file names, expectations from the spec and registers.
    for (size_t r = 0; r < spec.Rules.size(); r++)
    {
        const RuleSpec& rule = spec.Rules[r];
        if (!rule.Expect.Present)
            continue;
        for (Candidate& candidate : model.Candidates)
        {
            if (candidate.RuleIndex != (int)r)
                continue;
            std::wstring key;
            RegexOutcome outcome = rule.Expect.KeyRegex.Search(candidate.Name, &key);
            AddSelectionOutcome(model, outcome, rule.Expect.KeyRegex, candidate.Rel, rule.Id);
            if (outcome == RegexOutcome::Match)
                candidate.Key = key;
        }
        std::vector<std::wstring>& expected = model.ExpectedKeys[r];
        expected = rule.Expect.Keys;
        if (rule.Expect.Register.Present)
        {
            const RegisterEntry& entry = model.Registers[rule.Expect.Register.Path];
            if (entry.Ok)
            {
                int column = entry.Table.Column(rule.Expect.Register.Column);
                int where = rule.Expect.Register.WhereColumn.empty() ? -1 : entry.Table.Column(rule.Expect.Register.WhereColumn);
                for (size_t row = 0; row < entry.Table.Rows.size() && column >= 0; row++)
                {
                    if (where >= 0 && !EqualsNoCase(Trim(entry.Table.Cell(row, where)), Trim(rule.Expect.Register.WhereEquals)))
                        continue;
                    std::wstring key = Trim(entry.Table.Cell(row, column));
                    if (!key.empty())
                        expected.push_back(key);
                }
            }
        }
        std::vector<std::wstring> unique;
        for (const std::wstring& key : expected)
        {
            bool seen = false;
            for (const std::wstring& existing : unique)
                seen = seen || EqualsNoCase(existing, key);
            if (!seen)
                unique.push_back(key);
        }
        expected.swap(unique);
    }

    // Evidence lookup (C.4.8, C.4.9).
    for (Candidate& candidate : model.Candidates)
    {
        const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
        const ApprovalSpec& approval = rule.Approval;
        if (approval.NeedsEvidence())
        {
            bool evidence = false;
            if (approval.PathRegex.IsSet())
                evidence = evidence || approval.PathRegex.Search(candidate.Rel) == RegexOutcome::Match;
            if (approval.NameRegex.IsSet())
                evidence = evidence || approval.NameRegex.Search(candidate.Name) == RegexOutcome::Match;
            if (!approval.Sidecar.empty())
            {
                std::vector<size_t> sidecars = FindSidecars(approval.Sidecar, candidate, model.Files, index);
                for (size_t sidecar : sidecars)
                    model.SupportFiles.insert(sidecar);
                evidence = evidence || !sidecars.empty();
            }
            if (approval.Register.Present)
            {
                const RegisterEntry& entry = model.Registers[approval.Register.Path];
                int fileColumn = entry.Ok ? entry.Table.Column(approval.Register.FileColumn) : -1;
                int statusColumn = entry.Ok ? entry.Table.Column(approval.Register.StatusColumn) : -1;
                for (size_t row = 0; fileColumn >= 0 && statusColumn >= 0 && row < entry.Table.Rows.size(); row++)
                {
                    if (!RowNamesFile(entry.Table.Cell(row, fileColumn), candidate))
                        continue;
                    std::wstring status = Trim(entry.Table.Cell(row, statusColumn));
                    for (const std::wstring& approved : approval.Register.ApprovedValues)
                        evidence = evidence || EqualsNoCase(status, approved);
                }
            }
            candidate.ApprovalEvidence = evidence;
        }
        const LicenseSpec& license = rule.License;
        if (license.Present)
        {
            if (!license.Sidecar.empty())
            {
                candidate.LicenceEvidence = FindSidecars(license.Sidecar, candidate, model.Files, index);
                for (size_t sidecar : candidate.LicenceEvidence)
                    model.SupportFiles.insert(sidecar);
            }
            if (license.Register.Present)
            {
                const RegisterEntry& entry = model.Registers[license.Register.Path];
                int fileColumn = entry.Ok ? entry.Table.Column(license.Register.FileColumn) : -1;
                for (size_t row = 0; fileColumn >= 0 && row < entry.Table.Rows.size(); row++)
                {
                    if (!RowNamesFile(entry.Table.Cell(row, fileColumn), candidate))
                        continue;
                    candidate.LicenceFound = true;
                    const RegisterCsvSpec& reg = license.Register;
                    candidate.Licence = reg.LicenceColumn.empty() ? L"" : Trim(entry.Table.Cell(row, entry.Table.Column(reg.LicenceColumn)));
                    candidate.Licensor = reg.LicensorColumn.empty() ? L"" : Trim(entry.Table.Cell(row, entry.Table.Column(reg.LicensorColumn)));
                    candidate.LicenceExpires = reg.ExpiresColumn.empty() ? L"" : Trim(entry.Table.Cell(row, entry.Table.Column(reg.ExpiresColumn)));
                    candidate.LicenceScope = reg.ScopeColumn.empty() ? L"" : Trim(entry.Table.Cell(row, entry.Table.Column(reg.ScopeColumn)));
                    break;
                }
            }
        }
    }

    // Everything else is listed as "not included" (support files are not).
    std::vector<char> candidateFile(model.Files.size(), 0);
    for (const Candidate& candidate : model.Candidates)
        candidateFile[candidate.FileIndex] = 1;
    for (size_t i = 0; i < model.Files.size(); i++)
        if (!forbidden[i] && !candidateFile[i] && model.SupportFiles.count(i) == 0)
            model.Excluded.push_back(ExcludedFile{i, L"HO-SEL-003"});
    return model;
}

void InspectOne(Candidate& candidate, const std::wstring& fullPath, const Spec& spec, IImageInspector* images,
                IPdfPageInspector* pdf)
{
    candidate.Inspected = true;
    std::vector<unsigned char> head;
    DWORD error = ERROR_SUCCESS;
    if (!ReadHead(fullPath, FormatHeadBytes, head, error))
    {
        candidate.ReadError = error;
        return;
    }
    FormatDetection detection = DetectFormat(candidate.Name, head.data(), head.size(), spec.CustomFormats);
    candidate.Format = detection.Id;
    candidate.FormatKnown = detection.Known;
    candidate.SignatureOk = detection.SignatureOk;
    candidate.HasNamedStreams = HasNamedStreams(fullPath);
    if (!detection.SignatureOk || head.empty())
        return;
    if (candidate.Format == L"pdf")
    {
        candidate.PdfInspected = true;
        candidate.Pdf = InspectPdfStructure(fullPath);
        if (candidate.Pdf.Valid && !candidate.Pdf.Encrypted && pdf != nullptr)
        {
            candidate.PagesInspected = true;
            candidate.Pages = pdf->Inspect(fullPath, 30000);
        }
    }
    else if (IsRasterFormat(candidate.Format) && images != nullptr)
    {
        candidate.ImageInspected = true;
        candidate.Image = images->Inspect(fullPath, candidate.Format);
    }
}

void InspectCandidates(ReviewModel& model, const Spec& spec, IImageInspector* images, IPdfPageInspector* pdf,
                       IProgress& progress)
{
    std::map<size_t, size_t> inspected; // file index -> candidate holding the facts
    uint64_t total = model.Candidates.size(), done = 0;
    for (size_t c = 0; c < model.Candidates.size(); c++)
    {
        if (progress.StopRequested())
            return;
        Candidate& candidate = model.Candidates[c];
        progress.Report(Phase::Inspect, done++, total, candidate.Rel);
        if (candidate.Superseded || candidate.Deferred)
            continue; // superseded files are never delivered; placeholders are inspected at build
        auto previous = inspected.find(candidate.FileIndex);
        if (previous != inspected.end())
        {
            // A file shared by several rules is read once.
            const Candidate& source = model.Candidates[previous->second];
            candidate.Inspected = source.Inspected;
            candidate.ReadError = source.ReadError;
            candidate.Format = source.Format;
            candidate.FormatKnown = source.FormatKnown;
            candidate.SignatureOk = source.SignatureOk;
            candidate.HasNamedStreams = source.HasNamedStreams;
            candidate.PdfInspected = source.PdfInspected;
            candidate.Pdf = source.Pdf;
            candidate.PagesInspected = source.PagesInspected;
            candidate.Pages = source.Pages;
            candidate.ImageInspected = source.ImageInspected;
            candidate.Image = source.Image;
            continue;
        }
        InspectOne(candidate, FullPath(model.WorkingRoot, candidate.Rel), spec, images, pdf);
        inspected[candidate.FileIndex] = c;
    }
    progress.Report(Phase::Inspect, total, total, std::wstring());
}

namespace
{

void Push(std::vector<Finding>& out, const wchar_t* code, const std::wstring& item, std::vector<std::wstring> args,
          const std::wstring& ruleId = std::wstring())
{
    Finding finding = MakeFinding(code, item, std::move(args));
    finding.RuleId = ruleId;
    out.push_back(finding);
}

bool HasPageConstraints(const PdfSpec& pdf)
{
    return pdf.Pages.IsSet() || !pdf.PageSizes.empty() || pdf.Orientation != L"any" || pdf.AllPagesSameSize;
}

bool PageMatches(const PageSizeSpec& size, double w, double h, const std::wstring& orientation, double tolerance)
{
    bool portrait = h >= w;
    if (orientation == L"portrait" && !portrait)
        return false;
    if (orientation == L"landscape" && portrait)
        return false;
    double shortSize = (std::min)(size.WidthMm, size.HeightMm), longSize = (std::max)(size.WidthMm, size.HeightMm);
    double shortPage = (std::min)(w, h), longPage = (std::max)(w, h);
    return fabs(shortSize - shortPage) <= tolerance && fabs(longSize - longPage) <= tolerance;
}

std::wstring AllowedSizesText(const PdfSpec& pdf)
{
    std::vector<std::wstring> parts;
    for (const PageSizeSpec& size : pdf.PageSizes)
        parts.push_back(size.Name.empty() ? DecimalText(size.WidthMm, 1) + L" × " + DecimalText(size.HeightMm, 1) + L" mm"
                                          : size.Name);
    return Join(parts, L", ");
}

void CheckPdf(std::vector<Finding>& out, const Candidate& candidate, const RuleSpec& rule, const ITextCatalog& catalog)
{
    const PdfSpec& spec = rule.Pdf;
    if (!candidate.PdfInspected)
        return;
    if (!candidate.Pdf.Valid)
    {
        Push(out, L"HO-PDF-001", candidate.Rel, {}, rule.Id);
        return;
    }
    if (spec.Present && (!spec.VersionMin.empty() || !spec.VersionMax.empty()))
    {
        const std::wstring& version = candidate.Pdf.Version;
        if ((!spec.VersionMin.empty() && version < spec.VersionMin) || (!spec.VersionMax.empty() && version > spec.VersionMax))
        {
            std::wstring range = spec.VersionMin.empty() ? LabelText(catalog, Label::RANGE_MAX, {spec.VersionMax})
                                 : spec.VersionMax.empty() ? LabelText(catalog, Label::RANGE_MIN, {spec.VersionMin})
                                                           : LabelText(catalog, Label::RANGE_BETWEEN, {spec.VersionMin, spec.VersionMax});
            Push(out, L"HO-PDF-002", candidate.Rel, {version, range}, rule.Id);
        }
    }
    bool encrypted = candidate.Pdf.Encrypted || candidate.Pages.PasswordProtected;
    if (encrypted)
    {
        if (spec.Present && !spec.AllowPasswordProtected)
            Push(out, L"HO-PDF-010", candidate.Rel, {}, rule.Id);
        if (spec.Present && HasPageConstraints(spec))
            Push(out, L"HO-PDF-031", candidate.Rel, {LabelText(catalog, Label::UNAVAILABLE)}, rule.Id);
        return;
    }
    if (!candidate.PagesInspected || !candidate.Pages.Ok)
    {
        if (spec.Present && HasPageConstraints(spec))
            Push(out, L"HO-PDF-031", candidate.Rel,
                 {candidate.Pages.TimedOut ? LabelText(catalog, Label::TIMEOUT) : LabelText(catalog, Label::UNAVAILABLE)},
                 rule.Id);
        else if (candidate.PagesInspected)
            Push(out, L"HO-PDF-030", candidate.Rel, {}, rule.Id);
        return;
    }
    if (!spec.Present)
        return;
    const auto& sizes = candidate.Pages.SizesMm;
    int64_t pages = (int64_t)sizes.size();
    if (spec.Pages.IsSet() && !spec.Pages.Contains(pages))
        Push(out, L"HO-PDF-020", candidate.Rel, {NumberText(pages), RangeText(spec.Pages, catalog)}, rule.Id);
    if (!spec.PageSizes.empty())
    {
        for (size_t p = 0; p < sizes.size(); p++)
        {
            bool ok = false;
            for (const PageSizeSpec& size : spec.PageSizes)
                ok = ok || PageMatches(size, sizes[p].first, sizes[p].second, spec.Orientation, spec.ToleranceMm);
            if (!ok)
            {
                // The first failing page is reported; one finding per file keeps reviews readable.
                Push(out, L"HO-PDF-021", candidate.Rel,
                     {NumberText((int64_t)p + 1), PageSizeText(sizes[p].first, sizes[p].second, &catalog), AllowedSizesText(spec)},
                     rule.Id);
                break;
            }
        }
    }
    else if (spec.Orientation != L"any")
    {
        for (size_t p = 0; p < sizes.size(); p++)
        {
            bool portrait = sizes[p].second >= sizes[p].first;
            if (portrait != (spec.Orientation == L"portrait"))
            {
                Push(out, L"HO-PDF-023", candidate.Rel,
                     {NumberText((int64_t)p + 1), LabelText(catalog, portrait ? Label::PORTRAIT : Label::LANDSCAPE),
                      LabelText(catalog, portrait ? Label::LANDSCAPE : Label::PORTRAIT)},
                     rule.Id);
                break;
            }
        }
    }
    if (spec.AllPagesSameSize)
    {
        for (size_t p = 1; p < sizes.size(); p++)
        {
            if (fabs(sizes[p].first - sizes[0].first) > spec.ToleranceMm || fabs(sizes[p].second - sizes[0].second) > spec.ToleranceMm)
            {
                Push(out, L"HO-PDF-022", candidate.Rel, {}, rule.Id);
                break;
            }
        }
    }
}

void CheckImage(std::vector<Finding>& out, const Candidate& candidate, const RuleSpec& rule, const ITextCatalog& catalog)
{
    if (!candidate.ImageInspected)
        return;
    const ImageSpec& spec = rule.Image;
    const ImageFacts& facts = candidate.Image;
    if (!facts.Decoded)
    {
        if (facts.DecoderMissing)
            Push(out, spec.Present ? L"HO-IMG-031" : L"HO-IMG-030", candidate.Rel, {}, rule.Id);
        else
            Push(out, L"HO-IMG-001", candidate.Rel, {facts.Error}, rule.Id);
        return;
    }
    if (!spec.Present)
        return;
    int64_t w = facts.Width, h = facts.Height;
    if ((spec.Width.IsSet() && !spec.Width.Contains(w)) || (spec.Height.IsSet() && !spec.Height.Contains(h)))
    {
        std::vector<std::wstring> expected;
        if (spec.Width.IsSet())
            expected.push_back(L"W " + RangeText(spec.Width, catalog, L" px"));
        if (spec.Height.IsSet())
            expected.push_back(L"H " + RangeText(spec.Height, catalog, L" px"));
        Push(out, L"HO-IMG-010", candidate.Rel, {NumberText(w), NumberText(h), Join(expected, L"; ")}, rule.Id);
    }
    if (!spec.Exact.empty())
    {
        bool ok = false;
        std::vector<std::wstring> sizes;
        for (const auto& size : spec.Exact)
        {
            ok = ok || (size.first == w && size.second == h);
            sizes.push_back(NumberText(size.first) + L" × " + NumberText(size.second));
        }
        if (!ok)
            Push(out, L"HO-IMG-011", candidate.Rel, {NumberText(w), NumberText(h), Join(sizes, L", ")}, rule.Id);
    }
    if (spec.HasAspect && h > 0)
    {
        double ratio = (double)w / (double)h;
        if (fabs(ratio - spec.Aspect) / spec.Aspect > spec.AspectTolerance)
            Push(out, L"HO-IMG-012", candidate.Rel, {DecimalText(ratio, 3), spec.AspectText}, rule.Id);
    }
    int64_t longEdge = (std::max)(w, h), shortEdge = (std::min)(w, h);
    if (spec.LongEdge.IsSet() && !spec.LongEdge.Contains(longEdge))
        Push(out, L"HO-IMG-013", candidate.Rel,
             {LabelText(catalog, Label::LONG_EDGE), NumberText(longEdge), RangeText(spec.LongEdge, catalog, L" px")}, rule.Id);
    if (spec.ShortEdge.IsSet() && !spec.ShortEdge.Contains(shortEdge))
        Push(out, L"HO-IMG-013", candidate.Rel,
             {LabelText(catalog, Label::SHORT_EDGE), NumberText(shortEdge), RangeText(spec.ShortEdge, catalog, L" px")}, rule.Id);
    if (spec.HasDpiMin)
    {
        if (!facts.DpiKnown)
            Push(out, L"HO-IMG-032", candidate.Rel, {}, rule.Id);
        else if ((std::min)(facts.DpiX, facts.DpiY) + 0.5 < spec.DpiMin)
            Push(out, L"HO-IMG-020", candidate.Rel,
                 {DecimalText((std::min)(facts.DpiX, facts.DpiY), 0), DecimalText(spec.DpiMin, 0)}, rule.Id);
    }
    if (!spec.ColorModels.empty() && !facts.ColorModel.empty() &&
        std::find(spec.ColorModels.begin(), spec.ColorModels.end(), facts.ColorModel) == spec.ColorModels.end())
        Push(out, L"HO-IMG-021", candidate.Rel, {facts.ColorModel, Join(spec.ColorModels, L", ")}, rule.Id);
    if ((spec.Alpha == L"required" && facts.Alpha == 0) || (spec.Alpha == L"forbidden" && facts.Alpha == 1))
        Push(out, L"HO-IMG-022", candidate.Rel, {spec.Alpha}, rule.Id);
    if (spec.BitDepth.IsSet() && facts.BitsPerChannel > 0 && !spec.BitDepth.Contains(facts.BitsPerChannel))
        Push(out, L"HO-IMG-023", candidate.Rel, {NumberText(facts.BitsPerChannel), RangeText(spec.BitDepth, catalog)}, rule.Id);
    if (spec.Frames.HasMax && (int64_t)facts.Frames > spec.Frames.Max)
        Push(out, L"HO-IMG-024", candidate.Rel, {NumberText(facts.Frames), NumberText(spec.Frames.Max)}, rule.Id);
    if (spec.ForbidGps && facts.Gps)
        Push(out, L"HO-IMG-025", candidate.Rel, {}, rule.Id);
}

} // namespace

std::vector<Finding> CheckCandidateContent(const Candidate& candidate, const ScannedFile& file, const Spec& spec,
                                           const ITextCatalog& catalog)
{
    std::vector<Finding> out;
    const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
    if (candidate.ReadError != ERROR_SUCCESS)
    {
        Push(out, L"HO-IMG-001", candidate.Rel, {Win32ErrorText(candidate.ReadError)}, rule.Id);
        return out;
    }
    if ((file.Attributes & FILE_ATTRIBUTE_HIDDEN) && !spec.Content.HiddenAllowed)
        Push(out, L"HO-CONT-002", candidate.Rel, {}, rule.Id);
    if ((file.Attributes & FILE_ATTRIBUTE_SYSTEM) && !spec.Content.SystemAllowed)
        Push(out, L"HO-CONT-003", candidate.Rel, {}, rule.Id);
    if (file.Size == 0 && spec.Content.EmptyFiles != L"allow")
    {
        Push(out, L"HO-CONT-004", candidate.Rel, {}, rule.Id);
        if (spec.Content.EmptyFiles == L"warn")
            out.back().Sev = Severity::Warning; // policy-driven default (C.4.11)
    }
    if (spec.Content.HasMaxFileSize && file.Size > spec.Content.MaxFileSize)
        Push(out, L"HO-CONT-005", candidate.Rel,
             {FormatSize(file.Size), LabelText(catalog, Label::RANGE_MAX, {FormatSize(spec.Content.MaxFileSize)})}, rule.Id);
    if ((rule.HasFileSizeMin && file.Size < rule.FileSizeMin) || (rule.HasFileSizeMax && file.Size > rule.FileSizeMax))
    {
        std::wstring range = rule.HasFileSizeMin && rule.HasFileSizeMax
                                 ? LabelText(catalog, Label::RANGE_BETWEEN, {FormatSize(rule.FileSizeMin), FormatSize(rule.FileSizeMax)})
                             : rule.HasFileSizeMin ? LabelText(catalog, Label::RANGE_MIN, {FormatSize(rule.FileSizeMin)})
                                                   : LabelText(catalog, Label::RANGE_MAX, {FormatSize(rule.FileSizeMax)});
        Push(out, L"HO-CONT-005", candidate.Rel, {FormatSize(file.Size), range}, rule.Id);
    }

    bool ruleListsFormats = !rule.Formats.empty();
    if (!candidate.FormatKnown)
    {
        if (ruleListsFormats)
            Push(out, L"HO-FMT-001", candidate.Rel, {L"unknown", rule.Title}, rule.Id);
        if (spec.Content.HasAllowedFormats)
            Push(out, L"HO-FMT-003", candidate.Rel, {L"unknown"}, rule.Id);
        if (!ruleListsFormats && !spec.Content.HasAllowedFormats)
            Push(out, L"HO-FMT-004", candidate.Rel, {}, rule.Id);
    }
    else
    {
        if (!candidate.SignatureOk)
            Push(out, L"HO-FMT-002", candidate.Rel, {ExtensionOf(candidate.Name)}, rule.Id);
        if (ruleListsFormats && std::find(rule.Formats.begin(), rule.Formats.end(), candidate.Format) == rule.Formats.end())
            Push(out, L"HO-FMT-001", candidate.Rel, {candidate.Format, rule.Title}, rule.Id);
        if (spec.Content.HasAllowedFormats &&
            std::find(spec.Content.AllowedFormats.begin(), spec.Content.AllowedFormats.end(), candidate.Format) ==
                spec.Content.AllowedFormats.end())
            Push(out, L"HO-FMT-003", candidate.Rel, {candidate.Format}, rule.Id);
    }
    if (spec.Naming.SourceNameRegex.IsSet() && spec.Naming.SourceNameRegex.FullMatch(candidate.Name) == RegexOutcome::NoMatch)
        Push(out, L"HO-NAME-001", candidate.Rel, {}, rule.Id);
    if (candidate.HasNamedStreams)
        Push(out, L"HO-CONT-007", candidate.Rel, {}, rule.Id);
    CheckPdf(out, candidate, rule, catalog);
    CheckImage(out, candidate, rule, catalog);
    return out;
}

void ResolveFindings(std::vector<Finding>& findings, const Spec& spec, const RuleSpec* rule,
                     const ReviewerDecisions& decisions, std::vector<Finding>& destination)
{
    for (Finding& finding : findings)
    {
        finding.Sev = ResolveSeverity(finding.Code, finding.Sev, &spec.Severity, rule != nullptr ? &rule->Severity : nullptr);
        if (finding.Sev == Severity::Off)
            continue;
        const CodeInfo* info = FindCode(finding.Code);
        if (finding.Sev == Severity::Error && spec.AllowErrorOverride && info != nullptr && !info->Locked)
        {
            auto it = decisions.Overrides.find(ReviewerDecisions::OverrideKey(finding));
            if (it != decisions.Overrides.end())
            {
                finding.Overridden = true;
                finding.OverrideReason = it->second;
            }
        }
        destination.push_back(finding);
    }
}

void Evaluate(ReviewModel& model, const Spec& spec, const ReviewerDecisions& decisions, const SYSTEMTIME& today,
              const ITextCatalog& catalog)
{
    model.Findings.clear();
    model.Rules.assign(spec.Rules.size(), RuleSummary());
    std::vector<Finding> general = model.ScanFindings;
    general.insert(general.end(), model.SelectionFindings.begin(), model.SelectionFindings.end());
    for (Finding& finding : general)
    {
        const RuleSpec* rule = finding.RuleId.empty() ? nullptr : spec.FindRule(finding.RuleId);
        std::vector<Finding> one{finding};
        ResolveFindings(one, spec, rule, decisions, model.Findings);
    }

    for (Candidate& candidate : model.Candidates)
    {
        const RuleSpec& rule = spec.Rules[(size_t)candidate.RuleIndex];
        std::wstring key = ReviewerDecisions::Key(candidate.Rel, rule.Id);
        bool excludedByReviewer = decisions.Excluded.count(key) != 0;
        candidate.Included = !candidate.Superseded && !excludedByReviewer;
        candidate.Approved = decisions.Approved.count(key) != 0;
        std::vector<Finding> raw;
        if (candidate.Superseded)
            Push(raw, L"HO-SEL-001", candidate.Rel, {candidate.SupersededBy}, rule.Id);
        else if (excludedByReviewer)
            Push(raw, L"HO-SEL-004", candidate.Rel, {}, rule.Id);
        else
        {
            if (rule.Select.Latest.Present && !candidate.RevisionParsed)
                Push(raw, L"HO-SEL-006", candidate.Rel, {}, rule.Id);
            if (candidate.Deferred && !candidate.Inspected)
                Push(raw, L"HO-SCAN-005", candidate.Rel, {}, rule.Id);
            else
            {
                std::vector<Finding> content = CheckCandidateContent(candidate, model.Files[candidate.FileIndex], spec, catalog);
                raw.insert(raw.end(), content.begin(), content.end());
            }
            if (rule.Approval.NeedsEvidence() && !candidate.ApprovalEvidence)
                Push(raw, L"HO-APP-001", candidate.Rel, {}, rule.Id);
            if (rule.Approval.NeedsConfirm() && !candidate.Approved)
                Push(raw, L"HO-APP-002", candidate.Rel, {}, rule.Id);
            if (rule.License.Present)
            {
                bool evidence = candidate.LicenceFound || !candidate.LicenceEvidence.empty();
                if (rule.License.Required && !evidence)
                    Push(raw, L"HO-LIC-001", candidate.Rel, {}, rule.Id);
                if (candidate.LicenceFound && !candidate.LicenceExpires.empty())
                {
                    SYSTEMTIME expires;
                    if (!ParseIsoDate(candidate.LicenceExpires, expires))
                        Push(raw, L"HO-LIC-005", candidate.Rel, {candidate.LicenceExpires}, rule.Id);
                    else
                    {
                        int64_t days = DaysBetween(today, expires);
                        if (days < 0)
                            Push(raw, L"HO-LIC-002", candidate.Rel, {candidate.LicenceExpires}, rule.Id);
                        else if (days <= rule.License.ExpiryWarningDays)
                            Push(raw, L"HO-LIC-003", candidate.Rel, {candidate.LicenceExpires}, rule.Id);
                    }
                }
                if (candidate.LicenceFound && !rule.License.Register.AllowedScopes.empty())
                {
                    bool allowed = false;
                    for (const std::wstring& scope : rule.License.Register.AllowedScopes)
                        allowed = allowed || EqualsNoCase(scope, candidate.LicenceScope);
                    if (!allowed)
                        Push(raw, L"HO-LIC-004", candidate.Rel, {candidate.LicenceScope}, rule.Id);
                }
            }
        }
        candidate.Findings.clear();
        ResolveFindings(raw, spec, &rule, decisions, candidate.Findings);
        model.Findings.insert(model.Findings.end(), candidate.Findings.begin(), candidate.Findings.end());
        if (candidate.Included)
            model.Rules[(size_t)candidate.RuleIndex].Selected++;
    }

    // Rule-level omissions and register problems (C.4.5, C.4.6.4).
    for (size_t r = 0; r < spec.Rules.size(); r++)
    {
        const RuleSpec& rule = spec.Rules[r];
        RuleSummary& summary = model.Rules[r];
        std::vector<Finding> raw;
        int64_t selected = (int64_t)summary.Selected;
        if (rule.Required && selected == 0)
            Push(raw, L"HO-REQ-001", L"", {rule.Title}, rule.Id);
        else if (rule.Count.HasMin && selected < rule.Count.Min)
            Push(raw, L"HO-REQ-002", L"", {rule.Title, NumberText(selected), NumberText(rule.Count.Min)}, rule.Id);
        if (rule.Count.HasMax && selected > rule.Count.Max)
            Push(raw, L"HO-REQ-003", L"", {rule.Title, NumberText(selected), NumberText(rule.Count.Max)}, rule.Id);
        auto registerError = [&](const RegisterCsvSpec& reg, const wchar_t* code) {
            if (!reg.Present)
                return;
            auto it = model.Registers.find(reg.Path);
            if (it != model.Registers.end() && !it->second.Ok)
                Push(raw, code, reg.Path, {it->second.Error}, rule.Id);
        };
        registerError(rule.Expect.Register, L"HO-REQ-013");
        registerError(rule.Approval.Register, L"HO-APP-003");
        registerError(rule.License.Register, L"HO-LIC-005");
        if (rule.Expect.Present)
        {
            const std::vector<std::wstring>& expected = model.ExpectedKeys[r];
            summary.HasExpected = true;
            summary.Expected = expected.size();
            std::map<std::wstring, std::vector<const Candidate*>> byKey;
            for (const Candidate& candidate : model.Candidates)
            {
                if (candidate.RuleIndex != (int)r || !candidate.Included)
                    continue;
                if (candidate.Key.empty())
                {
                    if (!rule.Expect.AllowUnexpected)
                        Push(raw, L"HO-REQ-011", candidate.Rel, {candidate.Name}, rule.Id);
                    continue;
                }
                byKey[FoldKey(candidate.Key)].push_back(&candidate);
            }
            for (const std::wstring& key : expected)
            {
                auto it = byKey.find(FoldKey(key));
                if (it == byKey.end())
                    Push(raw, L"HO-REQ-010", L"", {key}, rule.Id);
            }
            for (const auto& entry : byKey)
            {
                const Candidate& first = *entry.second[0];
                if (entry.second.size() > 1)
                    Push(raw, L"HO-REQ-012", first.Rel, {first.Key}, rule.Id);
                bool known = false;
                for (const std::wstring& key : expected)
                    known = known || EqualsNoCase(key, first.Key);
                if (!known && !rule.Expect.AllowUnexpected && !expected.empty())
                    Push(raw, L"HO-REQ-011", first.Rel, {first.Key}, rule.Id);
            }
        }
        std::vector<Finding> resolved;
        ResolveFindings(raw, spec, &rule, decisions, resolved);
        for (const Finding& finding : resolved)
            if (finding.Code == L"HO-REQ-001" || finding.Code == L"HO-REQ-002" || finding.Code == L"HO-REQ-010")
                summary.Missing = true;
        model.Findings.insert(model.Findings.end(), resolved.begin(), resolved.end());
    }

    // "Not included" entries become findings only when the policy raises them to warnings.
    if (spec.Content.Unassigned == L"warn")
    {
        for (const ExcludedFile& excluded : model.Excluded)
        {
            if (excluded.Code != L"HO-SEL-003")
                continue;
            Finding finding = MakeFinding(L"HO-SEL-003", model.Files[excluded.FileIndex].Rel);
            finding.Sev = Severity::Warning;
            std::vector<Finding> one{finding};
            ResolveFindings(one, spec, nullptr, decisions, model.Findings);
        }
    }

    for (const Finding& finding : model.Findings)
    {
        if (finding.RuleId.empty())
            continue;
        for (size_t r = 0; r < spec.Rules.size(); r++)
        {
            if (spec.Rules[r].Id != finding.RuleId)
                continue;
            if (finding.Sev == Severity::Error && !finding.Overridden)
                model.Rules[r].Errors++;
            else if (finding.Sev == Severity::Warning)
                model.Rules[r].Warnings++;
        }
    }
}

Variables BuildVariables(const Spec& spec, const std::map<std::wstring, std::wstring>& values, const SYSTEMTIME& date)
{
    Variables variables;
    for (const VariableDef& def : spec.Variables)
    {
        auto it = values.find(def.Name);
        variables[def.Name] = it != values.end() ? it->second : def.Default;
    }
    auto dateValue = values.find(L"date");
    variables[L"date"] = dateValue != values.end() && !dateValue->second.empty() ? dateValue->second
                                                                                : FormatDate(date, L"yyyy-MM-dd");
    variables[L"specId"] = spec.Id;
    variables[L"specName"] = spec.Name;
    variables[L"specRevision"] = spec.Revision;
    return variables;
}

std::vector<Finding> ValidateVariables(const Spec& spec, const Variables& values)
{
    std::vector<Finding> findings;
    for (const VariableDef& def : spec.Variables)
    {
        auto it = values.find(def.Name);
        std::wstring value = it != values.end() ? it->second : std::wstring();
        if (value.empty())
        {
            if (def.Required)
                findings.push_back(MakeFinding(L"HO-SES-004", def.Name, {def.Label, L"a value is required"}));
            continue;
        }
        if (value.size() > 128)
            findings.push_back(MakeFinding(L"HO-SES-004", def.Name, {def.Label, L"the value is longer than 128 characters"}));
        else if (def.Pattern.IsSet() && def.Pattern.FullMatch(value) != RegexOutcome::Match)
            findings.push_back(MakeFinding(L"HO-SES-004", def.Name, {def.Label, L"the value does not match " + def.Pattern.Pattern()}));
        else if (!def.Choices.empty() && std::find(def.Choices.begin(), def.Choices.end(), value) == def.Choices.end())
            findings.push_back(MakeFinding(L"HO-SES-004", def.Name, {def.Label, L"choose one of: " + Join(def.Choices, L", ")}));
    }
    auto date = values.find(L"date");
    SYSTEMTIME parsed = {};
    if (date != values.end() && !ParseIsoDate(date->second, parsed))
        findings.push_back(MakeFinding(L"HO-SES-004", L"date", {L"date", L"use yyyy-MM-dd"}));
    return findings;
}

} // namespace handoff
