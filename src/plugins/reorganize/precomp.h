// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <CommDlg.h>
#include <crtdbg.h>
#include <stdio.h>
#include <commctrl.h>
#include <tchar.h>
#include <ostream>
#include <streambuf>

#if defined(_DEBUG) && defined(_MSC_VER)
#define new new (_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

#include "versinfo.rh2"

#include "spl_com.h"
#include "spl_base.h"
#include "spl_file.h"
#include "spl_gen.h"
#include "spl_gui.h"
#include "spl_menu.h"
#include "spl_fs.h"
#include "spl_vers.h"

#include "dbg.h"
#include "auxtools.h"

#include "reorganize.rh"
#include "lang/lang.rh"
