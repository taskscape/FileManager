// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "common\\monotonic_time.h"

#include "cfgdlg.h"
#include "common/scoped_native_resources.h"
#include "file_operation_filesystem.h"
#include "common/scoped_kernel_handle.h"
#include "operation_result.h"
#include "retry_policy.h"
#include "worker.h"
#include "execlog.h"
#include "release_diagnostics.h"
// Helpers extracted from this translation unit into their own files.
#include "file_attributes.h"
#include "security_helpers.h"
#include "async_copy_internals.h"

#include <Aclapi.h>
#include <Ntsecapi.h>
#include <bcrypt.h>
#include <shobjidl.h>
#include <strsafe.h>

#pragma comment(lib, "bcrypt.lib")

// CreateFileUtf8 / DeleteFileUtf8 / SetFileAttributesUtf8 / RemoveDirectoryUtf8
// are globally declared in common/strutils.h and defined in common/strutils.cpp.
int GetOptimalSyncCopyBufferSize(COperations* script, DWORD opFlags);

void InitWorker()
{
    if (NtDLL != NULL) // "always true"
    {
        DynNtQueryInformationFile = (NTQUERYINFORMATIONFILE)GetProcAddress(NtDLL, "NtQueryInformationFile"); // has no header
        DynNtFsControlFile = (NTFSCONTROLFILE)GetProcAddress(NtDLL, "NtFsControlFile");                      // has no header
    }
}

void ReleaseWorker()
{
    DynNtQueryInformationFile = NULL;
    DynNtFsControlFile = NULL;
}


void SetProgressDialog(HWND hProgressDlg, CProgressData* data, CProgressDlgData& dlgData)
{                                                              // wait for the response; the dialog must be updated
    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (!*dlgData.CancelWorker)                                // we need to stop the main thread
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, (WPARAM)data, 0);
}

int CalculateProgressPercent(const CQuadWord& progressCurrent, const CQuadWord& progressTotal)
{
    return progressCurrent >= progressTotal ? (progressTotal.Value == 0 ? 0 : 1000) : (int)((progressCurrent * CQuadWord(1000, 0)) / progressTotal).Value;
}

void SetProgress(HWND hProgressDlg, int operation, int summary, CProgressDlgData& dlgData)
{                                                              // notify about the change and continue without waiting for a reply
    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (!*dlgData.CancelWorker &&
        (*dlgData.OperationProgress != operation || *dlgData.SummaryProgress != summary))
    {
        *dlgData.OperationProgress = operation;
        *dlgData.SummaryProgress = summary;
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, 0, 0);
    }
}

void SetProgressWithoutSuspend(HWND hProgressDlg, int operation, int summary, CProgressDlgData& dlgData)
{ // notify about the change and continue without waiting for a reply
    if (!*dlgData.CancelWorker &&
        (*dlgData.OperationProgress != operation || *dlgData.SummaryProgress != summary))
    {
        *dlgData.OperationProgress = operation;
        *dlgData.SummaryProgress = summary;
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, 0, 0);
    }
}

BOOL GetDirTime(const char* dirName, FILETIME* ftModified);
BOOL DoCopyDirTime(HWND hProgressDlg, const char* targetName, FILETIME* modified, CProgressDlgData& dlgData, BOOL quiet);


void GetFileOverwriteInfo(char* buff, int buffLen, HANDLE file, const char* fileName, FILETIME* fileTime, BOOL* getTimeFailed)
{
    FILETIME lastWrite;
    SYSTEMTIME st;
    FILETIME ft;
    char date[50], time[50];
    if (!GetFileTime(file, NULL, NULL, &lastWrite) ||
        !FileTimeToLocalFileTime(&lastWrite, &ft) ||
        !FileTimeToSystemTime(&ft, &st))
    {
        if (getTimeFailed != NULL)
            *getTimeFailed = TRUE;
        date[0] = 0;
        time[0] = 0;
    }
    else
    {
        if (fileTime != NULL)
            *fileTime = ft;
        if (FormatUserDateTimeUtf8(&st, 0, time, _countof(time), FALSE) == 0)
            _snprintf_s(time, _countof(time), _TRUNCATE, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, date, _countof(date), TRUE) == 0)
            _snprintf_s(date, _countof(date), _TRUNCATE, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    }

    char attr[30];
    // Keep the fixed diagnostic attribute suffix valid even if its prefix is ever changed.
    if (FAILED(StringCchCopyA(attr, _countof(attr), ", ")))
        attr[0] = 0;
    DWORD attrs = SalGetFileAttributes(fileName);
    if (attrs != 0xFFFFFFFF)
        GetAttrsString(attr + 2, attrs);
    if (strlen(attr) == 2)
        attr[0] = 0;

    char number[50];
    CQuadWord size;
    DWORD err;
    if (SalGetFileSize(file, size, err))
        NumberToStr(number, size);
    else
        number[0] = 0; // error - size unknown

    _snprintf_s(buff, buffLen, _TRUNCATE, "%s, %s, %s%s", number, date, time, attr);
}

void GetDirInfo(char* buffer, int bufferLen, const char* dir)
{
    if (bufferLen <= 0)
        return;
    const char* dirFindFirst = dir;
    char dirFindFirstCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(dirFindFirst, dirFindFirstCopy);

    BOOL ok = FALSE;
    FILETIME lastWrite;
    if (NameEndsWithBackslash(dirFindFirst))
    { // FindFirstFile fails for a dir ending with a backslash (used for invalid directory names),
        // so in this situation we handle it through CreateFile and GetFileTime
        CStrP dirFindFirstW(ConvertAllocUtf8ToWide(dirFindFirst, -1));
        HANDLE file = dirFindFirstW != NULL
                          ? HANDLES_Q(CreateFileW(dirFindFirstW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL))
                          : INVALID_HANDLE_VALUE;
        if (file != INVALID_HANDLE_VALUE)
        {
            if (GetFileTime(file, NULL, NULL, &lastWrite))
                ok = TRUE;
            HANDLES(CloseHandle(file));
        }
    }
    else
    {
        WIN32_FIND_DATAW data;
        CStrP dirFindFirstW(ConvertAllocUtf8ToWide(dirFindFirst, -1));
        HANDLE find = dirFindFirstW != NULL ? HANDLES_Q(FindFirstFileW(dirFindFirstW, &data)) : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE)
        {
            HANDLES(FindClose(find));
            lastWrite = data.ftLastWriteTime;
            ok = TRUE;
        }
    }
    if (ok)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&lastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            char date[50], time[50];
            if (FormatUserDateTimeUtf8(&st, 0, time, _countof(time), FALSE) == 0)
                _snprintf_s(time, _countof(time), _TRUNCATE, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, date, _countof(date), TRUE) == 0)
                _snprintf_s(date, _countof(date), _TRUNCATE, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);

            _snprintf_s(buffer, bufferLen, _TRUNCATE, "%s, %s", date, time);
        }
        else
            _snprintf_s(buffer, bufferLen, _TRUNCATE, "%s, %s", LoadStr(IDS_INVALID_DATEORTIME), LoadStr(IDS_INVALID_DATEORTIME));
    }
    else
        buffer[0] = 0;
}

BOOL IsDirectoryEmpty(const char* name) // directories/subdirectories contain no files
{
    char dir[MAX_PATH + 5];
    int len = (int)strlen(name);
    if (len <= 0 || len >= _countof(dir) - 2)
        return FALSE;
    memcpy(dir, name, len);
    dir[len] = 0;
    if (dir[len - 1] != '\\')
        dir[len++] = '\\';
    char* end = dir + len;
    *end++ = '*';
    *end = 0;

    WIN32_FIND_DATAW fileData;
    CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
    // The search handle is closed by the wrapper even on the early "not empty" returns.
    CScopedFindHandle search(dirW != NULL ? HANDLES_Q(FindFirstFileW(dirW, &fileData)) : INVALID_HANDLE_VALUE);
    if (search.IsValid())
    {
        do
        {
            if (fileData.cFileName[0] == 0 ||
                fileData.cFileName[0] == L'.' && (fileData.cFileName[1] == 0 ||
                                                  fileData.cFileName[1] == L'.' && fileData.cFileName[2] == 0))
                continue;

            if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                int remaining = (int)(_countof(dir) - (end - dir));
                if (remaining <= 1 || ConvertWideToUtf8(fileData.cFileName, -1, end, remaining) == 0)
                    continue;
                if (!IsDirectoryEmpty(dir)) // the subdirectory is not empty
                    return FALSE;
            }
            else
            {
                return FALSE; // a file exists here
            }
        } while (FindNextFileW(search.Get(), &fileData));
    }
    return TRUE;
}

BOOL CheckFileOrDirADS(const char* fileName, BOOL isDir, CQuadWord* adsSize, wchar_t*** streamNames,
                       int* streamNamesCount, BOOL* lowMemory, DWORD* winError,
                       DWORD bytesPerCluster, CQuadWord* adsOccupiedSpace,
                       BOOL* onlyDiscardableStreams)
{
    if (adsSize != NULL)
        adsSize->SetUI64(0);
    if (adsOccupiedSpace != NULL)
        adsOccupiedSpace->SetUI64(0);
    if (streamNames != NULL)
        *streamNames = NULL;
    if (streamNamesCount != NULL)
        *streamNamesCount = 0;
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (winError != NULL)
        *winError = NO_ERROR;
    if (onlyDiscardableStreams != NULL)
        *onlyDiscardableStreams = TRUE;

    if (DynNtQueryInformationFile != NULL) // "always true"
    {
        // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
        // trims the spaces/dots and works with a different path
        const char* fileNameCrFile = fileName;
        char fileNameCrFileCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);

        HANDLE file = HANDLES_Q(CreateFileUtf8(fileNameCrFile, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           NULL, OPEN_EXISTING,
                                           isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL));
        if (file == INVALID_HANDLE_VALUE)
        {
            if (winError != NULL)
                *winError = GetLastError();
            return FALSE;
        }

        // get stream info
        NTSTATUS uStatus;
        IO_STATUS_BLOCK ioStatus;
        BYTE buffer[65535]; // Windows XP cannot handle more than 65535 (no idea why)
        uStatus = DynNtQueryInformationFile(file, &ioStatus, buffer, sizeof(buffer), FileStreamInformation);
        HANDLES(CloseHandle(file));
        if (uStatus != 0 /* anything other than success is an error (including warnings) */)
        {
            if (winError != NULL)
            {
                if (uStatus == STATUS_BUFFER_OVERFLOW)
                    *winError = ERROR_INSUFFICIENT_BUFFER;
                else
                    *winError = LsaNtStatusToWinError(uStatus);
            }
            return FALSE;
        }

        TDirectArray<wchar_t*>* streamNamesAux = NULL;
        if (streamNames != NULL)
        {
            streamNamesAux = new TDirectArray<wchar_t*>(10, 100);
            if (streamNamesAux == NULL)
            {
                if (lowMemory != NULL)
                    *lowMemory = TRUE;
                TRACE_E(LOW_MEMORY);
                return FALSE;
            }
        }

        // iterate through the streams
        PFILE_STREAM_INFORMATION psi = (PFILE_STREAM_INFORMATION)buffer;
        BOOL ret = FALSE;
        BOOL lowMem = FALSE;
        if (ioStatus.Information > 0) // check whether we obtained any data at all
        {
            while (1)
            {
                if (psi->NameLength != 7 * 2 || _memicmp(psi->Name, L"::$DATA", 7 * 2)) // ignore default stream
                {
                    ret = TRUE;
                    if (adsSize != NULL)
                        *adsSize += CQuadWord(psi->Size.LowPart, psi->Size.HighPart); // sum of the total size of all alternate data streams
                    if (adsOccupiedSpace != NULL && bytesPerCluster != 0)
                    {
                        CQuadWord fileSize(psi->Size.LowPart, psi->Size.HighPart);
                        *adsOccupiedSpace += fileSize - ((fileSize - CQuadWord(1, 0)) % CQuadWord(bytesPerCluster, 0)) +
                                             CQuadWord(bytesPerCluster - 1, 0);
                    }

                    if (onlyDiscardableStreams != NULL)
                    {                                                                                                                      // if an ADS appears that is unknown or indispensable, switch 'onlyDiscardableStreams' to FALSE
                        if ((psi->NameLength < 29 * 2 || _memicmp(psi->Name, L":\x05Q30lsldxJoudresxAaaqpcawXc:", 29 * 2) != 0) &&         // Win2K thumbnail in an ADS: 5952 bytes (depends on JPEG compression)
                            (psi->NameLength < 40 * 2 || _memicmp(psi->Name, L":{4c8cc155-6c1e-11d1-8e41-00c04fb9386d}:", 40 * 2) != 0) && // Win2K thumbnail in an ADS: 0 bytes
                            (psi->NameLength < 9 * 2 || _memicmp(psi->Name, L":KAVICHS:", 9 * 2) != 0))                                    // Kaspersky antivirus: 36/68 bytes
                        {
                            *onlyDiscardableStreams = FALSE;
                        }
                    }

                    if (streamNamesAux != NULL) // collecting Unicode names of alternate data streams
                    {
                        wchar_t* str = (wchar_t*)malloc(psi->NameLength + 2);
                        if (str != NULL)
                        {
                            memcpy(str, psi->Name, psi->NameLength);
                            str[psi->NameLength / 2] = 0;
                            streamNamesAux->Add(str);
                            if (!streamNamesAux->IsGood())
                            {
                                free(str);
                                streamNamesAux->ResetState();
                                if (lowMemory != NULL)
                                    *lowMemory = TRUE;
                                lowMem = TRUE;
                                break;
                            }
                        }
                        else
                        {
                            if (lowMemory != NULL)
                                *lowMemory = TRUE;
                            lowMem = TRUE;
                            TRACE_E(LOW_MEMORY);
                            break;
                        }
                    }
                    else
                    {
                        if (adsSize == NULL && adsOccupiedSpace == NULL && onlyDiscardableStreams == NULL)
                            break; // nothing else to find out (no names, stream sizes, or only-discardable-streams collected)
                    }
                }
                if (psi->NextEntry == 0)
                    break;
                psi = (PFILE_STREAM_INFORMATION)((BYTE*)psi + psi->NextEntry); // move to next item
            }
        }
        if (streamNamesAux != NULL)
        {
            if (lowMem || !ret) // lack of memory or no ADS, release all names
            {
                int i;
                for (i = 0; i < streamNamesAux->Count; i++)
                    free(streamNamesAux->At(i));
            }
            else // everything OK, pass the names to the caller
            {
                if (streamNamesCount != NULL)
                    *streamNamesCount = streamNamesAux->Count;
                *streamNames = streamNamesAux->GetData();
                streamNamesAux->DetachArray();
            }
            delete streamNamesAux;
        }
        return ret;
    }
    return FALSE;
}

