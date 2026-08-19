// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

/* Windows Imaging Component back-end for the PictView plug-in. See PVWicEngine.h
 * for why the engine holds no COM state between calls.
 */
#include "precomp.h"

#include <wincodec.h>
#include <shlwapi.h>
#include <strsafe.h>

#include "lib\\pvw32dll.h"
#include "renderer.h"
#include "pictview.h"
#include "PixelAccess.h"
#include "PVWicEngine.h"
#include "..\\..\\common\\checked_arithmetic.h"

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// The engine reports itself as 2.0.0 so the About box can tell a WIC build from
// a legacy PVW32Cnv build at a glance.
#define WIC_ENGINE_VERSION 0x20000

// Rows handed to the progress callback in one go while post-processing pixels.
#define WIC_PROGRESS_BAND 64

// Callback supplied through PVSetParam; resolves engine text IDs against the
// plug-in's language module.
static const char*(WINAPI* ExtTextProc)(int msgID) = NULL;

//****************************************************************************
//
// COM apartment
//
// Every entry point that touches WIC brackets itself with this. Nothing COM
// survives the bracket, so it is legal to leave the apartment again on exit.
//

class CComApartment
{
public:
    CComApartment()
    {
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        // RPC_E_CHANGED_MODE means the thread already lives in an STA. That is
        // fine for WIC; we just must not balance it with CoUninitialize.
        Owned = SUCCEEDED(hr);
    }
    ~CComApartment()
    {
        if (Owned)
            CoUninitialize();
    }

private:
    BOOL Owned;
};

template <class T>
static void SafeRelease(T*& p)
{
    if (p != NULL)
    {
        p->Release();
        p = NULL;
    }
}

static IWICImagingFactory* CreateFactory()
{
    IWICImagingFactory* factory = NULL;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return NULL;
    return factory;
}

//****************************************************************************
//
// Format tables
//

struct CWicContainerMap
{
    const GUID* Container;
    DWORD Format;      // PVF_xxx
    DWORD Compression; // PVCS_xxx assumed when the container implies one
    const char* Name;  // fallback when the language module has no string
};

static const CWicContainerMap ContainerMap[] = {
    {&GUID_ContainerFormatBmp, PVF_BMP, PVCS_NO_COMPRESSION, "Windows Bitmap"},
    {&GUID_ContainerFormatPng, PVF_PNG, PVCS_DEFLATE, "Portable Network Graphics"},
    {&GUID_ContainerFormatJpeg, PVF_JPG, PVCS_JPEG_HUFFMAN, "JPEG"},
    {&GUID_ContainerFormatGif, PVF_GIF, PVCS_LZW, "CompuServe GIF"},
    {&GUID_ContainerFormatTiff, PVF_TIFF, PVCS_UNKNOWN, "TIFF"},
    {&GUID_ContainerFormatIco, PVF_ICO, PVCS_NO_COMPRESSION, "Windows Icon"},
    {&GUID_ContainerFormatDds, PVF_DDS, PVCS_COMPRESSED, "DirectDraw Surface"},
    {&GUID_ContainerFormatWmp, PVF_X, PVCS_COMPRESSED, "JPEG XR"},
    {&GUID_ContainerFormatAdng, PVF_RAW, PVCS_UNKNOWN, "Digital Negative"},
};

// TIFF tag 259 values that map onto a PVCS_xxx the properties dialog can name.
static DWORD TiffCompressionToPVCS(DWORD tag)
{
    switch (tag)
    {
    case 1:
        return PVCS_NO_COMPRESSION;
    case 2:
        return PVCS_HUFFMAN;
    case 3:
        return PVCS_CCITT_3;
    case 4:
        return PVCS_CCITT_4;
    case 5:
        return PVCS_LZW;
    case 6:
    case 7:
        return PVCS_JPEG_HUFFMAN;
    case 8:
    case 32946:
        return PVCS_DEFLATE;
    case 32773:
        return PVCS_PACKBITS;
    }
    return PVCS_UNKNOWN;
}

const char* WicLoadEngineText(int code)
{
    if (ExtTextProc != NULL)
    {
        const char* text = ExtTextProc(code);
        if (text != NULL && text[0] != 0)
            return text;
    }
    return "";
}

//****************************************************************************
//
// CWicImage
//

struct CWicImage
{
    DWORD Signature; // guards against stale handles from the caller

    // --- source, one of the three ---
    WCHAR* FileName; // NULL unless opened from a path
    BYTE* SourceData; // owned copy of an in-memory image file
    DWORD SourceSize;

    BOOL WantThumbnail; // PVOF_THUMBNAIL was requested

    // --- header information, valid from PVOpenImageEx on ---
    DWORD FrameCount;
    DWORD DecodedFrame; // (DWORD)-1 while no frame is decoded
    DWORD Format;
    DWORD Compression;
    DWORD ColorModel;
    DWORD SrcWidth, SrcHeight;
    DWORD FileSize;
    DWORD HorDPI, VerDPI;
    DWORD Flags; // PVFF_xxx
    DWORD TotalBitDepth;
    DWORD ReportedColors; // PictView colour code of the decode target
    char Info1[PV_MAX_INFO_LEN], Info2[PV_MAX_INFO_LEN], Info3[PV_MAX_INFO_LEN];
    char* Comment;
    DWORD CommentSize;
    PVFormatSpecificInfo FSI;

    // --- decoded surface ---
    BYTE* Bits;   // top-down rows, DWORD-aligned stride
    BYTE** Lines; // Lines[y] -> Bits + y * Stride
    DWORD Stride;
    DWORD Width, Height, Bpp;
    DWORD BytesPerLine; // byte-aligned row length the plug-in expects to iterate
    RGBQUAD Palette[256];
    DWORD PaletteColors;
    PVImageHandles Handles; // handed out by PVGetHandles2, one block per image

    // 32bpp BGRA original, kept only for images with alpha so that a later
    // PVSetBkHandle can re-composite without decoding the file again.
    BYTE* AlphaBits;
    DWORD AlphaStride;

    // Mirrored copy of Bits, rebuilt whenever the requested mirroring changes.
    // GDI does not reliably mirror a StretchDIBits call through a negative
    // destination extent, so the flip happens in our own pixels instead.
    BYTE* MirrorBits;
    int MirrorFlags; // MIRROR_HOR | MIRROR_VERT of the cached copy, -1 when stale

    COLORREF BkColor;

    // --- presentation state ---
    int StretchWidth, StretchHeight; // signed: a negative value mirrors that axis
    DWORD StretchMode;

    LPPVImageSequence Sequence;
};

#define WIC_IMAGE_SIGNATURE 0x57494349 // 'WICI'

#define MIRROR_HOR 1
#define MIRROR_VERT 2

static CWicImage* FromHandle(LPPVHandle Img)
{
    CWicImage* image = (CWicImage*)Img;
    if (image == NULL || IsBadReadPtr(image, sizeof(DWORD)) || image->Signature != WIC_IMAGE_SIGNATURE)
        return NULL;
    return image;
}

// Drops the mirrored copy after anything changes the pixels behind it.
static void InvalidateMirror(CWicImage* image)
{
    if (image->MirrorBits != NULL)
    {
        free(image->MirrorBits);
        image->MirrorBits = NULL;
    }
    image->MirrorFlags = -1;
}

static void FreeSurface(CWicImage* image)
{
    InvalidateMirror(image);
    if (image->Lines != NULL)
    {
        free(image->Lines);
        image->Lines = NULL;
    }
    if (image->Bits != NULL)
    {
        free(image->Bits);
        image->Bits = NULL;
    }
    if (image->AlphaBits != NULL)
    {
        free(image->AlphaBits);
        image->AlphaBits = NULL;
    }
    image->Width = image->Height = image->Bpp = image->Stride = image->BytesPerLine = 0;
    image->PaletteColors = 0;
}

static void FreeSequence(CWicImage* image)
{
    LPPVImageSequence seq = image->Sequence;
    while (seq != NULL)
    {
        LPPVImageSequence next = seq->pNext;
        if (seq->ImgHandle != NULL)
            DeleteObject(seq->ImgHandle);
        if (seq->TransparentHandle != NULL)
            DeleteObject(seq->TransparentHandle);
        free(seq);
        seq = next;
    }
    image->Sequence = NULL;
}

static void DestroyImage(CWicImage* image)
{
    FreeSurface(image);
    image->DecodedFrame = (DWORD)-1;
    FreeSequence(image);
    if (image->FileName != NULL)
        free(image->FileName);
    if (image->SourceData != NULL)
        free(image->SourceData);
    if (image->Comment != NULL)
        free(image->Comment);
    image->Signature = 0;
    free(image);
}

