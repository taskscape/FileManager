// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA)
#include "..\\common\\checked_arithmetic.h"
// The upload's parameter object remains dialog-owned until this worker joins.
#include "..\\common\\thread_owner.h"

#include <winhttp.h>
#include <string>
#include <vector>

BOOL AnalyzeResponse(const char* str, int strLen, CUploadParams* uploadParams);

namespace
{
const wchar_t kServerName[] = L"reports.taskscape.com";
const wchar_t kUploadPath[] = L"/api/v1/crash-reports";
const char kMultipartBoundary[] = "---------------------------OpenSalamanderCrashReport";
const DWORD kIoBufferSize = 64 * 1024;
const size_t kMaximumResponseSize = 64 * 1024;
const int kUploadRetryCount = 1;

CRITICAL_SECTION UploadRequestLock;
BOOL UploadRequestLockInitialized = FALSE;
HINTERNET ActiveUploadRequest = NULL;
HINTERNET ActiveUploadSession = NULL;

void SetWinHttpError(CUploadParams* uploadParams, DWORD error)
{
    // Preserve actionable failure classes instead of presenting every network
    // failure as a generic transport error to the crash-report dialog.
    if (error == ERROR_WINHTTP_TIMEOUT)
    {
        sprintf(uploadParams->ErrorMessage, "Network timeout (%lu).", error);
        return;
    }
    if (error == ERROR_WINHTTP_LOGIN_FAILURE)
    {
        sprintf(uploadParams->ErrorMessage, "Authentication failure (%lu).", error);
        return;
    }
    if (error == ERROR_WINHTTP_INVALID_SERVER_RESPONSE || error == ERROR_WINHTTP_HEADER_NOT_FOUND ||
        error == ERROR_WINHTTP_SECURE_FAILURE || error == ERROR_WINHTTP_REDIRECT_FAILED)
    {
        sprintf(uploadParams->ErrorMessage, "Protocol or TLS failure (%lu).", error);
        return;
    }
    sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_HTTP_ERROR, HLanguage), error);
}

void SetHttpStatusError(CUploadParams* uploadParams, DWORD status)
{
    // HTTP authentication and a malformed/server-rejected protocol response
    // must remain distinguishable from a deadline so retry advice is sound.
    if (status == HTTP_STATUS_DENIED || status == HTTP_STATUS_PROXY_AUTH_REQ)
    {
        sprintf(uploadParams->ErrorMessage, "Authentication failure (HTTP %lu).", status);
        return;
    }
    sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_HTTP_STATUS, HLanguage), status);
}

void FreeProxyConfig(WINHTTP_CURRENT_USER_IE_PROXY_CONFIG* proxyConfig)
{
    if (proxyConfig->lpszAutoConfigUrl != NULL)
        GlobalFree(proxyConfig->lpszAutoConfigUrl);
    if (proxyConfig->lpszProxy != NULL)
        GlobalFree(proxyConfig->lpszProxy);
    if (proxyConfig->lpszProxyBypass != NULL)
        GlobalFree(proxyConfig->lpszProxyBypass);
}

// WinHTTP's default proxy configuration supports system-wide explicit proxy
// settings.  This additionally honours an explicitly configured per-user
// Internet Settings proxy; PAC and auto-detect settings remain WinHTTP's
// normal responsibility and are deliberately not interpreted by this code.
BOOL ConfigureExplicitProxy(HINTERNET session, DWORD* error)
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxyConfig;
    ZeroMemory(&proxyConfig, sizeof(proxyConfig));
    if (!WinHttpGetIEProxyConfigForCurrentUser(&proxyConfig))
    {
        DWORD proxyError = GetLastError();
        if (proxyError == ERROR_FILE_NOT_FOUND)
            return TRUE;
        *error = proxyError;
        return FALSE;
    }

    BOOL result = TRUE;
    if (proxyConfig.lpszProxy != NULL && proxyConfig.lpszProxy[0] != L'\0')
    {
        WINHTTP_PROXY_INFO proxyInfo;
        proxyInfo.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        proxyInfo.lpszProxy = proxyConfig.lpszProxy;
        proxyInfo.lpszProxyBypass = proxyConfig.lpszProxyBypass;
        result = WinHttpSetOption(session, WINHTTP_OPTION_PROXY, &proxyInfo, sizeof(proxyInfo));
        if (!result)
            *error = GetLastError();
    }

    FreeProxyConfig(&proxyConfig);
    return result;
}

BOOL IsUploadCancelled(const CUploadParams* uploadParams)
{
    return InterlockedCompareExchange((volatile LONG*)&uploadParams->Cancelled, 0, 0) != 0;
}

