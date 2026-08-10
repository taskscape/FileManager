// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <wininet.h>

#include "checkver.h"
#include "checkver.rh"
#include "checkver.rh2"
#include "lang\lang.rh"

const char* SCRIPT_URL_FTP_EN = "ftp://ftp.taskscape.com/pub/taskscape/salamand/checkver/salupdate40_en.txt";
const char* SCRIPT_URL_FTP_CZ = "ftp://ftp.taskscape.com/pub/taskscape/salamand/checkver/salupdate40_cz.txt";
const char* SCRIPT_URL_HTTP = "https://www.taskscape.com/salupdate40/";
const char* SCRIPT_URL_HTTP_AFTERINSTALL = "https://www.taskscape.com/salupdatenew40/";

const char* AGENT_NAME = "Open Salamander CheckVer Plugin";
const char* GetInetErrorText(DWORD dError);

namespace
{
// WinINet applies the connect deadline while resolving the host as well, so
// these values keep each remote phase bounded without changing user settings.
const DWORD kResolveAndConnectTimeoutMs = 15000;
const DWORD kSendTimeoutMs = 15000;
const DWORD kReceiveTimeoutMs = 30000;

CRITICAL_SECTION DownloadHandleLock;
BOOL DownloadHandleLockInitialized = FALSE;
HINTERNET ActiveDownloadSession = NULL;
HINTERNET ActiveDownloadRequest = NULL;
volatile LONG DownloadCancelled = FALSE;
volatile LONG DownloadThreadActive = FALSE;

BOOL IsDownloadCancelled()
{
    return InterlockedCompareExchange(&DownloadCancelled, FALSE, FALSE) != FALSE;
}

BOOL RegisterActiveDownloadSession(HINTERNET session)
{
    BOOL registered = FALSE;
    EnterCriticalSection(&DownloadHandleLock);
    if (!IsDownloadCancelled())
    {
        ActiveDownloadSession = session;
        registered = TRUE;
    }
    LeaveCriticalSection(&DownloadHandleLock);

    if (!registered)
        InternetCloseHandle(session);
    return registered;
}

BOOL RegisterActiveDownloadRequest(HINTERNET request)
{
    BOOL registered = FALSE;
    EnterCriticalSection(&DownloadHandleLock);
    if (!IsDownloadCancelled())
    {
        ActiveDownloadRequest = request;
        registered = TRUE;
    }
    LeaveCriticalSection(&DownloadHandleLock);

    if (!registered)
        InternetCloseHandle(request);
    return registered;
}

void CloseOwnedDownloadRequest(HINTERNET request)
{
    BOOL closeHandle = TRUE;
    EnterCriticalSection(&DownloadHandleLock);
    if (ActiveDownloadRequest == request)
        ActiveDownloadRequest = NULL;
    else if (IsDownloadCancelled())
        closeHandle = FALSE; // cancellation already closed the sole active handle
    LeaveCriticalSection(&DownloadHandleLock);

    if (closeHandle)
        InternetCloseHandle(request);
}

void CloseOwnedDownloadSession(HINTERNET session)
{
    BOOL closeHandle = TRUE;
    EnterCriticalSection(&DownloadHandleLock);
    if (ActiveDownloadSession == session)
        ActiveDownloadSession = NULL;
    else if (IsDownloadCancelled())
        closeHandle = FALSE; // cancellation already closed the active session
    LeaveCriticalSection(&DownloadHandleLock);

    if (closeHandle)
        InternetCloseHandle(session);
}

BOOL SetDownloadTimeouts(HINTERNET session, DWORD* error)
{
    // Keep DNS/connect, request write, and response read failures independently bounded.
    DWORD resolveAndConnectTimeout = kResolveAndConnectTimeoutMs;
    DWORD sendTimeout = kSendTimeoutMs;
    DWORD receiveTimeout = kReceiveTimeoutMs;
    if (!InternetSetOption(session, INTERNET_OPTION_CONNECT_TIMEOUT, &resolveAndConnectTimeout, sizeof(resolveAndConnectTimeout)) ||
        !InternetSetOption(session, INTERNET_OPTION_SEND_TIMEOUT, &sendTimeout, sizeof(sendTimeout)) ||
        !InternetSetOption(session, INTERNET_OPTION_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout)) ||
        !InternetSetOption(session, INTERNET_OPTION_DATA_RECEIVE_TIMEOUT, &receiveTimeout, sizeof(receiveTimeout)))
    {
        *error = GetLastError();
        return FALSE;
    }
    return TRUE;
}

