// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string.h>
#include <vector>

// Native relative opens/renames bind to the retained directory object. The
// Win32 rename wrapper rejects RootDirectory on supported Windows versions.
// Missing entry points fail closed; there is no pathname-replacement fallback.
// Local ABI types avoid colliding with the core worker's legacy NT declarations.
namespace PublicationNative
{
struct UnicodeString { USHORT Length, MaximumLength; WCHAR* Buffer; };
struct ObjectAttributes
{
    ULONG Length;
    HANDLE RootDirectory;
    UnicodeString* ObjectName;
    ULONG Attributes;
    void* SecurityDescriptor;
    void* SecurityQualityOfService;
};
struct IoStatus
{
    union { LONG Status; void* Pointer; };
    ULONG_PTR Information;
};
}

inline BOOL RelativeFileStatus(LONG status)
{
    if (status >= 0) return TRUE;
    typedef ULONG (WINAPI *TStatusToError)(LONG);
    TStatusToError convert = (TStatusToError)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlNtStatusToDosError");
    SetLastError(convert == NULL ? ERROR_GEN_FAILURE : convert(status));
    return FALSE;
}

inline BOOL IsRelativeFileName(const WCHAR* name)
{
    if (name == NULL || *name == 0 || wcslen(name) > 32767 ||
        wcspbrk(name, L"\\/:") != NULL || wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0)
    { SetLastError(ERROR_INVALID_NAME); return FALSE; }
    return TRUE;
}

inline HANDLE OpenRelativePublicationFile(HANDLE directory, const WCHAR* name,
                                         DWORD access = GENERIC_READ | DELETE | FILE_WRITE_ATTRIBUTES,
                                         DWORD sharing = FILE_SHARE_READ, BOOL createNew = FALSE)
{
    if (!IsRelativeFileName(name)) return INVALID_HANDLE_VALUE;
    typedef LONG (NTAPI *TOpen)(PHANDLE, ACCESS_MASK, PublicationNative::ObjectAttributes*, PublicationNative::IoStatus*,
                                   PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    TOpen openFile = (TOpen)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    if (openFile == NULL) { SetLastError(ERROR_PROC_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    PublicationNative::UnicodeString fileName;
    fileName.Length = (USHORT)(wcslen(name) * sizeof(WCHAR));
    fileName.MaximumLength = fileName.Length;
    fileName.Buffer = const_cast<WCHAR*>(name);
    PublicationNative::ObjectAttributes attributes = {};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = directory;
    attributes.ObjectName = &fileName;
    attributes.Attributes = 0x40; // OBJ_CASE_INSENSITIVE matches the existing Win32 boundary.
    PublicationNative::IoStatus status = {};
    HANDLE file = INVALID_HANDLE_VALUE;
    // FILE_CREATE never replaces a sibling staging/lease file. FILE_OPEN and
    // FILE_OPEN_REPARSE_POINT retain an existing leaf without following a link.
    if (!RelativeFileStatus(openFile(&file, access | SYNCHRONIZE,
                                      &attributes, &status, NULL, 0, sharing,
                                      createNew ? 2 : 1, 0x20 | 0x40 | 0x00200000, NULL, 0)))
        return INVALID_HANDLE_VALUE;
    return file;
}

inline BOOL RenameRelativePublicationFile(HANDLE file, HANDLE directory, const WCHAR* name)
{
    if (!IsRelativeFileName(name)) return FALSE;
    typedef LONG (NTAPI *TRename)(HANDLE, PublicationNative::IoStatus*, PVOID, ULONG, ULONG);
    TRename renameFile = (TRename)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationFile");
    if (renameFile == NULL) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    const size_t chars = wcslen(name);
    const DWORD bytes = (DWORD)(sizeof(FILE_RENAME_INFO) + (chars + 1) * sizeof(WCHAR));
    std::vector<BYTE> buffer(bytes, 0);
    // FILE_RENAME_INFO and native FILE_RENAME_INFORMATION share this layout.
    FILE_RENAME_INFO* information = (FILE_RENAME_INFO*)buffer.data();
    information->ReplaceIfExists = FALSE;
    information->RootDirectory = directory;
    information->FileNameLength = (DWORD)(chars * sizeof(WCHAR));
    memcpy(information->FileName, name, (chars + 1) * sizeof(WCHAR));
    PublicationNative::IoStatus status = {};
    return RelativeFileStatus(renameFile(file, &status, information, bytes, 10)); // FileRenameInformation
}
