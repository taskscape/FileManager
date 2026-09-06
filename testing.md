# Automated testing

Run all commands from the repository root. The test suite has four layers:

- PowerShell source-contract and native compatibility probes.
- NUnit source-contract and local TLS integration tests.
- FlaUI/UIA3 tests that drive the native Windows application.
- Optional fault-injection, filesystem-topology, and cross-volume characterization lanes.
- Loopback FTP, FTPS, and HTTP protocol fixtures for fragmented replies, disconnects, TLS, and stalls.

Visual Studio 2026 and its developer-command environment are the authoritative native build environment. Most scripts support Windows PowerShell 5.1. The SQLite recovery probe is the exception: use 64-bit PowerShell 7.4 or newer (`pwsh.exe`) for an x64 DLL.

## Building

### Complete release-pipeline build

The release-parity runner is the authoritative local equivalent of the GitHub Actions release gate and installer-build jobs. Running it without arguments selects this mode automatically:

```powershell
.\scripts\runtests.ps1
```

The defaults are `-BaseCommit HEAD^`, `-PlatformToolset v145`, `-BuildNumber 0`, the release NUnit filter, and an automatically generated TRX path. Use `-NoReleasePipeline` when you intentionally want only the ordinary test inventory.

First inspect the blocking environment without mutating disks, build outputs, or installed tools:

```powershell
.\scripts\runtests.ps1 -PrerequisiteOnly
```

The parity command requires an interactive Windows desktop with complete Git history, VS 2026 v145 C++ tools, Windows PowerShell, and PowerShell 7. It probes the host's fixed `D:\` drive; when that drive is absent or not writable, all second-volume-dependent UI tests are reported as successful capability skips. It performs the workflow's staged Debug build and strict artifact resolution, then runs the aggregate test gate. Only after that gate passes does it perform the separate Release build, PE audit, symbol validation, pinned Inno Setup installation, staging, and installer compilation. It does not publish a GitHub release.

`scripts\build-installer.ps1` remains available for packaging-only iteration, but it is not a replacement for release-pipeline validation.

## Quick start

### Complete local runner

Run every automated test whose prerequisites are available on the current machine:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\scripts\runtests.ps1 -NoReleasePipeline
```

`scripts/runtests.ps1` collects all PowerShell/native probes, both x64 and x86 compatibility variants, the built 7-Zip wrapper/oracle corpus gate, the complete NUnit project, and the optional Application Verifier lane. It runs every collected check even after a failure and prints passed, failed, and explicitly skipped checks. The 7-Zip gate requires a `7z.exe`-compatible independent oracle; strict release runs fail if it is unavailable. The runner creates only the guarded `filemanager-testdata` directory and the suffixed current-user configuration key before executing the complete UI project.

Each run removes its own GUID-named `TestResults\runtests-build-*` directory, including after a failure. Pass `-KeepBuildArtifacts` only when the isolated native build outputs are needed for diagnosis.

Every NUnit case that actually launches `salamand.exe` also writes a retained execution transcript below a GUID-named
`TestResults\ui-test-transcripts-*` directory. Unlike the staged build and `filemanager-testdata` sandbox, these logs
are kept after successful and failed runs. A run skipped before `salamand.exe` starts produces no transcript.

`-PlatformToolset v145` selects the toolset used for both the built executable and native safety target. CI may supply `-NUnitTrxPath` to retain the complete executable result inventory as a workflow artifact.

The native safety target also exercises the production `CStableMoveSource` owner against real temporary files. Its cases reject an already-open writer, block writes and renames after copy readers close, reopen the same contents for retries, and verify normal/read-only deletion plus read-only restoration after an injected deletion failure. These checks run through the existing native-safety lane in both the local runner and CI.

The same target exercises conditional overwrite publication through retained file and directory handles. It forces destination writes/renames after acquisition, creates unexpected occupants before publication, injects journal/rename/flush/backup-cleanup failures, and checks read-only preservation. A junction is retargeted between acquisition and publication; only the originally opened directory may change. These native cases require no additional administrator privileges or separate CI lane.

`OperationRecoveryTests.h`, included by that native target, executes the production recovery parser and publication code. It covers changed destinations, same-name staging substitutes, truncated data, same-length corruption with restored timestamps, changed parent identity/junction resolution, new named streams, legacy/manual-only evidence, retries/new temporary records, sharing failures, occupied backups, and unavailable parent directories. Both Resume and Discard must preserve unverifiable files. Faults at rename, deletion, journal write, journal flush, and torn-record boundaries keep recovery pending; a failed discard outcome can be persisted after restart without deleting another file. Mixed journals skip previously resolved items, and Cancel remains discoverable. Real incompatible handles exercise the live-writer and exclusive-recoverer lifetime contract.

The native journal-size cases place a ready item at the end of complete journals one byte below, exactly at, and one byte above 16 MiB. Parsing uses a 64 KiB input buffer and bounded records, with no total file-size cutoff; item state grows with the plan. A malformed, overlong, NUL-containing, unsupported, or truncated record prevents mutation of that journal. Named-stream and reparse files remain manual-only because this evidence format fingerprints the main data stream. Interrupted publications with ambiguous backup state are also retained for manual inspection.

`FtpDownloadTests.h` exercises the production FTP staging owner on real files: conditional overwrite, empty and read-only destinations, verified resume, changed target/stage/version, exclusive ownership, incomplete checkpoints, short writes, and failures in flush, metadata, rename, close, and backup cleanup. Mode/version retries must durably revoke the previous checkpoint before replacing private bytes while retaining the approved destination handle. Per-request completion tests cover early completion, persistent errors, concurrent waiters, timeout, and cancellation of a wait without cancelling disk completion.

`FtpDownloadReliabilityUiTests` drives the actual FTP quick-connect, queued copy/move, and direct-view paths against `LoopbackFtpDownloadServer`. Payload barriers exercise interruption and cancellation, and guarded finalization faults verify that `DELE` cannot precede a successful local durable result. These deterministic tests run in the normal NUnit inventory under the existing isolated UI profile, with no external server or credentials. The application preserves the harness's temporary directory through environment regeneration so viewer-cache files also remain inside `filemanager-testdata`.

The FTP fixture sets `FILEMANAGER_UI_FTP_FAULT` to `pause`, `flush`, `metadata`, `commit`, `close`, `admission`, or `close-admission`. Injection additionally requires `FILEMANAGER_UI_ISOLATED=1`, the exact sandbox configuration root, a validated `filemanager-testdata` directory, and an exclusively claimed one-use `.ftp-reliability.arm` file there. The pause is bounded; `.entered`, `.release`, and `.completed` files in that same directory coordinate only the selected test request. Normal invocations do not enable these faults. Hardware power-loss behavior is outside these software failure fixtures.

### GitHub release-pipeline parity

Run the complete local equivalent of the GitHub release gate and installer-build jobs with:

```powershell
.\scripts\runtests.ps1 -ReleasePipeline -BaseCommit HEAD^ -BuildNumber 0
```

`-ReleasePipeline` applies the release category filter and unexpected-skip policy, omits the nightly Application Verifier diagnostic, then preflights the cached, SHA-256- and Authenticode-verified Inno Setup compiler in portable current-user mode before building `Release|x64`. The preflight has a bounded installer timeout and retains a wrapper diagnostic log even when Inno Setup cannot create its own log. It audits Release PE hardening, re-runs the native regression subset, verifies the private symbol index, stages the installer, and compiles it. Provisioning logs are retained below the runner temporary directory for diagnosis. It deliberately does not publish a GitHub release. Pass `-KeepBuildArtifacts` to retain the isolated Debug and Release build trees after diagnosis.

Unlike the ordinary runner, release-pipeline mode refuses ambient UI roots and uses the same fresh-volume topology as Actions. It tests uncommitted local changes so developers can validate the exact snapshot they intend to push; GitHub then exercises that snapshot once committed. Its generated `TestResults\runtests-release-gate-*\runtests-v145.trx` is the local counterpart of the uploaded GitHub test artifact.

The runner uses the CI pull-request base for changed-line ratchets or accepts `-BaseCommit` explicitly; it does not guess from a potentially stale local tracking branch. It discovers an existing Debug or Release x64 SQLite DLL when possible. Supply prerequisites explicitly or require a fully provisioned run with:

```powershell
.\scripts\runtests.ps1 `
  -BaseCommit origin/main `
  -SqliteDll .\src\vcxproj\sqlite\salamander\Debug_x64\utils\sqlite.dll `
  -FailOnSkipped
```

Run individual checks with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify-operation-completion-protocol.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify-durable-copy-commit.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test-release-input-pinning.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test-zlib-compatibility.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\test-bzip2-compatibility.ps1
```

The process-scoped execution-policy bypass does not change the machine-wide policy.

### NUnit contracts and integration tests

The NUnit project targets .NET 10:

```powershell
dotnet restore .\tests\FileManager.UiTests\FileManager.UiTests.csproj
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj `
  --no-restore `
  --logger "console;verbosity=minimal"
```

The project declares `<IsTestProject>true</IsTestProject>`, so no MSBuild command-line override is needed for discovery. When the normal .NET or NuGet directories are unavailable, redirect generated state before restoring:

```powershell
$env:DOTNET_CLI_HOME = Join-Path $PWD '.dotnet-cli'
$env:NUGET_PACKAGES = Join-Path $PWD '.nuget-packages'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
$env:DOTNET_NOLOGO = '1'

dotnet restore .\tests\FileManager.UiTests\FileManager.UiTests.csproj
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj `
  --no-restore `
  --logger "console;verbosity=minimal"
```

These directories contain generated package and CLI state and must not be committed.

### Native UI tests

UI tests run against the interactive current user only after selecting the guarded test-data and registry boundaries:

```powershell
$env:FILEMANAGER_UI_TESTDATA_ROOT = "$env:USERPROFILE\filemanager-testdata"
$env:FILEMANAGER_UI_CONFIG_ROOT = 'Software\Open Salamander\6.0-filemanager-testdata'
$env:FILEMANAGER_UI_EXE = 'C:\path\to\salamand.exe'

dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj `
  --filter 'TestCategory=UI'
```

The harness creates and removes only `filemanager-testdata` and its subfolders, and creates then removes only `HKCU\Software\Open Salamander\6.0-filemanager-testdata`. Recycle-bin tests inspect the current user's bin but never empty or remove it.

Optional UI settings:

