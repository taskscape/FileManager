// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <stdarg.h>

//silent flags
#define SF_TOOLONGNAMES 0x0001
#define SF_CREATEFILE 0x0002
#define SF_WRITEFILE 0x0004
#define SF_BADDATA 0x0008
#define SF_BADMETHOD 0x0010
#define SF_SKIPALL 0x0020
#define SF_OVEWRITEALL 0x0040
#define SF_OVEWRITEALL_RO 0x0080
#define SF_SKIPALLCRYPT 0x0100

//multivol modes
#define MV_DETACH_ARCHIVE 1
#define MV_PROCESS_ARCHIVE 2

extern HANDLE Heap;
extern bool RemoveTemp;
extern const char* Title;
extern bool SafeMode;
extern HINSTANCE HInstance;
void CloseMapping();
int HandleError(CStringIndex message, unsigned long err, char* fileName = NULL, bool* retry = NULL, unsigned silentFlag = 0, bool noSkip = false);

static BOOL SelfExtrAppendCharacter(char* buffer, size_t bufferSize, size_t* used, char value)
{
    if (*used + 1 >= bufferSize)
        return FALSE;
    buffer[(*used)++] = value;
    buffer[*used] = 0;
    return TRUE;
}

static BOOL SelfExtrAppendText(char* buffer, size_t bufferSize, size_t* used, const char* text)
{
    while (*text != 0)
    {
        if (!SelfExtrAppendCharacter(buffer, bufferSize, used, *text++))
            return FALSE;
    }
    return TRUE;
}

static BOOL SelfExtrAppendUnsigned(char* buffer, size_t bufferSize, size_t* used, unsigned value, unsigned base, size_t width, char padding)
{
    static const char digits[] = "0123456789ABCDEF";
    char reverse[16];
    size_t length = 0;
    do
    {
        reverse[length++] = digits[value % base];
        value /= base;
    } while (value != 0);
    while (length < width)
    {
        if (!SelfExtrAppendCharacter(buffer, bufferSize, used, padding))
            return FALSE;
        width--;
    }
    while (length > 0)
    {
        if (!SelfExtrAppendCharacter(buffer, bufferSize, used, reverse[--length]))
            return FALSE;
    }
    return TRUE;
}

static BOOL SelfExtrVFormat(char* buffer, size_t bufferSize, const char* format, va_list args)
{
    // The standalone SFX is CRT-free; support its bounded %s/%d/%u/%x/%X formats without wsprintf.
    size_t used = 0;
    buffer[0] = 0;
    while (*format != 0)
    {
        if (*format != '%')
        {
            if (!SelfExtrAppendCharacter(buffer, bufferSize, &used, *format++))
                return FALSE;
            continue;
        }
        format++;
        if (*format == '%')
        {
            if (!SelfExtrAppendCharacter(buffer, bufferSize, &used, *format++))
                return FALSE;
            continue;
        }
        char padding = ' ';
        size_t width = 0;
        if (*format == '0')
        {
            padding = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9')
            width = width * 10 + (size_t)(*format++ - '0');
        if (*format == 's')
        {
            const char* text = va_arg(args, const char*);
            if (!SelfExtrAppendText(buffer, bufferSize, &used, text != NULL ? text : "(null)"))
                return FALSE;
        }
        else if (*format == 'd' || *format == 'i')
        {
            int value = va_arg(args, int);
            if (value < 0 && !SelfExtrAppendCharacter(buffer, bufferSize, &used, '-'))
                return FALSE;
            unsigned magnitude = value < 0 ? (unsigned)(-(value + 1)) + 1 : (unsigned)value;
            if (!SelfExtrAppendUnsigned(buffer, bufferSize, &used, magnitude, 10, width, padding))
                return FALSE;
        }
        else if (*format == 'u' || *format == 'x' || *format == 'X')
        {
            unsigned value = va_arg(args, unsigned);
            if (!SelfExtrAppendUnsigned(buffer, bufferSize, &used, value, *format == 'u' ? 10 : 16, width, padding))
                return FALSE;
        }
        else
        {
            return FALSE;
        }
        format++;
    }
    return TRUE;
}

static BOOL SelfExtrFormat(char* buffer, size_t bufferSize, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    BOOL result = SelfExtrVFormat(buffer, bufferSize, format, args);
    va_end(args);
    return result;
}

static BOOL SelfExtrFormatUserDateTime(const SYSTEMTIME* time, DWORD flags, char* buffer, int bufferSize, BOOL isDate)
{
    // Keep the standalone SFX UTF-8 while obtaining user-visible text through locale-name APIs.
    WCHAR localeName[LOCALE_NAME_MAX_LENGTH];
    WCHAR formatted[50];
    if (GetUserDefaultLocaleName(localeName, ARRAYSIZE(localeName)) == 0)
        return FALSE;
    int length = isDate
                     ? GetDateFormatEx(localeName, flags, time, NULL, formatted, ARRAYSIZE(formatted), NULL)
                     : GetTimeFormatEx(localeName, flags, time, NULL, formatted, ARRAYSIZE(formatted));
    return length != 0 && WideCharToMultiByte(CP_UTF8, 0, formatted, -1, buffer, bufferSize, NULL, NULL) != 0;
}

#ifndef SALAMANDER_UTF8_WINAPI_SELFEXTR
#define SALAMANDER_UTF8_WINAPI_SELFEXTR

static WCHAR* SelfExtrAllocWideFromUtf8(const char* src)
{
    if (src == NULL)
        return NULL;
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, NULL, 0);
    if (len <= 0)
        return NULL;
    WCHAR* buf = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, sizeof(WCHAR) * len);
    if (buf == NULL)
        return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src, -1, buf, len) == 0)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return NULL;
    }
    return buf;
}

