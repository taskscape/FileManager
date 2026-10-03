// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Staging, publication, outputs, and verification (handoff-spec.md C.12.1
// groups Build, Outputs, Verify), including a fault injected at every
// file-system call of a build.

#include "TestFramework.h"
#include "Fixtures.h"

#include <algorithm>

#include "build.h"
#include "csv.h"
#include "verify.h"
#include "text_util.h"

using namespace handoff;

namespace
{

class FaultFs : public Win32FileSystem
{
public:
    int Calls = 0;
    int FailAt = -1;
    DWORD FailError = ERROR_DISK_FULL;
    const char* FailOnlyFirst = nullptr; // fail the first call with this name, once
    int RenameSharingFailures = 0;
    bool RenameCollision = false;
    std::string Failed;

    bool Fail(const char* name)
    {
        Calls++;
        bool fail = Calls == FailAt;
        if (FailOnlyFirst != nullptr && strcmp(FailOnlyFirst, name) == 0)
        {
            fail = true;
            FailOnlyFirst = nullptr;
        }
        if (fail)
        {
            Failed = name;
            SetLastError(FailError);
        }
        return fail;
    }

    std::wstring FailOpenContaining; // fail one open of a matching path with a sharing violation

    HANDLE Open(const std::wstring& path, DWORD access, DWORD share, DWORD disposition, DWORD flags) override
    {
        if (!FailOpenContaining.empty() && path.find(FailOpenContaining) != std::wstring::npos)
        {
            FailOpenContaining.clear();
            SetLastError(ERROR_SHARING_VIOLATION);
            return INVALID_HANDLE_VALUE;
        }
        return Fail("Open") ? INVALID_HANDLE_VALUE : Win32FileSystem::Open(path, access, share, disposition, flags);
    }
    BOOL Read(HANDLE file, void* buffer, DWORD length, DWORD* read) override
    {
        return Fail("Read") ? FALSE : Win32FileSystem::Read(file, buffer, length, read);
    }
    BOOL Write(HANDLE file, const void* buffer, DWORD length, DWORD* written) override
    {
        return Fail("Write") ? FALSE : Win32FileSystem::Write(file, buffer, length, written);
    }
    BOOL Flush(HANDLE file) override { return Fail("Flush") ? FALSE : Win32FileSystem::Flush(file); }
    BOOL Close(HANDLE file) override
    {
        // A failed close still releases the handle, as CloseHandle does on real I/O errors.
        bool fail = Fail("Close");
        BOOL closed = Win32FileSystem::Close(file);
        if (fail)
        {
            SetLastError(FailError);
            return FALSE;
        }
        return closed;
    }
    BOOL SetLastWriteTime(HANDLE file, const FILETIME& time) override
    {
        return Fail("SetLastWriteTime") ? FALSE : Win32FileSystem::SetLastWriteTime(file, time);
    }
    BOOL GetInformation(HANDLE file, BY_HANDLE_FILE_INFORMATION& information) override
    {
        return Fail("GetInformation") ? FALSE : Win32FileSystem::GetInformation(file, information);
    }
    BOOL SetAttributes(HANDLE file, DWORD attributes) override
    {
        return Fail("SetAttributes") ? FALSE : Win32FileSystem::SetAttributes(file, attributes);
    }
    BOOL CreateFolder(const std::wstring& path) override { return Fail("CreateFolder") ? FALSE : Win32FileSystem::CreateFolder(path); }
    BOOL RenameRelative(HANDLE item, HANDLE directory, const std::wstring& name) override
    {
        if (Fail("RenameRelative"))
            return FALSE;
        if (RenameCollision)
        {
            SetLastError(ERROR_ALREADY_EXISTS);
            return FALSE;
        }
        if (RenameSharingFailures > 0)
        {
            RenameSharingFailures--;
            SetLastError(ERROR_SHARING_VIOLATION);
            return FALSE;
        }
        return Win32FileSystem::RenameRelative(item, directory, name);
    }
    BOOL DeleteOwnedFile(const std::wstring& path) override { return Fail("DeleteOwnedFile") ? FALSE : Win32FileSystem::DeleteOwnedFile(path); }
    BOOL RemoveOwnedFolder(const std::wstring& path) override
    {
        return Fail("RemoveOwnedFolder") ? FALSE : Win32FileSystem::RemoveOwnedFolder(path);
    }
    BOOL DeleteByHandle(HANDLE item) override { return Fail("DeleteByHandle") ? FALSE : Win32FileSystem::DeleteByHandle(item); }
    DWORD GetAttributes(const std::wstring& path) override
    {
        return Fail("GetAttributes") ? INVALID_FILE_ATTRIBUTES : Win32FileSystem::GetAttributes(path);
    }
    void Pause(DWORD) override {}
};

struct Callbacks : IBuildCallbacks
{
    int StopAfter = -1;
    int Reports = 0;
    int Retries = 0;
    bool RetryOnce = false;
    void Report(Phase, uint64_t, uint64_t, const std::wstring&) override { Reports++; }
    bool StopRequested() override { return StopAfter >= 0 && Reports >= StopAfter; }
    bool AskRetry(const std::wstring&, DWORD) override { return RetryOnce && ++Retries == 1; }
};

struct Prepared
{
    std::wstring Working;
    SpecLoadResult Loaded;
    ReviewModel Model;
    ReviewerDecisions Decisions;
    Variables Values;
    PackagePlan Plan;
    SYSTEMTIME Now = {};
    std::unique_ptr<IImageInspector> Images = CreateWicImageInspector();
    std::unique_ptr<IPdfPageInspector> Pdf = CreateWinRtPdfPageInspector();