| Variable | Enables |
| --- | --- |
| `FILEMANAGER_UI_ARGUMENTS` | Extra application arguments, such as a test-only `-c` configuration file. |
| `FILEMANAGER_UI_TRANSCRIPT_ROOT` | Optional absolute output directory for retained per-test `salamand.exe` execution transcripts. `runtests.ps1` assigns a GUID-named directory under `TestResults`; direct `dotnet test` runs default below the NUnit work directory. |
| `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` | The runtime command ID for FTP **Organize Bookmarks** persistence cases. |
| `FILEMANAGER_UI_FTP_CONNECT_COMMAND` | Optional runtime Connect to FTP Server command ID for protocol UI fixture runs outside `runtests.ps1`; the runner discovers it from the freshly built menu. |
| `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` | Exhaustive configuration-write crash recovery. |
| `FILEMANAGER_UI_CROSS_VOLUME_ROOT` | Cross-volume move tests using only a GUID child below the supplied dedicated root. |
| `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` | Metadata-loss behavior on a different FAT/FAT32/exFAT-like volume. |
| `FILEMANAGER_UI_RECYCLE_BIN=1` | Recycle-bin deletion using the current user's default recycle-bin setting; the test never empties or removes the bin. |
| `FILEMANAGER_UI_LEAK_CYCLES` | Lifecycle resource samples (5–200; default 20, nightly 100) for handles, GDI, USER, and private bytes. |

Run a specialized lane by category after setting its required environment:

```powershell
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=FaultInjection'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=CrossVolume'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=AlternateDataStreams'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=Recovery'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=ReparsePoints'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=RecycleBin'
dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj --filter 'TestCategory=LockStress'
```

### External MojeRzeczy FTPS UI test

This live-server test is `Explicit` and excluded from normal and release-pipeline runs. It uses the `MOJERZEC_USERNAME` and `MOJERZEC_PASSWORD` variables from `C:\Projects\FtpMojerzeczy`, explicit FTPS on port 21, passive mode, binary transfer, and accepts the server's invalid certificate only in the disposable test profile. Debug error dialogs are logged to `TestResults\ftp-debug-error-dialogs`, dismissed, and reported as a test failure so the external lane does not require a manual click. If either credential is absent, it passes without opening an FTP connection and reports that FTP UI tests have not been performed due to missing credentials.

```powershell
$env:MOJERZEC_USERNAME = 'your-username'
$env:MOJERZEC_PASSWORD = 'your-password'
.\scripts\run-ftp-test.ps1
```

See [`tests/FileManager.UiTests/README.md`](tests/FileManager.UiTests/README.md) for additional UI-lane details.

### cmark-gfm hardening probe

The cmark-gfm probe invokes `cl.exe`, so start it through Visual Studio 2026:

```powershell
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
$command = 'call "' + $vsDevCmd + '" -arch=x64 -host_arch=x64 && ' +
           'powershell -NoProfile -ExecutionPolicy Bypass ' +
           '-File "' + (Join-Path $PWD 'tools\test-cmark-gfm-hardening.ps1') + '"'
& $env:ComSpec /d /s /c $command
```

Adjust the edition or installation path when necessary.

### SQLite recovery probe

Build the Debug x64 SQLite target with Visual Studio 2026, then use 64-bit PowerShell 7.4 or newer:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass `
  -File .\tools\test-sqlite-recovery.ps1 `
  -SqliteDll .\src\vcxproj\sqlite\salamander\Debug_x64\utils\sqlite.dll
