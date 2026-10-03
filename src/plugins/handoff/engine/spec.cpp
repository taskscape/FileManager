// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "spec.h"
#include "text_util.h"

#include <algorithm>
#include <initializer_list>
#include <math.h>

namespace handoff
{

const RuleSpec* Spec::FindRule(const std::wstring& id) const
{
    for (const RuleSpec& rule : Rules)
        if (rule.Id == id)
            return &rule;
    return nullptr;
}

namespace
{
struct NamedSize
{
    const wchar_t* Name;
    double Width, Height;
};

// C.4.7.1, portrait width x height in millimetres.
const NamedSize PageSizes[] = {
    {L"A0", 841, 1189},        {L"A1", 594, 841},          {L"A2", 420, 594},          {L"A3", 297, 420},
    {L"A4", 210, 297},         {L"A5", 148, 210},          {L"A6", 105, 148},          {L"B1", 707, 1000},
    {L"B2", 500, 707},         {L"B3", 353, 500},          {L"B4", 250, 353},          {L"Letter", 215.9, 279.4},
    {L"Legal", 215.9, 355.6},  {L"Tabloid", 279.4, 431.8}, {L"ARCH-A", 228.6, 304.8}, {L"ARCH-B", 304.8, 457.2},
    {L"ARCH-C", 457.2, 609.6}, {L"ARCH-D", 609.6, 914.4},  {L"ARCH-E", 914.4, 1219.2}};
} // namespace

bool LookupPageSize(const std::wstring& name, double& widthMm, double& heightMm)
{
    for (const NamedSize& size : PageSizes)
    {
        if (EqualsNoCase(name, size.Name))
        {
            widthMm = size.Width;
            heightMm = size.Height;
            return true;
        }
    }
    return false;
}

std::wstring NamePageSize(double widthMm, double heightMm, double toleranceMm, bool* landscape)
{
    if (landscape != nullptr)
        *landscape = widthMm > heightMm;
    double shortSide = (std::min)(widthMm, heightMm), longSide = (std::max)(widthMm, heightMm);
    for (const NamedSize& size : PageSizes)
        if (fabs(shortSide - size.Width) <= toleranceMm && fabs(longSide - size.Height) <= toleranceMm)
            return size.Name;
    return std::wstring();
}

const std::vector<std::wstring>& KnownTopLevelMembers()
{
    static const std::vector<std::wstring> members = {
        L"handoffSpec", L"id", L"name", L"revision", L"description", L"variables", L"package",
        L"naming", L"content", L"formats", L"rules", L"severity", L"policy"};
    return members;
}

namespace
{

typedef std::initializer_list<const wchar_t*> Names;

bool MatchesIdentifier(const std::wstring& text, size_t maxLength, bool allowDot, bool allowUpper)
{
    if (text.empty() || text.size() > maxLength)
        return false;
    for (size_t i = 0; i < text.size(); i++)
    {
        wchar_t c = text[i];
        bool lower = c >= L'a' && c <= L'z';
        bool upper = c >= L'A' && c <= L'Z';
        bool digit = c >= L'0' && c <= L'9';
        if (i == 0)
        {
            if (!(lower || digit || (allowUpper && upper)))
                return false;
            continue;
        }
        if (!(lower || digit || c == L'-' || c == L'_' || (allowDot && c == L'.') || (allowUpper && upper)))
            return false;
    }
    return true;
}

bool IsVariableName(const std::wstring& name)
{
    if (name.empty() || name.size() > 32)
        return false;
    if (!((name[0] >= L'A' && name[0] <= L'Z') || (name[0] >= L'a' && name[0] <= L'z')))
        return false;
    for (wchar_t c : name)
        if (!iswalnum(c) || c > 127)
            return false;
    return true;
}

class SpecReader
{
public:
    SpecReader(Spec& spec, std::vector<Finding>& findings, const ITextCatalog& catalog)
        : S(spec), F(findings), Catalog(catalog)
    {
    }

    void Read(const JsonValue& root)
    {
        if (!root.IsObject())
        {
            Add(L"HO-SPEC-004", root, L"", {L"/", L"expected an object"});
            return;
        }
        CheckMembers(root, L"", KnownTopLevelMembers());
        const JsonValue* version = root.Find(L"handoffSpec");
        if (version == nullptr)
        {
            Add(L"HO-SPEC-003", root, L"/handoffSpec", {L"handoffSpec"});
            return;
        }
        int64_t v = 0;
        if (!version->AsInteger(v) || v != 1)
        {
            // Later fields may mean something else in another version, so stop here.
            Add(L"HO-SPEC-005", *version, L"/handoffSpec", {version->IsNumber() ? version->String : L"?"});
            return;
        }
        S.Version = v;
        std::wstring id;
        if (Str(root, L"id", L"", id, true, 64) && !MatchesIdentifier(id, 64, true, false))
            Add(L"HO-SPEC-004", *root.Find(L"id"), L"/id", {L"id", L"use 1-64 lower-case letters, digits, '.', '_' or '-'"});
        S.Id = id;
        Str(root, L"name", L"", S.Name, true, 128);
        Str(root, L"revision", L"", S.Revision, false, 64);
        Str(root, L"description", L"", S.Description, false, 4096);

        if (const JsonValue* formats = Obj(root, L"formats", L""))
            ReadFormats(*formats, L"/formats");
        if (const JsonValue* variables = Obj(root, L"variables", L""))
            ReadVariables(*variables, L"/variables");
        if (const JsonValue* naming = Obj(root, L"naming", L""))
            ReadNaming(*naming, L"/naming");
        const JsonValue* package = Obj(root, L"package", L"");
        if (package == nullptr)
        {
            if (root.Find(L"package") == nullptr)
                Add(L"HO-SPEC-003", root, L"/package", {L"package"});
        }
        else
            ReadPackage(*package, L"/package");
        if (const JsonValue* content = Obj(root, L"content", L""))
            ReadContent(*content, L"/content");
        ReadRules(root);
        if (const JsonValue* severity = Obj(root, L"severity", L""))
            ReadSeverityMap(*severity, L"/severity", S.Severity);
        if (const JsonValue* policy = Obj(root, L"policy", L""))
        {
            CheckMembers(*policy, L"/policy", {L"allowErrorOverride", L"requireWarningAcknowledgement"});
            Bool(*policy, L"allowErrorOverride", L"/policy", S.AllowErrorOverride);
            Bool(*policy, L"requireWarningAcknowledgement", L"/policy", S.RequireWarningAcknowledgement);
        }
        CheckOutputs(package);
    }

private:
    Spec& S;
    std::vector<Finding>& F;
    const ITextCatalog& Catalog;

