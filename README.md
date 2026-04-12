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

### Creating Installer

Open Salamander uses [Inno Setup](https://jrsoftware.org/isinfo.php) to create the installer. The installer script is located at `Installer\setup.iss`.

#### Building Locally

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

1. Builds the solution using MSBuild
2. Stages all required files (executables, plugins, language files, toolbars)
3. Installs Inno Setup via Chocolatey
4. Compiles the installer with build number versioning
5. Creates a GitHub release with the installer attached

The workflow is triggered on pushes to `main` and produces versioned installers named `OpenSalamander_5.0.{build_number}.exe`.

## Customization

### Icons
Open Salamander uses scalable SVG icons for its toolbars.

- **Location:** `src\res\toolbars`
- **Format:** Standard SVG
- **Dimensions:** The standard viewbox is **16x16 pixels**.
- **Process:** To add or update an icon, simply place the `.svg` file in the `src\res\toolbars` directory. The build scripts (`!populate_build_dir.cmd` for local dev and `Create-Sfx.ps1` for installer) will automatically include them.

### Execution Logging

The execution logging system runs only in DEBUG builds. It records major application execution paths (startup, plugin loading, directory listing, file operations, and key UI features) through the Trace system. The logs are emitted as TRACE messages, so they appear in the Trace Server when it is connected. In release builds, the logging calls are compiled out and produce no output.

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

A few Open Salamander 5.0 plugins are either not included or cannot be compiled. For instance, the PictView engine ```pvw32cnv.dll``` is not open-sourced, so we should consider switching to [WIC](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-about-windows-imaging-codec) or another library. The Encrypt plugin is incompatible with modern SSD disks and has been deprecated. The UnRAR plugin lacks [unrar.dll](https://www.rarlab.com/rar_add.htm), and the FTP plugin is missing [OpenSSL](https://www.openssl.org/) libraries. Both issues are solvable as both projects are open source. To build WinSCP plugin you need Embarcadero C++ Builder.

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

### Core Components

#### Main Window (`src/mainwnd*.cpp`, `src/mainwnd.h`)
`CMainWindow` is the top-level window that hosts the entire application. It contains:
- Two `CFilesWindow` instances (left and right panels)
- Drive bars, toolbars (`CMainToolBar`, `CPluginsBar`, `CBottomToolBar`, `CUserMenuBar`, `CHotPathsBar`)
- Menu system (`CMenuBar`, `CMenuPopup`, `CMenuNew`)
- Status window (`CStatusWindow`) and tooltip window (`CToolTipWindow`)

#### File Panels (`src/fileswn*.cpp`, `src/filesbx*.cpp`)
`CFilesWindow` implements a single file panel. Key sub-components:
- `CFilesBox` — the virtual list control that renders file entries
- `CHeaderLine` — column headers with drag-to-resize
- `CPathHistory` — forward/back navigation history
- `CIconCache` — icon lookup cache to avoid redundant shell requests
- `CFilesArray` / `CFileData` — in-memory directory listing

Each panel operates in one of three modes driven by the path type: `ptDisk` (local/network drive), `ptZIPArchive` (archive root), or `ptPluginFS` (virtual file system provided by a plugin).

#### File Operations (`src/worker.h`, `src/execute.cpp`)
Long-running operations (copy, move, delete, rename) execute on dedicated worker threads so the UI remains responsive. The operation pipeline is:

1. User triggers a command → `CCriteriaData` is built with masks, attributes, and speed limits.
2. A `COperations` object is created and a worker thread is started.
3. The worker processes each file, updates a `CProgressData` structure, and communicates back via Windows messages.
4. On completion or cancellation, both source and target panels are refreshed.

`CTransferSpeedMeter` and `CProgressSpeedMeter` measure throughput and estimate time remaining in real time.

#### Plugin System (`src/plugins.h`, `src/plugins/`)
Plugins are DLLs that export a well-known entry point. The core defines abstract interface classes that plugins implement:

| Interface | Purpose |
|-----------|---------|
| `CPluginInterfaceAbstract` | Base interface; version negotiation |
| `CPluginInterfaceForArchiverAbstract` | List/pack/unpack archives |
| `CPluginInterfaceForViewerAbstract` | File preview/viewer |
| `CPluginInterfaceForFileSystemAbstract` | Virtual file systems (e.g. FTP) |
| `CPluginInterfaceForThumbLoaderAbstract` | Thumbnail generation |

`CPluginInterfaceEncapsulation` wraps every plugin call with `EnterPlugin()` / `LeavePlugin()` guards for thread safety and call-stack tracking. Plugins are discovered from their subdirectories at startup, loaded with `LoadLibraryUtf8()`, and version-checked before activation.

#### Common Library (`src/common/`)
Shared infrastructure used by both the core and plugins:

| File | Purpose |
|------|---------|
| `array.h` | `TIndirectArray<T>` — typed dynamic array template (~4900 lines) |
| `handles.h/cpp` | Handle tracking and leak detection in debug builds |
| `messages.h/cpp` | Typed message box helpers |
| `allochan.h/cpp` | Allocation tracking wrappers |
| `heap.h/cpp` | Custom heap management |
| `crc32.h/cpp` | CRC-32 checksum |
| `moore.h/cpp` | Boyer-Moore string search |
| `multimon.cpp` | Multi-monitor layout support |

#### Crash Reporting (`src/salmon/`)
`SalmonInit()` is called before `WinMain` via `MyEntryPoint()` in `salamdr1.cpp`. It installs an unhandled-exception filter that captures a minidump and call-stack trace, enabling post-mortem analysis of field crashes.

### Key Data Structures

| Type | Description |
|------|-------------|
| `CFileData` | Metadata for one file or directory (name, size, time, attributes, plugin-specific data) |
| `CSalamanderDirectory` | Holds the full listing for a directory |
| `CCriteriaData` | Parameters for a copy/move operation: masks, date range, size limits, speed cap |
| `CChangeCaseData` | Options for a batch rename-case operation |
| `CAttrsData` | Parameters for changing file attributes in bulk |

### Window Class Hierarchy

All UI objects derive from a thin `CWindow` base that wraps a WinAPI `HWND` and routes messages through a virtual `WindowProc()`. There are no MFC `CWnd` semantics; message handling is done with explicit `WM_*` comparisons.

```
CWindow
├── CMainWindow
│   ├── CFilesWindow (×2, left/right panels)
│   │   ├── CFilesBox
│   │   └── CHeaderLine
│   ├── CMenuBar
│   ├── CMainToolBar / CPluginsBar / ...
│   ├── CDriveBar
│   └── CStatusWindow
└── CDialog (modal/modeless dialogs)
```

### Build System

The solution (`src/vcxproj/salamand.sln`) targets MSVC v143 (VS 2022). MSBuild property sheets layer the configuration:

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