```

### Pull-request ratchets

The changed-line ratchets require the pull request base commit:

```powershell
.\tools\verify-no-new-terminatethread.ps1 -BaseCommit origin/main
.\tools\verify-no-new-raw-thread-creation.ps1 -BaseCommit origin/main
.\tools\verify-no-new-gettickcount.ps1 -BaseCommit origin/main
.\tools\verify-no-new-max-path-buffers.ps1 -BaseCommit origin/main
.\tools\verify-no-new-unsafe-string-calls.ps1 -BaseCommit origin/main
```

The toolbar icon coverage check has no parameters:

```powershell
.\tools\verify-fluent-icon-coverage.ps1
```

## Complete automated test catalog

### PowerShell and native probes

| Test | Description |
| --- | --- |
| `verify-operation-completion-protocol.ps1` | Verifies that cancellation requests do not destroy worker-owned state, workers publish owned completion records, and the UI resumes and closes operations through the asynchronous completion protocol. |
| `verify-durable-copy-commit.ps1` | Checks write-through creation, flush/close/verification ordering, transactional replacement, retry paths, and deterministic filesystem fault boundaries for durable copy commits. |
| `test-release-input-pinning.ps1` | Checks that release inputs have a complete lock record, all workflow actions use reviewed immutable commits, and publication consumes the gated immutable installer artifact. |
| `audit-pe-hardening.ps1` | Inspects linked Release PE headers for ASLR, DEP/NX, CFG, CET compatibility, and high-entropy VA. The release workflow runs it after the Release build. |
| `test-unsafe-api-baseline.ps1` | Compares every repository unsafe API fingerprint with the reviewed generated baseline and rejects new or duplicated unsafe calls. |
| `test-zlib-compatibility.ps1` | Compiles the checked-in zlib sources and replays the retained legacy, checksum-error, invalid-deflate, and truncated-stream vectors. Supports `-Architecture x64` and `x86`. |
| `test-bzip2-compatibility.ps1` | Compiles the checked-in bzip2 sources and checks golden and legacy streams, truncation rejection, and the malformed fuzz-vector corpus. Supports `-Architecture x64` and `x86`. |
| `test-cmark-gfm-hardening.ps1` | Compiles the production Markdown renderer, compares retained snapshots, checks safe link and raw-HTML behavior, exercises extension combinations, and enforces input, nesting, node, and output limits. |
| `test-7zip-compatibility.ps1` | Archives the retained corpus through the exact built wrapper/library pair, compares independent-oracle extraction manifests, and rejects named header, payload, and footer corruption regressions. |
| `test-sqlite-recovery.ps1` | Exercises the supplied SQLite DLL with WAL/FULL settings, interrupted transactions, committed-row recovery, integrity checks, and controlled b-tree corruption detection. |
| `DeterministicNetworkFixtureTests` | Runs local HTTP, FTP, and FTPS fixtures in every profile for fragmented replies, controlled disconnect, TLS negotiation, and deadlines; with an isolated profile, it also drives the native FTP quick-connect dialog through its fragmented greeting and login boundary. |
| `verify-no-new-terminatethread.ps1` | Rejects newly added native `TerminateThread` calls while leaving legacy debt to dedicated migrations. |
| `verify-no-new-raw-thread-creation.ps1` | Rejects new first-party `CreateThread` or `_beginthreadex` calls outside the reviewed thread-owner boundaries. |
| `verify-no-new-gettickcount.ps1` | Rejects new wrap-prone `GetTickCount` calls so timeout and scheduling code uses the 64-bit monotonic clock. |
| `verify-no-new-max-path-buffers.ps1` | Rejects newly added native fixed arrays whose bounds contain `MAX_PATH` unless a documented exemption applies. |
| `verify-no-new-unsafe-string-calls.ps1` | Rejects newly added unchecked `strcpy`, `strcat`, `sprintf`, `lstrcpy`, `lstrcat`, and `wsprintf` calls. |
| `verify-fluent-icon-coverage.ps1` | Ensures every mapped toolbar command has an SVG, core rows no longer use shell fallbacks, SVG colors stay in the approved palette, and the application icon remains separate. |

The zlib and bzip2 `.hex` files under `tests/` and the cmark-gfm Markdown/HTML snapshots are retained inputs consumed by these probes rather than independent runners.

### NUnit source-contract and integration tests

These tests do not drive the FileManager UI and can run in a normal developer profile.

#### `ApplicationVersionContractTests`

- `Product_major_is_6_and_build_components_remain_automatic` — keeps native, manifest, configuration, shell-extension, installer, workflow, and README version declarations synchronized at product major 6 while preserving automatic build numbering.

#### `SChannelTlsIntegrationTests`

- `LocalTlsServerNegotiatesTheRequiredProtocol(Tls12)` — proves a local SChannel client and server negotiate TLS 1.2.
- `LocalTlsServerNegotiatesTheRequiredProtocol(Tls13)` — proves a local SChannel client and server negotiate TLS 1.3 when supported by the host.
- `SelfSignedServerCertificateIsRejectedWithoutAnExplicitUserException` — proves default validation rejects an untrusted self-signed server certificate.

#### `ToolbarIconSizeContractTests`

- `Customize_toolbar_exposes_persisted_small_medium_and_large_icon_sizes` — checks resource, configuration, validation, persistence, and live-change wiring for 16, 24, and 32 pixel toolbar icons.
- `Toolbar_image_lists_scale_independently_from_menu_images_and_app_icon` — keeps toolbar scaling separate from menu image lists and the application icon.
- `Fluent_toolbar_spacing_scales_with_each_configured_icon_size` — verifies padding, separators, customization layout, startup, and live refresh use the configured icon geometry.
- `Go_to_hot_path_uses_a_conservative_saved_location_glyph` — protects the reviewed saved-folder visual and palette for the hot-path command.
- `High_fidelity_reference_catalog_contains_every_runtime_toolbar_icon` — requires the 91-icon SVG reference mirror and the expected 3840×3440 catalog image.

#### `NativeSafetyRegressionTests`

- `Unchecked_string_calls_are_ratchet_gated_and_external_boundaries_report_capacity_and_encoding_failures` — keeps unsafe-string ratchets and checked boundary-error reporting in place.
- `New_fixed_max_path_buffers_are_rejected_by_the_changed_lines_ci_ratchet` — keeps the changed-line `MAX_PATH` buffer gate and exemption register wired into CI.
- `Bundled_zlib_is_current_has_retained_compatibility_vectors_and_runs_in_ci` — pins the zlib vendor record, compatibility vectors, probe, and CI hook.
- `Bundled_bzip2_uses_the_verified_release_and_replays_archive_parser_regressions` — pins the bzip2 vendor record, golden/fuzz vectors, probe, and CI hook.
- `Trust_boundary_text_uses_bounded_owned_storage_and_explicit_capacity_failures` — checks bounded storage and explicit failures at native text trust boundaries.
- `External_size_fields_use_checked_arithmetic_before_allocation_or_io` — requires checked arithmetic before external sizes reach allocation or I/O.
- `Transactional_copy_results_preserve_phase_error_paths_retryability_and_partial_effects_for_legacy_dialogs` — preserves structured copy-phase results and their legacy dialog adapters.
- `File_operation_failures_capture_the_primary_error_before_cleanup_and_offer_copyable_context` — protects primary error capture and copyable diagnostic context.
- `Kernel_handle_ownership_is_scoped_and_preserves_legacy_close_failures` — verifies scoped kernel handles without hiding legacy close errors.
- `Scoped_native_resources_protect_file_operations_and_plugin_boundaries` — checks RAII-style native resource protection at operation and plug-in boundaries.
- `Lock_ordering_has_rank_assertions_timeout_diagnostics_and_a_nightly_verifier_lane` — protects ranked locks, timeout diagnostics, and verifier workflow coverage.
- `Thread_owners_keep_worker_lifetime_stop_completion_naming_com_and_exception_policy_together` — keeps worker lifecycle responsibilities in the common thread-owner abstraction.
- `Crash_report_compression_uses_the_restricted_loader_owned_worker_and_shutdown_contract` — verifies crash compression uses restricted loading, owned work, and safe shutdown.
- `Shared_plugin_thread_queue_uses_owned_cooperative_shutdown_without_forced_termination` — protects cooperative shutdown of the shared plug-in queue.
- `Seven_zip_task_dispatch_owns_worker_completion_cancellation_and_progress_subclass_lifetime` — checks 7-Zip worker, cancellation, completion, and progress-window ownership.
- `Shutdown_deadlines_report_named_phases_and_preserve_shared_state_until_safe_join` — protects named shutdown deadlines and state lifetime through joins.
- `Check_path_workers_use_signaled_work_cancellation_and_deadline_waits` — requires event-driven path workers with cancellation and bounded waits.
- `Monotonic_64_bit_timers_cross_the_32_bit_boundary_and_reject_backward_samples` — checks timer wraparound and backward-sample handling.
- `Win32_path_boundaries_use_owned_wide_paths_and_extended_length_syntax` — protects UTF-16 ownership and extended-length Win32 path handling.
- `Destructive_operations_keep_the_handle_identity_guard` — requires identity revalidation before destructive filesystem actions.
- `Reparse_point_policy_never_traverses_or_hydrates_unselected_targets` — protects the no-traversal/no-hydration reparse-point policy.
- `Copy_engine_uses_unambiguous_64_bit_file_size_and_seek_wrappers` — keeps copy sizes and offsets on explicit 64-bit wrappers.
- `Plugin_readers_preserve_full_file_sizes_and_reject_only_their_explicit_caps` — parameterized checks keep plug-in readers from narrowing file sizes outside documented caps.
- `Split_combine_stages_and_verifies_output_before_publishing_it` — requires staged, verified, write-through split/combine publication.
- `File_operation_planning_uses_an_immutable_plan_and_narrow_filesystem_adapter` — protects immutable plans and the injectable filesystem seam.
- `File_operation_correlation_ids_cross_plan_worker_ui_journal_and_log_boundaries` — requires one correlation ID across operation layers.
- `Central_retry_policy_bounds_transient_read_retries_and_blocks_destructive_commits` — keeps retry attempts bounded and destructive commits gated.
- `Transactional_copy_and_move_expose_each_durable_phase_to_a_deterministic_fault_adapter` — protects fault injection at every copy/move durability phase.
- `Native_destructive_operation_characterization_suite_retains_the_required_scenarios` — prevents removal of required copy, move, rename, delete, recovery, ADS, Find, View, and Edit UI scenarios.
- `Crash_report_uploads_use_certificate_validated_https_with_explicit_consent` — requires explicit consent and validated HTTPS for report uploads.
- `Dynamic_library_loads_use_restricted_search_paths` — checks process and call-site DLL search restrictions.
- `Thumbnail_and_archive_metadata_use_the_restartable_parser_broker` — protects brokered parsing for thumbnail and archive metadata.
- `Plugin_entry_scope_and_callback_state_restore_host_state_after_an_unwinding_entry_point` — verifies plug-in entry unwinding restores callback and host state.
- `Delete_manager_callback_registration_invalidates_before_window_teardown_and_rejects_stale_generations` — protects callback-window invalidation and generation checks.
- `Icon_work_pool_bounds_memory_and_prioritizes_visible_current_generation_work` — checks icon queue bounds, prioritization, and stale-generation rejection.
- `Directory_listing_uses_bounded_checkpoints_and_a_retained_metadata_budget` — requires bounded listing checkpoints and metadata budgets.
- `Allocation_emergency_is_noninteractive_and_defers_recovery_to_the_ui_thread` — protects noninteractive allocation failure handling and UI-thread recovery.
- `Background_producers_bound_non_durable_work_and_report_backpressure` — checks bounded queues and backpressure diagnostics.
- `Plugin_callbacks_are_contained_and_the_failing_plugin_is_deferred_for_unload` — protects callback exception containment and deferred unload.
- `Configuration_saves_stage_validate_and_atomically_select_a_generation` — requires staged, validated, atomic configuration generations.
- `Configuration_profiles_are_schema_versioned_migrated_and_validated_before_loading` — protects profile schema migration and validation.
- `Metadata_preservation_contract_records_losses_and_gates_move_source_deletion` — requires metadata-loss records and user gating before source deletion.
- `Security_descriptor_copy_uses_the_privilege_aware_preservation_matrix` — checks privilege-aware security descriptor preservation.
- `Release_diagnostic_ring_is_bounded_sanitized_and_reported_only_through_the_existing_consent_flow` — protects bounded sanitized diagnostics and consent-based reporting.
- `Network_operations_have_phase_deadlines_cancellation_and_failure_classification` — requires phase-specific network deadlines, cancellation, and error classification.
- `Ftp_downloads_stage_identity_validate_resume_and_publish_only_after_a_durable_commit` — protects staged, resumable, identity-checked FTP publication.
- `Ftp_certificate_exceptions_are_endpoint_bound_expiring_and_pinned` — checks endpoint-bound, expiring certificate exceptions and pinning.
- `Bundled_7zip_uses_26_02_and_preserves_upgrade_compatibility_contract` — pins the 7-Zip version and upgrade compatibility boundary.
- `Bundled_sqlite_uses_a_verified_current_amalgamation_and_exercises_the_owned_database_recovery_contract` — pins SQLite provenance, build options, ownership policy, recovery probe, and CI hook.
- `Bundled_cmark_gfm_uses_the_verified_release_and_the_viewer_rejects_unsafe_or_unbounded_rendering` — pins cmark-gfm provenance and the renderer hardening contract.

### Native UI and filesystem characterization tests

All fixtures in this section require the guarded `FILEMANAGER_UI_TESTDATA_ROOT`, `FILEMANAGER_UI_CONFIG_ROOT`, `FILEMANAGER_UI_EXE`, and an interactive desktop unless a stricter requirement is stated.

#### Containment audit of the file-operation lane

The file-operation lane (`FileOperationUiTests`, `FileAccessUiTests`, and every fixture deriving from
`FileOperationUiTestBase`) drives real destructive commands against a live desktop, so its blast radius was audited
explicitly. The findings below describe what the lane is allowed to touch and how that is enforced in code.

**Everything the lane creates, mutates, or deletes is seeded by the harness.** `FileOperationWorkspace` builds a fresh
`<test-data root>\<GUID>\source` and `...\target` pair per test and writes every file and directory tree the cases
act on. No case in the lane names a path that is not derived from `Workspace.SourcePath`, `Workspace.TargetPath`,
`Workspace.SourceDirectory`, or `Workspace.TargetDirectory`; none reads `%TEMP%`, `%USERPROFILE%`, a special folder,
or a literal drive path.

The boundaries are enforced rather than assumed:

| Boundary | Enforcement |
| --- | --- |
| Test-data root | `UiTestSettings.TestDataRoot` skips the lane unless the path is absolute and its leaf is literally `filemanager-testdata`. |
| Deletion safety | `UiTestSandbox` writes a `.filemanager-testdata-owner` marker into every root it creates and refuses to reuse or delete a directory that lacks it. |
| Registry | Writes are confined to `HKCU\Software\Open Salamander\6.0-filemanager-testdata`; the lane never touches the live configuration root. |
| Temporary files | Each launched process gets `TEMP` and `TMP` redirected below the test-data root. |
| Processes | Teardown kills only the instances the fixture launched, plus crash reporters matched by full path next to the executable under test. |
| Reparse points | Cleanup deletes a junction or symlink itself and never recurses into its target. |

Individual cases that look like they might reach outside do not:

- **Invalid-destination failures.** `Copy_or_move_failure_does_not_modify_source` and
  `Create_directory_failure_keeps_existing_file_intact` submit a destination *inside* the workspace that cannot
  succeed: `blocked-target` is seeded as a file, so creating a child below it fails without leaving the sandbox.
- **Find.** `Find_files_searches_subdirectories_from_the_active_panel` sets the search root explicitly to
  `Workspace.SourceDirectory` before searching, so recursion cannot escape the workspace. It only reads.
- **View.** The internal viewer opens a seeded file and is closed by the test. It only reads.
- **Long paths and Unicode.** `Unicode_normalization_surrogate_and_long_path_operations_preserve_distinct_entries`
  builds the deepest path the product accepts, still entirely below the workspace.
- **Recycle Bin.** `Delete_to_recycle_bin_...` is opt-in (`FILEMANAGER_UI_RECYCLE_BIN=1`) and `ShellRecycleBin`
  exposes only `GetItemCount`. The case leaves one harness-created file in the bin and never empties or restores it.
- **Second volumes.** The runner automatically uses `D:\filemanager-testdata` when fixed writable `D:\` is
  available and different from the primary sandbox. NTFS supports the ordinary cross-volume cases; the
  ADS-unsupported case runs only on FAT/FAT32/exFAT. Missing or unusable `D:\` produces an explicit successful skip
  for every test that depends on a second volume. Any selected root is registered as an owned root so the same
  marker and cleanup rules apply.

##### The external editor, and why the lane ships its own

One case did reach outside: `Edit_file_opens_the_selected_file_in_the_configured_editor`. The product seeds
`notepad.exe` for `*.*` (`src/mainwnd_init.cpp`), and on Windows 11 that resolves to the packaged, tabbed Notepad.
Measured on a Windows 11 host, opening a second file produces another process but still **one** window, whose caption
becomes the newly opened file. The case matched a desktop window by caption and then closed it, so with Notepad
already open it could close a window holding unsaved documents belonging to whoever was running the tests. The same
launch also pinned the workspace as the editor's working directory, which blocked sandbox cleanup afterwards.

The lane now supplies its own editor:

- `tests/FileManager.UiTests.EditorStub` builds `SandboxEditor.exe`, a GUI-subsystem WinForms app that shows the
  opened file in a window titled `SandboxEditor - <file name>`. It is a GUI app deliberately: Windows Terminal is the
  default console delegate on Windows 11 and shares one tabbed window between launches, which would reproduce the
  same hazard with a console stub.
- `SandboxEditor.Install()` copies it into `<test-data root>\editor`, so the editor binary itself lives in the
  disposable sandbox and is removed with it.
- `ConfigurationEditorPage.RewriteSelectedEditor` points the profile at that copy **through the real Configuration
  dialog**. A committed configuration generation is checksum-protected, and the product correctly rejects a
  hand-edited generation and falls back to the previous profile
  (`SelectCommittedConfigurationGeneration` in `src/mainwnd_config.cpp`), so the registry must never be written
  directly. The page is located by content, not by a translated tree label: only the Editors page carries a command,
  and the seeded value is `notepad.exe` while every seeded viewer entry is empty.
- `SandboxEditor.WaitForProfileEntry` waits for the committed generation to record the change before the restart,
  because the product writes its profile after the property sheet closes.
- The stub moves its own working directory out of the workspace at startup, and teardown kills any stub still running
  from the sandbox copy, matched by full image path so an editor belonging to the current user is never affected.

If the stub is missing the case is skipped with an explicit message; it never falls back to the machine editor.

##### Journals must not leak between cases

Teardown kills the application, which can interrupt an operation and leave an incomplete journal in the shared
`<test-data root>\appdata\Open Salamander\operation-journals` folder. The next start then raises the modal
**Recover file operations** prompt, which owns the main window. `WaitForNativeMainWindow` deliberately skips a
disabled main window, so a single leaked journal strands every remaining case in the run behind a window that never
becomes usable. `FileOperationUiTestBase` therefore purges that folder both after the application stops and before
the next start, retrying briefly because an exiting process can hold its journal open for a moment.
`OperationRecoveryCharacterizationUiTests` seeds a journal on purpose and overrides `BeforeFileManagerStarted`
without calling base, so its own scenario is unaffected.

For the same reason the harness now acknowledges the **Open Salamander Configuration** notice at startup: an
interrupted profile write makes the product report that it fell back to the last verified profile, through an
owner-less message box shown before the main window exists.

##### Running the lane

The lane takes over the desktop: it opens modal confirmation prompts, drives selection, and launches the editor. Run
it on a session that can be left alone, not alongside interactive work.

##### The retained execution transcript

The harness creates one UTF-8 `.log` file during `[SetUp]` for every case that passes the sandbox guard and is about
to launch `salamand.exe`. It starts before sandbox initialization and closes either from the setup-failure handler
or from `[TearDown]`, so setup, test-body, and teardown failures are all retained. The file is attached to the NUnit result and remains under
`FILEMANAGER_UI_TRANSCRIPT_ROOT`; `runtests.ps1` sets that to a GUID-named `TestResults\ui-test-transcripts-*`
directory. Direct IDE or `dotnet test` runs use `<NUnit work directory>\TestResults\ui-test-transcripts` unless the
variable is set explicitly. A prerequisite or category skip that never attempts to launch the executable does not
create an empty log.

Each record has a monotonically increasing sequence number, elapsed seconds from test setup, an absolute UTC
timestamp, a category, and details. Read from the header downward to reconstruct the test in order. The most useful
categories are:

- `HARNESS`, `SANDBOX`, and `NUNIT`: test identity, executable and arguments, sandbox boundaries, restarts, and the
  NUnit outcome, failure message, and stack trace visible when teardown begins.
- `PROCESS`: launch, PID, observation, deliberate termination, and exit of `salamand.exe` and its sibling
  `salmon.exe` crash reporter.
- `ACTION` and `WAIT`: user-equivalent native commands, quick searches, control/button operations, and the start and
  completion of window waits. Dynamic command IDs remain numeric when no stable host name exists.
- `WINDOW`: an opened, changed, closed, or final-snapshot top-level HWND. Dialog and crash-reporter records include
  their title plus a textual control tree (control ID, class, visible/enabled state, and text). The large volatile
  main-panel child tree is intentionally omitted. Password-style edit controls are written as
  `<redacted-password>`.
- `PRODUCT-DIALOGS`: the native product transcript tailed into the retained log while the test runs, followed by a
  final line-count/path record before the disposable sandbox is deleted. This is authoritative for product dialogs
  too short-lived to meet the 100 ms HWND observer.
- `CRASH-REPORT`, `CRASH-REPORT-TEXT`, and `OBSERVER-ERROR`: retained crash-artifact metadata and bounded textual
  `.TXT`, `.INF`, `.OPS`, or `.BUG` content, plus any diagnostic observer failure. Binary `.DMP` and `.7Z` files are
  copied beside the log in a `.artifacts` directory and represented in the log by path, size, and modification time.

For a hang, start at the final `ACTION` or `WAIT`, then inspect the following `WINDOW` and `PRODUCT-DIALOGS` records.
A product-side `SHOW` with no matching `RESULT` identifies an unanswered prompt. For a
crash, find the `salamand.exe` exit, read the `salmon.exe` window/control text, then follow the retained artifact
paths. A passing case ends with `teardown.complete` and `transcript.complete`; absence of those markers means the
test host itself stopped before normal finalization. Secrets supplied through password edit controls or launch
environment overrides are never logged.

The `PRODUCT-DIALOGS` section comes from a second, product-side safety net. The lane's hardest failures were all the
same shape: a case timed out waiting for something, and polling alone could not say whether the application had
asked a question, asked a different one, or asked nothing at all. The product writes every dialog it raises while
the sandbox is active:

- `LogUiTestDialog` (`src/path_checking.cpp`) appends one line per event to `<test-data root>\ui-test-dialogs.log`,
  with a timestamp, `SHOW` or `RESULT`, the flags, the chosen button, the caption and the text. It is a no-op unless
  `IsFileManagerUiTestSandboxRequested()`, and the file is shared for reading so a run can be watched live.
- `CMessageBox::Execute` (`src/msgbox.cpp`) records every message box, including the ones built directly instead of
  through `SalMessageBox` — the delete confirmation is one of those.
- The worker's `WM_USER_DIALOG` handler (`src/dialogs_file_ops.cpp`) records every operation prompt through
  `LogUiTestOperationDialog`. Those are custom templates rather than message boxes, so nothing else sees them. Only
  the array slots a given dialog kind really passes as strings are read, because several kinds carry DWORD or BOOL
  values in the same positions.

A `SHOW` with no matching `RESULT` names exactly the prompt a stalled run is waiting on. The harness imports this
file before `filemanager-testdata` is removed, so it is no longer lost at teardown. That single signal
identified four separate causes: a metadata-preservation gate raised once per item, an unanswered delete
confirmation, the shell's own "File In Use" window appearing instead of the product's error dialog, and an overwrite
prompt proving that a copy had been dispatched for the wrong item.

##### Two product defects the lane found

Both are fixed, and neither was visible without the transcript.

- **The Recycle Bin owned the error UI.** The worker's recycle-bin delete passed `FOF_SILENT`, which hides only the
  progress dialog. A locked file made the shell raise its own **File In Use** window, taking the failure out of the
  operation's Retry/Skip/Skip All contract and blocking the worker on a modal the product does not own.
  `RunRecycleBinDeleteOnSta` now also passes `FOF_NOERRORUI` and reads the per-item result through an
  `IFileOperationProgressSink`, because `PerformOperations` reports the batch rather than the item. The recycle-bin
  branches additionally verify through a handle opened for deletion (`VerifyFileDeletable`): an attribute-only open
  bypasses the sharing check entirely, so the previous check accepted a file nobody could delete.
- **Overwrites must not inherit stale alternate data streams.** Conditional publication now moves the approved
  destination to an owned backup, then renames only the staged file into an empty destination name. It never
  merges the old file's streams. The existing ADS overwrite case still requires destination-only streams to
  disappear and source streams to survive; post-commit stream enumeration/deletion is no longer needed.

##### Two product limits the lane respects

Neither is fixed, and neither is something the lane should assert away. Both are recorded here because a case that
crosses one fails for a reason that has nothing to do with what it is testing.

- **Paths stop at `PATH_MAX_PATH`.** A full directory path is capped at 248 characters including the terminator
  (`src/plugins/shared/spl_gen.h`), and the script builder rejects anything longer with **Error Building Script**.
  Support for paths beyond `MAX_PATH` is listed as outstanding in `refactoring.md`. The Unicode case therefore
  computes how many path segments fit inside that budget instead of hard-coding a depth, so it probes the boundary
  the product actually claims.
- **Dialog text stops at the ANSI code page.** Every dialog is still an ANSI window — `CDialog` is only ever
  constructed with `unicodeWnd` false (`src/common/winlib.h`) — so text placed in an input control is round-tripped
  through the ANSI code page. A supplementary-plane character reaches the control as `?`, and the product would
  rename the file to that. Copying and deleting a name carrying a surrogate pair works and is covered, because those
  paths never pass the name through a dialog; only the rename target avoids one.

##### Known state on a developer workstation

Measured on a Windows 11 developer machine (Release x64 build, v145 toolset). "Before" is the state when this work
started, "containment" is after the sandbox and journal work described above, and "after" is current.

| Batch | Before | After containment | After |
| --- | --- | --- | --- |
| `FileOperationUiTests` Copy | 2 passed / 11 failed | 7 / 6 | 13 / 0 |
| `FileOperationUiTests` Move + Rename | 2 / 10 | 5 / 7 | 12 / 0 |
| `FileOperationUiTests` Delete, Create, Cancel, Unicode | 7 / 5 | 7 / 5 | 12 / 0 |
| `FileAccessUiTests` | 1 / 2 | 3 / 0 | 3 / 0 |

The lane passes 40 of 40 locally. The first jump came from removing the journal and configuration-notice cascades,
which turned one failing case into a run-long series of "did not expose its main window" timeouts. The second came
from four harness defects, plus the two product defects above:

- **Text never reached the control.** `SetOperationPath` used `SetWindowText`, which across a process boundary
  updates the cached window title that `GetWindowText` then reads back. The write and its verification agreed with
  each other while the control stayed empty and the application read nothing. Copy and Move hid this because their
  default already names the wanted destination; Create Directory and Rename were submitted with the default instead.
  Both now go through `WM_SETTEXT`/`WM_GETTEXT`, which repaired five cases at once.
- **The delete confirmation was never answered.** `CMessageBox` assigns its button IDs after the buttons are created,
  so UI Automation still reports the template placeholder and a search for `IDYES` found nothing. The confirmation
  stood open and the delete silently never ran. It is now found and clicked natively, and the click is posted rather
  than sent, because answering it runs the whole operation inside the application's message handling.
- **A gated prompt is raised per item.** The metadata-preservation gate that removal of a moved source sits behind
  names one item at a time, so answering it once left the rest of the tree waiting.
  `WaitForFileSystemAnsweringQuestions` answers while it waits for the outcome.
- **Fixtures raced the panel.** Alternate-data-stream files created inside a case were not listed yet when
  quick-search ran, so the operation acted on nothing. They are seeded with the rest of the workspace.

Selection deserves its own note. The panel is driven by incremental search, so a fixture whose name is only a prefix
of another cannot be addressed on its own: the harness, not the product, would decide which file an operation acted
on. The two Unicode normalization fixtures are therefore given distinct ASCII prefixes. They still differ only by
the normalization of the same grapheme, which is what that case is about.

A timeout now names the windows that were open, which is what separates "the prompt never appeared" from "a
different prompt appeared".

### Per-test native UI catalog

This catalog documents every automated test that drives the native FileManager UI (FlaUI/UIA3), plus the one loopback FTP case that opens the product Connect dialog. Source-contract NUnit cases that never launch `salamand.exe` remain in [NUnit source-contract and integration tests](#nunit-source-contract-and-integration-tests). The four `DeterministicNetworkFixtureTests` loopback sockets that never open a FileManager window are listed only in the PowerShell/native probe table.

Shared assumptions for every native UI case unless a test states otherwise:

- An interactive current-user Windows desktop, UIA3, and a complete deployed runtime (`salamand.exe`, `salmon.exe`, and the plug-in payloads the selected case needs).
- Guarded `FILEMANAGER_UI_TESTDATA_ROOT` (leaf name `filemanager-testdata`), `FILEMANAGER_UI_CONFIG_ROOT`, and `FILEMANAGER_UI_EXE`.
- Fixtures are non-parallel; they take over the desktop (modals, selection, optional editor) and must not run beside interactive work.
- File-operation cases start with `-l`/`-r` on a fresh GUID workspace and select items through the panel’s ANSI incremental search, so fixture names must not be prefixes of other listed names.
- Dialogs are located by native command IDs and resource/automation IDs, not translated menu text.
- The lane never empties the Recycle Bin, never writes the live configuration hive, and never falls back to the machine’s default editor.

Shared out-of-scope aspects unless a test states otherwise: visual layout and theming, performance/throughput, accessibility beyond UIA3 discovery, plug-in installation, localization of captions, operations outside the sandbox, paths longer than `PATH_MAX_PATH`, supplementary-plane names typed into ANSI dialogs, and live network except the dedicated FTP cases.

#### `BasicUiTests` — risk-based lifecycle matrix

`Basic_ui_scenario` is one NUnit method parameterized by `BasicUiScenarios.All`. The seven generated names are categorized `UI` and `LockStress` so the nightly verifier lane can repeat this compact matrix independently of the release gate. Prolonged soak belongs to a separately scheduled run, not these seven cases.

##### `UI_001_MainWindow_Cold_start`

- **Does:** Launches FileManager into the isolated profile and inspects the native main window.
- **Tests:** Cold-start attachment of UIA3 to the top-level owner window.
- **Confirms:** The main window has a non-zero HWND, a non-empty title, is enabled, has a non-empty bounding rectangle, and can take focus.
- **Assumptions:** Shared sandbox and executable; no leftover modal (journal recovery or configuration fallback) owns the main window.
- **Out of scope:** Panel contents, menus, plug-in load success, and any command other than existing as a usable window.

##### `UI_002_AccessibilityTree_Owner_drawn_accessibility`

- **Does:** Enumerates UIA3 descendants of the main window after launch.
- **Tests:** That the native UI exposes an automation tree at all.
- **Confirms:** `FindAllDescendants()` returns at least one element.
- **Assumptions:** Shared sandbox; the owner-drawn native menu is not required to appear as a UIA `MenuBar`.
- **Out of scope:** Completeness of the tree, names/roles of controls, keyboard navigation, and screen-reader correctness.

##### `UI_003_ConfigurationCancel_Discarded_settings`

- **Does:** Opens Configuration by command ID, dismisses it without committing, and re-checks the main window.
- **Tests:** Cancel/close of the configuration property sheet.
- **Confirms:** The dialog can be opened and cancelled; the main window remains usable afterwards.
- **Assumptions:** Shared sandbox; Configuration command ID is valid in the deployed build.
- **Out of scope:** Whether any setting actually changed, persistence, and which page was visible.

##### `UI_004_ConfigurationCommit_Committed_settings`

- **Does:** Opens Configuration and accepts it without changing a control, then re-checks the main window.
- **Tests:** The commit/close lifecycle of the configuration property sheet.
- **Confirms:** Accepting Configuration does not disable or lose the main window.
- **Assumptions:** Shared sandbox; a no-op commit is a valid configuration generation.
- **Out of scope:** Whether the writer produced a new generation, checksum contents, and any visible setting change.

##### `UI_005_ConfigurationPersistence_Persisted_settings_restart`

- **Does:** Toggles the first Configuration checkbox, commits, waits for the asynchronous profile write, restarts FileManager, reopens Configuration, asserts the toggled value, then restores the original value.
- **Tests:** End-to-end persistence of one committed boolean through process restart.
- **Confirms:** The first checkbox value after restart matches the committed opposite of the original; the fixture restores independence for later cases.
- **Assumptions:** The first checkbox is a durable, round-trippable setting; the product finishes writing the profile after the property sheet closes (`WaitForConfigurationClearReadOnlyPersistence`).
- **Out of scope:** Other Configuration pages, migration of older schemas, crash-during-write recovery (see `ConfigurationRecoveryUiTests`), and UI layout of the sheet.

##### `UI_006_RestartAfterCommit_Restart_after_settings_commit`

- **Does:** Commits Configuration with no intended setting change, restarts FileManager, and re-asserts the main window.
- **Tests:** Clean process restart after a configuration commit.
- **Confirms:** A post-commit restart still exposes an enabled, titled, sized main window.
- **Assumptions:** Shared sandbox; commit does not leave a blocking modal.
- **Out of scope:** Persistence of a specific setting (covered by `UI_005`) and resource-leak budgets (covered by `LifecycleLeakUiTests`).

##### `UI_007_FtpBookmarkCreationPersists_Plugin_profile_persistence`

- **Does:** Opens FTP Organize Bookmarks, creates a unique bookmark, renames it, waits for the organizer list and then the isolated profile commit, restarts, and reopens the organizer.
- **Tests:** FTP plug-in bookmark create/rename persistence across restart.
- **Confirms:** The edited unique name is present in the organizer both before Close persists the collection and after a full restart.
- **Assumptions:** The FTP plug-in is deployed; `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` (or the runner-discovered equivalent) identifies Organize Bookmarks; the owner-drawn list is queried natively rather than through UIA item text.
- **Out of scope:** Connecting to a server, transferring files, certificate handling, and bookmark fields other than the displayed name.

#### `FileAccessUiTests`

##### `Find_files_searches_subdirectories_from_the_active_panel`

- **Does:** Opens Find Files, sets the mask to `find-target.txt`, roots the search at `Workspace.SourceDirectory`, enables subdirectory search, disables other options that would change semantics, and clicks Find Now.
- **Tests:** Recursive Find from the workspace source panel using stable control IDs (`2505`, `2501`, `2503`, `2508`, results `2510`).
- **Confirms:** The results list contains exactly one row for the unique nested file.
- **Assumptions:** The workspace seed includes that unique nested name; Find options are forced so a prior case’s saved Find state cannot change the search.
- **Out of scope:** Content search, regular expressions, archives, other drives, result activation, and replacing files from Find.

##### `View_file_opens_the_selected_file_in_the_internal_viewer`

- **Does:** Selects `view-file.txt` and issues View File.
- **Tests:** Dispatch of the internal viewer for a selected text file.
- **Confirms:** A window of class `Salamander's Viewer Window` opens whose caption contains the file name; the test then closes only that window.
- **Assumptions:** Internal viewer is available; selection via quick-search hits the seeded file.
- **Out of scope:** Rendered text, encodings, hex view, plugins in the viewer, and printing.

