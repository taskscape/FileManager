// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Bounded JSON. Limits are the plan-file contract: depth 32, strings of
// 65,536 UTF-8 bytes, documents of 256 MiB, and no duplicate keys.

#include "util.h"

#include <map>

namespace reorg
{

enum EJsonKind
{
    JsonNull = 0,
    JsonBool,
    JsonInt,
    JsonDouble,
    JsonString,
    JsonArray,
    JsonObject
};

struct CJsonValue
{
    EJsonKind Kind;
    bool Bool;
    __int64 Int;
    double Double;
    std::string String;
    std::vector<CJsonValue> Array;
    std::map<std::string, CJsonValue> Object;

    CJsonValue() : Kind(JsonNull), Bool(false), Int(0), Double(0) {}

    static CJsonValue MakeNull() { return CJsonValue(); }
    static CJsonValue MakeBool(bool value)
    {
        CJsonValue v;
        v.Kind = JsonBool;
        v.Bool = value;
        return v;
    }
    static CJsonValue MakeInt(__int64 value)
    {
        CJsonValue v;
        v.Kind = JsonInt;
        v.Int = value;
        return v;
    }
    static CJsonValue MakeDouble(double value)
    {
        CJsonValue v;
        v.Kind = JsonDouble;
        v.Double = value;
        return v;
    }
    static CJsonValue MakeString(const std::string& value)
    {
        CJsonValue v;
        v.Kind = JsonString;
        v.String = value;
        return v;
    }
    static CJsonValue MakeStringW(const std::wstring& value) { return MakeString(WideToUtf8(value)); }

    const CJsonValue* Find(const char* key) const
    {
        if (Kind != JsonObject)
            return NULL;
        std::map<std::string, CJsonValue>::const_iterator it = Object.find(key);
        return it == Object.end() ? NULL : &it->second;
    }
};

struct CJsonParseResult
{
    bool Ok;
    CJsonValue Value;
    int Line;
    int Column;
    std::string Error;

    CJsonParseResult() : Ok(false), Line(1), Column(1) {}
};

CJsonParseResult JsonParse(const std::string& utf8);
std::string JsonWrite(const CJsonValue& value);

} // namespace reorg
