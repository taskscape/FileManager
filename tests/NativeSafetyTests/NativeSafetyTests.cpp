// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string>
#include "../../src/common/resource_strings_utf8.h" // exercise the production resource loader with real multilingual resources
#include "../../src/common/network_resources_utf8.h" // verify the exact provider/cache conversion used by net: enumeration
#include "../../src/common/unicode_shell_link.h" // exercise production Unicode layout and COM/system text boundaries

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

int TestResourceStringsUtf8()
{
    const HINSTANCE instance = GetModuleHandleW(NULL);
    // Resource boundaries cover toolbar hints and pending-network labels as well as menu captions.
    const char* expected[] = {u8"&Utw\u00f3rz katalog...\tF7", u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144", "&Create Directory...\tF7",
                              u8"Przeszukiwanie sieci\u2026", u8"Odzyskiwanie usuni\u0119tych plik\u00f3w"};
    for (int index = 0; index < static_cast<int>(_countof(expected)); ++index)
    {
        const int bytes = static_cast<int>(strlen(expected[index]));
        if (LoadStringUtf8(instance, 4001 + index, NULL, 0) != bytes)
            return Fail("resource loader measured characters instead of UTF-8 bytes");
        std::string buffer(bytes + 2, '#');
        // A buffer without terminator space must fail without publishing a partial character or overwriting its sentinel.
        if (LoadStringUtf8(instance, 4001 + index, &buffer[0], bytes) != 0 || buffer[0] != 0 || buffer[bytes] != '#')
            return Fail("resource loader accepted or overran an undersized UTF-8 destination");
        if (LoadStringUtf8(instance, 4001 + index, &buffer[0], bytes + 1) != bytes ||
            strcmp(buffer.c_str(), expected[index]) != 0 || buffer[bytes + 1] != '#')
            return Fail("resource loader corrupted a localized label or its terminator boundary");
    }
    char missing[] = "sentinel";
    if (LoadStringUtf8(instance, 4999, missing, sizeof(missing)) != 0 || missing[0] != 0)
        return Fail("missing resources left stale display text in the destination");
    return 0;
}

int TestNetworkResourcesUtf8()
{
    // Names returned by WNetEnumResourceW must remain navigable after caching and converting back for WNetOpenEnumW.
    WCHAR remote[] = L"Ca\u0142a sie\u0107";
    WCHAR comment[] = L"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1";
    WCHAR provider[] = L"Sie\u0107 Microsoft Windows";
    WCHAR local[] = L"";
    NETRESOURCEW source = {RESOURCE_GLOBALNET, RESOURCETYPE_DISK, RESOURCEDISPLAYTYPE_NETWORK,
                          RESOURCEUSAGE_CONTAINER, local, remote, comment, provider};
    CNetworkResourceUtf8 utf8(source);
    if (utf8.Error != NO_ERROR || strcmp(utf8.Resource.lpRemoteName, u8"Ca\u0142a sie\u0107") != 0 ||
        strcmp(utf8.Resource.lpComment, u8"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1") != 0 ||
        strcmp(utf8.Resource.lpProvider, u8"Sie\u0107 Microsoft Windows") != 0 ||
        utf8.Resource.lpLocalName == NULL || utf8.Resource.lpLocalName[0] != 0)
        return Fail("network provider text was converted through ANSI or lost empty fields");
    remote[0] = L'X';
    CNetworkResourceWide wide(utf8.Resource);
    if (wide.Error != NO_ERROR || wcscmp(wide.Resource.lpRemoteName, L"Ca\u0142a sie\u0107") != 0 ||
        wcscmp(wide.Resource.lpComment, comment) != 0 || wcscmp(wide.Resource.lpProvider, provider) != 0 ||
        wide.Resource.dwScope != source.dwScope || wide.Resource.dwType != source.dwType ||
        wide.Resource.dwDisplayType != source.dwDisplayType || wide.Resource.dwUsage != source.dwUsage)
        return Fail("cached network text lost ownership, flags or its Unicode navigation identity");
    NETRESOURCEW empty = {};
    CNetworkResourceUtf8 nullFields(empty);
    if (nullFields.Error != NO_ERROR || nullFields.Resource.lpRemoteName != NULL ||
        nullFields.Resource.lpLocalName != NULL || nullFields.Resource.lpComment != NULL || nullFields.Resource.lpProvider != NULL)
        return Fail("network conversion changed absent fields into empty strings");
    char invalidUtf8[] = "\xc5";
    NETRESOURCEA invalid = {};
    invalid.lpRemoteName = invalidUtf8;
    CNetworkResourceWide rejected(invalid);
    WCHAR invalidUtf16[] = {0xd800, 0};
    empty.lpRemoteName = invalidUtf16;
    CNetworkResourceUtf8 rejectedWide(empty);
    if (rejected.Error != ERROR_NO_UNICODE_TRANSLATION || rejectedWide.Error != ERROR_NO_UNICODE_TRANSLATION)
        return Fail("network conversion accepted malformed text and changed its identity");
    return 0;
}

