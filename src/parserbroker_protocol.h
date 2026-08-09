// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

// This protocol is intentionally a plain, packed byte contract.  It must not
// contain pointers, handles, C++ objects, or structures whose layout changes
// with the host SDK.
static const DWORD PARSER_BROKER_MAGIC = 0x42525350; // 'PSRB'
static const WORD PARSER_BROKER_VERSION = 1;
static const DWORD PARSER_BROKER_MAX_PAYLOAD = 1024 * 1024;
static const DWORD PARSER_BROKER_MAX_PATH_BYTES = 32768;
static const DWORD PARSER_BROKER_MAX_THUMBNAIL_DIMENSION = 512;

enum EParserBrokerMessageType
{
    pbmtThumbnailRequest = 1,
    pbmtThumbnailResponse = 2,
    pbmtArchiveMetadataRequest = 3,
    pbmtArchiveMetadataResponse = 4,
};

enum EParserBrokerStatus
{
    pbsOk = 0,
    pbsInvalidRequest = 1,
    pbsUnsupported = 2,
    pbsFailed = 3,
    pbsResourceLimit = 4,
};

#pragma pack(push, 1)
struct CParserBrokerMessageHeader
{
    DWORD Magic;
    WORD Version;
    WORD Type;
    DWORD PayloadLength;
    DWORD CorrelationId;
    DWORD Status;
};

struct CParserBrokerThumbnailRequest
{
    DWORD Width;
    DWORD Height;
    DWORD Flags;
    DWORD PathBytes;
};

struct CParserBrokerThumbnailResponse
{
    DWORD Width;
    DWORD Height;
    DWORD PixelBytes;
};

struct CParserBrokerArchiveMetadataRequest
{
    DWORD PathBytes;
};

struct CParserBrokerArchiveMetadataResponse
{
    ULONGLONG FileSize;
    FILETIME LastWriteTime;
    DWORD ItemCount;
};
#pragma pack(pop)

static_assert(sizeof(CParserBrokerMessageHeader) == 20, "The broker header is an IPC wire format.");
