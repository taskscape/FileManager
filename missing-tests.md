# Missing automated UI tests for the main menu and toolbars

Status: source audit of commit `ab579e5` on 2026-09-12.

This document identifies user-facing commands on the main menu and main toolbars that still have no behavioral UI test. It is deliberately limited to functionality that is common, deterministic, and reasonably easy to automate within the current NUnit/FlaUI test framework.

The audit compares the command surface in [`src/menu_templates.cpp`](src/menu_templates.cpp) and [`src/toolbar_button_defs.cpp`](src/toolbar_button_defs.cpp) with the tests and command adapters under [`tests/FileManager.UiTests`](tests/FileManager.UiTests). A command is considered covered only when a test launches Open Salamander, invokes that behavior, and verifies a user-observable result. Source-contract tests, icon rendering tests, and merely checking that a toolbar command is enabled do not count as behavioral coverage.

An older inventory exists as `missing_tests.md`. It predates several current fixtures. The following scenarios from that inventory are now implemented and therefore are **not** gaps: entering/ascending folders, swapping and refreshing panels, Change Directory, mask selection, a multi-command session, copy-then-View/Edit, Pack/Unpack, ZIP copy-out, Find focus/content/duplicates, filtering, Compare Directories, Change Attributes, Change Case, text conversion, copy masks, NTFS compression, and real default-middle-toolbar button routing for Copy, Swap Panels, Find, Create Directory, and View.

## Current coverage at a glance

| Main surface | Current behavioral coverage | Important remaining gap |
| --- | --- | --- |
| Left / Right | Enter and parent navigation, Change Directory, Refresh, Filter, Swap Panels | Back/Forward history, synchronize to the other panel, sorting/view modes, panel zoom |
| Files | Rename, View, Edit, Copy, Move, Delete, attributes, case, conversion, Pack/Unpack | Edit New, Properties, modifying an archive in-place |
| Edit | Select by mask, Select All as part of another test, Unselect All | Clipboard Cut/Copy/Paste and most selection-management commands |
| Commands | Create Directory, Change Directory, Compare Directories, Find | Calculate Occupied Space, Calculate Directory Sizes, Make File List, Drive Information |
| Plugins | FTP connection/bookmark/download behavior through the FTP plug-in | Plugins Manager smoke and deployed-plug-in inventory |
| Options | Configuration commit/cancel/persistence and top-toolbar icon size | Toolbar visibility/layout persistence and hot paths |
| Help | Search when a configured CHM is available | About and locally available Contents/Keyboard entry points |
| Toolbar activation | Representative default-middle-toolbar buttons are physically clicked and their outcomes verified | Broader configurable-toolbar/layout coverage remains open |

## Recommended tests

Priority meanings:

- **P0** — common daily workflow or a blind spot across many already-tested commands.
- **P1** — prominent main-menu/toolbar functionality with a small deterministic scenario.
- **P2** — useful read-only smoke coverage; implement after the state-changing workflows.

Effort assumes reuse of `FileOperationUiTestBase`, `NativeCommands`, and the existing sandbox.

| Priority | Proposed fixture / scenario | Effort | Default UI lane |
| --- | --- | --- | --- |
| Done (2026-09-12) | `MainToolbarRoutingUiTests` | Medium | Yes |
| Done (2026-09-12) | `PanelHistory_and_other_panel_path_commands_navigate_to_the_expected_owned_folder` | Small | Yes |
| P0 | `Clipboard_copy_and_cut_paste_between_panels_preserve_the_expected_source` | Small | Yes, on the required isolated interactive profile |
| P0 | `SelectionCommandsUiTests` | Small | Yes |
| P1 | `Sort_view_and_zoom_commands_change_the_active_panel_state` | Medium | Yes |
| P1 | `Edit_new_creates_and_opens_a_file_in_the_sandbox_editor` | Small | Yes |
| P1 | `Occupied_space_and_file_list_report_the_seeded_tree` | Small | Yes |
| P1 | `Copy_into_and_delete_from_an_open_ZIP_updates_only_the_archive` | Medium | Capability-gated by the existing ZIP deployment flag |
| P1 | `Hot_path_navigates_and_persists_after_restart` | Medium | Yes |
| P1 | `Visible_toolbars_can_be_toggled_and_the_layout_persists` | Small | Yes |
| P2 | `Drive_information_and_properties_open_for_the_sandbox_volume_and_file` | Small | Yes |
| P2 | `Plugins_manager_and_about_dialogs_open_with_expected read-only data` | Small | Yes |

