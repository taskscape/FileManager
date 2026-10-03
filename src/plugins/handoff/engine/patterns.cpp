// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "patterns.h"
#include "text_util.h"

namespace handoff
{

// ---------------------------------------------------------------------------
// SafeRegex

bool SafeRegex::Compile(const std::wstring& pattern, std::wstring& error)
{
    Regex.reset();
    Source = pattern;
    Groups = 0;
    if (pattern.size() > MaxPatternLength)
    {
        error = L"the pattern is longer than " + NumberText((int64_t)MaxPatternLength) + L" characters";
        return false;
    }
    try
    {
        // ECMAScript, always case-insensitive (C.4.6.2). Construction can throw
        // regex_error for malformed input, which becomes a validation finding.
        auto regex = std::make_shared<std::wregex>(pattern, std::regex_constants::ECMAScript |
                                                                std::regex_constants::icase |
                                                                std::regex_constants::optimize);
        Groups = (int)regex->mark_count();
        Regex = regex;
        return true;
    }
    catch (const std::regex_error& e)
    {
        error = Utf8ToWideLossy(e.what());
        return false;
    }
    catch (const std::exception&)
    {
        error = L"the pattern could not be compiled";
        return false;
    }
}

RegexOutcome SafeRegex::FullMatch(const std::wstring& subject) const
{
    if (Regex == nullptr)
        return RegexOutcome::Match;
    if (subject.size() > MaxSubjectLength)
        return RegexOutcome::TooLong;
    try
    {
        // The VS 2026 STL bounds backtracking (complexity and frame limits) and
        // throws instead of hanging; older STLs honour _REGEX_MAX_* limits.
        return std::regex_match(subject, *Regex) ? RegexOutcome::Match : RegexOutcome::NoMatch;
    }
    catch (const std::exception&)
    {
        return RegexOutcome::TooComplex;
    }
}

RegexOutcome SafeRegex::Search(const std::wstring& subject, std::wstring* group1, size_t* position,
                               size_t* length) const
{
    if (Regex == nullptr)
        return RegexOutcome::NoMatch;
    if (subject.size() > MaxSubjectLength)
        return RegexOutcome::TooLong;
    try
    {
        std::wsmatch match;
        if (!std::regex_search(subject, match, *Regex))
            return RegexOutcome::NoMatch;
        if (group1 != nullptr)
            *group1 = match.size() > 1 && match[1].matched ? match[1].str() : std::wstring();
        if (position != nullptr)
            *position = (size_t)match.position(0);
        if (length != nullptr)
            *length = (size_t)match.length(0);
        return RegexOutcome::Match;
    }
    catch (const std::exception&)
    {
        return RegexOutcome::TooComplex;
    }
}

// ---------------------------------------------------------------------------
// Glob

static bool HasWildcard(const std::wstring& segment)
{
    return segment.find_first_of(L"*?[{") != std::wstring::npos;
}

// Expands non-nested {a,b} groups; the total number of alternatives is bounded.
static bool ExpandBraces(const std::wstring& pattern, std::vector<std::wstring>& out, std::wstring& error)
{
    out.assign(1, std::wstring());
    for (size_t i = 0; i < pattern.size(); i++)
    {
        wchar_t c = pattern[i];
        if (c == L'}')
        {
            error = L"unbalanced '}'";
            return false;
        }
        if (c != L'{')
        {
            for (std::wstring& alternative : out)
                alternative += c;
            continue;
        }
        size_t close = pattern.find(L'}', i + 1);
        if (close == std::wstring::npos)
        {
            error = L"unbalanced '{'";
            return false;
        }
        std::wstring body = pattern.substr(i + 1, close - i - 1);
        if (body.find(L'{') != std::wstring::npos)
        {
            error = L"nested '{' groups are not supported";
            return false;
        }
        std::vector<std::wstring> choices = Split(body, L',');
        std::vector<std::wstring> expanded;
        for (const std::wstring& prefix : out)
            for (const std::wstring& choice : choices)
                expanded.push_back(prefix + choice);
        if (expanded.size() > 16)
        {
            error = L"more than 16 '{...}' alternatives";
            return false;
        }
        out.swap(expanded);
        i = close;
    }
    return true;
}

static bool ValidateClasses(const std::wstring& segment, std::wstring& error)
{
    for (size_t i = 0; i < segment.size(); i++)
    {
        if (segment[i] != L'[')
            continue;
        size_t close = segment.find(L']', i + 1);
        if (close == std::wstring::npos || close == i + 1 || (close == i + 2 && segment[i + 1] == L'!'))
        {
            error = L"malformed '[...]' class";
            return false;
        }
        i = close;
    }
    return true;
}

bool Glob::Compile(const std::wstring& pattern, std::wstring& error)
{
    Alternatives.clear();
    Source = ToSlashes(pattern);
    Prefix.clear();
    if (Source.empty())
    {
        error = L"empty pattern";
        return false;
    }
    if (Source[0] == L'/' || Source.find(L':') != std::wstring::npos)
    {
        error = L"the pattern must be relative";
        return false;
    }
    std::vector<std::wstring> sourceSegments = Split(Source, L'/');
    for (const std::wstring& segment : sourceSegments)
    {
        if (segment.empty() || segment == L"." || segment == L"..")
        {
            error = L"empty, '.' or '..' path segment";
            return false;
        }
    }
    // The static prefix covers directory segments only; the last segment names files.
    std::vector<std::wstring> prefix;
    for (size_t i = 0; i + 1 < sourceSegments.size() && !HasWildcard(sourceSegments[i]); i++)
        prefix.push_back(sourceSegments[i]);
    Prefix = Join(prefix, L"/");

    std::vector<std::wstring> expanded;
    if (!ExpandBraces(Source, expanded, error))
        return false;
    for (const std::wstring& alternative : expanded)
    {
        Alternative compiled;
        for (const std::wstring& segment : Split(alternative, L'/'))
        {
            if (segment.empty())
            {
                error = L"empty path segment";
                return false;
            }
            if (!ValidateClasses(segment, error))
                return false;
            compiled.Segments.push_back(segment == L"**" ? segment : FoldKey(segment));
        }
        Alternatives.push_back(compiled);
    }
    return true;
}

bool Glob::MatchSegment(const std::wstring& pattern, const std::wstring& text)
{
    // Iterative wildcard matching with a single backtrack point for '*':
    // linear in practice, quadratic at worst, never recursive.
    size_t p = 0, t = 0, starP = std::wstring::npos, starT = 0;
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == L'*')
        {
            while (p < pattern.size() && pattern[p] == L'*')
                p++;
            starP = p;
            starT = t;
            continue;
        }
        bool matched = false;
        size_t next = p + 1;
        if (p < pattern.size())
        {
            wchar_t pc = pattern[p];
            if (pc == L'?')
                matched = true;
            else if (pc == L'[')
            {
                size_t close = pattern.find(L']', p + 1);
                size_t i = p + 1;
                bool negate = i < close && pattern[i] == L'!';
                if (negate)
                    i++;
                bool inClass = false;
                for (; i < close; i++)
                {
                    if (i + 2 < close && pattern[i + 1] == L'-')
                    {
                        if (text[t] >= pattern[i] && text[t] <= pattern[i + 2])
                            inClass = true;
                        i += 2;
                    }
                    else if (text[t] == pattern[i])
                        inClass = true;
                }
                matched = inClass != negate;
                next = close + 1;
            }
            else
                matched = pc == text[t];
        }
        if (matched)
        {
            p = next;
            t++;
            continue;
        }
        if (starP == std::wstring::npos)
            return false;
        p = starP;
        t = ++starT;
    }
    while (p < pattern.size() && pattern[p] == L'*')
        p++;
    return p == pattern.size();
}

