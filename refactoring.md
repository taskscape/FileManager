# Stability and Resilience Improvement Proposal

## Operational audit ledger

This document is the working record for a read-only stability and resilience audit of commit `ab8a3e827ed7f58eebf3c95bc5c88141f12a69a9` on branch `main` (2026-08-01). Findings are recorded here before being converted into the ranked proposal below.

### Scope and method

- Reviewed the native file-operation, worker-thread, plug-in, configuration, crash-reporting, build, release, installer, and UI-test paths.
- Ranked risks by potential for data loss, deadlock, process termination, unrecoverable state, release compromise, and regression escape.
- Treated legacy constraints as real: improvements should be introduced behind seams and characterized before behavior changes, not through a wholesale rewrite.
- No source code or runtime configuration is changed by this proposal.

### Repository snapshot

- The product is a large Win32 C/C++ solution: 93 Visual C++ projects, approximately 958 `.cpp`, 1,134 `.h`, and 212 `.c` files.
- Several central implementation files exceed 4,000 lines, including `src/async_copy.cpp`, `src/mainwnd_commands.cpp`, `src/fileswindow_execute.cpp`, `src/app_entry.cpp`, and `src/path_utils.cpp`.
- Automated tests currently consist primarily of `tests/FileManager.UiTests`, an NUnit/FlaUI suite. Its 100 repeated lifecycle cases plus 18 focused file-operation cases cover normal, cancelled, and deliberately blocked create/copy/rename/move/delete flows against disposable directory trees.
- Pull-request CI compiles Debug Win32/x64 but does not run tests. The release workflow builds Release x64 and publishes directly from pushes to `main`.

### Confirmed high-risk findings

1. **Forced thread termination remains in legacy subsystems.** The cache and icon-worker shutdown paths have been converted to cooperative cancellation and safe joins. `src/path_checking.cpp` remains separately tracked because its teardown also has an unsafe synchronization-destruction order.
2. **One shutdown sequence destroys synchronization primitives before workers are guaranteed stopped.** `ReleaseCheckThreads` in `src/path_checking.cpp` deletes critical sections before signaling and joining its threads.
3. **Several UI/worker handshakes wait forever.** File-operation startup and worker suspension use `INFINITE` waits; worker code also uses synchronous `SendMessage` to the UI, creating circular-wait potential.
4. **Cancellation state is shared through plain `BOOL` pointers and globals.** These accesses do not provide atomic visibility or an explicit state machine.
5. **Overwrite is transactional for native file copies.** Confirmed overwrites now copy to a uniquely reserved sibling temporary file and replace the requested destination only after the durable copy commit point and an atomic same-volume commit.
6. **Implemented: cross-volume moves verify retried copies before source deletion.** They wait for the durable copy commit point, including closed-output size/metadata verification. When the copy path needed an I/O retry, the closed source and destination are fully re-read and compared with SHA-256 before deletion; a failure leaves the source intact.
7. **Implemented: direct-to-new-destination copies have a durable completion point.** All core copies now request write-through, flush and successfully close the output, then reopen it to verify its file metadata and size before reporting success.
8. **Legacy size and seek APIs are widespread.** `GetFileSize` and `SetFilePointer` retain sentinel/error ambiguity and complicate correct files larger than 4 GiB.
9. **Path and string handling remains fixed-buffer heavy.** Thousands of `MAX_PATH` references and many unchecked copy/format calls make long paths and boundary inputs fragile.
10. **Plug-ins execute in-process behind manually balanced global entry state.** A plug-in failure can skip cleanup, corrupt host bookkeeping, or terminate the file manager.
11. **DLL loading does not use a constrained search policy.** Plug-in loading ultimately calls `LoadLibraryW` without modern search flags, and no plug-in signature verification was found.
12. **Configuration has useful shutdown backup handling, but ordinary saves update the active settings directly.** A generalized staged, validated, versioned commit is absent.
13. **Crash upload is an unsafe custom HTTP implementation.** `src/salmon/upload.cpp` uses plaintext HTTP, buffers the entire multipart request, calculates lengths with `int`, performs a single `send`, and lacks robust timeout/partial-I/O handling.
14. **Crash dumps may capture sensitive process memory.** Dump flags include private read/write memory and data segments, with no redaction callback or explicit privacy/retention policy in the reviewed path.
15. **Release dependency acquisition is weakly verified.** The OpenSSL archive is accepted after a ZIP-magic check rather than a pinned cryptographic digest or signature.
16. **Release signing is not implemented in the repository workflow.** `tools/codesign/sign_with_retry.cmd` is a placeholder, and the installer script does not define signing steps.
17. **Installer staging can select stale or mismatched binaries.** `tools/prepare_installer.ps1` recursively falls back to the first matching output and suppresses some errors.
18. **Several bundled libraries are substantially old.** Reviewed versions include OpenSSL 1.0.2u, bzip2 1.0.6, SQLite 3.28.0, zlib 1.2.11, 7-Zip 16.04, and cmark-gfm 0.29.0.gfm.0.
19. **Compiler hardening and static-analysis lanes are limited.** The common project properties use warning level 3 and disable secure-CRT warnings; no repository CI lane for ASan, CodeQL, clang-cl, or `/analyze` was found.
20. **The safety net remains shallow for the product's highest-risk behavior.** Executable UI coverage now exercises normal, cancelled, and deliberately blocked create/copy/rename/move/delete operations, but there are no native characterization tests, parser fuzzers, disk-full fault tests, crash-consistency tests, or automated copy/move/delete recovery scenarios.

### Working prioritization rules

1. Prevent data loss and process corruption first.
2. Remove deadlocks and unbounded waits next.
3. Make release inputs and outputs deterministic and verifiable.
4. Add characterization and fault-injection coverage before invasive refactoring.
5. Introduce RAII, safer APIs, and narrower ownership incrementally at subsystem seams.
6. Ratchet new code immediately while migrating legacy code in measured batches.

## Ranked improvements

### Critical: data integrity, deadlocks, and release safety

### 1. Implemented: eliminate `TerminateThread` from cache and icon-worker shutdown

- **Delivered:** The cache-handle and icon-reader workers use their termination events as cooperative cancellation signals. Their startup and retry delays are interruptible, shutdown waits for a measured one-second deadline, and a deadline breach is logged before a safe join continues. The owning cache/panel state is therefore not destroyed while its worker can still access it.
- **Guardrail:** Pull-request CI runs `tools/verify-no-new-terminatethread.ps1`, which rejects newly added `TerminateThread` calls in native source while retaining the reviewed legacy baseline. Remaining call sites are to be removed by their owning subsystem changes, beginning with the check-path teardown in improvement 2.

### 2. Correct the check-path worker teardown order

- **Justification:** `ReleaseCheckThreads` deletes `ReadCDVolNameCS` and `CheckPathCS` before it signals or joins their users (`src/path_checking.cpp:85`). A worker that wakes or is still running can touch destroyed synchronization state.
- **Proposed solution:** Set a stop flag atomically, signal all wait events, join every thread, close thread/event handles, and only then destroy the critical sections. Add a repeated startup/shutdown test under Application Verifier.

### 3. Replace infinite progress-dialog startup waits with a bounded protocol — Implemented

- **Justification:** The UI passes stack-backed startup data to a new thread and waits forever for its continuation event (`src/dialogs_file_ops.cpp:350`). A creation, initialization, or exception-path failure can freeze the UI.
- **Implemented:** Startup state now lives in a reference-counted owned object with copied startup inputs and duplicated event handles. The UI waits at most ten seconds while dispatching only paint messages; the dialog reports ready or failed, and a timeout signals cancellation. If the thread has already accepted the script, it owns and cancels/frees it rather than allowing the caller to release memory still in use.

### 4. Implemented: remove the worker-to-UI circular wait at operation completion

- **Delivered:** The worker now frees its operation script and posts `WM_USER_PROGRDLG_WORKERCOMPLETE` with an owned `CWorkerCompletion` result. The progress dialog consumes that result asynchronously, then joins the already-independent worker only to close its handle. Completion no longer uses synchronous `WM_COMMAND`, `ReplyMessage`, or a UI-controlled continuation event, so modal, cancel, close, and shutdown paths cannot hold worker cleanup hostage. `tools/verify-operation-completion-protocol.ps1`, run by pull-request CI, deterministically guards the close, cancel, and shutdown protocol invariants.

### 5. Implemented: model file-operation cancellation as atomic state

- **Justification:** Cancellation is shared through plain `BOOL*` fields and globals such as `CPFirstTerminate`; this does not define memory visibility or distinguish cancel-requested, stopping, completed, and failed states.
- **Delivered:** `COperations` now owns an interlocked lifecycle (`planned`, `running`, `cancel-requested`, `stopping`, `completed`, and `failed`) plus a manual-reset cancellation event. Dialog and worker paths make idempotent cancellation requests through that owner, worker completion records the terminal state, and debug builds break on invalid transitions. Existing low-level helper call sites use a compatibility view backed by this owner rather than sharing a `BOOL*`.

### 6. Implemented: make destination overwrite transactional