void CorrectCaseOfTgtName(char* tgtName, BOOL dataRead, WIN32_FIND_DATAW* data)
{
    if (!dataRead)
    {
        CStrP tgtNameW(ConvertAllocUtf8ToWide(tgtName, -1));
        if (tgtNameW == NULL)
            return;
        HANDLE find = HANDLES_Q(FindFirstFileW(tgtNameW, data));
        if (find != INVALID_HANDLE_VALUE)
            HANDLES(FindClose(find));
        else
            return; // failed to read data for the target file; abort
    }
    char dataName[MAX_PATH];
    if (ConvertWideToUtf8(data->cFileName, -1, dataName, _countof(dataName)) == 0)
        return;
    int len = (int)strlen(dataName);
    int tgtNameLen = (int)strlen(tgtName);
    if (tgtNameLen >= len && StrICmp(tgtName + tgtNameLen - len, dataName) == 0)
        memcpy(tgtName + tgtNameLen - len, dataName, len);
}

void RecordSkippedFileProgressState(COperation* op, CQuadWord& lastTransferredFileSize,
                               COperations* script, const CQuadWord& pSize)
{
    if (op->FileSize < COPY_MIN_FILE_SIZE)
    {
        lastTransferredFileSize += op->FileSize;                      // file size
        if (op->Size > COPY_MIN_FILE_SIZE)                            // should always be at least COPY_MIN_FILE_SIZE, but be safe...
            lastTransferredFileSize += op->Size - COPY_MIN_FILE_SIZE; // add the ADS size
    }
    else
        lastTransferredFileSize += op->Size; // file size + ADS
    script->SetTFSandProgressSize(lastTransferredFileSize, pSize);
}

// Outcome of the post-copy durable-commit phase. The caller translates these
// values onto its legacy control-flow targets (COPY_ERROR_2 / SKIP_COPY /
// COPY_AGAIN / continue), so this phase can live outside the goto mesh.
enum ECopyCommitPhase
{
    cpcrProceed,
    cpcrCancel,
    cpcrSkip,
    cpcrRestart,
};

// Durable-copy verification plus, for transactional overwrites, the
// ReplaceFile-based commit and stale-stream cleanup extracted from DoCopyFile.
// Former inline goto exits map onto the returned phase; retry loops stay inside.
static ECopyCommitPhase VerifyAndCommitCopyTarget(COperation* op, HWND hProgressDlg,
                                                  const char* requestedTargetName,
                                                  COperations* script, CProgressDlgData& dlgData,
                                                  BOOL transactionalTarget, BOOL* transactionalTargetCommitted,
                                                  BOOL copyADS, BOOL* suspiciousIoRetry)
{
    DWORD verificationError = NO_ERROR;
    COperationResult verificationResult = VerifyDurableCopyCommit(op->TargetName, op->FileSize);
    while (!verificationResult.ToLegacyBool(&verificationError))
    {
        TRACE_I("DoCopyFile(): durable copy commit verification failed for " << op->TargetName << ": " << GetErrorText(verificationError));
        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        if (*dlgData.CancelWorker)
            return cpcrCancel;

        if (dlgData.SkipAllFileWrite)
            return cpcrSkip;

        int ret = IDCANCEL;
        char diagnosticSummary[2 * 3 * MAX_PATH + 512];
        char diagnosticText[2 * 3 * MAX_PATH + 768];
        verificationResult.BuildDiagnosticSummary(diagnosticSummary, _countof(diagnosticSummary));
        _snprintf_s(diagnosticText, _countof(diagnosticText), _TRUNCATE,
                    "%s\r\n\r\nDiagnostic (copy with Ctrl+C):\r\n%s",
                    GetErrorText(verificationError), diagnosticSummary);
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = LoadStr(IDS_ERRORWRITINGFILE);
        data[2] = op->TargetName;
        data[3] = diagnosticText;
        SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        switch (ret)
        {
        case IDRETRY:
            if (suspiciousIoRetry != NULL)
                *suspiciousIoRetry = TRUE;
            ClearReadOnlyAttr(op->TargetName);
            if (DeleteFileUtf8(op->TargetName) == 0)
            {
                DWORD err = GetLastError();
                verificationResult.AppendCleanupError(orcpDeleteUnverifiedTarget, err, op->TargetName);
                TRACE_E("DoCopyFile(): Unable to remove unverified copy target: " << op->TargetName << ", error: " << GetErrorText(err));
            }
            return cpcrRestart;

        case IDB_SKIPALL:
            dlgData.SkipAllFileWrite = TRUE;
        case IDB_SKIP:
            return cpcrSkip;

        case IDCANCEL:
            return cpcrCancel;
        }
    }

    if (transactionalTarget)
    {
        DWORD err = NO_ERROR;
        if (!script->JournalMarkTemporaryReady())
        {
            return cpcrCancel; // ERROR_WRITE_FAULT path of the former goto COPY_ERROR_2
        }
        // The dialog keeps its legacy BOOL/error contract while the
        // worker preserves the phase and temporary-target effect for migration.
        COperationResult commitResult = CommitTransactionalTargetFile(requestedTargetName, op->TargetName,
                                                                       op->TargetIdentity);
        while (!commitResult.ToLegacyBool(&err))
        {
            TRACE_I("DoCopyFile(): unable to commit transactional overwrite of " << requestedTargetName << ": " << GetErrorText(err));
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return cpcrCancel;

            if (dlgData.SkipAllFileWrite)
                return cpcrSkip;

            int ret = IDCANCEL;
            char diagnosticSummary[2 * 3 * MAX_PATH + 512];
            char diagnosticText[2 * 3 * MAX_PATH + 768];
            commitResult.BuildDiagnosticSummary(diagnosticSummary, _countof(diagnosticSummary));
            _snprintf_s(diagnosticText, _countof(diagnosticText), _TRUNCATE,
                        "%s\r\n\r\nDiagnostic (copy with Ctrl+C):\r\n%s",
                        GetErrorText(err), diagnosticSummary);
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
            data[2] = (char*)requestedTargetName; // dialog payload is char* by legacy contract; read-only here
            data[3] = diagnosticText;
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                commitResult = CommitTransactionalTargetFile(requestedTargetName, op->TargetName,
                                                             op->TargetIdentity);
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllFileWrite = TRUE;
            case IDB_SKIP:
                return cpcrSkip;

            case IDCANCEL:
                return cpcrCancel;
            }
        }
        *transactionalTargetCommitted = TRUE;
        if (copyADS)
        {
            // The commit merged the replaced file's streams into the
            // result; only the source's streams belong on it.
            RemoveCommittedStreamsMissingFromSource(op->SourceName, requestedTargetName);
        }
    }
    return cpcrProceed;
}

