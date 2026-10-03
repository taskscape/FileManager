// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Fixtures.h"

#include <objbase.h>
#include <wincodec.h>
#include <winioctl.h>
#include <algorithm>
#include <deque>
#include <stdio.h>

#include "file_system.h"
#include "text_util.h"

using namespace handoff;

namespace fixtures
{

namespace
{
std::wstring RootPath;
int Counter = 0;

bool RemoveTree(const std::wstring& path)
{
    // Junctions are removed as links and never followed.
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(LongPath(PathJoin(path, L"*")).c_str(), &data);
    bool ok = true;
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            std::wstring child = PathJoin(path, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    ok = RemoveDirectoryW(LongPath(child).c_str()) && ok;
                else
                    ok = RemoveTree(child) && ok;
            }
            else
            {
                SetFileAttributesW(LongPath(child).c_str(), FILE_ATTRIBUTE_NORMAL);
                ok = DeleteFileW(LongPath(child).c_str()) && ok;
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    SetFileAttributesW(LongPath(path).c_str(), FILE_ATTRIBUTE_NORMAL);
    return RemoveDirectoryW(LongPath(path).c_str()) && ok;
}

template <class T> struct Com
{
    T* P = nullptr;
    ~Com()
    {
        if (P)
            P->Release();
    }
    T** operator&() { return &P; }
    T* operator->() { return P; }
};
} // namespace

bool CreateRoot()
{
    wchar_t temp[MAX_PATH + 1];
    DWORD length = GetTempPathW(MAX_PATH + 1, temp);
    if (length == 0)
        return false;
    GUID guid;
    CoCreateGuid(&guid);
    wchar_t text[64];
    StringFromGUID2(guid, text, 64);
    std::wstring id = text;
    id = id.substr(1, id.size() - 2);
    RootPath = FullPathOf(std::wstring(temp) + L"HandoffEngineTests-" + id);
    return CreateDirectoryW(LongPath(RootPath).c_str(), NULL) != FALSE;
}

bool RemoveRoot()
{
    return RootPath.empty() || RemoveTree(RootPath);
}

const std::wstring& Root()
{
    return RootPath;
}

std::wstring NewDir(const wchar_t* label)
{
    std::wstring path = PathJoin(RootPath, std::wstring(label) + L"-" + NumberText(++Counter));
    CreateDirectoryW(LongPath(path).c_str(), NULL);
    return path;
}

static void EnsureParent(const std::wstring& path)
{
    std::wstring parent = ParentOf(path);
    if (parent.empty() || GetFileAttributesW(LongPath(parent).c_str()) != INVALID_FILE_ATTRIBUTES)
        return;
    EnsureParent(parent);
    CreateDirectoryW(LongPath(parent).c_str(), NULL);
}

bool WriteBytes(const std::wstring& path, const std::string& bytes)
{
    EnsureParent(path);
    HANDLE file = CreateFileW(LongPath(path).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, NULL);
    CloseHandle(file);
    return ok && written == bytes.size();
}

std::string ReadBytes(const std::wstring& path)
{
    HANDLE file = CreateFileW(LongPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return std::string();
    LARGE_INTEGER size = {};
    GetFileSizeEx(file, &size);
    std::string bytes((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    if (!bytes.empty())
        ReadFile(file, &bytes[0], (DWORD)bytes.size(), &read, NULL);
    CloseHandle(file);
    bytes.resize(read);
    return bytes;
}

bool Exists(const std::wstring& path)
{
    return GetFileAttributesW(LongPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring FileSha(const std::wstring& path)
{
    std::string bytes = ReadBytes(path);
    return Sha256Hex(bytes.data(), bytes.size());
}

static std::vector<std::wstring> List(const std::wstring& root, bool folders)
{
    std::vector<std::wstring> out;
    std::deque<std::wstring> pending{std::wstring()};
    while (!pending.empty())
    {
        std::wstring rel = pending.front();
        pending.pop_front();
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(LongPath(PathJoin(rel.empty() ? root : PathJoin(root, rel), L"*")).c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;
            std::wstring child = RelJoin(rel, name);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (folders)
                    out.push_back(child + L"/");
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    pending.push_back(child);
            }
            else
                out.push_back(child);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::wstring> ListFiles(const std::wstring& root) { return List(root, false); }
std::vector<std::wstring> ListEntries(const std::wstring& root) { return List(root, true); }

std::string MakePdf(const std::vector<std::pair<double, double>>& pages, bool encrypted)
{
    // A minimal but well-formed PDF with exact cross-reference offsets, so
    // Windows.Data.Pdf opens it like any producer's file.
    std::vector<std::string> objects;
    objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    std::string kids;
    for (size_t i = 0; i < pages.size(); i++)
        kids += std::to_string(3 + i) + " 0 R ";
    objects.push_back("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(pages.size()) + " >>");
    for (const auto& page : pages)
    {
        char box[128];
        _snprintf_s(box, _TRUNCATE, "[0 0 %.3f %.3f]", page.first, page.second);
        objects.push_back(std::string("<< /Type /Page /Parent 2 0 R /Resources << >> /MediaBox ") + box + " >>");
    }
    if (encrypted)
        objects.push_back("<< /Filter /Standard /V 1 /R 2 /O (0123456789abcdef0123456789abcdef) /U (0123456789abcdef0123456789abcdef) /P -4 >>");
    std::string pdf = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objects.size(); i++)
    {
        offsets.push_back(pdf.size());
        pdf += std::to_string(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    size_t xref = pdf.size();
    pdf += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t offset : offsets)
    {
        char line[32];
        _snprintf_s(line, _TRUNCATE, "%010zu 00000 n \n", offset);
        pdf += line;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R";
    if (encrypted)
        pdf += " /Encrypt " + std::to_string(objects.size()) + " 0 R /ID [<00112233445566778899aabbccddeeff> <00112233445566778899aabbccddeeff>]";
    pdf += " >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return pdf;
}

bool MakeImage(const std::wstring& path, const GUID& container, UINT width, UINT height, double dpi, const GUID& pixelFormat,
               bool gps)
{
    EnsureParent(path);
    Com<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return false;
    Com<IWICStream> stream;
    Com<IWICBitmapEncoder> encoder;
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> properties;
    if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(container, NULL, &encoder)) || FAILED(encoder->Initialize(stream.P, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, &properties)) || FAILED(frame->Initialize(properties.P)) ||
        FAILED(frame->SetSize(width, height)) || FAILED(frame->SetResolution(dpi, dpi)))
        return false;
    WICPixelFormatGUID format = pixelFormat;
    if (FAILED(frame->SetPixelFormat(&format)))
        return false;
    if (gps)
    {
        Com<IWICMetadataQueryWriter> writer;
        if (FAILED(frame->GetMetadataQueryWriter(&writer)))
            return false;
        // GPSLatitude: three rationals (degrees, minutes, seconds).
        ULONGLONG values[3] = {52ULL | (1ULL << 32), 13ULL | (1ULL << 32), 0ULL | (1ULL << 32)};
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_VECTOR | VT_UI8;
        value.cauh.cElems = 3;
        value.cauh.pElems = (ULARGE_INTEGER*)values;
        HRESULT hr = writer->SetMetadataByName(container == GUID_ContainerFormatTiff ? L"/ifd/gps/{ushort=2}" : L"/app1/ifd/gps/{ushort=2}", &value);
        if (FAILED(hr))
            return false;
    }
    Com<IWICComponentInfo> info;
    Com<IWICPixelFormatInfo> formatInfo;
    UINT bits = 24;
    if (SUCCEEDED(factory->CreateComponentInfo(format, &info)) &&
        SUCCEEDED(info->QueryInterface(IID_PPV_ARGS(&formatInfo))))
        formatInfo->GetBitsPerPixel(&bits);
    UINT stride = (width * bits + 7) / 8;
    std::vector<BYTE> pixels((size_t)stride * height, 0x80);
    return SUCCEEDED(frame->WritePixels(height, stride, (UINT)pixels.size(), pixels.data())) && SUCCEEDED(frame->Commit()) &&
           SUCCEEDED(encoder->Commit());
}

static void Put32(std::string& out, unsigned value)
{
    out += (char)(value >> 24), out += (char)(value >> 16), out += (char)(value >> 8), out += (char)value;
}

static void Put16(std::string& out, unsigned value)
{
    out += (char)(value >> 8), out += (char)value;
}

std::string MakePsd(unsigned width, unsigned height, unsigned short mode, bool withResolution, double dpi)
{
    std::string psd = "8BPS";
    Put16(psd, 1);
    psd.append(6, '\0');
    Put16(psd, mode == 4 ? 4 : 3);
    Put32(psd, height);
    Put32(psd, width);
    Put16(psd, 8);
    Put16(psd, mode);
    Put32(psd, 0); // colour mode data
    std::string resources;
    if (withResolution)
    {
        resources += "8BIM";
        Put16(resources, 0x03ED);
        Put16(resources, 0); // empty Pascal name, padded to even
        Put32(resources, 16);
        unsigned fixed = (unsigned)(dpi * 65536.0);
        Put32(resources, fixed);
        Put16(resources, 1);
        Put16(resources, 1);
        Put32(resources, fixed);
        Put16(resources, 1);
        Put16(resources, 1);
    }
    Put32(psd, (unsigned)resources.size());
    psd += resources;
    Put32(psd, 0); // layer and mask information
    Put16(psd, 0); // raw image data (truncated is fine for header inspection)
    return psd;
}

std::string MakeOtf()
{
    std::string otf = "OTTO";
    otf.append(252, '\x01');
    return otf;
}

bool MakeJunction(const std::wstring& link, const std::wstring& target)
{
    if (!CreateDirectoryW(LongPath(link).c_str(), NULL))
        return false;
    HANDLE handle = CreateFileW(LongPath(link).c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    std::wstring substitute = L"\\??\\" + FullPathOf(target);
    std::wstring print = FullPathOf(target);
    size_t pathBytes = (substitute.size() + 1 + print.size() + 1) * sizeof(WCHAR);
    std::vector<BYTE> buffer(16 + pathBytes, 0);
    DWORD* tag = (DWORD*)buffer.data();
    *tag = IO_REPARSE_TAG_MOUNT_POINT;
    WORD* words = (WORD*)(buffer.data() + 4);
    words[0] = (WORD)(8 + pathBytes);                   // ReparseDataLength
    words[2] = 0;                                        // SubstituteNameOffset
    words[3] = (WORD)(substitute.size() * sizeof(WCHAR)); // SubstituteNameLength
    words[4] = (WORD)((substitute.size() + 1) * sizeof(WCHAR));
    words[5] = (WORD)(print.size() * sizeof(WCHAR));
    memcpy(buffer.data() + 16, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
    memcpy(buffer.data() + 16 + (substitute.size() + 1) * sizeof(WCHAR), print.c_str(), (print.size() + 1) * sizeof(WCHAR));
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(), (DWORD)buffer.size(), NULL, 0, &returned, NULL);
    CloseHandle(handle);
    return ok != FALSE;
}

std::wstring RepositoryRoot()
{
    wchar_t module[1024];
    GetModuleFileNameW(NULL, module, 1024);
    std::wstring dir = ParentOf(module);
    while (!dir.empty())
    {
        if (Exists(PathJoin(dir, L"src\\plugins\\handoff\\templates")))
            return dir;
        std::wstring parent = ParentOf(dir);
        if (parent == dir)
            break;
        dir = parent;
    }
    return std::wstring();
}

std::string ReadTemplate(const wchar_t* fileName)
{
    return ReadBytes(PathJoin(RepositoryRoot(), std::wstring(L"src\\plugins\\handoff\\templates\\") + fileName));
}

SpecLoadResult ParseSpecText(const std::string& json)
{
    SpecLoadResult result = ParseSpecification((const unsigned char*)json.data(), json.size());
    result.Model.Sha256 = Sha256Hex(json.data(), json.size());
    return result;
}

void BuildQuickStartTree(const std::wstring& w)
{
    WriteBytes(PathJoin(w, L".handoff\\client-delivery.handoff.json"), ReadTemplate(L"client-delivery-example.handoff.json"));
    std::vector<std::pair<double, double>> a4 = {{A4W, A4H}, {A4W, A4H}};
    WriteBytes(PathJoin(w, L"Approved\\Brand-Guidelines_v3.pdf"), MakePdf(a4));
    WriteBytes(PathJoin(w, L"Approved\\Brand-Guidelines_v4.pdf"), MakePdf({{A4W, A4H}, {A4W, A4H}, {A4W, A4H}}));
    WriteBytes(PathJoin(w, L"Approved\\Stationery_v2.pdf"), MakePdf({{A4W, A4H}}));
    // Distinct page layouts keep every delivered file's content unique (no HO-CONT-006 noise).
    WriteBytes(PathJoin(w, L"Artwork\\Final\\Logo\\ACME-logo-primary.ai"), MakePdf({{A3W, A3H}}));
    WriteBytes(PathJoin(w, L"Artwork\\Final\\Logo\\ACME-logo-mono.svg"),
               "<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>\n");
    WriteBytes(PathJoin(w, L"Artwork\\Final\\Campaign\\Hero.psd"), MakePsd(2400, 1600, 3, true, 300));
    WriteBytes(PathJoin(w, L"Artwork\\WIP\\Hero-explorations.psd"), MakePsd(800, 600, 3, false, 0));
    WriteBytes(PathJoin(w, L"Assets\\Fonts\\Inter-Regular.otf"), MakeOtf());
    WriteBytes(PathJoin(w, L"Assets\\licences.csv"),
               "File,Licence,Licensor,Expires,Scope\r\nInter-Regular.otf,OFL-1.1,The Inter Project Authors,,Unlimited\r\n");
    MakeImage(PathJoin(w, L"Assets\\Stock\\city-skyline.jpg"), GUID_ContainerFormatJpeg, 3200, 2000, 300,
              GUID_WICPixelFormat24bppBGR);
    WriteBytes(PathJoin(w, L"Assets\\Stock\\city-skyline.licence.pdf"), MakePdf({{A3H, A3W}}));
    WriteBytes(PathJoin(w, L"Thumbs.db"), std::string(64, '\x07'));
}

} // namespace fixtures