    void Add(const wchar_t* code, const JsonValue& at, const std::wstring& pointer, std::vector<std::wstring> args)
    {
        if (F.size() >= 200)
            return; // C.4.1: findings per validation are capped
        Finding finding = MakeFinding(code, L"", std::move(args));
        finding.Line = at.Line;
        finding.Column = at.Column;
        finding.Pointer = pointer.empty() ? L"/" : pointer;
        F.push_back(finding);
    }

    static std::wstring Ptr(const std::wstring& base, const wchar_t* name) { return JsonPointerAppend(base, name); }

    void CheckMembers(const JsonValue& object, const std::wstring& pointer, const std::vector<std::wstring>& known)
    {
        for (const auto& member : object.Members)
        {
            if (member.first == L"$comment")
                continue;
            if (std::find(known.begin(), known.end(), member.first) != known.end())
                continue;
            // A close known name is offered as a "did you mean" hint for typos.
            std::wstring best;
            size_t bestDistance = 3;
            for (const std::wstring& candidate : known)
            {
                size_t distance = EditDistance(member.first, candidate);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = candidate;
                }
            }
            std::wstring hint = best.empty() ? std::wstring() : LabelText(Catalog, Label::DID_YOU_MEAN, {best});
            Add(L"HO-SPEC-002", member.second, JsonPointerAppend(pointer, member.first), {member.first, hint});
        }
    }

    void CheckMembers(const JsonValue& object, const std::wstring& pointer, Names known)
    {
        std::vector<std::wstring> names;
        for (const wchar_t* name : known)
            names.push_back(name);
        CheckMembers(object, pointer, names);
    }

    void TypeError(const JsonValue& value, const std::wstring& pointer, const wchar_t* expected)
    {
        Add(L"HO-SPEC-004", value, pointer, {pointer, std::wstring(L"expected ") + expected});
    }

    const JsonValue* Obj(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
            return nullptr;
        if (!value->IsObject())
        {
            TypeError(*value, Ptr(pointer, name), L"an object");
            return nullptr;
        }
        return value;
    }

    bool Str(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, std::wstring& out,
             bool required = false, size_t maxLength = 4096)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
        {
            if (required)
                Add(L"HO-SPEC-003", parent, Ptr(pointer, name), {name});
            return false;
        }
        if (!value->IsString())
        {
            TypeError(*value, Ptr(pointer, name), L"a string");
            return false;
        }
        if (value->String.size() > maxLength)
        {
            Add(L"HO-SPEC-006", *value, Ptr(pointer, name), {L"string longer than " + NumberText((int64_t)maxLength) + L" characters"});
            return false;
        }
        if (required && Trim(value->String).empty())
        {
            Add(L"HO-SPEC-004", *value, Ptr(pointer, name), {Ptr(pointer, name), L"must not be empty"});
            return false;
        }
        out = value->String;
        return true;
    }

