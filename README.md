# Open Salamander

Open Salamander is a fast and reliable two-panel file manager for Windows.

[![Latest release](https://img.shields.io/github/v/release/taskscape/FileManager)](https://github.com/taskscape/FileManager/releases/latest)
[![License: GPL v2](https://img.shields.io/badge/license-GPLv2-blue.svg)](doc/license_gpl.txt)

**Current line:** 6.0 · [Download](https://github.com/taskscape/FileManager/releases/latest) · [Website](https://www.opensalamander.org/)

## Contents

- [Origin](#origin)
- [What's new in 6.0](#whats-new-in-60)
- [Open Salamander 5.0](#open-salamander-50)
- [Development](#development)
  - [Prerequisites](#prerequisites)
  - [Building](#building)
  - [Automated testing](#automated-testing)
  - [Creating the installer](#creating-the-installer)
  - [Update detection](#update-detection)
- [Customization](#customization)
- [Repository layout](#repository-layout)
- [Architecture](#architecture-and-code-structure)
- [License](#license)

## Origin

The original version of Servant Salamander was developed by Petr Šolín during his studies at the Czech Technical University. He released it as freeware in 1997. After graduation, Petr Šolín founded the company [Altap](https://www.altap.cz/) in cooperation with Jan Ryšavý. In 2001 they released the first shareware version of the program. In 2007 a new version was renamed to Altap Salamander 2.5. Many other programmers and translators [contributed](AUTHORS) to the project. In 2019, Altap was acquired by [Fine](https://www.finesoftware.eu/). After this acquisition, Altap Salamander 4.0 was released as freeware. In 2023, the project was open sourced under the GPLv2 license as Open Salamander 5.0.

The name Servant Salamander came about when Petr Šolín and his friend Pavel Schreib were brainstorming a name for this project. At that time, the well-known file managers were the aging Norton Commander and the rising Windows Commander. They questioned why a file manager should be named Commander, which implied that it commanded instead of served. This thought led to the birth of the name Servant Salamander.

Salamander was our first major C++ project. Historically it did not follow the [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines), and it made little use of smart pointers, [RAII](https://en.cppreference.com/w/cpp/language/raii), [STL](https://github.com/microsoft/STL), or [WIL](https://github.com/microsoft/wil). Open Salamander 6.0 starts adopting those practices on new and refurbished paths—scoped handle owners, selected WIL COM pointers, and modern Win32 replacements—while remaining a pure WinAPI application with no MFC or similar UI framework. Many comments were originally written in Czech; an active effort is translating them to English.

We would like to thank [Fine](https://www.finesoftware.eu/) for making the open-sourced Salamander release possible.

## What's new in 6.0

The highlights below cover repository work from **March through September 2026**, spanning the last 5.0.x builds and the 6.0 line (current GitHub releases are versioned `6.0.{build}`).

### User interface and Unicode

- Fluent-style SVG toolbar icons, an improved command bar, and per-monitor DPI awareness (`DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2`).
- Internal paths are full UTF-16, so Unicode names, UI text, and long paths are handled without the old `MAX_PATH` / ANSI bottlenecks.
- Common file dialogs use the modern Shell interfaces (`IFileOpenDialog` / `IFileDialog`) instead of the legacy `GetOpenFileName` path.
- A **Copy full path** command is available from the panel context menu.
- Configuration can be saved immediately during a session, including a follow-up save when FTP would otherwise nest a registry transaction. A failed payload write keeps the previous saved profile and reports how to retry the settings retained in memory. Concurrent instances preserve generations that their own startup did not load and confirm.

### File operations and reliability

- Copy, move, overwrite, and cancel now use durable commit boundaries, recoverable journaling, and cross-volume move protection.
- Native overwrites retain the approved destination through publication. A conflicting new destination is preserved; the previous version is restored or retained as a sibling ending in `.previous`, with its location recorded in the operation journal. A failed publication or durability check retains a move's source. Retained backups should be kept until the conflict is resolved.
- Startup recovery resumes or removes a temporary file only when its recorded identity and content still match, including the destination and parent directory. Cancel, conflicts, unavailable storage, and failed recovery remain pending. Live operations in another instance are excluded. Legacy journals, partial copies, files with named streams, and interrupted publications with retained backups are preserved for manual recovery; the report lists their paths. Large journals are read incrementally, and unreadable journals are reported instead of silently skipped.
- Native workers revalidate file identity, track NTFS/ReFS/FAT/SMB metadata loss, and exercise junctions, symlinks, mount points, and cloud placeholders.
- Resource cleanup uses RAII owners (`CScopedKernelHandle` and related scoped types) on the paths that were refurbished.
- Crash reporting uploads over HTTPS; release symbols stay private and are indexed by CodeView GUID/age plus SHA-256 for exact-build symbolization.

### Plugins and network

- **PictView** decodes and encodes through [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec). The closed-source `pvw32cnv.dll` engine and `salpvenv.exe` envelope are gone. WIC covers mainstream formats plus OS codecs (HEIF, WebP, AVIF, camera RAW), but not the long tail of legacy formats the original engine handled.
- **FTP/FTPS** uses Windows SChannel (no bundled TLS DLLs). Transfers are transactional and resumable, certificate exceptions are stored more safely, and passive FTPS data-channel TLS plus expired-exception compaction crashes were fixed.
- FTP downloads keep an existing local file intact while receiving data into a unique sibling `.salftp-*.part` file. The matching `.meta` file records ownership and verified resume checkpoints. A move deletes its remote source only after local validation, flush, metadata, publication, and checked completion succeed. Cancellation or failure can leave these private files for a later retry; keep them together. Resume requires a matching remote version and unchanged local evidence. Legacy, corrupted, named-stream, reparse-point, or ambiguous publication states require manual inspection; a `.previous` sibling retains the old destination when publication cleanup cannot finish. Restart never replays remote deletion.
- Bundled engines were upgraded: [7-Zip 26.02](src/plugins/7zip/doc/upgrade-26.02.md), zlib, SQLite (with defined recovery behavior), bzip2, and cmark-gfm.
- UnRAR plugin loading was restored; 64-bit file-size handling was applied to the active plug-in readers.

### Build, tests, and releases

- The solution targets **Visual Studio 2026** (MSVC v145). Windows 11 SDK `10.0.26100.4654` is the documented SDK component.
- Every push to `main` builds a versioned Inno Setup installer and publishes a GitHub release named `OpenSalamander_6.0.{build_number}.exe`.
- Shortly after startup, Open Salamander checks `https://api.github.com/repos/taskscape/FileManager/releases/latest`. When a newer release exists, the title bar shows `(update available)` and **Help → Download update** opens the [releases page](https://github.com/taskscape/FileManager/releases).
- Automated coverage now includes FlaUI/UIA3 UI tests, VHDX/virtual-volume cases, release-parity packaging (`scripts\runtests.ps1`), PE hardening audits, and an optional Application Verifier lock-stress lane.

## Open Salamander 5.0

The 5.0 line was the first open-source release. Work that remains part of the product includes:

- SVG toolbar icons in place of legacy bitmaps, for scaling on modern displays.
- Asynchronous file-icon loading on a dedicated thread pool.
- A 1 MB buffer for local-to-local file operations.
- Unicode handling in window titles, file execution, and viewer output.
- Systematic translation of Czech comments to English (`// CommentsTranslationProject: TRANSLATED`).
- Threading and directory-refresh reliability fixes, including Access Denied errors in worker threads.

## Development

### Prerequisites

- Windows 11 or newer
- [Visual Studio 2026](https://visualstudio.microsoft.com/downloads/) with the [Desktop development with C++](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-180) workload
- [Windows 11 (10.0.26100.4654) SDK](https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/) optional component in Visual Studio 2026

#### Optional tools

- [Git](https://git-scm.com/downloads)
- [PowerShell 7.4](https://learn.microsoft.com/en-us/powershell/scripting/install/installing-powershell-on-windows) or newer
- [HTML Help Workshop 1.3](https://learn.microsoft.com/en-us/answers/questions/265752/htmlhelp-workshop-download-for-chm-compiler-instal)
- [Application Verifier for Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/application-verifier) for the nightly lock-stress lane (the `Locks` layer must be registered)

### Building

Set the `OPENSAL_BUILD_DIR` environment variable to the build directory. The path must end with a backslash, for example `D:\Build\OpenSal\`.

The solution `src\vcxproj\salamand.sln` can be built from Visual Studio or from the command line with `src\vcxproj\rebuild.cmd`.

Use `src\vcxproj\!populate_build_dir.cmd` to copy the files required to run Open Salamander into the build directory.

### Automated testing

See [testing.md](testing.md) for test prerequisites, commands, safety requirements, CI coverage, and a short description of every automated test in the repository.

Reliability objectives, responsible roles, required incident records, and release-review evidence are defined in [reliability.md](reliability.md). Release symbols remain private: the retained artifact contains PDBs plus a verified CodeView GUID/age and SHA-256 index for exact-build crash symbolization.

### Creating the installer

Open Salamander uses [Inno Setup](https://jrsoftware.org/isinfo.php). The script is `Installer\setup.iss`.

#### Building locally

**Recommended:** the release-parity runner reproduces the GitHub release gate and installer-build jobs locally:

```powershell
.\scripts\runtests.ps1
```

With no arguments, the runner assumes `HEAD^`, build number `0`, toolset `v145`, the release test filter, and a generated TRX path. It runs the complete Debug release gate, preflights the cached and verified pinned Inno Setup compiler in portable current-user mode, builds Release x64, audits PE hardening, verifies private symbols, stages files, and compiles the installer. It does not publish a GitHub release.

`scripts\build-installer.ps1` is useful for packaging-only iteration, but it does not replace release-pipeline validation.

**Manual build:** for debugging, run the individual steps:

1. Install [Inno Setup 6](https://jrsoftware.org/isdl.php) or later.
2. Build the solution in the Release | x64 configuration.
3. Stage the files and compile the installer:

```powershell
# Stage files for the installer
.\tools\prepare_installer.ps1 -BuildDir "build_stage" -StagingDir "Installer_Staging"

# Compile the installer
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "Installer\setup.iss"
```

The installer is written to `Installer\Output\`.

#### GitHub Actions CI/CD

The workflow `.github\workflows\build-installer.yml` runs on pushes to `main` and:

1. Runs the complete `scripts\runtests.ps1` inventory without skipped tests on the dedicated release-test runner.
2. Builds the solution with MSBuild.
3. Stages executables, plugins, language files, and toolbars.
4. Downloads the version- and SHA-256-pinned Inno Setup input and verifies its Authenticode signature.
5. Compiles the installer with build-number versioning.
6. Creates a GitHub release with the installer attached.

Installers are named `OpenSalamander_6.0.{build_number}.exe`.

### Update detection

Open Salamander checks for newer builds automatically. The mechanism uses the GitHub releases above; no version files or manual steps are required.

1. Shortly after startup, a single background thread queries `https://api.github.com/repos/taskscape/FileManager/releases/latest` (see `src\update_check.cpp`).
2. The release `"published_at"` UTC timestamp is compared with the PE COFF header link timestamp embedded in the running executable. That stamp stays correct across installer and file-copy operations.
3. If the newest release was published **at least one hour** after this executable was linked, an update is considered available. The margin absorbs clock skew and CI publishing delay.
4. If there is no internet connection, the check is skipped silently and never retried — exactly one attempt is made per session.

When an update is found, the main window title bar gains an `(update available)` suffix and **Help → Download update** (which replaced the former *Official Support Forum* entry) becomes enabled. It opens the [releases page](https://github.com/taskscape/FileManager/releases) in the default web browser. Until then the item stays disabled.

## Customization

### Icons

Open Salamander uses scalable SVG icons for its toolbars.

| Property | Value |
| --- | --- |
| Location | `src\res\toolbars` |
| Format | Standard SVG |
| Dimensions | The standard viewBox is **16×16** pixels |

To add or update an icon, place the `.svg` file in `src\res\toolbars`. The build scripts (`!populate_build_dir.cmd` for local development and `Create-Sfx.ps1` for the installer) include them automatically.

### Execution logging

The execution logging system runs only in DEBUG builds. It records major application paths (startup, plugin loading, directory listing, file operations, and key UI features) through the Trace system. Logs are emitted as TRACE messages and appear in the Trace Server when it is connected. In release builds the logging calls are compiled out.

#### Running the Trace Server

Build the `tserver` project in the **Debug | Win32** configuration, then start `tserver.exe` before debugging Open Salamander. The standard debug output path is `src\vcxproj\tserver\tserver\Debug\tserver.exe`. From PowerShell:

```powershell
Start-Process C:\Projects\FileManager\src\vcxproj\tserver\tserver\Debug\tserver.exe
```

Leave the window titled **Trace Server** open while the application runs. To verify that it is still running:

```powershell
Get-Process tserver
```

Visual Studio can start both processes automatically: configure `tserver` and the main Open Salamander project as multiple startup projects, with `tserver` set to start without debugging. Start the Trace Server with the same Windows user and privilege level as the application being debugged.

If the debug shutdown dialog reports monitored handles that remain open, choose **Yes** only after the Trace Server is running. The application will connect and send the list of outstanding handles (including their allocation locations); it does not launch the Trace Server itself.

### Contributing

This project welcomes contributions that build and enhance Open Salamander.

Source files use UTF-8 with BOM and are formatted with `clang-format`. See `normalize.ps1` for details.

## Repository layout

| Path | Contents |
| --- | --- |
| `convert\` | Conversion tables for the Convert command |
| `doc\` | Documentation |
| `help\` | User-manual source files |
| `src\` | Open Salamander core source code |
| `src\common\` | Shared libraries |
| `src\common\dep\` | Shared third-party libraries |
| `src\lang\` | English resources |
| `src\plugins\` | Plugin source code |
| `src\reglib\` | Access to Windows Registry files |
| `src\res\` | Image resources |
| `src\salmon\` | Crash detecting and reporting |
| `src\salopen\` | Open-files helper |
| `src\salspawn\` | Process-spawning helper |
| `src\setup\` | Installer and uninstaller |
| `src\sfx7zip\` | Self-extractor based on 7-Zip |
| `src\shellext\` | Shell extension DLL |
| `src\translator\` | Translate Salamander UI to other languages |
| `src\tserver\` | Trace Server for info and error messages |
| `src\vcxproj\` | Visual Studio project files |
| `tools\` | Minor utilities |
| `translations\` | Translations into other languages |

A few Open Salamander 5.0 plugins are either not included or cannot be compiled. The PictView plugin used to be one of them, because its `pvw32cnv.dll` engine is not open-sourced; it now decodes and encodes through [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec). The Encrypt plugin is incompatible with modern SSD disks and has been deprecated. The UnRAR plugin lacks [unrar.dll](https://www.rarlab.com/rar_add.htm). The FTP plugin uses Windows SChannel for FTPS and does not require bundled TLS DLLs. Building the WinSCP plugin requires Embarcadero C++ Builder.

## Architecture and code structure

### Overview

Open Salamander is a pure WinAPI C++ application with no external UI frameworks (no MFC, ATL, or Qt). The architecture follows a layered design with a plugin-based extensibility model. The codebase targets Windows Vista+ (`WINVER=0x0601`) and supports both x86 and x64 builds.

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

### Source file organisation

The codebase is organised into focused file groups. Each group uses a common prefix so related files sort together:

| Prefix | Files | What it contains |
| --- | ---: | --- |
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

### Core components

#### Main window (`src/mainwnd_*.cpp`, `src/mainwnd.h`)

`CMainWindow` is the top-level window that hosts the entire application. Its implementation is split across seven focused files:

| File | Contents |
| --- | --- |
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

#### File panels (`src/fileswindow_*.cpp`, `src/filesbox_*.cpp`, `src/fileswnd.h`)

`CFilesWindow` implements a single file panel. Its implementation is split across twelve focused files:

| File | Contents |
| --- | --- |
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

#### File copy engine (`src/worker.h`, `src/worker.cpp`, `src/async_copy.cpp`, `src/operations_core.cpp`, `src/transfer_speed.cpp`)

Long-running operations (copy, move, delete, rename) execute on dedicated worker threads. The implementation is split across four files:

| File | Contents |
| --- | --- |
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

#### ZIP and archive API (`src/zip_*.cpp`, `src/zip.h`)

The plugin-facing archive API (`CSalamanderGeneral`, `CSalamanderDirectory`, etc.) is split into four files:

| File | Contents |
| --- | --- |
| `zip_progress.cpp` | `CZIPUnpackProgress` — progress dialog integration |
| `zip_general_api.cpp` | `CSalamanderGeneral` — the main archive API surface (~3 000 lines) |
| `zip_utilities.cpp` | `CSalamanderBMSearchDataImp`, `CSalamanderMD5Imp`, `CSalamanderPNG`, `CSalamanderCrypt` |
| `zip_directory.cpp` | `CSalamanderDirectory`, `CSalamanderForOperations`, `TestFreeSpace` |

#### Dialog boxes (`src/dialogs_*.cpp`, `src/dialogs.h`)

All dialogs are grouped by function:

| File | Contents |
| --- | --- |
| `dialogs_file_ops.cpp` | Copy, move, delete confirmation dialogs |
| `dialogs_rename.cpp` | Rename and batch-rename dialogs |
| `dialogs_attributes.cpp` | File-attribute dialogs, `CZipSizeResultsDialog`, `CPasswordDialog` |
| `dialogs_config_general.cpp` | Configuration pages: `CConfigPageGeneral`, `CConfigPageRegional`, `CConfigPageView` |
| `dialogs_config_viewers.cpp` | Configuration pages: Viewers, Associations |
| `dialogs_config_panels.cpp` | Configuration pages: Panels, Colors |
| `dialogs_config_environment.cpp` | Environment/paths configuration page |
| `dialogs_config_packing.cpp` | Packing/archiving configuration page |

#### Find dialog (`src/find_*.cpp`, `src/find.h`)

| File | Contents |
| --- | --- |
| `find_results.cpp` | `CFindOptions`, `CFoundFilesData`, `CFoundFilesListView` — data model and list control |
| `find_dialog_ui.cpp` | `CFindDialog` — search dialog UI and logic (~3 300 lines) |
| `finddlg2.cpp` | `CFindDialog` continuation methods, `CFindTBHeader` toolbar |

#### GUI controls (`src/gui_*.cpp`, `src/gui.h`)

Reusable custom controls shared across all dialogs and the main window:

| File | Contents |
| --- | --- |
| `gui_progressbar.cpp` | `CProgressBar` — animated, self-moving progress bar |
| `gui_statictext.cpp` | `CStaticText` — text control with ellipsis, path compaction, tooltips |
| `gui_controls.cpp` | `CButton`, `CColorArrowButton`, `CToolbarHeader`, `CAnimate`, layout helpers |

`CGuiBitmap` (a memory-DC helper for flicker-free button drawing) is declared in `gui_bitmap.h` and shared by `gui_progressbar.cpp` and `gui_controls.cpp`.

#### Plugin system (`src/plugins_*.cpp`, `src/plugins.h`, `src/plugins/`)

| File | Contents |
| --- | --- |
| `plugins_loading.cpp` | Discovery, `LoadLibraryUtf8`, version check, activation |
| `plugins_interface.cpp` | `CPluginInterfaceEncapsulation` — thread-safety guards |
| `plugins_archiver.cpp` | Archiver plugin adapter |
| `plugins_filesystem.cpp` | Virtual file-system plugin adapter |

Plugins implement abstract interface classes:

| Interface | Purpose |
| --- | --- |
| `CPluginInterfaceAbstract` | Base; version negotiation |
| `CPluginInterfaceForArchiverAbstract` | List/pack/unpack archives |
| `CPluginInterfaceForViewerAbstract` | File preview/viewer |
| `CPluginInterfaceForFileSystemAbstract` | Virtual file systems (e.g. FTP) |
| `CPluginInterfaceForThumbLoaderAbstract` | Thumbnail generation |

Every call is wrapped with `EnterPlugin()` / `LeavePlugin()` guards for thread safety and call-stack tracking.

#### Application entry (`src/app_entry.cpp`, `src/app_globals.cpp`)

| File | Contents |
| --- | --- |
| `app_globals.cpp` | All global variable definitions — runtime flags, GDI handles, colour tables, enabler arrays, `CMainWindowLock MainWindowCS` |
| `app_entry.cpp` | `MyEntryPoint`, `WinMain`, `WinMainBody`, locale init, graphics init, CRC-32 helpers |

#### Common library (`src/common/`)

Shared infrastructure used by both the core and all plugins:

| File | Purpose |
| --- | --- |
| `array.h` | `TIndirectArray<T>` — typed dynamic array template |
| `strutils.h` / `strutils.cpp` | UTF-8 ↔ wide string conversion, `CreateFileUtf8`, `DeleteFileUtf8`, etc. |
| `handles.h` / `handles.cpp` | Handle tracking and leak detection in debug builds |
| `messages.h` / `messages.cpp` | Typed message-box helpers |
| `allochan.h` / `allochan.cpp` | Allocation tracking wrappers |
| `heap.h` / `heap.cpp` | Custom heap management |
| `crc32.h` / `crc32.cpp` | CRC-32 checksum |
| `moore.h` / `moore.cpp` | Boyer-Moore string search |
| `multimon.cpp` | Multi-monitor layout support |

#### Crash reporting (`src/salmon/`)

`SalmonInit()` is called before `WinMain` via `MyEntryPoint()` in `app_entry.cpp`. It installs an unhandled-exception filter that captures a minidump and call-stack trace, enabling post-mortem analysis of field crashes.

### Key data structures

| Type | Description |
| --- | --- |
| `CFileData` | Metadata for one file or directory (name, size, time, attributes, plugin-specific data) |
| `CSalamanderDirectory` | Full directory listing exposed to plugins via the archive API |
| `CCriteriaData` | Parameters for a copy/move: masks, date range, size limits, speed cap |
| `CAsyncCopyParams` | Overlapped-I/O buffer block for the async copy path (declared in `worker.h`) |
| `CWorkerData` / `CProgressDlgData` | Per-operation worker state shared across the copy-engine translation units |
| `CDirectorySizeCache` | Cached directory-size results (avoids redundant recursive scans) |
| `CVisibleFileItemsArray` | Tracks which file items are currently visible in the panel for incremental refresh |
| `CChangeCaseData` | Options for a batch rename-case operation |
| `CAttrsData` | Parameters for changing file attributes in bulk |

### Window class hierarchy

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

### Naming conventions

The codebase follows these conventions for new and refactored code:

| Category | Convention | Example |
| --- | --- | --- |
| Dialog classes | `C*Dialog` | `CPasswordDialog`, `CZipSizeResultsDialog` |
| Window classes | `C*Window` | `CMainWindow`, `CFilesWindow` |
| Data-holder structs | `C*Data` or `C*Info` | `CFileData`, `CCriteriaData` |
| Manager / cache classes | `C*Manager` or `C*Cache` | `CDirectorySizeCache`, `CIconCache` |
| Locks / critical sections | `C*Lock` | `CMainWindowLock`, `CStringResourceLock` |

### Build system

The solution (`src/vcxproj/salamand.sln`) and CI target MSVC v145 (Visual Studio 2026). MSBuild property sheets layer the configuration:

- `sal_base.props` — common defines, include paths, warning level
- `sal_debug.props` / `sal_release.props` — optimization and debug flags
- `x86.props` / `x64.props` — platform-specific settings

The `OPENSAL_BUILD_DIR` environment variable controls where build artifacts are placed. `!populate_build_dir.cmd` stages executables, plugins, language files, and resources into a runnable layout.

Each plugin is its own `.vcxproj` linked into the solution and produces a DLL placed alongside the main executable.

### Diagnostics and debugging

- `CALL_STACK_MESSAGE*` macros maintain a lightweight call-stack log available in crash reports without requiring full debug symbols.
- `HANDLES_ENABLE` (debug-only) activates `handles.h` tracking that asserts on leaked or double-closed WinAPI handles.
- The Trace Server (`src/tserver/`) receives `TRACE_*` messages emitted throughout the codebase and displays them in a separate window; all trace calls are compiled out in release builds.

## Resources

- [Open Salamander website](https://www.opensalamander.org/)
- [GitHub releases](https://github.com/taskscape/FileManager/releases)
- [Automated testing](testing.md)
- [Reliability process](reliability.md)

## License

Open Salamander is open source software licensed [GPLv2](doc/license_gpl.txt) and later.
Individual [files and libraries](doc/third_party.txt) have a different, but compatible license.
