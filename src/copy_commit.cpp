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

// Transactional target creation/commit, durability verification, SHA-256 content
// verification, and SalCreateFileEx extracted from async_copy.cpp as a mechanical
// move. Helpers previously file-local were promoted to external linkage with
// declarations in async_copy_internals.h because their callers remain in
// async_copy.cpp; SourceHasStream and CalculateFileSha256 stay file-local.
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

// Reserve a unique file in the destination directory.  The reservation is opened with
// CREATE_ALWAYS by DoCopyFile and is never visible under the requested target name.
// Keeping the temporary file beside its final name guarantees that ReplaceFileW is a
// same-volume commit.
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

// ReplaceFileW preserves the previous destination until the file system commits the
// replacement.  If another actor removed the destination after the overwrite prompt,
// a write-through same-volume rename is the equivalent commit for a newly absent file.
COperationResult CommitTransactionalTargetFile(const char* targetName, const char* temporaryName,
                                                       const COperation::CFileIdentity& expectedTargetIdentity)
{
    // A name can be swapped while the temporary copy is being written.  Reopen
    // it without following reparse points and compare both its object ID and
    // handle-resolved final path before ReplaceFileW can touch it.
    DWORD error = ERROR_SUCCESS;
    if (!VerifyFileIdentity(targetName, expectedTargetIdentity, &error))
        return COperationResult::Failure(orpVerifyDestinationIdentity, error, temporaryName, targetName,
                                         IsRetryableOperationError(error), opeTemporaryTargetReady);

    if (OperationExecutionFileSystem().ReplaceFile(targetName, temporaryName))
        return COperationResult::Success(orpCommitTransactionalTarget, temporaryName, targetName,
                                         opeTemporaryTargetReady | opeDestinationCommitted);

    error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND &&
        OperationExecutionFileSystem().MoveFile(temporaryName, targetName))
    {
        return COperationResult::Success(orpCommitTransactionalTarget, temporaryName, targetName,
                                         opeTemporaryTargetReady | opeDestinationCommitted);
    }
    if (error == ERROR_FILE_NOT_FOUND)
        error = GetLastError();
    return COperationResult::Failure(orpCommitTransactionalTarget, error, temporaryName, targetName,
                                     IsRetryableOperationError(error), opeTemporaryTargetReady);
}

// TRUE when the source carries a stream of this name. Files hold a handful of
// streams, so re-enumerating per candidate is cheaper than building an index.
static BOOL SourceHasStream(const WCHAR* sourcePath, const WCHAR* streamName)
{
    WIN32_FIND_STREAM_DATA stream;
    HANDLE find = FindFirstStreamW(sourcePath, FindStreamInfoStandard, &stream, 0);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;

    BOOL found = FALSE;
    do
    {
        if (_wcsicmp(stream.cStreamName, streamName) == 0)
        {
            found = TRUE;
            break;
        }
    } while (FindNextStreamW(find, &stream));
    FindClose(find);
    return found;
}

// ReplaceFileW deliberately merges the replaced file's alternate data streams
// into the committed result, so a stream that existed only on the old
// destination would outlive the file it belonged to. An overwrite has to leave
// the destination equal to the source, so remove what the source does not
// carry. This runs after the commit, never before: the transactional target
// exists precisely so that a failed commit leaves the old file untouched.
void RemoveCommittedStreamsMissingFromSource(const char* sourceName, const char* targetName)
{
    CPathW sourcePathW(sourceName);
    CPathW targetPathW(targetName);

    WIN32_FIND_STREAM_DATA targetStream;
    HANDLE find = FindFirstStreamW(targetPathW.CStr(), FindStreamInfoStandard, &targetStream, 0);
    if (find == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (_wcsicmp(targetStream.cStreamName, L"::$DATA") == 0)
            continue; // the file's own contents, replaced by the commit itself
        if (SourceHasStream(sourcePathW.CStr(), targetStream.cStreamName))
            continue;

        // An ADS name is a suffix rather than a child path, so append it into
        // growable storage without inserting a slash and preserve long paths.
        CPathW streamPath(targetPathW.CStr());
        size_t targetLength = streamPath.GetLength();
        size_t streamLength = wcslen(targetStream.cStreamName);
        WCHAR* streamPathBuffer = streamPath.GetBuffer(targetLength + streamLength + 1);
        if (streamPathBuffer == NULL)
            continue;
        memcpy(streamPathBuffer + targetLength, targetStream.cStreamName,
               (streamLength + 1) * sizeof(WCHAR));
        streamPath.ReleaseBuffer((int)(targetLength + streamLength));
        const WCHAR* streamApiPath = streamPath.GetPathForWin32Api();
        if (streamApiPath == NULL || !DeleteFileW(streamApiPath))
        {
            DWORD err = GetLastError();
            TRACE_E("RemoveCommittedStreamsMissingFromSource(): unable to remove a stale alternate data stream from "
                    << targetName << ", error: " << GetErrorText(err));
        }
    } while (FindNextStreamW(find, &targetStream));
    FindClose(find);
}

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
