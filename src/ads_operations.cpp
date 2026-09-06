// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <Ntsecapi.h> // LsaNtStatusToWinError

#include "file_operation_filesystem.h"
#include "worker.h"

#include "async_copy_internals.h"

// Alternate-data-stream detection/deletion and ADS copying extracted from
// async_copy.cpp as a mechanical move. CheckFileOrDirADS remains declared in
// worker.h; the rest of the ADS surface is declared in async_copy_internals.h.
// NOTE: the trailing default argument of DoCopyADS moved to that declaration,
// because a definition-visible default no longer covers callers in async_copy.cpp.
BOOL DeleteAllADS(HANDLE file, const char* fileName)
{
    if (DynNtQueryInformationFile != NULL) // "always true"
    {
        // get stream info
        NTSTATUS uStatus;
        IO_STATUS_BLOCK ioStatus;
        BYTE buffer[65535]; // Windows XP cannot handle more than 65535 (no idea why)
        uStatus = DynNtQueryInformationFile(file, &ioStatus, buffer, sizeof(buffer), FileStreamInformation);
        if (uStatus != 0 /* anything other than success is an error (including warnings) */)
        {
            DWORD err;
            if (uStatus == STATUS_BUFFER_OVERFLOW)
                err = ERROR_INSUFFICIENT_BUFFER;
            else
                err = LsaNtStatusToWinError(uStatus);
            TRACE_I("DeleteAllADS(" << fileName << "): NtQueryInformationFile failed: " << GetErrorText(err));
            return FALSE;
        }

        // iterate through the streams
        PFILE_STREAM_INFORMATION psi = (PFILE_STREAM_INFORMATION)buffer;
        if (ioStatus.Information > 0) // verify that we received any data at all
        {
            WCHAR adsFullName[2 * MAX_PATH];
            adsFullName[0] = 0;
            WCHAR* adsPart = NULL;
            int adsPartSize = 0;
            while (1)
            {
                if (psi->NameLength != 7 * 2 || _memicmp(psi->Name, L"::$DATA", 7 * 2)) // ignore default stream
                {
                    if (adsFullName[0] == 0) // convert the file name only when needed for the first time to save CPU time
                    {
                        if (ConvertA2U(fileName, -1, adsFullName, 2 * MAX_PATH) == 0)
                            return FALSE; // "always false"
                        adsPart = adsFullName + wcslen(adsFullName);
                        adsPartSize = (int)((adsFullName + 2 * MAX_PATH) - adsPart);
                        if (adsPartSize > 0)
                        {
                            *adsPart++ = L':';
                            adsPartSize--;
                        }
                        else
                            return FALSE; // "always false"
                    }
                    WCHAR* start = (WCHAR*)psi->Name;
                    WCHAR* nameEnd = (WCHAR*)((char*)psi->Name + psi->NameLength);
                    if (start < nameEnd && *start == L':')
                        start++;
                    WCHAR* end = start;
                    while (end < nameEnd && *end != L':')
                        end++;
                    if (end - start >= adsPartSize)
                    {
                        TRACE_I("DeleteAllADS(" << fileName << "): too long ADS name!");
                        return FALSE;
                    }
                    if (end > start)
                    {
                        memcpy(adsPart, start, (end - start) * sizeof(WCHAR));
                        adsPart[end - start] = 0;
                        if (!DeleteFileW(adsFullName))
                        {
                            DWORD err = GetLastError();
                            TRACE_IW(L"DeleteAllADS(" << adsFullName << L"): DeleteFile has failed: " << GetErrorTextW(err));
                            return FALSE;
                        }
                    }
                }
                if (psi->NextEntry == 0)
                    break;
                psi = (PFILE_STREAM_INFORMATION)((BYTE*)psi + psi->NextEntry); // move to next item
            }
        }
    }
    return TRUE;
}

void MyStrCpyNW(wchar_t* s1, wchar_t* s2, int maxChars)
{
    if (maxChars == 0)
        return;
    while (--maxChars && *s2 != 0)
        *s1++ = *s2++;
    *s1 = 0;
}

void CutADSNameSuffix(char* s)
{
    char* end = strrchr(s, ':');
    if (end != NULL && stricmp(end, ":$DATA") == 0)
        *end = 0;
}

// conversion to the extended-path variant, see the MSDN article "File Name Conventions"
void DoLongName(char* buf, const char* name, int bufSize)
{
    if (*name == '\\')
        _snprintf_s(buf, bufSize, _TRUNCATE, "\\\\?\\UNC%s", name + 1); // UNC
    else
        _snprintf_s(buf, bufSize, _TRUNCATE, "\\\\?\\%s", name); // standard path
}