    bool Bool(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, bool& out)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
            return false;
        if (!value->IsBool())
        {
            TypeError(*value, Ptr(pointer, name), L"true or false");
            return false;
        }
        out = value->Boolean;
        return true;
    }

    bool Int(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, int64_t& out, int64_t minimum,
             int64_t maximum, bool required = false)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
        {
            if (required)
                Add(L"HO-SPEC-003", parent, Ptr(pointer, name), {name});
            return false;
        }
        int64_t v;
        if (!value->AsInteger(v))
        {
            TypeError(*value, Ptr(pointer, name), L"an integer");
            return false;
        }
        if (v < minimum || v > maximum)
        {
            Add(L"HO-SPEC-004", *value, Ptr(pointer, name),
                {Ptr(pointer, name), L"must be between " + NumberText(minimum) + L" and " + NumberText(maximum)});
            return false;
        }
        out = v;
        return true;
    }

    bool Num(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, double& out, double minimum,
             double maximum, bool required = false)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
        {
            if (required)
                Add(L"HO-SPEC-003", parent, Ptr(pointer, name), {name});
            return false;
        }
        double v;
        if (!value->AsDouble(v))
        {
            TypeError(*value, Ptr(pointer, name), L"a number");
            return false;
        }
        if (v < minimum || v > maximum)
        {
            Add(L"HO-SPEC-004", *value, Ptr(pointer, name),
                {Ptr(pointer, name), L"must be between " + DecimalText(minimum, 0) + L" and " + DecimalText(maximum, 0)});
            return false;
        }
        out = v;
        return true;
    }

    bool Enum(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, std::wstring& out, Names values)
    {
        std::wstring text;
        if (!Str(parent, name, pointer, text))
            return false;
        std::vector<std::wstring> allowed;
        for (const wchar_t* candidate : values)
        {
            allowed.push_back(candidate);
            if (text == candidate)
            {
                out = text;
                return true;
            }
        }
        Add(L"HO-SPEC-004", *parent.Find(name), Ptr(pointer, name),
            {Ptr(pointer, name), L"must be one of: " + Join(allowed, L", ")});
        return false;
    }

    bool StrArray(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer,
                  std::vector<std::wstring>& out, size_t maxItems = 64, bool required = false)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
        {
            if (required)
                Add(L"HO-SPEC-003", parent, Ptr(pointer, name), {name});
            return false;
        }
        if (!value->IsArray())
        {
            TypeError(*value, Ptr(pointer, name), L"an array of strings");
            return false;
        }
        if (value->Items.size() > maxItems)
        {
            Add(L"HO-SPEC-006", *value, Ptr(pointer, name), {L"more than " + NumberText((int64_t)maxItems) + L" entries"});
            return false;
        }
        out.clear();
        bool ok = true;
        for (size_t i = 0; i < value->Items.size(); i++)
        {
            const JsonValue& item = value->Items[i];
            if (!item.IsString())
            {
                TypeError(item, Ptr(pointer, name) + L"/" + NumberText((int64_t)i), L"a string");
                ok = false;
                continue;
            }
            out.push_back(item.String);
        }
        return ok;
    }

    bool Regex(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, SafeRegex& out,
               int minGroups = 0, bool required = false)
    {
        std::wstring text;
        if (!Str(parent, name, pointer, text, required, SafeRegex::MaxPatternLength + 1))
            return false;
        std::wstring error;
        if (!out.Compile(text, error))
        {
            Add(L"HO-SPEC-010", *parent.Find(name), Ptr(pointer, name), {error});
            return false;
        }
        if (out.GroupCount() < minGroups)
        {
            Add(L"HO-SPEC-010", *parent.Find(name), Ptr(pointer, name), {L"the pattern needs a capturing group"});
            out = SafeRegex();
            return false;
        }
        return true;
    }

    bool Globs(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, std::vector<Glob>& out,
               bool required)
    {
        std::vector<std::wstring> patterns;
        if (!StrArray(parent, name, pointer, patterns, 64, required))
            return false;
        if (required && patterns.empty())
        {
            Add(L"HO-SPEC-004", *parent.Find(name), Ptr(pointer, name), {Ptr(pointer, name), L"must contain at least one pattern"});
            return false;
        }
        const JsonValue* array = parent.Find(name);
        for (size_t i = 0; i < patterns.size(); i++)
        {
            Glob glob;
            std::wstring error;
            if (!glob.Compile(patterns[i], error))
            {
                Add(L"HO-SPEC-011", array->Items[i], Ptr(pointer, name) + L"/" + NumberText((int64_t)i),
                    {patterns[i] + L" (" + error + L")"});
                continue;
            }
            out.push_back(glob);
        }
        return true;
    }

    bool Size(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, uint64_t& out)
    {
        const JsonValue* value = parent.Find(name);
        if (value == nullptr)
            return false;
        int64_t integer;
        if (value->AsInteger(integer) && integer >= 0)
        {
            out = (uint64_t)integer;
            return true;
        }
        if (value->IsString() && ParseSize(value->String, out))
            return true;
        TypeError(*value, Ptr(pointer, name), L"a size such as 1048576 or \"200 MB\"");
        return false;
    }

    void Range(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, IntRange& out,
               int64_t minimum, int64_t maximum)
    {
        const JsonValue* value = Obj(parent, name, pointer);
        if (value == nullptr)
            return;
        std::wstring p = Ptr(pointer, name);
        CheckMembers(*value, p, {L"min", L"max"});
        out.HasMin = Int(*value, L"min", p, out.Min, minimum, maximum);
        out.HasMax = Int(*value, L"max", p, out.Max, minimum, maximum);
        if (out.HasMin && out.HasMax && out.Min > out.Max)
            Add(L"HO-SPEC-004", *value, p, {p, L"min must not exceed max"});
    }

    bool HasVariable(const std::wstring& name) const
    {
        if (IsBuiltInVariable(name))
            return true;
        for (const VariableDef& variable : S.Variables)
            if (variable.Name == name)
                return true;
        return false;
    }

    // Validates tokens: package-level templates may use variables and built-ins;
    // per-file templates may also use per-file tokens ({key}/{rev} need their rule features).
    void CheckTemplate(const NameTemplate& tmpl, const JsonValue& at, const std::wstring& pointer, const RuleSpec* rule)
    {
        for (const NameTemplate::Part& part : tmpl.Parts)
        {
            if (!part.Token)
                continue;
            const std::wstring& name = part.Text;
            bool ok;
            if (IsPerFileToken(name))
            {
                ok = rule != nullptr;
                if (ok && name == L"key")
                    ok = rule->Expect.Present;
                if (ok && name == L"rev")
                    ok = rule->Select.Latest.Present;
                if (ok && name == L"seq" && !part.Format.empty())
                    ok = part.Format.find_first_not_of(L'0') == std::wstring::npos && part.Format.size() <= 9;
            }
            else
                ok = HasVariable(name);
            if (ok && !part.Format.empty() && name != L"seq")
                ok = name == L"date" && IsSupportedDateFormat(part.Format);
            if (!ok)
                Add(L"HO-SPEC-012", at, pointer, {part.Format.empty() ? name : name + L":" + part.Format});
        }
    }

    bool Template(const JsonValue& parent, const wchar_t* name, const std::wstring& pointer, NameTemplate& out,
                  const RuleSpec* rule, bool required, bool folder)
    {
        std::wstring text;
        if (!Str(parent, name, pointer, text, required, 512))
            return false;
        std::wstring error;
        const JsonValue& at = *parent.Find(name);
        std::wstring p = Ptr(pointer, name);
        if (!out.Parse(text, error))
        {
            Add(L"HO-SPEC-012", at, p, {text + L" (" + error + L")"});
            return false;
        }
        CheckTemplate(out, at, p, rule);
        if (folder && out.UsesToken(L"seq"))
            Add(L"HO-SPEC-012", at, p, {L"seq"}); // {seq} numbers files within a folder, so folders cannot use it
        // Only literal text can introduce separators; ':' inside {date:fmt} or {seq:00} is token syntax.
        std::wstring literal;
        for (const NameTemplate::Part& part : out.Parts)
            literal += part.Token ? std::wstring(L"x") : part.Text;
        if (folder)
        {
            // Folder templates use '/' between segments and must stay inside the package.
            std::wstring slashes = ToSlashes(literal);
            if (!slashes.empty() && (slashes[0] == L'/' || slashes.find(L':') != std::wstring::npos))
                Add(L"HO-SPEC-013", at, p, {text});
            else if (!slashes.empty())
            {
                for (const std::wstring& segment : Split(slashes, L'/'))
                {
                    if (segment.empty() || segment == L"." || segment == L"..")
                    {
                        Add(L"HO-SPEC-013", at, p, {text});
                        break;
                    }
                }
            }
        }
        else if (literal.find_first_of(L"/\\:") != std::wstring::npos)
            Add(L"HO-SPEC-004", at, p, {p, L"must produce a file name without folders"});
        return true;
    }

    void ReadFormats(const JsonValue& formats, const std::wstring& pointer)
    {
        for (const auto& member : formats.Members)
        {
            if (member.first == L"$comment")
                continue;
            std::wstring p = JsonPointerAppend(pointer, member.first);
            const JsonValue& value = member.second;
            if (!MatchesIdentifier(member.first, 32, false, false) || IsBuiltInFormat(member.first))
            {
                Add(L"HO-SPEC-014", value, p, {member.first});
                continue;
            }
            if (!value.IsObject())
            {
                TypeError(value, p, L"an object");
                continue;
            }
            CheckMembers(value, p, {L"extensions", L"magic", L"text", L"label"});
            FormatDef def;
            def.Id = member.first;
            def.BuiltIn = false;
            Str(value, L"label", p, def.Label, false, 64);
            Bool(value, L"text", p, def.Text);
            std::vector<std::wstring> extensions;
            StrArray(value, L"extensions", p, extensions, 16, true);
            for (const std::wstring& extension : extensions)
            {
                std::wstring ext = LowerInvariant(extension);
                if (ext.size() < 2 || ext[0] != L'.' || ext.find_first_of(L"/\\:*?") != std::wstring::npos ||
                    FormatForExtension(S.CustomFormats, ext) != nullptr)
                {
                    Add(L"HO-SPEC-014", *value.Find(L"extensions"), Ptr(p, L"extensions"), {member.first + L" " + extension});
                    continue;
                }
                def.Extensions.push_back(ext);
            }
            if (const JsonValue* magic = value.Find(L"magic"))
            {
                if (!magic->IsArray())
                    TypeError(*magic, Ptr(p, L"magic"), L"an array");
                else
                {
                    for (size_t i = 0; i < magic->Items.size() && i < 16; i++)
                    {
                        const JsonValue& entry = magic->Items[i];
                        std::wstring ep = Ptr(p, L"magic") + L"/" + NumberText((int64_t)i);
                        if (!entry.IsObject())
                        {
                            TypeError(entry, ep, L"an object");
                            continue;
                        }
                        CheckMembers(entry, ep, {L"offset", L"hex", L"ascii"});
                        MagicBytes bytes;
                        int64_t offset = 0;
                        Int(entry, L"offset", ep, offset, 0, (int64_t)FormatHeadBytes - 1);
                        bytes.Offset = (size_t)offset;
                        std::wstring hex, ascii;
                        bool hasHex = Str(entry, L"hex", ep, hex, false, 512);
                        bool hasAscii = Str(entry, L"ascii", ep, ascii, false, 256);
                        if (hasHex == hasAscii)
                        {
                            Add(L"HO-SPEC-004", entry, ep, {ep, L"use exactly one of \"hex\" or \"ascii\""});
                            continue;
                        }
                        if (hasHex)
                        {
                            bool ok = hex.size() % 2 == 0 && !hex.empty();
                            for (size_t c = 0; ok && c < hex.size(); c += 2)
                            {
                                wchar_t pair[3] = {hex[c], hex[c + 1], 0};
                                if (!iswxdigit(pair[0]) || !iswxdigit(pair[1]))
                                    ok = false;
                                else
                                    bytes.Bytes.push_back((unsigned char)wcstoul(pair, nullptr, 16));
                            }
                            if (!ok)
                            {
                                Add(L"HO-SPEC-004", entry, Ptr(ep, L"hex"), {Ptr(ep, L"hex"), L"expected pairs of hexadecimal digits"});
                                continue;
                            }
                        }
                        else
                        {
                            std::string utf8 = WideToUtf8(ascii);
                            bytes.Bytes.assign(utf8.begin(), utf8.end());
                        }
                        if (bytes.Offset + bytes.Bytes.size() > FormatHeadBytes)
                        {
                            Add(L"HO-SPEC-004", entry, ep, {ep, L"signature must lie within the first 4096 bytes"});
                            continue;
                        }
                        def.Magic.push_back(bytes);
                    }
                }
            }
            S.CustomFormats.push_back(def);
        }
    }

    void ReadVariables(const JsonValue& variables, const std::wstring& pointer)
    {
        for (const auto& member : variables.Members)
        {
            if (member.first == L"$comment")
                continue;
            std::wstring p = JsonPointerAppend(pointer, member.first);
            if (!IsVariableName(member.first) || IsReservedVariableName(member.first))
            {
                Add(L"HO-SPEC-004", member.second, p, {member.first, L"use a name of 1-32 letters and digits that is not reserved"});
                continue;
            }
            if (!member.second.IsObject())
            {
                TypeError(member.second, p, L"an object");
                continue;
            }
            const JsonValue& value = member.second;
            CheckMembers(value, p, {L"label", L"required", L"default", L"pattern", L"choices"});
            VariableDef def;
            def.Name = member.first;
            def.Label = member.first;
            Str(value, L"label", p, def.Label, false, 128);
            Bool(value, L"required", p, def.Required);
            Str(value, L"default", p, def.Default, false, 128);
            Regex(value, L"pattern", p, def.Pattern);
            StrArray(value, L"choices", p, def.Choices, 64);
            S.Variables.push_back(def);
        }
    }

    void ReadNaming(const JsonValue& naming, const std::wstring& p)
    {
        CheckMembers(naming, p, {L"charset", L"allowSpaces", L"case", L"maxNameLength", L"transliterate", L"replacement",
                                 L"sourceNameRegex"});
        std::wstring charset;
        if (Enum(naming, L"charset", p, charset, {L"portable", L"unicode"}))
            S.Naming.Portable = charset == L"portable";
        Bool(naming, L"allowSpaces", p, S.Naming.AllowSpaces);
        Enum(naming, L"case", p, S.Naming.CaseMode, {L"keep", L"lower", L"upper"});
        int64_t length;
        if (Int(naming, L"maxNameLength", p, length, 8, 255))
            S.Naming.MaxNameLength = (int)length;
        Bool(naming, L"transliterate", p, S.Naming.Transliterate);
        std::wstring replacement;
        if (Str(naming, L"replacement", p, replacement))
        {
            if (replacement.size() != 1 || (replacement[0] != L'_' && replacement[0] != L'-'))
                Add(L"HO-SPEC-004", *naming.Find(L"replacement"), Ptr(p, L"replacement"), {Ptr(p, L"replacement"), L"must be \"_\" or \"-\""});
            else
                S.Naming.Replacement = replacement[0];
        }
        Regex(naming, L"sourceNameRegex", p, S.Naming.SourceNameRegex);
    }

    void ReadPackage(const JsonValue& package, const std::wstring& p)
    {
        CheckMembers(package, p, {L"folderName", L"outputs", L"contents", L"limits", L"preserveModifiedTime",
                                  L"buildRecord", L"manifest"});
        Template(package, L"folderName", p, S.Package.FolderName, nullptr, true, false);
        if (const JsonValue* outputs = Obj(package, L"outputs", p))
        {
            std::wstring op = Ptr(p, L"outputs");
            CheckMembers(*outputs, op, {L"manifestJson", L"manifestCsv", L"contents"});
            if (const JsonValue* json = outputs->Find(L"manifestJson"))
            {
                if (json->IsNull())
                    Add(L"HO-SPEC-015", *json, Ptr(op, L"manifestJson"), {L"null"}); // verification needs it
                else
                    Str(*outputs, L"manifestJson", op, S.Package.ManifestJson, true, 128);
            }
            for (auto entry : {std::make_pair(L"manifestCsv", &S.Package.ManifestCsv),
                               std::make_pair(L"contents", &S.Package.Contents)})
            {
                const JsonValue* value = outputs->Find(entry.first);
                if (value != nullptr && value->IsNull())
                    entry.second->clear();
                else
                    Str(*outputs, entry.first, op, *entry.second, false, 128);
            }
        }
        if (const JsonValue* contents = Obj(package, L"contents", p))
        {
            std::wstring cp = Ptr(p, L"contents");
            CheckMembers(*contents, cp, {L"title", L"groupBy", L"notes"});
            Template(*contents, L"title", cp, S.Package.ContentsTitle, nullptr, false, false);
            std::wstring groupBy;
            if (Enum(*contents, L"groupBy", cp, groupBy, {L"folder", L"rule"}))
                S.Package.GroupByRule = groupBy == L"rule";
            std::vector<std::wstring> notes;
            if (StrArray(*contents, L"notes", cp, notes, 32))
            {
                for (size_t i = 0; i < notes.size(); i++)
                {
                    NameTemplate note;
                    std::wstring error;
                    const JsonValue& at = contents->Find(L"notes")->Items[i];
                    std::wstring np = Ptr(cp, L"notes") + L"/" + NumberText((int64_t)i);
                    if (!note.Parse(notes[i], error))
                        Add(L"HO-SPEC-012", at, np, {notes[i] + L" (" + error + L")"});
                    else
                    {
                        CheckTemplate(note, at, np, nullptr);
                        S.Package.Notes.push_back(note);
                    }
                }
            }
        }
        if (S.Package.ContentsTitle.IsEmpty())
        {
            std::wstring error;
            S.Package.ContentsTitle.Parse(L"{specName}", error);
        }
        if (const JsonValue* limits = Obj(package, L"limits", p))
        {
            std::wstring lp = Ptr(p, L"limits");
            CheckMembers(*limits, lp, {L"maxFiles", L"maxTotalBytes", L"maxRelativePathLength"});
            Int(*limits, L"maxFiles", lp, S.Package.MaxFiles, 1, 1000000);
            S.Package.HasMaxTotalBytes = Size(*limits, L"maxTotalBytes", lp, S.Package.MaxTotalBytes);
            Int(*limits, L"maxRelativePathLength", lp, S.Package.MaxRelativePathLength, 20, 32767);
        }
        Bool(package, L"preserveModifiedTime", p, S.Package.PreserveModifiedTime);
        std::wstring buildRecord;
        if (Enum(package, L"buildRecord", p, buildRecord, {L"beside", L"none"}))
            S.Package.BuildRecordBeside = buildRecord == L"beside";
        if (const JsonValue* manifest = Obj(package, L"manifest", p))
        {
            CheckMembers(*manifest, Ptr(p, L"manifest"), {L"includeSourcePaths"});
            Bool(*manifest, L"includeSourcePaths", Ptr(p, L"manifest"), S.Package.IncludeSourcePaths);
        }
    }

    void CheckOutputs(const JsonValue* package)
    {
        // Outputs are plain file names at the package root and must not collide (C.4.4).
        JsonValue fallback;
        const JsonValue* outputs = package != nullptr ? package->Find(L"outputs") : nullptr;
        const JsonValue& at = outputs != nullptr ? *outputs : (package != nullptr ? *package : fallback);
        std::vector<std::wstring> names;
        for (const std::wstring* name : {&S.Package.ManifestJson, &S.Package.ManifestCsv, &S.Package.Contents})
        {
            if (name->empty())
                continue;
            bool valid = !HasInvalidWindowsNameChars(*name) && !IsReservedDeviceName(*name) && Trim(*name) == *name &&
                         name->back() != L'.';
            for (const std::wstring& other : names)
                if (EqualsNoCase(other, *name))
                    valid = false;
            if (!valid)
                Add(L"HO-SPEC-015", at, L"/package/outputs", {*name});
            names.push_back(*name);
        }
    }

    void ReadContent(const JsonValue& content, const std::wstring& p)
    {
        CheckMembers(content, p, {L"allowedFormats", L"forbid", L"hidden", L"system", L"emptyFiles", L"maxFileSize",
                                  L"duplicates", L"unassigned"});
        if (StrArray(content, L"allowedFormats", p, S.Content.AllowedFormats, 128))
        {
            S.Content.HasAllowedFormats = true;
            CheckFormatIds(S.Content.AllowedFormats, *content.Find(L"allowedFormats"), Ptr(p, L"allowedFormats"));
        }
        if (content.Find(L"forbid") != nullptr)
        {
            // A specified list replaces the default (C.4.11).
            S.Content.Forbid.clear();
            Globs(content, L"forbid", p, S.Content.Forbid, false);
        }
        std::wstring value;
        if (Enum(content, L"hidden", p, value, {L"forbid", L"allow"}))
            S.Content.HiddenAllowed = value == L"allow";
        if (Enum(content, L"system", p, value, {L"forbid", L"allow"}))
            S.Content.SystemAllowed = value == L"allow";
        Enum(content, L"emptyFiles", p, S.Content.EmptyFiles, {L"forbid", L"warn", L"allow"});
        S.Content.HasMaxFileSize = Size(content, L"maxFileSize", p, S.Content.MaxFileSize);
        Enum(content, L"duplicates", p, S.Content.Duplicates, {L"warn", L"forbid", L"allow"});
        Enum(content, L"unassigned", p, S.Content.Unassigned, {L"info", L"warn", L"off"});
    }

    void CheckFormatIds(const std::vector<std::wstring>& ids, const JsonValue& at, const std::wstring& pointer)
    {
        for (const std::wstring& id : ids)
            if (FindFormat(S.CustomFormats, id) == nullptr)
                Add(L"HO-SPEC-014", at, pointer, {id});
    }

    void ReadSeverityMap(const JsonValue& object, const std::wstring& p, SeverityMap& out)
    {
        for (const auto& member : object.Members)
        {
            if (member.first == L"$comment")
                continue;
            std::wstring mp = JsonPointerAppend(p, member.first);
            const CodeInfo* info = FindCode(member.first);
            if (info == nullptr)
            {
                Add(L"HO-SPEC-004", member.second, mp, {member.first, L"unknown finding code"});
                continue;
            }
            if (info->Locked)
            {
                Add(L"HO-SPEC-017", member.second, mp, {member.first});
                continue;
            }
            Severity severity;
            if (!member.second.IsString() || !ParseSeverityName(member.second.String, severity))
            {
                Add(L"HO-SPEC-004", member.second, mp, {member.first, L"must be one of: error, warning, info, off"});
                continue;
            }
            out[member.first] = severity;
        }
    }

    void ReadRegister(const JsonValue& parent, const std::wstring& p, RegisterCsvSpec& out, Names columns,
                      Names required)
    {
        const JsonValue* reg = Obj(parent, L"registerCsv", p);
        if (reg == nullptr)
            return;
        std::wstring rp = Ptr(p, L"registerCsv");
        std::vector<std::wstring> known = {L"path"};
        for (const wchar_t* column : columns)
            known.push_back(column);
        CheckMembers(*reg, rp, known);
        out.Present = true;
        if (Str(*reg, L"path", rp, out.Path, true, 1024))
        {
            out.Path = ToSlashes(out.Path);
            if (!IsSafeRelativePath(out.Path))
                Add(L"HO-SPEC-013", *reg->Find(L"path"), Ptr(rp, L"path"), {out.Path});
        }
        for (const wchar_t* name : required)
            if (reg->Find(name) == nullptr)
                Add(L"HO-SPEC-003", *reg, Ptr(rp, name), {name});
        Str(*reg, L"column", rp, out.Column, false, 128);
        Str(*reg, L"fileColumn", rp, out.FileColumn, false, 128);
        Str(*reg, L"statusColumn", rp, out.StatusColumn, false, 128);
        Str(*reg, L"licenceColumn", rp, out.LicenceColumn, false, 128);
        Str(*reg, L"licensorColumn", rp, out.LicensorColumn, false, 128);
        Str(*reg, L"expiresColumn", rp, out.ExpiresColumn, false, 128);
        Str(*reg, L"scopeColumn", rp, out.ScopeColumn, false, 128);
        StrArray(*reg, L"approvedValues", rp, out.ApprovedValues, 32);
        StrArray(*reg, L"allowedScopes", rp, out.AllowedScopes, 32);
        if (const JsonValue* where = Obj(*reg, L"where", rp))
        {
            CheckMembers(*where, Ptr(rp, L"where"), {L"column", L"equals"});
            Str(*where, L"column", Ptr(rp, L"where"), out.WhereColumn, true, 128);
            Str(*where, L"equals", Ptr(rp, L"where"), out.WhereEquals, false, 256);
        }
    }

    void CheckSidecars(const std::vector<std::wstring>& patterns, const JsonValue& at, const std::wstring& pointer)
    {
        for (const std::wstring& pattern : patterns)
        {
            std::wstring stripped = ReplaceAll(ReplaceAll(pattern, L"{stem}", L"x"), L"{name}", L"x");
            if (stripped.find(L'{') != std::wstring::npos || stripped.find(L'}') != std::wstring::npos)
                Add(L"HO-SPEC-012", at, pointer, {pattern});
            else if (!IsSafeRelativePath(stripped))
                Add(L"HO-SPEC-013", at, pointer, {pattern});
        }
    }

    void ReadRules(const JsonValue& root)
    {
        const JsonValue* rules = root.Find(L"rules");
        if (rules == nullptr)
        {
            Add(L"HO-SPEC-003", root, L"/rules", {L"rules"});
            return;
        }
        if (!rules->IsArray())
        {
            TypeError(*rules, L"/rules", L"an array of rules");
            return;
        }
        if (rules->Items.empty())
            Add(L"HO-SPEC-004", *rules, L"/rules", {L"/rules", L"must contain at least one rule"});
        if (rules->Items.size() > 256)
        {
            Add(L"HO-SPEC-006", *rules, L"/rules", {L"more than 256 rules"});
            return;
        }
        for (size_t i = 0; i < rules->Items.size(); i++)
        {
            const JsonValue& value = rules->Items[i];
            std::wstring p = L"/rules/" + NumberText((int64_t)i);
            if (!value.IsObject())
            {
                TypeError(value, p, L"an object");
                continue;
            }
            S.Rules.emplace_back();
            ReadRule(value, p, S.Rules.back());
            for (size_t j = 0; j + 1 < S.Rules.size(); j++)
                if (!S.Rules.back().Id.empty() && S.Rules[j].Id == S.Rules.back().Id)
                    Add(L"HO-SPEC-007", *value.Find(L"id"), Ptr(p, L"id"), {L"rule id \"" + S.Rules.back().Id + L"\""});
        }
    }

    void ReadRule(const JsonValue& value, const std::wstring& p, RuleSpec& rule)
    {
        CheckMembers(value, p, {L"id", L"title", L"description", L"role", L"required", L"count", L"select", L"expect",
                                L"formats", L"fileSize", L"pdf", L"image", L"approval", L"license", L"target",
                                L"severity"});
        if (Str(value, L"id", p, rule.Id, true, 48) && !MatchesIdentifier(rule.Id, 48, false, false))
            Add(L"HO-SPEC-004", *value.Find(L"id"), Ptr(p, L"id"), {Ptr(p, L"id"), L"use 1-48 lower-case letters, digits, '_' or '-'"});
        rule.Title = rule.Id;
        Str(value, L"title", p, rule.Title, false, 128);
        Str(value, L"description", p, rule.Description, false, 1024);
        Enum(value, L"role", p, rule.Role, {L"deliverable", L"source", L"asset", L"document", L"supporting"});
        Bool(value, L"required", p, rule.Required);
        Range(value, L"count", p, rule.Count, 0, 1000000);
        if (!rule.Count.HasMin)
        {
            rule.Count.HasMin = true;
            rule.Count.Min = rule.Required ? 1 : 0;
        }

        const JsonValue* select = Obj(value, L"select", p);
        if (select == nullptr)
        {
            if (value.Find(L"select") == nullptr)
                Add(L"HO-SPEC-003", value, Ptr(p, L"select"), {L"select"});
        }
        else
            ReadSelect(*select, Ptr(p, L"select"), rule.Select);

        if (const JsonValue* expect = Obj(value, L"expect", p))
        {
            std::wstring ep = Ptr(p, L"expect");
            CheckMembers(*expect, ep, {L"keyRegex", L"keys", L"registerCsv", L"allowUnexpected"});
            rule.Expect.Present = true;
            Regex(*expect, L"keyRegex", ep, rule.Expect.KeyRegex, 1, true);
            StrArray(*expect, L"keys", ep, rule.Expect.Keys, 1024);
            ReadRegister(*expect, ep, rule.Expect.Register, {L"column", L"where"}, {L"column"});
            Bool(*expect, L"allowUnexpected", ep, rule.Expect.AllowUnexpected);
        }

        bool hasFormats = StrArray(value, L"formats", p, rule.Formats, 64);
        if (hasFormats)
            CheckFormatIds(rule.Formats, *value.Find(L"formats"), Ptr(p, L"formats"));

        if (const JsonValue* fileSize = Obj(value, L"fileSize", p))
        {
            std::wstring fp = Ptr(p, L"fileSize");
            CheckMembers(*fileSize, fp, {L"min", L"max"});
            rule.HasFileSizeMin = Size(*fileSize, L"min", fp, rule.FileSizeMin);
            rule.HasFileSizeMax = Size(*fileSize, L"max", fp, rule.FileSizeMax);
        }

        if (const JsonValue* pdf = Obj(value, L"pdf", p))
        {
            ReadPdf(*pdf, Ptr(p, L"pdf"), rule.Pdf);
            if (hasFormats && std::find(rule.Formats.begin(), rule.Formats.end(), L"pdf") == rule.Formats.end())
                Add(L"HO-SPEC-016", *pdf, Ptr(p, L"pdf"), {L"pdf", rule.Id});
        }
        if (const JsonValue* image = Obj(value, L"image", p))
        {
            ReadImage(*image, Ptr(p, L"image"), rule.Image);
            bool anyRaster = false;
            for (const std::wstring& id : rule.Formats)
                anyRaster = anyRaster || IsRasterFormat(id);
            if (hasFormats && !anyRaster)
                Add(L"HO-SPEC-016", *image, Ptr(p, L"image"), {L"image", rule.Id});
        }

        if (const JsonValue* approval = Obj(value, L"approval", p))
        {
            std::wstring ap = Ptr(p, L"approval");
            CheckMembers(*approval, ap, {L"mode", L"evidence"});
            Enum(*approval, L"mode", ap, rule.Approval.Mode, {L"none", L"evidence", L"confirm", L"evidenceAndConfirm"});
            if (const JsonValue* evidence = Obj(*approval, L"evidence", ap))
            {
                std::wstring evp = Ptr(ap, L"evidence");
                CheckMembers(*evidence, evp, {L"pathRegex", L"nameRegex", L"sidecar", L"registerCsv"});
                Regex(*evidence, L"pathRegex", evp, rule.Approval.PathRegex);
                Regex(*evidence, L"nameRegex", evp, rule.Approval.NameRegex);
                if (StrArray(*evidence, L"sidecar", evp, rule.Approval.Sidecar, 16))
                    CheckSidecars(rule.Approval.Sidecar, *evidence->Find(L"sidecar"), Ptr(evp, L"sidecar"));
                ReadRegister(*evidence, evp, rule.Approval.Register, {L"fileColumn", L"statusColumn", L"approvedValues"},
                             {L"fileColumn", L"statusColumn", L"approvedValues"});
            }
            if (rule.Approval.NeedsEvidence() && !rule.Approval.PathRegex.IsSet() && !rule.Approval.NameRegex.IsSet() &&
                rule.Approval.Sidecar.empty() && !rule.Approval.Register.Present)
                Add(L"HO-SPEC-004", *approval, ap, {ap, L"evidence modes need at least one evidence kind"});
        }

        if (const JsonValue* license = Obj(value, L"license", p))
        {
            std::wstring lp = Ptr(p, L"license");
            CheckMembers(*license, lp, {L"required", L"sidecar", L"registerCsv", L"includeEvidence", L"evidenceFolder",
                                        L"expiryWarningDays"});
            rule.License.Present = true;
            Bool(*license, L"required", lp, rule.License.Required);
            if (StrArray(*license, L"sidecar", lp, rule.License.Sidecar, 16))
                CheckSidecars(rule.License.Sidecar, *license->Find(L"sidecar"), Ptr(lp, L"sidecar"));
            ReadRegister(*license, lp, rule.License.Register,
                         {L"fileColumn", L"licenceColumn", L"licensorColumn", L"expiresColumn", L"scopeColumn", L"allowedScopes"},
                         {L"fileColumn"});
            Bool(*license, L"includeEvidence", lp, rule.License.IncludeEvidence);
            rule.License.HasEvidenceFolder =
                Template(*license, L"evidenceFolder", lp, rule.License.EvidenceFolder, nullptr, false, true);
            int64_t days;
            if (Int(*license, L"expiryWarningDays", lp, days, 0, 3650))
                rule.License.ExpiryWarningDays = (int)days;
            if (rule.License.Required && rule.License.Sidecar.empty() && !rule.License.Register.Present)
                Add(L"HO-SPEC-004", *license, lp, {lp, L"a required licence needs \"sidecar\" or \"registerCsv\""});
        }

        std::wstring error;
        rule.Target.Name.Parse(L"{name}", error);
        if (const JsonValue* target = Obj(value, L"target", p))
        {
            std::wstring tp = Ptr(p, L"target");
            CheckMembers(*target, tp, {L"folder", L"name", L"keepSubfolders"});
            Template(*target, L"folder", tp, rule.Target.Folder, &rule, false, true);
            Template(*target, L"name", tp, rule.Target.Name, &rule, false, false);
            Bool(*target, L"keepSubfolders", tp, rule.Target.KeepSubfolders);
        }

        if (const JsonValue* severity = Obj(value, L"severity", p))
            ReadSeverityMap(*severity, Ptr(p, L"severity"), rule.Severity);
    }

    void ReadSelect(const JsonValue& select, const std::wstring& p, SelectSpec& out)
    {
        CheckMembers(select, p, {L"include", L"exclude", L"nameRegex", L"pathRegex", L"shared", L"latestRevision"});
        Globs(select, L"include", p, out.Include, true);
        Globs(select, L"exclude", p, out.Exclude, false);
        Regex(select, L"nameRegex", p, out.NameRegex);
        Regex(select, L"pathRegex", p, out.PathRegex);
        Bool(select, L"shared", p, out.Shared);
        if (const JsonValue* latest = Obj(select, L"latestRevision", p))
        {
            std::wstring lp = Ptr(p, L"latestRevision");
            CheckMembers(*latest, lp, {L"regex", L"scope", L"prefixOrder"});
            out.Latest.Present = Regex(*latest, L"regex", lp, out.Latest.Regex, 1, true);
            std::wstring scope;
            if (Enum(*latest, L"scope", lp, scope, {L"name", L"folder"}))
                out.Latest.PerFolder = scope == L"folder";
            if (StrArray(*latest, L"prefixOrder", lp, out.Latest.PrefixOrder, 26))
            {
                for (const std::wstring& prefix : out.Latest.PrefixOrder)
                {
                    bool letters = !prefix.empty();
                    for (wchar_t c : prefix)
                        letters = letters && iswalpha(c);
                    if (!letters)
                        Add(L"HO-SPEC-004", *latest->Find(L"prefixOrder"), Ptr(lp, L"prefixOrder"),
                            {Ptr(lp, L"prefixOrder"), L"prefixes are letters only"});
                }
            }
        }
    }

    void ReadPdf(const JsonValue& pdf, const std::wstring& p, PdfSpec& out)
    {
        CheckMembers(pdf, p, {L"version", L"pages", L"pageSizes", L"orientation", L"toleranceMm", L"allPagesSameSize",
                              L"allowPasswordProtected"});
        out.Present = true;
        if (const JsonValue* version = Obj(pdf, L"version", p))
        {
            std::wstring vp = Ptr(p, L"version");
            CheckMembers(*version, vp, {L"min", L"max"});
            for (auto entry : {std::make_pair(L"min", &out.VersionMin), std::make_pair(L"max", &out.VersionMax)})
            {
                if (Str(*version, entry.first, vp, *entry.second, false, 8))
                {
                    const std::wstring& text = *entry.second;
                    bool ok = text.size() == 3 && iswdigit(text[0]) && text[1] == L'.' && iswdigit(text[2]) &&
                              text >= L"1.0" && text <= L"2.0";
                    if (!ok)
                    {
                        Add(L"HO-SPEC-004", *version->Find(entry.first), Ptr(vp, entry.first), {Ptr(vp, entry.first), L"use \"1.0\" to \"2.0\""});
                        entry.second->clear();
                    }
                }
            }
        }
        Range(pdf, L"pages", p, out.Pages, 0, 100000);
        if (const JsonValue* sizes = pdf.Find(L"pageSizes"))
        {
            if (!sizes->IsArray())
                TypeError(*sizes, Ptr(p, L"pageSizes"), L"an array");
            else
            {
                for (size_t i = 0; i < sizes->Items.size() && i < 64; i++)
                {
                    const JsonValue& item = sizes->Items[i];
                    std::wstring ip = Ptr(p, L"pageSizes") + L"/" + NumberText((int64_t)i);
                    PageSizeSpec size;
                    if (item.IsString())
                    {
                        if (!LookupPageSize(item.String, size.WidthMm, size.HeightMm))
                        {
                            Add(L"HO-SPEC-018", item, ip, {item.String});
                            continue;
                        }
                        size.Name = item.String;
                    }
                    else if (item.IsObject())
                    {
                        CheckMembers(item, ip, {L"widthMm", L"heightMm"});
                        if (!Num(item, L"widthMm", ip, size.WidthMm, 1, 5000, true) ||
                            !Num(item, L"heightMm", ip, size.HeightMm, 1, 5000, true))
                            continue;
                    }
                    else
                    {
                        TypeError(item, ip, L"a size name or {\"widthMm\", \"heightMm\"}");
                        continue;
                    }
                    out.PageSizes.push_back(size);
                }
            }
        }
        Enum(pdf, L"orientation", p, out.Orientation, {L"any", L"portrait", L"landscape"});
        Num(pdf, L"toleranceMm", p, out.ToleranceMm, 0, 20);
        Bool(pdf, L"allPagesSameSize", p, out.AllPagesSameSize);
        Bool(pdf, L"allowPasswordProtected", p, out.AllowPasswordProtected);
    }

    void ReadImage(const JsonValue& image, const std::wstring& p, ImageSpec& out)
    {
        CheckMembers(image, p, {L"width", L"height", L"longEdge", L"shortEdge", L"exact", L"aspectRatio", L"dpi",
                                L"colorModel", L"alpha", L"bitDepth", L"frames", L"forbidGps"});
        out.Present = true;
        Range(image, L"width", p, out.Width, 1, 1000000);
        Range(image, L"height", p, out.Height, 1, 1000000);
        Range(image, L"longEdge", p, out.LongEdge, 1, 1000000);
        Range(image, L"shortEdge", p, out.ShortEdge, 1, 1000000);
        Range(image, L"bitDepth", p, out.BitDepth, 1, 64);
        Range(image, L"frames", p, out.Frames, 1, 100000);
        if (const JsonValue* exact = image.Find(L"exact"))
        {
            if (!exact->IsArray())
                TypeError(*exact, Ptr(p, L"exact"), L"an array");
            else
            {
                for (size_t i = 0; i < exact->Items.size() && i < 64; i++)
                {
                    const JsonValue& item = exact->Items[i];
                    std::wstring ip = Ptr(p, L"exact") + L"/" + NumberText((int64_t)i);
                    if (!item.IsObject())
                    {
                        TypeError(item, ip, L"an object");
                        continue;
                    }
                    CheckMembers(item, ip, {L"width", L"height"});
                    int64_t w = 0, h = 0;
                    if (Int(item, L"width", ip, w, 1, 1000000, true) && Int(item, L"height", ip, h, 1, 1000000, true))
                        out.Exact.push_back(std::make_pair(w, h));
                }
            }
        }
        if (const JsonValue* aspect = Obj(image, L"aspectRatio", p))
        {
            std::wstring ap = Ptr(p, L"aspectRatio");
            CheckMembers(*aspect, ap, {L"value", L"tolerance"});
            const JsonValue* value = aspect->Find(L"value");
            double ratio = 0;
            if (value == nullptr)
                Add(L"HO-SPEC-003", *aspect, Ptr(ap, L"value"), {L"value"});
            else if (value->IsString())
            {
                std::vector<std::wstring> parts = Split(value->String, L':');
                double w = parts.size() == 2 ? wcstod(parts[0].c_str(), nullptr) : 0;
                double h = parts.size() == 2 ? wcstod(parts[1].c_str(), nullptr) : 0;
                if (w > 0 && h > 0)
                    ratio = w / h;
                out.AspectText = value->String;
            }
            else
            {
                value->AsDouble(ratio);
                out.AspectText = value->String;
            }
            if (value != nullptr && !(ratio > 0))
                Add(L"HO-SPEC-004", *value, Ptr(ap, L"value"), {Ptr(ap, L"value"), L"use \"W:H\" or a positive number"});
            else if (value != nullptr)
            {
                out.HasAspect = true;
                out.Aspect = ratio;
            }
            Num(*aspect, L"tolerance", ap, out.AspectTolerance, 0, 1);
        }
        if (const JsonValue* dpi = Obj(image, L"dpi", p))
        {
            CheckMembers(*dpi, Ptr(p, L"dpi"), {L"min"});
            out.HasDpiMin = Num(*dpi, L"min", Ptr(p, L"dpi"), out.DpiMin, 1, 100000);
        }
        if (StrArray(image, L"colorModel", p, out.ColorModels, 8))
        {
            for (const std::wstring& model : out.ColorModels)
            {
                if (model != L"rgb" && model != L"cmyk" && model != L"gray" && model != L"indexed" && model != L"lab")
                    Add(L"HO-SPEC-004", *image.Find(L"colorModel"), Ptr(p, L"colorModel"),
                        {Ptr(p, L"colorModel"), L"must be one of: rgb, cmyk, gray, indexed, lab"});
            }
        }
        Enum(image, L"alpha", p, out.Alpha, {L"any", L"required", L"forbidden"});
        Bool(image, L"forbidGps", p, out.ForbidGps);
    }
};

} // namespace

