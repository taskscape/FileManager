// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string>

#include "../../src/common/checked_arithmetic.h"
#include "../../src/operation_execution_filesystem.h"
#include "../../src/common/scoped_readonly_file.h"
#include "../../src/common/stable_move_source.h" // exercise the exact source-ownership contract used by moves
#include "../../src/common/conditional_file_publication.h" // execute the production race boundary on real files
#include "../../src/common/operation_recovery.h" // exercise claimed journals and verified recovery on real files
#include "../../src/common/configuration_payload.h" // reject incomplete payloads even when later writes succeed
#include "../../src/common/ftp_transactional_download.h" // share the actual FTP staging and durable completion boundary
#include "../../src/common/file_close_completion.h"
#include <future>
#include <winioctl.h>
#include <sddl.h>

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

// Test double that fails a chosen file-system phase with a given Win32 error.
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

// Exercise actual Windows replacement, including Git-style read-only files and
// a sharing failure, so rollback and handle-based restoration are verified on disk.
int TestReadOnlyReplacement()
{
    wchar_t temporaryPath[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t stage[MAX_PATH];
    if (!GetTempPathW(_countof(temporaryPath), temporaryPath) ||
        !GetTempFileNameW(temporaryPath, L"rot", 0, target))
        return Fail("could not reserve read-only replacement target");
    if (!GetTempFileNameW(temporaryPath, L"ros", 0, stage))
    {
        DeleteFileW(target);
        return Fail("could not reserve read-only replacement stage");
    }

    const char* failure = NULL;
    for (int scenario = 0; scenario < 8 && failure == NULL; ++scenario)
    {
        const DWORD targetAttrs = (scenario & 1) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
        const DWORD stageAttrs = (scenario & 2) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
        const BOOL blockReplacement = (scenario & 4) != 0;
        HANDLE files[2] = {
            CreateFileW(target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL),
            CreateFileW(stage, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)};
        for (int i = 0; i < 2; ++i)
        {
            DWORD written;
            if (files[i] == INVALID_HANDLE_VALUE ||
                !WriteFile(files[i], i == 0 ? "old" : "new", 3, &written, NULL) || written != 3)
                failure = "could not write read-only replacement fixture";
            if (files[i] != INVALID_HANDLE_VALUE)
                CloseHandle(files[i]);
        }
        if (!SetFileAttributesW(target, targetAttrs) || !SetFileAttributesW(stage, stageAttrs))
            failure = "could not set replacement fixture attributes";

        HANDLE blocker = blockReplacement ? CreateFileW(target, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
        if (blockReplacement && blocker == INVALID_HANDLE_VALUE)
            failure = "could not block replacement for rollback test";
        {
            CScopedReadOnlyFile original;
            CScopedReadOnlyFile replacement;
            if (failure == NULL && (!original.MakeWritable(target) || !replacement.MakeWritable(stage)))
                failure = "could not prepare read-only replacement";
            if (failure == NULL)
            {
                const BOOL committed = ReplaceFileW(target, stage, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL);
                if (committed)
                    original.Dismiss();
                if (committed == blockReplacement)
                    failure = "replacement did not match the expected sharing outcome";
                if (!replacement.Restore() || (!committed && !original.Restore()))
                    failure = "read-only restoration failed after replacement";
            }
        }
        if (blocker != INVALID_HANDLE_VALUE)
            CloseHandle(blocker);
        const DWORD expected = blockReplacement ? targetAttrs : stageAttrs;
        if ((GetFileAttributesW(target) & FILE_ATTRIBUTE_READONLY) != (expected & FILE_ATTRIBUTE_READONLY))
            failure = "replacement did not preserve destination read-only state";
        if (blockReplacement &&
            (GetFileAttributesW(stage) & FILE_ATTRIBUTE_READONLY) != (stageAttrs & FILE_ATTRIBUTE_READONLY))
            failure = "failed replacement changed stage read-only state";
        HANDLE check = CreateFileW(target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        char actual[3];
        DWORD read = 0;
        if (check == INVALID_HANDLE_VALUE || !ReadFile(check, actual, sizeof(actual), &read, NULL) ||
            read != 3 || memcmp(actual, blockReplacement ? "old" : "new", 3) != 0)
            failure = "replacement did not preserve the expected file content";
        if (check != INVALID_HANDLE_VALUE)
            CloseHandle(check);
        SetFileAttributesW(target, FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(stage, FILE_ATTRIBUTE_NORMAL);
    }
    DeleteFileW(target);
    DeleteFileW(stage);
    return failure != NULL ? Fail(failure) : 0;
}

// Only disposition is injected; successful calls use real handles in owned fixtures.
class CMoveSourceFileSystem : public CPhaseFailingFileSystem
{
public:
    CMoveSourceFileSystem() : CPhaseFailingFileSystem(fpSuccess, ERROR_SUCCESS), FailDeletion(FALSE) {}
    BOOL FailDeletion;
    BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, void* data, DWORD size) override
    {
        if (FailDeletion && kind == FileDispositionInfo)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        return ::SetFileInformationByHandle(file, kind, data, size);
    }
};

int TestStableMoveSource()
{
    // User temp roots can exceed MAX_PATH; fixtures use the full Unicode API capacity.
    std::vector<WCHAR> temporaryPath(32768), sourceStorage(32768);
    WCHAR* source = sourceStorage.data();
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"sms", 0, source))
        return Fail("could not reserve stable move fixture");
    const std::wstring renamed = std::wstring(source) + L".renamed";
    const char* failure = NULL;
    CMoveSourceFileSystem fileSystem;
    for (int scenario = 0; scenario < 3 && failure == NULL; ++scenario)
    {
        HANDLE writer = CreateFileW(source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written;
        if (writer == INVALID_HANDLE_VALUE || !WriteFile(writer, "original", 8, &written, NULL) || written != 8)
            failure = "could not populate stable move fixture";
        {
            CStableMoveSource blocked;
            if (failure == NULL && blocked.Open(source))
                failure = "a move accepted an already active writer";
        }
        if (writer != INVALID_HANDLE_VALUE)
            CloseHandle(writer);
        if (scenario != 0)
            SetFileAttributesW(source, FILE_ATTRIBUTE_READONLY);
        {
            CStableMoveSource lease;
            if (failure == NULL && !lease.Open(source))
                failure = "could not acquire stable move source";
            for (int phase = 0; phase < 2 && failure == NULL; ++phase)
            {
                // Simulate reader retry and the post-copy/pre-delete gap. Closing
                // readers must never release the move's independent source lease.
                HANDLE reader = lease.OpenReader(FILE_FLAG_SEQUENTIAL_SCAN);
                char contents[8];
                DWORD read = 0;
                if (reader == INVALID_HANDLE_VALUE || !ReadFile(reader, contents, 8, &read, NULL) ||
                    read != 8 || memcmp(contents, "original", 8) != 0)
                    failure = "reopened move reader did not retain original contents";
                if (reader != INVALID_HANDLE_VALUE)
                    CloseHandle(reader);
                writer = CreateFileW(source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, 0, NULL);
                if (writer != INVALID_HANDLE_VALUE)
                {
                    failure = "writer entered the gap after a move reader closed";
                    CloseHandle(writer);
                }
                if (MoveFileW(source, renamed.c_str()))
                    failure = "source was renamed while the move still owned it";
            }
            if (failure == NULL && scenario == 2)
            {
                fileSystem.FailDeletion = TRUE;
                if (lease.Delete(fileSystem) || !(GetFileAttributesW(source) & FILE_ATTRIBUTE_READONLY))
                    failure = "failed deletion did not retain the read-only source";
                fileSystem.FailDeletion = FALSE;
            }
            if (failure == NULL && !lease.Delete(fileSystem))
                failure = "stable move source could not be deleted through its held handle";
        }
        if (failure == NULL && GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES)
            failure = "successful move disposition left the source visible";
        SetFileAttributesW(source, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(source);
    }
    SetFileAttributesW(source, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(renamed.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(source);
    DeleteFileW(renamed.c_str());
    return failure != NULL ? Fail(failure) : 0;
}

bool WritePublicationFixture(const std::wstring& path, const char* contents)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    const DWORD size = (DWORD)strlen(contents);
    const bool ok = WriteFile(file, contents, size, &written, NULL) && written == size;
    return CloseHandle(file) && ok;
}

std::string ReadPublicationFixture(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return "";
    char contents[32] = {};
    DWORD read = 0;
    const bool ok = ReadFile(file, contents, sizeof(contents), &read, NULL) != FALSE;
    CloseHandle(file);
    return ok ? std::string(contents, read) : "";
}

// Inject at the actual rename/flush boundary, leaving all successful operations
// to Windows so the test observes preserved bytes rather than an emulated plan.
class CPublicationFileSystem : public CMoveSourceFileSystem
{
public:
    std::wstring TargetPath;
    int Renames = 0;
    int InsertOccupantAt = 0;
    int FailRenameAt = 0;
    BOOL FailFlush = FALSE;
    BOOL RenameFileByHandle(HANDLE file, HANDLE directory, const WCHAR* name) override
    {
        ++Renames;
        if (Renames == InsertOccupantAt && !WritePublicationFixture(TargetPath, "newer")) return FALSE;
        if (Renames == FailRenameAt) { SetLastError(ERROR_DISK_FULL); return FALSE; }
        return COperationExecutionFileSystem::RenameFileByHandle(file, directory, name);
    }
    BOOL FlushFileBuffers(HANDLE file) override
    {
        if (FailFlush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return ::FlushFileBuffers(file);
    }
};

struct CPublicationRecordFixture
{
    const char* FailState = NULL;
    std::string States;
    static BOOL Append(void* context, const char* state, const WCHAR*)
    {
        CPublicationRecordFixture& fixture = *(CPublicationRecordFixture*)context;
        fixture.States += std::string(state) + "\n";
        return fixture.FailState == NULL || strcmp(fixture.FailState, state) != 0;
    }
};

BOOL SetPublicationFixtureDacl(const std::wstring& path, const WCHAR* sddl)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL)) return FALSE;
    PACL dacl = NULL;
    BOOL present, defaulted;
    BOOL ok = GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted);
    if (ok) ok = SetNamedSecurityInfoW(const_cast<WCHAR*>(path.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      NULL, NULL, dacl, NULL) == ERROR_SUCCESS;
    LocalFree(descriptor);
    return ok;
}

BOOL PublicationFixtureDaclMatches(const std::wstring& path, const WCHAR* sddl)
{
    PSECURITY_DESCRIPTOR expected = NULL, actual = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &expected, NULL)) return FALSE;
    PACL wanted = NULL, found = NULL;
    BOOL present, defaulted;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const BOOL ok = GetSecurityDescriptorDacl(expected, &present, &wanted, &defaulted) &&
                    GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          NULL, NULL, &found, NULL, &actual) == ERROR_SUCCESS &&
                    GetSecurityDescriptorControl(actual, &control, &revision) && (control & SE_DACL_PROTECTED) &&
                    wanted != NULL && found != NULL && wanted->AclSize == found->AclSize &&
                    memcmp(wanted, found, wanted->AclSize) == 0;
    LocalFree(expected);
    if (actual != NULL) LocalFree(actual);
    return ok;
}

