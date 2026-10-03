// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Precompiled header for the host-facing Delivery Handoff sources only; the
// engine (engine/*.cpp) is host-independent and compiles without it (D-11).
// Standard library headers come before the SDK headers, and the debug
// "new (_NORMAL_BLOCK, ...)" redefinition used by older plug-ins is not
// applied, because it breaks placement new inside the standard containers the
// UI shares with the engine.

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <tchar.h>
#include <commdlg.h> // spl_gen.h declares OPENFILENAME-based helpers
#include <commctrl.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <strsafe.h>
#include <crtdbg.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <ostream> // dbg.h and mhandles.h declare trace streams on top of it
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_gui.h"
#include "spl_menu.h"
#include "spl_vers.h"
#include "dbg.h"
#include "mhandles.h"
#include "winliblt.h"
#include "auxtools.h"
