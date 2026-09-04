// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "svg.h"
#include <strsafe.h> // Bound SVG lookup paths shared by both toolbar rows.

#define NANOSVG_IMPLEMENTATION
#include "nanosvg\nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg\nanosvgrast.h"

CSVGSprite SVGArrowRight;
CSVGSprite SVGArrowRightSmall;
CSVGSprite SVGArrowMore;
CSVGSprite SVGArrowLess;
CSVGSprite SVGArrowDropDown;

// alternative: http://stackoverflow.com/questions/11376288/fast-computing-of-log2-for-64-bit-integers
// (probably could be found for shorter versions as well)
//
// the following solution has the advantage that for constants it will be calculated during precompilation
// LOG2_k(n) returns floor(log2(n)) and is valid for values 0 <= n < 1 << k
#define LOG2_2(n) ((n) & 0x2 ? 1 : 0)
#define LOG2_4(n) ((n) & 0xC ? 2 + LOG2_2((n) >> 2) : LOG2_2(n))
#define LOG2_8(n) ((n) & 0xF0 ? 4 + LOG2_4((n) >> 4) : LOG2_4(n))
#define LOG2_16(n) ((n) & 0xFF00 ? 8 + LOG2_8((n) >> 8) : LOG2_8(n))
#define LOG2_32(n) ((n) & 0xFFFF0000 ? 16 + LOG2_16((n) >> 16) : LOG2_16(n))
#define LOG2_64(n) ((n) & 0xFFFFFFFF00000000 ? 32 + LOG2_32((n) >> 32) : LOG2_32(n))

//__popcnt16, __popcnt, __popcnt64
//https://msdn.microsoft.com/en-us/library/bb385231(v=vs.100).aspx

DWORD GetSVGSysColor(int index)
{
    DWORD color = GetSysColor(index);
    DWORD ret = 0xFF000000;
    ret |= GetBValue(color) << 16;
    ret |= GetGValue(color) << 8;
    ret |= GetRValue(color);
    return ret;
}

//*****************************************************************************
//
// RenderSVGImage
//

char* ReadSVGFile(const char* fileName)
{
    char* buff = NULL;
    HANDLE hFile = HANDLES_Q(CreateFileUtf8(fileName, GENERIC_READ,
                                        FILE_SHARE_READ, NULL,
                                        OPEN_EXISTING,
                                        FILE_FLAG_SEQUENTIAL_SCAN,
                                        NULL));
    if (hFile != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER fileSize;
        // SVG input is stored in a DWORD-sized buffer, so reject a size that cannot be represented safely.
        if (GetFileSizeEx(hFile, &fileSize) && fileSize.QuadPart >= 0 &&
            (ULONGLONG)fileSize.QuadPart < MAXDWORD)
        {
            DWORD size = (DWORD)fileSize.QuadPart;
            buff = (char*)malloc(size + 1);
            DWORD read;
            if (ReadFile(hFile, buff, size, &read, NULL) && read == size)
            {
                buff[size] = 0;
            }
            else
            {
                TRACE_E("ReadSVGFile(): ReadFile() failed on " << fileName);
                free(buff);
                buff = NULL;
            }
        }
        else
        {
            TRACE_E("ReadSVGFile(): cannot obtain a representable file size for " << fileName);
        }
        HANDLES(CloseHandle(hFile));
    }
    else
    {
        TRACE_I("ReadSVGFile(): cannot open SVG file " << fileName);
    }
    return buff;
}

// Both toolbar rows must resolve the same deployed assets and development fallbacks.
static char* ReadToolbarSVG(const char* svgName, int iconSize)
{
    char svgFile[2 * MAX_PATH];
    DWORD length = GetModuleFileName(NULL, svgFile, _countof(svgFile));
    if (length == 0 || length >= _countof(svgFile))
        return NULL;
    char* s = strrchr(svgFile, '\\');
    if (s == NULL)
        return NULL;
    *s = 0;
    const char* directories[] = {"toolbars", "..\\src\\res\\toolbars", "..\\..\\src\\res\\toolbars"};
    // A 16 px toolbar at 200% DPI still needs the simpler 16 px drawing, rasterized to 32 physical pixels.
    int logicalSize = MulDiv(iconSize, 100, GetScaleForSystemDPI());
    int masterSize = logicalSize <= 16 ? 16 : logicalSize <= 24 ? 24 : 32;
    for (int i = 0; i < _countof(directories); i++)
    {
        char path[2 * MAX_PATH];
        if (masterSize != 16 && SUCCEEDED(StringCchPrintfA(path, _countof(path), "%s\\%s\\%d\\%s.svg", svgFile, directories[i], masterSize, svgName)))
        {
            char* variant = ReadSVGFile(path);
            if (variant != NULL)
                return variant;
        }
        // Existing deployments and third-party additions can still supply only the original 16 px master.
        if (FAILED(StringCchPrintfA(path, _countof(path), "%s\\%s\\%s.svg", svgFile, directories[i], svgName)))
            continue;
        char* svg = ReadSVGFile(path);
        if (svg != NULL)
            return svg;
    }
    return NULL;
}