### 1. Real toolbar-button routing — Implemented (2026-09-12)

**Implemented coverage**

`MainToolbarRoutingUiTests.Default_middle_toolbar_buttons_route_to_their_user_observable_commands` now physically clicks default-middle-toolbar buttons. The product uses its custom `WinLib Universal Window2` control rather than `ToolbarWindow32`; `NativeCommands.TryInvokeToolbarCommand` identifies the narrow vertical bar, derives each square icon cell from the source-defined default layout, and queues mouse down/up at its center. The test fails distinctly when the bar is absent, a documented command is absent, its client rectangle is unusable, or it cannot receive its click.

The implemented representative set covers the default middle toolbar's Copy, Swap Panels, Find, Create Directory, and View routes. Other configurable top-toolbar/layout scenarios remain separate gaps.

**Implemented test type**

One parameterized native UI integration fixture, plus a lightweight source-contract assertion that the selected smoke commands remain in the documented default toolbar strings.

**Automated scenarios**

Use a small representative set rather than clicking every destructive button:

1. Click **Copy** on `copy-file.txt`, commit to the other panel, and assert the exact destination bytes while preserving the source.
2. Click **Swap Panels** and assert the active title/path changes as in `PanelNavigationUiTests`.
3. Click **Find** and assert the Find results control (`2510`) appears; close it.
4. Click **Create Directory**, enter an owned relative name, accept, and assert the directory exists.
5. Click **View** on `view-file.txt`, assert the internal viewer class and filename, and close it.

Together these cover no-dialog, modal-dialog, modeless-dialog, file-operation, and secondary-window command routes.

**Implementation in the current framework**

- Added `NativeCommands.TryInvokeToolbarCommand(window, command)` beside `TryGetToolbarCommandEnabled`.
- Locate the custom narrow vertical `WinLib Universal Window2` middle toolbar, resolve the documented default-layout index, derive its square icon-cell rectangle without including unused lower area, and send mouse down/up at the center. Use a screen-coordinate mouse click only if the toolbar implementation ignores client mouse messages.
- Reuse the existing `FileOperationUiTestBase` selection, dialog, filesystem-wait, and viewer-window helpers. Do not locate buttons by translated tooltip text.
- Keep the direct-`WM_COMMAND` tests: they remain valuable command-handler tests. The new fixture is a small routing layer, not a rewrite of the whole suite.
- Every new interop constant/helper needs the nearby intent comment required by `AGENTS.md`.

**Verified behavior**

Each click must produce the same observable result as its existing direct-command counterpart, and the test must fail distinctly when the documented button is absent, its geometry is unusable, or it cannot be clicked.

### 2. Back/Forward history and “path from other panel” — Implemented (2026-09-12)

**Implemented coverage**

`PanelHistoryUiTests.PanelHistory_and_other_panel_path_commands_navigate_to_the_expected_owned_folder` exercises the prominent Back, Forward, and **Go to Path from Other Panel** commands (`CM_ACTIVEBACK`, `CM_ACTIVEFORWARD`, and `CM_ACTIVE_AS_OTHER`; the left/right-specific menu variants share the same behavior) in one stateful end-to-end navigation test derived from `FileOperationUiTestBase`.

**Automated scenario**