void SetUploadError(CUploadParams* uploadParams, DWORD error)
{
    if (IsUploadCancelled(uploadParams))
    {
        uploadParams->Cancelled = TRUE;
        error = ERROR_CANCELLED;
    }
    SetWinHttpError(uploadParams, error);
}

BOOL RegisterActiveUploadSession(HINTERNET session, const CUploadParams* uploadParams)
{
    BOOL registered = FALSE;
    EnterCriticalSection(&UploadRequestLock);
    if (!IsUploadCancelled(uploadParams))
    {
        ActiveUploadSession = session;
        registered = TRUE;
    }
    LeaveCriticalSection(&UploadRequestLock);

    if (!registered)
        WinHttpCloseHandle(session);
    return registered;
}

BOOL RegisterActiveUploadRequest(HINTERNET request, const CUploadParams* uploadParams)
{
    BOOL registered = FALSE;
    EnterCriticalSection(&UploadRequestLock);
    if (!IsUploadCancelled(uploadParams))
    {
        ActiveUploadRequest = request;
        registered = TRUE;
    }
    LeaveCriticalSection(&UploadRequestLock);

    if (!registered)
        WinHttpCloseHandle(request);
    return registered;
}

void CloseActiveUploadRequest(HINTERNET request)
{
    BOOL closeRequest = FALSE;
    EnterCriticalSection(&UploadRequestLock);
    if (ActiveUploadRequest == request)
    {
        ActiveUploadRequest = NULL;
        closeRequest = TRUE;
    }
    LeaveCriticalSection(&UploadRequestLock);

    if (closeRequest)
        WinHttpCloseHandle(request);
}

void CloseActiveUploadSession(HINTERNET session)
{
    BOOL closeSession = FALSE;
    EnterCriticalSection(&UploadRequestLock);
    if (ActiveUploadSession == session)
    {
        ActiveUploadSession = NULL;
        closeSession = TRUE;
    }
    LeaveCriticalSection(&UploadRequestLock);

    if (closeSession)
        WinHttpCloseHandle(session);
}

BOOL WriteRequestData(HINTERNET request, const void* data, DWORD size, DWORD* error)
{
    const BYTE* bytes = (const BYTE*)data;
    while (size != 0)
    {
        DWORD written = 0;
        if (!WinHttpWriteData(request, bytes, size, &written))
        {
            *error = GetLastError();
            return FALSE;
        }
        if (written == 0)
        {
            *error = ERROR_WRITE_FAULT;
            return FALSE;
        }
        bytes += written;
        size -= written;
    }
    return TRUE;
}

BOOL FinishChunkedRequest(HINTERNET request, DWORD* error)
{
    DWORD written = 0;
    if (!WinHttpWriteData(request, NULL, 0, &written))
    {
        *error = GetLastError();
        return FALSE;
    }
    return TRUE;
}

BOOL ReadResponse(HINTERNET request, std::string* response, DWORD* error)
{
    std::vector<char> buffer(kIoBufferSize);
    DWORD bufferSize;
    if (!CheckedCastSizeToDword(buffer.size(), &bufferSize))
    {
        *error = ERROR_ARITHMETIC_OVERFLOW;
        return FALSE;
    }
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            *error = GetLastError();
            return FALSE;
        }
        if (available == 0)
            return TRUE;
        size_t availableSize;
        size_t responseSize;
        // Network-provided byte counts must be representable before they are
        // combined with owned storage or handed back to a legacy int parser.
        if (!CheckedCastDwordToSize(available, &availableSize) ||
            !CheckedAddSize(response->size(), availableSize, &responseSize) ||
            responseSize > kMaximumResponseSize)
        {
            *error = ERROR_INSUFFICIENT_BUFFER;
            return FALSE;
        }

        DWORD bytesToRead = available < bufferSize ? available : bufferSize;
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), bytesToRead, &read))
        {
            *error = GetLastError();
            return FALSE;
        }
        if (read == 0)
        {
            *error = ERROR_CONNECTION_ABORTED;
            return FALSE;
        }
        response->append(buffer.data(), read);
    }
}

