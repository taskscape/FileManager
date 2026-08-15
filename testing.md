# Automated testing

Run all commands from the repository root. The test suite has four layers:

- PowerShell source-contract and native compatibility probes.
- NUnit source-contract and local TLS integration tests.
- FlaUI/UIA3 tests that drive the native Windows application.
- Optional fault-injection, filesystem-topology, and cross-volume characterization lanes.
- Loopback FTP, FTPS, and HTTP protocol fixtures for fragmented replies, disconnects, TLS, and stalls.

Visual Studio 2026 and its developer-command environment are the authoritative native build environment. Most scripts support Windows PowerShell 5.1. The SQLite recovery probe is the exception: use 64-bit PowerShell 7.4 or newer (`pwsh.exe`) for an x64 DLL.

## Quick start

### Complete local runner

Run every automated test whose prerequisites are available on the current machine:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File .\runtests.ps1
```

`runtests.ps1` collects all PowerShell/native probes, both x64 and x86 compatibility variants, the built 7-Zip wrapper/oracle corpus gate, the complete NUnit project, and the optional Application Verifier lane. It runs every collected check even after a failure and prints passed, failed, and explicitly skipped checks. The 7-Zip gate requires a `7z.exe`-compatible independent oracle; strict release runs fail if it is unavailable. It never manufactures `FILEMANAGER_UI_ISOLATED`: the complete UI project runs only when the caller has supplied a disposable profile and its filesystem topology; otherwise that project is explicitly skipped while independent checks continue.

Each run removes its own GUID-named `TestResults\runtests-build-*` directory, including after a failure. Pass `-KeepBuildArtifacts` only when the isolated native build outputs are needed for diagnosis.

`-PlatformToolset v143|v145` selects the toolset used for both the built executable and native safety target (default `v145`). CI supplies `-NUnitTrxPath` only for toolset-parity jobs; that explicit path retains the complete executable result inventory for comparison instead of deleting the caller-owned TRX.

The runner uses the CI pull-request base for changed-line ratchets or accepts `-BaseCommit` explicitly; it does not guess from a potentially stale local tracking branch. It discovers an existing Debug or Release x64 SQLite DLL when possible. Supply prerequisites explicitly or require a fully provisioned run with:

```powershell
.\runtests.ps1 `
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

The NUnit project targets .NET 8:

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

UI tests intentionally skip unless they run in an interactive disposable Windows profile with a built executable:

```powershell
$env:FILEMANAGER_UI_ISOLATED = '1'
$env:FILEMANAGER_UI_EXE = 'C:\path\to\salamand.exe'

dotnet test .\tests\FileManager.UiTests\FileManager.UiTests.csproj `
  --filter 'TestCategory=UI'
```

Never set `FILEMANAGER_UI_ISOLATED=1` in a normal developer profile. These tests persist configuration, launch an editor, perform native file operations, and expect the Windows profile to be disposable.

Optional UI settings:

| Variable | Enables |
| --- | --- |
| `FILEMANAGER_UI_ARGUMENTS` | Extra application arguments, such as a test-only `-c` configuration file. |
| `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` | The runtime command ID for FTP **Organize Bookmarks** persistence cases. |
| `FILEMANAGER_UI_FTP_CONNECT_COMMAND` | Optional runtime Connect to FTP Server command ID for protocol UI fixture runs outside `runtests.ps1`; the runner discovers it from the freshly built menu. |
| `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` | Exhaustive configuration-write crash recovery. |
| `FILEMANAGER_UI_CROSS_VOLUME_ROOT` | Cross-volume move tests using only a GUID child below the supplied dedicated root. |
| `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` | Metadata-loss behavior on a different FAT/FAT32/exFAT-like volume. |
| `FILEMANAGER_UI_RECYCLE_BIN=1` | Recycle-bin deletion in an isolated profile using the default recycle-bin setting. |
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
| `compare-vstest-trx.ps1` | Rejects mismatched discovered-test inventories or outcomes between v143 and v145 native-regression and complete executable UI results. |
| `test-unsafe-api-baseline.ps1` | Compares every repository unsafe API fingerprint with the reviewed generated baseline and rejects new or duplicated unsafe calls. |
| `new-toolset-pe-manifest.ps1` / `compare-toolset-pe-manifests.ps1` | Record v143/v145 Release PE inventories and compare path, architecture, and version identity while retaining output hashes for provenance. |
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

All fixtures in this section require `FILEMANAGER_UI_ISOLATED=1`, `FILEMANAGER_UI_EXE`, and an interactive desktop unless a stricter requirement is stated.

#### `BasicUiTests` — risk-based lifecycle matrix