SpecLoadResult ParseSpecification(const unsigned char* data, size_t length)
{
    return ParseSpecification(data, length, DefaultCatalog());
}

SpecLoadResult ParseSpecification(const unsigned char* data, size_t length, const ITextCatalog& catalog)
{
    SpecLoadResult result;
    // Default forbidden content (C.4.11); a specified list replaces it.
    for (const wchar_t* pattern : {L"**/Thumbs.db", L"**/.DS_Store", L"**/desktop.ini", L"**/~$*", L"**/*.tmp",
                                   L"**/*.bak", L"**/__MACOSX/**", L"**/.git/**"})
    {
        Glob glob;
        std::wstring error;
        glob.Compile(pattern, error);
        result.Model.Content.Forbid.push_back(glob);
    }
    JsonValue root;
    JsonError error;
    if (!ParseJson(data, length, SpecJsonLimits(), root, error))
    {
        Finding finding = MakeFinding(error.Code.c_str(), L"", {error.Message});
        finding.Line = error.Line;
        finding.Column = error.Column;
        finding.Pointer = error.Pointer.empty() ? L"/" : error.Pointer;
        result.Findings.push_back(finding);
        return result;
    }
    SpecReader reader(result.Model, result.Findings, catalog);
    reader.Read(root);
    result.Ok = true;
    for (const Finding& finding : result.Findings)
        if (finding.Sev == Severity::Error)
            result.Ok = false;
    return result;
}

} // namespace handoff
