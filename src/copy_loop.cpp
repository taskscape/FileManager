// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <Ntsecapi.h> // LsaNtStatusToWinError

#include "common/scoped_native_resources.h"
#include "file_operation_filesystem.h"
#include "release_diagnostics.h"
#include "retry_policy.h"
#include "worker.h"

#include "async_copy_internals.h"

// Buffered and overlapped copy-loop engine extracted from async_copy.cpp as a
// mechanical move: CAsyncCopyParams implementations, tail verification,
// sync/async DeviceIoControl + compression/encryption attribute application,
// DoCopyFileLoopOrig, the CCopy_Context state machine, DisableLocalBuffering,
// and DoCopyFileLoopAsync. Entry points are declared in async_copy_internals.h.
// ****************************************************************************
// CAsyncCopyParams (declaration in worker.h, implementations below)
//

CAsyncCopyParams::CAsyncCopyParams()
{
    memset(Buffers, 0, sizeof(Buffers));
    memset(Overlapped, 0, sizeof(Overlapped));
    UseAsyncAlg = FALSE;
    HasFailed = FALSE;
}

void CAsyncCopyParams::Init(BOOL useAsyncAlg)
{
    UseAsyncAlg = useAsyncAlg;
    if (UseAsyncAlg && Buffers[0] == NULL)
    {
        for (int i = 0; i < 8; i++)
        {
            Buffers[i] = malloc(ASYNC_COPY_BUF_SIZE);
            Overlapped[i].hEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
            if (Overlapped[i].hEvent == NULL)
            {
                DWORD err = GetLastError();
                TRACE_E("Unable to create synchronization object for Copy rutine: " << GetErrorText(err));
                HasFailed = TRUE;
            }
        }
    }
}

CAsyncCopyParams::~CAsyncCopyParams()
{
    for (int i = 0; i < 8; i++)
    {
        if (Buffers[i] != NULL)
            free(Buffers[i]);
        if (Overlapped[i].hEvent != NULL)
            HANDLES(CloseHandle(Overlapped[i].hEvent));
    }
}

