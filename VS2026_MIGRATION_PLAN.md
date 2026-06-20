# Visual Studio 2026 Migration Plan

Tracking the move of the FileManager (Open Salamander) solution from the **v143 (VS 2022)**
build toolset to building under **Visual Studio 2026**.

**Chosen approach: Option A** — keep the projects targeting `v143` and make VS 2026 use the
v143 toolset (install the v143 component). This is the lowest-risk path: no project/code
changes, builds are bit-for-bit identical. Option B (retarget to VS 2026's `v145` toolset) is
documented at the bottom as the future alternative.

---

## How to read this file

Each task is a checkbox:

- `- [ ]` = **outstanding** (not done yet)
- `- [x]` = **done** (with a short note on what was verified/changed and by whom: "agent" = done
  by Claude in-repo; "you" = a manual/GUI/installer step the agent cannot perform)
- `- [~]` = **partially done / blocked** — see the inline note for what remains

A task done by the **agent** means the change is already in the working tree (e.g. files deleted,
findings recorded). A task marked **(you)** is a manual step (Visual Studio Installer, opening the
IDE, building) that must be performed on the machine — the agent cannot drive the GUI installer.

When you finish a manual task, change its `- [ ]` to `- [x]` and add a dated note.
"Outstanding work" at any time = every box that is still `- [ ]` or `- [~]`.

Last updated: 2026-06-20 (agent).

---

## Findings (investigation results — reference)

- **Cause of the warnings**: the solution is opened in **Visual Studio 2026 Insiders**
  (`C:\Program Files\Microsoft Visual Studio\18\Insiders`) but every project targets
  `PlatformToolset = v143`, and VS 2026 does **not** ship/own v143. The warnings/“Designtime build
  failed … IntelliSense might be unavailable” are purely a missing-toolset problem, **not** a code problem.
- **VS 2026 toolset**: token **`v145`**, MSBuild VCTargets **`v180`**, **MSVC 14.51.36231**.
  Only `v145` is present under the VS 2026 install.
- **VS 2022 Community is also installed** (`...\2022\Community`) with MSVC **14.44.35207** (= v143),
  so the code already builds with v143; only VS 2026 lacks the toolset.
- **Toolset usage across the repo (per-project; not centralized in any `.props`):**
  - **Now uniformly `v143`** (352 entries) after the cleanup below. Originally: `v143` ×350,
    `v142` ×2 (`reglib`), `v140` ×8 (legacy `vcproj2015_manison`).
  - `v142` (`src/reglib/REGLIBA.vcxproj`, a standalone app in its own `regliba.sln`) → **retargeted to `v143`** (agent).
  - `v140` (`src/plugins/portables/vcproj2015_manison/*`, legacy VS2015 duplicate) → **retired** (agent).
  - **Net effect: VS 2026 needs exactly ONE toolset component — `v143`.**
- **`WindowsTargetPlatformVersion = 10.0`** everywhere → "use latest installed SDK". Installed SDKs:
  `10.0.19041.0`, `10.0.22621.0`, `10.0.26100.0` → resolves fine under VS 2026. **No SDK change needed.**
- **`LanguageStandard`**: 318× `stdcpplatest`, 12× `stdcpp14`, 4× `stdcpp17`, 4× `stdcpp20`.
  (Only relevant to Option B — `stdcpplatest` + MSVC 14.51 is where new conformance errors could appear.)
- **Solution format**: `salamand.sln` is stamped Format 12 / "Version 17" (87 project refs). VS 2026
  will rewrite the version stamp on save (cosmetic).

---

## Option A — make VS 2026 build with the existing v143 toolset

### Tasks

- [x] **Confirm a Windows 11 SDK is installed** so `WindowsTargetPlatformVersion=10.0` resolves under
  VS 2026. — agent: found `10.0.19041.0`, `10.0.22621.0`, `10.0.26100.0`.
- [x] **Confirm v143 exists on the machine** (proves the toolset can be added to VS 2026). — agent:
  VS 2022 Community has MSVC 14.44.35207 (v143).
- [x] **Retire the dead `vcproj2015_manison` (v140) project set** so VS 2026 doesn't trip over a v140
  toolset it can't load. — agent: verified no external references in any `.sln/.vcxproj/.props/script`,
  then `git rm -r src/plugins/portables/vcproj2015_manison/` (staged deletion, **not committed**). The
  live portables project remains at `src/plugins/portables/vcxproj/portables.vcxproj` (v143).
- [x] **v143 toolset installed into VS 2026.** — agent (elevated): installed component
  **`Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`** (MSVC 14.44 / 17.14 = **v143**, latest
  v143; matches the 14.44 already in the side-by-side VS 2022). Result: VS 2026 now has MSVC
  `14.44.35207` (v143) alongside `14.51.36231` (v145), and the **`v143` PlatformToolset** is registered
  (`...\18\Insiders\MSBuild\Microsoft\VC\v170\...\PlatformToolsets\v143`). Installer exit `3010` =
  success, **reboot recommended**.
  - **IMPORTANT — correct component ID**: in VS 2026 the generic `VC.Tools.x86.x64` ("latest") means
    **v145**, not v143. To get v143 you must use the **fixed-version side-by-side** component
    `VC.14.44.17.14.x86.x64`. (My first attempt used a non-existent id `VC.143.x86.x64`, which the
    installer silently treated as a no-op — do not use that.)
  - Command used (run from an **elevated** prompt; quote the spaced path or it gets truncated):
    ```
    "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe" modify --installPath "C:\Program Files\Microsoft Visual Studio\18\Insiders" --add Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64 --quiet --norestart
    ```
  - GUI equivalent: Installer → VS Community 2026 → Modify → **Individual components** → search "14.44"
    → check **"MSVC v143 - VS 2022 C++ x64/x86 build tools (v14.44-17.14)"**.
- [x] **Toolset resolution verified via command-line build.** — agent: built `src\vcxproj\sqlite\sqlite.vcxproj`
  (`Debug|x64`) with VS 2026's MSBuild (`...\18\Insiders\MSBuild\Current\Bin\MSBuild.exe`) →
  **Build succeeded, 0 warnings, 0 errors**, no "build tools for v143 cannot be found". Confirms VS 2026
  now resolves `<PlatformToolset>v143</PlatformToolset>`.
- [ ] **(you) Reload the solution in VS 2026** and confirm the IDE warnings are gone / IntelliSense works
  (the underlying cause is fixed; this just refreshes the IDE — a restart also clears the 3010 reboot flag).
- [x] **Full `salamand.sln` build (`Debug|x64`) run via VS 2026 MSBuild.** — agent: **`salamand.exe`
  (7.8 MB) + 33 plugins built clean** (0 warnings). Only **2 projects fail — `pictview` and `portables`**
  — and solely on `atlbase.h` (the missing ATL component above), not on the toolset. This validates the
  v143 migration end-to-end. It also surfaced and fixed a regression in `src/utf8gui.h` (an unused
  `ListBoxAddStringUtf8` helper used the undeclared `LB_SETUNICODEFORMAT`; removed — unstaged change).
- [ ] **(you) After installing ATL**, rebuild to confirm `pictview`/`portables` compile, and build the
  other configs (`Release|x64`, `Debug|Win32`, `Release|Win32`) for full coverage.
- [x] **`reglib` (`REGLIBA.vcxproj`) handled** — agent: it's a standalone **Application** project in its
  own `regliba.sln` (not in `salamand.sln`; only `!clear.bat` references it). Retargeted `v142 → v143`
  so the whole repo needs only the single v143 component (no separate v142 install). Edit is unstaged.

### Requirements / assumptions to verify

- [x] **v143 IS available in VS 2026 and is now installed.** agent: VS 2026 offers v143 only as
  **fixed-version side-by-side** components (`VC.14.30.17.0` … `VC.14.44.17.14`); the generic
  `VC.Tools.x86.x64` = v145. Installed `VC.14.44.17.14.x86.x64` (latest v143). Confirmed by the clean
  `sqlite` build above.
- [x] **MFC not needed; ATL IS needed.** — agent: all 16 `<UseOfMfc>` are `false` (no MFC). BUT
  `pictview` and `portables` directly `#include <atlbase.h>`, so they need the v143 **ATL** component
  **`Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL`**. (My earlier "ATL not needed" only checked
  `<UseOfMfc>` and was wrong — corrected after the build surfaced `atlbase.h` errors.)
  - **Install status: BLOCKED in this automated context.** The elevated `vs_installer --add ...ATL`
    returns exit **8006** because the installer's online feed refresh (`aka.ms/vs/channels`,
    `aka.ms/vs/installer/latest/feed`) is **canceled** here (`"Failed to update the latest installer
    feed: A task was canceled"`). Package downloads work (the v143 toolset installed fine), but the
    aka.ms feed step fails. **(you)**: install ATL via the **GUI Installer** (handles the feed
    interactively) — search "14.44" → "MSVC v143 ATL (v14.44-17.14)" — or retry the CLI when the
    network/feed is reachable.
