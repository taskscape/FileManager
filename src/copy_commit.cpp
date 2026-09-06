// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <bcrypt.h>
#include <strsafe.h>

#pragma comment(lib, "bcrypt.lib")

#include "file_operation_filesystem.h"
#include "operation_result.h"
#include "worker.h"

#include "async_copy_internals.h"
#include "common/conditional_file_publication.h"

// Transactional target creation/commit, durability verification, SHA-256 content
// verification, and SalCreateFileEx extracted from async_copy.cpp as a mechanical
// move. Helpers previously file-local were promoted to external linkage with
// declarations in async_copy_internals.h because their callers remain in
// async_copy.cpp; CalculateFileSha256 stays file-local.
DWORD GetTemporaryNameSeed()
{
    // Preserve the legacy 12-bit, 10 ms filename seed while avoiding a 32-bit uptime wrap.
    return (DWORD)((CMonotonicClock::Now() / 10) % 0xFFF);
}

HANDLE SalCreateFileEx(const char* fileName, DWORD desiredAccess,
                       DWORD shareMode, DWORD flagsAndAttributes, BOOL* encryptionNotSupported)
{
    DWORD err = ERROR_SUCCESS;
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileName, -1));
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                       CREATE_NEW, flagsAndAttributes, NULL));
    if (out == INVALID_HANDLE_VALUE)
    {
        err = GetLastError();
        if (encryptionNotSupported != NULL && (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
        { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
            out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                        CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL));
            if (out != INVALID_HANDLE_VALUE)
            {
                *encryptionNotSupported = TRUE;
                NOHANDLES(CloseHandle(out));
                out = INVALID_HANDLE_VALUE;
                if (!DeleteFileW(fileNameW)) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                    TRACE_I("Unable to delete testing target file: " << fileName);
            }
        }
        if (err == ERROR_FILE_EXISTS || // check whether this is merely overwriting the DOS name
            err == ERROR_ALREADY_EXISTS ||
            err == ERROR_ACCESS_DENIED)
        {
            WIN32_FIND_DATAW data;
            HANDLE find = HANDLES_Q(FindFirstFileW(fileNameW, &data));
            if (find != INVALID_HANDLE_VALUE)
            {
                HANDLES(FindClose(find));
                if (err != ERROR_ACCESS_DENIED || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    const char* tgtName = SalPathFindFileName(fileName);
                    char altName[MAX_PATH];
                    char fullName[MAX_PATH];
                    if (ConvertWideToUtf8(data.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                        altName[0] = 0;
                    if (ConvertWideToUtf8(data.cFileName, -1, fullName, _countof(fullName)) == 0)
                        fullName[0] = 0;
                    if (StrICmp(tgtName, altName) == 0 && // match only for DOS name
                        StrICmp(tgtName, fullName) != 0) // (full name differs)
                    {
                        // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (no extra DOS name needed)
                        char tmpName[MAX_PATH + 20];
                        if (strlen(fileName) >= _countof(tmpName))
                        {
                            TRACE_E("SalCreateFileEx(): path too long for DOS-name collision workaround: " << fileName);
                        }
                        else
                        {
                            CPathW tmpDirW(fileNameW);
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
                                if (tmpName[0] != 0) // if we successfully "tidied" the conflicting file, try creating
                                {                    // the target file again, then restore the original name
                                    out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                                                CREATE_NEW, flagsAndAttributes, NULL));
                                    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported != NULL &&
                                        (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
                                    { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                        out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                                                    CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL));
                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            *encryptionNotSupported = TRUE;
                                            NOHANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            if (!DeleteFileW(fileNameW)) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                                                TRACE_E("Unable to delete testing target file: " << fileName);
                                        }
                                    }
                                    if (!SalMoveFile(tmpName, origFullName))
                                    { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of fileName (the DOS name)
                                        TRACE_I("Unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);

                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            NOHANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            DeleteFileW(fileNameW);
                                            if (!SalMoveFile(tmpName, origFullName))
                                                TRACE_E("Fatal unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);
                                        }
                                    }
                                    else
                                    {
                                        if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                        {
                                            CStrP origFullNameW(ConvertAllocUtf8ToWide(origFullName, -1));
                                            if (origFullNameW != NULL)
                                                SetFileAttributesW(origFullNameW, origFullNameAttr); // leave without extra handling or retry; not critical (normally toggles unpredictably)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (out == INVALID_HANDLE_VALUE)
            SetLastError(err);
        return out;
    }

// Preserve normal-copy sharing while moves reopen their retained source object.
HANDLE OpenCopySourceForRead(const char* sourceName, CStableMoveSource* stableMoveSource, DWORD flags)
{
    if (stableMoveSource == NULL)
        return HANDLES_Q(CreateFileUtf8(sourceName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING, flags, NULL));
    // A retry must read the locked object, including after the pathname changes.
    HANDLE reader = stableMoveSource->OpenReader(flags);
    if (reader != INVALID_HANDLE_VALUE)
        HANDLES_ADD(__htFile, __hoCreateFile, reader);
    return reader;
}

// Reserve a sibling so publication stays on the destination volume.
BOOL CreateTransactionalTargetFileName(const char* targetName, char* temporaryName, int temporaryNameLen)
{
    WCHAR targetDirectoryW[3 * MAX_PATH];
    CPathW targetPathW(targetName);
    if (FAILED(StringCchCopyW(targetDirectoryW, _countof(targetDirectoryW), targetPathW.CStr())))
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    if (!CutDirectoryW(targetDirectoryW))
    {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    char targetDirectory[3 * MAX_PATH];
    CPathW(targetDirectoryW).ToUtf8(targetDirectory, sizeof(targetDirectory));
    return SalGetTempFileName(targetDirectory, "SALCP", temporaryName, temporaryNameLen, TRUE);
}

HANDLE OpenTransactionalTargetFile(const char* temporaryName, DWORD desiredAccess,
                                          DWORD flagsAndAttributes, BOOL* encryptionNotSupported)
{
    HANDLE out = OperationExecutionFileSystem().CreateFile(temporaryName, desiredAccess, 0,
                                                            CREATE_ALWAYS, flagsAndAttributes);
    if (out != INVALID_HANDLE_VALUE)
        HANDLES_ADD(__htFile, __hoCreateFile, out);
    if (out == INVALID_HANDLE_VALUE && (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
    {
        out = OperationExecutionFileSystem().CreateFile(temporaryName, desiredAccess, 0,
                                                         CREATE_ALWAYS,
                                                         flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY));
        if (out != INVALID_HANDLE_VALUE)
        {
            HANDLES_ADD(__htFile, __hoCreateFile, out);
            *encryptionNotSupported = TRUE;
            HANDLES(CloseHandle(out));
            out = INVALID_HANDLE_VALUE;
        }
    }
    return out;
}

// The journal owns durable publication facts; the shared native helper owns
// handles and never substitutes a different pathname occupant for either file.
static BOOL RecordCopyPublication(void* context, const char* state, const WCHAR* backup)
{
    return ((COperations*)context)->JournalRecordPublicationState(state, backup);
}

COperationResult CommitTransactionalTargetFile(const char* targetName, const char* temporaryName,
                                                       const COperation::CFileIdentity& expectedTargetIdentity,
                                                       const CRecoveryObjectEvidence& expectedTemporaryIdentity,
                                                       ULONGLONG expectedSize,
                                                       COperations* script)
{
    // Keep the approved destination open through its rename to a recoverable
    // backup. Relative publication cannot follow a retargeted ancestor junction.
    DWORD error = ERROR_SUCCESS;
    CPathW targetPathW(targetName);
    CPathW temporaryPathW(temporaryName);
    CConditionalFilePublication publication;
    if (script == NULL || (expectedTargetIdentity.State != 1 && expectedTargetIdentity.State != 2))
        error = ERROR_INVALID_DATA;
    else if (!publication.Open(targetPathW.GetPathForWin32Api(), temporaryPathW.GetPathForWin32Api(),
                                expectedTargetIdentity.State == 2, !script->CopySecurity))
        error = GetLastError();
    else if (expectedTargetIdentity.State == 2 &&
             !VerifyFileHandleIdentity(publication.TargetHandle(), expectedTargetIdentity, &error))
    {
        // Identity mismatch is a conflict, even if the name still exists.
    }
    if (error != ERROR_SUCCESS)
        return COperationResult::Failure(orpVerifyDestinationIdentity, error, temporaryName, targetName,
                                         IsRetryableOperationError(error), opeTemporaryTargetReady);

    // A same-name substitute or a truncated stage must not become recoverable
    // merely because an earlier copy attempt wrote a ready marker.
    CRecoveryObjectEvidence actualTemporary;
    if (!ReadRecoveryObjectIdentity(publication.TemporaryHandle(), actualTemporary)) error = GetLastError();
    else if (!SameRecoveryObject(expectedTemporaryIdentity, actualTemporary) || actualTemporary.Length != expectedSize)
        error = ERROR_INVALID_DATA;
    if (error == ERROR_SUCCESS && !script->JournalMarkTemporaryReady(targetName, temporaryName,
        publication.DirectoryHandle(), publication.TargetHandle(), publication.TemporaryHandle(), !script->CopySecurity))
        error = ERROR_WRITE_FAULT;
    if (error != ERROR_SUCCESS)
        return COperationResult::Failure(orpVerifyDurableCopy, error, temporaryName, targetName,
                                         IsRetryableOperationError(error), opeTemporaryTargetReady);

    const CPublicationOutcome outcome = publication.Commit(OperationExecutionFileSystem(), RecordCopyPublication, script);
    DWORD effects = opeTemporaryTargetReady;
    if (outcome.Committed) effects |= opeDestinationCommitted;
    if (outcome.BackupRetained) effects |= opePublicationBackupRetained;
    // Failure to finish backup cleanup is reported with the committed effect;
    // the caller keeps a move source and cannot retry an already published file.
    error = outcome.Error != ERROR_SUCCESS ? outcome.Error : outcome.CleanupError;
    COperationResult result = error == ERROR_SUCCESS ?
        COperationResult::Success(orpCommitTransactionalTarget, temporaryName, targetName,
                                  effects) :
        COperationResult::Failure(orpCommitTransactionalTarget, error, temporaryName, targetName,
                                  !outcome.Committed && IsRetryableOperationError(error), effects);
    // The journal records the precise '<temporary>.previous' backup name.
    result.AppendCleanupError(orcpPublicationBackup, outcome.CleanupError, temporaryName);
    return result;
}

// Conditional publication carries only the staged streams. No post-commit ADS
// deletion is needed, so an enumeration error cannot remove a valid copied stream.

// A successful write is not a copy commit.  Reopen the closed destination and
// verify its on-disk file metadata before reporting success, replacing an old
// target, or allowing a cross-volume move to remove its source.
COperationResult VerifyDurableCopyCommit(const char* targetName, const CQuadWord& expectedSize)
{
    HANDLE target = HANDLES_Q(CreateFileUtf8(targetName, GENERIC_READ,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (target == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        return COperationResult::Failure(orpVerifyDurableCopy, error, NULL, targetName,
                                         IsRetryableOperationError(error));
    }

    BY_HANDLE_FILE_INFORMATION information;
    BOOL verified = GetFileInformationByHandle(target, &information);
    DWORD error = ERROR_SUCCESS;
    if (!verified)
        error = GetLastError();
    else if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
             information.nFileSizeLow != expectedSize.LoDWord ||
             information.nFileSizeHigh != expectedSize.HiDWord)
    {
        error = ERROR_WRITE_FAULT;
        verified = FALSE;
    }

    // Capture the verification result before CloseHandle can replace GetLastError.
    COperationResult result = verified ? COperationResult::Success(orpVerifyDurableCopy, NULL, targetName) :
                                         COperationResult::Failure(orpVerifyDurableCopy, error, NULL, targetName,
                                                                   IsRetryableOperationError(error));
    if (!HANDLES(CloseHandle(target)))
    {
        DWORD cleanupError = GetLastError();
        if (result.Succeeded())
            result = COperationResult::Failure(orpVerifyDurableCopy, cleanupError, NULL, targetName,
                                               IsRetryableOperationError(cleanupError));
        else
            result.AppendCleanupError(orcpCloseVerificationHandle, cleanupError, targetName);
    }
    return result;
}

// A post-close size check establishes a durable destination, but it cannot
// distinguish two equally sized byte streams.  Cross-volume moves retain the
// source until this comparison succeeds after a copy path has had suspicious
// I/O retries.
static BOOL CalculateFileSha256(const char* fileName, BYTE digest[32], DWORD* error)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR hashObject = NULL;
    BYTE* buffer = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD hashObjectLength = 0;
    DWORD resultLength = 0;
    BOOL calculated = FALSE;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectLength,
                          sizeof(hashObjectLength), &resultLength, 0) != 0 ||
        (hashObject = (PUCHAR)malloc(hashObjectLength)) == NULL ||
        BCryptCreateHash(algorithm, &hash, hashObject, hashObjectLength, NULL, 0, 0) != 0)
    {
        *error = ERROR_NOT_SUPPORTED;
        goto CLEANUP;
    }

    if ((file = HANDLES_Q(CreateFileUtf8(fileName, GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL))) == INVALID_HANDLE_VALUE)
    {
        *error = GetLastError();
        goto CLEANUP;
    }
    if ((buffer = (BYTE*)malloc(64 * 1024)) == NULL)
    {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        goto CLEANUP;
    }

    while (1)
    {
        DWORD bytesRead;
        if (!ReadFile(file, buffer, 64 * 1024, &bytesRead, NULL))
        {
            *error = GetLastError();
            goto CLEANUP;
        }
        if (bytesRead == 0)
            break;
        if (BCryptHashData(hash, buffer, bytesRead, 0) != 0)
        {
            *error = ERROR_READ_FAULT;
            goto CLEANUP;
        }
    }
    if (BCryptFinishHash(hash, digest, 32, 0) != 0)
    {
        *error = ERROR_READ_FAULT;
        goto CLEANUP;
    }
    calculated = TRUE;

CLEANUP:
    if (file != INVALID_HANDLE_VALUE && !HANDLES(CloseHandle(file)) && calculated)
    {
        *error = GetLastError();
        calculated = FALSE;
    }
    if (buffer != NULL)
        free(buffer);
    if (hash != NULL)
        BCryptDestroyHash(hash);
    if (hashObject != NULL)
        free(hashObject);
    if (algorithm != NULL)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return calculated;
}

BOOL VerifyFullFileContentSha256(const char* sourceName, const char* targetName, DWORD* error)
{
    BYTE sourceDigest[32];
    BYTE targetDigest[32];
    if (!CalculateFileSha256(sourceName, sourceDigest, error) ||
        !CalculateFileSha256(targetName, targetDigest, error))
    {
        return FALSE;
    }
    if (memcmp(sourceDigest, targetDigest, sizeof(sourceDigest)) != 0)
    {
        *error = ERROR_CRC;
        return FALSE;
    }
    return TRUE;
}