OVERLAPPED*
CAsyncCopyParams::InitOverlapped(int i)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlapped(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = 0;
    Overlapped[i].OffsetHigh = 0;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

OVERLAPPED*
CAsyncCopyParams::InitOverlappedWithOffset(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlappedWithOffset(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

void CAsyncCopyParams::SetOverlappedToEOF(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::SetOverlappedToEOF(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0xC0000011 /* STATUS_END_OF_FILE */; // NTSTATUS code equivalent to system error code ERROR_HANDLE_EOF
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    SetEvent(Overlapped[i].hEvent);
}

#define RETRYCOPY_TAIL_MINSIZE (32 * 1024) // at least two blocks of this size are verified at the end of the file tested in CheckTailOfOutFile(); afterwards the block size grows up to ASYNC_COPY_BUF_SIZE (if reading is fast enough); NOTE: must be <= ASYNC_COPY_BUF_SIZE
#define RETRYCOPY_TESTINGTIME 3000         // duration of the CheckTailOfOutFile() test in [ms]

void LogTailVerificationError(const char* txt, DWORD err)
{
    TRACE_I("CheckTailOfOutFile(): " << txt << " Error: " << GetErrorText(err));
}

BOOL CheckTailOfOutFile(CAsyncCopyParams* asyncPar, HANDLE in, HANDLE out, const CQuadWord& offset,
                        const CQuadWord& curInOffset, BOOL ignoreReadErrOnOut)
{
    CScopedHeapBuffer inputBuffer(malloc(ASYNC_COPY_BUF_SIZE));
    CScopedHeapBuffer outputBuffer(malloc(ASYNC_COPY_BUF_SIZE));
    char* bufIn = (char*)inputBuffer.Get();
    char* bufOut = (char*)outputBuffer.Get();
    if (bufIn == NULL || bufOut == NULL)
    {
        // Tail verification must fail cleanly when its temporary comparison
        // buffers cannot be owned, rather than leaking one buffer or crashing.
        TRACE_E("CheckTailOfOutFile(): unable to allocate verification buffers.");
        return FALSE;
    }

    CMonotonicTimePoint startTime = CMonotonicClock::Now();
    CMonotonicTimePoint rutineStartTime = startTime;
    CQuadWord lastOffset = offset;
    int roundNum = 1;
    DWORD curBufSize = RETRYCOPY_TAIL_MINSIZE;
    CMonotonicTimePoint lastRoundStartTime = 0;
    DWORD lastRoundBufSize = 0;
    BOOL searchLongLastingBlock = TRUE;
    BOOL ok;
    while (1)
    {
        CMonotonicTimePoint roundStartTime = CMonotonicClock::Now();
        ok = FALSE;
        CQuadWord start;
        start.Value = lastOffset.Value > curBufSize ? lastOffset.Value - curBufSize : 0;
        DWORD size = (DWORD)(lastOffset.Value - start.Value);
        if (size == 0)
        {
            ok = TRUE;
            break; // nothing to verify
        }
#ifdef WORKER_COPY_DEBUG_MSG
        TRACE_I("CheckTailOfOutFile(): check: " << start.Value << " - " << lastOffset.Value << ", size: " << size);
#endif // WORKER_COPY_DEBUG_MSG
        if (asyncPar == NULL)
        {
            CFileOffsetResult inSeek = SalSetFilePointerEx(in, start, FILE_BEGIN);
            if (inSeek.Succeeded)
            { // set the 'start' offset in the input
                CFileOffsetResult outSeek = SalSetFilePointerEx(out, start, FILE_BEGIN);
                if (outSeek.Succeeded)
                { // set the 'start' offset in the output
                    DWORD read;
                    if (ReadFile(out, bufOut, size, &read, NULL) && read == size)
                    { // read 'size' bytes into the output buffer (fails if opened without read access)
                        if (ReadFile(in, bufIn, size, &read, NULL) && read == size)
                        {                                         // read 'size' bytes into the input buffer
                            if (memcmp(bufIn, bufOut, size) == 0) // compare whether the input/output buffers match
                                ok = TRUE;
                            else
                                TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file, tail without differences: " << (offset.Value - lastOffset.Value));
                        }
                        else
                            LogTailVerificationError("Unable to read IN file.", GetLastError());
                    }
                    else
                    {
                        if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                        {
                            LogTailVerificationError("Unable to read OUT file, but it was not broken, so it's no problem.", GetLastError());
                            ok = TRUE;
                            break;
                        }
                        else
                            LogTailVerificationError("Unable to read OUT file.", GetLastError());
                    }
                }
                else
                    LogTailVerificationError("Unable to set file pointer to start offset in OUT file.", outSeek.Error);
            }
            else
                LogTailVerificationError("Unable to set file pointer to start offset in IN file.", inSeek.Error);
        }
        else
        {
            // asynchronously read the block starting at 'start' of length 'size' bytes from in and out, then compare
            DWORD readOut;
            if ((ReadFile(out, bufOut, size, NULL,
                          asyncPar->InitOverlappedWithOffset(0, start)) ||
                 GetLastError() == ERROR_IO_PENDING) &&
                GetOverlappedResult(out, asyncPar->GetOverlapped(0), &readOut, TRUE))
            {
                DWORD readIn;
                if ((ReadFile(in, bufIn, size, NULL,
                              asyncPar->InitOverlappedWithOffset(1, start)) ||
                     GetLastError() == ERROR_IO_PENDING) &&
                    GetOverlappedResult(in, asyncPar->GetOverlapped(1), &readIn, TRUE))
                {
                    if (readOut != size || readIn != size ||
                        memcmp(bufIn, bufOut, size) != 0) // compare whether the input/output buffers match
                    {
                        TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file (async), tail without differences: " << (offset.Value - lastOffset.Value));
                    }
                    else
                        ok = TRUE;
                }
                else
                    LogTailVerificationError("Unable to read IN file (async).", GetLastError());
            }
            else
            {
                if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                {
                    LogTailVerificationError("Unable to read OUT file (async), but it was not broken, so it's no problem.", GetLastError());
                    ok = TRUE;
                    break;
                }
                else
                    LogTailVerificationError("Unable to read OUT file (async).", GetLastError());
            }
        }
        if (!ok)
            break;
        lastOffset = start;
        DWORD curBufSizeBackup = curBufSize;
        if (roundNum > 1)
        {
            const CMonotonicTimePoint ti = CMonotonicClock::Now();
            if (searchLongLastingBlock)
            {
                CMonotonicDuration t1 = CMonotonicClock::Elapsed(lastRoundStartTime, roundStartTime);
                CMonotonicDuration t2 = CMonotonicClock::Elapsed(roundStartTime, ti);
                if (roundNum == 2 && t1 > 300 && 10 * t2 < t1) // first iteration waits for the disk/network to be ready, shift the start time (so we spend the configured time reading instead of just waiting)
                {
#ifdef WORKER_COPY_DEBUG_MSG
                    TRACE_I("CheckTailOfOutFile(): detected long lasting first block, start time shifted by " << ((roundStartTime - startTime) / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                    startTime = roundStartTime;
                }
                else
                {
                    if (t2 > 1000 && ((curBufSize * 10) / lastRoundBufSize) * t1 < t2)
                    { // unexpectedly long block read, likely waiting for disk "verification" or similar; ignore this block once so the overall check still behaves normally
                        searchLongLastingBlock = FALSE;
                        CMonotonicDuration sh = t2 - ((unsigned __int64)curBufSize * t1) / lastRoundBufSize;
#ifdef WORKER_COPY_DEBUG_MSG
                        TRACE_I("CheckTailOfOutFile(): detected long lasting block, start time shifted by " << (sh / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                        startTime += sh;
                    }
                }
            }
            if (CMonotonicClock::Elapsed(startTime, ti) > RETRYCOPY_TESTINGTIME)
                break; // we have been reading long enough; stop after the mandatory two rounds
            if (CMonotonicClock::Elapsed(roundStartTime, ti) < 300 && curBufSize < ASYNC_COPY_BUF_SIZE)
            { // when reading is fast enough, enlarge the buffer to avoid excessive reverse seeking (toward the beginning of the file)
                curBufSize *= 2;
                if (curBufSize > ASYNC_COPY_BUF_SIZE)
                    curBufSize = ASYNC_COPY_BUF_SIZE;
            }
        }
        roundNum++;
        lastRoundStartTime = roundStartTime;
        lastRoundBufSize = curBufSizeBackup;
    }

    if (ok && asyncPar == NULL) // reposition input/output to required offsets
    {
        CFileOffsetResult inSeek = SalSetFilePointerEx(in, curInOffset, FILE_BEGIN);
        if (!inSeek.Succeeded)
        {
            LogTailVerificationError("Unable to set file pointer back to current offset in IN file.", inSeek.Error);
            ok = FALSE;
        }
        if (ok)
        {
            CFileOffsetResult outSeek = SalSetFilePointerEx(out, offset, FILE_BEGIN);
            if (!outSeek.Succeeded)
            {
                LogTailVerificationError("Unable to set file pointer back to current offset in OUT file.", outSeek.Error);
                ok = FALSE;
            }
        }
    }
#ifdef WORKER_COPY_DEBUG_MSG
    if (!ok)
        TRACE_I("CheckTailOfOutFile(): aborting Retry...");
    else
    {
        TRACE_I("CheckTailOfOutFile(): " << (offset.Value - lastOffset.Value) / 1024.0 << " KB tested in " << CMonotonicClock::Elapsed(rutineStartTime, CMonotonicClock::Now()) / 1000.0 << " secs (clear read time: " << CMonotonicClock::Elapsed(startTime, CMonotonicClock::Now()) / 1000.0 << " secs).");
    }
#endif // WORKER_COPY_DEBUG_MSG
    return ok;
}

BOOL SyncOrAsyncDeviceIoControl(CAsyncCopyParams* asyncPar, HANDLE hDevice, DWORD dwIoControlCode,
                                LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                                DWORD nOutBufferSize, LPDWORD lpBytesReturned, DWORD* err)
{
    if (asyncPar->UseAsyncAlg) // asynchronous variant
    {
        if (!DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                             nOutBufferSize, NULL, asyncPar->InitOverlapped(0)) &&
                GetLastError() != ERROR_IO_PENDING ||
            !GetOverlappedResult(hDevice, asyncPar->GetOverlapped(0), lpBytesReturned, TRUE))
        { // error, return FALSE
            *err = GetLastError();
            return FALSE;
        }
    }
    else // synchronous variant
    {
        if (!DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                             nOutBufferSize, lpBytesReturned, NULL))
        { // error, return FALSE
            *err = GetLastError();
            return FALSE;
        }
    }
    *err = NO_ERROR;
    return TRUE;
}

void SetCompressAndEncryptedAttrs(const char* name, DWORD attr, HANDLE* out, BOOL openAlsoForRead,
                                  BOOL* encryptionNotSupported, CAsyncCopyParams* asyncPar)
{
    if (*out != INVALID_HANDLE_VALUE)
    {
        DWORD err = NO_ERROR;
        DWORD curAttr = SalGetFileAttributes(name);
        if ((curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            USHORT state = COMPRESSION_FORMAT_NONE;
            ULONG length;
            if (!SyncOrAsyncDeviceIoControl(asyncPar, *out, FSCTL_SET_COMPRESSION, &state,
                                            sizeof(USHORT), NULL, 0, &length, &err))
            {
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << name << "! error=" << GetErrorText(err));
            }
        }
        if (curAttr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_ENCRYPTED) != (curAttr & FILE_ATTRIBUTE_ENCRYPTED))
        { // SalCreateFileEx above likely failed
            err = NO_ERROR;
            HANDLES(CloseHandle(*out)); // close the file; otherwise we cannot change its encrypted attribute
            CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
            if (nameW == NULL)
                err = ERROR_NO_UNICODE_TRANSLATION;
            if (attr & FILE_ATTRIBUTE_ENCRYPTED)
            {
                if (err == NO_ERROR && !EncryptFileW(nameW))
                {
                    err = GetLastError();
                    if (encryptionNotSupported != NULL)
                        *encryptionNotSupported = TRUE;
                }
            }
            else
            {
                if (err == NO_ERROR && !DecryptFileW(nameW, 0))
                    err = GetLastError();
            }
            if (err != NO_ERROR)
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Encrypted attribute for " << name << "! error=" << GetErrorText(err));
            // reopen the existing file to continue writing
            if (err == NO_ERROR)
            {
                *out = HANDLES_Q(CreateFileW(nameW, GENERIC_WRITE | (openAlsoForRead ? GENERIC_READ : 0), 0, NULL, OPEN_ALWAYS,
                                             asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            }
            else
                *out = INVALID_HANDLE_VALUE;
            if (openAlsoForRead && *out == INVALID_HANDLE_VALUE) // problem: reopening failed, try write-only
            {
                *out = HANDLES_Q(CreateFileW(nameW, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                             asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            }
            if (*out == INVALID_HANDLE_VALUE) // still a problem: cannot reopen; delete it + report an error
            {
                err = GetLastError();
                if (nameW != NULL)
                    DeleteFileW(nameW);
                SetLastError(err);
            }
        }
        if (*out != INVALID_HANDLE_VALUE && // only when reopening succeeded (and we did not delete the file)
            (curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) != 0)
        {
            USHORT state = COMPRESSION_FORMAT_DEFAULT;
            ULONG length;
            if (!SyncOrAsyncDeviceIoControl(asyncPar, *out, FSCTL_SET_COMPRESSION, &state,
                                            sizeof(USHORT), NULL, 0, &length, &err))
            {
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << name << "! error=" << GetErrorText(err));
            }
        }
    }
}

void DoCopyFileLoopOrig(HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                        COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated,
                        COperation* op, const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy,
                        HWND hProgressDlg, CQuadWord& operationDone, CQuadWord& fileSize,
                        int bufferSize, int& allocWholeFileOnStart, BOOL& copyAgain,
                        CStableMoveSource* stableMoveSource)
{
    int autoRetryAttemptsSNAP = 0;
    DWORD read;
    DWORD written;
    while (1)
    {
        if (ReadFile(in, buffer, limitBufferSize, &read, NULL))
        {
            autoRetryAttemptsSNAP = 0;
            if (read == 0)
                break;                                                     // EOF
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            while (1)
            {
                if (OperationExecutionFileSystem().WriteFile(out, buffer, read, &written, NULL) &&
                    read == written)
                {
                    break;
                }

            WRITE_ERROR:

                DWORD err;
                err = GetLastError();

                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }

                if (dlgData.SkipAllFileWrite)
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                int ret;
                ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                data[2] = op->TargetName;
                if (err == NO_ERROR && read != written)
                    err = ERROR_DISK_FULL;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY: // on a network we must reopen the handle; local access forbids it due to sharing
                {
                    if (out != NULL)
                    {
                        if (wholeFileAllocated)
                            SetEndOfFile(out);     // otherwise on a floppy the remaining bytes would be written
                        HANDLES(CloseHandle(out)); // close the invalid handle
                    }
                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                                               OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                    {
                        CFileOffsetResult outputSize = SalGetFileSizeEx(out);
                        if (!outputSize.Succeeded || // cannot obtain the size
                            outputSize.Value < operationDone ||                     // file is too small
                            wholeFileAllocated && outputSize.Value > fileSize &&
                                outputSize.Value > operationDone + CQuadWord(read, 0) || // pre-allocated file is too large (beyond the reserved size and beyond the written portion including the current block) = extra bytes were appended (allocWholeFileOnStart should be 0 /* need-test */)
                            !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                        { // restart the whole operation
                            HANDLES(CloseHandle(in));
                            HANDLES(CloseHandle(out));
                            DeleteFileUtf8(op->TargetName);
                            copyAgain = TRUE; // goto COPY_AGAIN;
                            return;
                        }
                    }
                    else // still cannot open; problem persists
                    {
                        out = NULL;
                        goto WRITE_ERROR;
                    }
                    break;
                }

                case IDB_SKIPALL:
                    dlgData.SkipAllFileWrite = TRUE;
                case IDB_SKIP:
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                case IDCANCEL:
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }
                }
            }
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            operationDone += CQuadWord(read, 0);
            SetProgressWithoutSuspend(hProgressDlg, CalculateProgressPercent(operationDone, op->Size),
                                      CalculateProgressPercent(totalDone + operationDone, script->TotalSize), dlgData);

            if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
            {                                                              // worker resumes and fetches a fresh copy buffer size
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                script->GetNewBufSize(&limitBufferSize, bufferSize);
            }
        }
        else
        {
        READ_ERROR:

            DWORD err;
            err = GetLastError();
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            if (dlgData.SkipAllFileRead)
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            DWORD retryDelay;
            if (PrepareAutomaticRetry(err, &autoRetryAttemptsSNAP, rokReadOnly,
                                      script->GetCancellationEvent(), &retryDelay))
            { // Read retries have no commit side effect, so the central policy can pace them safely.
                // Record the retry kind, but never the source path that caused it.
                RecordReleaseDiagnosticRetry("copy_network_read");
                script->RecordItemRetry(); // automatic retries need the same correlation history as dialog retries
                if (!WaitForAutomaticRetry(script->GetCancellationEvent(), retryDelay))
                {
                    copyError = TRUE;
                    return;
                }
                goto RETRY_COPY;
            }

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORREADINGFILE);
            data[2] = op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
            {
                // User-approved reports need the retry edge, not the file name or error text.
                RecordReleaseDiagnosticRetry("copy_read");
            RETRY_COPY:

                if (in != NULL)
                    HANDLES(CloseHandle(in)); // close the invalid handle
                // Retrying a move must keep reading the source held by its owner.
                in = OpenCopySourceForRead(op->SourceName, stableMoveSource, FILE_FLAG_SEQUENTIAL_SCAN);
                if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                {
                    CFileOffsetResult inputSize = SalGetFileSizeEx(in);
                    if (!inputSize.Succeeded ||
                        inputSize.Value < operationDone ||
                        !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                    { // cannot obtain the size or the file is too small; restart the whole operation
                        HANDLES(CloseHandle(in));
                        if (wholeFileAllocated)
                            SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                        HANDLES(CloseHandle(out));
                        DeleteFileUtf8(op->TargetName);
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                }
                else // still cannot open; problem persists
                {
                    in = NULL;
                    goto READ_ERROR;
                }
                break;
            }

            case IDB_SKIPALL:
                dlgData.SkipAllFileRead = TRUE;
            case IDB_SKIP:
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            case IDCANCEL:
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }
            }
        }
    }

    if (wholeFileAllocated) // we pre-allocated the complete file layout (meaning the allocation was useful; for example, the file cannot be empty)
    {
        if (operationDone < fileSize) // and the source file shrank
        {
            if (!SetEndOfFile(out)) // trim it here
            {
                written = read = 0;
                goto WRITE_ERROR;
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CFileOffsetResult currentSize = SalGetFileSizeEx(out);
            if (currentSize.Succeeded && currentSize.Value == operationDone)
            { // verify that no extra bytes were appended at the end and that truncation works
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (currentSize.Succeeded)
                {
                    char num1[50];
                    char num2[50];
                    TRACE_E("DoCopyFileLoopOrig(): unable to allocate whole file size before copy operation, please report "
                            "under what conditions this occurs! Error: different file sizes: target="
                            << NumberToStr(num1, currentSize.Value) << " bytes, source=" << NumberToStr(num2, operationDone) << " bytes");
                }
                else
                {
                    TRACE_E("DoCopyFileLoopOrig(): unable to test result of allocation of whole file size before copy operation, please report "
                            "under what conditions this occurs! SalGetFileSizeEx("
                            << op->TargetName << ") error: " << GetErrorText(currentSize.Error));
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                HANDLES(CloseHandle(out));
                out = NULL;
                ClearReadOnlyAttr(op->TargetName); // if it somehow became read-only (should never happen), so we know how to handle it
                if (DeleteFileUtf8(op->TargetName))
                {
                    HANDLES(CloseHandle(in));
                    copyAgain = TRUE; // goto COPY_AGAIN;
                    return;
                }
                else
                {
                    written = read = 0;
                    goto WRITE_ERROR;
                }
            }
        }
    }
}

enum CCopy_BlkState
{
    cbsFree,       // block not in use
    cbsRead,       // reading source file blocks - completed (waiting to be written)
    cbsInProgress, // --- states below mean "waiting for the operation to finish" (completed states are above)
    cbsReading,    // reading source file blocks - requested (in progress)
    cbsTestingEOF, // checking the end of the source file
    cbsWriting,    // writing the block to the target file
    cbsDiscarded,  // attempted to read beyond the end of the source file (should only return error: EOF)
};

enum CCopy_ForceOp
{
    fopNotUsed, // free to read or write as needed
    fopReading, // forced to read
    fopWriting  // forced to write
};

struct CCopy_Context
{
    CAsyncCopyParams* AsyncPar;

    CCopy_ForceOp ForceOp;        // TRUE = must read now, FALSE = must write now
    BOOL ReadingDone;             // TRUE = the source file has been fully read
    CCopy_BlkState BlockState[8]; // block state
    DWORD BlockDataLen[8];        // for each block: expected data (cbsReading + cbsTestingEOF), valid data (cbsWriting)
    CQuadWord BlockOffset[8];     // for each block: block offset in the source/target file (also stored in the 'AsyncPar' OVERLAPPED)
    DWORD BlockTime[8];           // for each block: "time" when the last async operation in this block started
    DWORD CurTime;                // "time" counter for 'BlockTime', handles wrap-around (though unlikely)
    int FreeBlocks;               // current number of free blocks (cbsFree)
    int FreeBlockIndex;           // candidate index of a free block (cbsFree); must be verified
    int ReadingBlocks;            // current number of blocks being read(cbsReading and cbsTestingEOF)
    int WritingBlocks;            // current number of blocks being written (cbsWriting)
    CQuadWord ReadOffset;         // offset for reading the next block from the source file (previous one is already in progress)
    CQuadWord WriteOffset;        // offset for writing the next block to the target file (previous one is already in progress)
    int AutoRetryAttemptsSNAP;    // number of automatic Retry attempts (max 3): SNAP servers sporadically return ERROR_NETNAME_DELETED while reading, Retry button reportedly helps, so trigger it automatically

    // selected DoCopyFileLoopAsync parameters to avoid passing a long argument list everywhere
    CProgressDlgData* DlgData;
    COperation* Op;
    HWND HProgressDlg;
    HANDLE* In;
    HANDLE* Out;
    BOOL WholeFileAllocated;
    COperations* Script;
    CQuadWord* OperationDone;
    const CQuadWord* TotalDone;
    const CQuadWord* LastTransferredFileSize;

    CCopy_Context(CAsyncCopyParams* asyncPar, int numOfBlocks, CProgressDlgData* dlgData, COperation* op,
                  HWND hProgressDlg, HANDLE* in, HANDLE* out, BOOL wholeFileAllocated, COperations* script,
                  CQuadWord* operationDone, const CQuadWord* totalDone, const CQuadWord* lastTransferredFileSize)
    {
        AsyncPar = asyncPar;
        ForceOp = fopNotUsed;
        ReadingDone = FALSE;
        CurTime = 0;
        for (int i = 0; i < _countof(BlockState); i++)
            BlockState[i] = cbsFree;
        memset(BlockDataLen, 0, sizeof(BlockDataLen));
        memset(BlockOffset, 0, sizeof(BlockOffset));
        memset(BlockTime, 0, sizeof(BlockTime));
        FreeBlocks = numOfBlocks;
        FreeBlockIndex = 0;
        ReadingBlocks = 0;
        WritingBlocks = 0;
        ReadOffset.SetUI64(0);
        WriteOffset.SetUI64(0);
        AutoRetryAttemptsSNAP = 0;

        DlgData = dlgData;
        Op = op;
        HProgressDlg = hProgressDlg;
        In = in;
        Out = out;
        WholeFileAllocated = wholeFileAllocated;
        Script = script;
        OperationDone = operationDone;
        TotalDone = totalDone;
        LastTransferredFileSize = lastTransferredFileSize;
    }

    BOOL IsOperationDone(int numOfBlocks)
    {
        return ReadingDone && FreeBlocks == numOfBlocks;
    }

    BOOL StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF);
    BOOL StartWriting(int blkIndex, DWORD* err);
    int FindBlock(CCopy_BlkState state);
    void FreeBlock(int blkIndex);
    void DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex);
    void GetNewFileSize(const char* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize);

    BOOL HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain);
    BOOL HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                          const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset);

    // interrupts any pending asynchronous operations
    void CancelOpPhase1();
    // ensures that all asynchronous operations have really finished + positions the pointer at the end of the contiguous
    // portion of the target file so the file is truncated correctly (before a possible closing and deletion)
    // WARNING: frees unnecessary blocks; only those with data read from the input file remain, and they still
    //          follow WriteOffset (usable for retry)
    void CancelOpPhase2(int errBlkIndex);
    BOOL RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain);
    BOOL RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain, const CQuadWord& allocFileSize,
                           const CQuadWord& maxWriteOffset);
    BOOL HandleSuspModeAndCancel(BOOL* copyError);
};

BOOL DisableLocalBuffering(CAsyncCopyParams* asyncPar, HANDLE file, DWORD* err)
{
    CALL_STACK_MESSAGE1("DisableLocalBuffering()");
    if (DynNtFsControlFile != NULL) // "always true"
    {
        IO_STATUS_BLOCK ioStatus;
        ResetEvent(asyncPar->Overlapped[0].hEvent);
        ULONG status = DynNtFsControlFile(file, asyncPar->Overlapped[0].hEvent, NULL,
                                          0, &ioStatus, 0x00140390 /* IOCTL_LMR_DISABLE_LOCAL_BUFFERING */,
                                          NULL, 0, NULL, 0);
        if (status == STATUS_PENDING) // must wait for the operation to finish; it runs asynchronously
        {
            CALL_STACK_MESSAGE1("DisableLocalBuffering(): STATUS_PENDING");
            WaitForSingleObject(asyncPar->Overlapped[0].hEvent, INFINITE);
            status = ioStatus.Status;
        }
        if (status == 0 /* STATUS_SUCCESS */)
            return TRUE;
        *err = LsaNtStatusToWinError(status);
    }
    else
        *err = ERROR_INVALID_FUNCTION;
    return FALSE;
}

BOOL CCopy_Context::StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "ReadFile: %d 0x%08X 0x%08X", blkIndex, ReadOffset.LoDWord, readSize);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    if (!ReadFile(*In, AsyncPar->Buffers[blkIndex], readSize, NULL,
                  AsyncPar->InitOverlappedWithOffset(blkIndex, ReadOffset)) &&
        GetLastError() != ERROR_IO_PENDING)
    { // a read error occurred; handle it
        *err = GetLastError();
        if (*err == ERROR_HANDLE_EOF) // synchronously reported EOF; convert it to an asynchronously reported EOF
            AsyncPar->SetOverlappedToEOF(blkIndex, ReadOffset);
        else
            return FALSE;
    }
    // if the read was completed synchronously (or via cache, which we cannot detect),
    // we must write something now; otherwise writing may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = opCompleted ? fopWriting : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("ReadFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    BlockOffset[blkIndex] = ReadOffset;
    BlockDataLen[blkIndex] = readSize;
    if (!testEOF) // block was cbsFree before calling this method
    {
        ReadOffset.Value += readSize;
        BlockState[blkIndex] = cbsReading;
    }
    else
        BlockState[blkIndex] = cbsTestingEOF;
    BlockTime[blkIndex] = CurTime++;
    FreeBlocks--;
    ReadingBlocks++;
    return TRUE;
}

