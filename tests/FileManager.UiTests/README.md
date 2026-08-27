# FileManager UI tests

This project contains parameterized FlaUI/UIA3 lifecycle cases together with focused file-operation, recovery, plug-in, toolbar, TLS, and native-safety characterization tests for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete/find/view/edit commands.

The tests run as the current interactive Windows user only when a guarded filesystem and registry sandbox are selected. The harness creates and deletes its own `filemanager-testdata` directory and the suffixed registry key after each run.

The repository runner configures the guarded test-data root, configuration root, and executable automatically. Set variables manually only when invoking `dotnet test` directly or enabling an optional lane:

- `FILEMANAGER_UI_TESTDATA_ROOT` — absolute path ending in `filemanager-testdata`; all test-created files live below it.
- `FILEMANAGER_UI_CONFIG_ROOT=Software\Open Salamander\6.0-filemanager-testdata` — selects the registry tree created and removed by the harness.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` in a deployed runtime. The runtime must include `salmon.exe` and the plug-in payloads used by the selected tests; an executable by itself is not a complete UI test artifact.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- FTP menu IDs are published with the launching process ID below the owned test-data root, so each fixture waits for the exact FileManager instance it controls before issuing a plug-in command.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — selected automatically by `scripts/runtests.ps1` when fixed writable `D:\` is available, or supplied manually for the focused runner. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` — selected automatically by `scripts/runtests.ps1` when fixed writable `D:\` uses FAT/FAT32/exFAT, or supplied manually for the focused runner. On NTFS, the ADS-unsupported scenario is reported as an allowed capability skip.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.
- `FILEMANAGER_UI_ZIP_PLUGIN=1` — confirms that the Zip plug-in is deployed and enabled for the reported ZIP-navigation characterization test.
- `FILEMANAGER_UI_HELP_SEARCH_TERM` — a search term known to exist in the language-specific deployed `salamand.chm`.
- `FILEMANAGER_UI_HELP_EXPECTED_RESULT` — result text expected for that Help search term.

The test runner never mounts test virtual disks. If fixed writable `D:\` is unavailable, all second-volume-dependent tests are reported as successful, explicit capability skips.

The release-equivalent repository runner requires `SeCreateSymbolicLinkPrivilege` for its disposable reparse-point fixtures. If that runner lacks the privilege, it reports the missing privilege and skips the complete UI lane as an allowed environment limitation; the UI tests are not reported as completed. A focused or direct NUnit run remains useful on such a host: junction coverage continues, while only the directory-symbolic-link case is reported as an explicit capability skip.

The repository runner creates the guarded filesystem and registry sandbox automatically. On an interactive Windows desktop session, its default filter runs the complete UI category:

```powershell
.\run-ui-tests.ps1 -ConfirmIsolatedProfile
```

Run every discovered test in the project, including non-UI contract and TLS cases, by clearing the default filter:

```powershell
.\run-ui-tests.ps1 -ConfirmIsolatedProfile -Filter ''
```

The runner derives its paths from the checkout and deliberately does not fall back to an installed application, because a stale executable can turn all native-command cases into misleading failures. Build the complete current-branch solution first, or pass a deployed artifact explicitly with `-ExecutablePath`. When the selected filter can run FTP coverage and standard Visual Studio output has scattered the matching FTP build under the plug-in project, the focused runner copies `salamand.exe`, `salmon.exe`, runtime resources, `ftp.spl`, and its English language file into a disposable coherent runtime and removes that runtime afterward. It fails before FTP testing if those same-configuration artifacts are unavailable; it never combines an external executable with checkout plug-ins. `-NoBuild` skips both managed build and restore for a repeated run; it does not build the native application.

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh GUID directory below `filemanager-testdata` for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, copy and move overwrite/skip conflict choices, rename overwrite-decline and case-only behavior, mixed-selection deletion, continuation after a skipped delete error, file search, internal viewing, configured-editor launch, metadata preservation, cancellation after the worker has started, and destination/name failures. ADS cases create named, empty, large, and edge-named streams; exercise overwrite and retry after a temporarily denied stream; and verify cross-volume preservation or explicit source retention when an ADS-unsupported target reports metadata loss. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never empty the recycle bin; they only inspect it after moving test-root data to it.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

## Reported-defect characterization

`ReportedDefectCharacterizationUiTests` records ZIP navigation after its information dialog and language-specific Help Search results without changing application behavior. A failed product assertion is valid evidence that the reported defect remains; a stalled or crashed test host is not an acceptable result. Existing focused cases cover the other two reports: `Move_overwrite_replaces_the_existing_target_and_removes_the_source` verifies move-collision replacement, and `Edit_file_opens_the_selected_file_in_the_configured_editor` verifies the Files > Edit command through a harness-owned editor.

ZIP is skipped unless `FILEMANAGER_UI_ZIP_PLUGIN=1` explicitly confirms deployment and enablement. Help Search is skipped unless `help/<language>/salamand.chm` is deployed beside the selected executable and both Help fixture variables are set. The tests must not install or reconfigure plug-ins, replace help content, or modify application behavior to make an assertion pass.

## Live MojeRzeczy FTPS UI lane

`MojeRzeczyFtpsUiTests` contacts an external server and is deliberately marked `Explicit` and excluded from `scripts/runtests.ps1` and CI. It reads its credentials only from the variables used by `C:\Projects\FtpMojerzeczy`, configures explicit FTPS on port 21 with passive and binary transfer, accepts a hostname-invalid certificate for the disposable session only, dismisses the plug-in's modeless welcome message, then downloads `/skan.txt` into the disposable test-data root. It waits for the worker to release the file and verifies the downloaded size against `C:\Projects\FtpMojerzeczy\skan.txt`. When the Debug build shows an error dialog, the test records its native text under `TestResults\ftp-debug-error-dialogs`, dismisses it, and fails rather than waiting for desktop input. If either credential is absent, it passes without opening an FTP connection and reports that FTP UI tests have not been performed due to missing credentials.

```powershell
$env:MOJERZEC_USERNAME = "your-username"
$env:MOJERZEC_PASSWORD = "your-password"
.\scripts\run-ftp-test.ps1
```

Do not reuse this certificate policy outside this one test server.

## Transactional configuration fault injection

The `FaultInjection` category first measures the exact registry-write count of a real configuration commit, then starts fresh executables at uniformly spaced payload writes and five named transaction mutations (checksum, completion marker, generation flush, active-generation selector, and store flush). Named atomic-tail points remain stable when earlier tests or plug-ins change the inactive snapshot's write count. The bounded structural matrix remains practical when plug-ins add thousands of values. The native test hook terminates each process immediately after the selected successful registry mutation. A clean restart must show either the complete baseline setting or the complete candidate setting; the test fails if the process did not terminate at the requested boundary or if restart exposes a mixture.

Run this separately on the disposable profile because it intentionally terminates the executable many times:

```powershell
$env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=FaultInjection
```

The executable honors `FILEMANAGER_CONFIG_FAULT_AFTER_WRITE` and `FILEMANAGER_CONFIG_FAULT_REPORT` only while `FILEMANAGER_UI_ISOLATED=1` is present. The test also supplies `FILEMANAGER_CONFIG_FAULT_ARM_FILE` and creates that owned marker immediately before accepting the Configuration dialog, preventing automatic plug-in startup saves from consuming the requested boundary. Users should not need to set these variables manually.
