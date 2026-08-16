# Open Salamander

Open Salamander is a fast and reliable two-panel file manager for Windows.

## Origin

The original version of Servant Salamander was developed by Petr Šolín during his studies at the Czech Technical University. He released it as freeware in 1997. After graduation, Petr Šolín founded the company [Altap](https://www.altap.cz/) in cooperation with Jan Ryšavý. In 2001 they released the first shareware version of the program. In 2007 a new version was renamed to Altap Salamander 2.5. Many other programmers and translators [contributed](AUTHORS) to the project. In 2019, Altap was acquired by [Fine](https://www.finesoftware.eu/). After this acquisition, Altap Salamander 4.0 was released as freeware. In 2023, the project was open sourced under the GPLv2 license as Open Salamander 5.0.

The name Servant Salamander came about when Petr Šolín and his friend Pavel Schreib were brainstorming name for this project. At that time, the well-known file managers were the aging Norton Commander and the rising Windows Commander. They questioned why a file manager should be named Commander, which implied that it commanded instead of served. This thought led to the birth of the name Servant Salamander.

Please bear with us as Salamander was our first major project where we learned to program in C++. From a technology standpoint, it does not use [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), smart pointers, [RAII](https://en.cppreference.com/w/cpp/language/raii), [STL](https://github.com/microsoft/STL), or [WIL](https://github.com/microsoft/wil), all of which were just beginning to evolve during the time Salamander was created. Historically, many comments were written in Czech. However, an active community effort is translating the codebase to English to improve accessibility for international contributors. Salamander is a pure WinAPI application and does not use any frameworks, such as MFC.

We would like to thank [Fine company](https://www.finesoftware.eu/) for making the open sourced Salamander release possible.

## Open Salamander 5.0 Updates

The 5.0 release marks a transition to open development with several key enhancements:

- **UI Modernization:** Introduced high-quality SVG icons for toolbars, replacing legacy bitmaps for better scaling on modern displays.
- **Performance Breakthroughs:**
  - **Asynchronous Loading:** File icons are now loaded using a dedicated thread pool, significantly speeding up directory browsing.
  - **Optimized I/O:** Local-to-local file operations now use a 1MB buffer to minimize system calls and improve throughput.
  - **Memory Management:** Refined memory allocation strategies specifically for Unicode string handling.
- **Enhanced Unicode Support:** Comprehensive fixes for Unicode handling in window titles, file execution, and viewer outputs, ensuring full compatibility with international filenames.
- **Codebase Internationalization:** We are systematically translating legacy Czech comments into English (`// CommentsTranslationProject: TRANSLATED`) to foster a global contributor community.
- **Reliability:** Addressed critical threading issues, fixed "Access Denied" errors in worker threads, and resolved stability bugs in directory refreshing.

## Development

### Prerequisites

- Windows 11 or newer
- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)
- [Desktop development with C++](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170) workload installed in VS2022
- [Windows 11 (10.0.26100.4654) SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) optional component installed in VS2022

### Optional requirements

- [Git](https://git-scm.com/downloads)
- [PowerShell 7.4](https://learn.microsoft.com/en-us/powershell/scripting/install/installing-powershell-on-windows) or newer
- [HTMLHelp Workshop 1.3](https://learn.microsoft.com/en-us/answers/questions/265752/htmlhelp-workshop-download-for-chm-compiler-instal)

- Set the ```OPENSAL_BUILD_DIR``` environment variable to specify the build directory. Make sure the path has a trailing backslah, e.q. ```D:\Build\OpenSal\```

### Building

Solution ```\src\vcxproj\salamand.sln``` may be built from within Visual Studio or from the command-line using ```\src\vcxproj\rebuild.cmd```.

Use ```\src\vcxproj\!populate_build_dir.cmd``` to populate build directory with files required to run Open Salamander.

### Automated testing

See [testing.md](testing.md) for test prerequisites, commands, safety requirements, CI coverage, and a short description of every automated test available in the repository.

Reliability objectives, responsible roles, required incident records, and release-review evidence are defined in [reliability.md](reliability.md). Release symbols remain private: the retained artifact contains PDBs plus a verified CodeView GUID/age and SHA-256 index for exact-build crash symbolization.

### Creating Installer

Open Salamander uses [Inno Setup](https://jrsoftware.org/isinfo.php) to create the installer. The installer script is located at `Installer\setup.iss`.

#### Building Locally

**Recommended:** Use the automated build script that reproduces the GitHub Actions workflow:

```powershell
# Build Release configuration with v143 toolset (VS2022)
.\scripts\build-installer.ps1

# Build with different configuration or toolset
.\scripts\build-installer.ps1 -Configuration Debug -PlatformToolset v145

# Skip tests (for quick iteration)
.\scripts\build-installer.ps1 -SkipTests
```

The script automatically:
- Builds the solution with MSBuild
- Runs native regression tests
- Creates PE manifest for toolset verification
- Installs Inno Setup 6.7.3 (if not present)
- Stages files for Inno Setup
- Compiles the installer

**Manual build:** For debugging, you can run the individual steps:

1. Install [Inno Setup 6](https://jrsoftware.org/isdl.php) or later
2. Build the solution in Release|x64 configuration
3. Stage the files and compile the installer:

```powershell
# Stage files for the installer
.\tools\prepare_installer.ps1 -BuildDir "build_stage" -StagingDir "Installer_Staging"

# Compile the installer
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "Installer\setup.iss"
```

The installer will be created in `Installer\Output\`.

#### GitHub Actions CI/CD

The repository includes a GitHub Actions workflow (`.github\workflows\build-installer.yml`) that automatically:

1. Runs the complete `scripts/runtests.ps1` inventory without skipped tests on the dedicated release-test runner
2. Builds the solution using MSBuild
3. Stages all required files (executables, plugins, language files, toolbars)
4. Downloads the version- and SHA-256-pinned Inno Setup input and verifies its Authenticode signature
5. Compiles the installer with build number versioning
6. Creates a GitHub release with the installer attached

The workflow is triggered on pushes to `main` and produces versioned installers named `OpenSalamander_6.0.{build_number}.exe`.

## Customization

### Icons
Open Salamander uses scalable SVG icons for its toolbars.

- **Location:** `src\res\toolbars`
- **Format:** Standard SVG
- **Dimensions:** The standard viewbox is **16x16 pixels**.
- **Process:** To add or update an icon, simply place the `.svg` file in the `src\res\toolbars` directory. The build scripts (`!populate_build_dir.cmd` for local dev and `Create-Sfx.ps1` for installer) will automatically include them.

### Execution Logging

The execution logging system runs only in DEBUG builds. It records major application execution paths (startup, plugin loading, directory listing, file operations, and key UI features) through the Trace system. The logs are emitted as TRACE messages, so they appear in the Trace Server when it is connected. In release builds, the logging calls are compiled out and produce no output.

#### Running the Trace Server

Build the `tserver` project in the `Debug | Win32` configuration, then start `tserver.exe` before debugging Open Salamander. The standard debug output path is `src\vcxproj\tserver\tserver\Debug\tserver.exe`; it can be launched from PowerShell with:

```powershell
Start-Process C:\Projects\FileManager\src\vcxproj\tserver\tserver\Debug\tserver.exe
```

Leave the window titled **Trace Server** open while the application runs. To verify that it is still running, use:

```powershell
Get-Process tserver
```

Visual Studio can start both processes automatically: configure `tserver` and the main Open Salamander project as multiple startup projects, with `tserver` set to start without debugging. Start the Trace Server with the same Windows user and privilege level as the application being debugged.

If the debug shutdown dialog reports monitored handles that remain open, choose **Yes** only after the Trace Server is running. The application will connect and send the list of outstanding handles (including their allocation locations); it does not launch the Trace Server itself.

### Contributing

This project welcomes contributions to build and enhance Open Salamander!

## Repository Content

```bash
\convert         Conversion tables for the Convert command
\doc             Documentation
\help            User manual source files
\src             Open Salamander core source code
\src\common      Shared libraries
\src\common\dep  Shared third-party libraries
\src\lang        English resources
\src\plugins     Plugins source code
\src\reglib      Access to Windows Registry files
\src\res         Image resources
\src\salmon      Crash detecting and reporting
\src\salopen     Open files helper
\src\salspawn    Process spawning helper
\src\setup       Installer and uinstaller
\src\sfx7zip     Self-extractor based on 7-Zip
\src\shellext    Shell extension DLL
\src\translator  Translate Salamander UI to other languages
\src\tserver     Trace Server to display info and error messages
\src\vcxproj     Visual Studio project files
\tools           Minor utilities
\translations    Translations into other languages
```

A few Open Salamander 5.0 plugins are either not included or cannot be compiled. For instance, the PictView engine ```pvw32cnv.dll``` is not open-sourced, so we should consider switching to [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec) or another library. The Encrypt plugin is incompatible with modern SSD disks and has been deprecated. The UnRAR plugin lacks [unrar.dll](https://www.rarlab.com/rar_add.htm). The FTP plugin uses Windows SChannel for FTPS and does not require bundled TLS DLLs. To build WinSCP plugin you need Embarcadero C++ Builder.

All the source code uses UTF-8-BOM encoding and is formatted with ```clang-format```. Refer to the ```\normalize.ps1``` script for more information.

## Architecture and Code Structure

### Overview

Open Salamander is a pure WinAPI C++ application with no external UI frameworks (no MFC, ATL, or Qt). The architecture follows a layered design with a plugin-based extensibility model. The codebase targets Windows Vista+ (WINVER=0x0601) and supports both x86 and x64 builds.

```
┌─────────────────────────────────────────────────────────┐
│                     UI Layer                            │
│   Main Window · File Panels · Menus · Toolbars          │
├─────────────────────────────────────────────────────────┤
│                 Business Logic Layer                    │
│   File Operations · Plugin Manager · Configuration      │
├─────────────────────────────────────────────────────────┤
│                  Service Layer                          │
│   Worker Threads · Icon Cache · File System Access      │
├─────────────────────────────────────────────────────────┤
│                   Data Layer                            │
│   Registry Storage · WinAPI File System · Plugin Data   │
└─────────────────────────────────────────────────────────┘
```

### Source File Organisation

The codebase is organised into focused file groups. Each group uses a common prefix so related files sort together:

| Prefix | Files | What it contains |
|--------|-------|-----------------|
| `mainwnd_*` | 7 | Main window: init, config, messages, commands, panels, shutdown, HTML help |
| `fileswindow_*` | 12 | File panel implementation: navigation, display, operations, archiving, etc. |
| `filesbox_*` | 2 | Virtual list-box rendering and keyboard/mouse input |
| `dialogs_*` | 8 | All dialog boxes: file ops, rename, attributes, configuration pages |
| `plugins_*` | 4 | Plugin loader, interface layer, archiver and file-system adapters |
| `toolbar_*` | 8 | Toolbar core, rendering, drag-and-drop, drive bar, hot-paths, user menu |
| `menu_*` | 4 | Popup menus, shared resources, templates, window queue |
| `gui_*` | 3 | Reusable GUI controls: progress bar, static text, buttons and animations |
| `zip_*` | 4 | ZIP/archive API: progress, general API, utilities, directory management |
| `find_*` | 3 | Find dialog: result data structures, dialog UI, toolbar |
| `app_*` | 2 | Application entry point and global variable definitions |
| `transfer_speed`, `async_copy`, `operations_core`, `worker` | 4 | File-copy engine: speed meters, async copy, operation dispatch |

**Rule of thumb adopted for new code:** one logical component or feature per file; aim for files under 1 500 lines.

### Core Components

#### Main Window (`src/mainwnd_*.cpp`, `src/mainwnd.h`)

`CMainWindow` is the top-level window that hosts the entire application. Its implementation is split across seven focused files:

| File | Contents |
|------|----------|
| `mainwnd_init.cpp` | Constructor, window creation, destruction, toolbar/panel layout |
| `mainwnd_config.cpp` | Configuration load/save, registry persistence |
| `mainwnd_messages.cpp` | `WindowProc` switch — delegates to the two extracted handlers below |
| `mainwnd_commands.cpp` | `HandleWmCommand` — all `WM_COMMAND` menu and toolbar dispatch (~2 500 cases) |
| `mainwnd_shutdown.cpp` | `HandleShutdown` — `WM_CLOSE`, `WM_ENDSESSION`, `WM_QUERYENDSESSION` |
| `mainwnd_help.cpp` | `CSalamanderHelp`, `OpenHtmlHelp`, `MessageBoxHelpCallback` |
| `mainwnd_panels.cpp` | Panel layout helpers, splitter, focus management |

Key hosted objects:
- Two `CFilesWindow` instances (left and right panels)
- Drive bars, toolbars (`CMainToolBar`, `CPluginsBar`, `CBottomToolBar`, `CUserMenuBar`, `CHotPathsBar`)
- Menu system (`CMenuBar`, `CMenuPopup`, `CMenuNew`)
- `CPanelStatusBar` (status bar below each panel) and `CToolTipWindow`

`CMainWindowLock` (declared in `mainwnd.h`) serialises access to the main window during shutdown; `extern CMainWindowLock MainWindowCS` is defined in `app_globals.cpp`.

#### File Panels (`src/fileswindow_*.cpp`, `src/filesbox_*.cpp`, `src/fileswnd.h`)

`CFilesWindow` implements a single file panel. Its implementation is split across twelve focused files:

| File | Contents |
|------|----------|
| `fileswindow_init.cpp` | Constructor, creation, destruction |
| `fileswindow_navigation.cpp` | `ReadDirectory`, `ChangeDir`, path history |
| `fileswindow_display.cpp` | Painting: `SetFontAndColors`, `DrawIcon`, `DrawItem` |
| `fileswindow_operations.cpp` | `MakeFileList`, `MoveFiles`, `BuildScriptMain` — copy/move scripting |
| `fileswindow_archiving.cpp` | Pack, unpack, panel enumeration data |
| `fileswindow_file_actions.cpp` | Convert, `ChangeAttr`, `FindFile`, `ViewFile` |
| `fileswindow_execute.cpp` | Open/execute files, view-template selection |
| `fileswindow_quicksearch.cpp` | Find-as-you-type quick search |
| `fileswindow_columns.cpp` | Column configuration and management |
| `fileswindow_delete.cpp` | `DeleteThroughRecycleBin`, `FilesAction` |
| `fileswindow_dir_reading.cpp` | `CVisibleFileItemsArray`, directory reading |
| `fileswindow_wndproc.cpp` | `WindowProc`, `LockUI`, `OpenDirHistory` |

The virtual list-box rendering lives in `filesbox_rendering.cpp` (`CFileListBox`) and keyboard/mouse input in `filesbox_input.cpp` (`CFileListHeader`).

Each panel operates in one of three modes driven by the path type: `ptDisk` (local/network drive), `ptZIPArchive` (archive root), or `ptPluginFS` (virtual file system provided by a plugin).

#### File Copy Engine (`src/worker.h`, `src/worker.cpp`, `src/async_copy.cpp`, `src/operations_core.cpp`, `src/transfer_speed.cpp`)

Long-running operations (copy, move, delete, rename) execute on dedicated worker threads. The implementation is split across four files:

| File | Contents |
|------|----------|
| `transfer_speed.cpp` | `CTransferSpeedMeter`, `CProgressSpeedMeter` — real-time throughput measurement |
| `async_copy.cpp` | `CCopy_Context`, `DoCopyFile`, `DoMoveFile`, `DoDeleteFile`, `DoCreateDir` — the core async copy engine with overlapped I/O |
| `operations_core.cpp` | `DoConvert`, `DoChangeAttrs`, `ThreadWorker`, `StartWorker`, `COperationsQueue` |
| `worker.cpp` | `COperations` methods, UTF-8 file helpers, buffer-size heuristics |

The operation pipeline:
1. User triggers a command → `CCriteriaData` is built with masks, attributes, and speed limits.
2. A `COperations` object is created and a worker thread is started via `StartWorker`.
3. The worker processes each file, updates `CProgressData`, and communicates back via Windows messages.
4. On completion or cancellation, both source and target panels are refreshed.

`CAsyncCopyParams` (declared in `worker.h`) manages overlapped-I/O buffers for the async path. `CWorkerData` and `CProgressDlgData` (also in `worker.h`) carry per-operation state across the four translation units.

#### ZIP and Archive API (`src/zip_*.cpp`, `src/zip.h`)

The plugin-facing archive API (`CSalamanderGeneral`, `CSalamanderDirectory`, etc.) is split into four files:

| File | Contents |
|------|----------|
| `zip_progress.cpp` | `CZIPUnpackProgress` — progress dialog integration |
| `zip_general_api.cpp` | `CSalamanderGeneral` — the main archive API surface (~3 000 lines) |
| `zip_utilities.cpp` | `CSalamanderBMSearchDataImp`, `CSalamanderMD5Imp`, `CSalamanderPNG`, `CSalamanderCrypt` |
| `zip_directory.cpp` | `CSalamanderDirectory`, `CSalamanderForOperations`, `TestFreeSpace` |

#### Dialog Boxes (`src/dialogs_*.cpp`, `src/dialogs.h`)

All dialogs are grouped by function:

| File | Contents |
|------|----------|
| `dialogs_file_ops.cpp` | Copy, move, delete confirmation dialogs |
| `dialogs_rename.cpp` | Rename and batch-rename dialogs |
| `dialogs_attributes.cpp` | File-attribute dialogs, `CZipSizeResultsDialog`, `CPasswordDialog` |
| `dialogs_config_general.cpp` | Configuration pages: `CConfigPageGeneral`, `CConfigPageRegional`, `CConfigPageView` |
| `dialogs_config_viewers.cpp` | Configuration pages: Viewers, Associations |
| `dialogs_config_panels.cpp` | Configuration pages: Panels, Colors |
| `dialogs_config_environment.cpp` | Environment/paths configuration page |
| `dialogs_config_packing.cpp` | Packing/archiving configuration page |

#### Find Dialog (`src/find_*.cpp`, `src/find.h`)

| File | Contents |
|------|----------|
| `find_results.cpp` | `CFindOptions`, `CFoundFilesData`, `CFoundFilesListView` — data model and list control |
| `find_dialog_ui.cpp` | `CFindDialog` — search dialog UI and logic (~3 300 lines) |
| `finddlg2.cpp` | `CFindDialog` continuation methods, `CFindTBHeader` toolbar |

#### GUI Controls (`src/gui_*.cpp`, `src/gui.h`)

Reusable custom controls shared across all dialogs and the main window:

| File | Contents |
|------|----------|
| `gui_progressbar.cpp` | `CProgressBar` — animated, self-moving progress bar |
| `gui_statictext.cpp` | `CStaticText` — text control with ellipsis, path compaction, tooltips |
| `gui_controls.cpp` | `CButton`, `CColorArrowButton`, `CToolbarHeader`, `CAnimate`, layout helpers |

`CGuiBitmap` (a memory-DC helper for flicker-free button drawing) is declared in `gui_bitmap.h` and shared by `gui_progressbar.cpp` and `gui_controls.cpp`.

#### Plugin System (`src/plugins_*.cpp`, `src/plugins.h`, `src/plugins/`)

| File | Contents |
|------|----------|
| `plugins_loading.cpp` | Discovery, `LoadLibraryUtf8`, version check, activation |
| `plugins_interface.cpp` | `CPluginInterfaceEncapsulation` — thread-safety guards |
| `plugins_archiver.cpp` | Archiver plugin adapter |
| `plugins_filesystem.cpp` | Virtual file-system plugin adapter |

Plugins implement abstract interface classes:

| Interface | Purpose |
|-----------|---------|
| `CPluginInterfaceAbstract` | Base; version negotiation |
| `CPluginInterfaceForArchiverAbstract` | List/pack/unpack archives |
| `CPluginInterfaceForViewerAbstract` | File preview/viewer |
| `CPluginInterfaceForFileSystemAbstract` | Virtual file systems (e.g. FTP) |
| `CPluginInterfaceForThumbLoaderAbstract` | Thumbnail generation |

Every call is wrapped with `EnterPlugin()` / `LeavePlugin()` guards for thread safety and call-stack tracking.

#### Application Entry (`src/app_entry.cpp`, `src/app_globals.cpp`)

| File | Contents |
|------|----------|
| `app_globals.cpp` | All global variable definitions — runtime flags, GDI handles, colour tables, enabler arrays, `CMainWindowLock MainWindowCS` |
| `app_entry.cpp` | `MyEntryPoint`, `WinMain`, `WinMainBody`, locale init, graphics init, CRC-32 helpers |

#### Common Library (`src/common/`)

Shared infrastructure used by both the core and all plugins:

| File | Purpose |
|------|---------|
| `array.h` | `TIndirectArray<T>` — typed dynamic array template |
| `strutils.h/cpp` | UTF-8 ↔ wide string conversion, `CreateFileUtf8`, `DeleteFileUtf8`, etc. |
| `handles.h/cpp` | Handle tracking and leak detection in debug builds |
| `messages.h/cpp` | Typed message-box helpers |
| `allochan.h/cpp` | Allocation tracking wrappers |
| `heap.h/cpp` | Custom heap management |
| `crc32.h/cpp` | CRC-32 checksum |
| `moore.h/cpp` | Boyer-Moore string search |
| `multimon.cpp` | Multi-monitor layout support |

#### Crash Reporting (`src/salmon/`)

`SalmonInit()` is called before `WinMain` via `MyEntryPoint()` in `app_entry.cpp`. It installs an unhandled-exception filter that captures a minidump and call-stack trace, enabling post-mortem analysis of field crashes.

### Key Data Structures

| Type | Description |
|------|-------------|
| `CFileData` | Metadata for one file or directory (name, size, time, attributes, plugin-specific data) |
| `CSalamanderDirectory` | Full directory listing exposed to plugins via the archive API |
| `CCriteriaData` | Parameters for a copy/move: masks, date range, size limits, speed cap |
| `CAsyncCopyParams` | Overlapped-I/O buffer block for the async copy path (declared in `worker.h`) |
| `CWorkerData` / `CProgressDlgData` | Per-operation worker state shared across the copy-engine translation units |
| `CDirectorySizeCache` | Cached directory-size results (avoids redundant recursive scans) |
| `CVisibleFileItemsArray` | Tracks which file items are currently visible in the panel for incremental refresh |
| `CChangeCaseData` | Options for a batch rename-case operation |
| `CAttrsData` | Parameters for changing file attributes in bulk |

### Window Class Hierarchy

All UI objects derive from a thin `CWindow` base that wraps a WinAPI `HWND` and routes messages through a virtual `WindowProc()`. There are no MFC `CWnd` semantics; message handling is done with explicit `WM_*` comparisons.

```
CWindow
├── CMainWindow
│   ├── CFilesWindow (×2, left/right panels)
│   │   ├── CFileListBox          (virtual list-box rendering)
│   │   └── CFileListHeader       (column headers with drag-to-resize)
│   ├── CMenuBar
│   ├── CMainToolBar / CPluginsBar / CBottomToolBar / ...
│   ├── CDriveBar
│   └── CPanelStatusBar           (status bar below each panel)
└── CDialog (modal/modeless dialogs)
    ├── CFindDialog               (find dialog with CFoundFilesListView)
    ├── CConfigPageGeneral / CConfigPageRegional / CConfigPageView / ...
    ├── CPasswordDialog
    ├── CZipSizeResultsDialog
    └── CInlineRenameEdit         (in-place rename edit control)
```

### Naming Conventions

The codebase follows these conventions for new and refactored code:

| Category | Convention | Example |
|----------|-----------|---------|
| Dialog classes | `C*Dialog` | `CPasswordDialog`, `CZipSizeResultsDialog` |
| Window classes | `C*Window` | `CMainWindow`, `CFilesWindow` |
| Data-holder structs | `C*Data` or `C*Info` | `CFileData`, `CCriteriaData` |
| Manager / cache classes | `C*Manager` or `C*Cache` | `CDirectorySizeCache`, `CIconCache` |
| Locks / critical sections | `C*Lock` | `CMainWindowLock`, `CStringResourceLock` |

### Build System

The solution (`src/vcxproj/salamand.sln`) targets MSVC v145 (Visual Studio 2026) for local development. Release CI also builds v143 (VS 2022) and compares its release artifacts and complete executable test inventory with v145 before packaging. MSBuild property sheets layer the configuration:

- `sal_base.props` — common defines, include paths, warning level
- `sal_debug.props` / `sal_release.props` — optimization and debug flags
- `x86.props` / `x64.props` — platform-specific settings

The `OPENSAL_BUILD_DIR` environment variable controls where build artifacts are placed. `!populate_build_dir.cmd` stages executables, plugins, language files, and resources into a runnable layout.

Each plugin is its own `.vcxproj` linked into the solution and produces a DLL placed alongside the main executable.

### Diagnostics and Debugging

- `CALL_STACK_MESSAGE*` macros maintain a lightweight call-stack log available in crash reports without requiring full debug symbols.
- `HANDLES_ENABLE` (debug-only) activates `handles.h` tracking that asserts on leaked or double-closed WinAPI handles.
- The Trace Server (`src/tserver/`) receives `TRACE_*` messages emitted throughout the codebase and displays them in a separate window; all trace calls are compiled out in release builds.

## Resources

- [Open Salamander Website](https://www.opensalamander.org/)

## License

Open Salamander is open source software licensed [GPLv2](doc/license_gpl.txt) and later.
Individual [files and libraries](doc/third_party.txt) have a different, but compatible license.