    void Prepare(const std::wstring& working, const std::string& specText, const std::map<std::wstring, std::wstring>& values,
                 const std::vector<std::pair<std::wstring, std::wstring>>& approvals)
    {
        Working = working;
        Loaded = fixtures::ParseSpecText(specText);
        ScanInput input;
        input.WorkingRoot = working;
        NullProgress progress;
        ScanResult scan = ScanWorkingMaterial(input, progress);
        Model = Assemble(Loaded.Model, scan, working);
        InspectCandidates(Model, Loaded.Model, Images.get(), Pdf.get(), progress);
        for (const auto& approval : approvals)
            Decisions.Approved.insert(ReviewerDecisions::Key(approval.first, approval.second));
        Now.wYear = 2026, Now.wMonth = 10, Now.wDay = 3, Now.wHour = 14, Now.wMinute = 22;
        Evaluate(Model, Loaded.Model, Decisions, Now, DefaultCatalog());
        Values = BuildVariables(Loaded.Model, values, Now);
        Plan = PlanPackage(Model, Loaded.Model, Values, Now, 0, Decisions, DefaultCatalog());
    }

    BuildInput Input(const std::wstring& staging) const
    {
        BuildInput input;
        input.SpecModel = Loaded.Model;
        input.Model = Model;
        input.Plan = Plan;
        input.Decisions = Decisions;
        input.Values = Values;
        input.StagingRoot = staging;
        input.Now = Now;
        SystemTimeToFileTime(&Now, &input.NowUtc);
        input.SessionId = L"{0A1B2C3D-0000-4000-8000-000000000001}";
        input.User = L"TEST\\reviewer";
        input.Machine = L"TESTBOX";
        input.HostVersion = L"6.0.0";
        input.PluginVersion = L"1.0.0";
        input.SpecPath = L"spec.handoff.json";
        input.SpecText = L"{}";
        input.Scope = L"all";
        return input;
    }
};

std::map<std::wstring, std::wstring> Snapshot(const std::wstring& root)
{
    std::map<std::wstring, std::wstring> hashes;
    for (const std::wstring& rel : fixtures::ListFiles(root))
        hashes[rel] = fixtures::FileSha(PathJoin(root, rel));
    return hashes;
}

bool HasCode(const std::vector<Finding>& findings, const wchar_t* code, const std::wstring& item = std::wstring())
{
    for (const Finding& finding : findings)
        if (finding.Code == code && (item.empty() || EqualsNoCase(finding.Item, item)))
            return true;
    return false;
}

std::string SmallSpec()
{
    return "{ \"handoffSpec\": 1, \"id\": \"t.build\", \"name\": \"Build\", \"revision\": \"1\","
           " \"variables\": { \"client\": { \"required\": true } },"
           " \"package\": { \"folderName\": \"{client}_Package\" },"
           " \"rules\": ["
           "  { \"id\": \"docs\", \"title\": \"Documents\", \"required\": true, \"select\": { \"include\": [\"Docs/**/*.pdf\"] }, \"formats\": [\"pdf\"], \"target\": { \"folder\": \"PDF\" } },"
           "  { \"id\": \"text\", \"title\": \"Text\", \"select\": { \"include\": [\"Text/*.txt\"] }, \"target\": { \"folder\": \"Text/Notes\" } }"
           " ] }";
}

void SmallTree(const std::wstring& working)
{
    fixtures::WriteBytes(PathJoin(working, L"Docs\\a.pdf"), fixtures::MakePdf({{fixtures::A4W, fixtures::A4H}}));
    fixtures::WriteBytes(PathJoin(working, L"Docs\\sub\\b.pdf"), fixtures::MakePdf({{fixtures::A4W, fixtures::A4H}, {fixtures::A4W, fixtures::A4H}}));
    fixtures::WriteBytes(PathJoin(working, L"Text\\note.txt"), std::string(3 * 1024 * 1024 + 17, 'n')); // spans several copy chunks
}

} // namespace

