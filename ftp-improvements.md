# FTP, FTPS, and SFTP large-folder transfer improvements

## Scope and evidence

This is an implementation plan for copying local folders to servers and remote folders to local storage, especially trees containing thousands of small files. It covers both time before the first file transfers and total completion time. It does not implement the changes or claim measured speedups.

The findings below come from the working tree inspected on 2026-09-04, based on commit `e3ba5f93`. Symbols are the primary navigation references because line numbers will change. Proposed settings, types, files, and numeric tuning values are explicitly implementation recommendations, not existing interfaces or measured optima.

There are two distinct engines:

| Protocol | Implementation | Build constraint |
| --- | --- | --- |
| FTP and FTPS (FTP over TLS) | `src/plugins/ftp/`; FTPS uses Windows SChannel in `ssl.cpp` | Use the installed Visual Studio 2026 developer environment. `vcxproj/ftp.vcxproj` declares Debug/Release and Win32/x64 configurations. |
| SFTP (SSH File Transfer Protocol) | `src/plugins/winscp/core/`, integrated through `src/plugins/winscp/salamander/` | `README.md` states that this plugin requires Embarcadero C++ Builder. The tree contains `Salamand.bpr`, `ScpCore.bpr`, `SalamandForms.bpr`, and `Putty.bpr`. Verify the build and packaged plugin independently; an FTP/MSBuild success does not validate SFTP changes. |

Do not route SFTP work through FTP command handlers. The scheduling concepts can be shared, but the transport implementations and build work must remain separate.

## Confirmed starting points

Paths in this table are relative to the repository root.

| Finding | Exact source location | Consequence for this work |
| --- | --- | --- |
| FTP starts exactly one worker, with a FIXME about configuring the count. | `src/plugins/ftp/fs5.cpp`, `CPluginFSInterface::RunOperation`, `for (i = 0; i < 1; i++)` | Existing parallel-worker support is not automatically used for a large copy. |
| A worker can inherit the panel's control connection; additional workers can be added through the operation dialog. | `fs5.cpp`, `RunOperation`; `dialogs6.cpp`, worker-add handler; `operats2.cpp`, `CFTPOperation::AddWorker` | Extend this lifecycle instead of opening a new authenticated session for every file. |
| Maximum-connection settings are stored and displayed. The inspected worker-start paths do not enforce them. | `ftp.h`, `UseMaxConcurrentConnections` / `MaxConcurrentConnections`; `ftp.cpp`; `ftp3.cpp`; `fs5.cpp`; `dialogs6.cpp` | Implement actual admission control before enabling automatic parallelism. A configuration field alone is not a connection limit. |
| The queue initially prefers exploration/link-resolution items and re-enables that preference when such work appears. | `operats1.cpp`, `CFTPQueue::GetNextWaitingItem`, `HandleFirstWaitingItemIndex`, constructor | A single worker can spend substantial time exploring before transferring. This is a priority policy, not a universal barrier: multiple workers can transfer when no eligible discovery item is waiting. |
| Queue lookup falls back to a linear scan; directory expansion inserts into the item array. | `operats1.cpp`, `FindItemWithUID`, `ReplaceItemWithListOfItems` | UID lookup and array movement can become expensive as trees and completed-item history grow. The existing last-lookup cache and waiting cursor already avoid some scans. |
| The operation queue is capped at 100,000 items; the disk-work queue at 512. | `operats.h`, `FTP_OPERATION_QUEUE_LIMIT`, `FTP_DISK_WORK_QUEUE_LIMIT`; `operats1.cpp`, expansion rejection | Increasing these limits is not a scalability solution. Discovery needs backpressure and resumable expansion. |
| FTP already shares destination listings, coordinates workers waiting for a listing, and updates entries after writes. Lookup within a listing is binary search. | `operats9.cpp`, `CUploadListingCache`, `CUploadPathListing::FindItem`, `ReportStoreFile`, `ReportFileUploaded` | Preserve and extend the cache; do not propose replacing a supposed per-file LIST or linear lookup that is not present. Sorted-array insertion can still be costly. |
| Workers already cache their current remote directory. | `operatsb.cpp`, `HaveWorkingPath` / `WorkingPath` before `ftpcmdChangeWorkingPath` | Reduce directory switching through scheduling affinity rather than adding a duplicate CWD cache. |
| Built-in listings use configurable LIST text and server-specific parsing; no FEAT/MLSD/MLST command implementation was found in the FTP C++ sources. | `operats2.cpp`, `CFTPOperation::GetListCommand`; `ctrlcon4.cpp`; `operats7.cpp`; `parser*.cpp` | Add a negotiated machine-readable listing path while retaining custom LIST behavior. |
| FTPS uses shared SChannel credentials and already has asynchronous data-channel handshakes, but worker control login still calls the blocking handshake. | `ssl.cpp`, `InitSSL`, `BeginAsyncEncryptSocket`, `ContinueAsyncEncryptSocket`, `EncryptSocket`; `datacon1.cpp`, `datacon2.cpp`; `operats6.cpp`, `HandleEventInConnectingState`, `fwssConWaitForAUTHCmdRes` | Preserve asynchronous data handling and migrate the remaining worker login before scaling connections. Prove actual TLS resumption. |
| FTP read/write handoff buffers are 64 KiB and disk work runs through `CFTPDiskThread::Body`. | `datacon.h`, `DATACON_FLUSHBUFFERSIZE`, `DATACON_UPLOADFLUSHBUFFERSIZE`; `operats5.cpp` | Profile disk wait and allocation overhead after increasing useful concurrency. |
| FTP progress refresh already has a 100 ms minimum interval. | `dialogs.h`, `OPERDLG_UPDATEPERIOD`; `dialogs5.cpp`, delayed refresh and `RefreshItems` | Optimize work per refresh, not just the timer frequency. |
| SFTP submits the selected file list as one upload/download queue item; the adapter overrides the core queue limit to unlimited. | `salamander/FileSystem.cpp`, queue initialization and `CopyOrMoveFromFS` / `CopyOrMoveFromDiskToFS` paths; `core/Queue.cpp`, `TUploadQueueItem::DoExecute`, `TDownloadQueueItem::DoExecute` | Background queuing does not divide one selected folder among connections. Naively creating thousands of jobs would also risk excessive sessions and UI objects. |
| SFTP download request depth defaults to 4 and is used by the download loop. Upload/listing settings also exist with defaults 4/2, but the inspected upload/listing loops do not consume them. | `core/SessionData.cpp`; `core/SftpFileSystem.cpp`, `TSFTPDownloadQueue`, `TSFTPUploadQueue`, `TSFTPAsynchronousQueue`, `ReadDirectory` | Tuning the download setting can change behavior; changing the other constants alone cannot. Uploads already send asynchronously, so an upload depth of 4 is not their demonstrated runtime limit. |
| SFTP copy defaults to calculating total size first. | `core/CopyParam.cpp`, `TCopyParamType::Default`; `core/Terminal.cpp`, `CopyToLocal`, `CopyToRemote` | Avoid a required full-tree prepass for ordinary copies, while preserving an explicit exact-total option. |

