// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

/* Encoding half of the WIC engine: PVSaveImage and the capability query the
 * Save As dialog uses to populate its format, bit-depth and compression lists.
 *
 * Two very different consumers share PVSaveImage. The Save As dialog and the
 * batch converter write real files through a WIC encoder, while the thumbnail
 * loader and the print preview ask for PVF_RAW and collect uncompressed BGRA
 * scanlines through their own write callback. Both go through the same
 * crop -> flip/rotate -> scale pipeline so the two stay consistent.
 */
#include "precomp.h"

#include <wincodec.h>
#include <shlwapi.h>

#include "lib\\pvw32dll.h"
#include "renderer.h"
#include "pictview.h"
#include "PVWicEngine.h"

// Save As offers this as a speed hint for heavily downscaled thumbnails.
#define PVSF_SUPERFAST 0x8000000

template <class T>
static void SafeRelease(T*& p)
{
    if (p != NULL)
    {
        p->Release();
        p = NULL;
    }
}

// Initializes COM for WIC save and uninitializes it in the destructor.
class CComApartmentSave
{
public:
    CComApartmentSave() { Owned = SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)); }
    ~CComApartmentSave()
    {
        if (Owned)
            CoUninitialize();
    }

private:
    BOOL Owned;
};

//****************************************************************************
//
// Capability table
//
// WIC ships encoders for BMP, GIF, JPEG, PNG and TIFF only. The remaining
// PictView output formats had no free replacement, so they are reported as
// unsupported here and removed from the Save As filters.
//

struct CWicOutFormat
{
    DWORD Format;      // PVF_xxx
    const GUID* Container;
    DWORD Compressions; // bit set of PVSOCS_xxx
    BOOL Mono, Colors16, Colors256, Gray256, HiColor, TrueColor, TrueColorAlpha;
};

static const CWicOutFormat OutFormats[] = {
    //                                                     mono   16    256   gray   hi     tc     tc32
    {PVF_BMP, &GUID_ContainerFormatBmp, PVSOCS_NO_COMPRESSION,
     TRUE, TRUE, TRUE, TRUE, TRUE, TRUE, TRUE},
    {PVF_PNG, &GUID_ContainerFormatPng, PVSOCS_DEFLATE,
     TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE},
    {PVF_JPG, &GUID_ContainerFormatJpeg, PVSOCS_JPEG,
     FALSE, FALSE, FALSE, TRUE, FALSE, TRUE, FALSE},
    {PVF_GIF, &GUID_ContainerFormatGif, PVSOCS_LZW,
     TRUE, TRUE, TRUE, TRUE, FALSE, FALSE, FALSE},
    {PVF_TIFF, &GUID_ContainerFormatTiff,
     PVSOCS_NO_COMPRESSION | PVSOCS_LZW | PVSOCS_DEFLATE | PVSOCS_PACKBITS |
         PVSOCS_CCITT_3 | PVSOCS_CCITT_4 | PVSOCS_JPEG,
     TRUE, TRUE, TRUE, TRUE, FALSE, TRUE, TRUE},
};

static const CWicOutFormat* FindOutFormat(DWORD format)
{
    for (int i = 0; i < _countof(OutFormats); i++)
        if (OutFormats[i].Format == format)
            return &OutFormats[i];
    return NULL;
}

static DWORD CompressionToFlag(DWORD compression)
{
    switch (compression)
    {
    case PVCS_NO_COMPRESSION:
        return PVSOCS_NO_COMPRESSION;
    case PVCS_RLE:
        return PVSOCS_RLE;
    case PVCS_PACKBITS:
        return PVSOCS_PACKBITS;
    case PVCS_HUFFMAN:
        return PVSOCS_HUFFMAN;
    case PVCS_CCITT_3:
        return PVSOCS_CCITT_3;
    case PVCS_CCITT_4:
        return PVSOCS_CCITT_4;
    case PVCS_LZW:
        return PVSOCS_LZW;
    case PVCS_JPEG_HUFFMAN:
        return PVSOCS_JPEG;
    case PVCS_DEFLATE:
        return PVSOCS_DEFLATE;
    case PVCS_ASCII:
        return PVSOCS_ASCII;
    }
    return 0;
}

