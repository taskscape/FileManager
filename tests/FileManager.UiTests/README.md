# FileManager UI tests

This project contains 100 basic, parameterized FlaUI/UIA3 NUnit cases plus focused file-operation integration cases for the native FileManager UI. The cases cover application launch, accessibility-tree discovery, Configuration dialog cancel/commit/restart flows, a committed setting verified after restart, FTP bookmark creation plus edit verified after restart, and native disk create/copy/rename/move/delete commands.

The tests intentionally refuse to run unless `FILEMANAGER_UI_ISOLATED=1` is set. The application persists configuration under the current user registry hive, so run them under a dedicated Windows test account or another isolated user profile.

Set these environment variables before running:

- `FILEMANAGER_UI_ISOLATED=1` — confirms that the current Windows profile is disposable.
- `FILEMANAGER_UI_EXE` — absolute path to `salamand.exe` or a debug build of the executable.
- `FILEMANAGER_UI_ARGUMENTS` — optional command-line arguments, for example a test-only `-c` configuration file.
- `FILEMANAGER_UI_FTP_ORGANIZE_COMMAND` — runtime command ID allocated by FileManager for the FTP Client **Organize Bookmarks** menu command. This enables the 10 FTP bookmark persistence cases; without it, only those cases are skipped with an explicit message.

Run the suite on an interactive Windows desktop session:

```powershell
dotnet test tests/FileManager.UiTests/FileManager.UiTests.csproj --filter TestCategory=UI
```

The configuration dialog is opened through its stable native command ID only to avoid locale-dependent menu text. All window discovery, control inspection, focus, dialog lifecycle, and restart assertions use FlaUI/UIA3.

File-operation cases create a fresh disposable directory tree under the system temporary directory for every test, start the left and right panels with `-l`/`-r`, and use the host's stable native command IDs. They verify files and nested directory trees after normal operations, cancelled operation dialogs, destination/name failures, and a locked-file delete failure. The tests never use a caller-supplied directory as their mutation target.

The FTP plug-in menu command has no compile-time host command ID: FileManager allocates it while loading plug-ins. Keep the value in the isolated test environment rather than hard-coding it into the test project. The dialog controls themselves are located by their stable plug-in resource IDs and their persistence is asserted through UIA3 after a full application restart.