// Native-size alpha icons avoid stretching plug-in bitmaps and work on hover/selection backgrounds.
HICON LoadToolbarSVGIcon(const char* svgName, int iconSize, BOOL grayscale)
{
    if (svgName == NULL || iconSize <= 0 || iconSize > 1024)
        return NULL;
    char* svg = ReadToolbarSVG(svgName, iconSize);
    if (svg == NULL)
        return NULL;
    NSVGimage* image = nsvgParse(svg, "px", 96);
    free(svg);
    if (image == NULL)
        return NULL;
    if (image->width <= 0 || image->height <= 0 || image->shapes == NULL)
    {
        nsvgDelete(image);
        return NULL;
    }
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = iconSize;
    bi.bmiHeader.biHeight = -iconSize;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    BYTE* pixels = NULL;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    // A zero AND mask lets the 32-bit alpha channel define every transparent edge.
    int maskBytes = ((iconSize + 15) / 16) * 2 * iconSize;
    void* maskPixels = calloc(maskBytes, 1);
    HBITMAP mask = maskPixels != NULL ? CreateBitmap(iconSize, iconSize, 1, 1, maskPixels) : NULL;
    free(maskPixels);
    HICON icon = NULL;
    if (rast != NULL && color != NULL && pixels != NULL && mask != NULL)
    {
        float scale = iconSize / max(image->width, image->height);
        nsvgRasterize(rast, image, (iconSize - image->width * scale) / 2,
                      (iconSize - image->height * scale) / 2, scale, pixels, iconSize, iconSize, iconSize * 4);
        // NanoSVG emits premultiplied BGRA for AlphaBlend; image-list insertion premultiplies HICON channels itself.
        if (grayscale)
        {
            // Luminance is linear in premultiplied channels; preserve alpha for menu hover/disabled edges.
            for (int i = 0; i < iconSize * iconSize; i++)
            {
                BYTE* pixel = pixels + i * 4;
                BYTE luminance = (BYTE)((19 * pixel[0] + 183 * pixel[1] + 54 * pixel[2] + 128) / 256);
                pixel[0] = pixel[1] = pixel[2] = luminance;
            }
        }
        // Restore straight alpha at the HICON boundary to avoid dark fringes after ImageList_AddIcon.
        for (int i = 0; i < iconSize * iconSize; i++)
        {
            BYTE* pixel = pixels + i * 4;
            if (pixel[3] != 0)
                for (int channel = 0; channel < 3; channel++)
                    pixel[channel] = (BYTE)min(255, (pixel[channel] * 255 + pixel[3] / 2) / pixel[3]);
        }
        ICONINFO info = {};
        info.fIcon = TRUE;
        info.hbmColor = color;
        info.hbmMask = mask;
        icon = CreateIconIndirect(&info);
    }
    if (mask != NULL)
        DeleteObject(mask);
    if (color != NULL)
        DeleteObject(color);
    if (rast != NULL)
        nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
    return icon;
}

