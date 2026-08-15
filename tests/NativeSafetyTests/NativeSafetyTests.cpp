// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "../../src/common/checked_arithmetic.h"
#include "../../src/operation_execution_filesystem.h"

namespace
{
int Fail(const char* message)
{
    fprintf(stderr, "NativeSafetyTests: %s\n", message);
    return 1;
}

int TestCheckedArithmeticBoundaries()
{
    uint64_t value = 0;
    DWORD dword = 0;
    size_t size = 0;

    // These native boundary cases prevent a future caller from accepting an
    // allocation or Win32 byte count that has silently wrapped.
    if (!CheckedAddUInt64(UINT64_MAX - 1, 1, &value) || value != UINT64_MAX)
        return Fail("CheckedAddUInt64 rejected a valid boundary sum");
    if (CheckedAddUInt64(UINT64_MAX, 1, &value))
        return Fail("CheckedAddUInt64 accepted an overflowing sum");
    if (!CheckedMultiplyUInt64(UINT64_MAX, 1, &value) || value != UINT64_MAX)
        return Fail("CheckedMultiplyUInt64 rejected a valid boundary product");
    if (CheckedMultiplyUInt64(UINT64_MAX, 2, &value))
        return Fail("CheckedMultiplyUInt64 accepted an overflowing product");
    if (!CheckedCastUInt64ToDword(MAXDWORD, &dword) || dword != MAXDWORD)
        return Fail("CheckedCastUInt64ToDword rejected MAXDWORD");
    if (CheckedCastUInt64ToDword((uint64_t)MAXDWORD + 1, &dword))
        return Fail("CheckedCastUInt64ToDword accepted a truncated value");
    if (!CheckedCastUInt64ToSize(1, &size) || size != 1)
        return Fail("CheckedCastUInt64ToSize rejected a valid value");
    return 0;
}

int TestNativeFileOperationCharacterization()
{
    wchar_t temporaryPath[MAX_PATH];
    wchar_t temporaryDirectory[MAX_PATH];
    const char payload[] = "native-characterization";
    const char* failure = NULL;

    // Keep the C++ characterization self-owned so destructive Win32 calls cannot escape the test directory.
    if (GetTempPathW(_countof(temporaryPath), temporaryPath) == 0 ||
        GetTempFileNameW(temporaryPath, L"nst", 0, temporaryDirectory) == 0 ||
        !DeleteFileW(temporaryDirectory) || !CreateDirectoryW(temporaryDirectory, NULL))
        return Fail("could not create the native characterization directory");

    const std::wstring source = std::wstring(temporaryDirectory) + L"\\source.txt";
    const std::wstring copied = std::wstring(temporaryDirectory) + L"\\copied.txt";
    const std::wstring renamed = std::wstring(temporaryDirectory) + L"\\renamed.txt";
    const std::wstring moved = std::wstring(temporaryDirectory) + L"\\moved.txt";
    HANDLE sourceHandle = CreateFileW(source.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD bytesWritten = 0;
    if (sourceHandle == INVALID_HANDLE_VALUE ||
        !WriteFile(sourceHandle, payload, sizeof(payload) - 1, &bytesWritten, NULL) ||
        bytesWritten != sizeof(payload) - 1)
    {
        failure = "could not create the native source file";
    }
    if (sourceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(sourceHandle);

    if (failure == NULL && !CopyFileW(source.c_str(), copied.c_str(), FALSE))
        failure = "native copy did not create the destination";
    if (failure == NULL && !MoveFileW(copied.c_str(), renamed.c_str()))
        failure = "native rename did not preserve the copied entry";
    if (failure == NULL && !MoveFileW(source.c_str(), moved.c_str()))
        failure = "native move did not relocate the source entry";
    if (failure == NULL && (!DeleteFileW(renamed.c_str()) || !DeleteFileW(moved.c_str())))
        failure = "native delete did not remove the moved and renamed entries";
    if (failure == NULL && (GetFileAttributesW(renamed.c_str()) != INVALID_FILE_ATTRIBUTES ||
                            GetFileAttributesW(moved.c_str()) != INVALID_FILE_ATTRIBUTES))
        failure = "native delete left a visible file entry";

    DeleteFileW(source.c_str());
    DeleteFileW(copied.c_str());
    DeleteFileW(renamed.c_str());
    DeleteFileW(moved.c_str());
    RemoveDirectoryW(temporaryDirectory);
    return failure != NULL ? Fail(failure) : 0;
}

class CPhaseFailingFileSystem : public COperationExecutionFileSystem
{
public:
    enum EFailingPhase
    {
        fpCreate,
        fpWrite,
        fpMetadata,
        fpFlush,
        fpReplace,
        fpMove,
        fpDelete,
        fpSuccess
    } FailingPhase;

    CPhaseFailingFileSystem(EFailingPhase failingPhase, DWORD failingError)
        : FailingPhase(failingPhase), FailingError(failingError), Calls(0) {}

    HANDLE CreateFile(const char*, DWORD, DWORD, DWORD, DWORD) override { return Fail(fpCreate) ? INVALID_HANDLE_VALUE : (HANDLE)1; }
    BOOL WriteFile(HANDLE, const void*, DWORD bytesToWrite, DWORD* bytesWritten, LPOVERLAPPED) override
    {
        if (Fail(fpWrite))
            return FALSE;
        if (bytesWritten != NULL)
            *bytesWritten = bytesToWrite;
        return TRUE;
    }
    BOOL SetFileTime(HANDLE, const FILETIME*, const FILETIME*, const FILETIME*) override { return !Fail(fpMetadata); }
    BOOL FlushFileBuffers(HANDLE) override { return !Fail(fpFlush); }
    BOOL ReplaceFile(const char*, const char*) override { return !Fail(fpReplace); }
    BOOL MoveFile(const char*, const char*) override { return !Fail(fpMove); }
    BOOL SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, void*, DWORD) override { return !Fail(fpDelete); }

    int GetCalls() const { return Calls; }

private:
    BOOL Fail(EFailingPhase phase) const
    {
        ++Calls;
        if (FailingPhase != phase)
            return FALSE;
        SetLastError(FailingError);
        return TRUE;
    }

    DWORD FailingError;
    mutable int Calls;
};

CPhaseFailingFileSystem::EFailingPhase RunTransactionalFaultSequence(COperationExecutionFileSystem& fileSystem,
                                                                       BOOL useMoveCommit)
{
    DWORD bytesWritten = 0;
    FILE_DISPOSITION_INFO disposition = {TRUE};
    HANDLE target = fileSystem.CreateFile("temporary", GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (target == INVALID_HANDLE_VALUE)
        return CPhaseFailingFileSystem::fpCreate;
    if (!fileSystem.WriteFile(target, "x", 1, &bytesWritten, NULL) || bytesWritten != 1)
        return CPhaseFailingFileSystem::fpWrite;
    if (!fileSystem.SetFileTime(target, NULL, NULL, NULL))
        return CPhaseFailingFileSystem::fpMetadata;
    if (!fileSystem.FlushFileBuffers(target))
        return CPhaseFailingFileSystem::fpFlush;
    if (useMoveCommit)
    {
        if (!fileSystem.MoveFile("temporary", "target"))
            return CPhaseFailingFileSystem::fpMove;
    }
    else if (!fileSystem.ReplaceFile("target", "temporary"))
        return CPhaseFailingFileSystem::fpReplace;
    if (!fileSystem.SetFileInformationByHandle((HANDLE)1, FileDispositionInfo, &disposition, sizeof(disposition)))
        return CPhaseFailingFileSystem::fpDelete;
    return CPhaseFailingFileSystem::fpSuccess;
}

int TestExecutionAdapterFaultInjection()
{
    const CPhaseFailingFileSystem::EFailingPhase phases[] = {
        CPhaseFailingFileSystem::fpCreate, CPhaseFailingFileSystem::fpWrite,
        CPhaseFailingFileSystem::fpMetadata, CPhaseFailingFileSystem::fpFlush,
        CPhaseFailingFileSystem::fpReplace, CPhaseFailingFileSystem::fpMove,
        CPhaseFailingFileSystem::fpDelete};
    const int expectedCalls[] = {1, 2, 3, 4, 5, 5, 6};

    const DWORD operationalErrors[] = {ERROR_DISK_FULL, ERROR_DISK_QUOTA_EXCEEDED, ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION};
    for (size_t errorIndex = 0; errorIndex != _countof(operationalErrors); ++errorIndex)
    {
        for (size_t phaseIndex = 0; phaseIndex != _countof(phases); ++phaseIndex)
        {
            CPhaseFailingFileSystem fake(phases[phaseIndex], operationalErrors[errorIndex]);
            // Exercise one ordered durable sequence through the product seam, then
            // restore it before the stack-owned fake can be destroyed. A failing
            // pre-commit phase must never reach replacement or source deletion.
            SetOperationExecutionFileSystemForTests(&fake);
            CPhaseFailingFileSystem::EFailingPhase failedPhase =
                RunTransactionalFaultSequence(OperationExecutionFileSystem(), phases[phaseIndex] == CPhaseFailingFileSystem::fpMove);
            SetOperationExecutionFileSystemForTests(NULL);
            if (failedPhase != phases[phaseIndex] || fake.GetCalls() != expectedCalls[phaseIndex] ||
                GetLastError() != operationalErrors[errorIndex])
                return Fail("the execution filesystem fake did not stop the durable sequence at its selected phase/error pairing");
        }
    }
    return 0;
}
}

int main()
{
    int result = TestCheckedArithmeticBoundaries();
    if (result != 0)
        return result;
    result = TestNativeFileOperationCharacterization();
    return result != 0 ? result : TestExecutionAdapterFaultInjection();
}
