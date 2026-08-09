// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "common\\checked_arithmetic.h"
#include "parserbroker.h"
#include "thumbnl.h"

CParserBrokerClient ParserBroker;

static const DWORD BrokerConnectTimeout = 5000;
static const DWORD BrokerRequestTimeout = 10000;

static BOOL BrokerOverlappedIo(HANDLE pipe, void* buffer, DWORD length, BOOL write, DWORD timeout)
{
    BYTE* current = (BYTE*)buffer;
    DWORD remaining = length;
    while (remaining != 0)
    {
        OVERLAPPED overlapped;
        ZeroMemory(&overlapped, sizeof(overlapped));
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (overlapped.hEvent == NULL)
            return FALSE;

        DWORD transferred = 0;
        BOOL completed = write ? WriteFile(pipe, current, remaining, &transferred, &overlapped)
                               : ReadFile(pipe, current, remaining, &transferred, &overlapped);
        if (!completed && GetLastError() == ERROR_IO_PENDING)
        {
            if (WaitForSingleObject(overlapped.hEvent, timeout) == WAIT_OBJECT_0)
                completed = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
            else
            {
                CancelIoEx(pipe, &overlapped);
                completed = FALSE;
                SetLastError(WAIT_TIMEOUT);
            }
        }
        CloseHandle(overlapped.hEvent);
        if (!completed || transferred == 0)
            return FALSE;
        current += transferred;
        remaining -= transferred;
    }
    return TRUE;
}

static BOOL BrokerConnectPipe(HANDLE pipe)
{
    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (overlapped.hEvent == NULL)
        return FALSE;

    BOOL connected = ConnectNamedPipe(pipe, &overlapped);
    if (!connected)
    {
        DWORD error = GetLastError();
        if (error == ERROR_PIPE_CONNECTED)
            connected = TRUE;
        else if (error == ERROR_IO_PENDING && WaitForSingleObject(overlapped.hEvent, BrokerConnectTimeout) == WAIT_OBJECT_0)
        {
            DWORD ignored;
            connected = GetOverlappedResult(pipe, &overlapped, &ignored, FALSE);
        }
        else
            CancelIoEx(pipe, &overlapped);
    }
    CloseHandle(overlapped.hEvent);
    return connected;
}

CParserBrokerClient::CParserBrokerClient()
{
    Pipe = NULL;
    Process = NULL;
    Job = NULL;
    InitializeCriticalSection(&Lock);
    NextCorrelationId = 1;
    PipeName[0] = 0;
}

CParserBrokerClient::~CParserBrokerClient()
{
    Stop();
    DeleteCriticalSection(&Lock);
}

void CParserBrokerClient::Stop()
{
    if (Job != NULL)
    {
        TerminateJobObject(Job, ERROR_PROCESS_ABORTED);
        CloseHandle(Job);
        Job = NULL;
    }
    if (Process != NULL)
    {
        CloseHandle(Process);
        Process = NULL;
    }
    if (Pipe != NULL)
    {
        DisconnectNamedPipe(Pipe);
        CloseHandle(Pipe);
        Pipe = NULL;
    }
}

BOOL CParserBrokerClient::Start()
{
    if (Pipe != NULL && Process != NULL && WaitForSingleObject(Process, 0) == WAIT_TIMEOUT)
        return TRUE;
    Stop();

    wsprintfW(PipeName, L"\\\\.\\pipe\\OpenSal.ParserBroker.%08X", GetCurrentProcessId());
    Pipe = CreateNamedPipeW(PipeName,
                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            1, PARSER_BROKER_MAX_PAYLOAD, PARSER_BROKER_MAX_PAYLOAD, 0, NULL);
    if (Pipe == INVALID_HANDLE_VALUE)
    {
        Pipe = NULL;
        return FALSE;
    }

    WCHAR executable[MAX_PATH];
    DWORD executableLength = GetModuleFileNameW(NULL, executable, _countof(executable));
    if (executableLength == 0 || executableLength >= _countof(executable))
    {
        Stop();
        return FALSE;
    }
    WCHAR* filename = wcsrchr(executable, L'\\');
    if (filename == NULL)
    {
        Stop();
        return FALSE;
    }
    lstrcpyW(filename + 1, L"salbroker.exe");

    HANDLE currentToken = NULL;
    HANDLE restrictedToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &currentToken) ||
        !CreateRestrictedToken(currentToken, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &restrictedToken))
    {
        if (currentToken != NULL)
            CloseHandle(currentToken);
        Stop();
        return FALSE;
    }
    CloseHandle(currentToken);

    WCHAR commandLine[2 * MAX_PATH + 32];
    wsprintfW(commandLine, L"\"%s\" --pipe \"%s\"", executable, PipeName);
    STARTUPINFOW startup;
    PROCESS_INFORMATION processInfo;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&processInfo, sizeof(processInfo));
    startup.cb = sizeof(startup);
    BOOL created = CreateProcessAsUserW(restrictedToken, executable, commandLine, NULL, NULL, FALSE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &startup, &processInfo);
    CloseHandle(restrictedToken);
    if (!created)
    {
        Stop();
        return FALSE;
    }

    Job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    ZeroMemory(&limits, sizeof(limits));
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                               JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                               JOB_OBJECT_LIMIT_PROCESS_TIME;
    limits.BasicLimitInformation.PerProcessUserTimeLimit.QuadPart = 30LL * 10000000LL;
    limits.ProcessMemoryLimit = 256 * 1024 * 1024;
    if (Job == NULL || !SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(Job, processInfo.hProcess))
    {
        TerminateProcess(processInfo.hProcess, ERROR_ACCESS_DENIED);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        Stop();
        return FALSE;
    }
    Process = processInfo.hProcess;
    ResumeThread(processInfo.hThread);
    CloseHandle(processInfo.hThread);

    if (!BrokerConnectPipe(Pipe))
    {
        Stop();
        return FALSE;
    }
    return TRUE;
}

