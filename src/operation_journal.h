// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "operation_plan.h"

class COperations;
struct COperation;

// Durable, append-only facts for a native disk-operation script.  Recovery
// never replays ordinary destructive work: it can only commit a fully-written
// transactional sibling target, remove an uncommitted sibling target, or
// produce a reconciliation report.
class COperationJournal
{
private:
    HANDLE File;
    char Path[3 * MAX_PATH];
    int CurrentItem;

    BOOL Append(const char* text);
    BOOL AppendPlanOperand(EOperationPlanOperandKind kind, const char* path, DWORD value);
    BOOL AppendGoldenMasterPlan(COperations& operations);

public:
    COperationJournal();
    ~COperationJournal();

    BOOL Begin(COperations& operations);
    BOOL BeginItem(int itemIndex, const COperation* operation);
    BOOL SetTemporaryPath(const char* temporaryPath);
    BOOL MarkTemporaryReady();
    void CompleteItem(BOOL succeeded);
    void Finish(BOOL failed, BOOL cancelled);

    // Uses only fixed buffers and synchronous Win32 I/O so the allocator's
    // emergency path can leave an auditable recovery marker without allocating.
    static void PersistEmergencyShutdownState();
    static void OfferRecovery(HWND parent);
};