// Use one identity map for bundled plug-ins; third-party artwork remains the fallback.
const char* GetPluginSVGName(const char* dllName)
{
    if (dllName == NULL)
        return NULL;
    const char* module = dllName;
    for (const char* p = dllName; *p != 0; p++)
        if (*p == '\\' || *p == '/')
            module = p + 1;
    const struct { const char* Module; const char* SVG; } icons[] = {
        {"ftp.spl", "DriveFTP"}, {"winscp.spl", "DriveSFTP"},
        {"folders.spl", "PluginFolders"}, {"nethood.spl", "PluginNetwork"},
        {"portables.spl", "DrivePortable"}, {"wmobile.spl", "DriveMobile"},
        {"regedt.spl", "DriveRegistry"}, {"undelete.spl", "DriveUndelete"},
        {"7zip.spl", "PluginArchive"}, {"zip.spl", "PluginArchive"},
        {"unrar.spl", "PluginArchive"}, {"tar.spl", "PluginArchive"},
        {"unarj.spl", "PluginArchive"}, {"uncab.spl", "PluginArchive"},
        {"unlha.spl", "PluginArchive"}, {"pak.spl", "PluginArchive"},
        {"automation.spl", "PluginAutomation"}, {"checksum.spl", "PluginChecksum"},
        {"checkver.spl", "PluginUpdate"}, {"dbviewer.spl", "PluginDatabase"},
        {"diskmap.spl", "PluginDiskMap"}, {"filecomp.spl", "PluginCompare"},
        {"ieviewer.spl", "PluginWeb"}, {"mmviewer.spl", "PluginMedia"},
        {"peviewer.spl", "PluginExecutable"}, {"pictview.spl", "PluginPicture"},
        {"renamer.spl", "PluginRename"}, {"splitcbn.spl", "PluginSplit"},
        {"unchm.spl", "PluginHelp"}, {"unfat.spl", "PluginDiskRecovery"},
        {"uniso.spl", "DriveOptical"}, {"unmime.spl", "Email"},
        {"unole.spl", "PluginCompound"}};
    for (int i = 0; i < _countof(icons); i++)
        if (StrICmp(module, icons[i].Module) == 0)
            return icons[i].SVG;
    return NULL;
}

// Draw command icons from the same source used by the drive bar.
void RenderSVGImage(NSVGrasterizer* rast, HDC hDC, int x, int y, const char* svgName, int iconSize, COLORREF bkColor, BOOL enabled)
{
    char* svg = ReadToolbarSVG(svgName, iconSize);
    if (svg != NULL)
    {
        HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
        BITMAPINFOHEADER bmhdr;
        memset(&bmhdr, 0, sizeof(bmhdr));
        bmhdr.biSize = sizeof(bmhdr);
        bmhdr.biWidth = iconSize;
        bmhdr.biHeight = -iconSize;
        if (bmhdr.biHeight == 0)
            bmhdr.biHeight = -1;
        bmhdr.biPlanes = 1;
        bmhdr.biBitCount = 32;
        bmhdr.biCompression = BI_RGB;
        void* lpMemBits = NULL;
        HBITMAP hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO*)&bmhdr, DIB_RGB_COLORS, &lpMemBits, NULL, 0));
        // Restore the selected bitmap before releasing it, including after parse failures.
        HGDIOBJ oldBitmap = SelectObject(hMemDC, hMemBmp);

        RECT r;
        r.left = x;
        r.top = y;
        r.right = x + iconSize;
        r.bottom = y + iconSize;
        SetBkColor(hDC, bkColor);
        ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &r, "", 0, NULL);

        float sysDPIScale = (float)GetScaleForSystemDPI();
        NSVGimage* image = nsvgParse(svg, "px", sysDPIScale);

        // A bad optional asset must not crash an otherwise usable toolbar.
        if (image == NULL || image->width <= 0 || image->height <= 0)
        {
            if (image != NULL)
                nsvgDelete(image);
            SelectObject(hMemDC, oldBitmap);
            HANDLES(DeleteObject(hMemBmp));
            HANDLES(DeleteDC(hMemDC));
            free(svg);
            return;
        }

        if (!enabled)
        {
            DWORD disabledColor = GetSVGSysColor(COLOR_BTNSHADOW); // JRYFIXME - initial draft, where should we get the disabled color from?
            NSVGshape* shape = image->shapes;
            while (shape != NULL)
            {
                // Custom optical drawings use strokes too; disabled state must tint both paint kinds.
                if (shape->fill.type == NSVG_PAINT_COLOR)
                    shape->fill.color = disabledColor;
                if (shape->stroke.type == NSVG_PAINT_COLOR)
                    shape->stroke.color = disabledColor;
                shape = shape->next;
            }
        }

        // Fit the vector artwork to the requested image-list cell; DPI is already reflected in iconSize.
        float scaleX = (float)iconSize / image->width;
        float scaleY = (float)iconSize / image->height;
        float scale = min(scaleX, scaleY);
        float xOffset = (iconSize - image->width * scale) / 2;
        float yOffset = (iconSize - image->height * scale) / 2;
        nsvgRasterize(rast, image, xOffset, yOffset, scale, (BYTE*)lpMemBits, iconSize, iconSize, iconSize * 4);
        nsvgDelete(image);

        BLENDFUNCTION bf;
        bf.BlendOp = AC_SRC_OVER;
        bf.BlendFlags = 0;
        bf.SourceConstantAlpha = 0xff; // want to use per-pixel alpha values
        bf.AlphaFormat = AC_SRC_ALPHA;
        AlphaBlend(hDC, x, y, iconSize, iconSize, hMemDC, 0, 0, iconSize, iconSize, bf);

        SelectObject(hMemDC, oldBitmap);
        HANDLES(DeleteObject(hMemBmp));
        HANDLES(DeleteDC(hMemDC));

        free(svg);
    }
}