HT_TEST(Build, PublishesTheQuickStartPackage)
{
    std::wstring working = fixtures::NewDir(L"build-quick");
    std::wstring staging = fixtures::NewDir(L"build-staging");
    fixtures::BuildQuickStartTree(working);
    std::map<std::wstring, std::wstring> before = Snapshot(working);
    Prepared prepared;
    prepared.Prepare(working, fixtures::ReadTemplate(L"client-delivery-example.handoff.json"),
                     {{L"client", L"ACME"}, {L"project", L"Rebrand"}},
                     {{L"Approved/Brand-Guidelines_v4.pdf", L"approved-pdfs"}, {L"Approved/Stationery_v2.pdf", L"approved-pdfs"}});
    HT_CHECK(ComputeGate(prepared.Model, prepared.Plan, prepared.Loaded.Model).CanBuild);
    BuildInput input = prepared.Input(staging);
    Win32FileSystem fs;
    Callbacks callbacks;
    BuildResult result = BuildPackage(input, fs, callbacks, prepared.Images.get(), prepared.Pdf.get(), DefaultCatalog());
    HT_CHECK(result.Outcome == BuildOutcome::Published);
    for (const Finding& finding : result.Findings)
        fprintf(stderr, "    build: %ls %ls\n", finding.Code.c_str(), FindingMessage(finding, DefaultCatalog()).c_str());
    std::wstring package = PathJoin(staging, L"ACME-Rebrand_Delivery_2026-10-03_R01");
    HT_CHECK(result.PackagePath == package && fixtures::Exists(package));
    std::vector<std::wstring> expected = {
        L"01_Approved_PDFs/ACME-Rebrand_Brand-Guidelines_v4.pdf", L"01_Approved_PDFs/ACME-Rebrand_Stationery_v2.pdf",
        L"02_Source_Artwork/Campaign/Hero.psd", L"02_Source_Artwork/Logo/ACME-logo-mono.svg",
        L"02_Source_Artwork/Logo/ACME-logo-primary.ai", L"03_Licensed_Assets/Fonts/Inter-Regular.otf",
        L"03_Licensed_Assets/Images/city-skyline.jpg", L"03_Licensed_Assets/Licences/city-skyline.licence.pdf",
        L"CONTENTS.txt", L"manifest.csv", L"manifest.json"};
    std::sort(expected.begin(), expected.end());
    HT_CHECK(fixtures::ListFiles(package) == expected);
    HT_CHECK(fixtures::FileSha(PathJoin(package, L"03_Licensed_Assets\\Images\\city-skyline.jpg")) ==
             fixtures::FileSha(PathJoin(working, L"Assets\\Stock\\city-skyline.jpg")));
    // Only the package and its build record remain in the staging location.
    std::vector<std::wstring> stagingEntries = fixtures::ListEntries(staging);
    HT_CHECK(std::count_if(stagingEntries.begin(), stagingEntries.end(), [](const std::wstring& e) { return e.find(L'/') == e.size() - 1 || e.find(L'/') == std::wstring::npos; }) == 2);
    HT_CHECK(result.BuildRecordWritten && fixtures::Exists(PathJoin(staging, L"ACME-Rebrand_Delivery_2026-10-03_R01.handoff-build.json")));
    HT_CHECK(Snapshot(working) == before);

    // manifest.json is authoritative and parses back.
    std::string json = fixtures::ReadBytes(PathJoin(package, L"manifest.json"));
    HT_CHECK(json.compare(0, 3, "\xEF\xBB\xBF") != 0 && json.find('\r') == std::string::npos);
    ParsedManifest manifest;
    std::wstring error;
    HT_CHECK(ParseManifest((const unsigned char*)json.data(), json.size(), manifest, error));
    HT_CHECK(manifest.Files.size() == 8 && manifest.Outputs.size() == 2 && manifest.PackageName == L"ACME-Rebrand_Delivery_2026-10-03_R01");
    HT_CHECK(result.ManifestSha256 == Sha256Hex(json.data(), json.size()));
    for (const ManifestOutput& output : manifest.Outputs)
        HT_CHECK(fixtures::FileSha(PathJoin(package, output.Path)) == output.Sha256);
    for (const ManifestEntry& entry : manifest.Files)
        HT_CHECK(fixtures::FileSha(PathJoin(package, ToBackslashes(entry.Path))) == entry.Sha256);
    HT_CHECK(json.find("\"approval\": \"evidence+confirmed\"") != std::string::npos);
    HT_CHECK(json.find("\"licenceFor\"") != std::string::npos && json.find("\"licence\": \"OFL-1.1\"") != std::string::npos);
    HT_CHECK(json.find("Approved/") == std::string::npos); // no working paths in client outputs

    std::string csv = fixtures::ReadBytes(PathJoin(package, L"manifest.csv"));
    HT_CHECK(csv.compare(0, 3, "\xEF\xBB\xBF") == 0 && csv.find("Path,Bytes,SHA-256") == 3 && csv.find("\r\n") != std::string::npos);
    std::string contents = fixtures::ReadBytes(PathJoin(package, L"CONTENTS.txt"));
    HT_CHECK(contents.compare(0, 3, "\xEF\xBB\xBF") == 0);
    HT_CHECK(contents.find("ACME Rebrand \xE2\x80\x94 delivery contents\r\n") == 3);
    HT_CHECK(contents.find("Package:        ACME-Rebrand_Delivery_2026-10-03_R01") != std::string::npos);
    HT_CHECK(contents.find("01_Approved_PDFs/\r\n") != std::string::npos && contents.find("PDF, pages: 3, A4 portrait") != std::string::npos);
    HT_CHECK(contents.find("listed in manifest.csv and manifest.json.") != std::string::npos);

    // A freshly built package verifies cleanly, and the build record authenticates the manifest.
    VerifyInput verify;
    verify.PackageRoot = package;
    verify.SpecModel = &prepared.Loaded.Model;
    verify.BuildRecord = ReadBuildRecordBeside(package);
    NullProgress progress;
    VerifyResult verified = VerifyPackage(verify, progress, prepared.Images.get(), prepared.Pdf.get(), DefaultCatalog());
    HT_CHECK(verify.BuildRecord.Found && verified.ManifestAuthenticated);
    HT_CHECK(verified.Status == L"verified" || verified.Status == L"verifiedWithWarnings");
    for (const Finding& finding : verified.Findings)
    {
        if (finding.Sev == Severity::Error || finding.Sev == Severity::Warning)
            fprintf(stderr, "    verify: %ls %ls\n", finding.Code.c_str(), finding.Item.c_str());
        HT_CHECK(finding.Sev != Severity::Error);
    }
}