`Basic_ui_scenario` generates the seven cases below. They are categorized as `LockStress` so the nightly verifier lane can repeat the compact risk matrix independently of the release gate.

| Scenario family | Cases | Description |
| --- | ---: | --- |
| Main window | 1 | Launches the application and verifies the native main window is usable. |
| Accessibility tree | 1 | Verifies UIA3 can discover the expected accessible descendants. |
| Configuration cancel | 1 | Opens Configuration, cancels, and confirms the main window remains usable. |
| Configuration commit | 1 | Opens Configuration, commits, and confirms normal dialog lifecycle. |
| Configuration persistence | 1 | Changes a visible setting and verifies it after reopening Configuration. |
| Restart after commit | 1 | Commits configuration and verifies the application restarts cleanly. |
| FTP bookmark persistence | 1 | Creates and renames an FTP bookmark, restarts, and verifies the edited bookmark; requires `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND`. |

#### `FileAccessUiTests`

- `Find_files_searches_subdirectories_from_the_active_panel` — searches the active panel for a unique nested file and verifies one result.
- `View_file_opens_the_selected_file_in_the_internal_viewer` — opens the selected text file in Salamander's internal viewer.
- `Edit_file_opens_the_selected_file_in_the_configured_editor` — launches the configured external editor with the selected file.

#### `FileOperationUiTests`

- `Create_directory_creates_requested_nested_directory` — creates a nested directory through the native dialog.
- `Copy_file_copies_content_to_other_panel` — copies one file, preserves content, and retains the source.
- `Copy_preserves_last_write_time_metadata` — verifies copied last-write time metadata.
- `Copy_preserves_multiple_empty_large_and_edge_named_alternate_data_streams` — copies multiple representative ADS values.
- `Copy_overwrite_replaces_target_streams_and_removes_stale_streams` — overwrites ADS content and removes target-only streams.
- `Copy_retries_a_temporarily_denied_alternate_data_stream_without_losing_it` — releases a locked stream and verifies Retry completes the copy.
- `Copy_overwrite_replaces_the_existing_target_only_after_the_user_confirms` — checks single-file overwrite confirmation.
- `Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree` — applies Overwrite All to every conflicting descendant.
- `Copy_skip_keeps_the_existing_target_and_the_source` — checks single-file Skip behavior.
- `Copy_skip_all_keeps_the_existing_conflicting_tree` — applies Skip All without changing conflicting targets.
- `Copy_file_persists_a_completed_recovery_journal_with_item_intent` — verifies the completed durable journal, immutable plan, states, and correlation ID.
- `Copy_directory_copies_all_descendants_to_other_panel` — copies a complete nested directory tree.
- `Unicode_normalization_surrogate_and_long_path_operations_preserve_distinct_entries` — copies composed/decomposed Unicode and surrogate-pair names plus a >260-character descendant path, then renames and deletes distinct normalization forms.
- `Rename_file_renames_without_changing_content` — renames a file and preserves content.
- `Rename_directory_preserves_all_descendants` — renames a directory and retains its tree.
- `Rename_case_only_change_preserves_the_file_and_updates_its_displayed_name` — verifies a case-only directory-entry rename.
- `Rename_overwrite_replaces_the_collision_without_losing_source_metadata` — confirms file overwrite and preserves source timestamp metadata.
- `Move_file_moves_content_to_other_panel` — performs a same-volume file move and removes the source.
- `Move_directory_moves_all_descendants_to_other_panel` — performs a same-volume directory-tree move.
- `Move_overwrite_replaces_the_existing_target_and_removes_the_source` — confirms a move overwrite and source removal.
- `Move_skip_keeps_the_existing_target_and_the_unmoved_source` — checks that Skip retains both versions.
- `Move_overwrite_all_replaces_every_conflict_before_removing_the_source_tree` — verifies all conflicts commit before source-tree removal.
- `Move_skip_all_retains_conflicting_sources_but_moves_nonconflicting_siblings` — retains skipped sources while continuing nonconflicting moves.
- `Delete_file_removes_the_selected_file` — deletes one file.
- `Delete_directory_removes_all_descendants` — deletes a complete directory tree.
- `Delete_mixed_selection_removes_the_selected_file_and_directory_tree` — deletes a selected file and directory together.
- `Delete_to_recycle_bin_removes_the_source_and_creates_a_recoverable_shell_item` — verifies recycle-bin deletion; requires `FILEMANAGER_UI_RECYCLE_BIN=1`.
- `Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation` — cancels at a conflict prompt and verifies both files plus the journal cancellation record.
- `Cancelling_operation_dialog_leaves_source_and_target_unchanged` — four cases cover cancelled create, copy, move, and rename dialogs.
- `Create_directory_failure_keeps_existing_file_intact` — verifies a directory/file collision does not change the file.
- `Copy_or_move_failure_does_not_modify_source` — two cases verify invalid copy and move destinations leave sources intact.
- `Rename_overwrite_decline_keeps_the_original_file_and_existing_target` — chooses No at the rename overwrite prompt and retains both files.
- `Rename_directory_collision_keeps_both_directory_trees` — verifies directory rename collisions cannot overwrite either tree.
- `Delete_skip_for_locked_file_keeps_it_and_continues_with_later_items` — skips a sharing violation and deletes later selected items.

