// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "json.h"

namespace reorg
{
namespace
{

const size_t kMaxDocument = 256u * 1024u * 1024u;
const size_t kMaxString = 65536;
const int kMaxDepth = 32;

struct CParser
{
    const std::string& Text;
    size_t Index;
    int Line;
    int Column;
    std::string Error;
    int ErrorLine;
    int ErrorColumn;

    CParser(const std::string& text) : Text(text), Index(0), Line(1), Column(1), ErrorLine(1), ErrorColumn(1) {}

    bool Fail(const char* message)
    {
        Error = message;
        ErrorLine = Line;
        ErrorColumn = Column;
        return false;
    }

    void Advance()
    {
        if (Index >= Text.size())
            return;
        if (Text[Index] == '\n')
        {
            ++Line;
            Column = 1;
        }
        else
            ++Column;
        ++Index;
    }

    void SkipWs()
    {
        while (Index < Text.size())
        {
            char ch = Text[Index];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
                break;
            Advance();
        }
    }

    bool ParseValue(CJsonValue& out, int depth);

    bool ParseString(std::string& out)
    {
        if (Index >= Text.size() || Text[Index] != '"')
            return Fail("expected string");
        Advance();
        out.clear();
        while (Index < Text.size())
        {
            unsigned char ch = (unsigned char)Text[Index];
            if (ch == '"')
            {
                Advance();
                return true;
            }
            if (ch == '\\')
            {
                Advance();
                if (Index >= Text.size())
                    return Fail("unterminated escape");
                char esc = Text[Index];
                Advance();
                char produced = 0;
                if (esc == '"' || esc == '\\' || esc == '/')
                    produced = esc;
                else if (esc == 'b')
                    produced = '\b';
                else if (esc == 'f')
                    produced = '\f';
                else if (esc == 'n')
                    produced = '\n';
                else if (esc == 'r')
                    produced = '\r';
                else if (esc == 't')
                    produced = '\t';
                else if (esc == 'u')
                {
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (Index >= Text.size())
                            return Fail("short unicode escape");
                        char h = Text[Index];
                        Advance();
                        code <<= 4;
                        if (h >= '0' && h <= '9')
                            code += (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            code += (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            code += (unsigned)(h - 'A' + 10);
                        else
                            return Fail("bad unicode escape");
                    }
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        if (Index + 1 >= Text.size() || Text[Index] != '\\' || Text[Index + 1] != 'u')
                            return Fail("lone surrogate");
                        Advance();
                        Advance();
                        unsigned low = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            if (Index >= Text.size())
                                return Fail("short unicode escape");
                            char h = Text[Index];
                            Advance();
                            low <<= 4;
                            if (h >= '0' && h <= '9')
                                low += (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f')
                                low += (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F')
                                low += (unsigned)(h - 'A' + 10);
                            else
                                return Fail("bad unicode escape");
                        }
                        if (low < 0xDC00 || low > 0xDFFF)
                            return Fail("lone surrogate");
                        code = 0x10000 + (((code - 0xD800) << 10) | (low - 0xDC00));
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                        return Fail("lone surrogate");
                    wchar_t wide[2];
                    int wideCount = 1;
                    if (code < 0x10000)
                        wide[0] = (wchar_t)code;
                    else
                    {
                        code -= 0x10000;
                        wide[0] = (wchar_t)(0xD800 + (code >> 10));
                        wide[1] = (wchar_t)(0xDC00 + (code & 0x3FF));
                        wideCount = 2;
                    }
                    std::string utf8 = WideToUtf8(std::wstring(wide, wide + wideCount));
                    if (out.size() + utf8.size() > kMaxString)
                        return Fail("string too long");
                    out += utf8;
                    continue;
                }
                else
                    return Fail("bad escape");
                if (out.size() + 1 > kMaxString)
                    return Fail("string too long");
                out.push_back(produced);
                continue;
            }
            if (ch < 0x20)
                return Fail("raw control character in string");
            // UTF-8. Reject overlong sequences and encoded surrogate code points.
            int need = 1;
            unsigned codepoint = ch;
            if ((ch & 0x80) == 0)
                need = 1;
            else if ((ch & 0xE0) == 0xC0)
            {
                need = 2;
                codepoint = ch & 0x1F;
            }
            else if ((ch & 0xF0) == 0xE0)
            {
                need = 3;
                codepoint = ch & 0x0F;
            }
            else if ((ch & 0xF8) == 0xF0)
            {
                need = 4;
                codepoint = ch & 0x07;
            }
            else
                return Fail("invalid UTF-8");
            if (Index + (size_t)need > Text.size())
                return Fail("truncated UTF-8");
            std::string chunk;
            chunk.push_back((char)ch);
            Advance();
            for (int i = 1; i < need; ++i)
            {
                unsigned char cont = (unsigned char)Text[Index];
                if ((cont & 0xC0) != 0x80)
                    return Fail("invalid UTF-8");
                codepoint = (codepoint << 6) | (cont & 0x3F);
                chunk.push_back((char)cont);
                Advance();
            }
            if ((need == 2 && codepoint < 0x80) || (need == 3 && codepoint < 0x800) || (need == 4 && codepoint < 0x10000) ||
                codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
                return Fail("lone surrogate");
            if (out.size() + chunk.size() > kMaxString)
                return Fail("string too long");
            out += chunk;
        }
        return Fail("unterminated string");
    }
};

bool CParser::ParseValue(CJsonValue& out, int depth)
{
    SkipWs();
    if (Index >= Text.size())
        return Fail("unexpected end");
    if (depth > kMaxDepth)
        return Fail("nesting too deep");
    char ch = Text[Index];
    if (ch == '"')
    {
        std::string s;
        if (!ParseString(s))
            return false;
        out = CJsonValue::MakeString(s);
        return true;
    }
    if (ch == '{')
    {
        Advance();
        out.Kind = JsonObject;
        SkipWs();
        if (Index < Text.size() && Text[Index] == '}')
        {
            Advance();
            return true;
        }
        while (Index < Text.size())
        {
            SkipWs();
            std::string key;
            if (!ParseString(key))
                return false;
            if (out.Object.find(key) != out.Object.end())
                return Fail("duplicate key");
            SkipWs();
            if (Index >= Text.size() || Text[Index] != ':')
                return Fail("expected colon");
            Advance();
            CJsonValue child;
            if (!ParseValue(child, depth + 1))
                return false;
            out.Object[key] = child;
            SkipWs();
            if (Index < Text.size() && Text[Index] == ',')
            {
                Advance();
                continue;
            }
            if (Index < Text.size() && Text[Index] == '}')
            {
                Advance();
                return true;
            }
            return Fail("expected comma or end of object");
        }
        return Fail("unterminated object");
    }
    if (ch == '[')
    {
        Advance();
        out.Kind = JsonArray;
        SkipWs();
        if (Index < Text.size() && Text[Index] == ']')
        {
            Advance();
            return true;
        }
        while (Index < Text.size())
        {
            CJsonValue child;
            if (!ParseValue(child, depth + 1))
                return false;
            out.Array.push_back(child);
            SkipWs();
            if (Index < Text.size() && Text[Index] == ',')
            {
                Advance();
                continue;
            }
            if (Index < Text.size() && Text[Index] == ']')
            {
                Advance();
                return true;
            }
            return Fail("expected comma or end of array");
        }
        return Fail("unterminated array");
    }
    if (ch == 't' || ch == 'f' || ch == 'n')
    {
        const char* literal = ch == 't' ? "true" : (ch == 'f' ? "false" : "null");
        for (int i = 0; literal[i]; ++i)
        {
            if (Index >= Text.size() || Text[Index] != literal[i])
                return Fail("bad literal");
            Advance();
        }
        if (ch == 't')
            out = CJsonValue::MakeBool(true);
        else if (ch == 'f')
            out = CJsonValue::MakeBool(false);
        else
            out = CJsonValue::MakeNull();
        return true;
    }
    if (ch == '-' || (ch >= '0' && ch <= '9'))
    {
        size_t start = Index;
        if (ch == '-')
            Advance();
        if (Index >= Text.size() || Text[Index] < '0' || Text[Index] > '9')
            return Fail("bad number");
        if (Text[Index] == '0')
            Advance();
        else
        {
            while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
                Advance();
        }
        bool frac = false;
        if (Index < Text.size() && Text[Index] == '.')
        {
            frac = true;
            Advance();
            if (Index >= Text.size() || Text[Index] < '0' || Text[Index] > '9')
                return Fail("bad number");
            while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
                Advance();
        }
        if (Index < Text.size() && (Text[Index] == 'e' || Text[Index] == 'E'))
        {
            frac = true;
            Advance();
            if (Index < Text.size() && (Text[Index] == '+' || Text[Index] == '-'))
                Advance();
            if (Index >= Text.size() || Text[Index] < '0' || Text[Index] > '9')
                return Fail("bad number");
            while (Index < Text.size() && Text[Index] >= '0' && Text[Index] <= '9')
                Advance();
        }
        std::string token = Text.substr(start, Index - start);
        if (!frac)
        {
            bool neg = token[0] == '-';
            const char* digits = token.c_str() + (neg ? 1 : 0);
            unsigned __int64 mag = 0;
            for (const char* p = digits; *p; ++p)
            {
                unsigned __int64 next = mag * 10 + (unsigned)(*p - '0');
                if (next < mag)
                    return Fail("integer overflow");
                mag = next;
            }
            if (!neg && mag <= (unsigned __int64)INT64_MAX)
            {
                out = CJsonValue::MakeInt((__int64)mag);
                return true;
            }
            if (neg && mag <= (unsigned __int64)INT64_MAX + 1)
            {
                if (mag == (unsigned __int64)INT64_MAX + 1)
                    out = CJsonValue::MakeInt(INT64_MIN);
                else
                    out = CJsonValue::MakeInt(-(__int64)mag);
                return true;
            }
            return Fail("integer overflow");
        }
        out = CJsonValue::MakeDouble(strtod(token.c_str(), NULL));
        return true;
    }
    return Fail("unexpected token");
}

void WriteString(std::string& out, const std::string& value)
{
    out.push_back('"');
    const unsigned char* p = (const unsigned char*)value.data();
    size_t n = value.size();
    for (size_t i = 0; i < n;)
    {
        unsigned char ch = p[i];
        if (ch == '"' || ch == '\\')
        {
            out.push_back('\\');
            out.push_back((char)ch);
            ++i;
        }
        else if (ch == '\b')
        {
            out += "\\b";
            ++i;
        }
        else if (ch == '\f')
        {
            out += "\\f";
            ++i;
        }
        else if (ch == '\n')
        {
            out += "\\n";
            ++i;
        }
        else if (ch == '\r')
        {
            out += "\\r";
            ++i;
        }
        else if (ch == '\t')
        {
            out += "\\t";
            ++i;
        }
        else if (ch < 0x20)
        {
            char buf[8];
            _snprintf_s(buf, _countof(buf), _TRUNCATE, "\\u%04x", ch);
            out += buf;
            ++i;
        }
        else
        {
            out.push_back((char)ch);
            ++i;
        }
    }
    out.push_back('"');
}

void WriteValue(std::string& out, const CJsonValue& value, int indent)
{
    switch (value.Kind)
    {
    case JsonNull:
        out += "null";
        break;
    case JsonBool:
        out += value.Bool ? "true" : "false";
        break;
    case JsonInt:
    {
        char buf[32];
        _snprintf_s(buf, _countof(buf), _TRUNCATE, "%lld", value.Int);
        out += buf;
        break;
    }
    case JsonDouble:
    {
        char buf[64];
        _snprintf_s(buf, _countof(buf), _TRUNCATE, "%.17g", value.Double);
        out += buf;
        break;
    }
    case JsonString:
        WriteString(out, value.String);
        break;
    case JsonArray:
        if (value.Array.empty())
        {
            out += "[]";
            break;
        }
        out += "[\n";
        for (size_t i = 0; i < value.Array.size(); ++i)
        {
            out.append((size_t)(indent + 1) * 2, ' ');
            WriteValue(out, value.Array[i], indent + 1);
            if (i + 1 != value.Array.size())
                out.push_back(',');
            out.push_back('\n');
        }
        out.append((size_t)indent * 2, ' ');
        out.push_back(']');
        break;
    case JsonObject:
        if (value.Object.empty())
        {
            out += "{}";
            break;
        }
        out += "{\n";
        size_t index = 0;
        for (std::map<std::string, CJsonValue>::const_iterator it = value.Object.begin(); it != value.Object.end(); ++it, ++index)
        {
            out.append((size_t)(indent + 1) * 2, ' ');
            WriteString(out, it->first);
            out += ": ";
            WriteValue(out, it->second, indent + 1);
            if (index + 1 != value.Object.size())
                out.push_back(',');
            out.push_back('\n');
        }
        out.append((size_t)indent * 2, ' ');
        out.push_back('}');
        break;
    }
}

} // namespace

CJsonParseResult JsonParse(const std::string& utf8)
{
    CJsonParseResult result;
    std::string text = utf8;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    if (text.size() > kMaxDocument)
    {
        result.Error = "document too large";
        return result;
    }
    CParser parser(text);
    if (!parser.ParseValue(result.Value, 1))
    {
        result.Error = parser.Error;
        result.Line = parser.ErrorLine;
        result.Column = parser.ErrorColumn;
        return result;
    }
    parser.SkipWs();
    if (parser.Index != text.size())
    {
        result.Error = "trailing data";
        result.Line = parser.Line;
        result.Column = parser.Column;
        return result;
    }
    result.Ok = true;
    return result;
}

std::string JsonWrite(const CJsonValue& value)
{
    std::string out;
    WriteValue(out, value, 0);
    out.push_back('\n');
    return out;
}

} // namespace reorg