##### `Edit_file_opens_the_selected_file_in_the_configured_editor`

- **Does:** Rewrites the Editors page in Configuration to `SandboxEditor.exe` (copied under the test-data root), waits for the checksum-protected profile, restarts, selects `edit-file.txt`, and issues Edit File.
- **Tests:** Files > Edit dispatch to the configured external editor through the real Configuration UI.
- **Confirms:** A process other than FileManager shows a window titled `SandboxEditor - edit-file.txt`. The test closes only that owned window.
- **Assumptions:** `SandboxEditor.exe` built by `FileManager.UiTests.EditorStub` is present; Configuration Editors page is identified by content (command field seeded as `notepad.exe`); the stub must not be skipped in favor of the machine editor. Windows 11 packaged Notepad is unsafe as an assertion target because it can reuse one tabbed window.
- **Out of scope:** Saving from the editor, associations other than `*.*`, DDE, and whatever editor the current user has installed.

#### `FileOperationUiTests`

All cases below seed a shared workspace (files, trees, ADS, conflict counterparts, `blocked-target` as a file). Unicode/long-path names are seeded before startup so the panel lists them. Operations wait for destination handles to close before reading content.

##### `Create_directory_creates_requested_nested_directory`

- **Does:** Submits Create Directory with path `created\nested` and answers the intermediate-folder `MB_OKCANCEL` prompt with IDOK.
- **Tests:** Nested directory creation through the native dialog, including the “create the whole branch” confirmation.
- **Confirms:** `created\nested` exists under the source panel path.
- **Assumptions:** The intermediate folder does not already exist; the affirmative button is IDOK, not IDYES.
- **Out of scope:** Permissions failures, existing-directory no-ops, and names that do not round-trip through the ANSI dialog.

