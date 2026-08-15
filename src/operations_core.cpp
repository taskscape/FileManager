// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "cfgdlg.h"
#include "worker.h"
#include "execlog.h"
#include "common/allochan.h"
#include "release_diagnostics.h"

#include <Aclapi.h>
#include <Ntsecapi.h>
#include <strsafe.h>

// CreateFileUtf8 / DeleteFileUtf8 / SetFileAttributesUtf8 / RemoveDirectoryUtf8
// are globally declared in common/strutils.h and defined in common/strutils.cpp.
extern NTQUERYINFORMATIONFILE DynNtQueryInformationFile;
extern NTFSCONTROLFILE DynNtFsControlFile;

// Forward declarations of helpers defined in async_copy.cpp
void GainWriteOwnerAccess();
DWORD CompressFile(char* fileName, DWORD attrs);
DWORD UncompressFile(char* fileName, DWORD attrs);
DWORD MyEncryptFile(HWND hProgressDlg, char* fileName, DWORD attrs, DWORD finalAttrs,
                    CProgressDlgData& dlgData, BOOL& cancelOper, BOOL preserveDate);
DWORD MyDecryptFile(char* fileName, DWORD attrs, BOOL preserveDate);
BOOL DoCopyFile(COperation* op, HWND hProgressDlg, void* buffer,
                COperations* script, CQuadWord& totalDone,
                DWORD clearReadonlyMask, BOOL* skip, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CProgressDlgData& dlgData, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL isMove, CAsyncCopyParams*& asyncPar, BOOL* suspiciousIoRetry);

struct TMN_REPARSE_DATA_BUFFER
{
  DWORD ReparseTag;
  WORD  ReparseDataLength;
  WORD  Reserved;
  WORD  SubstituteNameOffset;
  WORD  SubstituteNameLength;
  WORD  PrintNameOffset;
  WORD  PrintNameLength;
  WCHAR PathBuffer[1];
};

#define IO_REPARSE_TAG_VALID_VALUES 0xE000FFFF
#define IsReparseTagValid(x) (!((x)&~IO_REPARSE_TAG_VALID_VALUES)&&((x)>IO_REPARSE_TAG_RESERVED_RANGE))
#define MAXIMUM_REPARSE_DATA_BUFFER_SIZE      ( 16 * 1024 )


/*
  HANDLE srcDir = HANDLES_Q(CreateFileUtf8(name, GENERIC_READ, 0, 0, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL));
  if (srcDir != INVALID_HANDLE_VALUE)
  {
    HANDLE tgtDir = HANDLES_Q(CreateFileUtf8("D:\\ZUMPA\\link", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL));
    if (tgtDir != INVALID_HANDLE_VALUE)
    {
      char szBuff[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
      TMN_REPARSE_DATA_BUFFER& rdb = *(TMN_REPARSE_DATA_BUFFER*)szBuff;

      DWORD dwBytesReturned;
      if (DeviceIoControl(srcDir, FSCTL_GET_REPARSE_POINT, NULL, 0, (LPVOID)&rdb,
                          MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dwBytesReturned, 0) &&
          IsReparseTagValid(rdb.ReparseTag))
      {
        DWORD dwBytesReturnedDummy;
        if (DeviceIoControl(tgtDir, FSCTL_SET_REPARSE_POINT, (LPVOID)&rdb, dwBytesReturned,
                            NULL, 0, &dwBytesReturnedDummy, 0))
        {
          TRACE_I("eureka?");
        }
      }
      HANDLES(CloseHandle(tgtDir));
    }
    HANDLES(CloseHandle(srcDir));
  }
  return FALSE;
*/

BOOL DoDeleteDirLinkAux(const char* nameDelLink, const COperation::CFileIdentity& expectedIdentity, DWORD* err)
{
    // remove the reparse point from directory 'nameDelLink'
    if (err != NULL)
        *err = ERROR_SUCCESS;
    BOOL ok = FALSE;
    DWORD attr = GetFileAttributesUtf8(nameDelLink);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT))
    {
        HANDLE dir = HANDLES_Q(CreateFileUtf8(nameDelLink, GENERIC_WRITE | DELETE | FILE_READ_ATTRIBUTES, 0, 0, OPEN_EXISTING,
                                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL));
        if (dir != INVALID_HANDLE_VALUE)
        {
            DWORD dummy;
            char buf[MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
            REPARSE_GUID_DATA_BUFFER* juncData = (REPARSE_GUID_DATA_BUFFER*)buf;
            if (!VerifyFileHandleIdentity(dir, expectedIdentity, err))
            {
                ok = FALSE;
            }
            else if (DeviceIoControl(dir, FSCTL_GET_REPARSE_POINT, NULL, 0, juncData,
                                MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &dummy, NULL) == 0)
            {
                if (err != NULL)
                    *err = GetLastError();
            }
            else
            {
                if (juncData->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
                    juncData->ReparseTag != IO_REPARSE_TAG_SYMLINK)
                { // if this is not a volume mount point, junction, or symlink, report an error (we could probably delete it, but better refuse than break something...)
                    TRACE_E("DoDeleteDirLinkAux(): Unknown type of reparse point (tag is 0x" << std::hex << juncData->ReparseTag << std::dec << "): " << nameDelLink);
                    if (err != NULL)
                        *err = 4394 /* ERROR_REPARSE_TAG_MISMATCH */;
                }
                else
                {
                    REPARSE_GUID_DATA_BUFFER rgdb = {0};
                    rgdb.ReparseTag = juncData->ReparseTag;

                    DWORD dwBytes;
                    FILE_DISPOSITION_INFO disposition;
                    disposition.DeleteFile = TRUE;
                    if (DeviceIoControl(dir, FSCTL_DELETE_REPARSE_POINT, &rgdb, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE,
                                        NULL, 0, &dwBytes, 0) != 0 &&
                        SetFileInformationByHandle(dir, FileDispositionInfo, &disposition, sizeof(disposition)))
                    {
                        ok = TRUE;
                    }
                    else
                    {
                        if (err != NULL)
                            *err = GetLastError();
                    }
                }
            }
            HANDLES(CloseHandle(dir));
        }
        else
        {
            if (err != NULL)
                *err = GetLastError();
        }
    }
    else
        ok = TRUE; // the reparse point is already gone
    return ok;
}

BOOL DeleteDirLink(const char* name, DWORD* err)
{
    // if the path ends with a space/dot, we must append '\\'; otherwise CreateFile
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameDelLink = name;
    char nameDelLinkCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameDelLink, nameDelLinkCopy);

    COperation operation;
    memset(&operation, 0, sizeof(operation));
    operation.Opcode = ocDeleteDirLink;
    operation.SourceName = (char*)nameDelLink;
    if (!CaptureOperationFileIdentities(&operation, err))
        return FALSE;
    return DoDeleteDirLinkAux(nameDelLink, operation.SourceIdentity, err);
}

BOOL DoDeleteDirLink(HWND hProgressDlg, COperation* operation, const CQuadWord& size, COperations* script,
                     CQuadWord& totalDone, CProgressDlgData& dlgData)
{
    char* name = operation->SourceName;
    // if the path ends with a space/dot, we must append '\\'; otherwise CreateFile
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameDelLink = name;
    char nameDelLinkCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameDelLink, nameDelLinkCopy);

    while (1)
    {
        DWORD err;
        BOOL ok = DoDeleteDirLinkAux(nameDelLink, operation->SourceIdentity, &err);

        if (ok)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

            totalDone += size;
            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDeleteErr)
                goto SKIP_DELETE_LINK;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORDELETINGDIRLINK);
            data[2] = (char*)nameDelLink;
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
            SKIP_DELETE_LINK:

                totalDone += size;
                script->SetProgressSize(totalDone);
                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

