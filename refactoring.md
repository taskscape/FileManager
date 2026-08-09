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

### 11. Replace legacy file-size and seek APIs in operation code

- **Justification:** `GetFileSize` and `SetFilePointer` are used broadly, including the overwrite path (`src/async_copy.cpp:4486`), retaining sentinel/error ambiguity and fragile 64-bit handling.
- **Proposed solution:** Add characterized wrappers around `GetFileSizeEx` and `SetFilePointerEx`, migrate the copy/move engine first, and then ratchet remaining callers. Return a typed result containing either the 64-bit value or the captured Win32 error.

### 12. Replace the custom crash uploader with HTTPS WinHTTP

- **Justification:** `src/salmon/upload.cpp:13` sends crash dumps over raw HTTP port 80. Reports and potentially sensitive dump contents are exposed to interception and modification.
- **Proposed solution:** Use WinHTTP over TLS with normal certificate and hostname validation, a versioned HTTPS endpoint, explicit proxy support, and a clear consent screen. Refuse downgrade to plaintext.

### 13. Stream crash uploads with correct framing and bounded I/O

- **Justification:** The uploader casts file size into `int`, allocates the entire request, knowingly writes an imprecise `Content-Length`, assumes one `send` transmits everything, and has no robust timeouts (`src/salmon/upload.cpp:29-69`, `220-240`).
- **Proposed solution:** Use 64-bit checked arithmetic, stream fixed-size chunks, let WinHTTP frame the body, set connect/send/receive deadlines, support cancellation, cap response size, and retry only idempotent pre-commit failures.

### 14. Replace OpenSSL 1.0.2u with a supported TLS implementation

- **Justification:** The release downloads OpenSSL 1.0.2u and ships legacy `libeay32.dll`/`ssleay32.dll` (`.github/workflows/build-installer.yml:45`, `tools/prepare_installer.ps1:70`). An obsolete TLS stack increases security and interoperability failures.
- **Proposed solution:** Prefer Windows SChannel to reduce bundled attack surface; otherwise move to a supported OpenSSL branch with a documented compatibility plan. Add TLS 1.2/1.3 integration tests, certificate-failure tests, and an upgrade cadence.

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

### 19. Constrain DLL search paths

- **Justification:** `LoadLibraryUtf8` converts the name and calls `LoadLibraryW` without restricted search flags (`src/common/handles.cpp:2211`). Current-directory or PATH precedence can load unintended dependencies.
- **Proposed solution:** Initialize `SetDefaultDllDirectories`, use absolute canonical plug-in paths with `LoadLibraryExW` and explicit `LOAD_LIBRARY_SEARCH_*` flags, and add only approved directories with `AddDllDirectory`.

### 20. Establish plug-in trust and quarantine policy

- **Justification:** Plug-ins are loaded in-process, and no `WinVerifyTrust` check was found in the reviewed loader. A damaged or replaced plug-in has the same privileges and address space as the host.
- **Proposed solution:** Verify Authenticode signatures and file hashes before load, record publisher decisions, quarantine failed updates, and give users a safe-mode launch that disables third-party plug-ins.

### High: fault containment and core correctness

### 21. Move risky parsers and previewers out of process

- **Justification:** Archive, media, database, and preview plug-ins parse untrusted files inside the main process. Memory corruption or a hang in one parser can terminate the file manager and any active operations.
- **Proposed solution:** Create a low-privilege broker process with a versioned, length-checked IPC contract, job-object resource limits, and kill/restart recovery. Start with thumbnail and archive metadata extraction.

### 22. Make plug-in entry bookkeeping exception-safe

- **Justification:** `EnterPlugin`/`LeavePlugin` manually balance a global counter around a direct plug-in entry call (`src/plugins_loading.cpp:2278-2290`). An exception or early exit can permanently leave the host in “inside plug-in” state.
- **Proposed solution:** Add an RAII scope guard that restores plug-in state, locks, and interface placeholders on every exit. Make nesting state thread-local or explicitly synchronized.

### 23. Add failure barriers around every plug-in callback

- **Justification:** Host-to-plug-in calls are numerous and not governed by one recovery contract; at least one plug-in thread exception path terminates the process. A single extension fault should not take down unrelated file operations.
- **Proposed solution:** Route callbacks through a common boundary that records the plug-in identity, validates results, restores host invariants, disables the failing extension, and reports a recoverable error. Use process isolation where recovery from memory corruption cannot be trusted.

### 24. Make configuration saves transactional