static BOOL SelfExtrWideToUtf8Buffer(const WCHAR* src, char* dst, int dstSize)
{
    if (dst == NULL || dstSize <= 0)
        return FALSE;
    if (src == NULL)
    {
        dst[0] = 0;
        return TRUE;
    }
    int len = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, dstSize, NULL, NULL);
    if (len == 0)
        dst[0] = 0;
    return len != 0;
}

static BOOL SelfExtrConvertFindDataWToUtf8(const WIN32_FIND_DATAW* src, WIN32_FIND_DATAA* dst)
{
    if (dst == NULL || src == NULL)
        return FALSE;
    // This CRT-free SFX target cannot link the compiler-generated memset used by ZeroMemory for this structure.
    volatile BYTE* dstBytes = (volatile BYTE*)dst;
    for (size_t i = 0; i < sizeof(*dst); i++)
        dstBytes[i] = 0;
    dst->dwFileAttributes = src->dwFileAttributes;
    dst->ftCreationTime = src->ftCreationTime;
    dst->ftLastAccessTime = src->ftLastAccessTime;
    dst->ftLastWriteTime = src->ftLastWriteTime;
    dst->nFileSizeHigh = src->nFileSizeHigh;
    dst->nFileSizeLow = src->nFileSizeLow;
    dst->dwReserved0 = src->dwReserved0;
    dst->dwReserved1 = src->dwReserved1;
    SelfExtrWideToUtf8Buffer(src->cFileName, dst->cFileName, (int)sizeof(dst->cFileName));
    SelfExtrWideToUtf8Buffer(src->cAlternateFileName, dst->cAlternateFileName, (int)sizeof(dst->cAlternateFileName));
    return TRUE;
}

static HANDLE CreateFileUtf8Local(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                  LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                                  DWORD flagsAndAttributes, HANDLE templateFile)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = SelfExtrAllocWideFromUtf8(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    h = CreateFileW(fileNameW, desiredAccess, shareMode, securityAttributes,
                    creationDisposition, flagsAndAttributes, templateFile);
    HeapFree(GetProcessHeap(), 0, fileNameW);
    return h;
}

static BOOL DeleteFileUtf8Local(const char* fileName)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = SelfExtrAllocWideFromUtf8(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = DeleteFileW(fileNameW);
    HeapFree(GetProcessHeap(), 0, fileNameW);
    return ok;
}

