# FileManager UI tests

This project contains 100 basic, parameterized FlaUI/UIA3 NUnit cases plus focused file-operation characterization cases for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete/find/view/edit commands.

The tests run as the current interactive Windows user only when a guarded filesystem and registry sandbox are selected. The harness creates and deletes its own `filemanager-testdata` directory and the suffixed registry key after each run.

Set these environment variables before running:

- `FILEMANAGER_UI_TESTDATA_ROOT` — absolute path ending in `filemanager-testdata`; all test-created files live below it.
- `FILEMANAGER_UI_CONFIG_ROOT=Software\Open Salamander\6.0-filemanager-testdata` — selects the registry tree created and removed by the harness.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- FTP menu IDs are published with the launching process ID below the owned test-data root, so each fixture waits for the exact FileManager instance it controls before issuing a plug-in command.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — selected automatically as `D:\filemanager-testdata` when fixed writable `D:\` is available. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` — selected automatically when fixed writable `D:\` uses FAT/FAT32/exFAT. On NTFS `D:\`, the ADS-unsupported scenario is reported as an allowed capability skip.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.

The test runner never mounts test virtual disks. If fixed writable `D:\` is unavailable, all second-volume-dependent tests are reported as successful, explicit capability skips.

The complete UI suite also requires `SeCreateSymbolicLinkPrivilege` for its disposable reparse-point fixtures. If the
runner lacks that privilege, it reports the missing privilege and skips the complete UI suite as an allowed environment
limitation; the UI tests are not reported as completed.

Run the suite on an interactive Windows desktop session:

```powershell
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=UI
```

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh GUID directory below `filemanager-testdata` for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, copy and move overwrite/skip conflict choices, rename overwrite-decline and case-only behavior, mixed-selection deletion, continuation after a skipped delete error, file search, internal viewing, configured-editor launch, metadata preservation, cancellation after the worker has started, and destination/name failures. ADS cases create named, empty, large, and edge-named streams; exercise overwrite and retry after a temporarily denied stream; and verify cross-volume preservation or explicit source retention when an ADS-unsupported target reports metadata loss. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never empty the recycle bin; they only inspect it after moving test-root data to it.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

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
