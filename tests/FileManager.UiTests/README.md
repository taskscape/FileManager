# FileManager UI tests

This project contains 100 basic, parameterized FlaUI/UIA3 NUnit cases plus focused file-operation characterization cases for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete/find/view/edit commands.

The tests run as the current interactive Windows user only when a guarded filesystem and registry sandbox are selected. The harness creates and deletes its own `filemanager-testdata` directory and the suffixed registry key after each run.

Set these environment variables before running:

- `FILEMANAGER_UI_TESTDATA_ROOT` — absolute path ending in `filemanager-testdata`; all test-created files live below it.
- `FILEMANAGER_UI_CONFIG_ROOT=Software\Open Salamander\6.0-filemanager-testdata` — selects the registry tree created and removed by the harness.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` — runtime command ID allocated by FileManager for the FTP Client **Organize Bookmarks** menu command. This enables the 10 FTP bookmark persistence cases; without it, only those cases are skipped with an explicit message.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — an existing dedicated directory on a different volume from `%TEMP%`. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` — an existing dedicated directory on a different volume that does not support alternate data streams (for example FAT/FAT32/exFAT). This enables the ADS metadata-loss decision scenario and the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.

The release workflow creates fresh NTFS source/cross-volume VHDX images and an exFAT ADS-unsupported VHDX image for each complete UI job, then detaches them in an `always()` cleanup step. Local runs may continue to point the two additional-volume variables at dedicated existing test volumes.

Run the suite on an interactive Windows desktop session:

```powershell
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=UI
```

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh GUID directory below `filemanager-testdata` for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, copy and move overwrite/skip conflict choices, rename overwrite-decline and case-only behavior, mixed-selection deletion, continuation after a skipped delete error, file search, internal viewing, configured-editor launch, metadata preservation, cancellation after the worker has started, and destination/name failures. ADS cases create named, empty, large, and edge-named streams; exercise overwrite and retry after a temporarily denied stream; and verify cross-volume preservation or explicit source retention when an ADS-unsupported target reports metadata loss. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never empty the recycle bin; they only inspect it after moving test-root data to it.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

## Transactional configuration fault injection

The `FaultInjection` category first measures the exact registry-write count of a real configuration commit, then starts fresh executables at uniformly spaced payload writes and five named transaction mutations (checksum, completion marker, generation flush, active-generation selector, and store flush). Named atomic-tail points remain stable when earlier tests or plug-ins change the inactive snapshot's write count. The bounded structural matrix remains practical when plug-ins add thousands of values. The native test hook terminates each process immediately after the selected successful registry mutation. A clean restart must show either the complete baseline setting or the complete candidate setting; the test fails if the process did not terminate at the requested boundary or if restart exposes a mixture.

Run this separately on the disposable profile because it intentionally terminates the executable many times:

```powershell
$env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=FaultInjection
```

The executable honors `FILEMANAGER_CONFIG_FAULT_AFTER_WRITE` and `FILEMANAGER_CONFIG_FAULT_REPORT` only while `FILEMANAGER_UI_ISOLATED=1` is present. The test also supplies `FILEMANAGER_CONFIG_FAULT_ARM_FILE` and creates that owned marker immediately before accepting the Configuration dialog, preventing automatic plug-in startup saves from consuming the requested boundary. Users should not need to set these variables manually.
