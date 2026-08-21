# FileManager UI tests

This project contains 100 basic, parameterized FlaUI/UIA3 NUnit cases plus focused file-operation characterization cases for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete/find/view/edit commands.

The tests intentionally refuse to run unless `FILEMANAGER_UI_ISOLATED=1` is set. The application persists configuration under the current user registry hive, so run them under a dedicated Windows test account or another isolated user profile.

Set these environment variables before running:

- `FILEMANAGER_UI_ISOLATED=1` — confirms that the current Windows profile is disposable.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CONFIG_FAULT_BOUNDARY_LIMIT` — optional positive boundary cap for short fault-injection smoke runs; omit it for the exhaustive lane.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — an existing dedicated directory on a different volume from `%TEMP%`. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT` — an existing dedicated directory on a different volume that does not support alternate data streams (for example FAT/FAT32/exFAT). This enables the ADS metadata-loss decision scenario and the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.
- `FILEMANAGER_UI_ZIP_PLUGIN=1` — confirms that the Zip plug-in is installed and enabled for the reported ZIP-navigation characterization test.
- `FILEMANAGER_UI_HELP_SEARCH_TERM` — a search term known to exist in the language-specific deployed `salamand.chm`.
- `FILEMANAGER_UI_HELP_EXPECTED_RESULT` — text expected in the Help Search result for that term.

Run the suite on an interactive Windows desktop session:

```powershell
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=UI
```

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh disposable directory tree under the system temporary directory for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, copy and move overwrite/skip conflict choices, rename overwrite-decline and case-only behavior, mixed-selection deletion, continuation after a skipped delete error, file search, internal viewing, configured-editor launch, metadata preservation, cancellation after the worker has started, and destination/name failures. ADS cases create named, empty, large, and edge-named streams; exercise overwrite and retry after a temporarily denied stream; and verify cross-volume preservation or explicit source retention when an ADS-unsupported target reports metadata loss. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never use a caller-supplied directory as their mutation target; cross-volume cases use only a GUID child under the explicitly configured disposable root.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. The isolated test protocol queries that runtime ID from the tested process; when the FTP plug-in is absent, only the dependent cases are skipped with an explicit prerequisite message. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

## Reported-defect characterization

`ReportedDefectCharacterizationUiTests` records four product reports without changing application behavior: ZIP navigation after its information dialog, language-specific Help Search results, move-collision overwrite and cancel semantics, and responsiveness plus editor launch through **Files > Edit**. These are regression contracts, so a failed assertion can be the intended evidence that the reported product defect is present; a crashed or stalled test host is never an acceptable result.

The move and edit cases are self-contained. ZIP is skipped unless `FILEMANAGER_UI_ZIP_PLUGIN=1` explicitly confirms the plug-in is installed and enabled. Help Search is skipped unless a language-specific `help/<language>/salamand.chm` is deployed and both Help fixture variables are set. For example:

```powershell
$env:FILEMANAGER_UI_ISOLATED = '1'
$env:FILEMANAGER_UI_EXE = 'D:\FileManager\src\vcxproj\salamander\Debug_x64\salamand.exe'
$env:FILEMANAGER_UI_ZIP_PLUGIN = '1'
$env:FILEMANAGER_UI_HELP_SEARCH_TERM = '<term present in this CHM language>'
$env:FILEMANAGER_UI_HELP_EXPECTED_RESULT = '<expected rendered result text>'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj `
  --filter 'FullyQualifiedName~ReportedDefectCharacterizationUiTests'
```

The fixture creates only disposable files under its per-test temporary workspace. It may open the configured external editor, but closes only the editor window identified by its unique test filename. It must not install or reconfigure plug-ins, replace help content, change editor configuration, or modify native application code to make the assertions pass.

## Transactional configuration fault injection

The `FaultInjection` category first measures the exact registry-write count of a real configuration commit, then starts a fresh executable for every write boundary. After validating a clean baseline, the fixture keeps an immutable in-memory snapshot of the FileManager configuration and recovery-backup registry roots; while no FileManager process is running, it restores that snapshot before every boundary and again during final cleanup. The native test hook terminates the process immediately after the selected successful registry mutation. A clean restart must show either the complete baseline setting or the complete candidate setting; the test fails if the process did not terminate at the requested boundary or if restart exposes a mixture.

Run this separately on the disposable profile because it intentionally terminates the executable many times:

```powershell
$env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=FaultInjection
```

The fixture supplies `FILEMANAGER_CONFIG_FAULT_REPORT` to measure a completed save and arms each interruption through the isolated native test protocol; users should not set these internal controls manually.
