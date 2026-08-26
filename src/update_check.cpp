// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <wininet.h>
#include <winhttp.h>

#include "update_check.h"
#include "consts.h"

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "winhttp.lib")

// Update detection rides on the existing release publishing mechanism only:
// the CI workflow publishes a GitHub release with the installer on every push
// to main, so the Releases API publish time of the latest release compared
// against the link timestamp embedded in our own PE header tells us whether a
// newer build exists. No version files or manual steps are involved.
#define UPDATE_CHECK_API_HOST L"api.github.com"
#define UPDATE_CHECK_API_PATH L"/repos/taskscape/FileManager/releases/latest"

// a release must be at least this much newer than our executable to be
// advertised, absorbing clock skew and the delay between building locally
// and CI publishing the same sources
#define UPDATE_CHECK_MIN_AGE_SECONDS 3600

DWORD EnablerUpdateAvailable = FALSE;

// linker-provided DOS header of salamander.exe itself, used to read the PE
// link timestamp without opening the file
extern "C" IMAGE_DOS_HEADER __ImageBase;

static volatile LONG UpdateCheckStarted = FALSE; // guards the single per-session attempt
static BOOL UpdateAvailable = FALSE;
static HWND HUpdateCheckNotifyWindow = NULL;

BOOL IsUpdateAvailable()
{
    return UpdateAvailable;
}

// returns UTC link time of salamander.exe as seconds since January 1, 1970
static ULONGLONG GetOwnModuleLinkTimestamp()
{
    // unlike file timestamps, the PE COFF timestamp survives installer and
    // copy operations, making the comparison deterministic
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)((BYTE*)&__ImageBase + __ImageBase.e_lfanew);
    return (ULONGLONG)ntHeaders->FileHeader.TimeDateStamp;
}

// extracts the "published_at" ISO-8601 UTC timestamp from the releases JSON;
// 'json' must be null-terminated and 'jsonSize' excludes the terminator
static BOOL ExtractPublishedTime(const char* json, size_t jsonSize, ULONGLONG* publishedSeconds)
{
    const char key[] = "\"published_at\":\"";
    const char* pos = strstr(json, key);
    if (pos == NULL || (size_t)(pos - json) + strlen(key) >= jsonSize)
        return FALSE;
    pos += strlen(key); // format: 2026-08-26T12:34:56Z

    int year, month, day, hour, minute, second;
    if (sscanf_s(pos, "%4d-%2d-%2dT%2d:%2d:%2dZ",
                 &year, &month, &day, &hour, &minute, &second) != 6)
        return FALSE;

    SYSTEMTIME st = {0};
    st.wYear = (WORD)year;
    st.wMonth = (WORD)month;
    st.wDay = (WORD)day;
    st.wHour = (WORD)hour;
    st.wMinute = (WORD)minute;
    st.wSecond = (WORD)second;
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) // SYSTEMTIME here is UTC
        return FALSE;
    *publishedSeconds = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000000 - 11644473600ULL;
    return TRUE;
}

static DWORD WINAPI UpdateCheckThreadF(LPVOID param)
{
    // without internet connectivity the check is silently skipped and never
    // retried: there is exactly one attempt per session by design
    DWORD connFlags = 0;
    if (!InternetGetConnectedState(&connFlags, 0))
        return 0;

    BOOL updateFound = FALSE;
    HINTERNET session = WinHttpOpen(L"OpenSalamander-UpdateCheck",
                                    WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session != NULL)
    {
        WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);
        HINTERNET connection = WinHttpConnect(session, UPDATE_CHECK_API_HOST,
                                              INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (connection != NULL)
        {
            HINTERNET request = WinHttpOpenRequest(connection, L"GET", UPDATE_CHECK_API_PATH,
                                                   NULL, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (request != NULL)
            {
                if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                    WinHttpReceiveResponse(request, NULL))
                {
                    DWORD status = 0, statusSize = sizeof(status);
                    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                            WINHTTP_NO_HEADER_INDEX) &&
                        status == HTTP_STATUS_OK)
                    {
                        // read the whole JSON body into a growing heap buffer
                        char* body = NULL;
                        size_t bodySize = 0, bodyAlloc = 0;
                        BOOL readError = FALSE;
                        DWORD bytesRead = 0;
                        for (;;)
                        {
                            char chunk[4096];
                            if (!WinHttpReadData(request, chunk, sizeof(chunk), &bytesRead))
                            {
                                readError = TRUE;
                                break;
                            }
                            if (bytesRead == 0)
                                break;
                            if (bodySize + bytesRead + 1 > bodyAlloc)
                            {
                                size_t newAlloc = bodyAlloc == 0 ? 32 * 1024 : bodyAlloc * 2;
                                while (bodySize + bytesRead + 1 > newAlloc)
                                    newAlloc *= 2;
                                char* newBody = (char*)realloc(body, newAlloc);
                                if (newBody == NULL)
                                {
                                    readError = TRUE;
                                    break;
                                }
                                body = newBody;
                                bodyAlloc = newAlloc;
                            }
                            memcpy(body + bodySize, chunk, bytesRead);
                            bodySize += bytesRead;
                        }
                        // terminate for strstr/sscanf_s; every allocation
                        // reserved one extra byte for this terminator
                        if (body != NULL)
                            body[bodySize] = '\0';
                        if (!readError && body != NULL && bodySize > 0)
                        {
                            ULONGLONG publishedSeconds;
                            if (ExtractPublishedTime(body, bodySize, &publishedSeconds) &&
                                publishedSeconds > GetOwnModuleLinkTimestamp() + UPDATE_CHECK_MIN_AGE_SECONDS)
                            {
                                updateFound = TRUE;
                            }
                        }

                        free(body);
                    }
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connection);
        }
        WinHttpCloseHandle(session);
    }

    if (updateFound)
        UpdateAvailable = TRUE; // UI thread reads this only after our posted message arrives

    if (HUpdateCheckNotifyWindow != NULL)
        PostMessage(HUpdateCheckNotifyWindow, WM_USER_UPDATE_CHECK_DONE, 0, 0);
    return 0;
}

void StartUpdateCheck(HWND hNotifyWindow)
{
    HUpdateCheckNotifyWindow = hNotifyWindow;
    if (InterlockedExchange(&UpdateCheckStarted, TRUE))
        return; // already attempted in this session; failures are never retried
    HANDLES(CreateThread(NULL, 0, UpdateCheckThreadF, NULL, 0, NULL));
}
