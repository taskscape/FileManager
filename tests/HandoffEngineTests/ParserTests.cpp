// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// JSON reader/writer, specification validation, patterns, naming, CSV, and
// format signatures (handoff-spec.md C.12.1 groups JSON, Spec, Glob/regex,
// Templates/naming, Formats).

#include "TestFramework.h"
#include "Fixtures.h"

#include "csv.h"
#include "formats.h"
#include "json.h"
#include "name_template.h"
#include "patterns.h"
#include "spec.h"
#include "text_util.h"

using namespace handoff;

namespace
{

JsonError ParseError(const std::string& text, const JsonLimits& limits = SpecJsonLimits())
{
    JsonValue root;
    JsonError error;
    ParseJson((const unsigned char*)text.data(), text.size(), limits, root, error);
    return error;
}

bool HasCode(const std::vector<Finding>& findings, const wchar_t* code)
{
    for (const Finding& finding : findings)
        if (finding.Code == code)
            return true;
    return false;
}

const Finding* FindCodeIn(const std::vector<Finding>& findings, const wchar_t* code)
{
    for (const Finding& finding : findings)
        if (finding.Code == code)
            return &finding;
    return nullptr;
}

// A valid minimal specification with one placeholder that tests replace.
std::string MinimalSpec(const std::string& extraTop = "", const std::string& ruleExtra = "")
{
    return "{\n  \"handoffSpec\": 1,\n  \"id\": \"t.minimal\",\n  \"name\": \"Minimal\",\n" + extraTop +
           "  \"package\": { \"folderName\": \"Package_{date}\" },\n"
           "  \"rules\": [ { \"id\": \"all\", \"select\": { \"include\": [\"**/*\"] }" + ruleExtra + " } ]\n}\n";
}

// The minimal specification with its rule's "select" object replaced.
std::string ReplaceSelect(const std::string& select)
{
    std::string text = MinimalSpec();
    const std::string original = "{ \"include\": [\"**/*\"] }";
    return text.replace(text.find(original), original.size(), select);
}

} // namespace

HT_TEST(Json, ParsesValidDocumentWithPositions)
{
    std::string text = "\xEF\xBB\xBF{\n  \"a\": [1, -2.5e3, true, false, null],\n  \"b\": \"x\\u00e9\\ud83d\\ude00\"\n}";
    JsonValue root;
    JsonError error;
    HT_CHECK(ParseJson((const unsigned char*)text.data(), text.size(), SpecJsonLimits(), root, error));
    const JsonValue* a = root.Find(L"a");
    HT_CHECK(a != nullptr && a->Items.size() == 5 && a->Line == 2 && a->Column == 8);
    int64_t one = 0;
    HT_CHECK(a != nullptr && a->Items[0].AsInteger(one) && one == 1);
    double big = 0;
    HT_CHECK(a != nullptr && a->Items[1].AsDouble(big) && big == -2500.0 && !a->Items[1].AsInteger(one));
    const JsonValue* b = root.Find(L"b");
    HT_CHECK(b != nullptr && b->String == std::wstring(L"x\u00e9") + L"\U0001F600");
}

HT_TEST(Json, ReportsSyntaxErrorsWithLineAndColumn)
{
    JsonError e = ParseError("{\n  \"a\": 1,\n}");
    HT_CHECK(e.Code == L"HO-SPEC-001" && e.Line == 3 && e.Column == 1); // reported at the '}' that follows the comma
    e = ParseError("{\"a\": \"unterminated}");
    HT_CHECK(e.Code == L"HO-SPEC-001");
    e = ParseError("{\"a\": \"bad \\q escape\"}");
    HT_CHECK(e.Code == L"HO-SPEC-001");
    e = ParseError("{\"a\": 01}");
    HT_CHECK(e.Code == L"HO-SPEC-001");
    e = ParseError("{\"a\": NaN}");
    HT_CHECK(e.Code == L"HO-SPEC-001");
    e = ParseError("{} {}");
    HT_CHECK(e.Code == L"HO-SPEC-001");
    e = ParseError("// comment\n{}");
    HT_CHECK(e.Code == L"HO-SPEC-001" && e.Line == 1 && e.Column == 1);
}

