# FileManager UI tests

This project contains 100 basic, parameterized FlaUI/UIA3 NUnit cases plus focused file-operation characterization cases for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete commands.

The tests intentionally refuse to run unless `FILEMANAGER_UI_ISOLATED=1` is set. The application persists configuration under the current user registry hive, so run them under a dedicated Windows test account or another isolated user profile.

Set these environment variables before running:

- `FILEMANAGER_UI_ISOLATED=1` — confirms that the current Windows profile is disposable.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` — runtime command ID allocated by FileManager for the FTP Client **Organize Bookmarks** menu command. This enables the 10 FTP bookmark persistence cases; without it, only those cases are skipped with an explicit message.
- `FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1` — explicitly enables the exhaustive transactional-configuration crash-recovery lane described below.
- `FILEMANAGER_UI_CROSS_VOLUME_ROOT` — an existing dedicated directory on a different volume from `%TEMP%`. This enables the cross-volume move characterization fixture; the fixture creates and removes only a GUID-named child below this directory.
- `FILEMANAGER_UI_RECYCLE_BIN=1` — explicitly enables the recycle-bin characterization test. It requires the default recycle-bin delete setting in the isolated profile and adds one disposable file to that profile's recycle bin.

Run the suite on an interactive Windows desktop session:

```powershell
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=UI
```

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh disposable directory tree under the system temporary directory for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, overwrite/overwrite-all and skip/skip-all conflict choices, metadata preservation, cancellation after the worker has started, destination/name failures, and a locked-file delete failure. The recovery fixture seeds an incomplete durable journal with a ready transactional sibling file before launch, then verifies the real startup reconciliation flow. The tests never use a caller-supplied directory as their mutation target; cross-volume cases use only a GUID child under the explicitly configured disposable root.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.

## Transactional configuration fault injection

The `FaultInjection` category first measures the exact registry-write count of a real configuration commit, then starts a fresh executable for every write boundary. The native test hook terminates that process immediately after the selected successful registry mutation. A clean restart must show either the complete baseline setting or the complete candidate setting; the test fails if the process did not terminate at the requested boundary or if restart exposes a mixture.

Run this separately on the disposable profile because it intentionally terminates the executable many times:

```powershell
$env:FILEMANAGER_UI_CONFIG_FAULT_INJECTION = '1'
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=FaultInjection
```

The executable honors `FILEMANAGER_CONFIG_FAULT_AFTER_WRITE` and `FILEMANAGER_CONFIG_FAULT_REPORT` only while `FILEMANAGER_UI_ISOLATED=1` is present. The test supplies those two variables to its child executable processes; users should not need to set them manually.
