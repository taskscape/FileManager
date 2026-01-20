// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// SAFE_ALLOC macro removes code that tests if memory allocation succeeded (see allochan.*)

// convert Unicode string (UTF-16) to ANSI multibyte string; 'src' is Unicode string;
// 'srcLen' is length of Unicode string (without terminating zero; when -1 is specified, length is determined
// by terminating zero); 'bufSize' (must be greater than 0) is size of target buffer
// 'buf' for ANSI string; if 'compositeCheck' is TRUE, uses flag WC_COMPOSITECHECK
// (see MSDN), must not be used for filenames (NTFS distinguishes names written as
// precomposed and composite, i.e. does not perform name normalization); 'codepage' is code page
// of ANSI string; returns number of characters written to 'buf' (including terminating zero); on error
// returns zero (details see GetLastError()); always ensures zero-terminated 'buf' (even on error);
// if 'buf' is small, function returns zero, but in 'buf' at least part of string is converted
int ConvertU2A(const WCHAR* src, int srcLen, char* buf, int bufSize,
               BOOL compositeCheck = FALSE, UINT codepage = CP_ACP);

// convert Unicode string (UTF-16) to allocated ANSI multibyte string (caller is
// responsible for string deallocation); 'src' is Unicode string; 'srcLen' is length of Unicode
// string (without terminating zero; when -1 is specified, length is determined by terminating zero);
// if 'compositeCheck' is TRUE, uses flag WC_COMPOSITECHECK (see MSDN), must not be used
// for filenames (NTFS distinguishes names written as precomposed and composite, i.e.
// does not perform name normalization); 'codepage' is code page of ANSI string; returns allocated
// ANSI string; on error returns NULL (details see GetLastError())
char* ConvertAllocU2A(const WCHAR* src, int srcLen, BOOL compositeCheck = FALSE, UINT codepage = CP_ACP);

// convert ANSI multibyte string to Unicode string (UTF-16); 'src' is ANSI string;
// 'srcLen' is length of ANSI string (without terminating zero; when -1 is specified, length is determined
// by terminating zero); 'bufSize' (must be greater than 0) is size of target buffer
// 'buf' for Unicode string; 'codepage' is code page of ANSI string;
// returns number of characters written to 'buf' (including terminating zero); on error returns zero
// (details see GetLastError()); always ensures zero-terminated 'buf' (even on error);
// if 'buf' is small, function returns zero, but in 'buf' at least part of string is converted
int ConvertA2U(const char* src, int srcLen, WCHAR* buf, int bufSizeInChars,
               UINT codepage = CP_ACP);

// convert ANSI multibyte string to allocated (caller is responsible for deallocation
// of string) Unicode string (UTF-16); 'src' is ANSI string; 'srcLen' is length of ANSI
// string (without terminating zero; when -1 is specified, length is determined by terminating zero);
// 'codepage' is code page of ANSI string; returns allocated Unicode string; on
// error returns NULL (details see GetLastError())
WCHAR* ConvertAllocA2U(const char* src, int srcLen, UINT codepage = CP_ACP);

// conversions between UTF-8 and UTF-16; 'srcLen' is length without terminating zero
// (when -1 is specified, length is determined by terminating zero); returns number of characters
// written to 'buf' (including terminating zero) or 0 on error; always ensures
// zero-terminated 'buf' (even on error)
int ConvertUtf8ToWide(const char* src, int srcLen, WCHAR* buf, int bufSizeInChars);
int ConvertWideToUtf8(const WCHAR* src, int srcLen, char* buf, int bufSizeInBytes);

// convert UTF-8 to allocated UTF-16 string (caller frees)
WCHAR* ConvertAllocUtf8ToWide(const char* src, int srcLen);
// convert UTF-16 to allocated UTF-8 string (caller frees)
char* ConvertAllocWideToUtf8(const WCHAR* src, int srcLen);

