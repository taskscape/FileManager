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
    int CurrentAttempt;
    char* Buffer;
    int BufferUsed;
    BOOL WriteFailed; // a partial checkpoint must never be followed by an apparent successful one

    BOOL SpillBuffer();
    BOOL FlushDurable();
    BOOL Append(const char* text);
    BOOL AppendPlanOperand(EOperationPlanOperandKind kind, const char* path, DWORD value);
    BOOL AppendGoldenMasterPlan(COperations& operations);

public:
    COperationJournal();
    ~COperationJournal();

    BOOL Begin(COperations& operations);
    BOOL BeginItem(int itemIndex, const COperation* operation, int attempt);
    void RecordRetry(int attempt);
    BOOL SetTemporaryPath(const char* temporaryPath);
    // Readiness belongs to a held staging object and its verified destination context.
    BOOL MarkTemporaryReady(const char* targetPath, const char* temporaryPath,
                             HANDLE directory, HANDLE target, HANDLE temporary, BOOL preserveTargetSecurity);
    // Flush intent before the destination name is vacated, then record each
    // publication boundary so an interrupted overwrite retains its backup path.
    BOOL RecordPublicationState(const char* state, const WCHAR* backupPath);
    void CompleteItem(BOOL succeeded);
    void Finish(BOOL failed, BOOL cancelled);

    // Runs after the allocator's posted UI notification, keeping filesystem I/O
    // out of the failing allocation thread while retaining an auditable marker.
    static void PersistEmergencyShutdownState();
    static void OfferRecovery(HWND parent);
};
