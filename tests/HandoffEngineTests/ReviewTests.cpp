// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Scanning, selection, inspection, evidence, evaluation, and planning
// (handoff-spec.md C.12.1 groups Scanner, Selection, PDF, Images, Evidence, Planner).

#include "TestFramework.h"
#include "Fixtures.h"

#include <wincodec.h>
#include <algorithm>
#include <math.h>

#include "plan.h"
#include "review.h"
#include "text_util.h"

using namespace handoff;

namespace
{

struct Session
{
    SpecLoadResult Loaded;
    ScanResult Scan;
    ReviewModel Model;
    ReviewerDecisions Decisions;
    PackagePlan Plan;
    std::unique_ptr<IImageInspector> Images = CreateWicImageInspector();
    std::unique_ptr<IPdfPageInspector> Pdf = CreateWinRtPdfPageInspector();
    SYSTEMTIME Today = {};

    Session(const std::wstring& working, const std::string& specText, bool inspect = true)
    {
        Loaded = fixtures::ParseSpecText(specText);
        ScanInput input;
        input.WorkingRoot = working;
        NullProgress progress;
        Scan = ScanWorkingMaterial(input, progress);
        Model = Assemble(Loaded.Model, Scan, working);
        if (inspect)
            InspectCandidates(Model, Loaded.Model, Images.get(), Pdf.get(), progress);
        GetLocalTime(&Today);
        Today.wYear = 2026, Today.wMonth = 10, Today.wDay = 3;
        Reevaluate();
    }

    void Reevaluate(const std::map<std::wstring, std::wstring>& values = {})
    {
        Evaluate(Model, Loaded.Model, Decisions, Today, DefaultCatalog());
        Variables variables = BuildVariables(Loaded.Model, values, Today);
        Plan = PlanPackage(Model, Loaded.Model, variables, Today, 0, Decisions, DefaultCatalog());
    }

    std::vector<Finding> All() const
    {
        std::vector<Finding> all = Model.Findings;
        all.insert(all.end(), Plan.Findings.begin(), Plan.Findings.end());
        return all;
    }

    const Finding* Find(const wchar_t* code, const std::wstring& item = std::wstring()) const
    {
        for (const Finding& finding : Model.Findings)
            if (finding.Code == code && (item.empty() || EqualsNoCase(finding.Item, item)))
                return &finding;
        for (const Finding& finding : Plan.Findings)
            if (finding.Code == code && (item.empty() || EqualsNoCase(finding.Item, item)))
                return &finding;
        return nullptr;
    }

    const Candidate* CandidateFor(const std::wstring& rel) const
    {
        for (const Candidate& candidate : Model.Candidates)
            if (EqualsNoCase(candidate.Rel, rel))
                return &candidate;
        return nullptr;
    }

    const PlannedFile* Target(const std::wstring& sourceRel) const
    {
        for (const PlannedFile& file : Plan.Files)
            if (EqualsNoCase(file.SourceRel, sourceRel))
                return &file;
        return nullptr;
    }
};

std::string MakeSpec(const std::string& variables, const std::string& extraTop, const std::string& rules)
{
    return "{ \"handoffSpec\": 1, \"id\": \"t.review\", \"name\": \"Review\",\n" + variables + extraTop +
           "\"package\": { \"folderName\": \"Pkg\" },\n \"rules\": [" + rules + "] }";
}

void DumpFindings(const Session& session)
{
    for (const Finding& finding : session.All())
        fprintf(stderr, "    %ls %ls %ls\n", finding.Code.c_str(), finding.Item.c_str(), FindingMessage(finding, DefaultCatalog()).c_str());
}

} // namespace

