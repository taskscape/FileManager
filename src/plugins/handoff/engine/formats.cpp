// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "formats.h"
#include "text_util.h"

#include <ctype.h>
#include <string.h>

namespace handoff
{

namespace
{
FormatDef Def(const wchar_t* id, const wchar_t* label, std::vector<std::wstring> extensions, bool raster = false)
{
    FormatDef def;
    def.Id = id;
    def.Label = label;
    def.Extensions = std::move(extensions);
    def.Raster = raster;
    return def;
}
} // namespace

const std::vector<FormatDef>& BuiltInFormats()
{
    // C.4.7 built-in table; labels are technical type names shown in CONTENTS.txt.
    static const std::vector<FormatDef> formats = {
        Def(L"pdf", L"PDF", {L".pdf"}),
        Def(L"ai", L"Adobe Illustrator", {L".ai"}),
        Def(L"eps", L"EPS", {L".eps"}),
        Def(L"psd", L"PSD", {L".psd", L".psb"}, true),
        Def(L"svg", L"SVG", {L".svg"}),
        Def(L"indd", L"InDesign", {L".indd"}),
        Def(L"png", L"PNG", {L".png"}, true),
        Def(L"jpeg", L"JPEG", {L".jpg", L".jpeg", L".jpe"}, true),
        Def(L"tiff", L"TIFF", {L".tif", L".tiff"}, true),
        Def(L"gif", L"GIF", {L".gif"}, true),
        Def(L"bmp", L"BMP", {L".bmp"}, true),
        Def(L"webp", L"WebP", {L".webp"}, true),
        Def(L"heic", L"HEIC", {L".heic", L".heif"}, true),
        Def(L"avif", L"AVIF", {L".avif"}, true),
        Def(L"zip", L"ZIP", {L".zip"}),
        Def(L"docx", L"Word document", {L".docx"}),
        Def(L"xlsx", L"Excel workbook", {L".xlsx"}),
        Def(L"pptx", L"PowerPoint presentation", {L".pptx"}),
        Def(L"dwg", L"DWG", {L".dwg"}),
        Def(L"dxf", L"DXF", {L".dxf"}),
        Def(L"ifc", L"IFC", {L".ifc"}),
        Def(L"otf", L"OpenType font", {L".otf"}),
        Def(L"ttf", L"TrueType font", {L".ttf"}),
        Def(L"ttc", L"TrueType collection", {L".ttc"}),
        Def(L"woff", L"WOFF font", {L".woff"}),
        Def(L"woff2", L"WOFF2 font", {L".woff2"}),
        Def(L"mp4", L"MP4 video", {L".mp4", L".m4v"}),
        Def(L"mov", L"QuickTime video", {L".mov"}),
        Def(L"txt", L"Text", {L".txt"}),
        Def(L"csv", L"CSV", {L".csv"}),
        Def(L"md", L"Markdown", {L".md"}),
    };
    return formats;
}

bool IsBuiltInFormat(const std::wstring& id)
{
    for (const FormatDef& def : BuiltInFormats())
        if (def.Id == id)
            return true;
    return false;
}

bool IsRasterFormat(const std::wstring& id)
{
    for (const FormatDef& def : BuiltInFormats())
        if (def.Id == id)
            return def.Raster;
    return false;
}

const FormatDef* FindFormat(const std::vector<FormatDef>& custom, const std::wstring& id)
{
    for (const FormatDef& def : custom)
        if (def.Id == id)
            return &def;
    for (const FormatDef& def : BuiltInFormats())
        if (def.Id == id)
            return &def;
    return nullptr;
}

const FormatDef* FormatForExtension(const std::vector<FormatDef>& custom, const std::wstring& extension)
{
    std::wstring ext = LowerInvariant(extension);
    for (const FormatDef& def : custom)
        for (const std::wstring& candidate : def.Extensions)
            if (candidate == ext)
                return &def;
    for (const FormatDef& def : BuiltInFormats())
        for (const std::wstring& candidate : def.Extensions)
            if (candidate == ext)
                return &def;
    return nullptr;
}

std::wstring FormatLabel(const std::vector<FormatDef>& custom, const std::wstring& id)
{
    const FormatDef* def = FindFormat(custom, id);
    if (def == nullptr || def->Label.empty())
        return UpperInvariant(id);
    return def->Label;
}

static bool StartsWith(const unsigned char* head, size_t length, size_t offset, const char* text)
{
    size_t n = strlen(text);
    return length >= offset + n && memcmp(head + offset, text, n) == 0;
}

static bool StartsWithBytes(const unsigned char* head, size_t length, size_t offset,
                            std::initializer_list<unsigned char> bytes)
{
    if (length < offset + bytes.size())
        return false;
    size_t i = offset;
    for (unsigned char b : bytes)
        if (head[i++] != b)
            return false;
    return true;
}

static bool Contains(const unsigned char* head, size_t length, size_t limit, const char* text)
{
    size_t n = strlen(text);
    size_t end = length < limit ? length : limit;
    for (size_t i = 0; i + n <= end; i++)
        if (memcmp(head + i, text, n) == 0)
            return true;
    return false;
}

// ISO base media "ftyp" box brand at offset 8.
static bool FtypBrand(const unsigned char* head, size_t length, std::initializer_list<const char*> brands)
{
    if (!StartsWith(head, length, 4, "ftyp") || length < 12)
        return false;
    for (const char* brand : brands)
        if (memcmp(head + 8, brand, 4) == 0)
            return true;
    return false;
}

bool LooksLikeText(const unsigned char* head, size_t length)
{
    if (length >= 2 && ((head[0] == 0xFF && head[1] == 0xFE) || (head[0] == 0xFE && head[1] == 0xFF)))
        return true; // BOM-marked UTF-16
    for (size_t i = 0; i < length; i++)
        if (head[i] == 0)
            return false;
    // The head may end in the middle of a multi-byte sequence; ignore at most 3 trailing bytes.
    for (size_t trim = 0; trim <= 3 && trim <= length; trim++)
        if (IsValidUtf8(head, length - trim))
            return true;
    return false;
}

static bool SvgSignature(const unsigned char* head, size_t length)
{
    std::wstring text;
    if (length >= 2 && head[0] == 0xFF && head[1] == 0xFE)
        text.assign((const wchar_t*)(head + 2), (length - 2) / 2);
    else
    {
        size_t start = (length >= 3 && head[0] == 0xEF && head[1] == 0xBB && head[2] == 0xBF) ? 3 : 0;
        text = Utf8ToWideLossy(std::string((const char*)head + start, length - start));
    }
    // Skip the XML declaration, comments, and DOCTYPE before the first element.
    size_t pos = 0;
    for (int guard = 0; guard < 64; guard++)
    {
        pos = text.find(L'<', pos);
        if (pos == std::wstring::npos)
            return false;
        if (text.compare(pos, 5, L"<?xml") == 0 || text.compare(pos, 2, L"<?") == 0)
        {
            size_t end = text.find(L"?>", pos);
            if (end == std::wstring::npos)
                return false;
            pos = end + 2;
        }
        else if (text.compare(pos, 4, L"<!--") == 0)
        {
            size_t end = text.find(L"-->", pos);
            if (end == std::wstring::npos)
                return false;
            pos = end + 3;
        }
        else if (text.compare(pos, 2, L"<!") == 0)
        {
            size_t end = text.find(L'>', pos);
            if (end == std::wstring::npos)
                return false;
            pos = end + 1;
        }
        else
            return text.compare(pos, 4, L"<svg") == 0;
    }
    return false;
}

static bool BuiltInSignature(const std::wstring& id, const unsigned char* h, size_t n)
{
    if (id == L"pdf")
        return Contains(h, n, 1024, "%PDF-");
    if (id == L"ai")
        return StartsWith(h, n, 0, "%PDF-") || StartsWith(h, n, 0, "%!PS-Adobe");
    if (id == L"eps")
        return StartsWithBytes(h, n, 0, {0xC5, 0xD0, 0xD3, 0xC6}) ||
               (StartsWith(h, n, 0, "%!PS-Adobe-") && Contains(h, n, 64, "EPSF"));
    if (id == L"psd")
        return StartsWith(h, n, 0, "8BPS") && n >= 6 && h[4] == 0 && (h[5] == 1 || h[5] == 2);
    if (id == L"svg")
        return SvgSignature(h, n);
    if (id == L"indd")
        return StartsWithBytes(h, n, 0, {0x06, 0x06, 0xED, 0xF5, 0xD8, 0x1D, 0x46, 0xE5, 0xBD, 0x31, 0xEF, 0xE7,
                                         0xFE, 0x74, 0xB7, 0x1D});
    if (id == L"png")
        return StartsWithBytes(h, n, 0, {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});
    if (id == L"jpeg")
        return StartsWithBytes(h, n, 0, {0xFF, 0xD8, 0xFF});
    if (id == L"tiff")
        return StartsWithBytes(h, n, 0, {0x49, 0x49, 0x2A, 0x00}) || StartsWithBytes(h, n, 0, {0x4D, 0x4D, 0x00, 0x2A}) ||
               StartsWithBytes(h, n, 0, {0x49, 0x49, 0x2B, 0x00}) || StartsWithBytes(h, n, 0, {0x4D, 0x4D, 0x00, 0x2B});
    if (id == L"gif")
        return StartsWith(h, n, 0, "GIF87a") || StartsWith(h, n, 0, "GIF89a");
    if (id == L"bmp")
        return StartsWith(h, n, 0, "BM");
    if (id == L"webp")
        return StartsWith(h, n, 0, "RIFF") && StartsWith(h, n, 8, "WEBP");
    if (id == L"heic")
        return FtypBrand(h, n, {"heic", "heix", "mif1", "msf1"});
    if (id == L"avif")
        return FtypBrand(h, n, {"avif", "avis"});
    if (id == L"zip")
        return StartsWithBytes(h, n, 0, {'P', 'K', 3, 4}) || StartsWithBytes(h, n, 0, {'P', 'K', 5, 6});
    if (id == L"docx" || id == L"xlsx" || id == L"pptx")
        return StartsWithBytes(h, n, 0, {'P', 'K', 3, 4});
    if (id == L"dwg")
        return StartsWith(h, n, 0, "AC10") && n >= 6 && isdigit(h[4]) && isdigit(h[5]);
    if (id == L"dxf")
    {
        size_t i = 0;
        while (i < n && (h[i] == ' ' || h[i] == '\t' || h[i] == '\r' || h[i] == '\n'))
            i++;
        if (i >= n || h[i] != '0')
            return false;
        i++;
        while (i < n && (h[i] == ' ' || h[i] == '\t'))
            i++;
        if (i >= n || (h[i] != '\r' && h[i] != '\n'))
            return false;
        while (i < n && (h[i] == '\r' || h[i] == '\n' || h[i] == ' ' || h[i] == '\t'))
            i++;
        return StartsWith(h, n, i, "SECTION");
    }
    if (id == L"ifc")
        return StartsWith(h, n, 0, "ISO-10303-21;");
    if (id == L"otf")
        return StartsWith(h, n, 0, "OTTO");
    if (id == L"ttf")
        return StartsWithBytes(h, n, 0, {0x00, 0x01, 0x00, 0x00}) || StartsWith(h, n, 0, "true");
    if (id == L"ttc")
        return StartsWith(h, n, 0, "ttcf");
    if (id == L"woff")
        return StartsWith(h, n, 0, "wOFF");
    if (id == L"woff2")
        return StartsWith(h, n, 0, "wOF2");
    if (id == L"mov")
        return FtypBrand(h, n, {"qt  "});
    if (id == L"mp4")
        return StartsWith(h, n, 4, "ftyp") && !FtypBrand(h, n, {"heic", "heix", "mif1", "msf1", "avif", "avis", "qt  "});
    if (id == L"txt" || id == L"csv" || id == L"md")
        return LooksLikeText(h, n);
    return true;
}

FormatDetection DetectFormat(const std::wstring& fileName, const unsigned char* head, size_t headLength,
                             const std::vector<FormatDef>& custom)
{
    FormatDetection detection;
    const FormatDef* def = FormatForExtension(custom, ExtensionOf(fileName));
    if (def == nullptr)
    {
        detection.Id = L"unknown";
        return detection;
    }
    detection.Id = def->Id;
    detection.Known = true;
    if (headLength == 0)
        return detection; // empty files are reported once, as HO-CONT-004
    if (def->BuiltIn)
        detection.SignatureOk = BuiltInSignature(def->Id, head, headLength);
    else if (def->Text)
        detection.SignatureOk = LooksLikeText(head, headLength);
    else if (!def->Magic.empty())
    {
        detection.SignatureOk = false;
        for (const MagicBytes& magic : def->Magic)
        {
            if (headLength >= magic.Offset + magic.Bytes.size() &&
                memcmp(head + magic.Offset, magic.Bytes.data(), magic.Bytes.size()) == 0)
            {
                detection.SignatureOk = true;
                break;
            }
        }
    }
    return detection;
}

} // namespace handoff
