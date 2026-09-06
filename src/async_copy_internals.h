// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

#include "operation_result.h"
#include "worker.h"
#include "common/stable_move_source.h" // retain the move source across reader retries and destination commit
#include "common/recovery_evidence.h" // readiness must refer to the staging object opened by the writer

// Declarations shared between async_copy.cpp and the helper files extracted from it
// (ads_operations.cpp, copy_commit.cpp, copy_loop.cpp), so cross-file internals
// stay in sync instead of drifting as ad-hoc externs. Include after precomp.h.

extern NTQUERYINFORMATIONFILE DynNtQueryInformationFile;
extern NTFSCONTROLFILE DynNtFsControlFile;

DWORD GetTemporaryNameSeed();

// alternate data streams (ads_operations.cpp)
BOOL DeleteAllADS(HANDLE file, const char* fileName);
BOOL DeleteThroughRecycleBin(HWND owner, const char* path, DWORD* error); // recycle_bin_delete.cpp
BOOL CheckTailOfOutFile(CAsyncCopyParams* asyncPar, HANDLE in, HANDLE out, const CQuadWord& offset,
                        const CQuadWord& curInOffset, BOOL ignoreReadErrOnOut);
BOOL DoCopyADS(HWND hProgressDlg, const char* sourceName, BOOL isDir, const char* targetName,
               CQuadWord const& totalDone, CQuadWord& operDone, CQuadWord const& operTotal,
               CProgressDlgData& dlgData, COperations* script, BOOL* skip, void* buffer,
               int optimalBufferSize = 0, BOOL stableMoveSource = FALSE);

// transactional target handling (copy_commit.cpp)
BOOL CreateTransactionalTargetFileName(const char* targetName, char* temporaryName, int temporaryNameLen);
HANDLE OpenTransactionalTargetFile(const char* temporaryName, DWORD desiredAccess,
                                   DWORD flagsAndAttributes, BOOL* encryptionNotSupported);
COperationResult CommitTransactionalTargetFile(const char* targetName, const char* temporaryName,
                                                       const COperation::CFileIdentity& expectedTargetIdentity,
                                                       const CRecoveryObjectEvidence& expectedTemporaryIdentity,
                                                       ULONGLONG expectedSize,
                                                       COperations* script);
COperationResult VerifyDurableCopyCommit(const char* targetName, const CQuadWord& expectedSize);
BOOL VerifyFullFileContentSha256(const char* sourceName, const char* targetName, DWORD* error);
// Reopen the held source object for moves; ordinary copies retain their sharing policy.
HANDLE OpenCopySourceForRead(const char* sourceName, CStableMoveSource* stableMoveSource, DWORD flags);

// copy loop plumbing (copy_loop.cpp)
// SetProgressWithoutSuspend remains defined in async_copy.cpp; it had no prior
// header declaration because all callers shared one translation unit.
void SetProgressWithoutSuspend(HWND hProgressDlg, int operation, int summary, CProgressDlgData& dlgData);
BOOL SyncOrAsyncDeviceIoControl(CAsyncCopyParams* asyncPar, HANDLE hDevice, DWORD dwIoControlCode,
                                LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                                DWORD nOutBufferSize, LPDWORD lpBytesReturned, DWORD* err);
void SetCompressAndEncryptedAttrs(const char* name, DWORD attr, HANDLE* out, BOOL openAlsoForRead,
                                  BOOL* encryptionNotSupported, CAsyncCopyParams* asyncPar);
BOOL DisableLocalBuffering(CAsyncCopyParams* asyncPar, HANDLE file, DWORD* err);
void DoCopyFileLoopOrig(HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                        COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated,
                        COperation* op, const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy,
                        HWND hProgressDlg, CQuadWord& operationDone, CQuadWord& fileSize,
                        int bufferSize, int& allocWholeFileOnStart, BOOL& copyAgain,
                        CStableMoveSource* stableMoveSource = NULL);
void DoCopyFileLoopAsync(CAsyncCopyParams* asyncPar, HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                         COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated, COperation* op,
                         const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy, HWND hProgressDlg,
                         CQuadWord& operationDone, CQuadWord& fileSize, int bufferSize,
                         int& allocWholeFileOnStart, BOOL& copyAgain, const CQuadWord& lastTransferredFileSize);