DWORD WINAPI WicIsOutCombSupported(int Fmt, int Compr, int Colors, int ColorModel)
{
    const CWicOutFormat* out = FindOutFormat((DWORD)Fmt);
    if (out == NULL)
        return (DWORD)-1;

    BOOL depthOk;
    switch (Colors)
    {
    case 2:
        depthOk = out->Mono;
        break;
    case 16:
        depthOk = out->Colors16;
        break;
    case 256:
        depthOk = (ColorModel == PVCM_GRAYS) ? out->Gray256 : out->Colors256;
        break;
    case PV_COLOR_HC15:
    case PV_COLOR_HC16:
        depthOk = out->HiColor;
        break;
    case PV_COLOR_TC24:
        depthOk = out->TrueColor;
        break;
    case PV_COLOR_TC32:
        depthOk = out->TrueColorAlpha;
        break;
    default:
        depthOk = FALSE;
        break;
    }
    if (!depthOk)
        return (DWORD)-1;

    if (Compr != PVCS_DEFAULT)
    {
        DWORD flag = CompressionToFlag((DWORD)Compr);
        if (flag == 0 || (out->Compressions & flag) == 0)
            return (DWORD)-1;
        // CCITT and JPEG inside TIFF only make sense at their native depth.
        if ((flag & (PVSOCS_CCITT_3 | PVSOCS_CCITT_4)) != 0 && Colors != 2)
            return (DWORD)-1;
        if (flag == PVSOCS_JPEG && Colors != PV_COLOR_TC24 && !(Colors == 256 && ColorModel == PVCM_GRAYS))
            return (DWORD)-1;
    }
    return 0;
}

//****************************************************************************
//
// Working image
//
// The decoded surface is expanded to 32bpp BGRA once and then pushed through
// the WIC transform chain. Alpha was already composited over the viewer
// background when the frame was decoded, matching what the original engine did
// for PVSetBkHandle, so the alpha byte here is always opaque.
//

static IWICBitmap* CreateWorkingBitmap(IWICImagingFactory* factory, const CWicSurface& surface)
{
    size_t stride = (size_t)surface.Width * 4;
    size_t total = stride * surface.Height;
    BYTE* buffer = (BYTE*)malloc(total);
    if (buffer == NULL)
        return NULL;

    for (DWORD y = 0; y < surface.Height; y++)
    {
        const BYTE* src = surface.Bits + (size_t)y * surface.Stride;
        BYTE* dst = buffer + (size_t)y * stride;
        for (DWORD x = 0; x < surface.Width; x++)
        {
            RGBQUAD color;
            switch (surface.Bpp)
            {
            case 1:
                color = surface.Palette[(src[x >> 3] & (0x80 >> (x & 7))) ? 1 : 0];
                break;
            case 4:
                color = surface.Palette[(x & 1) ? (src[x >> 1] & 0x0F) : (src[x >> 1] >> 4)];
                break;
            case 8:
                color = surface.Palette[src[x]];
                break;
            default:
                color.rgbBlue = src[x * 3];
                color.rgbGreen = src[x * 3 + 1];
                color.rgbRed = src[x * 3 + 2];
                break;
            }
            dst[x * 4] = color.rgbBlue;
            dst[x * 4 + 1] = color.rgbGreen;
            dst[x * 4 + 2] = color.rgbRed;
            dst[x * 4 + 3] = 255;
        }
    }

    IWICBitmap* bitmap = NULL;
    HRESULT hr = factory->CreateBitmapFromMemory(surface.Width, surface.Height,
                                                 GUID_WICPixelFormat32bppBGRA,
                                                 (UINT)stride, (UINT)total, buffer, &bitmap);
    free(buffer);
    return SUCCEEDED(hr) ? bitmap : NULL;
}