1. Seed `history-a\a.txt` and `history-b\b.txt` below the source workspace.
2. Enter `history-a`, then use Change Directory to visit `history-b` and establish the adjacent real product-history transition.
3. Dispatch Back (`832`) and assert the main-window title contains `history-a`; copy `a.txt` to the target to prove the active directory is functional.
4. Dispatch Forward (`833`) and assert the title contains `history-b`; copy `b.txt`.
5. Change the right panel to a distinct owned directory, activate the left panel, dispatch `CM_ACTIVE_AS_OTHER` (`847`), and assert the left path now matches the right. Copy a right-only fixture back to prove the synchronized path is used.

**Implementation in the current framework**

Add commented constants to `NativeCommands`, then reuse `OpenFocusedItem`, `WaitForMainWindowTitleContaining`, `ActivateSourcePanel`, `ActivateTargetPanel`, and `ExecuteWithPath`. Verify navigation with both title/path and filesystem outcomes; a title-only assertion can pass while the panel is still refreshing.

**Pass criteria**

Back and Forward visit the expected owned folders in order, and path synchronization changes only the requested panel without losing the other panel’s current path.

### 3. Clipboard Copy/Cut/Paste between panels

**Missing functionality**

The Files menu Copy/Move engine is well covered, but the separate Edit menu and toolbar clipboard routes (`CM_CLIPCOPY`, `CM_CLIPCUT`, and `CM_CLIPPASTE`) have no test. These use Windows shell clipboard semantics and different product code paths from F5/F6.

**Recommended test type**

A two-scenario native UI integration fixture. It should run only in the same isolated interactive Windows profile already required for UI tests, because the clipboard is desktop-global.

**Automated scenarios**

- **Copy/Paste:** select `clipboard-copy.txt` on the left, dispatch Copy (`773`), activate the right panel, dispatch Paste (`775`), wait for release, and assert identical bytes exist on both sides.
- **Cut/Paste:** select `clipboard-cut.txt`, dispatch Cut (`774`), activate the right panel, dispatch Paste, and assert the target exists and the source is removed only after the paste completes.
- Optional follow-up: exercise Copy Full Name/Copy Name/Copy Full Path (`709`-`711`) and read `CF_UNICODETEXT`, asserting the exact sandbox name/path.

**Implementation in the current framework**

- Derive from `FileOperationUiTestBase`; use product commands for all clipboard operations and filesystem assertions for completion.
- Add a bounded wait for a shell copy progress window and reuse `AnswerQuestionIfPrompted` for collisions. Do not use a fixed sleep as the completion oracle.
- Ensure the test profile is disposable and interactive. Do not run this fixture when `FILEMANAGER_UI_ISOLATED` is absent, and do not attempt to restore arbitrary clipboard formats belonging to a real user.
- If release runners cannot guarantee exclusive clipboard ownership, tag this fixture `Clipboard` and route it through a separate explicit interactive job rather than silently skipping it in the complete gate.

**Pass criteria**

Copy preserves the source, Cut removes it only after successful Paste, and target bytes match exactly.

### 4. Selection management beyond masks

**Missing functionality**

The suite covers Select by mask, an internal Select All call, and Unselect All. It does not cover the remaining Edit menu/toolbar selection model:

- Select/Unselect by focused extension or name (`788`-`791`).
- Invert Selection (`845`).
- Reselect after an operation (`865`).
- Store/Restore Selection (`866`/`867`).
- Hide Selected, Hide Unselected, and Show All (`2945`-`2947`).
- Go to Previous/Next Selected (`2938`/`2939`).

These commands share state and are especially suited to one-session characterization.

**Recommended test type**

A compact `SelectionCommandsUiTests` fixture with three behavioral tests.

**Automated scenarios**

