// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "json.h"
#include "text_util.h"

#include <stdlib.h>
#include <math.h>

namespace handoff
{

JsonLimits SpecJsonLimits()
{
    // C.4.1: specifications are small, hand-written documents.
    return JsonLimits{1024 * 1024, 32, 4096, 256, 1024, 32, 65536};
}

JsonLimits ManifestJsonLimits()
{
    // Manifests list every packaged file, so arrays are large but shallow.
    return JsonLimits{32 * 1024 * 1024, 16, 32768, 64, 200000, 32, 2000000};
}

const JsonValue* JsonValue::Find(const wchar_t* name) const
{
    if (Type != JsonType::Object)
        return nullptr;
    for (const auto& member : Members)
        if (member.first == name)
            return &member.second;
    return nullptr;
}

bool JsonValue::AsInteger(int64_t& value) const
{
    if (Type != JsonType::Number)
        return false;
    if (String.find_first_of(L".eE") != std::wstring::npos)
        return false;
    errno = 0;
    long long parsed = _wcstoi64(String.c_str(), nullptr, 10);
    if (errno != 0)
        return false;
    const long long limit = 9007199254740992LL; // 2^53, exactly representable everywhere
    if (parsed > limit || parsed < -limit)
        return false;
    value = parsed;
    return true;
}

bool JsonValue::AsDouble(double& value) const
{
    if (Type != JsonType::Number)
        return false;
    value = wcstod(String.c_str(), nullptr);
    return isfinite(value) != 0;
}

std::wstring JsonPointerAppend(const std::wstring& base, const std::wstring& key)
{
    std::wstring escaped = ReplaceAll(ReplaceAll(key, L"~", L"~0"), L"/", L"~1");
    return base + L"/" + escaped;
}

namespace
{

class Parser
{
public:
    Parser(const unsigned char* data, size_t length, const JsonLimits& limits, JsonError& error)
        : Data(data), Length(length), Limits(limits), Error(error)
    {
    }

