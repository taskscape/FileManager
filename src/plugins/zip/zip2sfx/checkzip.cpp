// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
//#include <windows.h>
#include <crtdbg.h>
#include <stdio.h>

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "selfextr\\comdefs.h"
#include "typecons.h"
#include "checkzip.h"
#include "sfxmake\\sfxmake.h"
#include "chicon.h"
#include "zip2sfx.h"

static BOOL GetZipFileSizeDword(HANDLE file, DWORD* size)
{
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(file, &fileSize))
        return FALSE;
    if (fileSize.QuadPart < 0 || (ULONGLONG)fileSize.QuadPart > MAXDWORD)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }
    // ZIP2SFX records archive offsets as DWORD values, so preserve that format boundary explicitly.
    *size = (DWORD)fileSize.QuadPart;
    return TRUE;
}

static BOOL SeekZipFileDword(HANDLE file, DWORD offset)
{
    LARGE_INTEGER move;
    move.QuadPart = offset;
    // The archive scanner carries DWORD offsets, while SetFilePointerEx removes the legacy sentinel ambiguity.
    return SetFilePointerEx(file, move, NULL, FILE_BEGIN);
}

BOOL CheckEntries(DWORD dirOffs, DWORD totalEntries)
{
    DWORD offs = dirOffs;
    CFileHeader header;
    DWORD i;
    for (i = 0; i < totalEntries; i++)
    {
        if (!SeekZipFileDword(ZipFile, offs))
            return Error(STR_ERRACCESS, ZipName);
        if (!Read(ZipFile, &header, sizeof(CFileHeader)))
            return Error(STR_ERRREAD, ZipName);
        if (header.Signature != SIG_CENTRFH)
            return Error(STR_BADFORMAT);
        if (header.Flag & GPF_ENCRYPTED)
            Encrypt = TRUE;
        if (header.Method != CM_STORED && header.Method != CM_DEFLATED)
            return Error(STR_BADMETHOD);
        if (header.VersionExtr >= 45 &&
            (header.Size == 0xFFFFFFFF ||
             header.CompSize == 0xFFFFFFFF ||
             header.LocHeaderOffs == 0xFFFFFFFF ||
             header.StartDisk == 0xFFFF))
        {
            // Zip64 detected; reject the archive
            return Error(STR_ZIP64);
        }
        offs += sizeof(CFileHeader) + header.NameLen + header.ExtraLen + header.CommentLen;
    }
    return TRUE;
}

BOOL CheckZip()
{
    if (!GetZipFileSizeDword(ZipFile, &ArcSize))
        return Error(STR_ERRACCESS, ZipName);
    DWORD toRead;
    DWORD offs;
    if (ArcSize < 22)
        return Error(STR_BADFORMAT);
    if (ArcSize > 0xFFFF)
    {
        toRead = 0xFFFF;
        offs = ArcSize - 0xFFFF;
    }
    else
    {
        toRead = ArcSize;
        offs = 0;
    }
    if (!SeekZipFileDword(ZipFile, offs))
        return Error(STR_ERRACCESS, ZipName);
    if (!Read(ZipFile, IOBuffer, toRead))
        return Error(STR_ERRREAD, ZipName);
    for (char* ptr = IOBuffer + toRead - 22; ptr > IOBuffer; ptr--)
    {
        if (*(__UINT32*)ptr == SIG_EOCENTRDIR)
        {
            EOCentrDirOffs = offs + (ptr - IOBuffer);
            CEOCentrDirRecord* eocdr = (CEOCentrDirRecord*)ptr;
            if (eocdr->DiskNum > 0)
                return Error(STR_MULTVOL);
            if (eocdr->TotalEntries == 0)
                return Error(STR_EMPTYARCHIVE);
            if (EOCentrDirOffs > eocdr->CentrDirOffs + eocdr->CentrDirSize)
                return Error(STR_BADFORMAT, ZipName);
            return CheckEntries(eocdr->CentrDirOffs, eocdr->TotalEntries);
        }
    }
    return Error(STR_BADFORMAT);
}
