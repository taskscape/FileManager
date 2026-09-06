// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Included in the native fixture namespace. Faults change one real filesystem
// boundary, so success requires preserved bytes and production sidecar evidence.
class CFtpFaultFileSystem : public CFtpDownloadFileSystem
{
public:
    HANDLE Stage = INVALID_HANDLE_VALUE;
    BOOL FailFlush = FALSE, FailClose = FALSE, FailMetadata = FALSE, FailJournalFlush = FALSE, FailDelete = FALSE;
    int FailRenameAt = 0, Renames = 0;
    DWORD MaximumWrite = MAXDWORD;
    BOOL FlushFileBuffers(HANDLE file) override
    {
        if ((FailFlush && file == Stage) || (FailJournalFlush && file != Stage))
        { SetLastError(ERROR_DISK_FULL); return FALSE; }
        return CFtpDownloadFileSystem::FlushFileBuffers(file);
    }
    BOOL CloseFile(HANDLE file) override
    {
        const BOOL closed = CloseHandle(file);
        if (FailClose) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return closed;
    }
    BOOL RenameFileByHandle(HANDLE file, HANDLE directory, const WCHAR* name) override
    {
        if (++Renames == FailRenameAt) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        return CFtpDownloadFileSystem::RenameFileByHandle(file, directory, name);
    }
    BOOL SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access, const FILETIME* write) override
    {
        if (FailMetadata) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        return CFtpDownloadFileSystem::SetFileTime(file, creation, access, write);
    }
    BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS type, void* data, DWORD count) override
    {
        if (FailDelete && type == FileDispositionInfo) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        return CFtpDownloadFileSystem::SetFileInformationByHandle(file, type, data, count);
    }
    BOOL WriteFile(HANDLE file, const void* data, DWORD count, DWORD* written, LPOVERLAPPED overlap) override
    { return ::WriteFile(file, data, count < MaximumWrite ? count : MaximumWrite, written, overlap); }
};

