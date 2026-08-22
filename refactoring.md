# Refactoring Recommendations: Largest Source Files

## Findings

Excluding vendored/third-party code (`common/dep/*`, `plugins/7zip`, `plugins/winscp`,
`plugins/mmviewer`, `sqlite3.h`, `tools/.venv`) and generated code, the largest
first-party files are all in `src/`:

| File | Lines | Notes |
|---|---|---|
| `src/async_copy.cpp` | 7,251 | Copy/move/delete/create-dir engine + ADS, security, metadata, recycle bin, transactions |
| `src/app_entry.cpp` | 4,765 | Application startup/shutdown |
| `src/path_utils.cpp` | 4,695 | Path manipulation utilities |
| `src/mainwnd_commands.cpp` | 4,672 | Main window command dispatch |
| `src/mainwnd_config.cpp` | 4,565 | Configuration dialog logic |
| `src/plugins_loading.cpp` | 4,359 | Plugin loading/lifecycle |
| `src/fileswindow_execute.cpp` | 4,348 | File window execution paths |

`async_copy.cpp` is the worst offender and the highest-value target: it mixes at
least seven distinct concerns in one translation unit.

## Recommended refactor #1 (highest priority): split `async_copy.cpp` -- DONE

The file already has clean internal seams — functions cluster by concern with
minimal cross-dependencies. Proposed split (new files next to `async_copy.cpp`,
added to `vcxproj/salamand.vcxproj`):

1. **`file_attributes.cpp`** (~500 lines)
   - `CompressFile`, `UncompressFile`, `MyEncryptFile`, `MyDecryptFile`,
     `SetCompressAndEncryptedAttrs`
   - Already forward-declared in `operations_core.cpp`; move those declarations
     into a small header (e.g. `file_attributes.h`) instead of ad-hoc externs.

2. **`ads_operations.cpp`** (~900 lines)
   - `CheckFileOrDirADS` (declared in `worker.h`), `DeleteAllADS`, `DoCopyADS`,
     `CutADSNameSuffix`, `MyStrCpyNW`, stream helpers
   - `CheckFileOrDirADS` is also called from `fileswindow_operations.cpp`.

3. **`security_helpers.cpp`** (~700 lines)
   - `GainWriteOwnerAccess`, `IsUserAdmin`, `CSrcSecurity`,
     `AreEqualSids`/`AreEqualExplicitAces`/`IsSecurityDescriptorPreserved`,
     `SetDaclWithInheritance`, `DoCopySecurity`
   - `IsUserAdmin` is declared in `consts.h`; `GainWriteOwnerAccess` is an
     extern in `operations_core.cpp` — promote both to a header.

4. **`metadata_preservation.cpp`** (~450 lines)
   - `GetMetadataTargetFileSystem`, `GetMetadataPreservationContract`,
     `RecordMetadataLoss`, `RecordPlannedMetadataLosses`,
     `ConfirmMetadataLossesBeforeSourceDeletion`
   - Already declared in `worker.h`; only the definitions move.

5. **`copy_commit.cpp`** (~600 lines)
   - Transactional target handling (`CreateTransactionalTargetFileName`,
     `OpenTransactionalTargetFile`, `CommitTransactionalTargetFile`),
     durability verification (`VerifyDurableCopyCommit`),
     SHA-256 verification (`CalculateFileSha256`,
     `VerifyFullFileContentSha256`), `SalCreateFileEx`

6. **`copy_loop.cpp`** (~1,100 lines)
   - `CCopy_Context` methods, `DoCopyFileLoopOrig`, `DoCopyFileLoopAsync`,
     `DisableLocalBuffering`, `SyncOrAsyncDeviceIoControl`,
     `CAsyncCopyParams` implementation

7. **`recycle_bin_delete.cpp`** (~200 lines)
   - `CRecycleBinDeleteSink`, `RunRecycleBinDeleteOnSta`, `DeleteThroughRecycleBin`,
     `GetFileOperationError`

What remains in `async_copy.cpp` (~2,500–3,000 lines): the top-level operation
entry points `DoCopyFile`, `DoMoveFile`, `DoDeleteFile`, `DoCreateDir`,
`DoDeleteDir`, `SalCreateDirectoryEx`, `DoCopyDirTime`, progress helpers
(`SetProgress*`, `CaclProg`). These share heavy state via `CProgressDlgData`
and are the natural core of the file.