HT_TEST(Scanner, SkipsLinksSpecificationsAndHonoursLimits)
{
    std::wstring working = fixtures::NewDir(L"scan");
    std::wstring outside = fixtures::NewDir(L"outside");
    fixtures::WriteBytes(PathJoin(outside, L"secret.txt"), "outside");
    fixtures::WriteBytes(PathJoin(working, L"a\\b\\c.txt"), "c");
    fixtures::WriteBytes(PathJoin(working, L".handoff\\spec.handoff.json"), "{}");
    fixtures::WriteBytes(PathJoin(working, L"Łódź\\plan.txt"), "unicode");
    bool junction = fixtures::MakeJunction(PathJoin(working, L"link"), outside) &&
                    fixtures::MakeJunction(PathJoin(working, L"a\\cycle"), working);
    HT_CHECK(junction);
    ScanInput input;
    input.WorkingRoot = working;
    NullProgress progress;
    ScanResult scan = ScanWorkingMaterial(input, progress);
    std::vector<std::wstring> rels;
    for (const ScannedFile& file : scan.Files)
        rels.push_back(file.Rel);
    std::sort(rels.begin(), rels.end());
    HT_CHECK(rels.size() == 2);
    HT_CHECK(std::find(rels.begin(), rels.end(), L"a/b/c.txt") != rels.end());
    HT_CHECK(std::find(rels.begin(), rels.end(), L"Łódź/plan.txt") != rels.end());
    int links = 0;
    for (const Finding& finding : scan.Findings)
        links += finding.Code == L"HO-SCAN-002" ? 1 : 0;
    HT_CHECK(links == 2);

    input.MaxEntries = 2;
    ScanResult limited = ScanWorkingMaterial(input, progress);
    HT_CHECK(limited.LimitExceeded && limited.Findings.back().Code == L"HO-SCAN-004");

    input.MaxEntries = 1000000;
    input.SelectionOnly = true;
    input.SelectedNames = {L"a"};
    ScanResult selected = ScanWorkingMaterial(input, progress);
    HT_CHECK(selected.Files.size() == 1 && selected.Files[0].Rel == L"a/b/c.txt");
}

HT_TEST(Scanner, HonoursCancellation)
{
    std::wstring working = fixtures::NewDir(L"cancel");
    for (int i = 0; i < 600; i++)
        fixtures::WriteBytes(PathJoin(working, L"f" + NumberText(i) + L".txt"), "x");
    struct Stop : IProgress
    {
        void Report(Phase, uint64_t, uint64_t, const std::wstring&) override {}
        bool StopRequested() override { return true; }
    } stop;
    ScanInput input;
    input.WorkingRoot = working;
    ScanResult scan = ScanWorkingMaterial(input, stop);
    HT_CHECK(scan.Cancelled && scan.Files.size() < 600);
}

HT_TEST(Selection, FirstMatchOwnsFilesUnlessShared)
{
    std::wstring working = fixtures::NewDir(L"own");
    fixtures::WriteBytes(PathJoin(working, L"docs\\a.txt"), "a");
    fixtures::WriteBytes(PathJoin(working, L"other.bin"), "b");
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"first\", \"select\": { \"include\": [\"docs/*.txt\"] } },"
        "{ \"id\": \"second\", \"select\": { \"include\": [\"**/*.txt\"] } },"
        "{ \"id\": \"third\", \"select\": { \"include\": [\"**/*.txt\"], \"shared\": true }, \"target\": { \"folder\": \"copy\" } }"));
    HT_CHECK(session.Loaded.Ok);
    int owners = 0;
    for (const Candidate& candidate : session.Model.Candidates)
        owners += candidate.Rel == L"docs/a.txt" ? 1 : 0;
    HT_CHECK(owners == 2); // first and the shared third rule
    HT_CHECK(session.Find(L"HO-SEL-002", L"docs/a.txt") != nullptr);
    HT_CHECK(session.Model.Excluded.size() == 1 && session.Model.Excluded[0].Code == L"HO-SEL-003");
    HT_CHECK(session.Plan.Files.size() == 2);
}

HT_TEST(Selection, KeepsOnlyTheLatestRevision)
{
    std::wstring working = fixtures::NewDir(L"rev");
    for (const wchar_t* name : {L"A-101_P01.txt", L"A-101_P07.txt", L"A-101_C01.txt", L"A-102_P02.txt", L"A-102_P10.txt",
                                L"Notes_vB.txt", L"Notes_vAA.txt", L"Notes_vZ.txt", L"Loose.txt"})
        fixtures::WriteBytes(PathJoin(working, name), "x");
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"drawings\", \"select\": { \"include\": [\"A-*.txt\"], \"latestRevision\": { \"regex\": \"_([PC]\\\\d{2})$\", \"prefixOrder\": [\"P\", \"C\"] } } },"
        "{ \"id\": \"notes\", \"select\": { \"include\": [\"Notes_*.txt\", \"Loose.txt\"], \"latestRevision\": { \"regex\": \"_v([A-Z]+)$\" } } }"));
    HT_CHECK(session.Loaded.Ok);
    auto superseded = [&](const wchar_t* rel) {
        const Candidate* candidate = session.CandidateFor(rel);
        return candidate != nullptr && candidate->Superseded;
    };
    HT_CHECK(superseded(L"A-101_P01.txt") && superseded(L"A-101_P07.txt") && !superseded(L"A-101_C01.txt"));
    HT_CHECK(superseded(L"A-102_P02.txt") && !superseded(L"A-102_P10.txt"));
    HT_CHECK(superseded(L"Notes_vB.txt") && superseded(L"Notes_vZ.txt") && !superseded(L"Notes_vAA.txt"));
    HT_CHECK(!superseded(L"Loose.txt") && session.Find(L"HO-SEL-006", L"Loose.txt") != nullptr);
    const Finding* sel = session.Find(L"HO-SEL-001", L"A-101_P07.txt");
    HT_CHECK(sel != nullptr && sel->Args[0] == L"C01");
}

