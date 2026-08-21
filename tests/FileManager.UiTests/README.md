# FileManager UI tests

This project contains a seven-case parameterized FlaUI/UIA3 lifecycle group, together with focused file-operation, recovery, plug-in, toolbar, TLS, and native-safety characterization tests. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete/find/view/edit commands.

The tests intentionally refuse to run unless `FILEMANAGER_UI_ISOLATED=1` is set. The application persists configuration under the current user registry hive, so run them under a dedicated Windows test account or another isolated user profile.

Set these environment variables before running:

The `run-ui-tests.ps1` entry point described below sets `FILEMANAGER_UI_ISOLATED` and `FILEMANAGER_UI_EXE` for its child test process. Configure the remaining variables only when their optional test lanes are required. When calling `dotnet test` directly, set the first two variables manually.

- `FILEMANAGER_UI_ISOLATED=1` — confirms that the current Windows profile is disposable.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` — runtime command ID allocated by FileManager for the FTP Client **Organize Bookmarks** menu command. This enables the 10 FTP bookmark persistence cases; without it, only those cases are skipped with an explicit message.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — an existing dedicated directory on a different volume from `%TEMP%`. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` — an existing dedicated directory on a different volume that does not support alternate data streams (for example FAT/FAT32/exFAT). This enables the ADS metadata-loss decision scenario and the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.

Run the interactive UI category on an interactive Windows desktop session. This is the runner default and currently selects approximately 60 cases:

```powershell
.\run-ui-tests.ps1 -ConfirmIsolatedProfile
```

Run the entire test project, currently 125 discovered cases, by explicitly clearing the default category filter:

```powershell
.\run-ui-tests.ps1 -ConfirmIsolatedProfile -Filter ''
```

The complete run includes UI, native-safety, TLS, toolbar-contract, and other non-UI tests. Tests whose optional prerequisites are unavailable—such as the ZIP plug-in, deployed Help content, a second volume, fault injection, or Recycle Bin configuration—are reported as skipped rather than omitted from discovery.

The runner derives paths from its repository location, prefers an existing checkout build, and otherwise checks the Windows `App Paths` registration for an installed `salamand.exe`. Use `-ExecutablePath 'C:\Program Files\Open Salamander\salamand.exe'` to select another installation, `-Filter 'Name~Copy_file'` for a focused run, or `-NoBuild` after the managed test project is already built. The confirmation switch is intentionally required because the application writes configuration under the current user profile. File-operation tests create their own disposable source and target directories; callers do not need to configure shared test folders.

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh disposable directory tree under the system temporary directory for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, copy and move overwrite/skip conflict choices, rename overwrite-decline and case-only behavior, mixed-selection deletion, continuation after a skipped delete error, file search, internal viewing, configured-editor launch, metadata preservation, cancellation after the worker has started, and destination/name failures. ADS cases create named, empty, large, and edge-named streams; exercise overwrite and retry after a temporarily denied stream; and verify cross-volume preservation or explicit source retention when an ADS-unsupported target reports metadata loss. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never use a caller-supplied directory as their mutation target; cross-volume cases use only a GUID child under the explicitly configured disposable root.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

## Transactional configuration fault injection

The `FaultInjection` category first measures the exact registry-write count of a real configuration commit, then starts a fresh executable for every write boundary. The native test hook terminates that process immediately after the selected successful registry mutation. A clean restart must show either the complete baseline setting or the complete candidate setting; the test fails if the process did not terminate at the requested boundary or if restart exposes a mixture.

Run this separately on the disposable profile because it intentionally terminates the executable many times:

```powershell
$env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=FaultInjection
```

The executable honors `FILEMANAGER_CONFIG_FAULT_AFTER_WRITE` and `FILEMANAGER_CONFIG_FAULT_REPORT` only while `FILEMANAGER_UI_ISOLATED=1` is present. The test supplies those two variables to its child executable processes; users should not need to set them manually.