BOOL UploadReportAttempt(CUploadParams* uploadParams, BOOL* retryable)
{
    *retryable = FALSE;

    const char* fileNameOnly = strrchr(uploadParams->FileName, '\\');
    fileNameOnly = fileNameOnly == NULL ? uploadParams->FileName : fileNameOnly + 1;

    char multipartPrefix[3 * MAX_PATH + 512];
    sprintf_s(multipartPrefix, sizeof(multipartPrefix),
              "--%s\r\n"
              "Content-Disposition: form-data; name=\"taskscapefile\"; filename=\"%s\"\r\n"
              "Content-Type: application/octet-stream\r\n\r\n",
              kMultipartBoundary, fileNameOnly);
    const char multipartSuffix[] = "\r\n--" "---------------------------OpenSalamanderCrashReport" "--\r\n";
    DWORD multipartPrefixLength;
    DWORD multipartSuffixLength;
    // The multipart framing is sent through DWORD-based WinHTTP APIs, so do
    // not allow a future dynamic framing change to truncate its length.
    if (!CheckedCastSizeToDword(strlen(multipartPrefix), &multipartPrefixLength) ||
        !CheckedCastSizeToDword(strlen(multipartSuffix), &multipartSuffixLength))
    {
        SetUploadError(uploadParams, ERROR_ARITHMETIC_OVERFLOW);
        return FALSE;
    }

    HANDLE file = CreateFile(uploadParams->FileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_FILE_OPEN, HLanguage), uploadParams->FileName);
        return FALSE;
    }

    BOOL result = FALSE;
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize) || fileSize.QuadPart < 0)
    {
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_FILE_SIZE, HLanguage), uploadParams->FileName);
    }
    else
    {
        DWORD error = ERROR_SUCCESS;
        HINTERNET session = WinHttpOpen(L"Open Salamander Bug Reporter/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == NULL)
        {
            SetWinHttpError(uploadParams, GetLastError());
        }
        else if (!RegisterActiveUploadSession(session, uploadParams))
        {
            SetUploadError(uploadParams, ERROR_CANCELLED);
        }
        else
        {
            // Bound all network operations; normal certificate-chain and hostname
            // validation are intentionally left enabled by not changing security flags.
            if (!WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000))
            {
                SetUploadError(uploadParams, GetLastError());
            }
            else if (!ConfigureExplicitProxy(session, &error))
            {
                SetUploadError(uploadParams, error);
            }
            else
            {
                HINTERNET connection = WinHttpConnect(session, kServerName, INTERNET_DEFAULT_HTTPS_PORT, 0);
                if (connection == NULL)
                {
                    SetUploadError(uploadParams, GetLastError());
                }
                else
                {
                    HINTERNET request = WinHttpOpenRequest(connection, L"POST", kUploadPath, NULL, WINHTTP_NO_REFERER,
                                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
                    if (request == NULL)
                    {
                        SetUploadError(uploadParams, GetLastError());
                    }
                    else
                    {
                        if (!RegisterActiveUploadRequest(request, uploadParams))
                        {
                            SetUploadError(uploadParams, ERROR_CANCELLED);
                        }
                        else
                        {
                            // A fixed HTTPS endpoint never follows a redirect, so a server
                            // cannot downgrade a report to plaintext HTTP.
                            DWORD disabledFeatures = WINHTTP_DISABLE_REDIRECTS;
                            if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabledFeatures, sizeof(disabledFeatures)))
                            {
                                SetUploadError(uploadParams, GetLastError());
                            }
                            else
                            {
                                // WinHTTP owns the Transfer-Encoding framing.  This avoids a
                                // lossy DWORD Content-Length calculation and permits reports
                                // larger than 4 GiB without allocating them in memory.
                                const wchar_t contentType[] = L"Content-Type: multipart/form-data; boundary=---------------------------OpenSalamanderCrashReport\r\nTransfer-Encoding: chunked\r\n";
                                if (!WinHttpSendRequest(request, contentType, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0,
                                                    WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH, 0))
                                {
                                    error = GetLastError();
                                    // No multipart bytes have been written yet, so this is the
                                    // one point at which repeating a POST cannot duplicate a
                                    // committed crash report.
                                    *retryable = !IsUploadCancelled(uploadParams);
                                    SetUploadError(uploadParams, error);
                                }
                                else if (!WriteRequestData(request, multipartPrefix, multipartPrefixLength, &error))
                                {
                                    SetUploadError(uploadParams, error);
                                }
                                else
                                {
                                std::vector<char> buffer(kIoBufferSize);
                                BOOL writeSucceeded = TRUE;
                                uint64_t remaining = (uint64_t)fileSize.QuadPart;
                                DWORD bufferSize;
                                if (!CheckedCastSizeToDword(buffer.size(), &bufferSize))
                                {
                                    error = ERROR_ARITHMETIC_OVERFLOW;
                                    writeSucceeded = FALSE;
                                }
                                while (remaining != 0)
                                {
                                    if (IsUploadCancelled(uploadParams))
                                    {
                                        error = ERROR_CANCELLED;
                                        writeSucceeded = FALSE;
                                        break;
                                    }

                                    DWORD read = 0;
                                    DWORD bytesToRead = bufferSize;
                                    if (remaining < bufferSize && !CheckedCastUInt64ToDword(remaining, &bytesToRead))
                                    {
                                        error = ERROR_ARITHMETIC_OVERFLOW;
                                        writeSucceeded = FALSE;
                                        break;
                                    }
                                    if (!ReadFile(file, buffer.data(), bytesToRead, &read, NULL))
                                    {
                                        error = GetLastError();
                                        writeSucceeded = FALSE;
                                        break;
                                    }
                                    if (read == 0)
                                    {
                                        error = ERROR_HANDLE_EOF;
                                        writeSucceeded = FALSE;
                                        break;
                                    }
                                    if (read > remaining)
                                    {
                                        error = ERROR_ARITHMETIC_OVERFLOW;
                                        writeSucceeded = FALSE;
                                        break;
                                    }
                                    remaining -= read;
                                    if (!WriteRequestData(request, buffer.data(), read, &error))
                                    {
                                        writeSucceeded = FALSE;
                                        break;
                                    }
                                }

                                if (!writeSucceeded)
                                {
                                    SetUploadError(uploadParams, error);
                                }
                                else if (!WriteRequestData(request, multipartSuffix, multipartSuffixLength, &error))
                                {
                                    SetUploadError(uploadParams, error);
                                }
                                else if (!FinishChunkedRequest(request, &error))
                                {
                                    SetUploadError(uploadParams, error);
                                }
                                else if (!WinHttpReceiveResponse(request, NULL))
                                {
                                    SetUploadError(uploadParams, GetLastError());
                                }
                                else
                                {
                                    DWORD status = 0;
                                    DWORD statusSize = sizeof(status);
                                    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
                                    {
                                        SetUploadError(uploadParams, GetLastError());
                                    }
                                    else if (status < 200 || status >= 300)
                                    {
                                        SetHttpStatusError(uploadParams, status);
                                    }
                                    else
                                    {
                                        std::string response;
                                        if (!ReadResponse(request, &response, &error))
                                            SetUploadError(uploadParams, error);
                                        else
                                        {
                                            int responseLength;
                                            if (!CheckedCastSizeToInt(response.size(), &responseLength))
                                                SetUploadError(uploadParams, ERROR_ARITHMETIC_OVERFLOW);
                                            else
                                                result = AnalyzeResponse(response.c_str(), responseLength, uploadParams);
                                        }
                                    }
                                }
                                }
                            }
                        }
                        CloseActiveUploadRequest(request);
                    }
                    WinHttpCloseHandle(connection);
                }
            }
            CloseActiveUploadSession(session);
        }
    }

    CloseHandle(file);
    return result;
}

