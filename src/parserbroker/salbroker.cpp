// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>

#include "../common/checked_arithmetic.h"
#include "../parserbroker_protocol.h"

static BOOL ReadExact(HANDLE pipe, void* buffer, DWORD length)
{
    BYTE* current = (BYTE*)buffer;
    while (length != 0)
    {
        DWORD received;
        if (!ReadFile(pipe, current, length, &received, NULL) || received == 0)
            return FALSE;
        current += received;
        length -= received;
    }
    return TRUE;
}

static BOOL WriteExact(HANDLE pipe, const void* buffer, DWORD length)
{
    const BYTE* current = (const BYTE*)buffer;
    while (length != 0)
    {
        DWORD sent;
        if (!WriteFile(pipe, current, length, &sent, NULL) || sent == 0)
            return FALSE;
        current += sent;
        length -= sent;
    }
    return TRUE;
}

static BOOL IsValidPathPayload(const BYTE* payload, DWORD payloadLength, DWORD pathOffset, DWORD pathBytes, const WCHAR** path)
{
    if (pathBytes == 0 || pathBytes > PARSER_BROKER_MAX_PATH_BYTES || (pathBytes & 1) != 0 ||
        pathOffset > payloadLength || pathBytes > payloadLength - pathOffset)
        return FALSE;
    // The client sends no terminator over the wire.  Make a private copy before using shell APIs.
    *path = (const WCHAR*)(payload + pathOffset);
    return TRUE;
}

static BOOL CopyPath(const WCHAR* source, DWORD bytes, WCHAR* target, DWORD capacity)
{
    if (bytes / sizeof(WCHAR) >= capacity)
        return FALSE;
    memcpy(target, source, bytes);
    target[bytes / sizeof(WCHAR)] = 0;
    return TRUE;
}

static EParserBrokerStatus MakeThumbnail(const BYTE* request, DWORD requestLength, BYTE* response, DWORD responseCapacity, DWORD* responseLength)
{
    if (requestLength < sizeof(CParserBrokerThumbnailRequest))
        return pbsInvalidRequest;
    const CParserBrokerThumbnailRequest* thumbnailRequest = (const CParserBrokerThumbnailRequest*)request;
    const WCHAR* pathPayload;
    if (thumbnailRequest->Width == 0 || thumbnailRequest->Height == 0 ||
        thumbnailRequest->Width > PARSER_BROKER_MAX_THUMBNAIL_DIMENSION ||
        thumbnailRequest->Height > PARSER_BROKER_MAX_THUMBNAIL_DIMENSION ||
        !IsValidPathPayload(request, requestLength, sizeof(*thumbnailRequest), thumbnailRequest->PathBytes, &pathPayload))
        return pbsInvalidRequest;
    WCHAR path[MAX_PATH];
    if (!CopyPath(pathPayload, thumbnailRequest->PathBytes, path, _countof(path)))
        return pbsInvalidRequest;

    IShellItem* item = NULL;
    HRESULT hr = SHCreateItemFromParsingName(path, NULL, IID_PPV_ARGS(&item));
    if (FAILED(hr))
        return pbsFailed;
    IShellItemImageFactory* imageFactory = NULL;
    hr = item->QueryInterface(IID_PPV_ARGS(&imageFactory));
    item->Release();
    if (FAILED(hr))
        return pbsUnsupported;

    SIZE size = { (LONG)thumbnailRequest->Width, (LONG)thumbnailRequest->Height };
    HBITMAP bitmap = NULL;
    hr = imageFactory->GetImage(size, SIIGBF_RESIZETOFIT | SIIGBF_BIGGERSIZEOK, &bitmap);
    imageFactory->Release();
    if (FAILED(hr) || bitmap == NULL)
        return pbsFailed;

    BITMAP bitmapInfo;
    if (GetObject(bitmap, sizeof(bitmapInfo), &bitmapInfo) != sizeof(bitmapInfo) || bitmapInfo.bmWidth <= 0 || bitmapInfo.bmHeight <= 0 ||
        bitmapInfo.bmWidth > (LONG)PARSER_BROKER_MAX_THUMBNAIL_DIMENSION || bitmapInfo.bmHeight > (LONG)PARSER_BROKER_MAX_THUMBNAIL_DIMENSION)
    {
        DeleteObject(bitmap);
        return pbsFailed;
    }
    DWORD pixels;
    DWORD pixelBytes;
    DWORD packedResponseLength;
    // Shell-returned dimensions still cross an isolation boundary.  Guard the
    // packed response arithmetic before its size controls the output buffer.
    if (!CheckedMultiplyDword((DWORD)bitmapInfo.bmWidth, (DWORD)bitmapInfo.bmHeight, &pixels) ||
        !CheckedMultiplyDword(pixels, (DWORD)sizeof(DWORD), &pixelBytes) ||
        !CheckedAddDword((DWORD)sizeof(CParserBrokerThumbnailResponse), pixelBytes, &packedResponseLength) ||
        packedResponseLength > responseCapacity)
    {
        DeleteObject(bitmap);
        return pbsResourceLimit;
    }
    BITMAPINFO info;
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = bitmapInfo.bmWidth;
    info.bmiHeader.biHeight = -bitmapInfo.bmHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL);
    int lines = GetDIBits(dc, bitmap, 0, bitmapInfo.bmHeight,
                           response + sizeof(CParserBrokerThumbnailResponse), &info, DIB_RGB_COLORS);
    ReleaseDC(NULL, dc);
    DeleteObject(bitmap);
    if (lines != bitmapInfo.bmHeight)
        return pbsFailed;

    CParserBrokerThumbnailResponse* thumbnailResponse = (CParserBrokerThumbnailResponse*)response;
    thumbnailResponse->Width = bitmapInfo.bmWidth;
    thumbnailResponse->Height = bitmapInfo.bmHeight;
    thumbnailResponse->PixelBytes = pixelBytes;
    *responseLength = packedResponseLength;
    return pbsOk;
}