HT_TEST(Json, RejectsInvalidUtf8AndSurrogates)
{
    HT_CHECK(ParseError("{\"a\": \"\xC3\x28\"}").Code == L"HO-SPEC-001");
    HT_CHECK(ParseError("{\"a\": \"\xE0\x80\xAF\"}").Code == L"HO-SPEC-001"); // overlong
    HT_CHECK(ParseError("{\"a\": \"\xED\xA0\x80\"}").Code == L"HO-SPEC-001"); // encoded surrogate
    HT_CHECK(ParseError("{\"a\": \"\\ud83d\"}").Code == L"HO-SPEC-001");
    HT_CHECK(ParseError("{\"a\": \"\\ude00\"}").Code == L"HO-SPEC-001");
    HT_CHECK(ParseError("{\"a\": \"\\u0000\"}").Code == L"HO-SPEC-001");
    HT_CHECK(ParseError("{\"a\": \"tab\there\"}").Code == L"HO-SPEC-001");
}

HT_TEST(Json, ReportsDuplicateMembers)
{
    JsonError e = ParseError("{\"a\": 1, \"a\": 2}");
    HT_CHECK(e.Code == L"HO-SPEC-007" && e.Pointer == L"/a");
}

HT_TEST(Json, EnforcesLimitsAtTheBoundary)
{
    JsonLimits limits = SpecJsonLimits();
    std::string deep = std::string((size_t)limits.MaxDepth, '[') + std::string((size_t)limits.MaxDepth, ']');
    HT_CHECK(ParseError(deep).Code.empty());
    std::string tooDeep = std::string((size_t)limits.MaxDepth + 1, '[') + std::string((size_t)limits.MaxDepth + 1, ']');
    HT_CHECK(ParseError(tooDeep).Code == L"HO-SPEC-006");
    // Very deep input must fail fast instead of exhausting the stack.
    std::string hostile(200000, '[');
    HT_CHECK(ParseError(hostile).Code == L"HO-SPEC-006");
    std::string atLimit = "\"" + std::string(limits.MaxString, 'x') + "\"";
    HT_CHECK(ParseError(atLimit).Code.empty());
    std::string overLimit = "\"" + std::string(limits.MaxString + 1, 'x') + "\"";
    HT_CHECK(ParseError(overLimit).Code == L"HO-SPEC-006");
    std::string bigFile(limits.MaxBytes + 1, ' ');
    HT_CHECK(ParseError(bigFile).Code == L"HO-SPEC-006");
    std::string items = "[";
    for (size_t i = 0; i <= limits.MaxItems; i++)
        items += i == 0 ? "1" : ",1";
    items += "]";
    HT_CHECK(ParseError(items).Code == L"HO-SPEC-006");
    std::string number = std::string(limits.MaxNumber + 1, '1');
    HT_CHECK(ParseError(number).Code == L"HO-SPEC-006");
}

HT_TEST(Json, WriterIsDeterministicAndRoundTrips)
{
    JsonWriter writer;
    writer.BeginObject();
    writer.Member(L"text", L"quote \" backslash \\ tab \t \u0142");
    writer.MemberInt(L"n", -42);
    writer.Key(L"empty");
    writer.BeginArray();
    writer.EndArray();
    writer.EndObject();
    std::string text = writer.Text();
    HT_CHECK(text == "{\n  \"text\": \"quote \\\" backslash \\\\ tab \\t \xC5\x82\",\n  \"n\": -42,\n  \"empty\": []\n}\n");
    JsonValue root;
    JsonError error;
    HT_CHECK(ParseJson((const unsigned char*)text.data(), text.size(), SpecJsonLimits(), root, error));
    HT_CHECK(root.Find(L"text") != nullptr && root.Find(L"text")->String == L"quote \" backslash \\ tab \t \u0142");
}

