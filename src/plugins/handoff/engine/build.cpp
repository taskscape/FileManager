// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "build.h"
#include "json.h"
#include "text_util.h"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace handoff
{

std::wstring PartialFolderName(const std::wstring& packageName, const std::wstring& sessionId)
{
    std::wstring id;
    for (wchar_t c : sessionId)
        if (iswxdigit(c) && id.size() < 8)
            id += (wchar_t)towlower(c);
    return L"." + packageName + L".handoff-" + id + L".partial";
}

const wchar_t* PartialMarkerName()
{
    return L".handoff-partial";
}

namespace
{

const DWORD CopyChunk = 1024 * 1024;

enum class CopyStatus
{
    Ok,
    Retryable,
    Fatal,
    Cancelled
};

class Builder
{
public:
    Builder(BuildInput& input, IHandoffFileSystem& fs, IBuildCallbacks& callbacks, IImageInspector* images,
            IPdfPageInspector* pdf, const ITextCatalog& catalog, BuildResult& result)
        : In(input), Fs(fs), Callbacks(callbacks), Images(images), Pdf(pdf), Catalog(catalog), Result(result)
    {
    }

    void Run()
    {
        const PackagePlan& plan = In.Plan;
        PackagePath = PathJoin(In.StagingRoot, plan.PackageName);
        DWORD stagingAttributes = Fs.GetAttributes(In.StagingRoot);
        if (stagingAttributes == INVALID_FILE_ATTRIBUTES || !(stagingAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            Add(L"HO-BUILD-003", In.StagingRoot, {In.StagingRoot, Win32ErrorText(GetLastError())});
            return;
        }
        if (Fs.GetAttributes(PackagePath) != INVALID_FILE_ATTRIBUTES)
        {
            Add(L"HO-BUILD-001", plan.PackageName, {plan.PackageName});
            return;
        }
        if (!CreatePartial())
        {
            // A partial folder that exists (for example after a marker failure) is still ours to remove.
            if (!Partial.empty())
                Abandon();
            return;
        }
        bool ok = CreateFolders() && CopyFiles() && CheckDuplicates() && WriteOutputs() && VerifyTree();
        if (ok && Callbacks.StopRequested())
        {
            Cancelled = true;
            ok = false;
        }
        if (!ok)
        {
            Abandon();
            return;
        }
        Publish();
    }

private:
    struct CreatedItem
    {
        std::wstring Path;
        bool Folder;
    };

    BuildInput& In;
    IHandoffFileSystem& Fs;
    IBuildCallbacks& Callbacks;
    IImageInspector* Images;
    IPdfPageInspector* Pdf;
    const ITextCatalog& Catalog;
    BuildResult& Result;

    std::wstring PackagePath, Partial;
    HANDLE Retained = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION RetainedInfo = {};
    std::vector<CreatedItem> Created;
    bool MarkerPresent = false;
    bool Cancelled = false;
    uint64_t Done = 0;
    struct Written
    {
        uint64_t Bytes;
        std::wstring Sha256;
    };
    std::map<std::wstring, Written> Files; // FoldKey(target rel)
    std::vector<std::wstring> OutputOrder;
    std::map<std::wstring, Written> OutputFiles;

    void Add(const wchar_t* code, const std::wstring& item, std::vector<std::wstring> args)
    {
        Result.Findings.push_back(MakeFinding(code, item, std::move(args)));
    }

    std::wstring Target(const std::wstring& rel) const { return PathJoin(Partial, ToBackslashes(rel)); }

    bool CreatePartial()
    {
        Partial = PathJoin(In.StagingRoot, PartialFolderName(In.Plan.PackageName, In.SessionId));
        if (!Fs.CreateFolder(Partial))
        {
            Add(L"HO-BUILD-003", Partial, {FileNameOf(Partial), Win32ErrorText(GetLastError())});
            Partial.clear();
            return false;
        }
        // The retained handle (no FILE_SHARE_DELETE) keeps other instances from deleting
        // or swapping the folder and marks this build as live (C.5.1 step 4).
        Retained = Fs.Open(Partial, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
        if (Retained == INVALID_HANDLE_VALUE || !Fs.GetInformation(Retained, RetainedInfo) ||
            !(RetainedInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (RetainedInfo.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        {
            DWORD error = GetLastError();
            if (Retained != INVALID_HANDLE_VALUE)
                Fs.Close(Retained);
            Retained = INVALID_HANDLE_VALUE;
            Fs.RemoveOwnedFolder(Partial);
            Add(L"HO-BUILD-003", Partial, {FileNameOf(Partial), Win32ErrorText(error)});
            Partial.clear();
            return false;
        }
        Fs.SetAttributes(Retained, FILE_ATTRIBUTE_HIDDEN); // cosmetic; failure is not fatal
        return WriteMarker();
    }

    bool WriteMarker()
    {
        JsonWriter writer;
        writer.BeginObject();
        writer.MemberInt(L"handoffPartial", 1);
        writer.Member(L"session", In.SessionId);
        writer.MemberInt(L"pid", (int64_t)GetCurrentProcessId());
        writer.Member(L"startedUtc", FormatIsoUtc(In.NowUtc));
        writer.EndObject();
        std::wstring sha;
        std::string text = writer.Text();
        if (!WriteOwnedFile(PathJoin(Partial, PartialMarkerName()), text, sha))
            return false;
        MarkerPresent = true;
        return true;
    }

    bool CreateFolders()
    {
        for (const std::wstring& folder : In.Plan.Folders)
        {
            std::wstring path = Target(folder);
            if (!Fs.CreateFolder(path))
            {
                // An existing entry was not created by this session (C.5.7 step 3.1).
                Add(L"HO-BUILD-003", folder, {folder, Win32ErrorText(GetLastError())});
                return false;
            }
            Created.push_back(CreatedItem{path, true});
        }
        return true;
    }

    // Creates a new file (never replacing), writes, flushes, closes, and re-reads it.
    bool WriteOwnedFile(const std::wstring& path, const std::string& bytes, std::wstring& sha)
    {
        HANDLE file = Fs.Open(path, GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
        if (file == INVALID_HANDLE_VALUE)
        {
            Add(L"HO-BUILD-003", FileNameOf(path), {FileNameOf(path), Win32ErrorText(GetLastError())});
            return false;
        }
        Created.push_back(CreatedItem{path, false});
        DWORD written = 0;
        bool ok = Fs.Write(file, bytes.data(), (DWORD)bytes.size(), &written) && written == bytes.size() && Fs.Flush(file);
        DWORD error = GetLastError();
        ok = Fs.Close(file) && ok;
        if (!ok)
        {
            Add(L"HO-BUILD-003", FileNameOf(path), {FileNameOf(path), Win32ErrorText(error)});
            return false;
        }
        sha = Sha256Hex(bytes.data(), bytes.size());
        std::wstring reread;
        uint64_t size = 0;
        if (!HashFile(path, reread, size) || reread != sha || size != bytes.size())
        {
            Add(L"HO-BUILD-004", FileNameOf(path), {FileNameOf(path)});
            return false;
        }
        return true;
    }

    bool HashFile(const std::wstring& path, std::wstring& sha, uint64_t& size)
    {
        HANDLE file = Fs.Open(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        Sha256 hash;
        std::vector<unsigned char> buffer(CopyChunk);
        size = 0;
        bool ok = true;
        for (;;)
        {
            DWORD read = 0;
            if (!Fs.Read(file, buffer.data(), CopyChunk, &read))
            {
                ok = false;
                break;
            }
            if (read == 0)
                break;
            hash.Update(buffer.data(), read);
            size += read;
            if (Callbacks.StopRequested())
            {
                Cancelled = true;
                ok = false;
                break;
            }
        }
        Fs.Close(file);
        sha = hash.FinishHex();
        return ok && !sha.empty();
    }

    void ForgetCreated(const std::wstring& path)
    {
        for (size_t i = Created.size(); i-- > 0;)
        {
            if (Created[i].Path == path)
            {
                Created.erase(Created.begin() + (std::ptrdiff_t)i);
                return;
            }
        }
    }

    CopyStatus CopyOnce(const PlannedFile& file, std::wstring& sha, DWORD& error)
    {
        std::wstring source = PathJoin(In.Model.WorkingRoot, ToBackslashes(file.SourceRel));
        std::wstring target = Target(file.TargetRel);
        // FILE_SHARE_READ blocks writers for the duration of the copy.
        HANDLE in = Fs.Open(source, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN);
        if (in == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return CopyStatus::Retryable;
        }
        BY_HANDLE_FILE_INFORMATION info;
        if (!Fs.GetInformation(in, info))
        {
            error = GetLastError();
            Fs.Close(in);
            return CopyStatus::Retryable;
        }
        uint64_t size = ((uint64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        if (size != file.Size || CompareFileTime(&info.ftLastWriteTime, &file.LastWrite) != 0)
        {
            Fs.Close(in);
            Add(L"HO-BUILD-002", file.SourceRel, {file.SourceRel + L" (" + LabelText(Catalog, Label::CHANGED_SIZE_OR_TIME) + L")"});
            return CopyStatus::Fatal;
        }
        HANDLE out = Fs.Open(target, GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN);
        if (out == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            Fs.Close(in);
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
            {
                Add(L"HO-BUILD-003", file.TargetRel, {file.TargetRel, Win32ErrorText(error)});
                return CopyStatus::Fatal;
            }
            return CopyStatus::Retryable;
        }
        Created.push_back(CreatedItem{target, false});
        Sha256 hash;
        std::vector<unsigned char> buffer(CopyChunk);
        uint64_t copied = 0, startDone = Done;
        CopyStatus status = CopyStatus::Ok;
        for (;;)
        {
            DWORD read = 0, written = 0;
            if (!Fs.Read(in, buffer.data(), CopyChunk, &read))
            {
                error = GetLastError();
                status = CopyStatus::Retryable;
                break;
            }
            if (read == 0)
                break;
            if (!Fs.Write(out, buffer.data(), read, &written) || written != read)
            {
                error = written != read && GetLastError() == ERROR_SUCCESS ? ERROR_WRITE_FAULT : GetLastError();
                status = CopyStatus::Retryable;
                break;
            }
            hash.Update(buffer.data(), read);
            copied += read;
            Done += read;
            Callbacks.Report(Phase::Copy, Done, In.Plan.TotalBytes, file.TargetRel);
            if (Callbacks.StopRequested())
            {
                status = CopyStatus::Cancelled;
                break;
            }
        }
        if (status == CopyStatus::Ok && copied != size)
        {
            Fs.Close(out);
            Fs.Close(in);
            Add(L"HO-BUILD-002", file.SourceRel, {file.SourceRel + L" (" + LabelText(Catalog, Label::CHANGED_SIZE_OR_TIME) + L")"});
            return CopyStatus::Fatal;
        }
        if (status == CopyStatus::Ok && In.SpecModel.Package.PreserveModifiedTime && !Fs.SetLastWriteTime(out, file.LastWrite))
        {
            error = GetLastError();
            status = CopyStatus::Retryable;
        }
        if (status == CopyStatus::Ok && !Fs.Flush(out))
        {
            error = GetLastError();
            status = CopyStatus::Retryable;
        }
        bool closed = Fs.Close(out) != FALSE;
        if (status == CopyStatus::Ok && !closed)
        {
            error = GetLastError();
            status = CopyStatus::Retryable;
        }
        Fs.Close(in);
        if (status != CopyStatus::Ok)
        {
            // A retried or cancelled file starts again from nothing (C.5.7 step 3.4).
            if (Fs.DeleteOwnedFile(target))
                ForgetCreated(target);
            Done = startDone;
            return status;
        }
        sha = hash.FinishHex();
        std::wstring reread;
        uint64_t rereadSize = 0;
        Callbacks.Report(Phase::Verify, Done, In.Plan.TotalBytes, file.TargetRel);
        if (!HashFile(target, reread, rereadSize))
        {
            if (Cancelled)
                return CopyStatus::Cancelled;
            Add(L"HO-BUILD-004", file.TargetRel, {file.TargetRel});
            return CopyStatus::Fatal;
        }
        if (reread != sha || rereadSize != size)
        {
            Add(L"HO-BUILD-004", file.TargetRel, {file.TargetRel + L" (" + LabelText(Catalog, Label::HASH_MISMATCH) + L")"});
            return CopyStatus::Fatal;
        }
        return CopyStatus::Ok;
    }

    bool CopyFiles()
    {
        for (const PlannedFile& file : In.Plan.Files)
        {
            std::wstring sha;
            for (;;)
            {
                DWORD error = ERROR_SUCCESS;
                CopyStatus status = CopyOnce(file, sha, error);
                if (status == CopyStatus::Ok)
                    break;
                if (status == CopyStatus::Cancelled)
                {
                    Cancelled = true;
                    return false;
                }
                if (status == CopyStatus::Fatal)
                    return false;
                if (!Callbacks.AskRetry(file.SourceRel, error))
                {
                    Add(L"HO-BUILD-003", file.SourceRel, {file.SourceRel, Win32ErrorText(error)});
                    return false;
                }
            }
            Files[FoldKey(file.TargetRel)] = Written{file.Size, sha};
            if (file.Deferred && file.CandidateIndex >= 0 && !CheckDeferred(file))
                return false;
        }
        return true;
    }

    // Online-only files were not read during review; their copies are checked now (C.5.7 step 3.6).
    bool CheckDeferred(const PlannedFile& file)
    {
        Candidate& candidate = In.Model.Candidates[(size_t)file.CandidateIndex];
        InspectOne(candidate, Target(file.TargetRel), In.SpecModel, Images, Pdf);
        candidate.Deferred = false;
        const RuleSpec& rule = In.SpecModel.Rules[(size_t)candidate.RuleIndex];
        const ScannedFile& scanned = In.Model.Files[candidate.FileIndex];
        std::vector<Finding> raw = CheckCandidateContent(candidate, scanned, In.SpecModel, Catalog);
        std::vector<Finding> resolved;
        ResolveFindings(raw, In.SpecModel, &rule, In.Decisions, resolved);
        for (const Finding& finding : resolved)
        {
            if (finding.Sev == Severity::Error && !finding.Overridden)
            {
                Add(L"HO-BUILD-005", file.SourceRel, {file.SourceRel + L": " + FindingMessage(finding, Catalog)});
                return false;
            }
            Result.Findings.push_back(finding);
        }
        return true;
    }

    bool CheckDuplicates()
    {
        if (In.SpecModel.Content.Duplicates == L"allow")
            return true;
        std::map<std::wstring, std::wstring> firstBySha;
        std::vector<Finding> raw;
        for (const PlannedFile& file : In.Plan.Files)
        {
            const Written& written = Files[FoldKey(file.TargetRel)];
            if (written.Bytes == 0)
                continue; // empty files are covered by HO-CONT-004
            auto it = firstBySha.find(written.Sha256);
            if (it == firstBySha.end())
            {
                firstBySha[written.Sha256] = file.TargetRel;
                continue;
            }
            Finding finding = MakeFinding(L"HO-CONT-006", file.SourceRel, {it->second});
            finding.RuleId = file.RuleId;
            // The policy decides the default; "forbid" stops the build before publication (C.4.11).
            finding.Sev = In.SpecModel.Content.Duplicates == L"forbid" ? Severity::Error : Severity::Warning;
            raw.push_back(finding);
        }
        std::vector<Finding> resolved;
        ResolveFindings(raw, In.SpecModel, nullptr, In.Decisions, resolved);
        bool ok = true;
        for (const Finding& finding : resolved)
        {
            Result.Findings.push_back(finding);
            if (finding.Sev == Severity::Error && !finding.Overridden)
                ok = false;
        }
        return ok;
    }

    ManifestFile DescribeFile(const PlannedFile& file)
    {
        const Spec& spec = In.SpecModel;
        ManifestFile entry;
        entry.Path = file.TargetRel;
        const Written& written = Files[FoldKey(file.TargetRel)];
        entry.Bytes = written.Bytes;
        entry.Sha256 = written.Sha256;
        entry.ModifiedUtc = FormatIsoUtc(spec.Package.PreserveModifiedTime ? file.LastWrite : In.NowUtc);
        entry.Rule = file.RuleId;
        entry.Role = file.Role;
        entry.Format = file.Format;
        entry.FormatLabel = FormatLabel(spec.CustomFormats, file.Format);
        entry.Key = file.Key;
        entry.Revision = file.Revision;
        if (spec.Package.IncludeSourcePaths)
            entry.Source = file.SourceRel;
        const RuleSpec* rule = spec.FindRule(file.RuleId);
        if (rule != nullptr)
            entry.RuleTitle = rule->Title;
        if (file.Evidence)
        {
            entry.LicenceFor = file.LicenceFor;
            std::sort(entry.LicenceFor.begin(), entry.LicenceFor.end());
            return entry;
        }
        const Candidate& candidate = In.Model.Candidates[(size_t)file.CandidateIndex];
        if (candidate.PdfInspected)
        {
            entry.HasPdf = true;
            entry.PdfVersion = candidate.Pdf.Version;
            if (candidate.PagesInspected && candidate.Pages.Ok)
            {
                entry.Pages = (int64_t)candidate.Pages.SizesMm.size();
                for (const auto& size : candidate.Pages.SizesMm)
                {
                    std::wstring token = PageSizeText(size.first, size.second, nullptr);
                    if (std::find(entry.PageSizes.begin(), entry.PageSizes.end(), token) == entry.PageSizes.end())
                    {
                        entry.PageSizes.push_back(token);
                        entry.PageSizesLocalized.push_back(PageSizeText(size.first, size.second, &Catalog));
                    }
                }
            }
        }
        if (candidate.ImageInspected && candidate.Image.Decoded)
        {
            entry.HasImage = true;
            entry.Width = candidate.Image.Width;
            entry.Height = candidate.Image.Height;
            entry.DpiKnown = candidate.Image.DpiKnown;
            entry.DpiX = candidate.Image.DpiX;
            entry.DpiY = candidate.Image.DpiY;
            entry.ColorModel = candidate.Image.ColorModel;
        }
        if (rule != nullptr)
        {
            bool evidence = rule->Approval.NeedsEvidence() && candidate.ApprovalEvidence;
            bool confirmed = rule->Approval.NeedsConfirm() && candidate.Approved;
            entry.Approval = evidence && confirmed ? L"evidence+confirmed" : evidence ? L"evidence" : confirmed ? L"confirmed" : L"";
            if (rule->License.Present)
            {
                entry.HasLicence = true;
                entry.Licence = candidate.Licence;
                entry.Licensor = candidate.Licensor;
                entry.LicenceExpires = candidate.LicenceExpires;
                for (const PlannedFile& other : In.Plan.Files)
                    if (other.Evidence && std::find(other.LicenceFor.begin(), other.LicenceFor.end(), file.TargetRel) != other.LicenceFor.end())
                        entry.LicenceEvidence.push_back(other.TargetRel);
            }
        }
        return entry;
    }

    ManifestData MakeManifest()
    {
        const Spec& spec = In.SpecModel;
        ManifestData data;
        data.PackageName = In.Plan.PackageName;
        data.CreatedUtc = FormatIsoUtc(In.NowUtc);
        for (const VariableDef& variable : spec.Variables)
            data.Variables.push_back(std::make_pair(variable.Name, In.Values[variable.Name]));
        data.Variables.push_back(std::make_pair(std::wstring(L"date"), In.Values[L"date"]));
        data.SpecId = spec.Id;
        data.SpecName = spec.Name;
        data.SpecRevision = spec.Revision;
        data.SpecSha256 = spec.Sha256;
        data.GeneratorVersion = In.PluginVersion;
        data.HostVersion = In.HostVersion;
        std::map<std::wstring, std::wstring> targetBySource;
        for (const PlannedFile& file : In.Plan.Files)
        {
            data.Files.push_back(DescribeFile(file));
            targetBySource[FoldKey(file.SourceRel)] = file.TargetRel;
        }
        // Client-facing findings: warnings and infos about delivered files or the
        // package, with package-relative paths only (D-10, C.7.1).
        std::vector<Finding> all = In.Model.Findings;
        all.insert(all.end(), In.Plan.Findings.begin(), In.Plan.Findings.end());
        all.insert(all.end(), Result.Findings.begin(), Result.Findings.end());
        for (const Finding& finding : all)
        {
            if (finding.Sev == Severity::Error)
            {
                if (finding.Overridden)
                    data.Overrides++;
                continue;
            }
            if (finding.Code == L"HO-SEL-003" || finding.Code == L"HO-SEL-001" || finding.Code == L"HO-SEL-002" ||
                finding.Code == L"HO-SEL-004" || finding.Code.compare(0, 8, L"HO-SCAN-") == 0)
                continue;
            Finding copy = finding;
            if (!copy.Item.empty())
            {
                auto it = targetBySource.find(FoldKey(copy.Item));
                if (it == targetBySource.end())
                    continue;
                copy.Item = it->second;
            }
            // Arguments may name working paths; client-facing text uses the package path.
            for (std::wstring& arg : copy.Args)
            {
                auto it = targetBySource.find(FoldKey(arg));
                if (it != targetBySource.end())
                    arg = it->second;
            }
            if (copy.Sev == Severity::Warning)
                data.Warnings++;
            data.Findings.push_back(copy);
        }
        data.Status = data.Warnings > 0 ? L"verifiedWithWarnings" : L"verified";
        return data;
    }

    bool WriteOutputs()
    {
        const Spec& spec = In.SpecModel;
        Result.Manifest = MakeManifest();
        TemplateContext context;
        context.Variables = &In.Values;
        context.Date = TemplateDate(In.Values, In.Now);
        ContentsOptions options;
        options.Title = spec.Package.ContentsTitle.Expand(context);
        for (const NameTemplate& note : spec.Package.Notes)
            options.Notes.push_back(note.Expand(context));
        options.GroupByRule = spec.Package.GroupByRule;
        for (const RuleSpec& rule : spec.Rules)
            options.RuleOrder.push_back(std::make_pair(rule.Id, rule.Title));
        options.DateText = In.Values[L"date"];
        options.JsonName = spec.Package.ManifestJson;
        options.CsvName = spec.Package.ManifestCsv;

        auto write = [&](const std::wstring& name, const std::string& bytes) -> bool {
            std::wstring sha;
            if (!WriteOwnedFile(PathJoin(Partial, name), bytes, sha))
                return false;
            OutputOrder.push_back(name);
            OutputFiles[FoldKey(name)] = Written{bytes.size(), sha};
            Result.Manifest.Outputs.push_back(ManifestOutput{name, bytes.size(), sha});
            return true;
        };
        if (!spec.Package.Contents.empty() && !write(spec.Package.Contents, ContentsText(Result.Manifest, options, Catalog)))
            return false;
        if (!spec.Package.ManifestCsv.empty() && !write(spec.Package.ManifestCsv, ManifestCsvText(Result.Manifest)))
            return false;
        std::string json = ManifestJsonText(Result.Manifest, Catalog);
        std::wstring sha;
        if (!WriteOwnedFile(PathJoin(Partial, spec.Package.ManifestJson), json, sha))
            return false;
        OutputOrder.push_back(spec.Package.ManifestJson);
        OutputFiles[FoldKey(spec.Package.ManifestJson)] = Written{json.size(), sha};
        Result.ManifestSha256 = sha;
        return true;
    }

    bool VerifyTree()
    {
        // The staged tree must equal the plan exactly before it may be published (C.5.7 step 6).
        std::map<std::wstring, uint64_t> expectedFiles;
        for (const PlannedFile& file : In.Plan.Files)
            expectedFiles[FoldKey(file.TargetRel)] = Files[FoldKey(file.TargetRel)].Bytes;
        for (const auto& output : OutputFiles)
            expectedFiles[output.first] = output.second.Bytes;
        std::set<std::wstring> expectedFolders;
        for (const std::wstring& folder : In.Plan.Folders)
            expectedFolders.insert(FoldKey(folder));

        std::map<std::wstring, uint64_t> actualFiles;
        std::set<std::wstring> actualFolders;
        std::deque<std::wstring> pending{std::wstring()};
        while (!pending.empty())
        {
            std::wstring rel = pending.front();
            pending.pop_front();
            WIN32_FIND_DATAW data;
            HANDLE find = FindFirstFileExW(LongPath(PathJoin(rel.empty() ? Partial : Target(rel), L"*")).c_str(),
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
                    actualFolders.insert(FoldKey(child));
                    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                        pending.push_back(child);
                }
                else if (!(rel.empty() && name == PartialMarkerName()))
                    actualFiles[FoldKey(child)] = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
        if (actualFiles != expectedFiles || actualFolders != expectedFolders)
        {
            Add(L"HO-BUILD-004", In.Plan.PackageName, {LabelText(Catalog, Label::TREE_MISMATCH)});
            return false;
        }
        return true;
    }

    void Abandon()
    {
        if (Cancelled)
            Add(L"HO-BUILD-009", L"", {});
        Result.Outcome = Cancelled ? BuildOutcome::Cancelled : BuildOutcome::NotBuilt;
        if (Partial.empty())
            return;
        if (In.KeepFailedPartial)
        {
            if (!MarkerPresent)
                WriteMarker();
            Fs.Close(Retained);
            Retained = INVALID_HANDLE_VALUE;
            Result.PartialPath = Partial;
            return;
        }
        // Only entries this session created are removed, newest first (C.5.7 step 11).
        bool clean = true;
        for (size_t i = Created.size(); i-- > 0;)
        {
            const CreatedItem& item = Created[i];
            BOOL removed = item.Folder ? Fs.RemoveOwnedFolder(item.Path) : Fs.DeleteOwnedFile(item.Path);
            clean = clean && removed;
        }
        clean = clean && Fs.DeleteByHandle(Retained);
        Fs.Close(Retained);
        Retained = INVALID_HANDLE_VALUE;
        if (!clean)
        {
            Add(L"HO-BUILD-008", Partial, {Partial});
            Result.PartialPath = Partial;
        }
    }

    void Publish()
    {
        // The marker identifies unfinished folders; a complete package must not carry it.
        std::wstring marker = PathJoin(Partial, PartialMarkerName());
        if (!Fs.DeleteOwnedFile(marker))
        {
            Add(L"HO-BUILD-003", PartialMarkerName(), {PartialMarkerName(), Win32ErrorText(GetLastError())});
            Abandon();
            return;
        }
        ForgetCreated(marker);
        MarkerPresent = false;
        Fs.SetAttributes(Retained, FILE_ATTRIBUTE_NORMAL);
        Callbacks.Report(Phase::Publish, 0, 1, In.Plan.PackageName);
        HANDLE staging = Fs.Open(In.StagingRoot, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS);
        BY_HANDLE_FILE_INFORMATION current;
        bool identity = staging != INVALID_HANDLE_VALUE && Fs.GetInformation(Retained, current) &&
                        current.dwVolumeSerialNumber == RetainedInfo.dwVolumeSerialNumber &&
                        current.nFileIndexHigh == RetainedInfo.nFileIndexHigh &&
                        current.nFileIndexLow == RetainedInfo.nFileIndexLow;
        DWORD error = identity ? ERROR_SUCCESS : GetLastError();
        bool published = false, collision = false;
        for (int attempt = 0; identity && attempt <= 10; attempt++)
        {
            if (Fs.RenameRelative(Retained, staging, In.Plan.PackageName))
            {
                published = true;
                break;
            }
            error = GetLastError();
            if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
            {
                collision = true;
                break;
            }
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED)
                break;
            // An indexer or antivirus scanner briefly holding a file blocks a directory rename.
            for (int wait = 0; wait < 5 && !Callbacks.StopRequested(); wait++)
                Fs.Pause(100);
            if (Callbacks.StopRequested())
            {
                Cancelled = true;
                break;
            }
        }
        if (staging != INVALID_HANDLE_VALUE)
            Fs.Close(staging);
        if (Cancelled && !published)
        {
            Abandon();
            return;
        }
        if (!published)
        {
            Fs.SetAttributes(Retained, FILE_ATTRIBUTE_HIDDEN);
            if (!MarkerPresent)
                WriteMarker();
            Fs.Close(Retained);
            Retained = INVALID_HANDLE_VALUE;
            Result.Outcome = BuildOutcome::Retained;
            Result.PartialPath = Partial;
            if (collision)
                Add(L"HO-BUILD-001", In.Plan.PackageName, {In.Plan.PackageName});
            else
                Add(L"HO-BUILD-006", In.Plan.PackageName,
                    {error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED ? LabelText(Catalog, Label::PUBLISH_BLOCKED)
                                                                                    : Win32ErrorText(error),
                     Partial});
            return;
        }
        Fs.Close(Retained);
        Retained = INVALID_HANDLE_VALUE;
        Result.Outcome = BuildOutcome::Published;
        Result.PackagePath = PackagePath;
        Callbacks.Report(Phase::Publish, 1, 1, In.Plan.PackageName);
        WriteBuildRecord();
    }

    void WriteBuildRecord()
    {
        if (!In.SpecModel.Package.BuildRecordBeside)
            return;
        BuildRecordData record;
        record.PackageName = In.Plan.PackageName;
        record.PackagePath = PackagePath;
        record.ManifestSha256 = Result.ManifestSha256;
        record.SessionId = In.SessionId;
        record.User = In.User;
        record.Machine = In.Machine;
        record.StartedUtc = FormatIsoUtc(In.NowUtc);
        FILETIME finished;
        GetSystemTimeAsFileTime(&finished);
        record.FinishedUtc = FormatIsoUtc(finished);
        record.HostVersion = In.HostVersion;
        record.PluginVersion = In.PluginVersion;
        record.WorkingRoot = In.Model.WorkingRoot;
        record.Scope = In.Scope;
        record.SpecPath = In.SpecPath;
        record.SpecSha256 = In.SpecModel.Sha256;
        record.SpecText = In.SpecText;
        for (const DecisionRecord& decision : In.Decisions.Log)
            record.Decisions.push_back(BuildRecordDecision{decision.Action, decision.Source, decision.RuleId, decision.Code,
                                                           decision.Reason, decision.User, decision.Utc});
        for (const PlannedFile& file : In.Plan.Files)
            record.Files.push_back(std::make_pair(file.SourceRel, std::make_pair(file.TargetRel, Files[FoldKey(file.TargetRel)].Sha256)));
        record.Findings = In.Model.Findings;
        record.Findings.insert(record.Findings.end(), In.Plan.Findings.begin(), In.Plan.Findings.end());
        record.Findings.insert(record.Findings.end(), Result.Findings.begin(), Result.Findings.end());
        std::string text = BuildRecordText(record, Catalog);
        std::wstring path = PathJoin(In.StagingRoot, In.Plan.PackageName + L".handoff-build.json");
        HANDLE file = Fs.Open(path, GENERIC_WRITE, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL);
        DWORD written = 0;
        bool ok = file != INVALID_HANDLE_VALUE && Fs.Write(file, text.data(), (DWORD)text.size(), &written) &&
                  written == text.size() && Fs.Flush(file);
        DWORD error = GetLastError();
        if (file != INVALID_HANDLE_VALUE)
            ok = Fs.Close(file) && ok;
        if (!ok)
        {
            // The package itself stays valid without its record (C.5.7 step 10).
            Add(L"HO-BUILD-007", path, {Win32ErrorText(error)});
            if (file != INVALID_HANDLE_VALUE)
                Fs.DeleteOwnedFile(path);
            return;
        }
        Result.BuildRecordPath = path;
        Result.BuildRecordWritten = true;
    }
};

} // namespace

BuildResult BuildPackage(BuildInput& input, IHandoffFileSystem& fs, IBuildCallbacks& callbacks, IImageInspector* images,
                         IPdfPageInspector* pdf, const ITextCatalog& catalog)
{
    BuildResult result;
    Builder builder(input, fs, callbacks, images, pdf, catalog, result);
    builder.Run();
    // Build findings take the same severity overrides as review findings.
    std::vector<Finding> resolved;
    for (Finding& finding : result.Findings)
    {
        const RuleSpec* rule = finding.RuleId.empty() ? nullptr : input.SpecModel.FindRule(finding.RuleId);
        std::vector<Finding> one{finding};
        ResolveFindings(one, input.SpecModel, rule, input.Decisions, resolved);
    }
    result.Findings.swap(resolved);
    return result;
}

std::vector<StalePartial> FindPartialFolders(const std::wstring& stagingRoot, IHandoffFileSystem& fs)
{
    std::vector<StalePartial> partials;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileExW(LongPath(PathJoin(stagingRoot, L".*.partial")).c_str(), FindExInfoBasic, &data,
                                   FindExSearchNameMatch, NULL, 0);
    if (find == INVALID_HANDLE_VALUE)
        return partials;
    do
    {
        std::wstring name = data.cFileName;
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            name.find(L".handoff-") == std::wstring::npos)
            continue;
        std::wstring path = PathJoin(stagingRoot, name);
        if (fs.GetAttributes(PathJoin(path, PartialMarkerName())) == INVALID_FILE_ATTRIBUTES)
            continue; // without a marker the folder is not ours
        StalePartial partial;
        partial.Path = path;
        // A live build holds the folder without FILE_SHARE_DELETE, so this open fails.
        HANDLE probe = fs.Open(path, DELETE | SYNCHRONIZE, 0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
        if (probe == INVALID_HANDLE_VALUE)
            partial.Live = GetLastError() == ERROR_SHARING_VIOLATION;
        else
            fs.Close(probe);
        partials.push_back(partial);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return partials;
}

bool RemovePartialFolder(const std::wstring& path, IHandoffFileSystem& fs, std::wstring& failedPath)
{
    // Post-order removal without recursion; links are removed as links, never followed.
    std::vector<std::wstring> folders{path};
    std::vector<std::wstring> order;
    while (!folders.empty())
    {
        std::wstring folder = folders.back();
        folders.pop_back();
        order.push_back(folder);
        if (order.size() > 100000)
            break;
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileExW(LongPath(PathJoin(folder, L"*")).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                       NULL, 0);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            std::wstring child = PathJoin(folder, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                {
                    if (!fs.RemoveOwnedFolder(child))
                        failedPath = child;
                }
                else
                    folders.push_back(child);
            }
            else
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(LongPath(child).c_str(), data.dwFileAttributes & ~(DWORD)FILE_ATTRIBUTE_READONLY);
                if (!fs.DeleteOwnedFile(child))
                    failedPath = child;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    for (size_t i = order.size(); i-- > 0;)
        if (!fs.RemoveOwnedFolder(order[i]) && failedPath.empty())
            failedPath = order[i];
    return failedPath.empty();
}

} // namespace handoff
