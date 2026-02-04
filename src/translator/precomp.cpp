// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

// the TASKSCAPEDB project contains three groups of modules
//
// 1) precomp.cpp builds taskscapedb.pch (/Yc"precomp.h")
// 2) modules that use taskscapedb.pch (/Yu"precomp.h")
// 3) commons and tasklist.cpp have their own automatically generated
//    WINDOWS.PCH (/YX"windows.h" /Fp"$(OutDir)\WINDOWS.PCH")
