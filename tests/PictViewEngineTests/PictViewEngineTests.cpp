// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

/* Functional tests for the PictView WIC imaging engine.
 *
 * These drive the real CPVW32DLL table that PVWicEngine.cpp installs, the same
 * way the viewer, the thumbnail loader and the print preview do. The only
 * fixture written by hand is an uncompressed 24bpp BMP; every other format in
 * the suite is produced by the engine's own save path and then read back, so a
 * regression on either side shows up as a pixel mismatch rather than as a
 * silently different image.
 */
#include "precomp.h"

#include <stdio.h>

#include "lib\\pvw32dll.h"
#include "renderer.h"
#include "pictview.h"
#include "PVWicEngine.h"

namespace
{

int Failures = 0;

void Fail(const char* test, const char* detail)
{
    fprintf(stderr, "PictViewEngineTests: FAILED %s - %s\n", test, detail);
    Failures++;
}

bool Check(bool condition, const char* test, const char* detail)
{
    if (!condition)
        Fail(test, detail);
    return condition;
}

//****************************************************************************
//
// Fixtures
//

// Paths here are deliberately not bounded by MAX_PATH: the temporary
// directory can sit below a long path, and the repository ratchet rejects
// new MAX_PATH-shaped buffers for exactly that reason.
const int TestPathChars = 1024;

const int FixtureWidth = 16;
const int FixtureHeight = 12;

// A small palette that survives a trip through an 8bpp indexed format exactly.
COLORREF ExpectedPixel(int x, int y)
{
    static const COLORREF colors[4] = {RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(255, 255, 0)};
    return colors[((x / 4) + (y / 3)) % 4];
}

// Writes a bottom-up uncompressed 24bpp BMP without going through any codec, so
// the suite has one fixture the engine did not produce itself.
bool WriteFixtureBmp(const WCHAR* path)
{
    const int stride = ((FixtureWidth * 3 + 3) / 4) * 4;
    const DWORD pixelBytes = (DWORD)(stride * FixtureHeight);

    BITMAPFILEHEADER fileHeader;
    memset(&fileHeader, 0, sizeof(fileHeader));
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;

    BITMAPINFOHEADER header;
    memset(&header, 0, sizeof(header));
    header.biSize = sizeof(header);
    header.biWidth = FixtureWidth;
    header.biHeight = FixtureHeight; // bottom-up, the BMP default
    header.biPlanes = 1;
    header.biBitCount = 24;
    header.biCompression = BI_RGB;
    header.biSizeImage = pixelBytes;

    BYTE* pixels = (BYTE*)calloc(pixelBytes, 1);
    if (pixels == NULL)
        return false;
    for (int y = 0; y < FixtureHeight; y++)
    {
        BYTE* row = pixels + (size_t)(FixtureHeight - 1 - y) * stride; // bottom-up
        for (int x = 0; x < FixtureWidth; x++)
        {
            COLORREF color = ExpectedPixel(x, y);
            row[x * 3] = GetBValue(color);
            row[x * 3 + 1] = GetGValue(color);
            row[x * 3 + 2] = GetRValue(color);
        }
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        free(pixels);
        return false;
    }
    DWORD written = 0;
    bool ok = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, NULL) != 0 &&
              WriteFile(file, &header, sizeof(header), &written, NULL) != 0 &&
              WriteFile(file, pixels, pixelBytes, &written, NULL) != 0;
    CloseHandle(file);
    free(pixels);
    return ok;
}

//****************************************************************************
//
// Helpers over the engine table
//

LPPVHandle OpenFile(const char* utf8Path, PVImageInfo* info)
{
    PVOpenImageExInfo open;
    memset(&open, 0, sizeof(open));
    open.cbSize = sizeof(open);
    open.FileName = utf8Path;

    LPPVHandle image = NULL;
    if (PVW32DLL.PVOpenImageEx(&image, &open, info, sizeof(*info)) != PVC_OK)
        return NULL;
    return image;
}