//****************************************************************************
//
// Decoder construction
//
// Re-created on demand from whichever source the image was opened with.
//

static IWICBitmapDecoder* CreateDecoder(IWICImagingFactory* factory, CWicImage* image)
{
    IWICBitmapDecoder* decoder = NULL;
    if (image->FileName != NULL)
    {
        if (FAILED(factory->CreateDecoderFromFilename(image->FileName, NULL, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder)))
            return NULL;
        return decoder;
    }
    if (image->SourceData != NULL)
    {
        IStream* stream = SHCreateMemStream(image->SourceData, image->SourceSize);
        if (stream == NULL)
            return NULL;
        HRESULT hr = factory->CreateDecoderFromStream(stream, NULL, WICDecodeMetadataCacheOnDemand, &decoder);
        stream->Release();
        if (FAILED(hr))
            return NULL;
        return decoder;
    }
    return NULL;
}

// Honours PVOF_THUMBNAIL: falls back to the full frame whenever the file has no
// embedded thumbnail, which is what the caller's retry loop expects.
static IWICBitmapSource* SelectSource(IWICBitmapFrameDecode* frame, BOOL wantThumbnail)
{
    if (wantThumbnail)
    {
        IWICBitmapSource* thumb = NULL;
        if (SUCCEEDED(frame->GetThumbnail(&thumb)) && thumb != NULL)
            return thumb;
    }
    frame->AddRef();
    return frame;
}

//****************************************************************************
//
// Header inspection
//

static DWORD PixelFormatToColors(const GUID& pf, DWORD* colorModel, DWORD* totalBits, BOOL* hasAlpha)
{
    *colorModel = PVCM_RGB;
    *hasAlpha = FALSE;

    if (pf == GUID_WICPixelFormatBlackWhite || pf == GUID_WICPixelFormat1bppIndexed)
    {
        *totalBits = 1;
        return 2;
    }
    if (pf == GUID_WICPixelFormat2bppIndexed || pf == GUID_WICPixelFormat2bppGray)
    {
        *totalBits = 2;
        if (pf == GUID_WICPixelFormat2bppGray)
            *colorModel = PVCM_GRAYS;
        return 256; // expanded to 8bpp indexed on decode
    }
    if (pf == GUID_WICPixelFormat4bppIndexed)
    {
        *totalBits = 4;
        return 16;
    }
    if (pf == GUID_WICPixelFormat4bppGray)
    {
        *totalBits = 4;
        *colorModel = PVCM_GRAYS;
        return 16;
    }
    if (pf == GUID_WICPixelFormat8bppIndexed)
    {
        *totalBits = 8;
        return 256;
    }
    if (pf == GUID_WICPixelFormat8bppGray || pf == GUID_WICPixelFormat8bppAlpha)
    {
        *totalBits = 8;
        *colorModel = PVCM_GRAYS;
        return 256;
    }
    if (pf == GUID_WICPixelFormat16bppBGR555)
    {
        *totalBits = 15;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat16bppBGR565)
    {
        *totalBits = 16;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat16bppGray)
    {
        *totalBits = 16;
        *colorModel = PVCM_GRAYS;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat32bppCMYK)
    {
        *totalBits = 32;
        *colorModel = PVCM_CMYK;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat40bppCMYKAlpha)
    {
        *totalBits = 40;
        *colorModel = PVCM_CMYK;
        *hasAlpha = TRUE;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat32bppBGRA || pf == GUID_WICPixelFormat32bppPBGRA ||
        pf == GUID_WICPixelFormat32bppRGBA || pf == GUID_WICPixelFormat32bppPRGBA)
    {
        *totalBits = 32;
        *hasAlpha = TRUE;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat64bppRGBA || pf == GUID_WICPixelFormat64bppBGRA ||
        pf == GUID_WICPixelFormat64bppPRGBA || pf == GUID_WICPixelFormat128bppRGBAFloat)
    {
        *totalBits = 64;
        *hasAlpha = TRUE;
        return PV_COLOR_TC24;
    }
    if (pf == GUID_WICPixelFormat48bppRGB || pf == GUID_WICPixelFormat48bppBGR)
    {
        *totalBits = 48;
        return PV_COLOR_TC24;
    }

    // 24bpp and anything exotic we have not enumerated: composite to plain BGR.
    *totalBits = 24;
    return PV_COLOR_TC24;
}

static DWORD ReadMetadataUInt(IWICMetadataQueryReader* reader, LPCWSTR query, DWORD defaultValue)
{
    if (reader == NULL)
        return defaultValue;
    PROPVARIANT value;
    PropVariantInit(&value);
    DWORD result = defaultValue;
    if (SUCCEEDED(reader->GetMetadataByName(query, &value)))
    {
        switch (value.vt)
        {
        case VT_UI1:
            result = value.bVal;
            break;
        case VT_UI2:
            result = value.uiVal;
            break;
        case VT_UI4:
            result = value.ulVal;
            break;
        case VT_I2:
            result = (DWORD)value.iVal;
            break;
        case VT_I4:
            result = (DWORD)value.lVal;
            break;
        case VT_BOOL:
            result = value.boolVal ? 1 : 0;
            break;
        }
    }
    PropVariantClear(&value);
    return result;
}

static BOOL HasMetadata(IWICMetadataQueryReader* reader, LPCWSTR query)
{
    if (reader == NULL)
        return FALSE;
    PROPVARIANT value;
    PropVariantInit(&value);
    BOOL found = SUCCEEDED(reader->GetMetadataByName(query, &value));
    PropVariantClear(&value);
    return found;
}

static void StoreComment(CWicImage* image, IWICMetadataQueryReader* reader)
{
    static const LPCWSTR queries[] = {
        L"/tEXt/{str=Comment}",       // PNG
        L"/text/{str=Comment}",       // PNG, alternate spelling
        L"/app1/ifd/{ushort=270}",    // JPEG/TIFF ImageDescription
        L"/ifd/{ushort=270}",         // TIFF ImageDescription
        L"/com/TextEntry",            // JPEG comment marker
    };

    if (reader == NULL)
        return;
    for (int i = 0; i < _countof(queries); i++)
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(reader->GetMetadataByName(queries[i], &value)))
        {
            char buffer[1024];
            buffer[0] = 0;
            if (value.vt == VT_LPSTR && value.pszVal != NULL)
                StringCchCopyA(buffer, _countof(buffer), value.pszVal);
            else if (value.vt == VT_LPWSTR && value.pwszVal != NULL)
                WideCharToMultiByte(CP_ACP, 0, value.pwszVal, -1, buffer, _countof(buffer), NULL, NULL);
            if (buffer[0] != 0)
            {
                size_t length = strlen(buffer);
                image->Comment = (char*)malloc(length + 1);
                if (image->Comment != NULL)
                {
                    memcpy(image->Comment, buffer, length + 1);
                    image->CommentSize = (DWORD)length;
                }
                PropVariantClear(&value);
                return;
            }
        }
        PropVariantClear(&value);
    }
}

// Fills the header fields of 'image' from frame 'frameIndex'. Leaves the
// decoded surface alone.
static PVCODE InspectFrame(CWicImage* image, IWICBitmapDecoder* decoder, DWORD frameIndex)
{
    IWICBitmapFrameDecode* frame = NULL;
    if (FAILED(decoder->GetFrame(frameIndex, &frame)) || frame == NULL)
        return PVC_NO_MORE_IMAGES;

    IWICBitmapSource* source = SelectSource(frame, image->WantThumbnail);

    UINT width = 0, height = 0;
    source->GetSize(&width, &height);
    image->SrcWidth = width;
    image->SrcHeight = height;

    WICPixelFormatGUID pf = {0};
    source->GetPixelFormat(&pf);
    BOOL hasAlpha = FALSE;
    image->ReportedColors = PixelFormatToColors(pf, &image->ColorModel, &image->TotalBitDepth, &hasAlpha);

    double dpiX = 0, dpiY = 0;
    if (SUCCEEDED(source->GetResolution(&dpiX, &dpiY)))
    {
        image->HorDPI = (DWORD)(dpiX + 0.5);
        image->VerDPI = (DWORD)(dpiY + 0.5);
    }

    IWICMetadataQueryReader* frameReader = NULL;
    frame->GetMetadataQueryReader(&frameReader);

    if (image->Format == PVF_TIFF)
    {
        DWORD tag = ReadMetadataUInt(frameReader, L"/ifd/{ushort=259}", 0);
        if (tag != 0)
            image->Compression = TiffCompressionToPVCS(tag);
    }

    if (HasMetadata(frameReader, L"/app1/ifd/exif") || HasMetadata(frameReader, L"/ifd/exif") ||
        HasMetadata(frameReader, L"/app1/ifd/{ushort=274}"))
        image->Flags |= PVFF_EXIF;
    if (HasMetadata(frameReader, L"/app13/irb/8bimiptc/iptc") || HasMetadata(frameReader, L"/ifd/iptc"))
        image->Flags |= PVFF_IPTC;

    if (image->Comment == NULL)
        StoreComment(image, frameReader);

    if (image->Format == PVF_GIF)
    {
        image->FSI.GIF.XPosition = ReadMetadataUInt(frameReader, L"/imgdesc/Left", 0);
        image->FSI.GIF.YPosition = ReadMetadataUInt(frameReader, L"/imgdesc/Top", 0);
        image->FSI.GIF.Delay = ReadMetadataUInt(frameReader, L"/grctlext/Delay", 0) * 10; // to ms
        image->FSI.GIF.DisposalMethod = ReadMetadataUInt(frameReader, L"/grctlext/Disposal", PVDM_UNDEFINED);
        image->FSI.GIF.TranspIndex = ReadMetadataUInt(frameReader, L"/grctlext/TransparencyFlag", 0)
                                         ? ReadMetadataUInt(frameReader, L"/grctlext/TransparentColorIndex", 0)
                                         : (unsigned)-1;
    }

    SafeRelease(frameReader);
    source->Release();
    frame->Release();
    return PVC_OK;
}