HT_TEST(Spec, EmbeddedTemplatesValidateCleanly)
{
    for (const wchar_t* name : {L"client-delivery-example.handoff.json", L"design-studio.handoff.json",
                                L"architecture-issue.handoff.json", L"consultancy-report.handoff.json"})
    {
        std::string text = fixtures::ReadTemplate(name);
        HT_CHECK(!text.empty());
        SpecLoadResult result = fixtures::ParseSpecText(text);
        HT_CHECK(result.Ok);
        HT_CHECK(result.Findings.empty());
        for (const Finding& finding : result.Findings)
            fprintf(stderr, "  %ls %ls %ls\n", name, finding.Code.c_str(), finding.Pointer.c_str());
    }
}

HT_TEST(Spec, ExampleModelHasDefaultsApplied)
{
    SpecLoadResult result = fixtures::ParseSpecText(fixtures::ReadTemplate(L"client-delivery-example.handoff.json"));
    HT_CHECK(result.Ok && result.Model.Rules.size() == 4);
    const RuleSpec& pdfs = result.Model.Rules[0];
    HT_CHECK(pdfs.Required && pdfs.Count.HasMin && pdfs.Count.Min == 1 && pdfs.Select.Latest.Present);
    HT_CHECK(pdfs.Approval.NeedsEvidence() && pdfs.Approval.NeedsConfirm());
    HT_CHECK(!result.Model.Rules[2].Required && result.Model.Rules[2].Count.Min == 0);
    HT_CHECK(result.Model.Package.ManifestJson == L"manifest.json" && result.Model.Package.MaxRelativePathLength == 180);
    HT_CHECK(result.Model.Naming.Portable && result.Model.Naming.MaxNameLength == 100);
    HT_CHECK(result.Model.Content.Forbid.size() == 6);
}

HT_TEST(Spec, ReportsEveryValidationCode)
{
    struct Case
    {
        const wchar_t* Code;
        std::string Text;
    };
    std::vector<Case> cases = {
        {L"HO-SPEC-001", "{ \"handoffSpec\": 1, }"},
        {L"HO-SPEC-002", MinimalSpec("  \"descripton\": \"typo\",\n")},
        {L"HO-SPEC-003", "{ \"handoffSpec\": 1, \"id\": \"x\", \"name\": \"n\", \"rules\": [] }"},
        {L"HO-SPEC-004", MinimalSpec("  \"revision\": 5,\n")},
        {L"HO-SPEC-005", "{ \"handoffSpec\": 2 }"},
        {L"HO-SPEC-006", MinimalSpec("  \"description\": \"" + std::string(5000, 'x') + "\",\n")},
        {L"HO-SPEC-007", MinimalSpec("", " }, { \"id\": \"all\", \"select\": { \"include\": [\"*\"] }")},
        {L"HO-SPEC-010", ReplaceSelect("{ \"include\": [\"*\"], \"nameRegex\": \"(unclosed\" }")},
        {L"HO-SPEC-011", ReplaceSelect("{ \"include\": [\"a/{x,y\"] }")},
        {L"HO-SPEC-012", MinimalSpec("", ", \"target\": { \"name\": \"{unknownVariable}{ext}\" }")},
        {L"HO-SPEC-013", MinimalSpec("", ", \"target\": { \"folder\": \"../outside\" }")},
        {L"HO-SPEC-014", MinimalSpec("", ", \"formats\": [\"nosuchformat\"]")},
        {L"HO-SPEC-015", MinimalSpec("").replace(MinimalSpec("").find("\"folderName\""), 0,
                                                  "\"outputs\": { \"manifestCsv\": \"MANIFEST.JSON\" }, ")},
        {L"HO-SPEC-016", MinimalSpec("", ", \"formats\": [\"png\"], \"pdf\": { \"pages\": { \"min\": 1 } }")},
        {L"HO-SPEC-017", MinimalSpec("  \"severity\": { \"HO-NAME-012\": \"warning\" },\n")},
        {L"HO-SPEC-018", MinimalSpec("", ", \"formats\": [\"pdf\"], \"pdf\": { \"pageSizes\": [\"A9\"] }")},
    };
    for (const Case& test : cases)
    {
        SpecLoadResult result = fixtures::ParseSpecText(test.Text);
        if (!HT_CHECK(HasCode(result.Findings, test.Code)))
        {
            fprintf(stderr, "  expected %ls; got:", test.Code);
            for (const Finding& finding : result.Findings)
                fprintf(stderr, " %ls(%ls)", finding.Code.c_str(), finding.Pointer.c_str());
            fprintf(stderr, "\n");
        }
    }
}