HT_TEST(Build, LeavesNoPackageWhenAnyFileSystemCallFails)
{
    std::wstring working = fixtures::NewDir(L"fault-working");
    SmallTree(working);
    std::map<std::wstring, std::wstring> before = Snapshot(working);
    Prepared prepared;
    prepared.Prepare(working, SmallSpec(), {{L"client", L"X"}}, {});
    HT_CHECK(ComputeGate(prepared.Model, prepared.Plan, prepared.Loaded.Model).CanBuild);

    FaultFs counting;
    Callbacks quiet;
    BuildInput baseline = prepared.Input(fixtures::NewDir(L"fault-count"));
    BuildResult reference = BuildPackage(baseline, counting, quiet, nullptr, nullptr, DefaultCatalog());
    HT_CHECK(reference.Outcome == BuildOutcome::Published);
    int calls = counting.Calls;
    HT_CHECK(calls > 20);
    int published = 0, failed = 0;
    for (int at = 1; at <= calls; at++)
    {
        std::wstring staging = fixtures::NewDir(L"fault");
        FaultFs fs;
        fs.FailAt = at;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        std::wstring package = PathJoin(staging, L"X_Package");
        if (result.Outcome == BuildOutcome::Published)
        {
            // Faults the design tolerates (hidden attribute, build record) still publish a complete package.
            published++;
            if (!HT_CHECK(fixtures::ListFiles(package).size() == 6))
                fprintf(stderr, "  fault %d at %s published %zu files\n", at, fs.Failed.c_str(), fixtures::ListFiles(package).size());
        }
        else
        {
            failed++;
            if (!HT_CHECK(!fixtures::Exists(package)))
                fprintf(stderr, "  fault %d at %s left a package\n", at, fs.Failed.c_str());
            if (!result.PartialPath.empty())
            {
                HT_CHECK(fixtures::Exists(result.PartialPath));
                std::wstring failedPath;
                Win32FileSystem cleanup;
                HT_CHECK(RemovePartialFolder(result.PartialPath, cleanup, failedPath));
            }
            std::vector<std::wstring> left = fixtures::ListEntries(staging);
            if (!HT_CHECK(left.empty()))
                fprintf(stderr, "  fault %d at %s left %ls\n", at, fs.Failed.c_str(), left.empty() ? L"" : left[0].c_str());
        }
    }
    HT_CHECK(failed > 0);
    HT_CHECK(Snapshot(working) == before); // working material is never modified
}