//*****************************************************************************
//
// CSVGSprite
//

CSVGSprite::CSVGSprite()
{
    for (int i = 0; i < SVGSTATE_COUNT; i++)
        HBitmaps[i] = NULL;
    Clean();
}

CSVGSprite::~CSVGSprite()
{
    Clean();
}

void CSVGSprite::Clean()
{
    for (int i = 0; i < SVGSTATE_COUNT; i++)
    {
        if (HBitmaps[i] != NULL)
        {
            HANDLES(DeleteObject(HBitmaps[i]));
            HBitmaps[i] = NULL;
        }
    }
    Width = -1;
    Height = -1;
}

char* CSVGSprite::LoadSVGResource(int resID)
{
    char* ret = NULL;
    HRSRC hRsrc = FindResource(HInstance, MAKEINTRESOURCE(resID), RT_RCDATA);
    if (hRsrc != NULL)
    {
        char* rawSVG = (char*)LoadResource(HInstance, hRsrc);
        if (rawSVG != NULL)
        {
            DWORD size = SizeofResource(HInstance, hRsrc);
            if (size > 0)
            {
                NSVGimage* image = NULL;
                NSVGrasterizer* rast = NULL;

                char* terminatedSVG = (char*)malloc(size + 1);
                memcpy(terminatedSVG, rawSVG, size);
                terminatedSVG[size] = 0;
                ret = terminatedSVG;
            }
            else
            {
                TRACE_E("LoadSVGResource() Invalid resource data! resID=" << resID);
            }
        }
        else
        {
            TRACE_E("LoadSVGResource() Cannot load resource! resID=" << resID);
        }
    }
    else
    {
        TRACE_E("LoadSVGResource() Resource not found! resID=" << resID);
    }
    return ret;
}

void CSVGSprite::GetScaleAndSize(const NSVGimage* image, const SIZE* sz, float* scale, int* width, int* height)
{
    if (sz->cx != -1 || sz->cy != -1)
    {
        float scaleX, scaleY;
        if (sz->cx != -1)
            scaleX = sz->cx / image->width;
        if (sz->cy != -1)
            scaleY = sz->cy / image->height;
        if (sz->cx == -1)
        {
            *scale = scaleY;
            *height = sz->cy;
            *width = (int)(image->width * *scale);
        }
        else
        {
            if (sz->cy == -1)
            {
                *scale = scaleX;
                *width = sz->cx;
                *height = (int)(image->height * *scale);
            }
            else
            {
                *scale = min(scaleX, scaleY);
                *width = (int)(image->width * *scale);
                *height = (int)(image->height * *scale);
            }
        }
    }
    else
    {
        *scale = (float)GetScaleForSystemDPI() / 100;
        *width = (int)(image->width * *scale);
        *height = (int)(image->height * *scale);
    }
}
/*
HBITMAP
CSVGSprite::LoadSVGToBitmap(int resID, SIZE *sz)
{
  if (sz == NULL)
    TRACE_C("LoadSVGToBitmap(): invalid parameters!");

  HBITMAP hMemBmp = NULL;

  char *terminatedSVG = LoadSVGResource(resID);
  if (terminatedSVG != NULL)
  {
    NSVGimage *image = NULL;
    image = nsvgParse(terminatedSVG, "px", (float)GetSystemDPI());
    free(terminatedSVG);

    float scale;
    int w, h;
    GetScaleAndSize(image, sz, &scale, &w, &h);

    NSVGrasterizer *rast = NULL;
    rast = nsvgCreateRasterizer();

    HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
    BITMAPINFOHEADER bmhdr;
    memset(&bmhdr, 0, sizeof(bmhdr));
    bmhdr.biSize = sizeof(bmhdr);
    bmhdr.biWidth = w;
    bmhdr.biHeight = -h;
    if (bmhdr.biHeight == 0) bmhdr.biHeight = -1;
    bmhdr.biPlanes = 1;
    bmhdr.biBitCount = 32;
    bmhdr.biCompression = BI_RGB;
    void *lpMemBits = NULL;
    hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO *)&bmhdr, DIB_RGB_COLORS, &lpMemBits, NULL, 0));
    HANDLES(DeleteDC(hMemDC));

    nsvgRasterize(rast, image, 0, 0, scale, (BYTE*)lpMemBits, w, h, w * 4);

    sz->cx = w;
    sz->cy = h;

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);
  }
  return hMemBmp;
}
*/
void CSVGSprite::CreateDIB(int width, int height, HBITMAP* hMemBmp, void** lpMemBits)
{
    HDC hMemDC = HANDLES(CreateCompatibleDC(NULL));
    BITMAPINFOHEADER bmhdr;
    memset(&bmhdr, 0, sizeof(bmhdr));
    bmhdr.biSize = sizeof(bmhdr);
    bmhdr.biWidth = width;
    bmhdr.biHeight = -height;
    if (bmhdr.biHeight == 0)
        bmhdr.biHeight = -1;
    bmhdr.biPlanes = 1;
    bmhdr.biBitCount = 32;
    bmhdr.biCompression = BI_RGB;
    *hMemBmp = HANDLES(CreateDIBSection(hMemDC, (CONST BITMAPINFO*)&bmhdr, DIB_RGB_COLORS, lpMemBits, NULL, 0));
    HANDLES(DeleteDC(hMemDC));
}