static void FillInfoStrings(CWicImage* image)
{
    const char* name = WicLoadEngineText(image->Format);
    if (name[0] == 0)
    {
        name = "Bitmap";
        for (int i = 0; i < _countof(ContainerMap); i++)
        {
            if (ContainerMap[i].Format == image->Format)
            {
                name = ContainerMap[i].Name;
                break;
            }
        }
    }
    StringCchCopyA(image->Info1, PV_MAX_INFO_LEN, name);

    if (image->FrameCount > 1)
        _snprintf_s(image->Info2, PV_MAX_INFO_LEN, _TRUNCATE, "%u pages", image->FrameCount);
    else
        image->Info2[0] = 0;

    if (image->Flags & PVFF_EXIF)
        StringCchCopyA(image->Info3, PV_MAX_INFO_LEN, WicLoadEngineText(418)); // "Contains EXIF info"
    else
        image->Info3[0] = 0;
}

static void FillImageInfo(CWicImage* image, LPPVImageInfo info, int size)
{
    memset(info, 0, size);
    info->cbSize = size;
    info->Width = image->SrcWidth;
    info->Height = image->SrcHeight;
    info->FileSize = image->FileSize;
    info->Colors = image->ReportedColors;
    info->Format = image->Format;
    info->Flags = image->Flags;
    info->ColorModel = image->ColorModel;
    info->NumOfImages = image->FrameCount;
    info->CurrentImage = (image->DecodedFrame == (DWORD)-1) ? 0 : image->DecodedFrame;
    info->Compression = image->Compression;
    info->HorDPI = image->HorDPI;
    info->VerDPI = image->VerDPI;
    info->TotalBitDepth = image->TotalBitDepth;
    info->FSI = &image->FSI;
    info->Comment = image->Comment;
    info->CommentSize = image->CommentSize;
    memcpy(info->Info1, image->Info1, PV_MAX_INFO_LEN);
    memcpy(info->Info2, image->Info2, PV_MAX_INFO_LEN);
    memcpy(info->Info3, image->Info3, PV_MAX_INFO_LEN);

    // BytesPerLine describes the decoded surface; before the first decode the
    // plug-in only uses it for display, so derive it from the header.
    DWORD bpp = image->Bpp;
    if (bpp == 0)
        bpp = (image->ReportedColors == 2) ? 1 : (image->ReportedColors == 16) ? 4
                                             : (image->ReportedColors <= 256)  ? 8
                                                                               : 24;
    info->BytesPerLine = (image->SrcWidth * bpp + 7) / 8;

    info->StretchedWidth = abs(image->StretchWidth);
    info->StretchedHeight = abs(image->StretchHeight);
    info->StretchMode = image->StretchMode;
}

//****************************************************************************
//
// Surface allocation and pixel conversion
//

static PVCODE AllocateSurface(CWicImage* image, DWORD width, DWORD height, DWORD bpp)
{
    FreeSurface(image);

    if (width == 0 || height == 0)
        return PVC_INVALID_DIMENSIONS;

    // DWORD-aligned rows so the surface can feed StretchDIBits unchanged, while
    // BytesPerLine stays byte-aligned because that is what the plug-in's
    // histogram and pipette code iterates over.
    uint64_t bits = (uint64_t)width * bpp;
    uint64_t stride = ((bits + 31) / 32) * 4;
    uint64_t total = stride * height;
    size_t allocation = 0;
    if (!CheckedCastUInt64ToSize(total, &allocation) || allocation == 0)
        return PVC_OOM;

    image->Bits = (BYTE*)malloc(allocation);
    if (image->Bits == NULL)
        return PVC_OOM;
    memset(image->Bits, 0, allocation);

    image->Lines = (BYTE**)malloc(sizeof(BYTE*) * height);
    if (image->Lines == NULL)
    {
        free(image->Bits);
        image->Bits = NULL;
        return PVC_OOM;
    }

    image->Stride = (DWORD)stride;
    image->Width = width;
    image->Height = height;
    image->Bpp = bpp;
    image->BytesPerLine = (DWORD)((bits + 7) / 8);
    for (DWORD y = 0; y < height; y++)
        image->Lines[y] = image->Bits + (size_t)y * (size_t)stride;
    return PVC_OK;
}

static void BuildGrayPalette(CWicImage* image, DWORD colors)
{
    for (DWORD i = 0; i < colors; i++)
    {
        BYTE level = (colors > 1) ? (BYTE)((i * 255) / (colors - 1)) : 0;
        image->Palette[i].rgbRed = image->Palette[i].rgbGreen = image->Palette[i].rgbBlue = level;
        image->Palette[i].rgbReserved = 0;
    }
    image->PaletteColors = colors;
}

static BOOL CopyWicPalette(IWICImagingFactory* factory, IWICBitmapSource* source, CWicImage* image)
{
    IWICPalette* palette = NULL;
    if (FAILED(factory->CreatePalette(&palette)))
        return FALSE;
    BOOL ok = FALSE;
    if (SUCCEEDED(source->CopyPalette(palette)))
    {
        UINT count = 0;
        WICColor colors[256];
        if (SUCCEEDED(palette->GetColors(256, colors, &count)) && count > 0)
        {
            memset(image->Palette, 0, sizeof(image->Palette));
            for (UINT i = 0; i < count && i < 256; i++)
            {
                image->Palette[i].rgbRed = (BYTE)((colors[i] >> 16) & 0xFF);
                image->Palette[i].rgbGreen = (BYTE)((colors[i] >> 8) & 0xFF);
                image->Palette[i].rgbBlue = (BYTE)(colors[i] & 0xFF);
                image->Palette[i].rgbReserved = 0;
            }
            image->PaletteColors = count;
            ok = TRUE;
        }
    }
    palette->Release();
    return ok;
}

// Blends one 32bpp BGRA row over BkColor into a 24bpp BGR row.
static void CompositeRow(const BYTE* src, BYTE* dst, DWORD width, COLORREF bk)
{
    const int bkR = GetRValue(bk);
    const int bkG = GetGValue(bk);
    const int bkB = GetBValue(bk);
    for (DWORD x = 0; x < width; x++)
    {
        const int alpha = src[3];
        if (alpha == 255)
        {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
        }
        else if (alpha == 0)
        {
            dst[0] = (BYTE)bkB;
            dst[1] = (BYTE)bkG;
            dst[2] = (BYTE)bkR;
        }
        else
        {
            const int inv = 255 - alpha;
            dst[0] = (BYTE)((src[0] * alpha + bkB * inv) / 255);
            dst[1] = (BYTE)((src[1] * alpha + bkG * inv) / 255);
            dst[2] = (BYTE)((src[2] * alpha + bkR * inv) / 255);
        }
        src += 4;
        dst += 3;
    }
}

static void RecompositeAlpha(CWicImage* image)
{
    if (image->AlphaBits == NULL || image->Bits == NULL || image->Bpp != 24)
        return;
    InvalidateMirror(image);
    for (DWORD y = 0; y < image->Height; y++)
        CompositeRow(image->AlphaBits + (size_t)y * image->AlphaStride,
                     image->Bits + (size_t)y * image->Stride, image->Width, image->BkColor);
}