1. Seed `same-a.txt`, `same-b.txt`, and `same-c.dat`; focus a `.txt`, select by extension, copy, and assert only the `.txt` files arrived. Repeat select/unselect by focused name with same stems in different directories if name matching is intended to include extensions.
2. Select two files, Store Selection, clear/invert it, Restore Selection, and copy. The exact target names prove the restored set, avoiding fragile accessibility inspection.
3. Select one of three fixtures, Hide Selected, assert the panel item count decreases, then Show All and assert it returns. For Previous/Next Selected, move the focus, clear the selection while retaining focus, copy the focused item, and assert which file was chosen.

**Implementation in the current framework**

Add the stable IDs to `NativeCommands`. Reuse `SelectSourceItems`, `ClearActiveSelection`, `SelectAllActivePanel`, and filesystem-result assertions. Add a native/accessibility item-count helper for `SalamanderItemsBox` if UIA does not expose a reliable child count; do not assert localized status-line text.

**Pass criteria**

Each command selects, restores, hides, or focuses exactly the expected names, and no unselected fixture is copied or deleted.

### 5. Sorting, view mode, and panel zoom

**Missing functionality**

No UI test covers the Left/Right **Sort By** commands, view-mode selection, Smart Column Mode, or Zoom Panel. These are primary browsing controls and are represented on the configurable top and panel toolbars.

**Recommended test type**

A deterministic presentation-and-behavior fixture. Prefer behavioral inference over screenshots.

**Automated scenarios**

- Seed files whose alphabetical, size, and timestamp orders differ. Dispatch Sort by Name/Size/Time (`700`, `703`, `702` for the active panel), press Home in `SalamanderItemsBox`, clear selection, and copy the focused item. The copied filename proves the first row and therefore the sort mode.
- Switch between the fresh profile’s Brief and Detailed view commands and assert the detailed header window appears/disappears and the file panel remains usable.
- Capture both panel rectangles, dispatch active-panel Zoom (`2934`), assert the active panel occupies nearly the full working width and the other panel is hidden/collapsed, dispatch it again, and assert both panels return.
- Toggle Smart Column Mode and verify its check state through the toolbar/menu state or persisted configuration after restart.

**Implementation in the current framework**

Extend `NativeCommands` with a commented `PressHome` helper and, if necessary, `TB_GETSTATE`/menu-state accessors. Use distinct sizes and timestamps with generous separation so filesystem timestamp resolution cannot make ordering ambiguous. Geometry assertions should use relative widths, not pixel-perfect coordinates or screenshots.

**Pass criteria**

Each sort chooses the expected first file, view changes preserve navigation/selection, and zoom round-trips without changing either panel path.

### 6. Edit New with the harness-owned editor

**Missing functionality**

Files > Edit New (`CM_EDITNEW`, Shift+F4) is separate from Edit. The current Edit tests prove that an existing file opens in `SandboxEditor`; they do not prove the new-file dialog creates the requested path and launches the configured editor.

**Recommended test type**

A small end-to-end test added to `FileAccessUiTests`.

**Automated scenario**

Configure `SandboxEditor` using the existing `ConfigurationDialogPages.RewriteSelectedEditor` flow, restart, dispatch Edit New (`744`), enter `created-by-edit-new.txt` in the standard operation-path control, accept, and wait for `SandboxEditor - created-by-edit-new.txt`. Close only that owned stub window, refresh, and assert the new file exists in the source directory and can be viewed or copied.

**Implementation in the current framework**

Reuse the exact editor setup and owned-window matching from `Edit_file_opens_the_selected_file_in_the_configured_editor`, and reuse `WaitForOperationDialog`, `SetDialogPath`, and `CloseDialog`. Do not invoke Notepad or another machine association.

**Pass criteria**

The requested file is created under the owned source directory, the stub receives exactly that path, and closing it returns a usable main window.

### 7. Calculate Occupied Space, Calculate Directory Sizes, Make File List, and Drive Information

**Missing functionality**

These prominent Commands menu/toolbar operations have no behavioral tests:

- Calculate Occupied Space (`CM_OCCUPIEDSPACE`, `739`).
- Calculate Directory Sizes (`CM_CALCDIRSIZES`, `813`).
- Make File List (`CM_FILELIST`, `811`).
- Drive Information (`CM_DRIVEINFO`, `752`).