// Applies crop, mirroring/rotation and scaling in the order the plug-in's
// callers assume: crop rectangles arrive in original-image coordinates, and
// PVSaveImageInfo.Width/Height describe the final output size.
static IWICBitmapSource* BuildTransformChain(IWICImagingFactory* factory, IWICBitmapSource* source,
                                             LPPVSaveImageInfo sii, IUnknown** keepAlive, int keepAliveMax,
                                             int* keepAliveCount)
{
    IWICBitmapSource* current = source;

    if (sii->CropWidth != 0 && sii->CropHeight != 0)
    {
        IWICBitmapClipper* clipper = NULL;
        if (SUCCEEDED(factory->CreateBitmapClipper(&clipper)))
        {
            WICRect rect;
            rect.X = (INT)sii->CropLeft;
            rect.Y = (INT)sii->CropTop;
            rect.Width = (INT)sii->CropWidth;
            rect.Height = (INT)sii->CropHeight;
            if (SUCCEEDED(clipper->Initialize(current, &rect)))
            {
                if (*keepAliveCount < keepAliveMax)
                    keepAlive[(*keepAliveCount)++] = clipper;
                current = clipper;
            }
            else
                clipper->Release();
        }
    }

    WICBitmapTransformOptions transform = WICBitmapTransformRotate0;
    if (sii->Flags & PVSF_ROTATE90)
        transform = WICBitmapTransformRotate90;
    if (sii->Flags & PVSF_FLIP_HOR)
        transform = (WICBitmapTransformOptions)(transform | WICBitmapTransformFlipHorizontal);
    if (sii->Flags & PVSF_FLIP_VERT)
        transform = (WICBitmapTransformOptions)(transform | WICBitmapTransformFlipVertical);
    if (transform != WICBitmapTransformRotate0)
    {
        IWICBitmapFlipRotator* rotator = NULL;
        if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotator)))
        {
            if (SUCCEEDED(rotator->Initialize(current, transform)))
            {
                if (*keepAliveCount < keepAliveMax)
                    keepAlive[(*keepAliveCount)++] = rotator;
                current = rotator;
            }
            else
                rotator->Release();
        }
    }

    if (sii->Width != 0 && sii->Height != 0)
    {
        UINT width = 0, height = 0;
        current->GetSize(&width, &height);
        if (width != sii->Width || height != sii->Height)
        {
            IWICBitmapScaler* scaler = NULL;
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)))
            {
                // A heavily downscaled thumbnail asks for speed over ringing.
                WICBitmapInterpolationMode mode = (sii->Flags & PVSF_SUPERFAST)
                                                      ? WICBitmapInterpolationModeLinear
                                                      : WICBitmapInterpolationModeFant;
                if (SUCCEEDED(scaler->Initialize(current, sii->Width, sii->Height, mode)))
                {
                    if (*keepAliveCount < keepAliveMax)
                        keepAlive[(*keepAliveCount)++] = scaler;
                    current = scaler;
                }
                else
                    scaler->Release();
            }
        }
    }

    return current;
}

//****************************************************************************
//
// Raw output
//
// PVF_RAW is the plug-in's private channel to the thumbnail maker and the print
// preview: uncompressed BGRA rows handed to the caller's write callback in
// top-to-bottom order.
//

static PVCODE WriteRawRows(IWICBitmapSource* source, LPPVSaveImageInfo sii,
                           TProgressProc progress, void* progressArg)
{
    if ((sii->Flags & PVSF_USERDEFINED_OUTPUT) == 0 || sii->WriteFunc == NULL)
        return PVC_UNSUP_OUT_PARAMS;

    UINT width = 0, height = 0;
    source->GetSize(&width, &height);
    if (width == 0 || height == 0)
        return PVC_INVALID_DIMENSIONS;

    const UINT stride = width * 4;
    BYTE* row = (BYTE*)malloc(stride);
    if (row == NULL)
        return PVC_OOM;

    if (sii->SeekFunc != NULL)
        sii->SeekFunc(progressArg, 0, FILE_BEGIN);

    PVCODE result = PVC_OK;
    for (UINT y = 0; y < height; y++)
    {
        WICRect rect;
        rect.X = 0;
        rect.Y = (INT)y;
        rect.Width = (INT)width;
        rect.Height = 1;
        if (FAILED(source->CopyPixels(&rect, stride, stride, row)))
        {
            result = PVC_READING_ERROR;
            break;
        }
        if (sii->WriteFunc(progressArg, row, stride) != stride)
        {
            result = PVC_WRITING_ERROR;
            break;
        }
        // The save-side progress contract is inverted: TRUE means "cancel".
        if (progress != NULL && ((y & 15) == 15) && progress((int)((y * 100) / height), progressArg))
        {
            result = PVC_CANCELED;
            break;
        }
    }

    free(row);
    return result;
}

//****************************************************************************
//
// Encoded output
//

static GUID TargetPixelFormat(DWORD colors, DWORD colorModel)
{
    switch (colors)
    {
    case 2:
        return GUID_WICPixelFormatBlackWhite;
    case 16:
        return GUID_WICPixelFormat4bppIndexed;
    case 256:
        return (colorModel == PVCM_GRAYS) ? GUID_WICPixelFormat8bppGray : GUID_WICPixelFormat8bppIndexed;
    case PV_COLOR_HC15:
        return GUID_WICPixelFormat16bppBGR555;
    case PV_COLOR_HC16:
        return GUID_WICPixelFormat16bppBGR565;
    case PV_COLOR_TC32:
        return GUID_WICPixelFormat32bppBGRA;
    }
    return GUID_WICPixelFormat24bppBGR;
}

