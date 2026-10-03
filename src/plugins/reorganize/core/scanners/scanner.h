// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "../overlay.h"

namespace reorg
{

struct CReference
{
    std::wstring Referrer;
    std::wstring RawText;
    std::wstring ResolvedOriginal;
    bool Relative;
    int Line;
    const wchar_t* ScannerId;
    CReference() : Relative(false), Line(0), ScannerId(L"") {}
};

class IReferenceSink
{
public:
    virtual ~IReferenceSink() {}
    virtual void Add(CReference& reference) = 0;
};

class IReferenceScanner
{
public:
    virtual ~IReferenceScanner() {}
    virtual const wchar_t* Id() const = 0;
    virtual bool Accepts(const CSnapshotItem& item) const = 0;
    virtual void Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel) = 0;
};

class CShortcutScanner : public IReferenceScanner
{
public:
    virtual const wchar_t* Id() const { return L"lnk"; }
    virtual bool Accepts(const CSnapshotItem& item) const;
    virtual void Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel);
};

class CUrlScanner : public IReferenceScanner
{
public:
    virtual const wchar_t* Id() const { return L"url"; }
    virtual bool Accepts(const CSnapshotItem& item) const;
    virtual void Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel);
};

class CTextScanner : public IReferenceScanner
{
public:
    std::vector<std::wstring> Extensions;
    DWORD MaxBytes;
    CTextScanner() : MaxBytes(4194304) {}
    virtual const wchar_t* Id() const { return L"text"; }
    virtual bool Accepts(const CSnapshotItem& item) const;
    virtual void Scan(const CSnapshotItem& item, IFileSystemProbe& probe, IReferenceSink& sink, const CCancellation& cancel);
};

bool ReferenceBreaks(const CReference& reference, const COverlayNode* target, const std::wstring& referrerProposedDir);

} // namespace reorg