// Reads one pixel back through the same path the pipette uses.
bool ReadPixel(LPPVHandle image, DWORD colors, int x, int y, COLORREF* out)
{
    RGBQUAD rgb;
    int index = 0;
    if (!PVW32DLL.GetRGBAtCursor(image, colors, x, y, &rgb, &index))
        return false;
    *out = RGB(rgb.rgbRed, rgb.rgbGreen, rgb.rgbBlue);
    return true;
}

bool ComparePixels(LPPVHandle image, DWORD colors, const char* test)
{
    for (int y = 0; y < FixtureHeight; y++)
    {
        for (int x = 0; x < FixtureWidth; x++)
        {
            COLORREF actual = 0;
            if (!ReadPixel(image, colors, x, y, &actual))
            {
                Fail(test, "GetRGBAtCursor failed");
                return false;
            }
            if (actual != ExpectedPixel(x, y))
            {
                char detail[128];
                _snprintf_s(detail, _countof(detail), _TRUNCATE,
                            "pixel (%d,%d) is %06X, expected %06X", x, y,
                            (unsigned)actual, (unsigned)ExpectedPixel(x, y));
                Fail(test, detail);
                return false;
            }
        }
    }
    return true;
}

struct CRawSink
{
    BYTE* Data;
    size_t Size;
    size_t Capacity;
};

DWORD WINAPI RawWrite(void* appSpecific, void* data, DWORD size)
{
    CRawSink* sink = (CRawSink*)appSpecific;
    if (sink->Size + size > sink->Capacity)
    {
        size_t capacity = (sink->Capacity == 0) ? 65536 : sink->Capacity * 2;
        while (capacity < sink->Size + size)
            capacity *= 2;
        BYTE* grown = (BYTE*)realloc(sink->Data, capacity);
        if (grown == NULL)
            return 0;
        sink->Data = grown;
        sink->Capacity = capacity;
    }
    memcpy(sink->Data + sink->Size, data, size);
    sink->Size += size;
    return size;
}

DWORD WINAPI RawSeek(void* appSpecific, LONG newPos, int origin)
{
    CRawSink* sink = (CRawSink*)appSpecific;
    if (origin == FILE_BEGIN && newPos == 0)
        sink->Size = 0;
    return (DWORD)sink->Size;
}

const char* EngineText(int msgID)
{
    // The engine asks its host for localized names; the tests only need the
    // callback to exist and to answer for the codes the properties dialog uses.
    switch (msgID)
    {
    case PVCS_NO_COMPRESSION:
        return "Uncompressed";
    case PVCS_LZW:
        return "LZW";
    case PVCS_DEFLATE:
        return "Deflating";
    }
    return "";
}

//****************************************************************************
//
// Tests
//

void TestOpenAndDecodeBmp(const char* bmpUtf8)
{
    const char* test = "open and decode a 24bpp BMP";
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;

    Check(info.Width == FixtureWidth && info.Height == FixtureHeight, test, "wrong dimensions");
    Check(info.Format == PVF_BMP, test, "container was not recognised as BMP");
    Check(info.Colors == PV_COLOR_TC24, test, "expected a true-colour surface");
    Check(info.NumOfImages == 1, test, "expected a single frame");
    Check(info.BytesPerLine == FixtureWidth * 3, test, "BytesPerLine must be byte-aligned, not DWORD-aligned");

    Check(PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0) == PVC_OK, test, "PVReadImage2 failed");

    LPPVImageHandles handles = NULL;
    if (Check(PVW32DLL.PVGetHandles2(image, &handles) == PVC_OK && handles != NULL, test, "PVGetHandles2 failed"))
        Check(handles->pLines != NULL, test, "the engine published no scanline table");

    ComparePixels(image, info.Colors, test);
    PVW32DLL.PVCloseImage(image);
}

