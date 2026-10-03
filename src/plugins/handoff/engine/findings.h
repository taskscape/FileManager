// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Findings: stable codes with a catalogue default severity (C.6), optional
// specification overrides (C.4.12), and localized messages supplied by the
// host through ITextCatalog (the engine falls back to English).

#include <map>
#include <string>
#include <vector>

namespace handoff
{

enum class Severity
{
    Off = 0,
    Info = 1,
    Warning = 2,
    Error = 3
};

const wchar_t* SeverityName(Severity severity); // "error", "warning", "info", "off"
bool ParseSeverityName(const std::wstring& text, Severity& severity);

struct CodeInfo
{
    const wchar_t* Code; // "HO-SPEC-001"
    int TextId;          // IDS_HO_SPEC_001
    Severity Default;
    bool Locked;         // cannot be changed by 'severity' or overridden by reviewers
    const wchar_t* English;
};

const CodeInfo* FindCode(const std::wstring& code);
const std::vector<CodeInfo>& AllCodes();

struct LabelInfo
{
    const wchar_t* Name;
    int TextId;
    const wchar_t* English;
};

enum class Label
{
#define HO_LABEL(name, text) name,
#include "labels.inc"
#undef HO_LABEL
    Count
};

// Host-provided localization; the default implementation returns English.
class ITextCatalog
{
public:
    virtual ~ITextCatalog() {}
    virtual std::wstring Text(int textId, const wchar_t* english) const = 0;
};

class EnglishCatalog : public ITextCatalog
{
public:
    std::wstring Text(int, const wchar_t* english) const override { return english; }
};

const ITextCatalog& DefaultCatalog();
std::wstring LabelText(const ITextCatalog& catalog, Label label, const std::vector<std::wstring>& args = {});

typedef std::map<std::wstring, Severity> SeverityMap;

struct Finding
{
    std::wstring Code;
    Severity Sev = Severity::Error;
    std::wstring Item;   // working- or package-relative path, or empty for package-wide findings
    std::wstring RuleId; // rule scope for 'severity' overrides
    std::vector<std::wstring> Args;
    int Line = 0;        // specification findings only
    int Column = 0;
    std::wstring Pointer;
    bool Overridden = false; // an error accepted by a reviewer (C.4.12)
    std::wstring OverrideReason;
};

std::wstring FindingMessage(const Finding& finding, const ITextCatalog& catalog);

// Resolves the effective severity: catalogue (or policy-driven) default, then
// the top-level map, then the rule map; locked codes ignore both maps.
Severity ResolveSeverity(const std::wstring& code, Severity policyDefault, const SeverityMap* topLevel,
                         const SeverityMap* rule);

// Convenience constructor applying ResolveSeverity with the catalogue default.
Finding MakeFinding(const wchar_t* code, const std::wstring& item, std::vector<std::wstring> args = {});

} // namespace handoff