BOOL UploadReport(CUploadParams* uploadParams)
{
    uploadParams->ErrorMessage[0] = 0;

    for (int attempt = 0; attempt <= kUploadRetryCount; attempt++)
    {
        BOOL retryable = FALSE;
        if (UploadReportAttempt(uploadParams, &retryable))
            return TRUE;
        if (!retryable || IsUploadCancelled(uploadParams))
            break;
    }
    return FALSE;
}
} // namespace

// Server response numeric status values retained for compatibility.
#define UPLOAD_ERR_OK 0
#define UPLOAD_ERR_INI_SIZE 1
#define UPLOAD_ERR_FORM_SIZE 2
#define UPLOAD_ERR_PARTIAL 3
#define UPLOAD_ERR_NO_FILE 4
#define UPLOAD_ERR_NO_TMP_DIR 5
#define UPLOAD_ERR_CANT_WRITE 6
#define UPLOAD_ERR_EXTENSION 7

BOOL GetFilesError(int err, CUploadParams* uploadParams)
{
    uploadParams->ErrorMessage[0] = 0;
    switch (err)
    {
    case UPLOAD_ERR_OK:
        return TRUE;

    case UPLOAD_ERR_INI_SIZE:
    case UPLOAD_ERR_FORM_SIZE:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_INI_SIZE, HLanguage), err);
        break;

    case UPLOAD_ERR_PARTIAL:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_PARTIAL, HLanguage), err);
        break;

    case UPLOAD_ERR_NO_FILE:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_NO_FILE, HLanguage), err);
        break;

    case UPLOAD_ERR_NO_TMP_DIR:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_NO_TMP_DIR, HLanguage), err);
        break;

    case UPLOAD_ERR_CANT_WRITE:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_CANT_WRITE, HLanguage), err);
        break;

    case UPLOAD_ERR_EXTENSION:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_EXTENSION, HLanguage), err);
        break;

    default:
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_ERR_UNKNOWN, HLanguage), err);
        break;
    }
    return FALSE;
}