static void ApplyEncoderOptions(IPropertyBag2* bag, LPPVSaveImageInfo sii)
{
    if (bag == NULL)
        return;

    PROPBAG2 option;
    VARIANT value;

    if (sii->Format == PVF_JPG)
    {
        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"ImageQuality";
        value.vt = VT_R4;
        value.fltVal = (sii->Misc.JPEG.Quality > 0 && sii->Misc.JPEG.Quality <= 100)
                           ? (float)sii->Misc.JPEG.Quality / 100.0f
                           : 0.9f;
        bag->Write(1, &option, &value);

        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"JpegYCrCbSubsampling";
        value.vt = VT_UI1;
        // The dialog stores 0 for 1:1:1 (no chroma subsampling) and 1 for 2x1.
        value.bVal = sii->Misc.JPEG.SubSampling ? (BYTE)WICJpegYCrCbSubsampling444
                                                : (BYTE)WICJpegYCrCbSubsampling420;
        bag->Write(1, &option, &value);
    }
    else if (sii->Format == PVF_TIFF)
    {
        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"TiffCompressionMethod";
        value.vt = VT_UI1;
        switch (sii->Compression)
        {
        case PVCS_NO_COMPRESSION:
            value.bVal = WICTiffCompressionNone;
            break;
        case PVCS_CCITT_3:
            value.bVal = WICTiffCompressionCCITT3;
            break;
        case PVCS_CCITT_4:
            value.bVal = WICTiffCompressionCCITT4;
            break;
        case PVCS_LZW:
            value.bVal = sii->Flags & PVSF_PREDICT ? WICTiffCompressionLZWHDifferencing
                                                   : WICTiffCompressionLZW;
            break;
        case PVCS_PACKBITS:
        case PVCS_RLE:
            value.bVal = WICTiffCompressionRLE;
            break;
        case PVCS_DEFLATE:
            value.bVal = WICTiffCompressionZIP;
            break;
        default:
            value.bVal = WICTiffCompressionDontCare;
            break;
        }
        bag->Write(1, &option, &value);

        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"CompressionQuality";
        value.vt = VT_R4;
        value.fltVal = (sii->Misc.TIFF.JPEGQuality > 0 && sii->Misc.TIFF.JPEGQuality <= 100)
                           ? (float)sii->Misc.TIFF.JPEGQuality / 100.0f
                           : 0.9f;
        bag->Write(1, &option, &value);
    }
    else if (sii->Format == PVF_PNG)
    {
        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"InterlaceOption";
        value.vt = VT_BOOL;
        value.boolVal = (sii->Flags & PVSF_INTERLACE) ? VARIANT_TRUE : VARIANT_FALSE;
        bag->Write(1, &option, &value);
    }
    else if (sii->Format == PVF_BMP && sii->Colors == PV_COLOR_TC32)
    {
        memset(&option, 0, sizeof(option));
        VariantInit(&value);
        option.pstrName = (LPOLESTR)L"EnableV5Header32bppBGRA";
        value.vt = VT_BOOL;
        value.boolVal = VARIANT_TRUE;
        bag->Write(1, &option, &value);
    }
}

static void WriteComment(IWICBitmapFrameEncode* frameEncode, LPPVSaveImageInfo sii)
{
    if (sii->Comment == NULL || sii->CommentSize == 0)
        return;

    IWICMetadataQueryWriter* writer = NULL;
    if (FAILED(frameEncode->GetMetadataQueryWriter(&writer)) || writer == NULL)
        return;

    int wideLength = MultiByteToWideChar(CP_ACP, 0, sii->Comment, -1, NULL, 0);
    WCHAR* wide = (wideLength > 0) ? (WCHAR*)malloc(wideLength * sizeof(WCHAR)) : NULL;
    if (wide != NULL && MultiByteToWideChar(CP_ACP, 0, sii->Comment, -1, wide, wideLength) != 0)
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_LPWSTR;
        value.pwszVal = wide;
        switch (sii->Format)
        {
        case PVF_PNG:
            writer->SetMetadataByName(L"/tEXt/{str=Comment}", &value);
            break;
        case PVF_JPG:
            writer->SetMetadataByName(L"/app1/ifd/{ushort=270}", &value);
            break;
        case PVF_TIFF:
            writer->SetMetadataByName(L"/ifd/{ushort=270}", &value);
            break;
        case PVF_GIF:
            writer->SetMetadataByName(L"/commentext/TextEntry", &value);
            break;
        }
        // PropVariantClear would free 'wide' as well; do it once, here.
        value.pwszVal = NULL;
        PropVariantClear(&value);
    }
    free(wide);
    writer->Release();
}

