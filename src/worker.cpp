// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "cfgdlg.h"
#include "worker.h"
#include "execlog.h"
#include "operation_journal.h"
#include "release_diagnostics.h"

#include <Aclapi.h>
#include <Ntsecapi.h>

// these functions have no header, we must load them dynamically
NTQUERYINFORMATIONFILE DynNtQueryInformationFile = NULL;
NTFSCONTROLFILE DynNtFsControlFile = NULL;

COperationsQueue OperationsQueue; // queue of disk operations

namespace
{
volatile LONG NextOperationCorrelationSequence = 0;

void CreateOperationCorrelationId(char* destination, int destinationLen)
{
    // Process, monotonic tick, and an atomic dispatch ordinal stay unique when commands share a tick.
    _snprintf_s(destination, destinationLen, _TRUNCATE, "%08lX-%08lX-%08lX",
                GetCurrentProcessId(), (DWORD)CMonotonicClock::Now(), // low 32 bits of the 64-bit monotonic clock keep the fixed-width format wrap-free of the legacy API
                (DWORD)InterlockedIncrement(&NextOperationCorrelationSequence));
}
}

WCHAR* SafeConvertAllocUtf8ToWide(const char* src, int len)
{
    __try {
        return ConvertAllocUtf8ToWide(src, len);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_NOACCESS);
        return NULL;
    }
}

HANDLE SafeCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                              DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
    __try {
        return CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                           dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_NOACCESS);
        return INVALID_HANDLE_VALUE;
    }
}