- **Justification:** Shutdown backup logic exists, but ordinary `SaveConfig` paths write many values into the active settings tree. Interruption can leave a partially updated configuration.
- **Proposed solution:** Serialize to a staging key/file, validate it, write a completion marker and checksum, then atomically switch the active generation. Retain the last known-good generation until the next successful startup.

### 25. Version and validate the complete configuration schema

- **Justification:** A large evolving setting surface makes partial, malformed, or future-version data a stability risk. Individual defaulting does not prove cross-field invariants.
- **Proposed solution:** Store a schema version, validate ranges and dependent fields before applying anything, run explicit idempotent migrations, and fall back to a known-good profile with a user-visible diagnostic.

### 26. Test backup restoration and interrupted configuration writes

- **Justification:** Existing backup behavior is valuable but is only resilient if restoration works after failure at every write boundary.
- **Proposed solution:** Add fault-injection tests that stop writes after each registry/file operation, restart the executable, and assert either the old or complete new configuration appears—never a mixture.

### 27. Extract a testable file-operation planning seam

- **Justification:** Planning, prompting, progress, execution, and Win32 I/O are intertwined across very large source files, making the most dangerous logic difficult to characterize without UI automation.
- **Proposed solution:** Introduce a pure operation-plan model and a narrow filesystem adapter while preserving behavior. Golden-master the generated plans before changing execution semantics.

### 28. Build native characterization tests for copy, move, delete, and rename

- **Justification:** The current UI suite does not cover core destructive operations, so regressions in conflict handling, metadata, cancellation, and rollback can escape.
- **Proposed solution:** Add native integration tests using disposable directories and volumes. Cover overwrite choices, skip/all choices, same- and cross-volume moves, recycle-bin behavior, cancellation, and restart reconciliation.

### 29. Add crash-consistency fault injection at every operation phase

- **Justification:** Happy-path tests cannot demonstrate the atomicity promised by a file manager. Failures must be explored between create, write, metadata, flush, replace, and source delete.
- **Proposed solution:** Put Win32 file calls behind an injectable adapter in tests, fail each call deterministically, and assert the invariant: the original, the complete replacement, or a recoverable journal exists.

### 30. Implemented: add executable-level file-operation scenarios

- **Delivered:** `tests/FileManager.UiTests/FileOperationUiTests.cs` launches the real executable with fresh source and target panel paths per case. It verifies nested directory creation; file and directory-tree copy, rename, move, and delete; cancelled operation dialogs; invalid destinations/names; and a locked-file delete failure. Assertions read the disposable filesystem directly, and fixture teardown terminates only the launched executable before deleting the workspace.
- **Remaining scope:** Native fault injection, controlled mid-copy cancellation, cross-volume behavior, restart reconciliation, and metadata fidelity belong to improvements 28, 29, 31-33, and 85.

### 31. Publish an explicit metadata preservation contract

- **Justification:** Current code handles timestamps, attributes, some ACL data, and alternate streams, but behavior varies by operation and filesystem. Users cannot tell when fidelity is reduced.
- **Proposed solution:** Define required, best-effort, and unsupported metadata per NTFS/ReFS/FAT/SMB operation, then make the engine record and display any losses before source deletion.

### 32. Test alternate data streams end to end

- **Justification:** ADS handling has specialized buffer and retry paths in `src/async_copy.cpp`; uncommon branches are high-regression territory and can silently lose content.
- **Proposed solution:** Create files with multiple named streams, empty streams, large streams, denied streams, and stream-name edge cases. Verify same-volume, cross-volume, resume, overwrite, and unsupported-target behavior.

### 33. Verify ACL and ownership preservation under privilege variation

- **Justification:** Security descriptor copying behaves differently with and without backup/restore privileges and across filesystems. Partial success can leave unexpectedly permissive or inaccessible output.
- **Proposed solution:** Add a privilege-aware matrix for owner, group, DACL, inheritance, deny ACEs, and inaccessible descriptors. Fail or warn according to the published metadata contract.

### 34. Exercise junction, symlink, mount-point, and cloud-placeholder cases

- **Justification:** Reparse tags alter whether an operation targets the link or its destination. A mistake can recurse outside the selected tree or delete unintended data.
- **Proposed solution:** Build disposable reparse topologies, include cycles and changed targets, and assert no traversal outside the operation root. Add explicit policies for unknown tags and cloud hydration.

### 35. Introduce a dynamic wide-path abstraction

- **Justification:** Core code contains thousands of `MAX_PATH` uses and repeated UTF-8-to-wide conversions into fixed buffers. Long, deeply nested, and multi-byte paths can fail or truncate unpredictably.
- **Proposed solution:** Use dynamically sized UTF-16 paths at Win32 boundaries, preserve display strings separately, normalize only when required by a specific API, and support extended-length syntax consistently.

