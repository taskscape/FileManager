// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/************************************************************************************

What can be extracted from HICON provided by the OS?

  Using GetIconInfo() the OS returns copies of MASK and COLOR bitmaps. These can be further
  examined by calling GetObject(), through which we extract geometry and color arrangement.
  These are copies of bitmaps, not original bitmaps held inside the OS. MASK is
  always a 1-bit bitmap. COLOR is a bitmap compatible with the screen DC. There is therefore no
  way to obtain information about the real color depth of the COLOR bitmap from this data.

  A special case are purely black and white icons. These are passed entirely in MASK, which
  is then 2x higher. COLOR is then NULL. The upper half of the MASK bitmap is the AND part and the lower
  half is the XOR part. This case can be easily detected using the COLOR == NULL test.

  From Windows XP onwards there is another special case: icons containing an ALPHA channel. These are
  DIBs with a color depth of 32 bits, where each point is composed of ARGB components.

  




************************************************************************************/

//
// There is potential space for optimizing our ImageList implementation.
// DIBs could be held in the same format as the screen runs. BitBlt
// is supposedly faster (I haven't verified) according to MSDN:
//   http://support.microsoft.com/default.aspx?scid=kb;EN-US;230492
//   (HOWTO: Retrieving an Optimal DIB Format for a Device)
//
// Several factors speak against this optimization:
//   - we would have to support different data formats in code (15, 16, 24, 32 bits)
//   - because we simultaneously draw at most tens of icons, drawing speed
//     is not critical for us; I measured these drawing speeds:
//     (100,000 times a 16x16, 32bpp DIB was drawn to screen via BitBlt)
//     screen resolution       total time      (W2K, Matrox G450)
//     32 bpp                  0.40 s
//     24 bpp                  0.80 s
//     16 bpp                  0.65 s
//      8 bpp                  1.16 s
//   - Somehow we would still have to hold icons with ALPHA channel, which are 32 bpp
//

//
// Why we need our own ImageList alternative:
//
// ImageList from CommonControls has one fundamental problem: if we ask it
// to hold DeviceDependentBitmaps, it cannot display a blended item. Instead
// it draws it with a pattern.
//
// If a DIB bitmap is held, blending works great, but drawing
// a classic item is orders of magnitude slower (DIB->screen conversion).
//
// Additionally, there is a risk that in some implementations calling ImageList_SetBkColor
// does not physically change the held bitmap based on the mask, but only sets an internal
// variable. Of course, drawing is then slower because masking needs to be performed.
// I tested under W2K and the function works correctly.
//
// The only option would be to keep ImageList for holding data and only reprogram
// blending. However, a problem arises in the ImageList_GetImageInfo function, which
// allows access to internal Image/Mask bitmaps. ImageList has them constantly
// selected in MemDC, so according to MSDN (Q131279: SelectObject() Fails After
// ImageList_GetImageInfo()) the only option is to call CopyImage first and only then
// work with the bitmap. This would lead to incredibly slow drawing
// of blended items.
//
// Another risk for ImageList are invert body icons. An icon consists of
// two bitmaps: MASK and COLORS. The mask is ANDed to the target and colors are XORed over it.
// Thanks to XORing, icons can invert some of their parts. This is mainly used by
// cursors, see WINDOWS\Cursors.
//

//******************************************************************************
//
// CIconList
//
//
// Following W2K's example, we hold items in a bitmap 4 items wide. Operations
// on a bitmap oriented this way will probably be faster.

#define IL_DRAW_BLEND 0x00000001       // 50% will use blendClr color
#define IL_DRAW_TRANSPARENT 0x00000002 // during drawing preserve original background (if not specified, background will be filled with defined color)
#define IL_DRAW_ASALPHA 0x00000004     // uses (inverted) color in BLUE channel as alpha, with which it mixes specified foreground color to background; currently used for throbber
#define IL_DRAW_MASK 0x00000010        // draw mask

class CIconList : public CGUIIconListAbstract
{
private:
    int ImageWidth; // dimensions of one image
    int ImageHeight;
    int ImageCount;  // number of images in bitmap
    int BitmapWidth; // dimensions of held bitmaps
    int BitmapHeight;

    // images are arranged from left to right and top to bottom
    HBITMAP HImage;   // DIB, its raw data is in ImageRaw variable
    DWORD* ImageRaw;  // ARGB values; Alpha: 0x00=transparent, 0xFF=opaque, others=partial_transparency(only for IL_TYPE_ALPHA)
    BYTE* ImageFlags; // array with 'imageCount' elements; (IL_TYPE_xxx)

    COLORREF BkColor; // current background color (points where Alpha==0x00)

    // shared variables across all imagelists -- save memory
    static HDC HMemDC;                       // shared mem dc
    static HBITMAP HOldBitmap;               // original bitmap
    static HBITMAP HTmpImage;                // cache for paint + temporary mask storage
    static DWORD* TmpImageRaw;               // raw data from HTmpImage
    static int TmpImageWidth;                // dimensions of HTmpImage in points
    static int TmpImageHeight;               // dimensions of HTmpImage in points
    static int MemDCLocks;                   // for mem dc destruction
    static CRITICAL_SECTION CriticalSection; // access synchronization
    static int CriticalSectionLocks;         // for CriticalSection construction/destruction

public:
    //    BOOL     Dump; // if TRUE, raw data is dumped to TRACE

public:
    CIconList();
    ~CIconList();

