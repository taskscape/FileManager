// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Add _CRTDBG_MAP_ALLOC macro to DEBUG version of project, otherwise leak source is not shown.

#if defined(_DEBUG) && !defined(HEAP_DISABLE)

#define GCHEAP_MAX_USED_MODULES 100 // how many modules at most should be remembered for load before leak output

// called for modules where memory leaks may be reported, if memory leaks are detected,
// "as image" load occurs (without module init) of all such registered modules (during
// memory leak check these modules are already unloaded), and only then memory leak output = visible
// .cpp module names instead of "#File Error#" messages, also MSVC doesn't bother with a bunch of generated
// exceptions (module names are available)
// can be called from any thread
void AddModuleWithPossibleMemoryLeaks(const TCHAR* fileName);

#endif // defined(_DEBUG) && !defined(HEAP_DISABLE)