//****************************************************************************
//
// Decoding
//

static PVCODE DecodeFrame(CWicImage* image, DWORD frameIndex, TProgressProc progress, void* progressArg)
{
    CComApartment com;

    IWICImagingFactory* factory = CreateFactory();
    if (factory == NULL)
        return PVC_EXCEPTION;

    PVCODE result = PVC_EXCEPTION;
    IWICBitmapDecoder* decoder = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICBitmapSource* source = NULL;
    IWICFormatConverter* converter = NULL;

    do
    {
        decoder = CreateDecoder(factory, image);
        if (decoder == NULL)
        {
            result = PVC_CANNOT_OPEN_FILE;
            break;
        }
        if (FAILED(decoder->GetFrame(frameIndex, &frame)) || frame == NULL)
        {
            result = PVC_NO_MORE_IMAGES;
            break;
        }
        source = SelectSource(frame, image->WantThumbnail);

        UINT width = 0, height = 0;
        source->GetSize(&width, &height);
        if (width == 0 || height == 0)
        {
            result = PVC_INVALID_DIMENSIONS;
            break;
        }

        WICPixelFormatGUID pf = {0};
        source->GetPixelFormat(&pf);
        DWORD colorModel = PVCM_RGB, totalBits = 24;
        BOOL hasAlpha = FALSE;
        DWORD colors = PixelFormatToColors(pf, &colorModel, &totalBits, &hasAlpha);

        // Decide the surface format. Indexed sources keep their own depth so the
        // pipette can still report a palette index; everything else lands on
        // 24bpp BGR, with alpha composited over the current background.
        GUID target;
        DWORD bpp;
        if (colors == 2 && (pf == GUID_WICPixelFormatBlackWhite || pf == GUID_WICPixelFormat1bppIndexed))
        {
            // Stay in the source's own bilevel format: converting between the two
            // would only cost a pass and a synthesised palette.
            target = pf;
            bpp = 1;
        }
        else if (colors == 16 && pf == GUID_WICPixelFormat4bppIndexed)
        {
            target = GUID_WICPixelFormat4bppIndexed;
            bpp = 4;
        }
        else if (pf == GUID_WICPixelFormat8bppGray)
        {
            target = GUID_WICPixelFormat8bppGray;
            bpp = 8;
        }
        else if (colors == 256 && (pf == GUID_WICPixelFormat8bppIndexed ||
                                   pf == GUID_WICPixelFormat2bppIndexed || pf == GUID_WICPixelFormat2bppGray ||
                                   pf == GUID_WICPixelFormat4bppGray))
        {
            target = GUID_WICPixelFormat8bppIndexed;
            bpp = 8;
        }
        else if (hasAlpha)
        {
            target = GUID_WICPixelFormat32bppBGRA;
            bpp = 32; // narrowed to 24bpp after compositing
        }
        else
        {
            target = GUID_WICPixelFormat24bppBGR;
            bpp = 24;
        }

        if (pf != target)
        {
            if (FAILED(factory->CreateFormatConverter(&converter)))
                break;
            if (FAILED(converter->Initialize(source, target, WICBitmapDitherTypeNone, NULL, 0.0,
                                             WICBitmapPaletteTypeMedianCut)))
            {
                result = PVC_UNSUP_COLOR_DEPTH;
                break;
            }
            source->Release();
            source = converter;
            converter = NULL; // ownership moved into 'source'
        }

        if (progress != NULL && progress(0, progressArg))
        {
            result = PVC_CANCELED;
            break;
        }

        if (bpp == 32)
        {
            // Decode to a scratch BGRA buffer, keep it for later re-compositing,
            // and publish a 24bpp surface built from it.
            uint64_t alphaStride = (uint64_t)width * 4;
            uint64_t alphaTotal = alphaStride * height;
            size_t alphaAllocation = 0;
            if (!CheckedCastUInt64ToSize(alphaTotal, &alphaAllocation))
            {
                result = PVC_OOM;
                break;
            }
            BYTE* alphaBits = (BYTE*)malloc(alphaAllocation);
            if (alphaBits == NULL)
            {
                result = PVC_OOM;
                break;
            }
            if (FAILED(source->CopyPixels(NULL, (UINT)alphaStride, (UINT)alphaAllocation, alphaBits)))
            {
                free(alphaBits);
                result = PVC_READING_ERROR;
                break;
            }
            result = AllocateSurface(image, width, height, 24);
            if (result != PVC_OK)
            {
                free(alphaBits);
                break;
            }
            image->AlphaBits = alphaBits;
            image->AlphaStride = (DWORD)alphaStride;

            for (DWORD y = 0; y < height; y += WIC_PROGRESS_BAND)
            {
                DWORD last = min(y + WIC_PROGRESS_BAND, height);
                for (DWORD row = y; row < last; row++)
                    CompositeRow(alphaBits + (size_t)row * (size_t)alphaStride,
                                 image->Bits + (size_t)row * image->Stride, width, image->BkColor);
                if (progress != NULL && progress((int)((last * 100) / height), progressArg))
                {
                    result = PVC_CANCELED;
                    break;
                }
            }
            if (result == PVC_CANCELED)
                break;
        }
        else
        {
            result = AllocateSurface(image, width, height, bpp);
            if (result != PVC_OK)
                break;
            uint64_t total = (uint64_t)image->Stride * height;
            size_t bufferSize = 0;
            if (!CheckedCastUInt64ToSize(total, &bufferSize))
            {
                result = PVC_OOM;
                break;
            }
            if (FAILED(source->CopyPixels(NULL, image->Stride, (UINT)bufferSize, image->Bits)))
            {
                result = PVC_READING_ERROR;
                break;
            }
            if (progress != NULL && progress(100, progressArg))
            {
                result = PVC_CANCELED;
                break;
            }
        }

        // Palette for the indexed surfaces.
        if (bpp <= 8)
        {
            if (!CopyWicPalette(factory, source, image))
            {
                DWORD entries = (bpp == 1) ? 2 : (bpp == 4) ? 16
                                                            : 256;
                BuildGrayPalette(image, entries);
            }
            else if (bpp == 1 && image->PaletteColors < 2)
            {
                BuildGrayPalette(image, 2);
            }
        }

        image->DecodedFrame = frameIndex;
        image->ReportedColors = (bpp == 1) ? 2 : (bpp == 4) ? 16
                                             : (bpp == 8)   ? 256
                                                            : PV_COLOR_TC24;
        image->ColorModel = colorModel;
        image->SrcWidth = width;
        image->SrcHeight = height;
        if (image->StretchWidth == 0 || image->StretchHeight == 0)
        {
            image->StretchWidth = (int)width;
            image->StretchHeight = (int)height;
        }
        result = PVC_OK;
    } while (0);

    if (result != PVC_OK)
        image->DecodedFrame = (DWORD)-1; // a half-built surface must not look decoded

    SafeRelease(converter);
    SafeRelease(source);
    SafeRelease(frame);
    SafeRelease(decoder);
    factory->Release();
    return result;
}

PVCODE WicEnsureFrameDecoded(LPPVHandle Img, int imageIndex, TProgressProc progress, void* progressArg)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    if (imageIndex < 0)
        imageIndex = 0;
    if ((DWORD)imageIndex >= image->FrameCount)
        return PVC_NO_MORE_IMAGES;
    if (image->DecodedFrame == (DWORD)imageIndex && image->Bits != NULL)
        return PVC_OK;
    if (image->FileName == NULL && image->SourceData == NULL)
        return (image->Bits != NULL) ? PVC_OK : PVC_INVALID_HANDLE; // attached bitmap
    return DecodeFrame(image, (DWORD)imageIndex, progress, progressArg);
}

BOOL WicGetSurface(LPPVHandle Img, CWicSurface* surface)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL || image->Bits == NULL || surface == NULL)
        return FALSE;
    surface->Bits = image->Bits;
    surface->Stride = image->Stride;
    surface->Width = image->Width;
    surface->Height = image->Height;
    surface->Bpp = image->Bpp;
    surface->Palette = (image->Bpp <= 8) ? image->Palette : NULL;
    surface->PaletteColors = image->PaletteColors;
    surface->AlphaBits = image->AlphaBits;
    surface->AlphaStride = image->AlphaStride;
    surface->BkColor = image->BkColor;
    return TRUE;
}

//****************************************************************************
//
// Drawing
//