BOOL CParserBrokerClient::Invoke(WORD type, const void* request, DWORD requestLength,
                                 WORD responseType, void* response, DWORD responseCapacity,
                                 DWORD* responseLength)
{
    // A pipe response is correlated with one request, so serialize access from
    // the icon workers and archive navigation threads.
    EnterCriticalSection(&Lock);
    BOOL completed = FALSE;
    for (int attempt = 0; attempt != 2; ++attempt)
    {
        if (Start() && InvokeOnce(type, request, requestLength, responseType, response, responseCapacity, responseLength))
        {
            completed = TRUE;
            break;
        }
        Stop(); // timeout, malformed response, or crash: kill and recreate the untrusted process.
    }
    LeaveCriticalSection(&Lock);
    return completed;
}

BOOL CParserBrokerClient::InvokeOnce(WORD type, const void* request, DWORD requestLength,
                                     WORD responseType, void* response, DWORD responseCapacity,
                                     DWORD* responseLength)
{
    if (requestLength > PARSER_BROKER_MAX_PAYLOAD)
        return FALSE;
    CParserBrokerMessageHeader requestHeader;
    requestHeader.Magic = PARSER_BROKER_MAGIC;
    requestHeader.Version = PARSER_BROKER_VERSION;
    requestHeader.Type = type;
    requestHeader.PayloadLength = requestLength;
    requestHeader.CorrelationId = NextCorrelationId++;
    requestHeader.Status = pbsOk;
    if (requestHeader.CorrelationId == 0)
        requestHeader.CorrelationId = NextCorrelationId++;

    if (!BrokerOverlappedIo(Pipe, &requestHeader, sizeof(requestHeader), TRUE, BrokerRequestTimeout) ||
        (requestLength != 0 && !BrokerOverlappedIo(Pipe, (void*)request, requestLength, TRUE, BrokerRequestTimeout)))
        return FALSE;

    CParserBrokerMessageHeader responseHeader;
    if (!BrokerOverlappedIo(Pipe, &responseHeader, sizeof(responseHeader), FALSE, BrokerRequestTimeout) ||
        responseHeader.Magic != PARSER_BROKER_MAGIC || responseHeader.Version != PARSER_BROKER_VERSION ||
        responseHeader.Type != responseType || responseHeader.CorrelationId != requestHeader.CorrelationId ||
        responseHeader.Status != pbsOk || responseHeader.PayloadLength > responseCapacity ||
        responseHeader.PayloadLength > PARSER_BROKER_MAX_PAYLOAD)
        return FALSE;
    if (responseHeader.PayloadLength != 0 &&
        !BrokerOverlappedIo(Pipe, response, responseHeader.PayloadLength, FALSE, BrokerRequestTimeout))
        return FALSE;
    *responseLength = responseHeader.PayloadLength;
    return TRUE;
}