- **Delivered:** After the existing overwrite and protected-file confirmations, `DoCopyFile` reserves a uniquely named sibling temporary file rather than deleting or truncating the destination. It applies the existing metadata pipeline to that temporary file, flushes the copied data before close, and commits with `ReplaceFileW(REPLACEFILE_WRITE_THROUGH)`. If another actor removed the destination after confirmation, a same-volume write-through `MoveFileExW` commits the temporary file instead. Retry, skip, cancel, low-space, and metadata-error paths delete only the temporary reservation; the original destination remains untouched until commit succeeds.

### 7. Implemented: define and enforce a durable copy commit point

- **Delivered:** Every core native copy opens the output with `FILE_FLAG_WRITE_THROUGH` where supported, calls `FlushFileBuffers`, and treats a failed flush or close as a copy failure. After closing, it reopens the destination and validates that it is a file with the expected size metadata. This is the durable copy commit point: UI success, write-through replacement of an existing destination, and cross-volume move-source deletion occur only after it passes.
- **Guardrail:** `tools/verify-durable-copy-commit.ps1`, run by pull-request CI, verifies the ordering of write-through creation, flush, close, post-close metadata verification, replacement, and move-source deletion.

### 8. Implemented: verify retried cross-volume moves before deleting the source

- **Delivered:** Cross-volume moves still require the durable destination close plus closed-output size/metadata validation already enforced by `DoCopyFile`. If a retry-resume, Lantastic mismatch retry, flush/close retry, or durable-copy verification retry occurred, `DoMoveFile` reopens the still-present source and committed destination and compares full SHA-256 digests before it calls `DeleteFileUtf8` on the source. A mismatch or hashing failure offers retry/skip/cancel; skip and cancel retain the source. `tools/verify-durable-copy-commit.ps1`, run in pull-request CI, checks that the SHA-256 gate remains between the copy and source deletion.

### 9. Implemented: add a recoverable journal for multi-item file operations

- **Delivered:** `src/operation_journal.cpp` creates a write-through, append-only journal for every native operation script in `%APPDATA%\\Open Salamander\\operation-journals`. It records each mutating item’s source, destination, captured handle identity, transactional temporary path, and `prepared`/`temporary-ready`/`committed` transitions. Journal writes happen before each mutating item and are flushed before the operation continues.
- **Recovery:** Startup detects journals without a terminal record. The user may resume only a fully-written sibling transactional target, roll back journaled uncommitted temporary targets, or leave files untouched. Every recovery choice produces a reconciliation report containing the original journal records and unresolved-item count; ordinary incomplete direct writes are reported rather than replayed automatically.

### 10. Implemented: revalidate file identity immediately before destructive actions

- **Delivered:** `src/file_identity.cpp` captures the source and destination identity as each worker item begins, before any confirmation or I/O delay. It opens with `FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS`, records the volume serial and file ID, and fingerprints `GetFinalPathNameByHandleW` output. Transactional overwrite checks that captured identity immediately before `ReplaceFileW`/`MoveFileExW` can run and refuses a changed or newly introduced target.
- **Deletion:** Direct file and empty-directory deletion now reopens and rechecks the recorded identity, then applies `FileDispositionInfo` to that verified handle. This binds the destructive operation to the opened object instead of a subsequently re-resolved name and preserves reparse-point behavior. Recycle Bin requests are still delegated to the shell but retain the same pre-action identity capture for the native direct-delete path.

### 11. Replace legacy file-size and seek APIs in operation code — Implemented (2026-08-09)

- **Implementation:** `CFileOffsetResult` carries a 64-bit value, success state, and the Win32 error captured by `SalGetFileSizeEx` or `SalSetFilePointerEx`. The copy/move engine, including ADS, retry, allocation, truncation, and overwrite checks, now uses these wrappers.
- **Follow-up:** Other legacy callers remain subject to the planned ratchet; the regression test prevents raw `GetFileSize` or `SetFilePointer` calls from returning to `src/async_copy.cpp`.

### 12. Replace the custom crash uploader with HTTPS WinHTTP — Implemented (2026-08-09)

- **Delivered:** `src/salmon/upload.cpp` now streams the existing crash-report archive through WinHTTP to `https://reports.taskscape.com/api/v1/crash-reports`. It uses the secure-request flag and default SChannel certificate-chain/hostname validation, supports explicit Windows/Internet Settings proxies, bounds network I/O, and rejects all redirects so HTTPS cannot be downgraded to HTTP.
- **Consent and reporting:** The crash-report dialog now explicitly states that Send Report transmits the dump archive, optional description, and optional email to `reports.taskscape.com` over HTTPS, and points users to View Report and Do Not Send Report. `reporting.md` records the exact archive contents, protocol, expected endpoint, configured endpoint, and legacy address.

### 13. Stream crash uploads with correct framing and bounded I/O

- **Justification:** The uploader casts file size into `int`, allocates the entire request, knowingly writes an imprecise `Content-Length`, assumes one `send` transmits everything, and has no robust timeouts (`src/salmon/upload.cpp:29-69`, `220-240`).
- **Proposed solution:** Use 64-bit checked arithmetic, stream fixed-size chunks, let WinHTTP frame the body, set connect/send/receive deadlines, support cancellation, cap response size, and retry only idempotent pre-commit failures.

### 14. Replace OpenSSL 1.0.2u with a supported TLS implementation — Implemented (2026-08-09)

- **Delivered:** The FTP plug-in uses the Windows SChannel stream security package with TLS 1.2 and TLS 1.3 enabled; it no longer loads OpenSSL or ships `libeay32.dll`/`ssleay32.dll`. Certificate-chain, hostname, validity, and revocation checks still use the Windows certificate APIs, preserving the existing explicit, per-connection user exception flow.
- **Verification:** `SChannelTlsIntegrationTests` negotiates local TLS 1.2 and TLS 1.3 servers and proves that a self-signed certificate is rejected without an explicit exception. The release workflow and installer staging have no TLS runtime download or DLL copy.
- **Cadence:** SChannel updates arrive through supported Windows servicing. Before each supported-Windows baseline change and each quarterly release, run the TLS integration tests on the oldest supported Windows release and Windows Server 2022 or newer, confirm TLS 1.2 and TLS 1.3 negotiation, and review Windows TLS/security advisories. Treat a failed certificate-rejection test or a newly disabled protocol as a release blocker.

### 15. Gate releases on the complete verification suite

- **Justification:** Every push to `main` builds and immediately publishes a non-draft release (`.github/workflows/build-installer.yml:3`, `110-117`) without a test job.
- **Proposed solution:** Split build, test, package, sign, and publish into dependent jobs. Publish only immutable artifacts from a protected tag after native tests, UI smoke tests, installer tests, and security checks pass.

### 16. Authenticode-sign every executable, DLL, plug-in, and installer

- **Justification:** `tools/codesign/sign_with_retry.cmd:1` is a placeholder and no installer signing directive is present. Unsigned artifacts are harder to authenticate and more likely to be blocked or replaced unnoticed.
- **Proposed solution:** Use a hardware-backed or managed certificate, timestamp signatures, verify every staged PE before packaging, sign the installer last, and fail release if any required signature or timestamp is invalid.

### 17. Cryptographically pin all downloaded release inputs

- **Justification:** The OpenSSL download is accepted after checking only the first `PK` bytes (`.github/workflows/build-installer.yml:50-71`). Any ZIP from that endpoint satisfies the check.
- **Proposed solution:** Store an approved SHA-256 digest and provenance for each dependency, verify before extraction, and prefer an authenticated package registry or checked-in lock manifest. Treat digest changes as reviewed dependency updates.

### 18. Make installer staging manifest-driven and fail-closed

- **Justification:** `tools/prepare_installer.ps1:29-45` recursively selects the first matching binary from build and source trees, and several copies suppress errors. A release can silently mix stale architectures or builds.
- **Proposed solution:** Generate a build-output manifest containing exact paths, architecture, version, commit, and hash; stage only those entries into a fresh directory. Reject duplicates, missing optionality declarations, wrong PE architecture, or unexpected files.

### 19. Constrain DLL search paths — Implemented (2026-08-09)

- **Delivered:** Startup now requires the Windows DLL-directory APIs (including the Windows 7 KB2533623 backport), sets the process default to the application directory, System32, and explicitly approved user directories, and registers only the application directory as a process-wide user directory.
- **Load contract:** Both Debug and Release `LoadLibraryUtf8` implementations canonicalize their UTF-8 target with `GetFullPathNameW` and load it with `LoadLibraryExW` using `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`, `LOAD_LIBRARY_SEARCH_SYSTEM32`, and `LOAD_LIBRARY_SEARCH_USER_DIRS`. Plug-in dependencies therefore resolve from the explicitly named plug-in directory, approved application directory, or System32—not the current directory or `PATH`.

### 20. Establish plug-in trust and quarantine policy

- **Justification:** Plug-ins are loaded in-process, and no `WinVerifyTrust` check was found in the reviewed loader. A damaged or replaced plug-in has the same privileges and address space as the host.
- **Proposed solution:** Verify Authenticode signatures and file hashes before load, record publisher decisions, quarantine failed updates, and give users a safe-mode launch that disables third-party plug-ins.

### High: fault containment and core correctness

### 21. Move risky parsers and previewers out of process — Implemented