// Round-trips the fixture through an encoder and verifies the pixels survive.
void TestSaveRoundTrip(const char* bmpUtf8, const WCHAR* outPathW, const char* outPathUtf8,
                       DWORD format, DWORD colors, DWORD colorModel, const char* test)
{
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;
    if (!Check(PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0) == PVC_OK, test, "PVReadImage2 failed"))
    {
        PVW32DLL.PVCloseImage(image);
        return;
    }

    PVSaveImageInfo save;
    memset(&save, 0, sizeof(save));
    save.cbSize = sizeof(save);
    save.Format = format;
    save.Colors = colors;
    save.ColorModel = colorModel;
    save.Compression = PVCS_DEFAULT;
    save.HorDPI = 96;
    save.VerDPI = 96;

    DeleteFileW(outPathW);
    PVCODE saved = PVW32DLL.PVSaveImage(image, outPathUtf8, &save, NULL, NULL, 0);
    PVW32DLL.PVCloseImage(image);
    if (!Check(saved == PVC_OK, test, "PVSaveImage failed"))
        return;
    if (!Check(GetFileAttributesW(outPathW) != INVALID_FILE_ATTRIBUTES, test, "no output file was produced"))
        return;

    PVImageInfo reopened;
    LPPVHandle result = OpenFile(outPathUtf8, &reopened);
    if (!Check(result != NULL, test, "the saved file could not be reopened"))
        return;
    Check(reopened.Width == FixtureWidth && reopened.Height == FixtureHeight, test, "dimensions changed");
    if (Check(PVW32DLL.PVReadImage2(result, NULL, NULL, NULL, NULL, 0) == PVC_OK, test, "PVReadImage2 failed"))
        ComparePixels(result, reopened.Colors, test);
    PVW32DLL.PVCloseImage(result);
}

// The thumbnail loader and the print preview both take this path: PVF_RAW plus
// a caller-supplied write callback receiving 32bpp BGRA rows.
void TestRawCallbackOutput(const char* bmpUtf8)
{
    const char* test = "PVF_RAW output through a write callback";
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;
    PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0);

    CRawSink sink;
    memset(&sink, 0, sizeof(sink));

    PVSaveImageInfo save;
    memset(&save, 0, sizeof(save));
    save.cbSize = sizeof(save);
    save.Format = PVF_RAW;
    save.Colors = PV_COLOR_TC32;
    save.ColorModel = PVCM_RGB;
    save.Compression = PVCS_NO_COMPRESSION;
    save.Flags = PVSF_USERDEFINED_OUTPUT;
    save.WriteFunc = RawWrite;
    save.SeekFunc = RawSeek;

    PVCODE code = PVW32DLL.PVSaveImage(image, NULL, &save, NULL, &sink, 0);
    PVW32DLL.PVCloseImage(image);

    if (!Check(code == PVC_OK, test, "PVSaveImage failed"))
    {
        free(sink.Data);
        return;
    }
    if (!Check(sink.Size == (size_t)FixtureWidth * FixtureHeight * 4, test, "wrong number of raw bytes"))
    {
        free(sink.Data);
        return;
    }

    bool ok = true;
    for (int y = 0; y < FixtureHeight && ok; y++)
    {
        const BYTE* row = sink.Data + (size_t)y * FixtureWidth * 4;
        for (int x = 0; x < FixtureWidth; x++)
        {
            COLORREF actual = RGB(row[x * 4 + 2], row[x * 4 + 1], row[x * 4]);
            if (actual != ExpectedPixel(x, y))
            {
                Fail(test, "raw rows are not top-down BGRA of the decoded image");
                ok = false;
                break;
            }
        }
    }
    free(sink.Data);
}

void TestCrop(const char* bmpUtf8)
{
    const char* test = "PVCropImage";
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;
    PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0);

    if (Check(PVW32DLL.PVCropImage(image, 4, 3, 8, 6) == PVC_OK, test, "crop failed"))
    {
        PVImageInfo cropped;
        PVW32DLL.PVGetImageInfo(image, &cropped, sizeof(cropped), 0);
        Check(cropped.Width == 8 && cropped.Height == 6, test, "cropped size is wrong");

        COLORREF actual = 0;
        if (Check(ReadPixel(image, cropped.Colors, 0, 0, &actual), test, "GetRGBAtCursor failed"))
            Check(actual == ExpectedPixel(4, 3), test, "crop did not move the origin");
    }
    PVW32DLL.PVCloseImage(image);
}