### Mechanics / safety

- Pure mechanical move of function bodies; no signature or behavior changes.
- Shared declarations currently duplicated as externs in `operations_core.cpp`
  should be consolidated into headers to prevent drift.
- Verify each new .cpp compiles standalone with precompiled headers
  (`precomp.h`) — keep the same include set initially.
- No project-file changes beyond adding `<ClCompile>` entries.
- Validation: build the solution; run any existing UI test suites covering
  copy/move/delete (`tests/FileManager.UiTests`).

### Implementation summary (completed)

Refactor #1 was implemented in two verified batches. `async_copy.cpp` went from
**7,251 to 3,278 lines**; all seven proposed files were created and registered in
`vcxproj/salamand.vcxproj`:

| New file | Actual lines | Contents |
|---|---|---|
| `recycle_bin_delete.cpp` | 180 | `CRecycleBinDeleteSink`, STA executor, `DeleteThroughRecycleBin`, `GetFileOperationError` |
| `security_helpers.cpp/.h` | 567 + 34 | privilege setup, `IsUserAdmin`, `CSrcSecurity`, descriptor comparison, `DoCopySecurity` |
| `metadata_preservation.cpp` | 203 | contract + loss recording/confirmation (declarations already in `worker.h`) |
| `file_attributes.cpp/.h` | 255 + 15 | `CompressFile`, `UncompressFile`, `MyEncryptFile`, `MyDecryptFile` |
| `ads_operations.cpp` | 727 | ADS detection/deletion/copying (`CheckFileOrDirADS`, `DeleteAllADS`, `DoCopyADS`) |
| `copy_commit.cpp` | 443 | transactional target, durability check, SHA-256, `SalCreateFileEx` |
| `copy_loop.cpp` | 1,672 | `CAsyncCopyParams`, tail verification, `CCopy_Context`, both copy loops |

Additionally, a new **`src/async_copy_internals.h`** consolidates every
declaration shared between `async_copy.cpp` and the extracted files, replacing
the ad-hoc externs previously duplicated in `operations_core.cpp`.

Deviations from the original plan (all mechanical, behavior-preserving):

- `SetCompressAndEncryptedAttrs` moved to `copy_loop.cpp` (not
  `file_attributes.cpp`) because it is inseparable from
  `SyncOrAsyncDeviceIoControl`; the four pure attribute helpers stayed in
  `file_attributes.cpp`.
- Helpers that were file-local but whose callers remain in `async_copy.cpp`
  (`GetTemporaryNameSeed`, transactional-target trio, `VerifyDurableCopyCommit`,
  `RemoveCommittedStreamsMissingFromSource`, `VerifyFullFileContentSha256`,
  `DeleteAllADS`, `CheckTailOfOutFile`) were promoted to external linkage with
  declarations in `async_copy_internals.h`.
- `DoCopyADS`'s default argument (`int optimalBufferSize = 0`) moved from its
  definition to the shared declaration.