- **Delivered:** `salbroker.exe` is a restricted-token helper placed in a kill-on-close job with per-process memory and CPU limits. The host owns a local named pipe, validates a versioned and length-checked protocol on both sides, and kills/restarts the broker once after a timeout, malformed reply, or process failure.
- **Initial scope:** Thumbnail requests no longer call thumbnail-loader plug-ins in the icon worker; the helper obtains the image through the Shell thumbnail provider and returns bounded pixels only. Archive-file metadata is also requested through the helper before archive handling. The v1 archive response deliberately carries only file metadata; format-specific archive navigation and extraction remain a separate migration because their plug-in data and callbacks cannot safely cross this ABI boundary.

### 22. Implemented: make plug-in entry bookkeeping exception-safe

- **Delivered:** `CPluginEntryScope` now owns the direct `SalamanderPluginEntry` call. Its destructor always releases the plug-in entry state, restores `SalamanderGeneral`, and clears the temporary `-1` interface placeholder if the entry point unwinds or exits before returning an interface. The scope uses an RAII data lock for every interface transition.
- **Synchronization:** The process-wide plug-in nesting counter is now atomic, and its first-entry/last-exit transitions are serialized with an SRW lock. Shutdown and notification callers query it through `IsInPlugin()` rather than reading mutable state directly.
- **Verification:** `NativeSafetyRegressionTests.Plugin_entry_scope_restores_host_state_after_an_unwinding_entry_point` guards the scope, placeholder cleanup, synchronization, callers, and this ledger entry.

### 23. Add failure barriers around every plug-in callback — Implemented

- **Justification:** Host-to-plug-in calls are numerous and not governed by one recovery contract; at least one plug-in thread exception path terminates the process. A single extension fault should not take down unrelated file operations.
- **Proposed solution:** Route callbacks through a common boundary that records the plug-in identity, validates results, restores host invariants, disables the failing extension, and reports a recoverable error. Use process isolation where recovery from memory corruption cannot be trusted.
- **Implementation (2026-08-09):** The shared `PLUGIN_CALLBACK` SEH boundary now covers the base, file-system, and plug-in-data callback facades. It records the owner, returns conservative initialized results after a fault, restores refresh/directory invariants through the caller's normal `LeavePlugin`, prevents reload at startup, and queues the extension for deferred unload. The plug-in thread wrapper now follows the same recovery path instead of terminating `salamand.exe`. Existing out-of-process parser brokering remains the isolation boundary for untrusted parser/preview work where in-process recovery cannot be trusted.

### 24. Make configuration saves transactional — Implemented (2026-08-09)

- **Justification:** Shutdown backup logic exists, but ordinary `SaveConfig` paths write many values into the active settings tree. Interruption can leave a partially updated configuration.
- **Implementation (2026-08-09):** `SaveConfig` now serializes every host and plug-in setting into the inactive slot beneath `Configuration Generations`, computes and verifies a recursive checksum, writes a completion marker, flushes the slot, and changes the root `Active Generation` DWORD only after validation succeeds. Startup validates the selected slot before exposing it to existing readers and automatically falls back to the other verified slot if the selected one is incomplete or corrupt. The prior slot is retained until the next successful startup; legacy direct trees are read unchanged and migrate on their next automatic save. Plug-in commits such as FTP site-bookmark edits now invoke the complete host transaction rather than writing their private subkey in place.

### 25. Version and validate the complete configuration schema — Implemented (2026-08-09)

- **Justification:** A large evolving setting surface makes partial, malformed, or future-version data a stability risk. Individual defaulting does not prove cross-field invariants.
- **Implementation (2026-08-09):** Each committed host snapshot now carries `Configuration Schema Version` and is accepted only after an explicit, idempotent schema migration and complete-profile validation. The gate rejects future schemas/configurations, missing required sections, invalid window geometry, out-of-range display/viewer values, and incompatible visible/separated drive masks before `LoadConfig` can expose settings to global state. Invalid active generations fall back to the last verified generation; if neither generation validates, the default profile is used and startup displays a diagnostic. The schema version is excluded from the snapshot checksum solely so pre-schema transactional snapshots can receive the metadata-only v0-to-v1 migration without rewriting user data; all actual settings remain checksum-protected.

### 26. Test backup restoration and interrupted configuration writes — Implemented (2026-08-09)

- **Justification:** Existing backup behavior is valuable but is only resilient if restoration works after failure at every write boundary.
- **Implementation (2026-08-09):** The registry-worker boundary now has an isolated-profile-only crash hook scoped to `SaveConfig`. The `FaultInjection` executable test first measures the exact mutation count for a complete configuration commit, then terminates a fresh process after every successful registry mutation, including generation cleanup, staging, completion markers, and both flush/selector commit boundaries. It restarts the executable after each interruption and accepts only the complete baseline profile or complete candidate profile; any mixed, defaulted, or unvalidated state fails the test. The opt-in lane is documented in `tests/FileManager.UiTests/README.md` because it deliberately launches and terminates the executable once per write boundary.

### 27. Extract a testable file-operation planning seam — Implemented

- **Justification:** Planning, prompting, progress, execution, and Win32 I/O are intertwined across very large source files, making the most dangerous logic difficult to characterize without UI automation.
- **Proposed solution:** Introduce a pure operation-plan model and a narrow filesystem adapter while preserving behavior. Golden-master the generated plans before changing execution semantics.
- **Implementation (2026-08-09):** `COperationPlan` now deep-captures every generated script instruction and its operands before the worker begins; it owns no dialog, progress, worker, or Win32 I/O state. `CFileOperationFileSystem` isolates the planning-time attribute and free-space facts behind a replaceable read-only adapter, while the production implementation retains the existing Win32-backed helpers. The durable operation journal persists the immutable `PLAN|1`/`PLANITEM` snapshot before item preparation, and the executable copy scenario asserts its exact source/target intent as the golden-master contract. Execution continues to interpret the existing `COperations` script unchanged.

### 28. Build native characterization tests for copy, move, delete, and rename — Implemented

- **Justification:** The current UI suite does not cover core destructive operations, so regressions in conflict handling, metadata, cancellation, and rollback can escape.
- **Proposed solution:** Add native integration tests using disposable directories and volumes. Cover overwrite choices, skip/all choices, same- and cross-volume moves, recycle-bin behavior, cancellation, and restart reconciliation.

- **Delivered (2026-08-09):** The executable-level NUnit/FlaUI suite now characterizes native conflict decisions (`Yes`, `All`, `Skip`, and `Skip All`), same-volume copy/move/delete/rename metadata and cancellation behavior, and the durable cancellation journal. `CrossVolumeMoveCharacterizationUiTests` uses an explicitly supplied disposable second-volume root and verifies that a tree reaches its target before the source disappears. `OperationRecoveryCharacterizationUiTests` seeds a ready transactional sibling target and verifies the real startup recovery prompt commits it and marks the journal reconciled. The recycle-bin test uses `SHQueryRecycleBin` around a real delete and remains opt-in because it intentionally changes the isolated profile's shell recycle-bin contents.

### 29. Add crash-consistency fault injection at every operation phase — Implemented (2026-08-09)

- **Delivered:** `COperationExecutionFileSystem` is an execution-only, replaceable Win32 adapter. Native tests can install a deterministic fake with `SetOperationExecutionFileSystemForTests` and fail the exact create, write, metadata (`SetFileTime`), flush, replace/rename, or identity-guarded source-delete call without changing the operation plan, worker, or UI flow.
- **Crash-consistency boundary:** Transactional copy routes sibling-target creation, synchronous and overlapped writes, metadata persistence, `FlushFileBuffers`, `ReplaceFileW`/write-through rename, and the `SetFileInformationByHandle` source deletion through that adapter. The existing append-only journal is written before an item begins and marks a fully flushed temporary target ready before replacement, so every injected failure leaves the original, the complete replacement, or durable recovery facts.
- **Regression coverage:** `NativeSafetyRegressionTests.Transactional_copy_and_move_expose_each_durable_phase_to_a_deterministic_fault_adapter` locks the seam and every call-site boundary in place, including the durable journal transitions.

### 30. Implemented: add executable-level file-operation scenarios

- **Delivered:** `tests/FileManager.UiTests/FileOperationUiTests.cs` launches the real executable with fresh source and target panel paths per case. It verifies nested directory creation; file and directory-tree copy, rename, move, and delete; cancelled operation dialogs; invalid destinations/names; and a locked-file delete failure. Assertions read the disposable filesystem directly, and fixture teardown terminates only the launched executable before deleting the workspace.
- **Remaining scope:** Native fault injection, controlled mid-copy cancellation, cross-volume behavior, restart reconciliation, and metadata fidelity belong to improvements 28, 29, 31-33, and 85.

### 31. Publish an explicit metadata preservation contract — Implemented

- **Delivered:** `architecture.md` defines required, best-effort, and unsupported metadata for NTFS, ReFS, FAT-family, and SMB copy and move operations. `CMetadataPreservationContract` mirrors that taxonomy in the native worker, while the planner records accepted known ADS/ACL losses and the copy engine records actual timestamp, attribute, ACL, ADS, and compression/EFS losses.
- **Source-deletion gate:** Cross-volume moves now aggregate unacknowledged losses in `CProgressDlgData::MetadataLosses` and display them immediately before every source-file or source-directory deletion. The default response retains the source and changes the remaining move into a copy; only an explicit confirmation permits deletion.
- **Verification:** `NativeSafetyRegressionTests.Metadata_preservation_contract_records_losses_and_gates_move_source_deletion` checks the contract, planner records, worker gate, localized prompt, and refactoring status.

