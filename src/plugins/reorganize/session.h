// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The debug new macro breaks placement new inside the plan core.
#ifdef new
#undef new
#endif

#include "plan.h"
#include "planio.h"
#include "compiler.h"
#include "mapping.h"
#include "validate.h"
#include "fsprobe.h"

struct CReorgSession
{
    reorg::CPlanDocument Plan;
    reorg::CPlanHistory History;
    reorg::CSnapshot Snapshot;
    reorg::COverlay Overlay;
    reorg::CIssueSink Issues;
    reorg::CWin32FileSystemProbe Probe;
    bool Open;
    bool Analyzing;
    std::vector<std::wstring> Recent;

    CReorgSession() : Open(false), Analyzing(false) {}
};

CReorgSession& Session();
CSalamanderGeneralAbstract* Salamander();
void SetSalamander(CSalamanderGeneralAbstract* salamander);

std::wstring ToWide(const char* utf8);
std::string ToUtf8(const std::wstring& text);
const char* ChangeText(reorg::EChangeKind kind);
const char* ReversibleText(reorg::EReversibility value);

// Rescans the plan roots and revalidates. FALSE when no plan is open or a folder could not be
// scanned; the scan failure is then the only (blocking) issue and 'error' describes it.
bool RefreshAnalysis(std::wstring* error = NULL);
bool CreatePlan(const std::wstring& name, const std::wstring& scope, const std::wstring& destination, std::wstring& error);
bool OpenPlanFile(const std::wstring& path, std::wstring& error);
bool SavePlanFileAs(const std::wstring& path, std::wstring& error);
void ClosePlan();
bool StageDiskMove(const std::wstring& source, const std::wstring& destinationDir, const std::wstring& newName, std::wstring& error);
void RememberRecent(const std::wstring& path);