BOOL SafeDeleteFileW(LPCWSTR lpFileName)
{
    __try {
        return DeleteFileW(lpFileName);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
}

BOOL SafeSetFileAttributesW(LPCWSTR lpFileName, DWORD dwFileAttributes)
{
    __try {
        return SetFileAttributesW(lpFileName, dwFileAttributes);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
}

BOOL SafeRemoveDirectoryW(LPCWSTR lpPathName)
{
    __try {
        return RemoveDirectoryW(lpPathName);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
}

// CreateFileUtf8, DeleteFileUtf8, SetFileAttributesUtf8, RemoveDirectoryUtf8
// are globally defined in common/strutils.cpp — no local definitions needed here.

// if defined, various debug messages are written to TRACE
//#define WORKER_COPY_DEBUG_MSG

// comment out when we no longer want to monitor all the messages from the asynchronous copy algorithm
//#define ASYNC_COPY_DEBUG_MSG

// Helper function to determine optimal buffer size for synchronous copy operations
// Returns the buffer size based on drive types and operation flags
int GetOptimalSyncCopyBufferSize(COperations* script, DWORD opFlags)
{
    // For removable disks (floppies), use the smallest buffer
    if (script->RemovableSrcDisk || script->RemovableTgtDisk)
        return REMOVABLE_DISK_COPY_BUFFER;

    // For fast local-to-local operations (both source and target are fast drives),
    // use the larger 1MB buffer for better throughput on SSDs and HDDs
    if ((opFlags & OPFL_SRCPATH_IS_FAST) && (opFlags & OPFL_TGTPATH_IS_FAST) &&
        !(opFlags & OPFL_SRCPATH_IS_NET) && !(opFlags & OPFL_TGTPATH_IS_NET))
    {
        return FAST_LOCAL_COPY_BUFFER;
    }

    // Default: use standard buffer for network or mixed operations
    return OPERATION_BUFFER;
}

//
// ****************************************************************************
// CTransferSpeedMeter
//

//
// ****************************************************************************
// COperations
//

COperations::COperations(int base, int delta, char* waitInQueueSubject, char* waitInQueueFrom,
                         char* waitInQueueTo) : TDirectArray<COperation>(base, delta), Sizes(1, 400)
{
    CancellationEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
    if (CancellationEvent == NULL)
        TRACE_E("Unable to create file-operation cancellation event.");
    OperationState = opsPlanned;
    Journal = NULL;
    CurrentItemSequence = -1;
    CurrentItemAttempt = 0;
    CreateOperationCorrelationId(CorrelationId, _countof(CorrelationId));
    TotalSize = CQuadWord(0, 0);
    CompressedSize = CQuadWord(0, 0);
    OccupiedSpace = CQuadWord(0, 0);
    TotalFileSize = CQuadWord(0, 0);
    FreeSpace = CQuadWord(0, 0);
    BytesPerCluster = 0;
    ClearReadonlyMask = 0xFFFFFFFF;
    InvertRecycleBin = FALSE;
    CanUseRecycleBin = TRUE;
    SameRootButDiffVolume = FALSE;
    TargetPathSupADS = FALSE;
    //  TargetPathSupEFS = FALSE;
    TargetMetadataFileSystem = mtfsUnknown;
    PlannedMetadataLosses = mmlNone;
    IsCopyOrMoveOperation = FALSE;
    OverwriteOlder = FALSE;
    CopySecurity = FALSE;
    PreserveDirTime = FALSE;
    SourcePathIsNetwork = FALSE;
    CopyAttrs = FALSE;
    StartOnIdle = FALSE;
    ShowStatus = FALSE;
    IsCopyOperation = FALSE;
    FastMoveUsed = FALSE;
    ChangeSpeedLimit = FALSE;
    FilesCount = 0;
    DirsCount = 0;
    RemapNameFrom = NULL;
    RemapNameFromLen = 0;
    RemapNameTo = NULL;
    RemapNameToLen = 0;
    RemovableTgtDisk = FALSE;
    RemovableSrcDisk = FALSE;
    SkipAllCountSizeErrors = FALSE;
    WorkPath1[0] = 0;
    WorkPath1InclSubDirs = FALSE;
    WorkPath2[0] = 0;
    WorkPath2InclSubDirs = FALSE;
    WaitInQueueSubject = waitInQueueSubject; // released in FreeScript()
    WaitInQueueFrom = waitInQueueFrom;       // released in FreeScript()
    WaitInQueueTo = waitInQueueTo;           // released in FreeScript()
    HANDLES(InitializeCriticalSection(&StatusCS));
    TransferredFileSize = CQuadWord(0, 0);
    ProgressSize = CQuadWord(0, 0);
    UseSpeedLimit = FALSE;
    SpeedLimit = 1;
    SleepAfterWrite = -1;
    LastBufferLimit = 1;
    LastSetupTime = CMonotonicClock::Now();
    BytesTrFromLastSetup = CQuadWord(0, 0);
    UseProgressBufferLimit = FALSE;
    ProgressBufferLimit = ASYNC_SLOW_COPY_BUF_SIZE;
    LastProgBufLimTestTime = CMonotonicClock::AtLeastDurationAgo(1000); // due immediately without a wrap-unsafe subtraction
    LastFileBlockCount = 0;
    LastFileStartTime = CMonotonicClock::Now();
}

COperations::~COperations()
{
    delete Journal;
    if (CancellationEvent != NULL)
        HANDLES(CloseHandle(CancellationEvent));
    HANDLES(DeleteCriticalSection(&StatusCS));
}

BOOL COperations::BeginJournal()
{
    if (Journal != NULL)
        return FALSE;
    Journal = new COperationJournal;
    // Called from the worker after the progress dialog is pumping messages.
    if (Journal == NULL || !Journal->Begin(*this))
    {
        delete Journal;
        Journal = NULL;
        TRACE_E("Unable to create durable file-operation journal.");
        return FALSE;
    }
    return TRUE;
}

int COperations::BeginItemAttempt(int itemIndex)
{
    CurrentItemSequence = itemIndex;
    CurrentItemAttempt = 1;
    return CurrentItemAttempt;
}

int COperations::RecordItemRetry()
{
    if (CurrentItemSequence < 0)
        return 0;
    ++CurrentItemAttempt;
    // A synchronous dialog response is part of the worker's item attempt, not a new operation.
    if (Journal != NULL)
        Journal->RecordRetry(CurrentItemAttempt);
    ExecLogFileOperationRetry(CorrelationId, CurrentItemSequence, CurrentItemAttempt);
    return CurrentItemAttempt;
}

BOOL COperations::JournalBeginItem(int itemIndex, const COperation* operation, int attempt)
{
    return Journal == NULL || Journal->BeginItem(itemIndex, operation, attempt);
}

BOOL COperations::JournalSetTemporaryPath(const char* temporaryPath)
{
    return Journal == NULL || Journal->SetTemporaryPath(temporaryPath);
}

BOOL COperations::JournalMarkTemporaryReady()
{
    return Journal == NULL || Journal->MarkTemporaryReady();
}

void COperations::JournalCompleteItem(BOOL succeeded)
{
    if (Journal != NULL)
        Journal->CompleteItem(succeeded);
}

void COperations::FinishJournal(BOOL failed, BOOL cancelled)
{
    if (Journal != NULL)
        Journal->Finish(failed, cancelled);
}

static void AssertOperationTransition(BOOL valid, EOperationState from, EOperationState to)
{
#ifdef _DEBUG
    if (!valid)
    {
        TRACE_E("Invalid file-operation state transition: " << from << " -> " << to);
        DebugBreak();
    }
#else
    UNREFERENCED_PARAMETER(valid);
    UNREFERENCED_PARAMETER(from);
    UNREFERENCED_PARAMETER(to);
#endif
}

EOperationState COperations::GetOperationState() const
{
    return (EOperationState)InterlockedCompareExchange(const_cast<volatile LONG*>(&OperationState), opsPlanned, opsPlanned);
}

BOOL COperations::IsCancellationRequested() const
{
    return CancellationEvent != NULL && WaitForSingleObject(CancellationEvent, 0) == WAIT_OBJECT_0 ||
           GetOperationState() == opsCancelRequested;
}

BOOL COperations::Start()
{
    LONG state = InterlockedCompareExchange(&OperationState, opsRunning, opsPlanned);
    // Retain the lifecycle edge in release reports without capturing operation paths.
    if (state == opsPlanned)
        RecordReleaseDiagnosticOperationTransition(state, opsRunning);
    AssertOperationTransition(state == opsPlanned, (EOperationState)state, opsRunning);
    return state == opsPlanned;
}

BOOL COperations::RequestCancellation()
{
    for (;;)
    {
        LONG state = InterlockedCompareExchange(&OperationState, opsPlanned, opsPlanned);
        if (state == opsCancelRequested || state == opsStopping)
        {
            if (CancellationEvent != NULL)
                SetEvent(CancellationEvent);
            return FALSE; // already requested: deliberately idempotent
        }
        if (state == opsCompleted || state == opsFailed)
            return FALSE;
        if (state != opsPlanned && state != opsRunning)
        {
            AssertOperationTransition(FALSE, (EOperationState)state, opsCancelRequested);
            return FALSE;
        }
        if (InterlockedCompareExchange(&OperationState, opsCancelRequested, state) == state)
        {
            // Cancellation ordering is often the missing clue in a field hang.
            RecordReleaseDiagnosticOperationTransition(state, opsCancelRequested);
            if (CancellationEvent != NULL)
                SetEvent(CancellationEvent);
            return TRUE;
        }
    }
}

BOOL COperations::BeginStopping()
{
    for (;;)
    {
        LONG state = InterlockedCompareExchange(&OperationState, opsPlanned, opsPlanned);
        if (state == opsStopping)
            return TRUE;
        if (state != opsRunning && state != opsCancelRequested)
        {
            AssertOperationTransition(FALSE, (EOperationState)state, opsStopping);
            return FALSE;
        }
        if (InterlockedCompareExchange(&OperationState, opsStopping, state) == state)
        {
            // Preserve only the state transition, never the affected file names.
            RecordReleaseDiagnosticOperationTransition(state, opsStopping);
            return TRUE;
        }
    }
}

BOOL COperations::Complete(BOOL failed)
{
    EOperationState target = failed ? opsFailed : opsCompleted;
    LONG state = InterlockedCompareExchange(&OperationState, target, opsStopping);
    // Completion is retained so reports show whether a worker reached its terminal state.
    if (state == opsStopping)
        RecordReleaseDiagnosticOperationTransition(state, target);
    AssertOperationTransition(state == opsStopping, (EOperationState)state, target);
    return state == opsStopping;
}

BOOL COperations::Fail()
{
    for (;;)
    {
        LONG state = InterlockedCompareExchange(&OperationState, opsPlanned, opsPlanned);
        if (state == opsFailed)
            return TRUE;
        if (state == opsCompleted)
        {
            AssertOperationTransition(FALSE, (EOperationState)state, opsFailed);
            return FALSE;
        }
        if (InterlockedCompareExchange(&OperationState, opsFailed, state) == state)
        {
            // Failure transitions are safe context for an approved diagnostic report.
            RecordReleaseDiagnosticOperationTransition(state, opsFailed);
            return TRUE;
        }
    }
}

void COperations::SetTFS(const CQuadWord& TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize = TFS;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::CalcLimitBufferSize(int* limitBufferSize, int bufferSize)
{
    if (limitBufferSize != NULL)
    {
        *limitBufferSize = UseSpeedLimit && SpeedLimit < (DWORD)bufferSize ? (UseProgressBufferLimit && ProgressBufferLimit < SpeedLimit ? ProgressBufferLimit : SpeedLimit) : (UseProgressBufferLimit && ProgressBufferLimit < (DWORD)bufferSize ? ProgressBufferLimit : bufferSize);
    }
}

void COperations::EnableProgressBufferLimit(BOOL useProgressBufferLimit)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        UseProgressBufferLimit = useProgressBufferLimit;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetFileStartParams()
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        LastFileBlockCount = 0;
        LastFileStartTime = CMonotonicClock::Now();
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetTFSandProgressSize(const CQuadWord& TFS, const CQuadWord& pSize,
                                        int* limitBufferSize, int bufferSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize = TFS;
        ProgressSize = pSize;
        CalcLimitBufferSize(limitBufferSize, bufferSize);
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetNewBufSize(int* limitBufferSize, int bufferSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        CalcLimitBufferSize(limitBufferSize, bufferSize);
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToSpeedMetersAndTFSandPS(DWORD bytesCount, BOOL onlyToProgressSpeedMeter,
                                                   int bufferSize, int* limitBufferSize, DWORD maxPacketSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        CMonotonicTimePoint ti = CMonotonicClock::Now(); // 64-bit monotonic sample keeps the speed-limit arithmetic wrap-free

        if (maxPacketSize == 0)
            CalcLimitBufferSize((int*)&maxPacketSize, bufferSize);

        DWORD bytesCountForSpeedMeters = bytesCount;
        if (!onlyToProgressSpeedMeter)
        {
            if (limitBufferSize != NULL && bytesCount > 0)
            {
                if (UseSpeedLimit)
                {
                    DWORD sleepNow = 0;
                    if (SleepAfterWrite == -1) // this is the first packet, set up the speed limit parameters
                    {
                        CalcLimitBufferSize(limitBufferSize, bufferSize);
                        LastBufferLimit = *limitBufferSize;
                        if (SpeedLimit >= HIGH_SPEED_LIMIT)
                        { // here there is no risk of receiving more data than the speed limit allows (HIGH_SPEED_LIMIT must be >= the largest buffer)
                            SleepAfterWrite = 0;
                            BytesTrFromLastSetup.SetUI64(bytesCount);
                        }
                        else
                        {
                            SleepAfterWrite = (1000 * *limitBufferSize) / SpeedLimit; // for the first second of the transfer assume the transfer itself is "infinitely fast" (determining the actual speed from the first packet is unrealistic)
                            if (bytesCount > SpeedLimit)                              // a slowdown occurred during the operation (for example, a 32 KB buffer read & write happened and the speed limit is 1 B/s, so theoretically we should now wait 32768 seconds, which is naturally unrealistic)
                                sleepNow = 1000;                                      // wait one second + add to the speed meter only the bytes allowed by the speed limit (e.g., just 1 B)
                            else
                                sleepNow = (SleepAfterWrite * bytesCount) / *limitBufferSize;
                            BytesTrFromLastSetup.SetUI64(0);
                            LastSetupTime = ti + sleepNow;
                        }
                    }
                    else
                    {
                        if ((LONGLONG)(ti - LastSetupTime) >= 1000 || BytesTrFromLastSetup.Value + bytesCount >= SpeedLimit ||
                            SpeedLimit >= HIGH_SPEED_LIMIT && BytesTrFromLastSetup.Value + bytesCount >= SpeedLimit / HIGH_SPEED_LIMIT_BRAKE_DIV)
                        { // time to recalculate the speed limit parameters + possibly "brake"
                            __int64 sleepFromLastSetup64 = (SleepAfterWrite * BytesTrFromLastSetup.Value) / LastBufferLimit;
                            DWORD sleepFromLastSetup = sleepFromLastSetup64 < 1000 ? (DWORD)sleepFromLastSetup64 : 1000;
                            BytesTrFromLastSetup += CQuadWord(bytesCount, 0);
                            __int64 idealTotalTime64 = (1000 * BytesTrFromLastSetup.Value + SpeedLimit - 1) / SpeedLimit; // "+ SpeedLimit - 1" is for rounding
                            int idealTotalTime = idealTotalTime64 < 10000 ? (int)idealTotalTime64 : 10000;
                            if (idealTotalTime > (LONGLONG)(ti - LastSetupTime))
                            {
                                sleepNow = (DWORD)(idealTotalTime - (LONGLONG)(ti - LastSetupTime)); // need to brake (we are faster or only slightly slower than the speed limit); the signed projection handles a future LastSetupTime after braking
                                if (sleepNow > 1000)                              // waiting longer than a second makes no sense (the meter will accept at most *limitBufferSize)
                                    sleepNow = 1000;
                            }
                            // else sleepNow = 0;  // we are slower than the speed limit (at ideal speed we would wait the proportional part of SleepAfterWrite)

                            CalcLimitBufferSize(limitBufferSize, bufferSize);
                            LastBufferLimit = *limitBufferSize;

                            if (SpeedLimit >= HIGH_SPEED_LIMIT)
                                SleepAfterWrite = 0;
                            else
                            {
                                int idealTotalSleep = (int)(sleepFromLastSetup + (idealTotalTime - (LONGLONG)(ti - LastSetupTime)));
                                if (idealTotalSleep > 0) // speed limit is lower than the copy speed, we will brake after each packet
                                    SleepAfterWrite = (DWORD)(((unsigned __int64)idealTotalSleep * LastBufferLimit) / BytesTrFromLastSetup.Value);
                                else
                                    SleepAfterWrite = 0; // speed limit is higher than the copy speed (no need to brake)
                            }
                            LastSetupTime = ti + sleepNow;
                            BytesTrFromLastSetup.SetUI64(0);
                        }
                        else // for intermediate packets use the precomputed parameters
                        {
                            BytesTrFromLastSetup += CQuadWord(bytesCount, 0);
                            *limitBufferSize = LastBufferLimit < bufferSize ? LastBufferLimit : bufferSize;
                            if (SleepAfterWrite > 0)
                            {
                                sleepNow = (SleepAfterWrite * bytesCount) / LastBufferLimit;
                                if (sleepNow > 1000) // waiting longer than a second makes no sense (the meter will accept at most the speed limit)
                                    sleepNow = 1000;
                            }
                        }
                    }
                    if (bytesCount > SpeedLimit)               // a slowdown occurred during the operation (for example, a 32 KB buffer read & write happened and the speed limit is 1 B/s, so theoretically we should now wait 32768 seconds, which is naturally unrealistic)
                        bytesCountForSpeedMeters = SpeedLimit; // add to the speed meter only the bytes allowed by the speed limit (e.g., just 1 B)
                    if (sleepNow > 0)                          // braking because of the speed limit
                    {
                        HANDLES(LeaveCriticalSection(&StatusCS));
                        Sleep(sleepNow);
                        HANDLES(EnterCriticalSection(&StatusCS));
                        ti = CMonotonicClock::Now();
                    }
                }
                else
                    CalcLimitBufferSize(limitBufferSize, bufferSize); // without limit - full speed (except for ProgressBufferLimit)
            }
            TransferSpeedMeter.BytesReceived(bytesCountForSpeedMeters, ti, maxPacketSize);
            TransferredFileSize.Value += bytesCount;

            if (UseProgressBufferLimit &&
                (++LastFileBlockCount >= ASYNC_SLOW_COPY_BUF_MINBLOCKS || // provided there is enough data for the test
                 ProgressBufferLimit * LastFileBlockCount >= ASYNC_SLOW_COPY_BUF_MINBLOCKS * ASYNC_SLOW_COPY_BUF_SIZE) &&
                ti - LastProgBufLimTestTime >= 1000) // and it is time for another test
            {                                        // compute ProgressBufferLimit for the next round (the next read still uses the current value)
                TransferSpeedMeter.AdjustProgressBufferLimit(&ProgressBufferLimit, LastFileBlockCount, LastFileStartTime);
                LastProgBufLimTestTime = CMonotonicClock::Now();
                if (LastFileBlockCount > 1000000000)
                    LastFileBlockCount = 1000000; // overflow protection (just a ton of blocks, the exact count is not that important)
            }
        }
        ProgressSpeedMeter.BytesReceived(bytesCountForSpeedMeters, ti, maxPacketSize);
        ProgressSize.Value += bytesCount;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToTFSandSetProgressSize(const CQuadWord& bytesCount, const CQuadWord& pSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize += bytesCount;
        ProgressSize = pSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::AddBytesToTFS(const CQuadWord& bytesCount)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferredFileSize += bytesCount;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetTFS(CQuadWord* TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *TFS = TransferredFileSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetTFSandResetTrSpeedIfNeeded(CQuadWord* TFS)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *TFS = TransferredFileSize;
        if (TransferSpeedMeter.ResetSpeed)
        {
            TransferSpeedMeter.JustConnected();
            if (UseSpeedLimit)
            {
                SleepAfterWrite = -1; // compute when the first packet arrives
LastSetupTime = CMonotonicClock::Now();
                    BytesTrFromLastSetup.SetUI64(0);
            }
        }
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::SetProgressSize(const CQuadWord& pSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        ProgressSize = pSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

void COperations::GetStatus(CQuadWord* transferredFileSize, CQuadWord* transferSpeed,
                            CQuadWord* progressSize, CQuadWord* progressSpeed,
                            BOOL* useSpeedLimit, DWORD* speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    *transferredFileSize = TransferredFileSize;
    *progressSize = ProgressSize;
    TransferSpeedMeter.GetSpeed(transferSpeed);
    ProgressSpeedMeter.GetSpeed(progressSpeed);
    *useSpeedLimit = UseSpeedLimit;
    *speedLimit = SpeedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

void COperations::InitSpeedMeters(BOOL operInProgress)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        TransferSpeedMeter.JustConnected();
        ProgressSpeedMeter.JustConnected();
        if (UseSpeedLimit)
        {
            SleepAfterWrite = -1; // compute when the first packet arrives
LastSetupTime = CMonotonicClock::Now();
              BytesTrFromLastSetup.SetUI64(0);
        }
        // after a pause, a speed limit change, or an error dialog discard the old data
        if (operInProgress)
        {
            LastFileBlockCount = 0;
            LastFileStartTime = CMonotonicClock::Now();
            LastProgBufLimTestTime = CMonotonicClock::Now(); // postpone the next test by a second so that we have relevant data
        }
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
}

BOOL COperations::GetTFSandProgressSize(CQuadWord* transferredFileSize, CQuadWord* progressSize)
{
    if (ShowStatus)
    {
        HANDLES(EnterCriticalSection(&StatusCS));
        *transferredFileSize = TransferredFileSize;
        *progressSize = ProgressSize;
        HANDLES(LeaveCriticalSection(&StatusCS));
    }
    return ShowStatus;
}

void COperations::SetSpeedLimit(BOOL useSpeedLimit, DWORD speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    UseSpeedLimit = useSpeedLimit;
    SpeedLimit = speedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

void COperations::GetSpeedLimit(BOOL* useSpeedLimit, DWORD* speedLimit)
{
    HANDLES(EnterCriticalSection(&StatusCS));
    *useSpeedLimit = UseSpeedLimit;
    *speedLimit = SpeedLimit;
    HANDLES(LeaveCriticalSection(&StatusCS));
}

//