### 32. Test alternate data streams end to end — Implemented

- **Justification:** ADS handling has specialized buffer and retry paths in `src/async_copy.cpp`; uncommon branches are high-regression territory and can silently lose content.
- **Resolution:** Added executable-level ADS characterization tests covering same-volume copy of named, empty, large, and edge-named streams; overwrite replacement and stale-stream removal; retry after a temporarily denied stream; cross-volume preservation on ADS-capable targets; and source retention when an ADS-unsupported target reports metadata loss. ADS-dependent lanes self-skip when their explicitly configured filesystem capability is unavailable.

### 33. Verify ACL and ownership preservation under privilege variation — Implemented

- **Delivered:** `DoCopySecurity` now snapshots both descriptors, uses a complete owner/group/DACL update only when `SeRestorePrivilege` is available, and otherwise changes only a DACL whose owner and group already match. It verifies owner, group, DACL protection/inheritance, and the complete multiset of explicit ACEs, including deny ACEs. A failed verification restores the prior descriptor (or prior DACL in the no-privilege branch) rather than accepting a partial security update.
- **Contract behavior:** `architecture.md` publishes the NTFS/ReFS/SMB and FAT-family privilege matrix. Unreadable descriptors and unavailable privilege do not mutate the target; they take the existing best-effort permission-warning path and are recorded as `mmlSecurity`, so a cross-volume move retains its source unless the user explicitly accepts the loss.
- **Verification:** `NativeSafetyRegressionTests.Security_descriptor_copy_uses_the_privilege_aware_preservation_matrix` covers the source-level guard, post-write verification, rollback, explicit-ACE/inheritance checks, inaccessible-descriptor behavior, published matrix, and implementation status.

### 34. Exercise junction, symlink, mount-point, and cloud-placeholder cases — Implemented (2026-08-09)

- **Implementation:** `BuildScriptDir` now makes every directory reparse point a hard planning boundary. Copy, move, count, convert, and recursive attribute work skip it rather than enumerating its target; reparse files are also skipped before a planner read can hydrate a cloud placeholder. This deliberately replaces the legacy “copy link target content” route. The link itself is not recreated by copy/move until that behavior can be implemented as a separate handle-first feature.
- **Deletion policy:** The existing identity-checked `FILE_FLAG_OPEN_REPARSE_POINT` delete path continues to target the link itself. `DoDeleteDirLinkAux` accepts only mount-point/junction and symbolic-link tags; unknown tags fail with `ERROR_REPARSE_TAG_MISMATCH`, never by resolving their target.
- **Verification:** `ReparsePointTopologyUiTests` constructs disposable junctions with an outside target, a changed target, and a cycle before launch. It proves copying does not materialize or traverse either target and proves deletion removes only the junction. `NativeSafetyRegressionTests.Reparse_point_policy_never_traverses_or_hydrates_unselected_targets` ratchets the source policy, placeholder handling, unknown-tag refusal, topology coverage, and documentation.

### 35. Introduce a dynamic wide-path abstraction — Implemented

- **Justification:** Core code contains thousands of `MAX_PATH` uses and repeated UTF-8-to-wide conversions into fixed buffers. Long, deeply nested, and multi-byte paths can fail or truncate unpredictably.
- **Implemented:** `CWidePath` now owns dynamically sized UTF-16 display and Win32 API spellings at the common boundary. It retains the caller's UTF-8 display string, converts strictly with the existing compatibility fallback, and only resolves an absolute path when the consuming API requires it. Long drive and UNC paths are consistently emitted as `\\?\` and `\\?\UNC\` respectively. Core file, directory, attribute, enumeration, and secure DLL-loading wrappers now consume its API spelling, while diagnostics continue to use the display spelling.
- **Verification:** `NativeSafetyRegressionTests.Win32_path_boundaries_use_owned_wide_paths_and_extended_length_syntax` guards dynamic conversion, display/API separation, dynamic full-path sizing, drive/UNC extended prefixes, and the migrated wrapper boundaries.

### 36. Ban new fixed `MAX_PATH` buffers — Implemented

- **Justification:** A broad path rewrite is risky, but allowing new fixed buffers increases the backlog and perpetuates boundary bugs.
- **Implemented:** Pull-request CI now runs `tools/verify-no-new-max-path-buffers.ps1`. It compares only native source lines added since the PR base and rejects new `char` or `WCHAR` arrays with bounds containing `MAX_PATH`; existing debt remains unchanged for incremental migration behind compatibility adapters.
- **Exemptions:** No exception is currently approved. A future exception must use a trailing `MAX_PATH-RATCHET-EXEMPT` identifier and have a matching file, reason, and removal condition in `tools/max-path-buffer-exemptions.md`; an unregistered or incomplete exemption fails CI.
- **Verification:** `NativeSafetyRegressionTests.New_fixed_max_path_buffers_are_rejected_by_the_changed_lines_ci_ratchet` guards the changed-lines scope, native-file coverage, rejection pattern, exemption contract, workflow wiring, and implementation status.

### 37. Ratchet unchecked string-copy and formatting calls — Implemented

- **Justification:** `strcpy`, `strcat`, `sprintf`, `lstrcpy`, `lstrcat`, and `wsprintf` remain common. Input length assumptions are dispersed and hard to review.
- **Implemented:** Pull-request CI now runs `tools/verify-no-new-unsafe-string-calls.ps1`, a native changed-lines ratchet for the listed unchecked APIs. `CopyStringChecked`, `FormatStringChecked`, and `ConvertWideToUtf8Checked` return explicit success, truncation, invalid-argument, or encoding-error status and never leave a partial result for callers to consume. Filesystem names now use strict, measured UTF-8 conversion, and startup validates the persisted language-file name while constructing the load path and its error messages.
- **Verification:** `NativeSafetyRegressionTests.Unchecked_string_calls_are_ratchet_gated_and_external_boundaries_report_capacity_and_encoding_failures` protects the CI wiring, API ban, exact-capacity terminator rule, one-over formatting rejection, measured UTF-8 expansion rule, and both migrated external-input boundaries.

### 38. Replace fixed buffers at trust boundaries first — Implemented

- **Justification:** Network responses, plug-in metadata, archive names, environment values, and crash-report fields are controlled outside the local function and therefore carry the highest overflow and truncation risk.
- **Implemented:** The crash uploader's HTTP response reader already keeps responses in a bounded dynamic `std::string`; FTP control replies now retain their dynamic read buffer only up to 64 KiB and fail the connection with `WSAEMSGSIZE` when a server has not completed a reply within that limit. Configuration-fault environment controls now query their required size and retain an owned `std::string` only up to the documented 32,767-character environment limit, so a changing or oversized value is never silently truncated. Crash-reporter shared-memory fields and module/dump paths are copied into bounded dynamic strings before they are parsed or composed. The fixed shared-memory and UI error arrays remain only compatibility boundaries: unterminated, oversized, or non-representable fields produce explicit `ERROR_INVALID_DATA` or `ERROR_INSUFFICIENT_BUFFER` failures instead of partial values.
- **Verification:** `NativeSafetyRegressionTests.Trust_boundary_text_uses_bounded_owned_storage_and_explicit_capacity_failures` guards the HTTP, FTP, environment, and crash-reporter limits; owned storage; shared-memory terminator checks; compatibility-only report-name API; and removal of the old fixed dump/path assembly.

### 39. Use checked arithmetic for sizes, offsets, and allocations — Implemented (2026-08-09)

- **Delivered:** `src/common/checked_arithmetic.h` supplies checked add, multiply, and cast helpers for `uint64_t`, `size_t`, and Win32 `DWORD`. The crash uploader now validates network response, multipart, file-stream, and legacy parser lengths before combining or narrowing them. The parser broker validates external path and thumbnail dimensions/byte counts on both sides of its IPC boundary before they control a buffer or I/O request.
- **Verification:** `NativeSafetyRegressionTests.External_size_fields_use_checked_arithmetic_before_allocation_or_io` protects the helpers and every current external uploader and parser-broker size field from reverting to unchecked arithmetic.

### 40. Make operation result types explicit — Implemented

- **Justification:** Many paths combine `BOOL`, mutable out parameters, `GetLastError`, and log-only secondary failures. This makes it easy to lose the original cause or treat partial completion as success.
- **Proposed solution:** Introduce a lightweight result type carrying phase, Win32/HRESULT code, source, destination, retryability, and partial-effect flags. Adapt it back to existing dialogs until callers migrate.
- **Resolution:** `COperationResult` now carries the complete outcome for durable copy verification and transactional overwrite commits. `ToLegacyBool` supplies the existing progress-dialog `BOOL`/Win32-error contract while further callers migrate.
- **Verification:** `NativeSafetyRegressionTests.Transactional_copy_results_preserve_phase_error_paths_retryability_and_partial_effects_for_legacy_dialogs` checks the result contract, transactional call sites, compatibility adapter, and implementation ledger.

### 41. Adopt RAII for kernel handles in touched code — Implemented

