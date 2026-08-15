// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Owns the two representations of a path at a Win32 boundary.  The original
// UTF-8 display value remains owned by the caller; this type keeps its UTF-16
// display spelling separate from the API spelling so adding \\?\ never leaks
// into UI, logs, histories, or plug-in data.
class CWidePath
{
public:
    explicit CWidePath(const char* utf8Path)
        : DisplayPath(NULL), ApiPath(NULL)
    {
        if (utf8Path == NULL)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return;
        }

        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, NULL, 0);
        if (length == 0 && GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
            length = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, NULL, 0);
        if (length == 0)
            return;

        DisplayPath = (WCHAR*)malloc(length * sizeof(WCHAR));
        if (DisplayPath == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return;
        }

        int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, DisplayPath, length);
        if (converted == 0 && GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
            converted = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, DisplayPath, length);
        if (converted == 0)
        {
            DWORD error = GetLastError();
            free(DisplayPath);
            DisplayPath = NULL;
            SetLastError(error);
        }
    }

    explicit CWidePath(const WCHAR* widePath)
        : DisplayPath(Duplicate(widePath)), ApiPath(NULL)
    {
    }

    ~CWidePath()
    {
        free(DisplayPath);
        free(ApiPath);
    }

    BOOL IsValid() const { return DisplayPath != NULL; }
    const WCHAR* GetDisplayPath() const { return DisplayPath; }

    // Supplies a UTF-16 path suitable for ordinary filesystem APIs.  Only
    // paths that exceed the legacy limit are made absolute and prefixed.
    const WCHAR* GetPathForWin32Api()
    {
        if (DisplayPath == NULL)
            return NULL;
        if (ApiPath != NULL)
            return ApiPath;
        // Owned display paths are always terminated, so use the CRT length probe before normalization.
        if (IsExtendedLengthPath(DisplayPath) || wcslen(DisplayPath) < MAX_PATH)
            return DisplayPath;
        return BuildAbsoluteApiPath();
    }

    // Some APIs, notably secure DLL loading, require an absolute path even
    // when it is short.  This is the only normalization performed here.
    const WCHAR* GetFullPathForWin32Api()
    {
        if (DisplayPath == NULL)
            return NULL;
        if (ApiPath != NULL)
            return ApiPath;
        if (IsExtendedLengthPath(DisplayPath))
            return DisplayPath;
        return BuildAbsoluteApiPath();
    }

private:
    WCHAR* DisplayPath;
    WCHAR* ApiPath;

    CWidePath(const CWidePath&);
    CWidePath& operator=(const CWidePath&);

    static WCHAR* Duplicate(const WCHAR* path)
    {
        if (path == NULL)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return NULL;
        }
        // Copy the complete terminated wide path into independently owned storage.
        size_t length = wcslen(path) + 1;
        WCHAR* copy = (WCHAR*)malloc(length * sizeof(WCHAR));
        if (copy == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        memcpy(copy, path, length * sizeof(WCHAR));
        return copy;
    }

    static BOOL IsExtendedLengthPath(const WCHAR* path)
    {
        return path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\';
    }

    static BOOL IsUncPath(const WCHAR* path)
    {
        return path[0] == L'\\' && path[1] == L'\\' && !IsExtendedLengthPath(path);
    }

    const WCHAR* BuildAbsoluteApiPath()
    {
        DWORD required = GetFullPathNameW(DisplayPath, 0, NULL, NULL);
        if (required == 0)
            return NULL;

        // The current directory can change between the size probe and copy.
        // Retry with the exact size returned by Win32 rather than truncating.
        for (int attempt = 0; attempt != 4; ++attempt)
        {
            WCHAR* fullPath = (WCHAR*)malloc(required * sizeof(WCHAR));
            if (fullPath == NULL)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return NULL;
            }

            DWORD copied = GetFullPathNameW(DisplayPath, required, fullPath, NULL);
            if (copied != 0 && copied < required)
            {
                ApiPath = PrefixExtendedLengthPathIfNeeded(fullPath);
                if (ApiPath == NULL)
                    free(fullPath);
                return ApiPath;
            }

            DWORD error = GetLastError();
            free(fullPath);
            if (copied == 0)
            {
                SetLastError(error);
                return NULL;
            }
            required = copied + 1;
        }

        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return NULL;
    }

    static WCHAR* PrefixExtendedLengthPathIfNeeded(WCHAR* fullPath)
    {
        // Full paths are allocated and terminated by BuildAbsoluteApiPath.
        size_t length = wcslen(fullPath);
        if (length < MAX_PATH || IsExtendedLengthPath(fullPath))
            return fullPath;

        static const WCHAR extendedPrefix[] = L"\\\\?\\";
        static const WCHAR extendedUncPrefix[] = L"\\\\?\\UNC\\";
        const WCHAR* prefix = IsUncPath(fullPath) ? extendedUncPrefix : extendedPrefix;
        size_t prefixLength = wcslen(prefix);
        const WCHAR* suffix = IsUncPath(fullPath) ? fullPath + 2 : fullPath;
        size_t suffixLength = wcslen(suffix);
        WCHAR* prefixedPath = (WCHAR*)malloc((prefixLength + suffixLength + 1) * sizeof(WCHAR));
        if (prefixedPath == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return NULL;
        }
        memcpy(prefixedPath, prefix, prefixLength * sizeof(WCHAR));
        memcpy(prefixedPath + prefixLength, suffix, (suffixLength + 1) * sizeof(WCHAR));
        free(fullPath);
        return prefixedPath;
    }
};
