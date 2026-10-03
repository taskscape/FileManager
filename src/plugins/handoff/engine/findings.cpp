// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "findings.h"
#include "text_ids.rh"
#include "text_util.h"

namespace handoff
{

const wchar_t* SeverityName(Severity severity)
{
    switch (severity)
    {
    case Severity::Error: return L"error";
    case Severity::Warning: return L"warning";
    case Severity::Info: return L"info";
    default: return L"off";
    }
}

bool ParseSeverityName(const std::wstring& text, Severity& severity)
{
    if (text == L"error")
        severity = Severity::Error;
    else if (text == L"warning")
        severity = Severity::Warning;
    else if (text == L"info")
        severity = Severity::Info;
    else if (text == L"off")
        severity = Severity::Off;
    else
        return false;
    return true;
}

static std::wstring CodeText(const char* raw)
{
    // "SPEC_001" -> "HO-SPEC-001"
    std::wstring code = L"HO-";
    for (const char* p = raw; *p != 0; p++)
        code += (*p == '_') ? L'-' : (wchar_t)*p;
    return code;
}

namespace
{
struct CatalogStorage
{
    std::vector<std::wstring> Codes;
    std::vector<CodeInfo> Infos;
    CatalogStorage()
    {
        // Codes and string IDs come from the same append-only list, so the
        // engine and the language modules cannot drift apart silently.
#define HO_CODE(name, severity, locked, text) \
    Codes.push_back(CodeText(#name));          \
    Infos.push_back(CodeInfo{nullptr, IDS_HO_##name, Severity::severity, (locked) != 0, text});
#include "catalog.inc"
#undef HO_CODE
        for (size_t i = 0; i < Infos.size(); i++)
            Infos[i].Code = Codes[i].c_str();
    }
};

const CatalogStorage& Catalog()
{
    static CatalogStorage storage;
    return storage;
}

const LabelInfo Labels[] = {
#define HO_LABEL(name, text) {L## #name, IDS_HL_##name, text},
#include "labels.inc"
#undef HO_LABEL
};
} // namespace

const std::vector<CodeInfo>& AllCodes()
{
    return Catalog().Infos;
}

const CodeInfo* FindCode(const std::wstring& code)
{
    for (const CodeInfo& info : Catalog().Infos)
        if (code == info.Code)
            return &info;
    return nullptr;
}

const ITextCatalog& DefaultCatalog()
{
    static EnglishCatalog english;
    return english;
}

std::wstring LabelText(const ITextCatalog& catalog, Label label, const std::vector<std::wstring>& args)
{
    const LabelInfo& info = Labels[(int)label];
    return FormatPositional(catalog.Text(info.TextId, info.English), args);
}

std::wstring FindingMessage(const Finding& finding, const ITextCatalog& catalog)
{
    const CodeInfo* info = FindCode(finding.Code);
    if (info == nullptr)
        return finding.Code;
    return FormatPositional(catalog.Text(info->TextId, info->English), finding.Args);
}

Severity ResolveSeverity(const std::wstring& code, Severity policyDefault, const SeverityMap* topLevel,
                         const SeverityMap* rule)
{
    const CodeInfo* info = FindCode(code);
    if (info != nullptr && info->Locked)
        return info->Default;
    Severity result = policyDefault;
    if (topLevel != nullptr)
    {
        auto it = topLevel->find(code);
        if (it != topLevel->end())
            result = it->second;
    }
    if (rule != nullptr)
    {
        auto it = rule->find(code);
        if (it != rule->end())
            result = it->second;
    }
    return result;
}

Finding MakeFinding(const wchar_t* code, const std::wstring& item, std::vector<std::wstring> args)
{
    Finding finding;
    finding.Code = code;
    const CodeInfo* info = FindCode(code);
    finding.Sev = info != nullptr ? info->Default : Severity::Error;
    finding.Item = item;
    finding.Args = std::move(args);
    return finding;
}

} // namespace handoff