HT_TEST(Build, RetriesCancelsAndRefusesChangedSources)
{
    std::wstring working = fixtures::NewDir(L"retry-working");
    SmallTree(working);
    Prepared prepared;
    prepared.Prepare(working, SmallSpec(), {{L"client", L"X"}}, {});

    // One failed read, then Retry: the file restarts and the package publishes.
    {
        std::wstring staging = fixtures::NewDir(L"retry");
        FaultFs fs;
        fs.FailOpenContaining = L"\\Text\\note.txt"; // the working source, not its staged copy
        Callbacks callbacks;
        callbacks.RetryOnce = true;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::Published && callbacks.Retries == 1);
    }
    // Cancel in the middle of copying: nothing remains.
    {
        std::wstring staging = fixtures::NewDir(L"cancel");
        Win32FileSystem fs;
        Callbacks callbacks;
        callbacks.StopAfter = 3;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::Cancelled && HasCode(result.Findings, L"HO-BUILD-009"));
        HT_CHECK(fixtures::ListEntries(staging).empty());
    }
    // A source edited after the review stops the build.
    {
        std::wstring staging = fixtures::NewDir(L"changed");
        fixtures::WriteBytes(PathJoin(working, L"Docs\\a.pdf"), fixtures::MakePdf({{fixtures::A3W, fixtures::A3H}}));
        Win32FileSystem fs;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::NotBuilt && HasCode(result.Findings, L"HO-BUILD-002"));
        HT_CHECK(fixtures::ListEntries(staging).empty());
    }
}

