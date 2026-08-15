# Legacy Win32 API inventory and modernization guide

## Scope and reading this inventory

This is a source-level inventory of Win32 APIs that are either deprecated/replaced
by Microsoft or represent a compatibility-era interface with a material modern
replacement.  It covers the `src` product tree as checked on 2026-08-15.  It
does **not** label ordinary retained Win32 APIs (for example `CreateWindowEx`,
`CreateFile`, GDI painting, `GetSystemMetrics` for non-DPI metrics, or
`SystemParametersInfo`) as deprecated merely because they are old.

Locations use `path:line` and refer to the call site at the time of this review.
Several APIs occur hundreds of times, especially the `lstr*` family.  For those
families, this document names the owners and the highest-risk sites, and the
repeatable inventory command below supplies the complete, current line list.
Bundled/vendor code is deliberately called out separately: update, fork, or
upstream-patch it rather than making an unreviewed mass edit in place.

### Implemented in this pass

* `src/codetbl.cpp` now obtains conversion-table sizes through `GetFileSizeEx`
  and explicitly rejects values that its DWORD parser cannot represent.
* `src/plugins/zip/selfextr/dialog.cpp` and `extended.cpp` now install and
  restore subclass procedures through `SetWindowLongPtr`; the self-extractor no
  longer narrows function pointers to `LONG` at these call sites.
* All active first-party `GetWindowLong`/`SetWindowLong` call sites now use the
  pointer-width `GetWindowLongPtr`/`SetWindowLongPtr` variants, including the
  Translator's explicit Unicode subclasses.
* `src/common/strutils.cpp` now provides `GetKnownFolderPathToAnsi`, which
  resolves a UTF-16 Known Folder and makes the one necessary bounded ANSI
  conversion at the existing application boundary. The core AppData callers in
  `drivelst.cpp`, `path_checking.cpp`, `salmoncl.cpp`, and `shiconov.cpp` use it.
* `src/mainwnd_messages.cpp` now obtains the Desktop notification PIDL with
  `SHGetKnownFolderIDList(FOLDERID_Desktop)` and releases its COM allocation.
* The Folders plug-in now starts its Desktop namespace PIDL with
  `SHGetKnownFolderIDList(FOLDERID_Desktop)`.
* `src/async_copy.cpp` now routes Recycle Bin delete requests through an owned
  STA `IFileOperation` executor rather than calling `SHFileOperation` from the
  copy worker.
* `src/shellib.cpp` now uses `IFileOpenDialog` for generic and network-rooted
  folder selection, eliminating its CSIDL/PIDL browser callback.
* The core Shell-folder resolver now obtains Computer and Network namespace
  PIDLs from `FOLDERID_ComputerFolder` and `FOLDERID_NetworkFolder`.
* The core special-folder commands now map their fixed CSIDL menu inputs to
  explicit `FOLDERID_*` values before opening the namespace PIDL.
* SalOpen now resolves its Computer and Network namespace roots through the
  matching `FOLDERID_*` PIDLs.
* The CRT-free SFX7Zip installer now maps every former setup CSIDL to its
  `FOLDERID_*` equivalent and converts the returned known-folder path to UTF-8.
* File Comparison now uses `SetFilePointerEx` for both its text reader and
  QWORD cache seeks; PictView obtains scroll ranges through `GetScrollInfo`.
* Core Recycle Bin deletion in `src/fileswindow_delete.cpp` now queues UTF-8
  paths as `IShellItem` instances through `IFileOperation`, preserving Shell
  undo and confirmation behavior without a double-NUL `SHFILEOPSTRUCT` list.
* `src/msgbox.cpp` now appends clipboard-export fragments with a bounded helper,
  including the normalized message body and button separators.
* `src/dialogs_file_ops.cpp` now formats beta-expiration dates through the shared
  locale-name formatter and bounds the resulting UTF-8 dialog text.
* DemoView, File Compare's CRT-free remote helper, and shared plug-in handle
  diagnostics now use bounded formatting in their remaining active paths.
* The private token-group, threaded-message, and Folders plug-in selection
  buffers now use the process heap; clipboard and Shell/API-owned allocations
  retain their required legacy deallocators.
* Core trace stream growth, thread-cache storage, and crash-dialog message
  copies now use `HeapAlloc`/`HeapReAlloc`/`HeapFree`, retaining the existing
  synchronous lifetime until the crash dialog thread returns.
* `src/packac.cpp` now stores executable-inspection tables, search scratch
  paths, posted status text, and auto-configuration drive lists on the process
  heap, with the receiving window releasing its posted status ownership.
* `src/shexreg.c` now builds Shell-extension registry keys and descriptions
  through CRT-free bounded helpers in both the application and `/NODEFAULTLIB`
  extension builds, failing cleanly instead of truncating a registration path.
* `src/olespy.cpp` now resolves diagnostic filesystem PIDLs through
  `IShellItem::GetDisplayName(SIGDN_FILESYSPATH)` and an explicit ANSI boundary.
* ZIP's four remaining SFX-language labels now convert their stored `LANGID` to
  a locale name and obtain the label with `GetLocaleInfoEx`.
* The standalone shell extension now stores its COM objects, token-query data,
  and temporary logging ACL on the process heap; the SID retains its required
  `FreeSid` deallocator and the ACL is released after `CreateMutex` consumes it.
* The standalone shell extension now writes its fixed-width log timestamp
  directly and uses fixed diagnostic messages, eliminating its active
  `wsprintf` calls without adding a CRT formatting dependency.
* The shared plug-in trace stream and crash-message copies now use the process
  heap, so every plug-in that links `src/plugins/shared/dbg.cpp` avoids private
  `GlobalAlloc` ownership.
* The core and Salmon security-attribute helpers now keep their temporary ACLs
  and token-information buffers on the process heap. Windows-allocated SID
  strings remain paired with `LocalFree`.
* The Shell-extension registry configuration list in `src/shexreg.c` now uses
  process-heap nodes across creation and deletion rather than private
  `GlobalAlloc` handles.
* The dynamic tree-property dialog template in `src/common/sheets.cpp` now has
  a bounded process-heap lifetime through its synchronous modal creation rather
  than using a private `HGLOBAL`.
* FTP's disk-space worker and active-listen wait-window path now use
  `CMonotonicClock` time points and checked delay narrowing, removing their
  32-bit tick-wrap arithmetic.
* File Compare's binary-comparison progress throttles now use
  `CMonotonicClock`, so long comparisons continue to refresh correctly past a
  32-bit tick wrap.
* IEViewer's close-all-windows wait loop now uses a 64-bit monotonic duration
  while retaining its existing finite/infinite timeout and 50 ms poll behavior.
* PictView's save-progress callback now uses 64-bit monotonic samples for its
  UI refresh and cancellation-message throttles.
* Nethood's cache standby deadline is now a 64-bit monotonic timestamp, so
  deferred re-enumeration remains correct after long system uptimes.
* Automation's script-list refresh gate now tracks its private five-second
  interval with `CMonotonicClock` rather than a wrapping tick count.
* ZIP's self-extractor progress dialog now uses a 64-bit monotonic redraw
  throttle, preserving its existing forced-refresh rule.
* PictView's fullscreen cursor-hide delay now stores the last mouse movement as
  an internal 64-bit monotonic timestamp.
* Renamer's synchronous source-reload preview delay now uses a private 64-bit
  monotonic timestamp while it pumps the dialog message queue.
* Renamer's modal operation-progress dialog now uses a 64-bit monotonic refresh
  deadline and retains the original strict 100 ms throttle boundary.
* Core automatic-retry jitter now draws its dephasing sample from the 64-bit
  monotonic clock, avoiding the former 32-bit tick wrap.
* Collision-checked temporary names and cache keys in the core, Salmon, FTP, and ZIP
  plug-in now fold a 64-bit monotonic sample instead of restarting their seeds
  on the 32-bit tick cycle.
* The SFX7Zip launch grace period and Undelete's trace-only snapshot duration
  now calculate elapsed time with non-wrapping 64-bit clocks.
* FTP's local file-close wait now tracks its deadline with `CMonotonicClock`
  and narrows only when passing a remaining delay to `WaitForSingleObject`.
* ZIP's CRT-free self-extractor now constructs its password-dialog title through
  a bounded local append helper instead of unbounded `lstrcpy`/`lstrcat` calls.
* The core crash reporter now copies module-version text through an explicit
  terminating bounded helper instead of `lstrcpyn`.
* DiskMap's private directory-enumeration statistics throttle now uses a
  64-bit monotonic timestamp.
* The core crash report now displays full 64-bit system uptime instead of a
  duration that wraps after 49.7 days.
* Core operation-journal and reconciliation-report filenames now fold a 64-bit
  monotonic sample into their established fixed-width naming format.
* FTP's operation-list double-click synthesizer now stores an internal 64-bit
  monotonic click timestamp.
* FTP's listing wait window now calculates its private elapsed-time display from
  a 64-bit monotonic timestamp while preserving whole-second carry.
* FTP's filesystem path-change optimization now uses an internal 64-bit
  monotonic timestamp across its path retrieval and follow-up change flow.
* Core operation-plan IDs, Jump List descriptions, and empty-panel labels now
  use bounded string operations with explicit failure or truncation policies.
* Core static-text tooltip exports now use their fixed protocol capacity through
  a bounded copy and fall back to an empty label when a tooltip cannot fit.
* File Comparison's compact split-position tooltip now rejects a label that
  exceeds its fixed ten-character presentation buffer.
* DemoPlug's fixed-capacity tooltip reply now uses a bounded copy, retaining an
  empty tooltip if the host request is invalid or cannot fit.
* The core debug heap now rejects an overlong module path rather than keeping a
  truncated name that could misidentify a module in its leak report.
* CheckVer initializes its legacy common-dialog filename buffer with a bounded
  copy rather than the unbounded `lstrcpy` helper.
* Core controls now bound both direct and toolbar-protocol tooltip replies at
  `TOOLTIP_TEXT_MAX`, clearing a reply that cannot fit.
* The Find dialog's localized toolbar tooltip now applies the same fixed-capacity
  protocol check before formatting it for display.
* Status-window tooltip replies now share a bounded helper for every fixed
  protocol label, including optional throbber and security text.
* The plug-ins toolbar now bounds the plug-in-name tooltip against the shared
  protocol buffer instead of using an unbounded copy.
* User-menu toolbar tooltips now apply the shared fixed-buffer policy to both
  expanded commands and configured item labels.
* Automation's Abort palette now bounds its localized toolbar-tooltip reply and
  leaves an oversized label empty.
* Database Viewer's localized toolbar-tooltip reply now uses the same bounded
  shared-buffer contract.
* Media Viewer, Demo Viewer, and PictView toolbar tooltip replies—including
  PictView's histogram labels—now use bounded copies at the shared capacity.
* DemoPlug's viewer toolbar now uses the same fixed-capacity localized-tooltip
  reply, completing the first-party toolbar-handler sweep.
* Code-table conversion discovery, the parser-broker sibling executable path,
  and copy diagnostics now use checked bounded copies for their local buffers.
* The Shell permissions-page fallback now bounds its fixed `CMINVOKECOMMANDINFO`
  parameter buffer before invoking the shell context menu.
* Salmon's child-process PATH extension now uses one bounded formatting call
  rather than separate legacy copy and concatenate operations.
* The private release-diagnostics ring now records full 64-bit monotonic ticks
  and bounds its fixed snapshot labels while exporting reports.
* Directory and archive confirmation flows now bound their fixed localized
  label, template, and error-title buffers rather than truncating resources.
* Core main and bottom toolbars now bound localized tooltip replies; the main
  toolbar also bounds its dynamic Paste-as-change-directory annotation.
* The core view-template API now rejects oversized standard or plug-in column
  metadata instead of silently truncating fixed column names and descriptions.
* Core mask-group assignment now uses a bounded copy that preserves an empty
  valid mask if an internal size invariant is violated.
* Core startup, crash, and module-list diagnostics now use 64-bit monotonic
  timestamps; the one legacy `MSG.time` display retains its required 32-bit
  comparison projection.
* Asynchronous-copy collision-retried temporary-name seeds now fold the
  existing 64-bit monotonic clock into their unchanged 12-bit, 10 ms format.
* Registry Editor's find worker and result-list refresh deadlines now use
  private 64-bit monotonic timestamps, preserving their former strict gates.
* Renamer preview keeps its intentional bounded filename prefix through a
  capacity-aware copy, while its dialog rejects a malformed oversized filter.
* PictView now bounds the text supplied to its fixed status-bar presentation
  buffer and clears a label that cannot fit.
* Registry Editor now bounds its key label, Regedit executable path, and export
  notification path while retaining the existing PATH-search fallback.
* DemoPlug's sample custom-column registration now uses bounded fixed-field
  metadata copies rather than unbounded `lstrcpy` calls.
* Database Viewer now bounds conversion-table menu labels before inserting them
  into its fixed presentation buffer.
* DiskMap's deferred focus-path handoff now rejects oversized paths rather than
  truncating a path before its later panel-switch command.
* Undelete's custom condition-column metadata now validates fixed SDK fields
  before registering the column with the host.
* Undelete's encrypted-file restore now builds source and destination paths
  atomically, refusing a path that exceeds its fixed operation buffer.
* Core shortcut formatting now enforces its public 50-byte result contract when
  appending localized modifier key names.
* Core language migration now validates the imported prior-version filename
  before storing it in the persistent fixed path setting.
* Core standard-column template construction now reserves its required second
  terminator and rejects oversized localized labels before registering metadata.
* Core View With menu construction now inserts only fully formatted fixed-buffer
  labels for internal, external, and plug-in viewers.