## Implementation order

| Priority | Change | Expected benefit |
| --- | --- | --- |
| P0 | 1. Add measurement and deterministic transfer fixtures | Establish which costs dominate and prevent unsubstantiated optimization claims. |
| P1 | 2. Enforce FTP connection limits and automatically schedule workers | Overlap per-file command, connection, and server delays. |
| P1 | 3. Interleave discovery with transfer and index the FTP queue | Start copying sooner; avoid queue growth and repeated scans. |
| P1 | 4. Implement FEAT/MLSD and preserve listing/cache correctness | Reduce metadata ambiguity and avoid unnecessary round trips. |
| P1 | 5. Verify and improve FTPS session resumption | Reduce repeated TLS handshake cost for small files. |
| P1, separate build track | 6. Split SFTP folder jobs into bounded work; eliminate mandatory size prepass | Enable concurrency inside a single selected tree. |
| P2 | 7. Bound and tune SFTP request pipelines | Improve transfers limited by bytes in flight and response latency. |
| P2, measurement-gated | 8. Reduce disk, allocation, progress, and logging overhead | Remove local bottlenecks once network scheduling improves. |

Implement 1 first. Land admission control and the asynchronous FTPS worker-login change from section 5 before automatic worker growth; land SFTP admission control before job splitting. Land the dependency model before incremental discovery. Treat every later step as independently benchmarkable.

## 1. Measure the real transfer path

**Change locations:** FTP `operats.h`, `operats1.cpp` through `operatsb.cpp`, `datacon1.cpp`, `datacon2.cpp`, `ssl.cpp`; SFTP `core/Queue.cpp`, `core/SftpFileSystem.cpp`, `core/FileOperationProgress.cpp`. Add a proposed `scripts/benchmark-ftp-transfers.ps1` and dedicated fixture support under `tests/FileManager.UiTests/Infrastructure/`.

Implement per-operation counters and monotonic duration measurements for:

- Discovery start/end, time to first payload byte, time to first completed file, final completion time, files/s, payload bytes/s, and per-file p50/p95 completion latency.
- Command counts by verb and duration: FTP login, CWD/PWD, TYPE, LIST/MLSD, SIZE, passive/active setup, RETR/STOR, and final replies; SFTP OPENDIR/READDIR, STAT/LSTAT/FSTAT, OPEN, READ/WRITE, CLOSE, SETSTAT, and rename.
- Active/idle workers, connection attempts and reuses, server refusals, retries, ready/blocked items, maximum queue depth, UID lookup work, directory-cache hits/misses, and duplicate listing fetches.
- FTPS full/resumed/unknown handshakes and their durations; SFTP outstanding request count/bytes and response latency.
- Disk queue wait/service time, socket starvation time, buffer allocations, CPU time, peak private bytes, open handles, and UI refresh duration.

Aggregate counters in memory and export one machine-readable JSON result per run. Do not synchronously write one telemetry record per packet. Redact credentials and sensitive command arguments. Record build ID, plugin binary identity, architecture, configuration, OS, server version, negotiated protocol, dataset seed, latency, concurrency, transfer mode, and enabled preservation options.

**Acceptance:** Instrumentation on/off changes median runtime by less than 3% on the controlled benchmark, or instrumentation is restricted to a diagnostic build. The report must distinguish executed, failed, and skipped tests. Existing `SChannelTlsIntegrationTests.cs` primarily exercises .NET TLS peers; it is not proof of native `ftp.spl` session reuse. Existing `scripts/run-ftp-test.ps1` is a credential-dependent live-server lane, not a repeatable performance baseline.

## 2. Automatically use a bounded FTP/FTPS worker pool

**Change locations:** `fs5.cpp`, `RunOperation`; `operats.h`; `operats2.cpp`, `AllocNewWorker` / `AddWorker`; `operats5.cpp`, `CFTPWorkersList`; `dialogs6.cpp`, manual worker addition; configuration in `ftp.h`, `ftp.cpp`, `ftp3.cpp`, `dialogs1.cpp`, and `dialogs8.cpp`.

1. Introduce an operation worker target, distinct from the maximum server connection limit. Proposed settings: `TransferParallelismMode = Auto | Fixed`, `TransferWorkerLimit = 4` for Auto, and fixed range 1..8. Preserve existing explicit maximum-connection settings and allow fixed 1 for compatibility. Migrate missing settings to Auto; do not reinterpret the currently disabled maximum's stored value of 1 as an active cap.
2. Add one admission controller shared by all FTP operations and panel connections in this process. Track endpoint/account/proxy/security identity and a host-level total. A worker holds a lease for its authenticated control connection; transfer the panel's existing lease when handing its connection to worker 0. A control/data pair counts as one logical connection under the existing configuration wording; separately measure actual sockets. Enforce any configured maximum on automatic, manual, reconnect, and panel connection paths. Limits imposed across other applications remain server-enforced.
3. Start with the inherited worker. Once discovery exposes at least 32 ready files, grow to two workers, then toward four only while ready work remains and measured throughput improves. Proposed sampling interval: 2 seconds; add at most one worker per interval. Keep manual/fixed mode deterministic. Leave single-file operations at one worker initially.
4. Keep authenticated workers alive across files. Favor files whose remote parent matches the worker's `WorkingPath`, with a bounded batch of 64 files before checking other eligible directories. Preserve the existing TYPE-state cache and reset connection state after reconnect.
5. On classified server connection-limit failures, reduce the target and use a shared reconnect delay with jitter. Proposed transient retry schedule: 1, 2, 4, 8, 16, 30 seconds, capped by the existing operation retry policy. Do not interpret every 421/425/450 response as a connection-limit refusal; retain reply-specific handling. Do not retry authentication or certificate failures as throughput tuning.
6. Never kill a healthy in-flight transfer merely to reduce the target. Retire idle workers, release leases on every failure/shutdown path, and keep the existing connection-return behavior.

**Acceptance:** A single selected 10,000-file folder uses multiple sessions automatically when allowed. A configured maximum of two is respected across two simultaneous operations plus panel browsing; manual addition cannot bypass it. A one-session server completes without a reconnect storm. Login counts scale with worker creation/recovery, not file count.

## 3. Stream folder discovery and remove queue scaling costs

**Change locations:** `operats.h`; `operats1.cpp`, `FindItemWithUID`, `ReplaceItemWithListOfItems`, `GetNextWaitingItem`, parent counters; `operats6.cpp` / `operats7.cpp`, remote discovery; `operatsa.cpp`, local upload discovery; `operats5.cpp`, local enumeration; `dialogs5.cpp` / `dialogs6.cpp`, item indexing.