bool Glob::MatchSegments(const std::vector<std::wstring>& pattern, const std::vector<std::wstring>& path)
{
    // reachable[j]: pattern prefix matched so far can align with path[j:].
    const size_t m = path.size();
    std::vector<char> reachable(m + 1, 0), next(m + 1, 0);
    reachable[0] = 1;
    for (const std::wstring& segment : pattern)
    {
        std::fill(next.begin(), next.end(), (char)0);
        if (segment == L"**")
        {
            // "**" matches zero or more whole segments.
            bool any = false;
            for (size_t j = 0; j <= m; j++)
            {
                any = any || reachable[j];
                next[j] = any;
            }
        }
        else
        {
            for (size_t j = 0; j < m; j++)
                if (reachable[j] && MatchSegment(segment, path[j]))
                    next[j + 1] = 1;
        }
        reachable.swap(next);
    }
    return reachable[m] != 0;
}

bool Glob::Matches(const std::wstring& relativePath) const
{
    std::vector<std::wstring> path = Split(FoldKey(ToSlashes(relativePath)), L'/');
    for (const Alternative& alternative : Alternatives)
        if (MatchSegments(alternative.Segments, path))
            return true;
    return false;
}

std::wstring RelativeBelowPrefix(const std::wstring& relativePath, const std::wstring& prefix)
{
    std::wstring dir = ParentOf(ToSlashes(relativePath));
    if (prefix.empty())
        return dir;
    if (dir.size() < prefix.size() || !EqualsNoCase(dir.substr(0, prefix.size()), prefix))
        return std::wstring();
    if (dir.size() == prefix.size())
        return std::wstring();
    if (dir[prefix.size()] != L'/')
        return std::wstring();
    return dir.substr(prefix.size() + 1);
}

} // namespace handoff
