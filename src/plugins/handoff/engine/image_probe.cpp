// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "inspect.h"
#include "text_util.h"

#include <wincodec.h>
#include <propvarutil.h>

namespace handoff
{

namespace
{

template <class T> class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() { Reset(); }
    void Reset()
    {
        if (Ptr != nullptr)
            Ptr->Release();
        Ptr = nullptr;
    }
    T** Put()
    {
        Reset();
        return &Ptr;
    }
    T* operator->() const { return Ptr; }
    T* Get() const { return Ptr; }

private:
    T* Ptr = nullptr;
};

uint32_t BigEndian32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint16_t BigEndian16(const unsigned char* p) { return (uint16_t)((p[0] << 8) | p[1]); }

bool ReadExact(HANDLE file, void* buffer, DWORD length)
{
    DWORD read = 0;
    return ReadFile(file, buffer, length, &read, NULL) && read == length;
}

bool Skip(HANDLE file, int64_t bytes)
{
    LARGE_INTEGER distance;
    distance.QuadPart = bytes;
    return SetFilePointerEx(file, distance, NULL, FILE_CURRENT) != FALSE;
}

bool IsGuidIn(const GUID& guid, std::initializer_list<const GUID*> list)
{
    for (const GUID* candidate : list)
        if (IsEqualGUID(guid, *candidate))
            return true;
    return false;
}

std::wstring ColorModelOf(const GUID& format)
{
    if (IsGuidIn(format, {&GUID_WICPixelFormat32bppCMYK, &GUID_WICPixelFormat64bppCMYK, &GUID_WICPixelFormat40bppCMYKAlpha,
                          &GUID_WICPixelFormat80bppCMYKAlpha}))
        return L"cmyk";
    if (IsGuidIn(format, {&GUID_WICPixelFormatBlackWhite, &GUID_WICPixelFormat2bppGray, &GUID_WICPixelFormat4bppGray,
                          &GUID_WICPixelFormat8bppGray, &GUID_WICPixelFormat16bppGray, &GUID_WICPixelFormat16bppGrayFixedPoint,
                          &GUID_WICPixelFormat16bppGrayHalf, &GUID_WICPixelFormat32bppGrayFloat,
                          &GUID_WICPixelFormat32bppGrayFixedPoint}))
        return L"gray";
    if (IsGuidIn(format, {&GUID_WICPixelFormat1bppIndexed, &GUID_WICPixelFormat2bppIndexed, &GUID_WICPixelFormat4bppIndexed,
                          &GUID_WICPixelFormat8bppIndexed}))
        return L"indexed";
    return L"rgb";
}

bool HasMetadata(IWICMetadataQueryReader* reader, const wchar_t* query)
{
    PROPVARIANT value;
    PropVariantInit(&value);
    HRESULT hr = reader->GetMetadataByName(query, &value);
    bool present = SUCCEEDED(hr) && value.vt != VT_EMPTY;
    PropVariantClear(&value);
    return present;
}

class WicImageInspector : public IImageInspector
{
public:
    ImageFacts Inspect(const std::wstring& path, const std::wstring& formatId) override
    {
        if (formatId == L"psd")
        {
            HANDLE file = OpenForInspection(path);
            if (file == INVALID_HANDLE_VALUE)
            {
                ImageFacts facts;
                facts.Error = NumberText(GetLastError());
                return facts;
            }
            ImageFacts facts = InspectPsd(file);
            CloseHandle(file);
            return facts;
        }
        return InspectWithWic(path);
    }

private:
    ComPtr<IWICImagingFactory> Factory;