int TestConditionalPublication()
{
    std::vector<WCHAR> temporaryPath(32768), directory(32768);
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"pub", 0, directory.data()) ||
        !DeleteFileW(directory.data()) || !CreateDirectoryW(directory.data(), NULL))
        return Fail("could not reserve conditional publication directory");
    const std::wstring target = std::wstring(directory.data()) + L"\\target";
    const std::wstring stage = std::wstring(directory.data()) + L"\\SALCP-stage";
    const std::wstring backup = stage + L".previous";
    const std::wstring swapped = target + L".swapped";
    const char* failure = NULL;
    for (int scenario = 0; scenario < 14 && failure == NULL; ++scenario)
    {
        const bool existing = scenario != 2 && scenario != 11;
        const bool readOnly = scenario == 4 || scenario == 7;
        if (!WritePublicationFixture(stage, "staged") || (existing && !WritePublicationFixture(target, "old")))
            failure = "could not populate conditional publication fixtures";
        if (readOnly && (!SetFileAttributesW(stage.c_str(), FILE_ATTRIBUTE_READONLY) ||
                          !SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY)))
            failure = "could not set publication read-only attributes";
        if (scenario == 10 && !WritePublicationFixture(backup, "unrelated"))
            failure = "could not create occupied backup fixture";
        const WCHAR* targetDacl = L"D:P(A;;FA;;;OW)";
        const WCHAR* sourceDacl = L"D:P(A;;FA;;;OW)(A;;FR;;;WD)";
        if (scenario >= 12 && (!SetPublicationFixtureDacl(target, targetDacl) || !SetPublicationFixtureDacl(stage, sourceDacl)))
            failure = "could not create protected publication ACL fixtures";
        CPublicationFileSystem fileSystem;
        fileSystem.TargetPath = target;
        fileSystem.InsertOccupantAt = scenario == 1 ? 2 : scenario == 2 ? 1 : 0;
        fileSystem.FailRenameAt = scenario == 5 ? 2 : 0;
        fileSystem.FailFlush = scenario == 6;
        fileSystem.FailDeletion = scenario == 7;
        CPublicationRecordFixture records;
        records.FailState = scenario == 3 ? "publication-planned" :
                            scenario == 8 ? "destination-backed-up" :
                            scenario == 9 ? "destination-published" : NULL;
        CPublicationOutcome outcome;
        {
            CConditionalFilePublication publication;
            if (failure == NULL && !publication.Open(target.c_str(), stage.c_str(), existing, scenario != 13))
                failure = "could not acquire publication files";
            if (failure == NULL && existing)
            {
                // This is the former validation/replacement gap: neither a
                // same-length edit nor a pathname swap may change the approved file.
                HANDLE writer = CreateFileW(target.c_str(), GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                              NULL, OPEN_EXISTING, 0, NULL);
                if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); failure = "destination write entered publication gap"; }
                if (MoveFileW(target.c_str(), swapped.c_str())) failure = "destination rename entered publication gap";
            }
            if (failure == NULL) outcome = publication.Commit(fileSystem, CPublicationRecordFixture::Append, &records);
        }
        const std::string actual = ReadPublicationFixture(target);
        const std::string staged = ReadPublicationFixture(stage);
        const std::string previous = ReadPublicationFixture(backup);
        if (failure == NULL)
        {
            if (scenario == 0 || scenario == 4 || scenario >= 11)
            {
                if (!outcome.Committed || outcome.Error != ERROR_SUCCESS || outcome.BackupRetained ||
                    actual != "staged" || !staged.empty() || !previous.empty())
                    failure = "successful publication did not preserve staged data";
            }
            else if (scenario == 1 || scenario == 2)
            {
                if (outcome.Committed || outcome.Error == ERROR_SUCCESS || actual != "newer" || staged != "staged" ||
                    previous != (existing ? "old" : ""))
                    failure = "conflicting publication destroyed an unexpected occupant or its backup";
            }
            else if (scenario == 3 || scenario == 5 || scenario == 8 || scenario == 10)
            {
                if (outcome.Committed || outcome.Error == ERROR_SUCCESS || actual != "old" || staged != "staged" ||
                    previous != (scenario == 10 ? "unrelated" : ""))
                    failure = "failed publication did not retain or restore the original destination";
            }
            else if (!outcome.Committed || !outcome.BackupRetained || actual != "staged" || previous != "old" ||
                     (scenario == 7 ? outcome.CleanupError == ERROR_SUCCESS : outcome.Error == ERROR_SUCCESS))
                failure = "post-publication failure lost its recoverable previous version";
            if (readOnly && (!(GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY) ||
                              (scenario == 7 && !(GetFileAttributesW(backup.c_str()) & FILE_ATTRIBUTE_READONLY))))
                failure = "publication or failed backup deletion changed read-only attributes";
            // Preserve a restricted old destination unless source-security copy
            // was explicitly selected; either choice must retain ACL protection.
            if (scenario >= 12 && !PublicationFixtureDaclMatches(target, scenario == 12 ? targetDacl : sourceDacl))
                failure = "publication changed the selected protected ACL policy";
        }
        if (failure != NULL) fprintf(stderr, "Publication scenario %d: error=%lu cleanup=%lu\n", scenario, outcome.Error, outcome.CleanupError);
        const std::wstring paths[] = {target, stage, backup, swapped};
        for (const std::wstring& path : paths) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
    }
    RemoveDirectoryW(directory.data());
    return failure == NULL ? 0 : Fail(failure);
}