* Core Find-history item copies and display titles now reject overlong persisted
  search fields instead of carrying truncated criteria or labels.
* Core Find-result stale-item cleanup now tests only a completely constructed
  fixed-buffer path, retaining a result when its path cannot fit.
* Core Find drag, context-menu, and file-history paths now reject oversized
  directory/name combinations before handing them to Shell item creation.
* Core advanced-filter summary formatting now uses checked localized appends and
  clears the display on an incomplete fixed-buffer description.
* Core drive-menu construction now clears an oversized cached panel path rather
  than using a truncated value to choose the initially focused drive item.
* Core regional and view-template settings now use bounded copies for persisted
  paths, localized view modes, shortcut labels, and edited view names.
* CheckVer now formats network-error text with bounded APIs while retaining its
  existing clipped caller-buffer diagnostic behavior.
* CheckVer's filter display and version parser now use explicit bounded copies;
  beta-letter expansion no longer writes past its fixed version buffer.
* PictView's Unicode thumbnail-cache path now copies its known directory-prefix
  length directly, retaining the separator before the `Thumbs.db` leaf name.
* Media Viewer now bounds its current-file cache, opening-error text, and window
  title, avoiding a truncated file identity or oversized localized title.
* Folders now omits an oversized dynamic Shell column rather than registering
  its truncated name in the fixed host column-metadata contract.
* Core mask-name formatting now retains its documented clipped display result
  through an explicit bounded copy instead of `lstrcpyn`.
* Core directory comparison now rejects an oversized panel root before appending
  the recursive search suffix, rather than enumerating a truncated directory.
* The drive-bar prefix cache, password-manager staging buffer, and PE Viewer
  section-name output now use explicit bounded or counted copies instead of the
  legacy `lstr*` family.
* Demo Viewer's thread file identity and common-dialog setup now validate their
  fixed-capacity strings, avoiding truncated file paths and malformed filters.
* DemoPlug's viewer now applies the same complete-path and valid-filter checks
  as Demo Viewer, keeping the SDK examples behaviorally aligned.
* DemoPlug's filesystem example now validates its connect-path handoff and
  icon-lookup filename suffix before constructing a full path.
* Core Attributes dialogs now express their fixed display and edit-control
  truncation limits through bounded copies instead of `lstrcpyn`.
* Core Drive Info now expresses its required `MAX_PATH` volume-root boundary
  through bounded copies while retaining the limits of its downstream volume APIs.
* Core critical-shutdown waits now measure their five-second deadline with the
  64-bit monotonic clock instead of wrap-prone `GetTickCount` arithmetic.
* UnCAB now bounds the localized error prefixes it extends with `FormatMessage`
  and validates its extraction progress text before displaying a filename.
* UnCAB now builds its dynamically allocated archive-entry names from measured
  components rather than using legacy `lstrcpy` calls.
* UnCAB now validates its fixed manifest destination directory before appending
  a separator, reporting its existing too-long-name error instead of truncating.
* UnCAB now rejects an archive source that cannot fit its fixed retry/error
  context field instead of retaining a truncated filename.
* Media Viewer's Ogg, MP4, MP3, and WAV parsers now bound their duration and
  codec presentation labels instead of copying via legacy `lstrcpy`.
* UnISO now copies ISO directory identifiers by their on-disk field length and
  terminates them locally instead of using `lstrcpyn`.
* UnRAR now copies an archive-entry name into its already-sized metadata
  allocation by the measured character count instead of `lstrcpy`.
* UnLHA now rejects oversized single-file archive and target paths before
  stripping or appending them for extraction, and bounds unpack-progress text.
* UnARJ now builds dynamically allocated archive-entry names from measured
  components instead of using legacy `lstrcpy` calls.
* UnCHM now validates the module path and bounds replacement of its companion
  library leaf before loading `chmlib.dll`, and skips extraction when source or
  target paths cannot fit completely.
* IE Viewer now validates startup navigation filenames and uses bounded URL and
  Markdown window-title formatting instead of legacy `lstr*` calls.
* NetHood now requires complete paths before probing `desktop.ini` or
  `target.lnk` during network-location discovery.
* PAK now copies dynamically allocated deletion metadata by the source's measured
  byte length and reuses its measured parent-marker length, removing its legacy
  `lstr*` usage while retaining exact ownership.
* MMViewer's WMA parser now bounds localized boolean labels in its fixed metadata
  presentation buffer and reports an insufficient-buffer HRESULT when needed.
* Split/Combine now copies the focused filename's known extension prefix by its
  measured length and terminates it explicitly instead of using `lstrcpyn`.
* Registry Editor now expresses its intentional status-bar clipping through
  `StringCchCopyNW`, rejects an oversized common-dialog filter, and uses a CRT
  length for the bounded registry-value byte count.
* Automation, CheckVer, UnOLE, and Media Viewer now use CRT-measured lengths for
  their terminated local strings; UnMIME retains its documented parser-field
  clipping through `StringCchCopyNA`.
* PictView now avoids partial crash-report metadata, panel-focus paths, EXIF
  locale filenames, and viewer-thread requests by requiring each fixed-buffer
  value to fit completely.
* Core list rendering, executable-extension matching, and mask parsing now use
  CRT string lengths for their already-terminated local text.
* File Compare and Renamer now use CRT string lengths when appending system
  diagnostics to their terminated local error buffers.
* Core tooltips, toolbar items, and execute-field caret placement now use CRT
  lengths for their terminated in-memory text.
* UnARJ now copies owned archive-entry names by their measured allocation,
  validates single-file extraction names and first-volume normalization paths,
  validates all extraction path builders, and bounds localized prefixes before
  appending system error descriptions.
* UnRAR now measures terminated archive names and roots through the CRT, rejects
  a single-file leaf name that cannot fit its fixed extraction buffer, and bounds
  retry diagnostics and SDK-supplied header names before continuing.
* PAK SPL now uses exact archive-entry allocations, complete extraction/packing
  paths, and bounded localized retry prefixes instead of active `lstr*` calls.
* ZIP self-extractor settings now use CRT lengths for parser fields and emitted
  setting bytes, and require a complete zip2sfx base directory before resolving
  a relative package path.
* The ZIP `zip2sfx` tool now derives self-extractor header offsets and serialized
  string sizes through a CRT helper rather than `lstrlen`.
* Core file execution now uses CRT lengths for view-template and Unicode display
  text; its intentional common-file-type clipping is expressed with `StringCchCopyNA`.
* ZIP's SFX dialogs now use CRT lengths and checked fixed-field copies for
  dialog, menu, and settings data; its About text retains explicit display clipping.
* ZIP archive creation now centralizes terminated SFX and archive-name length
  calculation through a CRT helper while retaining its integer serialized layout.
* ZIP archive creation now uses measured copies for exact dynamic entry names,
  rejects an overlong fixed executable path, and keeps progress text clipping explicit.
* ZIP defaults and archive deletion now use checked SFX-field copies and
  measured root-entry construction instead of active `lstr*` calls.
* Translator now builds temporary and backup target paths through a checked
  extension-replacement helper, rejecting an unrepresentable target identity.
* Translator's recent-project list now rejects oversized paths, uses checked
  fixed-slot copies, and stops removal shifts before the final array boundary.
* Translator's MUI package discovery now checks every root, wildcard, and
  selected-package path before probing or loading the corresponding resource.
* Translator now uses counted checklist excerpts and checked string, identifier,
  and process-path copies, rejecting destinations that cannot hold the full value.
* Translator now uses CRT lengths for terminated menu, dialog, clipboard, and
  version-resource text, with checked fixed font and resource-query fields.
* Translator's resource-header parser now copies counted semantic tokens exactly,
  clips only display excerpts explicitly, and rejects an oversized editor packet path.
* Translator's inline SLG defaults now use fixed-field capacities, and menu
  template names use their measured dynamic allocation rather than legacy copies.
* Translator's tree and text-list views now retain explicit bounded display text,
  with submenu indentation constrained before copying the remaining caption space.
* Translator's SalMenu parser now accepts template, dialog, control, and string
  identifiers only when each counted token fits its complete parser field.
* Translator's frame now uses checked command-line, project, import/export, and
  README path fields; oversized startup inputs no longer become partial actions.
* Core packer configuration now copies a persisted extension set with its fixed
  capacity before normalization, rejecting an incomplete registry value.
* The shared wide-string duplication helper now uses CRT-measured allocation
  length rather than the legacy Win32 length API.
* Core password recovery now securely erases its temporary scrambled-password
  allocation, including the terminator, without a legacy length call.
* Core Viewer startup now requires a full worker and file path, while its optional
  caption retains explicit fixed-field display clipping.
* Core temporary-directory cleanup and focus actions now require each discovered
  suffix to fit the remaining path buffer before acting on it.
* The shared Messages title setters now preserve their fixed presentation limit
  explicitly before converting between ANSI and Unicode storage.
* The shared Sheets Unicode bridge now uses explicit bounded display text, while
  dynamically allocated dialog titles use their measured full capacity.
* The shared bounded append helper now validates a terminated destination and
  expresses its existing remaining-capacity clipping through StrSafe APIs.
* Shared WinLib error labels now use generic StrSafe bounded copies, preserving
  their fixed presentation capacity in ANSI and Unicode configurations.
* The shared thread-owner launch record now stores its debugger-only worker name
  with an explicit bounded diagnostic-field copy.
* Core Quick Search now copies its matched filename prefix with an explicit
  caret-state display limit instead of relying on legacy prefix-copy semantics.
* Core hyperlink actions now require a complete shell target, and the animation
  tooltip now uses its documented message-buffer capacity explicitly.
* Core icon association lookup now rejects oversized counted registry keys before
  suffix probing; the folder type remains an explicit bounded display label.
* Core Message Box alias records now require a complete parser buffer, while
  button-width measurement retains explicit clipped preview text.
* Core packer and unpacker configuration now requires complete persisted
  extension sets before compatibility normalization.
* Core toolbar customization now derives exact owned-label allocation sizes from
  CRT lengths rather than legacy Win32 string-length calls.
* Core hot-path and user-menu toolbar captions now retain explicit bounded
  presentation copies before accelerator formatting.
* Core user-menu drag/drop now uses CRT wide filename measurement, and its compact
  toolbar captions retain an explicit bounded presentation copy.
* Core panel UTF-8 rendering and measurement now derive their GDI text length
  through CRT Unicode lengths after conversion.
* Core icon-thread submissions now require a complete queued file path and use
  the established zero request ID when that identity cannot fit.
* Core operation-journal diagnostics now bound their fallback identity, and
  transactional sibling checks require complete temporary and target directories.
* Shared UTF-8 GUI GDI wrappers now derive converted Unicode text lengths through
  CRT routines instead of legacy Win32 length calls.
* Core version-resource queries now require complete tokenization paths and use
  explicit bounded Unicode output copies for caller-sized display fields.
* Core PackAC worker-status trimming now handles empty text safely, while
  executable-extension matching uses CRT-measured filename lengths.
* Core shell overlay discovery now retains registry-derived handler names and
  descriptions through explicit bounded discovery/UI record copies.
* Core Viewer captions now retain explicit compact display text, while its
  file-change notification requires a complete containing directory path.
* Shared operation-worker paths and completion correlation IDs now require full
  fixed-field copies, clearing an unrepresentable identity rather than truncating it.
* Core code-table labels and accelerator-stripped comparison text now use
  explicit bounded configuration/display buffers.
* Core language selection now uses checked module-name and search-pattern fields,
  preventing an incomplete language-file identity from being selected or probed.
* Core hot-path getters now use explicit caller-sized bounded output fields and
  CRT lengths for their dynamically owned names and paths.
* TServer font restoration now copies its persisted `LOGFONT` face name through
  an explicit bounded field rather than a legacy Win32 string helper.
* Core column rendering now uses CRT Unicode/text lengths and an explicit bounded
  Shell-path display field.
* Core column clipboard path composition now uses an explicit remaining-capacity
  copy instead of a legacy prefix helper.
* Core panel message handling now requires complete buffered operation, focus,
  refresh, and file-enumeration path identities.
* Core directory reading now uses checked path fields for queued focus, disk
  enumeration, plugin enumeration, and archive-change notification.
* Core popup-menu creation now uses CRT menu-label lengths and explicit
  caller/template display-buffer limits.
* Core delete confirmation and panel-navigation paths now use explicit bounded
  label, caption, reparse-path, and remaining-capacity composition fields.
* Core shared-folder discovery now rejects incomplete local and UNC path
  identities instead of matching or returning truncated share paths.
* Core archiver GUI helpers now make their fixed tooltip and subject-display
  capacities explicit while preserving the plug-in facade's buffer contract.
* Core OLE spy diagnostics and `STRRET` compatibility output now use explicit
  fixed-field capacities while retaining their existing clipped-result behavior.
* Core call-stack diagnostics now use CRT text lengths and explicit fixed-record
  or complete-sidecar path fields.
* Core viewer file enumeration and inaccessible-path fallback now require
  complete request, result, and configured path identities.
* Core Find MD5 checks now require a complete file path; its searching-status
  display uses explicit base, append, and caller-output field limits.
* The shared wide-path adapter now uses CRT wide-string lengths for its owned,
  terminated display, duplicate, and extended-prefix paths.
* Core mount-point and volume-GUID resolution now requires complete source,
  root, and caller-output path identities.
* Core archive unpack masks now use an explicit display limit, and archive
  change notifications skip incomplete path comparisons.
* Core Find dialog named-search and log fields now use explicit display limits;
  Find log focus requires a complete containing path.
* Core toolbar button definitions now use explicit tooltip, persisted-layout,
  serialization-delimiter, and resource-row field behavior.