- **Implementation (2026-08-09):** `CScopedKernelHandle` is the small project wrapper for newly touched native code. It owns only valid kernel handles, preserves the existing `HANDLES` debug accounting on close, restores the caller's `GetLastError` during destructor cleanup, and requires an explicit `Reset` or `Release` for ownership replacement or transfer. The handle-identity capture and verified-delete boundary now use it, removing their raw `CloseHandle` cleanup paths while retaining the established close-failure result contract.
- **Verification:** `NativeSafetyRegressionTests.Kernel_handle_ownership_is_scoped_and_preserves_legacy_close_failures` guards the non-copyable owner, explicit transfer operations, debug tracking, last-error preservation, migrated identity/delete paths, and this ledger entry.

### 42. Adopt RAII for memory, mappings, and critical sections — Implemented

- **Implementation (2026-08-09):** `CScopedHeapBuffer`, `CScopedMappingView`, and `CScopedCriticalSection` are small non-copyable guards that preserve the existing allocator and Win32 ABI contracts. File-operation tail verification now owns its two scratch buffers through scope, the automation plug-in owns its mapped script view through all conversion exits, and parser-broker requests hold their critical section through scope.
- **Compatibility:** Returned automation script text retains its existing `FreeOleString` ownership contract; only local temporary resources are scoped. Guard destructors preserve `GetLastError` so existing caller result paths remain authoritative.
- **Verification:** `NativeSafetyRegressionTests.Scoped_native_resources_protect_file_operations_and_plugin_boundaries` checks the non-copyable guards, cleanup behavior, migrated memory/mapping/lock seams, and this ledger entry.

### 43. Standardize thread creation and ownership — Implemented (2026-08-09)

- **Justification:** Dozens of raw `CreateThread` calls distribute handle ownership, parameter lifetime, COM initialization, exception policy, and naming across the codebase.
- **Implementation (2026-08-09):** `CThreadOwner` is the common CRT-backed worker boundary. It owns the thread, manual-reset stop event, completion event, and copied launch record; names each worker, optionally establishes and balances a declared COM apartment, contains C++ exceptions, and always signals completion after normal callback execution. The check-path workers now use it end-to-end, including bounded shutdown diagnostics followed by a safe join. `tools/verify-no-new-raw-thread-creation.ps1`, run in pull-request CI, ratchets all newly changed first-party thread starts onto this boundary while legacy call sites are migrated by their owning subsystem work.

### 44. Define bounded shutdown deadlines without unsafe escalation — Implemented (2026-08-10)

- **Justification:** One-second waits followed by thread killing are arbitrary, while infinite waits can hang shutdown forever. Neither behavior explains what the worker is doing.
- **Implementation (2026-08-10):** `CThreadShutdownDeadline` gives every migrated worker a five-second cancellation phase and a thirty-second operation-recovery phase. A breach logs the named worker, phase, duration, and live thread state before the owner performs its mandatory safe join. The check-path, cache, panel-icon, directory-snooper, safe-handle-closer, and call-stack report workers now use that contract; the legacy auxiliary-worker drain also records a component label for each handle and applies it after each subsystem has requested closure.
- **Safety:** No migrated worker is detached: each can still observe panel, global, synchronization, or crash-recovery state. The process therefore remains alive until its worker joins and only then releases that state, rather than using `TerminateThread` or closing objects still in use.
- **Verification:** `NativeSafetyRegressionTests.Shutdown_deadlines_report_named_phases_and_preserve_shared_state_until_safe_join` guards the two deadlines, diagnostics, mandatory join, named auxiliary tracking, removal of the former forced termination paths, and this ledger entry.

### 45. Replace wrap-prone time calculations with monotonic 64-bit time — Implemented

- **Justification:** `GetTickCount` is used hundreds of times; its 32-bit wrap and ad hoc subtraction can break timeouts and throttles after long uptime.
- **Proposed solution:** Centralize monotonic timing on `GetTickCount64` or `QueryUnbiasedInterruptTime`, use duration types, and add wrap/clock-jump tests for remaining compatibility code.
- **Implementation:** `CMonotonicClock` supplies named 64-bit monotonic time points and durations from `GetTickCount64`, including elapsed/deadline helpers and the one saturating conversion required by Win32's `DWORD` timer API. The host-owned plug-in filesystem timer queue now stores and sorts those deadlines directly, so callback scheduling no longer depends on signed 32-bit wrap arithmetic.
- **Compatibility:** Existing `GetTickCount` values that are part of Win32 message fields or name-generation entropy remain bounded compatibility values; they are not used by the migrated timer queue for timeout decisions.
- **Verification:** `NativeSafetyRegressionTests.Monotonic_64_bit_timers_cross_the_32_bit_boundary_and_reject_backward_samples` exercises the former wrap boundary and a synthetic backward sample, and pins the native queue to the centralized clock seam.

### 46. Implemented: replace `Sleep` polling with signaled waits

- **Justification:** Fixed-delay polling delayed cancellation, wasted time, and made check-path completion tests schedule-sensitive. The shutdown path has already been migrated to cooperative safe joins; this change removes the remaining check-path polling waits.
- **Implementation (2026-08-10):** The reusable check-path worker now waits on its work and owner-cancellation events together, so shutdown wakes the idle thread without a synthetic request. Slot exhaustion waits for actual worker-completion events instead of polling every 100 ms. The grace and retry delays are waitable-timer deadline handles with explicit work-completed versus deadline-elapsed outcomes; no `Sleep` polling remains in `src/path_checking.cpp`.
- **Verification:** `NativeSafetyRegressionTests.Check_path_workers_use_signaled_work_cancellation_and_deadline_waits` guards the multi-handle cancellation wait, completion-driven slot handoff, waitable deadline protocol, and removal of `Sleep` from the check-path implementation.

### 47. Document and verify lock ordering — Implemented

- **Justification:** Thousands of critical-section enter/leave sites and cross-thread UI calls make lock inversion difficult to reason about.
- **Implementation (2026-08-10):** `CLockRank` defines the process-lifetime through external-broker order, and ranked `CScopedCriticalSection` acquisitions delegate to `LockOrderEnter`/`LockOrderLeave`. Debug builds retain a per-thread acquisition stack, assert on a non-recursive lower/equal rank, and, after ten seconds of contention, emit the requested lock, waiter thread, owner, recursion count, and held rank before preserving the existing blocking behavior. `CParserBrokerClient::Lock` is the first migration point at `lkrExternalBroker`; the architecture guide defines the rank families and explicitly prohibits holding a ranked lock across synchronous UI calls.
- **Verification:** `NativeSafetyRegressionTests.Lock_ordering_has_rank_assertions_timeout_diagnostics_and_a_nightly_verifier_lane` guards the rank API, inversion assertion, timeout diagnostics, parser-broker migration, documentation, and delivery workflow. `nightly-lock-stress.yml` runs the repeated isolated-profile UI lifecycle scenarios with Application Verifier's `Locks` layer and always clears its process-persistent configuration afterward.

### 48. Reduce unowned global mutable state — Implemented

- **Justification:** Globals such as plug-in entry state and worker flags obscure thread affinity and lifetime. A shutdown or re-entrant callback can mutate them from an unexpected context.
- **Implementation (2026-08-10):** The plug-in callback nesting counter and transition lock now live in `CPluginCallbackState`, owned for the plug-in subsystem lifetime by `CPlugins`. The main application thread owns construction and teardown; callback threads may enter, leave, and query the state through its atomic depth and SRW-locked transitions. Legacy `EnterPlugin`/`LeavePlugin`/`IsInPlugin` functions remain compatibility delegates, while the loader's `CPluginEntryScope` receives only `CPlugins::GetCallbackState()` so its exception cleanup does not depend on translation-unit global state. Further worker-state migrations remain deliberately subsystem-scoped.
- **Verification:** `NativeSafetyRegressionTests.Plugin_entry_scope_and_callback_state_restore_host_state_after_an_unwinding_entry_point` guards ownership by `CPlugins`, atomic/locked synchronization, removal of the former globals, narrow scope injection, compatibility delegates, and this implementation ledger.

### 49. Protect window and callback lifetimes — Implemented

- **Justification:** Background workers retain HWNDs and pointers while dialogs and panels can close. Posting or sending after destruction risks reuse of stale window handles or memory.
- **Implementation (2026-08-10):** `CDeleteManager` now owns a lock-protected callback registration instead of reading `MainWindow->HWindow` from worker threads. Main-window creation registers the target and a nonzero generation; each worker notification carries that generation. `WM_USER_PROCESSDELETEMAN` processes only the current `(HWND, generation)` pair, and `WM_DESTROY` invalidates the registration before child teardown, so a late post cannot activate a recycled window handle. Work queued before registration is notified when the window registers, while failed or invalidated registrations are discarded safely.
- **Verification:** `NativeSafetyRegressionTests.Delete_manager_callback_registration_invalidates_before_window_teardown_and_rejects_stale_generations` asserts the guarded registration, generated worker post, dispatch check, destruction ordering, and implementation ledger.

### 50. Bound background work queues — Implemented (2026-08-10)

