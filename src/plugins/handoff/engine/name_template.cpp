// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "name_template.h"
#include "text_util.h"

namespace handoff
{

static const wchar_t* PerFileTokens[] = {L"stem", L"ext", L"name", L"key", L"rev", L"rule", L"relDir", L"seq"};
static const wchar_t* BuiltIns[] = {L"date", L"specId", L"specName", L"specRevision"};

bool IsPerFileToken(const std::wstring& name)
{
    for (const wchar_t* token : PerFileTokens)
        if (name == token)
            return true;
    return false;
}

bool IsBuiltInVariable(const std::wstring& name)
{
    for (const wchar_t* token : BuiltIns)
        if (name == token)
            return true;
    return false;
}

bool IsReservedVariableName(const std::wstring& name)
{
    // 'folder' and 'dir' are reserved for future per-file tokens (C.4.3).
    return IsPerFileToken(name) || IsBuiltInVariable(name) || name == L"folder" || name == L"dir";
}

bool NameTemplate::Parse(const std::wstring& text, std::wstring& error)
{
    Text = text;
    Parts.clear();
    std::wstring literal;
    for (size_t i = 0; i < text.size(); i++)
    {
        wchar_t c = text[i];
        if (c == L'}')
        {
            error = L"unbalanced '}'";
            return false;
        }
        if (c != L'{')
        {
            literal += c;
            continue;
        }
        size_t close = text.find(L'}', i + 1);
        if (close == std::wstring::npos)
        {
            error = L"unbalanced '{'";
            return false;
        }
        std::wstring body = text.substr(i + 1, close - i - 1);
        if (body.empty() || body.find(L'{') != std::wstring::npos)
        {
            error = L"empty or nested token";
            return false;
        }
        if (!literal.empty())
        {
            Parts.push_back(Part{false, literal, L""});
            literal.clear();
        }
        Part token;
        token.Token = true;
        size_t colon = body.find(L':');
        token.Text = colon == std::wstring::npos ? body : body.substr(0, colon);
        if (colon != std::wstring::npos)
            token.Format = body.substr(colon + 1);
        Parts.push_back(token);
        i = close;
    }
    if (!literal.empty())
        Parts.push_back(Part{false, literal, L""});
    return true;
}

std::vector<std::wstring> NameTemplate::TokenNames() const
{
    std::vector<std::wstring> names;
    for (const Part& part : Parts)
        if (part.Token)
            names.push_back(part.Text);
    return names;
}

bool NameTemplate::UsesToken(const wchar_t* name) const
{
    for (const Part& part : Parts)
        if (part.Token && part.Text == name)
            return true;
    return false;
}

std::wstring NameTemplate::Expand(const TemplateContext& context) const
{
    std::wstring out;
    for (const Part& part : Parts)
    {
        if (!part.Token)
        {
            out += part.Text;
            continue;
        }
        const std::wstring& name = part.Text;
        if (name == L"date" && !part.Format.empty())
            out += FormatDate(context.Date, part.Format);
        else if (context.HasFile && name == L"stem")
            out += context.Stem;
        else if (context.HasFile && name == L"ext")
            out += context.Ext;
        else if (context.HasFile && name == L"name")
            out += context.Name;
        else if (context.HasFile && name == L"key")
            out += context.Key;
        else if (context.HasFile && name == L"rev")
            out += context.Rev;
        else if (context.HasFile && name == L"rule")
            out += context.Rule;
        else if (context.HasFile && name == L"relDir")
            out += context.RelDir;
        else if (context.HasFile && name == L"seq")
        {
            std::wstring digits = NumberText(context.Seq);
            // "{seq:000}" pads to the width of its format.
            while (digits.size() < part.Format.size())
                digits.insert(digits.begin(), L'0');
            out += digits;
        }
        else if (context.Variables != nullptr)
        {
            auto it = context.Variables->find(name);
            if (it != context.Variables->end())
                out += it->second;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sanitization (C.4.10 pipeline)

static std::wstring Transliterate(const std::wstring& text)
{
    // Letters that compatibility decomposition does not reduce to ASCII.
    static const struct
    {
        wchar_t From;
        const wchar_t* To;
    } table[] = {{L'Ł', L"L"},  {L'ł', L"l"}, {L'Đ', L"D"}, {L'đ', L"d"},
                 {L'Ø', L"O"},  {L'ø', L"o"}, {L'Æ', L"AE"}, {L'æ', L"ae"},
                 {L'Œ', L"OE"}, {L'œ', L"oe"}, {L'ß', L"ss"}, {L'Þ', L"Th"},
                 {L'þ', L"th"}, {L'ı', L"i"}};
    std::wstring mapped;
    for (wchar_t c : text)
    {
        bool replaced = false;
        for (const auto& entry : table)
        {
            if (entry.From == c)
            {
                mapped += entry.To;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            mapped += c;
    }
    int needed = NormalizeString(NormalizationKD, mapped.c_str(), (int)mapped.size(), NULL, 0);
    if (needed <= 0)
        return mapped;
    std::wstring decomposed((size_t)needed, L'\0');
    int written = NormalizeString(NormalizationKD, mapped.c_str(), (int)mapped.size(), &decomposed[0], needed);
    if (written <= 0)
        return mapped;
    decomposed.resize((size_t)written);
    std::vector<WORD> types(decomposed.size());
    std::wstring out;
    if (!GetStringTypeW(CT_CTYPE3, decomposed.c_str(), (int)decomposed.size(), types.data()))
        return decomposed;
    for (size_t i = 0; i < decomposed.size(); i++)
    {
        // Combining marks (accents) are dropped: "é" -> "e".
        if ((types[i] & C3_NONSPACING) != 0)
            continue;
        out += decomposed[i];
    }
    return out;
}

static bool IsPortableChar(wchar_t c, bool allowSpaces)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'.' ||
           c == L'_' || c == L'-' || (allowSpaces && c == L' ');
}

static void TruncateUnits(std::wstring& text, size_t units)
{
    if (text.size() <= units)
        return;
    text.resize(units);
    // Never leave half of a surrogate pair behind.
    if (!text.empty() && IS_HIGH_SURROGATE(text.back()))
        text.pop_back();
}

SanitizeResult SanitizeName(const std::wstring& raw, const NamingPolicy& policy, bool isFileName)
{
    SanitizeResult result;
    std::wstring value = NormalizeNfc(raw);
    if (policy.Portable && policy.Transliterate)
        value = Transliterate(value);

    std::wstring cleaned;
    for (size_t i = 0; i < value.size(); i++)
    {
        wchar_t c = value[i];
        bool allowed;
        if (policy.Portable)
            allowed = IsPortableChar(c, policy.AllowSpaces);
        else
            allowed = c >= 32 && wcschr(L"<>:\"/\\|?*", c) == NULL && (policy.AllowSpaces || c != L' ');
        if (!allowed)
        {
            if (!policy.Portable && IS_HIGH_SURROGATE(c) && i + 1 < value.size() && IS_LOW_SURROGATE(value[i + 1]))
            {
                cleaned += c;
                cleaned += value[++i];
                continue;
            }
            c = policy.Replacement;
        }
        // Runs of the replacement character collapse to one.
        if (c == policy.Replacement && !cleaned.empty() && cleaned.back() == policy.Replacement)
            continue;
        cleaned += c;
    }

    size_t start = 0, end = cleaned.size();
    auto trimmable = [&](wchar_t c) { return c == L' ' || c == L'.' || c == policy.Replacement; };
    while (start < end && trimmable(cleaned[start]))
        start++;
    while (end > start && trimmable(cleaned[end - 1]))
        end--;
    value = cleaned.substr(start, end - start);

    if (policy.CaseMode == L"lower")
        value = LowerInvariant(value);
    else if (policy.CaseMode == L"upper")
        value = UpperInvariant(value);

    if (policy.MaxNameLength > 0 && value.size() > (size_t)policy.MaxNameLength)
    {
        std::wstring ext = isFileName ? ExtensionOf(value) : std::wstring();
        if (!ext.empty() && ext.size() < (size_t)policy.MaxNameLength)
        {
            std::wstring stem = value.substr(0, value.size() - ext.size());
            TruncateUnits(stem, (size_t)policy.MaxNameLength - ext.size());
            while (!stem.empty() && trimmable(stem.back()))
                stem.pop_back();
            value = stem + ext;
        }
        else
            TruncateUnits(value, (size_t)policy.MaxNameLength);
        result.Truncated = true;
    }

    result.Value = value;
    result.Invalid = value.empty();
    result.Reserved = !value.empty() && IsReservedDeviceName(value);
    result.Changed = value != raw;
    return result;
}

} // namespace handoff