BOOL CCopy_Context::StartWriting(int blkIndex, DWORD* err)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "WriteFile: %d 0x%08X 0x%08X", blkIndex, WriteOffset.LoDWord, BlockDataLen[blkIndex]);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    if (!OperationExecutionFileSystem().WriteFile(*Out, AsyncPar->Buffers[blkIndex], BlockDataLen[blkIndex], NULL,
                   AsyncPar->InitOverlappedWithOffset(blkIndex, WriteOffset)) &&
        GetLastError() != ERROR_IO_PENDING)
    { // a write error occurred; handle it
        *err = GetLastError();
        return FALSE;
    }
    // if the write was completed synchronously (or via cache, which we cannot detect),
    // we must read something now; otherwise reading may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = !ReadingDone && opCompleted ? fopReading : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("WriteFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    WriteOffset.Value += BlockDataLen[blkIndex];
    BlockState[blkIndex] = cbsWriting; // block was cbsRead before calling this method
    BlockTime[blkIndex] = CurTime++;
    WritingBlocks++;
    return TRUE;
}

int CCopy_Context::FindBlock(CCopy_BlkState state)
{
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == state)
            return i;
    TRACE_C("CCopy_Context::FindBlock(): unable to find block with required state (" << (int)state << ").");
    return -1; // dead code, only for the compiler
}