#### Optional UI fixtures

- `ConfigurationRecoveryUiTests.Every_interrupted_configuration_write_restores_a_complete_profile` — measures every registry write boundary, terminates the process after each mutation, and verifies restart exposes either the complete baseline or complete candidate profile; requires `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` and category `FaultInjection`.
- `CrossVolumeMoveCharacterizationUiTests.Move_across_volumes_copies_the_complete_tree_before_removing_the_source` — verifies a cross-volume tree is fully copied before source removal; requires `FILEMANAGER_UI_CROSS_VOLUME_ROOT`.
- `CrossVolumeMoveCharacterizationUiTests.Move_across_ADS_capable_volumes_preserves_multiple_streams_before_removing_the_source` — verifies ADS preservation across two ADS-capable volumes before source removal.
- `AlternateDataStreamsUnsupportedTargetUiTests.Cross_volume_move_to_an_ADS_unsupported_target_keeps_the_source_when_metadata_loss_is_declined` — verifies default data reaches the target while declining ADS loss retains the source; requires `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT`.
- `OperationRecoveryCharacterizationUiTests.Restart_reconciliation_commits_a_fully_written_transactional_target` — seeds a ready temporary target and incomplete journal, then verifies startup reconciliation commits and records it.
- `ReparsePointTopologyUiTests.Copy_does_not_traverse_changed_or_cyclic_junction_targets_outside_the_operation_root` — verifies copy ignores changed and cyclic junction targets.
- `ReparsePointTopologyUiTests.Delete_junction_removes_only_the_link_and_never_its_target` — verifies deleting a junction preserves its target.
- `ReparsePointTopologyUiTests.Copy_does_not_traverse_a_directory_symbolic_link_outside_the_operation_root` — verifies copy ignores an external directory symlink; skips when the host lacks symlink privileges.
- `ToolbarIconSizeUiTests.Customize_toolbar_cycles_all_icon_sizes_and_persists_the_choice_after_restart` — exercises all three icon sizes, verifies live persistence, restarts, and restores the incoming setting.
- `LifecycleLeakUiTests.Repeated_clean_startup_and_shutdown_does_not_accumulate_process_resources` — starts fresh native processes repeatedly and rejects excessive Handle/GDI/USER/private-byte spread.

## Continuous-integration coverage

Pull-request CI runs the four changed-line ratchets, operation-completion and durable-copy contracts, zlib and bzip2 probes, cmark-gfm hardening, and the SQLite recovery probe after its Debug x64 build. The root runner additionally executes the built 7-Zip wrapper/oracle compatibility corpus whenever a `7z.exe`-compatible oracle is available; the release gate requires it.

The release workflow gates installer publication on the complete root runner using the dedicated `filemanager-ui` self-hosted environment. It supplies the built executable and SQLite DLL, enables configuration fault injection and Recycle Bin coverage, obtains its two dedicated filesystem roots and FTP command ID from repository variables, and uses `-FailOnSkipped`. The release therefore fails if any runner check fails, any external prerequisite is missing, or any NUnit case reports `Assert.Ignore`/`NotExecuted`. It retrieves Inno Setup only through [`tools/release-inputs.json`](tools/release-inputs.json), checks the locked SHA-256 and Authenticode publisher before installation, then passes an immutable uploaded installer artifact to the `production`-protected publish job. Private PDB artifacts are retained for 180 days and are never attached to the public release.

Required release repository variables are `FILEMANAGER_UI_CROSS_VOLUME_ROOT`, `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT`, and `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND`. The dedicated runner must provide Visual Studio 2026, .NET 8, PowerShell 7.4+, Application Verifier, an unlocked desktop, symlink creation privileges, an ADS-capable second volume, and an ADS-unsupported volume.

The nightly native-verifier lane runs the complete `UI` category with Application Verifier's Heaps, Handles, Locks, and Exceptions layers plus full PageHeap. `gflags.exe` is required and both Verifier and PageHeap settings are cleared in `finally` even if a test fails. The existing runner profile remains dedicated; provisioning and destroying a fresh Windows profile or VM is still an external runner-management requirement.