const char* GetInetFailureKind(DWORD error)
{
    if (error == ERROR_INTERNET_TIMEOUT)
        return "Network timeout";
    if (error == ERROR_INTERNET_LOGIN_FAILURE || error == ERROR_INTERNET_INCORRECT_PASSWORD)
        return "Authentication failure";
    if (error == ERROR_INTERNET_INVALID_URL || error == ERROR_INTERNET_PROTOCOL_NOT_FOUND ||
        error == ERROR_INTERNET_SEC_CERT_CN_INVALID || error == ERROR_INTERNET_SEC_CERT_DATE_INVALID ||
        error == ERROR_INTERNET_INVALID_CA)
        return "Protocol or TLS failure";
    return NULL;
}

void GetInetFailureText(DWORD error, char* buffer, int bufferSize)
{
    const char* kind = GetInetFailureKind(error);
    if (kind != NULL)
        _snprintf_s(buffer, bufferSize, _TRUNCATE, "%s (%lu)", kind, error);
    else
        lstrcpyn(buffer, GetInetErrorText(error), bufferSize);
}
} // namespace

// limitation - may be called from only one thread; otherwise buffer overwrites are not handled
const char* GetInetErrorText(DWORD dError)
{
    static char tempErrorText[1024];
    tempErrorText[0] = 0;

    DWORD count = FormatMessage(FORMAT_MESSAGE_FROM_HMODULE, GetModuleHandle("wininet.dll"), dError, 0,
                                tempErrorText, 1024, NULL);

    if (count > 0)
    {
        // trim garbage on the right
        char* p = tempErrorText + count - 1;
        while (p > tempErrorText && (*p == '\n' || *p == '\r' || *p == ' '))
        {
            *p = 0;
            p--;
        }
    }
    else
        lstrcpy(tempErrorText, "Unable to get error message");
    return tempErrorText;
    /*
  // hopefully we will not need this (considering the trivial internet usage)
  sprintf(szTemp, "%s error code: %d\nMessage: %s\n", szCallFunc, dError, strName);
  int response;

  if (dError == ERROR_INTERNET_EXTENDED_ERROR)
  {
    InternetGetLastResponseInfo(&dwIntError, NULL, &dwLength);
    if (dwLength)
    {
      if (!(szBuffer = (char *) LocalAlloc(LPTR, dwLength)))
      {
        lstrcat(szTemp, "Unable to allocate memory to display Internet error code. Error code: ");
        lstrcat(szTemp, _itoa(GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");

        response = MessageBox(hErr, (LPSTR)szTemp,"Error", MB_OK);
        return FALSE;
      }

      if (!InternetGetLastResponseInfo (&dwIntError, (LPTSTR) szBuffer, &dwLength))
      {
        lstrcat(szTemp, "Unable to get Internet error. Error code: ");
        lstrcat(szTemp, _itoa(GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");
        response = MessageBox(hErr, (LPSTR)szTemp, "Error", MB_OK);
        return FALSE;
      }

      if (!(szBufferFinal = (char *) LocalAlloc(LPTR, (strlen(szBuffer) + strlen(szTemp) + 1))))
      {
        lstrcat(szTemp, "Unable to allocate memory. Error code: ");
        lstrcat(szTemp, _itoa (GetLastError(), szBuffer, 10));
        lstrcat(szTemp, "\n");
        response = MessageBox(hErr, (LPSTR)szTemp, "Error", MB_OK);
        return FALSE;
      }

      lstrcpy(szBufferFinal, szTemp);
      lstrcat(szBufferFinal, szBuffer);
      LocalFree(szBuffer);
      response = MessageBox(hErr, (LPSTR)szBufferFinal, "Error", MB_OK);
      LocalFree(szBufferFinal);
    }
  }
  else
  {
    response = MessageBox(hErr, (LPSTR)szTemp,"Error",MB_OK);
  }

  return response;
*/
}

void IncMainDialogID()
{
    // neither callbacks nor trace must end up here - called from a thread which may run
    // when Salamander has long since exited
    EnterCriticalSection(&MainDialogIDSection);
    MainDialogID++;
    LeaveCriticalSection(&MainDialogIDSection);
}