static DWORD GetIndexedPixel(const BYTE* row, DWORD bpp, DWORD x)
{
    switch (bpp)
    {
    case 1:
        return (row[x >> 3] & (0x80 >> (x & 7))) ? 1u : 0u;
    case 4:
        return (x & 1) ? (row[x >> 1] & 0x0Fu) : (DWORD)(row[x >> 1] >> 4);
    default:
        return row[x];
    }
}

static void SetIndexedPixel(BYTE* row, DWORD bpp, DWORD x, DWORD value)
{
    switch (bpp)
    {
    case 1:
        if (value)
            row[x >> 3] |= (BYTE)(0x80 >> (x & 7));
        else
            row[x >> 3] &= (BYTE)~(0x80 >> (x & 7));
        break;
    case 4:
        if (x & 1)
            row[x >> 1] = (BYTE)((row[x >> 1] & 0xF0) | (value & 0x0F));
        else
            row[x >> 1] = (BYTE)((row[x >> 1] & 0x0F) | ((value & 0x0F) << 4));
        break;
    default:
        row[x] = (BYTE)value;
        break;
    }
}

// Returns the scanlines to blit for the requested mirroring, building and
// caching a flipped copy the first time each combination is asked for.
static const BYTE* GetDrawBits(CWicImage* image, int flags)
{
    if (flags == 0)
        return image->Bits;
    if (image->MirrorBits != NULL && image->MirrorFlags == flags)
        return image->MirrorBits;

    InvalidateMirror(image);
    size_t total = (size_t)image->Stride * image->Height;
    BYTE* mirrored = (BYTE*)malloc(total);
    if (mirrored == NULL)
        return image->Bits; // draw unmirrored rather than not at all
    memset(mirrored, 0, total);

    const DWORD bpp = image->Bpp;
    for (DWORD y = 0; y < image->Height; y++)
    {
        const DWORD srcY = (flags & MIRROR_VERT) ? (image->Height - 1 - y) : y;
        const BYTE* src = image->Bits + (size_t)srcY * image->Stride;
        BYTE* dst = mirrored + (size_t)y * image->Stride;
        if ((flags & MIRROR_HOR) == 0)
        {
            memcpy(dst, src, image->BytesPerLine);
            continue;
        }
        if (bpp == 24)
        {
            for (DWORD x = 0; x < image->Width; x++)
                memcpy(dst + (size_t)x * 3, src + (size_t)(image->Width - 1 - x) * 3, 3);
        }
        else
        {
            for (DWORD x = 0; x < image->Width; x++)
                SetIndexedPixel(dst, bpp, x, GetIndexedPixel(src, bpp, image->Width - 1 - x));
        }
    }

    image->MirrorBits = mirrored;
    image->MirrorFlags = flags;
    return mirrored;
}

// Builds the BITMAPINFO describing the decoded surface. The caller owns the
// returned block.
static BITMAPINFO* BuildBitmapInfo(CWicImage* image)
{
    DWORD paletteEntries = (image->Bpp <= 8) ? (1u << image->Bpp) : 0;
    size_t size = sizeof(BITMAPINFOHEADER) + paletteEntries * sizeof(RGBQUAD);
    BITMAPINFO* bmi = (BITMAPINFO*)calloc(1, size);
    if (bmi == NULL)
        return NULL;
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = (LONG)image->Width;
    bmi->bmiHeader.biHeight = -(LONG)image->Height; // our rows are top-down
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = (WORD)image->Bpp;
    bmi->bmiHeader.biCompression = BI_RGB;
    if (paletteEntries != 0)
        memcpy(bmi->bmiColors, image->Palette, min(paletteEntries, 256u) * sizeof(RGBQUAD));
    return bmi;
}

static PVCODE DrawSurface(CWicImage* image, HDC dc, int x, int y, LPRECT clip)
{
    if (image->Bits == NULL)
        return PVC_INVALID_HANDLE;

    BITMAPINFO* bmi = BuildBitmapInfo(image);
    if (bmi == NULL)
        return PVC_OOM;

    int destWidth = (image->StretchWidth != 0) ? image->StretchWidth : (int)image->Width;
    int destHeight = (image->StretchHeight != 0) ? image->StretchHeight : (int)image->Height;

    // A negative extent means "mirror this axis" while still filling the same
    // destination rectangle. That is the convention the renderer uses through
    // PVSetStretchParameters; the flip is applied to our own pixels because a
    // negative StretchDIBits extent is not a documented way to mirror.
    int mirror = 0;
    if (destWidth < 0)
    {
        mirror |= MIRROR_HOR;
        destWidth = -destWidth;
    }
    if (destHeight < 0)
    {
        mirror |= MIRROR_VERT;
        destHeight = -destHeight;
    }
    const BYTE* bits = GetDrawBits(image, mirror);

    int savedDC = SaveDC(dc);
    if (clip != NULL)
        IntersectClipRect(dc, clip->left, clip->top, clip->right, clip->bottom);

    // HALFTONE gives visibly better downscaling, but GDI only honours it well
    // for packed-pixel DIBs; palettised surfaces keep the cheaper mode.
    if (image->Bpp == 24 && (destWidth < (int)image->Width || destHeight < (int)image->Height))
    {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
    }
    else
    {
        SetStretchBltMode(dc, COLORONCOLOR);
    }

    int rows = StretchDIBits(dc, x, y, destWidth, destHeight,
                             0, 0, (int)image->Width, (int)image->Height,
                             bits, bmi, DIB_RGB_COLORS, SRCCOPY);

    if (savedDC != 0)
        RestoreDC(dc, savedDC);
    free(bmi);
    return (rows == GDI_ERROR) ? PVC_GDI_ERROR : PVC_OK;
}

//****************************************************************************
//
// Source acquisition
//

static BOOL LoadFileIntoImage(CWicImage* image, const char* fileNameUtf8)
{
    // Paths cross this boundary as UTF-8 like everywhere else in the plug-in,
    // so filenames outside the ANSI code page survive the round trip.
    WCHAR* wide = Utf8AllocWide(fileNameUtf8);
    if (wide == NULL)
        return FALSE;
    image->FileName = wide;

    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (GetFileAttributesExW(wide, GetFileExInfoStandard, &attributes) &&
        attributes.nFileSizeHigh == 0)
        image->FileSize = attributes.nFileSizeLow;
    return TRUE;
}

static BOOL LoadCallbackSource(CWicImage* image, LPPVOpenImageExInfo info)
{
    if (info->DataSize == 0 || info->ReadFunc == NULL)
        return FALSE;
    BYTE* buffer = (BYTE*)malloc(info->DataSize);
    if (buffer == NULL)
        return FALSE;
    if (info->SeekFunc != NULL)
        info->SeekFunc(info->Handle, 0, FILE_BEGIN);
    DWORD read = info->ReadFunc(info->Handle, buffer, info->DataSize);
    if (read == 0)
    {
        free(buffer);
        return FALSE;
    }
    image->SourceData = buffer;
    image->SourceSize = read;
    image->FileSize = read;
    return TRUE;
}

// Copies an externally supplied HBITMAP (screen capture, clipboard paste) into
// a 24bpp surface. Such an image has no file behind it, so it can never be
// re-decoded and its single frame is ready immediately.
static PVCODE LoadFromBitmap(CWicImage* image, HBITMAP bitmap)
{
    BITMAP header;
    if (GetObject(bitmap, sizeof(header), &header) == 0)
        return PVC_INVALID_HANDLE;

    PVCODE result = AllocateSurface(image, header.bmWidth, abs(header.bmHeight), 24);
    if (result != PVC_OK)
        return result;

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = header.bmWidth;
    bmi.bmiHeader.biHeight = -abs(header.bmHeight); // request top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    int copied = GetDIBits(screen, bitmap, 0, abs(header.bmHeight), image->Bits, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, screen);
    if (copied == 0)
    {
        FreeSurface(image);
        return PVC_GDI_ERROR;
    }

    image->SrcWidth = image->Width;
    image->SrcHeight = image->Height;
    image->FrameCount = 1;
    image->DecodedFrame = 0;
    image->Format = PVF_BMP;
    image->Compression = PVCS_NO_COMPRESSION;
    image->ColorModel = PVCM_RGB;
    image->ReportedColors = PV_COLOR_TC24;
    image->TotalBitDepth = 24;
    image->StretchWidth = (int)image->Width;
    image->StretchHeight = (int)image->Height;
    StringCchCopyA(image->Info1, PV_MAX_INFO_LEN,
                   WicLoadEngineText(3)[0] != 0 ? WicLoadEngineText(3) : "Imported bitmap");
    return PVC_OK;
}

//****************************************************************************
//
// Engine entry points
//