static BOOL BrokerMakePathPayload(const char* path, const void* prefix, DWORD prefixLength,
                                  BYTE* payload, DWORD payloadCapacity, DWORD* payloadLength)
{
    int chars = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (chars <= 1)
        return FALSE;
    uint64_t pathBytes64;
    DWORD pathBytes;
    DWORD totalPayloadLength;
    // The conversion result describes external path storage; reject an
    // impossible multiply or packed-message length before writing the buffer.
    if (!CheckedMultiplyUInt64((uint64_t)(chars - 1), (uint64_t)sizeof(WCHAR), &pathBytes64) ||
        !CheckedCastUInt64ToDword(pathBytes64, &pathBytes) ||
        !CheckedAddDword(prefixLength, pathBytes, &totalPayloadLength) ||
        pathBytes > PARSER_BROKER_MAX_PATH_BYTES || totalPayloadLength > payloadCapacity)
        return FALSE;
    memcpy(payload, prefix, prefixLength);
    if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, (WCHAR*)(payload + prefixLength), chars) != chars)
        return FALSE;
    *payloadLength = totalPayloadLength;
    return TRUE;
}

BOOL CParserBrokerClient::LoadThumbnail(const char* path, int width, int height, BOOL fastThumbnail,
                                        CSalamanderThumbnailMakerAbstract* maker)
{
    if (path == NULL || maker == NULL || width <= 0 || height <= 0 ||
        width > (int)PARSER_BROKER_MAX_THUMBNAIL_DIMENSION || height > (int)PARSER_BROKER_MAX_THUMBNAIL_DIMENSION)
        return FALSE;
    BYTE request[PARSER_BROKER_MAX_PATH_BYTES + sizeof(CParserBrokerThumbnailRequest)];
    CParserBrokerThumbnailRequest prefix;
    prefix.Width = width;
    prefix.Height = height;
    prefix.Flags = fastThumbnail ? 1 : 0;
    prefix.PathBytes = 0;
    DWORD requestLength;
    if (!BrokerMakePathPayload(path, &prefix, sizeof(prefix), request, sizeof(request), &requestLength))
        return FALSE;
    ((CParserBrokerThumbnailRequest*)request)->PathBytes = requestLength - sizeof(prefix);

    BYTE response[PARSER_BROKER_MAX_PAYLOAD];
    DWORD responseLength;
    if (!Invoke(pbmtThumbnailRequest, request, requestLength, pbmtThumbnailResponse,
                response, sizeof(response), &responseLength) || responseLength < sizeof(CParserBrokerThumbnailResponse))
        return FALSE;
    const CParserBrokerThumbnailResponse* thumbnail = (const CParserBrokerThumbnailResponse*)response;
    uint64_t expectedPixelBytes;
    DWORD expectedResponseLength;
    // Width, height, and PixelBytes arrive over IPC.  Calculate both the
    // pixel allocation and enclosing response length without wraparound.
    if (!CheckedMultiplyUInt64(thumbnail->Width, thumbnail->Height, &expectedPixelBytes) ||
        !CheckedMultiplyUInt64(expectedPixelBytes, (uint64_t)sizeof(DWORD), &expectedPixelBytes) ||
        !CheckedAddDword((DWORD)sizeof(*thumbnail), thumbnail->PixelBytes, &expectedResponseLength))
        return FALSE;
    if (thumbnail->Width == 0 || thumbnail->Height == 0 || thumbnail->Width > PARSER_BROKER_MAX_THUMBNAIL_DIMENSION ||
        thumbnail->Height > PARSER_BROKER_MAX_THUMBNAIL_DIMENSION || thumbnail->PixelBytes != expectedPixelBytes ||
        responseLength != expectedResponseLength ||
        !maker->SetParameters((int)thumbnail->Width, (int)thumbnail->Height, 0))
        return FALSE;
    return maker->ProcessBuffer(response + sizeof(*thumbnail), (int)thumbnail->Height);
}

BOOL CParserBrokerClient::QueryArchiveMetadata(const char* path, CParserBrokerArchiveMetadata* metadata)
{
    if (path == NULL || metadata == NULL)
        return FALSE;
    BYTE request[PARSER_BROKER_MAX_PATH_BYTES + sizeof(CParserBrokerArchiveMetadataRequest)];
    CParserBrokerArchiveMetadataRequest prefix;
    prefix.PathBytes = 0;
    DWORD requestLength;
    if (!BrokerMakePathPayload(path, &prefix, sizeof(prefix), request, sizeof(request), &requestLength))
        return FALSE;
    ((CParserBrokerArchiveMetadataRequest*)request)->PathBytes = requestLength - sizeof(prefix);

    CParserBrokerArchiveMetadataResponse response;
    DWORD responseLength;
    if (!Invoke(pbmtArchiveMetadataRequest, request, requestLength, pbmtArchiveMetadataResponse,
                &response, sizeof(response), &responseLength) || responseLength != sizeof(response))
        return FALSE;
    metadata->FileSize = response.FileSize;
    metadata->LastWriteTime = response.LastWriteTime;
    metadata->ItemCount = response.ItemCount;
    return TRUE;
}