DWORD
GetMainDialogID()
{
    // neither callbacks nor trace must end up here - called from a thread which may run
    // when Salamander has long since exited
    EnterCriticalSection(&MainDialogIDSection);
    DWORD id = MainDialogID;
    LeaveCriticalSection(&MainDialogIDSection);
    return id;
}

struct CTDData
{
    DWORD MainDialogID;
    BOOL FirstLoadAfterInstall;
    HANDLE Continue;
};

DWORD WINAPI ThreadDownload(void* param)
{
    CTDData* data = (CTDData*)param;
    DWORD dialogID = data->MainDialogID;
    BOOL firstLoadAfterInstall = data->FirstLoadAfterInstall;
    SetEvent(data->Continue); // let the main thread continue; from this point on the data are invalid (=NULL)
    data = NULL;

    // lock the DLL to prevent it from being unloaded while this function runs
    char buff[MAX_PATH];
    GetModuleFileName(DLLInstance, buff, MAX_PATH);
    HINSTANCE hLock = LoadLibrary(buff);

    BOOL exit = FALSE;

    DWORD errorCode = 0;
    HINTERNET hSession = NULL;
    HINTERNET hUrl = NULL;
    BOOL bResult = FALSE;
    DWORD dwBytesRead = 0;

    // is the main dialog still present and is it the one that opened us?
    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_PROTOCOL), FALSE);
        AddLogLine(LoadStr(IDS_INET_INIT), FALSE);
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        hSession = InternetOpen(AGENT_NAME, INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
        if (hSession == NULL || !RegisterActiveDownloadSession(hSession))
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID && !IsDownloadCancelled())
            {
                DWORD err = GetLastError();
                char errorText[1024];
                char buff2[1024];
                GetInetFailureText(err, errorText, sizeof(errorText));
                sprintf(buff2, LoadStr(IDS_INET_INIT_FAILED), errorText);
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
        else if (!SetDownloadTimeouts(hSession, &errorCode))
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID && !IsDownloadCancelled())
            {
                char errorText[1024];
                char buff2[1024];
                GetInetFailureText(errorCode, errorText, sizeof(errorText));
                sprintf(buff2, LoadStr(IDS_INET_INIT_FAILED), errorText);
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_CONNECT), FALSE);

        const char* scriptURL_FTP = strcmp(LoadStr(IDS_UPDATE_SCRIPT_LANG), "CZ") == 0 ? SCRIPT_URL_FTP_CZ : SCRIPT_URL_FTP_EN;

        switch (InternetProtocol)
        {
        case inetpFTPPassive:
        {
            // FTP - passive mode (gets through firewalls more often than standard FTP, but slower)
            hUrl = InternetOpenUrl(hSession, scriptURL_FTP, NULL, 0,
                                   INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_PASSIVE, 0);
            break;
        }

        case inetpFTP:
        {
            // FTP - standard mode
            hUrl = InternetOpenUrl(hSession, scriptURL_FTP, NULL, 0,
                                   INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_RELOAD, 0);
            break;
        }

        default:
        {
            // p.s. adjusted to allow version checking behind a firewall (tested at SPS)
            // HTTP (experimentally much faster than FTP-passive at SPS, probably due to HTTP caching,
            //       usually passes through firewalls)
            char scriptURL[200];
            _snprintf_s(scriptURL, _TRUNCATE, "%s?version=%s&lang=%s",
                        firstLoadAfterInstall ? SCRIPT_URL_HTTP_AFTERINSTALL : SCRIPT_URL_HTTP,
                        SalamanderTextVersion, LoadStr(IDS_UPDATE_SCRIPT_LANG));
            hUrl = InternetOpenUrl(hSession, scriptURL, NULL, 0,
                                   INTERNET_FLAG_DONT_CACHE | INTERNET_FLAG_RELOAD, 0);
            break;
        }
        }

        if (hUrl == NULL || !RegisterActiveDownloadRequest(hUrl))
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID && !IsDownloadCancelled())
            {
                DWORD err = GetLastError();
                char errorText[1024];
                char buff2[1024];
                GetInetFailureText(err, errorText, sizeof(errorText));
                sprintf(buff2, LoadStr(IDS_INET_CONNECT_FAILED), errorText);
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (dialogID == GetMainDialogID() && !exit)
    {
        AddLogLine(LoadStr(IDS_INET_READ), FALSE);
        bResult = InternetReadFile(hUrl, LoadedScript, LOADED_SCRIPT_MAX, &dwBytesRead);
        if (!bResult)
        {
            EnterCriticalSection(&MainDialogIDSection);
            if (dialogID == MainDialogID && !IsDownloadCancelled())
            {
                DWORD err = GetLastError();
                char errorText[1024];
                char buff2[1024];
                GetInetFailureText(err, errorText, sizeof(errorText));
                sprintf(buff2, LoadStr(IDS_INET_READ_FAILED), errorText);
                AddLogLine(buff2, TRUE);
            }
            LeaveCriticalSection(&MainDialogIDSection);
            exit = TRUE;
        }
    }

    if (hUrl != NULL)
        CloseOwnedDownloadRequest(hUrl);
    if (hSession != NULL)
        CloseOwnedDownloadSession(hSession);

    EnterCriticalSection(&MainDialogIDSection);
    DWORD id = MainDialogID;
    if (dialogID == GetMainDialogID())
    {
        if (!exit)
        {
            AddLogLine(LoadStr(IDS_INET_SUCCESS), FALSE);
            LoadedScriptSize = dwBytesRead;
        }
        else
            LoadedScriptSize = 0;
        PostMessage(HMainDialog, WM_USER_DOWNLOADTHREAD_EXIT, !exit, 0); // thread ends; data are loaded
        InterlockedExchange(&DownloadThreadActive, FALSE);
        FreeLibrary(hLock);                                              // release the lock
        LeaveCriticalSection(&MainDialogIDSection);
        return 0; // let the thread die naturally
    }
    else
    {
        LeaveCriticalSection(&MainDialogIDSection);
        // we were killed from the outside - after FreeLibrary the last lock on the SPL may be removed
        // (Salamander may no longer be running) and there would be nowhere to return to,
        // therefore call this function:
        InterlockedExchange(&DownloadThreadActive, FALSE);
        FreeLibraryAndExitThread(hLock, 3666);
        return 0; // we never return here, but the compiler cannot know that :-)
    }
}

