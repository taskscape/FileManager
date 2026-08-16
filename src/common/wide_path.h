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

// Dynamic UTF-16 path class that manages owned, resizeable WCHAR buffers
// without buffer length limits (supporting up to 32,767 characters for Win32 long paths).
class CPathW
{
public:
    CPathW()
        : Buffer(NULL), Capacity(0), Length(0), ApiPath(NULL)
    {
        EnsureCapacity(MAX_PATH);
    }

    explicit CPathW(const WCHAR* path)
        : Buffer(NULL), Capacity(0), Length(0), ApiPath(NULL)
    {
        Set(path);
    }

    explicit CPathW(const char* utf8Path)
        : Buffer(NULL), Capacity(0), Length(0), ApiPath(NULL)
    {
        Set(utf8Path);
    }

    CPathW(const CPathW& other)
        : Buffer(NULL), Capacity(0), Length(0), ApiPath(NULL)
    {
        Set(other.CStr());
    }

    CPathW& operator=(const CPathW& other)
    {
        if (this != &other)
        {
            Set(other.CStr());
        }
        return *this;
    }

    CPathW& operator=(const WCHAR* path)
    {
        Set(path);
        return *this;
    }

    CPathW& operator=(const char* utf8Path)
    {
        Set(utf8Path);
        return *this;
    }

    ~CPathW()
    {
        free(Buffer);
        free(ApiPath);
    }

    void Clear()
    {
        ClearApiPath();
        if (Buffer != NULL && Capacity > 0)
        {
            Buffer[0] = L'\0';
            Length = 0;
        }
    }

    BOOL IsEmpty() const
    {
        return Length == 0;
    }

    size_t GetLength() const
    {
        return Length;
    }

    size_t GetCapacity() const
    {
        return Capacity;
    }

    const WCHAR* CStr() const
    {
        return Buffer != NULL ? Buffer : L"";
    }

    const WCHAR* GetDisplayPath() const
    {
        return CStr();
    }

    operator const WCHAR*() const
    {
        return CStr();
    }

    BOOL Set(const WCHAR* widePath)
    {
        ClearApiPath();
        if (widePath == NULL)
        {
            Clear();
            return TRUE;
        }
        size_t len = wcslen(widePath);
        if (!EnsureCapacity(len + 1))
            return FALSE;
        memcpy(Buffer, widePath, (len + 1) * sizeof(WCHAR));
        Length = len;
        return TRUE;
    }