They are read-only and therefore unusually safe UI-test candidates.

**Recommended test type**

Two focused integration tests plus one read-only dialog smoke test.

**Automated scenarios**

1. Select a seeded directory containing two files with known byte lengths. Open Calculate Occupied Space and assert controls `IDS_FILESCOUNT` (`265`), `IDS_DIRSCOUNT` (`266`), and `IDS_SIZE` (`261`) report the expected counts and a non-zero size. Close with IDOK.
2. Select a small tree, open Make File List, choose **File** (`2390`), set the output path through `IDC_FL_FILENAME` (`2384`), accept, and assert the generated file contains each selected relative name exactly once. Keep the output below the workspace target.
3. Open Drive Information and assert the mount point (`571`) equals the sandbox drive root and the filesystem name (`561`) agrees with `DriveInfo.DriveFormat`; close without changing the volume label.
4. For Calculate Directory Sizes, run it on a seeded folder, wait for completion, and prove the panel remains responsive. If the size column can be queried reliably, assert the known total; otherwise combine it with Sort by Size and infer the result from row order.

**Implementation in the current framework**

Use `WaitForDialogWithControl` and the existing native get/set helpers. Assert stable control IDs and filesystem output rather than English captions or formatted localized size strings. Use `FileInfo.Length` for expected logical bytes; do not assert occupied clusters exactly because compression and cluster size vary by runner.

**Pass criteria**

Counts/names match the seeded tree, generated output remains inside the sandbox, and no command changes source contents.

### 8. Modify an archive through ordinary panel commands

**Missing functionality**

Current archive tests create archives with Pack, extract with Unpack or Copy, navigate into a ZIP, and delete the archive file itself. They never modify an open archive: copying a disk file into an archive or deleting a member from it. Those are ordinary Copy/Delete toolbar operations but use a materially different plug-in transaction path.

**Recommended test type**

A ZIP-capability-gated extension to `ArchiveOperationUiTests`.

**Automated scenarios**

- Seed a ZIP with `keep.txt` and `remove.txt`. Open it in the target panel, activate the disk panel, copy `add.txt` to the archive panel, close/leave the archive, and inspect it with `System.IO.Compression` to assert `add.txt`, `keep.txt`, and `remove.txt` exist with correct bytes.
- Reopen the ZIP, select `remove.txt`, dispatch Delete, confirm, leave the archive, and assert only `remove.txt` is gone. Verify `keep.txt` and `add.txt` are unchanged.

**Implementation in the current framework**

Reuse `UiTestSettings.RequireZipPlugin`, `OpenFocusedItem`, panel activation, `WaitForCommandEnabled`, and existing prompt helpers. Wait until the archive handle is released before inspecting it. Never repair or rewrite the ZIP from the test process; `System.IO.Compression` is only the independent oracle.

**Pass criteria**

Add/delete updates only the requested members, preserves unrelated entries and contents, and leaves Open Salamander usable.

### 9. Hot paths: assign, navigate, and persist

**Missing functionality**

Hot Paths are exposed through the Go submenu and toolbar, and their icon is covered by a rendering contract, but no UI test assigns or invokes one. The related Customize Hot Paths configuration flow is also untested.

**Recommended test type**

A single persistence test derived from `FileOperationUiTestBase`.

**Automated scenario**

Open Customize Hot Paths (`CM_CUSTOMIZE_HOTPATHS`, `925`, or the toolbar customization route), select the first slot, set it to `Workspace.SourcePath("hot-target")`, accept, invoke that slot from the Go/Hot Paths dropdown, and verify a unique file in that directory can be copied. Restart and invoke the same slot again to prove persistence.

**Implementation in the current framework**

Extend `ConfigurationDialogPages` to select the page by stable child controls, following its existing depth-first property-sheet traversal. Use native control IDs and command IDs for the slot; avoid translated tree labels and menu captions. Commit through the product so the configuration generation checksum remains valid, then wait for the active generation before restarting.