void TestRotate(const char* bmpUtf8)
{
    const char* test = "PVChangeImage rotate 90 CW";
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;
    PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0);

    if (Check(PVW32DLL.PVChangeImage(image, PVCF_ROTATE90CW) == PVC_OK, test, "rotate failed"))
    {
        PVImageInfo rotated;
        PVW32DLL.PVGetImageInfo(image, &rotated, sizeof(rotated), 0);
        Check(rotated.Width == FixtureHeight && rotated.Height == FixtureWidth, test, "dimensions were not swapped");

        // Rotating clockwise sends the source's bottom-left corner to the top left.
        COLORREF actual = 0;
        if (Check(ReadPixel(image, rotated.Colors, 0, 0, &actual), test, "GetRGBAtCursor failed"))
            Check(actual == ExpectedPixel(0, FixtureHeight - 1), test, "the corner pixel did not rotate");
    }
    PVW32DLL.PVCloseImage(image);
}

void TestDraw(const char* bmpUtf8)
{
    const char* test = "PVDrawImage into a memory DC";
    PVImageInfo info;
    LPPVHandle image = OpenFile(bmpUtf8, &info);
    if (!Check(image != NULL, test, "PVOpenImageEx failed"))
        return;
    PVW32DLL.PVReadImage2(image, NULL, NULL, NULL, NULL, 0);

    HDC screen = GetDC(NULL);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP surface = CreateCompatibleBitmap(screen, 64, 64);
    HGDIOBJ previous = SelectObject(memory, surface);

    PVW32DLL.PVSetStretchParameters(image, FixtureWidth, FixtureHeight, COLORONCOLOR);
    RECT clip = {0, 0, 64, 64};
    Check(PVW32DLL.PVDrawImage(image, memory, 0, 0, &clip) == PVC_OK, test, "PVDrawImage failed");
    Check(GetPixel(memory, 1, 1) == ExpectedPixel(1, 1), test, "the blitted pixel does not match the image");

    // A negative extent means "mirror this axis" while filling the same rectangle.
    PVW32DLL.PVSetStretchParameters(image, (DWORD)(-FixtureWidth), FixtureHeight, COLORONCOLOR);
    Check(PVW32DLL.PVDrawImage(image, memory, 0, 0, &clip) == PVC_OK, test, "mirrored PVDrawImage failed");
    Check(GetPixel(memory, 0, 1) == ExpectedPixel(FixtureWidth - 1, 1), test, "a negative width did not mirror");

    SelectObject(memory, previous);
    DeleteObject(surface);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    PVW32DLL.PVCloseImage(image);
}

void TestCapabilityQuery()
{
    const char* test = "PVIsOutCombSupported";
    Check(PVW32DLL.PVIsOutCombSupported(PVF_BMP, PVCS_DEFAULT, PV_COLOR_TC24, PVCM_RGB) != (DWORD)-1,
          test, "24bpp BMP should be writable");
    Check(PVW32DLL.PVIsOutCombSupported(PVF_TIFF, PVCS_CCITT_4, 2, PVCM_RGB) != (DWORD)-1,
          test, "bilevel CCITT G4 TIFF should be writable");
    Check(PVW32DLL.PVIsOutCombSupported(PVF_TIFF, PVCS_CCITT_4, PV_COLOR_TC24, PVCM_RGB) == (DWORD)-1,
          test, "CCITT G4 is only valid for bilevel images");
    Check(PVW32DLL.PVIsOutCombSupported(PVF_TGA, PVCS_DEFAULT, PV_COLOR_TC24, PVCM_RGB) == (DWORD)-1,
          test, "formats without a WIC encoder must be rejected");
    Check(PVW32DLL.PVIsOutCombSupported(PVF_JPG, PVCS_DEFAULT, 2, PVCM_RGB) == (DWORD)-1,
          test, "JPEG cannot store a bilevel image");
}

