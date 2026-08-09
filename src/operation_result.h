// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// File-operation failures cross worker, journal, and dialog boundaries.  Keep
// the original phase and effect state together so a later compatibility adapter
// cannot accidentally replace the causal error with a cleanup-side failure.
enum EOperationResultPhase
{
    orpNone,
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

    static COperationResult Success(EOperationResultPhase phase, const char* source,
                                    const char* destination, DWORD partialEffects = opeNone)
    {
        COperationResult result = {phase, ERROR_SUCCESS, S_OK, source, destination, FALSE, partialEffects};
        return result;
    }

    static COperationResult Failure(EOperationResultPhase phase, DWORD error, const char* source,
                                    const char* destination, BOOL retryable,
                                    DWORD partialEffects = opeNone)
    {
        COperationResult result = {phase, error, HRESULT_FROM_WIN32(error), source, destination,
                                   retryable, partialEffects};
        return result;
    }

    BOOL Succeeded() const { return Win32Error == ERROR_SUCCESS; }

    // Existing dialogs still expect BOOL plus an out error; this preserves their
    // behavior while callers migrate to the complete result contract.
    BOOL ToLegacyBool(DWORD* error) const
    {
        if (!Succeeded() && error != NULL)
            *error = Win32Error;
        return Succeeded();
    }
};

// Retries are only suggested for transient contention or transport states;
// callers retain the final decision because a commit may still be destructive.
inline BOOL IsRetryableOperationError(DWORD error)
{
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_BUSY || error == ERROR_NETWORK_BUSY ||
           error == ERROR_NOT_READY || error == ERROR_SEM_TIMEOUT ||
           error == ERROR_TIMEOUT;
}