static EParserBrokerStatus GetArchiveMetadata(const BYTE* request, DWORD requestLength, BYTE* response, DWORD responseCapacity, DWORD* responseLength)
{
    if (requestLength < sizeof(CParserBrokerArchiveMetadataRequest) || responseCapacity < sizeof(CParserBrokerArchiveMetadataResponse))
        return pbsInvalidRequest;
    const CParserBrokerArchiveMetadataRequest* metadataRequest = (const CParserBrokerArchiveMetadataRequest*)request;
    const WCHAR* pathPayload;
    if (!IsValidPathPayload(request, requestLength, sizeof(*metadataRequest), metadataRequest->PathBytes, &pathPayload))
        return pbsInvalidRequest;
    WCHAR path[MAX_PATH];
    if (!CopyPath(pathPayload, metadataRequest->PathBytes, path, _countof(path)))
        return pbsInvalidRequest;

    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &attributes))
        return pbsFailed;
    CParserBrokerArchiveMetadataResponse* metadata = (CParserBrokerArchiveMetadataResponse*)response;
    metadata->FileSize = ((ULONGLONG)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
    metadata->LastWriteTime = attributes.ftLastWriteTime;
    metadata->ItemCount = 0; // Format-specific listing remains deliberately outside this v1 metadata contract.
    *responseLength = sizeof(*metadata);
    return pbsOk;
}

static BOOL Serve(HANDLE pipe)
{
    for (;;)
    {
        CParserBrokerMessageHeader requestHeader;
        if (!ReadExact(pipe, &requestHeader, sizeof(requestHeader)))
            return FALSE;
        if (requestHeader.Magic != PARSER_BROKER_MAGIC || requestHeader.Version != PARSER_BROKER_VERSION ||
            requestHeader.PayloadLength > PARSER_BROKER_MAX_PAYLOAD)
            return FALSE;
        BYTE request[PARSER_BROKER_MAX_PAYLOAD];
        if (requestHeader.PayloadLength != 0 && !ReadExact(pipe, request, requestHeader.PayloadLength))
            return FALSE;

        BYTE response[PARSER_BROKER_MAX_PAYLOAD];
        DWORD responseLength = 0;
        WORD responseType = 0;
        EParserBrokerStatus status = pbsUnsupported;
        if (requestHeader.Type == pbmtThumbnailRequest)
        {
            responseType = pbmtThumbnailResponse;
            status = MakeThumbnail(request, requestHeader.PayloadLength, response, sizeof(response), &responseLength);
        }
        else if (requestHeader.Type == pbmtArchiveMetadataRequest)
        {
            responseType = pbmtArchiveMetadataResponse;
            status = GetArchiveMetadata(request, requestHeader.PayloadLength, response, sizeof(response), &responseLength);
        }

        CParserBrokerMessageHeader responseHeader;
        responseHeader.Magic = PARSER_BROKER_MAGIC;
        responseHeader.Version = PARSER_BROKER_VERSION;
        responseHeader.Type = responseType;
        responseHeader.PayloadLength = status == pbsOk ? responseLength : 0;
        responseHeader.CorrelationId = requestHeader.CorrelationId;
        responseHeader.Status = status;
        if (!WriteExact(pipe, &responseHeader, sizeof(responseHeader)) ||
            (responseHeader.PayloadLength != 0 && !WriteExact(pipe, response, responseHeader.PayloadLength)))
            return FALSE;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    const WCHAR* marker = wcsstr(commandLine, L"--pipe");
    if (marker == NULL)
        return ERROR_INVALID_PARAMETER;
    marker += 6;
    while (*marker == L' ')
        ++marker;
    if (*marker == L'\"')
        ++marker;
    WCHAR pipeName[MAX_PATH];
    int length = 0;
    while (marker[length] != 0 && marker[length] != L'\"' && length + 1 < _countof(pipeName))
    {
        pipeName[length] = marker[length];
        ++length;
    }
    pipeName[length] = 0;
    if (length == 0)
        return ERROR_INVALID_PARAMETER;

    HANDLE pipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pipe == INVALID_HANDLE_VALUE)
        return GetLastError();
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL served = Serve(pipe);
    CoUninitialize();
    CloseHandle(pipe);
    return served ? ERROR_SUCCESS : ERROR_BROKEN_PIPE;
}