// "Overwrite Older" optimization extracted from DoCopyFile: when the existing
// target is not older than the source and the user enabled the option, the copy
// is resolved without transferring data. Returns TRUE when the caller must stop
// processing this file (skip was reported); '*tgtNameCaseCorrected' and
// 'dataOut' mirror the state the inline block previously kept across the probe.
static BOOL SkipCopyIfTargetNotOlder(COperation* op, HWND hProgressDlg, COperations* script,
                                     CQuadWord& totalDone, CProgressDlgData& dlgData, BOOL* skip,
                                     BOOL invalidSrcName, BOOL invalidTgtName,
                                     BOOL* tgtNameCaseCorrected, WIN32_FIND_DATAW* dataOut)
{
    if ((op->OpFlags & OPFL_OVERWROLDERALRTESTED) != 0 ||
        invalidSrcName || invalidTgtName || !script->OverwriteOlder)
        return FALSE;

    HANDLE find;
    CStrP targetNameW(ConvertAllocUtf8ToWide(op->TargetName, -1));
    find = targetNameW != NULL ? HANDLES_Q(FindFirstFileW(targetNameW, dataOut)) : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    HANDLES(FindClose(find));

    CorrectCaseOfTgtName(op->TargetName, TRUE, dataOut);
    *tgtNameCaseCorrected = TRUE;

    const char* tgtName = SalPathFindFileName(op->TargetName);
    char outName[MAX_PATH];
    if (ConvertWideToUtf8(dataOut->cFileName, -1, outName, _countof(outName)) == 0)
        outName[0] = 0;
    if (!(StrICmp(tgtName, outName) == 0 &&                           // ensure it is not just a DOS-name match (that would change the DOS-name instead of overwriting)
          (dataOut->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)) // ensure it is not a directory (overwrite-older cannot help there)
        return FALSE;

    WIN32_FIND_DATAW dataIn;
    CStrP sourceNameW(ConvertAllocUtf8ToWide(op->SourceName, -1));
    find = sourceNameW != NULL ? HANDLES_Q(FindFirstFileW(sourceNameW, &dataIn)) : INVALID_HANDLE_VALUE;
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    HANDLES(FindClose(find));

    // truncate times to seconds (different file systems store timestamps with different precision, leading to "differences" even between "identical" times)
    *(unsigned __int64*)&dataIn.ftLastWriteTime = *(unsigned __int64*)&dataIn.ftLastWriteTime - (*(unsigned __int64*)&dataIn.ftLastWriteTime % 10000000);
    *(unsigned __int64*)&dataOut->ftLastWriteTime = *(unsigned __int64*)&dataOut->ftLastWriteTime - (*(unsigned __int64*)&dataOut->ftLastWriteTime % 10000000);

    if ((dataIn.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||             // verify the source is still a file
        CompareFileTime(&dataIn.ftLastWriteTime, &dataOut->ftLastWriteTime) > 0) // source file is newer than the target file - perform the copy operation
        return FALSE;

    CQuadWord fileSize(op->FileSize);
    if (fileSize < COPY_MIN_FILE_SIZE)
    {
        if (op->Size > COPY_MIN_FILE_SIZE)             // should always be at least COPY_MIN_FILE_SIZE, but play it safe...
            fileSize += op->Size - COPY_MIN_FILE_SIZE; // add the size of ADS streams
    }
    else
        fileSize = op->Size;
    totalDone += op->Size;
    script->AddBytesToTFSandSetProgressSize(fileSize, totalDone);

    SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
    if (skip != NULL)
        *skip = TRUE;
    return TRUE;
}

// Copies one file ('op') from source to target with full error handling.
// Control flow is a retry state machine built on gotos; phase map in order:
//   1. SkipCopyIfTargetNotOlder probe - "Overwrite Older" may resolve the
//      whole operation without any data transfer.
//   2. COPY_AGAIN: buffer-size selection (async tiers vs. sync optimum),
//     source open; re-entered after restart-worthy failures.
//   3. OPEN_TGT_FILE loop: creates the target either as a transactional
//     sibling reservation or directly via SalCreateFileEx; retries without
//     the Encrypted attribute when the filesystem refuses encryption
//     (SKIP_/CANCEL_ENCNOTSUP paths consult the user).
//   4. COPY: pre-allocation of the whole target size (fragmentation
//     avoidance, with delete-and-retry on failure), then delegates to
//     DoCopyFileLoopAsync / DoCopyFileLoopOrig; their outcomes map to
//     COPY_ERROR / SKIP_COPY / copyAgain -> COPY_AGAIN.
//   5. Post-copy metadata: timestamps, ADS streams, attribute verification,
//     NTFS security, then VerifyAndCommitCopyTarget (durability check +
//     transactional ReplaceFile commit + stale-stream cleanup).
//   6. Overwrite resolution when the target exists (confirm dialogs,
//     hidden/system confirmation, delete-before-overwrite workaround for
//     Samba, NORMAL_ERROR retry loop).
// 'skip' reports user Skip; 'suspiciousIoRetry' tells DoMoveFile to verify
// content by SHA-256 before deleting its source; asyncPar carries overlapped
// I/O state across files of one script. Returns FALSE only on cancel/error.
BOOL DoCopyFile(COperation* op, HWND hProgressDlg, void* buffer,
                COperations* script, CQuadWord& totalDone,
                DWORD clearReadonlyMask, BOOL* skip, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CProgressDlgData& dlgData, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL isMove, CAsyncCopyParams*& asyncPar, BOOL* suspiciousIoRetry)
{
    if (suspiciousIoRetry != NULL)
        *suspiciousIoRetry = FALSE;
    char* const requestedTargetName = op->TargetName;
    char transactionalTargetName[3 * MAX_PATH];
    BOOL transactionalTarget = FALSE;
    BOOL transactionalTargetCommitted = FALSE;
    struct CRestoreRequestedTargetName
    {
        COperation* Operation;
        char* RequestedTargetName;
        CRestoreRequestedTargetName(COperation* operation, char* requestedTargetName)
            : Operation(operation), RequestedTargetName(requestedTargetName) {}
        ~CRestoreRequestedTargetName() { Operation->TargetName = RequestedTargetName; }
    } RestoreRequestedTargetName(op, requestedTargetName);
    struct CCleanupTransactionalTarget
    {
        char* TargetName;
        BOOL* IsTransactionalTarget;
        BOOL* IsCommitted;
        CCleanupTransactionalTarget(char* targetName, BOOL* isTransactionalTarget, BOOL* isCommitted)
            : TargetName(targetName), IsTransactionalTarget(isTransactionalTarget), IsCommitted(isCommitted) {}
        ~CCleanupTransactionalTarget()
        {
            if (*IsTransactionalTarget && !*IsCommitted)
            {
                ClearReadOnlyAttr(TargetName);
                if (!DeleteFileUtf8(TargetName))
                    TRACE_I("DoCopyFile(): unable to remove uncommitted transactional target " << TargetName << ": " << GetErrorText(GetLastError()));
            }
        }
    } CleanupTransactionalTarget(transactionalTargetName, &transactionalTarget, &transactionalTargetCommitted);

    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoCopyFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not copy it,
    // CreateFile would trim the spaces/dots and copy a different file or under a different name
    BOOL invalidSrcName = FileNameIsInvalid(op->SourceName, TRUE);
    BOOL invalidTgtName = FileNameIsInvalid(op->TargetName, TRUE);

    // optimization: skipping all "older and identical" files is about 4x faster,
    // slowing down when the file is newer is 5%, so it should be well worth it
    // (it is safe to assume the user enables "Overwrite Older" when the skips occur)
    BOOL tgtNameCaseCorrected = FALSE; // TRUE = the letter case in the target name was already adjusted to match the existing target file (so overwriting does not change it)
    WIN32_FIND_DATAW dataOut;
    if (SkipCopyIfTargetNotOlder(op, hProgressDlg, script, totalDone, dlgData, skip,
                                 invalidSrcName, invalidTgtName, &tgtNameCaseCorrected, &dataOut))
        return TRUE;

    // decide which algorithm to use for copying: the ancient synchronous one or
    // the asynchronous one inspired by the Windows 7 CopyFileEx version:
    // - under Vista it misbehaved badly; forget Vista, it is almost dead anyway, and
    //   when using the old algorithm against Win7 over the network I saw no speed difference
    //   for uploads, and downloads were only 15% slower (acceptable)
    // - the asynchronous algorithm makes sense only over the network + when source/target is fast or network-based
    // - with the old algorithm, copying on Win7 over the network is easily 2x-3x slower for downloads,
    //   almost 2x slower for uploads, and about 30% slower for network-to-network copies
    BOOL useAsyncAlg = Windows7AndLater && Configuration.UseAsyncCopyAlg &&
                       op->FileSize.Value > 0 && // empty files are copied synchronously (no data)
                       ((op->OpFlags & OPFL_SRCPATH_IS_NET) && ((op->OpFlags & OPFL_TGTPATH_IS_NET) ||
                                                                (op->OpFlags & OPFL_TGTPATH_IS_FAST)) ||
                        (op->OpFlags & OPFL_TGTPATH_IS_NET) && (op->OpFlags & OPFL_SRCPATH_IS_FAST));

    if (asyncPar == NULL)
        asyncPar = new CAsyncCopyParams;

    asyncPar->Init(useAsyncAlg);
    script->EnableProgressBufferLimit(useAsyncAlg);
    struct CDisableProgressBufferLimit // ensure Script->EnableProgressBufferLimit(FALSE) is called on every exit from this function
    {
        COperations* Script;
        CDisableProgressBufferLimit(COperations* script) { Script = script; }
        ~CDisableProgressBufferLimit() { Script->EnableProgressBufferLimit(FALSE); }
    } DisableProgressBufferLimit(script);

    CQuadWord operationDone;
    CQuadWord lastTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);

COPY_AGAIN:

    operationDone = CQuadWord(0, 0);
    HANDLE in;

    if (skip != NULL)
        *skip = FALSE;

    int bufferSize;
    if (useAsyncAlg)
    {
        if (op->FileSize.Value <= 512 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_512KB;
        else if (op->FileSize.Value <= 2 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_2MB;
        else if (op->FileSize.Value <= 8 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_8MB;
        else
            bufferSize = ASYNC_COPY_BUF_SIZE;
    }
    else
        bufferSize = GetOptimalSyncCopyBufferSize(script, op->OpFlags);

    int limitBufferSize = bufferSize;
    script->SetTFSandProgressSize(lastTransferredFileSize, totalDone, &limitBufferSize, bufferSize);

    while (1)
    {
        if (!invalidSrcName && !asyncPar->Failed())
        {
            in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                      OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        }
        else
        {
            in = INVALID_HANDLE_VALUE;
        }
        if (in != INVALID_HANDLE_VALUE)
        {
            CQuadWord fileSize = op->FileSize;

            HANDLE out;
            BOOL lossEncryptionAttr = FALSE;
            BOOL skipAllocWholeFileOnStart = FALSE;
            while (1)
            {
            OPEN_TGT_FILE:

                BOOL encryptionNotSupported = FALSE;
                DWORD fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH |
                                  (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) |
                                  (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                if (!invalidTgtName)
                {
                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                    if (transactionalTarget)
                        out = OpenTransactionalTargetFile(op->TargetName, GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0), fileAttrs, &encryptionNotSupported);
                    else
                        out = SalCreateFileEx(op->TargetName, GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0), 0, fileAttrs, &encryptionNotSupported);
                    if (!encryptionNotSupported && script->CopyAttrs && out == INVALID_HANDLE_VALUE) // in case read access to the directory is not allowed (we added it only for setting the Compressed attribute), try creating a write-only file
                    {
                        if (transactionalTarget)
                            out = OpenTransactionalTargetFile(op->TargetName, GENERIC_WRITE, fileAttrs, &encryptionNotSupported);
                        else
                            out = SalCreateFileEx(op->TargetName, GENERIC_WRITE, 0, fileAttrs, &encryptionNotSupported);
                    }

                    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported && dlgData.FileOutLossEncrAll && !lossEncryptionAttr)
                    { // the user agreed to lose the Encrypted attribute for all problematic files, so make that happen here
                        lossEncryptionAttr = TRUE;
                        continue;
                    }
                    HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                   __hoCreateFile, out, GetLastError(), TRUE);
                    if (script->CopyAttrs)
                    {
                        fileAttrs = lossEncryptionAttr ? (op->Attr & ~FILE_ATTRIBUTE_ENCRYPTED) : op->Attr;
                        SetCompressAndEncryptedAttrs(op->TargetName, fileAttrs, &out, TRUE, NULL, asyncPar);
                    }

                    if (out != INVALID_HANDLE_VALUE && (fileAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                    { // verify that the Encrypted attribute is really set (on FAT it is simply ignored, the system does not return an error (for CreateFile specifically))
                        DWORD attrs;
                        attrs = SalGetFileAttributes(op->TargetName);
                        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
                        { // unable to apply the Encrypted attribute, ask the user what to do...
                            if (dlgData.FileOutLossEncrAll)
                                lossEncryptionAttr = TRUE;
                            else
                            {
                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CANCEL_ENCNOTSUP;

                                if (dlgData.SkipAllFileOutLossEncr)
                                    goto SKIP_ENCNOTSUP;

                                int ret;
                                ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = (char*)TRUE;
                                data[2] = op->TargetName;
                                data[3] = (char*)(INT_PTR)isMove;
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                switch (ret)
                                {
                                case IDB_ALL:
                                    dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                case IDYES:
                                    RecordMetadataLoss(dlgData, mmlCompressionAndEncryption, op->SourceName, op->TargetName);
                                    lossEncryptionAttr = TRUE;
                                    break;

                                case IDB_SKIPALL:
                                    dlgData.SkipAllFileOutLossEncr = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_ENCNOTSUP:

                                    HANDLES(CloseHandle(out));
                                    DeleteFileUtf8(op->TargetName);
                                    goto SKIP_OPEN_OUT;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_ENCNOTSUP:

                                    HANDLES(CloseHandle(out));
                                    DeleteFileUtf8(op->TargetName);
                                    goto CANCEL_OPEN2;
                                }
                                }
                            }
                        }
                    }
                }
                else
                {
                    out = INVALID_HANDLE_VALUE;
                }

                if (out != INVALID_HANDLE_VALUE)
                {

                COPY:

                    // if possible, allocate the required space for the file (prevents disk fragmentation + smoother writes to floppies)
                    BOOL wholeFileAllocated = FALSE;
                    if (!skipAllocWholeFileOnStart &&               // last time failed, so the same would probably happen now
                        allocWholeFileOnStart != 2 /* no */ &&      // allocating the whole file is not forbidden
                        fileSize > CQuadWord(limitBufferSize, 0) && // allocation is pointless below the copy buffer size
                        fileSize < CQuadWord(0, 0x80000000))        // file size is positive number (otherwise seeking is impossible - numbers above 8EB, so likely never happens)
                    {
                        BOOL fatal = TRUE;
                        BOOL ignoreErr = FALSE;
                        DWORD allocationError = NO_ERROR;
                        CFileOffsetResult allocationSeek = SalSetFilePointerEx(out, fileSize, FILE_BEGIN);
                        if (allocationSeek.Succeeded)
                        {
                            if (SetEndOfFile(out))
                            {
                                CFileOffsetResult rewind = SalSetFilePointerEx(out, CQuadWord(0, 0), FILE_BEGIN);
                                if (rewind.Succeeded)
                                {
                                    fatal = FALSE;
                                    wholeFileAllocated = TRUE;
                                }
                                else
                                    allocationError = rewind.Error;
                            }
                            else
                            {
                                allocationError = GetLastError();
                                if (allocationError == ERROR_DISK_FULL)
                                    ignoreErr = TRUE; // not enough space on the disk
                            }
                        }
                        else
                            allocationError = allocationSeek.Error;
                        if (fatal)
                        {
                            if (!ignoreErr)
                            {
                                TRACE_E("DoCopyFile(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! Error: " << GetErrorText(allocationError));
                                allocWholeFileOnStart = 2 /* no */; // we will forego further attempts on this target disk
                            }

                            // try truncating the file to zero so closing it does not trigger any unnecessary writes
                            SalSetFilePointerEx(out, CQuadWord(0, 0), FILE_BEGIN);
                            SetEndOfFile(out);

                            HANDLES(CloseHandle(out));
                            out = INVALID_HANDLE_VALUE;
                            ClearReadOnlyAttr(op->TargetName); // in case it was created as read-only (should never happen) so we can handle it
                            if (DeleteFileUtf8(op->TargetName))
                            {
                                skipAllocWholeFileOnStart = TRUE;
                                goto OPEN_TGT_FILE;
                            }
                            else
                                goto CREATE_ERROR;
                        }
                    }

                    script->SetFileStartParams();

                    BOOL copyError = FALSE;
                    BOOL skipCopy = FALSE;
                    BOOL copyAgain = FALSE;
                    if (useAsyncAlg)
                    {
                        DoCopyFileLoopAsync(asyncPar, in, out, buffer, limitBufferSize, script, dlgData, wholeFileAllocated, op,
                                            totalDone, copyError, skipCopy, hProgressDlg, operationDone, fileSize,
                                            bufferSize, allocWholeFileOnStart, copyAgain, lastTransferredFileSize);
                        // NOTE: neither 'in' nor 'out' has the file pointer (SetFilePointer) positioned at the end of the file,
                        //       'out' has it set only when (copyError || skipCopy)
                    }
                    else
                    {
                        DoCopyFileLoopOrig(in, out, buffer, limitBufferSize, script, dlgData, wholeFileAllocated, op,
                                           totalDone, copyError, skipCopy, hProgressDlg, operationDone, fileSize,
                                           bufferSize, allocWholeFileOnStart, copyAgain);
                    }

                    if (copyError)
                    {
                    COPY_ERROR:

                        if (in != NULL)
                            HANDLES(CloseHandle(in));
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                SetEndOfFile(out); // otherwise on a floppy the remaining part of the file would be written
                            HANDLES(CloseHandle(out));
                        }
                        DeleteFileUtf8(op->TargetName);
                        return FALSE;
                    }
                    if (skipCopy)
                    {
                    SKIP_COPY:

                        totalDone += op->Size;
                        RecordSkippedFileProgressState(op, lastTransferredFileSize, script, totalDone);

                        if (in != NULL)
                            HANDLES(CloseHandle(in));
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                SetEndOfFile(out); // otherwise on a floppy the remaining part of the file would be written
                            HANDLES(CloseHandle(out));
                        }
                        DeleteFileUtf8(op->TargetName);
                        SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                        if (skip != NULL)
                            *skip = TRUE;
                        return TRUE;
                    }
                    if (copyAgain)
                    {
                        if (suspiciousIoRetry != NULL)
                            *suspiciousIoRetry = TRUE;
                        goto COPY_AGAIN;
                    }

                    if (lantasticCheck)
                    {
                        CFileOffsetResult inSize = SalGetFileSizeEx(in);
                        CFileOffsetResult outSize = SalGetFileSizeEx(out);
                        if (!inSize.Succeeded || !outSize.Succeeded || inSize.Value != outSize.Value)
                        {                                                              // Lantastic 7.0: everything seems fine, but the result is wrong
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR;

                            if (dlgData.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(ERROR_DISK_FULL);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                if (suspiciousIoRetry != NULL)
                                    *suspiciousIoRetry = TRUE;
                                operationDone = CQuadWord(0, 0);
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone);
                                SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                                SalSetFilePointerEx(in, CQuadWord(0, 0), FILE_BEGIN);  // read again
                                SalSetFilePointerEx(out, CQuadWord(0, 0), FILE_BEGIN); // write again
                                SetEndOfFile(out);                        // truncate the output file
                                goto COPY;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }
                    }

                    FILETIME /*creation, lastAccess,*/ lastWrite;
                    BOOL ignoreGetFileTimeErr = FALSE;
                    while (!ignoreGetFileTimeErr &&
                           !GetFileTime(in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite))
                    {
                        DWORD err = GetLastError();

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto COPY_ERROR;

                        if (dlgData.SkipAllGetFileTime)
                            goto SKIP_COPY;

                        if (dlgData.IgnoreAllGetFileTimeErr)
                            goto IGNORE_GETFILETIME;

                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = LoadStr(IDS_ERRORGETTINGFILETIME);
                        data[2] = op->SourceName;
                        data[3] = GetErrorText(err);
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllGetFileTimeErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                        {
                        IGNORE_GETFILETIME:

                            ignoreGetFileTimeErr = TRUE;
                            break;
                        }

                        case IDB_SKIPALL:
                            dlgData.SkipAllGetFileTime = TRUE;
                        case IDB_SKIP:
                            goto SKIP_COPY;

                        case IDCANCEL:
                            goto COPY_ERROR;
                        }
                    }

                    HANDLES(CloseHandle(in));
                    in = NULL;

                    if (operationDone < COPY_MIN_FILE_SIZE) // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        script->AddBytesToSpeedMetersAndTFSandPS((DWORD)(COPY_MIN_FILE_SIZE - operationDone).Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                    DWORD attr = op->Attr & clearReadonlyMask;
                    if (copyADS) // copy ADS streams if needed
                    {
                        SetFileAttributesUtf8(op->TargetName, FILE_ATTRIBUTE_ARCHIVE); // probably unnecessary, it hardly slows copying; reason: the file must not be read-only to work with it
                        CQuadWord operDone = operationDone;                        // the file is already copied
                        if (operDone < COPY_MIN_FILE_SIZE)
                            operDone = COPY_MIN_FILE_SIZE; // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        BOOL adsSkip = FALSE;
                        // Pass the optimal buffer size computed from op->OpFlags for ADS copy
                        int adsBufferSize = GetOptimalSyncCopyBufferSize(script, op->OpFlags);
                        if (!DoCopyADS(hProgressDlg, op->SourceName, FALSE, op->TargetName, totalDone,
                                       operDone, op->Size, dlgData, script, &adsSkip, buffer, adsBufferSize) ||
                            adsSkip) // user hit cancel or skipped at least one ADS
                        {
                            if (out != NULL)
                                HANDLES(CloseHandle(out));
                            out = NULL;
                            if (DeleteFileUtf8(op->TargetName) == 0)
                            {
                                DWORD err = GetLastError();
                                TRACE_E("DoCopyFile(): Unable to remove newly created file: " << op->TargetName << ", error: " << GetErrorText(err));
                            }
                            if (!adsSkip)
                                return FALSE; // cancel the entire operation
                            if (skip != NULL)
                                *skip = TRUE; // it is a Skip, must report higher up (Move must not delete the source file)
                        }
                    }

                    if (out != NULL)
                    {
                        if (!ignoreGetFileTimeErr) // only if we did not ignore the error while reading the file time (nothing to set otherwise)
                        {
                            BOOL ignoreSetFileTimeErr = FALSE;
                            while (!ignoreSetFileTimeErr &&
                                    !OperationExecutionFileSystem().SetFileTime(out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite))
                            {
                                DWORD err = GetLastError();

                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto COPY_ERROR;

                                if (dlgData.SkipAllSetFileTime)
                                    goto SKIP_COPY;

                                if (dlgData.IgnoreAllSetFileTimeErr)
                                    goto IGNORE_SETFILETIME;

                                int ret;
                                ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = LoadStr(IDS_ERRORSETTINGFILETIME);
                                data[2] = op->TargetName;
                                data[3] = GetErrorText(err);
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;

                                case IDB_IGNOREALL:
                                    dlgData.IgnoreAllSetFileTimeErr = TRUE; // the break; is intentionally missing here
                                case IDB_IGNORE:
                                {
                                IGNORE_SETFILETIME:

                                    RecordMetadataLoss(dlgData, mmlLastWriteTime, op->SourceName, op->TargetName);
                                    ignoreSetFileTimeErr = TRUE;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    dlgData.SkipAllSetFileTime = TRUE;
                                case IDB_SKIP:
                                    goto SKIP_COPY;

                                case IDCANCEL:
                                    goto COPY_ERROR;
                                }
                            }
                        }
                        DWORD closeOrFlushError = NO_ERROR;
                        // This is the durable copy commit boundary.  It applies to new
                        // destinations too: DoMoveFile may delete the source only after
                        // DoCopyFile returns success from this boundary.
                        if (!OperationExecutionFileSystem().FlushFileBuffers(out))
                            closeOrFlushError = GetLastError();
                        if (!HANDLES(CloseHandle(out)) && closeOrFlushError == NO_ERROR)
                            closeOrFlushError = GetLastError();
                        out = NULL;
                        if (closeOrFlushError != NO_ERROR)
                        {
                            out = NULL;
                            DWORD err = closeOrFlushError;
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR;

                            if (dlgData.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                if (suspiciousIoRetry != NULL)
                                    *suspiciousIoRetry = TRUE;
                                if (DeleteFileUtf8(op->TargetName) == 0)
                                {
                                    DWORD err2 = GetLastError();
                                    TRACE_E("DoCopyFile(): Unable to remove newly created file: " << op->TargetName << ", error: " << GetErrorText(err2));
                                }
                                goto COPY_AGAIN;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }

                        SetFileAttributesUtf8(op->TargetName, script->CopyAttrs ? attr : (attr | FILE_ATTRIBUTE_ARCHIVE));
                    }

                    if (script->CopyAttrs) // verify whether the source file attributes were preserved
                    {
                        DWORD curAttrs;
                        curAttrs = SalGetFileAttributes(op->TargetName);
                        if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (attr & DISPLAYED_ATTRIBUTES))
                        {                                                              // attributes probably were not preserved, warn the user
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (dlgData.IgnoreAllSetAttrsErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = op->TargetName;
                                data[2] = (char*)(DWORD_PTR)(attr & DISPLAYED_ATTRIBUTES);
                                data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllSetAttrsErr = TRUE; // break is intentional here; nothing is missing
                            case IDB_IGNORE:
                                RecordMetadataLoss(dlgData, mmlAttributes, op->SourceName, op->TargetName);
                                break;

                            case IDCANCEL:
                            {
                            COPY_ERROR_2:

                                ClearReadOnlyAttr(op->TargetName); // the file must not be read-only if it is to be deleted
                                DeleteFileUtf8(op->TargetName);
                                return FALSE;
                            }
                            }
                        }
                    }

                    if (script->CopySecurity) // should we copy NTFS security permissions?
                    {
                        DWORD err;
                        if (!DoCopySecurity(op->SourceName, op->TargetName, &err, NULL))
                        {
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (dlgData.IgnoreAllCopyPermErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = op->SourceName;
                                data[2] = op->TargetName;
                                data[3] = (char*)(DWORD_PTR)err;
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                            case IDB_IGNORE:
                                RecordMetadataLoss(dlgData, mmlSecurity, op->SourceName, op->TargetName);
                                break;

                            case IDCANCEL:
                                goto COPY_ERROR_2;
                            }
                        }
                    }

                    // Durable commit phase: verify what landed on disk and, for a
                    // transactional overwrite, swap in the committed target.
                    switch (VerifyAndCommitCopyTarget(op, hProgressDlg, requestedTargetName, script, dlgData,
                                                      transactionalTarget, &transactionalTargetCommitted,
                                                      copyADS, suspiciousIoRetry))
                    {
                    case cpcrProceed:
                        break;

                    case cpcrCancel:
                        goto COPY_ERROR_2;

                    case cpcrSkip:
                        goto SKIP_COPY;

                    case cpcrRestart:
                        goto COPY_AGAIN;
                    }

                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    return TRUE;
                }
                else
                {
                    if (!invalidTgtName && encryptionNotSupported)
                    {
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_OPEN2;

                        if (dlgData.SkipAllFileOutLossEncr)
                            goto SKIP_OPEN_OUT;

                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = (char*)TRUE;
                        data[2] = op->TargetName;
                        data[3] = (char*)(INT_PTR)isMove;
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                        case IDYES:
                            RecordMetadataLoss(dlgData, mmlCompressionAndEncryption, op->SourceName, op->TargetName);
                            lossEncryptionAttr = TRUE;
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllFileOutLossEncr = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN_OUT:

                            totalDone += op->Size;
                            RecordSkippedFileProgressState(op, lastTransferredFileSize, script, totalDone);

                            HANDLES(CloseHandle(in));
                            SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                            if (skip != NULL)
                                *skip = TRUE;
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN2:

                            HANDLES(CloseHandle(in));
                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                    CREATE_ERROR:

                        DWORD err = GetLastError();
                        if (invalidTgtName)
                            err = ERROR_INVALID_NAME;
                        BOOL errDeletingFile = FALSE;
                        if (err == ERROR_FILE_EXISTS || // overwrite the file?
                            err == ERROR_ALREADY_EXISTS)
                        {
                            if (!dlgData.OverwriteAll && (dlgData.CnfrmFileOver || script->OverwriteOlder))
                            {
                                char sAttr[101], tAttr[101];
                                BOOL getTimeFailed;
                                getTimeFailed = FALSE;
                                FILETIME sFileTime, tFileTime;
                                GetFileOverwriteInfo(sAttr, _countof(sAttr), in, op->SourceName, &sFileTime, &getTimeFailed);
                                HANDLES(CloseHandle(in));
                                in = NULL;
                                out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                           OPEN_EXISTING, 0, NULL));
                                if (out != INVALID_HANDLE_VALUE)
                                {
                                    GetFileOverwriteInfo(tAttr, _countof(tAttr), out, op->TargetName, &tFileTime, &getTimeFailed);
                                    HANDLES(CloseHandle(out));
                                }
                                else
                                {
                                    getTimeFailed = TRUE;
                                    // This error field is a fixed dialog presentation buffer.
                                    StringCchCopyNA(tAttr, _countof(tAttr), LoadStr(IDS_ERR_FILEOPEN), _countof(tAttr) - 1);
                                }
                                out = NULL;

                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CANCEL_OPEN;

                                if (dlgData.SkipAllOverwrite)
                                    goto SKIP_OPEN;

                                int ret;
                                ret = IDCANCEL;

                                if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                                {
                                    // trim times to seconds (different file systems store times with different precision, so "differences" occurred even between "identical" times)
                                    *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                                    *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                                    if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                        ret = IDYES; // overwrite older files without asking
                                    else
                                        ret = IDB_SKIP; // skip other existing files
                                }
                                else
                                {
                                    // show the prompt
                                    char* data[5];
                                    data[0] = (char*)&ret;
                                    data[1] = op->TargetName;
                                    data[2] = tAttr;
                                    data[3] = op->SourceName;
                                    data[4] = sAttr;
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
                                }
                                switch (ret)
                                {
                                case IDB_ALL:
                                    dlgData.OverwriteAll = TRUE;
                                case IDYES:
                                default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                {
                                    in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                              OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    if (in == INVALID_HANDLE_VALUE)
                                        goto OPEN_IN_ERROR;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    dlgData.SkipAllOverwrite = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_OPEN:

                                    totalDone += op->Size;
                                    RecordSkippedFileProgressState(op, lastTransferredFileSize, script, totalDone);

                                    SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                                    if (skip != NULL)
                                        *skip = TRUE;
                                    return TRUE;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_OPEN:

                                    return FALSE;
                                }
                                }
                            }

                            DWORD attr = SalGetFileAttributes(op->TargetName);
                            if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                            {
                                if (!dlgData.OverwriteHiddenAll && dlgData.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                                {
                                    HANDLES(CloseHandle(in));
                                    in = NULL;

                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto CANCEL_OPEN;

                                    if (dlgData.SkipAllSystemOrHidden)
                                        goto SKIP_OPEN;

                                    int ret = IDCANCEL;
                                    char* data[4];
                                    data[0] = (char*)&ret;
                                    data[1] = LoadStr(IDS_CONFIRMFILEOVERWRITING);
                                    data[2] = op->TargetName;
                                    data[3] = LoadStr(IDS_WANTOVERWRITESHFILE);
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        dlgData.OverwriteHiddenAll = TRUE;
                                    case IDYES:
                                    default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                    {
                                        in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                                  OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                        if (in == INVALID_HANDLE_VALUE)
                                            goto OPEN_IN_ERROR;
                                        attr = SalGetFileAttributes(op->TargetName); // refresh attributes in case the user changed them
                                        break;
                                    }

                                    case IDB_SKIPALL:
                                        dlgData.SkipAllSystemOrHidden = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_OPEN;

                                    case IDCANCEL:
                                        goto CANCEL_OPEN;
                                    }
                                }
                            }

                            BOOL targetCannotOpenForWrite = FALSE;
                            while (1)
                            {
                                if (!transactionalTarget)
                                {
                                    // The overwrite decision has been made.  From this point on write only
                                    // to a sibling reservation, so retry, low-space, cancellation, and
                                    // metadata failures can remove that file without touching the old target.
                                    if (!CreateTransactionalTargetFileName(requestedTargetName, transactionalTargetName, _countof(transactionalTargetName)))
                                    {
                                        err = GetLastError();
                                        goto NORMAL_ERROR;
                                    }
                                    if (!script->JournalSetTemporaryPath(transactionalTargetName))
                                    {
                                        err = ERROR_WRITE_FAULT;
                                        goto NORMAL_ERROR;
                                    }
                                    op->TargetName = transactionalTargetName;
                                    transactionalTarget = TRUE;
                                    goto OPEN_TGT_FILE;
                                }

                                if (targetCannotOpenForWrite || mustDeleteFileBeforeOverwrite == 1 /* yes */)
                                { // the file must be deleted first
                                    BOOL chAttr = ClearReadOnlyAttr(op->TargetName, attr);

                                    if (!tgtNameCaseCorrected)
                                    {
                                        CorrectCaseOfTgtName(op->TargetName, FALSE, &dataOut);
                                        tgtNameCaseCorrected = TRUE;
                                    }

                                    if (DeleteFileUtf8(op->TargetName))
                                        goto OPEN_TGT_FILE; // if it is read-only (clearing the attribute may have failed), it can be deleted only on Samba with "delete readonly" enabled
                                    else                    // cannot delete either, end with an error...
                                    {
                                        err = GetLastError();
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        errDeletingFile = TRUE;
                                        goto NORMAL_ERROR;
                                    }
                                }
                                else // overwrite the file in place
                                {
                                    // if we have not yet tested truncating the file to zero, obtain the current file size
                                    CQuadWord origFileSize(0, 0); // file size before truncation
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                                   OPEN_EXISTING, 0, NULL));
                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            CFileOffsetResult originalSize = SalGetFileSizeEx(out);
                                            origFileSize = originalSize.Value;
                                            if (!originalSize.Succeeded)
                                                origFileSize.Set(0, 0); // error => set the size to zero and test it on another file
                                            HANDLES(CloseHandle(out));
                                        }
                                    }

                                    // open the file with ADS removal and truncation to zero
                                    BOOL chAttr = FALSE;
                                    if (attr != INVALID_FILE_ATTRIBUTES &&
                                        (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                                    { // CREATE_ALWAYS does not play well with read-only, hidden, or system attributes, so drop them if needed
                                        chAttr = TRUE;
                                        SetFileAttributesUtf8(op->TargetName, 0);
                                    }
                                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                                    DWORD access = GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0);
                                    fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN |
                                                (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | // setting attributes during CREATE_ALWAYS works since XP and is the only way to apply Encrypted attribute when the file denies read access
                                                (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, fileAttrs, NULL));
                                    if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    if (script->CopyAttrs && out == INVALID_HANDLE_VALUE)
                                    { // if read access to the directory is denied (we added it only for setting the Compressed attribute), try opening the file for write only
                                        access = GENERIC_WRITE;
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, fileAttrs, NULL));
                                        if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                            out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    }
                                    if (out == INVALID_HANDLE_VALUE) // target file cannot be opened for writing, so delete it and create it again
                                    {
                                        // handles the situation when a Samba file must be overwritten:
                                        // the file has mode 440+different_owner and sits in a directory where the current user has write access
                                        // (deletion works, but direct overwrite does not (cannot open for writing) - workaround:
                                        //  delete and recreate the file)
                                        // (Samba can allow deleting read-only files, which enables deleting them,
                                        //  otherwise Windows cannot delete a read-only file and we cannot drop 
                                        //  the "read-only" attribute because the current user is not the owner)
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // on target paths that support ADS also delete ADS on the target file (CREATE_ALWAYS should remove them, but on home W2K and XP they simply stay; no idea why, W2K and XP in VMWare delete ADS normally)
                                    if (script->TargetPathSupADS && !DeleteAllADS(out, op->TargetName))
                                    {
                                        HANDLES(CloseHandle(out));
                                        out = INVALID_HANDLE_VALUE;
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // if we have not yet tested truncating the file to zero, obtain the new file size
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        HANDLES(CloseHandle(out));
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, OPEN_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                        if (out == INVALID_HANDLE_VALUE) // cannot reopen the target file we just opened, unlikely, try deleting and recreating it
                                        {
                                            targetCannotOpenForWrite = TRUE;
                                            continue;
                                        }
                                        CQuadWord newFileSize(0, 0); // file size after truncation
                                        CFileOffsetResult currentSize = SalGetFileSizeEx(out);
                                        newFileSize = currentSize.Value;
                                        if (currentSize.Succeeded && // we have the new size
                                            newFileSize == CQuadWord(0, 0))                                             // file really has 0 bytes
                                        {
                                            if (origFileSize != CQuadWord(0, 0))            // truncation can only be tested on a non-zero file
                                                mustDeleteFileBeforeOverwrite = 2; /* no */ // success (not a SNAP server - NSA drive, truncation does not work there)
                                        }
                                        else
                                        {
                                            HANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            mustDeleteFileBeforeOverwrite = 1 /* yes */; // on error or when the size is non-zero, play it safe...
                                            continue;
                                        }
                                    }

                                    if (script->CopyAttrs || !lossEncryptionAttr && copyAsEncrypted)
                                    {
                                        encryptionNotSupported = FALSE;
                                        SetCompressAndEncryptedAttrs(op->TargetName, (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0),
                                                                     &out, script->CopyAttrs, &encryptionNotSupported, asyncPar);
                                        if (encryptionNotSupported) // unable to apply the Encrypted attribute, ask the user what to do...
                                        {
                                            if (dlgData.FileOutLossEncrAll)
                                                lossEncryptionAttr = TRUE;
                                            else
                                            {
                                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                                if (*dlgData.CancelWorker)
                                                    goto CANCEL_ENCNOTSUP;

                                                if (dlgData.SkipAllFileOutLossEncr)
                                                    goto SKIP_ENCNOTSUP;

                                                int ret;
                                                ret = IDCANCEL;
                                                char* data[4];
                                                data[0] = (char*)&ret;
                                                data[1] = (char*)TRUE;
                                                data[2] = op->TargetName;
                                                data[3] = (char*)(INT_PTR)isMove;
                                                SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                                switch (ret)
                                                {
                                                case IDB_ALL:
                                                    dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                                case IDYES:
                                                    lossEncryptionAttr = TRUE;
                                                    break;

                                                case IDB_SKIPALL:
                                                    dlgData.SkipAllFileOutLossEncr = TRUE;
                                                case IDB_SKIP:
                                                    goto SKIP_ENCNOTSUP;

                                                case IDCANCEL:
                                                    goto CANCEL_ENCNOTSUP;
                                                }
                                            }
                                        }
                                    }
                                }
                                break;
                            }

                            goto COPY;
                        }
                        else // regular error
                        {
                        NORMAL_ERROR:

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN2;

                            if (dlgData.SkipAllFileOpenOut)
                                goto SKIP_OPEN_OUT;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(errDeletingFile ? IDS_ERRORDELETINGFILE : IDS_ERROROPENINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileOpenOut = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN_OUT;

                            case IDCANCEL:
                                goto CANCEL_OPEN2;
                            }
                        }
                    }
                }
            }
        }
        else
        {
        OPEN_IN_ERROR:

            DWORD err = GetLastError();
            if (invalidSrcName)
                err = ERROR_INVALID_NAME;
            if (asyncPar->Failed())
                err = ERROR_NOT_ENOUGH_MEMORY;                         // cannot create the synchronization event = lack of resources (will probably never happens, so we do not bother)
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllFileOpenIn)
                goto SKIP_OPEN_IN;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERROROPENINGFILE);
            data[2] = op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllFileOpenIn = TRUE;
            case IDB_SKIP:
            {
            SKIP_OPEN_IN:

                totalDone += op->Size;
                RecordSkippedFileProgressState(op, lastTransferredFileSize, script, totalDone);

                SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                if (skip != NULL)
                    *skip = TRUE;
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

// Completes a successful same-volume rename: verifies that the attributes
// survived, applies the stashed source security, restores directory time, and
// reports progress. Extracted from DoMoveFile where it replaced the
// OPERATION_DONE goto target shared by three entry paths. Returns FALSE on
// cancellation; the already-moved file is intentionally left in place.
static BOOL FinishSameVolumeMove(COperation* op, HWND hProgressDlg, COperations* script,
                                 CQuadWord& totalDone, CProgressDlgData& dlgData,
                                 const char* sourceNameMvDir,
                                 const char* targetNameMvDir, BOOL dir,
                                 FILETIME* dirTimeModified, BOOL dirTimeModifiedIsValid,
                                 BOOL* setDirTimeAfterMove, CSrcSecurity* srcSecurity,
                                 BOOL srcSecurityErr)
{
    if (script->CopyAttrs) // check whether the source file attributes were preserved
    {
        DWORD curAttrs;
        curAttrs = SalGetFileAttributes(targetNameMvDir);
        if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (op->Attr & DISPLAYED_ATTRIBUTES))
        {                                                              // attributes probably were not preserved, warn the user
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            int ret;
            ret = IDCANCEL;
            if (dlgData.IgnoreAllSetAttrsErr)
                ret = IDB_IGNORE;
            else
            {
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = op->TargetName;
                data[2] = (char*)(DWORD_PTR)(op->Attr & DISPLAYED_ATTRIBUTES);
                data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
            }
            switch (ret)
            {
            case IDB_IGNOREALL:
                dlgData.IgnoreAllSetAttrsErr = TRUE; // the break; is intentionally missing here
            case IDB_IGNORE:
                RecordMetadataLoss(dlgData, mmlAttributes, op->SourceName, op->TargetName);
                break;

            case IDCANCEL:
                return FALSE; // the file was moved to the target + cancel occurred; but we would rather not move it back, nobody should mind much
            }
        }
    }

    if (script->CopySecurity && !srcSecurityErr) // should we copy NTFS security permissions?
    {
        DWORD err;
        if (!DoCopySecurity(sourceNameMvDir, targetNameMvDir, &err, srcSecurity))
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            int ret;
            ret = IDCANCEL;
            if (dlgData.IgnoreAllCopyPermErr)
                ret = IDB_IGNORE;
            else
            {
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = op->SourceName;
                data[2] = op->TargetName;
                data[3] = (char*)(DWORD_PTR)err;
                SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
            }
            switch (ret)
            {
            case IDB_IGNOREALL:
                dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
            case IDB_IGNORE:
                RecordMetadataLoss(dlgData, mmlSecurity, op->SourceName, op->TargetName);
                break;

            case IDCANCEL:
                return FALSE;
            }
        }
    }

    if (dir && dirTimeModifiedIsValid && *setDirTimeAfterMove != 2 /* no */)
    {
        FILETIME movedDirTimeModified;
        if (GetDirTime(targetNameMvDir, &movedDirTimeModified))
        {
            if (CompareFileTime(dirTimeModified, &movedDirTimeModified) == 0)
            {
                if (*setDirTimeAfterMove == 0 /* need test */)
                    *setDirTimeAfterMove = 2 /* no */;
            }
            else
            {
                if (*setDirTimeAfterMove == 0 /* need test */)
                    *setDirTimeAfterMove = 1 /* yes */;
                DoCopyDirTime(hProgressDlg, targetNameMvDir, dirTimeModified, dlgData, TRUE); // ignore any failure, this is just a hack (we already ignore time read errors from the directory); MoveFile should not change times
            }
        }
    }

    script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

    totalDone += op->Size;
    SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
    return TRUE;
}

