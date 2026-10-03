// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Page count and page sizes through Windows.Data.Pdf (handoff-spec.md D-04,
// C.5.4.3). This translation unit is the only C++/WinRT consumer: it builds
// without the precompiled header, and every WinRT exception stays inside it.

#include <windows.h>
#include <shcore.h>
#include <shlwapi.h>

#ifdef GetCurrentTime
#undef GetCurrentTime // windows.h macro collides with a WinRT member name
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Storage.Streams.h>

#include "inspect.h"
#include "text_util.h"

namespace handoff
{

namespace
{

class WinRtPdfPageInspector : public IPdfPageInspector
{
public:
    PdfPages Inspect(const std::wstring& path, unsigned timeoutMs) override
    {
        PdfPages pages;
        try
        {
            // The stream is opened by path through the long-path prefix and denies
            // writers only for the duration of the inspection.
            winrt::com_ptr<IStream> stream;
            HRESULT hr = SHCreateStreamOnFileEx(LongPath(path).c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE,
                                                nullptr, stream.put());
            if (FAILED(hr))
            {
                pages.Error = HResultText(hr);
                return pages;
            }
            winrt::Windows::Storage::Streams::IRandomAccessStream randomAccess{nullptr};
            hr = CreateRandomAccessStreamOverStream(stream.get(), BSOS_DEFAULT,
                                                    winrt::guid_of<winrt::Windows::Storage::Streams::IRandomAccessStream>(),
                                                    winrt::put_abi(randomAccess));
            if (FAILED(hr))
            {
                pages.Error = HResultText(hr);
                return pages;
            }
            auto operation = winrt::Windows::Data::Pdf::PdfDocument::LoadFromStreamAsync(randomAccess);
            auto status = operation.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status != winrt::Windows::Foundation::AsyncStatus::Completed)
            {
                if (status == winrt::Windows::Foundation::AsyncStatus::Started)
                {
                    operation.Cancel();
                    pages.TimedOut = true;
                    pages.Error = L"timeout";
                    return pages;
                }
                hr = operation.ErrorCode();
                if (hr == HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD))
                    pages.PasswordProtected = true;
                pages.Error = HResultText(hr);
                return pages;
            }
            auto document = operation.GetResults();
            pages.PasswordProtected = document.IsPasswordProtected();
            uint32_t count = document.PageCount();
            for (uint32_t i = 0; i < count; i++)
            {
                auto page = document.GetPage(i);
                // Size is in DIPs (1/96 inch) and already reflects the crop box and rotation.
                auto size = page.Size();
                pages.SizesMm.push_back(std::make_pair(size.Width * 25.4 / 96.0, size.Height * 25.4 / 96.0));
                page.Close();
            }
            document = nullptr;
            pages.Ok = true;
        }
        catch (const winrt::hresult_error& error)
        {
            if (error.code() == HRESULT_FROM_WIN32(ERROR_WRONG_PASSWORD))
                pages.PasswordProtected = true;
            pages.Error = HResultText(error.code());
        }
        catch (...)
        {
            pages.Error = L"unavailable";
        }
        return pages;
    }
};

} // namespace

std::unique_ptr<IPdfPageInspector> CreateWinRtPdfPageInspector()
{
    return std::unique_ptr<IPdfPageInspector>(new WinRtPdfPageInspector());
}

} // namespace handoff