HT_TEST(Spec, UnknownMemberSuggestsTheClosestName)
{
    SpecLoadResult result = fixtures::ParseSpecText(MinimalSpec("", ", \"licence\": { \"required\": true }"));
    const Finding* finding = FindCodeIn(result.Findings, L"HO-SPEC-002");
    HT_CHECK(finding != nullptr && finding->Pointer == L"/rules/0/licence" && finding->Line > 0);
    HT_CHECK(finding != nullptr && FindingMessage(*finding, DefaultCatalog()).find(L"did you mean \"license\"") != std::wstring::npos);
    HT_CHECK(result.Ok); // an unknown member is a warning
}

HT_TEST(Spec, CommentsAreIgnoredEverywhere)
{
    SpecLoadResult result = fixtures::ParseSpecText(MinimalSpec("  \"$comment\": \"top\",\n", ", \"$comment\": [1, 2]"));
    HT_CHECK(result.Ok && result.Findings.empty());
}

HT_TEST(Glob, MatchesTheDocumentedSyntax)
{
    struct Case
    {
        const wchar_t* Pattern;
        const wchar_t* Path;
        bool Expected;
    };
    Case cases[] = {
        {L"*.pdf", L"a.pdf", true},          {L"*.pdf", L"x/a.pdf", false},       {L"**/*.pdf", L"a.pdf", true},
        {L"**/*.pdf", L"x/y/a.PDF", true},   {L"a/**", L"a/b/c", true},           {L"a/**/z", L"a/z", true},
        {L"a/**/z", L"a/b/c/z", true},       {L"a/?.txt", L"a/b.txt", true},      {L"a/?.txt", L"a/bc.txt", false},
        {L"[abc].md", L"b.md", true},        {L"[!abc].md", L"b.md", false},      {L"[a-c]x", L"Bx", true},
        {L"*.{ai,psd}", L"x.psd", true},     {L"*.{ai,psd}", L"x.svg", false},    {L"Approved/**/*.pdf", L"approved/a/b.pdf", true},
        {L"**/~$*", L"x/~$doc.docx", true},  {L"**/\u0141*.txt", L"dir/\u0142odz.txt", true},
    };
    for (const Case& test : cases)
    {
        Glob glob;
        std::wstring error;
        HT_CHECK(glob.Compile(test.Pattern, error));
        if (!HT_CHECK(glob.Matches(test.Path) == test.Expected))
            fprintf(stderr, "  %ls vs %ls\n", test.Pattern, test.Path);
    }
    Glob glob;
    std::wstring error;
    HT_CHECK(glob.Compile(L"Artwork/Final/**/*.{ai,psd}", error) && glob.StaticPrefix() == L"Artwork/Final");
    HT_CHECK(RelativeBelowPrefix(L"Artwork/Final/Logo/Sub/a.ai", L"Artwork/Final") == L"Logo/Sub");
    HT_CHECK(!glob.Compile(L"../x", error) && !glob.Compile(L"C:/x", error) && !glob.Compile(L"a/{b,{c}}", error));
    HT_CHECK(!glob.Compile(L"{a,b,c,d,e}{1,2,3,4,5}", error)); // 25 alternatives
}