HT_TEST(Selection, ChecksExpectedItemsAndCounts)
{
    std::wstring working = fixtures::NewDir(L"expect");
    fixtures::WriteBytes(PathJoin(working, L"Register\\register.csv"), "Sheet;Issue\r\nA-101;R04\r\nA-102;R04\r\nA-103;R03\r\n");
    fixtures::WriteBytes(PathJoin(working, L"Sheets\\A-101_plan.txt"), "1");
    fixtures::WriteBytes(PathJoin(working, L"Sheets\\A-104_extra.txt"), "4");
    fixtures::WriteBytes(PathJoin(working, L"Sheets\\A-104_copy.txt"), "4b");
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"sheets\", \"required\": true, \"count\": { \"max\": 2 }, \"select\": { \"include\": [\"Sheets/*.txt\"] },"
        "  \"expect\": { \"keyRegex\": \"^(A-\\\\d{3})_\", \"keys\": [\"A-900\"], \"registerCsv\": { \"path\": \"Register/register.csv\", \"column\": \"Sheet\", \"where\": { \"column\": \"Issue\", \"equals\": \"R04\" } } } },"
        "{ \"id\": \"missing\", \"required\": true, \"select\": { \"include\": [\"Nothing/*\"] } }"));
    HT_CHECK(session.Loaded.Ok);
    HT_CHECK(session.Find(L"HO-REQ-010") != nullptr);
    int missing = 0;
    for (const Finding& finding : session.Model.Findings)
        if (finding.Code == L"HO-REQ-010")
            missing++;
    HT_CHECK(missing == 2); // A-102 (register) and A-900 (inline); A-103 is filtered out by "where"
    HT_CHECK(session.Find(L"HO-REQ-012") != nullptr);
    HT_CHECK(session.Find(L"HO-REQ-011") != nullptr);
    HT_CHECK(session.Find(L"HO-REQ-003") != nullptr);
    HT_CHECK(session.Find(L"HO-REQ-001") != nullptr);
    // The register is supporting material, not "not included".
    for (const ExcludedFile& excluded : session.Model.Excluded)
        HT_CHECK(session.Model.Files[excluded.FileIndex].Rel != L"Register/register.csv");

    // Excluding all candidates of a required rule is an omission.
    session.Decisions.Excluded.insert(ReviewerDecisions::Key(L"Sheets/A-101_plan.txt", L"sheets"));
    session.Decisions.Excluded.insert(ReviewerDecisions::Key(L"Sheets/A-104_extra.txt", L"sheets"));
    session.Decisions.Excluded.insert(ReviewerDecisions::Key(L"Sheets/A-104_copy.txt", L"sheets"));
    session.Reevaluate();
    bool omission = false;
    for (const Finding& finding : session.Model.Findings)
        omission = omission || (finding.Code == L"HO-REQ-001" && finding.RuleId == L"sheets");
    HT_CHECK(omission && session.Find(L"HO-SEL-004") != nullptr);
}

