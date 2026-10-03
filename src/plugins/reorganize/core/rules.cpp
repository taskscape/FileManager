// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "rules.h"

namespace reorg
{
namespace
{

const CRoot* FindScope(const CPlanDocument& plan, const std::wstring& idOrPath)
{
    for (size_t i = 0; i < plan.ScopeRoots.size(); ++i)
    {
        if (plan.ScopeRoots[i].Id == idOrPath || PathsEqual(plan.ScopeRoots[i].Path, idOrPath, false))
            return &plan.ScopeRoots[i];
    }
    return NULL;
}

const CRoot* FindDestLabel(const CPlanDocument& plan, const std::wstring& label)
{
    for (size_t i = 0; i < plan.DestinationRoots.size(); ++i)
    {
        if (NamesEqual(plan.DestinationRoots[i].Label, label, false))
            return &plan.DestinationRoots[i];
    }
    return NULL;
}

std::wstring RelativeToScope(const CSnapshotItem& item, const CRoot& scope)
{
    if (!IsUnderPath(item.Path, scope.Path, false))
        return item.ParentPath;
    if (PathsEqual(item.Path, scope.Path, false))
        return std::wstring();
    std::wstring prefix = scope.Path;
    if (!prefix.empty() && prefix.back() != L'\\')
        prefix.push_back(L'\\');
    std::wstring rest = item.Path.size() >= prefix.size() ? item.Path.substr(prefix.size()) : item.Path;
    return ParentPath(rest);
}

bool InRange(const std::wstring& iso, const std::wstring& from, const std::wstring& to)
{
    if (!from.empty() && iso < from)
        return false;
    if (!to.empty() && iso > to)
        return false;
    return true;
}

std::wstring FormatTimeToken(unsigned __int64 fileTime, const std::wstring& fmt)
{
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)fileTime;
    ft.dwHighDateTime = (DWORD)(fileTime >> 32);
    FILETIME local;
    FileTimeToLocalFileTime(&ft, &local);
    SYSTEMTIME st;
    FileTimeToSystemTime(&local, &st);
    if (fmt == L"yyyy")
    {
        wchar_t b[8];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"%04u", st.wYear);
        return b;
    }
    if (fmt == L"yy")
    {
        wchar_t b[8];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"%02u", st.wYear % 100);
        return b;
    }
    if (fmt == L"MM")
    {
        wchar_t b[8];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"%02u", st.wMonth);
        return b;
    }
    if (fmt == L"dd")
    {
        wchar_t b[8];
        _snwprintf_s(b, _countof(b), _TRUNCATE, L"%02u", st.wDay);
        return b;
    }
    if (fmt == L"Q")
        return std::wstring(1, (wchar_t)(L'0' + ((st.wMonth - 1) / 3 + 1)));
    return std::wstring();
}

} // namespace