HANDLE
StartDownloadThread(BOOL firstLoadAfterInstall)
{
    // Do not recycle the shared cancellation token until a detached worker has
    // observed it and released its WinINet handles.
    if (InterlockedCompareExchange(&DownloadThreadActive, TRUE, FALSE) != FALSE)
        return NULL;

    CTDData data;
    data.MainDialogID = GetMainDialogID();
    data.FirstLoadAfterInstall = firstLoadAfterInstall;
    data.Continue = CreateEvent(NULL, FALSE, FALSE, NULL);

    if (data.Continue == NULL)
    {
        TRACE_E("Unable to create Continue event.");
        InterlockedExchange(&DownloadThreadActive, FALSE);
        return NULL;
    }

    if (!DownloadHandleLockInitialized)
    {
        // A single token owns the active WinINet handle for this one-at-a-time download.
        InitializeCriticalSection(&DownloadHandleLock);
        DownloadHandleLockInitialized = TRUE;
    }
    InterlockedExchange(&DownloadCancelled, FALSE);

    DWORD threadID;
    HANDLE hThread = CreateThread(NULL, 0, ThreadDownload, &data, 0, &threadID);
    if (hThread == NULL)
    {
        TRACE_E("Unable to create Check Version Download thread.");
        InterlockedExchange(&DownloadThreadActive, FALSE);
    }
    else // wait until the thread takes the data
        WaitForSingleObject(data.Continue, INFINITE);

    CloseHandle(data.Continue);

    return hThread;
}

void CancelDownloadThread()
{
    InterlockedExchange(&DownloadCancelled, TRUE);

    HINTERNET session = NULL;
    HINTERNET request = NULL;
    EnterCriticalSection(&DownloadHandleLock);
    session = ActiveDownloadSession;
    request = ActiveDownloadRequest;
    ActiveDownloadSession = NULL;
    ActiveDownloadRequest = NULL;
    LeaveCriticalSection(&DownloadHandleLock);

    // Closing both WinINet levels interrupts a DNS/connect/read wait; the
    // worker sees the same token before it can begin a later phase.
    if (request != NULL)
        InternetCloseHandle(request);
    if (session != NULL)
        InternetCloseHandle(session);
}
