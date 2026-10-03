// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "scanner.h"

namespace reorg
{
namespace
{

bool EndsWith(const std::wstring& name, const wchar_t* ext)
{
    size_t n = wcslen(ext);
    return name.size() >= n && NamesEqual(name.substr(name.size() - n), ext, false);
}

} // namespace

bool CShortcutScanner::Accepts(const CSnapshotItem& item) const
{
    return !item.IsDir && EndsWith(item.Name, L".lnk");
}

void CShortcutScanner::Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel)
{
    (void)probe;
    if (cancel.IsCancelled())
        return;
    // Resolve is intentionally not called. Searching the disk can rewrite the link.
    CReference reference;
    reference.Referrer = item.Path;
    reference.ScannerId = Id();
    reference.RawText = item.Path;
    sink.Add(reference);
}

bool CUrlScanner::Accepts(const CSnapshotItem& item) const
{
    return !item.IsDir && EndsWith(item.Name, L".url");
}

void CUrlScanner::Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel)
{
    if (cancel.IsCancelled())
        return;
    std::vector<BYTE> data;
    DWORD error = 0;
    if (!probe.ReadFile(item.Path, 64 * 1024, data, error))
        return;
    std::string text(data.begin(), data.end());
    size_t url = text.find("URL=");
    if (url == std::string::npos)
        return;
    size_t end = text.find_first_of("\r\n", url);
    std::string value = text.substr(url + 4, end == std::string::npos ? std::string::npos : end - (url + 4));
    CReference reference;
    reference.Referrer = item.Path;
    reference.ScannerId = Id();
    reference.RawText = Utf8ToWide(value);
    if (value.rfind("file:", 0) == 0)
    {
        std::string path = value.substr(5);
        while (!path.empty() && path[0] == '/')
            path.erase(path.begin());
        reference.ResolvedOriginal = Utf8ToWide(path);
        for (size_t i = 0; i < reference.ResolvedOriginal.size(); ++i)
        {
            if (reference.ResolvedOriginal[i] == L'/')
                reference.ResolvedOriginal[i] = L'\\';
        }
    }
    sink.Add(reference);
}

bool CTextScanner::Accepts(const CSnapshotItem& item) const
{
    if (item.IsDir)
        return false;
    std::wstring ext;
    SplitName(item.Name, ext);
    if (!ext.empty() && ext[0] == L'.')
        ext = ext.substr(1);
    for (size_t i = 0; i < Extensions.size(); ++i)
    {
        if (NamesEqual(ext, Extensions[i], false))
            return true;
    }
    return false;
}

void CTextScanner::Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel)
{
    if (cancel.IsCancelled() || item.Size > MaxBytes)
        return;
    std::vector<BYTE> data;
    DWORD error = 0;
    if (!probe.ReadFile(item.Path, MaxBytes, data, error))
        return;
    size_t sniff = data.size() < 8192 ? data.size() : 8192;
    for (size_t i = 0; i < sniff; ++i)
    {
        if (data[i] == 0)
            return;
    }
    std::string text(data.begin(), data.end());
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t slash = text.find_first_of("\\/", pos);
        if (slash == std::string::npos || slash == 0)
            break;
        size_t start = slash;
        while (start > 0 && text[start - 1] != '"' && text[start - 1] != '\'' && text[start - 1] != '(' && text[start - 1] != ' ')
            --start;
        size_t end = slash;
        while (end < text.size() && text[end] != '"' && text[end] != '\'' && text[end] != ')' && text[end] != ' ' && text[end] != '\r' && text[end] != '\n')
            ++end;
        std::string token = text.substr(start, end - start);
        if (token.find(":\\") != std::string::npos || token.rfind("\\\\", 0) == 0)
        {
            CReference reference;
            reference.Referrer = item.Path;
            reference.ScannerId = Id();
            reference.RawText = Utf8ToWide(token).substr(0, 512);
            reference.Line = 1;
            sink.Add(reference);
        }
        pos = end + 1;
    }
}

bool ReferenceBreaks(const CReference& reference, const COverlayNode* target, const std::wstring& referrerProposedDir)
{
    (void)referrerProposedDir;
    if (target == NULL)
        return false;
    if (target->Change == ChangeUnchanged || target->Change == ChangeContains)
        return false;
    if (!reference.Relative)
        return true;
    return !PathsEqual(target->ProposedPath, reference.ResolvedOriginal, false);
}

} // namespace reorg