bool MatchRule(const CRule& rule, const CSnapshotItem& item, const CPlanDocument& plan, const CSnapshot& snapshot)
{
    (void)snapshot;
    if (!rule.Enabled || !rule.TemplateError.empty())
        return false;
    if (!rule.Match.ScopeRoot.empty())
    {
        const CRoot* scope = FindScope(plan, rule.Match.ScopeRoot);
        if (scope == NULL || !IsUnderPath(item.Path, scope->Path, false))
            return false;
    }
    if (rule.Match.ItemType == ItemFile && item.IsDir)
        return false;
    if (rule.Match.ItemType == ItemDir && !item.IsDir)
        return false;
    if (!rule.Match.RelativePathGlob.empty())
    {
        const CRoot* scope = FindScope(plan, rule.Match.ScopeRoot.empty() && !plan.ScopeRoots.empty() ? plan.ScopeRoots[0].Id : rule.Match.ScopeRoot);
        std::wstring relative = item.Path;
        if (scope && IsUnderPath(item.Path, scope->Path, false))
        {
            std::wstring prefix = scope->Path;
            if (!prefix.empty() && prefix.back() != L'\\')
                prefix.push_back(L'\\');
            relative = item.Path.size() > prefix.size() ? item.Path.substr(prefix.size()) : std::wstring();
        }
        if (!MatchGlob(relative, rule.Match.RelativePathGlob))
            return false;
    }
    std::wstring ext;
    std::wstring stem = SplitName(item.Name, ext);
    bool hasExt = !ext.empty();
    if (!rule.Match.NameMask.empty() && !AgreeMaskWide(item.Name, rule.Match.NameMask, hasExt))
        return false;
    if (rule.Match.HasExtensions)
    {
        bool found = false;
        std::wstring extBare = ext.size() && ext[0] == L'.' ? ext.substr(1) : ext;
        for (size_t i = 0; i < rule.Match.Extensions.size(); ++i)
        {
            if (NamesEqual(extBare, rule.Match.Extensions[i], false))
                found = true;
        }
        if (!found)
            return false;
    }
    if (rule.Match.HasSizeMin && item.Size < rule.Match.SizeMin)
        return false;
    if (rule.Match.HasSizeMax && item.Size > rule.Match.SizeMax)
        return false;
    std::wstring modified = FileTimeToIso(item.LastWrite);
    std::wstring created = FileTimeToIso(item.Created);
    if ((!rule.Match.ModifiedFrom.empty() || !rule.Match.ModifiedTo.empty()) && !InRange(modified, rule.Match.ModifiedFrom, rule.Match.ModifiedTo))
        return false;
    if ((!rule.Match.CreatedFrom.empty() || !rule.Match.CreatedTo.empty()) && !InRange(created, rule.Match.CreatedFrom, rule.Match.CreatedTo))
        return false;
    if ((item.Attributes & rule.Match.AttributesSet) != rule.Match.AttributesSet)
        return false;
    if ((item.Attributes & rule.Match.AttributesClear) != 0)
        return false;
    (void)stem;
    return true;
}

bool ExpandTemplate(const std::wstring& templ, const CSnapshotItem& item, const CPlanDocument& plan, const CSnapshot& snapshot,
                    std::wstring& destinationDir, std::wstring& newName, std::wstring& error)
{
    (void)snapshot;
    error.clear();
    std::wstring built;
    for (size_t i = 0; i < templ.size();)
    {
        if (templ[i] == L'{' && i + 1 < templ.size() && templ[i + 1] == L'{')
        {
            built.push_back(L'{');
            i += 2;
            continue;
        }
        if (templ[i] == L'}' && i + 1 < templ.size() && templ[i + 1] == L'}')
        {
            built.push_back(L'}');
            i += 2;
            continue;
        }
        if (templ[i] != L'{')
        {
            built.push_back(templ[i]);
            ++i;
            continue;
        }
        size_t end = templ.find(L'}', i + 1);
        if (end == std::wstring::npos)
        {
            error = L"unclosed token";
            return false;
        }
        std::wstring token = templ.substr(i + 1, end - i - 1);
        std::wstring value;
        if (token.rfind(L"dest:", 0) == 0)
        {
            const CRoot* dest = FindDestLabel(plan, token.substr(5));
            if (dest == NULL)
            {
                error = L"unknown destination label";
                return false;
            }
            value = dest->Path;
        }
        else if (token == L"scope")
        {
            const CRoot* scope = NULL;
            for (size_t s = 0; s < plan.ScopeRoots.size(); ++s)
            {
                if (IsUnderPath(item.Path, plan.ScopeRoots[s].Path, false))
                    scope = &plan.ScopeRoots[s];
            }
            if (scope == NULL)
            {
                error = L"item is outside every scope root";
                return false;
            }
            value = scope->Path;
        }
        else if (token == L"relDir")
        {
            for (size_t s = 0; s < plan.ScopeRoots.size(); ++s)
            {
                if (IsUnderPath(item.Path, plan.ScopeRoots[s].Path, false))
                {
                    value = RelativeToScope(item, plan.ScopeRoots[s]);
                    break;
                }
            }
        }
        else if (token == L"parent")
            value = LeafName(item.ParentPath);
        else if (token == L"name")
        {
            std::wstring ext;
            value = SplitName(item.Name, ext);
        }
        else if (token == L"ext")
        {
            std::wstring ext;
            SplitName(item.Name, ext);
            value = ext;
        }
        else if (token.rfind(L"modified:", 0) == 0)
        {
            value = FormatTimeToken(item.LastWrite, token.substr(9));
            if (value.empty())
            {
                error = L"unknown time format";
                return false;
            }
        }
        else if (token.rfind(L"created:", 0) == 0)
        {
            value = FormatTimeToken(item.Created, token.substr(8));
            if (value.empty())
            {
                error = L"unknown time format";
                return false;
            }
        }
        else if (token.rfind(L"size:", 0) == 0)
        {
            std::wstring bucket = token.substr(5);
            std::wstring actual = item.Size < 1024ull * 1024ull ? L"small" : (item.Size < 100ull * 1024ull * 1024ull ? L"medium" : L"large");
            if (bucket != L"bucket" && bucket != actual && bucket != L"small" && bucket != L"medium" && bucket != L"large")
            {
                // {size:bucket} inserts the bucket name. A specific bucket is a filter-like token that still inserts itself when it matches the documented form.
            }
            if (bucket == L"bucket")
                value = actual;
            else if (bucket == L"small" || bucket == L"medium" || bucket == L"large")
                value = actual;
            else
            {
                error = L"unknown size token";
                return false;
            }
        }
        else
        {
            error = L"unknown token";
            return false;
        }
        built += value;
        i = end + 1;
    }
    if (!built.empty() && built.back() == L'\\')
    {
        destinationDir = built.substr(0, built.size() - 1);
        newName = item.Name;
    }
    else
    {
        destinationDir = ParentPath(built);
        newName = LeafName(built);
    }
    std::wstring normalizedDir, dirError;
    if (!destinationDir.empty() && !NormalizePath(destinationDir, normalizedDir, dirError))
    {
        error = dirError;
        return false;
    }
    if (!normalizedDir.empty())
        destinationDir = normalizedDir;
    return true;
}