### 36. Ban new fixed `MAX_PATH` buffers

- **Justification:** A broad path rewrite is risky, but allowing new fixed buffers increases the backlog and perpetuates boundary bugs.
- **Proposed solution:** Add a changed-lines CI ratchet for new `char/WCHAR [...MAX_PATH...]`, with narrow documented exemptions. Migrate one subsystem at a time behind compatibility adapters.

### 37. Ratchet unchecked string-copy and formatting calls

- **Justification:** `strcpy`, `strcat`, `sprintf`, `lstrcpy`, `lstrcat`, and `wsprintf` remain common. Input length assumptions are dispersed and hard to review.
- **Proposed solution:** Ban new unsafe calls, introduce size-aware formatting returning truncation/error status, and migrate external-input boundaries first. Add tests that hit exact capacity, one-over, and encoding-expansion cases.

### 38. Replace fixed buffers at trust boundaries first

- **Justification:** Network responses, plug-in metadata, archive names, environment values, and crash-report fields are controlled outside the local function and therefore carry the highest overflow and truncation risk.
- **Proposed solution:** Parse into bounded dynamic containers with maximum accepted sizes and explicit conversion errors. Keep compatibility wrappers only at characterized internal boundaries.

### 39. Use checked arithmetic for sizes, offsets, and allocations

- **Justification:** The crash uploader demonstrates a file-size-plus-overhead cast into `int`; similar arithmetic in parsers and progress calculations can wrap before allocation or I/O.
- **Proposed solution:** Standardize checked add/multiply/cast helpers for `uint64_t`, `size_t`, and Win32 `DWORD`, reject impossible values, and fuzz every external size field.

### 40. Make operation result types explicit

- **Justification:** Many paths combine `BOOL`, mutable out parameters, `GetLastError`, and log-only secondary failures. This makes it easy to lose the original cause or treat partial completion as success.
- **Proposed solution:** Introduce a lightweight result type carrying phase, Win32/HRESULT code, source, destination, retryability, and partial-effect flags. Adapt it back to existing dialogs until callers migrate.

### 41. Adopt RAII for kernel handles in touched code

- **Justification:** Manual `CloseHandle` across numerous returns and exception paths makes leaks and double closes likely, especially during fault handling.
- **Proposed solution:** Use the already-vendored WIL handle wrappers or a small project wrapper. Require new/touched functions to transfer ownership explicitly and delete manual cleanup ladders only after characterization.

### 42. Adopt RAII for memory, mappings, and critical sections

- **Justification:** Raw `new/delete`, `malloc/free`, mapping views, and manual lock pairing amplify early-return and callback risks.
- **Proposed solution:** Introduce scoped buffers, view guards, and lock guards compatible with the current compiler and ABI. Start at plug-in and file-operation boundaries, where cleanup failures are most costly.

### 43. Standardize thread creation and ownership

- **Justification:** Dozens of raw `CreateThread` calls distribute handle ownership, parameter lifetime, COM initialization, exception policy, and naming across the codebase.
- **Proposed solution:** Provide one thread wrapper using `_beginthreadex` where CRT state is used, owning the handle and stop event, naming the thread, initializing COM when declared, and guaranteeing a completion signal.

### 44. Define bounded shutdown deadlines without unsafe escalation

- **Justification:** One-second waits followed by thread killing are arbitrary, while infinite waits can hang shutdown forever. Neither behavior explains what the worker is doing.
- **Proposed solution:** Give worker phases explicit cancellation deadlines, emit diagnostics on deadline breach, detach only components proven not to access destroyed state, and keep the process alive long enough to preserve operation recovery data.

### 45. Replace wrap-prone time calculations with monotonic 64-bit time

- **Justification:** `GetTickCount` is used hundreds of times; its 32-bit wrap and ad hoc subtraction can break timeouts and throttles after long uptime.
- **Proposed solution:** Centralize monotonic timing on `GetTickCount64` or `QueryUnbiasedInterruptTime`, use duration types, and add wrap/clock-jump tests for remaining compatibility code.

### 46. Replace `Sleep` polling with signaled waits

- **Justification:** Polling delays cancellation, wastes time, and creates schedule-sensitive tests; `ReleaseCheckThreads` even sleeps before force-killing (`src/path_checking.cpp:95`).
- **Proposed solution:** Wait on work, cancellation, and deadline handles together. Where periodic work is required, use waitable timers and make each wake reason explicit.

### 47. Document and verify lock ordering