HT_TEST(Pdf, ReadsPagesAndChecksConstraints)
{
    std::wstring working = fixtures::NewDir(L"pdf");
    using fixtures::A4W;
    using fixtures::A4H;
    fixtures::WriteBytes(PathJoin(working, L"three.pdf"), fixtures::MakePdf({{A4W, A4H}, {A4W, A4H}, {fixtures::A3H, fixtures::A3W}}));
    fixtures::WriteBytes(PathJoin(working, L"a4.pdf"), fixtures::MakePdf({{A4W, A4H}}));
    std::string truncated = fixtures::MakePdf({{A4W, A4H}});
    truncated.resize(truncated.size() - 7);
    fixtures::WriteBytes(PathJoin(working, L"truncated.pdf"), truncated);
    fixtures::WriteBytes(PathJoin(working, L"locked.pdf"), fixtures::MakePdf({{A4W, A4H}}, true));
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"pdfs\", \"select\": { \"include\": [\"*.pdf\"] }, \"formats\": [\"pdf\"],"
        "  \"pdf\": { \"pages\": { \"max\": 2 }, \"pageSizes\": [\"A4\"], \"orientation\": \"portrait\" } }"));
    HT_CHECK(session.Loaded.Ok);
    const Candidate* three = session.CandidateFor(L"three.pdf");
    HT_CHECK(three != nullptr && three->Pdf.Valid && three->Pdf.Version == L"1.7");
    HT_CHECK(three != nullptr && three->Pages.Ok && three->Pages.SizesMm.size() == 3);
    if (three != nullptr && three->Pages.SizesMm.size() == 3)
    {
        HT_CHECK(fabs(three->Pages.SizesMm[0].first - 210.0) < 0.5 && fabs(three->Pages.SizesMm[0].second - 297.0) < 0.5);
        HT_CHECK(fabs(three->Pages.SizesMm[2].first - 420.0) < 0.5 && fabs(three->Pages.SizesMm[2].second - 297.0) < 0.5);
    }
    HT_CHECK(session.Find(L"HO-PDF-020", L"three.pdf") != nullptr);
    const Finding* size = session.Find(L"HO-PDF-021", L"three.pdf");
    HT_CHECK(size != nullptr && size->Args[0] == L"3" && size->Args[1].find(L"A3") == 0);
    HT_CHECK(session.Find(L"HO-PDF-020", L"a4.pdf") == nullptr && session.Find(L"HO-PDF-021", L"a4.pdf") == nullptr);
    HT_CHECK(session.Find(L"HO-PDF-001", L"truncated.pdf") != nullptr);
    HT_CHECK(session.Find(L"HO-PDF-010", L"locked.pdf") != nullptr);
    HT_CHECK(session.Find(L"HO-PDF-031", L"locked.pdf") != nullptr);
}

HT_TEST(Images, ChecksDimensionsResolutionColourAndGps)
{
    std::wstring working = fixtures::NewDir(L"img");
    HT_CHECK(fixtures::MakeImage(PathJoin(working, L"small.png"), GUID_ContainerFormatPng, 1800, 900, 72, GUID_WICPixelFormat24bppBGR));
    HT_CHECK(fixtures::MakeImage(PathJoin(working, L"alpha.png"), GUID_ContainerFormatPng, 512, 512, 300, GUID_WICPixelFormat32bppBGRA));
    HT_CHECK(fixtures::MakeImage(PathJoin(working, L"print.tif"), GUID_ContainerFormatTiff, 2400, 1200, 300, GUID_WICPixelFormat32bppCMYK));
    bool gps = fixtures::MakeImage(PathJoin(working, L"located.jpg"), GUID_ContainerFormatJpeg, 2400, 1600, 300, GUID_WICPixelFormat24bppBGR, true);
    HT_CHECK(gps);
    fixtures::WriteBytes(PathJoin(working, L"art.psd"), fixtures::MakePsd(2600, 1300, 4, true, 300));
    fixtures::WriteBytes(PathJoin(working, L"nores.psd"), fixtures::MakePsd(2600, 1300, 3, false, 0));
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"images\", \"select\": { \"include\": [\"*.{png,tif,jpg,psd}\"] }, \"formats\": [\"png\", \"tiff\", \"jpeg\", \"psd\"],"
        "  \"image\": { \"longEdge\": { \"min\": 2000 }, \"dpi\": { \"min\": 300 }, \"colorModel\": [\"rgb\"], \"forbidGps\": true } }"));
    HT_CHECK(session.Loaded.Ok);
    const Candidate* smallImage = session.CandidateFor(L"small.png");
    HT_CHECK(smallImage != nullptr && smallImage->Image.Decoded && smallImage->Image.Width == 1800 && smallImage->Image.Height == 900);
    const Finding* edge = session.Find(L"HO-IMG-013", L"small.png");
    HT_CHECK(edge != nullptr && edge->Args[1] == L"1800");
    HT_CHECK(session.Find(L"HO-IMG-020", L"small.png") != nullptr);
    const Candidate* alpha = session.CandidateFor(L"alpha.png");
    HT_CHECK(alpha != nullptr && alpha->Image.Alpha == 1 && alpha->Image.BitsPerChannel == 8);
    HT_CHECK(session.Find(L"HO-IMG-021", L"print.tif") != nullptr);
    HT_CHECK(session.Find(L"HO-IMG-020", L"print.tif") == nullptr);
    if (gps)
        HT_CHECK(session.Find(L"HO-IMG-025", L"located.jpg") != nullptr);
    const Candidate* art = session.CandidateFor(L"art.psd");
    HT_CHECK(art != nullptr && art->Image.Width == 2600 && art->Image.ColorModel == L"cmyk" && art->Image.DpiKnown &&
             fabs(art->Image.DpiX - 300) < 0.5);
    HT_CHECK(session.Find(L"HO-IMG-032", L"nores.psd") != nullptr);
}