    ImageFacts InspectWithWic(const std::wstring& path)
    {
        ImageFacts facts;
        if (Factory.Get() == nullptr &&
            FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(Factory.Put()))))
        {
            facts.Error = L"WIC is unavailable";
            facts.DecoderMissing = true;
            return facts;
        }
        // Opening the handle ourselves keeps long paths working and shares with writers.
        HANDLE file = OpenForInspection(path);
        if (file == INVALID_HANDLE_VALUE)
        {
            facts.Error = NumberText(GetLastError());
            return facts;
        }
        ComPtr<IWICBitmapDecoder> decoder;
        HRESULT hr = Factory->CreateDecoderFromFileHandle((ULONG_PTR)file, NULL, WICDecodeMetadataCacheOnDemand,
                                                          decoder.Put());
        if (FAILED(hr))
        {
            CloseHandle(file);
            facts.DecoderMissing = hr == WINCODEC_ERR_COMPONENTNOTFOUND;
            facts.Error = HResultText(hr);
            return facts;
        }
        UINT frames = 0;
        decoder->GetFrameCount(&frames);
        facts.Frames = frames;
        ComPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, frame.Put());
        if (SUCCEEDED(hr))
            hr = frame->GetSize(&facts.Width, &facts.Height);
        if (FAILED(hr))
        {
            frame.Reset();
            decoder.Reset();
            CloseHandle(file);
            facts.Error = L"the image could not be decoded";
            return facts;
        }
        facts.Decoded = true;
        if (SUCCEEDED(frame->GetResolution(&facts.DpiX, &facts.DpiY)) && facts.DpiX > 0 && facts.DpiY > 0)
            facts.DpiKnown = true;
        WICPixelFormatGUID pixelFormat;
        if (SUCCEEDED(frame->GetPixelFormat(&pixelFormat)))
        {
            facts.ColorModel = ColorModelOf(pixelFormat);
            ComPtr<IWICComponentInfo> info;
            if (SUCCEEDED(Factory->CreateComponentInfo(pixelFormat, info.Put())))
            {
                ComPtr<IWICPixelFormatInfo2> formatInfo;
                if (SUCCEEDED(info->QueryInterface(IID_PPV_ARGS(formatInfo.Put()))))
                {
                    UINT channels = 0, bits = 0;
                    BOOL transparency = FALSE;
                    if (SUCCEEDED(formatInfo->GetChannelCount(&channels)) && SUCCEEDED(formatInfo->GetBitsPerPixel(&bits)) &&
                        channels > 0)
                        facts.BitsPerChannel = (int)(bits / channels);
                    if (SUCCEEDED(formatInfo->SupportsTransparency(&transparency)))
                        facts.Alpha = transparency ? 1 : 0;
                    if (facts.ColorModel == L"indexed")
                        facts.Alpha = -1; // palette transparency is not described by the pixel format
                }
            }
        }
        ComPtr<IWICMetadataQueryReader> metadata;
        if (SUCCEEDED(frame->GetMetadataQueryReader(metadata.Put())))
        {
            // GPS latitude in JPEG/TIFF EXIF; the photo metadata policy covers other containers.
            facts.Gps = HasMetadata(metadata.Get(), L"/app1/ifd/gps/{ushort=2}") ||
                        HasMetadata(metadata.Get(), L"/ifd/gps/{ushort=2}") ||
                        HasMetadata(metadata.Get(), L"System.GPS.Latitude");
        }
        metadata.Reset();
        frame.Reset();
        decoder.Reset();
        CloseHandle(file);
        return facts;
    }
};

} // namespace

ImageFacts InspectPsd(HANDLE file)
{
    ImageFacts facts;
    unsigned char header[26];
    if (!ReadExact(file, header, sizeof(header)) || memcmp(header, "8BPS", 4) != 0 ||
        (BigEndian16(header + 4) != 1 && BigEndian16(header + 4) != 2))
    {
        facts.Error = L"not a PSD/PSB header";
        return facts;
    }
    uint16_t channels = BigEndian16(header + 12);
    facts.Height = BigEndian32(header + 14);
    facts.Width = BigEndian32(header + 18);
    facts.BitsPerChannel = BigEndian16(header + 22);
    switch (BigEndian16(header + 24))
    {
    case 1: facts.ColorModel = L"gray"; break;
    case 2: facts.ColorModel = L"indexed"; break;
    case 3: facts.ColorModel = L"rgb"; break;
    case 4: facts.ColorModel = L"cmyk"; break;
    case 9: facts.ColorModel = L"lab"; break;
    default: facts.ColorModel = L""; break;
    }
    (void)channels; // PSD alpha channels are ambiguous; alpha stays unknown (C.5.4.5)
    facts.Decoded = true;

    // Colour mode data section, then the image resources section (bounded walk).
    unsigned char length[4];
    if (!ReadExact(file, length, 4) || !Skip(file, BigEndian32(length)) || !ReadExact(file, length, 4))
        return facts;
    uint32_t resourcesLength = BigEndian32(length);
    if (resourcesLength > 1024 * 1024)
        resourcesLength = 1024 * 1024;
    std::vector<unsigned char> resources(resourcesLength);
    if (resourcesLength == 0 || !ReadExact(file, resources.data(), resourcesLength))
        return facts;
    size_t pos = 0;
    while (pos + 12 <= resources.size())
    {
        if (memcmp(resources.data() + pos, "8BIM", 4) != 0)
            break;
        uint16_t id = BigEndian16(resources.data() + pos + 4);
        size_t nameLength = resources[pos + 6];
        size_t nameField = (1 + nameLength + 1) & ~(size_t)1; // Pascal string padded to even
        size_t sizeOffset = pos + 6 + nameField;
        if (sizeOffset + 4 > resources.size())
            break;
        uint32_t size = BigEndian32(resources.data() + sizeOffset);
        size_t data = sizeOffset + 4;
        if (data + size > resources.size())
            break;
        if (id == 0x03ED && size >= 16)
        {
            // ResolutionInfo: 16.16 fixed-point resolution; unit 2 means pixels per centimetre.
            double h = BigEndian32(resources.data() + data) / 65536.0;
            uint16_t hUnit = BigEndian16(resources.data() + data + 4);
            double v = BigEndian32(resources.data() + data + 8) / 65536.0;
            uint16_t vUnit = BigEndian16(resources.data() + data + 12);
            facts.DpiX = hUnit == 2 ? h * 2.54 : h;
            facts.DpiY = vUnit == 2 ? v * 2.54 : v;
            facts.DpiKnown = facts.DpiX > 0 && facts.DpiY > 0;
            break;
        }
        pos = data + ((size + 1) & ~(size_t)1);
    }
    return facts;
}

std::unique_ptr<IImageInspector> CreateWicImageInspector()
{
    return std::unique_ptr<IImageInspector>(new WicImageInspector());
}

} // namespace handoff