- [x] **Spectre libs not needed** — agent: no `<SpectreMitigation>` element in any project (default off).
- [x] **WindowsTargetPlatformVersion = 10.0 resolves** — verified (SDK 10.0.26100.0 present).
- [ ] **(you) Confirm the team's intended primary IDE.** If VS 2026 is meant to fully replace VS 2022,
  plan Option B (below) as a follow-up so the repo no longer depends on the legacy v143 component.

---

## Status summary

**Done (agent):** findings recorded; SDK + v143 presence verified; `vcproj2015_manison` retired;
`reglib` retargeted `v142 → v143`; MFC/ATL and Spectre confirmed not required. Result: the repo now
uses a **single toolset, `v143`** (352 entries), so VS 2026 needs exactly one component. All changes
are **unstaged** on the current branch (nothing committed). No main-app `.vcxproj` toolset values
changed — Option A deliberately keeps v143.

**Done (agent, elevated):** installed v143 (`VC.14.44.17.14.x86.x64`) into VS 2026; verified the `v143`
PlatformToolset registered. Ran a full `salamand.sln` `Debug|x64` build via VS 2026 MSBuild:
**`salamand.exe` + 33 plugins build clean (0 warnings)**; only `pictview` + `portables` fail, solely on
the missing **ATL** component. Fixed a regression in `src/utf8gui.h` found by the build. The reported
"build tools for v143 cannot be found" cause is **resolved** and the migration is validated.

