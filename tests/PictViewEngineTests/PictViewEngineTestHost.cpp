// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

/* Host stubs that let the PictView WIC engine be linked into a console test.
 *
 * The engine only reaches out of its own two translation units for the plug-in
 * globals below, so satisfying them here exercises the real PVWicEngine.cpp and
 * PVWicSave.cpp rather than a copy of them. Everything the viewer, thumbnail
 * loader and printer would supply is deliberately inert: the test drives the
 * CPVW32DLL table directly.
 */
#include "precomp.h"

#include "lib\\pvw32dll.h"
#include "renderer.h"
#include "pictview.h"

// Defined by the engine's own translation units in the product build; here they
// stand in for the plug-in host that the test does not need.
CPVW32DLL PVW32DLL;
CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;

char* LoadStr(int resID)
{
    static char buffer[64];
    _snprintf_s(buffer, _countof(buffer), _TRUNCATE, "<string %d>", resID);
    return buffer;
}

PVCODE CreateThumbnail(LPPVHandle, LPPVSaveImageInfo, int, DWORD, DWORD, int, int,
                       CSalamanderThumbnailMakerAbstract*, DWORD, TProgressProc, void*)
{
    return PVC_OK;
}

PVCODE SimplifyImageSequence(LPPVHandle, HDC, int, int, LPPVImageSequence&, const COLORREF&)
{
    return PVC_OK;
}