HT_TEST(Regex, BoundsHostilePatternsAndSubjects)
{
    SafeRegex regex;
    std::wstring error;
    HT_CHECK(regex.Compile(L"(a+)+$", error));
    std::wstring subject(1000, L'a');
    subject += L'b';
    ULONGLONG start = GetTickCount64();
    RegexOutcome outcome = regex.FullMatch(subject);
    HT_CHECK(outcome == RegexOutcome::TooComplex || outcome == RegexOutcome::NoMatch);
    HT_CHECK(GetTickCount64() - start < 5000);
    HT_CHECK(regex.FullMatch(std::wstring(2000, L'a')) == RegexOutcome::TooLong);
    HT_CHECK(!regex.Compile(std::wstring(600, L'a'), error));
    HT_CHECK(regex.Compile(L"_R(\\d{2})$", error) && regex.GroupCount() == 1);
    std::wstring group;
    HT_CHECK(regex.Search(L"Plan_r07", &group) == RegexOutcome::Match && group == L"07"); // case-insensitive
}

HT_TEST(Naming, TransliteratesAndSanitizes)
{
    NamingPolicy portable;
    struct Case
    {
        const wchar_t* Raw;
        const wchar_t* Expected;
    };
    Case cases[] = {
        {L"\u0141\u00f3d\u017a \u017c\u00f3\u0142w.pdf", L"Lodz_zolw.pdf"},
        {L"Stra\u00dfe M\u00fcller.txt", L"Strasse_Muller.txt"},
        {L"Caf\u00e9 cr\u00e8me.doc", L"Cafe_creme.doc"},
        {L"P\u0159\u00edli\u0161 \u017elu\u0165ou\u010dk\u00fd.csv", L"Prilis_zlutoucky.csv"},
        {L"\u00d8resund \u00e6ble.png", L"Oresund_aeble.png"},
        {L"  .hidden..name?.txt", L"hidden..name_.txt"},
    };
    for (const Case& test : cases)
    {
        SanitizeResult result = SanitizeName(test.Raw, portable, true);
        HT_CHECK_TEXT(result.Value, test.Expected);
        HT_CHECK(result.Changed);
    }
    NamingPolicy unicode;
    unicode.Portable = false;
    SanitizeResult cjk = SanitizeName(L"\u8a2d\u8a08\u56f3 A.pdf", unicode, true);
    HT_CHECK_TEXT(cjk.Value, L"\u8a2d\u8a08\u56f3_A.pdf");
    HT_CHECK(SanitizeName(L"CON.txt", portable, true).Reserved && SanitizeName(L"lpt9", portable, false).Reserved);
    HT_CHECK(SanitizeName(L"???", portable, true).Invalid);
    NamingPolicy shortNames;
    shortNames.MaxNameLength = 12;
    SanitizeResult truncated = SanitizeName(L"averylongname.pdf", shortNames, true);
    HT_CHECK(truncated.Truncated && truncated.Value == L"averylon.pdf");
    NamingPolicy lower;
    lower.CaseMode = L"lower";
    HT_CHECK_TEXT(SanitizeName(L"Report.PDF", lower, true).Value, L"report.pdf");
}

HT_TEST(Naming, ExpandsTokensIncludingPaddedSequence)
{
    NameTemplate tmpl;
    std::wstring error;
    HT_CHECK(tmpl.Parse(L"{client}_{key}_{rev}_{seq:000}_{date:yyyyMMdd}{ext}", error));
    std::map<std::wstring, std::wstring> variables = {{L"client", L"ACME"}};
    TemplateContext context;
    context.Variables = &variables;
    context.Date.wYear = 2026, context.Date.wMonth = 10, context.Date.wDay = 3;
    context.HasFile = true;
    context.Key = L"A101";
    context.Rev = L"04";
    context.Seq = 7;
    context.Ext = L".pdf";
    HT_CHECK_TEXT(tmpl.Expand(context), L"ACME_A101_04_007_20261003.pdf");
    HT_CHECK(!tmpl.Parse(L"{unbalanced", error) && !tmpl.Parse(L"x}", error));
}