BOOL CreatePublicationJunction(const std::wstring& link, const std::wstring& target)
{
    // Junctions need no symbolic-link privilege and keep this regression runnable
    // under the same non-administrator account as the self-hosted pipeline.
    struct CMountPointData
    {
        DWORD Tag;
        WORD DataLength, Reserved, SubstituteOffset, SubstituteLength, PrintOffset, PrintLength;
        WCHAR Names[1];
    };
    const std::wstring substitute = L"\\??\\" + target;
    const DWORD bytes = (DWORD)(offsetof(CMountPointData, Names) +
                                (substitute.size() + 1 + target.size() + 1) * sizeof(WCHAR));
    std::vector<BYTE> buffer(bytes, 0);
    CMountPointData* data = (CMountPointData*)buffer.data();
    data->Tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->DataLength = (WORD)(bytes - 8);
    data->SubstituteLength = (WORD)(substitute.size() * sizeof(WCHAR));
    data->PrintOffset = data->SubstituteLength + sizeof(WCHAR);
    data->PrintLength = (WORD)(target.size() * sizeof(WCHAR));
    memcpy(data->Names, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
    memcpy((BYTE*)data->Names + data->PrintOffset, target.c_str(), (target.size() + 1) * sizeof(WCHAR));
    if (!CreateDirectoryW(link.c_str(), NULL)) return FALSE;
    HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (directory == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return FALSE; }
    DWORD returned;
    const BOOL ok = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, data, bytes, NULL, 0, &returned, NULL);
    CloseHandle(directory);
    if (!ok) RemoveDirectoryW(link.c_str());
    return ok;
}