static PVCODE EncodeToStream(IWICImagingFactory* factory, IWICBitmapSource* source,
                             const CWicOutFormat* out, LPPVSaveImageInfo sii, IStream* stream,
                             TProgressProc progress, void* progressArg)
{
    IWICBitmapEncoder* encoder = NULL;
    IWICBitmapFrameEncode* frameEncode = NULL;
    IPropertyBag2* options = NULL;
    IWICFormatConverter* converter = NULL;
    IWICPalette* palette = NULL;
    PVCODE result = PVC_WRITING_ERROR;

    do
    {
        if (FAILED(factory->CreateEncoder(*out->Container, NULL, &encoder)) ||
            FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache)))
            break;
        if (FAILED(encoder->CreateNewFrame(&frameEncode, &options)))
            break;
        ApplyEncoderOptions(options, sii);
        if (FAILED(frameEncode->Initialize(options)))
            break;

        UINT width = 0, height = 0;
        source->GetSize(&width, &height);
        if (FAILED(frameEncode->SetSize(width, height)))
            break;

        if (sii->HorDPI != 0 && sii->VerDPI != 0)
            frameEncode->SetResolution((double)sii->HorDPI, (double)sii->VerDPI);

        GUID target = TargetPixelFormat(sii->Colors, sii->ColorModel);
        IWICBitmapSource* encodeSource = source;

        if (FAILED(factory->CreateFormatConverter(&converter)))
            break;
        BOOL canConvert = FALSE;
        converter->CanConvert(GUID_WICPixelFormat32bppBGRA, target, &canConvert);
        if (canConvert)
        {
            // Reducing to an indexed surface needs a palette derived from the
            // actual pixels; error diffusion keeps gradients from banding.
            WICBitmapDitherType dither = WICBitmapDitherTypeNone;
            WICBitmapPaletteType paletteType = WICBitmapPaletteTypeCustom;
            if (target == GUID_WICPixelFormat4bppIndexed || target == GUID_WICPixelFormat8bppIndexed)
            {
                if (SUCCEEDED(factory->CreatePalette(&palette)) &&
                    SUCCEEDED(palette->InitializeFromBitmap(source,
                                                            (target == GUID_WICPixelFormat4bppIndexed) ? 16 : 256,
                                                            FALSE)))
                    dither = WICBitmapDitherTypeErrorDiffusion;
                else
                    SafeRelease(palette);
            }
            else if (target == GUID_WICPixelFormatBlackWhite)
            {
                dither = WICBitmapDitherTypeErrorDiffusion;
                paletteType = WICBitmapPaletteTypeFixedBW;
            }

            if (SUCCEEDED(converter->Initialize(source, target, dither, palette, 50.0, paletteType)))
                encodeSource = converter;
        }

        if (FAILED(frameEncode->SetPixelFormat(&target)))
            break;
        WriteComment(frameEncode, sii);

        if (progress != NULL && progress(10, progressArg))
        {
            result = PVC_CANCELED;
            break;
        }

        if (FAILED(frameEncode->WriteSource(encodeSource, NULL)))
            break;
        if (FAILED(frameEncode->Commit()) || FAILED(encoder->Commit()))
            break;

        if (progress != NULL)
            progress(100, progressArg);
        result = PVC_OK;
    } while (0);

    SafeRelease(palette);
    SafeRelease(converter);
    SafeRelease(options);
    SafeRelease(frameEncode);
    SafeRelease(encoder);
    return result;
}

static PVCODE EncodeToFile(IWICImagingFactory* factory, IWICBitmapSource* source,
                           const CWicOutFormat* out, LPPVSaveImageInfo sii, LPCWSTR fileName,
                           TProgressProc progress, void* progressArg)
{
    IStream* stream = NULL;
    if (FAILED(SHCreateStreamOnFileEx(fileName, STGM_WRITE | STGM_CREATE | STGM_SHARE_EXCLUSIVE,
                                      FILE_ATTRIBUTE_NORMAL, TRUE, NULL, &stream)))
        return PVC_ERROR_CREATING_FILE;

    PVCODE result = EncodeToStream(factory, source, out, sii, stream, progress, progressArg);
    stream->Release();
    if (result != PVC_OK)
        DeleteFileW(fileName);
    return result;
}