- **Justification:** Icon, thumbnail, directory, and plug-in work can grow with directory size or slow consumers, increasing memory pressure and shutdown latency.
- **Implementation (2026-08-10):** `CIconThreadPool` now keeps its icon-provider requests in 64 fixed slots instead of an unbounded producer backlog. It coalesces matching type/path/index/size requests within a listing generation, chooses visible-panel work before background warming, and lets visible work evict only dormant background work when the capacity is reached. `BeginGeneration` and `CancelObsoleteGenerations` discard queued work from superseded panel listings while active providers remain cooperatively cancellable and retain their fixed slot until they return. The same generation/metrics contract is the required boundary for the remaining thumbnail, directory, and plug-in producers as they are moved onto the shared queue.
- **Backpressure and shutdown:** `CIconQueueMetrics` exposes capacity, queued and active work, submitted/completed/coalesced/cancelled counts, rejected submissions, visible preemptions, and high-water mark. Completion is event-driven rather than a polling sleep, and shutdown cancels dormant work before the safe worker joins so queue storage cannot be released while a provider still uses it.
- **Follow-through (2026-08-10):** Find now caps retained results at 100,000 and deferred directories at 4,096; when the directory budget fills it continues depth-first, while an over-limit result set stops with an explicit diagnostic rather than silently dropping matches. Parser broker admission is capped at eight outstanding calls, with accepted/rejected/high-water metrics. FTP keeps its durable user intent but caps operation expansion at 100,000 items and local-disk work at 512, returning an explicit failure to the existing operation error path instead of discarding queued transfers; both expose queue metrics. The directory snooper already waits for each refresh acknowledgement, so its effective outstanding capacity is one; it now reports posted, awaited, and suspend-batched refresh counts.
- **Verification:** `NativeSafetyRegressionTests.Icon_work_pool_bounds_memory_and_prioritizes_visible_current_generation_work` guards the fixed capacity, request identity, visible priority/preemption rule, generation cancellation, metrics, event-driven completion, removal of circular-tail races/polling, and this implementation ledger.

### Important: resource limits, diagnostics, network, and dependencies

### 51. Set resource budgets for directory enumeration — Implemented (2026-08-10)

- **Justification:** Very large directories and slow shares can monopolize memory/UI update traffic even when each individual call succeeds.
- **Implementation (2026-08-10):** Disk-panel enumeration now uses fixed 256-entry checkpoints. Each checkpoint yields to the safe-wait window's cancellation thread and probes cancellation, while the existing time-based probe remains for high-latency individual Win32 calls. The list control remains count-based: it receives one final item count and invalidates only its visible-item arrays instead of accepting a per-entry UI-insertion message stream. The panel retains at most 100,000 file/directory metadata records (with one reserved parent-directory record); on reaching that ceiling it stops enumeration, preserves the usable partial listing, and explains that the path or filter must be refined.
- **Verification:** `NativeSafetyRegressionTests.Directory_listing_uses_bounded_checkpoints_and_a_retained_metadata_budget` simulates one million entries to pin the checkpoint cadence and guards the native batch boundary, cancellation probe, metadata ceiling, count-based list virtualization, user-facing limit text, and this implementation ledger. High-latency shares remain covered by the independent 200 ms cancellation probe between `FindNextFileW` returns.

### 52. Reserve memory for graceful out-of-memory handling — Implemented

- **Justification:** When allocation fails, even constructing an error or journal record may fail, leaving no safe way to cancel an operation.
- **Proposed solution:** Allocate a small emergency reserve at startup, release it on first OOM, stop accepting work, persist minimal recovery state, and offer a controlled exit.
- **Implementation (2026-08-10):** The allocation handler precommits a 64 KiB process-heap reserve. Its first failure atomically releases that reserve, then its pre-registered UI notification persists an append-only `memory-pressure` operation-recovery marker with fixed buffers and write-through I/O, rejects new queued or starting file operations, and begins controlled shutdown. Existing operations retain their individual journals for normal reconciliation.
- **Verification:** `NativeSafetyRegressionTests.Allocation_emergency_is_noninteractive_and_defers_recovery_to_the_ui_thread` pins reserve ownership, one-time activation, recovery persistence, shutdown handoff, operation admission guards, and this ledger entry without trying to exhaust the test machine's memory.

### 53. Remove modal UI and retry loops from the global allocation handler — Implemented (2026-08-10)

- **Justification:** `src/common/allochan.cpp` serializes allocation failure handling and can display message boxes or sleep while the failing thread holds unrelated locks, creating deadlock and re-entrancy risks.
- **Proposed solution:** Make the handler allocation-free and non-interactive: release the reserve, set an atomic fatal-pressure state, and notify the UI through a preallocated channel. Never loop indefinitely inside allocator recovery.
- **Implementation:** `TaskscapeLtdNewHandler` now releases the 64 KiB reserve once, records fatal pressure atomically, and immediately returns failure. It no longer serializes callers, formats diagnostics, displays modal UI, sleeps, retries, or invokes recovery callbacks. The pre-registered `WM_USER_ALLOCATION_EMERGENCY` channel transfers journal persistence and the existing controlled-close request to the main UI thread.
- **Verification:** `NativeSafetyRegressionTests.Allocation_emergency_is_noninteractive_and_defers_recovery_to_the_ui_thread` protects the reserve/state transition and window handoff while rejecting modal dialogs, sleeps, allocator-thread locking, and recovery callbacks.

### 54. Add a bounded release-build diagnostic ring buffer — Implemented

- **Justification:** Much operational logging is debug-only, so field deadlocks and intermittent I/O failures lack the event sequence needed for diagnosis.
- **Proposed solution:** Keep a low-overhead in-memory ring of sanitized operation transitions, waits, retries, and plug-in identities. Attach it to user-approved reports and allow local export.
- **Implementation:** `release_diagnostics.cpp` provides a 128-entry, allocation-free ring with per-entry publication markers. Worker lifecycle, startup waits, copy retries, and plug-in DLL leaf names record only sanitized labels. The crash text report renders the ring before `End.` and writes a local `.OPS` sidecar, which Salmon packages/uploads only after the user selects **Send Report**.
- **Verification:** `NativeSafetyRegressionTests.Release_diagnostic_ring_is_bounded_sanitized_and_reported_only_through_the_existing_consent_flow` pins the capacity, publication protocol, safe fields, producer coverage, report attachment, local-view documentation, project registration, and implemented ledger entry.

### 55. Assign correlation IDs to operations and workers — Implemented

- **Justification:** Parallel copies, dialogs, callbacks, and retries otherwise produce ambiguous traces, especially after a crash or cancellation race.
- **Proposed solution:** Generate an operation ID at command dispatch and propagate it through plan, worker, UI, journal, and log records. Include item sequence and attempt number.

- **Implementation:** `COperations` creates an immutable process/tick/dispatch-sequence ID. Plans, worker startup/completion, visible progress-dialog titles, journals, and debug execution-log records retain that ID. The journal and logs also record each item sequence and initial or retried attempt; synchronous retry dialogs and automatic retry paths advance the attempt.
- **Verification:** `FileOperationUiTests.Copy_file_persists_a_completed_recovery_journal_with_item_intent` checks durable plan/item correlation, and `NativeSafetyRegressionTests.File_operation_correlation_ids_cross_plan_worker_ui_journal_and_log_boundaries` pins every handoff.

### 56. Preserve the first actionable error and its context — Implemented (2026-08-10)

- **Justification:** Repeated cleanup calls can overwrite `GetLastError`, while log-only failures lose the operation phase and affected path.
- **Proposed solution:** Capture errors immediately into the explicit result type, append cleanup errors without replacing the primary cause, and present a copyable diagnostic summary.
- **Implementation (2026-08-10):** `COperationResult` now retains the initial phase/error/path outcome and records up to two named cleanup failures as secondary evidence. Durable-copy verification captures its result before closing the target handle, so a close failure cannot replace an earlier metadata or size failure; retry cleanup likewise records a failed deletion of an unverified target. The existing progress dialog now renders a fixed-buffer phase/error/source/destination/effects summary after the localized error text; users can copy the whole message with its established Ctrl+C behavior.
- **Verification:** `NativeSafetyRegressionTests.File_operation_failures_capture_the_primary_error_before_cleanup_and_offer_copyable_context` pins the cleanup phases, bounded append-only evidence, capture-before-close ordering, retry-cleanup recording, copyable dialog text, and this implementation ledger entry.

### 57. Centralize retry policy — Implemented (2026-08-10)

- **Justification:** Ad hoc retry prompts and delays can repeat permanent failures, overload network shares, or duplicate side effects.
- **Proposed solution:** Classify transient Win32/network errors, use capped exponential backoff with jitter, honor cancellation, and never automatically retry a destructive commit unless idempotency is proven.

- **Implementation (2026-08-10):** `retry_policy.h` is now the shared authority for transient Win32/network classification, a maximum of three exponential retries (100 ms, 200 ms, 400 ms before bounded jitter), and cancellation-event waits. Synchronous and asynchronous source-read recovery use it; move and directory-delete commit paths are classified as destructive, so the policy rejects automatic retries and leaves the existing Retry/Skip/Cancel prompt as the decision boundary. `COperationResult` uses the same classification for its retryability field.
- **Verification:** `NativeSafetyRegressionTests.Central_retry_policy_bounds_transient_read_retries_and_blocks_destructive_commits` pins the common classification, cap, jittered cancellation-aware delay, copy call sites, destructive classifications, absence of the former ad hoc sleeps, and this ledger entry.

### 58. Apply deadlines and cancellation to all network operations — Implemented (2026-08-10)

