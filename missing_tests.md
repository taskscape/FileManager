# Missing end-user tests

This document lists automated UI tests that would be useful from an **end-user** point of view and are **not** covered by the suite described in [testing.md](testing.md). It is a gap analysis, not a runner inventory.

Sources: user-manual topics under `help/src/hh/salamand/`, plug-in help, [architecture.md](architecture.md) (disk / archive / plug-in filesystem panel modes), [README.md](README.md), [tests/FileManager.UiTests/README.md](tests/FileManager.UiTests/README.md), and the native command surface (`src/menu_templates.cpp`, `src/toolbar_button_defs.cpp`).

## How to read this list

Each proposed test names:

- **User intent** — what a person sitting at the two-panel UI is trying to do.
- **Why it is missing** — what today’s cases stop short of proving.
- **Would confirm** — the observable outcome that should be green.
- **Suggested chain** — a sequence of commands, not a single isolated click, when that is how people actually work.
- **Harness notes** — sandbox, English/resource IDs, and product limits already recorded in `testing.md` (ANSI dialogs, `PATH_MAX_PATH`, Recycle Bin never emptied).

Priority:

| Rank | Meaning |
| --- | --- |
| **P1** | Daily two-panel file-manager work; a regression would be noticed immediately. |
| **P2** | Common next step after a P1 action (archives, Find results, clipboard, filter). |
| **P3** | Valuable but narrower (NTFS extras, split/combine, viewers, most plug-ins). |

Items already characterized (copy/move/rename/delete variants, Find-once, View-open, Edit-open, ZIP-open, Help Search, FTP bookmark persist, one live FTPS download, loopback FTP greeting, reparse copy/delete, journal resume, toolbar icon size, configuration persist) are **not** repeated as gaps. Follow-on steps those cases never take **are** listed.

Do not treat a missing test as a product defect. Several P3 items are hard to automate safely (shell, Explorer, live network, user-installed associations).

## Already covered (do not re-propose)

From the native UI catalog in `testing.md`:

- Cold start, Configuration cancel/commit/persist/restart, FTP bookmark create/rename persist.
- Create directory, copy/move/rename/delete (including overwrite/skip, mixed selection, locked-file skip, recycle-bin item count, large flat delete).
- Same-volume vs cross-volume move, ADS copy/overwrite/retry, ADS-loss decline (quarantined), reparse non-traversal.
- Find returning one nested name; internal viewer window class; Edit via harness-owned `SandboxEditor`.
- ZIP navigation after the information dialog (when the Zip plug-in is enabled); Help Search with configured CHM terms.
- Loopback FTP quick-connect past a fragmented greeting; live MojeRzeczy download of `/skan.txt` (explicit, not CI).
- Startup under Application Verifier; toolbar Small/Medium/Large persist; leak soak (quarantined); configuration crash recovery.

Those cases almost always stop at **one command and a filesystem assertion**. End users typically **navigate, select, operate, then use the result**.

---

## P1 — Dual-panel navigation and everyday command chains

### Enter a subdirectory, operate, then go to parent

- **User intent:** Browse a tree with Enter / Backspace (or Ctrl+Page Down / Ctrl+Page Up) rather than only launching with `-l`/`-r` already on the leaf.
- **Why it is missing:** Every file-operation case starts the panels on the workspace roots. `Change Directory` (`help/.../navigating_changedir.htm`) is never driven.
- **Would confirm:** After entering `copy-tree`, the directory line and listing show `nested`; after going to parent, the listing shows `copy-tree` again; a subsequent copy still uses the active panel’s current path.
- **Suggested chain:** Start on source root → Enter `copy-tree` → copy `payload.txt` to the other panel → parent directory → listing still valid.
- **Harness notes:** Directory line text and/or native path query; owner-drawn list after `ReadDirectory`.

### Swap panels, then copy in the opposite direction

- **User intent:** Commands → Swap Panels, then copy the former target tree back.
- **Why it is missing:** Tests always treat left as source (`-p 1`) and never swap.
- **Would confirm:** After swap, Copy still writes into the inactive panel’s current directory; content matches; original source is unchanged.
- **Suggested chain:** Copy file left→right → Swap → copy a different file (now left was the old right) → both destinations exist.

### Refresh after an external change