void CCopy_Context::FreeBlock(int blkIndex)
{
    if (BlockState[blkIndex] == cbsReading || BlockState[blkIndex] == cbsTestingEOF)
        ReadingBlocks--;
    if (BlockState[blkIndex] == cbsWriting)
        WritingBlocks--;
    BlockState[blkIndex] = cbsFree;
    FreeBlockIndex = blkIndex;
    FreeBlocks++;
}

void CCopy_Context::DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex)
{
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (i == excludeIndex)
            continue;
        CCopy_BlkState st = BlockState[i];
        if ((st == cbsRead || st == cbsReading) && BlockOffset[i] >= fileSize)
        {
            if (st == cbsRead) // discard data read beyond the end of the file; they are useless
                FreeBlock(i);
            else
            {
                BlockState[i] = cbsDiscarded; // reading past the end of the file is pointless; no reason to adjust BlockTime
                ReadingBlocks--;
            }
        }
    }
}

void CCopy_Context::GetNewFileSize(const char* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize)
{
    CFileOffsetResult currentSize = SalGetFileSizeEx(file);
    if (!currentSize.Succeeded)
    {
        TRACE_E("CCopy_Context::GetNewFileSize(): SalGetFileSizeEx(" << fileName << "): unexpected error: " << GetErrorText(currentSize.Error));
        *fileSize = minFileSize;
    }
    else
    {
        *fileSize = currentSize.Value;
        if (*fileSize < minFileSize) // if GetFileSize happened to return a shorter length than already read
            *fileSize = minFileSize;
    }
}

