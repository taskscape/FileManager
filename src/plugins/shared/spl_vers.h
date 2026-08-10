// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

// WARNING: cannot be replaced by "#pragma once" because it is included from .rc file and it seems resource compiler does not support "#pragma once"
#ifndef __SPL_VERS_H
#define __SPL_VERS_H

#if defined(APSTUDIO_INVOKED) && !defined(APSTUDIO_READONLY_SYMBOLS)
#error this file is not editable by Microsoft Visual C++
#endif //defined(APSTUDIO_INVOKED) && !defined(APSTUDIO_READONLY_SYMBOLS)

// conversion macros num->str
#define VERSINFO_xstr(s) VERSINFO_str(s)
#define VERSINFO_str(s) #s

// The release-managed major stays separate from the automatically injected build date.
#define VERSINFO_SALAMANDER_MAJOR 6
#define VERSINFO_SALAMANDER_MINORA 0
#define VERSINFO_SALAMANDER_MINORB 0

// Version format: MAJOR.BUILDDATE  (e.g. "6.20260620")
// VERSINFO_SALAMANDER_BUILDDATE is the build date in YYYYMMDD format.
// It is normally injected by the build: src\Directory.Build.props computes the current
// date and passes VERSINFO_SALAMANDER_BUILDDATE_DYNAMIC=YYYYMMDD to both the C/C++ and
// resource compilers, so the version always reflects the actual build date. The literal
// fallback is used only for builds that bypass that props file (e.g. ad-hoc compiles).
#ifdef VERSINFO_SALAMANDER_BUILDDATE_DYNAMIC
#define VERSINFO_SALAMANDER_BUILDDATE VERSINFO_xstr(VERSINFO_SALAMANDER_BUILDDATE_DYNAMIC)
#else
#define VERSINFO_SALAMANDER_BUILDDATE "20260620"
#endif
#define VERSINFO_SALAMANDER_VERSION VERSINFO_xstr(VERSINFO_SALAMANDER_MAJOR) "." VERSINFO_SALAMANDER_BUILDDATE VERSINFO_BETAVERSION_TXT
#define VERSINFO_SAL_SHORT_VERSION VERSINFO_xstr(VERSINFO_SALAMANDER_MAJOR) VERSINFO_SALAMANDER_BUILDDATE VERSINFO_BETAVERSIONSHORT_TXT

#ifdef VERSINFO_MAJOR      // is defined only if used from plugin
#if (VERSINFO_MINORB == 0) // do not write zero in hundredths: 2.50 -> 2.5
#define VERSINFO_VERSION VERSINFO_xstr(VERSINFO_MAJOR) "." VERSINFO_xstr(VERSINFO_MINORA) VERSINFO_BETAVERSION_TXT
#define VERSINFO_VERSION_NO_PLATFORM VERSINFO_xstr(VERSINFO_MAJOR) "." VERSINFO_xstr(VERSINFO_MINORA) VERSINFO_BETAVERSION_TXT_NO_PLATFORM
#else
#define VERSINFO_VERSION VERSINFO_xstr(VERSINFO_MAJOR) "." VERSINFO_xstr(VERSINFO_MINORA) VERSINFO_xstr(VERSINFO_MINORB) VERSINFO_BETAVERSION_TXT
#define VERSINFO_VERSION_NO_PLATFORM VERSINFO_xstr(VERSINFO_MAJOR) "." VERSINFO_xstr(VERSINFO_MINORA) VERSINFO_xstr(VERSINFO_MINORB) VERSINFO_BETAVERSION_TXT_NO_PLATFORM
#endif
#endif

#ifdef _WIN64
#define SAL_VER_PLATFORM "x64"
#else // _WIN64
#define SAL_VER_PLATFORM "x86"
#endif // _WIN64

// VERSINFO_BUILDNUMBER:
//
// Used to easily distinguish versions of all modules between individual Salamander
// versions (it is the last component of the version number of all plugins and
// of Salamander). Increase with each version (IB, DB, PB, beta, release or even
// even a test version sent to one user). An overview of various version types
// is in doc\versions.txt. Always add a comment describing which Salamander
// version the newly used build number belongs to.
//
// Overview of used VERSINFO_BUILDNUMBER values:
// 9 - 2.5 beta 9
// 10 - 2.5 beta 10
// 11 - 2.5 beta 11
// 13 - 2.5 RC1
// 14 - 2.5 RC2
// 15 - 2.5 RC3
// 0 - 2.5
// 16 - 2.51
// 18 - 2.52 beta 1
// 29 - 2.52 beta 2
// 32 - 2.52
// 49 - 2.53 beta 1
// 57 - 2.53 beta 2
// 63 - 2.53
// 69 - 2.54
// 91 - 3.0 beta 1
// 97 - 3.0 beta 2
// 108 - 3.0 beta 3
// 114 - 3.0 beta 4
// 120 - 3.0
// 126 - 3.01
// 132 - 3.02
// 138 - 3.03
// 144 - 3.04
// 150 - 3.05
// 156 - 3.06
// 165 - 3.07
// 174 - 3.08
// 175 - 3.08 (SDK)
// 176 - 3.08 (CB176)
// 177 - 4.0 beta 1 (DB177)
// 178 - 4.0 beta 1 (CB178)
// 179 - 4.0 beta 1 (IB179)
// 180 - 4.0
// 181 - 4.0 (SDK)
// 182 - 4.0 (CB182)
// 183 - 5.0