void CSVGSprite::ColorizeSVG(NSVGimage* image, DWORD state)
{
    if (state == SVGSTATE_ORIGINAL)
        return;

    int sysIndex;
    switch (state)
    {
    case SVGSTATE_ENABLED:
        sysIndex = COLOR_BTNTEXT;
        break;

    case SVGSTATE_DISABLED:
        sysIndex = COLOR_BTNSHADOW;
        break;

    default:
        sysIndex = COLOR_BTNTEXT;
        TRACE_E("CSVGSprite::ColorizeSVG() unknown state=" << state);
    }
    DWORD color = GetSVGSysColor(sysIndex);
    NSVGshape* shape = image->shapes;
    while (shape != NULL)
    {
        shape->fill.color = color;
        shape = shape->next;
    }
}

BOOL CSVGSprite::Load(int resID, int width, int height, DWORD states)
{
    if (states == 0 || states >= (1 << SVGSTATE_COUNT))
    {
        TRACE_E("CSVGSprite::Load() wrong states combination: " << states);
        states |= SVGSTATE_ORIGINAL;
    }
    Clean();

    char* terminatedSVG = LoadSVGResource(resID);
    if (terminatedSVG != NULL)
    {
        NSVGimage* image = NULL;
        image = nsvgParse(terminatedSVG, "px", (float)GetSystemDPI());
        free(terminatedSVG);

        float scale;
        SIZE sz = {width, height};
        GetScaleAndSize(image, &sz, &scale, &Width, &Height);

        NSVGrasterizer* rast = NULL;
        rast = nsvgCreateRasterizer();

        for (int i = 0; i < SVGSTATE_COUNT; i++)
        {
            DWORD state = 1 << i;
            if (states & state)
            {
                void* lpMemBits;
                CreateDIB(Width, Height, &HBitmaps[i], &lpMemBits);
                ColorizeSVG(image, state);
                nsvgRasterize(rast, image, 0, 0, scale, (BYTE*)lpMemBits, Width, Height, Width * 4);
            }
        }

        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);
    }
    return TRUE;
}

void CSVGSprite::GetSize(SIZE* s)
{
    s->cx = Width;
    s->cy = Height;
}

int CSVGSprite::GetWidth()
{
    return Width;
}

int CSVGSprite::GetHeight()
{
    return Height;
}

void CSVGSprite::AlphaBlend(HDC hDC, int x, int y, int width, int height, DWORD state)
{
    HDC hMemTmpDC = HANDLES(CreateCompatibleDC(hDC));
    int index = LOG2_32(state);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemTmpDC, HBitmaps[index]);

    if (width == -1)
        width = Width;
    if (height == -1)
        height = Height;

    BLENDFUNCTION bf;
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 0xff; // want to use per-pixel alpha values
    bf.AlphaFormat = AC_SRC_ALPHA;
    ::AlphaBlend(hDC, x, y, width, height, hMemTmpDC, 0, 0, Width, Height, bf);

    SelectObject(hMemTmpDC, hOldBitmap);
    HANDLES(DeleteDC(hMemTmpDC));
}
