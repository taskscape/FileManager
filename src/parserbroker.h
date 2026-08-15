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

// Broker admission stays bounded even when slow thumbnail or metadata callers
// arrive from several panel and navigation workers at once.
struct CParserBrokerQueueMetrics
{
    LONG Capacity;
    LONG Pending;
    LONGLONG Accepted;
    LONGLONG Rejected;
    LONGLONG HighWaterMark;
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
    volatile LONG PendingRequests;
    volatile LONGLONG AcceptedRequests;
    volatile LONGLONG RejectedRequests;
    volatile LONGLONG HighWaterMark;

    BOOL Start();
    // Start and Invoke already own Lock; keep teardown private so it cannot race a serialized pipe transaction.
    void StopLocked();
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

    // Exposes broker backpressure without retaining callers behind the serialized pipe.
    CParserBrokerQueueMetrics GetQueueMetrics();
};

extern CParserBrokerClient ParserBroker;