##### `Copy_file_copies_content_to_other_panel`

- **Does:** Copies `copy-file.txt` to the other panel.
- **Tests:** Single-file copy to the opposite panel path.
- **Confirms:** Target content is `copy-file-content`; the source file still exists.
- **Assumptions:** Same-volume workspace; default destination in the copy dialog is the other panel.
- **Out of scope:** Overwrite, ADS, attributes other than content, and progress UI.

##### `Copy_preserves_last_write_time_metadata`

- **Does:** Sets source last-write time to a fixed UTC timestamp, copies, and compares timestamps after the destination handle is released.
- **Tests:** Preservation of last-write time on copy.
- **Confirms:** Target last-write UTC equals the source’s after copy.
- **Assumptions:** The volume supports the timestamp precision used; metadata is only asserted after the worker closes the destination.
- **Out of scope:** Creation time, access time, security descriptors, and ADS timestamps.

##### `Copy_preserves_multiple_empty_large_and_edge_named_alternate_data_streams`

- **Does:** Copies `ads-copy.txt` after requiring ADS support on both workspace roots.
- **Tests:** Copy of default data plus named, empty, large, and dotted-name streams.
- **Confirms:** Default content and each seeded stream (`notes`, `empty`, `large`, `edge name.with.dots`) match on the target.
- **Assumptions:** Category `AlternateDataStreams`; both panels are on an ADS-capable filesystem (typically NTFS).
- **Out of scope:** Streams the product cannot name, encryption, and sparse-file semantics.

##### `Copy_overwrite_replaces_target_streams_and_removes_stale_streams`

- **Does:** Copies `ads-overwrite.txt` onto a target that already has different default data and a `stale` stream; confirms overwrite with IDYES.
- **Tests:** Confirmed overwrite of ADS: replacement streams appear and streams that existed only on the old target are absent after conditional publication.
- **Confirms:** Default data becomes `ads-overwrite-source`; `replacement` is present; `stale` is absent.
- **Assumptions:** ADS support on both volumes; the overwrite prompt is the operation prompt with IDYES.
- **Out of scope:** Skip/Skip All, and copy to a filesystem that cannot store ADS.

##### `Copy_retries_a_temporarily_denied_alternate_data_stream_without_losing_it`

- **Does:** Holds the source stream `temporarily-denied` open for read, starts copy, waits for Retry, releases the lock, clicks Retry.
- **Tests:** Retry after a sharing violation on an alternate stream.
- **Confirms:** After the destination is released, stream `temporarily-denied` contains `retry-stream-content`.
- **Assumptions:** ADS support; the worker surfaces IDRETRY rather than a shell “File In Use” window.
- **Out of scope:** Permanent denial, Skip of the stream, and default-stream locks.

##### `Copy_overwrite_replaces_the_existing_target_only_after_the_user_confirms`

- **Does:** Copies `overwrite-file.txt` onto an existing different target and confirms IDYES.
- **Tests:** Single-file overwrite confirmation.
- **Confirms:** Target content becomes `overwrite-source-content`; source is unchanged.
- **Assumptions:** A conflict counterpart was seeded in the target panel.
- **Out of scope:** Overwrite All, timestamps, and ADS.

##### `Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree`