* Core panel list-box input now uses CRT lengths for its owned column labels and
  UTF-8-to-wide display text.
* Core main-window messages now require complete queued notification and
  shared-memory paste paths while explicitly clipping the title-bar prefix.
* Core Salmon helper now uses CRT path lengths and requires complete shared
  bug-report and child executable paths.
* Core plug-in utility bridge now makes command and source-description clipping
  explicit, retains complete mask output, and rejects incomplete help-file names.
* Shared tracing now uses explicit trace-path, fixed crash-dialog, and exact
  dynamic diagnostic-message field capacities.
* Core general configuration now uses CRT lengths and explicit template-name,
  mask backup, hot-path label, and mask-editor field limits.
* Core editor input now uses CRT lengths, preserves complete plug-in command and
  drop-path identities, and makes drag-insert text clipping explicit.
* Core Find results now use CRT text lengths and a shared complete-path helper
  for selection comparison and asynchronous file-name enumeration.
* Core path checking now rejects incomplete worker, fallback, policy, and
  AppData path identities while using CRT registry-value lengths.
* Core panel initialization now uses explicit directory-line mask bounds, CRT
  extension lengths, and bounded cached Windows-directory compatibility prefixes.
* Core safe-file retries now return complete-or-empty skipped paths through one
  helper and make DOS-workaround/error-dialog fields explicit.
* Core shared-library clipboard paste now requires complete source, archive,
  internal, target, and temporary-directory path identities.
* Core ZIP general API now makes error-text clipping explicit and requires
  complete FS-name, focus, and disk-operation working path identities.
* Core truncated-string support now uses CRT lengths and explicit view-template,
  history-open, clipboard, and SalOpen shared-memory field semantics.
* Core panel configuration now uses explicit disconnected/progress display limits
  and requires complete removable-drive readiness paths.
* Core viewer configuration now uses explicit presentation limits for plugin and
  mask labels, while plugin and fallback-path selection identities are complete
  or empty.
* SalOpen shell navigation now uses CRT lengths and accepts only paths that fit
  its fixed shell-parser buffers before it forms a parent-directory identity.
* Core shell helpers now publish drag/drop target paths only after complete IPC
  copies, keep NetHood sidecar paths complete, and make bounded name outputs
  explicit.
* Core HTML Help resolution now uses a complete-or-fail copy helper for
  localized directories, fallback folders, and CHM file identities.
* Core panel navigation now requires complete redirector, reparse-point, focus,
  and requested-directory identities while retaining bounded Win32 file names.
* Core plugin integration now uses explicit menu and registry presentation
  limits, preserves filesystem-name suffix headroom, and returns complete
  NetHood filesystem identities.
* Core configuration loading now distinguishes bounded diagnostics and toolbar
  migration text from complete registry-key and restored default-directory
  identities.
* Core operation execution now uses explicit bounded progress and error labels
  while retaining complete worker correlation identifiers.
* Core startup now uses explicit fixed Hot Path, tray-tooltip, and title-bar
  presentation limits instead of legacy string APIs.
* Core status-window rendering now uses CRT Unicode lengths and explicit
  callback, hot-track, clipboard-path, and navigation-buffer limits.
* Core shared drag/drop objects now retain complete-or-empty real, filesystem,
  and temporary-directory identities for clipboard and cleanup operations.
* Core plugin filesystem encapsulation now retains selected filesystem names as
  complete-or-empty reopen identities.
* Core shell support now uses bounded search/error text and complete deferred
  drag/drop, archive, and fake-directory shared-memory identities.
* Core Find UI now uses bounded dialog/result fields and complete search and
  comparison paths, with CRT lengths for edit-line selection handling.
* Core asynchronous copy now uses complete metadata and transactional-path
  identities, bounded error text, and CRT Unicode/name lengths.
* Core plugin loading now uses explicit extension/menu/hotkey presentation
  limits and bounded language-module path construction.
* Core file actions now use bounded prompt/name fields and modern path,
  command-line, focus, temporary-name, and redirector copies.
* Core command handling now uses bounded caption and toolbar-layout fields,
  CRT name lengths, and complete-or-empty comparison, selection, and deferred
  focus path identities.
* Core startup now uses CRT lengths and explicit bounded diagnostics, title
  fields, command-line fallback paths, configuration paths, and deferred
  Notepad targets.
* Core panel execution now retains complete file, archive, and plug-in
  filesystem identities across re-entrant callbacks while bounding diagnostics
  and restore-focus presentation fields.
* Core drive-list handling now keeps complete connection, removable-drive, and
  plug-in filesystem identities across worker/UI handoffs, while limiting
  credential and tooltip/display fields to their presentation buffers.
* Core path utilities now use CRT lengths and explicit bounded dialog/history
  labels, while retaining complete traversal, archive, and stored navigation
  identities before performing path operations.
* Core string/resource helpers now use bounded formatter, language, and
  configuration text fields plus StrSafe path and counted UTF-16 reparse-point
  copies in filesystem and volume resolution.
* Core panel operations now use complete planning/source identities and bounded
  captions, while preserving the duplicate-name generator's MAX_PATH overflow
  sentinel through explicit remaining-capacity copies.
* Core menu queuing, DB Viewer, FTP's lexer, IE Viewer, PE Viewer, and Registry
  Editor now use CRT lengths or explicit counted-field copies in place of legacy
  `lstr*` helpers.
* FTP's listing-wait time estimate now expresses its established 20-byte display
  clipping through `StringCchCopyNA`.
* Core operation-progress dialogs now skip incomplete path notifications while
  retaining their bounded caption and status-display cache limits explicitly.
* DB Viewer now copies fixed-width DBF date fields by length and bounds its
  parser metadata, coding labels, and localized Boolean labels without legacy
  `lstr*` calls.
* DB Viewer's DBF and CSV parsers now retain opened filenames in owned dynamic
  storage instead of truncating them to `MAX_PATH`.
* Checksum now clears an oversized save-name suggestion and suppresses a focus
  command whose shared path payload cannot fit.
* DemoPlug's control-example and file-system repaint throttles now use
  monotonic time; its sample tick display remains a deliberate 32-bit visual
  value only.
* Registry Editor's cancellation-prompt poll deadline now uses a monotonic
  time point and `DeadlineAfter`, avoiding signed wraparound comparisons.
* Undelete's restore/copy progress dialog now throttles repainting with an
  internal 64-bit monotonic timestamp.
* The shared plug-in `CWindowQueue` close wait now calculates elapsed and
  remaining time with `CMonotonicClock`, benefiting every plug-in that uses it.

## Priority order

1. **Correctness and security:** remove pointer truncation, unbounded strings,
   obsolete file-position APIs, and 32-bit tick arithmetic.
2. **Shell behavior:** migrate deprecated CSIDL and `SHFileOperation` use before
   adding further Shell features.
3. **User experience:** use modern file/folder dialogs and per-monitor DPI APIs.
4. **Maintenance:** remove legacy allocation wrappers only where Windows API
   ownership rules do not require `GlobalFree` or `LocalFree`.

## Deprecated or replaced APIs

### `GetVersionEx` -- deprecated and manifest-sensitive

| Locations | Current purpose | Recommended change |
|---|---|---|
| `src/app_entry.cpp`, `src/bugreprt.cpp`, and `src/sfx7zip/install.c` | Compatibility-mode warning, diagnostic report, and retired pre-Windows-7 launch gates. | **Migrated.** Startup no longer makes a manifest-sensitive version-lie decision; the diagnostic and Automation use the existing `VerifyVersionInfo`-based compatibility helper, and the Windows-7-baseline installer removes its redundant version gates. |
| `src/plugins/automation/salamanderaut.cpp:250` | Automation version property. | Uses the existing `VerifyVersionInfo`-based compatibility helper, not `GetVersionEx`. Do not introduce version checks for feature selection. |
| `src/plugins/undelete/library/os.cpp` | Windows 9x/2000/Vista compatibility flags. | **Migrated.** The Windows 7+ baseline makes those branches fixed invariants, so version probing is removed. |
| `src/plugins/winscp/core/Configuration.cpp:567` | Bundled WinSCP OS check. | Vendor upgrade or capability-check migration during the next WinSCP upgrade. |
| `src/plugins/portables/wtl/atlapp.h:265,281` | Bundled WTL implementation. | Vendor upgrade; do not patch generated/bundled framework code unless the upgrade is blocked. |

`GetVersionEx` is explicitly deprecated. Dynamic lookup only suppresses a compiler
warning; it does not fix version virtualization or the dependency on a legacy
contract. Active first-party code no longer calls it.

### `SHGetFolderPath` / CSIDL -- replaced by Known Folders

| Locations | Current purpose | Recommended change |
|---|---|---|
| `src/common/strutils.cpp` and `src/{drivelst.cpp,path_checking.cpp,salmoncl.cpp,shiconov.cpp}` | Dropbox, application-data, bug-report, icon-cache, and profile locations. | **Migrated.** `GetKnownFolderPathToAnsi` calls `SHGetKnownFolderPath` with the matching `FOLDERID_*` and releases the allocated path. Callers retain their bounded ANSI path contract; a later path-type migration can remove that final conversion and `MAX_PATH` limit. |
| `src/plugins/winscp/core/Common.cpp:300` | Bundled WinSCP folder lookup. | Vendor upgrade or an isolated WinSCP compatibility patch. |

`src/plugins/nethood/cache.cpp` is migrated from `SHGetSpecialFolderPath` to
`SHGetKnownFolderPath(FOLDERID_NetHood, ...)`; it keeps an explicit `MAX_PATH`
ANSI boundary until the plug-in's path type is upgraded. Related older shell-path
calls in product code are migrated; bundled WinSCP still uses
`SHGetPathFromIDList`. `src/olespy.cpp` and `src/mainwnd_messages.cpp` resolve
filesystem PIDLs through `IShellItem::GetDisplayName` and a bounded output
conversion. Prefer `SHGetKnownFolderPath` for known folders and a Shell item
display-name lookup where a general file-system PIDL must be converted.

The Documents lookups in `src/plugins/{demoplug,ftp,pictview}` are migrated to
`SHGetKnownFolderPath(FOLDERID_Documents, ...)` and retain their respective
bounded UTF-8/TCHAR compatibility boundaries.
`src/shellib.cpp` likewise uses Known Folders for its Documents-or-Desktop
fallback without allocating special-folder PIDLs.

### `SHFileOperation` -- replaced by `IFileOperation`

| Locations | Current purpose | Recommended change |
|---|---|---|
| `src/fileswindow_delete.cpp` | Deletes selected panel items through the Recycle Bin. | **Migrated.** The UI STA creates `IFileOperation`, queues each UTF-8 path as an `IShellItem`, enables `FOF_ALLOWUNDO`, executes it, and observes Shell cancellation. |
| `src/finddlg2.cpp` | Deletes selected search results, optionally to Recycle Bin. | **Migrated.** The UI STA queues each UTF-8 result as an `IShellItem`, preserves `FOF_ALLOWUNDO` for Recycle Bin deletes, records failures, and observes Shell cancellation. |
| `src/path_utils.cpp` | Copies timestamp-associated files to multiple destinations. | **Migrated.** `IFileOperation` queues one source `IShellItem` and destination folder/name pair per file, preserving the old one-to-one mapping without `FOF_MULTIDESTFILES`. |
| `src/plugins/pictview/render1.cpp` | Viewer copy and delete commands. | **Migrated.** `IFileOperation` uses source and destination `IShellItem` instances, preserves Recycle Bin undo, and avoids the prior double-NUL `SHFILEOPSTRUCT` strings. |
| `src/async_copy.cpp` | Recycle Bin deletion in the background copy worker. | **Migrated.** A short-lived owned STA executes `IFileOperation` synchronously, preserving the worker's existing retry/error protocol without invoking the Shell API from its MTA worker. |
| `src/plugins/winscp/core/Common.cpp:1248` | Bundled WinSCP operation. | Update vendor code or isolate the compatibility layer. |

`IFileOperation` requires an STA.  Background callers must marshal each operation
to a dedicated STA or the UI STA; do not invoke it directly from an MTA worker.

### `SHBrowseForFolder` -- legacy folder picker

| Locations | Current purpose | Recommended change |
|---|---|---|
| `src/translator/translator.cpp` | Chooses a translation-project directory. | **Migrated.** `IFileDialog` uses `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`, an initial `IShellItem`, and `SIGDN_FILESYSPATH`; no PIDL is created. |
| `src/plugins/zip/selfextr/dialog.cpp` | Chooses extraction destination. | **Migrated.** `IFileDialog` preserves the initial destination, returns an `IShellItem` filesystem path, and requires the project to use the product's Windows-7 header baseline. |
| `src/plugins/uncab/dialogs.cpp` | Chooses CAB extraction directory. | **Migrated.** `IFileDialog` returns an `IShellItem` filesystem path directly and preserves the plug-in's bounded ANSI dialog field. |
| `src/shellib.cpp` | Generic folder picker, including the Network-rooted mode. | **Migrated.** `IFileOpenDialog` uses `FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM`, accepts the existing UTF-8 initial directory, and starts the network-only mode at `FOLDERID_NetworkFolder`. |

### `GetOpenFileName` / `GetSaveFileName` -- legacy common-dialog surface

The old `OPENFILENAME` dialogs are still supported, but their fixed buffers,
hook procedures, and limited namespace support make them a modernization target.
Active application-owned callers include `src/execute.cpp:2152` and the PictView
save/open flows in `src/plugins/pictview/saveas.cpp` and `src/plugins/pictview/render1.cpp`.
Use `IFileOpenDialog`/`IFileSaveDialog` (`IFileDialog`) for new work.  Plan this
as a behavior-preserving migration: map filters to `COMDLG_FILTERSPEC`, map
initial folders to `SetFolder`, and replace dialog hooks with
`IFileDialogEvents`/`IFileDialogCustomize` only where required.