HT_TEST(Evidence, ResolvesApprovalAndLicenceEvidence)
{
    std::wstring working = fixtures::NewDir(L"evidence");
    fixtures::WriteBytes(PathJoin(working, L"Final\\report.pdf"), fixtures::MakePdf({{100, 100}}));
    fixtures::WriteBytes(PathJoin(working, L"Final\\report.approved"), "ok");
    fixtures::WriteBytes(PathJoin(working, L"Final\\annex.pdf"), fixtures::MakePdf({{100, 100}}));
    fixtures::WriteBytes(PathJoin(working, L"Approvals.csv"), "File,Status\r\nFinal/annex.pdf,approved\r\n");
    fixtures::WriteBytes(PathJoin(working, L"Assets\\expired.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\soon.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\scope.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\baddate.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\none.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\sidecar.otf"), fixtures::MakeOtf());
    fixtures::WriteBytes(PathJoin(working, L"Assets\\sidecar.licence.txt"), "licence");
    fixtures::WriteBytes(PathJoin(working, L"Assets\\licences.csv"),
                         "File,Licence,Expires,Scope\r\nexpired.otf,L1,2026-01-01,Unlimited\r\nsoon.otf,L2,2026-10-20,Unlimited\r\n"
                         "scope.otf,L3,,Internal\r\nbaddate.otf,L4,next year,Unlimited\r\n");
    Session session(working, MakeSpec("", "",
        "{ \"id\": \"docs\", \"select\": { \"include\": [\"Final/*.pdf\"] }, \"approval\": { \"mode\": \"evidenceAndConfirm\","
        "  \"evidence\": { \"sidecar\": [\"{stem}.approved\"], \"registerCsv\": { \"path\": \"Approvals.csv\", \"fileColumn\": \"File\", \"statusColumn\": \"Status\", \"approvedValues\": [\"Approved\"] } } } },"
        "{ \"id\": \"fonts\", \"select\": { \"include\": [\"Assets/*.otf\"] }, \"license\": { \"required\": true, \"sidecar\": [\"{stem}.licence.txt\"],"
        "  \"includeEvidence\": true, \"evidenceFolder\": \"Licences\","
        "  \"registerCsv\": { \"path\": \"Assets/licences.csv\", \"fileColumn\": \"File\", \"licenceColumn\": \"Licence\", \"expiresColumn\": \"Expires\", \"scopeColumn\": \"Scope\", \"allowedScopes\": [\"Unlimited\"] } } }"));
    HT_CHECK(session.Loaded.Ok);
    HT_CHECK(session.CandidateFor(L"Final/report.pdf")->ApprovalEvidence && session.CandidateFor(L"Final/annex.pdf")->ApprovalEvidence);
    HT_CHECK(session.Find(L"HO-APP-001") == nullptr && session.Find(L"HO-APP-002", L"Final/report.pdf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-002", L"Assets/expired.otf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-003", L"Assets/soon.otf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-004", L"Assets/scope.otf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-005", L"Assets/baddate.otf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-001", L"Assets/none.otf") != nullptr);
    HT_CHECK(session.Find(L"HO-LIC-001", L"Assets/sidecar.otf") == nullptr);
    const PlannedFile* evidence = session.Target(L"Assets/sidecar.licence.txt");
    HT_CHECK(evidence != nullptr && evidence->TargetRel == L"Licences/sidecar.licence.txt" && evidence->Evidence &&
             evidence->LicenceFor.size() == 1);
    // Reviewer approval clears HO-APP-002.
    session.Decisions.Approved.insert(ReviewerDecisions::Key(L"Final/report.pdf", L"docs"));
    session.Decisions.Approved.insert(ReviewerDecisions::Key(L"Final/annex.pdf", L"docs"));
    session.Reevaluate();
    HT_CHECK(session.Find(L"HO-APP-002") == nullptr);
}