**Outstanding:**
1. **(you) Install the v143 ATL component** (`...VC.14.44.17.14.ATL`) — blocked here by an installer
   feed-cancellation (exit 8006); do it via the GUI Installer. Needed only by `pictview` + `portables`.
2. **(you) Reboot / restart VS 2026** (the v143 install reported 3010 = reboot recommended) and reload
   the solution; confirm IDE warnings/IntelliSense are clear.
3. **(you) After ATL**, rebuild (all 4 configs) to confirm a fully clean solution.

---

## Option B — retarget to VS 2026's `v145` toolset (IN PROGRESS)

Goal: build natively on VS 2026's own toolset (`v145` = MSVC 14.51) so the repo no longer depends on
the side-by-side v143 component. Same checkbox legend as the top of this file
(`[ ]` outstanding, `[x]` done, `[~]` partial/blocked; **agent** = done in-repo, **you** = manual).
This section is updated as work is done and as new work is discovered.

### Discoveries (Option B specifics)

- **`v145` = MSVC 14.51.36231**; VCTargets `v180`. Desktop PlatformToolset token is **`v145`**.
- **Prerequisite — the desktop v145 toolset is NOT installed.** As shipped here, VS 2026 has only the
  **v143** desktop toolset (the one installed for Option A → MSVC 14.44); there is **no**
  `Microsoft.VCToolsVersion.v145.default.txt` and no desktop `v145` PlatformToolset folder (only a
  *Windows Store* `v145` exists). MSVC 14.51 binaries are present but not wired as a desktop toolset.
  So a retarget to `v145` requires installing the v145 desktop toolset first.
- **Component IDs (from the VS 2026 catalog):** desktop toolset
  `Microsoft.VisualStudio.Component.VC.14.51.x86.x64`; ATL `Microsoft.VisualStudio.Component.VC.14.51.ATL`
  (needed by `pictview` + `portables`). MFC/Spectre still not needed.