static PVCODE WINAPI WicOpenImageEx(LPPVHandle* Img, LPPVOpenImageExInfo pOpenExInfo,
                                    LPPVImageInfo pImgInfo, int Size)
{
    if (Img == NULL || pOpenExInfo == NULL)
        return PVC_INCORRECT_PARAMETER;
    *Img = NULL;

    CWicImage* image = (CWicImage*)calloc(1, sizeof(CWicImage));
    if (image == NULL)
        return PVC_OOM;
    image->Signature = WIC_IMAGE_SIGNATURE;
    image->DecodedFrame = (DWORD)-1;
    image->MirrorFlags = -1;
    image->BkColor = RGB(255, 255, 255);
    image->StretchMode = COLORONCOLOR;
    image->FSI.cbSize = sizeof(image->FSI);
    image->WantThumbnail = (pOpenExInfo->Flags & PVOF_THUMBNAIL) != 0;

    PVCODE result;
    if (pOpenExInfo->Flags & PVOF_ATTACH_TO_HANDLE)
    {
        result = LoadFromBitmap(image, (HBITMAP)pOpenExInfo->Handle);
        if (result != PVC_OK)
        {
            DestroyImage(image);
            return result;
        }
        FillInfoStrings(image);
        if (pImgInfo != NULL)
            FillImageInfo(image, pImgInfo, Size);
        *Img = (LPPVHandle)image;
        return PVC_OK;
    }

    BOOL haveSource = FALSE;
    if (pOpenExInfo->Flags & PVOF_USERDEFINED_INPUT)
        haveSource = LoadCallbackSource(image, pOpenExInfo);
    if (!haveSource && pOpenExInfo->FileName != NULL)
        haveSource = LoadFileIntoImage(image, pOpenExInfo->FileName);
    if (!haveSource)
    {
        DestroyImage(image);
        return PVC_CANNOT_OPEN_FILE;
    }

    CComApartment com;
    IWICImagingFactory* factory = CreateFactory();
    if (factory == NULL)
    {
        DestroyImage(image);
        return PVC_EXCEPTION;
    }

    IWICBitmapDecoder* decoder = CreateDecoder(factory, image);
    if (decoder == NULL)
    {
        factory->Release();
        DestroyImage(image);
        return PVC_UNKNOWN_FILE_STRUCT;
    }

    GUID container = {0};
    decoder->GetContainerFormat(&container);
    image->Format = 0;
    image->Compression = PVCS_UNKNOWN;
    for (int i = 0; i < _countof(ContainerMap); i++)
    {
        if (container == *ContainerMap[i].Container)
        {
            image->Format = ContainerMap[i].Format;
            image->Compression = ContainerMap[i].Compression;
            break;
        }
    }

    UINT frames = 0;
    decoder->GetFrameCount(&frames);
    image->FrameCount = max(1u, frames);

    result = InspectFrame(image, decoder, 0);
    if (result == PVC_OK && image->Format == PVF_GIF)
    {
        IWICMetadataQueryReader* containerReader = NULL;
        if (SUCCEEDED(decoder->GetMetadataQueryReader(&containerReader)) && containerReader != NULL)
        {
            image->FSI.GIF.ScreenWidth = ReadMetadataUInt(containerReader, L"/logscrdesc/Width", image->SrcWidth);
            image->FSI.GIF.ScreenHeight = ReadMetadataUInt(containerReader, L"/logscrdesc/Height", image->SrcHeight);
            containerReader->Release();
        }
        else
        {
            image->FSI.GIF.ScreenWidth = image->SrcWidth;
            image->FSI.GIF.ScreenHeight = image->SrcHeight;
        }
        image->FSI.GIF.BgColor = RGB(255, 255, 255);
        if (image->FrameCount > 1)
            image->Flags |= PVFF_IMAGESEQUENCE;
    }

    decoder->Release();
    factory->Release();

    if (result != PVC_OK)
    {
        DestroyImage(image);
        return result;
    }

    FillInfoStrings(image);
    if (pImgInfo != NULL)
        FillImageInfo(image, pImgInfo, Size);
    *Img = (LPPVHandle)image;
    return PVC_OK;
}

static PVCODE WINAPI WicCloseImage(LPPVHandle Img)
{
    if (Img == NULL)
        return PVC_OK; // callers routinely close a handle they never opened
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    DestroyImage(image);
    return PVC_OK;
}

static PVCODE WINAPI WicGetImageInfo(LPPVHandle Img, LPPVImageInfo pImgInfo, int Size, int ImageIndex)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL || pImgInfo == NULL)
        return PVC_INVALID_HANDLE;
    if (ImageIndex < 0 || (DWORD)ImageIndex >= image->FrameCount)
        return PVC_NO_MORE_IMAGES;

    // Re-inspect only when the caller asks about a frame we have not looked at.
    if ((DWORD)ImageIndex != image->DecodedFrame && image->FileName != NULL)
    {
        CComApartment com;
        IWICImagingFactory* factory = CreateFactory();
        if (factory != NULL)
        {
            IWICBitmapDecoder* decoder = CreateDecoder(factory, image);
            if (decoder != NULL)
            {
                InspectFrame(image, decoder, (DWORD)ImageIndex);
                decoder->Release();
            }
            factory->Release();
        }
    }

    FillImageInfo(image, pImgInfo, Size);
    pImgInfo->CurrentImage = ImageIndex;
    return PVC_OK;
}

static PVCODE WINAPI WicReadImage2(LPPVHandle Img, HDC PaintDC, RECT* pDRect,
                                   TProgressProc Progress, void* AppSpecific, int ImageIndex)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;

    PVCODE result = WicEnsureFrameDecoded(Img, ImageIndex, Progress, AppSpecific);
    if (result != PVC_OK)
        return result;

    if (PaintDC != NULL)
    {
        RECT clip;
        if (pDRect != NULL)
            clip = *pDRect;
        int x = (pDRect != NULL) ? pDRect->left : 0;
        int y = (pDRect != NULL) ? pDRect->top : 0;
        DrawSurface(image, PaintDC, x, y, (pDRect != NULL) ? &clip : NULL);
    }
    return PVC_OK;
}

static PVCODE WINAPI WicDrawImage(LPPVHandle Img, HDC PaintDC, int X, int Y, LPRECT rect)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    if (PaintDC == NULL)
        return PVC_INCORRECT_PARAMETER;
    return DrawSurface(image, PaintDC, X, Y, rect);
}

static PVCODE WINAPI WicGetHandles2(LPPVHandle Img, LPPVImageHandles* pHandles)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL || pHandles == NULL)
        return PVC_INVALID_HANDLE;
    if (image->Bits == NULL)
        return PVC_INVALID_HANDLE;

    // The block belongs to the image, so two images examined from the same
    // thread cannot overwrite each other's scanline table.
    memset(&image->Handles, 0, sizeof(image->Handles));
    image->Handles.pLines = image->Lines;
    image->Handles.Palette = (image->Bpp <= 8) ? image->Palette : NULL;
    *pHandles = &image->Handles;
    return PVC_OK;
}

static PVCODE WINAPI WicSetBkHandle(LPPVHandle Img, COLORREF BkColor)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    if (image->BkColor != BkColor)
    {
        image->BkColor = BkColor;
        RecompositeAlpha(image);
    }
    return PVC_OK;
}

static PVCODE WINAPI WicSetStretchParameters(LPPVHandle Img, DWORD Width, DWORD Height, DWORD Mode)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    // The renderer encodes a mirrored axis as a negative extent, which arrives
    // here through a DWORD parameter.
    image->StretchWidth = (int)Width;
    image->StretchHeight = (int)Height;
    image->StretchMode = Mode;
    return PVC_OK;
}