HT_TEST(Planner, BuildsTargetsAndDetectsCollisionsAndLimits)
{
    std::wstring working = fixtures::NewDir(L"plan");
    fixtures::WriteBytes(PathJoin(working, L"In\\One\\café menu.txt"), "1");
    fixtures::WriteBytes(PathJoin(working, L"In\\Two\\cafe_menu.txt"), "2");
    fixtures::WriteBytes(PathJoin(working, L"In\\Two\\b.txt"), "3");
    fixtures::WriteBytes(PathJoin(working, L"Seq\\z.txt"), "z");
    fixtures::WriteBytes(PathJoin(working, L"Seq\\a.txt"), "a");
    fixtures::WriteBytes(PathJoin(working, L"Seq\\manifest.json"), "{}");
    Session session(working, MakeSpec("\"variables\": { \"client\": { \"default\": \"Zoë\" } },\n", "",
        "{ \"id\": \"flat\", \"select\": { \"include\": [\"In/**/*.txt\"] }, \"target\": { \"folder\": \"{client}/Out\" } },"
        "{ \"id\": \"seq\", \"select\": { \"include\": [\"Seq/*.txt\"] }, \"target\": { \"folder\": \"S\", \"name\": \"{seq:00}_{stem}{ext}\" } },"
        "{ \"id\": \"root\", \"select\": { \"include\": [\"Seq/*.json\"] } }"));
    HT_CHECK(session.Loaded.Ok);
    HT_CHECK(session.Plan.PackageName == L"Pkg");
    const PlannedFile* renamed = session.Target(L"In/One/café menu.txt");
    HT_CHECK(renamed != nullptr && renamed->TargetRel == L"Zoe/Out/cafe_menu.txt");
    HT_CHECK(session.Find(L"HO-NAME-010", L"In/One/café menu.txt") != nullptr);
    HT_CHECK(session.Find(L"HO-NAME-012", L"In/Two/cafe_menu.txt") != nullptr);
    HT_CHECK(session.Find(L"HO-NAME-012", L"Seq/manifest.json") != nullptr); // collides with the output
    HT_CHECK(session.Target(L"Seq/a.txt")->TargetRel == L"S/01_a.txt" && session.Target(L"Seq/z.txt")->TargetRel == L"S/02_z.txt");
    HT_CHECK(std::find(session.Plan.Folders.begin(), session.Plan.Folders.end(), L"Zoe") != session.Plan.Folders.end());
    Gate gate = ComputeGate(session.Model, session.Plan, session.Loaded.Model);
    HT_CHECK(!gate.CanBuild);

    Variables variables = BuildVariables(session.Loaded.Model, {}, session.Today);
    PackagePlan tight = PlanPackage(session.Model, session.Loaded.Model, variables, session.Today, 1024, session.Decisions,
                                    DefaultCatalog());
    bool space = false;
    for (const Finding& finding : tight.Findings)
        space = space || finding.Code == L"HO-PKG-003";
    HT_CHECK(space);
}

HT_TEST(Planner, EnforcesPathLengthAndPackageLimits)
{
    std::wstring working = fixtures::NewDir(L"limits");
    for (int i = 0; i < 3; i++)
        fixtures::WriteBytes(PathJoin(working, L"f" + NumberText(i) + L".txt"), std::string(2048, 'x'));
    std::string text = "{ \"handoffSpec\": 1, \"id\": \"t.limits\", \"name\": \"Limits\","
                       " \"package\": { \"folderName\": \"Pkg\", \"limits\": { \"maxFiles\": 2, \"maxTotalBytes\": \"5 KB\", \"maxRelativePathLength\": 20 } },"
                       " \"rules\": [ { \"id\": \"all\", \"select\": { \"include\": [\"*.txt\"] }, \"target\": { \"folder\": \"a-rather-long-folder\" } } ] }";
    Session session(working, text);
    HT_CHECK(session.Loaded.Ok);
    HT_CHECK(session.Find(L"HO-PKG-001") != nullptr && session.Find(L"HO-PKG-002") != nullptr);
    HT_CHECK(session.Find(L"HO-NAME-013", L"f0.txt") != nullptr);
}

