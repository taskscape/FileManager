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
// 'srcLen' je delka ANSI stringu (bez zakoncujici nuly; pri zadani -1 se delka urci
// by terminating zero); 'bufSize' (must be greater than 0) is size of target buffer
// 'buf' for Unicode string; 'codepage' is code page of ANSI string;
// returns number of characters written to 'buf' (including terminating zero); on error returns zero
// (detaily viz GetLastError()); vzdy zajisti nulou zakonceny 'buf' (i pri chybe);
// if 'buf' is small, function returns zero, but in 'buf' at least part of string is converted
int ConvertA2U(const char* src, int srcLen, WCHAR* buf, int bufSizeInChars,
               UINT codepage = CP_ACP);

// convert ANSI multibyte string to allocated (caller is responsible for deallocation
// stringu) Unicodovy string (UTF-16); 'src' je ANSI string; 'srcLen' je delka ANSI
// string (without terminating zero; when -1 is specified, length is determined by terminating zero);
// 'codepage' is code page of ANSI string; returns allocated Unicode string; on
// error returns NULL (details see GetLastError())
WCHAR* ConvertAllocA2U(const char* src, int srcLen, UINT codepage = CP_ACP);

// prevody mezi UTF-8 a UTF-16; 'srcLen' je delka bez zakoncujici nuly
// (when -1 is specified, length is determined by terminating zero); returns number of characters
// written to 'buf' (including terminating zero) or 0 on error; always ensures
// nulou zakonceny 'buf' (i pri chybe)
int ConvertUtf8ToWide(const char* src, int srcLen, WCHAR* buf, int bufSizeInChars);
int ConvertWideToUtf8(const WCHAR* src, int srcLen, char* buf, int bufSizeInBytes);

// convert UTF-8 to allocated UTF-16 string (caller frees)
WCHAR* ConvertAllocUtf8ToWide(const char* src, int srcLen);
// convert UTF-16 to allocated UTF-8 string (caller frees)
char* ConvertAllocWideToUtf8(const WCHAR* src, int srcLen);

// prevod WIN32_FIND_DATAW na WIN32_FIND_DATAA s UTF-8 jmeny
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

// drzi alokovany string, postara se o uvolneni pri prepisu jinym stringem (tez alokovanym)
// a pri sve destrukci
typedef CAllocP<WCHAR> CStrP;