    BOOL Set(const char* utf8Path)
    {
        ClearApiPath();
        if (utf8Path == NULL)
        {
            Clear();
            return TRUE;
        }
        int req = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, NULL, 0);
        if (req == 0 && GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
            req = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, NULL, 0);
        if (req <= 0)
        {
            Clear();
            return FALSE;
        }
        if (!EnsureCapacity(req))
            return FALSE;
        int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Path, -1, Buffer, (int)Capacity);
        if (converted == 0 && GetLastError() == ERROR_NO_UNICODE_TRANSLATION)
            converted = MultiByteToWideChar(CP_UTF8, 0, utf8Path, -1, Buffer, (int)Capacity);
        if (converted <= 0)
        {
            Clear();
            return FALSE;
        }
        Length = (size_t)(converted - 1);
        return TRUE;
    }

    WCHAR* GetBuffer(size_t minCapacityInChars)
    {
        ClearApiPath();
        if (!EnsureCapacity(minCapacityInChars))
            return NULL;
        return Buffer;
    }

    void ReleaseBuffer(int newLength = -1)
    {
        ClearApiPath();
        if (Buffer == NULL)
            return;
        if (newLength < 0)
        {
            Length = wcslen(Buffer);
        }
        else
        {
            Length = (size_t)newLength;
            if (Length < Capacity)
                Buffer[Length] = L'\0';
        }
    }

    BOOL Append(const WCHAR* subPath)
    {
        ClearApiPath();
        if (subPath == NULL || *subPath == L'\0')
            return TRUE;

        if (*subPath == L'\\' || *subPath == L'/')
            subPath++;

        if (Length > 0 && (Buffer[Length - 1] == L'\\' || Buffer[Length - 1] == L'/'))
        {
            Length--;
            Buffer[Length] = L'\0';
        }

        size_t subLen = wcslen(subPath);
        size_t needed = Length + (Length > 0 ? 1 : 0) + subLen + 1;
        if (!EnsureCapacity(needed))
            return FALSE;

        if (Length > 0)
        {
            Buffer[Length] = L'\\';
            Length++;
        }

        memcpy(Buffer + Length, subPath, (subLen + 1) * sizeof(WCHAR));
        Length += subLen;
        return TRUE;
    }

    BOOL Append(const char* utf8SubPath)
    {
        if (utf8SubPath == NULL || *utf8SubPath == '\0')
            return TRUE;
        CPathW sub(utf8SubPath);
        return Append(sub.CStr());
    }

    BOOL AddBackslash()
    {
        ClearApiPath();
        if (Length > 0 && Buffer[Length - 1] != L'\\' && Buffer[Length - 1] != L'/')
        {
            if (!EnsureCapacity(Length + 2))
                return FALSE;
            Buffer[Length] = L'\\';
            Buffer[Length + 1] = L'\0';
            Length++;
        }
        return TRUE;
    }

    void RemoveBackslash()
    {
        ClearApiPath();
        if (Length > 0 && (Buffer[Length - 1] == L'\\' || Buffer[Length - 1] == L'/'))
        {
            Buffer[Length - 1] = L'\0';
            Length--;
        }
    }

    void StripPath()
    {
        ClearApiPath();
        if (Length == 0)
            return;
        WCHAR* name = wcsrchr(Buffer, L'\\');
        WCHAR* forwardName = wcsrchr(Buffer, L'/');
        if (forwardName != NULL && (name == NULL || forwardName > name))
            name = forwardName;
        if (name != NULL)
        {
            size_t nameLen = wcslen(name + 1);
            memmove(Buffer, name + 1, (nameLen + 1) * sizeof(WCHAR));
            Length = nameLen;
        }
    }

    void RemoveExtension()
    {
        ClearApiPath();
        if (Length == 0)
            return;
        for (size_t i = Length; i > 0; --i)
        {
            WCHAR ch = Buffer[i - 1];
            if (ch == L'.')
            {
                Buffer[i - 1] = L'\0';
                Length = i - 1;
                break;
            }
            if (ch == L'\\' || ch == L'/')
                break;
        }
    }

    BOOL AddExtension(const WCHAR* extension)
    {
        ClearApiPath();
        if (extension == NULL || *extension == L'\0')
            return TRUE;

        for (size_t i = Length; i > 0; --i)
        {
            WCHAR ch = Buffer[i - 1];
            if (ch == L'.')
                return TRUE;
            if (ch == L'\\' || ch == L'/')
                break;
        }

        size_t extLen = wcslen(extension);
        if (!EnsureCapacity(Length + extLen + 1))
            return FALSE;

        memcpy(Buffer + Length, extension, (extLen + 1) * sizeof(WCHAR));
        Length += extLen;
        return TRUE;
    }

    BOOL AddExtension(const char* utf8Ext)
    {
        if (utf8Ext == NULL || *utf8Ext == '\0')
            return TRUE;
        CPathW ext(utf8Ext);
        return AddExtension(ext.CStr());
    }

    BOOL RenameExtension(const WCHAR* extension)
    {
        RemoveExtension();
        return AddExtension(extension);
    }

    BOOL RenameExtension(const char* utf8Ext)
    {
        RemoveExtension();
        return AddExtension(utf8Ext);
    }

    const WCHAR* FindFileName() const
    {
        if (Length <= 1)
            return CStr();
        for (size_t i = Length - 1; i > 0; --i)
        {
            WCHAR ch = Buffer[i - 1];
            if (ch == L'\\' || ch == L'/')
                return Buffer + i;
        }
        return CStr();
    }

    BOOL RemovePoints()
    {
        ClearApiPath();
        if (Buffer == NULL || Length == 0)
            return TRUE;

        WCHAR* afterRoot = Buffer;
        if (Length >= 3 && Buffer[1] == L':' && (Buffer[2] == L'\\' || Buffer[2] == L'/'))
        {
            afterRoot = Buffer + 3;
        }
        else if (Length >= 2 && Buffer[0] == L'\\' && Buffer[1] == L'\\')
        {
            const WCHAR* p = Buffer + 2;
            int slashCount = 0;
            while (*p != L'\0')
            {
                if (*p == L'\\' || *p == L'/')
                {
                    slashCount++;
                    if (slashCount == 2)
                    {
                        afterRoot = (WCHAR*)(p + 1);
                        break;
                    }
                }
                p++;
            }
        }

        WCHAR* d = afterRoot;
        while (*d != L'\0')
        {
            while (*d != L'\0' && *d != L'.')
                d++;
            if (*d == L'.')
            {
                if (d == afterRoot || (d > afterRoot && (*(d - 1) == L'\\' || *(d - 1) == L'/')))
                {
                    if (*(d + 1) == L'.' && (*(d + 2) == L'\\' || *(d + 2) == L'/' || *(d + 2) == L'\0'))
                    {
                        WCHAR* l = d - 1;
                        while (l > afterRoot && *(l - 1) != L'\\' && *(l - 1) != L'/')
                            l--;
                        if (l >= afterRoot)
                        {
                            if (*(d + 2) == L'\0')
                                *l = L'\0';
                            else
                                memmove(l, d + 3, (wcslen(d + 3) + 1) * sizeof(WCHAR));
                            d = l;
                        }
                        else
                        {
                            return FALSE;
                        }
                    }
                    else if (*(d + 1) == L'\\' || *(d + 1) == L'/' || *(d + 1) == L'\0')
                    {
                        if (*(d + 1) == L'\0')
                            *d = L'\0';
                        else
                            memmove(d, d + 2, (wcslen(d + 2) + 1) * sizeof(WCHAR));
                    }
                    else
                    {
                        d++;
                    }
                }
                else
                {
                    d++;
                }
            }
        }
        Length = wcslen(Buffer);
        return TRUE;
    }

    const WCHAR* GetPathForWin32Api()
    {
        if (Buffer == NULL)
            return NULL;
        if (ApiPath != NULL)
            return ApiPath;
        if (IsExtendedLengthPath(Buffer) || Length < MAX_PATH)
            return Buffer;
        return BuildAbsoluteApiPath();
    }

    const WCHAR* GetFullPathForWin32Api()
    {
        if (Buffer == NULL)
            return NULL;
        if (ApiPath != NULL)
            return ApiPath;
        if (IsExtendedLengthPath(Buffer))
            return Buffer;
        return BuildAbsoluteApiPath();
    }

    char* ToUtf8Alloc() const
    {
        if (Buffer == NULL)
            return NULL;
        int req = WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, NULL, 0, NULL, NULL);
        if (req < 0)
            return NULL;
        char* str = (char*)malloc(req + 1);
        if (str == NULL)
            return NULL;
        if (req > 0)
            WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, str, req, NULL, NULL);
        str[req] = '\0';
        return str;
    }

    BOOL ToUtf8(char* buffer, size_t bufferCapacityInBytes) const
    {
        if (buffer == NULL || bufferCapacityInBytes == 0)
            return FALSE;
        buffer[0] = '\0';
        if (Buffer == NULL || Length == 0)
            return TRUE;
        int req = WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, NULL, 0, NULL, NULL);
        if (req < 0 || (size_t)req >= bufferCapacityInBytes)
            return FALSE;
        WideCharToMultiByte(CP_UTF8, 0, Buffer, (int)Length, buffer, req, NULL, NULL);
        buffer[req] = '\0';
        return TRUE;
    }

    BOOL Equals(const WCHAR* other, BOOL caseSensitive = FALSE) const
    {
        if (other == NULL)
            return Buffer == NULL;
        if (Buffer == NULL)
            return FALSE;
        if (caseSensitive)
            return wcscmp(Buffer, other) == 0;
        return _wcsicmp(Buffer, other) == 0;
    }

    BOOL Equals(const CPathW& other, BOOL caseSensitive = FALSE) const
    {
        return Equals(other.CStr(), caseSensitive);
    }