// a) create a temporary file in the same directory as file 'name'
// b) transfer the contents of 'name' into the temporary file while applying the
//    conversions specified by convertData.CodeType and convertData.EOFType
// c) overwrite file 'name' with the temporary file
//
// convertData.EOFType  - determines how line endings are replaced
//            CR, LF, and CRLF are all considered end-of-line markers
//            0: leave line endings unchanged
//            1: replace line endings with CRLF (DOS, Windows, OS/2)
//            2: replace line endings with LF (UNIX)
//            3: replace line endings with CR (MAC)
BOOL DoConvert(HWND hProgressDlg, char* name, char* sourceBuffer, char* targetBuffer,
               const CQuadWord& size, COperations* script, CQuadWord& totalDone,
               CConvertData& convertData, CProgressDlgData& dlgData)
{
    // if the path ends with a space/dot it is invalid and we must not run the conversion,
    // CreateFile would trim the spaces/dots and convert a different file
    BOOL invalidName = FileNameIsInvalid(name, TRUE);

CONVERT_AGAIN:

    CQuadWord operationDone;
    operationDone = CQuadWord(0, 0);
    while (1)
    {
        // attempt to open the source file
        HANDLE hSource;
        if (!invalidName)
        {
            hSource = HANDLES_Q(CreateFileUtf8(name, GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        }
        else
        {
            hSource = INVALID_HANDLE_VALUE;
        }
        if (hSource != INVALID_HANDLE_VALUE)
        {
            // derive the path for the temporary file
            char tmpPath[3 * MAX_PATH];
            if (strlen(name) >= _countof(tmpPath))
            {
                TRACE_E("DoConvert(): source path is too long for temporary path handling: " << name);
                HANDLES(CloseHandle(hSource));
                return FALSE;
            }
            strcpy(tmpPath, name);
            char* terminator = strrchr(tmpPath, '\\');
            if (terminator == NULL)
            {
                // sanity check
                TRACE_E("Parameter 'name' must be full path to file (including path)");
                HANDLES(CloseHandle(hSource));
                return FALSE;
            }
            *(terminator + 1) = 0;

            // find a name for the temporary file and let the system create it
            char tmpFileName[MAX_PATH];
            BOOL tmpFileExists = FALSE;
            while (1)
            {
                if (SalGetTempFileName(tmpPath, "cnv", tmpFileName, _countof(tmpFileName), TRUE))
                {
                    tmpFileExists = TRUE;

                    // align the temp file attributes with the source file
                    DWORD srcAttrs = SalGetFileAttributes(name);
                    DWORD tgtAttrs = SalGetFileAttributes(tmpFileName);
                    BOOL changeAttrs = FALSE;
                    if (srcAttrs != INVALID_FILE_ATTRIBUTES && tgtAttrs != INVALID_FILE_ATTRIBUTES && srcAttrs != tgtAttrs)
                    {
                        changeAttrs = TRUE; // SetFileAttributes will be called later...
                        // does the NTFS compression flag differ?
                        if ((srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != (tgtAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (srcAttrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
                        {
                            UncompressFile(tmpFileName, tgtAttrs);
                        }
                        if ((srcAttrs & FILE_ATTRIBUTE_ENCRYPTED) != (tgtAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                        {
                            BOOL cancelOper = FALSE;
                            if (srcAttrs & FILE_ATTRIBUTE_ENCRYPTED)
                            {
                                MyEncryptFile(hProgressDlg, tmpFileName, tgtAttrs, 0 /* allow encrypting files with the SYSTEM attribute */,
                                              dlgData, cancelOper, FALSE);
                            }
                            else
                                MyDecryptFile(tmpFileName, tgtAttrs, FALSE);
                            if (*dlgData.CancelWorker || cancelOper)
                            {
                                HANDLES(CloseHandle(hSource));
                                ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                DeleteFileUtf8(tmpFileName);
                                return FALSE;
                            }
                        }
                        if ((srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != (tgtAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (srcAttrs & FILE_ATTRIBUTE_COMPRESSED) != 0)
                        {
                            CompressFile(tmpFileName, tgtAttrs);
                        }
                    }

                    // open the empty temporary file
                    HANDLE hTarget = HANDLES_Q(CreateFileUtf8(tmpFileName, GENERIC_WRITE, 0, NULL,
                                                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (hTarget != INVALID_HANDLE_VALUE)
                    {
                        DWORD read;
                        BOOL crlfBreak = FALSE;
                        while (1)
                        {
                            if (ReadFile(hSource, sourceBuffer, OPERATION_BUFFER, &read, NULL))
                            {
                                DWORD written;
                                if (read == 0)
                                    break;                                                 // EOF
                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                {
                                CONVERT_ERROR:

                                    if (hSource != NULL)
                                        HANDLES(CloseHandle(hSource));
                                    if (hTarget != NULL)
                                        HANDLES(CloseHandle(hTarget));
                                    ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                    DeleteFileUtf8(tmpFileName);
                                    return FALSE;
                                }

                                // translate sourceBuffer -> targetBuffer
                                char* sourceIterator;
                                char* targetIterator;
                                sourceIterator = sourceBuffer;
                                targetIterator = targetBuffer;
                                while (sourceIterator - sourceBuffer < (int)read)
                                {
                                    // lastChar is TRUE when sourceIterator points to the final character in the buffer
                                    BOOL lastChar = (sourceIterator - sourceBuffer == (int)read - 1);

                                    if (convertData.EOFType != 0)
                                    {
                                        if (crlfBreak && sourceIterator == sourceBuffer && *sourceIterator == '\n')
                                        {
                                            // we already processed this CRLF, leave the LF as is now
                                            crlfBreak = FALSE;
                                        }
                                        else
                                        {
                                            if (*sourceIterator == '\r' || *sourceIterator == '\n')
                                            {
                                                switch (convertData.EOFType)
                                                {
                                                case 2:
                                                    *targetIterator++ = convertData.CodeTable['\n'];
                                                    break;
                                                case 3:
                                                    *targetIterator++ = convertData.CodeTable['\r'];
                                                    break;
                                                default:
                                                {
                                                    *targetIterator++ = convertData.CodeTable['\r'];
                                                    *targetIterator++ = convertData.CodeTable['\n'];
                                                    break;
                                                }
                                                }
                                                // capture CRLF which splits across the buffer boundary
                                                if (lastChar && *sourceIterator == '\r')
                                                    crlfBreak = TRUE;
                                                // capture CRLF that is contiguous � skip the LF
                                                if (!lastChar &&
                                                    *sourceIterator == '\r' && *(sourceIterator + 1) == '\n')
                                                    sourceIterator++;
                                            }
                                            else
                                            {
                                                *targetIterator = convertData.CodeTable[(unsigned char)*sourceIterator];
                                                targetIterator++;
                                            }
                                        }
                                    }
                                    else
                                    {
                                        *targetIterator = convertData.CodeTable[(unsigned char)*sourceIterator];
                                        targetIterator++;
                                    }
                                    sourceIterator++;
                                }

                                // write the data to the temp file
                                while (1)
                                {
                                    if (WriteFile(hTarget, targetBuffer, (DWORD)(targetIterator - targetBuffer), &written, NULL) &&
                                        targetIterator - targetBuffer == (int)written)
                                        break;

                                WRITE_ERROR_CONVERT:

                                    DWORD err;
                                    err = GetLastError();

                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto CONVERT_ERROR;

                                    if (dlgData.SkipAllFileWrite)
                                        goto SKIP_CONVERT;

                                    int ret;
                                    ret = IDCANCEL;
                                    char* data[4];
                                    data[0] = (char*)&ret;
                                    data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                                    data[2] = tmpFileName;
                                    if (hTarget != NULL && err == NO_ERROR && targetIterator - targetBuffer != (int)written)
                                        err = ERROR_DISK_FULL;
                                    data[3] = GetErrorText(err);
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                    switch (ret)
                                    {
                                    case IDRETRY:
                                    {
                                        if (hSource == NULL && hTarget == NULL)
                                        {
                                            ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                            DeleteFileUtf8(tmpFileName);
                                            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                                            goto CONVERT_AGAIN;
                                        }
                                        break;
                                    }

                                    case IDB_SKIPALL:
                                        dlgData.SkipAllFileWrite = TRUE;
                                    case IDB_SKIP:
                                    {
                                    SKIP_CONVERT:

                                        totalDone += size;
                                        if (hSource != NULL)
                                            HANDLES(CloseHandle(hSource));
                                        if (hTarget != NULL)
                                            HANDLES(CloseHandle(hTarget));
                                        ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                        DeleteFileUtf8(tmpFileName);
                                        SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                                        return TRUE;
                                    }

                                    case IDCANCEL:
                                        goto CONVERT_ERROR;
                                    }
                                }
                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CONVERT_ERROR;

                                operationDone += CQuadWord(read, 0);
                                SetProgress(hProgressDlg,
                                            CaclProg(operationDone, size),
                                            CaclProg(totalDone + operationDone, script->TotalSize), dlgData);
                            }
                            else
                            {
                                DWORD err = GetLastError();
                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CONVERT_ERROR;

                                if (dlgData.SkipAllFileRead)
                                    goto SKIP_CONVERT;

                                int ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = LoadStr(IDS_ERRORREADINGFILE);
                                data[2] = name;
                                data[3] = GetErrorText(err);
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;
                                case IDB_SKIPALL:
                                    dlgData.SkipAllFileRead = TRUE;
                                case IDB_SKIP:
                                    goto SKIP_CONVERT;
                                case IDCANCEL:
                                    goto CONVERT_ERROR;
                                }
                            }
                        }
                        // close the files and update the global progress
                        // do not reuse operationDone so the progress stays correct even if the file changes "under our feet"
                        HANDLES(CloseHandle(hSource));
                        if (!HANDLES(CloseHandle(hTarget))) // even after a failed call we assume the handle is closed,
                        {                                   // see /viewtopic.php?f=6&t=8455
                            hSource = hTarget = NULL;       // (it states that the target file can be deleted, so the handle was not left open)
                            goto WRITE_ERROR_CONVERT;
                        }
                        totalDone += size;
                        // restore attributes (write operations have trouble with read-only)
                        if (changeAttrs)
                            SetFileAttributesUtf8(tmpFileName, srcAttrs);
                        // overwrite the original file with the temp file
                        while (1)
                        {
                            ClearReadOnlyAttr(name); // ensure it can be deleted
                            if (DeleteFileUtf8(name))
                            {
                                while (1)
                                {
                                    if (SalMoveFile(tmpFileName, name))
                                        return TRUE; // success
                                    else
                                    {
                                        DWORD err = GetLastError();

                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                        if (*dlgData.CancelWorker)
                                        {
                                            ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                            DeleteFileUtf8(tmpFileName);
                                            return FALSE;
                                        }

                                        if (dlgData.SkipAllMoveErrors)
                                        {
                                            ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                            DeleteFileUtf8(tmpFileName);
                                            return TRUE;
                                        }

                                        int ret = IDCANCEL;
                                        char* data[4];
                                        data[0] = (char*)&ret;
                                        data[1] = tmpFileName;
                                        data[2] = name;
                                        data[3] = GetErrorText(err);
                                        SendMessage(hProgressDlg, WM_USER_DIALOG, 3, (LPARAM)data);
                                        switch (ret)
                                        {
                                        case IDRETRY:
                                            break;

                                        case IDB_SKIPALL:
                                            dlgData.SkipAllMoveErrors = TRUE;
                                        case IDB_SKIP:
                                            ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                            DeleteFileUtf8(tmpFileName);
                                            return TRUE;

                                        case IDCANCEL:
                                            ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                            DeleteFileUtf8(tmpFileName);
                                            return FALSE;
                                        }
                                    }
                                }
                            }
                            else
                            {
                                DWORD err = GetLastError();

                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                {
                                CANCEL_CONVERT:

                                    ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                    DeleteFileUtf8(tmpFileName);
                                    return FALSE;
                                }

                                if (dlgData.SkipAllOverwriteErr)
                                    goto SKIP_OVERWRITE_ERROR;

                                int ret;
                                ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = LoadStr(IDS_ERROROVERWRITINGFILE);
                                data[2] = name;
                                data[3] = GetErrorText(err);
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

                                    ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                                    DeleteFileUtf8(tmpFileName);
                                    return TRUE;
                                }

                                case IDCANCEL:
                                    goto CANCEL_CONVERT;
                                }
                            }
                        }
                    }
                    else
                        goto TMP_OPEN_ERROR;
                }
                else
                {
                TMP_OPEN_ERROR:

                    DWORD err = GetLastError();

                    char fakeName[3 * MAX_PATH]; // name of the temp file that cannot be created/opened
                    if (tmpFileExists)
                    {
                        strcpy(fakeName, tmpFileName);
                        ClearReadOnlyAttr(tmpFileName); // ensure it can be deleted
                        DeleteFileUtf8(tmpFileName);        // the temp file exists, try to remove it
                        tmpFileExists = FALSE;
                    }
                    else
                    {
                        // assemble a fictitious temp-file name for the failed creation attempt
                        char* s = tmpPath + strlen(tmpPath);
                        if (s > tmpPath && *(s - 1) == '\\')
                            s--;
                        size_t fakeNamePrefixLen = (size_t)(s - tmpPath);
                        const char* fakeNameSuffix = "\\cnv0000.tmp";
                        size_t fakeNameSuffixLen = strlen(fakeNameSuffix);
                        if (fakeNamePrefixLen + fakeNameSuffixLen < _countof(fakeName))
                        {
                            memcpy(fakeName, tmpPath, fakeNamePrefixLen);
                            memcpy(fakeName + fakeNamePrefixLen, fakeNameSuffix, fakeNameSuffixLen + 1);
                        }
                        else
                        {
                            // Error reporting may clip this display-only fallback name, never the actual temp identity.
                            StringCchCopyNA(fakeName, _countof(fakeName), tmpPath, _countof(fakeName) - 1);
                        }
                    }

                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        goto CANCEL_OPEN2;

                    if (dlgData.SkipAllFileOpenOut)
                        goto SKIP_OPEN_OUT;

                    int ret;
                    ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_ERRORCREATINGTMPFILE);
                    data[2] = fakeName;
                    data[3] = GetErrorText(err);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;

                    case IDB_SKIPALL:
                        dlgData.SkipAllFileOpenOut = TRUE;
                    case IDB_SKIP:
                    {
                    SKIP_OPEN_OUT:

                        HANDLES(CloseHandle(hSource));
                        totalDone += size;
                        SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                        return TRUE;
                    }

                    case IDCANCEL:
                    {
                    CANCEL_OPEN2:

                        HANDLES(CloseHandle(hSource));
                        return FALSE;
                    }
                    }
                }
            }
        }
        else
        {
            DWORD err = GetLastError();
            if (invalidName)
                err = ERROR_INVALID_NAME;
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
            data[2] = name;
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

                totalDone += size;
                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoChangeAttrs(HWND hProgressDlg, char* name, const CQuadWord& size, DWORD attrs,
                   COperations* script, CQuadWord& totalDone,
                   FILETIME* timeModified, FILETIME* timeCreated, FILETIME* timeAccessed,
                   BOOL& changeCompression, BOOL& changeEncryption, DWORD fileAttr,
                   CProgressDlgData& dlgData)
{
    // if the path ends with a space/dot, we must append '\\'; otherwise
    // SetFileAttributes (and others) trims the spaces/dots and operates
    // on a different path
    const char* nameSetAttrs = name;
    char nameSetAttrsCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameSetAttrs, nameSetAttrsCopy);

    while (1)
    {
        DWORD error = ERROR_SUCCESS;
        BOOL showCompressErr = FALSE;
        BOOL showEncryptErr = FALSE;
        char* errTitle = NULL;
        if (changeCompression && (attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            error = UncompressFile(name, fileAttr);
            if (error != ERROR_SUCCESS)
            {
                errTitle = LoadStr(IDS_ERRORCOMPRESSING);
                if (error == ERROR_INVALID_FUNCTION)
                    showCompressErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeEncryption && (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        {
            error = MyDecryptFile(name, fileAttr, TRUE);
            if (error != ERROR_SUCCESS)
            {
                errTitle = LoadStr(IDS_ERRORENCRYPTING);
                if (error == ERROR_INVALID_FUNCTION)
                    showEncryptErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeCompression && (attrs & FILE_ATTRIBUTE_COMPRESSED))
        {
            error = CompressFile(name, fileAttr);
            if (error != ERROR_SUCCESS)
            {
                errTitle = LoadStr(IDS_ERRORCOMPRESSING);
                if (error == ERROR_INVALID_FUNCTION)
                    showCompressErr = TRUE; // not supported
            }
        }
        if (error == ERROR_SUCCESS && changeEncryption && (attrs & FILE_ATTRIBUTE_ENCRYPTED))
        {
            BOOL cancelOper = FALSE;
            error = MyEncryptFile(hProgressDlg, name, fileAttr, attrs, dlgData, cancelOper, TRUE);
            if (*dlgData.CancelWorker || cancelOper)
                return FALSE;
            if (error != ERROR_SUCCESS)
            {
                errTitle = LoadStr(IDS_ERRORENCRYPTING);
                if (error == ERROR_INVALID_FUNCTION)
                    showEncryptErr = TRUE; // not supported
            }
        }
        if (showCompressErr || showEncryptErr)
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (showCompressErr)
                changeCompression = FALSE;
            if (showEncryptErr)
                changeEncryption = FALSE;
            char* data[3];
            data[0] = LoadStr((showCompressErr && (attrs & FILE_ATTRIBUTE_COMPRESSED) || !showEncryptErr) ? IDS_ERRORCOMPRESSING : IDS_ERRORENCRYPTING);
            data[1] = name;
            data[2] = LoadStr((showCompressErr && (attrs & FILE_ATTRIBUTE_COMPRESSED) || !showEncryptErr) ? IDS_COMPRNOTSUPPORTED : IDS_ENCRYPNOTSUPPORTED);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 5, (LPARAM)data);
            error = ERROR_SUCCESS;
        }
        if (error == ERROR_SUCCESS && SetFileAttributesUtf8(nameSetAttrs, attrs))
        {
            BOOL isDir = ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
            // if any of the timestamps need to be set
            if (timeModified != NULL || timeCreated != NULL || timeAccessed != NULL)
            {
                HANDLE file;
                if (attrs & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesUtf8(nameSetAttrs, attrs & (~FILE_ATTRIBUTE_READONLY));
                file = HANDLES_Q(CreateFileUtf8(nameSetAttrs, GENERIC_READ | GENERIC_WRITE,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                                            NULL, OPEN_EXISTING, isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL));
                if (file != INVALID_HANDLE_VALUE)
                {
                    FILETIME ftCreated, ftAccessed, ftModified;
                    GetFileTime(file, &ftCreated, &ftAccessed, &ftModified);
                    if (timeCreated != NULL)
                        ftCreated = *timeCreated;
                    if (timeAccessed != NULL)
                        ftAccessed = *timeAccessed;
                    if (timeModified != NULL)
                        ftModified = *timeModified;
                    SetFileTime(file, &ftCreated, &ftAccessed, &ftModified);
                    HANDLES(CloseHandle(file));
                    if (attrs & FILE_ATTRIBUTE_READONLY)
                        SetFileAttributesUtf8(nameSetAttrs, attrs);
                }
                else
                {
                    if (attrs & FILE_ATTRIBUTE_READONLY)
                        SetFileAttributesUtf8(nameSetAttrs, attrs);
                    goto SHOW_ERROR;
                }
            }
            totalDone += size;
            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
        SHOW_ERROR:

            if (error == ERROR_SUCCESS)
                error = GetLastError();
            if (errTitle == NULL)
                errTitle = LoadStr(IDS_ERRORCHANGINGATTRS);

            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllChangeAttrs)
                goto SKIP_ATTRS_ERROR;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = errTitle;
            data[2] = name;
            data[3] = GetErrorText(error);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllChangeAttrs = TRUE;
            case IDB_SKIP:
            {
            SKIP_ATTRS_ERROR:

                totalDone += size;
                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

unsigned ThreadWorkerBody(void* parameter)
{
    CALL_STACK_MESSAGE1("ThreadWorkerBody()");
    CWorkerData* data = (CWorkerData*)parameter;
    char workerName[64];
    // Put the dispatch ID in the worker name so debugger and trace threads match the owning dialog.
    _snprintf_s(workerName, _countof(workerName), _TRUNCATE, "Worker-%s", data->CorrelationId);
    SetThreadNameInVCAndTrace(workerName);
    TRACE_I("Begin operation=" << data->CorrelationId);
    //--- create a local copy of the data
    HANDLE wContinue = data->WContinue;
    CProgressDlgData dlgData;
    dlgData.WorkerNotSuspended = data->WorkerNotSuspended;
    dlgData.CancelWorker.Bind(data->Script);
    dlgData.OperationProgress = data->OperationProgress;
    dlgData.SummaryProgress = data->SummaryProgress;
    dlgData.OverwriteAll = dlgData.OverwriteHiddenAll = dlgData.DeleteHiddenAll =
        dlgData.SkipAllFileWrite = dlgData.SkipAllFileRead =
            dlgData.SkipAllOverwrite = dlgData.SkipAllSystemOrHidden =
                dlgData.SkipAllFileOpenIn = dlgData.SkipAllFileOpenOut =
                    dlgData.SkipAllOverwriteErr = dlgData.SkipAllMoveErrors =
                        dlgData.SkipAllDeleteErr = dlgData.SkipAllDirCreate =
                            dlgData.SkipAllDirCreateErr = dlgData.SkipAllChangeAttrs =
                                dlgData.EncryptSystemAll = dlgData.SkipAllEncryptSystem =
                                    dlgData.IgnoreAllADSReadErr = dlgData.SkipAllFileADSOpenIn =
                                        dlgData.SkipAllFileADSOpenOut = dlgData.SkipAllFileADSRead =
                                            dlgData.SkipAllFileADSWrite = dlgData.DirOverwriteAll =
                                                dlgData.SkipAllDirOver = dlgData.IgnoreAllADSOpenOutErr =
                                                    dlgData.IgnoreAllSetAttrsErr = dlgData.IgnoreAllCopyPermErr =
                                                        dlgData.IgnoreAllCopyDirTimeErr = dlgData.SkipAllFileOutLossEncr =
                                                            dlgData.FileOutLossEncrAll = dlgData.SkipAllDirCrLossEncr =
                                                             dlgData.DirCrLossEncrAll = dlgData.IgnoreAllGetFileTimeErr =
                                                                     dlgData.IgnoreAllSetFileTimeErr = dlgData.SkipAllGetFileTime =
                                                                         dlgData.SkipAllSetFileTime = FALSE;
    dlgData.MetadataLosses.Clear();
    dlgData.KeepSourceAfterMetadataLoss = FALSE;
    dlgData.CnfrmFileOver = Configuration.CnfrmFileOver;
    dlgData.CnfrmDirOver = Configuration.CnfrmDirOver;
    dlgData.CnfrmSHFileOver = Configuration.CnfrmSHFileOver;
    dlgData.CnfrmSHFileDel = Configuration.CnfrmSHFileDel;
    dlgData.UseRecycleBin = Configuration.UseRecycleBin;
    dlgData.RecycleMasks.SetMasksString(Configuration.RecycleMasks.GetMasksString(),
                                        Configuration.RecycleMasks.GetExtendedMode());
    int errorPos;
    if (dlgData.UseRecycleBin == 2 &&
        !dlgData.PrepareRecycleMasks(errorPos))
        TRACE_E("Error in recycle-bin group mask.");
    COperations* script = data->Script;
    if (script->TotalSize == CQuadWord(0, 0))
    {
        script->TotalSize = CQuadWord(1, 0); // guard against division by zero
                                             // TRACE_E("ThreadWorkerBody(): script->TotalSize may not be zero!");  // when building the script we do not set the "synchronizing one", which caused issues in Calculate Occupied Space
    }

    if (script->CopySecurity)
        GainWriteOwnerAccess();

    HWND hProgressDlg = data->HProgressDlg;
    void* buffer = data->Buffer;
    BOOL bufferIsAllocated = data->BufferIsAllocated;
    CChangeAttrsData* attrsData = (CChangeAttrsData*)data->Buffer;
    DWORD clearReadonlyMask = data->ClearReadonlyMask;
    CConvertData convertData;
    if (data->ConvertData != NULL) // make a copy of the data for Convert
    {
        convertData = *data->ConvertData;
    }
    SetEvent(wContinue); // data ready; resume the main thread or the progress-dialog thread
                         //---
    SetProgress(hProgressDlg, 0, 0, dlgData);
    script->InitSpeedMeters(FALSE);

    char lastLantasticCheckRoot[MAX_PATH]; // last path root checked for Lantastic ("" = nothing checked yet)
    lastLantasticCheckRoot[0] = 0;
    BOOL lastIsLantasticPath = FALSE;                                                                                  // result of checking root lastLantasticCheckRoot
    int mustDeleteFileBeforeOverwrite = 0; /* need test */                                                             // (added for SNAP server - NSA drive - SetEndOfFile fails - 0/1/2 = need-test/yes/no
    int allocWholeFileOnStart = 0; /* need test */                                                                     // safety measure (e.g. SNAP servers - NSA drives - may fail); cannot risk a broken Copy - 0/1/2 = need-test/yes/no
    int setDirTimeAfterMove = script->PreserveDirTime && script->SourcePathIsNetwork ? 0 /* need test */ : 2 /* no */; // e.g. on Samba, moving/renaming a directory changes its date and time - 0/1/2 = need-test/yes/no

    BOOL Error = FALSE;
    CQuadWord totalDone;
    totalDone = CQuadWord(0, 0);
    CProgressData pd;
    BOOL novellRenamePatch = FALSE; // TRUE when the read-only attribute must be cleared before MoveFile (required on Novell)
    char* tgtBuffer = NULL;         // conversion buffer for ocConvert
    CAsyncCopyParams* asyncPar = NULL;
    if (buffer != NULL)
    {
        // prefetch strings so we do not load them for every operation individually (fills the LoadStr buffer quickly + throttles)
        char opStrCopying[50];
        // Operation labels are fixed progress-display fields.
        StringCchCopyNA(opStrCopying, _countof(opStrCopying), LoadStr(IDS_COPYING), _countof(opStrCopying) - 1);
        char opStrCopyingPrep[50];
        StringCchCopyNA(opStrCopyingPrep, _countof(opStrCopyingPrep), LoadStr(IDS_COPYINGPREP), _countof(opStrCopyingPrep) - 1);
        char opStrMoving[50];
        StringCchCopyNA(opStrMoving, _countof(opStrMoving), LoadStr(IDS_MOVING), _countof(opStrMoving) - 1);
        char opStrMovingPrep[50];
        StringCchCopyNA(opStrMovingPrep, _countof(opStrMovingPrep), LoadStr(IDS_MOVINGPREP), _countof(opStrMovingPrep) - 1);
        char opStrCreatingDir[50];
        StringCchCopyNA(opStrCreatingDir, _countof(opStrCreatingDir), LoadStr(IDS_CREATINGDIR), _countof(opStrCreatingDir) - 1);
        char opStrDeleting[50];
        StringCchCopyNA(opStrDeleting, _countof(opStrDeleting), LoadStr(IDS_DELETING), _countof(opStrDeleting) - 1);
        char opStrConverting[50];
        StringCchCopyNA(opStrConverting, _countof(opStrConverting), LoadStr(IDS_CONVERTING), _countof(opStrConverting) - 1);
        char opChangAttrs[50];
        StringCchCopyNA(opChangAttrs, _countof(opChangAttrs), LoadStr(IDS_CHANGINGATTRS), _countof(opChangAttrs) - 1);

        int i;
        for (i = 0; !*dlgData.CancelWorker && i < script->Count; i++)
        {
            COperation* op = &script->At(i);
            int attempt = script->BeginItemAttempt(i);

            DWORD identityError;
            if (!CaptureOperationFileIdentities(op, &identityError))
            {
                TRACE_E("Unable to capture handle identity before file-operation item " << i << ": " << GetErrorText(identityError));
                Error = TRUE;
                break;
            }

            if (!script->JournalBeginItem(i, op, attempt))
            {
                TRACE_E("Unable to persist file-operation journal transition before item " << i);
                Error = TRUE;
                break;
            }

            switch (op->Opcode)
            {
            case ocCopyFile:
            {
                const char* opName = "copy file";
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->SourceName, op->TargetName);
                pd.Operation = opStrCopying;
                pd.Source = op->SourceName;
                pd.Preposition = opStrCopyingPrep;
                pd.Target = op->TargetName;
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                BOOL lantasticCheck = IsLantasticDrive(op->TargetName, lastLantasticCheckRoot, _countof(lastLantasticCheckRoot), lastIsLantasticPath);

                Error = !DoCopyFile(op, hProgressDlg, buffer, script, totalDone,
                                    clearReadonlyMask, NULL, lantasticCheck, mustDeleteFileBeforeOverwrite,
                                    allocWholeFileOnStart, dlgData,
                                    (op->OpFlags & OPFL_COPY_ADS) != 0,
                                    (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                    FALSE, asyncPar, NULL);
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->SourceName, op->TargetName, !Error);
                break;
            }

            case ocMoveDir:
            case ocMoveFile:
            {
                const char* opName = op->Opcode == ocMoveDir ? "move dir" : "move file";
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->SourceName, op->TargetName);
                pd.Operation = opStrMoving;
                pd.Source = op->SourceName;
                pd.Preposition = opStrMovingPrep;
                pd.Target = op->TargetName;
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                BOOL lantasticCheck = IsLantasticDrive(op->TargetName, lastLantasticCheckRoot, _countof(lastLantasticCheckRoot), lastIsLantasticPath);
                BOOL ignInvalidName = op->Opcode == ocMoveDir && (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;

                Error = !DoMoveFile(op, hProgressDlg, buffer, script, totalDone,
                                    op->Opcode == ocMoveDir, clearReadonlyMask, &novellRenamePatch,
                                    lantasticCheck, mustDeleteFileBeforeOverwrite,
                                    allocWholeFileOnStart, dlgData,
                                    (op->OpFlags & OPFL_COPY_ADS) != 0,
                                    (op->OpFlags & OPFL_AS_ENCRYPTED) != 0,
                                    &setDirTimeAfterMove, asyncPar, ignInvalidName);
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->SourceName, op->TargetName, !Error);
                break;
            }

            case ocCreateDir:
            {
                const char* opName = "create dir";
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->TargetName, "");
                BOOL copyADS = (op->OpFlags & OPFL_COPY_ADS) != 0;
                BOOL crAsEncrypted = (op->OpFlags & OPFL_AS_ENCRYPTED) != 0;
                BOOL ignInvalidName = (op->OpFlags & OPFL_IGNORE_INVALID_NAME) != 0;
                pd.Operation = opStrCreatingDir;
                pd.Source = op->TargetName;
                pd.Preposition = "";
                pd.Target = "";
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                BOOL skip, alreadyExisted;
                Error = !DoCreateDir(hProgressDlg, op->TargetName, op->Attr, clearReadonlyMask, dlgData,
                                     totalDone, op->Size, op->SourceName, copyADS, script, buffer, skip,
                                     alreadyExisted, crAsEncrypted, ignInvalidName);
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->TargetName, "", !Error);
                if (!Error)
                {
                    if (skip) // skip directory creation
                    {
                        // skip all script operations up to the label that closes this directory
                        CQuadWord skipTotal(0, 0);
                        int createDirIndex = i;
                        while (++i < script->Count)
                        {
                            COperation* oper = &script->At(i);
                            if (oper->Opcode == ocLabelForSkipOfCreateDir && (int)oper->Attr == createDirIndex)
                            {
                                script->AddBytesToTFS(CQuadWord((DWORD)(DWORD_PTR)oper->SourceName, (DWORD)(DWORD_PTR)oper->TargetName));
                                break;
                            }
                            skipTotal += oper->Size;
                        }
                        if (i == script->Count)
                        {
                            i = createDirIndex;
                            TRACE_E("ThreadWorkerBody(): unable to find end-label for dir-create operation: opcode=" << op->Opcode << ", index=" << i);
                        }
                        else
                            totalDone += skipTotal;
                    }
                    else
                    {
                        if (alreadyExisted)
                            op->Attr = 0x10000000 /* dir already existed */;
                        else
                            op->Attr = 0x01000000 /* dir was created */;
                    }
                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                }
                break;
            }

            case ocCopyDirTime:
            {
                BOOL skipSetDirTime = FALSE;
                // locate the skip-label; it stores the index of the create-dir operation along with
                // whether the target directory already existed or was created (date/time are copied
                // only when we created the directory)
                COperation* skipLabel = NULL;
                if (i + 1 < script->Count && script->At(i + 1).Opcode == ocLabelForSkipOfCreateDir)
                    skipLabel = &script->At(i + 1);
                else
                {
                    if (i + 2 < script->Count && script->At(i + 2).Opcode == ocLabelForSkipOfCreateDir)
                        skipLabel = &script->At(i + 2);
                }
                if (skipLabel != NULL)
                {
                    if (skipLabel->Attr < (DWORD)script->Count)
                    {
                        COperation* crDir = &script->At(skipLabel->Attr);
                        if (crDir->Opcode == ocCreateDir && (crDir->OpFlags & OPFL_AS_ENCRYPTED) == 0)
                        {
                            if (crDir->Attr == 0x10000000 /* dir already existed */)
                                skipSetDirTime = TRUE;
                            else
                            {
                                if (crDir->Attr != 0x01000000 /* dir was created */)
                                    TRACE_E("ThreadWorkerBody(): unexpected value of Attr in create-dir operation (not 'existed' nor 'created')!");
                            }
                        }
                        else
                            TRACE_E("ThreadWorkerBody(): unexpected opcode or flags of create-dir operation! Opcode=" << crDir->Opcode << ", OpFlags=" << crDir->OpFlags);
                    }
                    else
                        TRACE_E("ThreadWorkerBody(): unexpected index of create-dir operation! index=" << skipLabel->Attr);
                }
                else
                    TRACE_E("ThreadWorkerBody(): unable to find end-label for dir-create operation (not in first following item nor in second following item)!");

                if (!skipSetDirTime)
                {
                    pd.Operation = opChangAttrs;
                    pd.Source = op->TargetName;
                    pd.Preposition = "";
                    pd.Target = "";
                    SetProgressDialog(hProgressDlg, &pd, dlgData);

                    SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                    FILETIME modified;
                    modified.dwLowDateTime = (DWORD)(DWORD_PTR)op->SourceName;
                    modified.dwHighDateTime = op->Attr;
                    Error = !DoCopyDirTime(hProgressDlg, op->TargetName, &modified, dlgData, FALSE);
                }
                if (!Error)
                {
                    script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                    totalDone += op->Size;
                    SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                }
                break;
            }

            case ocDeleteFile:
            case ocDeleteDir:
            case ocDeleteDirLink:
            {
                const char* opName = op->Opcode == ocDeleteFile ? "delete file" : (op->Opcode == ocDeleteDir ? "delete dir" : "delete dir link");
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->SourceName, "");
                pd.Operation = opStrDeleting;
                pd.Source = op->SourceName;
                pd.Preposition = "";
                pd.Target = "";
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                if (script->IsCopyOrMoveOperation && !script->IsCopyOperation)
                {
                    RecordPlannedMetadataLosses(dlgData, script, op->SourceName, NULL);
                    if (!ConfirmMetadataLossesBeforeSourceDeletion(hProgressDlg, dlgData, op->SourceName, NULL))
                    {
                        totalDone += op->Size;
                        script->SetProgressSize(totalDone);
                        SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                        break; // the user chose to retain this move source
                    }
                }

                if (op->Opcode == ocDeleteFile)
                {
                    Error = !DoDeleteFile(hProgressDlg, op, op->Size,
                                          script, totalDone, op->Attr, dlgData);
                }
                else
                {
                    if (op->Opcode == ocDeleteDir)
                    {
                        Error = !DoDeleteDir(hProgressDlg, op, op->Size,
                                              script, totalDone, op->Attr, (DWORD)(DWORD_PTR)op->TargetName != -1,
                                             dlgData);
                    }
                    else
                    {
                        Error = !DoDeleteDirLink(hProgressDlg, op, op->Size,
                                                  script, totalDone, dlgData);
                    }
                }
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->SourceName, "", !Error);
                break;
            }

            case ocConvert:
            {
                const char* opName = "convert file";
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->SourceName, "");
                // output buffer - the conversion will be performed in it (in the worst case,
                // when the input file contains only CR or LF and we translate them to CRLF,
                // this buffer is twice the size of sourceBuffer) and afterwards we will write from it
                // to the temporary file
                if (tgtBuffer == NULL) // first pass?
                {
                    tgtBuffer = (char*)malloc(FAST_LOCAL_COPY_BUFFER * 2);
                    if (tgtBuffer == NULL)
                    {
                        TRACE_E(LOW_MEMORY);
                        Error = TRUE;
                        break; // error ...
                    }
                }
                pd.Operation = opStrConverting;
                pd.Source = op->SourceName;
                pd.Preposition = "";
                pd.Target = "";
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                Error = !DoConvert(hProgressDlg, op->SourceName, (char*)buffer, tgtBuffer, op->Size, script,
                                   totalDone, convertData, dlgData);
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->SourceName, "", !Error);
                break;
            }

            case ocChangeAttrs:
            {
                const char* opName = "change attrs";
                ExecLogFileOperationStart(script->GetCorrelationId(), i, attempt, opName, op->SourceName, "");
                pd.Operation = opChangAttrs;
                pd.Source = op->SourceName;
                pd.Preposition = "";
                pd.Target = "";
                SetProgressDialog(hProgressDlg, &pd, dlgData);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);

                Error = !DoChangeAttrs(hProgressDlg, op->SourceName, op->Size, (DWORD)(DWORD_PTR)op->TargetName,
                                       script, totalDone,
                                       attrsData->ChangeTimeModified ? &attrsData->TimeModified : NULL,
                                       attrsData->ChangeTimeCreated ? &attrsData->TimeCreated : NULL,
                                       attrsData->ChangeTimeAccessed ? &attrsData->TimeAccessed : NULL,
                                       attrsData->ChangeCompression, attrsData->ChangeEncryption,
                                       op->Attr, dlgData);
                ExecLogFileOperationResult(script->GetCorrelationId(), i, script->GetCurrentItemAttempt(), opName, op->SourceName, "", !Error);
                break;
            }

            case ocLabelForSkipOfCreateDir:
                break; // no action
            }
            script->JournalCompleteItem(!Error);
            if (Error)
                break;
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        }
        if (!Error && !*dlgData.CancelWorker && i == script->Count && totalDone != script->TotalSize &&
            (totalDone != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0))) // intentional change of script->TotalSize to one (prevents division by zero)
        {
            TRACE_E("ThreadWorkerBody(): operation done: totalDone != script->TotalSize (" << totalDone.Value << " != " << script->TotalSize.Value << ")");
        }
        CQuadWord transferredFileSize, progressSize;
        if (!Error && !*dlgData.CancelWorker && i == script->Count &&
            script->GetTFSandProgressSize(&transferredFileSize, &progressSize) &&
            (transferredFileSize != script->TotalFileSize ||
             progressSize != script->TotalSize &&
                 (progressSize != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0)))) // intentional change of script->TotalSize to one (prevents division by zero)
        {
            if (transferredFileSize != script->TotalFileSize)
            {
                TRACE_E("ThreadWorkerBody(): operation done: transferredFileSize != script->TotalFileSize (" << transferredFileSize.Value << " != " << script->TotalFileSize.Value << ")");
            }
            if (progressSize != script->TotalSize &&
                (progressSize != CQuadWord(0, 0) || script->TotalSize != CQuadWord(1, 0)))
            {
                TRACE_E("ThreadWorkerBody(): operation done: progressSize != script->TotalSize (" << progressSize.Value << " != " << script->TotalSize.Value << ")");
            }
        }
    }
    if (asyncPar != NULL)
        delete asyncPar;
    if (tgtBuffer != NULL)
        free(tgtBuffer);
    if (bufferIsAllocated)
        free(buffer);
    script->FinishJournal(Error, script->IsCancellationRequested());
    script->BeginStopping();
    script->Complete(Error);
    EOperationState finalState = script->GetOperationState();
    BOOL cancellationRequested = script->IsCancellationRequested();

    // The progress dialog must never be part of worker shutdown.  In
    // particular, it can be blocked in an owned modal dialog or waiting for
    // this thread while a close/shutdown cancellation is in progress.  Release
    // all worker-owned data first, then transfer the small, self-contained
    // result to the UI by a posted message.
    CWorkerCompletion* completion = new CWorkerCompletion(finalState, cancellationRequested, script->GetCorrelationId());
    FreeScript(script);
    if (!PostMessage(hProgressDlg, WM_USER_PROGRDLG_WORKERCOMPLETE,
                     (WPARAM)Error, (LPARAM)completion))
    {
        // PostMessage can fail only when the progress window has already gone
        // away.  The worker is still fully cleaned up, and remains responsible
        // for the result which was not transferred to the UI.
        delete completion;
    }

    TRACE_I("End");
    return 0;
}

unsigned ThreadWorkerEH(void* param)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE
        return ThreadWorkerBody(param);
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Worker: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // harsher exit (this one still invokes something)
        return 1;
    }
#endif // CALLSTK_DISABLE
}

DWORD WINAPI ThreadWorkerOwned(void* param, HANDLE stopEvent)
{
    // Disk operations retain their legacy cancellation object; the owner event only provides common lifetime ownership.
    UNREFERENCED_PARAMETER(stopEvent);
    CCallStack stack;
    return ThreadWorkerEH(param);
}

CThreadOwner* StartWorker(COperations* script, HWND hDlg, CChangeAttrsData* attrsData,
                   CConvertData* convertData, HANDLE wContinue, HANDLE workerNotSuspended,
                   int* operationProgress, int* summaryProgress)
{
    // Do not allocate worker buffers or start a destructive script after the
    // allocator has declared the process unsafe for additional work.
    if (IsAllocationEmergencyActive())
    {
        script->Fail();
        return NULL;
    }
    CWorkerData data;
    data.WorkerNotSuspended = workerNotSuspended;
    data.OperationProgress = operationProgress;
    data.SummaryProgress = summaryProgress;
    data.WContinue = wContinue;
    data.ConvertData = convertData;
    data.Script = script;
    // Worker correlation IDs must remain complete so diagnostics join the right operation.
    if (FAILED(StringCchCopyA(data.CorrelationId, _countof(data.CorrelationId), script->GetCorrelationId())))
        data.CorrelationId[0] = 0;
    data.HProgressDlg = hDlg;
    data.ClearReadonlyMask = script->ClearReadonlyMask;
    if (attrsData != NULL)
    {
        data.Buffer = attrsData;
        data.BufferIsAllocated = FALSE;
    }
    else
    {
        data.BufferIsAllocated = TRUE;
        data.Buffer = malloc(FAST_LOCAL_COPY_BUFFER);
        if (data.Buffer == NULL)
        {
            TRACE_E(LOW_MEMORY);
            script->Fail();
            return NULL;
        }
    }
    ResetEvent(wContinue);
    if (!script->BeginJournal())
    {
        if (data.BufferIsAllocated)
            free(data.Buffer);
        script->Fail();
        return NULL;
    }
    if (!script->Start())
    {
        script->FinishJournal(TRUE, FALSE);
        return NULL;
    }

    CThreadOwner* worker = new CThreadOwner;
    if (worker == NULL || !worker->Start(ThreadWorkerOwned, &data, "operation worker"))
    {
        if (worker != NULL)
            delete worker;
        if (data.BufferIsAllocated)
            free(data.Buffer);
        TRACE_E("Unable to start Worker thread.");
        script->FinishJournal(TRUE, FALSE);
        script->Fail();
        return NULL;
    }
    //  SetThreadPriority(Worker, THREAD_PRIORITY_HIGHEST);
    DWORD copiedStartupData = WaitForSingleObject(wContinue, INFINITE); // wait until it copies the data (they are on the stack)
    // This wait is intentionally label-only; the ring must not retain script data from the stack.
    RecordReleaseDiagnosticWait("worker_startup", copiedStartupData);
    return worker;
}

void FreeScript(COperations* script)
{
    if (script == NULL)
        return;
    int i;
    for (i = 0; i < script->Count; i++)
    {
        COperation* op = &script->At(i);
        if (op->SourceName != NULL && op->Opcode != ocCopyDirTime && op->Opcode != ocLabelForSkipOfCreateDir)
            free(op->SourceName);
        if (op->TargetName != NULL && op->Opcode != ocChangeAttrs && op->Opcode != ocLabelForSkipOfCreateDir)
            free(op->TargetName);
    }
    if (script->WaitInQueueSubject != NULL)
        free(script->WaitInQueueSubject);
    if (script->WaitInQueueFrom != NULL)
        free(script->WaitInQueueFrom);
    if (script->WaitInQueueTo != NULL)
        free(script->WaitInQueueTo);
    delete script;
}

BOOL COperationsQueue::AddOperation(HWND dlg, BOOL startOnIdle, BOOL* startPaused)
{
    CALL_STACK_MESSAGE1("COperationsQueue::AddOperation()");

    // Queued work may allocate or outlive the recovery marker, so reject it
    // once OOM has requested the controlled shutdown path.
    if (IsAllocationEmergencyActive())
        return FALSE;

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++) // ensure uniqueness (an operation can be added only once)
        if (OperDlgs[i] == dlg)
            break;

    BOOL ret = FALSE;
    if (i == OperDlgs.Count) // the operation can be added
    {
        if (OperDlgs.Count >= DISK_OPERATION_QUEUE_LIMIT)
        {
            // Reject before allocating so queued scripts and dialog handles stay within the shutdown budget.
            RejectedSubmissions++;
            TRACE_E("COperationsQueue::AddOperation(): bounded operation queue rejected admission.");
        }
        else
        {
            OperDlgs.Add(dlg);
            if (OperDlgs.IsGood())
            {
                if (startOnIdle)
                {
                    int j;
                    for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
                        ; // if another operation is already running or was paused manually, start this one as "auto-paused"
                    *startPaused = j < OperPaused.Count;
                }
                else
                    *startPaused = FALSE;
                OperPaused.Add(*startPaused ? 1 /* auto-paused */ : 0 /* running */);
                if (!OperPaused.IsGood())
                {
                    OperPaused.ResetState();
                    OperDlgs.Delete(OperDlgs.Count - 1);
                    if (!OperDlgs.IsGood())
                        OperDlgs.ResetState();
                    RejectedSubmissions++;
                }
                else
                {
                    if (OperDlgs.Count > HighWaterMark)
                        HighWaterMark = OperDlgs.Count;
                    ret = TRUE;
                }
            }
            else
            {
                OperDlgs.ResetState();
                RejectedSubmissions++;
            }
        }
    }
    else
        TRACE_E("COperationsQueue::AddOperation(): this operation has already been added!");

    HANDLES(LeaveCriticalSection(&QueueCritSect));

    return ret;
}

void COperationsQueue::OperationEnded(HWND dlg, BOOL doNotResume, HWND* foregroundWnd)
{
    CALL_STACK_MESSAGE1("COperationsQueue::OperationEnded()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    BOOL found = FALSE;
    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            found = TRUE;
            OperDlgs.Delete(i);
            if (!OperDlgs.IsGood())
                OperDlgs.ResetState();
            OperPaused.Delete(i);
            if (!OperPaused.IsGood())
                OperPaused.ResetState();
            break;
        }
    }
    if (!found)
        TRACE_E("COperationsQueue::OperationEnded(): unexpected situation: operation was not found!");
    else
    {
        if (!doNotResume)
        {
            int j;
            for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
                ; // if no operation is running and none was paused manually, resume the first one in the queue
            if (j == OperPaused.Count && OperDlgs.Count > 0)
            {
                PostMessage(OperDlgs[0], WM_COMMAND, CM_RESUMEOPER, 0);
                if (foregroundWnd != NULL && GetForegroundWindow() == dlg)
                    *foregroundWnd = OperDlgs[0];
            }
        }
    }

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

void COperationsQueue::SetPaused(HWND dlg, int paused)
{
    CALL_STACK_MESSAGE1("COperationsQueue::SetPaused()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            OperPaused[i] = paused;
            break;
        }
    }
    if (i == OperDlgs.Count)
        TRACE_E("COperationsQueue::SetPaused(): operation was not found!");

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

BOOL COperationsQueue::IsEmpty()
{
    CALL_STACK_MESSAGE1("COperationsQueue::IsEmpty()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    BOOL ret = OperDlgs.Count == 0;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return ret;
}

void COperationsQueue::AutoPauseOperation(HWND dlg, HWND* foregroundWnd)
{
    CALL_STACK_MESSAGE1("COperationsQueue::AutoPauseOperation()");

    HANDLES(EnterCriticalSection(&QueueCritSect));

    int i;
    for (i = 0; i < OperDlgs.Count; i++)
    {
        if (OperDlgs[i] == dlg)
        {
            int j;
            for (j = i; j + 1 < OperDlgs.Count; j++)
                OperDlgs[j] = OperDlgs[j + 1];
            for (j = i; j + 1 < OperPaused.Count; j++)
                OperPaused[j] = OperPaused[j + 1];
            OperDlgs[j] = dlg;
            OperPaused[j] = 1 /* auto-paused */;
            break;
        }
    }
    if (i == OperDlgs.Count)
        TRACE_E("COperationsQueue::AutoPauseOperation(): operation was not found!");

    // if no operation is running and none was paused manually, resume the first one in the queue
    int j;
    for (j = 0; j < OperPaused.Count && OperPaused[j] == 1 /* auto-paused */; j++)
        ;
    if (j == OperPaused.Count && OperDlgs.Count > 0)
    {
        PostMessage(OperDlgs[0], WM_COMMAND, CM_RESUMEOPER, 0);
        if (foregroundWnd != NULL && GetForegroundWindow() == dlg)
            *foregroundWnd = OperDlgs[0];
    }

    HANDLES(LeaveCriticalSection(&QueueCritSect));
}

int COperationsQueue::GetNumOfOperations()
{
    CALL_STACK_MESSAGE1("COperationsQueue::GetNumOfOperations()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    int c = OperDlgs.Count;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return c;
}

COperationsQueueMetrics COperationsQueue::GetQueueMetrics()
{
    CALL_STACK_MESSAGE1("COperationsQueue::GetQueueMetrics()");

    HANDLES(EnterCriticalSection(&QueueCritSect));
    COperationsQueueMetrics metrics;
    metrics.Capacity = DISK_OPERATION_QUEUE_LIMIT;
    metrics.Queued = OperDlgs.Count;
    metrics.HighWaterMark = HighWaterMark;
    metrics.RejectedSubmissions = RejectedSubmissions;
    HANDLES(LeaveCriticalSection(&QueueCritSect));
    return metrics;
}