1. Introduce explicit dependency state for directories: `discoveryComplete`, `pendingChildren`, `destinationReady`, and finalization status. Reserve destination names before dispatch so two workers cannot create/write the same target. Make directories eligible for metadata finalization only after discovery finishes and all children settle. Preserve existing skip/error propagation, link-cycle detection, and postorder deletion for move operations that share the queue.
2. Replace discovery-only preference with separate ready-discovery and ready-transfer queues. With multiple workers, allow at most one discovery assignment while sufficient file work exists; lend that slot to transfers when discovery is finished or backpressured. With one worker, alternate a discovery chunk and up to 64 file transfers. Do not require a whole tree to be discovered before transferring its ready leaves.
3. Replace all-at-once directory expansion with `AppendDiscoveredChildren(parentUID, batch, continuation)` (proposed API). Publish batches of at most 256 entries. Mark `discoveryComplete` only after clean enumeration EOF and the relevant FTP final success reply. If a listing later fails, retain the incomplete parent and report/retry it; never declare a partially listed tree successful. Deduplicate already published entries during retry.
4. Add high/low watermarks of 8,192/4,096 ready file items as initial tuning values. Pause producers above the high watermark and resume below the low watermark. Release queue locks before waiting. Network listings must continue to drain into a bounded spool or parser staging area; do not block the socket thread or deadlock a server waiting to finish LIST. Local enumeration retains a continuation or spool rather than restarting from entry zero.
5. Keep stable item UIDs and add a UID-to-item index for constant-average-time lookup. Keep ready lists separate from the UI's display ordering. Use stable/chunked item storage rather than shifting a large pointer array for every expansion. Update retry, removal, replacement, and parent counters atomically with the index. Resolve display rows through a separate index or snapshot; do not reintroduce a full scan in `GetItemIndex` on every progress event.
6. The existing 100,000-item bound covers all retained items, not merely ready work. Backpressure alone therefore cannot fix it. Compact completed entries into per-directory counters and an append-only operation journal, and page history for the dialog. Keep unresolved/error/retry records and ancestor dependencies addressable; spill them when necessary rather than silently dropping them. If spool creation or writing fails, pause/fail visibly without losing ownership of pending items. Retain resource limits as defensive bounds, not as normal tree-size limits.

**Acceptance:** Copying begins before the final directory is enumerated. A tree with more than 100,000 files completes with bounded resident queue storage. Doubling a synthetic tree from 10,000 to 20,000 entries does not approximately quadruple scheduler CPU work. Cancellation, discovery retry, skip, overwrite, and move finalization remain correct with several workers. Directory timestamps are applied after child creation.

## 4. Negotiate efficient FTP listings and use metadata deliberately

**Change locations:** `ctrlcon.h` / `ctrlcon2.cpp`, command formatting; `ctrlcon1.cpp` and `operats6.cpp`, `CFTPWorker::HandleEventInConnectingState`, login negotiation; `ctrlcon4.cpp`, panel listings; `operats2.cpp`, operation listing settings; `operats6.cpp` / `operats7.cpp`, download discovery; `operats9.cpp`, upload cache; `operatsa.cpp` / `operatsb.cpp`, destination checks. Add a dedicated bounded MLSx parser beside `parser*.cpp`.

1. Add a per-authenticated-session capability record with unknown/supported/unsupported states. Send FEAT once after login. Recognize MLST capability advertisement for MLSx support; do not require an advertised feature literally named MLSD. Propagate capabilities when handing a connection to a worker and renegotiate after reconnect. Persist user overrides, not unqualified assumptions about a previously contacted server.
2. Prefer MLSD for automatic listing selection. Request supported facts using `OPTS MLST type;size;modify;` and retain server defaults if OPTS is rejected. Parse fact names case-insensitively, sizes with checked 64-bit conversion, timestamps as UTC, and filenames without splitting them on internal spaces. Ignore unknown facts; exclude `cdir` and `pdir` from recursive work. Missing facts remain unknown, not zero. Do not infer symlink targets from unspecified fact values.
3. Preserve explicitly configured LIST commands, LIST -a behavior, server path syntax, and legacy parsers. Fall back once on a definitive unsupported-command response; do not globally disable MLSD because one directory is denied or one data connection times out. A malformed result must not be cached as a complete directory.
4. Include listing mode, identity, path semantics, and a generation in cache keys. Continue sharing one in-flight destination listing among workers and applying `ReportStoreFile` / `ReportFileUploaded` updates. Do not add unconditional per-file SIZE, MDTM, or MLST requests when an operation's fresh listing already provides sufficient metadata. Preserve targeted checks required for resume offsets, ambiguous file types, stale entries, or explicit overwrite policies. Invalidate on reconnect or contradictory server results.
5. Track directory creation through the shared dependency record. After this operation successfully creates a new destination directory, initialize its local listing state instead of immediately listing it for every child. This is not an atomic no-overwrite guarantee against other clients: continue enforcing existing conflict policy, and revalidate when the server exposes a conflict or external modification. Do not promise that FTP STOR offers exclusive creation.
6. Profile sorted-array insertion in `CUploadPathListing::InsertNewItem`. If it dominates flat-folder uploads, use a per-directory pending-change map with path-type-correct equality, merging into the sorted snapshot in batches. Preserve binary-search behavior for the immutable snapshot and the existing waiting-worker notification protocol.