private:
    WCHAR* Buffer;
    size_t Capacity;
    size_t Length;
    WCHAR* ApiPath;

    void ClearApiPath()
    {
        if (ApiPath != NULL)
        {
            free(ApiPath);
            ApiPath = NULL;
        }
    }

    BOOL EnsureCapacity(size_t requiredChars)
    {
        if (requiredChars <= Capacity)
            return TRUE;
        size_t newCap = Capacity == 0 ? MAX_PATH : Capacity;
        while (newCap < requiredChars)
            newCap *= 2;
        WCHAR* newBuf = (WCHAR*)realloc(Buffer, newCap * sizeof(WCHAR));
        if (newBuf == NULL)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return FALSE;
        }
        Buffer = newBuf;
        Capacity = newCap;
        return TRUE;
    }

    static BOOL IsExtendedLengthPath(const WCHAR* path)
    {
        return path != NULL && path[0] == L'\\' && path[1] == L'\\' && path[2] == L'?' && path[3] == L'\\';
    }

    static BOOL IsUncPath(const WCHAR* path)
    {
        return path != NULL && path[0] == L'\\' && path[1] == L'\\' && !IsExtendedLengthPath(path);
    }

    const WCHAR* BuildAbsoluteApiPath()
    {
        if (Buffer == NULL)
            return NULL;
        DWORD required = GetFullPathNameW(Buffer, 0, NULL, NULL);
        if (required == 0)
            return NULL;

        for (int attempt = 0; attempt != 4; ++attempt)
        {
            WCHAR* fullPath = (WCHAR*)malloc(required * sizeof(WCHAR));
            if (fullPath == NULL)
            {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return NULL;
            }

            DWORD copied = GetFullPathNameW(Buffer, required, fullPath, NULL);
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