- **Does:** Copies `overwrite-all-tree` and answers the first conflict with Overwrite All (`IDB_ALL` / 185).
- **Tests:** Overwrite All applied to every conflicting descendant without further prompts.
- **Confirms:** `nested\first.txt` and `nested\second.txt` on the target match the source contents.
- **Assumptions:** The tree was seeded with conflicts on those descendants.
- **Out of scope:** Mix of overwrite and skip, and metadata-loss prompts.

##### `Copy_skip_keeps_the_existing_target_and_the_source`

- **Does:** Copies `skip-file.txt` and chooses Skip (`IDB_SKIP` / 173).
- **Tests:** Single-file Skip on copy conflict.
- **Confirms:** Target remains `skip-target-content`; source remains `skip-source-content`.
- **Assumptions:** Conflict counterpart seeded.
- **Out of scope:** Skip All and partial tree copy.

##### `Copy_skip_all_keeps_the_existing_conflicting_tree`

- **Does:** Copies `skip-all-tree` and chooses Skip All (`IDB_SKIPALL` / 174).
- **Tests:** Skip All for a conflicting directory tree.
- **Confirms:** Target nested files keep their original target contents; source files still exist.
- **Assumptions:** Entire nested tree conflicts.
- **Out of scope:** Nonconflicting siblings (see move Skip All) and source deletion.

##### `Copy_file_persists_a_completed_recovery_journal_with_item_intent`

- **Does:** Copies `copy-file.txt` and reads the durable `.opj` journal under the sandbox journal directory (shared-read, because the worker may still hold it).
- **Tests:** Completed-copy journal shape: plan snapshot, item intent, correlation ID, prepared/committed states, and `OPERATION|completed`.
- **Confirms:** A journal naming the source contains `OPERATION|completed`, a `PLANITEM` with source and target, a correlation ID reused on plan and attempt lines, and `STATE` prepared/committed records.
- **Assumptions:** Journals are redirected into the test-data root; completion is appended after planning, so the test waits for the completed record rather than a prefix.
- **Out of scope:** Crash recovery from this journal (see `OperationRecoveryCharacterizationUiTests`), journal encryption, and multi-item plans.

##### `Copy_directory_copies_all_descendants_to_other_panel`

- **Does:** Copies directory `copy-tree`.
- **Tests:** Recursive directory copy.
- **Confirms:** `copy-tree\nested\payload.txt` on the target is readable with `copy-tree-content`; the source tree remains.
- **Assumptions:** Nested payload was seeded.
- **Out of scope:** Junctions/symlinks (see `ReparsePointTopologyUiTests`) and empty directories as the only coverage.

##### `Ansi_round_trippable_unicode_and_long_path_operations_preserve_distinct_entries`

- **Does:** Copies two ANSI-round-trippable accented names (`é`, `ö`) selected by distinct ASCII prefixes, copies a deep tree budgeted to 247 usable path characters, then renames the first file and deletes the second.
- **Tests:** Copy/rename/delete of non-ASCII names that survive the process ANSI code page, plus a deep path at the product `PATH_MAX_PATH` budget; file IDs prove copies are new identities.
- **Confirms:** Both copied files and the long-path payload have the expected content and different identities from their sources; rename keeps `first-unicode-content` under the new accented name; delete removes only the second source name.
- **Assumptions:** Category `Unicode`; Western/Central-European ANSI code pages round-trip `é`/`ö`; panel quick-search is itself ANSI, so unique ASCII prefixes are required; depth is computed from the sandbox root rather than hard-coded.
- **Out of scope:** NFC vs NFD collapsing, supplementary-plane/surrogate names in dialogs (product gap), and paths longer than `PATH_MAX_PATH`.

##### `Rename_file_renames_without_changing_content`

- **Does:** Renames `rename-file.txt` to `renamed-file.txt`.
- **Tests:** Simple file rename in the source panel.
- **Confirms:** New name exists with original content; old name is gone.
- **Assumptions:** No colliding target name.
- **Out of scope:** Extension-change associations and undo.

##### `Rename_directory_preserves_all_descendants`

- **Does:** Renames `rename-tree` to `renamed-tree`.
- **Tests:** Directory rename retaining descendants.
- **Confirms:** Nested payload exists under the new name with original content; old directory is gone.
- **Assumptions:** Nested payload seeded.
- **Out of scope:** Cross-volume rename and junction targets.

##### `Rename_case_only_change_preserves_the_file_and_updates_its_displayed_name`

- **Does:** Renames `rename-case.txt` to `RENAME-CASE.txt`.
- **Tests:** Case-only rename on a case-insensitive volume treated as identity, not overwrite.
- **Confirms:** Directory enumeration shows `RENAME-CASE.txt`; content is unchanged.
- **Assumptions:** NTFS-style case-preserving volume; the product does not treat this as a collision.
- **Out of scope:** Truly case-sensitive filesystems and Explorer display vs on-disk name mismatches elsewhere.

##### `Rename_overwrite_replaces_the_collision_without_losing_source_metadata`

- **Does:** Sets a known last-write time on `rename-overwrite.txt`, renames onto `rename-overwrite-target.txt`, confirms IDYES.
- **Tests:** Confirmed file-rename overwrite plus timestamp carry-over.
- **Confirms:** Target content is the source content; source name is gone; last-write UTC matches the prepared timestamp.
- **Assumptions:** Collision file seeded; overwrite prompt uses IDYES.
- **Out of scope:** Directory collisions (rejected separately) and ADS on rename.

##### `Move_file_moves_content_to_other_panel`

- **Does:** Moves `move-file.txt` to the other panel after asserting source and target share a volume root.
- **Tests:** Same-volume file move.
- **Confirms:** Source is gone; target content is `move-file-content`.
- **Assumptions:** Default workspace is same-volume (cross-volume is a different fixture).
- **Out of scope:** Copy-then-delete across volumes and Recycle Bin.

##### `Move_directory_moves_all_descendants_to_other_panel`

- **Does:** Moves `move-tree`.
- **Tests:** Same-volume recursive move.
- **Confirms:** Nested payload is at the target with original content; source directory is gone.
- **Assumptions:** Same-volume workspace.
- **Out of scope:** Cross-volume metadata gates.

##### `Move_overwrite_replaces_the_existing_target_and_removes_the_source`

- **Does:** Moves `move-overwrite.txt` onto an existing target and confirms IDYES.
- **Tests:** Confirmed move overwrite: destination replaced before source identity is removed.
- **Confirms:** Target content is `move-overwrite-source-content`; source no longer exists.
- **Assumptions:** Collision seeded on the target panel.
- **Out of scope:** Skip, Overwrite All, and ADS.

##### `Move_skip_keeps_the_existing_target_and_the_unmoved_source`

- **Does:** Moves `move-skip.txt` and chooses Skip.
- **Tests:** Skipped move collision.
- **Confirms:** Target keeps `move-skip-target-content`; source keeps `move-skip-source-content`.
- **Assumptions:** Collision seeded.
- **Out of scope:** Partial tree moves.

##### `Move_overwrite_all_replaces_every_conflict_before_removing_the_source_tree`

- **Does:** Moves `move-overwrite-all-tree`, chooses Overwrite All, then answers per-item metadata-preservation questions with IDYES while waiting for the source tree to disappear.
- **Tests:** Overwrite All on move plus the metadata-preservation gate that must complete before source-tree deletion.
- **Confirms:** Conflicting descendants on the target match source contents; the source tree is eventually gone.
- **Assumptions:** The metadata gate is raised once per affected item; `WaitForFileSystemAnsweringQuestions` must keep answering rather than clicking once.
- **Out of scope:** Declining metadata loss (source would remain) and ADS-specific prompts.

##### `Move_skip_all_retains_conflicting_sources_but_moves_nonconflicting_siblings`

- **Does:** Moves `move-skip-all-tree` and chooses Skip All.
- **Tests:** Skip All that still commits independently nonconflicting siblings.
- **Confirms:** Conflicting target files unchanged; conflicting sources remain; `unique.txt` exists only on the target.
- **Assumptions:** Seeded mix of conflicting nested files and one unique sibling.
- **Out of scope:** Overwrite All and metadata-loss dialogs.

##### `Delete_file_removes_the_selected_file`

- **Does:** Selects `delete-file.txt`, issues Delete, confirms the delete message box if shown.
- **Tests:** Permanent (or profile-default) deletion of one file from the panel.
- **Confirms:** The source file no longer exists.
- **Assumptions:** Isolated profile delete confirmation can be answered via native button click (UIA IDs are placeholders); default may be Recycle Bin unless other tests changed the profile—this case only asserts the path is gone, not the bin.
- **Out of scope:** Recoverability, locked files, and mixed selection.

##### `Delete_directory_removes_all_descendants`

- **Does:** Deletes `delete-tree` after confirmation.
- **Tests:** Recursive directory delete.
- **Confirms:** The directory tree is gone.
- **Assumptions:** Confirmation answered; no open handles on descendants.
- **Out of scope:** Junction delete policy (separate fixture) and Recycle Bin item count.

##### `Delete_mixed_selection_removes_the_selected_file_and_directory_tree`

- **Does:** Selects `delete-mixed-file.txt` and `delete-mixed-tree` together and deletes.
- **Tests:** A delete plan that includes both a file and a recursive directory.
- **Confirms:** Both the file and the directory tree are gone.
- **Assumptions:** Multi-select via sequential quick-search and toggle works on the owner-drawn list.
- **Out of scope:** Partial failure in the mixed set (see locked-file skip).

##### `Delete_to_recycle_bin_removes_the_source_and_creates_a_recoverable_shell_item`

- **Does:** Records Recycle Bin item count for the volume, deletes `recycle-file.txt`, waits until the source is gone and the count increased.
- **Tests:** Delete-to-bin through the product when Recycle Bin is enabled.
- **Confirms:** Source path is gone; shell item count for that volume root is higher than before.
- **Assumptions:** Category `RecycleBin`; `FILEMANAGER_UI_RECYCLE_BIN=1`; the user/profile uses the default Recycle Bin; `ShellRecycleBin` only reads `GetItemCount` and never empties or restores; one harness file is left in the bin.
- **Out of scope:** Restore from the bin, which item appeared, other volumes’ bins, and `FOF_*` flags beyond what the product already sets.

##### `Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation`

- **Does:** Starts copy of `cancel-conflict.txt`, waits for the overwrite prompt (proving the worker is in progress), cancels via the progress window (`RequestCancellation`), then dismisses the still-open conflict with IDCANCEL and confirms cancellation if prompted.
- **Tests:** In-progress cancellation at a conflict, as opposed to declining the conflict (which journals as failure).
- **Confirms:** Target still has `cancel-conflict-target-content`; source still has source content; journal contains `OPERATION|cancelled`.
- **Assumptions:** Progress-window cancel is what writes `OPERATION|cancelled`; the worker cannot unwind until the parked conflict prompt is answered.
- **Out of scope:** Mid-byte cancellation without a prompt, and kill-process recovery.

##### `Cancelling_operation_dialog_leaves_source_and_target_unchanged` (four cases)