## 64-bit and large-value compatibility APIs

### `GetWindowLong` / `SetWindowLong` and `GetClassLong` / `SetClassLong`

These APIs return/store 32-bit `LONG` values.  They are unsafe for pointers,
window procedures, and extra data in 64-bit builds.  `GetWindowLongPtr` and
`SetWindowLongPtr` compile to the old calls in 32-bit builds, so they are the
portable replacement.

* **Critical pointer-truncation call sites:**
  `src/plugins/zip/selfextr/extended.cpp:410,447` and
  `src/plugins/zip/selfextr/dialog.cpp:54-55,107-108,149,191,853-855,926-928`
  previously subclassed controls through `GWL_WNDPROC` and cast `WNDPROC` to
  `LONG`. These sites are migrated to a `SetWindowLongPtr` helper in this pass.
  Prefer `SetWindowSubclass` for future new subclasses so comctl32 manages
  subclass lifetime.
* **Style/ID access:** all active first-party direct calls, including
  `src/file_enumeration.cpp`, `src/gui_controls.cpp`, `src/logo.cpp`,
  `src/msgbox.cpp`, `src/tserver/dialogs.cpp`, Translator, and the affected
  plug-ins, are now paired `GetWindowLongPtr`/`SetWindowLongPtr` calls with
  `LONG_PTR` storage where applicable. Call `SetWindowPos(...,
  SWP_FRAMECHANGED)` after a style change that affects the non-client frame.
* **Bundled WTL:** `src/plugins/portables/wtl/atlwinx.h:466-475` exposes
  `GetClassLong`/`SetClassLong`. Upgrade the WTL snapshot or use its pointer-width
  counterparts; do not edit the framework API piecemeal.
* **Resolved technical-debt marker:** `src/plugins/automation/abortpalette.cpp`
  now uses the pointer-width API; the prior x64 FIXME is no longer current.

### `GetFileSize` / `SetFilePointer` -- ambiguous 32-bit file offsets

`GetFileSize` and `SetFilePointer` combine a 32-bit result with an error sentinel,
and make error handling/large files unnecessarily fragile.  Examples include
`src/dialogs_config_panels.cpp:226`,
`src/plugins/filecomp/textio.cpp:121,536,584`, `src/plugins/ftp/operats5.cpp`,
`src/plugins/pak/spl/pak.cpp`, `src/plugins/uncab/uncab.cpp`, and
`src/plugins/unrar/unrar.cpp`.

`src/codetbl.cpp` was migrated in this pass. Its parser remains DWORD-based by
design, so the new helper fails a file larger than 4 GiB instead of silently
truncating the `GetFileSizeEx` result. `src/plugins/filecomp/textio.cpp` and
`filecache.cpp` now use `SetFilePointerEx`, preserving their `size_t`/`QWORD`
offsets without a 32-bit sentinel contract. `src/mainwnd_commands.cpp` also
uses `SetFilePointerEx` and `GetFileSizeEx` for generated file lists, rejecting
an oversized temporary clipboard file rather than narrowing its size.
`src/packac.cpp` uses a `SetFilePointerEx` helper for executable-header
inspection, preserving the signed PE-header offset and checking one Boolean
failure result. `src/dialogs_config_panels.cpp` now obtains the tip-file size
with `GetFileSizeEx` and rejects values that cannot fit its allocation size.
The core journal and SVG readers apply the same representability checks, and
the user-menu disk-cache entry records its generated batch file through the
existing 64-bit `SalGetFileSize` wrapper.
Viewer's before/after buffer loaders now use `SetFilePointerEx` for their
signed 64-bit resource offsets, removing sentinel checks and preserving the
existing read-error reporting flow.
The shell extension's diagnostic log writer now seeks to the log end through
`SetFilePointerEx`, avoiding the legacy API even on this best-effort append path.
SFX7Zip now uses `SetFilePointerEx` for PE-header traversal, archive-signature
rewinding, and archive-relative reads; its existing DWORD SFX-format offsets are
checked before converting them to the full-width OS seek position.
`src/safefile.cpp` now routes preallocation, safe seek/retry, and current-offset
recovery through `SalSetFilePointerEx`/`SalGetFileSizeEx`, retaining the error
code separately from the 64-bit value so an `INVALID_SET_FILE_POINTER` sentinel
cannot be mistaken for a valid offset.
The core desktop.ini reader, self-extractor parameter-file parser, and Automation
script loader now use `GetFileSizeEx`; each explicitly rejects a value that its
small fixed parser or `int` conversion boundary cannot represent.
Translator's whole-file loaders now share a `GetFileSizeEx` helper that rejects
files too large for their existing DWORD allocation and `ReadFile` boundaries.
Salmon's minidump-size fallback now compares the `GetFileSizeEx` result directly
against its 50 MiB policy threshold rather than combining 32-bit halves.
The SFX package builder now queries both embedded executables with
`GetFileSizeEx` and preserves its 64 KiB fixed-buffer limit before converting
the validated value to `DWORD`.
ZIP2SFX's settings loader and archive scanner now use `GetFileSizeEx`; the
scanner also uses `SetFilePointerEx` while explicitly retaining its DWORD ZIP
offset boundary and rejecting larger archives.
The ZIP2SFX builder now uses `SetFilePointerEx` for fixed-format SFX offsets,
the generated executable end position, and the archive-copy rewind, so each
seek has a separate Boolean failure result.
Its shared resource editor and ICO loader likewise use `SetFilePointerEx` after
their existing DWORD-format range checks, preserving file-format limits without
the legacy sentinel contract.
The SFX package generator uses `SetFilePointerEx` to move past its fixed header
and rewind before writing it, retaining the package layout while separating
success from the old `0xFFFFFFFF` error value.
ZIP's buffered reader and writer now centralize their QWORD seek positions in a
`SetFilePointerEx` helper, so cached reads, direct reads, output repositioning,
and flushes all preserve the complete archive offset.
The ZIP self-extractor now captures and restores its output retry position with
`SetFilePointerEx`, preserving a full-width offset instead of treating
`0xFFFFFFFF` as an unconditional failure.
Renamer's cross-volume copy resume and temporary command-output readers now
use `SetFilePointerEx`, retaining their `CQuadWord` retry positions and
checking temporary-stream seek failures before reading or allocating buffers.
The Registry library test tool now uses `GetFileSizeEx` and verifies room for
its terminating `WCHAR` before allocating its DWORD-sized parser input.
The PAK reader now uses `GetFileSizeEx` and rejects files beyond the archive
format's DWORD directory-offset range. Its DLL and SPL read/write retry paths
use `SetFilePointerEx`, retaining 64-bit retry positions where file handles can
outgrow the PAK input format's DWORD offsets.
FTP transfer setup, upload reads, and close-time cleanup now use a shared
`GetFileSizeEx`/`CQuadWord` adapter, preserving full 64-bit sizes and the
original error propagation for failed size queries.
Its resume truncation, overlap checking, buffered write/read, and final
end-of-file adjustment now share a `SetFilePointerEx`/`CQuadWord` helper, so
all transfer seeks preserve both offset halves and return an unambiguous result.
Both self-extractor archive mappers now use `GetFileSizeEx` and reject images
outside their unsigned-long offset representation before creating a map view.
Split/Combine now obtains 64-bit file sizes with `GetFileSizeEx`; progress keeps
the full `CQuadWord` total, while its intentionally small batch-file analyzer
rejects an unrepresentable or over-200 KiB input before allocation.
UnARJ now reads the archive size with `GetFileSizeEx` and reports files beyond
its DWORD parser-offset range as `ERROR_FILE_TOO_LARGE` through the existing
retry/error flow.
The self-extractor overwrite prompt now shows an existing destination's full
64-bit `GetFileSizeEx` value instead of truncating it to the legacy display
parameter.
Windows Mobile's local-to-CE copy fallback now uses `GetFileSizeEx` and rejects
sources beyond its DWORD CE transfer accounting boundary. The remaining
`CRAPI::GetFileSize` calls are RAPI/Windows CE compatibility methods, not local
Win32 file-size calls.
DiskMap's cushion-graphics loader now uses `GetFileSizeEx` and applies its
existing 1 MiB decoder-buffer limit before narrowing to `DWORD`.
Undelete's retrieval-pointer diagnostic and debug valid-data/sparse-file tests
now use `GetFileSizeEx` directly with their existing `LONGLONG` file offsets.
Its encrypted-backup restore rewind and QWORD sector reader now use
`SetFilePointerEx`, retaining full device positions and treating seek failure as
a distinct Boolean result.
UnISO's buffered-file implementation now obtains the underlying size through
`GetFileSizeEx` while preserving its DWORD-half plug-in interface for callers.
Its seek helper and buffered current-offset queries now use `SetFilePointerEx`.
The PAK and UnARJ retry helpers also use `SetFilePointerEx`, retaining complete
64-bit retry positions where output can grow beyond a DWORD and avoiding the
legacy API's ambiguous `0xFFFFFFFF` result.
UnCAB's CAB-relative seek bridge now uses `SetFilePointerEx`; it keeps the
Cabinet API's `long` result boundary while retaining full physical retry
positions for read and write failures.
UnMIME's parser now restores saved positions with `SetFilePointerEx` and leaves
its parser state unchanged if that seek fails.
UnISO's ISZ reader now uses `SetFilePointerEx` for header and block-table
seeks, verifying the requested DWORD-format offset without legacy ambiguity.
Its compressed and copied block readers now carry their existing 64-bit image
offsets through `SetFilePointerEx` instead of splitting them into legacy halves.
The DMG reader now uses `SetFilePointerEx` for end-relative footer probes,
plist seeks, and ADC block reads; malformed footer ranges are rejected before
they can underflow allocation sizes.
UnISO now has no direct `SetFilePointer` calls in its application-owned source;
all image-format seek paths use `SetFilePointerEx`.
UnRAR now uses `SetFilePointerEx` both for target seeks and for capturing the
full retry position before extraction writes, avoiding the legacy API's
ambiguous `0xFFFFFFFF` result while retaining the existing `CQuadWord` bridge.
TAR's decompressor offset setup and Debian subarchive traversal now use
`SetFilePointerEx`; the Debian bridge rejects physical offsets that its existing
DWORD `CArchive` interface cannot represent.

Use `GetFileSizeEx` and `SetFilePointerEx` with `LARGE_INTEGER`, checking their
Boolean result.  New core code should call the repository's existing wrappers
`SalGetFileSizeEx` and `SalSetFilePointerEx` declared at `src/consts.h:109-118`;
they make the 64-bit result/error contract explicit.  Do not replace a call with
`SetFilePointerEx` without auditing the surrounding `DWORD`/`CQuadWord` storage
and any on-disk-format limits.

Residual direct calls are limited to vendored 7-Zip, CHM, and SQLite code.
Update those third-party copies from a compatible upstream version (or carry a
minimal vendor patch with its provenance) rather than making ad-hoc API
substitutions. The remaining UnARJ and ZIP/DemoView matches are commented-out
recovery examples and should be removed when those obsolete paths are cleaned up.

### `GetDiskFreeSpace` -- legacy cluster/32-bit interface

The code has an intentional compatibility helper (`MyGetDiskFreeSpace`) and
already uses `GetDiskFreeSpaceEx` in the bug report at `src/bugreprt.cpp:2482-2504`.
Keep the low-level cluster query only where sector/cluster geometry is genuinely
needed.  For available/total/free capacity, migrate direct callers to
`GetDiskFreeSpaceEx` and 64-bit byte counts; expose that via the existing helper
instead of adding new `DWORD`-based call sites.

### `GetTickCount` -- 49.7-day wraparound

There are many `DWORD` timestamps throughout the core, FTP, PictView, and WinSCP
code (for example `src/app_entry.cpp:289,3969,4063,4125,4137,4184`,
`src/callstk.cpp:819`, `src/bugreprt.cpp:1711,2569,2603,2629`, and
`src/plugins/ftp/operats1.cpp:3683,3685`).  The repository now contains the
right migration primitive: `src/common/monotonic_time.h:8-62` wraps
`GetTickCount64` in named 64-bit time points and durations.