**Pass criteria**

The hot path navigates to the exact owned folder before and after restart, and no path outside `filemanager-testdata` is stored.

### 10. Toolbar visibility and persisted layout

**Missing functionality**

`ToolbarIconSizeUiTests` verifies Small/Medium/Large icon-size persistence but not the Options > Visible Toolbars commands. Top, middle, drive, user-menu, hot-path, edit-line, bottom, and plug-in bars can be toggled, and their persisted presence is central to the main window layout.

**Recommended test type**

A parameterized UI persistence test for a safe representative subset: top toolbar (`801`), middle toolbar (`860`), edit line (`746`), and bottom toolbar (`802`).

**Automated scenario**

For each surface, record the relevant native child handle/count and main panel rectangle, toggle the command off, wait until the child disappears and panels relayout, toggle it on, and assert it returns. Then leave one surface off, wait for the debounced configuration save, restart, assert the state persisted, restore it, and commit again.

**Implementation in the current framework**

Locate native children by class/control identity, not translated labels. Follow the restart/persistence pattern in `ToolbarIconSizeUiTests`; use condition-based waits for handle creation/destruction and reserve fixed sleeps only for the known 250 ms configuration debounce.

**Pass criteria**

Every toggled surface disappears and reappears without overlapping the panels, and the chosen state survives one restart.

### 11. File Properties and other read-only information dialogs

**Missing functionality**

Files > Properties (`CM_PROPERTIES`, `812`) is a high-frequency action with no test. Drive Information is also absent as described above. Security Permissions and NTFS Encrypt/Decrypt are not included here because they are privilege- and policy-dependent.

**Recommended test type**

A read-only smoke test for the shell property sheet, preferably in the normal isolated UI lane.

**Automated scenario**

Select an owned `properties.txt`, dispatch Properties, identify the new top-level property sheet by process/owner and its standard tab control, assert the sheet refers to the selected filename, then cancel it. Repeat for an owned directory if the same locator remains stable.

**Implementation in the current framework**

Use `WaitForDesktopWindow` with the current FileManager process/owner rather than matching a localized `"Properties"` caption. Close with Cancel and assert file length, timestamps, and attributes are unchanged. Never click Security/Advanced or mutate ACLs.

**Pass criteria**

The correct owned item’s property sheet opens and closes, with no filesystem metadata change and no leaked shell window.

### 12. Plugins Manager and About dialog smoke tests

**Missing functionality**

The suite exercises the FTP plug-in but never opens Plugins > Plugins Manager (`CM_PLUGINS`, `815`) or Help > About (`CM_HELP_ABOUT`, `2216`). A broken manager dialog, empty deployed plug-in inventory, or incorrect About version can therefore escape UI coverage.

**Recommended test type**

Read-only UI smoke tests.

**Automated scenarios**

- Open Plugins Manager, locate its stable list control from `IDD_PLUGINS`, assert at least the core deployed plug-ins required by the selected runtime are present, and Cancel. The exact required list should be derived from the staged runtime/installer manifest, not hard-coded from a developer machine.
- Open About, assert its version control identifies major version 6 and that the Close button works. This complements, rather than replaces, `ApplicationVersionContractTests`.
- If the deployed CHM exists, add Contents and Keyboard Shortcuts entry-point smoke tests using the same capability gate as `ReportedDefectCharacterizationUiTests`; do not duplicate the existing Search-result test.

**Implementation in the current framework**

Use command and resource IDs and match only owned process windows. Keep the tests read-only: do not add/remove/reorder plug-ins. A missing expected plug-in in a runner-selected complete runtime should fail as an artifact-staging error; an explicitly minimal runtime should use a documented capability gate.

**Pass criteria**

Both dialogs open, expose their expected stable controls/data, close cleanly, and leave the main window enabled.

## Lower-value or non-deterministic menu commands