Parameterized: Create Directory (`cancelled-directory`), Copy (`cancel-copy.txt`), Move (`cancel-move.txt`), Rename (`cancelled-rename.txt`).

- **Does:** Opens the corresponding operation dialog and closes it without committing (`commit: false`).
- **Tests:** Dialog-level cancel before the worker starts.
- **Confirms:** Create does not create the directory; copy/move do not create a target; rename does not create the new name; existing sources remain.
- **Assumptions:** Cancel is the dialog Close/Cancel path, not progress-window cancel.
- **Out of scope:** Cancel after the worker has started (see the conflicting-copy case).

##### `Create_directory_failure_keeps_existing_file_intact`

- **Does:** Submits Create Directory with name `create-collision`, which is already a file, then cancels the resulting failure UI.
- **Tests:** Failed create-as-directory against an existing file inside the workspace.
- **Confirms:** File content remains `create-collision-content`.
- **Assumptions:** Failure stays on the sandbox path; the harness uses `SubmitInvalidPathAndCancel`.
- **Out of scope:** Privilege errors and disk-full.

##### `Copy_or_move_failure_does_not_modify_source` (two cases)

Parameterized: Copy `copy-file.txt` and Move `move-file.txt` to `blocked-target\child.txt`, where `blocked-target` is a file.

- **Does:** Submits an impossible child path under a file and cancels the failure.
- **Tests:** Invalid destination for copy and move without leaving the sandbox.
- **Confirms:** Source still exists; `blocked-target` still exists as the seeded file.
- **Assumptions:** Destination is inside the workspace; it cannot succeed, so no child is created outside the sandbox.
- **Out of scope:** Network paths, ACL denial, and partial copy rollback beyond “source untouched.”

##### `Rename_overwrite_decline_keeps_the_original_file_and_existing_target`

- **Does:** Renames `rename-file.txt` onto `rename-collision.txt`, chooses IDNO on the overwrite prompt, then cancels the reopened rename dialog.
- **Tests:** Rename-specific skip (IDNO) returning to the rename dialog without mutating files.
- **Confirms:** Original and collision contents are unchanged.
- **Assumptions:** IDNO is skip for rename, not the copy/move Skip control; the rename dialog reappears with a Cancel button (automation ID `2`).
- **Out of scope:** Confirmed overwrite (separate case).

##### `Rename_directory_collision_keeps_both_directory_trees`

- **Does:** Attempts to rename `rename-collision-source` to `rename-collision-target` and cancels the failure.
- **Tests:** Directory-directory name collision is rejected rather than offered file overwrite.
- **Confirms:** Both trees’ `payload.txt` contents remain as seeded.
- **Assumptions:** Directory overwrite is not a supported prompt in this product path.
- **Out of scope:** Merging directory trees.

##### `Delete_skip_for_locked_file_keeps_it_and_continues_with_later_items`

- **Does:** Commits Configuration to immediate (Shift+Delete-style) deletion, holds `delete-locked.txt` open, selects it together with `delete-z-after-skip.txt`, deletes, confirms, and Skip on IDRETRY/Skip (`IDB_SKIP`).
- **Tests:** Product delete engine Retry/Skip after a sharing violation, continuing with later selected items.
- **Confirms:** The later file is gone; the locked file remains.
- **Assumptions:** Recycle Bin must be off for this engine: with the bin enabled the panel hands the selection to the shell (`CFilesWindow::DeleteThroughRecycleBin`), which would show “File In Use” instead of Skip. Alphabetical `delete-z-after-skip.txt` is processed after the locked file.
- **Out of scope:** Recycle Bin locked-file UI, Skip All, and Retry until success.

#### `LargeFlatDirectoryDeleteUiTests`

##### `Delete_large_flat_directory_removes_all_descendants`

- **Does:** Seeds 2,048 files in `delete-large-flat-directory` before launch, selects the directory, deletes, and confirms.
- **Tests:** Per-entry delete of a large flat directory without putting that cost on every ordinary file-operation case.
- **Confirms:** The directory eventually no longer exists.
- **Assumptions:** Confirmation answered; timeout is long enough for a realistic high-entry delete; workspace seed runs before panel enumeration.
- **Out of scope:** Progress correctness, cancellation mid-directory, Recycle Bin of 2,048 items, and memory/handle budgets during the delete.

#### `ApplicationVerifierStartupUiTests`

##### `Startup_exposes_the_native_main_window_under_the_selected_verifier_layer`

- **Does:** Relies on fixture launch (possibly under Application Verifier) and checks the main window class.
- **Tests:** Process startup under the selected verifier layer, isolated from the seven lifecycle scenarios.
- **Confirms:** Class name is `SalamanderMainWindowVer25`.
- **Assumptions:** Category `VerifierStartup`; verifier/PageHeap, when used, are applied by the nightly runner around the process, not by this assertion.
- **Out of scope:** Heaps/locks/exceptions diagnostics themselves, and any command after startup.

#### `ToolbarIconSizeUiTests`

##### `Customize_toolbar_cycles_all_icon_sizes_and_persists_the_choice_after_restart`

- **Does:** Opens Customize Top Toolbar, asserts the three combo labels, selects Small/Medium/Large via native `CBN_SELCHANGE` (not UIA `Select()`), reopening between sizes, waits 500 ms after Close for the 250 ms debounce, restarts, asserts Large is restored, then restores the incoming index.
- **Tests:** Live toolbar icon-size changes and persistence of the last choice.
- **Confirms:** Combo items are exactly Small 16, Medium 24, Large 32; each index sticks after Close/reopen; after restart the selection is Large (index 2).
- **Assumptions:** Resource ID `2748`; Close is automation ID `1`; English combo strings; native selection notification is required because UIA Select bypasses the legacy handler.
- **Out of scope:** Pixel-perfect glyphs, Fluent SVG assets (see `ToolbarIconSizeContractTests` and `verify-fluent-icon-coverage.ps1`), and menu/app-icon independence.

#### `LifecycleLeakUiTests`

##### `Repeated_clean_startup_and_shutdown_does_not_accumulate_process_resources`

- **Does:** Restarts FileManager `FILEMANAGER_UI_LEAK_CYCLES` times (default 20, nightly 100, clamped 5–200), samples handle/GDI/USER/private-byte counts, ignores cycle 1 (plug-in install), and checks spread plus early-vs-late mean shift.
- **Tests:** Cross-restart resource growth on clean start/stop.
- **Confirms:** After warm starts, spreads stay within 32 handles, 64 GDI, 12 USER, 64 MiB private bytes; mean shifts stay within 16 / 16 / 6 / 16 MiB.
- **Assumptions:** Category `Leak`; currently also `Quarantined` (`quarantined-ui-tests.json`) because a remote run measured a 39-handle warm-start spread against the 32-handle budget while Verifier and lock stress passed. Budgets compare equivalent warm starts, not the first cold plug-in install.
- **Out of scope:** Leaks that only appear while a command is in flight, GDI from user interaction, and calibrating whether a 39-handle spread is startup noise or a real leak (the quarantine expiry).

#### `ConfigurationRecoveryUiTests`

`ConfigurationPayloadTests.h` also runs in the existing native-safety target. It checks first-error retention across a worker-thread write and later successful calls, reset for a subsequent transaction, intended child counts, holes in numbered collections, and missing, empty, or incorrectly typed required fields against a private GUID registry key.

##### `Interrupted_configuration_writes_at_transaction_boundaries_restore_a_complete_profile`

- **Does:** Requires `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1`. Establishes a baseline checkbox, measures registry write count of a real commit, samples uniformly spaced payload writes plus five named phases (`checksum`, `complete`, `generation-flush`, `selector`, `store-flush`). For each point, arms a marker file only after startup, commits a toggle, expects exit code 121, restarts, and asserts the first checkbox is either complete baseline or complete candidate—not a mixture—then restores baseline.
- **Tests:** Crash recovery of transactional configuration generations.
- **Confirms:** Fault injection actually stops at the requested boundary; restart never shows a mixed profile.
- **Assumptions:** Category `FaultInjection`; `FILEMANAGER_UI_ISOLATED=1` so native hooks honor `FILEMANAGER_CONFIG_FAULT_*`; arm file prevents plug-in startup saves from consuming the boundary; named phases stay valid when plug-ins add thousands of snapshot values.
- **Out of scope:** Every individual registry value, concurrent writers, and migrating foreign profile versions during the crash.

#### `ConfigurationPayloadFailureUiTests`

Four cases inject one returned error into a nonmandatory value write or highlighting child-key creation, then allow subsequent writes to succeed. They require the unchanged selector, preserved saved checkbox, absent completion marker on the failed payload, one actionable error, and either recovery of the old value after process termination or successful retry of the in-memory candidate. These cases require the existing `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` capability and run in the normal complete pipeline inventory.

The fixture sets `FILEMANAGER_CONFIG_RETURN_ERROR` to `value:Title bar prefix text` or `key:Panel Items Hilighting`, using `FILEMANAGER_CONFIG_FAULT_ARM_FILE` to exclude startup saves. Native injection requires the exact sandbox registry root. `FILEMANAGER_UI_CONFIG_STATUS=1` writes `.config-save-status` inside the validated sandbox only after all coalesced saves finish, so observations cannot race an earlier startup save.

The crash reporter receives the sandbox's `appdata\Open Salamander` directory before the child process starts. An isolated crash must not leave a report in the user's Local AppData that blocks a later launch. The ADS retry regression waits for both the durable operation journal and closure of the progress window before teardown; released output handles alone cannot confirm successful worker finalization.

#### `ConfigurationRetirementUiTests`

Three cases start a second isolated process against the same sandbox profile. Barriers before retirement locks and after it reads the selector exercise intervening commits, reuse of the loaded slot with a new GUID, and exclusion of another process from the save mutex. They verify active/fallback preservation and restored settings on restart. Both processes and their crash reporters belong to fixture teardown.

`FILEMANAGER_CONFIG_RETIRE_BARRIER=before-lock` or `after-selector` requires the exact sandbox registry root, a validated test-data directory, and an exclusively claimed one-use `.config-retirement.arm` file. Sibling `.entered`, `.release`, `.completed`, and `.finish-release` markers coordinate bounded waits before cleanup and after unlocking; a timeout before cleanup preserves the fallback. The fixture stops the second process after inspecting its decision, before later startup saves can change the observation. These tests use the ordinary isolated NUnit lane, with no external service or administrator prerequisite.

#### `CrossVolumeMoveCharacterizationUiTests`

Requires `FILEMANAGER_UI_CROSS_VOLUME_ROOT` (runner uses writable `D:\filemanager-testdata` when available). The fixture deletes only its GUID child.

##### `Move_across_volumes_copies_the_complete_tree_before_removing_the_source`

- **Does:** Moves `move-tree` between different volume roots, accepts the metadata-preservation gate (IDYES), waits for destination release then source removal.
- **Tests:** Cross-volume move as copy-then-delete with an explicit metadata gate.
- **Confirms:** Nested payload is on the target with original content; source tree is gone.
- **Assumptions:** Category `CrossVolume`; volumes differ; timestamps often cannot be preserved, so the gate is expected.
- **Out of scope:** ADS (next case), FAT targets, and network drives.