- `CheckTailOfOutFileShowErr` keeps its default argument for now (its rename and
  explicit-call-site cleanup belong to refactor #2).
- `InitWorker`/`ReleaseWorker` remained in `async_copy.cpp` (NtDLL binding used
  by remaining entry points; not assigned by the plan).

Validation performed: Debug x64 build via VS 2026 MSBuild links cleanly, and
`tests/FileManager.UiTests` → `FileOperationUiTests` passed **37 / 37 runnable**
(1 test skipped as environment-conditional) against the refactored build.

## Recommended refactor #2: readability pass on remaining hot spots -- DONE (scoped)

Even without splitting files, several concrete readability wins inside
`async_copy.cpp`:

- **`DoCopyFile`** (~1,300 lines) and **`DoMoveFile`** (~670 lines) are single
  mega-functions. Extract cohesive phases into named helpers:
  - open source/target & overwrite-resolution phase
  - transactional-target setup/commit phase (pairs naturally with `copy_commit.cpp`)
  - post-copy attribute/ADS/security application phase
  - error/skip bookkeeping blocks
- **Naming**: names like `SetTFSandPSforSkippedFile`, `CaclProg`,
  `CheckTailOfOutFileShowErr` are cryptic; rename to intent-revealing names
  (e.g. `RecordSkippedFileProgressState`, `CalculateProgressPercent`) with a
  mechanical rename across call sites.
- **Default argument in declaration body** (`DWORD err = GetLastError()` in
  `CheckTailOfOutFileShowErr`) evaluates at every call site implicitly — make it
  explicit at call sites for clarity.

### Implementation summary (completed, scoped)

Implemented after refactor #1, on top of the split layout:

**Renames** (mechanical across all call sites):

| Old | New | Sites |
|---|---|---|
| `CaclProg` | `CalculateProgressPercent` | 48 (worker.h + 4 .cpp) |
| `SetTFSandPSforSkippedFile` | `RecordSkippedFileProgressState` | 5 (async_copy.cpp) |
| `CheckTailOfOutFileShowErr` | `LogTailVerificationError` | 11 (copy_loop.cpp) |

The implicit `DWORD err = GetLastError()` default argument was removed;
`LogTailVerificationError` now takes the error explicitly and its six bare call
sites pass `GetLastError()` themselves.

**Phase extractions** (each helper only has outward control flow, so no
behavior-preserving goto rewrite was needed):

- `SkipCopyIfTargetNotOlder` (async_copy.cpp): the "Overwrite Older" probe from
  `DoCopyFile`'s setup (~60 lines), including the skip/progress bookkeeping.
- `VerifyAndCommitCopyTarget` + `enum ECopyCommitPhase` (async_copy.cpp):
  durable-copy verification, transactional ReplaceFile commit, journal marking,
  and stale-stream cleanup (~130 lines). The former `goto COPY_ERROR_2` /
  `SKIP_COPY` / `COPY_AGAIN` exits map onto `cpcrCancel` / `cpcrSkip` /
  `cpcrRestart`, translated by a small switch at the single call site.
- `FinishSameVolumeMove` (async_copy.cpp): `DoMoveFile`'s post-rename phase
  (attribute verification, stashed-security application, directory-time
  restore, progress; ~110 lines). This removed the `OPERATION_DONE` and
  `MOVE_ERROR_2` labels and rewired three entry paths onto one named function.

**Deliberately not extracted:** the open-source/target & overwrite-resolution
phase and the remaining error/skip bookkeeping of `DoCopyFile`. Their backward
gotos (`OPEN_TGT_FILE`, `COPY`, plus retry loops through `NORMAL_ERROR`) cross
every candidate boundary; converting them would mean rewriting the retry state
machine rather than moving code, contradicting the behavior-preserving
constraint. Same reasoning applies to the overwrite prompts shared with
`NORMAL_ERROR` in `DoMoveFile`.

Validation: Debug x64 build links cleanly; `tests/FileManager.UiTests` →
`FileOperationUiTests` passed 37 / 37 runnable again after the changes.

## Lower-priority candidates (after async_copy)

- `path_utils.cpp` (4,695): group functions by theme (normalization,
  qualification, extension handling) into 2–3 files, e.g. `path_normalize.cpp`.

  ### Implementation summary (completed)

  On inspection the file turned out to be a grab-bag: the genuine path
  utilities (`SalPath*`, `SalGetTempFileName`, `SalRemovePointsFromPath`,
  `SalGetFullName`) already sat together at the top, while unrelated tenants
  occupied the rest. Instead of the proposed `path_normalize.cpp`, three
  thematic extractions were made (all mechanical moves; declarations live in
  existing headers, so no new headers were needed):

  | New file | Lines | Contents |
  |---|---|---|
  | `path_history.cpp` | 1,226 | `CPathHistoryItem`, `CPathHistory`, ampersand helpers, `CScrollPositionMemory`, `CFileHistory` |
  | `user_menu_icons.cpp` | 627 | `UserMenuIconBkgndReader` global + reader class, `CUserMenuIconData`, `CUserMenuItem(s)` |
  | `file_timestamps.cpp` | 494 | `CFileTimeStamps(Item)` incl. pack-and-clear flow; `CDynamicStringImp` method definitions moved along |

  `path_utils.cpp` went from **4,695 to 2,422 lines** and now holds only path
  utilities, temp-dir cleanup, aux-thread/refresh guards, mouse-wheel support,
  and directory editline helpers (the remaining tenants `CToolTipWindow` and
  the dialog-editline block are small enough to leave for a future pass).

  Validation: Debug x64 build links cleanly; `FileOperationUiTests` passed
  37 / 37 runnable.

- `app_entry.cpp`: separate startup sequence from shutdown/cleanup code.

  ### Implementation summary (completed)

  Two extractions were made (mechanical moves; declarations already existed in
  headers except where noted):

  | New file | Lines | Contents |
  |---|---|---|
  | `app_graphics.cpp` | 1,602 | highlight-color heuristics, `UpdateDefaultColors`, constant graphics/image lists, shortcut overlay, DPI scaling family, `InitializeGraphics`/`ReleaseGraphics`, `ColorsChanged` |
  | `app_shutdown.cpp/.h` | ~155 + 12 | whole-application teardown extracted from the tail of `WinMainBody` as `ShutdownSalamander()` (runs once after the message loop), plus `ReleasePreloadedStrings` moved in as pure cleanup |

  `app_entry.cpp` went from **4,765 to 3,110 lines** and now contains only the
  entry point, command-line parsing, startup sequence, path helpers, and misc
  utilities. Follow-up fixes required by the moves: the `void GetSystemDPI(HDC)`
  overload got its first header declaration (`consts.h`, next to the existing
  `int GetSystemDPI()`), and `ReleasePreloadedStrings` moved to
  `app_shutdown.cpp` with a declaration in `app_shutdown.h`.

  Note on scope: the shutdown code *inside* `WinMainBody`'s early-exit ladder
  (`EXIT_1..EXIT_9`) was deliberately left in place — those cascading failure
  paths are interleaved with startup state and cannot move without rewriting
  the ladder.

- `mainwnd_commands.cpp` / `mainwnd_config.cpp`: split per command category /
  config page if they continue growing; lower urgency since they follow an
  existing per-area naming convention.

  ### Implementation summary (completed, scoped)

  `mainwnd_config.cpp` (4,565 -> 3,494 lines) was split along its two clean
  seams; both clusters are mechanical moves whose exported entry points were
  already declared in salamand.h:

  | New file | Lines | Contents |
  |---|---|---|
  | `config_store.cpp/.h` | 512 + 13 | transactional configuration store: generation keys with checksum + schema validation/migration, begin/commit transaction helpers, retired-generation cleanup, UI-test store override |
  | `config_import.cpp` | 626 | import/upgrade from previous versions: AutoImportConfig handling, language detection, old-configuration discovery, registry rename helper, `DeleteOldConfigurations` |

  The store's internals that the save/load code also calls
  (`BeginConfigurationTransaction`, `CommitConfigurationTransaction`,
  `OpenCommittedConfigurationGeneration`,
  `RetirePreviousConfigurationGenerationAfterSuccessfulStartup`,
  `CONFIGURATION_ACTIVE_GENERATION_REG`) were promoted to external linkage and
  declared in the new `config_store.h`, so the three config translation units
  cannot drift apart.

  For `mainwnd_commands.cpp` (4,672 lines), only one extraction made sense: the
  context-help mode cluster (`CanEnterHelpMode`, `OnContextHelp`,
  `ProcessHelpMsg`, `ExitHelpMode` plus their message-range macros) moved into
  the existing `mainwnd_help.cpp` (now 628 lines), which matches the file's
  theme. **`HandleWmCommand` (~2,500 lines, a single 204-case switch over CM_*
  IDs) was deliberately left intact** — splitting cases into per-category
  handlers means rewriting shared-local state plumbing, not moving code.

  Validation: Debug x64 build links cleanly; `FileOperationUiTests` passed
  37 / 37 runnable.

## Explicitly not recommended

- Do not touch vendored dependencies (`common/dep/wil`, `fmt`, zlib, bzip2,
  7zip, WinSCP sources) — they track upstream and should stay pristine.
- Avoid introducing classes/abstractions during the first pass; do the
  mechanical file split first so behavior-preserving moves can be reviewed
  independently from design changes.