- **Justification:** Thousands of critical-section enter/leave sites and cross-thread UI calls make lock inversion difficult to reason about.
- **Proposed solution:** Assign lock ranks, wrap acquisition with debug assertions, record owner/waiter information on timeout, and run Application Verifier deadlock checks in a nightly stress lane.

### 48. Reduce unowned global mutable state

- **Justification:** Globals such as plug-in entry state and worker flags obscure thread affinity and lifetime. A shutdown or re-entrant callback can mutate them from an unexpected context.
- **Proposed solution:** Move state into explicit subsystem objects with documented owner threads, pass narrow references, and mark truly shared fields atomic or lock-protected. Migrate by subsystem rather than changing storage wholesale.

### 49. Protect window and callback lifetimes

- **Justification:** Background workers retain HWNDs and pointers while dialogs and panels can close. Posting or sending after destruction risks reuse of stale window handles or memory.
- **Proposed solution:** Pair callbacks with generation tokens/weak registrations, invalidate them before window destruction, and discard messages whose operation generation no longer matches.

### 50. Bound background work queues

- **Justification:** Icon, thumbnail, directory, and plug-in work can grow with directory size or slow consumers, increasing memory pressure and shutdown latency.
- **Proposed solution:** Use bounded queues with deduplication, priority for visible items, cancellation of obsolete generations, and explicit backpressure metrics.

### Important: resource limits, diagnostics, network, and dependencies

### 51. Set resource budgets for directory enumeration

- **Justification:** Very large directories and slow shares can monopolize memory/UI update traffic even when each individual call succeeds.
- **Proposed solution:** Enumerate in bounded batches, virtualize UI insertion, cap cached metadata, and yield/cancel between batches. Stress with millions of synthetic entries and high-latency enumeration.

### 52. Reserve memory for graceful out-of-memory handling

- **Justification:** When allocation fails, even constructing an error or journal record may fail, leaving no safe way to cancel an operation.
- **Proposed solution:** Allocate a small emergency reserve at startup, release it on first OOM, stop accepting work, persist minimal recovery state, and offer a controlled exit.

### 53. Remove modal UI and retry loops from the global allocation handler

- **Justification:** `src/common/allochan.cpp` serializes allocation failure handling and can display message boxes or sleep while the failing thread holds unrelated locks, creating deadlock and re-entrancy risks.
- **Proposed solution:** Make the handler allocation-free and non-interactive: release the reserve, set an atomic fatal-pressure state, and notify the UI through a preallocated channel. Never loop indefinitely inside allocator recovery.

### 54. Add a bounded release-build diagnostic ring buffer

- **Justification:** Much operational logging is debug-only, so field deadlocks and intermittent I/O failures lack the event sequence needed for diagnosis.
- **Proposed solution:** Keep a low-overhead in-memory ring of sanitized operation transitions, waits, retries, and plug-in identities. Attach it to user-approved reports and allow local export.

### 55. Assign correlation IDs to operations and workers

- **Justification:** Parallel copies, dialogs, callbacks, and retries otherwise produce ambiguous traces, especially after a crash or cancellation race.
- **Proposed solution:** Generate an operation ID at command dispatch and propagate it through plan, worker, UI, journal, and log records. Include item sequence and attempt number.

### 56. Preserve the first actionable error and its context

- **Justification:** Repeated cleanup calls can overwrite `GetLastError`, while log-only failures lose the operation phase and affected path.
- **Proposed solution:** Capture errors immediately into the explicit result type, append cleanup errors without replacing the primary cause, and present a copyable diagnostic summary.

### 57. Centralize retry policy

- **Justification:** Ad hoc retry prompts and delays can repeat permanent failures, overload network shares, or duplicate side effects.
- **Proposed solution:** Classify transient Win32/network errors, use capped exponential backoff with jitter, honor cancellation, and never automatically retry a destructive commit unless idempotency is proven.

### 58. Apply deadlines and cancellation to all network operations

- **Justification:** FTP, update, and report paths can block on DNS, connect, TLS, send, or receive while shutdown waits on their threads.
- **Proposed solution:** Define phase-specific timeouts, propagate one cancellation token, close sockets/requests to interrupt blocking I/O, and distinguish timeout from authentication or protocol failure.

### 59. Make FTP transfers transactional and resumable safely

- **Justification:** Network interruption and resume logic can leave a destination that looks complete but contains stale or duplicated bytes.
- **Proposed solution:** Download to a side file with persisted remote identity/size/time, validate resume offsets and server responses, flush and atomically rename on success, and keep incomplete files clearly marked.

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