- **Installer risk:** the same elevated `vs_installer` path can return exit **8006** (aka.ms feed
  refresh "A task was canceled") — see Option A. If blocked, install via the GUI Installer.
- **Conformance risk (the big one):** MSVC 14.51 + the repo's pervasive `<LanguageStandard>stdcpplatest`
  is much stricter than v143/14.44 on this decades-old C++ — expect new errors/warnings needing code
  fixes (iterative build-and-fix). v145 is also a **prerelease** toolset.

### Tasks

- [x] **Install the v145 desktop toolset + ATL into VS 2026** — agent (elevated): `VC.14.51.x86.x64` +
  `VC.14.51.ATL`, exit 0. `Microsoft.VCToolsVersion.v145.default.txt` and the desktop `v145`
  PlatformToolset are now present.
- [x] **Retarget the projects** `v143` → `v145`. — agent: bulk replace across `src\**\*.vcxproj` →
  **346 replacements in 91 files**; distribution now 100% `v145`. `WindowsTargetPlatformVersion` left at
  `10.0`. (reglib REGLIBA.vcxproj included.) Unstaged.
- [x] **Build `salamand.sln` (`Debug|x64`) on v145.** — agent: after the `/RTCc` fix, **clean build —
  exit 0, 0 errors, 0 warnings**. `salamand.exe` (5.20260620) + **35 plugins** built, incl. `pictview` +
  `portables` (ATL resolved). No source conformance changes were required.
- [x] **Fix conformance errors** — only one root cause (`/RTCc`/STL1013), fixed below. The decades-old
  code otherwise compiles clean under MSVC 14.51 / `stdcpplatest`.
- [x] **Build remaining configs** (`Release|x64`, `Debug|Win32`, `Release|Win32`). — agent: after the
  no-CRT fixes below, **all four configs build** on v145. `Debug|x64` and `Debug|Win32` are fully clean;
  `Release|x64`/`Release|Win32` are clean **except** the `zip2sfx` code-signing post-build step
  (`MSB3073`) — an environment issue (no signing cert locally), not a toolset/code problem, and it
  succeeds in CI where the sign script tolerates a missing cert.
- [x] **Update CI (GitHub Actions) to handle the v145 retarget.** — agent: see the CI section below.
- [ ] **(optional) Centralize the toolset**: now that `src\Directory.Build.props` exists, set
  `<PlatformToolset>` there once and remove the per-project values, so the next bump is one line.
  (Deferred: needs the per-project values removed and import-timing verified. Note CI relies on the
  `/p:PlatformToolset=v143` global override, which works regardless of per-project vs centralized.)

### Discovered conformance work (filled in as the v145 build surfaces errors)

- [x] **`/RTCc` rejected by MSVC 14.51 STL** (`error C2338 / STL1013: "The STL doesn't support /RTCc
  because it rejects conformant code. Remove the /RTCc option."`). Hit by **39 projects** — it's the
  `<SmallerTypeCheck>true</SmallerTypeCheck>` Debug option (v143/14.44 tolerated it; 14.51 hard-errors;
  `/RTCc` is non-conformant and discouraged by MS). — agent: set it to `false` in the 8 files that
  defined it: `plugins\shared\vcxproj\plugin_debug.props` (covers all plugins), `vcxproj\sal_debug.props`
  (salamand), `vcxproj\sqlite\sqlite_debug.props`, `vcxproj\salmon\salmon_debug.props`, and the
  `sfxmake`, `reglib`, `translator`, `tserver` vcxproj. Unstaged.
- [x] **No-CRT helper modules: unresolved `strcpy`/`strlen`/`memset` under v145** (linker `LNK2001`/
  `LNK2019`). The tiny CRT-free utilities (`salextx64` shell ext, `salopen`, `salspawn`) ignore the
  default libs and supply their own funcs via `lstrfix.inc` under `LSTRFIX_WITHOUT_RTL`; v145's
  optimizer now emits implicit `strcpy/strlen` (Release) and `memset` (x86) where v143/14.44 inlined
  them. — agent fix (final, verified): (a) added **`memset` only** to `src\common\lstrfix.inc` under
  `LSTRFIX_WITHOUT_RTL` with `#pragma function(memset)` (initially also added memcpy/memmove but those
  are already provided → `LNK2005`, so removed); (b) moved `LSTRFIX_WITHOUT_RTL` into the shared
  `shellext_base.props` + `salspawn_base.props` (was only in `_debug.props`, so Release lacked it);
  (c) set `<WholeProgramOptimization>false</WholeProgramOptimization>` in `shellext_release.props` +
  `salspawn_release.props` — defining the CRT helpers is incompatible with `/GL` (`C2268`). Rebuild
  confirmed `salextx64` + `salopen` now link.