std::vector<CRuleHit> ApplyRules(const CPlanDocument& plan, const CSnapshot& snapshot)
{
    std::vector<CRule> ordered = plan.Rules;
    for (size_t i = 0; i < ordered.size(); ++i)
    {
        for (size_t j = i + 1; j < ordered.size(); ++j)
        {
            if (ordered[j].Order < ordered[i].Order)
            {
                CRule tmp = ordered[i];
                ordered[i] = ordered[j];
                ordered[j] = tmp;
            }
        }
    }
    std::vector<CRuleHit> hits;
    for (std::map<std::wstring, CSnapshotItem>::const_iterator it = snapshot.Items.begin(); it != snapshot.Items.end(); ++it)
    {
        if (it->second.IsDir && PathsEqual(it->second.Path, it->first, true))
        {
            // Roots themselves are not rule targets unless a rule matches them as directories.
        }
        for (size_t r = 0; r < ordered.size(); ++r)
        {
            if (!MatchRule(ordered[r], it->second, plan, snapshot))
                continue;
            CRuleHit hit;
            std::wstring error;
            if (!ExpandTemplate(ordered[r].Destination, it->second, plan, snapshot, hit.DestinationDir, hit.NewName, error))
                break;
            hit.Source = it->second.Path;
            hit.RuleId = ordered[r].Id;
            hits.push_back(hit);
            break;
        }
    }
    return hits;
}

std::wstring ExpandKeepBoth(const std::wstring& pattern, const std::wstring& fileName, int n)
{
    std::wstring ext;
    std::wstring stem = SplitName(fileName, ext);
    std::wstring out;
    for (size_t i = 0; i < pattern.size();)
    {
        if (pattern.compare(i, 6, L"{name}") == 0)
        {
            out += stem;
            i += 6;
        }
        else if (pattern.compare(i, 5, L"{ext}") == 0)
        {
            out += ext;
            i += 5;
        }
        else if (pattern.compare(i, 3, L"{n}") == 0)
        {
            wchar_t buf[16];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", n);
            out += buf;
            i += 3;
        }
        else
        {
            out.push_back(pattern[i]);
            ++i;
        }
    }
    return out;
}

} // namespace reorg
