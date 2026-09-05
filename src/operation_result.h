// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "retry_policy.h"

// File-operation failures cross worker, journal, and dialog boundaries.  Keep
// reservation, verification, and commit phases with their effect state so a
// later compatibility adapter cannot replace the causal error with a cleanup-side failure.
enum EOperationResultPhase
{
    orpNone,
    orpPrepareTransactionalTarget,
    orpVerifyDurableCopy,
    orpVerifyDestinationIdentity,
    orpCommitTransactionalTarget
};

enum EOperationPartialEffect
{
    opeNone = 0,
    opeTemporaryTargetReady = 0x0001,
    opeDestinationCommitted = 0x0002
};

enum EOperationCleanupPhase
{
    orcpNone,
    orcpCloseVerificationHandle,
    orcpDeleteUnverifiedTarget,
    // Attribute rollback must not hide a failed commit or cause it to run twice.
    orcpRestoreReadOnlyAttribute
};

struct COperationCleanupError
{
    EOperationCleanupPhase Phase;
    DWORD Win32Error;
    const char* Path;
};

// This is intentionally a non-owning, lightweight transport object: operation
// paths remain owned by the script while the result is consumed by its dialog.
struct COperationResult
{
    EOperationResultPhase Phase;
    DWORD Win32Error;
    HRESULT HResult;
    const char* Source;
    const char* Destination;
    BOOL Retryable;
    DWORD PartialEffects;
    COperationCleanupError CleanupErrors[2];
    DWORD CleanupErrorCount;

    static COperationResult Success(EOperationResultPhase phase, const char* source,
                                    const char* destination, DWORD partialEffects = opeNone)
    {
        COperationResult result = {phase, ERROR_SUCCESS, S_OK, source, destination, FALSE, partialEffects,
                                   {{orcpNone, ERROR_SUCCESS, NULL}, {orcpNone, ERROR_SUCCESS, NULL}}, 0};
        return result;
    }

    static COperationResult Failure(EOperationResultPhase phase, DWORD error, const char* source,
                                    const char* destination, BOOL retryable,
                                    DWORD partialEffects = opeNone)
    {
        COperationResult result = {phase, error, HRESULT_FROM_WIN32(error), source, destination,
                                   retryable, partialEffects,
                                   {{orcpNone, ERROR_SUCCESS, NULL}, {orcpNone, ERROR_SUCCESS, NULL}}, 0};
        return result;
    }

    BOOL Succeeded() const { return Win32Error == ERROR_SUCCESS; }

    // Cleanup is secondary evidence: preserve the first actionable failure even
    // when closing or deleting subsequently changes the thread's last-error value.
    void AppendCleanupError(EOperationCleanupPhase phase, DWORD error, const char* path)
    {
        if (error != ERROR_SUCCESS && CleanupErrorCount < _countof(CleanupErrors))
        {
            CleanupErrors[CleanupErrorCount].Phase = phase;
            CleanupErrors[CleanupErrorCount].Win32Error = error;
            CleanupErrors[CleanupErrorCount].Path = path;
            ++CleanupErrorCount;
        }
    }

    static const char* PhaseName(EOperationResultPhase phase)
    {
        switch (phase)
        {
        case orpPrepareTransactionalTarget: return "prepare-transactional-target";
        case orpVerifyDurableCopy: return "verify-durable-copy";
        case orpVerifyDestinationIdentity: return "verify-destination-identity";
        case orpCommitTransactionalTarget: return "commit-transactional-target";
        default: return "none";
        }
    }

    static const char* CleanupPhaseName(EOperationCleanupPhase phase)
    {
        switch (phase)
        {
        case orcpCloseVerificationHandle: return "close-verification-handle";
        case orcpDeleteUnverifiedTarget: return "delete-unverified-target";
        // Keep restoration failures distinguishable in copy-dialog diagnostics.
        case orcpRestoreReadOnlyAttribute: return "restore-read-only-attribute";
        default: return "none";
        }
    }

    // This fixed-buffer text is suitable for existing message boxes, whose Ctrl+C
    // behavior gives support a stable, complete diagnostic without heap allocation.
    void BuildDiagnosticSummary(char* buffer, int bufferLength) const
    {
        if (buffer == NULL || bufferLength <= 0)
            return;

        int written = _snprintf_s(buffer, bufferLength, _TRUNCATE,
                                  "phase=%s; error=%lu (0x%08lX); source=%s; destination=%s; retryable=%s; effects=0x%08lX",
                                  PhaseName(Phase), Win32Error, Win32Error,
                                  Source != NULL ? Source : "", Destination != NULL ? Destination : "",
                                  Retryable ? "yes" : "no", PartialEffects);
        for (DWORD index = 0; written >= 0 && index < CleanupErrorCount; ++index)
        {
            written += _snprintf_s(buffer + written, bufferLength - written, _TRUNCATE,
                                   "; cleanup[%lu]=%s error=%lu path=%s", index,
                                   CleanupPhaseName(CleanupErrors[index].Phase),
                                   CleanupErrors[index].Win32Error,
                                   CleanupErrors[index].Path != NULL ? CleanupErrors[index].Path : "");
        }
    }

    // Existing dialogs still expect BOOL plus an out error; this preserves their
    // behavior while callers migrate to the complete result contract.
    BOOL ToLegacyBool(DWORD* error) const
    {
        if (!Succeeded() && error != NULL)
            *error = Win32Error;
        return Succeeded();
    }
};

// Reuse the central transient classification; callers still retain the final
// decision because a retryable error can occur after a destructive commit.
inline BOOL IsRetryableOperationError(DWORD error)
{
    return IsTransientOperationError(error);
}
