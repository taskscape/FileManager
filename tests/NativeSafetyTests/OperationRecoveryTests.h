// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Included inside NativeSafetyTests' fixture namespace. All mutations below use
// exact files in a fresh temporary directory; no external volumes are required.
class CRecoveryTestFileSystem : public CPublicationFileSystem
{
public:
    HANDLE Journal = INVALID_HANDLE_VALUE;
    BOOL FailJournalWrite = FALSE, FailJournalFlush = FALSE;
    int JournalWrites = 0, FailWriteAt = 0;
    std::string FailRecord;
    DWORD MaximumWrite = MAXDWORD;
    BOOL WriteFile(HANDLE file, const void* data, DWORD size, DWORD* written, LPOVERLAPPED overlap) override
    {
        if (file == Journal && (++JournalWrites == FailWriteAt || FailJournalWrite ||
            (!FailRecord.empty() && std::string((const char*)data, size).compare(0, FailRecord.size(), FailRecord) == 0)))
        { SetLastError(ERROR_DISK_FULL); return FALSE; }
        return ::WriteFile(file, data, size < MaximumWrite ? size : MaximumWrite, written, overlap);
    }
    BOOL FlushFileBuffers(HANDLE file) override
    {
        if (file == Journal && FailJournalFlush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return CPublicationFileSystem::FlushFileBuffers(file);
    }
};

BOOL WriteRecoveryFixture(const std::wstring& path, const std::string& contents, ULONGLONG writeTime = 0)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written = 0;
    BOOL ok = WriteFile(file, contents.data(), (DWORD)contents.size(), &written, NULL) && written == contents.size();
    const FILETIME stamp = {(DWORD)writeTime, (DWORD)(writeTime >> 32)};
    if (ok && writeTime != 0) ok = SetFileTime(file, NULL, NULL, &stamp);
    if (ok) ok = FlushFileBuffers(file);
    return CloseHandle(file) && ok;
}