int TestUnicodeTextBoundaries()
{
    // Sweep real GDI widths: Polish, CJK and supplementary characters must stay valid through both ellipsis modes.
    HDC dc = CreateCompatibleDC(NULL);
    if (dc == NULL)
        return Fail("could not create the Unicode layout DC");
    const std::wstring path = L"C:\\Za\u017c\u00f3\u0142\u0107\\\u6771\u4eac\\\U0001f4c1plik.txt";
    SIZE originalSize, dots;
    GetTextExtentPoint32W(dc, path.c_str(), (int)path.size(), &originalSize);
    GetTextExtentPoint32W(dc, L"...", 3, &dots);
    bool valid = true;
    for (int width = 0; width <= originalSize.cx + 10; ++width)
    {
        for (int mode = 0; mode != 2; ++mode)
        {
            std::wstring clipped, decoded;
            std::string utf8;
            SIZE measured;
            if (!EllipsizeUnicodeText(dc, path, width, mode != 0, L'\\', clipped, measured) ||
                !WideTextToUtf8(clipped.c_str(), (int)clipped.size(), utf8) || !Utf8TextToWide(utf8.c_str(), -1, decoded) ||
                decoded != clipped || (width >= dots.cx && measured.cx > width) ||
                (width >= originalSize.cx && clipped != path))
                valid = false;
        }
    }
    // The full filename survives path shortening when it and the ellipsis fit.
    const std::wstring leaf = path.substr(path.rfind(L'\\'));
    SIZE leafSize, measured;
    GetTextExtentPoint32W(dc, leaf.c_str(), (int)leaf.size(), &leafSize);
    std::wstring clipped;
    if (!EllipsizeUnicodeText(dc, path, leafSize.cx + dots.cx, true, L'\\', clipped, measured) ||
        clipped.size() < leaf.size() || clipped.substr(clipped.size() - leaf.size()) != leaf)
        valid = false;
    const std::string prefix = u8"Sie\u0107: ";
    const std::string body = u8"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1 plik";
    const std::string suffix = u8" - b\u0142\u0105d";
    const std::string source = prefix + body + suffix;
    for (int width = 0; width != 220; ++width)
    {
        for (int mode = 0; mode != 2; ++mode)
        {
            std::string truncated;
            std::wstring wide;
            if (!TruncateUtf8Substring(dc, source.c_str(), (int)prefix.size(), (int)body.size(), width, mode != 0, truncated) ||
                !Utf8TextToWide(truncated.c_str(), -1, wide) || truncated.compare(0, prefix.size(), prefix) != 0 ||
                truncated.size() < prefix.size() + suffix.size() ||
                truncated.substr(truncated.size() - suffix.size()) != suffix)
                valid = false;
        }
    }
    std::string zeroStart, emptyBody, longResult;
    std::string longText(12000, 'x');
    std::wstring rejected;
    if (!TruncateUtf8Substring(dc, body.c_str(), 0, (int)body.size(), 0, false, zeroStart) || zeroStart != "..." ||
        !TruncateUtf8Substring(dc, body.c_str(), 0, 0, 0, false, emptyBody) || emptyBody != body ||
        !TruncateUtf8Substring(dc, longText.c_str(), 0, (int)longText.size(), 120, false, longResult) || longText.size() != 12000 ||
        TruncateUtf8Substring(dc, body.c_str(), 3, (int)body.size() - 3, 120, false, longResult) ||
        Utf8TextToWide("\xc5", -1, rejected))
        valid = false;
    DeleteDC(dc);
    if (!valid)
        return Fail("Unicode ellipsis split a character, changed fixed text, or exceeded its measured width");

    std::string formatted;
    if (!FormatUtf8Pair(u8"Sie\u0107 %2!s! / %1!s!", u8"\u017b:\\", u8"\u6771\u4eac\U0001f4c1", formatted) ||
        formatted != u8"Sie\u0107 \u6771\u4eac\U0001f4c1 / \u017b:\\")
        return Fail("RDP indexed formatting lost Unicode or locale-specific insertion order");
    wchar_t* system = NULL;
    DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, ERROR_ACCESS_DENIED, 0, (wchar_t*)&system, 0, NULL);
    std::string error;
    std::wstring errorWide;
    bool converted = Win32ErrorTextUtf8(ERROR_ACCESS_DENIED, 0, error) && Utf8TextToWide(error.c_str(), -1, errorWide);
    std::wstring expected = count != 0 ? std::wstring(system, count) : L"";
    LocalFree(system);
    while (!expected.empty() && (expected.back() == L'\r' || expected.back() == L'\n'))
        expected.pop_back();
    if (count == 0 || !converted || errorWide != expected || Win32ErrorTextUtf8(0xdeadbeef, 0, error) || !error.empty())
        return Fail("system-error text used ACP, retained CRLF, or published stale text for an unknown code");
    return 0;
}

int TestUnicodeShellLinkTarget()
{
    // Use a real COM shortcut target; no network connection or target-file creation is needed for this boundary check.
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return Fail("could not initialize the shortcut COM test");
    IShellLinkW* link = NULL;
    HRESULT created = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link);
    const wchar_t* path = L"C:\\Za\u017c\u00f3\u0142\u0107\\\u6771\u4eac\\\U0001f4c1.txt";
    char buffer[1024] = {};
    char shortBuffer[] = "sentinel";
    bool valid = SUCCEEDED(created) && SUCCEEDED(link->SetPath(path)) && ShellLinkTargetUtf8(link, buffer, sizeof(buffer));
    std::wstring decoded;
    valid = valid && Utf8TextToWide(buffer, -1, decoded) && decoded == path &&
            !ShellLinkTargetUtf8(link, shortBuffer, sizeof(shortBuffer)) && strcmp(shortBuffer, "sentinel") == 0;
    if (link != NULL)
        link->Release();
    CoUninitialize();
    return valid ? 0 : Fail("shortcut target lost Unicode or partially overwrote an undersized destination");
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
    if (result == 0)
        result = TestResourceStringsUtf8(); // Polish bytes must reach the UTF-8 menu renderer unchanged
    if (result == 0)
        result = TestNetworkResourcesUtf8(); // provider names must survive both display and subsequent navigation
    if (result == 0)
        result = TestUnicodeTextBoundaries(); // layout must preserve character boundaries and UTF-8 system/RDP messages
    if (result == 0)
        result = TestUnicodeShellLinkTarget(); // COM targets must return complete UTF-8 paths
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
