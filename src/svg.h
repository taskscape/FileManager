// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct NSVGrasterizer;
struct NSVGimage;
void RenderSVGImage(NSVGrasterizer* rast, HDC hDC, int x, int y, const char* svgName, int iconSize, COLORREF bkColor, BOOL enabled);

// Rasterize at the destination size with alpha intact; the caller owns the returned icon.
HICON LoadToolbarSVGIcon(const char* svgName, int iconSize);

// Returns SysColor in the format for the SVG library (BGR instead of Win32 RGB).
DWORD GetSVGSysColor(int index);

//*****************************************************************************
//
// CSVGSprite
//

#define SVGSTATE_ORIGINAL 0x0001 // unchanged original SVG form
#define SVGSTATE_ENABLED 0x0002  // SVG nakolorovane do barvy enabled textu
#define SVGSTATE_DISABLED 0x0004 // SVG nakolorovane do barvy disabled textu
#define SVGSTATE_COUNT 3

// Objekt slouzi k vykresleni SVG prostrednictvi cachovaci bitmapy.
// Primarily holds the color version of the image rendered according to colors in the source SVG.
// Dale dokaze drzet barevne verze bitmapy (odtud Sprite v nazvu - vnitrne pouziva vetsi bitmapu s vice obrazky),
// napriklad "disabled", "active", "selected".
class CSVGSprite
{
public:
    CSVGSprite();
    ~CSVGSprite();

    // zahodi bitmapu, inicializuje promenne na vychozi stav
    void Clean();

    // 'states' je kombinace bitu z rodiny SVGSTATE_*
    BOOL Load(int resID, int width, int height, DWORD states);

    void GetSize(SIZE* s);
    int GetWidth();
    int GetHeight();

    // 'hDC' je cilove DC, kam se ma bitmapa vykreslit
    // 'x' a 'y' jsou cilove souradnice v 'hDC'
    // 'width' and 'height' are the target size; if they are -1, 'Width'/'Height' size is used
    void AlphaBlend(HDC hDC, int x, int y, int width, int height, DWORD state);

protected:
    // nacte resource do pameti, naalokuje buffer o bajt delsi a terminuje resource nulou
    // on success returns a pointer to allocated memory (must be freed), on error returns NULL
    char* LoadSVGResource(int resID);

    // Input 'sz' determines the size in pixels into which the SVG should fit after conversion to bitmap.
    // Pokud je jeden rozmer -1, neni urcen a dopocita se na zaklade zachovani pomeru stran.
    // Pokud nejsou urceny oba rozmery, prevezmou se ze zdrojovych dat.
    // On output, returns the output bitmap size in pixels.
    void GetScaleAndSize(const NSVGimage* image, const SIZE* sz, float* scale, int* width, int* height);

    // Creates a DIB of size 'width' and 'height', returns its handle and data pointer.
    void CreateDIB(int width, int height, HBITMAP* hMemBmp, void** lpMemBits);

    // natonuje SVG 'image' do barvy urcene stavem 'state'
    void ColorizeSVG(NSVGimage* image, DWORD state);

protected:
    int Width; // rozmer jednoho obrazku v bodech
    int Height;
    HBITMAP HBitmaps[SVGSTATE_COUNT];
};

//*****************************************************************************
//
// global variables
//

//extern HBITMAP HArrowRight;         // bitmapa vytvorena z SVG, pouzivame pro tlacitka jako sipku vpravo
//extern SIZE ArrowRightSize;         // rozmery v bodech
//HBITMAP HArrowRight = NULL;
//SIZE ArrowRightSize = { 0 };

extern CSVGSprite SVGArrowRight;
extern CSVGSprite SVGArrowRightSmall;
extern CSVGSprite SVGArrowMore;
extern CSVGSprite SVGArrowLess;
extern CSVGSprite SVGArrowDropDown;