struct CFtpDownloadFixture
{
    CRecoveryFixture Directory;
    std::vector<std::wstring> Owned;
    void Remember(const std::shared_ptr<CFtpTransactionalDownload>& download)
    {
        if (!download) return;
        Owned.push_back(download->StagePath()); Owned.push_back(download->MetadataPath());
        Owned.push_back(download->StagePath() + L".previous");
    }
    ~CFtpDownloadFixture()
    {
        // Delete exact files created by this fixture, after all shared owners close.
        for (const auto& path : Owned)
        { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
    }
};

int TestFtpStagedFiles()
{
    const char* names[] = {"overwrite", "resume checkpoint", "changed destination", "crash before checkpoint",
        "flush failure", "close failure", "backup rename failure", "timestamp failure", "resume clones original",
        "length mismatch", "new occupant of absent target", "changed staged bytes", "torn metadata",
        "different remote version", "exclusive lease", "short journal writes", "journal flush failure",
        "backup cleanup failure", "publication rename failure", "new empty file", "read-only destination"};
    for (int scenario = 0; scenario < (int)ARRAYSIZE(names); ++scenario)
    {
        CFtpDownloadFixture fixture;
        const std::wstring& target = fixture.Directory.Target;
        const BOOL absent = scenario == 10 || scenario == 19;
        if (fixture.Directory.Root.empty() || (!absent && !WriteRecoveryFixture(target, "original")))
            return Fail("could not prepare FTP fixture");
        if (scenario == 20 && !SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY))
            return Fail("could not set read-only FTP fixture");
        CFtpFaultFileSystem fileSystem;
        if (scenario == 15) fileSystem.MaximumWrite = 7;
        auto download = std::make_shared<CFtpTransactionalDownload>();
        if (!download->Create(target.c_str(), "anonymous|loopback|version1|binary", !absent, scenario == 8, fileSystem))
            return Fail("could not create a private FTP stage");
        fixture.Remember(download);
        fileSystem.Stage = download->File();
        const std::wstring stage = download->StagePath(), metadata = download->MetadataPath();
        const auto require = [&](BOOL passed, const char* reason) {
            if (!passed) fprintf(stderr, "FTP native case '%s': %s (Win32 %lu)\n", names[scenario], reason, GetLastError());
            return passed;
        };
        if (!require(absent ? GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES :
                             ReadPublicationFixture(target) == "original", "destination changed before data")) return 1;
        if (scenario != 8 && scenario != 19)
        {
            DWORD written = 0;
            if (!WriteFile(download->File(), "new data", 8, &written, NULL) || written != 8)
                return Fail("could not write FTP staged data");
        }
        if (!require(absent || ReadPublicationFixture(target) == "original", "destination changed during transfer")) return 1;
        if (scenario == 1 || scenario == 2 || scenario == 3 || (scenario >= 11 && scenario <= 14))
        {
            if (scenario != 3 && !download->Checkpoint(fileSystem)) return Fail("could not checkpoint FTP fixture");
            if (scenario == 14)
            {
                auto competing = CFtpTransactionalDownload::TryResume(target.c_str(), "anonymous|loopback|version1|binary", fileSystem);
                if (!require(!competing, "another process could claim a live stage")) return 1;
            }
            download.reset();
            if (scenario == 2 && !WriteRecoveryFixture(target, "modified")) return Fail("could not replace FTP destination");
            if (scenario == 11 && !WriteRecoveryFixture(stage, "tampered")) return Fail("could not mutate FTP stage");
            if (scenario == 12)
            {
                HANDLE file = CreateFileW(metadata.c_str(), FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
                DWORD written = 0;
                const BOOL ok = file != INVALID_HANDLE_VALUE && WriteFile(file, "WRIT", 4, &written, NULL) && written == 4;
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
                if (!ok) return Fail("could not tear FTP metadata record");
            }
            download = CFtpTransactionalDownload::TryResume(target.c_str(),
                scenario == 13 ? "anonymous|loopback|version2|binary" : "anonymous|loopback|version1|binary", fileSystem);
            if (scenario != 1 && scenario != 14)
            {
                if (!require(!download, "unverified restart was accepted")) return 1;
                if (!require(ReadPublicationFixture(target) == (scenario == 2 ? "modified" : "original"),
                              "rejected restart changed destination")) return 1;
                if (!require(ReadPublicationFixture(stage) == (scenario == 11 ? "tampered" : "new data"),
                              "rejected restart changed staged bytes")) return 1;
                continue;
            }
            if (!require(download != nullptr, "verified checkpoint could not resume")) return 1;
            fileSystem.Stage = download->File();
        }
        if (scenario == 10 && !WriteRecoveryFixture(target, "newer")) return Fail("could not insert unexpected FTP destination");
        fileSystem.FailFlush = scenario == 4;
        fileSystem.FailClose = scenario == 5;
        fileSystem.FailRenameAt = scenario == 6 ? 1 : scenario == 18 ? 2 : 0;
        fileSystem.FailMetadata = scenario == 7;
        fileSystem.FailJournalFlush = scenario == 16;
        fileSystem.FailDelete = scenario == 17;
        FILETIME time; GetSystemTimeAsFileTime(&time);
        const BOOL finished = download->Finish(fileSystem, scenario == 9 ? 9 : scenario == 19 ? 0 : 8, TRUE, &time);
        const BOOL expectedSuccess = scenario == 0 || scenario == 1 || scenario == 8 || scenario == 14 ||
                                     scenario == 15 || scenario == 19 || scenario == 20;
        if (!require(finished == expectedSuccess, "wrong durable completion result")) return 1;
        download.reset();
        if (expectedSuccess)
        {
            const char* expected = scenario == 8 ? "original" : scenario == 19 ? "" : "new data";
            if (!require(ReadPublicationFixture(target) == expected &&
                         GetFileAttributesW(stage.c_str()) == INVALID_FILE_ATTRIBUTES &&
                         GetFileAttributesW(metadata.c_str()) == INVALID_FILE_ATTRIBUTES,
                         "successful publication did not produce final contents and retire owned metadata")) return 1;
        }
        else if (scenario != 5 && scenario != 17)
        {
            if (!require(ReadPublicationFixture(target) == (scenario == 10 ? "newer" : "original"),
                         "failed publication damaged original destination")) return 1;
        }
        if (!expectedSuccess && !require(GetFileAttributesW(metadata.c_str()) != INVALID_FILE_ATTRIBUTES,
                                          "failed completion lost recovery evidence")) return 1;
        if (scenario == 17 && !require(ReadPublicationFixture(stage + L".previous") == "original",
                                       "failed backup deletion lost old contents")) return 1;
    }
    return 0;
}

int TestFtpIdentityRestart()
{
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        CFtpDownloadFixture fixture;
        const auto& target = fixture.Directory.Target;
        if (!WriteRecoveryFixture(target, "original")) return Fail("could not seed FTP identity fixture");
        CFtpFaultFileSystem fileSystem;
        auto download = std::make_shared<CFtpTransactionalDownload>();
        if (!download->Create(target.c_str(), "ascii-version", TRUE, FALSE, fileSystem))
            return Fail("could not create FTP identity fixture");
        fixture.Remember(download);
        fileSystem.Stage = download->File();
        const auto stage = download->StagePath();
        DWORD written = 0;
        if (!WriteFile(download->File(), "old prefix", 10, &written, NULL) || written != 10 ||
            !download->Checkpoint(fileSystem)) return Fail("could not write FTP old identity checkpoint");
        // A rejected journal write must not truncate the old private prefix.
        fileSystem.FailJournalFlush = scenario == 2;
        const BOOL restarted = download->RestartIdentity("binary-version", fileSystem);
        if (restarted != (scenario != 2) || ReadPublicationFixture(target) != "original")
            return Fail("FTP identity restart damaged the approved destination");
        LARGE_INTEGER length;
        if (!GetFileSizeEx(download->File(), &length) || length.QuadPart != (scenario == 2 ? 10 : 0))
            return Fail("FTP identity restart truncated before durable revocation or retained old prefix");
        if (scenario == 2) continue;
        if (!WriteFile(download->File(), "new binary", 10, &written, NULL) || written != 10)
            return Fail("could not write FTP new identity prefix");
        if (scenario == 0 && !download->Checkpoint(fileSystem)) return Fail("could not checkpoint new FTP identity");
        download.reset();
        if (CFtpTransactionalDownload::TryResume(target.c_str(), "ascii-version", fileSystem))
            return Fail("obsolete FTP identity resumed a new prefix");
        download = CFtpTransactionalDownload::TryResume(target.c_str(), "binary-version", fileSystem);
        if ((download != nullptr) != (scenario == 0) || ReadPublicationFixture(stage) != "new binary")
            return Fail("FTP restart ignored the new identity checkpoint boundary");
    }
    return 0;
}