**Acceptance:** On an unchanged, successfully listed source tree, discovery performs one successful listing per visited directory, not one per file. Unsupported FEAT/MLSD is handled without repeated failure for every directory. Include spaces, Unicode under negotiated encoding, missing/unknown facts, fractional timestamps, empty directories, permission-denied directories, and legacy LIST fixtures. Resume against a changed target must not trust a stale cached size.

MLSD/MLST facts and negotiation follow [RFC 3659, sections 7.5, 7.8, and 7.9](https://www.rfc-editor.org/rfc/rfc3659.html). This improves metadata acquisition; it does not eliminate FTP's per-file transfer command and data-connection costs.

## 5. Make FTPS resumption observable and preserve asynchronous progress

**Change locations:** `ssl.cpp`, `InitSSL`, `AdvanceHandshake`, `FinishEncryptSocket`, `BeginAsyncEncryptSocket`, `ContinueAsyncEncryptSocket`; `sockets.h`; `datacon1.cpp` / `datacon2.cpp`, control-to-data identity and handshake setup; `operats.h`, `operats3.cpp` / `operats4.cpp`, worker events; `operats6.cpp`, worker control login.

1. Retain reusable SChannel credentials and verify that associated control and data handshakes use the same intended server target identity. Do not substitute a passive data port or arbitrary IP as a new TLS identity. Separate transport address from certificate/session identity explicitly if current callers conflate them. Keep different certificate/client-authentication policies isolated.
2. Add full/resumed/unknown telemetry by querying `SECPKG_ATTR_SESSION_INFO` and inspecting the reconnect flag where supported. Do not treat `ReuseSSLSession = 2` as evidence: `FinishEncryptSocket` currently assigns it because SChannel owns caching, without checking whether this handshake resumed. Treat unsupported telemetry as unknown and corroborate with server logs.
3. Verify TLS 1.2 and TLS 1.3 separately, including repeated small uploads/downloads, ticket arrival, multiple control workers, server cache eviction, and a server requiring data-channel reuse tied to its control session. Reuse of some cached session is not proof that a strict server will accept the corresponding data channel. If shared credential/cache behavior chooses an unsuitable session, evaluate credential handles scoped to a control/data family, sharing the same handle within each family. Gate that change on integration evidence.
4. Preserve the existing nonblocking data handshake state machines and their FD_READ/FD_WRITE ownership, partial-token handling, cancellation, and deadlines. Replace the blocking `EncryptSocket` call in `CFTPWorker::HandleEventInConnectingState`, `fwssConWaitForAUTHCmdRes`, with an asynchronous control-handshake substate. Resume it from worker socket readiness events, give it a monotonic deadline, and advance to PBSZ/PROT only after successful completion. Extend the asynchronous completion interface to return an unverified certificate to the existing worker error/prompt flow: its current data-oriented calls pass no `unverifiedCert` output. Releasing `WorkerCritSect` around the blocking call does not make its socket-event execution asynchronous. Complete this migration before enabling wider worker growth.
5. Preserve certificate validation, certificate-change detection, exception expiry, TLS policy, and encrypted data protection. A failed resumption may fall back to a validated full handshake only when the server permits it. Report strict-reuse incompatibility without downgrading protection. Continue requiring the FTP final success response as well as successful data/TLS completion.

**Acceptance:** On a resumption-capable fixture, a warm sequence of 1,000 tiny files reports successful resumed data handshakes corroborated by server telemetry. A server declining resumption still completes via full handshakes when allowed. Stalling one worker's TLS handshake does not stop another worker's payload progress. Changed certificates and expired exceptions retain current behavior.

SChannel session-cache prerequisites are documented in [InitializeSecurityContext](https://learn.microsoft.com/en-us/windows/win32/secauthn/initializesecuritycontext--schannel); observed reconnect status is exposed by [SecPkgContext_SessionInfo](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-secpkgcontext_sessioninfo). Preserve the control/data protection relationship specified in [RFC 4217](https://www.rfc-editor.org/rfc/rfc4217.html).

## 6. Parallelize one SFTP folder operation without exploding the queue

**Change locations:** `salamander/FileSystem.cpp`, foreground/background copy submission and queue initialization; `core/Queue.h` / `core/Queue.cpp`; `core/Terminal.cpp`, `CopyToLocal` / `CopyToRemote`; `core/CopyParam.h` / `core/CopyParam.cpp`; `salamander/SalamandQueue.cpp`; `salamander/SalamanderPreferences.cpp` and associated form resources.

1. Introduce a parent folder-copy operation with bounded discovery, child work, and directory dependencies equivalent to section 3. Submit leaf-file batches to existing reusable `TTerminalItem` sessions. Start with up to 64 files from one directory per batch. A single selected folder must create multiple eligible batches; splitting only the top-level selection is insufficient.
2. Replace the adapter's `FQueue->TransfersLimit = 0` with a persisted bounded setting, initially four transfer sessions, and provide fixed 1 compatibility mode. Account for the panel session and other operations when enforcing an endpoint total. Reuse the queue's existing terminal lifecycle. Each concurrent worker owns its own terminal, file-system state, request IDs, and SSH transport; do not call one `TTerminal` concurrently from several threads.
3. Route both foreground and background folder copies through the parent scheduler, preserving the foreground wait/cancel behavior. Do not make performance depend on selecting the background queue option. Keep temporary-file/edit workflows on their existing constrained path unless separately validated.
4. Replace the mandatory recursive size calculation with incremental discovery for ordinary copies. Add a persisted `PrecalculateTransferSize` preference, default off for the new folder-copy path. When off, suppress the parent prepass and each child's `CalculateSize` prepass; show discovered bytes/files and an explicitly incomplete total. When on, reuse discovered metadata/manifests for transfer where valid rather than deliberately walking the same tree twice. Keep explicit calculate-size commands unchanged.
5. Reserve targets and coordinate directory creation, overwrite decisions, rename masks, symlink policy, timestamps, and permissions at the parent operation. Finalize directory metadata after all children finish. A failed child keeps the parent incomplete. Pause/cancel propagates to discovery and all sessions; retry only unfinished work.
6. Change `TSalamandQueueController::QueueListUpdate` and related progress handling to show one parent operation with aggregate progress and expandable failures. The current code can create a `TSalamandProgressThread` for each queue item; creating one per leaf file is unacceptable. Deduplicate pending panel refreshes by directory and publish them in batches.

**Acceptance:** One selected 10,000-file folder transfers through the configured bounded number of sessions in either UI mode. The first completed file does not wait for a full-tree size walk. Queue items, forms, threads, and connections stay bounded as the tree grows. Preserve-time/rights, overwrite prompts, cancellation, and partial retry work in both directions. Report separately whether the modified WinSCP plugin was actually built and loaded.

## 7. Tune SFTP requests with explicit bounds

**Change locations:** `core/SessionData.h` / `core/SessionData.cpp`, defaults, assignment, load, save, setters; `core/SftpFileSystem.h` / `core/SftpFileSystem.cpp`, `TSFTPQueue`, `TSFTPFixedLenQueue`, `TSFTPDownloadQueue`, `TSFTPAsynchronousQueue`, `TSFTPUploadQueue`, `ReadDirectory`, `TransferBlockSize`; `core/SecureShell.cpp`, transport flow-control measurements.

1. Persist and validate download/upload/listing queue settings through every session load/save/copy path. The current queue properties are initialized and copied, but the inspected storage code only handles neighboring SFTP settings such as `SFTPMaxPacketSize`. Invalid zero/negative/huge values must select a safe default or produce a settings error, never an unbounded allocation.
2. For downloads, begin trials at 32 outstanding requests rather than 4; compare 4, 16, 32, and 64. Keep existing server/SSH packet-size constraints. With 32 KiB payloads, four outstanding reads permit about 128 KiB in flight: at 50 ms RTT the window-only ceiling is approximately 2.5 MiB/s. This is an illustrative bound, not a measured speed or a statement that every connection uses a 32 KiB payload. Tiny files need section 6 because they cannot fill a deep per-file window.
3. Add an explicit request/byte bound to the asynchronous upload path; do not simply change the unused `SFTPUploadQueue` default. Before another WRITE would exceed either bound, pump responses until capacity is available while servicing cancellation and transport events. Initial trial limits: 32 requests, 2 MiB of outstanding payload per session, and 16 MiB per parent operation. Include retained packet/response buffers in a separate total-memory budget. Preserve ASCII conversion boundaries, resume offsets, and error attribution.
4. Make `SFTPListingQueue` drive a bounded READDIR request queue. The present implementation sends the next READDIR before parsing the current reply, but does not use this setting. Start at depth 2; benchmark 1, 2, 4, and 8. Handle out-of-order responses by ID, stop issuing on EOF, drain already-issued replies, and close the handle once. Provide a depth-1 compatibility fallback for demonstrated server defects, without hiding real permission or I/O errors.
5. Use request-ID lookup and a ring/deque for pending requests instead of front-deleting arrays when profiling shows those operations become significant. Audit `ReserveResponse`, `ReceivePacket`, and response disposal together; increasing depth must not turn linear response searches into the next bottleneck.
6. Treat packet size and request count as separate controls. Start with the current negotiated block sizes; do not assume a larger buffer fixes latency. Optionally implement `limits@openssh.com` negotiation and clamp packet/read/write/open-handle limits when advertised. Retain existing compatibility constraints when absent. Adjust depth from measured RTT, bandwidth, and memory budget only after fixed settings are validated.
7. Reuse listing attributes when sufficient, but retain FSTAT/STAT where freshness, symlink resolution, resume correctness, or preserve-time behavior requires it. Existing upload code already overlaps CLOSE/property requests with draining responses; preserve that work. A file completes only after all required write, close, and metadata statuses succeed.

**Acceptance:** Delayed/reordered replies, short reads, zero-length files, EOF with pending requests, write failure, cancellation, ASCII conversion, and resumed transfers produce correct output with bounded memory. A large-file high-latency test demonstrates that the requested window is actually reached. A many-small-file test measures the separate benefit of session-level concurrency.

OpenSSH exposes request count and buffer size independently in its [SFTP client manual](https://github.com/openssh/openssh-portable/blob/master/sftp.1). Its optional negotiated limits are described in the upstream [PROTOCOL document](https://github.com/openssh/openssh-portable/blob/master/PROTOCOL). These are compatibility references, not evidence that this embedded WinSCP revision already supports those extensions.

## 8. Optimize local work after measuring it

**Change locations:** FTP `datacon.h`, `datacon1.cpp`, `datacon2.cpp`, `operats5.cpp`, `ctrlcon2.cpp` (`CLogs::LogMessage`), `dialogs5.cpp`, `dialogs6.cpp`; SFTP `core/SftpFileSystem.cpp`, `core/FileBuffer.cpp`, `core/FileOperationProgress.cpp`, `salamander/SalamandQueue.cpp`.

- Reuse transfer buffers per worker instead of allocating the same 64 KiB handoff buffers for each small file. Benchmark 64/256/1,024 KiB for larger files under an operation memory cap; keep small-file buffers modest. TLS record limits still apply independently.
- If disk service is the bottleneck, replace the single disk-work executor with a small configurable pool, initially two workers. Serialize work for each file handle, keep local enumeration outside long disk critical sections, and preserve file-close ordering, completion-message ownership, cancellation, and the bounded queue. Retain a one-worker option for devices where concurrency causes seeks or regressions. Do not add a global flush per file or switch to unbuffered I/O as a default optimization.
- Keep the existing 100 ms FTP progress cadence. Aggregate dirty item IDs and refresh visible rows once per interval; move full error-list reconstruction and expensive sorting off per-packet/per-file callbacks. Send final completion/error updates promptly, and never hold queue locks while calling UI code.
- Batch log appends and visible log updates while preserving bounded history, diagnostics, and credential redaction. Measure verbose logging separately. Replace SFTP's every-ten-listing-entries progress trigger with time-based coalescing plus a final update; still service cancellation regularly.
- Evaluate compression only against measured CPU/bandwidth and data type. Preserve existing transfer semantics. Do not enable MODE Z or SSH compression for every file by default, and do not change ASCII/binary policy simply to improve benchmark results.

**Acceptance:** Keep these changes only when profiles show the targeted cost falls and end-to-end throughput improves without memory, cancellation, or correctness regressions. Raising buffers or disk threads must not become a substitute for the small-file scheduling changes.

## Benchmark and regression requirements

Run actual native plugin uploads and downloads; fixture-only TCP/TLS tests do not establish product performance. Use a disposable local profile and dedicated local/pre-provisioned test endpoints. A userspace delay proxy or fixture can model latency without administrator rights. Pre-provision any real FTP/FTPS/SFTP server and latency-shaping infrastructure; the self-hosted GitHub runner must not install services, change firewall rules, or write protected locations.

| Dataset | Purpose |
| --- | --- |
| One flat directory of 10,000 deterministic 4 KiB binary files | Per-file command, TLS, allocation, and UI overhead. |
| 1,000 directories containing 10 files each, plus empty directories | Listing count, dependency ordering, and time to first transfer. |
| A deep tree with 10,000 files within supported path limits | Traversal fairness, cycle checks, and directory finalization. |
| More than 100,000 small files | Queue backpressure, history storage, and bound handling. |
| Mixed empty, 1 KiB, 64 KiB, and 1 MiB files, plus a 1 GiB file | Fairness and payload-throughput effects. |
| Existing destination with a fixed mixture of identical names, different sizes, and partial files | Overwrite, skip, resume, and cache invalidation. |

For the 10,000-file datasets, test both directions using FTP, FTPS TLS 1.2, FTPS TLS 1.3 where supported, and SFTP. Compare fixed 1/2/4/8 workers and Auto at approximately 1/20/50/100 ms measured RTT. Use a fixed bandwidth ceiling and incompressible seeded content for the primary run; test compressible content separately. Keep metadata settings, transfer mode, server configuration, and endpoint storage identical across comparisons. Label unsupported combinations as skipped.

Use one warm-up and at least five measured runs per comparison, alternating baseline/candidate order. Reset destination state outside the timed interval. Report median and range, and distinguish cold login/cache runs from warm sessions. Hash every output in binary-copy tests and compare the complete tree; validate timestamps/permissions within protocol precision. For ASCII tests, compare against expected conversion rather than byte-identical source hashes.

Add fault scenarios: restricted session count, fragmented control replies, denied directories, server disconnect mid-file and mid-listing, stalled TLS handshake, data-close before final FTP reply, certificate change, SSH host-key change, full local disk, spool failure, and cancel while discovery is backpressured. Ensure no source deletion or successful-parent status follows an incomplete transfer. Do not infer remote durable storage from a successful upload response unless a separately negotiated durability feature provides it.

**Proposed release gates, to validate against the baseline:**

- At least 2x median files/s for the 10,000 x 4 KiB test at 50 ms RTT with four allowed sessions versus the current one-worker FTP behavior; apply the equivalent single-folder comparison to SFTP. This is a target, not a promise. Investigate misses using measured phase durations.
- First completed file within the first available directory's discovery/transfer window, before full-tree enumeration finishes; no compulsory SFTP size prepass in incremental mode.
- No more than 5% median regression for the fixed-one-worker large-file control case.
- No unbounded growth in ready tasks, outstanding requests, UI objects, handles, or resident history during the >100,000-file run. Record and enforce the chosen memory budgets, including listing spools and error history.
- Cancel stops issuing new work within 250 ms and reaches a settled state within 2 seconds on responsive peers; stalled-peer cases obey the configured monotonic deadlines and remain UI-responsive.
- Zero content mismatches, missing/duplicate files, lost errors, or silent partial-success results. Hard connection limits remain respected in concurrent-operation tests.

## Build, delivery, and review

This documentation-only change does not require a native build, and no performance benchmark was run while writing it. For implementation work:

1. Use Visual Studio 2026's installed developer-command environment. Build `src/plugins/ftp/vcxproj/ftp.vcxproj` with `Configuration=Debug, Platform=x64` for diagnostics and `Configuration=Release, Platform=x64` for performance. Validate Win32 configurations if they remain release targets. Record the actual VS installation, SDK/toolset, complete command, and result; do not infer C++ is unavailable from a separate Build Tools installation.
2. Build and package the WinSCP plugin through its verified C++ Builder environment before claiming SFTP fixes. Record compiler version, architecture, plugin identity, and the binary loaded by tests. If that environment is unavailable, report SFTP changes as unvalidated; do not block independent FTP/FTPS work or claim the main solution built them.
3. Add focused native/fixture tests for scheduler dependencies, queue indexing, MLSx parsing, request bounds, and retry ownership, plus product integration cases for the behaviors above. Avoid source-text assertions as the sole evidence of performance behavior.
4. Keep changes reviewable in the order listed above; record baseline/candidate benchmark artifacts with each performance change. Introduce settings with validation, persistence, UI text, and migration in the same change. Preserve a fixed-one-worker fallback.
5. Follow repository guidance: every source-code change includes a concise nearby comment explaining its intent, invariant, or compatibility constraint. Comments should explain, for example, why parent finalization waits for discovery EOF or why data TLS identity follows the control session.

Do not make automatic archive-and-extract, server shell execution, disabling encryption, disabling certificate/host-key checks, unlimited connections, or splitting a single file into multiple FTP ranges part of this plan. They either change the requested copy semantics, require capabilities a normal FTP account lacks, or do not address the primary many-small-files cost. The first implementation milestone should be bounded automatic FTP workers plus incremental scheduling, measured with the actual plugin.