static PVCODE WINAPI WicCropImage(LPPVHandle Img, int Left, int Top, int Width, int Height)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    if (image->Bits == NULL)
        return PVC_INVALID_HANDLE;
    if (Left < 0 || Top < 0 || Width <= 0 || Height <= 0 ||
        (DWORD)(Left + Width) > image->Width || (DWORD)(Top + Height) > image->Height)
        return PVC_INCORRECT_PARAMETER;

    // Cropping at a bit offset would need a shifting copy for the sub-byte
    // formats; align the left edge outwards instead of corrupting the rows.
    if (image->Bpp == 1)
        Left &= ~7;
    else if (image->Bpp == 4)
        Left &= ~1;

    const DWORD bpp = image->Bpp;
    const DWORD srcStride = image->Stride;
    BYTE* srcBits = image->Bits;
    BYTE** srcLines = image->Lines;
    const DWORD firstByte = (Left * bpp) / 8;
    const DWORD decodedFrame = image->DecodedFrame;

    // Detach the old surface so AllocateSurface cannot free what we still read.
    image->Bits = NULL;
    image->Lines = NULL;
    BYTE* oldAlpha = image->AlphaBits;
    DWORD oldAlphaStride = image->AlphaStride;
    image->AlphaBits = NULL;

    PVCODE result = AllocateSurface(image, Width, Height, bpp);
    if (result != PVC_OK)
    {
        free(srcLines);
        free(srcBits);
        free(oldAlpha);
        return result;
    }

    for (int y = 0; y < Height; y++)
        memcpy(image->Lines[y], srcBits + (size_t)(Top + y) * srcStride + firstByte, image->BytesPerLine);

    if (oldAlpha != NULL)
    {
        size_t alphaStride = (size_t)Width * 4;
        BYTE* alpha = (BYTE*)malloc(alphaStride * Height);
        if (alpha != NULL)
        {
            for (int y = 0; y < Height; y++)
                memcpy(alpha + (size_t)y * alphaStride,
                       oldAlpha + (size_t)(Top + y) * oldAlphaStride + (size_t)Left * 4, alphaStride);
            image->AlphaBits = alpha;
            image->AlphaStride = (DWORD)alphaStride;
        }
        free(oldAlpha);
    }

    free(srcLines);
    free(srcBits);

    image->DecodedFrame = decodedFrame;
    image->SrcWidth = image->Width;
    image->SrcHeight = image->Height;
    image->StretchWidth = (int)image->Width;
    image->StretchHeight = (int)image->Height;
    return PVC_OK;
}

// Rotates the decoded surface by 90 degrees. Sub-byte formats are promoted to
// 8bpp first because rotating them in place would mean bit-level scatter.
static PVCODE RotateSurface(CWicImage* image, BOOL clockwise)
{
    if (image->Bits == NULL)
        return PVC_INVALID_HANDLE;

    const DWORD decodedFrame = image->DecodedFrame;

    if (image->Bpp == 1 || image->Bpp == 4)
    {
        const DWORD srcBpp = image->Bpp;
        const DWORD width = image->Width, height = image->Height;
        const DWORD srcStride = image->Stride;
        BYTE* srcBits = image->Bits;
        BYTE** srcLines = image->Lines;
        image->Bits = NULL;
        image->Lines = NULL;

        PVCODE promoted = AllocateSurface(image, width, height, 8);
        if (promoted != PVC_OK)
        {
            free(srcLines);
            free(srcBits);
            return promoted;
        }
        for (DWORD y = 0; y < height; y++)
        {
            const BYTE* src = srcBits + (size_t)y * srcStride;
            BYTE* dst = image->Lines[y];
            for (DWORD x = 0; x < width; x++)
            {
                dst[x] = (srcBpp == 1) ? ((src[x >> 3] & (0x80 >> (x & 7))) ? 1 : 0)
                                       : ((x & 1) ? (src[x >> 1] & 0x0F) : (src[x >> 1] >> 4));
            }
        }
        free(srcLines);
        free(srcBits);
        image->ReportedColors = 256;
    }

    const DWORD bpp = image->Bpp;
    const DWORD bytes = bpp / 8;
    const DWORD width = image->Width, height = image->Height;
    const DWORD srcStride = image->Stride;
    BYTE* srcBits = image->Bits;
    BYTE** srcLines = image->Lines;
    BYTE* srcAlpha = image->AlphaBits;
    const DWORD srcAlphaStride = image->AlphaStride;
    image->Bits = NULL;
    image->Lines = NULL;
    image->AlphaBits = NULL;

    PVCODE result = AllocateSurface(image, height, width, bpp);
    if (result != PVC_OK)
    {
        free(srcLines);
        free(srcBits);
        free(srcAlpha);
        return result;
    }

    for (DWORD y = 0; y < height; y++)
    {
        const BYTE* src = srcBits + (size_t)y * srcStride;
        for (DWORD x = 0; x < width; x++)
        {
            DWORD dstX = clockwise ? (height - 1 - y) : y;
            DWORD dstY = clockwise ? x : (width - 1 - x);
            memcpy(image->Lines[dstY] + (size_t)dstX * bytes, src + (size_t)x * bytes, bytes);
        }
    }

    if (srcAlpha != NULL)
    {
        size_t alphaStride = (size_t)height * 4;
        BYTE* alpha = (BYTE*)malloc(alphaStride * width);
        if (alpha != NULL)
        {
            for (DWORD y = 0; y < height; y++)
            {
                const BYTE* src = srcAlpha + (size_t)y * srcAlphaStride;
                for (DWORD x = 0; x < width; x++)
                {
                    DWORD dstX = clockwise ? (height - 1 - y) : y;
                    DWORD dstY = clockwise ? x : (width - 1 - x);
                    memcpy(alpha + (size_t)dstY * alphaStride + (size_t)dstX * 4, src + (size_t)x * 4, 4);
                }
            }
            image->AlphaBits = alpha;
            image->AlphaStride = (DWORD)alphaStride;
        }
        free(srcAlpha);
    }

    free(srcLines);
    free(srcBits);

    image->DecodedFrame = decodedFrame;
    image->SrcWidth = image->Width;
    image->SrcHeight = image->Height;
    image->StretchWidth = (int)image->Width;
    image->StretchHeight = (int)image->Height;
    DWORD dpi = image->HorDPI;
    image->HorDPI = image->VerDPI;
    image->VerDPI = dpi;
    return PVC_OK;
}

static PVCODE WINAPI WicChangeImage(LPPVHandle Img, DWORD Flags)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL)
        return PVC_INVALID_HANDLE;
    if (Flags & PVCF_ROTATE90CW)
        return RotateSurface(image, TRUE);
    if (Flags & PVCF_ROTATE90CCW)
        return RotateSurface(image, FALSE);
    return PVC_INCORRECT_PARAMETER;
}

static DWORD WINAPI WicGetDLLVersion(void)
{
    return WIC_ENGINE_VERSION;
}

static const char* WINAPI WicGetErrorText(DWORD ErrorCode)
{
    // The engine's texts live in the plug-in's language module at IDS_DLL+code;
    // the code space is shared by PVC_ errors, PVCS_ compression names and
    // PVF_ format names, which is why the properties dialog can call this to
    // label a compression scheme.
    const char* text = WicLoadEngineText((int)ErrorCode);
    if (text[0] != 0)
        return text;

    switch (ErrorCode)
    {
    case PVC_OK:
        return "";
    case PVC_CANCELED:
        return "Canceled.";
    case PVC_OOM:
    case PVC_OUT_OF_MEMORY:
        return "Out of memory.";
    case PVC_INVALID_HANDLE:
        return "Invalid image handle.";
    case PVC_NO_MORE_IMAGES:
        return "No more images in the file.";
    case PVC_GDI_ERROR:
        return "A GDI operation failed.";
    case PVC_INVALID_DIMENSIONS:
        return "Invalid image dimensions.";
    case PVC_CANNOT_OPEN_FILE:
        return "Cannot open the file.";
    case PVC_UNKNOWN_FILE_STRUCT:
        return "No installed imaging codec recognizes this file.";
    case PVC_UNSUP_FILE_TYPE:
        return "Unsupported file type.";
    case PVC_UNSUP_COLOR_DEPTH:
        return "Unsupported color depth.";
    case PVC_UNSUP_OUT_PARAMS:
        return "Unsupported combination of output format, compression, and bit-depth.";
    case PVC_READING_ERROR:
        return "Error reading the image data.";
    case PVC_WRITING_ERROR:
        return "Error writing the image data.";
    case PVC_ERROR_CREATING_FILE:
        return "Cannot create the output file.";
    }
    return "Unknown error.";
}

static PVCODE WINAPI WicSetParam(LPPVHandle Img)
{
    // PVSetParam is (ab)used to hand the engine the plug-in's string loader.
    ExtTextProc = (const char*(WINAPI*)(int))Img;
    return PVC_OK;
}

static PVCODE WINAPI WicLoadFromClipboard(LPPVHandle* Img, LPPVImageInfo pImgInfo, int Size)
{
    if (Img == NULL)
        return PVC_INCORRECT_PARAMETER;
    *Img = NULL;

    if (!IsClipboardFormatAvailable(CF_BITMAP) && !IsClipboardFormatAvailable(CF_DIB))
        return PVC_UNSUP_FILE_TYPE;
    if (!OpenClipboard(NULL))
        return PVC_CANNOT_OPEN_FILE;

    PVCODE result = PVC_UNSUP_FILE_TYPE;
    HBITMAP bitmap = (HBITMAP)GetClipboardData(CF_BITMAP);
    if (bitmap != NULL)
    {
        PVOpenImageExInfo info;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        info.Flags = PVOF_ATTACH_TO_HANDLE;
        info.Handle = bitmap;
        result = WicOpenImageEx(Img, &info, pImgInfo, Size);
    }
    CloseClipboard();
    return result;
}