BOOL AnalyzeResponse(const char* str, int strLen, CUploadParams* uploadParams)
{
    // The v1 endpoint returns <response>X</response>, where X matches the
    // established upload-result values above.  The response body is bounded
    // before this parser is called.
    const char* tagOpen = strstr(str, "<response>");
    if (tagOpen != NULL)
    {
        const char* numBegin = tagOpen + strlen("<response>");
        const char* num = numBegin;
        while (*num >= '0' && *num <= '9' && num - numBegin < 10 && *num != 0)
            num++;
        if (num > numBegin)
        {
            const char* tagClose = strstr(num, "</response>");
            if (tagClose == num)
            {
                char buff[10];
                StringCchCopyNA(buff, (int)(num - numBegin + 1), numBegin, (int)(num - numBegin + 1)); // counted bounded copy instead of lstrcpyn
                return GetFilesError(atoi(buff), uploadParams);
            }
            sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_SYNTAX_ERROR_CLOSE, HLanguage));
        }
        else
            sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_SYNTAX_ERROR_VALUE, HLanguage));
    }
    else
        sprintf(uploadParams->ErrorMessage, LoadStr(IDS_SALMON_SYNTAX_ERROR_OPEN, HLanguage));
    return FALSE;
}

DWORD WINAPI UploadThreadF(void* param, HANDLE stopEvent)
{
    // WinHTTP cancellation is driven by Cancelled plus closing active handles;
    // the common owner stop event must not bypass that transport cleanup.
    (void)stopEvent;
    CUploadParams* uploadParams = (CUploadParams*)param;
    uploadParams->Result = UploadReport(uploadParams);
    return EXIT_SUCCESS;
}

// The owner closes the actual handle only after the upload callback has finished;
// HUploadThread remains a borrowed compatibility probe for the dialog.
CThreadOwner* UploadThreadOwner = NULL;
HANDLE HUploadThread = NULL;

BOOL StartUploadThread(CUploadParams* params)
{
    if (HUploadThread != NULL)
        return FALSE;
    if (!UploadRequestLockInitialized)
    {
        InitializeCriticalSection(&UploadRequestLock);
        UploadRequestLockInitialized = TRUE;
    }
    // Each upload owns a fresh cancellation scope and no handles from a prior run.
    EnterCriticalSection(&UploadRequestLock);
    ActiveUploadRequest = NULL;
    ActiveUploadSession = NULL;
    LeaveCriticalSection(&UploadRequestLock);
    InterlockedExchange(&params->Cancelled, FALSE);
    UploadThreadOwner = new CThreadOwner;
    if (UploadThreadOwner == NULL ||
        !UploadThreadOwner->Start(UploadThreadF, params, "crash-report upload"))
    {
        delete UploadThreadOwner;
        UploadThreadOwner = NULL;
        return FALSE;
    }
    HUploadThread = UploadThreadOwner->GetThreadHandle();
    return TRUE;
}

void CancelUploadThread(CUploadParams* params)
{
    if (params == NULL || HUploadThread == NULL)
        return;

    InterlockedExchange(&params->Cancelled, TRUE);

    HINTERNET request = NULL;
    HINTERNET session = NULL;
    EnterCriticalSection(&UploadRequestLock);
    request = ActiveUploadRequest;
    session = ActiveUploadSession;
    ActiveUploadRequest = NULL;
    ActiveUploadSession = NULL;
    LeaveCriticalSection(&UploadRequestLock);

    // Closing both WinHTTP layers interrupts any current request and prevents
    // a pending resolve/connect phase from delaying dialog shutdown.
    if (request != NULL)
        WinHttpCloseHandle(request);
    if (session != NULL)
        WinHttpCloseHandle(session);
}

BOOL IsUploadThreadRunning()
{
    if (HUploadThread == NULL)
        return FALSE;
    DWORD res = WaitForSingleObject(HUploadThread, 0);
    if (res != WAIT_TIMEOUT)
    {
        // Completion was observed before the dialog can release CUploadParams.
        UploadThreadOwner->StopAndJoin(0);
        delete UploadThreadOwner;
        UploadThreadOwner = NULL;
        HUploadThread = NULL;
        return FALSE;
    }
    return TRUE;
}