void CCopy_Context::CancelOpPhase1()
{
    if (!CancelIo(*In))
    {
        DWORD err = GetLastError();
        TRACE_E("CCopy_Context::CancelOpPhase1(): CancelIo(IN) failed, error: " << GetErrorText(err));
    }
    if (*Out != NULL && !CancelIo(*Out))
    {
        DWORD err = GetLastError();
        TRACE_E("CCopy_Context::CancelOpPhase1(): CancelIo(OUT) failed, error: " << GetErrorText(err));
    }
}

void CCopy_Context::CancelOpPhase2(int errBlkIndex)
{
    // NOTE: errBlkIndex == -1 for errors when issuing an async reading (no block assigned),
    //       for errors while truncating the file after the main copy loop finished (no block assigned),
    //       or for Cancel in the progress dialog (no block assigned)

    DWORD bytes;
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] > cbsInProgress)
        { // GetOverlappedResult should return results immediately because CancelIo() was called for both files
            if (GetOverlappedResult(BlockState[i] == cbsWriting ? *Out : *In, AsyncPar->GetOverlapped(i), &bytes, TRUE))
            {
                if (BlockState[i] == cbsReading && BlockDataLen[i] == bytes) // fully read -> convert to cbsRead block
                {
                    BlockState[i] = cbsRead;
                    ReadingBlocks--;
                }
                else
                {
                    if (BlockState[i] == cbsWriting && BlockDataLen[i] == bytes) // fully written -> convert to cbsRead block (might write again, so keep it)
                    {
                        BlockState[i] = cbsRead;
                        WritingBlocks--;
                    }
                }
            }
            else
            {
                DWORD err = GetLastError();
                if (i != errBlkIndex &&             // already reporting the error for this block; no need to repeat it in TRACE
                    err != ERROR_OPERATION_ABORTED) // not an error, merely reports cancellation (CancelIo() call)
                {                                   // log issues in other blocks, usually harmless and best ignored
                    TRACE_I("CCopy_Context::CancelOpPhase2(): GetOverlappedResult(" << (BlockState[i] == cbsWriting ? "OUT" : "IN") << ", " << i << ") returned error: " << GetErrorText(err));
                }
            }
            switch (BlockState[i])
            {
            case cbsReading:    // not fully read
            case cbsTestingEOF: // EOF test not finished
            case cbsDiscarded:
                FreeBlock(i);
                break;

            case cbsWriting:                      // unwritten block
                if (WriteOffset > BlockOffset[i]) // lower WriteOffset if needed
                    WriteOffset = BlockOffset[i];
                BlockState[i] = cbsRead; // not fully written but already read -> convert to cbsRead block (might write again, so keep it)
                WritingBlocks--;
                break;
            }
        }
    }

    ReadOffset = WriteOffset; // determine how far we have contiguous data from the offset where writing should resume
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] == cbsRead && BlockOffset[i] == ReadOffset) // block read directly after ReadOffset
        {
            ReadOffset.Value += BlockDataLen[i];
            i = -1; // start the search from the beginning again (with 8 blocks this is affordable, max 36 loop iterations)
        }
    }

    // drop blocks that are already written or too far ahead (not contiguous)
    // so they can be read again later
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == cbsRead && (BlockOffset[i] < WriteOffset || BlockOffset[i] > ReadOffset))
            FreeBlock(i);

    // when deleting the target file, set the file pointer to the end of the written portion;
    // the caller will truncate it with SetEndOfFile before deletion (otherwise zeroes might be written
    // from the end of the written part to the end of the pre-allocated file - pre-allocation is 
    // used to prevent fragmentation)
    if (*Out != NULL) // only if the target file was not closed meanwhile
    {
        CFileOffsetResult seekResult = SalSetFilePointerEx(*Out, WriteOffset, FILE_BEGIN);
        if (!seekResult.Succeeded)
        {
            TRACE_E("CCopy_Context::CancelOpPhase2(): unable to set file pointer in OUT file, error: " << GetErrorText(seekResult.Error));
        }
    }
}