// Moves one file ('op'). Two disjoint strategies chosen up front:
//   - Same root path and not forced-copy: native rename path. Stashes source
//     security before the rename (the source vanishes), verifies both file
//     identities, retries via the Novell read-only-attribute patch on
//     ACCESS_DENIED, works around 8.3 DOS-name collisions, resolves overwrite
//     by deleting the verified destination first (its identity then becomes
//     "absent"), and finishes with FinishSameVolumeMove.
//   - Otherwise: full DoCopyFile copy; only after a successful copy may the
//     source be deleted, gated by ConfirmMetadataLossesBeforeSourceDeletion
//     and, when suspicious I/O retries occurred, VerifyFullFileContentSha256;
//     the delete itself uses DeleteFileWithVerifiedIdentity with retry/skip
//     dialogs ('dir' variant never reaches here - directories are refused).
// Returns FALSE on cancel/error; TRUE also covers "skip" outcomes.
BOOL DoMoveFile(COperation* op, HWND hProgressDlg, void* buffer,
                COperations* script, CQuadWord& totalDone, BOOL dir,
                DWORD clearReadonlyMask, BOOL* novellRenamePatch, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CProgressDlgData& dlgData, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL* setDirTimeAfterMove, CAsyncCopyParams*& asyncPar,
                BOOL ignInvalidName)
{
    char log_buffer[1024];
    _snprintf_s(log_buffer, sizeof(log_buffer), _TRUNCATE, "DoMoveFile: Source='%s', Target='%s'", op->SourceName ? op->SourceName : "NULL", op->TargetName ? op->TargetName : "NULL");
    OutputDebugStringA(log_buffer);

    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoMoveFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not move it,
    // MoveFile would trim the spaces/dots and move a different file or under a different name,
    // directories fare better: appending a backslash helps there, we block the move
    // only when a new directory name would be invalid (when moving under the old
    // name, 'ignInvalidName' is TRUE)
    BOOL invalidName = FileNameIsInvalid(op->SourceName, TRUE, dir) ||
                       FileNameIsInvalid(op->TargetName, TRUE, dir && ignInvalidName);

    if (!copyAsEncrypted && !script->SameRootButDiffVolume && HasTheSameRootPath(op->SourceName, op->TargetName))
    {
        // if the path ends with a space or dot, we must append '\\', otherwise GetNamedSecurityInfo,
        // GetDirTime, SetFileAttributes, and MoveFile trim the spaces/dots and operate on a different path
        const char* sourceNameMvDir = op->SourceName;
        char sourceNameMvDirCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(sourceNameMvDir, sourceNameMvDirCopy);
        const char* targetNameMvDir = op->TargetName;
        char targetNameMvDirCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(targetNameMvDir, targetNameMvDirCopy);

        int autoRetryAttempts = 0;
        CSrcSecurity srcSecurity;
        BOOL srcSecurityErr = FALSE;
        if (!invalidName && script->CopySecurity) // should we copy NTFS security permissions?
        {
            CStrP sourceNameMvDirW(ConvertAllocUtf8ToWide(sourceNameMvDir, -1));
            if (sourceNameMvDirW != NULL)
            {
                srcSecurity.SrcError = GetNamedSecurityInfoW(sourceNameMvDirW, SE_FILE_OBJECT,
                                                            DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                                            &srcSecurity.SrcOwner, &srcSecurity.SrcGroup, &srcSecurity.SrcDACL,
                                                            NULL, &srcSecurity.SrcSD);
            }
            else
            {
                srcSecurity.SrcError = ERROR_NO_UNICODE_TRANSLATION;
            }
            if (srcSecurity.SrcError != ERROR_SUCCESS) // failed to read security info from the source file -> nothing to apply on the target
            {
                srcSecurityErr = TRUE;
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                    return FALSE;

                int ret;
                ret = IDCANCEL;
                if (dlgData.IgnoreAllCopyPermErr)
                    ret = IDB_IGNORE;
                else
                {
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = op->SourceName;
                    data[2] = op->TargetName;
                    data[3] = (char*)(DWORD_PTR)srcSecurity.SrcError;
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                }
                switch (ret)
                {
                case IDB_IGNOREALL:
                    dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                case IDB_IGNORE:
                    RecordMetadataLoss(dlgData, mmlSecurity, op->SourceName, op->TargetName);
                    break;

                case IDCANCEL:
                    return FALSE;
                }
            }
        }
        FILETIME dirTimeModified;
        BOOL dirTimeModifiedIsValid = FALSE;
        if (!invalidName && dir && !*novellRenamePatch && *setDirTimeAfterMove != 2 /* no */) // the issue apparently does not apply to Novell Netware, so ignore it there (affects e.g. Samba)
            dirTimeModifiedIsValid = GetDirTime(sourceNameMvDir, &dirTimeModified);
        COperation::CFileIdentity expectedTargetIdentity = op->TargetIdentity;
        while (1)
        {
            DWORD identityError = ERROR_SUCCESS;
            BOOL identitiesMatch = !invalidName &&
                                   VerifyFileIdentity(sourceNameMvDir, op->SourceIdentity, &identityError) &&
                                   VerifyFileIdentity(targetNameMvDir, expectedTargetIdentity, &identityError);
            if (identitiesMatch && !*novellRenamePatch && SalMoveFile(sourceNameMvDir, targetNameMvDir))
            {
                if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // Archive attribute was not set, MoveFile turned it on, clear it again
                    SetFileAttributesUtf8(targetNameMvDir, op->Attr);                  // leave without handling or retry, not important (it normally toggles chaotically)

                return FinishSameVolumeMove(op, hProgressDlg, script, totalDone, dlgData, sourceNameMvDir,
                                            targetNameMvDir, dir, &dirTimeModified, dirTimeModifiedIsValid,
                                            setDirTimeAfterMove, &srcSecurity, srcSecurityErr);
            }
            else
            {
                DWORD err = identitiesMatch ? GetLastError() : identityError;
                if (invalidName)
                    err = ERROR_INVALID_NAME;
                // Novell patch - before calling MoveFile we need to drop the read-only attribute
                if (!invalidName && *novellRenamePatch || err == ERROR_ACCESS_DENIED)
                {
                    DWORD attr = SalGetFileAttributes(sourceNameMvDir);
                    BOOL setAttr = ClearReadOnlyAttr(sourceNameMvDir, attr);
                    if (VerifyFileIdentity(sourceNameMvDir, op->SourceIdentity, &err) &&
                        VerifyFileIdentity(targetNameMvDir, expectedTargetIdentity, &err) &&
                        SalMoveFile(sourceNameMvDir, targetNameMvDir))
                    {
                        if (!*novellRenamePatch)
                            *novellRenamePatch = TRUE; // the next operations will go straight through here
                        if (setAttr || script->CopyAttrs && (attr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                        {
                            CStrP targetNameW(ConvertAllocUtf8ToWide(targetNameMvDir, -1));
                            if (targetNameW != NULL)
                                SetFileAttributesW(targetNameW, attr);
                        }

                        return FinishSameVolumeMove(op, hProgressDlg, script, totalDone, dlgData, sourceNameMvDir,
                                                    targetNameMvDir, dir, &dirTimeModified, dirTimeModifiedIsValid,
                                                    setDirTimeAfterMove, &srcSecurity, srcSecurityErr);
                    }
                    err = GetLastError();
                    if (setAttr)
                    {
                        CStrP sourceNameW(ConvertAllocUtf8ToWide(sourceNameMvDir, -1));
                        if (sourceNameW != NULL)
                            SetFileAttributesW(sourceNameW, attr);
                    }
                }

                if (StrICmp(op->SourceName, op->TargetName) != 0 && // provided this is not just a change of case
                    (err == ERROR_FILE_EXISTS ||                    // verify whether this is only overwriting the DOS name of the file/directory
                     err == ERROR_ALREADY_EXISTS) &&
                    targetNameMvDir == op->TargetName) // no invalid names are allowed here
                {
                    WIN32_FIND_DATAW findData;
                    CStrP targetNameW(ConvertAllocUtf8ToWide(op->TargetName, -1));
                    HANDLE find = targetNameW != NULL ? HANDLES_Q(FindFirstFileW(targetNameW, &findData)) : INVALID_HANDLE_VALUE;
                    if (find != INVALID_HANDLE_VALUE)
                    {
                        HANDLES(FindClose(find));
                        const char* tgtName = SalPathFindFileName(op->TargetName);
                        char altName[MAX_PATH];
                        char fullName[MAX_PATH];
                        if (ConvertWideToUtf8(findData.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                            altName[0] = 0;
                        if (ConvertWideToUtf8(findData.cFileName, -1, fullName, _countof(fullName)) == 0)
                            fullName[0] = 0;
                        if (StrICmp(tgtName, altName) == 0 && // match only on the DOS name
                            StrICmp(tgtName, fullName) != 0)  // (the full name is different)
                        {
                            // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (does not need an extra DOS name)
                            char tmpName[MAX_PATH + 20];
                            if (strlen(op->TargetName) >= _countof(tmpName))
                            {
                                TRACE_E("DoMoveFile(): target path too long for DOS-name collision workaround: " << op->TargetName);
                            }
                            else
                            {
                                CPathW targetBuf;
                                CPathW tmpDirW(op->GetTargetNameW(targetBuf));
                                CutDirectoryW(tmpDirW.GetBuffer(tmpDirW.GetLength() + 1));
                                tmpDirW.ReleaseBuffer();
                                tmpDirW.AddBackslash();
                                CPathW origFullNameW(tmpDirW);
                                origFullNameW.Append(fullName);
                                origFullNameW.ToUtf8(tmpName, sizeof(tmpName));
                                char origFullName[MAX_PATH + 20];
                                StringCchCopyA(origFullName, _countof(origFullName), tmpName);
                                char* tmpNamePart = tmpName + strlen(tmpName);
                                DWORD num = GetTemporaryNameSeed();
                                DWORD origFullNameAttr = SalGetFileAttributes(origFullName);
                                    while (1)
                                    {
                                        sprintf(tmpNamePart, "sal%03X", num++);
                                        if (SalMoveFile(origFullName, tmpName))
                                            break;
                                        DWORD e = GetLastError();
                                        if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                        {
                                            tmpName[0] = 0;
                                            break;
                                        }
                                    }
                                    if (tmpName[0] != 0) // if we managed to "tidy up" the conflicting file/directory, try moving it again
                                    {                    // then restore the original name of the "tidied" file/directory
                                        BOOL moveDone = SalMoveFile(sourceNameMvDir, op->TargetName);
                                        if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // the Archive attribute was not set; MoveFile turned it on, clear it again
                                        {
                                            CStrP targetNameW2(ConvertAllocUtf8ToWide(op->TargetName, -1));
                                            if (targetNameW2 != NULL)
                                                SetFileAttributesW(targetNameW2, op->Attr); // leave without handling or retry, not important (it normally toggles chaotically)
                                        }
                                        if (!SalMoveFile(tmpName, origFullName))
                                        { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of op->TargetName (the DOS name)
                                            TRACE_I("DoMoveFile(): Unexpected situation: unable to rename file/dir from tmp-name to original long file name! " << origFullName);
                                            if (moveDone)
                                            {
                                                if (SalMoveFile(op->TargetName, sourceNameMvDir))
                                                    moveDone = FALSE;
                                                if (!SalMoveFile(tmpName, origFullName))
                                                    TRACE_E("DoMoveFile(): Fatal unexpected situation: unable to rename file/dir from tmp-name to original long file name! " << origFullName);
                                            }
                                        }
                                        else
                                        {
                                            if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                                SetFileAttributesUtf8(origFullName, origFullNameAttr); // leave without handling or retry, not important (it normally toggles chaotically)
                                        }

                                        if (moveDone)
                                            return FinishSameVolumeMove(op, hProgressDlg, script, totalDone, dlgData,
                                                                        sourceNameMvDir, targetNameMvDir, dir,
                                                                        &dirTimeModified, dirTimeModifiedIsValid,
                                                                        setDirTimeAfterMove, &srcSecurity, srcSecurityErr);
                                    }
                                }
                            }
                        }
                    }

                if ((err == ERROR_ALREADY_EXISTS || // theoretically can happen for directories; prevent that (overwrite prompt is only for files)
                     err == ERROR_FILE_EXISTS) &&
                    !dir && StrICmp(op->SourceName, op->TargetName) != 0 &&
                    sourceNameMvDir == op->SourceName && targetNameMvDir == op->TargetName) // no invalid names allowed here (files only, and their names are validated)
                {
                    HANDLE in, out;
                    in = HANDLES_Q(CreateFileUtf8(op->SourceName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
                    if (in == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        goto NORMAL_ERROR;
                    }
                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
                    if (out == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        HANDLES(CloseHandle(in));
                        goto NORMAL_ERROR;
                    }

                    if (!dlgData.OverwriteAll && (dlgData.CnfrmFileOver || script->OverwriteOlder))
                    {
                        char sAttr[101], tAttr[101];
                        BOOL getTimeFailed;
                        getTimeFailed = FALSE;
                        FILETIME sFileTime, tFileTime;
                        GetFileOverwriteInfo(sAttr, _countof(sAttr), in, op->SourceName, &sFileTime, &getTimeFailed);
                        GetFileOverwriteInfo(tAttr, _countof(tAttr), out, op->TargetName, &tFileTime, &getTimeFailed);
                        HANDLES(CloseHandle(in));
                        HANDLES(CloseHandle(out));

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_OPEN;

                        if (dir)
                            TRACE_E("Error in script.");

                        if (dlgData.SkipAllOverwrite)
                            goto SKIP_OPEN;

                        int ret;
                        ret = IDCANCEL;

                        if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                        {
                            // trim timestamps to seconds (different file systems store times with different precision, leading to "differences" even between "matching" times)
                            *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                            *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                            if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                ret = IDYES; // older ones should be overwritten without asking
                            else
                                ret = IDB_SKIP; // skip the other existing ones
                        }
                        else
                        {
                            // display the prompt
                            char* data[5];
                            data[0] = (char*)&ret;
                            data[1] = op->TargetName;
                            data[2] = tAttr;
                            data[3] = op->SourceName;
                            data[4] = sAttr;
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.OverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllOverwrite = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN:

                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                        HANDLES(CloseHandle(in));
                        HANDLES(CloseHandle(out));
                    }

                    DWORD attr = SalGetFileAttributes(op->TargetName);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                    {
                        if (!dlgData.OverwriteHiddenAll && dlgData.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                        {
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (dlgData.SkipAllSystemOrHidden)
                                goto SKIP_OPEN;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_CONFIRMFILEOVERWRITING);
                            data[2] = op->TargetName;
                            data[3] = LoadStr(IDS_WANTOVERWRITESHFILE);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                            switch (ret)
                            {
                            case IDB_ALL:
                                dlgData.OverwriteHiddenAll = TRUE;
                            case IDYES:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllSystemOrHidden = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN;

                            case IDCANCEL:
                                goto CANCEL_OPEN;
                            }
                            attr = SalGetFileAttributes(op->TargetName); // may also fail (returns INVALID_FILE_ATTRIBUTES)
                        }
                    }

                    while (1)
                    {
                        DWORD identityError;
                        if (DeleteFileWithVerifiedIdentity(op->TargetName, op->TargetIdentity, &identityError))
                            break;
                        else
                        {
                            DWORD err2 = identityError;
                            if (err2 == ERROR_FILE_NOT_FOUND)
                                break; // if the user already deleted the file manually, everything is fine

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                return FALSE;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (dlgData.SkipAllOverwriteErr)
                                goto SKIP_OVERWRITE_ERROR;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERROROVERWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err2);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllOverwriteErr = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OVERWRITE_ERROR:

                                totalDone += op->Size;
                                script->SetProgressSize(totalDone);
                                SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                                return TRUE;
                            }

                            case IDCANCEL:
                                return FALSE;
                            }
                        }
                    }

                    // The overwrite delete is now committed.  Subsequent move retries must
                    // require the cleared destination to stay absent, not match its old file ID.
                    memset(&expectedTargetIdentity, 0, sizeof(expectedTargetIdentity));
                    expectedTargetIdentity.State = 1; // FileIdentityAbsent; see CFileIdentity.
                }
                else
                {
                NORMAL_ERROR:

                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllMoveErrors)
                        goto SKIP_MOVE_ERROR;

                    DWORD retryDelay;
                    if (PrepareAutomaticRetry(err, &autoRetryAttempts, rokDestructiveCommit,
                                              script->GetCancellationEvent(), &retryDelay))
                    { // The policy currently rejects this branch: move commit idempotency is not proven.
                        script->RecordItemRetry();
                        if (!WaitForAutomaticRetry(script->GetCancellationEvent(), retryDelay))
                            return FALSE;
                    }
                    else
                    {
                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = op->SourceName;
                        data[2] = op->TargetName;
                        data[3] = GetErrorText(err);
                        SendMessage(hProgressDlg, WM_USER_DIALOG, dir ? 4 : 3, (LPARAM)data);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllMoveErrors = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_MOVE_ERROR:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                            return TRUE;
                        }

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (dir)
        {
            TRACE_E("Error in script.");
            return FALSE;
        }

        BOOL skip;
        BOOL suspiciousIoRetry;
        DWORD err = NO_ERROR;
        BOOL notError = DoCopyFile(op, hProgressDlg, buffer, script, totalDone,
                                   clearReadonlyMask, &skip, lantasticCheck,
                                   mustDeleteFileBeforeOverwrite, allocWholeFileOnStart,
                                   dlgData, copyADS, copyAsEncrypted, TRUE, asyncPar, &suspiciousIoRetry);
        if (notError && !skip) // still need to clean up the file from the source
        {
            RecordPlannedMetadataLosses(dlgData, script, op->SourceName, op->TargetName);
            if (!ConfirmMetadataLossesBeforeSourceDeletion(hProgressDlg, dlgData, op->SourceName, op->TargetName))
                return TRUE; // target is complete; the user retained the move source

            // Retry-resume and retry-copy paths are evidence that the I/O path
            // was unstable.  Re-read both closed files and compare their full
            // SHA-256 digests before this move is allowed to delete its source.
            while (suspiciousIoRetry && !VerifyFullFileContentSha256(op->SourceName, op->TargetName, &err))
            {
                TRACE_I("DoMoveFile(): full SHA-256 verification failed for " << op->TargetName << ": " << GetErrorText(err));
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE);
                if (*dlgData.CancelWorker)
                    return FALSE;

                if (dlgData.SkipAllDeleteErr)
                    return TRUE; // retain the source; the verified delete precondition was not met

                int ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORDELETINGFILE);
                data[2] = op->SourceName;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDeleteErr = TRUE;
                case IDB_SKIP:
                    return TRUE; // retain the source; the verified delete precondition was not met

                case IDCANCEL:
                    return FALSE;
                }
            }

                    while (1)
                    {
                if (DeleteFileWithVerifiedIdentity(op->SourceName, op->SourceIdentity, &err))
                    break;
                {
                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllDeleteErr)
                        return TRUE;

                    int ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_ERRORDELETINGFILE);
                    data[2] = op->SourceName;
                    data[3] = GetErrorText(err);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;
                    case IDB_SKIPALL:
                        dlgData.SkipAllDeleteErr = TRUE;
                    case IDB_SKIP:
                        return TRUE;
                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
        }
        return notError;
    }
}

BOOL DoDeleteFile(HWND hProgressDlg, COperation* operation, const CQuadWord& size, COperations* script,
                  CQuadWord& totalDone, DWORD attr, CProgressDlgData& dlgData)
{
    char* name = operation->SourceName;
    // if the path ends with a space/dot it is invalid and we must not delete it,
    // DeleteFile would trim the spaces/dots and remove a different file
    BOOL invalidName = FileNameIsInvalid(name, TRUE);

    DWORD err;
    while (1)
    {
        if (!invalidName)
        {
            if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            {
                if (!dlgData.DeleteHiddenAll && dlgData.CnfrmSHFileDel)
                {
                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllSystemOrHidden)
                        goto SKIP_DELETE;

                    int ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_CONFIRMSHFILEDELETE);
                    data[2] = name;
                    data[3] = LoadStr(IDS_DELETESHFILE);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                    switch (ret)
                    {
                    case IDB_ALL:
                        dlgData.DeleteHiddenAll = TRUE;
                    case IDYES:
                        break;

                    case IDB_SKIPALL:
                        dlgData.SkipAllSystemOrHidden = TRUE;
                    case IDB_SKIP:
                        goto SKIP_DELETE;

                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
            err = ERROR_SUCCESS;
            BOOL useRecycleBin;
            switch (dlgData.UseRecycleBin)
            {
            case 0:
                useRecycleBin = script->CanUseRecycleBin && script->InvertRecycleBin;
                break;
            case 1:
                useRecycleBin = script->CanUseRecycleBin && !script->InvertRecycleBin;
                break;
            case 2:
            {
                if (!script->CanUseRecycleBin || script->InvertRecycleBin)
                    useRecycleBin = FALSE;
                else
                {
                    const char* fileName = strrchr(name, '\\');
                    if (fileName != NULL) // "always true"
                    {
                        fileName++;
                        int tmpLen = (int)strlen(fileName);
                        const char* ext = fileName + tmpLen;
                        //            while (ext > fileName && *ext != '.') ext--;
                        while (--ext >= fileName && *ext != '.')
                            ;
                        //            if (ext == fileName)   // ".cvspass" is treated as an extension in Windows ...
                        if (ext < fileName)
                            ext = fileName + tmpLen;
                        else
                            ext++;
                        useRecycleBin = dlgData.AgreeRecycleMasks(fileName, ext);
                    }
                    else
                    {
                        useRecycleBin = TRUE; // choose the safe option on error and delete via the Recycle Bin
                        TRACE_E("DoDeleteFile(): unexpected situation: filename does not contain backslash: " << name);
                    }
                }
                break;
            }
            }
            if (useRecycleBin)
            {
                // The shell accepts only a name, so it cannot consume our
                // verified handle. Recheck immediately before handing that
                // name to the shell and only then adjust its attributes. The
                // check opens for deletion so a file another process holds open
                // is reported here, through this operation's own error dialog,
                // instead of by whatever UI the shell decides to raise.
                if (!VerifyFileDeletable(name, operation->SourceIdentity, &err))
                    goto DELETE_READY;
                ClearReadOnlyAttr(name, attr);
                if (!PathContainsValidComponents((char*)name, FALSE))
                {
                    err = ERROR_INVALID_NAME;
                }
                else
                {
                    // The dedicated STA executor preserves silent Recycle Bin deletion without SHFileOperation's double-NUL list.
                    DeleteThroughRecycleBin(hProgressDlg, name, &err);
                }
            }
            else
            {
                DeleteFileWithVerifiedIdentity(name, operation->SourceIdentity, &err);
            }
        DELETE_READY:
            ; // A label must own a statement even when identity rejection only skips deletion.
        }
        else
        {
            err = ERROR_INVALID_NAME;
        }
        if (err == ERROR_SUCCESS)
        {
            totalDone += size;
            SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDeleteErr)
                goto SKIP_DELETE;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORDELETINGFILE);
            data[2] = name;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllDeleteErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_DELETE:

                totalDone += size;
                SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (!invalidName)
        {
            DWORD attr2 = SalGetFileAttributes(name); // get the current attribute state
            if (attr2 != INVALID_FILE_ATTRIBUTES)
                attr = attr2;
        }
    }
}

BOOL SalCreateDirectoryEx(const char* name, DWORD* err)
{
    if (err != NULL)
        *err = 0;
    // if the name ends with a space/dot we must append '\\', otherwise CreateDirectory
    // quietly trims the trailing spaces/dots and creates a different directory
    const char* nameCrDir = name;
    char nameCrDirBuf[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameCrDir, nameCrDirBuf);
    CStrP nameCrDirW(ConvertAllocUtf8ToWide(nameCrDir, -1));
    if (nameCrDirW == NULL)
    {
        if (err != NULL)
            *err = ERROR_NO_UNICODE_TRANSLATION;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return FALSE;
    }
    if (CreateDirectoryW(nameCrDirW, NULL))
        return TRUE;
    else
    {
        DWORD errLoc = GetLastError();
        if (name == nameCrDir &&            // a name ending with a space/dot cannot collide with a DOS name
            (errLoc == ERROR_FILE_EXISTS || // check whether this is only overwriting the file's DOS name
             errLoc == ERROR_ALREADY_EXISTS))
        {
            WIN32_FIND_DATAW data;
            CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
            HANDLE find = nameW != NULL ? HANDLES_Q(FindFirstFileW(nameW, &data)) : INVALID_HANDLE_VALUE;
            if (find != INVALID_HANDLE_VALUE)
            {
                HANDLES(FindClose(find));
                const char* tgtName = SalPathFindFileName(name);
                char altName[MAX_PATH];
                char fullName[MAX_PATH];
                if (ConvertWideToUtf8(data.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                    altName[0] = 0;
                if (ConvertWideToUtf8(data.cFileName, -1, fullName, _countof(fullName)) == 0)
                    fullName[0] = 0;
                if (StrICmp(tgtName, altName) == 0 && // match only for the DOS name
                    StrICmp(tgtName, fullName) != 0)  // (the full name differs)
                {
                    // rename ("tidy up") the file/directory whose DOS name conflicts to a temporary 8.3 name (no extra DOS name needed)
                    char tmpName[MAX_PATH + 20];
                    if (strlen(name) >= _countof(tmpName))
                    {
                        TRACE_E("SalCreateDirectoryEx(): path too long for DOS-name collision workaround: " << name);
                    }
                    else
                    {
                        CPathW tmpDirW(nameW != NULL ? CPathW(nameW.Ptr) : CPathW(name));
                        CutDirectoryW(tmpDirW.GetBuffer(tmpDirW.GetLength() + 1));
                        tmpDirW.ReleaseBuffer();
                        tmpDirW.AddBackslash();
                        CPathW origFullNameW(tmpDirW);
                        origFullNameW.Append(fullName);
                        origFullNameW.ToUtf8(tmpName, sizeof(tmpName));
                        char origFullName[MAX_PATH + 20];
                        StringCchCopyA(origFullName, _countof(origFullName), tmpName);
                        char* tmpNamePart = tmpName + strlen(tmpName);
                        DWORD num = GetTemporaryNameSeed();
                        DWORD origFullNameAttr = SalGetFileAttributes(origFullName);
                            while (1)
                            {
                                sprintf(tmpNamePart, "sal%03X", num++);
                                if (SalMoveFile(origFullName, tmpName))
                                    break;
                                DWORD e = GetLastError();
                                if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                {
                                    tmpName[0] = 0;
                                    break;
                                }
                            }
                            if (tmpName[0] != 0) // if we managed to "tidy up" the conflicting file, retry the move
                            {                    // and then restore the original name of the "tidied" file
                                BOOL createDirDone = nameW != NULL ? CreateDirectoryW(nameW, NULL) : FALSE;
                                if (!SalMoveFile(tmpName, origFullName))
                                { // this can apparently happen: inexplicably Windows creates a file named origFullName instead of name (the DOS name)
                                    TRACE_I("Unexpected situation: unable to rename file from tmp-name to original long file name! " << origFullName);
                                    if (createDirDone)
                                    {
                                        if (nameW != NULL && RemoveDirectoryW(nameW))
                                            createDirDone = FALSE;
                                        if (!SalMoveFile(tmpName, origFullName))
                                            TRACE_E("Fatal unexpected situation: unable to rename file from tmp-name to original long file name! " << origFullName);
                                    }
                                }
                                else
                                {
                                    if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                    {
                                        CStrP origFullNameW(ConvertAllocUtf8ToWide(origFullName, -1));
                                        if (origFullNameW != NULL)
                                            SetFileAttributesW(origFullNameW, origFullNameAttr); // leave it without extra handling or retries; not important (normally toggles unpredictably)
                                    }
                                }

                                if (createDirDone)
                                    return TRUE;
                            }
                        }
                    }
                }
            }
        if (err != NULL)
            *err = errLoc;
        return FALSE;
    }
}

BOOL GetDirTime(const char* dirName, FILETIME* ftModified)
{
    HANDLE dir;
    CStrP dirNameW(ConvertAllocUtf8ToWide(dirName, -1));
    dir = dirNameW != NULL
              ? HANDLES_Q(CreateFileW(dirNameW, GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS,
                                      NULL))
              : INVALID_HANDLE_VALUE;
    if (dir != INVALID_HANDLE_VALUE)
    {
        BOOL ret = GetFileTime(dir, NULL /*ftCreated*/, NULL /*ftAccessed*/, ftModified);
        HANDLES(CloseHandle(dir));
        return ret;
    }
    return FALSE;
}

BOOL DoCopyDirTime(HWND hProgressDlg, const char* targetName, FILETIME* modified, CProgressDlgData& dlgData, BOOL quiet)
{
    // if the path ends with a space/dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* targetNameCrFile = targetName;
    char targetNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(targetNameCrFile, targetNameCrFileCopy);
    CStrP targetNameW(ConvertAllocUtf8ToWide(targetNameCrFile, -1));

    BOOL showError = !quiet;
    DWORD error = NO_ERROR;
    DWORD attr = targetNameW != NULL ? GetFileAttributesW(targetNameW) : INVALID_FILE_ATTRIBUTES;
    BOOL setAttr = FALSE;
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
    {
        if (targetNameW != NULL)
            SetFileAttributesW(targetNameW, attr & ~FILE_ATTRIBUTE_READONLY);
        setAttr = TRUE;
    }
    HANDLE file;
    file = targetNameW != NULL
               ? HANDLES_Q(CreateFileW(targetNameW, GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL))
               : INVALID_HANDLE_VALUE;
    if (file != INVALID_HANDLE_VALUE)
    {
        if (SetFileTime(file, NULL /*&ftCreated*/, NULL /*&ftAccessed*/, modified))
            showError = FALSE; // success!
        else
            error = GetLastError();
        HANDLES(CloseHandle(file));
    }
    else
        error = GetLastError();
    if (setAttr)
    {
        if (targetNameW != NULL)
            SetFileAttributesW(targetNameW, attr);
    }

    if (showError)
    {
        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        if (*dlgData.CancelWorker)
            return FALSE;

        int ret;
        ret = IDCANCEL;
        if (dlgData.IgnoreAllCopyDirTimeErr)
            ret = IDB_IGNORE;
        else
        {
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = (char*)targetNameCrFile;
            data[2] = (char*)(DWORD_PTR)error;
            SendMessage(hProgressDlg, WM_USER_DIALOG, 11, (LPARAM)data);
        }
        switch (ret)
        {
        case IDB_IGNOREALL:
            dlgData.IgnoreAllCopyDirTimeErr = TRUE; // break intentionally omitted here
        case IDB_IGNORE:
            RecordMetadataLoss(dlgData, mmlLastWriteTime, NULL, targetNameCrFile);
            break;

        case IDCANCEL:
            return FALSE;
        }
    }
    return TRUE;
}

// Creates one target directory for the copy/move script: creates the
// directory (SalCreateDirectoryEx), applies cleared-readonly attributes,
// optionally copies alternate data streams from 'sourceDir', sets compression
// /encryption attributes, and handles the already-exists case as a
// user-confirmable overwrite ('alreadyExisted') with retry/skip/cancel loop.
// Progress is accounted with the synthetic CREATE_DIR_SIZE so the summary
// bar moves for directory-only operations. Returns FALSE on cancel/error;
// 'skip' reports user Skip.
BOOL DoCreateDir(HWND hProgressDlg, char* name, DWORD attr,
                 DWORD clearReadonlyMask, CProgressDlgData& dlgData,
                 CQuadWord& totalDone, CQuadWord& operTotal,
                 const char* sourceDir, BOOL adsCopy, COperations* script,
                 void* buffer, BOOL& skip, BOOL& alreadyExisted,
                 BOOL createAsEncrypted, BOOL ignInvalidName)
{
    if (script->CopyAttrs && createAsEncrypted)
        TRACE_E("DoCreateDir(): unexpected parameter value: createAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    skip = FALSE;
    alreadyExisted = FALSE;
    CQuadWord lastTransferredFileSize;
    script->GetTFS(&lastTransferredFileSize);

    BOOL invalidName = FileNameIsInvalid(name, TRUE, ignInvalidName);

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameCrDir = name;
    char nameCrDirCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameCrDir, nameCrDirCopy);
    const char* sourceDirCrDir = sourceDir;
    char sourceDirCrDirCopy[3 * MAX_PATH];
    if (sourceDirCrDir != NULL)
        MakeCopyWithBackslashIfNeeded(sourceDirCrDir, sourceDirCrDirCopy);

    while (1)
    {
        DWORD err;
        if (!invalidName && SalCreateDirectoryEx(name, &err))
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)CREATE_DIR_SIZE.Value, TRUE, 0, NULL, MAX_OP_FILESIZE); // directory already created

            DWORD newAttr = attr & clearReadonlyMask;
            if (sourceDir != NULL && adsCopy) // copy ADS when required
            {
                CQuadWord operDone = CREATE_DIR_SIZE; // directory already created
                BOOL adsSkip = FALSE;
                if (!DoCopyADS(hProgressDlg, sourceDir, TRUE, name, totalDone,
                               operDone, operTotal, dlgData, script, &adsSkip, buffer) ||
                    adsSkip) // user cancelled or skipped at least one ADS
                {
                    if (RemoveDirectoryUtf8(nameCrDir) == 0)
                    {
                        DWORD err2 = GetLastError();
                        TRACE_E("Unable to remove newly created directory: " << name << ", error: " << GetErrorText(err2));
                    }
                    if (!adsSkip)
                        return FALSE; // cancel the entire operation (Skip must return TRUE)
                    skip = TRUE;
                    newAttr = -1; // the directory should no longer exist, so do not apply attributes
                }
            }
            if (newAttr != -1)
            {
                if (script->CopyAttrs || createAsEncrypted) // set Compressed & Encrypted attributes based on the source directory
                {
                    if (createAsEncrypted)
                    {
                        newAttr &= ~FILE_ATTRIBUTE_COMPRESSED;
                        newAttr |= FILE_ATTRIBUTE_ENCRYPTED;
                    }
                    DWORD changeAttrErr = NO_ERROR;
                    DWORD currentAttrs = SalGetFileAttributes(name);
                    if (currentAttrs != INVALID_FILE_ATTRIBUTES)
                    {
                        if ((newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) == 0)
                        {
                            changeAttrErr = UncompressFile(name, currentAttrs);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_ENCRYPTED) != (currentAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                        {
                            BOOL dummyCancelOper = FALSE;
                            if (newAttr & FILE_ATTRIBUTE_ENCRYPTED)
                            {
                                changeAttrErr = MyEncryptFile(hProgressDlg, name, currentAttrs, 0 /* allow encrypting directories with the SYSTEM attribute */,
                                                              dlgData, dummyCancelOper, FALSE);

                                if ( //(WindowsVistaAndLater || script->TargetPathSupEFS) &&  // complain regardless of OS version and EFS support; originally directories on FAT could not be encrypted before Vista, we behave the same (to match Explorer, the Encrypted attribute is not that important)
                                    !dlgData.DirCrLossEncrAll && changeAttrErr != ERROR_SUCCESS)
                                {                                                              // failed to set the Encrypted attribute on the directory, ask the user what to do
                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto CANCEL_CRDIR;

                                    int ret;
                                    if (dlgData.SkipAllDirCrLossEncr)
                                        ret = IDB_SKIP;
                                    else
                                    {
                                        ret = IDCANCEL;
                                        char* data[4];
                                        data[0] = (char*)&ret;
                                        data[1] = (char*)FALSE;
                                        data[2] = name;
                                        data[3] = (char*)(!script->IsCopyOperation);
                                        SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                    }
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        dlgData.DirCrLossEncrAll = TRUE; // break intentionally omitted here
                                    case IDYES:
                                        RecordMetadataLoss(dlgData, mmlCompressionAndEncryption, sourceDir, name);
                                        break;

                                    case IDB_SKIPALL:
                                        dlgData.SkipAllDirCrLossEncr = TRUE;
                                    case IDB_SKIP:
                                    {
                                        ClearReadOnlyAttr(nameCrDir); // remove read-only attribute so the file can be deleted
                                        RemoveDirectoryUtf8(nameCrDir);
                                        script->SetTFS(lastTransferredFileSize); // add TFS only after the directory is fully outside; ProgressSize will be synced outside (no point in adjusting it here)
                                        skip = TRUE;
                                        return TRUE;
                                    }

                                    case IDCANCEL:
                                        goto CANCEL_CRDIR;
                                    }
                                }
                            }
                            else
                                changeAttrErr = MyDecryptFile(name, currentAttrs, FALSE);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != 0)
                        {
                            changeAttrErr = CompressFile(name, currentAttrs);
                        }
                    }
                    else
                        changeAttrErr = GetLastError();
                    if (changeAttrErr != NO_ERROR)
                    {
                        TRACE_I("DoCreateDir(): Unable to set Encrypted or Compressed attributes for " << name << "! error=" << GetErrorText(changeAttrErr));
                    }
                }
                SetFileAttributesUtf8(nameCrDir, newAttr);

                if (script->CopyAttrs) // verify whether the source file attributes were preserved
                {
                    DWORD curAttrs;
                    curAttrs = SalGetFileAttributes(name);
                    if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (newAttr & DISPLAYED_ATTRIBUTES))
                    {                                                              // attributes probably did not transfer; warn the user
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllSetAttrsErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = name;
                            data[2] = (char*)(DWORD_PTR)(newAttr & DISPLAYED_ATTRIBUTES);
                            data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllSetAttrsErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            RecordMetadataLoss(dlgData, mmlAttributes, sourceDir, name);
                            break;

                        case IDCANCEL:
                        {
                        CANCEL_CRDIR:

                            ClearReadOnlyAttr(nameCrDir); // remove read-only so the file can be deleted
                            RemoveDirectoryUtf8(nameCrDir);
                            return FALSE;
                        }
                        }
                    }
                }

                if (sourceDir != NULL && script->CopySecurity) // should NTFS security permissions be copied?
                {
                    DWORD err2;
                    if (!DoCopySecurity(sourceDir, name, &err2, NULL))
                    {
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllCopyPermErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = (char*)sourceDir;
                            data[2] = name;
                            data[3] = (char*)(DWORD_PTR)err2;
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllCopyPermErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            RecordMetadataLoss(dlgData, mmlSecurity, sourceDir, name);
                            break;

                        case IDCANCEL:
                            goto CANCEL_CRDIR;
                        }
                    }
                }
            }
            return TRUE;
        }
        else
        {
            if (invalidName)
                err = ERROR_INVALID_NAME;
            if (err == ERROR_ALREADY_EXISTS ||
                err == ERROR_FILE_EXISTS)
            {
                DWORD attr2 = SalGetFileAttributes(name);
                if (attr2 & FILE_ATTRIBUTE_DIRECTORY) // "directory overwrite"
                {
                    if (dlgData.CnfrmDirOver && !dlgData.DirOverwriteAll) // should we ask the user about overwriting the directory?
                    {
                        char sAttr[101], tAttr[101];
                        GetDirInfo(sAttr, _countof(sAttr), sourceDir);
                        GetDirInfo(tAttr, _countof(tAttr), name);

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            return FALSE;

                        if (dlgData.SkipAllDirOver)
                            goto SKIP_CREATE_ERROR;

                        int ret = IDCANCEL;
                        char* data[5];
                        data[0] = (char*)&ret;
                        data[1] = name;
                        data[2] = tAttr;
                        data[3] = (char*)sourceDir;
                        data[4] = sAttr;
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 7, (LPARAM)data);
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.DirOverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllDirOver = TRUE;
                        case IDB_SKIP:
                            goto SKIP_CREATE_ERROR;

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                    alreadyExisted = TRUE;
                    return TRUE; // o.k.
                }

                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                    return FALSE;

                if (dlgData.SkipAllDirCreate)
                    goto SKIP_CREATE_ERROR;

                int ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORCREATINGDIR);
                data[2] = name;
                data[3] = LoadStr(IDS_NAMEALREADYUSED);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDirCreate = TRUE;
                case IDB_SKIP:
                    goto SKIP_CREATE_ERROR;

                case IDCANCEL:
                    return FALSE;
                }
                continue;
            }

            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDirCreateErr)
                goto SKIP_CREATE_ERROR;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORCREATINGDIR);
            data[2] = name;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllDirCreateErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_CREATE_ERROR:

                skip = TRUE; // this is a skip (all operations within the directory must be skipped)
                return TRUE;
            }
            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoDeleteDir(HWND hProgressDlg, COperation* operation, const CQuadWord& size, COperations* script,
                 CQuadWord& totalDone, DWORD attr, BOOL dontUseRecycleBin, CProgressDlgData& dlgData)
{
    char* name = operation->SourceName;
    DWORD err;
    int AutoRetryCounter = 0;

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameRmDir = name;
    char nameRmDirCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameRmDir, nameRmDirCopy);

    while (1)
    {
        err = ERROR_SUCCESS;
        if (script->CanUseRecycleBin && !dontUseRecycleBin &&
            (script->InvertRecycleBin && dlgData.UseRecycleBin == 0 ||
             !script->InvertRecycleBin && dlgData.UseRecycleBin == 1) &&
            IsDirectoryEmpty(name)) // subdirectory must not contain any files!!!
        {
            if (!VerifyFileDeletable(nameRmDir, operation->SourceIdentity, &err))
                goto DELETE_DIR_READY;
            ClearReadOnlyAttr(nameRmDir, attr);
            if (!PathContainsValidComponents((char*)name, FALSE))
            {
                err = ERROR_INVALID_NAME;
            }
            else
            {
                // Directory Recycle Bin deletion follows the same STA path as files to retain its existing error handling.
                DeleteThroughRecycleBin(hProgressDlg, name, &err);
            }
        }
        else
        {
            DeleteFileWithVerifiedIdentity(nameRmDir, operation->SourceIdentity, &err);
        }

    DELETE_DIR_READY:
        if (err == ERROR_SUCCESS)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

            totalDone += size;
            SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDeleteErr)
                goto SKIP_DELETE;

            DWORD retryDelay;
            if (PrepareAutomaticRetry(err, &AutoRetryCounter, rokDestructiveCommit,
                                      script->GetCancellationEvent(), &retryDelay))
            { // Directory deletion has no idempotency proof, so the central policy rejects automatic retries.
                script->RecordItemRetry();
                if (!WaitForAutomaticRetry(script->GetCancellationEvent(), retryDelay))
                    return FALSE;
            }
            else
            {
                int ret;
                ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORDELETINGDIR);
                data[2] = (char*)nameRmDir;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDeleteErr = TRUE;
                case IDB_SKIP:
                {
                SKIP_DELETE:

                    totalDone += size;
                    script->SetProgressSize(totalDone);
                    SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone, script->TotalSize), dlgData);
                    return TRUE;
                }

                case IDCANCEL:
                    return FALSE;
                }
            }
        }

        DWORD attr2 = SalGetFileAttributes(nameRmDir); // get the current attribute state
        if (attr2 != INVALID_FILE_ATTRIBUTES)
            attr = attr2;
    }
}

#define FSCTL_GET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS)        // REPARSE_DATA_BUFFER
#define FSCTL_DELETE_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 43, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) // REPARSE_DATA_BUFFER,

#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)

/*  This code copies a junction point into an empty directory (the directory must be created in
    advance � to keep it simple we always use "D:\\ZUMPA\\link" here).

  People sometimes want to copy the contents of the junction, sometimes they want to copy only the junction as a link,
  and sometimes they want to skip it (unclear whether that should create an empty junction directory)...
  if we ever implement it properly, the script builder will need a comprehensive dialog asking what to do.

#define FSCTL_SET_REPARSE_POINT     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 41, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) // REPARSE_DATA_BUFFER,
#define FSCTL_GET_REPARSE_POINT     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS) // REPARSE_DATA_BUFFER

// Structure for FSCTL_SET_REPARSE_POINT, FSCTL_GET_REPARSE_POINT, and
// FSCTL_DELETE_REPARSE_POINT.
// This version of the reparse data buffer is only for Microsoft tags.
*/

