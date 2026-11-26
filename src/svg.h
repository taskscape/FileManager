// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct NSVGrasterizer;
struct NSVGimage;
void RenderSVGImage(NSVGrasterizer* rast, HDC hDC, int x, int y, const char* svgName, int iconSize, COLORREF bkColor, BOOL enabled);

// Rasterize at the destination size with alpha intact; the caller owns the returned icon.
HICON LoadToolbarSVGIcon(const char* svgName, int iconSize, BOOL grayscale = FALSE);

// Stable module identity keeps plug-in glyphs consistent across toolbars and translated menus.
const char* GetPluginSVGName(const char* dllName);

// Returns SysColor in the format for the SVG library (BGR instead of Win32 RGB).
DWORD GetSVGSysColor(int index);

//*****************************************************************************
//
// CSVGSprite
//

#define SVGSTATE_ORIGINAL 0x0001 // unchanged original SVG form
#define SVGSTATE_ENABLED 0x0002  // SVG colored with the enabled text color
#define SVGSTATE_DISABLED 0x0004 // SVG colored with the disabled text color
#define SVGSTATE_COUNT 3

// The object renders an SVG by means of a caching bitmap.
// Primarily holds the color version of the image rendered according to colors in the source SVG.
// It can also hold color versions of the bitmap (hence Sprite in the name - internally it uses a larger bitmap with multiple images),
// for example "disabled", "active", "selected".
class CSVGSprite
{
public:
    CSVGSprite();
    ~CSVGSprite();

    // discards the bitmap and initializes variables to the default state
    void Clean();

    // 'states' is a combination of bits from the SVGSTATE_* family
    BOOL Load(int resID, int width, int height, DWORD states);

    void GetSize(SIZE* s);
    int GetWidth();
    int GetHeight();

    // 'hDC' is the target DC where the bitmap should be drawn
    // 'x' and 'y' are the target coordinates in 'hDC'
    // 'width' and 'height' are the target size; if they are -1, 'Width'/'Height' size is used
    void AlphaBlend(HDC hDC, int x, int y, int width, int height, DWORD state);

protected:
    // loads the resource into memory, allocates a buffer one byte longer, and terminates the resource with a null
    // on success returns a pointer to allocated memory (must be freed), on error returns NULL
    char* LoadSVGResource(int resID);

    // Input 'sz' determines the size in pixels into which the SVG should fit after conversion to bitmap.
    // If one dimension is -1, it is unspecified and is computed so the aspect ratio is preserved.
    // If both dimensions are unspecified, they are taken from the source data.
    // On output, returns the output bitmap size in pixels.
    void GetScaleAndSize(const NSVGimage* image, const SIZE* sz, float* scale, int* width, int* height);

    // Creates a DIB of size 'width' and 'height', returns its handle and data pointer.
    void CreateDIB(int width, int height, HBITMAP* hMemBmp, void** lpMemBits);

    // tints the SVG 'image' to the color determined by 'state'
    void ColorizeSVG(NSVGimage* image, DWORD state);

protected:
    int Width; // size of one image in points
    int Height;
    HBITMAP HBitmaps[SVGSTATE_COUNT];
};

//*****************************************************************************
//
// global variables
//

//extern HBITMAP HArrowRight;         // bitmap created from SVG, used for buttons as a right arrow
//extern SIZE ArrowRightSize;         // dimensions in points
//HBITMAP HArrowRight = NULL;
//SIZE ArrowRightSize = { 0 };

extern CSVGSprite SVGArrowRight;
extern CSVGSprite SVGArrowRightSmall;
extern CSVGSprite SVGArrowMore;
extern CSVGSprite SVGArrowLess;
extern CSVGSprite SVGArrowDropDown;