// ! IMPORTANT: new build numbers must be written to the "default" branch first,
//              and only then to side branches (the complete list is only in the "default" branch)
#define VERSINFO_BUILDNUMBER 183

// VERSINFO_BETAVERSION_TXT:
//
// Changes with each build; for a release version, VERSINFO_BETAVERSION_TXT="".
// If we release special corrective beta versions like 2.5 beta 9a, we increase
// VERSINFO_BUILDNUMBER by one and set VERSINFO_BETAVERSION_TXT==" beta 9a".
//
// VERSINFO_BETAVERSIONSHORT_TXT is used for bug report naming, it is the shortest possible notation

// examples ("x86" is for 32-bit version, "x64" for 64-bit version, in following examples
// x86/x64 are interchangeable): " (x86)" (for release versions), " beta 2 (x64)", " beta 2 (SDK x86)",
// " RC1 (x64)", " beta 2 (IB21 x86)", " beta 2 (DB21 x64)", " beta 2 (PB21 x86)"
#define VERSINFO_BETAVERSION_TXT " (" SAL_VER_PLATFORM ")"
#define VERSINFO_BETAVERSION_TXT_NO_PLATFORM "" // copy the line above + remove SAL_VER_PLATFORM + if parentheses are empty, remove them + remove extra spaces

// examples (x86/x64 see previous paragraph): "x86" (for release versions), "B2x64", "B2SDKx86",
// "RC1x64", "B2IB21x86", "B2DB21x64", "B2PB21x86"
#define VERSINFO_BETAVERSIONSHORT_TXT SAL_VER_PLATFORM

// LAST_VERSION_OF_SALAMANDER:
//
// Support for checking Salamander version currency, which internal plugins
// (distributed in one package with Salamander) perform during the entry point
// (SalamanderPluginEntry), see CSalamanderPluginEntryAbstract::GetVersion()
// (in spl_base.h). It is mainly for simplicity: an internal plugin can call
// any method from the Salamander interface because, after checking for the last
// Salamander version, it is guaranteed that Salamander contains it (the only risk
// is loading into a newer Salamander version, which must also contain these methods).
//
// It is also used the other way around: so an internal plugin is certain that
// Salamander will call all its methods (including the newest ones), it returns this
// version as the version for which the plugin was built (see SalamanderPluginGetReqVer export).
//
// If some plugin returns a lower version than LAST_VERSION_OF_SALAMANDER from
// SalamanderPluginGetReqVer (for backward compatibility with older Salamander
// versions), it should add the SalamanderPluginGetSDKVer export and return
// LAST_VERSION_OF_SALAMANDER from it (SDK version used to build the plugin), so
// Salamander (for example the current or newer one) can also use plugin methods
// that did not yet exist in the version returned from SalamanderPluginGetReqVer.
//
// When changing the interface, follow the procedure described in doc\how_to_change.txt.
//
// Overview of used LAST_VERSION_OF_SALAMANDER values:
//   1  - 1.6 beta 4 + 5
//   2  - 1.6 beta 6
//   3  - 1.6 beta 7
//   4  - 2.0
//   5  - 2.5 beta 1
//   6  - 2.5 beta 2
//   7  - 2.5 beta 3
//   8  - 2.5 beta 4
//   9  - 2.5 beta 5
//   10 - 2.5 beta 6
//   11 - 2.5 beta 7
//   12 - 2.5 beta 8
//   13 - 2.5 beta 9
//   14 - 2.5 beta 10
//   15 - 2.5 beta 10a
//   16 - 2.5 beta 11
//   17 - 2.5 beta 12 (internal only, RC1 was released instead)
//   18 - 2.5 RC1
//   19 - 2.5 RC2
//   20 - 2.5 RC3
//   21 - 2.5
//   22 - 2.51
//   23 - 2.52 beta 1 (CAUTION: SDK incompatible with previous and later versions)
//   29 - 2.52 beta 2
//   31 - 2.52
//   39 - 2.53 beta 1 + 2.53 beta 1a
//   41 - 2.53 beta 2
//   43 - 2.53
//   45 - 2.54
//   54 - 3.0 beta 1
//   56 - 3.0 beta 2
//   60 - 3.0 beta 3
//   62 - 3.0 beta 4
//   64 - 3.0
//   66 - 3.01
//   68 - 3.02
//   70 - 3.03
//   72 - 3.04
//   74 - 3.05
//   76 - 3.06
//   79 - 3.07
//   81 - 3.08
// ! IMPORTANT: all versions from VC2008 must be < 100, all versions from VC2019 must be >= 100,
//              new version numbers must be written to the "default" branch first, and only
//              then to side branches (the complete list is only in the "default" branch)
//   101 - 4.0 beta 1 (DB177)
//   102 - 4.0
//   103 - 5.0

#define LAST_VERSION_OF_SALAMANDER 103
#define REQUIRE_LAST_VERSION_OF_SALAMANDER "This plugin requires Open Salamander 5." VERSINFO_SALAMANDER_BUILDDATE " (" SAL_VER_PLATFORM ") or later."

#endif // __SPL_VERS_H