- **User intent:** Another process (or the harness) creates a file in the current directory; the user presses Refresh and can select it.
- **Why it is missing:** Workspaces are seeded **before** launch so the first enumeration already contains fixtures. `CM_ACTIVEREFRESH` exists in the harness but is not asserted as the user-visible recovery from a stale listing.
- **Would confirm:** A file written to `Workspace.SourceDirectory` after startup is absent until Refresh, then selectable and copyable.
- **Suggested chain:** Write file on disk → Refresh → quick-search → copy to other panel.

### Select All / Unselect / mask select, then operate

- **User intent:** Gray `+` / Files → Select; then copy or delete the selection (`help/.../basicwork_select.htm`). Mixed-selection delete exists, but it uses harness multi-select, not the product’s Select dialog or “select by mask”.
- **Would confirm:** Select `*.txt` selects only text files; Copy transfers exactly those names; Unselect All leaves Copy acting on the focused item only (product rule: empty selection ⇒ focused item).
- **Suggested chain:** Seed `a.txt`, `b.txt`, `c.dat` → Select `*.txt` → copy to other panel → only the two `.txt` files appear there.

### Create → copy into it → rename → move → delete (happy path session)

- **User intent:** A short real session in one process without restarting FileManager between commands.
- **Why it is missing:** Each NUnit case gets a fresh process and workspace. Cross-command session state (panel path, leftover selection, operation queue idle, journal folder empty) is unproven.
- **Would confirm:** Nested dir exists; file content survives copy; rename; same-volume move removes source; delete removes the moved tree; main window stays enabled; no recovery prompt on a later restart in the same case.
- **Suggested chain:** Create `created\session` → copy `copy-file.txt` into it (after entering or by destination path) → rename file → move folder to other panel → delete it.
- **Harness notes:** Answer the nested-create IDOK prompt; wait for handle release between steps as existing copy tests do.

### Copy, then View the destination in the other panel

- **User intent:** Copy a text file, Tab to the other panel, View, read that it is the new copy.
- **Why it is missing:** View only opens a **source** fixture. Copy never checks that the inactive panel can focus the new name and open the viewer on **that** path.
- **Would confirm:** Viewer caption contains the target name; closing the viewer returns a usable main window; source still exists.
- **Suggested chain:** Copy `view-file.txt` → activate right panel → select the copy → View → close viewer.

### Copy, then Edit the destination with the sandbox editor

- **User intent:** Same as above for Files → Edit.
- **Why it is missing:** Edit only covers the configured editor on a **source** file after a Configuration rewrite and restart.
- **Would confirm:** `SandboxEditor - <copied name>` opens from the target panel after copy.
- **Suggested chain:** Install sandbox editor (existing Edit case) → copy `edit-file.txt` → Edit from target panel → close only the stub window.

### Open a document with a harness association (Enter), not Zip Open

- **User intent:** Enter on a `.txt` (or a stub `.exe` association) launches the associated app (`help/.../basicwork_open.htm`).
- **Why it is missing:** `OpenFile` is used only for ZIP characterization. Default Enter on a normal file is untested. Using the user’s Notepad is unsafe (same Windows 11 tabbed-window hazard as the old Edit case).
- **Would confirm:** A sandbox-associated extension opens `SandboxEditor` (or a dedicated stub) with the selected name; FileManager remains running.
- **Suggested chain:** Configuration associations (or a test-only file type in the isolated profile) → Enter on `open-file.txt` → owned window → close.
- **Harness notes:** Must not fall back to a machine-wide association that can close the user’s documents.

### Drive bar / change drive within the sandbox volume