The following items are missing too, but they should not be the first additions because they touch external applications, machine configuration, credentials, privileges, or volatile shell namespaces:

- Email Files, Command Shell, Open Active Folder in Explorer, and the special-folder shortcuts.
- Connect/Disconnect Network Drive, Shared Directories, and external network discovery.
- NTFS Encrypt/Decrypt and Security Permissions.
- Import/Export Configuration, which needs careful file-dialog isolation and full-profile rollback.
- View With/Edit With and normal document Open, which depend on machine/user associations unless another owned stub is added.
- Archivers Autoconfiguration, which scans machine drives for third-party executables.
- Download Update, which depends on update-server state and would open an external URL.
- Shell context menus and drag/drop, which are useful but substantially more brittle than stable command-ID scenarios.

When these are eventually tested, use an explicit capability category or a harness-owned stub/loopback service. They must not silently interact with the developer’s real editor, mail client, Explorer windows, clipboard, network shares, certificates, or configuration.

## Framework and runner rules for every implementation

1. Put behavioral cases in `tests/FileManager.UiTests`, deriving file/panel workflows from `FileOperationUiTestBase`. It already supplies GUID workspaces, `-l`/`-r` launch arguments, panel discovery, selection, dialog waits, operation-journal cleanup, and teardown of owned processes.
2. Keep `[Category("UI")]` through the base class or add it explicitly when deriving directly from `FileManagerUiTestBase`. Preserve `[NonParallelizable]`; all cases share an interactive desktop and isolated HKCU profile.
3. Add stable numeric commands and dialog controls to `Infrastructure/NativeCommands.cs`, each with a concise reason/invariant comment. Never locate owner-drawn main-menu entries or native dialogs by translated captions when a command/resource ID exists.
4. Invoke the real product UI path. Direct `WM_COMMAND` is consistent for command-handler scenarios; physical toolbar routing tests should additionally click a toolbar hit rectangle. Do not call product implementation methods from test code.
5. Assert user-observable results: exact files/bytes, selected paths, owned child windows, stable control values, or restart persistence. “No crash” and “dialog appeared” are sufficient only for explicitly named smoke tests.
6. Use bounded condition waits (`WaitForFileSystem`, `WaitForWindow`, `WaitForWindowToClose`, and handle-release checks). Avoid unconditional sleeps except for the documented configuration debounce or a short native idle-settle step already required by this legacy UI.
7. Keep every created or modified path below `FILEMANAGER_UI_TESTDATA_ROOT`, keep configuration under `Software\Open Salamander\6.0-filemanager-testdata`, and close/kill only processes launched by the fixture. Do not empty the Recycle Bin or modify machine-wide associations.
8. Capability-dependent cases must use explicit, actionable skip messages that are admitted by `scripts/runtests.ps1`; do not turn a missing required runtime payload into an ordinary pass.
9. Add a focused source-contract assertion when a new helper depends on default toolbar composition or a stable resource identifier. Such a contract supplements the UI test; it does not replace it.
10. After implementation, run the focused test first, then execute the authoritative parity command under the matching pipeline conditions:

    ```powershell
    .\scripts\runtests.ps1
    ```

    Report the full result, including skips and unavailable prerequisites. If workflow filtering, categories, prerequisites, or skip policy change, update `.github/workflows` and `scripts/runtests.ps1` together as required by `AGENTS.md`.

## Suggested implementation order

1. Toolbar click-through helper and five-command routing smoke.
2. Back/Forward/path synchronization and clipboard Copy/Cut/Paste.
3. Selection management and Edit New.
4. Occupied Space, Make File List, Drive Information, and Properties.
5. Sort/view/zoom and toolbar-visibility persistence.
6. ZIP in-place modification and Hot Paths persistence.
7. Plugins Manager/About read-only smoke.

This order closes the largest everyday-workflow blind spots first while requiring no product change, no public network, and no data outside the existing disposable UI-test boundaries.
