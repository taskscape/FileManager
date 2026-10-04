# File Manager user manual

File Manager is the Windows two-panel file manager built from Open Salamander. This manual explains how to complete common jobs and how to use the plugins represented in this repository. Menu names below use the English interface. A plugin command appears only when that plugin is installed and enabled; available formats and shell devices also depend on the local Windows installation.

## Contents

- [Start with the two panels](#start-with-the-two-panels)
- [Navigate and select](#navigate-and-select)
- [Work with files and folders](#work-with-files-and-folders)
- [Find, compare, and organize](#find-compare-and-organize)
- [View, edit, and share information](#view-edit-and-share-information)
- [Windows integration and everyday extras](#windows-integration-and-everyday-extras)
- [Archives and remote locations](#archives-and-remote-locations)
- [Personalize the workspace](#personalize-the-workspace)
- [Plugin guide](#plugin-guide)
  - [Reorganize](#reorganize)
  - [Delivery Handoff](#delivery-handoff)
  - [Transfer, storage, and recovery plugins](#transfer-storage-and-recovery-plugins)
  - [Archive plugins](#archive-plugins)
  - [Inspection and productivity plugins](#inspection-and-productivity-plugins)
  - [Developer sample plugins](#developer-sample-plugins)
- [When an operation does not go as expected](#when-an-operation-does-not-go-as-expected)

## Start with the two panels

The left and right panels show locations side by side. One panel is **active**: it receives keyboard input and supplies the focused or selected files for commands. The other is usually the destination. For example, to copy photos from a camera folder to an archive folder, show the camera folder on the left, the archive folder on the right, and activate the left panel before pressing **F5**. Read the destination shown in the Copy dialog before confirming; the panels can also show archives or plugin file systems.

The highlighted **focused item** and the **selected items** are different. If nothing is selected, a file operation acts on the focused item. If one or more items are selected, it acts on that selection. This distinction matters when the focus has moved to a different file after you selected a group. The status and information lines help you confirm the selection and its total size.

The path line above each panel shows the current location. The drive menu switches drives and opens plugin file systems. You can browse an archive as a folder when an archive plugin recognizes it, then return to its parent location. Commands also work from the menu and, where present, from the top, middle, or bottom toolbar; this manual gives menu paths and a few stable shortcuts so it works with customized toolbars.

### A first five-minute exercise

1. Create two empty practice folders, such as `C:\FM-Practice\Incoming` and `C:\FM-Practice\Sorted`, using **Commands → Create Directory** or **F7** in their parent folder.
2. Open `Incoming` in the left panel and `Sorted` in the right panel. Activate the left panel, create a small text file with **Files → New** if a suitable template is available, or copy in an existing harmless file.
3. Focus the file and press **F5**. Confirm that the Copy dialog points to `Sorted`, then copy it. Both panels should now show a copy.
4. Focus the copied file in the right panel and press **F3** to view it. Close the viewer, then press **F6** and give the copy a new name.
5. Select the practice copy and press **F8** to see the deletion confirmation. Check the Recycle Bin choice before proceeding. This exercise shows the distinction between source, destination, focus, and selection without involving important data.

## Navigate and select

### Move around folders and drives

To navigate to a known path, activate a panel and enter that path in its directory line or use the change-directory command. **Alt+F1** opens the left drive menu and **Alt+F2** opens the right drive menu. A drive menu may include local drives, network locations, and installed plugin file systems such as FTP or the registry. If a mapped drive is temporarily disconnected, selecting it can make Windows reconnect. If a network lookup stalls, **Esc** lets you continue while Windows finishes reporting the error.

For a recurring location, configure a **Hot Path** in **Configuration → Hot Paths**. Give it a meaningful name such as “Client deliveries” and point it at a local folder, UNC share, archive location, or plugin file system. The first ten Hot Paths can be reached by keyboard shortcuts; the configuration dialog shows the assignment. Directory history provides a different shortcut: it returns to folders you actually visited. Use Hot Paths for stable locations and history for an investigation you are doing now.

**Scenario — switching between a project and its delivery share:** Keep the project in one panel, store the delivery share as a Hot Path, and open it in the other. Before copying, inspect both path lines. This avoids accidentally placing the delivery inside the working project.

### Sort, display, and filter a panel

Click a column heading to sort a folder by name, extension, size, time, or another available column. Change the panel view in the View menu to choose how much detail to show. **Commands → Calculate Directory Sizes** (**Ctrl+Shift+F10**) computes sizes for the folders in the active panel; then sort by Size to find large directories. Pressing **Space** on a folder both selects it and calculates its size.

Use the panel filter when you need to see only certain files, such as `*.pdf` in a mixed deliverables folder. A filter hides nonmatching files in the display; it does not delete them. It remains in effect as you navigate, so clear it when files seem to be missing. Hiding selected or unselected names is a separate, temporary way to reduce clutter in the current panel. Color and highlighting rules can make file types easier to recognize without changing the files themselves.

### Select exactly the intended files

Use **Insert** or **Space** to toggle the focused item. Use **Edit → Select All**, selection masks, and unselect commands for larger sets. Check the panel's selection count before a batch operation. Selection can be saved and later loaded, including into the opposite panel; when loading, choose whether it replaces, adds to, subtracts from, or intersects the current selection.

**Scenario — send only signed PDFs:** Open the outgoing folder, filter to PDFs, select the signed versions, and check that no draft remains selected. Copy with **F5**. Clear the filter afterward so subsequent work shows all file types again.

### Find a name immediately

Simply start typing in an active file panel to enter quick search. It focuses the first name with the typed prefix. Use **Down** and **Up** for the next or previous match, **Backspace** to shorten the prefix, and **Esc** to leave quick search. Quick search finds names in the current panel; use the full Find dialog to search subfolders or contents.

## Work with files and folders

### Copy files or folders

1. Open the source in the active panel and the intended destination in the other panel.
2. Select files or folders. A selected folder includes its contents.
3. Choose **Files → Copy** or press **F5**. Read the **Copy to** path; change it if the other panel is not the right destination.
4. Open **Options** if you need masks or other copy criteria, then confirm. Respond to conflicts for the actual files involved instead of assuming that all duplicates should be overwritten.
5. Inspect the destination. If the operation was cancelled or interrupted, read its result and any recovery report before retrying.

Copying between a disk folder and an archive or plugin file system is supported where the plugin provides the operation. Copying directly from one archive or plugin file system to another is not generally supported; use a temporary disk folder as an intermediate location. A copy should leave the source in place. Recent native copy work uses journals and recovery checks, but a conflict or failed recovery can still require your decision. Do not delete a retained `.previous` backup or a reported journal until you know which version is correct.

### Move or rename

Select the items, choose **Files → Move/Rename** or press **F6**, and verify the target path or new name before confirming. To rename in place, leave the parent folder unchanged and change only the name. To move between panels, aim the dialog at the destination panel. A cross-volume move may need a copy-and-removal sequence, so keep the source until completion is confirmed. Moving from an archive to disk is not provided as a direct move; copy out, verify the result, and only then delete from the archive if supported.

**Scenario — file a finished report:** Put `C:\Projects\Report\Drafts` on the left and `C:\Projects\Report\Approved` on the right. Select only the signed report, press **F6**, confirm the full destination, and check that it is present there. If the dialog reports a name collision, decide whether the existing approved version must be retained.

For a large naming pattern across many files, use the [Renamer plugin](#renamer) instead of repeating F6. For a rearrangement across folders that needs a preview and reviewed plan, use [Reorganize](#reorganize).

### Delete and recover from ordinary mistakes

Choose **Files → Delete** or press **F8/Delete** after selecting the intended items. The configured Recycle Bin behavior determines whether deleted disk items are placed there or removed immediately. **Shift** inverts the configured behavior, so read the confirmation rather than relying on a remembered shortcut. Deleting a folder also deletes everything under it. Use the [Undelete plugin](#undelete) for supported deleted files that are no longer in the Recycle Bin, but stop writing to that volume first because new writes can reuse the deleted data.

### Create folders and other objects

Use **Commands → Create Directory** or **F7** in the panel that should contain the new folder. **Files → New** exposes Windows document and shortcut templates configured on that machine. After creation, the new item receives focus, so you can rename it immediately. For a project skeleton, create a few top-level folders first, then use copy or the User Menu for repeated structure; avoid making an archive or plugin file system the target unless its creation command is available.

### Change attributes, timestamps, and NTFS properties

Select items and choose **Files → Change Attributes** (**Ctrl+F2**) to set flags such as read-only, hidden, archive, or system and to adjust supported timestamps. With multiple items, a mixed checkbox means their values differ; set it deliberately if you want one value applied to all. NTFS compression and encryption are separate file-system features exposed by commands for supported NTFS paths. Check the target volume and permissions first; those properties may not survive every cross-volume or network transfer.

### Convert text files

Select the text files and open **Files → Convert** (**Ctrl+K**). Choose the current and desired character encoding and line endings, set a file mask, and decide whether to include files in selected subfolders. Preview or test with a copy when the original encoding is uncertain. This command changes file contents; it is not the same as changing only the displayed text encoding in a viewer.

## Find, compare, and organize

### Search by name, metadata, or content

Open the folder where the search should begin and choose **Commands → Find Files and Directories** (**Alt+F7** or **Ctrl+F**). Enter a name mask, then narrow the search by path, size, date, attributes, or contained text as needed. Review the found-items list before acting on it; found files can be viewed, edited, copied, moved, or deleted from that list. The Find dialog also offers duplicate detection using combinations of name, size, and content. Files with the same name are not necessarily duplicates: compare content before removing one.

**Scenario — find an outdated logo:** Search the project root for `*logo*`, limit the date range if useful, then open likely matches in the viewer. Save or copy the result list if somebody else must review which variants should be kept.

### Compare two folders

Show the folders in opposite panels and choose **Commands → Compare Directories** (**Ctrl+F10**). Select the criteria relevant to the job: missing names, newer or larger files, differing attributes, or differing content. After the comparison, differences are selected in the panels. Review those selections before copying or deleting, especially if the folders contain intentional local-only files. For two individual files, use the [File Comparator plugin](#file-comparator).

### Record or share a file list

Select the items and choose **Commands → Make File List** (**Ctrl+M**). Configure the line format, then send the result to a text file, clipboard, or internal viewer. A saved list can serve as an attachment inventory or a simple handoff checklist; it is not a content hash or proof that files have not changed. For integrity evidence, use [Checksum](#checksum) or [Delivery Handoff](#delivery-handoff).

### Change name case and file names

The case-changing command applies lower, upper, and mixed-case patterns to selected names, optionally within selected folders. Decide whether to change the complete name, only the stem, or only the extension. For search-and-replace, counters, regular expressions, or moves based on a name template, use [Renamer](#renamer). Check its live preview and any collisions before committing.

## View, edit, and share information

### Open, view, and edit

**Enter** opens the focused file with its configured action. **F3** opens the configured viewer; **Alt+F3** uses the alternate viewer. The internal viewer is useful for text and binary inspection, while plugins can handle images, media metadata, HTML, databases, and PE files. **F4** opens the configured editor. If the associated editor is unsuitable, use **Files → Edit With** and choose another configured editor. Viewing should not modify a source file; editing will.

**Scenario — inspect an unexpected download:** View a suspiciously named file first instead of executing it. If it is a text or CSV file, inspect its content and encoding. If it is an executable, use [Portable Executable Viewer](#portable-executable-viewer) for metadata; metadata inspection does not establish that a program is safe to run.

### Copy names and paths as text

Use **Edit → Copy Path + Name as Text** (**Alt+Insert**) for a focused item's full path. **Edit → Copy Name as Text** (**Alt+Shift+Insert**) copies only its name; **Edit → Copy Path as Text** (**Ctrl+Alt+Insert**) copies the current panel path. The UNC variant (**Ctrl+Shift+Insert**) is useful when sending a network location to a colleague. There is also a **Copy full path** panel context-menu command. These commands put text on the clipboard, not the file itself.

### Open recent work and run external tools

**Commands → List of Opened Files** (**Alt+F11**) revisits recently opened, viewed, or edited files. The **User Menu** (**F9**) runs configured external programs or opens documents, optionally passing the selected file as an argument. Configure each User Menu item with the executable, arguments, and working directory you actually need; test it on a harmless file before using it for a batch. The command line can launch Windows commands from a chosen panel location when that is more convenient than a separate terminal.

## Windows integration and everyday extras

### Clipboard operations and shell properties

**Edit → Copy, Cut, and Paste** work with file references on the Windows clipboard. You can paste into another File Manager panel or a compatible Windows application. Cutting does not remove the source at the instant you press Cut; the move occurs when you paste. Copying a path **as text** is a different command and replaces file references on the clipboard with text, so paste it into a message or terminal rather than expecting a file transfer. Clipboard file operations can bridge certain disk, archive, and plugin locations, subject to the plugin's capabilities.

Choose **Files → Properties** or **Alt+Enter** for the selected item to inspect Windows file properties, permissions, and, for a folder, sharing options. Property sheets come from Windows and installed shell extensions, so their tabs vary by file type and location. To inspect a shortcut or symbolic link's destination directly in a panel, focus it and choose **Commands → Go to Shortcut or Link Target** (**Ctrl+T**). If the target is a folder, the panel navigates there; if it is a file, the file receives focus. Confirm the new panel path before copying or deleting anything.

### Disk and network-share information

Open a drive in a panel and choose **Commands → Drive Information** (**Ctrl+F1**) to inspect used and free space, file-system type, and drive identity. This helps before copying a large archive or deciding whether NTFS-specific options are available. **Commands → Connect Network Drive** maps a share for repeated use; the matching disconnect command also closes plugin file-system connections. **Commands → Shared Directories** (**Ctrl+Shift+F9**) lists local shares and can help locate or stop a share. Sharing a folder and granting access should be done through its Windows Properties and your organization's permission policy; merely seeing a share does not mean another user can open it.

### Email and file lists

Select files and choose **Files → Email** (**Ctrl+E**) to hand them to the configured email application as attachments. Check attachment size limits and recipients in the email application before sending. For a large set, a ZIP archive or delivery package may be easier to review, but Email does not create either automatically. **Commands → Make File List** is better when the recipient needs an inventory rather than the files themselves.

### Refresh and revisit locations

Panels refresh automatically for many local changes. If an external program changes a network folder or a removable drive and the list looks stale, use the panel's **Refresh** command (**Ctrl+F9** or **Ctrl+R**). Use **Commands → List of Working Directories** (**Alt+F12**) to return to a folder you recently visited. This history is separate from **List of Opened Files** and from the named Hot Paths you configure yourself.

### Internal Viewer controls

When the internal viewer is open, **Ctrl+F** searches the content, **Ctrl+N** moves to the next result, and **Ctrl+P** moves to the previous one. **Ctrl+W** toggles line wrapping for long text lines. Use the viewer's mode and character-conversion commands when a log looks binary or its text is displayed with the wrong encoding. Changing the viewer's display mode does not rewrite the underlying file. If you need a saved conversion, use **Files → Convert** on a copy of the source.

### Stored connection credentials and running tasks

If you save FTP or other plugin credentials, review **Configuration → Security** and set a Master Password when the workflow calls for protected stored secrets. The application asks for it when an instance first needs the protected data; keep a recovery procedure for it because forgetting it affects access to saved credentials. **Help → Task List** shows running File Manager instances and can help investigate one that has stopped responding. Breaking a task is a diagnostic action, so first note any active copy or build whose recovery state you may need to inspect afterward.

## Archives and remote locations

### Create an archive

Select the desired files and folders, choose **Files → Pack** (**Alt+F5**), enter the archive name, and choose a packer such as ZIP or 7-Zip. If you give only a filename, the archive is created in the current folder. Review format-specific options such as compression, encryption, or multi-volume output. Create a small test archive and open it before deleting source files; packing is not an automatic backup unless the archive can be read and is stored somewhere appropriate.

### Read or extract an archive

Focus a supported archive and open it like a folder to browse its entries. To extract it, choose **Files → Unpack** (**Alt+F9** or **Alt+F6**) and specify the destination. A dot (`.`) means the current folder. To extract a subset, enter the archive and copy selected entries to a disk folder where the plugin permits it. Check destination paths and overwrite prompts; archives from outside your organization may contain unexpected names or structures.

### Reach a remote location

For FTP/FTPS, use the [FTP Client](#ftp-client). For a Windows network share, enter its UNC path or browse it through [Network](#network). The [WinSCP](#winscp-optional) source and help describe SFTP/SCP, but that plugin is not a project in the current Visual Studio solution; check whether it is actually installed before relying on it. [Windows Mobile](#windows-mobile) and [Portables](#portables) expose compatible connected devices as plugin file systems, subject to device and driver support.

## Personalize the workspace

Use **Configuration** to set panel views and columns, archive associations, file viewers and editors, confirmation prompts, Hot Paths, colors, and toolbars. Keep the commands you use every day visible, but retain confirmations for destructive operations until the workflow is familiar. To change which archive or viewer handles an extension, inspect the relevant associations rather than assuming that every plugin can write every format.

Open **Plugins → Plugins Manager** to inspect, add, remove, or configure a plugin. **Add** points at a `.spl` plugin file. Removing an entry from Plugins Manager changes the application configuration; it does not delete the plugin file from disk. If a documented command is missing, check the manager, then check whether the plugin binary and any companion files were staged with the build. Some capabilities need a Windows component, an installed codec, or an external library.

File Manager checks for application updates shortly after startup. When a newer release is reported, the title bar indicates it and **Help → Download update** opens the releases page. The [Check Version plugin](#check-version) also has a manual check command; use the in-app result and release page when deciding whether to update.

## Plugin guide

The following sections use a consistent pattern: a real job, the steps to perform it, and the result or constraint to check. Archive plugins share the Pack, Unpack, and browse workflow described above, so their sections focus on what each format plugin adds. Plugin names and exact menu text can vary with the installed language module.

| Plugin | Main job |
| --- | --- |
| [Reorganize](#reorganize) | Plan, preview, review, and apply folder moves. |
| [Delivery Handoff](#delivery-handoff) | Build and verify rule-based delivery packages. |
| [FTP Client](#ftp-client), [Network](#network), [Folders](#folders) | Browse FTP servers, network shares, and Windows shell locations. |
| [Portables](#portables), [Windows Mobile](#windows-mobile), [WinSCP](#winscp-optional) | Work with portable devices, legacy handhelds, or optional SSH transfers. |
| [Registry Editor](#registry-editor), [Undelete](#undelete), [UnFAT](#unfat) | Inspect registry data and recover supported deleted or image-based files. |
| [ZIP](#zip), [7-Zip](#7-zip), [TAR](#tar), [UnRAR](#unrar), [UnARJ](#unarj), [UnCAB](#uncab) | Pack or extract common and legacy archive formats. |
| [UnCHM](#unchm), [UnISO](#uniso), [UnLHA](#unlha), [UnMIME](#unmime), [UnOLE2](#unole2), [PAK](#pak) | Extract help, disc, encoded, compound, and game-package content. |
| [Checksum](#checksum), [DiskMap](#diskmap), [File Comparator](#file-comparator), [Database Viewer](#database-viewer) | Check integrity, visualize space, compare files, and inspect tabular data. |
| [Renamer](#renamer), [Split & Combine](#split--combine), [Automation](#automation), [Check Version](#check-version) | Perform batch naming, split files, run scripts, and check for updates. |
| [PictView](#pictview), [Internet Explorer Viewer](#internet-explorer-viewer), [Multimedia Viewer](#multimedia-viewer), [Portable Executable Viewer](#portable-executable-viewer) | Inspect images, web documents, media metadata, and executable structure. |
| [DemoMenu](#demomenu), [Salamander Demo Plugin](#salamander-demo-plugin), [DemoView](#demoview) | Learn the plugin SDK in a development build. |

### Reorganize

**Use it when:** a collection of files needs to move into a new structure, but you want to see the resulting tree and the individual moves before changing the disk. For example, a photographer may have `2026\Camera-A`, `2026\Camera-B`, and `2026\Exports` and want a client/year/shoot layout without losing the originals to an unreviewed batch command.

1. Open **Plugins → Reorganize → New Plan**. Choose a **scope** folder containing the source material and a **destination** folder for the proposed structure. Both must be disk paths; use a local or mapped/UNC file-system folder rather than an archive or another plugin panel.
2. Stage changes in the plan. You can move items from a disk panel into the `reorg:` proposed panel, rename an item in the proposed panel, add a mask-and-destination rule, or import a CSV mapping. The plugin records these as planned edits. The `reorg:` view shows the proposed destination tree with columns for the change, original location, issues, and reversibility. Inspect the tree before continuing.
3. If a staged change is wrong, use **Undo** or **Redo** in the plugin menu. This changes the plan, not the actual files. Save the plan as a UTF-8 `.reorgplan` file if you want to return later or share the intended layout for review. Opening a saved plan rescans the referenced folders.
4. Choose **Validate** to find conflicts and other issues. Open **Plan Review** to see the compiled operation steps as well as issues. If you want empty source folders cleaned up, set **Clean up emptied folders** here; the step list updates immediately. Read every move, creation, and cleanup step, then choose **Mark as Reviewed**.
5. Choose **Apply** and confirm. The plugin rescans the folders, checks that the compiled steps still match the review mark, and passes those steps to the host's durable file-operation engine. A changed plan, changed folder, blocking issue, or unreadable source prevents the reviewed plan from being applied as if it were still current. Return to Plan Review and mark the updated steps if necessary.
6. Inspect the destination and the operation summary. If an operation failed or stopped partway, follow the reported recovery information before starting another bulk operation. Optional cleanup moves emptied folders into a recovery store rather than permanently deleting them.

**Why this differs from F6:** F6 is suited to a known move or rename. Reorganize provides a proposed tree, rules and mappings, undo/redo while planning, validation, and an explicit review of the exact steps. It does not make copies or permanently delete files as a reorganization strategy. The reversibility column can report metadata loss or a change that is not exactly reversible; read it rather than assuming that every move can be undone without qualification.

### Delivery Handoff

**Use it when:** a client delivery has repeatable rules. An architecture firm, for example, may need approved A1/A3 PDFs, the matching drawing register, DWG/IFC source files, a specific revision naming pattern, and a contents list. Delivery Handoff uses a `*.handoff.json` specification to make those requirements visible before copying anything.

1. Show the working project folder in the **active** panel and the desired staging folder in the **inactive** panel. Both must be ordinary disk or network-share folders, and neither may contain the other. Delivery Handoff does not build directly into an archive, FTP panel, or portable-device panel.
2. If you have no specification, choose **Plugins → Delivery Handoff → New Specification from Template**. Templates cover a design studio handoff, architecture drawing issue, consultancy report, and example client delivery. By default the new file goes into the project's `.handoff` folder. Edit it in a text editor for your naming rules, expected items, formats, and evidence requirements. The plugin does not provide a graphical rule editor.
3. Choose **Validate Specification** to catch JSON or rule errors. The validator reports the location of each problem. Correct the file and validate again. Put shared specifications in the configured specification library if several projects use the same policy.
4. Choose **Build Delivery Package**. Confirm the working and staging folders; use **Swap** if they are reversed. Choose the whole working folder or only panel-selected items. Select the specification, enter required variables such as project code and revision, and inspect the proposed package folder name. An existing final package is not overwritten; increase the revision or choose another name if it already exists.
5. Choose **Scan**. The review window groups files by specification rule and reports missing expected items, wrong names, forbidden content, format problems, PDF page-size or image-size problems, and approval or licence evidence where the specification requires them. Exclude a candidate that should not ship, approve items requiring reviewer approval, and investigate findings using **Show in panel**. Use **Re-scan** after replacing or editing a source file. Some errors may be overridden only when the specification permits it and a reason is recorded; other errors cannot be overridden.
6. When no blocking errors remain, acknowledge any warnings and choose **Build**. Included files are copied to a hidden partial folder; each copy is checked with SHA-256 before the new folder is published. The originals are not moved, renamed, or modified. The default package includes `manifest.json`, `manifest.csv`, and `CONTENTS.txt`; a specification may rename outputs or disable the optional CSV and contents list. A default internal `<package>.handoff-build.json` record is written **beside** the package, outside the client-facing folder.
7. Before upload, focus or open the finished package in the active panel and choose **Verify Delivery Package**. Verification recomputes hashes and reports missing, changed, and unlisted files. With the build record and specification available, it can also authenticate the manifest and recheck relevant rules. If verification fails, correct the working material and build a new revision rather than editing a published package in place.

**Practical checks:** Review licence scope and expiry for included assets; a filename alone does not establish usage rights. Keep the build record internal because it can contain source paths and reviewer decisions. An interrupted build can leave a hidden partial folder, and the next build offers to remove an unused one. The plugin reports what was verified; it does not upload the package, send it by email, convert source files, sign it digitally, or independently re-create a human approval from the final folder.

### Transfer, storage, and recovery plugins

#### FTP Client

**Scenario — deliver files to a vendor's FTPS server.** Open **Plugins → FTP Client → Connect to FTP Server** (**Ctrl+Shift+F**). Create a bookmark with the hostname, initial path, username, and the connection settings required by the vendor, or use Quick Connect for a one-time session. Select FTPS when the server supports it and inspect the certificate information before accepting an exception. A bookmark can also be created from a currently open remote directory.

The remote tree appears in a panel. Show the local source or destination in the other panel and use **F5** to upload or download. Inspect the direction in the Copy dialog. The plugin also supports viewing remote files, deleting them, changing attributes or access rights where the server permits it, transfer mode choices, raw listings, and connection logs. Use the log when a server-specific listing or transfer fails. If a download is interrupted, private `.salftp-*.part` and `.meta` siblings may remain together for a verified resume. Keep them together until retry or manual inspection; do not assume that the visible destination was replaced. A remote source in an FTP move is removed only after the local result is validated and published. FTP/FTPS is different from SFTP over SSH.

#### Network

**Scenario — locate a share when you know the computer but not the share name.** Choose the **Network** plugin from a drive menu, browse computers, and open the available share. The plugin caches visited locations and reads network information without blocking the file panel. Once the share is open, show a local folder in the opposite panel and use ordinary copy commands. Access still depends on Windows credentials and share permissions. If the discovery list is incomplete, a known UNC path such as `\\server\share` may be more direct.

#### Folders

**Scenario — inspect the Windows Desktop or Recycle Bin alongside a disk folder.** Open **Folders** from the drive menu. It exposes shell locations such as Desktop, Control Panel, and Recycle Bin in a panel. Browse to the object you need, then use the shell action available for that object. A shell namespace item is not always a normal disk file, so do not expect every Files-menu operation to work on every icon.

#### Portables

**Scenario — copy photos from a phone that appears as a Windows portable device.** Connect and unlock the device, allow file access on its screen if prompted, then choose **Portables** from the drive menu. Browse the device's storage in one panel, open a local destination in the other, and copy a small test file first. Device availability and supported operations come from Windows Portable Devices and the device itself. If the device disappears mid-transfer, reconnect it and compare the resulting files before retrying.

#### Windows Mobile

**Scenario — retrieve data from a legacy Windows CE handheld.** Install the desktop connection software required by the device, connect the handheld, and open the **Windows Mobile** file system from the drive menu. Browse its folders and copy files to a disk folder for preservation. The plugin's help describes copy, move, delete, rename, view, and attribute operations, but these depend on the connected device and ActiveSync support. It is for Pocket PC, Smartphone, and Windows CE devices, not a general modern-phone protocol.

#### Registry Editor

**Scenario — inspect a program setting before changing it.** Open the **Registry Editor** plugin file system from a drive menu. Browse to a key, inspect values, and use the plugin's search command if you know only part of a value or key name. You can create or edit a value, create a key, rename, copy, move, or delete keys and values, and export keys. Export the relevant key first and record its original value before editing. The registry is live system configuration: a mistaken edit can affect Windows or applications immediately, and copying a key is not the same as copying an ordinary file.

#### Undelete

**Scenario — a deleted report is no longer in the Recycle Bin.** Stop writing to the affected volume, then open **Undelete** and choose the local volume or disk image to scan. Browse the recovered directory tree or search for the file, inspect its status, and restore it to a **different** volume or safe folder where possible. The plugin covers NTFS, FAT12/16/32, and exFAT and can restore supported NTFS alternate streams, compressed files, and encrypted files. Recovery is never guaranteed: deleted clusters may already have been reused, and restoring onto the same volume can overwrite data you still need to recover.

#### UnFAT

**Scenario — extract documents from an old FAT floppy image.** Focus an `.IMA` image and open it with **UnFAT** as an archive. Browse the FAT12/16/32 directory tree, select the needed entries, and copy them to a normal disk folder. Preserve the original image and do the extraction into a separate location. UnFAT reads FAT disk images; it is not a general raw-disk editor.

#### WinSCP (optional)

The repository includes WinSCP source/help material, but not a WinSCP project in the current Visual Studio solution, so check **Plugins Manager** before following this workflow. **Scenario — copy files to an SSH-based server:** If the plugin is installed, create or choose a stored SFTP session, authenticate with the method your server supports, and browse the remote side in a panel. Use **F5/F6** for supported transfers and inspect the remote target. The plugin help also describes synchronization, permission changes, and remote commands. SFTP/SCP requires an SSH server and is unrelated to the FTP Client's FTPS support.

### Archive plugins

For each archive below, first check whether its extension is associated with the intended plugin in **Configuration → Panels → Archives Associations**. Open the file to browse if supported, use **Files → Unpack** for extraction, and use **Files → Pack** only when the listed plugin can create that format. A read-only extractor does not become a packer merely because its archive opens in a panel.

#### ZIP

**Scenario — send a portable set of documents.** Select the documents, press **Alt+F5**, choose ZIP, and create the archive in the intended folder. Open the ZIP and inspect its contents; the ZIP plugin can also test archives, delete entries, edit a ZIP comment, create encrypted files and self-extracting or multi-volume archives. If you choose encryption, record the password through your organization's approved channel. A self-extracting archive is an executable and may be blocked by a recipient's security policy; ordinary ZIP is usually the simpler delivery format.

#### 7-Zip

**Scenario — archive a large folder with strong compression.** Select the folder, choose **Files → Pack**, select the 7-Zip packer, and set the desired compression and encryption options. Open the resulting `.7z` file to browse or test it before removing source material. The plugin also supports unpacking and deleting entries from 7-Zip archives. Ask recipients whether they can open `.7z`; a ZIP may be more interoperable.

#### TAR

**Scenario — inspect a Linux source bundle.** Focus a TAR or compressed TAR-related archive and open it in a panel. Browse the directory structure and unpack the entries you need to a disk folder. The plugin help lists TAR, GZIP, BZIP/BZIP2, RPM, CPIO, DEB, and Z for browsing and unpacking, and RPM information viewing. Use a separate packer if you need to create a new TAR package; this plugin is documented as an extractor/viewer.

#### UnRAR

**Scenario — receive a RAR archive from a partner.** Open the `.rar` in a panel, inspect its contents, and unpack to a chosen disk folder. The plugin reads and extracts RAR; it does not create RAR archives. Its engine may require a companion `unrar.dll`, so a plugin entry without the working engine is not enough. If opening fails, check the installed plugin payload before changing file associations.

#### UnARJ

**Scenario — retrieve a legacy ARJ backup.** Open an `.arj` file with UnARJ, select the entries needed for the current project, and unpack them to a separate folder. This plugin browses and extracts ARJ; keep the original archive until the recovered files have been checked.

#### UnCAB

**Scenario — inspect files packaged in a Windows CAB.** Open the `.cab`, browse its entries, and unpack the needed files to a working folder. CAB files often accompany installers; extracting an individual file does not run the installer or reproduce its setup actions.

#### UnCHM

**Scenario — extract images or HTML from a help file.** Associate `.chm` with UnCHM under Archives Associations if it currently opens in the Windows Help viewer. Then browse the CHM as an archive and copy the needed entries out. UnCHM browses and unpacks CHM; extracted HTML may refer to other extracted files by relative path, so keep its folder structure when necessary.

#### UnISO

**Scenario — recover a document from a CD/DVD image.** Open the image through UnISO, select a session if the image contains several, browse folders, and copy the document to a normal disk folder. The plugin also has a viewer for image properties and supports several optical-disc file-system variants, including ISO 9660 and UDF. It does not burn discs or change the source image.

#### UnLHA

**Scenario — open an old LZH software archive.** Open the `.lzh` file, inspect the contents, and unpack into a quarantine or work folder. The plugin browses and extracts LZH archives made by LHA; creation of new LZH files is outside its documented role.

#### UnMIME

**Scenario — recover an attachment from an exported message.** Open a supported `.eml`, BinHex, UUEncode, XXEncode, yEnc, or MIME-Base64 encoded file with UnMIME. Browse decoded entries, then copy the attachment to a disk folder. Review the extracted filename and content before opening it: decoding an attachment does not certify its origin or safety.

#### UnOLE2

**Scenario — inspect embedded streams in a legacy compound document.** Open an OLE Compound file with UnOLE2, browse its contained streams and storages, and extract the stream you need to a separate folder. The plugin is a browser/extractor; it does not edit the compound document or convert it to a modern Office format.

#### PAK

**Scenario — inspect and update resources in a Quake-era game package.** Associate the relevant `.pak` archive with PAK, browse its asset paths, and extract a texture or configuration file to a working folder. The plugin also registers archive editing and custom pack/unpack operations, so you can create a PAK or add and remove entries where the archive association offers those commands. Use **Optimize PAK** after edits if the plugin reports reclaimable space. Test an edited copy with the application that consumes it, preserve the original archive, and note internal paths when documenting where assets came from.

### Inspection and productivity plugins

#### Checksum

**Scenario — prove a downloaded data set matches the sender's checksum file.** Focus a supplied `.sfv`, `.md5`, `.sha1`, `.sha256`, or `.sha512` file and choose **Plugins → Checksum → Verify Checksums** (**Ctrl+Shift+V**). Wait for the status column to show which files passed, failed, or could not be found. A match verifies the bytes against that checksum file; if the checksum file came from an untrusted source, matching it alone does not establish authenticity.

To issue checksums yourself, select files and folders, choose **Plugins → Checksum → Calculate Checksums**, and choose CRC32, MD5, SHA-1, SHA-256, or SHA-512 as needed. The calculation window does not block other file-manager work. Save the results in a supported checksum-file format or copy an individual value to the clipboard. For current integrity exchanges, SHA-256 or SHA-512 is generally the better choice; use CRC32 or older hashes only when a recipient's format requires them. Keep the checksum file with the exact files it describes, without renaming or changing them afterward.

#### DiskMap

**Scenario — find why a project drive is full.** Open the project root in the active panel and choose **Plugins → DiskMap → Show DiskMap** (**Ctrl+Shift+D**). The treemap gives each file a rectangle proportional to the space it occupies. Start at the disk root if you want the whole volume. Follow the largest rectangles into their folders, then inspect candidates in the ordinary panel before deleting or archiving anything. DiskMap visualizes size; it does not decide whether a large file is safe to remove.

#### File Comparator

**Scenario — review two versions of a configuration file.** Put the old version in one panel and the new version in the other, focus the relevant file in each, and choose **Plugins → File Comparator → Compare Files** (**Ctrl+Shift+C**). You can also select two files in one panel or select one and focus the other. The plugin opens a side-by-side text or binary comparison; use its detailed-difference and whitespace views to distinguish changed characters from formatting-only changes. Recompare after editing a file. This is a visual comparison tool, not a merge operation that writes a reconciled file automatically.

#### Database Viewer

**Scenario — inspect a supplier's CSV without modifying it.** Focus the `.csv` and press **F3** if Database Viewer is associated with that format, or choose it through the viewer selection. The viewer also supports dBase and FoxPro DBF files and tab-delimited text. Check separators and text conversion if columns or characters look wrong. Use field selection, search, bookmarks, and clipboard export to inspect or share a few records. The viewer can show records and convert their displayed text code page; that display conversion does not rewrite the source database.

#### Renamer

**Scenario — turn camera names into a numbered delivery series.** Select the files, choose **Plugins → Renamer → Batch Rename** (**Ctrl+Shift+R**), and build a new-name rule from text, a counter, and the original extension. For example, use a `SiteVisit` prefix and a counter for a set of photos. Read the live preview for every output name before choosing **Rename**; check sort order, duplicate names, folders included in the selection, and whether a rule moves a file by changing its path. Renamer can also replace substrings, use regular expressions, insert dates or sizes, change case, and work recursively. Test a complicated expression on copies or a small subset first.

#### Split & Combine

**Scenario — send a file through a service with a per-file size limit.** Focus the source file and choose **Plugins → Split & Combine → Split File**. Specify the maximum part size or the number of parts, then send **all** parts and the generated combining information together. At the destination, select the parts or focus a part or the generated batch file and choose **Combine Files**. Check part order and output name before confirming. When the original CRC is available from the split, the plugin checks the joined result. Splitting does not compress or encrypt the data; it only divides it into pieces.

#### Automation

**Scenario — repeat a trusted routine across many selected files.** Put a Windows Script Host script in a configured Automation repository and choose it from the plugin's menu, plugin bar, or **Automation Open Script Menu** (**Ctrl+Shift+A**). For a one-off script, focus its file in a panel and choose **Run Focused Script**. The plugin supports the Windows Script Host engines installed on the machine, including the usual JScript and VBScript engines. A sample Count Lines script illustrates walking selected files and folders, masks, progress, and error handling; other samples may need external tools such as ImageMagick. Read a script before running it: it executes with your user privileges and can change files or launch programs.

#### Check Version

**Scenario — check for an update before an installation window.** Choose **Plugins → Check Version → Check For New Versions** and start a manual check. Read the result and open the official release information before installing anything. The application also has a host-owned startup update indication and **Help → Download update** route. The plugin's older help mentions a Taskscape server; rely on the current in-app result and release page for the actual version offered rather than treating that historical endpoint description as a deployment guarantee.

#### PictView

**Scenario — select photos for a report.** Associate supported image types with **PictView** and press **F3** on a photo. Move among files, zoom, inspect image properties and EXIF data, and use thumbnails to survey a folder. To make a derivative in another format, choose the viewer's **Save As** command; to create an illustration from the screen, use **Screen Capture**, crop it, and save the result. Keep the original image when experimenting with conversions. PictView now uses Windows Imaging Component: the exact formats available depend on Windows and installed codecs, and old PictView formats unsupported by those codecs may no longer open.

#### Internet Explorer Viewer

**Scenario — inspect a saved HTML or MHT document.** Associate the format with **Internet Explorer Viewer**, then press **F3** on the file. The plugin also advertises XML viewing. Because it is based on Internet Explorer components, rendering depends on what Windows still provides, and active content can behave differently from a plain-text view. Use the internal text viewer as the alternate viewer when you need to inspect markup rather than rendered content.

#### Multimedia Viewer

**Scenario — inventory audio metadata before migration.** Focus a supported MP3, Ogg Vorbis, WMA, WAV, or tracker-module file and open it in **Multimedia Viewer** to inspect details and tags. Select several media files and choose **Plugins → Multimedia Viewer → Export Info into HTML** to save a readable inventory. This is a metadata viewer and exporter; exporting information does not convert the media or repair incorrect tags.

#### Portable Executable Viewer

**Scenario — identify what a vendor DLL contains without running it.** Focus an `.exe` or `.dll` and open it in **Portable Executable Viewer**. Inspect the machine type, timestamps, import and export tables, debug information, and resources. Compare architecture and exported names with what the application expects. The viewer reads file structure; it does not prove publisher identity, compatibility, or absence of malicious code.

### Developer sample plugins

These three plugin projects illustrate the plugin SDK. They may appear in a development build or plugin manager, but they are examples rather than everyday file-management features. Keep them separate from production workflow choices.

#### DemoMenu

**Scenario — learn how a plugin adds a menu command.** Enable DemoMenu in a development installation, open its entry in the Plugins menu, and run its test command on harmless data. Then inspect `src/plugins/demomenu` to see how the command is registered and handled. Use the example to build a new menu extension; it is not a substitute for a production batch tool.

#### Salamander Demo Plugin

**Scenario — explore several plugin interfaces in one sample.** Enable the demo plugin in a development build and try its sample menu, file-system, archiver, viewer, and thumbnail surfaces with disposable files. Its source under `src/plugins/demoplug` shows how those SDK interfaces fit together. Treat sample operations as instructional and avoid important data.

#### DemoView

**Scenario — inspect a viewer plugin's integration.** Configure DemoView for a test file type, open a harmless file with **F3**, and review the sample viewer and thumbnail behavior. Its project under `src/plugins/demoview` is useful when creating a viewer plugin, while normal file inspection should use the internal viewer or a format-specific production plugin.

## When an operation does not go as expected

### The command or file type is missing

Open **Plugins → Plugins Manager** and check that the expected plugin is registered and enabled. Check its archive or viewer association for the extension. For a locally built copy, check that the `.spl`, language file, and any required sibling engine were placed in the running application's plugin directory; launching only `salamand.exe` from an incomplete build layout may leave plugins unavailable. For WinSCP, device plugins, image codecs, and UnRAR, also check their specific external prerequisites.

### A copy, move, or download stopped

Read the operation result and recovery report first. Confirm which source and destination files exist and their sizes before retrying. For native operations, a `.previous` sibling or journal may preserve the earlier destination. For FTP downloads, preserve the `.salftp-*.part` and matching `.meta` together until the plugin resumes them or you have deliberately resolved the state. A cancelled operation or storage error is not evidence that a destination is complete.

### A panel seems to omit files

Clear any active file filter or temporary hide setting. Check whether the panel is inside an archive, plugin file system, or a different path than expected. Refresh the panel, then check permissions or device connection if names remain absent. In a Reorganize `reorg:` panel, the proposed tree is a preview, so it intentionally differs from the physical disk until Apply completes.

### A delivery or reorganization is blocked

For Reorganize, open **Plan Review**, read its issues and exact steps, resolve disk or destination changes, and mark the current steps as reviewed. For Delivery Handoff, use the review window's **Problems only** and **Omissions** filters, correct the working files or specification, then **Re-scan**. An existing package name requires a new revision; a changed source requires another scan. Keep an unfinished delivery folder and a recovery store distinct from the final published output.

### Where to read more

The application's built-in Help contains detailed dialog and shortcut descriptions. The repository keeps its source at `help/src/hh/salamand/`, with plugin Help under each plugin's `help/` directory. [README.md](README.md) summarizes current features and [handoff-spec.md](handoff-spec.md) provides the Delivery Handoff specification and implementation reference. This manual covers how to use the visible workflows; plugin help and the actual installed build determine the precise controls and available codecs or devices on your machine.