int TestFtpCloseCompletion()
{
    DWORD error;
    CFileCloseCompletion ready;
    ready.Complete(ERROR_SUCCESS);
    ready.Complete(ERROR_WRITE_FAULT);
    if (!ready.Wait(0, error) || error != ERROR_SUCCESS) return Fail("early FTP completion was lost or overwritten");
    CFileCloseCompletion timeout;
    if (timeout.Wait(10, error) || error != ERROR_TIMEOUT) return Fail("unfinished FTP close did not time out");
    for (int round = 0; round < 20; ++round)
    {
        auto completion = std::make_shared<CFileCloseCompletion>();
        std::vector<std::future<bool>> waiters;
        for (int index = 0; index < 4; ++index)
            waiters.push_back(std::async(std::launch::async, [completion]() {
                DWORD status; return completion->Wait(5000, status) && status == ERROR_DISK_FULL;
            }));
        completion->Complete(ERROR_DISK_FULL);
        for (auto& waiter : waiters) if (!waiter.get()) return Fail("a concurrent FTP waiter missed the persistent error");
        if (!completion->Wait(0, error) || error != ERROR_DISK_FULL) return Fail("FTP completion could only be consumed once");
    }
    auto cancelled = std::make_shared<CFileCloseCompletion>();
    auto waiter = std::async(std::launch::async, [cancelled]() {
        DWORD status; return !cancelled->Wait(INFINITE, status) && status == ERROR_OPERATION_ABORTED;
    });
    cancelled->CancelWait();
    if (!waiter.get()) return Fail("FTP close wait cancellation claimed a successful result");
    cancelled->Complete(ERROR_SUCCESS);
    if (!cancelled->TryGet(error) || error != ERROR_SUCCESS) return Fail("cancelling a waiter cancelled the disk completion");
    return 0;
}

int TestFtpDownloadReliability()
{
    const int result = TestFtpStagedFiles();
    if (result != 0) return result;
    const int restart = TestFtpIdentityRestart();
    return restart != 0 ? restart : TestFtpCloseCompletion();
}