Use `CMonotonicClock::Now`, `Elapsed`, `HasElapsed`, and `HasReached` for new
or touched timing logic.  Convert stored time fields and arithmetic together;
do not widen only a local variable.  At a Win32 timer boundary, use
`RemainingWin32TimerDelay` so only the API delay remains a `DWORD`.
Windows Mobile's RAPI wait-and-dispatch loop and progress-dialog repaint
throttle now follow this pattern, so long-running message-pump timing no longer
depends on `GetTickCount` wraparound arithmetic.
DemoPlug's temporary-file wait-window throttle now keeps its batch start time
as a `CMonotonicTimePoint` and computes its remaining delay without wraparound.
FTP's disk-space worker and active-listen wait window now follow the same
pattern; the final conversion to the legacy wait-window `int` delay occurs only
after the elapsed duration has been checked against that bound.
File Compare's binary comparison worker also uses 64-bit time points for its
worker-local progress throttles, which have no external 32-bit timestamp
contract.
IEViewer's close-all-windows wait loop similarly retains its `DWORD` timeout
only as an input boundary while calculating elapsed and remaining time in 64
bits.
PictView's save-progress callback has no exported timestamp representation, so
its 100 ms UI refresh and 500 ms cancellation polling thresholds now use the
same monotonic type directly.
Nethood's cache standby list now stores an internal `CMonotonicTimePoint` while
retaining zero as its existing “not waiting” sentinel.
Automation's script-list cache now stores the last refresh as an internal
monotonic time point while preserving the legacy five-second refresh policy.
ZIP's self-extractor progress dialog also now calculates its 100 ms redraw
throttle with an internal monotonic timestamp.
DemoPlug's two private repaint throttles likewise use monotonic time; the only
intentional narrowing is the example dialog's human-readable tick formatting.
Registry Editor's cancellation prompt now uses a private monotonic deadline for
its 150 ms polling throttle.
Undelete's synchronous restore/copy progress dialog now uses a private
monotonic timestamp for its 50 ms repaint throttle.
The shared plug-in window-queue close wait now uses monotonic elapsed time and
only narrows the remaining delay at the `Sleep` API boundary.
PictView's fullscreen cursor-hide delay now retains its three-second policy
with a `CMonotonicTimePoint`, so prolonged fullscreen sessions cannot delay
cursor fading at a 32-bit tick wrap.
Renamer's source-reload operation now keeps its internal two-second preview
delay as a monotonic time point; the modal operation owns and pumps that state
on one UI thread.
The same plug-in's modal progress dialog now uses a monotonic deadline for its
100 ms refresh throttle, preserving the old strict comparison at the boundary.
The core automatic-retry policy now uses a 64-bit monotonic sample for its
bounded jitter seed, so retry clients remain dephased after long uptimes.
Temporary names and cache keys that retain fixed-width legacy key formats now
fold a 64-bit monotonic sample before probing for collisions. Their existing
collision loops remain the correctness mechanism, while long uptime no longer
restarts the initial seed every 49.7 days.
The SFX7Zip setup's bounded launch grace period now uses `GetTickCount64` in
its C-only target, and Undelete uses the shared monotonic helper for its
trace-only snapshot duration.
FTP's local file-close wait now keeps its elapsed and deadline calculations in
64 bits, leaving the existing `DWORD` timeout only at the Win32 wait boundary.
DiskMap's directory-enumeration statistics throttle now uses an internal
monotonic time point while retaining its strict 250 ms reporting cadence.
The crash reporter now takes its display-only system-uptime sample from
`GetTickCount64`, allowing diagnostic reports to represent long-running
systems accurately.
Operation-journal, reconciliation-report, and emergency-marker filenames now
retain their fixed-width formats while folding a 64-bit monotonic sample, so
their timestamp components do not repeat on the former tick-wrap cycle.
FTP's operation-list double-click synthesizer now measures its existing strict
system double-click interval with a private monotonic time point and clears the
matching location after producing a synthetic double-click.
FTP's listing wait window now calculates elapsed display time from a private
monotonic timestamp, retaining the existing whole-second update cadence and
sub-second carry between repaint calls.
FTP's filesystem path-change optimization now uses a monotonic timestamp for
its inclusive one-second handoff window, avoiding a false cache-path decision
when a long-running process crosses the legacy tick boundary.

## Compatibility-era APIs requiring bounded or ownership-aware replacements

### `lstrcpy`, `lstrcat`, `lstrcpyn`, `lstrlen`, `lstrcmp`, and `lstrcmpi`

The project uses this family extensively in the core and plug-ins (notably
`src/bugreprt.cpp`, `src/app_entry.cpp`, `src/async_copy.cpp`, `src/callstk.cpp`,
`src/path_utils.cpp`, `src/plugins/ftp/**`, `src/plugins/zip/**`, and
`src/plugins/pictview/**`).  `lstrcpy` and `lstrcat` have no destination length;
`lstrcpyn` can silently truncate, and the family preserves ANSI/TCHAR ambiguity.

Prioritize `src/bugreprt.cpp:539,841,1056-1305,1523-1862`, where repeated
append operations build diagnostic text, and `src/plugins/zip/selfextr/extended.cpp:416-420`,
where a fixed 200-byte title is built from variable strings.  Replace a complete
buffer-building operation, not one individual call, with one of:

* `StringCchCopy`/`StringCchCat`/`StringCchPrintf` with `_countof(destination)`
  and explicit failure handling for fixed buffers;