//****************************************************************************
//
// Image sequences (animated GIF)
//

// Builds the AND mask the renderer blits with SRCAND before SRCINVERT-ing the
// colour frame over it, and blacks out the transparent pixels in 'colorBits'
// so that the two-pass blit leaves the background showing through.
static HBITMAP BuildTransparencyMask(HDC dc, const BYTE* bgra, DWORD stride,
                                     DWORD width, DWORD height, BYTE* colorBits, DWORD colorStride)
{
    DWORD maskStride = ((width + 31) / 32) * 4;
    BYTE* maskBits = (BYTE*)calloc((size_t)maskStride * height, 1);
    if (maskBits == NULL)
        return NULL;

    BOOL anyTransparent = FALSE;
    for (DWORD y = 0; y < height; y++)
    {
        const BYTE* src = bgra + (size_t)y * stride;
        BYTE* mask = maskBits + (size_t)y * maskStride;
        BYTE* color = colorBits + (size_t)y * colorStride;
        for (DWORD x = 0; x < width; x++)
        {
            if (src[x * 4 + 3] < 128)
            {
                mask[x >> 3] |= (BYTE)(0x80 >> (x & 7)); // white in the mask
                color[x * 3] = color[x * 3 + 1] = color[x * 3 + 2] = 0;
                anyTransparent = TRUE;
            }
        }
    }
    if (!anyTransparent)
    {
        free(maskBits);
        return NULL;
    }

    HBITMAP mask = CreateBitmap(width, height, 1, 1, NULL);
    if (mask != NULL)
    {
        BITMAPINFO* bmi = (BITMAPINFO*)calloc(1, sizeof(BITMAPINFOHEADER) + 2 * sizeof(RGBQUAD));
        if (bmi != NULL)
        {
            bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi->bmiHeader.biWidth = (LONG)width;
            bmi->bmiHeader.biHeight = -(LONG)height;
            bmi->bmiHeader.biPlanes = 1;
            bmi->bmiHeader.biBitCount = 1;
            bmi->bmiHeader.biCompression = BI_RGB;
            bmi->bmiColors[1].rgbRed = bmi->bmiColors[1].rgbGreen = bmi->bmiColors[1].rgbBlue = 255;
            SetDIBits(dc, mask, 0, height, maskBits, bmi, DIB_RGB_COLORS);
            free(bmi);
        }
        else
        {
            DeleteObject(mask);
            mask = NULL;
        }
    }
    free(maskBits);
    return mask;
}

static PVCODE WINAPI WicReadImageSequence(LPPVHandle Img, LPPVImageSequence* ppSeq)
{
    CWicImage* image = FromHandle(Img);
    if (image == NULL || ppSeq == NULL)
        return PVC_INVALID_HANDLE;

    *ppSeq = NULL;
    if (image->Sequence != NULL)
    {
        *ppSeq = image->Sequence;
        return PVC_OK;
    }
    if (image->FileName == NULL && image->SourceData == NULL)
        return PVC_UNSUP_FILE_TYPE;

    CComApartment com;
    IWICImagingFactory* factory = CreateFactory();
    if (factory == NULL)
        return PVC_EXCEPTION;

    IWICBitmapDecoder* decoder = CreateDecoder(factory, image);
    if (decoder == NULL)
    {
        factory->Release();
        return PVC_CANNOT_OPEN_FILE;
    }

    HDC screen = GetDC(NULL);
    LPPVImageSequence head = NULL, tail = NULL;
    PVCODE result = PVC_OK;

    for (DWORD i = 0; i < image->FrameCount; i++)
    {
        IWICBitmapFrameDecode* frame = NULL;
        if (FAILED(decoder->GetFrame(i, &frame)) || frame == NULL)
            break;

        UINT width = 0, height = 0;
        frame->GetSize(&width, &height);

        IWICFormatConverter* converter = NULL;
        BYTE* bgra = NULL;
        DWORD bgraStride = width * 4;
        if (width != 0 && height != 0 && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                            NULL, 0.0, WICBitmapPaletteTypeCustom)))
        {
            bgra = (BYTE*)malloc((size_t)bgraStride * height);
            if (bgra != NULL &&
                FAILED(converter->CopyPixels(NULL, bgraStride, bgraStride * height, bgra)))
            {
                free(bgra);
                bgra = NULL;
            }
        }
        SafeRelease(converter);

        if (bgra == NULL)
        {
            frame->Release();
            result = PVC_READING_ERROR;
            break;
        }

        DWORD colorStride = ((width * 24 + 31) / 32) * 4;
        BYTE* colorBits = (BYTE*)calloc((size_t)colorStride * height, 1);
        LPPVImageSequence node = (LPPVImageSequence)calloc(1, sizeof(PVImageSequence));
        if (colorBits == NULL || node == NULL)
        {
            free(colorBits);
            free(node);
            free(bgra);
            frame->Release();
            result = PVC_OOM;
            break;
        }
        for (DWORD y = 0; y < height; y++)
        {
            const BYTE* src = bgra + (size_t)y * bgraStride;
            BYTE* dst = colorBits + (size_t)y * colorStride;
            for (DWORD x = 0; x < width; x++)
            {
                dst[x * 3] = src[x * 4];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }

        node->TransparentHandle = BuildTransparencyMask(screen, bgra, bgraStride, width, height,
                                                        colorBits, colorStride);

        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof(bmi));
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)width;
        bmi.bmiHeader.biHeight = -(LONG)height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;
        node->ImgHandle = CreateCompatibleBitmap(screen, width, height);
        if (node->ImgHandle != NULL)
            SetDIBits(screen, node->ImgHandle, 0, height, colorBits, &bmi, DIB_RGB_COLORS);

        IWICMetadataQueryReader* reader = NULL;
        frame->GetMetadataQueryReader(&reader);
        node->Rect.left = ReadMetadataUInt(reader, L"/imgdesc/Left", 0);
        node->Rect.top = ReadMetadataUInt(reader, L"/imgdesc/Top", 0);
        node->Rect.right = node->Rect.left + width;
        node->Rect.bottom = node->Rect.top + height;
        node->Delay = ReadMetadataUInt(reader, L"/grctlext/Delay", 10) * 10; // 1/100 s -> ms
        if (node->Delay == 0)
            node->Delay = 100;
        node->DisposalMethod = ReadMetadataUInt(reader, L"/grctlext/Disposal", PVDM_UNDEFINED);
        SafeRelease(reader);

        free(colorBits);
        free(bgra);
        frame->Release();

        if (head == NULL)
            head = node;
        else
            tail->pNext = node;
        tail = node;
    }

    ReleaseDC(NULL, screen);
    decoder->Release();
    factory->Release();

    if (head == NULL)
        return (result == PVC_OK) ? PVC_UNSUP_FILE_TYPE : result;

    image->Sequence = head;
    *ppSeq = head;
    return PVC_OK;
}

//****************************************************************************
//
// Table installation
//

static struct CPVW32DLL WicProcTable = {
    WicReadImage2,
    WicCloseImage,
    WicDrawImage,
    WicGetErrorText,
    WicOpenImageEx,
    WicSetBkHandle,
    WicGetDLLVersion,
    WicSetStretchParameters,
    WicLoadFromClipboard,
    WicGetImageInfo,
    WicSetParam,
    WicGetHandles2,
    WicSaveImage,
    WicChangeImage,
    WicIsOutCombSupported,
    WicReadImageSequence,
    WicCropImage,
    GetRGBAtCursor,
    CalculateHistogram,
    CreateThumbnail,
    SimplifyImageSequence};

BOOL InitWicEngine(HWND hParentWnd)
{
    CComApartment com;
    IWICImagingFactory* factory = CreateFactory();
    if (factory == NULL)
    {
        TRACE_E("Windows Imaging Component is not available");
        SalamanderGeneral->SalMessageBox(hParentWnd, LoadStr(IDS_DLL_NOTFOUND),
                                         LoadStr(IDS_ERRORTITLE), MB_ICONSTOP | MB_OK);
        return FALSE;
    }
    factory->Release();

    PVW32DLL = WicProcTable;
    PVW32DLL.Handle = NULL; // no external module backs the engine any more
    return TRUE;
}

void ReleaseWicEngine()
{
    ExtTextProc = NULL;
}