BOOL CCopy_Context::RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain)
{
    if (*In != NULL)
        HANDLES(CloseHandle(*In)); // close the invalid handle
    *In = HANDLES_Q(CreateFileUtf8(Op->SourceName, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (*In != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        CFileOffsetResult inputSize = SalGetFileSizeEx(*In);
        if (inputSize.Succeeded && inputSize.Value >= ReadOffset)
        { // size obtained and the file is large enough
            // if the source is on a network: disable local client-side in-memory caching
            // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
            //
            // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
            // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
            if ((Op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *In, err))
                TRACE_E("CCopy_Context::RetryCopyReadErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << Op->SourceName << ", error: " << GetErrorText(*err));
            // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
            // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
            // from AsyncPar may still be in use)
            if (CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, TRUE))
            {
                ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
                *OperationDone = WriteOffset;
                Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
                SetProgressWithoutSuspend(HProgressDlg, CalculateProgressPercent(*OperationDone, Op->Size),
                                          CalculateProgressPercent(*TotalDone + *OperationDone, Script->TotalSize), *DlgData);
                return TRUE; // success: proceed with retry
            }
        }
        // cannot obtain the size, the file is too small, or the last written part differs from the source -> restart from scratch
        HANDLES(CloseHandle(*In));
        if (WholeFileAllocated)
            SetEndOfFile(*Out); // otherwise on a floppy the remaining bytes would be written
        HANDLES(CloseHandle(*Out));
        DeleteFileUtf8(Op->TargetName);
        *copyAgain = TRUE; // goto COPY_AGAIN;
        return FALSE;
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *In = NULL;
        *errAgain = TRUE; // goto READ_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain)
{
    // NOTE: blkIndex == -1 when the async read request failed (no block assigned)

    CancelOpPhase1();

    while (1)
    {
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        if (*DlgData->CancelWorker)
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileRead)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        DWORD retryDelay;
        if (PrepareAutomaticRetry(err, &AutoRetryAttemptsSNAP, rokReadOnly,
                                  Script->GetCancellationEvent(), &retryDelay))
        { // Asynchronous reads use the same cancelable policy as synchronous reads.
            Script->RecordItemRetry(); // record a new correlated attempt even without a UI prompt
            if (!WaitForAutomaticRetry(Script->GetCancellationEvent(), retryDelay))
            {
                CancelOpPhase2(blkIndex);
                *copyError = TRUE;
                return FALSE;
            }
            ret = IDRETRY;
        }
        else
        {
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORREADINGFILE);
            data[2] = Op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(HProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        }
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyReadErr(&err, copyAgain, &errAgain))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again; repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileRead = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again; repeat the message
        TRACE_C("CCopy_Context::HandleReadingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain,
                                      const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    if (*Out != NULL)
    {
        if (WholeFileAllocated)
            SetEndOfFile(*Out);     // otherwise on a floppy the remaining bytes would be written
        HANDLES(CloseHandle(*Out)); // close the invalid handle
    }
    *Out = HANDLES_Q(CreateFileUtf8(Op->TargetName, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                                OPEN_ALWAYS, AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (*Out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        BOOL ok = TRUE;
        CFileOffsetResult outputSize = SalGetFileSizeEx(*Out);
        if (!outputSize.Succeeded || // cannot obtain the size
            outputSize.Value < WriteOffset ||                                                // file is too small
            WholeFileAllocated && outputSize.Value > allocFileSize && outputSize.Value > maxWriteOffset) // pre-allocated file is too large (greater than the pre-allocated size and the written portion including the current block) = extra bytes appended (allocWholeFileOnStart should be 0 /* need-test */)
        {                                                                        // restart the entire thing
            ok = FALSE;
        }
        // success (file size matches what we need)
        // if the target is on a network: disable local client-side in-memory caching
        // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
        //
        // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
        // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
        if (ok && (Op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *Out, err))
            TRACE_E("CCopy_Context::RetryCopyWriteErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << Op->TargetName << ", error: " << GetErrorText(*err));
        // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
        // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
        // from AsyncPar may still be in use)
        if (!ok || !CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, FALSE))
        {
            HANDLES(CloseHandle(*In));
            HANDLES(CloseHandle(*Out));
            DeleteFileUtf8(Op->TargetName);
            *copyAgain = TRUE; // goto COPY_AGAIN;
            return FALSE;
        }
        ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
        *OperationDone = WriteOffset;
        Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
        SetProgressWithoutSuspend(HProgressDlg, CalculateProgressPercent(*OperationDone, Op->Size),
                                  CalculateProgressPercent(*TotalDone + *OperationDone, Script->TotalSize), *DlgData);
        return TRUE; // success: proceed with retry
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *Out = NULL;
        *errAgain = TRUE; // goto WRITE_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                                     const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    // NOTE: blkIndex == -1 for an error while truncating the file after the main copy loop finishes (it has no assigned block)

    CancelOpPhase1();

    while (1)
    {
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we are supposed to be in suspend mode, wait ...
        if (*DlgData->CancelWorker)
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileWrite)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = LoadStr(IDS_ERRORWRITINGFILE);
        data[2] = Op->TargetName;
        data[3] = GetErrorText(err);
        SendMessage(HProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyWriteErr(&err, copyAgain, &errAgain, allocFileSize, maxWriteOffset))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again, repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileWrite = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again, repeat the message
        TRACE_C("CCopy_Context::HandleWritingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::HandleSuspModeAndCancel(BOOL* copyError)
{
    if (!Script->ChangeSpeedLimit)                                  // if the speed limit cannot change (otherwise this is not a "suitable" place to wait)
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we are supposed to be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        CancelOpPhase1();
        CancelOpPhase2(-1);
        *copyError = TRUE; // goto COPY_ERROR
        return TRUE;
    }
    return FALSE;
}

// Overlapped copy engine (Windows 7+ / network paths). Pipelines up to eight
// blocks: issues reads ahead of writes (at most half the blocks in flight per
// direction), detects EOF via zero-length probe reads, resizes 'fileSize' when
// the source shrinks mid-copy, and drives progress + speed-limit accounting
// from completed writes. All error/cancel/retry decisions delegate to
// CCopy_Context handlers; outputs mirror DoCopyFileLoopOrig ('copyError',
// 'skipCopy', 'copyAgain', truncation of pre-allocated targets). Disables SMB
// client-side caching for network endpoints before starting.
void DoCopyFileLoopAsync(CAsyncCopyParams* asyncPar, HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                         COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated, COperation* op,
                         const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy, HWND hProgressDlg,
                         CQuadWord& operationDone, CQuadWord& fileSize, int bufferSize,
                         int& allocWholeFileOnStart, BOOL& copyAgain, const CQuadWord& lastTransferredFileSize)
{
    CQuadWord allocFileSize = fileSize;
    DWORD err = NO_ERROR;
    DWORD bytes = 0; // helper DWORD - how many bytes were read/written in the block

    // if the source/target is on the network: disable local client-side in-memory caching
    // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
    if ((op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(asyncPar, in, &err))
        TRACE_E("DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << op->SourceName << ", error: " << GetErrorText(err));
    if ((op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(asyncPar, out, &err))
        TRACE_E("DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << op->TargetName << ", error: " << GetErrorText(err));

    // copy loop parameters
    int numOfBlocks = 8;

    // Copy operation context (prevents passing heaps of parameters to helper functions, now context methods)
    CCopy_Context ctx(asyncPar, numOfBlocks, &dlgData, op, hProgressDlg, &in, &out, wholeFileAllocated, script,
                      &operationDone, &totalDone, &lastTransferredFileSize);
    BOOL doCopy = TRUE;
    while (doCopy)
    {
        if (ctx.ForceOp != fopWriting && ctx.FreeBlocks > 0 && !ctx.ReadingDone && ctx.ReadingBlocks < (numOfBlocks + 1) / 2) // read in parallel at most up to half of the blocks
        {
            DWORD toRead = ctx.ReadOffset + CQuadWord(limitBufferSize, 0) <= fileSize ? limitBufferSize : (fileSize - ctx.ReadOffset).LoDWord;
            BOOL testEOF = toRead == 0;
            if (!testEOF || ctx.ReadingBlocks == 0) // data read or EOF test (the EOF test runs only when all reads are finished)
            {
                if (ctx.BlockState[ctx.FreeBlockIndex] != cbsFree)
                    ctx.FreeBlockIndex = ctx.FindBlock(cbsFree);
                // EOF test = read the entire block, otherwise read the usual 'toRead'
                if (ctx.StartReading(ctx.FreeBlockIndex, testEOF ? limitBufferSize : toRead, &err, testEOF))
                    continue; // success (asynchronous read started), try to start another read
                else
                { // error (starting asynchronous read)
                    if (!ctx.HandleReadingErr(-1, err, &copyError, &skipCopy, &copyAgain))
                        return; // cancel/skip(skip-all)/retry-complete
                    continue;   // retry-resume
                }
            }
        }
        // reading has already been issued or is unnecessary, check whether something is completed
        BOOL shouldWait = TRUE; // TRUE = nothing else can be queued asynchronously, we must wait for some pending operation to finish
        BOOL retryCopy = FALSE; // TRUE = after an error we should run Retry = start over from the beginning of the "doCopy" loop
        // two passes are needed only for synchronous writes (we want to mark it
        // completed immediately and not after another read, mainly for progress reporting)
        for (int afterWriting = 0; afterWriting < 2; afterWriting++)
        {
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress && HasOverlappedIoCompleted(asyncPar->GetOverlapped(i)))
                {
                    shouldWait = FALSE; // in the spirit of "keep it simple" (there are situations where it could remain TRUE, but we ignore them)
                    switch (ctx.BlockState[i])
                    {
                    case cbsReading:    // reading the source file into a block - requested (in progress)
                    case cbsTestingEOF: // testing for the end of the source file
                    {
                        BOOL testingEOF = ctx.BlockState[i] == cbsTestingEOF;

#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("READ done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        BOOL res = GetOverlappedResult(in, asyncPar->GetOverlapped(i), &bytes, TRUE);
                        if (testingEOF && res && bytes == 0)
                        {
                            res = FALSE; // MSDN says it should return FALSE and ERROR_HANDLE_EOF at EOF, so enforce that (Novell Netware 6.5 disk returns TRUE)
                            SetLastError(ERROR_HANDLE_EOF);
                        }
                        if (res || GetLastError() == ERROR_HANDLE_EOF)
                        {
                            ctx.AutoRetryAttemptsSNAP = 0;
                            if (!res) // EOF at the beginning of the block (for cbsReading only: EOF can also be before this block and will be handled later in a block with a lower offset)
                            {
                                // when GetOverlappedResult() returns FALSE it does not have to return bytes==0 
                                // (TRACE_C existed for that and crashes happened), so zero the bytes explicitly
                                bytes = 0;
                                if (testingEOF)
                                    ctx.ReadingDone = TRUE; // confirmed end of the source file, stop reading further
                                // we must not force fopWriting (we have not read anything, there is nothing to write), unless this is an EOF test,
                                // let the other asynchronous reads finish, then perform the EOF test, and only then continue with writing
                                ctx.ForceOp = fopNotUsed;
                            }
                            if (bytes < ctx.BlockDataLen[i]) // the file is shorter than expected -> set the new file size
                            {
                                if (!testingEOF || bytes != 0)
                                    ctx.ReadOffset = fileSize = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                if (!testingEOF)
                                    ctx.DiscardBlocksBehindEOF(fileSize, i);
                                if (bytes == 0) // EOF = no data, free the block
                                {
                                    ctx.FreeBlock(i);
                                    if (testingEOF)
                                        doCopy = !ctx.IsOperationDone(numOfBlocks); // verify whether this finished the copy
                                }
                                else
                                    ctx.BlockDataLen[i] = bytes; // pretend we intended to read exactly this much
                            }
                            else
                            {
                                if (testingEOF) // we were looking for EOF and read a full block; the file probably grew significantly, determine the new size
                                {
                                    ctx.ReadOffset = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                    ctx.GetNewFileSize(op->SourceName, in, &fileSize, ctx.ReadOffset);
                                }
                            }
                            if (ctx.BlockState[i] == cbsReading || ctx.BlockState[i] == cbsTestingEOF)
                            {
                                ctx.ReadingBlocks--;
                                ctx.BlockState[i] = cbsRead;
                            }
                        }
                        else // error
                        {
                            if (!ctx.HandleReadingErr(i, GetLastError(), &copyError, &skipCopy, &copyAgain))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                        }
                        break;
                    }

                    case cbsWriting: // writing a block to the target file
                    {
#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("WRITE done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        BOOL res = GetOverlappedResult(out, asyncPar->GetOverlapped(i), &bytes, TRUE);
                        if (!res || bytes != ctx.BlockDataLen[i]) // error
                        {
                            err = GetLastError();
                            if (err == NO_ERROR && bytes != ctx.BlockDataLen[i])
                                err = ERROR_DISK_FULL;
                            CQuadWord maxWriteOffset = ctx.WriteOffset;
                            if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                            break;
                        }

                        if (ctx.HandleSuspModeAndCancel(&copyError))
                            return; // cancel

                        script->AddBytesToSpeedMetersAndTFSandPS(bytes, FALSE, bufferSize, &limitBufferSize);

                        if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        operationDone += CQuadWord(bytes, 0);
                        SetProgressWithoutSuspend(hProgressDlg, CalculateProgressPercent(operationDone, op->Size),
                                                  CalculateProgressPercent(totalDone + operationDone, script->TotalSize), dlgData);

                        if (script->ChangeSpeedLimit)                                  // the speed limit is likely to change, this is a "suitable" place to wait until the
                        {                                                              // worker resumes so we can get the buffer size for copying again
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            script->GetNewBufSize(&limitBufferSize, bufferSize);
                        }

                        // break; // the break is intentionally missing here...
                    }
                    case cbsDiscarded: // reading the source file beyond its end (should only return the EOF error)
                    {
                        ctx.FreeBlock(i);
                        doCopy = !ctx.IsOperationDone(numOfBlocks);
                        break;
                    }
                    }
                }
                if (!doCopy || retryCopy)
                    break;
            }
            if (!doCopy || retryCopy)
                break;

            // we have read data into blocks, check whether they can be written to the target file;
            // written/discarded blocks were freed (we will read into them again at the top of the loop)
            CQuadWord nextReadBlkOffset; // lowest offset of a skipped cbsRead block
            do
            {
                nextReadBlkOffset.SetUI64(0);
                // write in parallel at most up to half of the blocks
                for (int i = 0; ctx.ForceOp != fopReading && i < _countof(ctx.BlockState) && ctx.WritingBlocks < (numOfBlocks + 1) / 2; i++)
                {
                    if (ctx.BlockState[i] == cbsRead)
                    {
                        if (ctx.WriteOffset == ctx.BlockOffset[i])
                        {
                            if (!ctx.StartWriting(i, &err))
                            { // error (asynchronous write)
                                CQuadWord maxWriteOffset = ctx.WriteOffset + CQuadWord(ctx.BlockDataLen[i], 0);
                                if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                    return;       // cancel/skip(skip-all)/retry-complete
                                retryCopy = TRUE; // retry-resume
                                break;
                            }
                        }
                        else
                        {
                            if (nextReadBlkOffset.Value == 0 || ctx.BlockOffset[i] < nextReadBlkOffset)
                                nextReadBlkOffset = ctx.BlockOffset[i];
                        }
                    }
                } // we have another cbsRead block adjoining the written portion of the target file -> keep writing
            } while (!retryCopy && ctx.ForceOp != fopReading && nextReadBlkOffset.Value != 0 && nextReadBlkOffset == ctx.WriteOffset &&
                     ctx.WritingBlocks < (numOfBlocks + 1) / 2); // write in parallel at most up to half of the blocks
            if (retryCopy || ctx.ForceOp != fopReading)
                break; // we are going to Retry or the write was not synchronous (finished in about 0 ms) or we only write now, so two passes are pointless
        }
        if (!doCopy || retryCopy)
            continue;

        if (shouldWait) // another pass through the loop is pointless, no chance to start a new read or write, wait
        {               // for the oldest asynchronous operation to finish
            DWORD oldestBlockTime = 0;
            int oldestBlockIndex = -1;
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress)
                {
                    DWORD ti = ctx.CurTime - ctx.BlockTime[i];
                    if (oldestBlockTime < ti)
                    {
                        oldestBlockTime = ti;
                        oldestBlockIndex = i;
                    }
                }
            }
            if (oldestBlockIndex == -1)
                TRACE_C("DoCopyFileLoopAsync(): unexpected situation: unable to find any block with operation in progress!");

#ifdef ASYNC_COPY_DEBUG_MSG
            TRACE_I("wait: GetOverlappedResult: " << oldestBlockIndex << (ctx.BlockState[oldestBlockIndex] == cbsWriting ? " WRITE" : " READ"));
#endif // ASYNC_COPY_DEBUG_MSG

            // wait for the oldest pending asynchronous operation to complete here
            // for the source file ('in') this covers: cbsReading, cbsTestingEOF, and cbsDiscarded
            // for the target file ('out') this covers only cbsWriting
            GetOverlappedResult(ctx.BlockState[oldestBlockIndex] == cbsWriting ? out : in,
                                asyncPar->GetOverlapped(oldestBlockIndex), &bytes, TRUE);

#ifdef ASYNC_COPY_DEBUG_MSG
            char sss[1000];
            sprintf(sss, "wait done: 0x%08X 0x%08X", ctx.BlockOffset[oldestBlockIndex].LoDWord, bytes);
            TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

            if (ctx.HandleSuspModeAndCancel(&copyError))
                return; // cancel
        }
    }
    if (ctx.ReadOffset != ctx.WriteOffset || operationDone != ctx.WriteOffset)
        TRACE_C("DoCopyFileLoopAsync(): unexpected situation after copy: ReadOffset != WriteOffset || operationDone != ctx.WriteOffset");

    if (wholeFileAllocated) // we allocated the full size of the file (meaning the allocation made sense, e.g. the file cannot be empty)
    {
        if (operationDone < allocFileSize) // and the source file shrank, trim it here
        {
            while (1)
            {
                CFileOffsetResult seekResult = SalSetFilePointerEx(out, ctx.WriteOffset, FILE_BEGIN);
                if (!seekResult.Succeeded ||
                    seekResult.Value != ctx.WriteOffset ||
                    !SetEndOfFile(out))
                {
                    DWORD err2 = !seekResult.Succeeded ? seekResult.Error : GetLastError();
                    if (seekResult.Succeeded && seekResult.Value != ctx.WriteOffset)
                        err2 = ERROR_INVALID_FUNCTION; // successful seek, but it reached an unexpected offset
                    if (!ctx.HandleWritingErr(-1, err2, &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                        return; // cancel/skip(skip-all)/retry-complete
                                // retry-resume
                }
                else
                    break; // success
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CFileOffsetResult currentSize = SalGetFileSizeEx(out);
            if (currentSize.Succeeded && currentSize.Value == operationDone)
            { // verify that no extra bytes were appended to the end of the file + that we can truncate the file
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (currentSize.Succeeded)
                {
                    char num1[50];
                    char num2[50];
                    TRACE_E("DoCopyFileLoopAsync(): unable to allocate whole file size before copy operation, please report "
                            "under what conditions this occurs! Error: different file sizes: target="
                            << NumberToStr(num1, currentSize.Value) << " bytes, source=" << NumberToStr(num2, operationDone) << " bytes");
                }
                else
                {
                    TRACE_E("DoCopyFileLoopAsync(): unable to test result of allocation of whole file size before copy operation, please report "
                            "under what conditions this occurs! SalGetFileSizeEx("
                            << op->TargetName << ") error: " << GetErrorText(currentSize.Error));
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                while (1)
                {
                    HANDLES(CloseHandle(out));
                    out = NULL;
                    ClearReadOnlyAttr(op->TargetName); // in case it was created as read-only (should never happen) so we can handle it
                    if (DeleteFileUtf8(op->TargetName))
                    {
                        HANDLES(CloseHandle(in));
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                    else
                    {
                        if (!ctx.HandleWritingErr(-1, GetLastError(), &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                            return; // cancel/skip(skip-all)/retry-complete
                                    // retry-resume
                    }
                }
            }
        }
    }
}