* `std::string`/`std::wstring` (or the repository's string/path type) followed
  by one checked boundary conversion; or
* a helper that returns the required length before allocating.

Keep `GlobalAlloc`/`GlobalFree` string payloads where clipboard/OLE ownership
requires an `HGLOBAL`; the safer string operation does not change that allocator
rule.
The ZIP self-extractor's password-dialog title now uses a CRT-free bounded
append helper for its fixed 200-byte buffer. On an oversized localized title it
falls back to the password label, never passing an unterminated buffer to the
window API.
The crash reporter's version-reader now makes its legacy truncation policy
explicit with a bounded helper, ensuring that even a very small caller buffer
is always terminated while crash diagnostics remain available.
Operation-plan capture now rejects an oversized correlation ID rather than
silently truncating an execution-bound identifier. Jump List descriptions keep
their intentional visible ellipsis through bounded copies, and an oversized
localized empty-panel label now fails closed to an empty UI string.
Core static-text tooltip export now uses the fixed `TOOLTIP_TEXT_MAX` protocol
capacity through a bounded copy and emits an empty label if an oversized
tooltip cannot be represented safely.
File Comparison applies the same explicit failure policy to its ten-character
split-position tooltip instead of silently truncating a value received through
`WM_SETTEXT`.
DemoPlug's sample control now bounds its fixed-capacity tooltip reply and keeps
the label empty if the host request cannot safely accept the text.
The core debug heap's module ledger now rejects an overlong path rather than
truncating the identifier later used to load symbols for a leak report.
CheckVer now bounds its default legacy common-dialog filename before supplying
the caller-owned buffer to `OPENFILENAME`.
Core control and toolbar tooltip replies now use the shared `TOOLTIP_TEXT_MAX`
protocol capacity, ensuring an oversized label is returned as an empty tooltip
rather than a partial string.
The Find dialog now uses that same capacity when it returns a localized toolbar
tooltip, before applying the existing tooltip-text formatting.
The status window now centralizes its fixed-capacity tooltip copies, including
the optional throbber and security labels, and clears any label that cannot fit.
The plug-ins toolbar now uses the same fixed tooltip-buffer policy for a plug-in
name, returning an empty tooltip rather than a truncated identifier.
User-menu toolbar tooltips now use that policy for either an expanded command
or the configured item name, avoiding silent shortening of user-configured text.
Automation's Abort palette now uses the same bounded reply for its localized
toolbar tooltip rather than copying it without a capacity check.
Database Viewer now applies that bounded reply policy before formatting a
localized toolbar tooltip for display.
Media Viewer, Demo Viewer, and PictView now apply it to their localized toolbar
tooltips; PictView's histogram labels share one bounded helper for every
channel-specific response.
DemoPlug's viewer now applies the same bounded tooltip reply, completing the
first-party `WM_USER_TBGETTOOLTIP` handlers identified by this inventory pass.
Code-table conversion discovery and parser-broker launch now check their
module-relative path appends, while copy diagnostics use a bounded attribute
prefix before composing their fixed output text.
The Shell permissions-page fallback now uses a bounded copy for its fixed
context-menu parameter buffer before invoking the `properties` verb.
Salmon's RTL PATH extension now builds the child-process environment value in a
single bounded formatting operation.
The private release-diagnostics ring now records full 64-bit monotonic uptime
and uses bounded snapshot labels, preserving its allocation-free diagnostics
across long system uptimes.
Directory and archive confirmation flows now reject an oversized localized
label, template, or error title into a valid empty fixed-buffer value instead
of silently truncating it.
Core main and bottom toolbars now bound their localized tooltip replies. The
main toolbar also bounds the dynamic Paste-as-change-directory annotation and
its tab-suffix preservation.
The core view-template API now rejects oversized standard or plug-in column
metadata before it is registered in fixed name and description fields.
Core mask-group assignment now uses a bounded copy consistent with the setter's
`MAX_GROUPMASK` invariant, retaining an empty valid mask if a corrupt source
violates that capacity.
Core startup, crash, and module-list diagnostics now retain full 64-bit
monotonic timestamps. The message-history report explicitly compares Windows'
wrapping `MSG::time` against the low 32-bit projection of the crash timestamp.
Asynchronous-copy collision-retried temporary names now obtain their existing
12-bit, 10 ms seed from the 64-bit monotonic clock rather than a wrapping tick.
Registry Editor's find worker and result-list refresh deadlines now use private
64-bit monotonic timestamps, retaining the original strict 1 ms and 500 ms
refresh gates without signed wraparound comparisons.
Renamer preview now retains its intentional bounded filename prefix through a
capacity-aware copy, while its legacy common-dialog helper rejects an oversized
filter rather than corrupting embedded filter separators.
PictView now bounds text supplied to its fixed status-bar presentation buffer,
clearing a label that cannot fit instead of silently truncating it.
Registry Editor now bounds its key label, Regedit executable path, and export
notification path, retaining the existing `regedit.exe` PATH-search fallback
when the Windows-directory buffer cannot hold the executable name.
DemoPlug's sample custom-column registration now uses bounded fixed-field
metadata copies before it registers the column with the SDK.
Database Viewer now bounds conversion-table menu labels before inserting them
into its fixed presentation buffer.
DiskMap's delayed file/folder focus callback now copies its fixed `MAX_PATH`
handoff buffer with `StringCchCopy`. An oversized input clears the pending
handoff and returns failure, preventing a later panel switch from focusing a
truncated, potentially different path.
Undelete's condition-column registration now uses bounded copies for the
host-defined `CColumn` name and description. If an unexpected localized value
does not fit, it does not register malformed metadata.
Undelete's restore paths are now built with a checked bounded copy followed by
the existing checked path append. A failed construction stops the restore before
any filesystem operation can use a truncated source or destination.
Core shortcut text now appends key names and separators through capacity-aware
operations. If localized names cannot fit the documented 50-byte output,
the helper returns an empty label rather than exposing a partial shortcut.
Core startup now copies the prior-version language filename through a bounded
operation; a malformed oversized legacy value takes the existing normal
language-selection path instead of corrupting the persisted setting.
Core standard-column templates now build their Name/Ext two-string metadata
with bounded copies that reserve both terminators. Oversized localized text
causes template construction to fail rather than overwriting adjacent metadata.
Core View With menu entries now use capacity-aware copies and formatting. An
oversized plug-in name or external-viewer command is omitted instead of being
silently truncated into a potentially misleading command label.
Core Find-history settings now use checked copies for every fixed text field and
bounded formatting for their descriptive title. Corrupt oversized values fall
back to empty valid fields, avoiding a truncated search criterion or label.
Core Find-result cleanup now joins the stored directory and item name through
bounded operations. A path that cannot fit is retained rather than being tested
or removed through a truncated filesystem path.
Core Find drag/context-menu enumeration and file-history updates now construct
their fixed paths through bounded operations. An oversized relative name makes
Shell item creation fail cleanly rather than selecting a truncated child path.
Core advanced-filter descriptions now append localized labels through bounded
operations. If the internal or caller buffer cannot hold the complete summary,
the method fails without presenting a partial filter description.
Core drive-menu creation now copies its cached active-panel path with a bounded
operation. An oversized value uses the existing no-path focus fallback rather
than selecting a drive based on a truncated path.
Core regional settings and view-template configuration now use bounded path and
label copies. Invalid persisted paths fall back to empty selections, localized
list labels clear if they cannot fit, and user-edited view names preserve the
former explicit fixed-field prefix behavior without legacy string helpers.
CheckVer's network-error formatter now uses bounded formatting and copying. Its
caller-provided error buffers retain the previous clipped-diagnostic behavior,
while the fixed fallback message uses a checked copy.
CheckVer now performs clipped filter-list and version-parser copies explicitly.
Its `6b`-style beta component expansion is capacity-checked, preserving a
parseable prefix instead of overrunning the fixed version component buffer.
PictView now copies the already measured Unicode thumbnail-cache directory
prefix with `memcpy` before appending `Thumbs.db`. This removes the legacy
count-including-terminator behavior and preserves the required path separator.
Media Viewer now records a current filename only when it fits its fixed state
buffer, while its opening-error and title strings use bounded formatting with a
plugin-name fallback. A parsed file never retains a truncated identity.
Folders now copies dynamically supplied Shell column names through the bounded
`CColumn` metadata field. A name that cannot fit is omitted, avoiding an
ambiguous truncated column registration.
Core mask-name formatting now uses an explicit capacity-aware copy in the
no-mask branch, retaining the established clipped output contract without the
legacy `lstrcpyn` helper.
Core directory comparison now includes copying the panel root in its checked
path-construction chain. An oversized root reports the existing too-long-path
error rather than enumerating a suffix from a truncated directory.
The drive bar now retains exactly the two-character drive or UNC prefix in its
three-byte cache through an explicit bounded copy. Password encryption copies
its source password with the allocation's already-computed length, and PE Viewer
copies all eight bytes from the non-terminated PE section-name field before
adding its own terminator for output.
Demo Viewer now rejects an oversized file identity, skips an oversized localized
file filter, and lets the common dialog choose its default directory if no valid
initial directory can be formed. This avoids interpreting partial fixed-buffer
values as paths or filter metadata.
DemoPlug's viewer follows the same policy for its independent sample
implementation, so the two SDK viewer examples do not diverge on oversized
file paths or malformed localized filters.
DemoPlug's filesystem sample now repeats the connect dialog instead of creating
a plug-in path from an oversized user part, and declines icon lookup when its
temporary full path cannot retain the entire filename.
Core Attributes dialogs now retain their established fixed-size backup,
ellipsis-display, and username-field limits through explicit bounded copies.
Drive Info now also uses explicit bounded copies for its root-path handoff to
the existing volume-information and remote-drive APIs. Those APIs continue to
define a `MAX_PATH` root boundary, so broader long-volume-path support remains a
separate API-contract migration.
Core critical-shutdown cleanup now compares the elapsed wait against a 64-bit
monotonic time point. The existing five-second policy is unchanged, but it no
longer depends on subtraction across the 32-bit tick wrap boundary.
UnCAB now initializes its fixed error buffers through a shared checked helper
before appending a system diagnostic with `FormatMessage`. Its extraction
progress label is also assembled with bounded operations, so it never displays
a partial filename.
UnCAB also copies its archive root and entry name by their already measured
lengths into an allocation sized for both components and its terminator.
Its fixed manifest destination buffer now rejects an oversized target before
adding the required separator, so later file-list entries cannot be based on a
truncated directory.
The existing fixed UnCAB file record now also rejects an oversized archive
source before retry/error handling begins, avoiding diagnostics that name only a
path prefix. A full dynamic redesign of that shared record remains separate.
Media Viewer's Ogg, MP4, MP3, and WAV parsers now use checked copies for their
fixed duration, codec, and channel-mode labels. A label that cannot fit is
cleared instead of showing a partial value.
UnISO now treats a directory record's file identifier as the counted ISO field
it is, copying exactly that length before adding a local terminator.
UnRAR now copies each enumerated archive-entry name by its stored length into
the allocation sized for that name and its terminator.
UnLHA's single-file extraction now validates both the source archive leaf and
target directory before forming the output path, reporting its existing
too-long-name error rather than extracting to a prefix. Its unpack-progress
label is also assembled through a checked copy/append chain.
UnARJ now copies its archive root and entry name by their already measured
lengths into an allocation sized for both components and its terminator.
UnCHM now rejects a truncated or malformed module path before composing the
companion `chmlib.dll` path, rather than overwriting a suffix through `lstrcpy`.
Single-file and whole-archive extraction also abort before using an incomplete
CHM object or destination path.
IE Viewer now skips navigation when its startup filename cannot fit locally, and
formats browser URL and Markdown window titles through checked fixed buffers so
partial locations are not shown as document identities.
NetHood now copies its candidate location through a checked path before probing
for `desktop.ini` and `target.lnk`, avoiding discovery requests against a
truncated sibling path.
PAK now sizes each owned deletion-metadata string from the input and copies that
exact byte range; its archive-parent marker likewise uses one measured length for
matching and consuming the constant. Its heap ownership no longer depends on a
legacy string API.
MMViewer's WMA parser now copies localized boolean labels through a checked
512-byte presentation buffer and reports `ERROR_INSUFFICIENT_BUFFER` rather than
returning truncated metadata.
Split/Combine now derives the focused filename's prefix length from its extension
pointer, copies that exact span, and terminates it explicitly instead of using
`lstrcpyn`.
Registry Editor now retains only its documented status-bar clipping through
`StringCchCopyNW`; common-dialog filters and selected-key paths are rejected or
cleared when incomplete, and the bounded registry-value byte count uses `strlen`
instead of `lstrlen`.
Automation, CheckVer, UnOLE, and Media Viewer now measure their terminated local
strings with the CRT before validating or passing character counts to their
consumers. UnMIME retains its deliberate fixed-field clipping through
`StringCchCopyNA` rather than `lstrcpyn`.
PictView now requires complete fixed-buffer values for crash-report metadata,
panel-focus handoffs, EXIF locale filenames, and viewer-thread requests,
clearing, skipping, or rejecting each operation rather than continuing with a
truncated identity.
Core list rendering, executable-extension matching, and mask parsing now obtain
their character counts from terminated local strings through the CRT rather than
the `lstrlen` API family.
File Compare and Renamer now likewise use `strlen` to locate the append point in
their terminated local error buffers before `FormatMessage` adds system details.
Core tooltip presentation, toolbar-item allocation, and execute-field caret
placement likewise derive their text lengths through the CRT instead of
`lstrlen`.
UnARJ now copies owned archive-entry names by their measured allocation, rejects
single-file requests and first-volume normalization paths that cannot fit, and
uses checked archive/destination construction and bounded localized error prefixes
extended by `FormatMessage`.
UnRAR now measures its terminated archive names and roots with the CRT, rejects a
single-file extraction leaf name that cannot fit its fixed buffer, and bounds
localized retry prefixes and RAR SDK header filenames before continuing.
PAK SPL now copies owned archive-entry strings by measured allocation, requires
complete archive, extraction, packing, and deletion paths, and bounds localized
retry prefixes before appending system error text.
ZIP self-extractor settings now use CRT lengths for parser fields and serialized
setting bytes, and reject a relative package resolution when the configured
zip2sfx base directory cannot fit its fixed working path.
The ZIP `zip2sfx` tool now uses one CRT-based helper for the SFX header's
integer-sized ANSI string lengths and offsets, preserving its serialized layout.
Core file execution now uses CRT lengths for view-template and Unicode display
text, while its intentional fixed-width common-file-type label clipping uses
`StringCchCopyNA` instead of `lstrcpyn`.
ZIP's SFX dialogs now use CRT string lengths and checked fixed-field copies for
dialog, menu, and settings data. An oversized configuration value is cleared
rather than preserved as a prefix; the fixed About presentation alone retains
its deliberate clipped behavior through `StringCchCopyNA`.
ZIP archive creation now centralizes terminated SFX and archive-name length
calculation through a CRT helper. The helper retains the existing integer-sized
serialized layout rather than widening on-disk offsets or record fields.
Its precisely sized entry-name allocations now use measured copies, while
fixed executable, directory-enumeration, and archive-comparison paths are
rejected when incomplete.
The fixed progress label retains its intentional clipped presentation through
`StringCchCopyNA`.
ZIP defaults and archive deletion now copy complete SFX settings with checked
field capacities, retain the intentional clipped About presentation, and build
the dynamically allocated root entry from its measured name. Its deletion
progress label now bounds both localized prefix and entry-name text explicitly.
ZIP's common archive formatter now honors its fixed 512-byte contract with
`StringCchCopyA`; its bounded string helpers use CRT lengths for terminated input.
ZIP repair now rejects a target path that cannot retain both its archive name and
repair suffix instead of using unbounded `lstrcpy`/`lstrcat`.
The ZIP SFX package builder now obtains its terminated text lengths through the
CRT before storing them in the existing DWORD package fields.
ZIP's entry-point now uses checked fixed-field and path copies for default SFX
discovery, persisted volume settings, and menu-selected archive paths.
ZIP's SFX-language dialog now bounds selected labels and system-error text, and
skips a language package when its full scan path cannot fit.
ZIP's multi-volume scan now carries explicit component and output capacities,
rejecting an oversized mask or discovered volume instead of using `lstr*` copies.
ZIP's archive dialogs now pass explicit capacities when splitting a path and omit
an unrepresentable self-extractor target rather than appending to a prefix.
ZIP dialogs now use CRT lengths for terminated UI text and checked copies for
volume-cache, password, language, and fixed label fields.
ZIP's `MakeFileName` API now takes destination capacity and rejects generated
volume names that cannot fit, with archive creation and dialog previews handling that failure explicitly.
Translator project paths now use capacity-aware normalization and checked copies;
an oversized persisted entry fails the project load rather than resolving a truncated path.
Translator's Unicode cleanup now uses CRT wide-string lengths when shifting a
terminated edited string in place.
Translator now copies SLG signature fields with declared capacities and refuses
to export a clipped translation through its fixed Unicode scratch buffer.
Translator now constructs temporary and backup target paths through a checked
extension-replacement helper; a missing extension or oversized target does not
cause the save flow to operate on a partial or unrelated file.
Translator's recent-project list now stores only complete `MAX_PATH` entries
with checked copies. Removing an entry shifts through the penultimate slot only,
so it never reads a nonexistent successor past the fixed array.
Translator's MUI package discovery now copies and appends every root, wildcard,
directory, and selected file with a checked capacity. An oversized component is
skipped rather than allowing resource probing or loading through a partial path.
Translator now copies checklist error excerpts from their counted file range and
requires full destinations for retrieved translations, context-menu identifiers,
and process paths. These flows therefore avoid treating a clipped or unbounded
string as a valid parser, clipboard, or executable identity.
Translator now uses CRT length routines for terminated menu, dialog, clipboard,
and version-resource text. Its fixed font-name and version-resource query fields
are copied with declared capacities, rejecting a partial query hierarchy.
Translator's resource-header parser now copies counted tokens only when the full
identifier fits, so clipped names cannot bind to a different symbol. Error and
list-view excerpts retain explicit display clipping, while the editor mail-slot
packet rejects a path that cannot fit its shared file field.
Translator's inline SLG defaults now use declared resource-field capacities and
clear an unrepresentable CRC state rather than retaining a clipped overwrite
decision. Its menu-template ownership copies the measured allocation directly.
Translator's tree and text-list views now use explicit bounded presentation copies.
Nested-menu indentation is clamped to leave a terminator and caption space, both
when populating and when updating a row after an edit.
Translator's SalMenu parser now copies its template, dialog, control, and string
tokens from exact line ranges only when the full value fits. This prevents a
truncated identifier from resolving to a different resource definition.
Translator's frame now stores command-line fields through checked capacities and
requires a complete absolute project path before loading. Its import/export path
fields and README filename replacement use explicit bounded copies, preserving
the module path when the new leaf cannot fit.
Core packer configuration now checks the persisted extension set before making a
lowercase normalization copy. A value that cannot fit the format buffer fails to
load rather than becoming a partial packer configuration.
The shared wide-string duplication helper now uses a CRT length for its exact
allocation size instead of the legacy Win32 length API.
Core password recovery now securely erases its temporary scrambled-password
backup through the terminator before freeing it, and tolerates a failed backup
allocation without dereferencing it during cleanup.
Core Viewer startup now rejects a file identity that cannot fit its worker or
constructor path buffer. Its optional caption remains an intentionally bounded
display field, now copied with an explicit fixed capacity.
Core temporary-directory cleanup and panel focus now append each discovered
directory suffix with a checked remaining capacity. An oversized suffix is
skipped rather than acting on a partial temporary path.
The shared Messages title setters now use explicit bounded ANSI and Unicode
presentation copies before performing their reciprocal code-page conversions.
The shared Sheets Unicode bridge now validates its destination and retains its
documented display clipping explicitly. Dialog titles allocated from a measured
length now copy through that exact capacity.
The shared bounded append helper now verifies the destination terminator inside
its declared field before explicitly clipping the appended source to the
remaining capacity.
Shared WinLib error labels now use generic StrSafe bounded copies, retaining
their fixed presentation capacity in both ANSI and Unicode configurations.
The shared thread-owner launch record now stores its debugger-only worker name
with an explicit bounded diagnostic-field copy, preserving startup behavior for
long descriptive names.
Core Quick Search now copies a matched filename prefix through an explicit
caret-state display limit, avoiding a legacy helper while retaining the focused
match behavior.
Core hyperlinks now clear a target that cannot fit their shell-action field. The
animation tooltip uses the `WM_USER_TTGETTEXT` protocol's documented capacity
instead of an unbounded legacy copy.
Core icon association lookup now rejects an oversized counted registry key before
constructing association suffixes. The fixed folder-type label retains explicit
display clipping when populated from Shell metadata.
Core Message Box alias records now require a complete parser buffer, falling back
to default button labels when oversized. Button-width measurement retains an
explicit clipped preview for arbitrarily long label text.
Core packer and unpacker configuration now verifies each persisted extension set
before compatibility normalization, rejecting a value that cannot fit intact.
Core toolbar customization now derives exact owned-label allocation sizes from
CRT lengths instead of legacy Win32 string-length calls.
Core hot-path and user-menu toolbar captions now retain explicit bounded
presentation copies before accelerator formatting.
Core user-menu drag/drop now uses CRT wide filename measurement, while its compact
toolbar captions retain explicit bounded presentation copies.
Core panel UTF-8 rendering and measurement now derive their GDI text length
through CRT Unicode lengths after conversion.
Core icon-thread submissions now reject a file path that cannot fit their work
record, returning the established zero request ID rather than queueing a partial
icon identity.
Core operation-journal diagnostics now bound their fallback identity, while
transactional sibling checks reject temporary or target paths that cannot be
represented completely before comparing directories.
Shared UTF-8 GUI GDI wrappers now derive their owned converted Unicode text
lengths through CRT routines instead of legacy Win32 length calls.
Core version-resource queries now require complete tokenization paths and use
explicit bounded Unicode output copies for caller-sized display fields.
Core PackAC worker-status trimming now handles empty text safely, while
executable-extension matching uses CRT-measured filename lengths.
Core shell overlay discovery now retains registry-derived handler names and
descriptions through explicit bounded discovery/UI record copies.
Core Viewer captions now retain explicit compact display text, while its
file-change notification requires a complete containing directory path.
Shared operation-worker paths and completion correlation IDs now require full
fixed-field copies, clearing an unrepresentable identity rather than retaining a
truncated source, target, or telemetry key.
Core code-table labels and accelerator-stripped comparison text now use explicit
bounded configuration/display buffers.
Core language selection now uses checked module-name and search-pattern fields,
preventing an incomplete language-file identity from being selected or probed.
Core hot-path getters now use explicit caller-sized bounded output fields and
CRT lengths for their dynamically owned names and paths.
TServer font restoration now copies its persisted `LOGFONT` face name through an
explicit bounded field rather than a legacy Win32 string helper.
Core column rendering now uses CRT Unicode/text lengths and an explicit bounded
Shell-path display field.
Core column clipboard path composition now uses an explicit remaining-capacity
copy for its fixed combined path field.
Core panel message handling now requires complete buffered operation, focus,
refresh, and file-enumeration path identities, so later asynchronous work does
not consume silently truncated paths.
Core directory reading now uses checked path fields for queued focus, disk
enumeration, plugin enumeration, and archive-change notification; an
unrepresentable path stops or suppresses the affected follow-up work.
Core popup-menu creation now uses CRT menu-label lengths and explicit
caller/template display-buffer limits while retaining intentional UI clipping.
Core delete confirmation and panel-navigation paths now use explicit bounded
label, caption, reparse-path, and remaining-capacity composition fields, so
only complete identities reach reparse and navigation operations.
Its archive and file-system transfer setup now also returns to the destination
dialog when its fixed working or external target path cannot retain a complete
identity.
Core shared-folder discovery now rejects incomplete local and UNC path
identities instead of matching or returning truncated share paths.
Core archiver GUI helpers now make their fixed tooltip and subject-display
capacities explicit while preserving the plug-in facade's buffer contract.
Core OLE spy diagnostics and `STRRET` compatibility output now use explicit
fixed-field capacities while retaining their existing clipped-result behavior.
Core call-stack diagnostics now use CRT text lengths and explicit fixed-record
or complete-sidecar path fields.
Core viewer file enumeration and inaccessible-path fallback now require
complete request, result, and configured path identities.
Core Find MD5 checks now require a complete file path; its searching-status
display uses explicit base, append, and caller-output field limits.
The shared wide-path adapter now uses CRT wide-string lengths for its owned,
terminated display, duplicate, and extended-prefix paths.
Core mount-point and volume-GUID resolution now requires complete source,
root, and caller-output path identities.
Core archive unpack masks now use an explicit display limit, and archive
change notifications skip incomplete path comparisons.
Archive packing now stops with its established cleanup when the enumerator's
fixed working path cannot retain the complete source identity.
Noninteractive archive-copy targets now use the same complete dialog-path
contract and established cleanup instead of silently truncating input.
Core Find dialog named-search and log fields now use explicit display limits;
Find log focus requires a complete containing path.
Core toolbar button definitions now use explicit tooltip, persisted-layout,
serialization-delimiter, and resource-row field behavior.
Core panel list-box input now uses CRT lengths for its owned column labels and
UTF-8-to-wide display text.
Core main-window messages now require complete queued notification and
shared-memory paste paths while explicitly clipping the title-bar prefix.
Core Salmon helper now uses CRT path lengths and requires complete shared
bug-report and child executable paths.
Core plug-in utility bridge now makes command and source-description clipping
explicit, retains complete mask output, and rejects incomplete help-file names.
Shared tracing now uses explicit trace-path, fixed crash-dialog, and exact
dynamic diagnostic-message field capacities.
Core general configuration now uses CRT lengths and explicit template-name,
mask backup, hot-path label, and mask-editor field limits.
Core editor input now uses CRT lengths, preserves complete plug-in command and
drop-path identities, and makes drag-insert text clipping explicit.
Core Find results now use CRT text lengths and a shared complete-path helper
for selection comparison and asynchronous file-name enumeration.
Core path checking now rejects incomplete worker, fallback, policy, and
AppData path identities while using CRT registry-value lengths.
Core panel initialization now uses explicit directory-line mask bounds, CRT
extension lengths, and bounded cached Windows-directory compatibility prefixes.
Core safe-file retries now return complete-or-empty skipped paths through one
helper and make DOS-workaround/error-dialog fields explicit.
Core shared-library clipboard paste now requires complete source, archive,
internal, target, and temporary-directory path identities.
Core ZIP general API now makes error-text clipping explicit and requires
complete FS-name, focus, and disk-operation working path identities.
Core truncated-string support now uses CRT lengths and explicit view-template,
history-open, clipboard, and SalOpen shared-memory field semantics.
Core panel configuration now uses explicit disconnected/progress display limits
and requires complete removable-drive readiness paths.
Core viewer configuration now uses explicit presentation limits for plugin and
mask labels, while plugin and fallback-path selection identities are complete
or empty.
SalOpen shell navigation now uses CRT lengths and accepts only paths that fit
its fixed shell-parser buffers before it forms a parent-directory identity.
Core shell helpers now publish drag/drop target paths only after complete IPC
copies, keep NetHood sidecar paths complete, and make bounded name outputs
explicit.
Core HTML Help resolution now uses a complete-or-fail copy helper for
localized directories, fallback folders, and CHM file identities.
Core panel navigation now requires complete redirector, reparse-point, focus,
and requested-directory identities while retaining bounded Win32 file names.
Core plugin integration now uses explicit menu and registry presentation
limits, preserves filesystem-name suffix headroom, and returns complete
NetHood filesystem identities.
Core configuration loading now distinguishes bounded diagnostics and toolbar
migration text from complete registry-key and restored default-directory
identities.
Core operation execution now uses explicit bounded progress and error labels
while retaining complete worker correlation identifiers.
Core startup now uses explicit fixed Hot Path, tray-tooltip, and title-bar
presentation limits instead of legacy string APIs.
Core status-window rendering now uses CRT Unicode lengths and explicit
callback, hot-track, clipboard-path, and navigation-buffer limits.
Core shared drag/drop objects now retain complete-or-empty real, filesystem,
and temporary-directory identities for clipboard and cleanup operations.
Core plugin filesystem encapsulation now retains selected filesystem names as
complete-or-empty reopen identities.
Core shell support now uses bounded search/error text and complete deferred
drag/drop, archive, and fake-directory shared-memory identities.
Core Find UI now uses bounded dialog/result fields and complete search and
comparison paths, with CRT lengths for edit-line selection handling.
Core asynchronous copy now uses complete metadata and transactional-path
identities, bounded error text, and CRT Unicode/name lengths.
Core plugin loading now uses explicit extension/menu/hotkey presentation
limits and bounded language-module path construction.
Core file actions now use bounded prompt/name fields and modern path,
command-line, focus, temporary-name, and redirector copies.
Core command handling now uses bounded caption and toolbar-layout fields, CRT
name lengths, and complete-or-empty comparison, selection, and deferred focus
path identities.
Core startup now uses CRT lengths and explicit bounded diagnostics, title
fields, command-line fallback paths, configuration paths, and deferred Notepad
targets.
Core panel execution now retains complete file, archive, and plug-in filesystem
identities across re-entrant callbacks while bounding diagnostics and
restore-focus presentation fields.
Core drive-list handling now keeps complete connection, removable-drive, and
plug-in filesystem identities across worker/UI handoffs, while limiting
credential and tooltip/display fields to their presentation buffers.
Core path utilities now use CRT lengths and explicit bounded dialog/history
labels, while retaining complete traversal, archive, and stored navigation
identities before performing path operations.
Core string/resource helpers now use bounded formatter, language, and
configuration text fields plus StrSafe path and counted UTF-16 reparse-point
copies in filesystem and volume resolution.
Core panel operations now use complete planning/source identities and bounded
captions, while preserving the duplicate-name generator's MAX_PATH overflow
sentinel through explicit remaining-capacity copies.
Core menu queuing, DB Viewer, FTP's lexer, IE Viewer, PE Viewer, and Registry
Editor now use CRT lengths for terminated local strings. DB Viewer's fixed DBF
record field and the FTP lexer token instead use explicit measured copies and
terminators, avoiding an implicit scan beyond their counted input ranges.
FTP's listing-wait time estimate now retains its fixed 20-byte presentation
limit through `StringCchCopyNA`, rather than relying on `lstrcpyn` truncation.
Core operation-progress dialogs now suppress a post-operation notification when
its copied working path is incomplete. Their captions and delayed status text
remain deliberately clipped display fields, now through explicit bounds.
DB Viewer now treats the date components stored in a DBF record as counted
bytes before parsing them. Its parser metadata, coding state, and localized
Boolean labels use explicit bounded copies, preventing presentation buffers
from overflowing or retaining incomplete file and configuration identities.
Its DBF and CSV parsers now own the complete opened filename and write it
directly to the file-information control, so later metadata and filesystem
queries do not depend on a legacy `MAX_PATH` copy.
Checksum now leaves its common save dialog without a suggested name when the
value is oversized, and posts its cross-window focus command only after the
full file path fits in the shared payload.

### `wsprintf` / `wvsprintf`

`wsprintf` has no caller-provided output size and `wvsprintf` has a historical
1,024-character limit. The CRT-free self-extractor's tracing path now uses a
local bounded formatter and bounded newline handling, keeping diagnostics usable
if a message exceeds its fixed buffer without adding a CRT dependency. Remaining
direct `wsprintf` callers in separate first-party components and bundled code
still require the same behavior-preserving treatment.
That formatter now also serves self-extractor status text, overwrite metadata,
shell command construction, and multi-volume prompts; all active calls in that
component are capacity-aware, while the remaining matches are commented-out
legacy snippets.
The core UNC-copy error, Find toolbar status tooltip, and skipped-result summary
now use `StringCchPrintfA`, omitting text that cannot fit their known buffers.
The core system-message prefix and system-menu shortcut labels now also use
bounded CRT formatting, preserving the existing string layout before appending
system text or menu labels.
ParserBroker now bounds both its pipe identity and child command line before
creating kernel objects or the restricted broker process.
Temporary file/directory name generation now bounds each collision suffix before
probing the candidate path.
The Salmon monitor launcher now bounds its child command line and shared-memory
mapping name before handing them to process and mapping APIs.
OLE Spy's per-item PIDL diagnostic fragment now uses bounded `TCHAR` formatting
before it is appended to the overall trace string.
The core message-box clipboard export now assembles every formatted fragment
through a bounded append helper, abandoning the copy when any fragment cannot
fit in its preallocated buffer.
IE Viewer's Internet-feature registry key construction now rejects an
unrepresentable feature name instead of overflowing its fixed key buffer.
MMViewer's numeric and binary WMA attributes now use bounded formatting and
report an insufficient-buffer HRESULT rather than returning truncated metadata.
File Compare's missing-file detail now uses bounded `TCHAR` formatting and falls
back to its generic localized error when the full detail cannot fit.
DemoView now skips filter-menu labels and messages that cannot be formatted into
their fixed UI buffers.
Demo Plug's timed control demonstration now ignores an oversized localized
format result instead of allowing the timer text to overrun its fixed buffer.
File Compare's CRT-free remote helper now builds its fixed IPC event name with a
bounded hexadecimal encoder, preserving the `/NODEFAULTLIB` configuration.
The shared plug-in handle diagnostics now formats its numeric system-error
prefix with a buffer-counted CRT call before appending `FormatMessage` output.
The shared plug-in TRACE_C message boxes now reject an oversized ANSI or Unicode
source-path prefix before appending diagnostic text.
SFX7Zip's CRT-free build now constructs its bounded launcher commands through
local append and hexadecimal helpers, avoiding both `wsprintf` and an unwanted
CRT formatting dependency under `/NODEFAULTLIB`.
Replace them with `StringCchPrintf`/`StringCchVPrintf` or `std::format` where the
toolchain and exception policy permit it.  Always pass `_countof(buffer)` and
handle/truncate an `STRSAFE_E_INSUFFICIENT_BUFFER` result deliberately.
PictView's bounded About-dialog buffers now use `StringCchPrintf` and retain
their original resource text if a format result does not fit.
The core Notepad launch command now uses `StringCchPrintfA` and fails safely if
the bounded command-line buffer cannot represent the selected path.
The core New File menu now uses `StringCchPrintfA` for its localized label and
omits the affected command if that label cannot fit.
The shared-folder stop confirmation now uses `StringCchPrintfA` and refuses to
delete a share when its full localized confirmation cannot be represented.
MMViewer's bounded About-dialog buffer now uses `StringCchPrintfA` and retains
the original title when a localized format result cannot fit.
PictView's thumbnail-save error now uses `StringCchPrintf` with a bounded
fallback message if an unexpectedly long localized result does not fit.
PAK's DLL/SPL error-formatting interface now carries the caller buffer capacity
and uses `StringCchVPrintfA`, with a bounded fallback for an oversized result.

### `GlobalAlloc` / `GlobalFree` and `LocalAlloc` / `LocalFree`

Global/local allocation is a 16-bit compatibility layer over the process heap.
For ordinary private allocations, use RAII containers/objects, `new`/`delete`,
or `HeapAlloc(GetProcessHeap())` paired with `HeapFree`.  Do **not** mechanically
replace every call:

* **Must remain `HGLOBAL`:** clipboard and OLE/DDE transfers at
  `src/shellsup.cpp:704-729`, `src/stswnd.cpp:1983`,
  `src/truncated_string.cpp:925,959,1049,1095`, `src/viewer2.cpp:1716`,
  `src/translator/translator.cpp:159,238`, `src/shellib.cpp:2696,2720`, and
  `src/salshlib.cpp:245,275,309`.  `SetClipboardData` takes ownership after
  success, so retain the existing free-on-failure behavior.
* **Already migrated:** the private token-query buffers in `src/async_copy.cpp`
  and `src/file_enumeration.cpp`, threaded message copies in
  `src/common/messages.cpp`, Folders plug-in selection array in
  `src/plugins/folders/fs2.cpp`, the core trace buffers in
  `src/common/trace.h`/`trace.cpp`, and the shared plug-in trace buffers in
  `src/plugins/shared/dbg.h`/`dbg.cpp` use process-heap storage. The core and
  Salmon temporary security ACLs also use the process heap because Windows only
  borrows them while creating the mutex or shared-memory object. The Shell
  extension registry configuration list in `src/shexreg.c` is likewise private
  process state and now uses `HeapAlloc`/`HeapFree` throughout. The dynamic
  tree-property dialog template in `src/common/sheets.cpp` is another private
  buffer; it is released after its synchronous modal creation completes.
  The standalone shell extension follows the same rule for its COM interface
  objects, token data, and logging ACL; only the API-created SID uses `FreeSid`.
* The `GlobalAlloc`/`GlobalFree` matches in the disabled historical
  `ENABLE_SH_MENU_EXT` block of `src/shexreg.c` are not compiled. Leave that
  code untouched unless the legacy feature is deliberately restored.
* **`LocalFree` is an API ownership requirement in several paths:** security
  descriptors/SIDs in `src/async_copy.cpp:564,761-856,891-892`, SID strings in
  `src/dialogs_config_general.cpp:208`, and the analogous Salmon string path
  must continue to be released with `LocalFree` because the allocating Windows
  API specifies it. Do not pair these allocations with a heap deallocator.
* **Wrapper exposure:** `src/common/handles.cpp:2261-2399` and
  `src/plugins/shared/mhandles.cpp:2312-2450` deliberately forward the legacy
  allocators. Deprecate their use in new code after callers have been migrated;
  do not delete the wrapper until ABI consumers have been audited.

### `OpenFile`, `_lopen`, `_lcreat`, and related `HFILE` APIs

The direct wrappers in `src/common/handles.cpp:2451-2469` and
`src/plugins/shared/mhandles.cpp:2494-2512` call `::_lcreat`, `::OpenFile`, and
`::_lopen`.  These 16-bit-style `HFILE` APIs have limited flags and inconsistent
error/handle semantics.  Replace wrapper users with `CreateFileW` (or the
repository's UTF-8 `CreateFileUtf8` layer) and `HANDLE`, then `CloseHandle`.
Keep the wrappers only as an explicitly deprecated ABI bridge until no plug-in
or external caller requires them.  Do not confuse the many class methods named
`OpenFile` with the Win32 API; only the `::OpenFile` forwarding lines are in
scope here.

### `RegisterClass` -- older window-class structure

The core `CWindow::RegisterUniversalClass`, plug-in shared window library,
CheckVer log window, SalOpen command window, PictView tooltip class, and FTP
socket worker window now use `WNDCLASSEX` with `RegisterClassEx`, preserving
both large and small icons. The self-extractor's temporary process-wait window
and IE Viewer main window now use the extended registration contract as well.
No first-party direct `RegisterClass` registrations remain in the audited source
set; matches are application methods rather than the Win32 API.
For new or touched classes use `WNDCLASSEX` with `RegisterClassEx`; it supports
the small icon and aligns with the pointer-width `Get/SetClassLongPtr` APIs.
This is a compatibility/readiness improvement, not an emergency deprecation.

### `GetScrollRange`

`src/plugins/pictview/render1.cpp` now uses `GetScrollInfo` with `SIF_RANGE |
SIF_PAGE`, preserving the existing range check while obtaining the additional
page-size data through the supported unified query.

## Locale, DPI, and UI modernization (supported APIs with legacy usage patterns)

### LCID-based `GetLocaleInfo`, `GetDateFormat`, and `GetTimeFormat`

The LCID interfaces remain supported, but the `...Ex` APIs accept locale names
and better fit modern locale fallback.  Calls are concentrated in file-list
display, execution, archive plug-ins, FTP, and renamer code: for example
`src/async_copy.cpp:186,188,256,258`, `src/execute.cpp:1066-1145`,
`src/fileswindow_display.cpp:1808`, `src/find_results.cpp:132,1152,1155`,
`src/bugreprt.cpp:1729-1746`, `src/string_resources.cpp:3148`, and
`src/plugins/ftp/{ctrlcon1.cpp,dialogs4.cpp,fs4.cpp,operats2.cpp,operats6.cpp}`.

Use `GetUserDefaultLocaleName`, `GetLocaleInfoEx`, `GetDateFormatEx`, and
`GetTimeFormatEx` for user-facing formatting.  Preserve `LOCALE_NOUSEROVERRIDE`
where it is intentional, and avoid changing fixed interchange formats into
localized formats. `src/bugreprt.cpp` is migrated to the user locale name and
`GetLocaleInfoEx`, retaining one bounded UTF-8 report boundary. Automation now
uses `LOCALE_USER_DEFAULT`, because `IDispatch` itself requires an LCID and no
thread-state lookup is needed.
Its file-information object now formats user-facing dates and times with the
locale-name APIs, retaining its TCHAR interface and avoiding a failed-format
length underflow.
Core startup now obtains the default ANSI code page through
`GetUserDefaultLocaleName` and `GetLocaleInfoEx` before selecting the font
charset.
Core copy-overwrite/directory metadata, task history, execution substitutions,
file-panel display, search results (including dynamic column-size sampling), and
both measured and rendered file columns now share a UTF-8 locale-name date/time
formatter, retaining their existing numeric fallbacks when formatting or
conversion fails.
The beta-expiration dialog now shares that locale-name date formatter and uses
bounded UTF-8 template expansion, with numeric and literal-text fallbacks for
unavailable locale data or oversized resources.
The Trace Server's export sizing and row rendering now use the locale-name APIs
while preserving its explicit `TIME_FORCE24HOURFORMAT` and fixed `hh:mm:ss`
format string.
The core language-pack name helper now converts its stored LCID to a locale name
with `LCIDToLocaleName` and calls `GetLocaleInfoEx`, retaining its existing
ANSI interface only at the final output boundary.
Translator's language selection list follows the same locale-name conversion,
using `GetLocaleInfoEx` before converting its bounded dialog label to ANSI.
ZIP's volume-size dialog now obtains the decimal separator through the named
user locale and `GetLocaleInfoEx`, then converts it into its fixed ANSI parser
buffer.
ZIP's archive file-information display now similarly formats date/time with
locale-name APIs before its bounded ANSI plug-in boundary.
The self-extractor's numeric formatter now retrieves the thousands separator
through the named user locale and `GetLocaleInfoEx` before using its bounded
ANSI display buffer.
Its overwrite-dialog file-information display now uses the same named-locale
formatters while retaining its UTF-8, CRT-free presentation contract.
Renamer's date and time expression defaults now share a named-locale
`GetLocaleInfoEx` helper, preserving their ANSI expression-buffer contract.
Its custom date/time expressions, preview columns, and file-information display
now also use `GetDateFormatEx`/`GetTimeFormatEx` through a bounded ANSI-to-UTF-16
format bridge, preserving user-supplied expression formats.
The core file-list date-width probe now uses `GetLocaleInfoEx` and
`GetDateFormatEx` with the user locale name before converting its bounded
measurement string to ANSI.
ZIP2SFX now converts the SFX metadata LCID with `LCIDToLocaleName` before
obtaining its status-label language through `GetLocaleInfoEx`.
The ZIP plug-in's SFX-language selection, reset, and favorites labels use the
same named-locale conversion, preserving their existing ANSI dialog boundary.
PE Viewer's resource-language label now converts the PE-format LCID to a locale
name before retrieving its ANSI display text with `GetLocaleInfoEx`.
Its version-string language report now follows the same locale-name conversion
instead of querying the LCID-based API directly.
PE Viewer's PE-header timestamp annotation now also formats date/time through
locale-name APIs before appending to its bounded ANSI output.
DiskMap's number formatter now converts its stored LCID with
`LCIDToLocaleName` and uses `GetLocaleInfoEx` for every numeric-format field,
retaining `TCHAR` conversion only at the formatter boundary.
Its long-date and time formatter now follows the same named-locale path while
retaining its TCHAR interface and existing numeric fallback.
PAK's file-information display now formats date and time through
`GetTimeFormatEx`/`GetDateFormatEx`, converting only at its ANSI UI boundary.
Windows Mobile now shares modern locale-name date/time formatting across both
its device and local filesystem file-information displays.
FTP's connection log headers, custom-column displays, and file-info strings now
share a locale-name formatter that converts only its bounded ANSI result; its
time-column path also now correctly uses the time formatter rather than the
former duplicated date call.
UnARJ's file-information display likewise uses `GetTimeFormatEx` and
`GetDateFormatEx` before its ANSI presentation boundary.
UnRAR's archive file-information display now follows the same locale-name path,
converting the bounded UTF-16 result only at its legacy ANSI plug-in boundary.
TAR's archive file-information display now does the same for its bounded ANSI
metadata fields.
The application-owned 7-Zip wrapper now formats both archive metadata paths
through locale-name APIs before their bounded ANSI presentation boundary.
CheckVer's last-check log line now follows the same path for its long-date
display, with a bounded ANSI conversion and its established numeric fallback.
Split/Combine's generated file-information text likewise uses named-locale date
and time APIs before its bounded ANSI presentation boundary.
Demo Plug's example creation, modification, and access-time columns now do the
same, so the SDK sample demonstrates the current locale-name API pattern.
Its viewer filter menu labels and selection message now use bounded formatting,
so the SDK sample also demonstrates safe menu-text construction.
Registry Editor's key date and time custom columns now use named-locale APIs
before returning their existing ANSI callback text.
PictView's file-information display now adapts named-locale output at its
existing TCHAR presentation boundary.
Undelete's recovered-file information now formats date/time through the
named-locale APIs before its bounded ANSI metadata presentation.
DBViewer's file details and DBF date cells now do the same; its deliberate
`LOCALE_NOUSEROVERRIDE` time format remains intact.
Its Go To Record dialog now uses bounded `TCHAR` formatting for the selected
record number.
UnCHM's archive file-information display now follows the same locale-name path,
converting the bounded result only for its existing ANSI plug-in UI.
UnCAB's file-information display now does the same.
UnLHA's file-information display now does the same.
UnMIME's decoder file-information display now does the same.
UnFAT's file-information/error formatter now does the same.
UnISO's viewer and ISO metadata formatter now do the same.
Residual direct date/time formatter calls are limited to the vendored 7-Zip
`NationalTime` implementation and a commented API example in `spl_file.h`.
Update the vendored implementation from compatible upstream code (or carry a
provenanced compatibility patch) and remove the obsolete comment when revising
the shared plug-in API documentation.

### System DPI from `GetDeviceCaps(LOGPIXELSX/Y)` and global metrics

`src/app_entry.cpp:1641-1643` and `src/translator/translator.cpp:478-480` cache
one system DPI; many font calculations use `GetDeviceCaps(..., LOGPIXELSY)`.
This works only as a system-DPI model and will be wrong when windows move between
mixed-DPI monitors.  Make the process per-monitor-DPI-aware, then obtain the DPI
from `GetDpiForWindow` (or `GetDpiForMonitor` for monitor-specific work) and use
`GetSystemMetricsForDpi` for DPI-scaled metrics.  Preserve printer `GetDeviceCaps`
uses in `src/plugins/pictview/print.cpp` and rendering code: those query printer
capabilities, not window DPI, and should not be replaced with `GetDpiForWindow`.

### ANSI/default-code-page boundary

Much of this application calls the encoding-neutral Win32 macros with `char`
buffers and `MAX_PATH`.  The most visible path examples are the `SHGetFolderPath`
uses above and `GetShortPathName`/directory API calls in `src/execute.cpp`.
Modernization should make internal filesystem/shell paths UTF-16 or a validated
UTF-8 abstraction, call explicit `W` APIs at the OS boundary, and allocate based
on required length.  Do not switch the global `UNICODE` macro as a mechanical
first step: that would change existing plug-in ABI and `char*` contracts.

## Complete-search command

Run this from the repository root after an update to refresh the detailed line
inventory.  It intentionally excludes vendored SQLite and 7-Zip; remove the two
`--glob` exclusions when auditing third-party upgrades.

```powershell
$apis = 'GetVersionEx','SHGetFolderPath','SHGetSpecialFolderPath',
    'SHGetPathFromIDList','SHFileOperation','SHBrowseForFolder',
    'GetWindowLong','SetWindowLong','GetClassLong','SetClassLong',
    'GetFileSize','SetFilePointer','GetDiskFreeSpace','GetTickCount',
    'GlobalAlloc','GlobalFree','LocalAlloc','LocalFree',
    'lstrcpy','lstrcat','lstrcpyn','lstrlen','lstrcmp','lstrcmpi',
    'wsprintf','wvsprintf','OpenFile','_lopen','_lcreat',
    'GetOpenFileName','GetSaveFileName','RegisterClass','GetScrollRange',
    'GetLocaleInfo','GetDateFormat','GetTimeFormat','GetThreadLocale',
    'GetUserDefaultLCID'
foreach ($api in $apis) {
    "`n## $api"
    rg --pcre2 -n -g '*.{c,cc,cpp,cxx,h,hpp,rc}' `
        "(?<![A-Za-z0-9_])$api\s*\(" src `
        --glob '!src/common/dep/**' --glob '!src/plugins/7zip/**'
}
```

## References

* [GetWindowLongPtr documentation](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-getwindowlongptra)
  recommends the pointer-width API for code that supports both 32- and 64-bit Windows.
* [IFileOperation documentation](https://learn.microsoft.com/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifileoperation)
  identifies it as the replacement for `SHFileOperation` and explains its item,
  error-reporting, and progress advantages.
* [Global and Local Functions](https://learn.microsoft.com/windows/win32/memory/global-and-local-functions)
  explains that these APIs are 16-bit compatibility wrappers, while documenting
  the clipboard/OLE cases that still require `HGLOBAL`.
* [LocalAlloc documentation](https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-localalloc)
  documents the important ownership exception: API results specified for
  `LocalFree` must remain paired with `LocalFree`.