HT_TEST(Build, PublicationRetriesKeepsBlockedPackagesAndDetectsPartials)
{
    std::wstring working = fixtures::NewDir(L"publish-working");
    SmallTree(working);
    Prepared prepared;
    prepared.Prepare(working, SmallSpec(), {{L"client", L"X"}}, {});
    {
        std::wstring staging = fixtures::NewDir(L"publish-retry");
        FaultFs fs;
        fs.RenameSharingFailures = 3;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        HT_CHECK(BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog()).Outcome == BuildOutcome::Published);
    }
    {
        std::wstring staging = fixtures::NewDir(L"publish-blocked");
        FaultFs fs;
        fs.RenameSharingFailures = 100;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::Retained && HasCode(result.Findings, L"HO-BUILD-006"));
        HT_CHECK(!fixtures::Exists(PathJoin(staging, L"X_Package")) && fixtures::Exists(PathJoin(result.PartialPath, L".handoff-partial")));
        Win32FileSystem real;
        std::vector<StalePartial> partials = FindPartialFolders(staging, real);
        HT_CHECK(partials.size() == 1 && !partials[0].Live);
        // A folder held without FILE_SHARE_DELETE belongs to a live build.
        HANDLE live = CreateFileW(LongPath(result.PartialPath).c_str(), FILE_READ_ATTRIBUTES | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
        partials = FindPartialFolders(staging, real);
        HT_CHECK(partials.size() == 1 && partials[0].Live);
        CloseHandle(live);
        std::wstring failedPath;
        HT_CHECK(RemovePartialFolder(result.PartialPath, real, failedPath) && fixtures::ListEntries(staging).empty());
    }
    {
        std::wstring staging = fixtures::NewDir(L"publish-collision");
        FaultFs fs;
        fs.RenameCollision = true;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::Retained && HasCode(result.Findings, L"HO-BUILD-001"));
        std::wstring failedPath;
        Win32FileSystem real;
        RemovePartialFolder(result.PartialPath, real, failedPath);
    }
    {
        std::wstring staging = fixtures::NewDir(L"publish-exists");
        CreateDirectoryW(LongPath(PathJoin(staging, L"X_Package")).c_str(), NULL);
        Win32FileSystem fs;
        Callbacks callbacks;
        BuildInput input = prepared.Input(staging);
        BuildResult result = BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog());
        HT_CHECK(result.Outcome == BuildOutcome::NotBuilt && HasCode(result.Findings, L"HO-BUILD-001"));
        HT_CHECK(fixtures::ListEntries(staging).size() == 1);
    }
}

