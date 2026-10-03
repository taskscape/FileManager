// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <winnetwk.h>
#include <stdlib.h>

// Network providers expose localized UTF-16 names; the cache and plug-in API store UTF-8.
// Keep converted fields owned until the receiving API/cache has copied them, preserving null fields.
inline DWORD CopyNetworkText(const WCHAR* source, char*& destination)
{
    destination = NULL;
    if (source == NULL)
        return NO_ERROR;
    int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, NULL, 0, NULL, NULL);
    if (size == 0)
        return GetLastError();
    destination = static_cast<char*>(malloc(size));
    if (destination == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, destination, size, NULL, NULL) == 0)
        return GetLastError();
    return NO_ERROR;
}

inline DWORD CopyNetworkText(const char* source, WCHAR*& destination)
{
    destination = NULL;
    if (source == NULL)
        return NO_ERROR;
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    if (size == 0)
        return GetLastError();
    destination = static_cast<WCHAR*>(malloc(static_cast<size_t>(size) * sizeof(WCHAR)));
    if (destination == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, destination, size) == 0)
        return GetLastError();
    return NO_ERROR;
}

template <class TResource>
class CConvertedNetworkResource
{
    CConvertedNetworkResource(const CConvertedNetworkResource&);
    CConvertedNetworkResource& operator=(const CConvertedNetworkResource&);

public:
    TResource Resource;
    DWORD Error;

    template <class TSource>
    explicit CConvertedNetworkResource(const TSource& source) : Resource(), Error(NO_ERROR)
    {
        Resource.dwScope = source.dwScope;
        Resource.dwType = source.dwType;
        Resource.dwDisplayType = source.dwDisplayType;
        Resource.dwUsage = source.dwUsage;
        Error = CopyNetworkText(source.lpLocalName, Resource.lpLocalName);
        if (Error == NO_ERROR)
            Error = CopyNetworkText(source.lpRemoteName, Resource.lpRemoteName);
        if (Error == NO_ERROR)
            Error = CopyNetworkText(source.lpComment, Resource.lpComment);
        if (Error == NO_ERROR)
            Error = CopyNetworkText(source.lpProvider, Resource.lpProvider);
    }

    ~CConvertedNetworkResource()
    {
        free(Resource.lpLocalName);
        free(Resource.lpRemoteName);
        free(Resource.lpComment);
        free(Resource.lpProvider);
    }
};

typedef CConvertedNetworkResource<NETRESOURCEA> CNetworkResourceUtf8;
typedef CConvertedNetworkResource<NETRESOURCEW> CNetworkResourceWide;
