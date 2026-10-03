// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Bounded, strict RFC 8259 reader and a deterministic writer. Specifications,
// registers, and manifests are shared between organizations and therefore
// untrusted: every size, depth, and count is limited, and every value keeps
// its line/column and JSON Pointer so validation can point at the exact spot.

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace handoff
{

enum class JsonType
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

struct JsonValue
{
    // Kept compact because a document may hold many values: number lexemes share
    // 'String', and JSON Pointers are rebuilt by consumers as they descend.
    JsonType Type = JsonType::Null;
    bool Boolean = false;
    int Line = 0;
    int Column = 0;
    std::wstring String; // string value, or the number lexeme
    std::vector<JsonValue> Items;
    std::vector<std::pair<std::wstring, JsonValue>> Members;

    const JsonValue* Find(const wchar_t* name) const;
    bool IsObject() const { return Type == JsonType::Object; }
    bool IsArray() const { return Type == JsonType::Array; }
    bool IsString() const { return Type == JsonType::String; }
    bool IsNumber() const { return Type == JsonType::Number; }
    bool IsBool() const { return Type == JsonType::Bool; }
    bool IsNull() const { return Type == JsonType::Null; }
    // Integers must be exact (no fraction/exponent) and within +/-2^53.
    bool AsInteger(int64_t& value) const;
    bool AsDouble(double& value) const;
};

struct JsonLimits
{
    size_t MaxBytes;
    int MaxDepth;
    size_t MaxString;  // UTF-16 units
    size_t MaxMembers; // per object
    size_t MaxItems;   // per array
    size_t MaxNumber;  // lexeme length
    size_t MaxValues;  // values per document; bounds memory independently of nesting
};

JsonLimits SpecJsonLimits();     // C.4.1 "Specification" column
JsonLimits ManifestJsonLimits(); // C.4.1 "Manifest" column

struct JsonError
{
    std::wstring Code; // HO-SPEC-001 syntax/encoding, HO-SPEC-006 limit, HO-SPEC-007 duplicate
    std::wstring Message;
    int Line = 0;
    int Column = 0;
    std::wstring Pointer;
};

// Parses one complete document. A leading UTF-8 BOM is skipped.
bool ParseJson(const unsigned char* data, size_t length, const JsonLimits& limits, JsonValue& root, JsonError& error);

std::wstring JsonPointerAppend(const std::wstring& base, const std::wstring& key);

// Pretty printer with two-space indentation and LF line endings; members are
// written in call order so outputs are byte-for-byte reproducible (C.7).
class JsonWriter
{
public:
    void BeginObject();
    void EndObject();
    void BeginArray();
    void EndArray();
    void Key(const std::wstring& name);
    void String(const std::wstring& value);
    void Integer(int64_t value);
    void Unsigned(uint64_t value);
    void Decimal(double value, int decimals);
    void Bool(bool value);
    void Null();
    // Convenience members.
    void Member(const std::wstring& name, const std::wstring& value) { Key(name), String(value); }
    void MemberInt(const std::wstring& name, int64_t value) { Key(name), Integer(value); }
    void MemberUInt(const std::wstring& name, uint64_t value) { Key(name), Unsigned(value); }
    void MemberBool(const std::wstring& name, bool value) { Key(name), Bool(value); }
    const std::string& Text();

private:
    struct Frame
    {
        bool Array;
        bool Empty;
    };
    void BeforeValue();
    void NewLine();
    void Raw(const std::string& text) { Out += text; }

    std::string Out;
    std::vector<Frame> Stack;
    bool AfterKey = false;
    bool Finished = false;
};

std::string JsonEscape(const std::wstring& value);

} // namespace handoff
