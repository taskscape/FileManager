// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Name templates ("{client}-{project}_{stem}{ext}") and the naming policy that
// turns their expansion into portable, Windows-valid names (C.4.10).

#include <windows.h>
#include <map>
#include <string>
#include <vector>

#include "patterns.h"

namespace handoff
{

struct NamingPolicy
{
    bool Portable = true;       // "portable": A-Z a-z 0-9 . _ - (plus space when allowed)
    bool AllowSpaces = false;
    std::wstring CaseMode = L"keep"; // keep | lower | upper
    int MaxNameLength = 120;
    bool Transliterate = true;
    wchar_t Replacement = L'_';
    SafeRegex SourceNameRegex;  // optional; HO-NAME-001 when a working name does not match
};

struct SanitizeResult
{
    std::wstring Value;
    bool Changed = false;   // differs from the expanded input (HO-NAME-010)
    bool Truncated = false; // HO-NAME-015
    bool Invalid = false;   // empty after cleanup (HO-NAME-011)
    bool Reserved = false;  // reserved device name (HO-NAME-014)
};

// 'isFileName' keeps the extension when truncating.
SanitizeResult SanitizeName(const std::wstring& raw, const NamingPolicy& policy, bool isFileName);

struct TemplateContext
{
    // Package variables plus built-ins (date, specId, specName, specRevision).
    const std::map<std::wstring, std::wstring>* Variables = nullptr;
    SYSTEMTIME Date = {}; // for {date:fmt}
    bool HasFile = false;
    std::wstring Stem, Ext, Name, Key, Rev, Rule, RelDir;
    int Seq = 0;
};

class NameTemplate
{
public:
    struct Part
    {
        bool Token = false;
        std::wstring Text;   // literal text or token name
        std::wstring Format; // after ':' for {date:...} and {seq:...}
    };

    bool Parse(const std::wstring& text, std::wstring& error);
    bool IsEmpty() const { return Parts.empty(); }
    const std::wstring& Source() const { return Text; }
    std::vector<std::wstring> TokenNames() const;
    bool UsesToken(const wchar_t* name) const;
    std::wstring Expand(const TemplateContext& context) const;

    std::vector<Part> Parts;

private:
    std::wstring Text;
};

bool IsPerFileToken(const std::wstring& name);
bool IsBuiltInVariable(const std::wstring& name);
bool IsReservedVariableName(const std::wstring& name);

} // namespace handoff