##### `Move_across_ADS_capable_volumes_preserves_multiple_streams_before_removing_the_source`

- **Does:** Writes two named streams on `ads-cross-volume.txt`, moves across volumes, accepts the metadata gate, waits for destination then source deletion.
- **Tests:** ADS preservation when both volumes support ADS.
- **Confirms:** Source is gone; `first` and `second` streams match on the target.
- **Assumptions:** Categories `CrossVolume` and `AlternateDataStreams`; both roots are ADS-capable (typically NTFS on C: and D:).
- **Out of scope:** ADS-unsupported targets (quarantined fixture below).

#### `AlternateDataStreamsUnsupportedTargetUiTests`

##### `Cross_volume_move_to_an_ADS_unsupported_target_keeps_the_source_when_metadata_loss_is_declined`

- **Does:** Moves `ads-unsupported-target.txt` to a FAT/FAT32/exFAT-like volume, confirms ADS-loss copy of the default stream (caption `Confirm Alternate Data Streams Loss`, IDYES), then declines the following metadata-loss Question (IDNO).
- **Tests:** Default data published to an ADS-incapable target while declining source deletion so named streams are not lost.
- **Confirms:** Target has default content and no `must-not-silently-disappear` stream; source still exists with that stream.
- **Assumptions:** Categories `CrossVolume`, `AlternateDataStreams`, and currently `Quarantined` because a remote run selected inherited `ads-overwrite.txt` and showed overwrite instead of ADS-loss. Requires `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT`; source NTFS, target not ADS-capable; target must not already contain the name (overwrite would prove a panel-selection race).
- **Out of scope:** Accepting source deletion after ADS loss, and NTFS-to-NTFS moves.

#### `OperationRecoveryCharacterizationUiTests`

##### `Restart_reconciliation_commits_a_fully_written_transactional_target`

- **Does:** Seeds a journal immediately before each launch, then answers the recovery choice and summary. Version-2 fixtures record actual file IDs, creation/write times, lengths, SHA-256 digests, parent identity, attempt, and security policy. The eight cases cover verified overwrite, absent target, discard, Cancel, legacy resume/discard, changed destination, and changed stage.
- **Tests:** Real startup admission, evidence validation, conditional publication, and conservative handling of older journals. The normal and ADS overwrite tests separately check readiness emitted by the actual copy worker, including the SHA-256 digest or manual-only marker, so hand-seeded fixtures cannot conceal writer/reader format drift.
- **Confirms:** Verified Resume publishes the staged contents; verified Discard retains the old destination. Both persist terminal reconciliation. Cancel, legacy evidence and changed files preserve the files and leave the journal pending.
- **Assumptions:** Category `Recovery`; the fixture purges unrelated old journals before creating its own and allows the disabled owner window during the recovery prompt.
- **Out of scope:** Power loss and real removable-volume disconnection. Deterministic native cases cover mixed-item restart, claimed ownership, unavailable parents and persistence faults.

#### `ReparsePointTopologyUiTests`

Topology is created before launch. Junction/symlink targets live under the workspace root but outside the selected operation root. Category `ReparsePoints`.

##### `Copy_does_not_traverse_changed_or_cyclic_junction_targets_outside_the_operation_root`

- **Does:** Copies `reparse-operation-root`, which contains a junction retargeted from `outside-first-target` to `outside-changed-target` and a junction that points back at the operation root.
- **Tests:** Copy planner must not follow changed or cyclic directory junctions.
- **Confirms:** `inside.txt` is copied; outside sentinels are unchanged; `changed-junction` and `cycle-junction` are not materialized on the target.
- **Assumptions:** `mklink /J` works without extra privilege; retargeting by delete-and-recreate is visible to the product as a changed reparse target.
- **Out of scope:** Copying the junction itself as a reparse point, and mount points.

##### `Delete_junction_removes_only_the_link_and_never_its_target`

- **Does:** Deletes `delete-junction` whose target is `delete-target` with `sentinel.txt`.
- **Tests:** Junction delete removes the link only.
- **Confirms:** The junction path is gone; the target sentinel content is unchanged.
- **Assumptions:** Confirmation answered; cleanup similarly must not recurse into junction targets.
- **Out of scope:** Recycle Bin of junctions and symlink delete.

##### `Copy_does_not_traverse_a_directory_symbolic_link_outside_the_operation_root`

- **Does:** Copies the same operation root when a directory symlink `outside-symlink` could be created.
- **Tests:** Copy must not materialize or traverse a directory symbolic link to an outside target.
- **Confirms:** `inside.txt` copied; outside sentinel unchanged; `outside-symlink` absent on the target.
- **Assumptions:** `SeCreateSymbolicLinkPrivilege` (or equivalent); otherwise `Assert.Ignore` with an explicit capability message. Release-equivalent `runtests.ps1` skips the whole UI lane if the privilege is missing rather than reporting partial completion.
- **Out of scope:** File symlinks, junction coverage (always attempted), and creating links as the copy result.

#### `ReportedDefectCharacterizationUiTests`

These cases record current product behavior for reported defects. A failed assertion is valid evidence the defect remains; a stalled or crashed host is not acceptable. They must not install plug-ins or change application behavior to make an assertion pass.

##### `Zip_open_after_information_dialog_navigates_into_archive`

- **Does:** Seeds `zip-open-characterization.zip` with `zip-open-payload.txt`, selects the archive, issues Open, dismisses an optional information dialog (IDOK within 3 s), waits until the main-window title contains the zip name, then quick-searches the payload name.
- **Tests:** ZIP plug-in navigation after the information dialog.
- **Confirms:** The panel has navigated into the archive (title + listed payload), not merely a decorative title change.
- **Assumptions:** `FILEMANAGER_UI_ZIP_PLUGIN=1` confirms the Zip plug-in is deployed and enabled.
- **Out of scope:** Extract, nested archives, passworded zip, and fixing the reported defect if navigation still fails.

##### `Help_search_returns_the_configured_existing_help_result`

- **Does:** Issues Help Search, finds HTML Help (`HH Parent`), types `FILEMANAGER_UI_HELP_SEARCH_TERM`, presses Enter, waits for a descendant whose name contains `FILEMANAGER_UI_HELP_EXPECTED_RESULT`, then closes only that captured HH window.
- **Tests:** Language-specific compiled help search against the deployed `salamand.chm`.
- **Confirms:** The configured existing result appears in the Help UI.
- **Assumptions:** Both Help environment variables are set; `help/<language>/salamand.chm` exists beside the executable.
- **Out of scope:** Full-text ranking, other CHMs, and installing help during the test.

#### `DeterministicNetworkFixtureTests.ProductFtpControlConnectionTests`

Loopback HTTP/FTP/FTPS/stall cases in the parent fixture never launch FileManager. The nested fixture does.

##### `Quick_connect_consumes_a_fragmented_greeting_before_the_fixture_disconnects`

- **Does:** Starts a loopback FTP server that sends a split multiline `220` greeting then waits for one line and disconnects. Opens the product Connect to FTP Server dialog and connects to `127.0.0.1:port`.
- **Tests:** Native FTP reply reader across socket-read boundaries before login.
- **Confirms:** The first client command starts with `USER `; the scripted server completes.
- **Assumptions:** Isolated profile; Connect command ID is available; 10 s bound for login command and server completion.
- **Out of scope:** Password/login success, TLS, data channel, bookmarks, and the live MojeRzeczy server.

#### `MojeRzeczyFtpsUiTests`

Explicit, `LiveFtp`, excluded from `scripts/runtests.ps1` and CI. Run via `scripts/run-ftp-test.ps1`.

##### `Quick_connect_downloads_skan_txt_with_explicit_ftps_passive_binary_transfer_and_an_invalid_certificate`

- **Does:** If `MOJERZEC_USERNAME`/`MOJERZEC_PASSWORD` are missing, passes immediately with a message that FTP UI tests were not performed. Otherwise quick-connects to `ftp.mojerzeczy.com` with explicit FTPS (port 21, passive, binary, AUTH TLS), accepts a hostname-invalid certificate for this session only (does not persist the exception), dismisses Welcome Message, copies `/skan.txt` into a GUID folder under the test-data root (queue checkbox off), waits until the file can be opened with `FileShare.None` at the reference size from `C:\Projects\FtpMojerzeczy\skan.txt`. Debug error dialogs are logged under `TestResults\ftp-debug-error-dialogs` (or `FILEMANAGER_UI_FTP_DEBUG_ERROR_LOG_DIRECTORY`), dismissed, and fail the test.
- **Tests:** Live explicit FTPS download against that one server, including invalid-certificate handling in the disposable profile.
- **Confirms:** Downloaded `skan.txt` byte length matches the local reference file.
- **Assumptions:** External network; reference file exists and is non-empty; left panel is the post-connect remote listing; certificate policy must not be reused for other servers.
- **Out of scope:** Upload, resume, other hosts, content equality beyond size, and CI/release-gate execution.

## Continuous-integration coverage

Pull-request CI runs the four changed-line ratchets, operation-completion and durable-copy contracts, zlib and bzip2 probes, cmark-gfm hardening, and the SQLite recovery probe after its Debug x64 build. The root runner additionally executes the built 7-Zip wrapper/oracle compatibility corpus whenever a `7z.exe`-compatible oracle is available; the release gate requires it.

The release workflow gates installer publication on the complete root runner using the dedicated `filemanager-ui` self-hosted environment. It supplies the built executable and SQLite DLL, enables configuration fault injection and Recycle Bin coverage, detects fixed writable `D:\` for second-volume coverage, and uses `-FailOnSkipped`; known second-volume and missing-privilege environment skips are explicitly reported and allowed. When `SeCreateSymbolicLinkPrivilege` is unavailable, the complete UI suite is skipped as not completed rather than run partially. The release therefore fails if any runner check fails or any other NUnit case reports `Assert.Ignore`/`NotExecuted`. It retrieves Inno Setup only through [`tools/release-inputs.json`](tools/release-inputs.json), checks the locked SHA-256 and Authenticode publisher before installation, then passes an immutable uploaded installer artifact to the `production`-protected publish job. Private PDB artifacts are retained for 180 days and are never attached to the public release.

The dedicated runner must provide Visual Studio 2026, .NET 10, PowerShell 7.4+, Application Verifier, and an unlocked desktop. Symlink creation privileges are required for completed UI coverage; if they are absent, the runner reports the UI lane as not completed and skips it without failing the release gate. A fixed writable `D:\` is optional: without it, the runner reports all second-volume-dependent tests as successful capability skips.

The nightly native-verifier lane runs the complete `UI` category with Application Verifier's Heaps, Handles, Locks, and Exceptions layers plus full PageHeap. `gflags.exe` is required and both Verifier and PageHeap settings are cleared in `finally` even if a test fails. The existing runner profile remains dedicated; provisioning and destroying a fresh Windows profile or VM is still an external runner-management requirement.