    virtual BOOL WINAPI Create(int imageWidth, int imageHeight, int imageCount);
    virtual BOOL WINAPI CreateFromImageList(HIMAGELIST hIL, int requiredImageSize = -1);          // if 'requiredImageSize' is -1, geometry from hIL is used
    virtual BOOL WINAPI CreateFromPNG(HINSTANCE hInstance, LPCTSTR lpBitmapName, int imageWidth); // loads PNG from resource, must be a long strip one row high
    virtual BOOL WINAPI CreateFromRawPNG(const void* rawPNG, DWORD rawPNGSize, int imageWidth);
    virtual BOOL WINAPI CreateFromBitmap(HBITMAP hBitmap, int imageCount, COLORREF transparentClr); // absorbs bitmap (max 256 colors), must be a long strip one row high
    virtual BOOL WINAPI CreateAsCopy(const CIconList* iconList, BOOL grayscale);
    virtual BOOL WINAPI CreateAsCopy(const CGUIIconListAbstract* iconList, BOOL grayscale);

    // converts icon list to grayscale version
    virtual BOOL WINAPI ConvertToGrayscale(BOOL forceAlphaForBW);

    // compresses bitmap to 32-bit PNG with alpha channel (one long row)
    // if everything succeeds, returns TRUE and pointer to allocated memory, which must then be deallocated
    // on error returns FALSE
    virtual BOOL WINAPI SaveToPNG(BYTE** rawPNG, DWORD* rawPNGSize);

    virtual BOOL WINAPI ReplaceIcon(int index, HICON hIcon);

    // creates icon from position 'index'; returns its handle or NULL on failure
    // returned icon must be destroyed using API DestroyIcon after use
    virtual HICON WINAPI GetIcon(int index);
    HICON GetIcon(int index, BOOL useHandles);

    // creates imagelist (one row, number of columns according to number of items); returns its handle or NULL on failure
    // returned imagelist must be destroyed using API ImageList_Destroy() after use
    virtual HIMAGELIST WINAPI GetImageList();

    // copies one item from 'srcIL' at position 'srcIndex' to position 'dstIndex'
    virtual BOOL WINAPI Copy(int dstIndex, CIconList* srcIL, int srcIndex);

    // copies one item from position 'srcIndex' to 'hDstImageList' at position 'dstIndex'
    //    BOOL CopyToImageList(HIMAGELIST hDstImageList, int dstIndex, int srcIndex);

    virtual BOOL WINAPI Draw(int index, HDC hDC, int x, int y, COLORREF blendClr, DWORD flags);

    virtual BOOL WINAPI SetBkColor(COLORREF bkColor);
    virtual COLORREF WINAPI GetBkColor();

private:
    // if it doesn't exist, creates HTmpImage
    // if HTmpImage exists and is smaller than 'width' x 'height', creates a new one
    // returns TRUE on success, otherwise returns FALSE and keeps previous HTmpImage
    BOOL CreateOrEnlargeTmpImage(int width, int height);

    // returns handle of bitmap currently selected in HMemDC
    // if HMemDC doesn't exist, returns NULL
    HBITMAP GetCurrentBitmap();

    // 'index' specifies icon position in HImage
    // returns TRUE if image 'index' in HImage contained alpha channel
    BYTE ApplyMaskToImage(int index, BYTE forceXOR);

    // for debugging purposes -- displays dump of ARGB values of color bitmap and mask
    //    void DumpToTrace(int index, BOOL dumpMask);

    // pixel-by-pixel rendering followed by BitBlt is in RELEASE version
    // only 30% slower compared to pure BitBlt

    BOOL DrawALPHA(HDC hDC, int x, int y, int index, COLORREF bkColor);
    BOOL DrawXOR(HDC hDC, int x, int y, int index, COLORREF bkColor);
    BOOL AlphaBlend(HDC hDC, int x, int y, int index, COLORREF bkColor, COLORREF fgColor);
    BOOL DrawMask(HDC hDC, int x, int y, int index, COLORREF fgColor, COLORREF bkColor);
    BOOL DrawALPHALeaveBackground(HDC hDC, int x, int y, int index);
    BOOL DrawAsAlphaLeaveBackground(HDC hDC, int x, int y, int index, COLORREF fgColor);

    void StoreMonoIcon(int index, WORD* mask);

    // special support function for CreateFromBitmap(); copies from 'hSrcBitmap'
    // selected number of items to 'dstIndex'; assumes that 'hSrcBitmap' will be a long
    // strip of icons, one row high
    // transparentClr specifies the color to be considered transparent
    // it is assumed that the source bitmap has the same icon dimensions as the target (ImageWidth, ImageHeight)
    // one copy operation can work with at most one row of the target bitmap,
    // cannot for example copy data to two rows in the target bitmap
    BOOL CopyFromBitmapIternal(int dstIndex, HBITMAP hSrcBitmap, int srcIndex, int imageCount, COLORREF transparentClr);
};

HBITMAP LoadPNGBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName, DWORD flags);
HBITMAP LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags);

inline BYTE GetGrayscaleFromRGB(int red, int green, int blue)
{
    //  int brightness = (76*(int)red + 150*(int)green + 29*(int)blue) / 255;
    int brightness = (55 * (int)red + 183 * (int)green + 19 * (int)blue) / 255;
    //  int brightness = (40*(int)red + 175*(int)green + 60*(int)blue) / 255;
    if (brightness > 255)
        brightness = 255;
    return (BYTE)brightness;
}