- **User intent:** Click the drive bar or Change Drive to another folder on the same volume that the sandbox owns.
- **Why it is missing:** Panels are pinned by `-l`/`-r` only.
- **Would confirm:** After changing the left panel to `Workspace.TargetDirectory` (same volume), listings match that folder; Copy then uses the new current directory as source.
- **Harness notes:** Stay inside owned `filemanager-testdata` children. Do not automate arbitrary `C:\` or the user’s profile.

---

## P2 — Archives, Find, filter, clipboard, compare, attributes

### Pack selected files, then Unpack to the other panel

- **User intent:** Alt+F5 Pack, then Alt+F9 Unpack (`help/.../basicwork_pack.htm`, `basicwork_unpack.htm`). Files → Pack/Unpack are first-class Files menu commands (`CM_PACK`, `CM_UNPACK`).
- **Why it is missing:** ZIP coverage only **navigates into** a pre-seeded archive after Open. Pack and Unpack dialogs are never submitted. 7-Zip compatibility is a **native probe** (`test-7zip-compatibility.ps1`), not the Pack UI.
- **Would confirm:** A Zip (or 7-Zip, if enabled) archive appears with the selected members; Unpack into an empty target folder restores the same names and content.
- **Suggested chain:** Select two files → Pack to `session.zip` in the target panel → Unpack that archive into a new folder → content equals sources.
- **Harness notes:** Requires Zip and/or 7-Zip plug-in deployed (`FILEMANAGER_UI_ZIP_PLUGIN` pattern). Prefer Zip for the default lane.

### Navigate into ZIP, copy a member out to disk

- **User intent:** Open archive in the panel (`ptZIPArchive`), copy a file to the opposite **disk** panel.
- **Why it is missing:** After ZIP Open, the case only quick-searches the payload name. It never copies out or deletes inside the archive.
- **Would confirm:** Target disk file content is `zip-characterization-content`; archive still contains the member.
- **Suggested chain:** Open zip → select payload → Copy to other panel → wait for release → read file.

### Unpack (or copy-out), then delete the archive

- **User intent:** Extract, then remove the `.zip` from disk.
- **Would confirm:** Extracted tree intact; archive file gone; panel listing updates.
- **Suggested chain:** Unpack → Delete archive with confirmation → extracted files remain.

### Find results: focus in panel, then copy or delete

- **User intent:** Find → jump to the hit in the panel (`help/.../finddlg_main.htm`) → operate on it.
- **Why it is missing:** Find only asserts **one row in the results list**, then closes the dialog. No “Go to file”, no copy from Find, no delete of a found name.
- **Would confirm:** After activating the result, the source panel focuses `find-target.txt`; Copy places it on the target; or Delete removes only that file.
- **Suggested chain:** Find unique nested file → activate result → Copy → close Find → target has the file.

### Find with content containing text

- **User intent:** Search file **contents**, not only names.
- **Why it is missing:** The existing case forces name mask + path + subdirectories and explicitly disables other options so prior Find state cannot leak. Content search is never turned on.
- **Would confirm:** A unique string inside one nested file yields exactly one result; a non-matching string yields zero.
- **Harness notes:** Keep the search root at `Workspace.SourceDirectory`. Do not search the whole user profile.

### Find duplicates, then delete extras

- **User intent:** Find Duplicate Files (`help/.../finddlg_duplicate.htm`) by name+size+content, then delete one copy.
- **Would confirm:** Two identical files are grouped; deleting one leaves the other; a unique file is not listed as a duplicate.
- **Suggested chain:** Seed two equal files and one different → Find duplicates → delete one duplicate from results or panel → one copy remains.

### Panel filter, then copy what is visible

- **User intent:** Filter dialog (`help/.../dlgboxes_filtr.htm`) so the panel shows `*.txt` only, then Select All and Copy.
- **Why it is missing:** No filter test. Copy always operates on explicit quick-search names.
- **Would confirm:** Filtered listing omits `c.dat`; copy does not create `c.dat` on the target; clearing the filter restores the full listing.
- **Suggested chain:** Seed mixed names → Filter `*.txt` → Select All → Copy → clear filter.

### Clipboard Copy/Paste between panels (and Cut/Paste as move)

- **User intent:** Edit → Copy / Cut / Paste (`help/.../advwork_clipboard.htm`). Pack/unpack via clipboard is documented but secondary.
- **Why it is missing:** The characterization suite **forbids** clipboard sequence observation in the harness (`GetClipboardSequenceNumber` must not appear). That is a CI constraint, not a reason the **product** clipboard path should stay untested on a dedicated interactive runner.
- **Would confirm:** Copy+Paste duplicates the file on the target without removing the source; Cut+Paste removes the source after paste; ghosted icons after Cut revert if a second Copy happens without Paste.
- **Suggested chain:** Select file → Copy → activate other panel → Paste → compare content; repeat with Cut.
- **Harness notes:** Run only where the session owns the clipboard (same caution as the UI lane taking over the desktop). Do not paste into Explorer.

### Compare Directories, then copy the selected differences

- **User intent:** Commands → Compare Directories (`help/.../dlgboxes_cmpdirs.htm`): clears selection, selects files that exist only on one side or that differ by size/time/content, then the user copies the selection.
- **Why it is missing:** `CM_COMPAREDIRS` is on the toolbar; no UI case.
- **Would confirm:** With left `a.txt` only, right `b.txt` only, and a same-name file with different content, Compare-by-content selects the expected names; Copy of the left-only selection creates `a.txt` on the right.
- **Suggested chain:** Seed asymmetric trees → Compare by content → Copy selected from left → listings more aligned.

### Change attributes, then copy (attributes survive)

- **User intent:** Files → Change Attributes (`CM_CHANGEATTR`), e.g. set Read-only, then copy.
- **Why it is missing:** Copy asserts last-write time and ADS, not Archive/Read-only/Hidden after the Attributes dialog.
- **Would confirm:** Source is read-only after OK; destination after copy is read-only (or matches the product’s documented preservation options).
- **Suggested chain:** Change Attributes on `copy-file.txt` → Copy → assert attributes on both sides.

### Change case of names (Files → Change Case)

- **User intent:** Batch rename case (`CM_CHANGECASE`), distinct from the single-file case-only rename already tested.
- **Would confirm:** Selected `mixed.txt` becomes `MIXED.txt` (or the chosen scheme) without rewriting content.
- **Suggested chain:** Select several files → Change Case → listing shows new casing; content unchanged.

### Convert text encoding / line endings

- **User intent:** Files → Convert (`CM_CONVERTFILES`, `help/.../advwork_convert.htm`).
- **Would confirm:** A UTF-8 file with LF becomes the selected encoding/EOL on disk; a binary file is skipped or left unchanged per dialog options.
- **Suggested chain:** Seed `convert-me.txt` → Convert with a known table → read bytes.

### Copy with file mask / include subdirectories options on the Copy dialog

- **User intent:** The Copy dialog is not only a destination path; users restrict by mask and “including subdirectories”.
- **Why it is missing:** Tests fill the path control and confirm overwrite/skip. Masks and “copy files of directories” options are unused.
- **Would confirm:** Copy of a tree with mask `*.txt` copies text files only; directories still created as needed.
- **Suggested chain:** Seed mixed tree → Copy with mask → assert names on target.

---

## P2 — FTP and other virtual filesystems (beyond greeting / one download)

### Loopback FTP: login, list, download, disconnect

- **User intent:** Connect to FTP Server, see a listing, copy a file to the local panel, disconnect (`help` FTP using_connect / using_addbookmark).
- **Why it is missing:** The nested `ProductFtpControlConnectionTests` only proves a `USER` command after a fragmented `220`. The live MojeRzeczy test is explicit, needs secrets, and checks **size only**. Ordinary CI never lists or downloads from a scripted FTP **data** channel.
- **Would confirm:** After login to a scripted fixture, the panel lists a known name; Copy to the sandbox writes the expected bytes; Disconnect returns both panels to disk paths.
- **Suggested chain:** Scripted FTP (control + PASV data) → Connect → select file → Copy to right panel → Disconnect.
- **Harness notes:** Keep this on 127.0.0.1. Do not persist certificate exceptions (live test already documents that rule).

### FTP upload (local → remote) then re-download

- **User intent:** Copy from disk panel into the FTP panel.
- **Why it is missing:** Live test only downloads `/skan.txt`. Upload, overwrite on server, and resume are untested in UI.
- **Would confirm:** Uploaded bytes match; a second download matches; source local file still exists.
- **Suggested chain:** Connect (loopback or explicit live) → Copy local file to remote → Copy back to a different local name → compare.

### Open Plugin FS (Files → Open Plugin File System)

- **User intent:** Open a VFS (FTP, Registry, Network, Portables) from the command (`help/.../basicwork_openfs.htm`).
- **Why it is missing:** FTP is reached via Connect dialog, not the generic Open FS command. Registry Editor / Network / Portables have no UI tests.
- **Would confirm:** Choosing a deployed FS plug-in changes the panel mode (`ptPluginFS`) and shows a non-empty or documented empty listing without crashing.
- **Harness notes:** Prefer Registry plug-in on `HKCU` under the test configuration key only, or a loopback FTP FS.

---

## P3 — Viewer, compare files, split/combine, NTFS, shell, plug-ins

### Internal viewer: Find, wrap, next/previous file, clipboard copy of selection

- **User intent:** Viewer help topics (`viewer_finding.htm`, `viewer_wrap.htm`, `viewer_clipboard.htm`, `viewer_otherfiles.htm`).
- **Why it is missing:** View only checks window class and caption, then closes.
- **Would confirm:** Find in file highlights a seeded string; Next File opens the next panel item; Wrap toggles without losing the window; Copy places text on the clipboard when the lane is allowed to use it.

### File Comparator plug-in on two selected files

- **User intent:** Compare two text files from the panels (`src/plugins/filecomp`).
- **Would confirm:** Differences window opens for unequal files; equal files report no differences; closing returns to the main window.
- **Suggested chain:** Select left `a.txt` and right `b.txt` (or two in one panel per the plug-in’s documented selection rules) → Compare Files.

### Split then Combine (Split & Combine plug-in)

- **User intent:** Split a file into parts, then Combine with CRC (`src/plugins/splitcbn` help).
- **Would confirm:** Parts exist; Combine restores original bytes and CRC; parts can be deleted afterwards.
- **Suggested chain:** Split `payload.bin` to 3 parts in the target → Combine to a new name → hash equals original.
- **Harness notes:** Source-contract tests already require staged split/combine **implementation**; this gap is the **UI** dialogs.

### Checksum plug-in: compute and verify

- **User intent:** Generate a checksum file and verify it.
- **Would confirm:** A `.sha256` (or the plug-in’s default) is created; Verify reports success; a mutated file reports failure.

### 7-Zip Pack / Test archive from the UI

- **User intent:** Pack with 7-Zip packer, then Test archive (`using_testingarc.htm`).
- **Why it is missing:** `test-7zip-compatibility.ps1` is an oracle corpus against the wrapper, not Alt+F5 / Test from the panel.
- **Would confirm:** Created `.7z` lists members; Test reports success; a truncated copy reports failure without hanging the UI.

### NTFS Compress / Encrypt (Files → NTFS Commands)

- **User intent:** `help/.../advwork_ntfscmds.htm`.
- **Would confirm:** Compress sets the Compressed attribute; Uncompress clears it; Encrypt/Decrypt behave as documented and are not combined with compress.
- **Harness notes:** NTFS-only; skip on FAT. Encryption may need a user certificate—skip with an explicit capability message if unavailable. Keep files inside the sandbox.

### Recycle Bin: restore is still out of product-test policy — optional inspect-only chain

- **User intent:** Delete to bin, then restore from the shell Recycle Bin UI.
- **Why it is missing:** The existing test only checks item **count** and never restores or empties.
- **Recommendation:** Prefer **not** automating Restore (touches the user’s bin). If added, restore **only** the harness-named item and re-assert the sandbox path. Never Empty Recycle Bin.

### Command line: insert focused name and run a sandbox command

- **User intent:** `help/.../othertask_cmdline.htm` — Ctrl+Enter inserts the name; Enter runs a shell command.
- **Would confirm:** A command that writes a file under the workspace (e.g. `cmd /c copy ...`) completes; Refresh shows the result.
- **Harness notes:** Redirect `TEMP`; do not launch interactive `cmd` windows that outlive the test. Close-shell-after-execution should be on in the isolated profile.

### User Menu item that opens the sandbox editor

- **User intent:** User Menu bar (`advwork_usermenu.htm`).
- **Would confirm:** A disposable profile User Menu entry runs `SandboxEditor` with the focused file.

### Hot paths: assign, Go To, persist after restart

- **User intent:** Hot paths (`navigating_hotpaths.htm`, configuration hot paths). Toolbar contract tests mention a saved-location **glyph**, not the Go To behavior.
- **Would confirm:** Assigning a hot path to `Workspace.SourceDirectory\copy-tree` and invoking it changes the active panel path; restart still has the assignment in the isolated profile.

### PictView / IE Viewer / Markdown viewer on a seeded file

- **User intent:** View an image or `.md` with the associated **viewer plug-in**, not only Salamander’s text viewer.
- **Would confirm:** The correct viewer window class or caption opens; close does not kill FileManager.
- **Harness notes:** PictView is WIC-backed ([README.md](README.md)); seed a tiny PNG. Markdown hardening is already a **compiler probe**, not UI.

### Renamer plug-in: preview and rename a mask

- **User intent:** Batch rename with preview, then apply.
- **Would confirm:** `file01.txt` → `file01.bak` (or similar) as previewed; cancelled preview changes nothing.

### DiskMap / Undelete / WinSCP / Portables / Automation scripts

- **User intent:** Niche plug-ins.
- **Recommendation:** Do **not** block the release UI lane on these. Add characterization only when a reported defect needs a reproducible desktop script. WinSCP is a large third-party UI; live servers belong in an Explicit lane like MojeRzeczy.

### Shell context menu and Explorer copy-hook

- **User intent:** Right-click in the panel; Explorer integration (`salextx64.dll`).
- **Recommendation:** Low automation value and high flake/risk (other software’s context menus). Prefer a focused manual charter unless a specific shell-extension regression is filed.

### Drag-and-drop between panels

- **User intent:** Drag files from left list to right list (copy vs move depending on keys/volume).
- **Why it is missing:** All operations use menu/command IDs. Drag/drop is a primary novice path.
- **Would confirm:** Drag-copy keeps source; drag-move (same volume) removes source; overlay/cursor matches documented modifiers if observable.
- **Harness notes:** FlaUI drag on owner-drawn lists is brittle; try native mouse messages on the list HWND after hit-testing an item rect.

### Operations queue: start a second copy while the first is waiting

- **User intent:** Queue another operation from the Copy dialog (“Add to queue” exists on FTP download; disk operations have `COperationsQueue`).
- **Would confirm:** Two copies complete in order; both destinations are correct; cancelling one does not abort the other.
- **Suggested chain:** Start a copy that hits a conflict prompt → queue a second non-conflicting copy → resolve the first → both results present.

### Single-instance: second launch with a path focuses the existing window

- **User intent:** Opening a folder with a second `salamand.exe` according to the configured single-instance policy ([architecture.md](architecture.md) bootstrap).
- **Would confirm:** A second process does not leave two main windows (or does, if the isolated profile allows multiple instances—assert the configured policy); the existing panel path updates when the product is supposed to pass the path through.
- **Harness notes:** Only launch processes the fixture will kill; never attach to a user-owned Salamander.

### Update available indicator

- **User intent:** Title suffix `(update available)` and Help → Download update ([README.md](README.md)).
- **Recommendation:** Mock GitHub releases only if a local fixture can replace `api.github.com`; do not hit the real network in the default UI lane. Low priority versus file operations.

---

## Product limits — do not write tests that demand the opposite

These are already recorded in `testing.md` as limits the lane respects. New tests should **stay inside** them or skip with an explicit message:

- Paths longer than `PATH_MAX_PATH` (247 usable characters) produce **Error Building Script**.
- Supplementary-plane characters typed into ANSI dialogs become `?`.
- Directory rename collisions are rejected (no merge).
- Recycle Bin tests must not empty the bin.
- ADS-unsupported and leak tests are quarantined until their documented races/budgets are fixed—un-quarantine is more valuable than adding a third overlapping case.

---

## Suggested implementation order

1. **Session happy path** (create → copy → rename → move → delete) and **Enter/parent navigation** — reuse `FileOperationUiTestBase` with multiple `ExecuteWithPath` calls in one test.
2. **Copy then View/Edit on the target panel** — small extensions of `FileAccessUiTests`.
3. **Pack + Unpack** and **copy a ZIP member to disk** — highest-value archive gap; Zip plug-in flag already exists.
4. **Find → focus → copy** and **panel filter → copy**.
5. **Loopback FTP list + download + disconnect** — makes CI FTP coverage real without MojeRzeczy.
6. **Compare Directories → copy differences**; **Change Attributes → copy**.
7. **Clipboard** and **drag-and-drop** on a dedicated interactive job, not on a runner that must not touch the clipboard.
8. Plug-in characterization (comparator, split/combine, checksum, 7-Zip UI) as optional categories, analogous to `RecycleBin` / `LiveFtp`.

Each new case should stay inside the owned `filemanager-testdata` / `6.0-filemanager-testdata` boundaries, use command and resource IDs rather than translated menus, wait for destination handle release, and purge journals between cases as the current lane does.
