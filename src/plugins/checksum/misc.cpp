// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "checksum.h"
#include "checksum.rh"
#include "checksum.rh2"
#include "lang\lang.rh"
#include "misc.h"

BOOL Error(HWND hParent, int lastErr, int title, int error, ...)
{
    CALL_STACK_MESSAGE4("Error(, %d, %d, %d, ...)", lastErr, title, error);
    char buf[1024];
    *buf = 0;
    va_list arglist;
    va_start(arglist, error);
    vsprintf(buf, LoadStr(error), arglist);
    va_end(arglist);
    if (lastErr != ERROR_SUCCESS)
    {
        strcat(buf, " ");
        size_t l = strlen(buf);
        // Salamander's message box expects UTF-8 localized system text.
        SalamanderGeneral->GetErrorText(lastErr, buf + l, static_cast<int>(1024 - l));
    }
    SalamanderGeneral->SalMessageBox(hParent, buf, LoadStr(title), MSGBOXEX_OK | MSGBOXEX_ICONEXCLAMATION);

    return FALSE;
}

BOOL SafeReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytesToRead, DWORD* pnBytesRead, char* fileName,
                  HWND parent, BOOL* skippedReadError, BOOL* skipAllReadErrors)
{
    if (skippedReadError != NULL)
        *skippedReadError = FALSE;
    while (!ReadFile(hFile, lpBuffer, nBytesToRead, pnBytesRead, NULL))
    {
        if (skippedReadError != NULL && *skipAllReadErrors)
        {
            *skippedReadError = TRUE;
            return FALSE;
        }
        int lastErr = GetLastError();
        char error[1024];
        // DialogError renders diagnostics as UTF-8.
        SalamanderGeneral->GetErrorText(lastErr, error, _countof(error));
        DWORD buttons = skippedReadError == NULL ? BUTTONS_RETRYCANCEL : BUTTONS_RETRYSKIPCANCEL;
        int result = SalamanderGeneral->DialogError(parent, buttons, fileName, error, LoadStr(IDS_READERROR));
        switch (result)
        {
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;

        case DIALOG_RETRY:
            continue;

        case DIALOG_SKIPALL:
            *skipAllReadErrors = TRUE;
        case DIALOG_SKIP:
            *skippedReadError = TRUE;
            return FALSE;
        }
    }
    return TRUE;
}

BOOL SafeWriteFile(HANDLE hFile, LPVOID lpBuffer, DWORD nBytesToWrite, DWORD* pnBytesWritten,
                   char* fileName, HWND parent)
{
    while (!WriteFile(hFile, lpBuffer, nBytesToWrite, pnBytesWritten, NULL))
    {
        int lastErr = GetLastError();
        char error[1024];
        // DialogError renders diagnostics as UTF-8.
        SalamanderGeneral->GetErrorText(lastErr, error, _countof(error));
        if (SalamanderGeneral->DialogError(parent, BUTTONS_RETRYCANCEL, fileName, error,
                                           LoadStr(IDS_WRITEERROR)) != DIALOG_RETRY)
            return FALSE;
    }
    return TRUE;
}

BOOL SafeOpenCreateFileUtf8Local(LPCTSTR fileName, DWORD desiredAccess, DWORD shareMode, DWORD creationDisposition,
                        DWORD flagsAndAttributes, HANDLE* hFile, BOOL* skip, int* silent, HWND parent)
{
    CALL_STACK_MESSAGE6("SafeOpenCreateFileUtf8Local(%s, 0x%X, 0x%X, 0x%X, 0x%X, , , )", fileName, desiredAccess,
                        shareMode, creationDisposition, flagsAndAttributes);

    while ((*hFile = CreateFileUtf8Local(fileName, desiredAccess, shareMode, NULL, creationDisposition,
                                flagsAndAttributes, NULL)) == INVALID_HANDLE_VALUE &&
           ((silent != NULL) ? !*silent : 1))
    {
        int lastErr = GetLastError();
        char error[1024];
        strcpy(error, LoadStr(IDS_ERROROPENING));
        strcat(error, ": ");
        size_t l = strlen(error);
        // DialogError renders diagnostics as UTF-8.
        SalamanderGeneral->GetErrorText(lastErr, error + l, static_cast<int>(1024 - l));
        int result;
        if (skip == NULL)
            result = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYCANCEL, fileName, error, NULL);
        else
            result = SalamanderGeneral->DialogError(parent, BUTTONS_RETRYSKIPCANCEL, fileName, error, NULL);
        switch (result)
        {
        case DIALOG_CANCEL:
        case DIALOG_FAIL:
            return FALSE;

        case DIALOG_RETRY:
            continue;

        case DIALOG_SKIPALL:
            *silent = 1;
        case DIALOG_SKIP:
            if (skip != NULL)
                *skip = TRUE;
            return TRUE;
        }
    }
    if (skip != NULL)
        *skip = (*hFile == INVALID_HANDLE_VALUE);
    return TRUE;
}

void GetFirstWord(char* str, int& pos, int& len, char delimitChar)
{
    CALL_STACK_MESSAGE_NONE // frequently called function
        // CALL_STACK_MESSAGE4("GetFirstWord(, %d, %d, %d)", pos, len, delimitChar);
        pos = 0;
    while (str[pos] && ((BYTE)str[pos] <= ' '))
        pos++;
    len = pos;
    while (str[len] && ((BYTE)str[len] > ' ') && str[len] != delimitChar)
        len++;
    len -= pos;
}

void GetLastWord(char* str, int& pos, int& len, char delimitChar)
{
    CALL_STACK_MESSAGE_NONE // frequently called function
        // CALL_STACK_MESSAGE4("GetLastWord(, %d, %d, %d)", pos, len, delimitChar);
        len = (int)strlen(str);
    while (len > 0 && ((BYTE)str[len - 1] <= ' '))
        len--;
    pos = len;
    while (pos > 0 && ((BYTE)str[pos - 1] > ' ') && str[pos - 1] != delimitChar)
        pos--;
    len -= pos;
}

BYTE hex(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return 0;
}

BOOL IsHex(const char* str, int len)
{
    CALL_STACK_MESSAGE2("IsHex(, %d)", len);
    for (int i = 0; i < len; i++, str++)
        if (!((*str >= '0' && *str <= '9') || (*str >= 'A' && *str <= 'F') || (*str >= 'a' && *str <= 'f')))
            return FALSE;
    return TRUE;
}
