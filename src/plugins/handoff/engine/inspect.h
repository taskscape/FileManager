// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Content inspection (C.5.4): format heads, PDF structure, PDF pages through
// Windows.Data.Pdf, and raster images through WIC or the PSD header. Every
// probe reads a bounded amount of data and never modifies the file.

#include <windows.h>
#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

namespace handoff
{

// Non-intrusive open for inspection: long-path aware and shared with writers,
// so reviewing never blocks the applications that own the working files.
HANDLE OpenForInspection(const std::wstring& path);
// Reads up to 'maximum' bytes from the start of the file.
bool ReadHead(const std::wstring& path, size_t maximum, std::vector<unsigned char>& head, DWORD& error);
// Reads a whole file of at most 'maximum' bytes (larger files fail with ERROR_FILE_TOO_LARGE).
bool ReadWholeFile(const std::wstring& path, size_t maximum, std::vector<unsigned char>& data, DWORD& error);
// True when the file carries named data streams besides the default one.
bool HasNamedStreams(const std::wstring& path);

struct PdfStructure
{
    bool Valid = false;
    std::wstring Version; // "1.7"
    bool Encrypted = false;
    bool Linearized = false;
    DWORD Error = ERROR_SUCCESS;
};

PdfStructure InspectPdfStructure(const std::wstring& path);

struct PdfPages
{
    bool Ok = false;
    bool PasswordProtected = false;
    bool TimedOut = false;
    std::wstring Error;
    std::vector<std::pair<double, double>> SizesMm; // width, height per page
};

class IPdfPageInspector
{
public:
    virtual ~IPdfPageInspector() {}
    // Must run on a thread in the multithreaded apartment.
    virtual PdfPages Inspect(const std::wstring& path, unsigned timeoutMs) = 0;
};

// Windows.Data.Pdf (C++/WinRT); returns an inspector that reports
// "unavailable" when the runtime class cannot be activated.
std::unique_ptr<IPdfPageInspector> CreateWinRtPdfPageInspector();

class NullPdfPageInspector : public IPdfPageInspector
{
public:
    PdfPages Inspect(const std::wstring&, unsigned) override
    {
        PdfPages pages;
        pages.Error = L"unavailable";
        return pages;
    }
};

struct ImageFacts
{
    bool Decoded = false;
    bool DecoderMissing = false; // HO-IMG-030/031
    std::wstring Error;
    uint32_t Width = 0, Height = 0;
    bool DpiKnown = false;
    double DpiX = 0, DpiY = 0;
    std::wstring ColorModel; // rgb | cmyk | gray | indexed | lab | "" unknown
    int Alpha = -1;          // -1 unknown, 0 no, 1 yes
    int BitsPerChannel = 0;  // 0 unknown
    uint32_t Frames = 1;
    bool Gps = false;
};

class IImageInspector
{
public:
    virtual ~IImageInspector() {}
    // 'formatId' selects the PSD header parser for "psd"; other raster formats use WIC.
    virtual ImageFacts Inspect(const std::wstring& path, const std::wstring& formatId) = 0;
};

// WIC must be used from a thread in the multithreaded apartment.
std::unique_ptr<IImageInspector> CreateWicImageInspector();

// PSD/PSB header and resolution resource (C.5.4.5), exposed for tests.
ImageFacts InspectPsd(HANDLE file);

} // namespace handoff
