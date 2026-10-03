// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "outputs.h"
#include "csv.h"
#include "json.h"
#include "text_util.h"

#include <algorithm>
#include <map>

namespace handoff
{

uint64_t ManifestData::TotalBytes() const
{
    uint64_t total = 0;
    for (const ManifestFile& file : Files)
        total += file.Bytes;
    return total;
}

static void WriteFinding(JsonWriter& writer, const Finding& finding, const ITextCatalog& catalog, bool includeOverride)
{
    writer.BeginObject();
    writer.Member(L"severity", SeverityName(finding.Sev));
    writer.Member(L"code", finding.Code);
    writer.Member(L"path", finding.Item);
    writer.Member(L"message", FindingMessage(finding, catalog));
    if (includeOverride && finding.Overridden)
    {
        writer.MemberBool(L"overridden", true);
        writer.Member(L"reason", finding.OverrideReason);
    }
    writer.EndObject();
}

std::string ManifestJsonText(const ManifestData& data, const ITextCatalog& catalog)
{
    // Member order follows C.7.1 so identical inputs produce identical bytes.
    JsonWriter writer;
    writer.BeginObject();
    writer.MemberInt(L"handoffManifest", 1);
    writer.Key(L"generator");
    writer.BeginObject();
    writer.Member(L"name", L"Open Salamander Delivery Handoff");
    writer.Member(L"version", data.GeneratorVersion);
    writer.Member(L"host", data.HostVersion);
    writer.EndObject();
    writer.Key(L"package");
    writer.BeginObject();
    writer.Member(L"name", data.PackageName);
    writer.Member(L"createdUtc", data.CreatedUtc);
    writer.Key(L"variables");
    writer.BeginObject();
    for (const auto& variable : data.Variables)
        writer.Member(variable.first, variable.second);
    writer.EndObject();
    writer.EndObject();
    writer.Key(L"spec");
    writer.BeginObject();
    writer.Member(L"id", data.SpecId);
    writer.Member(L"name", data.SpecName);
    writer.Member(L"revision", data.SpecRevision);
    writer.Member(L"sha256", data.SpecSha256);
    writer.EndObject();
    writer.Key(L"summary");
    writer.BeginObject();
    writer.Member(L"status", data.Status);
    writer.MemberUInt(L"files", data.Files.size());
    writer.MemberUInt(L"bytes", data.TotalBytes());
    writer.MemberInt(L"errors", data.Errors);
    writer.MemberInt(L"warnings", data.Warnings);
    writer.MemberInt(L"overrides", data.Overrides);
    writer.EndObject();
    writer.Key(L"outputs");
    writer.BeginArray();
    for (const ManifestOutput& output : data.Outputs)
    {
        writer.BeginObject();
        writer.Member(L"path", output.Path);
        writer.MemberUInt(L"bytes", output.Bytes);
        writer.Member(L"sha256", output.Sha256);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key(L"files");
    writer.BeginArray();
    for (const ManifestFile& file : data.Files)
    {
        writer.BeginObject();
        writer.Member(L"path", file.Path);
        writer.MemberUInt(L"bytes", file.Bytes);
        writer.Member(L"sha256", file.Sha256);
        writer.Member(L"modifiedUtc", file.ModifiedUtc);
        if (!file.Rule.empty() && file.Role != L"licence-evidence")
            writer.Member(L"rule", file.Rule);
        writer.Member(L"role", file.Role);
        writer.Member(L"format", file.Format);
        if (!file.Key.empty())
            writer.Member(L"key", file.Key);
        if (!file.Revision.empty())
            writer.Member(L"revision", file.Revision);
        if (file.HasPdf)
        {
            writer.Key(L"pdf");
            writer.BeginObject();
            writer.Member(L"version", file.PdfVersion);
            if (file.Pages >= 0)
            {
                writer.MemberInt(L"pages", file.Pages);
                writer.Key(L"pageSizes");
                writer.BeginArray();
                for (const std::wstring& size : file.PageSizes)
                    writer.String(size);
                writer.EndArray();
            }
            writer.EndObject();
        }
        if (file.HasImage)
        {
            writer.Key(L"image");
            writer.BeginObject();
            writer.MemberUInt(L"width", file.Width);
            writer.MemberUInt(L"height", file.Height);
            if (file.DpiKnown)
            {
                writer.Key(L"dpi");
                writer.BeginArray();
                writer.Integer((int64_t)(file.DpiX + 0.5));
                writer.Integer((int64_t)(file.DpiY + 0.5));
                writer.EndArray();
            }
            if (!file.ColorModel.empty())
                writer.Member(L"colorModel", file.ColorModel);
            writer.EndObject();
        }
        if (!file.Approval.empty())
            writer.Member(L"approval", file.Approval);
        if (file.HasLicence)
        {
            writer.Key(L"license");
            writer.BeginObject();
            if (!file.Licence.empty())
                writer.Member(L"licence", file.Licence);
            if (!file.Licensor.empty())
                writer.Member(L"licensor", file.Licensor);
            if (!file.LicenceExpires.empty())
                writer.Member(L"expires", file.LicenceExpires);
            if (!file.LicenceEvidence.empty())
            {
                writer.Key(L"evidence");
                writer.BeginArray();
                for (const std::wstring& evidence : file.LicenceEvidence)
                    writer.String(evidence);
                writer.EndArray();
            }
            writer.EndObject();
        }
        if (!file.LicenceFor.empty())
        {
            writer.Key(L"licenceFor");
            writer.BeginArray();
            for (const std::wstring& asset : file.LicenceFor)
                writer.String(asset);
            writer.EndArray();
        }
        if (!file.Source.empty())
            writer.Member(L"source", file.Source);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key(L"findings");
    writer.BeginArray();
    for (const Finding& finding : data.Findings)
        WriteFinding(writer, finding, catalog, false);
    writer.EndArray();
    writer.EndObject();
    return writer.Text();
}

std::string ManifestCsvText(const ManifestData& data)
{
    // UTF-8 with BOM so spreadsheets detect the encoding; CRLF; English headers for tooling (C.7.2).
    std::wstring text;
    const wchar_t* header[] = {L"Path", L"Bytes", L"SHA-256", L"Modified (UTC)", L"Rule", L"Role", L"Format", L"Key",
                               L"Revision", L"Pages", L"Page sizes", L"Width (px)", L"Height (px)", L"DPI",
                               L"Colour model", L"Approval", L"Licence", L"Licensor", L"Licence expires"};
    for (size_t i = 0; i < _countof(header); i++)
        text += (i > 0 ? L"," : L"") + CsvField(header[i]);
    text += L"\r\n";
    for (const ManifestFile& file : data.Files)
    {
        std::vector<std::wstring> cells = {
            file.Path, NumberText((int64_t)file.Bytes), file.Sha256, file.ModifiedUtc, file.Rule, file.Role, file.Format,
            file.Key, file.Revision, file.Pages >= 0 ? NumberText(file.Pages) : L"", Join(file.PageSizes, L"; "),
            file.HasImage ? NumberText(file.Width) : L"", file.HasImage ? NumberText(file.Height) : L"",
            file.DpiKnown ? DecimalText(file.DpiX, 0) + (file.DpiX != file.DpiY ? L"x" + DecimalText(file.DpiY, 0) : L"") : L"",
            file.ColorModel, file.Approval, file.Licence, file.Licensor, file.LicenceExpires};
        for (size_t i = 0; i < cells.size(); i++)
            text += (i > 0 ? L"," : L"") + CsvField(cells[i]);
        text += L"\r\n";
    }
    return "\xEF\xBB\xBF" + WideToUtf8(text);
}

static std::wstring PadRight(const std::wstring& text, size_t width)
{
    return text.size() >= width ? text : text + std::wstring(width - text.size(), L' ');
}

static std::wstring PadLeft(const std::wstring& text, size_t width)
{
    return text.size() >= width ? text : std::wstring(width - text.size(), L' ') + text;
}

static std::wstring Details(const ManifestFile& file, const ITextCatalog& catalog)
{
    std::wstring details;
    if (file.Role == L"licence-evidence")
        details = LabelText(catalog, Label::CT_EVIDENCE);
    else if (file.HasPdf && file.Pages >= 0)
    {
        std::vector<std::wstring> unique;
        for (const std::wstring& size : file.PageSizesLocalized)
            if (std::find(unique.begin(), unique.end(), size) == unique.end())
                unique.push_back(size);
        details = unique.empty() ? LabelText(catalog, Label::CT_PDF, {NumberText(file.Pages)})
                                 : LabelText(catalog, Label::CT_PDF_SIZE, {NumberText(file.Pages), Join(unique, L", ")});
    }
    else if (file.HasImage)
        details = LabelText(catalog, Label::CT_PIXELS, {file.FormatLabel, NumberText(file.Width), NumberText(file.Height)});
    else
        details = file.FormatLabel;
    if (!file.Licence.empty())
        details += L", " + LabelText(catalog, Label::CT_LICENCE, {file.Licence});
    return details;
}

std::string ContentsText(const ManifestData& data, const ContentsOptions& options, const ITextCatalog& catalog)
{
    std::wstring text = options.Title + L"\r\n";
    std::vector<std::pair<std::wstring, std::wstring>> header = {
        {LabelText(catalog, Label::CT_PACKAGE), data.PackageName},
        {LabelText(catalog, Label::CT_DATE), options.DateText},
        {LabelText(catalog, Label::CT_SPEC), data.SpecName + (data.SpecRevision.empty() ? L"" : L" (" + data.SpecRevision + L")")},
        {LabelText(catalog, Label::CT_FILES), NumberText((int64_t)data.Files.size()) + L" (" + FormatSize(data.TotalBytes()) + L")"}};
    size_t labelWidth = 0;
    for (const auto& line : header)
        labelWidth = (std::max)(labelWidth, line.first.size());
    for (const auto& line : header)
        text += PadRight(line.first, labelWidth + 2) + line.second + L"\r\n";
    for (const std::wstring& note : options.Notes)
        text += note + L"\r\n";
    text += L"\r\n";

    // Groups: folders in path order, or rules in specification order.
    std::vector<std::pair<std::wstring, std::vector<const ManifestFile*>>> groups;
    auto groupOf = [&](const std::wstring& name) -> std::vector<const ManifestFile*>& {
        for (auto& group : groups)
            if (group.first == name)
                return group.second;
        groups.push_back(std::make_pair(name, std::vector<const ManifestFile*>()));
        return groups.back().second;
    };
    if (options.GroupByRule)
    {
        for (const auto& rule : options.RuleOrder)
            for (const ManifestFile& file : data.Files)
                if (file.Rule == rule.first)
                    groupOf(rule.second).push_back(&file);
    }
    else
    {
        for (const ManifestFile& file : data.Files)
        {
            std::wstring folder = ParentOf(file.Path);
            groupOf(folder.empty() ? L"/" : folder + L"/").push_back(&file);
        }
    }
    size_t nameWidth = 0, sizeWidth = 0, detailsWidth = 0;
    for (const ManifestFile& file : data.Files)
    {
        nameWidth = (std::max)(nameWidth, (options.GroupByRule ? file.Path : FileNameOf(file.Path)).size());
        sizeWidth = (std::max)(sizeWidth, FormatSize(file.Bytes).size());
        detailsWidth = (std::max)(detailsWidth, Details(file, catalog).size());
    }
    for (const auto& group : groups)
    {
        text += group.first + L"\r\n";
        for (const ManifestFile* file : group.second)
        {
            std::wstring name = options.GroupByRule ? file->Path : FileNameOf(file->Path);
            std::wstring line = L"  " + PadRight(name, nameWidth + 2) + PadLeft(FormatSize(file->Bytes), sizeWidth) + L"   " +
                                PadRight(Details(*file, catalog), detailsWidth + 2) + (options.GroupByRule ? L"" : file->RuleTitle);
            while (!line.empty() && line.back() == L' ')
                line.pop_back();
            text += line + L"\r\n";
        }
    }
    text += L"\r\n";
    text += options.CsvName.empty() ? LabelText(catalog, Label::CT_CHECKSUMS_ONE, {options.JsonName})
                                    : LabelText(catalog, Label::CT_CHECKSUMS_TWO, {options.CsvName, options.JsonName});
    text += L"\r\n";
    return "\xEF\xBB\xBF" + WideToUtf8(text);
}

std::string BuildRecordText(const BuildRecordData& data, const ITextCatalog& catalog)
{
    JsonWriter writer;
    writer.BeginObject();
    writer.MemberInt(L"handoffBuildRecord", 1);
    writer.Key(L"package");
    writer.BeginObject();
    writer.Member(L"name", data.PackageName);
    writer.Member(L"path", data.PackagePath);
    writer.Member(L"manifestSha256", data.ManifestSha256);
    writer.EndObject();
    writer.Key(L"session");
    writer.BeginObject();
    writer.Member(L"id", data.SessionId);
    writer.Member(L"user", data.User);
    writer.Member(L"machine", data.Machine);
    writer.Member(L"startedUtc", data.StartedUtc);
    writer.Member(L"finishedUtc", data.FinishedUtc);
    writer.Member(L"host", data.HostVersion);
    writer.Member(L"plugin", data.PluginVersion);
    writer.EndObject();
    writer.Member(L"workingRoot", data.WorkingRoot);
    writer.Member(L"scope", data.Scope);
    writer.Key(L"spec");
    writer.BeginObject();
    writer.Member(L"path", data.SpecPath);
    writer.Member(L"sha256", data.SpecSha256);
    writer.Member(L"text", data.SpecText);
    writer.EndObject();
    writer.Key(L"decisions");
    writer.BeginArray();
    for (const BuildRecordDecision& decision : data.Decisions)
    {
        writer.BeginObject();
        writer.Member(L"action", decision.Action);
        writer.Member(L"source", decision.Source);
        if (!decision.RuleId.empty())
            writer.Member(L"rule", decision.RuleId);
        if (!decision.Code.empty())
            writer.Member(L"code", decision.Code);
        if (!decision.Reason.empty())
            writer.Member(L"reason", decision.Reason);
        writer.Member(L"user", decision.User);
        writer.Member(L"utc", decision.Utc);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key(L"files");
    writer.BeginArray();
    for (const auto& file : data.Files)
    {
        writer.BeginObject();
        writer.Member(L"source", file.first);
        writer.Member(L"target", file.second.first);
        writer.Member(L"sha256", file.second.second);
        writer.EndObject();
    }
    writer.EndArray();
    writer.Key(L"findings");
    writer.BeginArray();
    for (const Finding& finding : data.Findings)
        WriteFinding(writer, finding, catalog, true);
    writer.EndArray();
    writer.EndObject();
    return writer.Text();
}

} // namespace handoff