// The "update EXIF thumbnails" command encodes a JPEG straight into the
// caller's memory buffer. Encoders seek freely while they write, so the output
// is staged in an HGLOBAL stream and handed to the write callback in one pass
// once it is complete.
static PVCODE EncodeToCallback(IWICImagingFactory* factory, IWICBitmapSource* source,
                               const CWicOutFormat* out, LPPVSaveImageInfo sii,
                               TProgressProc progress, void* progressArg)
{
    if (sii->WriteFunc == NULL)
        return PVC_UNSUP_OUT_PARAMS;

    IStream* stream = NULL;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream)))
        return PVC_OOM;

    PVCODE result = EncodeToStream(factory, source, out, sii, stream, progress, progressArg);
    if (result == PVC_OK)
    {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        stream->Seek(zero, STREAM_SEEK_SET, NULL);

        if (sii->SeekFunc != NULL)
            sii->SeekFunc(progressArg, 0, FILE_BEGIN);

        BYTE buffer[16 * 1024];
        for (;;)
        {
            ULONG read = 0;
            if (FAILED(stream->Read(buffer, sizeof(buffer), &read)) || read == 0)
                break;
            if (sii->WriteFunc(progressArg, buffer, read) != read)
            {
                result = PVC_WRITING_ERROR;
                break;
            }
        }
    }
    stream->Release();
    return result;
}

//****************************************************************************
//
// PVSaveImage
//

PVCODE WINAPI WicSaveImage(LPPVHandle Img, const char* OutFName, LPPVSaveImageInfo pSii,
                           TProgressProc Progress, void* AppSpecific, int ImageIndex)
{
    if (pSii == NULL)
        return PVC_INCORRECT_PARAMETER;
    if (pSii->Flags & PVSF_APPEND_PAGE)
        return PVC_UNSUP_OUT_PARAMS; // multi-page append has no WIC equivalent

    PVCODE result = WicEnsureFrameDecoded(Img, ImageIndex, NULL, NULL);
    if (result != PVC_OK)
        return result;

    CWicSurface surface;
    if (!WicGetSurface(Img, &surface))
        return PVC_INVALID_HANDLE;

    const BOOL raw = (pSii->Format == PVF_RAW);
    const CWicOutFormat* out = raw ? NULL : FindOutFormat(pSii->Format);
    if (!raw && out == NULL)
        return PVC_UNSUP_OUT_PARAMS;
    if (!raw && WicIsOutCombSupported(pSii->Format, pSii->Compression, pSii->Colors, pSii->ColorModel) == (DWORD)-1)
        return PVC_UNSUP_OUT_PARAMS;

    CComApartmentSave com;
    IWICImagingFactory* factory = NULL;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return PVC_EXCEPTION;

    IWICBitmap* working = CreateWorkingBitmap(factory, surface);
    if (working == NULL)
    {
        factory->Release();
        return PVC_OOM;
    }

    IUnknown* keepAlive[4] = {NULL, NULL, NULL, NULL};
    int keepAliveCount = 0;
    IWICBitmapSource* chain = BuildTransformChain(factory, working, pSii, keepAlive,
                                                  _countof(keepAlive), &keepAliveCount);

    if (raw)
    {
        result = WriteRawRows(chain, pSii, Progress, AppSpecific);
    }
    else if (pSii->Flags & PVSF_USERDEFINED_OUTPUT)
    {
        result = EncodeToCallback(factory, chain, out, pSii, Progress, AppSpecific);
    }
    else if (OutFName == NULL)
    {
        result = PVC_UNSUP_OUT_PARAMS;
    }
    else
    {
        WCHAR* wideName = Utf8AllocWide(OutFName);
        if (wideName == NULL)
            result = PVC_ERROR_CREATING_FILE;
        else
        {
            result = EncodeToFile(factory, chain, out, pSii, wideName, Progress, AppSpecific);
            free(wideName);
        }
    }

    for (int i = keepAliveCount - 1; i >= 0; i--)
        keepAlive[i]->Release();
    working->Release();
    factory->Release();
    return result;
}