HT_TEST(Verify, DetectsTamperingAndSpecificationChanges)
{
    std::wstring working = fixtures::NewDir(L"verify-working");
    SmallTree(working);
    Prepared prepared;
    prepared.Prepare(working, SmallSpec(), {{L"client", L"X"}}, {});
    std::wstring staging = fixtures::NewDir(L"verify");
    Win32FileSystem fs;
    Callbacks callbacks;
    BuildInput input = prepared.Input(staging);
    HT_CHECK(BuildPackage(input, fs, callbacks, nullptr, nullptr, DefaultCatalog()).Outcome == BuildOutcome::Published);
    std::wstring package = PathJoin(staging, L"X_Package");
    NullProgress progress;
    auto run = [&](const Spec* spec) {
        VerifyInput verify;
        verify.PackageRoot = package;
        verify.SpecModel = spec;
        verify.BuildRecord = ReadBuildRecordBeside(package);
        return VerifyPackage(verify, progress, nullptr, nullptr, DefaultCatalog());
    };
    VerifyResult clean = run(&prepared.Loaded.Model);
    HT_CHECK(clean.Status == L"verified" && clean.ManifestAuthenticated && clean.FilesChecked == 5);

    std::string pdf = fixtures::ReadBytes(PathJoin(package, L"PDF\\a.pdf"));
    pdf[pdf.size() / 2] ^= 0x20; // same size, different bytes
    fixtures::WriteBytes(PathJoin(package, L"PDF\\a.pdf"), pdf);
    DeleteFileW(LongPath(PathJoin(package, L"PDF\\sub\\b.pdf")).c_str());
    DeleteFileW(LongPath(PathJoin(package, L"PDF\\b.pdf")).c_str());
    fixtures::WriteBytes(PathJoin(package, L"extra.txt"), "extra");
    fixtures::WriteBytes(PathJoin(package, L"Text\\Thumbs.db"), "thumbs");
    std::string contents = fixtures::ReadBytes(PathJoin(package, L"CONTENTS.txt"));
    fixtures::WriteBytes(PathJoin(package, L"CONTENTS.txt"), contents + "x");
    VerifyResult tampered = run(&prepared.Loaded.Model);
    HT_CHECK(tampered.Status == L"failed");
    HT_CHECK(HasCode(tampered.Findings, L"HO-VER-003", L"PDF/a.pdf"));
    HT_CHECK(HasCode(tampered.Findings, L"HO-VER-002"));
    HT_CHECK(HasCode(tampered.Findings, L"HO-VER-004", L"extra.txt"));
    HT_CHECK(HasCode(tampered.Findings, L"HO-VER-006", L"Text/Thumbs.db"));
    HT_CHECK(HasCode(tampered.Findings, L"HO-VER-005", L"CONTENTS.txt"));

    VerifyResult noSpec = run(nullptr);
    HT_CHECK(HasCode(noSpec.Findings, L"HO-VER-011"));
    Spec changed = prepared.Loaded.Model;
    changed.Sha256 = L"0000000000000000000000000000000000000000000000000000000000000000";
    HT_CHECK(HasCode(run(&changed).Findings, L"HO-VER-010"));

    std::string manifest = fixtures::ReadBytes(PathJoin(package, L"manifest.json"));
    fixtures::WriteBytes(PathJoin(package, L"manifest.json"), manifest + "\n");
    HT_CHECK(HasCode(run(&prepared.Loaded.Model).Findings, L"HO-VER-007"));
    fixtures::WriteBytes(PathJoin(package, L"manifest.json"), "{ \"not\": \"a manifest\" }");
    VerifyResult invalid = run(&prepared.Loaded.Model);
    HT_CHECK(!invalid.ManifestOk && HasCode(invalid.Findings, L"HO-VER-001") && invalid.Status == L"failed");
}

HT_TEST(Outputs, CsvGuardsFormulasAndContentsGroupsByRule)
{
    ManifestData data;
    data.PackageName = L"P";
    data.SpecName = L"S";
    ManifestFile file;
    file.Path = L"-danger/=cmd.txt";
    file.Bytes = 2048;
    file.Sha256 = std::wstring(64, L'a');
    file.Rule = L"r";
    file.RuleTitle = L"Rule";
    file.FormatLabel = L"Text";
    data.Files.push_back(file);
    std::string csv = ManifestCsvText(data);
    HT_CHECK(csv.find("'-danger/=cmd.txt,2048,") != std::string::npos);
    ContentsOptions options;
    options.Title = L"Title";
    options.GroupByRule = true;
    options.RuleOrder = {{L"r", L"Rule"}};
    options.JsonName = L"manifest.json";
    std::string contents = ContentsText(data, options, DefaultCatalog());
    HT_CHECK(contents.find("\r\nRule\r\n  -danger/=cmd.txt  2 KB   Text\r\n") != std::string::npos);
    HT_CHECK(contents.find("listed in manifest.json.") != std::string::npos);
}
