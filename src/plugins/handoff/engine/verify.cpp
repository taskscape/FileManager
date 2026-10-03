// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "verify.h"
#include "file_system.h"
#include "json.h"
#include "review.h"
#include "text_util.h"

#include <algorithm>
#include <deque>
#include <set>

namespace handoff
{

namespace
{

bool GetString(const JsonValue& object, const wchar_t* name, std::wstring& out, bool required, std::wstring& error)
{
    const JsonValue* value = object.Find(name);
    if (value == nullptr)
    {
        if (required)
            error = std::wstring(L"missing \"") + name + L"\"";
        return !required;
    }
    if (!value->IsString())
    {
        error = std::wstring(L"\"") + name + L"\" is not a string";
        return false;
    }
    out = value->String;
    return true;
}

bool IsSha256(const std::wstring& text)
{
    return text.size() == 64 && text.find_first_not_of(L"0123456789abcdef") == std::wstring::npos;
}

bool ReadEntry(const JsonValue& value, std::wstring& path, uint64_t& bytes, std::wstring& sha, std::wstring& error)
{
    if (!value.IsObject())
    {
        error = L"an entry is not an object";
        return false;
    }
    int64_t size = -1;
    const JsonValue* bytesValue = value.Find(L"bytes");
    if (!GetString(value, L"path", path, true, error) || !GetString(value, L"sha256", sha, true, error))
        return false;
    if (bytesValue == nullptr || !bytesValue->AsInteger(size) || size < 0)
    {
        error = L"invalid \"bytes\" for " + path;
        return false;
    }
    // Manifest paths are package-relative and must stay inside the package (C.10.1).
    if (!IsSafeRelativePath(path) || !IsSha256(sha))
    {
        error = L"invalid entry " + path;
        return false;
    }
    bytes = (uint64_t)size;
    return true;
}

} // namespace

bool ParseManifest(const unsigned char* data, size_t length, ParsedManifest& manifest, std::wstring& error)
{
    manifest = ParsedManifest();
    JsonValue root;
    JsonError jsonError;
    if (!ParseJson(data, length, ManifestJsonLimits(), root, jsonError))
    {
        error = jsonError.Message + L" (" + NumberText(jsonError.Line) + L":" + NumberText(jsonError.Column) + L")";
        return false;
    }
    int64_t version = 0;
    const JsonValue* versionValue = root.Find(L"handoffManifest");
    if (!root.IsObject() || versionValue == nullptr || !versionValue->AsInteger(version) || version != 1)
    {
        error = L"not a handoffManifest 1 document";
        return false;
    }
    const JsonValue* package = root.Find(L"package");
    const JsonValue* spec = root.Find(L"spec");
    const JsonValue* outputs = root.Find(L"outputs");
    const JsonValue* files = root.Find(L"files");
    if (package == nullptr || !package->IsObject() || spec == nullptr || !spec->IsObject() || outputs == nullptr ||
        !outputs->IsArray() || files == nullptr || !files->IsArray())
    {
        error = L"missing package, spec, outputs, or files";
        return false;
    }
    if (!GetString(*package, L"name", manifest.PackageName, true, error) ||
        !GetString(*package, L"createdUtc", manifest.CreatedUtc, false, error) ||
        !GetString(*spec, L"id", manifest.SpecId, true, error) || !GetString(*spec, L"name", manifest.SpecName, false, error) ||
        !GetString(*spec, L"revision", manifest.SpecRevision, false, error) ||
        !GetString(*spec, L"sha256", manifest.SpecSha256, false, error))
        return false;
    for (const JsonValue& value : outputs->Items)
    {
        ManifestOutput output;
        if (!ReadEntry(value, output.Path, output.Bytes, output.Sha256, error))
            return false;
        manifest.Outputs.push_back(output);
    }
    for (const JsonValue& value : files->Items)
    {
        ManifestEntry entry;
        if (!ReadEntry(value, entry.Path, entry.Bytes, entry.Sha256, error) ||
            !GetString(value, L"rule", entry.Rule, false, error) || !GetString(value, L"role", entry.Role, false, error) ||
            !GetString(value, L"format", entry.Format, false, error) || !GetString(value, L"key", entry.Key, false, error) ||
            !GetString(value, L"approval", entry.Approval, false, error))
            return false;
        manifest.Files.push_back(entry);
    }
    return true;
}

std::wstring FindManifest(const std::wstring& packageRoot)
{
    std::wstring standard = PathJoin(packageRoot, L"manifest.json");
    DWORD attributes = GetFileAttributesW(LongPath(standard).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return standard;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(LongPath(PathJoin(packageRoot, L"*.json")).c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, NULL, 0);
    if (find == INVALID_HANDLE_VALUE)
        return std::wstring();
    std::vector<std::wstring> matches;
    int checked = 0;
    do
    {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (++checked > 10)
            break;
        std::wstring path = PathJoin(packageRoot, data.cFileName);
        std::vector<unsigned char> bytes;
        DWORD error;
        JsonValue root;
        JsonError jsonError;
        int64_t version = 0;
        if (ReadWholeFile(path, ManifestJsonLimits().MaxBytes, bytes, error) &&
            ParseJson(bytes.data(), bytes.size(), ManifestJsonLimits(), root, jsonError) && root.Find(L"handoffManifest") != nullptr &&
            root.Find(L"handoffManifest")->AsInteger(version) && version == 1)
            matches.push_back(path);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return matches.size() == 1 ? matches[0] : std::wstring();
}

BuildRecordInfo ReadBuildRecordBeside(const std::wstring& packageRoot)
{
    BuildRecordInfo info;
    std::wstring root = packageRoot;
    while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/'))
        root.pop_back();
    std::wstring path = PathJoin(ParentOf(root), FileNameOf(root) + L".handoff-build.json");
    std::vector<unsigned char> bytes;
    DWORD error;
    if (!ReadWholeFile(path, 64 * 1024 * 1024, bytes, error))
        return info;
    JsonValue record;
    JsonError jsonError;
    JsonLimits limits = ManifestJsonLimits();
    limits.MaxBytes = 64 * 1024 * 1024;
    limits.MaxString = 2 * 1024 * 1024; // the specification text is embedded as one string
    if (!ParseJson(bytes.data(), bytes.size(), limits, record, jsonError))
        return info;
    const JsonValue* version = record.Find(L"handoffBuildRecord");
    int64_t v = 0;
    if (version == nullptr || !version->AsInteger(v) || v != 1)
        return info;
    std::wstring ignored;
    if (const JsonValue* package = record.Find(L"package"))
        GetString(*package, L"manifestSha256", info.ManifestSha256, false, ignored);
    if (const JsonValue* spec = record.Find(L"spec"))
    {
        GetString(*spec, L"sha256", info.SpecSha256, false, ignored);
        GetString(*spec, L"path", info.SpecPath, false, ignored);
        GetString(*spec, L"text", info.SpecText, false, ignored);
    }
    info.Found = true;
    info.Path = path;
    return info;
}

namespace
{

bool HashPackageFile(const std::wstring& path, std::wstring& sha, uint64_t& size, IProgress& progress)
{
    Win32FileSystem fs;
    HANDLE file = fs.Open(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    Sha256 hash;
    std::vector<unsigned char> buffer(1024 * 1024);
    size = 0;
    bool ok = true;
    for (;;)
    {
        DWORD read = 0;
        if (!fs.Read(file, buffer.data(), (DWORD)buffer.size(), &read))
        {
            ok = false;
            break;
        }
        if (read == 0)
            break;
        hash.Update(buffer.data(), read);
        size += read;
        if (progress.StopRequested())
        {
            ok = false;
            break;
        }
    }
    fs.Close(file);
    sha = hash.FinishHex();
    return ok;
}

} // namespace

VerifyResult VerifyPackage(const VerifyInput& input, IProgress& progress, IImageInspector* images, IPdfPageInspector* pdf,
                           const ITextCatalog& catalog)
{
    VerifyResult result;
    std::vector<Finding> raw;
    result.ManifestPath = FindManifest(input.PackageRoot);
    std::vector<unsigned char> bytes;
    DWORD readError = ERROR_FILE_NOT_FOUND;
    std::wstring error = L"manifest.json";
    if (result.ManifestPath.empty() || !ReadWholeFile(result.ManifestPath, ManifestJsonLimits().MaxBytes, bytes, readError))
        error = result.ManifestPath.empty() ? L"manifest.json" : Win32ErrorText(readError);
    else if (ParseManifest(bytes.data(), bytes.size(), result.Manifest, error))
        result.ManifestOk = true;
    if (!result.ManifestOk)
    {
        result.Findings.push_back(MakeFinding(L"HO-VER-001", L"", {error}));
        result.Status = L"failed";
        return result;
    }
    const ParsedManifest& manifest = result.Manifest;
    std::wstring manifestName = FileNameOf(result.ManifestPath);
    std::wstring manifestSha = Sha256Hex(bytes.data(), bytes.size());

    // Manifest authentication against the build record (C.5.8 step 4).
    if (input.BuildRecord.Found && !input.BuildRecord.ManifestSha256.empty())
    {
        result.ManifestAuthenticated = input.BuildRecord.ManifestSha256 == manifestSha;
        if (!result.ManifestAuthenticated)
            raw.push_back(MakeFinding(L"HO-VER-007", manifestName));
    }

    // Integrity of every listed file and output.
    uint64_t total = manifest.Files.size() + manifest.Outputs.size(), done = 0;
    std::set<std::wstring> listed{FoldKey(manifestName)};
    auto check = [&](const std::wstring& rel, uint64_t bytesExpected, const std::wstring& shaExpected, bool output) {
        listed.insert(FoldKey(rel));
        progress.Report(Phase::Verify, done++, total, rel);
        std::wstring sha;
        uint64_t size = 0;
        std::wstring full = PathJoin(input.PackageRoot, ToBackslashes(rel));
        if (GetFileAttributesW(LongPath(full).c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            raw.push_back(MakeFinding(L"HO-VER-002", rel));
            return;
        }
        if (!HashPackageFile(full, sha, size, progress) || size != bytesExpected || sha != shaExpected)
            raw.push_back(MakeFinding(output ? L"HO-VER-005" : L"HO-VER-003", rel));
        result.FilesChecked++;
    };
    for (const ManifestEntry& entry : manifest.Files)
    {
        if (progress.StopRequested())
            break;
        check(entry.Path, entry.Bytes, entry.Sha256, false);
    }
    for (const ManifestOutput& output : manifest.Outputs)
        check(output.Path, output.Bytes, output.Sha256, true);

    // Unlisted content anywhere below the package root.
    std::deque<std::wstring> pending{std::wstring()};
    std::vector<std::pair<std::wstring, DWORD>> present;
    while (!pending.empty())
    {
        std::wstring rel = pending.front();
        pending.pop_front();
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW(LongPath(PathJoin(input.PackageRoot, ToBackslashes(RelJoin(rel, L"*")))).c_str(),
                                       FindExInfoBasic, &data, FindExSearchNameMatch, NULL, 0);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            std::wstring child = RelJoin(rel, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    pending.push_back(child);
                continue;
            }
            present.push_back(std::make_pair(child, data.dwFileAttributes));
            if (listed.count(FoldKey(child)) == 0)
                raw.push_back(MakeFinding(L"HO-VER-004", child));
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    const Spec* spec = input.SpecModel;
    if (spec == nullptr)
        raw.push_back(MakeFinding(L"HO-VER-011", L"", {manifest.SpecId}));
    else
    {
        if (!manifest.SpecSha256.empty() && spec->Sha256 != manifest.SpecSha256)
            raw.push_back(MakeFinding(L"HO-VER-010", L""));
        // Rule re-check on the package itself (C.5.8 step 5).
        for (const auto& item : present)
        {
            for (const Glob& glob : spec->Content.Forbid)
            {
                if (glob.Matches(item.first))
                {
                    raw.push_back(MakeFinding(L"HO-VER-006", item.first, {glob.Pattern()}));
                    break;
                }
            }
        }
        std::vector<size_t> perRule(spec->Rules.size(), 0);
        for (const ManifestEntry& entry : manifest.Files)
        {
            for (const std::wstring& segment : Split(entry.Path, L'/'))
            {
                SanitizeResult clean = SanitizeName(segment, spec->Naming, true);
                if (clean.Reserved)
                    raw.push_back(MakeFinding(L"HO-NAME-014", entry.Path, {segment}));
                else if (clean.Changed)
                {
                    raw.push_back(MakeFinding(L"HO-NAME-001", entry.Path));
                    break;
                }
            }
            if ((int64_t)entry.Path.size() > spec->Package.MaxRelativePathLength)
                raw.push_back(MakeFinding(L"HO-NAME-013", entry.Path,
                                          {NumberText((int64_t)entry.Path.size()), NumberText(spec->Package.MaxRelativePathLength)}));
            if (!entry.Approval.empty())
                raw.push_back(MakeFinding(L"HO-VER-012", entry.Path, {entry.Approval}));
            int ruleIndex = -1;
            for (size_t r = 0; r < spec->Rules.size(); r++)
                if (spec->Rules[r].Id == entry.Rule && entry.Role != L"licence-evidence")
                    ruleIndex = (int)r;
            if (ruleIndex < 0)
                continue;
            perRule[(size_t)ruleIndex]++;
            std::wstring full = PathJoin(input.PackageRoot, ToBackslashes(entry.Path));
            if (GetFileAttributesW(LongPath(full).c_str()) == INVALID_FILE_ATTRIBUTES)
                continue;
            Candidate candidate;
            candidate.RuleIndex = ruleIndex;
            candidate.Rel = entry.Path;
            candidate.Name = FileNameOf(entry.Path);
            candidate.Key = entry.Key;
            InspectOne(candidate, full, *spec, images, pdf);
            ScannedFile file;
            file.Rel = entry.Path;
            file.Name = candidate.Name;
            file.Size = entry.Bytes;
            WIN32_FILE_ATTRIBUTE_DATA attributes;
            if (GetFileAttributesExW(LongPath(full).c_str(), GetFileExInfoStandard, &attributes))
                file.Attributes = attributes.dwFileAttributes;
            std::vector<Finding> content = CheckCandidateContent(candidate, file, *spec, catalog);
            raw.insert(raw.end(), content.begin(), content.end());
        }
        for (size_t r = 0; r < spec->Rules.size(); r++)
        {
            const RuleSpec& rule = spec->Rules[r];
            int64_t count = (int64_t)perRule[r];
            if (rule.Required && count == 0)
                raw.push_back(MakeFinding(L"HO-REQ-001", L"", {rule.Title}));
            else if (rule.Count.HasMin && count < rule.Count.Min)
                raw.push_back(MakeFinding(L"HO-REQ-002", L"", {rule.Title, NumberText(count), NumberText(rule.Count.Min)}));
            if (rule.Count.HasMax && count > rule.Count.Max)
                raw.push_back(MakeFinding(L"HO-REQ-003", L"", {rule.Title, NumberText(count), NumberText(rule.Count.Max)}));
            if (rule.Expect.Present)
            {
                // Registers live in the working folder, so only inline keys are re-checked here.
                for (const std::wstring& key : rule.Expect.Keys)
                {
                    bool found = false;
                    for (const ManifestEntry& entry : manifest.Files)
                        found = found || (entry.Rule == rule.Id && EqualsNoCase(entry.Key, key));
                    if (!found)
                        raw.push_back(MakeFinding(L"HO-REQ-010", L"", {key}));
                }
            }
            for (Finding& finding : raw)
                if (finding.RuleId.empty() && (finding.Code.compare(0, 7, L"HO-REQ-") == 0))
                    finding.RuleId = rule.Id;
        }
    }

    ReviewerDecisions none;
    if (spec != nullptr)
    {
        for (Finding& finding : raw)
        {
            const RuleSpec* rule = finding.RuleId.empty() ? nullptr : spec->FindRule(finding.RuleId);
            std::vector<Finding> one{finding};
            ResolveFindings(one, *spec, rule, none, result.Findings);
        }
    }
    else
        result.Findings = raw;
    bool errors = false, warnings = false;
    for (const Finding& finding : result.Findings)
    {
        errors = errors || finding.Sev == Severity::Error;
        warnings = warnings || finding.Sev == Severity::Warning;
    }
    result.Status = errors ? L"failed" : warnings ? L"verifiedWithWarnings" : L"verified";
    progress.Report(Phase::Verify, total, total, std::wstring());
    return result;
}

std::wstring SeverityLabel(Severity severity, const ITextCatalog& catalog)
{
    switch (severity)
    {
    case Severity::Error: return LabelText(catalog, Label::SEV_ERROR);
    case Severity::Warning: return LabelText(catalog, Label::SEV_WARNING);
    default: return LabelText(catalog, Label::SEV_INFO);
    }
}

std::wstring FindingLine(const Finding& finding, const ITextCatalog& catalog)
{
    std::wstring line = SeverityLabel(finding.Sev, catalog) + L"  " + finding.Code + L"  ";
    if (!finding.Item.empty())
        line += finding.Item + L": ";
    line += FindingMessage(finding, catalog);
    if (finding.Line > 0)
        line += L" (" + NumberText(finding.Line) + L":" + NumberText(finding.Column) + L" " + finding.Pointer + L")";
    if (finding.Overridden)
        line += L" [override: " + finding.OverrideReason + L"]";
    return line;
}

std::string ReportText(const std::wstring& title, const std::vector<std::pair<std::wstring, std::wstring>>& header,
                       const std::vector<std::pair<std::wstring, std::vector<std::wstring>>>& sections)
{
    std::wstring text = title + L"\r\n" + std::wstring(title.size(), L'=') + L"\r\n";
    size_t width = 0;
    for (const auto& line : header)
        width = (std::max)(width, line.first.size());
    for (const auto& line : header)
        text += line.first + std::wstring(width + 2 - line.first.size(), L' ') + line.second + L"\r\n";
    for (const auto& section : sections)
    {
        text += L"\r\n" + section.first + L"\r\n" + std::wstring(section.first.size(), L'-') + L"\r\n";
        for (const std::wstring& line : section.second)
            text += line + L"\r\n";
    }
    return "\xEF\xBB\xBF" + WideToUtf8(text);
}

} // namespace handoff