void TestErrorText()
{
    const char* test = "PVGetErrorText";
    const char* text = PVW32DLL.PVGetErrorText(PVC_CANNOT_OPEN_FILE);
    Check(text != NULL && text[0] != 0, test, "an error code produced no text");
    // The code space is shared with the compression names the properties dialog shows.
    const char* compression = PVW32DLL.PVGetErrorText(PVCS_LZW);
    Check(compression != NULL && strcmp(compression, "LZW") == 0, test,
          "compression names must come from the host string table");
}

void TestMissingFile()
{
    const char* test = "opening a file no codec understands";
    PVImageInfo info;
    LPPVHandle image = OpenFile("Z:\\this\\path\\does\\not\\exist.png", &info);
    Check(image == NULL, test, "a missing file must not produce a handle");
    Check(PVW32DLL.PVCloseImage(NULL) == PVC_OK, test, "closing a NULL handle must be tolerated");
}

} // namespace

int main()
{
    if (!InitWicEngine(NULL))
    {
        fprintf(stderr, "PictViewEngineTests: the Windows Imaging Component is unavailable\n");
        return 1;
    }
    PVW32DLL.PVSetParam((LPPVHandle)EngineText);

    WCHAR directory[TestPathChars];
    WCHAR temp[TestPathChars];
    if (GetTempPathW(_countof(temp), temp) == 0 ||
        GetTempFileNameW(temp, L"pve", 0, directory) == 0 ||
        !DeleteFileW(directory) || !CreateDirectoryW(directory, NULL))
    {
        fprintf(stderr, "PictViewEngineTests: could not create the test directory\n");
        return 1;
    }

    WCHAR bmpPath[TestPathChars];
    _snwprintf_s(bmpPath, _countof(bmpPath), _TRUNCATE, L"%s\\fixture.bmp", directory);
    if (!WriteFixtureBmp(bmpPath))
    {
        fprintf(stderr, "PictViewEngineTests: could not write the BMP fixture\n");
        return 1;
    }

    char bmpUtf8[TestPathChars * 3];
    WideToUtf8Buffer(bmpPath, bmpUtf8, (int)sizeof(bmpUtf8));

    TestOpenAndDecodeBmp(bmpUtf8);
    TestRawCallbackOutput(bmpUtf8);
    TestCrop(bmpUtf8);
    TestRotate(bmpUtf8);
    TestDraw(bmpUtf8);
    TestCapabilityQuery();
    TestErrorText();
    TestMissingFile();

    struct
    {
        const WCHAR* name;
        DWORD format;
        DWORD colors;
        DWORD colorModel;
        const char* test;
    } roundTrips[] = {
        {L"out.png", PVF_PNG, PV_COLOR_TC24, PVCM_RGB, "PNG round trip (24bpp)"},
        {L"out.bmp", PVF_BMP, PV_COLOR_TC24, PVCM_RGB, "BMP round trip (24bpp)"},
        {L"out.tif", PVF_TIFF, PV_COLOR_TC24, PVCM_RGB, "TIFF round trip (24bpp)"},
        {L"out8.png", PVF_PNG, 256, PVCM_RGB, "PNG round trip (256 colours)"},
        {L"out.gif", PVF_GIF, 256, PVCM_RGB, "GIF round trip (256 colours)"},
    };
    for (int i = 0; i < _countof(roundTrips); i++)
    {
        WCHAR outPath[TestPathChars];
        char outUtf8[TestPathChars * 3];
        _snwprintf_s(outPath, _countof(outPath), _TRUNCATE, L"%s\\%s", directory, roundTrips[i].name);
        WideToUtf8Buffer(outPath, outUtf8, (int)sizeof(outUtf8));
        TestSaveRoundTrip(bmpUtf8, outPath, outUtf8, roundTrips[i].format, roundTrips[i].colors,
                          roundTrips[i].colorModel, roundTrips[i].test);
        DeleteFileW(outPath);
    }

    DeleteFileW(bmpPath);
    RemoveDirectoryW(directory);
    ReleaseWicEngine();

    if (Failures != 0)
    {
        fprintf(stderr, "PictViewEngineTests: %d check(s) failed\n", Failures);
        return 1;
    }
    printf("PictViewEngineTests: all checks passed\n");
    return 0;
}
