// Exercise production SVG loading and rasterization with Windows image lists at every supported size/DPI combination.
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include <windows.h>
#include <commctrl.h>
#include <strsafe.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "svg.h"
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#define HANDLES(value) (value)

static int TestScale = 100;
int GetScaleForSystemDPI() { return TestScale; }
DWORD GetSVGSysColor(int index)
{
    COLORREF c = GetSysColor(index);
    return 0xff000000 | (GetBValue(c) << 16) | (GetGValue(c) << 8) | GetRValue(c);
}
// File I/O is the only substituted service; the optical selection and both rendering paths are copied verbatim.
char* ReadSVGFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return NULL;
    std::string value((std::istreambuf_iterator<char>(file)), {});
    char* result = (char*)malloc(value.size() + 1);
    if (result) memcpy(result, value.c_str(), value.size() + 1);
    return result;
}
#include "svg-functions.inc"

static void Require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

static std::vector<BYTE> IconPixels(HICON icon, int size)
{
    ICONINFO ii = {};
    Require(GetIconInfo(icon, &ii), "GetIconInfo failed");
    BITMAP bm = {};
    GetObject(ii.hbmColor, sizeof(bm), &bm);
    Require(bm.bmWidth == size && bm.bmHeight == size, "Incorrect native icon dimensions");
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size; bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    std::vector<BYTE> pixels(size * size * 4);
    HDC dc = CreateCompatibleDC(NULL);
    int rows = GetDIBits(dc, ii.hbmColor, 0, size, pixels.data(), &bi, DIB_RGB_COLORS);
    DeleteDC(dc); DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask);
    Require(rows == size, "Unable to inspect native alpha");
    return pixels;
}