- [x] **`LNK1000: Internal error during IMAGE::EmitRelocations`** on the `lang_*` resource DLLs
  (Win32, and one Release|x64) — an **intermittent** v145 *prerelease-linker* crash. It hit different
  projects on different runs and **did not recur** on rebuild (Debug|Win32 then linked clean). Treat as
  a flaky Preview-toolset issue (retry the build); not a code problem. Worth keeping an eye on / report
  upstream if it persists.
- [ ] **`zip2sfx` Release: code-signing post-build step fails** (`MSB3073` from
  `tools\codesign\sign_with_retry.cmd`) **only in my local env** (no signing cert). **Not** a
  toolset/v145 problem; the CI sign script tolerates a missing cert and the `zip2sfx.exe` itself builds.
  No action needed for the migration; relevant only if signing locally.

### Install / build results (Option B)

- [x] **v145 desktop toolset + ATL installed** — agent (elevated): `VC.14.51.x86.x64` + `VC.14.51.ATL`,
  exit 0. `Microsoft.VCToolsVersion.v145.default.txt` and the desktop `v145` PlatformToolset are present.
- [x] **Final v145 build status — all four configs build.** `Debug|x64` + `Debug|Win32`: fully clean
  (0 errors, 0 warnings; `salamand.exe` + 35 plugins incl. ATL-using `pictview`/`portables`).
  `Release|x64` + `Release|Win32`: clean except the local-only `zip2sfx` signing step.
  **Bottom line: the decades-old codebase compiles on VS 2026 / MSVC 14.51 / `stdcpplatest` with the
  only required changes being build-option fixes (`/RTCc` off, `/GL` off for 2 no-CRT modules) plus a
  one-line `memset` helper — no real source/conformance changes.**

### CI (GitHub Actions)

- [x] **Updated both build workflows for the v145 retarget.** GitHub-hosted runners (`windows-2022`,
  `windows-2025`) ship VS2022 = **v143** only; VS2026/**v145** is prerelease and cannot be added to
  VS2022. So CI pins the toolset to v143 via a global MSBuild property
  (**`/p:PlatformToolset=v143`**, added through a new `PLATFORM_TOOLSET` env var), which **overrides**
  the per-project `<PlatformToolset>v145</PlatformToolset>`. Same sources, builds identically.
  - `.github/workflows/pr-msbuild.yml` (Debug, Win32+x64, `/warnaserror`) and
    `.github/workflows/build-installer.yml` (Release x64 → installer) both updated. — agent.
  - Verified locally: `sqlite.vcxproj` (declares `v145`) builds with `/p:PlatformToolset=v143` → exit 0
    (the override works exactly as CI will use it).
  - The new `src\Directory.Build.props` (dynamic build date) works in CI unchanged (MSBuild computes
    the date). New source files (`utf8gui.h`, etc.) are picked up automatically.
  - **(you, optional)** To run CI on v145 instead, use a self-hosted/custom runner with VS2026 and set
    `PLATFORM_TOOLSET: v145` — not recommended yet (prerelease toolset; intermittent `LNK1000`).

---

## Verification commands (re-run to re-check state)

```powershell
# Toolset distribution across all projects
Get-ChildItem -Recurse -Filter *.vcxproj |
  Select-String -Pattern "<PlatformToolset>([^<]+)</PlatformToolset>" -AllMatches |
  % { $_.Matches } | % { $_.Groups[1].Value } | Group-Object | ft Count,Name -Auto

# VS 2026 toolset present
Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Tools\MSVC" -Directory | % Name

# Installed Windows SDKs
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" -Directory | % Name

# Confirm the legacy folder is gone
Test-Path "src\plugins\portables\vcproj2015_manison"   # should be False
```