// Convert UTF-8 to wide string using stack buffer with heap fallback for long paths.
// Tries stack buffer first, falls back to heap allocation only if needed.
// Returns pointer to the converted string (either stackBuf or newly allocated).
// Sets *heapAllocated to TRUE if heap allocation was used (caller must free).
// Returns NULL on error.
// Usage example:
//   WCHAR stackBuf[MAX_PATH];
//   BOOL heapAlloc;
//   WCHAR* fileNameW = ConvertUtf8ToWideStackOrHeap(fileName, stackBuf, MAX_PATH, &heapAlloc);
//   if (fileNameW != NULL) { ... use fileNameW ... }
//   if (heapAlloc) free(fileNameW);
inline WCHAR* ConvertUtf8ToWideStackOrHeap(const char* src, WCHAR* stackBuf, int stackBufSizeInChars, BOOL* heapAllocated)
{
    *heapAllocated = FALSE;
    if (src == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    // Get required length first
    int len = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (len <= 0)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return NULL;
    }

    if (len <= stackBufSizeInChars)
    {
        // Fits in stack buffer - use it directly (no heap allocation)
        MultiByteToWideChar(CP_UTF8, 0, src, -1, stackBuf, stackBufSizeInChars);
        return stackBuf;
    }

    // Path too long for stack buffer - fall back to heap allocation
    WCHAR* heapBuf = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (heapBuf == NULL)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, src, -1, heapBuf, len);
    *heapAllocated = TRUE;
    return heapBuf;
}

// Helper class for automatic cleanup of stack-or-heap wide string buffers.
// If the string was heap-allocated, it frees the memory on destruction.
// Usage:
//   WCHAR stackBuf[MAX_PATH];
//   CStrStackOrHeap fileNameW(fileName, stackBuf, MAX_PATH);
//   if (fileNameW != NULL) { ... use fileNameW ... }
class CStrStackOrHeap
{
public:
    WCHAR* Ptr;
    BOOL HeapAllocated;

public:
    // Convert UTF-8 string using stack buffer with heap fallback
    CStrStackOrHeap(const char* src, WCHAR* stackBuf, int stackBufSizeInChars)
    {
        Ptr = ConvertUtf8ToWideStackOrHeap(src, stackBuf, stackBufSizeInChars, &HeapAllocated);
    }

    ~CStrStackOrHeap()
    {
        if (HeapAllocated && Ptr != NULL)
            free(Ptr);
    }

    operator WCHAR*() { return Ptr; }
    BOOL IsValid() const { return Ptr != NULL; }
};

// convert WIN32_FIND_DATAW to WIN32_FIND_DATAA with UTF-8 names
BOOL ConvertFindDataWToUtf8(const WIN32_FIND_DATAW& src, WIN32_FIND_DATAA* dst);

// UTF-8 file API wrappers (use WinAPI W variants under the hood)
HANDLE CreateFileUtf8(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                      LPSECURITY_ATTRIBUTES securityAttributes, DWORD creationDisposition,
                      DWORD flagsAndAttributes, HANDLE templateFile);
BOOL DeleteFileUtf8(const char* fileName);
BOOL CreateDirectoryUtf8(const char* dirName, LPSECURITY_ATTRIBUTES securityAttributes);
BOOL RemoveDirectoryUtf8(const char* dirName);
BOOL SetFileAttributesUtf8(const char* fileName, DWORD attrs);
DWORD GetFileAttributesUtf8(const char* fileName);

// copy string 'txt' to newly allocated string, NULL = low memory (risk only if
// allochan.* is not used) or 'txt'==NULL
WCHAR* DupStr(const WCHAR* txt);

// holds pointer to allocated memory, takes care of its deallocation when overwritten by another pointer to
// allocated memory and on its destruction
template <class PTR_TYPE>
class CAllocP
{
public:
    PTR_TYPE* Ptr;

public:
    CAllocP(PTR_TYPE* ptr = NULL) { Ptr = ptr; }
    ~CAllocP()
    {
        if (Ptr != NULL)
            free(Ptr);
    }

    PTR_TYPE* GetAndClear()
    {
        PTR_TYPE* p = Ptr;
        Ptr = NULL;
        return p;
    }

    operator PTR_TYPE*() { return Ptr; }
    PTR_TYPE* operator=(PTR_TYPE* p)
    {
        if (Ptr != NULL)
            free(Ptr);
        return Ptr = p;
    }
};

// holds allocated string, takes care of deallocation when overwritten by another string (also allocated)
// and on its destruction
typedef CAllocP<WCHAR> CStrP;
