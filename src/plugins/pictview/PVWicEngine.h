// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

/* Windows Imaging Component back-end for the PictView plug-in.
 *
 * The plug-in talks to its imaging engine exclusively through the CPVW32DLL
 * function table, which used to be filled either from the proprietary
 * PVW32Cnv.dll (x86, in-process) or from the SalPVEnv.exe envelope (x64, over
 * shared memory).  Neither is redistributable, so both paths are replaced by
 * the in-process WIC implementation declared here.
 *
 * No WIC interface pointer ever outlives a single engine call: an image keeps
 * only its source (a path or an owned byte buffer) plus the decoded surface,
 * and re-creates a decoder whenever it needs pixels again.  That keeps the
 * engine indifferent to which thread calls it and to whether that thread has
 * an apartment, which matters because the viewer, the thumbnail loader and the
 * batch converter all drive the table from threads of their own.
 */
#pragma once

// Fills PVW32DLL with the WIC-backed implementation. Returns FALSE and reports
// the reason to the user when WIC itself is unusable.
BOOL InitWicEngine(HWND hParentWnd);

// Releases process-wide engine state. Safe to call when InitWicEngine failed.
void ReleaseWicEngine();

// Raw scanlines of the frame currently decoded into 'Img', for the save path in
// PVWicSave.cpp. Returns FALSE when no frame has been decoded yet.
struct CWicSurface
{
    const BYTE* Bits;      // top-down rows
    DWORD Stride;          // allocation stride, DWORD-aligned
    DWORD Width, Height;
    DWORD Bpp;             // 1, 4, 8 or 24
    const RGBQUAD* Palette; // NULL for 24bpp
    DWORD PaletteColors;
    const BYTE* AlphaBits; // 32bpp BGRA source, NULL unless the image has alpha
    DWORD AlphaStride;
    COLORREF BkColor;
};

BOOL WicGetSurface(LPPVHandle Img, CWicSurface* surface);

// Decodes 'imageIndex' into the image if a different frame (or nothing) is
// currently decoded, so save paths can run without a preceding PVReadImage2.
PVCODE WicEnsureFrameDecoded(LPPVHandle Img, int imageIndex, TProgressProc progress, void* progressArg);

// Localized engine text for a PVC_/PVCS_/PVF_ code, "" when the language
// module has no string for it.
const char* WicLoadEngineText(int code);

// Implemented in PVWicSave.cpp.
PVCODE WINAPI WicSaveImage(LPPVHandle Img, const char* OutFName, LPPVSaveImageInfo pSii,
                           TProgressProc Progress, void* AppSpecific, int ImageIndex);
DWORD WINAPI WicIsOutCombSupported(int Fmt, int Compr, int Colors, int ColorModel);