- **Justification:** FTP, update, and report paths can block on DNS, connect, TLS, send, or receive while shutdown waits on their threads.
- **Proposed solution:** Define phase-specific timeouts, propagate one cancellation token, close sockets/requests to interrupt blocking I/O, and distinguish timeout from authentication or protocol failure.

- **Implementation (2026-08-10):** CheckVer now applies 15-second DNS/connect and send deadlines plus a 30-second receive deadline to its WinINet session. Its single cancellation token closes the currently active session or URL handle, so dismissing the dialog or unloading the plug-in interrupts a blocked network phase instead of merely detaching its thread. The existing nonblocking FTP transport now names its DNS, TCP-connect, and FTP/TLS protocol deadlines separately, bounds the temporary blocking SChannel handshake with socket send/receive deadlines, and retains its established socket-close cancellation path plus distinct resolve/connect/reply errors. Salmon retains its bounded WinHTTP phases, now registers both the session and request against the upload token, closes both on cancellation, and reports timeout, authentication, and protocol/TLS failures distinctly.
- **Verification:** `NativeSafetyRegressionTests.Network_operations_have_phase_deadlines_cancellation_and_failure_classification` pins the timeout settings, cancellation-handle closure, FTP phase boundaries, failure classification, and this implementation ledger entry.

### 59. Make FTP transfers transactional and resumable safely — Implemented (2026-08-10)

- **Justification:** Network interruption and resume logic can leave a destination that looks complete but contains stale or duplicated bytes.
- **Proposed solution:** Download to a side file with persisted remote identity/size/time, validate resume offsets and server responses, flush and atomically rename on success, and keep incomplete files clearly marked.