HT_TEST(Variables, ValidatesRequiredPatternAndChoices)
{
    SpecLoadResult loaded = fixtures::ParseSpecText(MakeSpec(
        "\"variables\": { \"client\": { \"required\": true, \"pattern\": \"^[a-z]{2,4}$\" }, \"purpose\": { \"default\": \"A\", \"choices\": [\"A\", \"B\"] } },\n",
        "", "{ \"id\": \"x\", \"select\": { \"include\": [\"*\"] } }"));
    HT_CHECK(loaded.Ok);
    SYSTEMTIME today = {};
    GetLocalTime(&today);
    HT_CHECK(ValidateVariables(loaded.Model, BuildVariables(loaded.Model, {}, today)).size() == 1);
    // Patterns are case-insensitive like every specification pattern (C.4.6.2); length still applies.
    HT_CHECK(ValidateVariables(loaded.Model, BuildVariables(loaded.Model, {{L"client", L"toolong"}}, today)).size() == 1);
    HT_CHECK(ValidateVariables(loaded.Model, BuildVariables(loaded.Model, {{L"client", L"ACME"}, {L"purpose", L"C"}}, today)).size() == 1);
    HT_CHECK(ValidateVariables(loaded.Model, BuildVariables(loaded.Model, {{L"client", L"ACME"}, {L"date", L"2026-02-30"}}, today)).size() == 1);
    HT_CHECK(ValidateVariables(loaded.Model, BuildVariables(loaded.Model, {{L"client", L"ACME"}}, today)).empty());
}

HT_TEST(QuickStart, ReviewMatchesTheUserGuide)
{
    std::wstring working = fixtures::NewDir(L"quick");
    fixtures::BuildQuickStartTree(working);
    Session session(working, fixtures::ReadTemplate(L"client-delivery-example.handoff.json"));
    HT_CHECK(session.Loaded.Ok);
    HT_CHECK(session.CandidateFor(L"Approved/Brand-Guidelines_v3.pdf")->Superseded);
    HT_CHECK(session.Find(L"HO-APP-002", L"Approved/Brand-Guidelines_v4.pdf") != nullptr);
    session.Decisions.Approved.insert(ReviewerDecisions::Key(L"Approved/Brand-Guidelines_v4.pdf", L"approved-pdfs"));
    session.Decisions.Approved.insert(ReviewerDecisions::Key(L"Approved/Stationery_v2.pdf", L"approved-pdfs"));
    session.Reevaluate({{L"client", L"ACME"}, {L"project", L"Rebrand"}});
    Gate gate = ComputeGate(session.Model, session.Plan, session.Loaded.Model);
    if (!HT_CHECK(gate.CanBuild && gate.Errors == 0))
        DumpFindings(session);
    HT_CHECK(session.Plan.PackageName == L"ACME-Rebrand_Delivery_2026-10-03_R01");
    HT_CHECK(session.Target(L"Approved/Brand-Guidelines_v4.pdf")->TargetRel == L"01_Approved_PDFs/ACME-Rebrand_Brand-Guidelines_v4.pdf");
    HT_CHECK(session.Target(L"Artwork/Final/Logo/ACME-logo-primary.ai")->TargetRel == L"02_Source_Artwork/Logo/ACME-logo-primary.ai");
    HT_CHECK(session.Target(L"Assets/Stock/city-skyline.licence.pdf")->TargetRel == L"03_Licensed_Assets/Licences/city-skyline.licence.pdf");
    HT_CHECK(session.Plan.Files.size() == 8);
    bool forbidden = false, wip = false;
    for (const ExcludedFile& excluded : session.Model.Excluded)
    {
        const std::wstring& rel = session.Model.Files[excluded.FileIndex].Rel;
        forbidden = forbidden || (rel == L"Thumbs.db" && excluded.Code == L"HO-CONT-001");
        wip = wip || (rel == L"Artwork/WIP/Hero-explorations.psd" && excluded.Code == L"HO-SEL-003");
    }
    HT_CHECK(forbidden && wip);
}
