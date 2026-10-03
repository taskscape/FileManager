// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Pattern matching used by specifications: bounded regular expressions
// (C.4.6.2) and path globs (C.4.6.1).

#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace handoff
{

enum class RegexOutcome
{
    NoMatch,
    Match,
    TooLong,   // subject over 1 024 UTF-16 units (HO-SEL-007)
    TooComplex // the STL aborted backtracking (HO-SEL-008)
};

class SafeRegex
{
public:
    static const size_t MaxPatternLength = 512;
    static const size_t MaxSubjectLength = 1024;

    // Compiles once at validation; 'error' receives a readable reason.
    bool Compile(const std::wstring& pattern, std::wstring& error);
    bool IsSet() const { return Regex != nullptr; }
    const std::wstring& Pattern() const { return Source; }
    int GroupCount() const { return Groups; }

    RegexOutcome FullMatch(const std::wstring& subject) const;
    // Search; on Match, 'group1' (when requested) receives capture group 1 and
    // 'position'/'length' the extent of the whole match.
    RegexOutcome Search(const std::wstring& subject, std::wstring* group1 = nullptr, size_t* position = nullptr,
                        size_t* length = nullptr) const;

private:
    std::shared_ptr<std::wregex> Regex;
    std::wstring Source;
    int Groups = 0;
};

class Glob
{
public:
    // Compiles a glob; returns false with 'error' for malformed patterns.
    bool Compile(const std::wstring& pattern, std::wstring& error);
    // 'relativePath' uses '/' separators and is relative to the working root.
    bool Matches(const std::wstring& relativePath) const;
    const std::wstring& Pattern() const { return Source; }
    // Leading segments without wildcards ("Artwork/Final" for "Artwork/Final/**/*.ai").
    const std::wstring& StaticPrefix() const { return Prefix; }

private:
    struct Alternative
    {
        std::vector<std::wstring> Segments; // folded; "**" kept literally
    };
    std::vector<Alternative> Alternatives;
    std::wstring Source;
    std::wstring Prefix;

    static bool MatchSegment(const std::wstring& pattern, const std::wstring& text);
    static bool MatchSegments(const std::vector<std::wstring>& pattern, const std::vector<std::wstring>& path);
};

// Strips 'StaticPrefix' (and its separator) from a relative path; empty when not below it.
std::wstring RelativeBelowPrefix(const std::wstring& relativePath, const std::wstring& prefix);

} // namespace handoff