    bool Run(JsonValue& root)
    {
        if (Length >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
            Pos = 3; // a UTF-8 BOM is tolerated, and only at the very start
        SkipWhitespace();
        if (!ParseValue(root, 0, L""))
            return false;
        SkipWhitespace();
        if (Pos != Length)
            return Fail(L"HO-SPEC-001", L"unexpected content after the end of the document", L"");
        return true;
    }

private:
    const unsigned char* Data;
    size_t Length;
    const JsonLimits& Limits;
    JsonError& Error;
    size_t Pos = 0;
    int Line = 1;
    int Column = 1;
    size_t Values = 0;

    bool Fail(const wchar_t* code, const std::wstring& message, const std::wstring& pointer)
    {
        Error.Code = code;
        Error.Message = message;
        Error.Line = Line;
        Error.Column = Column;
        Error.Pointer = pointer;
        return false;
    }

    void Advance()
    {
        unsigned char c = Data[Pos++];
        if (c == '\n')
        {
            Line++;
            Column = 1;
        }
        else if ((c & 0xC0) != 0x80) // columns count Unicode scalar values, not bytes
            Column++;
    }

    void SkipWhitespace()
    {
        while (Pos < Length && (Data[Pos] == ' ' || Data[Pos] == '\t' || Data[Pos] == '\n' || Data[Pos] == '\r'))
            Advance();
    }

    bool Literal(const char* text, JsonValue& value, JsonType type, bool boolean)
    {
        size_t n = strlen(text);
        if (Length - Pos < n || memcmp(Data + Pos, text, n) != 0)
            return Fail(L"HO-SPEC-001", L"invalid literal", L"");
        for (size_t i = 0; i < n; i++)
            Advance();
        value.Type = type;
        value.Boolean = boolean;
        return true;
    }

    bool ParseValue(JsonValue& value, int depth, const std::wstring& pointer)
    {
        if (++Values > Limits.MaxValues)
            return Fail(L"HO-SPEC-006", L"too many values in the document", pointer);
        if (Pos >= Length)
            return Fail(L"HO-SPEC-001", L"unexpected end of the document", pointer);
        value.Line = Line;
        value.Column = Column;
        unsigned char c = Data[Pos];
        switch (c)
        {
        case '{':
            return ParseObject(value, depth, pointer);
        case '[':
            return ParseArray(value, depth, pointer);
        case '"':
            value.Type = JsonType::String;
            return ParseString(value.String, pointer);
        case 't':
            return Literal("true", value, JsonType::Bool, true);
        case 'f':
            return Literal("false", value, JsonType::Bool, false);
        case 'n':
            return Literal("null", value, JsonType::Null, false);
        default:
            if (c == '-' || (c >= '0' && c <= '9'))
                return ParseNumber(value, pointer);
            if (c >= 0x80)
                return Fail(L"HO-SPEC-001", L"non-ASCII character outside a string", pointer);
            return Fail(L"HO-SPEC-001", std::wstring(L"unexpected character '") + (wchar_t)c + L"'", pointer);
        }
    }

    bool ParseObject(JsonValue& value, int depth, const std::wstring& pointer)
    {
        // Nesting is bounded before descending, so input cannot exhaust the stack.
        if (depth + 1 > Limits.MaxDepth)
            return Fail(L"HO-SPEC-006", L"nesting deeper than " + NumberText(Limits.MaxDepth) + L" levels", pointer);
        value.Type = JsonType::Object;
        Advance(); // '{'
        SkipWhitespace();
        if (Pos < Length && Data[Pos] == '}')
        {
            Advance();
            return true;
        }
        for (;;)
        {
            SkipWhitespace();
            if (Pos >= Length || Data[Pos] != '"')
                return Fail(L"HO-SPEC-001", L"expected a member name in double quotes", pointer);
            int keyLine = Line, keyColumn = Column;
            std::wstring key;
            if (!ParseString(key, pointer))
                return false;
            std::wstring memberPointer = JsonPointerAppend(pointer, key);
            for (const auto& existing : value.Members)
            {
                if (existing.first == key)
                {
                    Line = keyLine, Column = keyColumn;
                    return Fail(L"HO-SPEC-007", L"member \"" + key + L"\"", memberPointer);
                }
            }
            if (value.Members.size() >= Limits.MaxMembers)
                return Fail(L"HO-SPEC-006", L"more than " + NumberText((int64_t)Limits.MaxMembers) + L" members", pointer);
            SkipWhitespace();
            if (Pos >= Length || Data[Pos] != ':')
                return Fail(L"HO-SPEC-001", L"expected ':' after a member name", memberPointer);
            Advance();
            SkipWhitespace();
            value.Members.emplace_back(key, JsonValue());
            if (!ParseValue(value.Members.back().second, depth + 1, memberPointer))
                return false;
            SkipWhitespace();
            if (Pos >= Length)
                return Fail(L"HO-SPEC-001", L"unterminated object", pointer);
            if (Data[Pos] == ',')
            {
                Advance();
                SkipWhitespace();
                if (Pos < Length && Data[Pos] == '}')
                    return Fail(L"HO-SPEC-001", L"trailing comma before '}'", pointer);
                continue;
            }
            if (Data[Pos] == '}')
            {
                Advance();
                return true;
            }
            return Fail(L"HO-SPEC-001", L"expected ',' or '}'", pointer);
        }
    }

    bool ParseArray(JsonValue& value, int depth, const std::wstring& pointer)
    {
        if (depth + 1 > Limits.MaxDepth)
            return Fail(L"HO-SPEC-006", L"nesting deeper than " + NumberText(Limits.MaxDepth) + L" levels", pointer);
        value.Type = JsonType::Array;
        Advance(); // '['
        SkipWhitespace();
        if (Pos < Length && Data[Pos] == ']')
        {
            Advance();
            return true;
        }
        for (;;)
        {
            SkipWhitespace();
            if (value.Items.size() >= Limits.MaxItems)
                return Fail(L"HO-SPEC-006", L"more than " + NumberText((int64_t)Limits.MaxItems) + L" array elements", pointer);
            std::wstring itemPointer = pointer + L"/" + NumberText((int64_t)value.Items.size());
            value.Items.emplace_back();
            if (!ParseValue(value.Items.back(), depth + 1, itemPointer))
                return false;
            SkipWhitespace();
            if (Pos >= Length)
                return Fail(L"HO-SPEC-001", L"unterminated array", pointer);
            if (Data[Pos] == ',')
            {
                Advance();
                SkipWhitespace();
                if (Pos < Length && Data[Pos] == ']')
                    return Fail(L"HO-SPEC-001", L"trailing comma before ']'", pointer);
                continue;
            }
            if (Data[Pos] == ']')
            {
                Advance();
                return true;
            }
            return Fail(L"HO-SPEC-001", L"expected ',' or ']'", pointer);
        }
    }

    bool ParseNumber(JsonValue& value, const std::wstring& pointer)
    {
        size_t start = Pos;
        auto digit = [&](size_t p) { return p < Length && Data[p] >= '0' && Data[p] <= '9'; };
        size_t p = Pos;
        if (p < Length && Data[p] == '-')
            p++;
        if (!digit(p))
            return Fail(L"HO-SPEC-001", L"invalid number", pointer);
        if (Data[p] == '0')
            p++;
        else
            while (digit(p))
                p++;
        if (p < Length && Data[p] == '.')
        {
            p++;
            if (!digit(p))
                return Fail(L"HO-SPEC-001", L"invalid number fraction", pointer);
            while (digit(p))
                p++;
        }
        if (p < Length && (Data[p] == 'e' || Data[p] == 'E'))
        {
            p++;
            if (p < Length && (Data[p] == '+' || Data[p] == '-'))
                p++;
            if (!digit(p))
                return Fail(L"HO-SPEC-001", L"invalid number exponent", pointer);
            while (digit(p))
                p++;
        }
        if (p - start > Limits.MaxNumber)
            return Fail(L"HO-SPEC-006", L"number longer than " + NumberText((int64_t)Limits.MaxNumber) + L" characters", pointer);
        value.Type = JsonType::Number;
        value.String.assign(Data + start, Data + p); // ASCII only by construction
        while (Pos < p)
            Advance();
        return true;
    }

    static bool HexDigit(unsigned char c, unsigned& out)
    {
        if (c >= '0' && c <= '9')
            out = c - '0';
        else if (c >= 'a' && c <= 'f')
            out = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            out = c - 'A' + 10;
        else
            return false;
        return true;
    }

    bool ReadHex4(unsigned& code, const std::wstring& pointer)
    {
        if (Length - Pos < 4)
            return Fail(L"HO-SPEC-001", L"truncated \\u escape", pointer);
        code = 0;
        for (int i = 0; i < 4; i++)
        {
            unsigned d;
            if (!HexDigit(Data[Pos], d))
                return Fail(L"HO-SPEC-001", L"invalid \\u escape", pointer);
            code = code * 16 + d;
            Advance();
        }
        return true;
    }

    bool Append(std::wstring& out, unsigned codePoint, const std::wstring& pointer)
    {
        if (codePoint >= 0x10000)
        {
            codePoint -= 0x10000;
            out += (wchar_t)(0xD800 + (codePoint >> 10));
            out += (wchar_t)(0xDC00 + (codePoint & 0x3FF));
        }
        else
            out += (wchar_t)codePoint;
        if (out.size() > Limits.MaxString)
            return Fail(L"HO-SPEC-006", L"string longer than " + NumberText((int64_t)Limits.MaxString) + L" characters", pointer);
        return true;
    }

    bool ParseString(std::wstring& out, const std::wstring& pointer)
    {
        Advance(); // opening quote
        out.clear();
        for (;;)
        {
            if (Pos >= Length)
                return Fail(L"HO-SPEC-001", L"unterminated string", pointer);
            unsigned char c = Data[Pos];
            if (c == '"')
            {
                Advance();
                return true;
            }
            if (c < 0x20)
                return Fail(L"HO-SPEC-001", L"control character in a string", pointer);
            if (c == '\\')
            {
                Advance();
                if (Pos >= Length)
                    return Fail(L"HO-SPEC-001", L"unterminated escape", pointer);
                unsigned char e = Data[Pos];
                unsigned code = 0;
                switch (e)
                {
                case '"': code = '"'; break;
                case '\\': code = '\\'; break;
                case '/': code = '/'; break;
                case 'b': code = 8; break;
                case 'f': code = 12; break;
                case 'n': code = 10; break;
                case 'r': code = 13; break;
                case 't': code = 9; break;
                case 'u':
                {
                    Advance();
                    if (!ReadHex4(code, pointer))
                        return false;
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        // A high surrogate must be followed by an escaped low surrogate.
                        if (Length - Pos < 2 || Data[Pos] != '\\' || Data[Pos + 1] != 'u')
                            return Fail(L"HO-SPEC-001", L"unpaired surrogate in \\u escape", pointer);
                        Advance();
                        Advance();
                        unsigned low;
                        if (!ReadHex4(low, pointer))
                            return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return Fail(L"HO-SPEC-001", L"unpaired surrogate in \\u escape", pointer);
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                        return Fail(L"HO-SPEC-001", L"unpaired surrogate in \\u escape", pointer);
                    if (code == 0)
                        return Fail(L"HO-SPEC-001", L"\\u0000 is not allowed", pointer);
                    if (!Append(out, code, pointer))
                        return false;
                    continue;
                }
                default:
                    return Fail(L"HO-SPEC-001", L"invalid escape sequence", pointer);
                }
                Advance();
                if (!Append(out, code, pointer))
                    return false;
                continue;
            }
            if (c < 0x80)
            {
                Advance();
                if (!Append(out, c, pointer))
                    return false;
                continue;
            }
            // Manual UTF-8 decoding rejects overlong forms, surrogates, and values above U+10FFFF.
            unsigned codePoint;
            int extra;
            unsigned minimum;
            if ((c & 0xE0) == 0xC0)
                codePoint = c & 0x1F, extra = 1, minimum = 0x80;
            else if ((c & 0xF0) == 0xE0)
                codePoint = c & 0x0F, extra = 2, minimum = 0x800;
            else if ((c & 0xF8) == 0xF0)
                codePoint = c & 0x07, extra = 3, minimum = 0x10000;
            else
                return Fail(L"HO-SPEC-001", L"invalid UTF-8", pointer);
            if (Length - Pos <= (size_t)extra)
                return Fail(L"HO-SPEC-001", L"invalid UTF-8", pointer);
            for (int i = 1; i <= extra; i++)
            {
                unsigned char cc = Data[Pos + i];
                if ((cc & 0xC0) != 0x80)
                    return Fail(L"HO-SPEC-001", L"invalid UTF-8", pointer);
                codePoint = (codePoint << 6) | (cc & 0x3F);
            }
            if (codePoint < minimum || codePoint > 0x10FFFF || (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                return Fail(L"HO-SPEC-001", L"invalid UTF-8", pointer);
            for (int i = 0; i <= extra; i++)
                Advance();
            if (!Append(out, codePoint, pointer))
                return false;
        }
    }
};

} // namespace

bool ParseJson(const unsigned char* data, size_t length, const JsonLimits& limits, JsonValue& root, JsonError& error)
{
    root = JsonValue();
    error = JsonError();
    if (length > limits.MaxBytes)
    {
        error.Code = L"HO-SPEC-006";
        error.Message = L"file larger than " + FormatSize(limits.MaxBytes);
        error.Line = 1;
        error.Column = 1;
        return false;
    }
    Parser parser(data, length, limits, error);
    if (!parser.Run(root))
    {
        root = JsonValue();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writer

std::string JsonEscape(const std::wstring& value)
{
    std::string utf8 = WideToUtf8(value);
    std::string out;
    out.reserve(utf8.size() + 2);
    out += '"';
    for (unsigned char c : utf8)
    {
        switch (c)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20)
            {
                static const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 15];
            }
            else
                out += (char)c;
        }
    }
    out += '"';
    return out;
}

void JsonWriter::NewLine()
{
    Out += '\n';
    Out.append(Stack.size() * 2, ' ');
}

void JsonWriter::BeforeValue()
{
    if (AfterKey)
    {
        AfterKey = false;
        return;
    }
    if (!Stack.empty())
    {
        if (!Stack.back().Empty)
            Out += ',';
        Stack.back().Empty = false;
        NewLine();
    }
}

void JsonWriter::BeginObject()
{
    BeforeValue();
    Out += '{';
    Stack.push_back(Frame{false, true});
}

void JsonWriter::EndObject()
{
    bool empty = Stack.back().Empty;
    Stack.pop_back();
    if (!empty)
        NewLine();
    Out += '}';
}

void JsonWriter::BeginArray()
{
    BeforeValue();
    Out += '[';
    Stack.push_back(Frame{true, true});
}

void JsonWriter::EndArray()
{
    bool empty = Stack.back().Empty;
    Stack.pop_back();
    if (!empty)
        NewLine();
    Out += ']';
}

void JsonWriter::Key(const std::wstring& name)
{
    BeforeValue();
    Out += JsonEscape(name);
    Out += ": ";
    AfterKey = true;
}

void JsonWriter::String(const std::wstring& value)
{
    BeforeValue();
    Out += JsonEscape(value);
}

void JsonWriter::Integer(int64_t value)
{
    BeforeValue();
    Out += std::to_string((long long)value);
}

void JsonWriter::Unsigned(uint64_t value)
{
    BeforeValue();
    Out += std::to_string((unsigned long long)value);
}

void JsonWriter::Decimal(double value, int decimals)
{
    BeforeValue();
    Out += WideToUtf8(DecimalText(value, decimals));
}

void JsonWriter::Bool(bool value)
{
    BeforeValue();
    Out += value ? "true" : "false";
}

void JsonWriter::Null()
{
    BeforeValue();
    Out += "null";
}

const std::string& JsonWriter::Text()
{
    if (!Finished)
    {
        Out += '\n';
        Finished = true;
    }
    return Out;
}

} // namespace handoff