struct CRecoveryFixture
{
    std::wstring Root, Journal, Target, Stage;
    CRecoveryObjectEvidence Parent, Old, Ready;
    CRecoveryFixture()
    {
        // Match production's Unicode path capacity without adding fixed ANSI-era buffers.
        std::vector<WCHAR> base(32768), root(32768);
        if (GetTempPathW((DWORD)base.size(), base.data()) && GetTempFileNameW(base.data(), L"rcv", 0, root.data()) &&
            DeleteFileW(root.data()) && CreateDirectoryW(root.data(), NULL)) Root = root.data();
        Journal = Root + L"\\recovery.opj"; Target = Root + L"\\target"; Stage = Root + L"\\SALCP-stage";
    }
    ~CRecoveryFixture()
    {
        if (Root.empty()) return;
        const std::wstring names[] = {Target, Stage, Stage + L".previous", Root + L"\\replacement", Journal,
                                      Root + L"\\target2", Root + L"\\SALCP-stage2", Root + L"\\SALCP-stage2.previous"};
        for (const auto& name : names) { SetFileAttributesW(name.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(name.c_str()); }
        RemoveDirectoryW(Root.c_str());
    }
    BOOL Seed(BOOL present = TRUE)
    {
        if (Root.empty() || !WriteRecoveryFixture(Stage, "staged") || (present && !WriteRecoveryFixture(Target, "old"))) return FALSE;
        CConditionalFilePublication publication;
        return publication.Open(Target.c_str(), Stage.c_str(), present) &&
            ReadRecoveryObjectIdentity(publication.DirectoryHandle(), Parent) &&
            CaptureRecoveryEvidence(publication.TemporaryHandle(), Ready) &&
            (!present || CaptureRecoveryEvidence(publication.TargetHandle(), Old));
    }
    std::string Records(int index = 0) const
    {
        const std::string item = std::to_string(index), target = RecoveryUtf8Path(Target.c_str()), stage = RecoveryUtf8Path(Stage.c_str());
        return "ITEM|" + item + "|copy-file|source|" + target + "|unavailable\r\nSTATE|" + item + "|prepared|attempt=1\r\nTEMP|" +
            item + "|" + stage + "\r\nREADY2|" + item + "|attempt=1|auto=1|" + target + "|" + stage + "|" +
            SerializeRecoveryEvidence(Old) + "|" + SerializeRecoveryEvidence(Ready) + "|" + SerializeRecoveryEvidence(Parent) +
            "|security=1|end\r\nSTATE|" + item + "|temporary-ready|attempt=1\r\n";
    }
    BOOL Save(const std::string& tail = "") const
    {
        return WriteRecoveryFixture(Journal, "FORMAT|2\r\nOPERATION|planned|items=1\r\n" + Records() + tail);
    }
};

int TestRecoveryConflicts()
{
    const char* scenarios[] = {"valid overwrite", "valid absent target", "changed destination", "replaced stage", "truncated stage",
        "same-length tamper with restored timestamp", "different parent", "legacy evidence", "retry revokes readiness", "new TEMP revokes readiness",
        "truncated record", "overlong record", "malformed evidence", "new named stream", "manual-only evidence", "sharing violation",
        "occupied backup", "failed deletion", "failed rename", "failed journal write", "failed journal flush", "new destination", "unavailable parent"};
    for (int action : {IDYES, IDNO})
    for (int scenario = 0; scenario < (int)_countof(scenarios); ++scenario)
    {
        if ((scenario == 17 && action == IDYES) || (scenario == 18 && action == IDNO)) continue;
        CRecoveryFixture fixture;
        if (!fixture.Seed(scenario != 1 && scenario != 21)) return Fail("cannot seed recovery conflict fixture");
        if (scenario == 6) fixture.Parent.IndexLow ^= 1;
        if (!fixture.Save()) return Fail("cannot save recovery conflict journal");
        std::string expectedTarget = scenario == 1 ? "" : "old", expectedStage = "staged";
        if (scenario == 2 || scenario == 21) { expectedTarget = "newer"; if (!WriteRecoveryFixture(fixture.Target, expectedTarget)) return Fail("cannot change recovery target"); }
        if (scenario == 22)
        {
            // Retain the real files but point the complete plan at an unavailable
            // parent, as happens when removable storage is absent at startup.
            std::string record = "FORMAT|2\r\n" + fixture.Records();
            const std::string root = RecoveryUtf8Path(fixture.Root.c_str());
            for (size_t position = 0; (position = record.find(root, position)) != std::string::npos; position += root.size() + 8)
                record.replace(position, root.size(), root + "\\offline");
            if (!WriteRecoveryFixture(fixture.Journal, record)) return Fail("cannot seed offline recovery journal");
        }
        if (scenario == 3)
        {
            if (!MoveFileW(fixture.Stage.c_str(), (fixture.Root + L"\\replacement").c_str()) ||
                !WriteRecoveryFixture(fixture.Stage, "other")) return Fail("cannot replace recovery stage");
            expectedStage = "other";
        }
        if (scenario == 4 || scenario == 5)
        {
            expectedStage = scenario == 4 ? "s" : "forged";
            if (!WriteRecoveryFixture(fixture.Stage, expectedStage, fixture.Ready.WriteTime)) return Fail("cannot corrupt recovery stage");
        }
        if (scenario == 7)
        {
            const std::string legacy = "FORMAT|1\r\nITEM|0|copy-file|source|" + RecoveryUtf8Path(fixture.Target.c_str()) +
                "|identity\r\nTEMP|0|" + RecoveryUtf8Path(fixture.Stage.c_str()) + "\r\nSTATE|0|temporary-ready\r\n";
            if (!WriteRecoveryFixture(fixture.Journal, legacy)) return Fail("cannot seed legacy journal");
        }
        if (scenario == 8 && !fixture.Save("RETRY|0|attempt=2\r\nSTATE|0|temporary-ready|attempt=2\r\n")) return Fail("cannot seed retry journal");
        if (scenario == 9 && !fixture.Save("TEMP|0|" + RecoveryUtf8Path(fixture.Stage.c_str()) + "\r\nSTATE|0|temporary-ready|attempt=1\r\n"))
            return Fail("cannot seed new-stage journal");
        if (scenario == 10 && !fixture.Save("STATE|0|committed")) return Fail("cannot seed truncated journal");
        if (scenario == 11 && !fixture.Save(std::string(65537, 'x') + "\r\n")) return Fail("cannot seed overlong record");
        if (scenario == 12 || scenario == 14)
        {
            std::string record = fixture.Records();
            const std::string from = scenario == 12 ? SerializeRecoveryEvidence(fixture.Ready) : "auto=1";
            const std::string to = scenario == 12 ? "0000000100000000,0,0,0,0,0,unverified" : "auto=0";
            record.replace(record.find(from), from.size(), to);
            if (!WriteRecoveryFixture(fixture.Journal, "FORMAT|2\r\n" + record)) return Fail("cannot seed unsupported evidence");
        }
        if (scenario == 13 && !WriteRecoveryFixture(fixture.Stage + L":added", "new stream")) return Fail("cannot seed recovery ADS");
        if (scenario == 16 && !WriteRecoveryFixture(fixture.Stage + L".previous", "unrelated")) return Fail("cannot seed occupied recovery backup");
        HANDLE blocker = scenario == 15 ? CreateFileW(fixture.Target.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
        if (scenario == 15 && blocker == INVALID_HANDLE_VALUE) return Fail("cannot open recovery sharing blocker");
        const char* failure = NULL;
        {
            COperationRecovery recovery;
            const BOOL loaded = recovery.Load(fixture.Journal.c_str());
            if (!loaded && scenario != 10 && scenario != 11 && scenario != 12) failure = "valid recovery journal did not parse";
            CRecoveryTestFileSystem fileSystem;
            fileSystem.Journal = recovery.Lease.Get(); fileSystem.MaximumWrite = 7; // exercise successful short-write completion too
            fileSystem.FailDeletion = scenario == 17; fileSystem.FailRenameAt = scenario == 18 ? 1 : 0;
            fileSystem.FailJournalWrite = scenario == 19; fileSystem.FailJournalFlush = scenario == 20;
            int resumed = 0, discarded = 0, unresolved = 0;
            recovery.Reconcile(action, fileSystem, resumed, discarded, unresolved);
            if (scenario <= 1)
            {
                expectedTarget = action == IDYES ? "staged" : expectedTarget; expectedStage.clear();
                if (resumed != (action == IDYES) || discarded != (action == IDNO) || unresolved != 0 || recovery.NeedsRecovery())
                    failure = "verified recovery did not persist its result";
            }
            else if (resumed != 0 || discarded != 0 || unresolved != 1 || !recovery.NeedsRecovery())
                failure = "failed recovery lost its pending state";
        }
        if (blocker != INVALID_HANDLE_VALUE) CloseHandle(blocker);
        if (ReadPublicationFixture(fixture.Target) != expectedTarget || ReadPublicationFixture(fixture.Stage) != expectedStage)
            failure = "recovery changed an unverified file";
        if (scenario > 1)
        {
            COperationRecovery restart;
            restart.Load(fixture.Journal.c_str());
            if (!restart.NeedsRecovery()) failure = "restart suppressed unresolved recovery";
        }
        if (failure != NULL)
        { fprintf(stderr, "Recovery scenario %s action=%d: %s (error=%lu)\n", scenarios[scenario], action, failure, GetLastError()); return 1; }
    }
    return 0;
}

int TestRecoveryOwnershipAndRetry()
{
    CRecoveryFixture fixture;
    if (!fixture.Seed() || !fixture.Save()) return Fail("cannot seed recovery ownership fixture");
    // A live writer's production sharing flags exclude discovery, and exactly
    // one recovery actor can claim the same journal after that writer closes.
    HANDLE writer = CreateFileW(fixture.Journal.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (writer == INVALID_HANDLE_VALUE) return Fail("cannot open live journal writer");
    COperationRecovery first, second;
    if (first.Load(fixture.Journal.c_str()) || first.Error != ERROR_SHARING_VIOLATION) return Fail("recovery claimed a live writer");
    CloseHandle(writer);
    if (!first.Load(fixture.Journal.c_str()) || second.Load(fixture.Journal.c_str()) || second.Error != ERROR_SHARING_VIOLATION)
        return Fail("recovery claim was not exclusive");
    CRecoveryTestFileSystem fileSystem; fileSystem.Journal = first.Lease.Get();
    int resumed = 0, discarded = 0, unresolved = 0;
    first.Reconcile(IDCANCEL, fileSystem, resumed, discarded, unresolved);
    if (unresolved != 1 || !first.NeedsRecovery()) return Fail("Cancel suppressed recovery");
    first.Lease.Close();
    if (!second.Load(fixture.Journal.c_str()) || !second.NeedsRecovery()) return Fail("Cancel was terminal after restart");
    fileSystem.Journal = second.Lease.Get(); unresolved = 0;
    second.Reconcile(IDYES, fileSystem, resumed, discarded, unresolved);
    if (resumed != 1 || unresolved != 0) return Fail("recovery could not resume after Cancel");
    second.Lease.Close();
    if (!first.Load(fixture.Journal.c_str()) || first.NeedsRecovery()) return Fail("completed recovery was offered again");
    return 0;
}

int TestRecoveryMixedOutcomes()
{
    CRecoveryFixture fixture;
    if (!fixture.Seed()) return Fail("cannot seed mixed recovery fixture");
    const std::string first = fixture.Records();
    const std::wstring originalTarget = fixture.Target;
    fixture.Target = fixture.Root + L"\\target2"; fixture.Stage = fixture.Root + L"\\SALCP-stage2";
    if (!fixture.Seed()) return Fail("cannot seed second recovery item");
    if (!WriteRecoveryFixture(fixture.Journal, "FORMAT|2\r\n" + first + fixture.Records(1)) ||
        !WriteRecoveryFixture(fixture.Stage, "broken")) return Fail("cannot persist mixed recovery fixture");
    {
        COperationRecovery recovery; if (!recovery.Load(fixture.Journal.c_str())) return Fail("cannot load mixed recovery journal");
        CRecoveryTestFileSystem fileSystem; fileSystem.Journal = recovery.Lease.Get();
        int resumed = 0, discarded = 0, unresolved = 0;
        recovery.Reconcile(IDYES, fileSystem, resumed, discarded, unresolved);
        if (resumed != 1 || unresolved != 1 || !recovery.NeedsRecovery()) return Fail("mixed recovery lost a per-item result");
    }
    if (!WriteRecoveryFixture(originalTarget, "later") || !WriteRecoveryFixture(fixture.Stage, "staged", fixture.Ready.WriteTime))
        return Fail("cannot repair pending mixed item");
    {
        COperationRecovery recovery; if (!recovery.Load(fixture.Journal.c_str())) return Fail("cannot reload mixed recovery journal");
        CRecoveryTestFileSystem fileSystem; fileSystem.Journal = recovery.Lease.Get();
        int resumed = 0, discarded = 0, unresolved = 0;
        recovery.Reconcile(IDYES, fileSystem, resumed, discarded, unresolved);
        if (resumed != 1 || unresolved != 0 || recovery.NeedsRecovery() || ReadPublicationFixture(originalTarget) != "later")
            return Fail("restart replayed an already resolved item");
    }
    DeleteFileW(originalTarget.c_str());
    return 0;
}

int TestRecoveryLargeJournals()
{
    for (int extra = -1; extra <= 1; ++extra)
    {
        CRecoveryFixture fixture;
        if (!fixture.Seed()) return Fail("cannot seed large journal fixture");
        std::string journal = "FORMAT|2\r\n";
        const std::string ready = fixture.Records();
        const size_t size = 16 * 1024 * 1024 + extra;
        while (journal.size() + ready.size() + 65536 < size) journal += "PLANITEM|" + std::string(65525, 'x') + "\r\n";
        journal += "PLANITEM|" + std::string(size - journal.size() - ready.size() - 11, 'x') + "\r\n";
        journal += ready;
        if (journal.size() != size || !WriteRecoveryFixture(fixture.Journal, journal)) return Fail("cannot write journal boundary fixture");
        COperationRecovery recovery;
        if (!recovery.Load(fixture.Journal.c_str()) || !recovery.NeedsRecovery()) return Fail("valid large journal disappeared during discovery");
        CRecoveryTestFileSystem fileSystem; fileSystem.Journal = recovery.Lease.Get();
        int resumed = 0, discarded = 0, unresolved = 0;
        recovery.Reconcile(IDYES, fileSystem, resumed, discarded, unresolved);
        if (resumed != 1 || unresolved != 0) return Fail("ready item near the end of a large journal could not be recovered");
    }
    return 0;
}

int TestRecoveryInterruptedPersistence()
{
    for (BOOL torn : {FALSE, TRUE})
    {
        CRecoveryFixture fixture;
        if (!fixture.Seed() || !fixture.Save()) return Fail("cannot seed recovery write-failure fixture");
        {
            COperationRecovery recovery;
            if (!recovery.Load(fixture.Journal.c_str())) return Fail("cannot claim write-failure fixture");
            CRecoveryTestFileSystem fileSystem; fileSystem.Journal = recovery.Lease.Get();
            fileSystem.MaximumWrite = torn ? 7 : MAXDWORD; fileSystem.FailWriteAt = torn ? 2 : 0;
            if (!torn) fileSystem.FailRecord = "RECOVERY|0|discarded";
            int resumed = 0, discarded = 0, unresolved = 0;
            recovery.Reconcile(IDNO, fileSystem, resumed, discarded, unresolved);
            if (unresolved != 1 || discarded != 0 || recovery.Lease.GetWriteError() != ERROR_DISK_FULL ||
                recovery.Lease.Append(fileSystem, "OPERATION|reconciled\r\n"))
                return Fail("failed outcome persistence was not latched");
        }
        COperationRecovery restart;
        const BOOL loaded = restart.Load(fixture.Journal.c_str());
        if (loaded == torn || !restart.NeedsRecovery()) return Fail("failed outcome vanished after restart");
        if (ReadPublicationFixture(fixture.Target) != "old" || ReadPublicationFixture(fixture.Stage) != (torn ? "staged" : ""))
            return Fail("write failure crossed an unrecorded destructive boundary");
        if (!torn)
        {
            CRecoveryTestFileSystem fileSystem; fileSystem.Journal = restart.Lease.Get();
            int resumed = 0, discarded = 0, unresolved = 0;
            restart.Reconcile(IDNO, fileSystem, resumed, discarded, unresolved);
            if (discarded != 1 || unresolved != 0 || restart.NeedsRecovery()) return Fail("discard outcome could not be retried after its append failed");
        }
    }
    return 0;
}

int TestRecoveryParentRedirection()
{
    for (int action : {IDYES, IDNO})
    {
        CRecoveryFixture fixture;
        const std::wstring first = fixture.Root + L"\\first", second = fixture.Root + L"\\second", link = fixture.Root + L"\\link";
        if (!CreateDirectoryW(first.c_str(), NULL) || !CreateDirectoryW(second.c_str(), NULL) || !CreatePublicationJunction(link, first))
            return Fail("cannot create recovery junction fixture");
        fixture.Target = link + L"\\target"; fixture.Stage = link + L"\\SALCP-stage";
        if (!fixture.Seed() || !fixture.Save() || !WriteRecoveryFixture(second + L"\\target", "newer") ||
            !WriteRecoveryFixture(second + L"\\SALCP-stage", "other") || !RemoveDirectoryW(link.c_str()) || !CreatePublicationJunction(link, second))
            return Fail("cannot retarget recovery junction");
        const char* failure = NULL;
        {
            COperationRecovery recovery; if (!recovery.Load(fixture.Journal.c_str())) return Fail("cannot load junction recovery journal");
            CRecoveryTestFileSystem fileSystem; fileSystem.Journal = recovery.Lease.Get();
            int resumed = 0, discarded = 0, unresolved = 0;
            recovery.Reconcile(action, fileSystem, resumed, discarded, unresolved);
            if (unresolved != 1 || resumed != 0 || discarded != 0 || ReadPublicationFixture(first + L"\\target") != "old" ||
                ReadPublicationFixture(first + L"\\SALCP-stage") != "staged" || ReadPublicationFixture(second + L"\\target") != "newer" ||
                ReadPublicationFixture(second + L"\\SALCP-stage") != "other") failure = "recovery followed a changed parent into unrelated files";
        }
        RemoveDirectoryW(link.c_str());
        for (const auto& directory : {first, second})
        {
            DeleteFileW((directory + L"\\target").c_str()); DeleteFileW((directory + L"\\SALCP-stage").c_str());
            RemoveDirectoryW(directory.c_str());
        }
        if (failure != NULL) return Fail(failure);
    }
    return 0;
}

int TestOperationRecovery()
{
    int result = TestRecoveryConflicts();
    if (result == 0) result = TestRecoveryOwnershipAndRetry();
    if (result == 0) result = TestRecoveryMixedOutcomes();
    if (result == 0) result = TestRecoveryLargeJournals();
    if (result == 0) result = TestRecoveryInterruptedPersistence();
    if (result == 0) result = TestRecoveryParentRedirection();
    return result;
}