HT_TEST(Csv, ParsesQuotedFieldsAndSemicolons)
{
    std::string text = "\xEF\xBB\xBF" "File,Note\r\n\"a,b.pdf\",\"says \"\"hi\"\"\nline\"\r\n\r\nc.pdf,plain\r\n";
    CsvTable table;
    std::wstring error;
    HT_CHECK(ParseCsv((const unsigned char*)text.data(), text.size(), table, error));
    HT_CHECK(table.Rows.size() == 2 && table.Cell(0, 0) == L"a,b.pdf" && table.Cell(0, 1) == L"says \"hi\"\nline");
    HT_CHECK(table.Column(L"note") == 1);
    std::string semicolons = "Sheet;Issue\nA-101;R04\n";
    HT_CHECK(ParseCsv((const unsigned char*)semicolons.data(), semicolons.size(), table, error) && table.Cell(0, 1) == L"R04");
    std::string broken = "a,b\n\"unterminated";
    HT_CHECK(!ParseCsv((const unsigned char*)broken.data(), broken.size(), table, error));
    HT_CHECK(CsvField(L"=SUM(A1)") == L"'=SUM(A1)" && CsvField(L"-x") == L"'-x" && CsvField(L"a,b") == L"\"a,b\"");
}

HT_TEST(Formats, DetectsSignaturesAndMismatches)
{
    std::vector<FormatDef> none;
    auto detect = [&](const wchar_t* name, const std::string& head) {
        return DetectFormat(name, (const unsigned char*)head.data(), head.size(), none);
    };
    HT_CHECK(detect(L"a.pdf", fixtures::MakePdf({{100, 100}})).SignatureOk);
    HT_CHECK(!detect(L"a.pdf", "not a pdf").SignatureOk);
    HT_CHECK(detect(L"a.png", "\x89PNG\r\n\x1a\n....").SignatureOk);
    HT_CHECK(!detect(L"a.png", "\xFF\xD8\xFF\xE0").SignatureOk);
    HT_CHECK(detect(L"a.JPG", "\xFF\xD8\xFF\xE0").Id == L"jpeg");
    HT_CHECK(detect(L"a.psd", fixtures::MakePsd(10, 10, 3, false, 0)).SignatureOk);
    HT_CHECK(detect(L"a.svg", "\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><!DOCTYPE svg><svg/>").SignatureOk);
    HT_CHECK(!detect(L"a.svg", "<html></html>").SignatureOk);
    HT_CHECK(detect(L"a.otf", fixtures::MakeOtf()).SignatureOk);
    HT_CHECK(detect(L"a.dxf", "  0\r\nSECTION\r\n").SignatureOk);
    HT_CHECK(detect(L"a.heic", std::string("\0\0\0\x18" "ftypheic", 12)).SignatureOk);
    HT_CHECK(!detect(L"a.mp4", std::string("\0\0\0\x18" "ftypheic", 12)).SignatureOk);
    HT_CHECK(!detect(L"a.txt", std::string("a\0b", 3)).SignatureOk);
    FormatDetection unknown = detect(L"a.xyz", "data");
    HT_CHECK(!unknown.Known && unknown.Id == L"unknown");
    HT_CHECK(detect(L"empty.pdf", "").SignatureOk); // empty files are reported once, as HO-CONT-004

    SpecLoadResult custom = fixtures::ParseSpecText(
        MinimalSpec("  \"formats\": { \"rvt\": { \"extensions\": [\".rvt\"], \"magic\": [ { \"offset\": 0, \"hex\": \"D0CF11E0\" } ] } },\n"));
    HT_CHECK(custom.Ok);
    HT_CHECK(DetectFormat(L"m.rvt", (const unsigned char*)"\xD0\xCF\x11\xE0zz", 6, custom.Model.CustomFormats).SignatureOk);
    HT_CHECK(!DetectFormat(L"m.rvt", (const unsigned char*)"PK\x03\x04zz", 6, custom.Model.CustomFormats).SignatureOk);
}