static std::vector<BYTE> Paint(const char* name, HICON icon, int size, BOOL enabled, COLORREF background)
{
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size; bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    BYTE* pixels = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
    HDC dc = CreateCompatibleDC(NULL);
    HGDIOBJ old = SelectObject(dc, bitmap);
    for (int i = 0; i < size * size; i++)
    {
        pixels[4*i] = GetBValue(background); pixels[4*i+1] = GetGValue(background);
        pixels[4*i+2] = GetRValue(background); pixels[4*i+3] = 255;
    }
    if (icon)
    {
        HIMAGELIST list = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK, 1, 0);
        Require(ImageList_AddIcon(list, icon) == 0, "Image-list insertion failed");
        ImageList_Draw(list, 0, dc, 0, 0, ILD_TRANSPARENT);
        ImageList_Destroy(list);
    }
    else
    {
        NSVGrasterizer* raster = nsvgCreateRasterizer();
        RenderSVGImage(raster, dc, 0, 0, name, size, background, enabled);
        nsvgDeleteRasterizer(raster);
    }
    GdiFlush();
    std::vector<BYTE> result(pixels, pixels + size * size * 4);
    for (int i = 0; i < size * size; i++) result[4*i+3] = 255;
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    return result;
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "Expected runtime SVG directory and output directory");
        const std::filesystem::path source(argv[1]), output(argv[2]);
        std::filesystem::create_directories(output);
        INITCOMMONCONTROLSEX common = {sizeof(common), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&common);
        Require(!LoadToolbarSVGIcon(NULL, 16) && !LoadToolbarSVGIcon("Missing", 16) &&
                !LoadToolbarSVGIcon("Back", 0) && !LoadToolbarSVGIcon("Back", 1025), "Invalid-input fallback failed");
        // Probe actual loader fallback with a disposable single-master asset beside this test executable.
        auto fallbackDir = output / "toolbars";
        std::filesystem::create_directories(fallbackDir);
        std::filesystem::copy_file(source / "Back.svg", fallbackDir / "Fallback.svg", std::filesystem::copy_options::overwrite_existing);
        HICON fallback = LoadToolbarSVGIcon("Fallback", 32);
        Require(fallback != NULL, "Missing optical variant did not fall back to the master");
        DestroyIcon(fallback);
        int families = 0, cases = 0;
        DWORD baseline = 0;
        const int logicalSizes[] = {16, 24, 32};
        const int scales[] = {100, 125, 150, 200, 250, 300, 400, 500};
        for (auto& entry : std::filesystem::directory_iterator(source))
        {
            if (entry.path().extension() != ".svg") continue;
            std::string name = entry.path().stem().string();
            families++;
            for (int logical : logicalSizes) for (int dpi : scales)
            {
                TestScale = dpi;
                int size = logical * dpi / 100;
                char* selected = ReadToolbarSVG(name.c_str(), size);
                Require(selected != NULL, name + ": missing source");
                std::string expected = "width=\"" + std::to_string(logical) + "\"";
                Require(strstr(selected, expected.c_str()) != NULL, name + ": wrong optical master at DPI " + std::to_string(dpi));
                // Allow at most 0.1 logical pixel of rounding in upstream two-decimal path exports.
                NSVGimage* geometry = nsvgParse(selected, "px", 96);
                Require(geometry != NULL, name + ": invalid SVG");
                for (NSVGshape* shape = geometry->shapes; shape; shape = shape->next)
                {
                    float inset = shape->stroke.type == NSVG_PAINT_NONE ? 0 : shape->strokeWidth / 2;
                    Require(shape->bounds[0] - inset >= -0.1f && shape->bounds[1] - inset >= -0.1f &&
                            shape->bounds[2] + inset <= logical + 0.1f && shape->bounds[3] + inset <= logical + 0.1f,
                            name + ": artwork extends beyond its " + std::to_string(logical) + " px view box: " + std::to_string(shape->bounds[0]) + "," + std::to_string(shape->bounds[1]) + "," + std::to_string(shape->bounds[2]) + "," + std::to_string(shape->bounds[3]));
                }
                nsvgDelete(geometry);
                free(selected);
                HICON normal = LoadToolbarSVGIcon(name.c_str(), size);
                HICON gray = LoadToolbarSVGIcon(name.c_str(), size, TRUE);
                Require(normal && gray, name + ": empty icon");
                auto a = IconPixels(normal, size), b = IconPixels(gray, size);
                int covered = 0, transparent = 0, antialias = 0;
                for (int p = 0; p < size * size; p++)
                {
                    int alpha = a[4*p+3];
                    // Half-pixel-aligned regular strokes can be entirely antialiased at the smallest size.
                    covered += alpha > 0; transparent += alpha == 0; antialias += alpha > 0 && alpha < 255;
                    if (alpha != b[4*p+3]) Require(false, name + ": grayscale changed coverage");
                    if (b[4*p] != b[4*p+1] || b[4*p+1] != b[4*p+2]) Require(false, name + ": grayscale color leak");
                    // HICON stores straight color; image-list insertion supplies premultiplication.
                }
                Require(covered > 0 && transparent > 0, name + ": empty or opaque raster " + std::to_string(logical) + "/" + std::to_string(dpi) + " covered=" + std::to_string(covered) + " clear=" + std::to_string(transparent));
                const COLORREF backgrounds[] = {RGB(243,242,241), RGB(255,255,255), RGB(229,241,251)};
                for (COLORREF background : backgrounds)
                {
                    auto list = Paint(name.c_str(), normal, size, TRUE, background);
                    auto toolbar = Paint(name.c_str(), NULL, size, TRUE, background);
                    // Validate disabled fill and stroke tint at every DPI, including antialiased edge coverage.
                    auto disabled = Paint(name.c_str(), NULL, size, FALSE, background);
                    COLORREF shadow = GetSysColor(COLOR_BTNSHADOW);
                    const int foreground[] = {GetBValue(shadow), GetGValue(shadow), GetRValue(shadow)};
                    const int backdrop[] = {GetBValue(background), GetGValue(background), GetRValue(background)};
                    for (int p = 0; p < size * size; p++) for (int c = 0; c < 3; c++)
                    {
                        int expected = (foreground[c] * a[4*p+3] + backdrop[c] * (255-a[4*p+3])) / 255;
                        if (abs(disabled[4*p+c] - expected) > 2) Require(false, name + ": disabled tint/coverage mismatch");
                    }
                    for (size_t p = 0; p < list.size(); p++)
                        // Build diagnostics only on failure; large DPI samples contain millions of channel comparisons.
                        if (abs((int)list[p] - (int)toolbar[p]) > 2)
                            Require(false, name + ": toolbar/image-list mismatch at " + std::to_string(logical) + "/" + std::to_string(dpi) + " byte " + std::to_string(p) + ": " + std::to_string(list[p]) + " vs " + std::to_string(toolbar[p]));
                    // Save actual-size samples for visual review; every DPI combination is still validated above.
                    if (dpi == 100 && background == backgrounds[0])
                    {
                        for (int state = 0; state < 3; state++)
                        {
                            auto pixels = state == 0 ? list : state == 1 ? Paint(name.c_str(), gray, size, TRUE, background) : disabled;
                            std::ofstream file(output / (name + "-" + std::to_string(logical) + "-" + std::to_string(state) + ".bgra"), std::ios::binary);
                            file.write((const char*)pixels.data(), pixels.size());
                        }
                    }
                }
                DestroyIcon(normal); DestroyIcon(gray);
                cases++;
                if (cases == 1) baseline = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            }
        }
        DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        Require(after <= baseline, "GDI resources grew during rendering");
        printf("PASS: %d families, %d logical-size/DPI combinations, color/grayscale alpha, three backgrounds, both renderers; GDI %lu -> %lu.\n", families, cases, baseline, after);
        return 0;
    }
    catch (const std::exception& error)
    {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