static BOOL MoveFileUtf8Local(const char* srcName, const char* destName)
{
    BOOL ok = FALSE;
    WCHAR* srcNameW = SelfExtrAllocWideFromUtf8(srcName);
    WCHAR* destNameW = SelfExtrAllocWideFromUtf8(destName);
    if (srcNameW == NULL || destNameW == NULL)
    {
        if (srcNameW != NULL)
            HeapFree(GetProcessHeap(), 0, srcNameW);
        if (destNameW != NULL)
            HeapFree(GetProcessHeap(), 0, destNameW);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = MoveFileW(srcNameW, destNameW);
    HeapFree(GetProcessHeap(), 0, srcNameW);
    HeapFree(GetProcessHeap(), 0, destNameW);
    return ok;
}

static BOOL CopyFileUtf8Local(const char* existingFileName, const char* newFileName, BOOL failIfExists)
{
    BOOL ok = FALSE;
    WCHAR* existingW = SelfExtrAllocWideFromUtf8(existingFileName);
    WCHAR* newW = SelfExtrAllocWideFromUtf8(newFileName);
    if (existingW == NULL || newW == NULL)
    {
        if (existingW != NULL)
            HeapFree(GetProcessHeap(), 0, existingW);
        if (newW != NULL)
            HeapFree(GetProcessHeap(), 0, newW);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = CopyFileW(existingW, newW, failIfExists);
    HeapFree(GetProcessHeap(), 0, existingW);
    HeapFree(GetProcessHeap(), 0, newW);
    return ok;
}

static DWORD GetFileAttributesUtf8Local(const char* fileName)
{
    DWORD attrs = INVALID_FILE_ATTRIBUTES;
    WCHAR* fileNameW = SelfExtrAllocWideFromUtf8(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return attrs;
    }
    attrs = GetFileAttributesW(fileNameW);
    HeapFree(GetProcessHeap(), 0, fileNameW);
    return attrs;
}

static BOOL SetFileAttributesUtf8Local(const char* fileName, DWORD attrs)
{
    BOOL ok = FALSE;
    WCHAR* fileNameW = SelfExtrAllocWideFromUtf8(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = SetFileAttributesW(fileNameW, attrs);
    HeapFree(GetProcessHeap(), 0, fileNameW);
    return ok;
}

static BOOL CreateDirectoryUtf8Local(const char* dirName, LPSECURITY_ATTRIBUTES securityAttributes)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = SelfExtrAllocWideFromUtf8(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = CreateDirectoryW(dirNameW, securityAttributes);
    HeapFree(GetProcessHeap(), 0, dirNameW);
    return ok;
}

static BOOL RemoveDirectoryUtf8Local(const char* dirName)
{
    BOOL ok = FALSE;
    WCHAR* dirNameW = SelfExtrAllocWideFromUtf8(dirName);
    if (dirNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ok = RemoveDirectoryW(dirNameW);
    HeapFree(GetProcessHeap(), 0, dirNameW);
    return ok;
}

static HANDLE FindFirstFileUtf8Local(const char* fileName, WIN32_FIND_DATAA* findFileData)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    WCHAR* fileNameW = SelfExtrAllocWideFromUtf8(fileName);
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    WIN32_FIND_DATAW dataW;
    h = FindFirstFileW(fileNameW, &dataW);
    HeapFree(GetProcessHeap(), 0, fileNameW);
    if (h == INVALID_HANDLE_VALUE)
        return h;
    if (findFileData != NULL)
        SelfExtrConvertFindDataWToUtf8(&dataW, findFileData);
    return h;
}

static BOOL FindNextFileUtf8Local(HANDLE hFindFile, WIN32_FIND_DATAA* findFileData)
{
    WIN32_FIND_DATAW dataW;
    if (!FindNextFileW(hFindFile, &dataW))
        return FALSE;
    if (findFileData != NULL)
        SelfExtrConvertFindDataWToUtf8(&dataW, findFileData);
    return TRUE;
}

#ifndef SALAMANDER_WINAPI_MACROS_GUARD
#define SALAMANDER_WINAPI_MACROS_GUARD
#ifdef CreateFile
#pragma push_macro("CreateFile")
#undef CreateFile
#define SALAMANDER_POP_CREATEFILE
#endif
#ifdef DeleteFile
#pragma push_macro("DeleteFile")
#undef DeleteFile
#define SALAMANDER_POP_DELETEFILE
#endif
#ifdef MoveFile
#pragma push_macro("MoveFile")
#undef MoveFile
#define SALAMANDER_POP_MOVEFILE
#endif
#ifdef CopyFile
#pragma push_macro("CopyFile")
#undef CopyFile
#define SALAMANDER_POP_COPYFILE
#endif
#ifdef GetFileAttributes
#pragma push_macro("GetFileAttributes")
#undef GetFileAttributes
#define SALAMANDER_POP_GETFILEATTRIBUTES
#endif
#ifdef SetFileAttributes
#pragma push_macro("SetFileAttributes")
#undef SetFileAttributes
#define SALAMANDER_POP_SETFILEATTRIBUTES
#endif
#ifdef CreateDirectory
#pragma push_macro("CreateDirectory")
#undef CreateDirectory
#define SALAMANDER_POP_CREATEDIRECTORY
#endif
#ifdef RemoveDirectory
#pragma push_macro("RemoveDirectory")
#undef RemoveDirectory
#define SALAMANDER_POP_REMOVEDIRECTORY
#endif
#ifdef FindFirstFile
#pragma push_macro("FindFirstFile")
#undef FindFirstFile
#define SALAMANDER_POP_FINDFIRSTFILE
#endif
#ifdef FindNextFile
#pragma push_macro("FindNextFile")
#undef FindNextFile
#define SALAMANDER_POP_FINDNEXTFILE
#endif
#endif // SALAMANDER_WINAPI_MACROS_GUARD
#define CreateFile CreateFileUtf8Local
#define DeleteFile DeleteFileUtf8Local
#define MoveFile MoveFileUtf8Local
#define CopyFile CopyFileUtf8Local
#define GetFileAttributes GetFileAttributesUtf8Local
#define SetFileAttributes SetFileAttributesUtf8Local
#define CreateDirectory CreateDirectoryUtf8Local
#define RemoveDirectory RemoveDirectoryUtf8Local
#define FindFirstFile FindFirstFileUtf8Local
#define FindNextFile FindNextFileUtf8Local

#ifdef SALAMANDER_RESTORE_WINAPI_MACROS
#ifdef SALAMANDER_POP_CREATEFILE
#pragma pop_macro("CreateFile")
#undef SALAMANDER_POP_CREATEFILE
#endif
#ifdef SALAMANDER_POP_DELETEFILE
#pragma pop_macro("DeleteFile")
#undef SALAMANDER_POP_DELETEFILE
#endif
#ifdef SALAMANDER_POP_MOVEFILE
#pragma pop_macro("MoveFile")
#undef SALAMANDER_POP_MOVEFILE
#endif
#ifdef SALAMANDER_POP_COPYFILE
#pragma pop_macro("CopyFile")
#undef SALAMANDER_POP_COPYFILE
#endif
#ifdef SALAMANDER_POP_GETFILEATTRIBUTES
#pragma pop_macro("GetFileAttributes")
#undef SALAMANDER_POP_GETFILEATTRIBUTES
#endif
#ifdef SALAMANDER_POP_SETFILEATTRIBUTES
#pragma pop_macro("SetFileAttributes")
#undef SALAMANDER_POP_SETFILEATTRIBUTES
#endif
#ifdef SALAMANDER_POP_CREATEDIRECTORY
#pragma pop_macro("CreateDirectory")
#undef SALAMANDER_POP_CREATEDIRECTORY
#endif
#ifdef SALAMANDER_POP_REMOVEDIRECTORY
#pragma pop_macro("RemoveDirectory")
#undef SALAMANDER_POP_REMOVEDIRECTORY
#endif
#ifdef SALAMANDER_POP_FINDFIRSTFILE
#pragma pop_macro("FindFirstFile")
#undef SALAMANDER_POP_FINDFIRSTFILE
#endif
#ifdef SALAMANDER_POP_FINDNEXTFILE
#pragma pop_macro("FindNextFile")
#undef SALAMANDER_POP_FINDNEXTFILE
#endif
#endif // SALAMANDER_RESTORE_WINAPI_MACROS
#endif // SALAMANDER_UTF8_WINAPI_SELFEXTR

#ifdef EXT_VER
CStringIndex MapFile(const char* name, HANDLE* file, HANDLE* mapping,
                     const unsigned char** data, unsigned long* size, int* err);
void UnmapFile(HANDLE file, HANDLE mapping, const unsigned char* data);
#endif //EXT_VER

#ifdef _DEBUG
void Trace(char* message, ...);
#define TRACE1(a1) Trace(a1);
#define TRACE2(a1, a2) Trace(a1, a2);
#define TRACE3(a1, a2, a3) Trace(a1, a2, a3);
#define TRACE4(a1, a2, a3, a4) Trace(a1, a2, a3, a4);
#define TRACE5(a1, a2, a3, a4, a5) Trace(a1, a2, a3, a5);

#else //_DEBUG
#define TRACE1(a1) ;
#define TRACE2(a1, a2) ;
#define TRACE3(a1, a2, a3) ;
#define TRACE4(a1, a2, a3, a4) ;
#define TRACE5(a1, a2, a3, a4, a5) ;

#endif //_DEBUG