int TestPublicationDirectoryRedirection()
{
    std::vector<WCHAR> temporaryPath(32768), rootBuffer(32768);
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"pdr", 0, rootBuffer.data()) ||
        !DeleteFileW(rootBuffer.data()) || !CreateDirectoryW(rootBuffer.data(), NULL))
        return Fail("could not reserve publication redirection root");
    const std::wstring root = rootBuffer.data(), first = root + L"\\first", second = root + L"\\second", link = root + L"\\link";
    const std::wstring target = link + L"\\target", stage = link + L"\\SALCP-stage";
    const char* failure = NULL;
    if (!CreateDirectoryW(first.c_str(), NULL) || !CreateDirectoryW(second.c_str(), NULL) ||
        !WritePublicationFixture(first + L"\\target", "old") || !WritePublicationFixture(first + L"\\SALCP-stage", "staged") ||
        !WritePublicationFixture(second + L"\\target", "newer") || !WritePublicationFixture(second + L"\\SALCP-stage", "unrelated") ||
        !CreatePublicationJunction(link, first))
        failure = "could not create publication junction fixtures";
    {
        CConditionalFilePublication publication;
        CPublicationFileSystem fileSystem;
        CPublicationRecordFixture records;
        if (failure == NULL && !publication.Open(target.c_str(), stage.c_str(), TRUE))
            failure = "could not open publication through junction";
        // Retarget after acquisition, at the same time a pathname-based commit
        // would previously resolve a different directory and overwrite its file.
        if (failure == NULL && (!RemoveDirectoryW(link.c_str()) || !CreatePublicationJunction(link, second)))
            failure = "could not retarget publication junction";
        if (failure == NULL)
        {
            CPublicationOutcome outcome = publication.Commit(fileSystem, CPublicationRecordFixture::Append, &records);
            if (!outcome.Committed || outcome.Error != ERROR_SUCCESS)
                failure = "publication did not retain its opened directory after junction retarget";
        }
    }
    if (failure == NULL && (ReadPublicationFixture(first + L"\\target") != "staged" ||
                            ReadPublicationFixture(second + L"\\target") != "newer" ||
                            ReadPublicationFixture(second + L"\\SALCP-stage") != "unrelated"))
        failure = "retargeted junction redirected publication into another directory";
    // Remove only owned exact paths, unlinking the junction before its targets.
    RemoveDirectoryW(link.c_str());
    const std::wstring directories[] = {first, second};
    for (const std::wstring& directory : directories)
    {
        DeleteFileW((directory + L"\\target").c_str());
        DeleteFileW((directory + L"\\SALCP-stage").c_str());
        DeleteFileW((directory + L"\\SALCP-stage.previous").c_str());
        RemoveDirectoryW(directory.c_str());
    }
    RemoveDirectoryW(root.c_str());
    return failure == NULL ? 0 : Fail(failure);
}

#include "OperationRecoveryTests.h" // shared fixture helpers above keep recovery tests within their owned directories
#include "FtpDownloadTests.h" // exercise private staging, restart evidence and persistent multi-waiter completion
#include "ConfigurationPayloadTests.h" // require intended registry entries, fields and one save-wide error result

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
    // Run the real-filesystem regression before deterministic fault injection.
    if (result == 0)
        result = TestReadOnlyReplacement();
    if (result == 0)
        result = TestStableMoveSource(); // no source writes may escape through retry or commit gaps
    if (result == 0)
        result = TestConditionalPublication(); // an unexpected occupant must survive every publication race
    if (result == 0)
        result = TestPublicationDirectoryRedirection(); // renamed ancestors cannot redirect retained directory operations
    if (result == 0)
        result = TestOperationRecovery(); // changed or unresolved objects must survive another startup
    if (result == 0)
        result = TestFtpDownloadReliability(); // a failed local outcome must preserve the original files
    if (result == 0)
        result = TestConfigurationPayloadReliability(); // incomplete optional collections cannot become accepted snapshots
    return result != 0 ? result : TestExecutionAdapterFaultInjection();
}