- **Implementation:** Viewer/cache downloads now write to a sibling `.ftp-incomplete` file and a write-through `.meta` sidecar that records the remote host, path, name, byte size, and available last-write time. Only an exact metadata match, a binary transfer, and a staged length no greater than the remote size can issue `REST`; a refused response discards that prefix rather than appending to it. The staged file is flushed, checked against the expected size, and promoted with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` only after the final FTP success response and data flush. Interrupted or validation-failed data stay marked as incomplete and the visible cache name is untouched.
- **Verification:** `NativeSafetyRegressionTests.Ftp_downloads_stage_identity_validate_resume_and_publish_only_after_a_durable_commit` guards the persisted identity, binary-only resume offset check, `REST` response gate, append offset, size verification, write-through rename, incomplete suffix, and this ledger entry.

### 60. Strengthen FTP certificate exception storage

- **Justification:** Certificate checking exists, but resilience depends on binding any user exception to the intended host and certificate lifecycle rather than a broadly reusable approval.
- **Proposed solution:** Store hostname, port, SPKI/certificate fingerprint, decision scope, and expiry; warn on change; and test hostname mismatch, expiry, chain failure, and renewed certificates.

### 61. Upgrade the bundled 7-Zip code

- **Justification:** The vendored 7-Zip version is 16.04, leaving years of parser, format, and robustness fixes unapplied in a component that handles untrusted archives.
- **Proposed solution:** Move through supported releases with corpus differential tests, fuzz regression cases, and extraction compatibility snapshots before enabling the new version by default.

### 62. Upgrade SQLite and define database recovery behavior

- **Justification:** The vendored SQLite reports 3.28.0. Old storage code misses later correctness, corruption-detection, and platform fixes.
- **Proposed solution:** Upgrade to a current supported amalgamation, record compile options, enable integrity checks for owned databases, use WAL/transaction settings intentionally, and test interrupted writes and corrupt pages.

### 63. Upgrade zlib

- **Justification:** Vendored zlib 1.2.11 is obsolete and used on untrusted compressed data paths.
- **Proposed solution:** Upgrade to a supported version, run compression/decompression compatibility vectors, retain corrupt-input regression files, and include it in the dependency update cadence.

### 64. Upgrade bzip2

- **Justification:** Vendored bzip2 1.0.6 dates from 2010 (`src/common/dep/bzip2/decompress.c:11`), increasing maintenance and parser risk.
- **Proposed solution:** Update to the current maintained release or replace it behind the archive adapter, then run golden archives, truncation cases, and fuzz corpus replay.

### 65. Upgrade cmark-gfm and harden rendered-content defaults

- **Justification:** The bundled cmark-gfm 0.29.0.gfm.0 is old, and Markdown may include adversarial links, nesting, or large inputs.
- **Proposed solution:** Upgrade behind snapshot tests, enable safe rendering defaults, cap input/tree/output sizes, and fuzz extension combinations used by the application.

### 66. Maintain a machine-readable dependency inventory and SBOM

- **Justification:** Vendored source and downloaded binaries are otherwise easy to overlook, making emergency upgrade and release impact analysis slow.
- **Proposed solution:** Record component, version, source, license, hash, patches, owner, and end-of-support date; generate CycloneDX/SPDX SBOMs for each installer and diff them in review.

### 67. Fuzz all untrusted parsers continuously

- **Justification:** Archive, image, database, Markdown, FTP listing, and plug-in metadata parsers process attacker-controlled bytes, yet no fuzz harnesses were found.
- **Proposed solution:** Add libFuzzer/OneFuzz-compatible harnesses at memory-buffer seams, seed them with existing samples, retain every crash as a regression, and run short PR plus long scheduled campaigns.

### 68. Enforce extraction-root containment

- **Justification:** Archive entries can contain absolute paths, drive prefixes, traversal segments, alternate separators, or reparse interactions that escape the chosen destination.
- **Proposed solution:** Canonicalize each candidate under an opened destination root, reject escape or device names, create directories handle-first where practical, and revalidate before writing.

### 69. Defend against archive and decompression bombs

- **Justification:** Small hostile inputs can request enormous output, file counts, nesting, or CPU time and destabilize the whole application.
- **Proposed solution:** Enforce configurable limits on expanded bytes, compression ratio, entry count, nesting, path length, memory, and elapsed time; require explicit user override with projected impact.

### 70. Validate every plug-in contract result

- **Justification:** In-process plug-ins can return null, malformed strings, invalid counts, stale handles, or structures from a mismatched SDK version.
- **Proposed solution:** Centralize SDK boundary validation, require structure sizes/version negotiation, cap counts and strings, copy untrusted data into host-owned storage, and disable the plug-in on contract violation.

### 71. Add integer-overflow guards at parser boundaries

- **Justification:** Entry counts, dimensions, offsets, and compressed sizes often feed multiplication and pointer arithmetic before allocation.
- **Proposed solution:** Use checked numeric helpers, validate ranges before narrowing, and make sanitizer/fuzzer tests cover maximum, wraparound, negative, and overlapping offsets.

### 72. Raise compiler warnings incrementally to `/W4`

- **Justification:** Shared properties currently specify warning level 3 (`src/vcxproj/sal_base.props:19`). Important conversion, shadowing, initialization, and API-misuse signals may remain hidden.
- **Proposed solution:** Capture a reviewed baseline per project, enable `/W4` for new/touched code immediately, and burn down warnings by subsystem without blanket suppressions.

### 73. Add MSVC `/analyze` as a required changed-code lane

- **Justification:** Static analysis can find invalid handle use, buffer contracts, null dereferences, and lock errors that ordinary compilation misses.
- **Proposed solution:** Run `/analyze` on core operations and common libraries first, baseline known findings with owner/reason/expiry, and fail on new high-confidence diagnostics.

### 74. Add an x64 AddressSanitizer test build

- **Justification:** Raw memory ownership and complex parsers make use-after-free, overflow, and double-free defects likely to evade UI smoke tests.
- **Proposed solution:** Build supported projects with MSVC ASan, run native characterization tests and parser corpora, archive symbolized reports, and suppress only proven third-party noise.

### 75. Enable modern binary hardening flags

- **Justification:** The common properties do not establish a visible baseline for Control Flow Guard, SDL checks, Spectre mitigation, or current platform mitigations.
- **Proposed solution:** Audit every Release PE for ASLR, DEP, CFG, CET compatibility, high-entropy VA, and safe exception handling; enable supported flags centrally and document justified exceptions.

### 76. Add a clang-cl compatibility build

- **Justification:** A second compiler catches undefined behavior, non-portable assumptions, and diagnostics hidden by MSVC-specific behavior.
- **Proposed solution:** Build the core and selected plug-ins with clang-cl in a non-release lane, then ratchet coverage as incompatibilities are fixed.

### 77. Add CodeQL or equivalent semantic analysis

- **Justification:** Cross-function data-flow issues in path handling, allocation sizes, and command execution are difficult to detect with compiler warnings alone.
- **Proposed solution:** Run C/C++ CodeQL on pull requests and schedules, tune queries for unsafe path construction and external input, and require triage of high-severity results.

### 78. Fail CI on newly introduced unsafe APIs

- **Justification:** Secure-CRT warnings are disabled in `src/vcxproj/sal_base.props:16`, so modernization can regress even while old debt is being reduced.
- **Proposed solution:** Maintain a generated baseline of unsafe-call sites and reject additions in changed code. Allow narrow expiring exemptions that state the proven buffer invariant.

### 79. Build Release Win32 and x64 on pull requests

- **Justification:** PR CI covers only Debug Win32/x64 (`.github/workflows/pr-msbuild.yml:24-39`), while optimization, conditional compilation, and linker settings can create Release-only failures.
- **Proposed solution:** Add Release matrices for all shipped architectures, cache intermediates where safe, and make the exact publish build reusable from the verified workflow.

### 80. Verify v143 and v145 toolset parity

- **Justification:** Local projects target v145 while hosted CI forcibly builds v143 (`.github/workflows/pr-msbuild.yml:41-47`). Assuming identical behavior leaves compiler/runtime drift unmeasured.
- **Proposed solution:** Add a scheduled or self-hosted v145 lane, compare warnings/tests/artifact metadata, and make one toolset the declared release authority until parity is demonstrated.

### Foundational: test depth, observability, and sustainable delivery

### 81. Run the UI suite in CI under a disposable Windows profile

- **Justification:** The suite refuses to run without `FILEMANAGER_UI_ISOLATED=1` and an interactive profile (`tests/FileManager.UiTests/README.md:5-17`), and current workflows never invoke it.
- **Proposed solution:** Provision a dedicated ephemeral runner account/VM, install the built artifact, run UIA tests with artifacts and screenshots, and destroy the profile after each job.

### 82. Replace repetition-based “100 cases” with a risk-based scenario matrix

- **Justification:** The suite now adds focused file-operation cases to the 100 parameterized launch/configuration/FTP flows. Repetition can improve flake detection but does not replace coverage of conflict dialogs, long paths, recovery, network loss, and installer lifecycle.
- **Proposed solution:** Keep a smaller repetition soak separately and make the main suite distinct: file operations, conflict dialogs, cancellation, long paths, plug-in failure, recovery, network loss, and installer lifecycle.

### 83. Add native unit tests for paths, masks, and serialization

- **Justification:** Foundational parsing and normalization helpers are reused widely but currently have no fast native regression suite.
- **Proposed solution:** Extract narrow pure seams and table-test UNC/device paths, roots, wildcards, case behavior, invalid Unicode, registry serialization, and version migrations.

### 84. Add property-based tests for path and mask invariants

- **Justification:** Handwritten edge cases rarely cover the combinatorial space of separators, dots, prefixes, casing, wildcards, and Unicode.
- **Proposed solution:** Generate valid and invalid inputs and assert round-trip, containment, idempotence, and “never escape root” properties. Persist minimized failures as examples.

### 85. Build a deterministic filesystem fault shim

- **Justification:** Real disks do not reliably reproduce sharing violations, short writes, disconnects, access changes, or failures at exact operation phases.
- **Proposed solution:** Route core calls through a thin interface in tests and script outcomes by call number/path. Keep production dispatch direct so the seam adds negligible runtime risk.

### 86. Test network behavior against local deterministic servers

- **Justification:** Public FTP/HTTP endpoints introduce flakiness and cannot precisely produce partial replies, TLS failures, stalls, or reconnect boundaries.
- **Proposed solution:** Run local FTP/FTPS/HTTP fixtures that script protocol responses, bandwidth, disconnects, and certificate states; assert cancellation, timeout, resume, and cleanup.

### 87. Add shell-extension and plug-in crash smoke tests

- **Justification:** Explorer-hosted extensions and in-process plug-ins cross process/ABI boundaries that normal core tests do not exercise.
- **Proposed solution:** Install into an isolated VM, invoke each COM shell action and plug-in entry, inject controlled hangs/crashes, and verify the host remains usable or quarantines the component.

### 88. Run repeated startup/shutdown leak tests

- **Justification:** Manual handles, GDI objects, threads, and plug-in lifecycles commonly leak only after many reopen cycles.
- **Proposed solution:** Loop clean and loaded-profile startup/shutdown hundreds of times, track process handle/GDI/USER counts and private bytes, and fail on statistically significant growth.

### 89. Add Application Verifier and PageHeap lanes

- **Justification:** Heap misuse, invalid handles, lock problems, and unsafe shutdown ordering may not crash under normal allocation and timing.
- **Proposed solution:** Run focused executable scenarios with Heaps, Handles, Locks, and Exceptions checks; use full PageHeap for targeted nightly tests and archive verifier logs.

### 90. Add a parallel-operation cancellation soak

- **Justification:** The hardest races occur when copy, directory refresh, icon loading, plug-in work, pause/resume, dialog close, and application shutdown overlap.
- **Proposed solution:** Execute seeded randomized schedules for hours, cancel at every phase, and assert bounded completion, no verifier findings, no leaked workers, and recoverable disk state.

### 91. Cover disk-full, quota, read-only, and sharing-violation failures

- **Justification:** These ordinary operational failures directly challenge overwrite safety, retry policy, and accurate user reporting.
- **Proposed solution:** Use virtual disks/quotas and held handles to create each condition at create, write, metadata, flush, and commit phases; verify originals remain intact.

### 92. Test files above 4 GiB, sparse files, and very large offsets

- **Justification:** Legacy size/seek calls and narrowing conversions often work on small fixtures while failing at 32-bit boundaries.
- **Proposed solution:** Use sparse fixtures to cheaply cover 4 GiB−1, 4 GiB, 4 GiB+1, multi-terabyte logical sizes, resume offsets, progress math, and cross-volume behavior on both architectures.

### 93. Add a Unicode, normalization, and long-path matrix

- **Justification:** Mixed UTF-8/UTF-16 code and fixed buffers can mishandle surrogate pairs, combining characters, trailing dots/spaces, case collisions, and extended paths.
- **Proposed solution:** Test creation, display, selection, copy, archive, rename, and deletion across NTFS/SMB with representative scripts and normalization forms; compare identities by handle rather than normalized display text.

### 94. Retain symbols and map every crash to an exact build

- **Justification:** A dump is much less useful if the matching PDB, compiler flags, dependency set, and source revision cannot be recovered.
- **Proposed solution:** Publish private symbol artifacts indexed by product version and PE identifiers, embed commit/build metadata, enforce retention, and validate symbolization during release.

### 95. Minimize, encrypt, and govern crash-dump data

- **Justification:** Full dumps include private read/write memory and data segments (`src/salmon/minidump.cpp:56-83`), which may contain paths, credentials, document contents, or network data.
- **Proposed solution:** Default to the smallest useful dump, filter sensitive ranges/modules with callbacks, require informed consent, encrypt in transit and at rest, impose local/server retention, and provide delete/export controls.

### 96. Register Windows application restart and recovery callbacks

- **Justification:** No core use of `RegisterApplicationRestart` or `RegisterApplicationRecoveryCallback` was found. A crash can therefore lose unsaved UI/session context even when file-operation recovery data exists.
- **Proposed solution:** Register a bounded, allocation-light recovery callback that persists safe session and journal references, then restart into a recovery screen without automatically repeating destructive actions.

### 97. Make builds reproducible and publish provenance

- **Justification:** Current staging records build time and runner number and can discover outputs dynamically, making two builds of the same commit difficult to compare.
- **Proposed solution:** Pin toolchains and dependencies, normalize timestamps/paths where supported, generate hashes and SLSA-style provenance, and periodically reproduce a release on a clean runner.

### 98. Pin GitHub Actions by immutable SHA and minimize release permissions

- **Justification:** Workflows use mutable tags and even `ammaraskar/msvc-problem-matcher@master` (`.github/workflows/pr-msbuild.yml:50-64`); the release job has repository write permission throughout.
- **Proposed solution:** Pin each action to a reviewed commit SHA, use dependabot-style updates, keep build/test jobs read-only, and grant `contents: write` only to the final protected publish job.

### 99. Introduce a canary release and installer rollback gate

- **Justification:** A non-draft release is published immediately after packaging, with no automated clean-install, upgrade, uninstall, launch, or rollback verification.
- **Proposed solution:** Produce an internal/draft canary, test it on clean supported Windows images, verify signatures and file manifest after install/upgrade/uninstall, then promote the exact same artifact after approval and health checks.

### 100. Establish reliability objectives, ownership, and a staged delivery roadmap

- **Justification:** A list of technical fixes will decay without measurable outcomes and accountable owners. The current automated safety-net maturity is approximately **2/10**: useful repeated UI smoke coverage exists, but core native operations and failure modes are largely untested and CI does not run the suite.
- **Proposed solution:** Track data-loss incidents, operation failure rate, hang-free sessions, crash-free sessions, recovery success, and release escape rate. Target 4/10 after characterization and CI gates, 7/10 after transactional operations/fault injection/hardening, and 10/10 only when release, recovery, fuzzing, and field feedback form a maintained closed loop.

## Recommended execution sequence

1. **Contain immediate hazards:** improvements 1-20, starting with check-thread teardown, cancellation/wait protocols, transactional overwrite, release gating, and artifact verification.
2. **Create the change safety net:** improvements 27-30, 81, 83-93. Characterize current observable behavior before changing ownership or control flow.
3. **Harden subsystem boundaries:** improvements 21-26 and 31-60, delivered in small independently testable tranches.
4. **Modernize dependencies and build defenses:** improvements 61-80, with compatibility corpora and explicit baselines.
5. **Close the operational loop:** improvements 94-100, so failures are recoverable, diagnosable, attributable to an exact build, and prevented from recurring.

The first milestone should not attempt a broad C++ rewrite. It should establish three enforceable invariants: no worker is force-killed, an overwrite never destroys the last good destination before durable commit, and no release is published from unverified or ambiguously staged inputs.
