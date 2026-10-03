// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "session.h"

static CReorgSession g_Session;
static CSalamanderGeneralAbstract* g_Salamander = NULL;

CReorgSession& Session()
{
    return g_Session;
}

CSalamanderGeneralAbstract* Salamander()
{
    return g_Salamander;
}

void SetSalamander(CSalamanderGeneralAbstract* salamander)
{
    g_Salamander = salamander;
}

std::wstring ToWide(const char* utf8)
{
    if (utf8 == NULL || utf8[0] == 0)
        return std::wstring();
    return reorg::Utf8ToWide(utf8);
}

std::string ToUtf8(const std::wstring& text)
{
    return reorg::WideToUtf8(text);
}

const char* ChangeText(reorg::EChangeKind kind)
{
    switch (kind)
    {
    case reorg::ChangeMovedHere:
        return "Moved here";
    case reorg::ChangeRenamed:
        return "Renamed";
    case reorg::ChangeMovedAndRenamed:
        return "Moved and renamed";
    case reorg::ChangeMovedWithFolder:
        return "Moved with folder";
    case reorg::ChangeNewFolder:
        return "New folder";
    case reorg::ChangeKeptBoth:
        return "Keep both";
    case reorg::ChangeReplaces:
        return "Replaces";
    case reorg::ChangeMerged:
        return "Merged";
    case reorg::ChangeContains:
        return "Contains changes";
    case reorg::ChangeApplied:
        return "Applied";
    default:
        return "";
    }
}

const char* ReversibleText(reorg::EReversibility value)
{
    switch (value)
    {
    case reorg::RevExact:
        return "Exact";
    case reorg::RevWithLoss:
        return "With loss";
    case reorg::RevUntilFinalized:
        return "Until finalized";
    case reorg::RevNotReversible:
        return "Not reversible";
    default:
        return "";
    }
}

static void AddRoot(std::vector<reorg::CRoot>& roots, const std::wstring& path)
{
    reorg::CRoot root;
    root.Path = path;
    root.Id = reorg::NewGuid();
    reorg::CVolumeInfo info;
    if (Session().Probe.GetVolume(path, info))
    {
        wchar_t serial[16];
        swprintf_s(serial, L"0x%08X", info.Serial);
        root.VolumeSerial = serial;
        root.FileSystem = info.FileSystem;
    }
    roots.push_back(root);
}

bool RefreshAnalysis()
{
    if (!Session().Open)
        return false;
    Session().Analyzing = true;
    std::vector<std::wstring> roots;
    for (size_t i = 0; i < Session().Plan.ScopeRoots.size(); ++i)
        roots.push_back(Session().Plan.ScopeRoots[i].Path);
    for (size_t i = 0; i < Session().Plan.DestinationRoots.size(); ++i)
        roots.push_back(Session().Plan.DestinationRoots[i].Path);
    reorg::CCancellation cancel;
    Session().Snapshot.Capture(Session().Probe, roots, true, cancel);
    Session().Overlay = reorg::BuildOverlay(Session().Snapshot, Session().Plan);
    reorg::CAnalysisContext context;
    context.Plan = &Session().Plan;
    context.Snapshot = &Session().Snapshot;
    context.Overlay = &Session().Overlay;
    Session().Issues.Issues.clear();
    reorg::ValidatePlan(context, Session().Issues);
    Session().Analyzing = false;
    return true;
}

bool CreatePlan(const std::wstring& name, const std::wstring& scope, const std::wstring& destination, std::wstring& error)
{
    std::wstring scopePath;
    std::wstring destPath;
    if (!reorg::NormalizePath(scope, scopePath, error) || !reorg::IsDiskPath(scopePath))
    {
        error = L"The scope must be a disk path.";
        return false;
    }
    if (!reorg::NormalizePath(destination, destPath, error) || !reorg::IsDiskPath(destPath))
    {
        error = L"The destination must be a disk path.";
        return false;
    }
    Session().Plan = reorg::CPlanDocument();
    Session().History.Clear();
    Session().Plan.PlanId = reorg::NewGuid();
    Session().Plan.Name = name.empty() ? L"Reorganization" : name;
    Session().Plan.CreatedUtc = reorg::UtcNowIso();
    Session().Plan.ModifiedUtc = Session().Plan.CreatedUtc;
    Session().Plan.ExtensionsJson = "{}";
    AddRoot(Session().Plan.ScopeRoots, scopePath);
    AddRoot(Session().Plan.DestinationRoots, destPath);
    Session().Plan.Dirty = true;
    Session().Open = true;
    return RefreshAnalysis();
}

bool OpenPlanFile(const std::wstring& path, std::wstring& error)
{
    std::vector<BYTE> data;
    DWORD readError = 0;
    if (!Session().Probe.ReadFile(path, 256ull * 1024ull * 1024ull, data, readError))
    {
        error = L"The plan file could not be read.";
        return false;
    }
    std::string text(data.begin(), data.end());
    reorg::CPlanDocument plan;
    reorg::CPlanIoResult loaded = reorg::LoadPlanJson(text, plan);
    if (!loaded.Ok)
    {
        error = loaded.Error;
        return false;
    }
    Session().Plan = plan;
    Session().Plan.FilePath = path;
    Session().History.Clear();
    Session().Open = true;
    RememberRecent(path);
    return RefreshAnalysis();
}

bool SavePlanFileAs(const std::wstring& path, std::wstring& error)
{
    if (!Session().Open)
    {
        error = L"No plan is open.";
        return false;
    }
    reorg::CPlanIoResult saved = reorg::SavePlanFile(Session().Probe, path, Session().Plan);
    if (!saved.Ok)
    {
        error = saved.Error.empty() ? L"The plan could not be saved." : saved.Error;
        return false;
    }
    Session().Plan.FilePath = path;
    Session().Plan.Dirty = false;
    RememberRecent(path);
    return true;
}

void ClosePlan()
{
    Session().Plan = reorg::CPlanDocument();
    Session().History.Clear();
    Session().Snapshot = reorg::CSnapshot();
    Session().Overlay = reorg::COverlay();
    Session().Issues.Issues.clear();
    Session().Open = false;
}

bool StageDiskMove(const std::wstring& source, const std::wstring& destinationDir, const std::wstring& newName, std::wstring& error)
{
    if (!Session().Open)
    {
        error = L"No plan is open.";
        return false;
    }
    reorg::CStageResult staged = reorg::StageMove(Session().Plan, Session().History, Session().Snapshot, source, destinationDir, newName, L"panel");
    if (!staged.Ok)
    {
        error = staged.Error;
        return false;
    }
    RefreshAnalysis();
    return true;
}

void RememberRecent(const std::wstring& path)
{
    std::vector<std::wstring>& recent = Session().Recent;
    for (size_t i = 0; i < recent.size(); ++i)
    {
        if (reorg::PathsEqual(recent[i], path, false))
        {
            recent.erase(recent.begin() + (ptrdiff_t)i);
            break;
        }
    }
    recent.insert(recent.begin(), path);
    if (recent.size() > 10)
        recent.resize(10);
}