// copies ADS into the newly created file/directory
// returns FALSE only when cancelled; success + Skip both return TRUE; Skip sets 'skip'
// (when not NULL) to TRUE
// 'optimalBufferSize' is the pre-computed optimal buffer size (0 = use default based on script flags)
BOOL DoCopyADS(HWND hProgressDlg, const char* sourceName, BOOL isDir, const char* targetName,
               CQuadWord const& totalDone, CQuadWord& operDone, CQuadWord const& operTotal,
               CProgressDlgData& dlgData, COperations* script, BOOL* skip, void* buffer,
               int optimalBufferSize, BOOL stableMoveSource)
{
    BOOL doCopyADSRet = TRUE;
    BOOL lowMemory;
    DWORD adsWinError;
    wchar_t** streamNames;
    int streamNamesCount;
    BOOL skipped = FALSE;
    CQuadWord lastTransferredFileSize, finalTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);
    finalTransferredFileSize = lastTransferredFileSize;
    if (operTotal > operDone) // it should always be at least equal, but we play it safe...
        finalTransferredFileSize += (operTotal - operDone);

COPY_ADS_AGAIN:

    if (CheckFileOrDirADS(sourceName, isDir, NULL, &streamNames, &streamNamesCount,
                          &lowMemory, &adsWinError, 0, NULL, NULL) &&
        !lowMemory && streamNames != NULL)
    {                                  // we have the list of ADS, let's try to copy them to the target file/directory
        wchar_t srcName[2 * MAX_PATH]; // MAX_PATH for the file name as well as the ADS name (no idea what the actual maximum lengths are)
        wchar_t tgtName[2 * MAX_PATH];
        char longSourceName[MAX_PATH + 100];
        char longTargetName[MAX_PATH + 100];
        DoLongName(longSourceName, sourceName, MAX_PATH + 100);
        DoLongName(longTargetName, targetName, MAX_PATH + 100);
        if (!ConvertUtf8ToWide(longSourceName, -1, srcName, 2 * MAX_PATH))
            srcName[0] = 0;
        if (!ConvertUtf8ToWide(longTargetName, -1, tgtName, 2 * MAX_PATH))
            tgtName[0] = 0;
        wchar_t* srcEnd = srcName + wcslen(srcName);
        if (srcEnd > srcName && *(srcEnd - 1) == L'\\')
            *--srcEnd = 0;
        wchar_t* tgtEnd = tgtName + wcslen(tgtName);
        if (tgtEnd > tgtName && *(tgtEnd - 1) == L'\\')
            *--tgtEnd = 0;

        // Use pre-computed buffer size if provided, otherwise fall back to default logic
        int bufferSize = (optimalBufferSize > 0) ? optimalBufferSize :
            (script->RemovableSrcDisk || script->RemovableTgtDisk ? REMOVABLE_DISK_COPY_BUFFER : OPERATION_BUFFER);

        char nameBuf[2 * MAX_PATH];
        BOOL endProcessing = FALSE;
        CQuadWord operationDone;
        int i;
        for (i = 0; i < streamNamesCount; i++)
        {
            MyStrCpyNW(srcEnd, streamNames[i], (int)(2 * MAX_PATH - (srcEnd - srcName)));
            MyStrCpyNW(tgtEnd, streamNames[i], (int)(2 * MAX_PATH - (tgtEnd - tgtName)));

        COPY_AGAIN_ADS:

            operationDone = CQuadWord(0, 0);
            int limitBufferSize = bufferSize;
            script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone, &limitBufferSize, bufferSize);

            BOOL doNextFile = FALSE;
            while (1)
            {
                // The retained move owner has DELETE access; ADS readers must share it.
                DWORD sourceSharing = FILE_SHARE_READ | (stableMoveSource ? FILE_SHARE_DELETE : FILE_SHARE_WRITE);
                HANDLE in = CreateFileW(srcName, GENERIC_READ, sourceSharing, NULL,
                                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, in, GetLastError(), TRUE);
                if (in != INVALID_HANDLE_VALUE)
                {
                    CFileOffsetResult fileSizeResult = SalGetFileSizeEx(in);
                    CQuadWord fileSize = fileSizeResult.Value;
                    if (!fileSizeResult.Succeeded)
                    {
                        TRACE_E("SalGetFileSizeEx(some ADS of " << sourceName << "): unexpected error: " << GetErrorText(fileSizeResult.Error));
                        fileSize.SetUI64(0);
                    }

                    while (1)
                    {
                        HANDLE out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                       __hoCreateFile, out, GetLastError(), TRUE);

                        BOOL canOverwriteMACADSs = TRUE;

                    COPY_OVERWRITE:

                        if (out != INVALID_HANDLE_VALUE)
                        {
                            canOverwriteMACADSs = FALSE;

                            // if possible, pre-allocate the required space (avoids disk fragmentation and smooths writes to floppies)
                            BOOL wholeFileAllocated = FALSE;
                            if (fileSize > CQuadWord(limitBufferSize, 0) && // pointless to pre-allocate below the copy buffer size
                                fileSize < CQuadWord(0, 0x80000000))        // file size must be positive (otherwise seeking fails � values above 8 EB, so practically never)
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
                                            ignoreErr = TRUE; // low disk space
                                    }
                                }
                                else
                                    allocationError = allocationSeek.Error;
                                if (fatal)
                                {
                                    if (!ignoreErr)
                                    {
                                        TRACE_E("DoCopyADS(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! Error: " << GetErrorText(allocationError));
                                    }

                                    // try truncating the file to zero so closing it does not trigger unnecessary writes
                                    SalSetFilePointerEx(out, CQuadWord(0, 0), FILE_BEGIN);
                                    SetEndOfFile(out);

                                    HANDLES(CloseHandle(out));
                                    out = INVALID_HANDLE_VALUE;
                                    if (DeleteFileW(tgtName))
                                    {
                                        out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, out, GetLastError(), TRUE);
                                        if (out == INVALID_HANDLE_VALUE)
                                            goto CREATE_ERROR_ADS;
                                    }
                                    else
                                        goto CREATE_ERROR_ADS;
                                }
                            }

                            DWORD read;
                            DWORD written;
                            while (1)
                            {
                                if (ReadFile(in, buffer, limitBufferSize, &read, NULL))
                                {
                                    if (read == 0)
                                        break;                                                     // EOF
                                    if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                    {
                                    COPY_ERROR_ADS:

                                        if (in != NULL)
                                            HANDLES(CloseHandle(in));
                                        if (out != NULL)
                                        {
                                            if (wholeFileAllocated)
                                                SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                            HANDLES(CloseHandle(out));
                                        }
                                        DeleteFileW(tgtName);
                                        doCopyADSRet = FALSE;
                                        endProcessing = TRUE;
                                        break;
                                    }

                                    while (1)
                                    {
            if (OperationExecutionFileSystem().WriteFile(out, buffer, read, &written, NULL) && read == written)
                                            break;

                                    WRITE_ERROR_ADS:

                                        DWORD err;
                                        err = GetLastError();

                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                        if (*dlgData.CancelWorker)
                                            goto COPY_ERROR_ADS;

                                        if (dlgData.SkipAllFileADSWrite)
                                            goto SKIP_COPY_ADS;

                                        int ret;
                                        ret = IDCANCEL;
                                        char* data[4];
                                        data[0] = (char*)&ret;
                                        data[1] = LoadStr(IDS_ERRORWRITINGADS);
                                        ConvertWideToUtf8(tgtName, -1, nameBuf, 2 * MAX_PATH);
                                        nameBuf[2 * MAX_PATH - 1] = 0;
                                        CutADSNameSuffix(nameBuf);
                                        data[2] = nameBuf;
                                        if (err == NO_ERROR && read != written)
                                            err = ERROR_DISK_FULL;
                                        data[3] = GetErrorText(err);
                                        SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                        switch (ret)
                                        {
                                        case IDRETRY: // on a network we must reopen the handle; local access would not allow sharing
                                        {
                                            if (in == NULL && out == NULL)
                                            {
                                                DeleteFileW(tgtName);
                                                goto COPY_AGAIN_ADS;
                                            }
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out);     // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out)); // close the invalid handle
                                            }
                                            out = CreateFileW(tgtName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_ALWAYS,
                                                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                            HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                           __hoCreateFile, out, GetLastError(), TRUE);
                                            if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                            {
                                                CFileOffsetResult outputSize = SalGetFileSizeEx(out);
                                                if (!outputSize.Succeeded ||
                                                    outputSize.Value < operationDone ||
                                                    !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                                                { // cannot determine the size or the file is too small; restart the entire copy
                                                    HANDLES(CloseHandle(in));
                                                    HANDLES(CloseHandle(out));
                                                    DeleteFileW(tgtName);
                                                    goto COPY_AGAIN_ADS;
                                                }
                                            }
                                            else // still cannot open; problem persists
                                            {
                                                out = NULL;
                                                goto WRITE_ERROR_ADS;
                                            }
                                            break;
                                        }

                                        case IDB_SKIPALL:
                                            dlgData.SkipAllFileADSWrite = TRUE;
                                        case IDB_SKIP:
                                        {
                                        SKIP_COPY_ADS:

                                            if (in != NULL)
                                                HANDLES(CloseHandle(in));
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out));
                                            }
                                            DeleteFileW(tgtName);
                                            if (skip != NULL)
                                                *skip = TRUE;
                                            skipped = TRUE;
                                            endProcessing = TRUE;
                                            break;
                                        }

                                        case IDCANCEL:
                                            goto COPY_ERROR_ADS;
                                        }
                                        if (endProcessing)
                                            break;
                                    }
                                    if (endProcessing)
                                        break;
                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto COPY_ERROR_ADS;

                                    script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    operationDone += CQuadWord(read, 0);
                                    SetProgressWithoutSuspend(hProgressDlg, CalculateProgressPercent(operDone + operationDone, operTotal),
                                                              CalculateProgressPercent(totalDone + operDone + operationDone, script->TotalSize),
                                                              dlgData);

                                    if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
                                    {                                                              // worker resumes and fetch a fresh copy buffer size
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                        script->GetNewBufSize(&limitBufferSize, bufferSize);
                                    }
                                }
                                else
                                {
                                READ_ERROR_ADS:

                                    DWORD err;
                                    err = GetLastError();
                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto COPY_ERROR_ADS;

                                    if (dlgData.SkipAllFileADSRead)
                                        goto SKIP_COPY_ADS;

                                    int ret = IDCANCEL;
                                    char* data[4];
                                    data[0] = (char*)&ret;
                                    data[1] = LoadStr(IDS_ERRORREADINGADS);
                                    ConvertWideToUtf8(srcName, -1, nameBuf, 2 * MAX_PATH);
                                    nameBuf[2 * MAX_PATH - 1] = 0;
                                    CutADSNameSuffix(nameBuf);
                                    data[2] = nameBuf;
                                    data[3] = GetErrorText(err);
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                    switch (ret)
                                    {
                                    case IDRETRY:
                                    {
                                        if (in != NULL)
                                            HANDLES(CloseHandle(in)); // close the invalid handle

                                        in = CreateFileW(srcName, GENERIC_READ, sourceSharing, NULL,
                                                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, in, GetLastError(), TRUE);
                                        if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                        {
                                            CFileOffsetResult inputSize = SalGetFileSizeEx(in);
                                            if (!inputSize.Succeeded ||
                                                inputSize.Value < operationDone ||
                                                !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                                            { // cannot obtain size or the file is too small; restart the entire operation
                                                HANDLES(CloseHandle(in));
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out));
                                                DeleteFileW(tgtName);
                                                goto COPY_AGAIN_ADS;
                                            }
                                        }
                                        else // still cannot open; problem persists
                                        {
                                            in = NULL;
                                            goto READ_ERROR_ADS;
                                        }
                                        break;
                                    }
                                    case IDB_SKIPALL:
                                        dlgData.SkipAllFileADSRead = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_COPY_ADS;
                                    case IDCANCEL:
                                        goto COPY_ERROR_ADS;
                                    }
                                }
                            }
                            if (endProcessing)
                                break;

                            if (wholeFileAllocated &&     // the entire target layout was pre-allocated
                                operationDone < fileSize) // and the source file shrank
                            {
                                if (!SetEndOfFile(out)) // trim it here
                                {
                                    written = read = 0;
                                    goto WRITE_ERROR_ADS;
                                }
                            }

                            // commented out because it sets the time of the file/directory that owns the ADS instead of the ADS timestamps
                            //              FILETIME creation, lastAccess, lastWrite;
                            //              GetFileTime(in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);
                            //              SetFileTime(out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);

                            HANDLES(CloseHandle(in));
                            if (!HANDLES(CloseHandle(out))) // even after a failed call we assume the handle is closed,
                            {                               // see /viewtopic.php?f=6&t=8455
                                in = out = NULL;            // (reports that the target file can be deleted, so its handle was not left open)
                                written = read = 0;
                                goto WRITE_ERROR_ADS;
                            }

                            // commented out because it sets the attributes of the file/directory that owns the ADS instead of the ADS attributes
                            //              DWORD attr = DynGetFileAttributesW(srcName);
                            //              if (attr != INVALID_FILE_ATTRIBUTES) DynSetFileAttributesW(tgtName, attr);

                            operDone += operationDone;
                            lastTransferredFileSize += operationDone;
                            doNextFile = TRUE;
                        }
                        else
                        {
                        CREATE_ERROR_ADS:

                            DWORD err = GetLastError();

                            // Macintosh compatibility: NTFS automatically creates ADS entries myFile:Afp_Resource and myFile:Afp_AfpInfo,
                            // overwrite them silently with the versions from the source file
                            if (canOverwriteMACADSs &&
                                (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) &&
                                (_wcsnicmp(streamNames[i], L":Afp_Resource", 13) == 0 &&
                                     (streamNames[i][13] == 0 || streamNames[i][13] == L':') ||
                                 _wcsnicmp(streamNames[i], L":Afp_AfpInfo", 12) == 0 &&
                                     (streamNames[i][12] == 0 || streamNames[i][12] == L':')))
                            {
                                out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                  FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                               __hoCreateFile, out, GetLastError(), TRUE);

                                canOverwriteMACADSs = FALSE;
                                goto COPY_OVERWRITE;
                            }

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN2_ADS;

                            if (dlgData.SkipAllFileADSOpenOut)
                                goto SKIP_OPEN_OUT_ADS;

                            if (dlgData.IgnoreAllADSOpenOutErr)
                                goto IGNORE_OPENOUTADS;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERROROPENINGADS);
                            ConvertWideToUtf8(tgtName, -1, nameBuf, 2 * MAX_PATH);
                            nameBuf[2 * MAX_PATH - 1] = 0;
                            CutADSNameSuffix(nameBuf);
                            data[2] = nameBuf;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllADSOpenOutErr = TRUE; // break is intentionally omitted here
                            case IDB_IGNORE:
                            {
                            IGNORE_OPENOUTADS:

                                RecordMetadataLoss(dlgData, mmlAlternateDataStreams, sourceName, targetName);
                                HANDLES(CloseHandle(in));
                                operDone += fileSize;
                                lastTransferredFileSize += fileSize;
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone);
                                doNextFile = TRUE;
                                break;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileADSOpenOut = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OPEN_OUT_ADS:

                                HANDLES(CloseHandle(in));
                                if (skip != NULL)
                                    *skip = TRUE;
                                skipped = TRUE;
                                endProcessing = TRUE;
                                break;
                            }

                            case IDCANCEL:
                            {
                            CANCEL_OPEN2_ADS:

                                HANDLES(CloseHandle(in));
                                doCopyADSRet = FALSE;
                                endProcessing = TRUE;
                                break;
                            }
                            }
                        }
                        if (doNextFile || endProcessing)
                            break;
                    }
                }
                else
                {
                    DWORD err = GetLastError();
                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }

                    if (dlgData.SkipAllFileADSOpenIn)
                        goto SKIP_OPEN_IN_ADS;

                    int ret;
                    ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_ERROROPENINGADS);
                    ConvertWideToUtf8(srcName, -1, nameBuf, 2 * MAX_PATH);
                    nameBuf[2 * MAX_PATH - 1] = 0;
                    CutADSNameSuffix(nameBuf);
                    data[2] = nameBuf;
                    data[3] = GetErrorText(err);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;

                    case IDB_SKIPALL:
                        dlgData.SkipAllFileADSOpenIn = TRUE;
                    case IDB_SKIP:
                    {
                    SKIP_OPEN_IN_ADS:

                        if (skip != NULL)
                            *skip = TRUE;
                        skipped = TRUE;
                        endProcessing = TRUE;
                        break;
                    }

                    case IDCANCEL:
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }
                    }
                }
                if (doNextFile || endProcessing)
                    break;
            }
            if (endProcessing)
                break;
        }

        for (i = 0; i < streamNamesCount; i++)
            free(streamNames[i]);
        free(streamNames);
    }
    else
    {
        if (adsWinError != NO_ERROR) // display the Windows error (low-memory warning goes only to TRACE_E)
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.IgnoreAllADSReadErr)
                goto IGNORE_ADS;

            int ret;
            ret = IDCANCEL;
            char* data[3];
            data[0] = (char*)&ret;
            data[1] = (char*)sourceName;
            data[2] = GetErrorText(adsWinError);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 6, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                goto COPY_ADS_AGAIN;

            case IDB_IGNOREALL:
                dlgData.IgnoreAllADSReadErr = TRUE; // break is intentionally omitted here
            case IDB_IGNORE:
            {
            IGNORE_ADS:

                RecordMetadataLoss(dlgData, mmlAlternateDataStreams, sourceName, targetName);
                script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

                SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone + operTotal, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (lowMemory)
            doCopyADSRet = FALSE; // lack of memory -> cancel the operation
    }
    if (doCopyADSRet && skipped)
    {
        script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

        SetProgress(hProgressDlg, 0, CalculateProgressPercent(totalDone + operTotal, script->TotalSize), dlgData);
    }
    return doCopyADSRet;
}
