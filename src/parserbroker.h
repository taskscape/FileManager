// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "parserbroker_protocol.h"

class CSalamanderThumbnailMakerAbstract;

struct CParserBrokerArchiveMetadata
{
    ULONGLONG FileSize;
    FILETIME LastWriteTime;
    DWORD ItemCount;
};

class CParserBrokerClient
{
private:
    HANDLE Pipe;
    HANDLE Process;
    HANDLE Job;
    CRITICAL_SECTION Lock;
    DWORD NextCorrelationId;
    WCHAR PipeName[MAX_PATH];

    BOOL Start();
    void Stop();
    BOOL Invoke(WORD type, const void* request, DWORD requestLength,
                WORD responseType, void* response, DWORD responseCapacity,
                DWORD* responseLength);
    BOOL InvokeOnce(WORD type, const void* request, DWORD requestLength,
                    WORD responseType, void* response, DWORD responseCapacity,
                    DWORD* responseLength);

public:
    CParserBrokerClient();
    ~CParserBrokerClient();

    BOOL LoadThumbnail(const char* path, int width, int height, BOOL fastThumbnail,
                       CSalamanderThumbnailMakerAbstract* maker);
    BOOL QueryArchiveMetadata(const char* path, CParserBrokerArchiveMetadata* metadata);
};

extern CParserBrokerClient ParserBroker;
