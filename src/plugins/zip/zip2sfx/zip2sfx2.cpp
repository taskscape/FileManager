// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
//#include <windows.h>
#include <crtdbg.h>
#include <stdio.h>

#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression

#include "selfextr\\comdefs.h"
#include "typecons.h"
#include "sfxmake\\sfxmake.h"
#include "chicon.h"
#include "crc32.h"
#include "iosfxset.h"
#include "zip2sfx.h"
#include "inflate.h"

#include "zip2sfx.rh"

static int SfxStringLength(const char* text)
{
    // The SFX header stores ANSI string sizes and offsets as int-sized byte counts.
    return static_cast<int>(strlen(text));
}

LPTSTR
StrNChr(LPCTSTR lpStart, int nChar, char wMatch)
{
    if (lpStart == NULL)
        return NULL;
    int i = SfxStringLength(lpStart);
    if (i > nChar)
        i = nChar;
    LPCTSTR lpEnd = lpStart + nChar;
    while (lpStart < lpEnd)
    {
        if (*lpStart == wMatch)
            return (LPTSTR)lpStart;
        lpStart++;
    }
    return NULL;
}

LPTSTR StrRChr(LPCTSTR lpStart, LPCTSTR lpEnd, char wMatch)
{
    lpEnd--;
    while (lpEnd >= lpStart)
    {
        if (*lpEnd == wMatch)
            return (LPTSTR)lpEnd;
        lpEnd--;
    }
    return NULL;
}

void* LoadRCData(int id, DWORD& size)
{
    HINSTANCE module = GetModuleHandle(NULL);
    HRSRC hRsrc = FindResource(module, MAKEINTRESOURCE(id), RT_RCDATA);
    if (hRsrc)
    {
        void* data = LoadResource(module, hRsrc);
        if (data)
        {
            size = SizeofResource(module, hRsrc);
            return data;
        }
    }
    return NULL;
}

BOOL WriteSFXHeader()
{
    CSelfExtrHeader header;
    int offs = sizeof(CSelfExtrHeader);

    int l = SfxStringLength(Settings.Command);
    if (l)
    {
        header.CommandOffs = offs;
        offs += ++l;
    }
    else
        header.CommandOffs = 0;
    header.TextOffs = offs;
    l = SfxStringLength(Settings.Text);
    offs += ++l;
    header.TitleOffs = offs;
    l = SfxStringLength(Settings.Title);
    offs += ++l;
    header.SubDirOffs = offs;

    unsigned td;
    const char* sd;
    const char* sdl;
    const char* sdr;
    HKEY key;
    // no need to test the return value; it was verified earlier
    ParseTargetDir(Settings.TargetDir, &td, &sd, &sdl, &sdr, &key);
    l = SfxStringLength(sd);
    offs += ++l;
    header.AboutOffs = offs;
    l = SfxStringLength(About);
    offs += ++l;
    header.ExtractBtnTextOffs = offs;
    l = SfxStringLength(Settings.ExtractBtnText);
    offs += ++l;
    header.VendorOffs = offs;
    l = SfxStringLength(Settings.Vendor);
    offs += ++l;
    header.WWWOffs = offs;
    l = SfxStringLength(Settings.WWW);
    offs += ++l;
    header.ArchiveNameOffs = offs;
    //l = lstrlen(archName);
    //ArchiveDataOffs += l; // we accounted for this earlier
    //offs += ++l;
    offs++;
    header.TargetDirSpecOffs = offs;
    offs += (sdr - sdl) + 1;
    if (td == SE_REGVALUE)
    {
        offs += sizeof(LONG); // prepend the HKEY value; even the 64-bit build stores only a 32-bit key for known roots (e.g. HKEY_CURRENT_USER)
        const char* bs = StrNChr(sdl, sdr - sdl, '\\');
        if (!bs)
            offs += 1; // add a separator character between the subkey and value
    }
    header.MBoxStyle = (SfxStringLength(Settings.MBoxTitle) ||
                        Settings.MBoxText && SfxStringLength(Settings.MBoxText))
                           ? Settings.MBoxStyle
                           : -1;
    header.MBoxTitleOffs = offs;
    l = SfxStringLength(Settings.MBoxTitle);
    offs += ++l;
    header.MBoxTextOffs = offs;
    if (Settings.MBoxText)
    {
        l = SfxStringLength(Settings.MBoxText);
        offs += ++l;
    }
    else
        offs++;
    header.WaitForOffs = offs;
    l = SfxStringLength(Settings.WaitFor);
    offs += Settings.Flags & SE_REMOVEAFTER ? ++l : 1;

    header.Signature = SELFEXTR_SIG;
    header.HeaderSize = offs;
    header.Flags = Settings.Flags;
    header.Flags = Settings.Flags | td;
    if (td == SE_TEMPDIREX)
        header.Flags |= SE_TEMPDIR;
    header.EOCentrDirOffs = EOCentrDirOffs;
    header.ArchiveSize = ArcSize;

    if (!Write(ExeFile, &header, sizeof(CSelfExtrHeader)))
        return FALSE;
    l = SfxStringLength(Settings.Command);
    if (l &&
        !Write(ExeFile, Settings.Command, ++l))
        return FALSE;
    if (!Write(ExeFile, Settings.Text, SfxStringLength(Settings.Text) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.Title, SfxStringLength(Settings.Title) + 1))
        return FALSE;
    if (!Write(ExeFile, (void*)sd, SfxStringLength(sd) + 1))
        return FALSE;
    if (!Write(ExeFile, About, SfxStringLength(About) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.ExtractBtnText, SfxStringLength(Settings.ExtractBtnText) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.Vendor, SfxStringLength(Settings.Vendor) + 1))
        return FALSE;
    if (!Write(ExeFile, Settings.WWW, SfxStringLength(Settings.WWW) + 1))
        return FALSE;
    if (!Write(ExeFile, "", 1))
        return FALSE;
    if (td == SE_REGVALUE)
    {
        // key - even the 64-bit build stores only a 32-bit key; known roots (e.g. HKEY_CURRENT_USER) are defined as 32-bit IDs
        LONG lkey = (LONG)key;
        if (!Write(ExeFile, &lkey, sizeof(lkey)))
            return FALSE;

        //subkey
        const char* bs = StrRChr(sdl, sdr, '\\');
        if (bs)
        {
            if (!Write(ExeFile, (void*)sdl, bs - sdl))
                return FALSE;
        }
        if (!Write(ExeFile, "", 1))
            return FALSE;

        //value
        if (bs)
        {
            bs++;
            if (!Write(ExeFile, (void*)bs, sdr - bs))
                return FALSE;
        }
        else
        {
            if (!Write(ExeFile, (void*)sdl, sdr - sdl))
                return FALSE;
        }
        if (!Write(ExeFile, "", 1))
            return FALSE;
    }
    else
    {
        if (!Write(ExeFile, (void*)sdl, sdr - sdl))
            return FALSE;
        if (!Write(ExeFile, "", 1))
            return FALSE;
    }
    if (!Write(ExeFile, Settings.MBoxTitle, SfxStringLength(Settings.MBoxTitle) + 1))
        return FALSE;
    const char* str = Settings.MBoxText ? Settings.MBoxText : "";
    if (!Write(ExeFile, str, SfxStringLength(str) + 1))
        return FALSE;
    str = Settings.Flags & SE_REMOVEAFTER ? Settings.WaitFor : "";
    if (!Write(ExeFile, str, SfxStringLength(str) + 1))
        return FALSE;
    return TRUE;
}

BOOL WriteSfxExecutable()
{
    char* buffer;
    unsigned size;
    DWORD offset;
    CSfxFileHeader sfxHead;
    char langName[128];

    printf(StringTable[STR_WRITINGEXE]);
    SfxPackage = CreateFile(Settings.SfxFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (SfxPackage == INVALID_HANDLE_VALUE)
        return Error(STR_ERROPEN, Settings.SfxFile);

    if (!Read(SfxPackage, &sfxHead, sizeof(CSfxFileHeader)))
        return Error(STR_ERRREAD, Settings.SfxFile);
    if (sfxHead.Signature != SFX_SIGNATURE)
        return Error(STR_CORRUPTSFX, Settings.SfxFile);
    if (sfxHead.CompatibleVersion != SFX_SUPPORTEDVERSION)
        return Error(STR_BADSFXVER, Settings.SfxFile);
    if (sfxHead.HeaderCRC != UpdateCrc((__UINT8*)&sfxHead, sizeof(CSfxFileHeader) - sizeof(DWORD), INIT_CRC, CrcTab))
        return Error(STR_CORRUPTSFX, Settings.SfxFile);

    WCHAR localeName[LOCALE_NAME_MAX_LENGTH];
    WCHAR languageName[128];
    LCID languageLcid = MAKELCID(MAKELANGID(sfxHead.LangID, SUBLANG_NEUTRAL), SORT_DEFAULT);
    if (LCIDToLocaleName(languageLcid, localeName, _countof(localeName), 0) != 0 &&
        GetLocaleInfoEx(localeName, LOCALE_SLANGUAGE, languageName, _countof(languageName)) != 0 &&
        WideCharToMultiByte(CP_ACP, 0, languageName, -1, langName, 128, NULL, NULL) != 0)
    {
        char* c = strchr(langName, ' ');
        if (c)
            *c = 0;
    }
    else
        langName[0] = 0;
    // The SFX metadata stores an LCID; convert it to a locale name before formatting its ANSI status label.
    printf(StringTable[STR_SFXINFO], Settings.SfxFile, langName);

    //copy executable
    BOOL bigSfx = Encrypt || Settings.Flags & SE_REMOVEAFTER && Settings.WaitFor;
    if (bigSfx)
    {
        size = sfxHead.BigSfxSize;
        offset = sfxHead.BigSfxOffset;
    }
    else
    {
        size = sfxHead.SmallSfxSize;
        offset = sfxHead.SmallSfxOffset;
    }
    buffer = (char*)malloc(size);
    if (!buffer)
        return Error(STR_LOWMEM);
    BOOL success = FALSE;
    // The SFX header remains DWORD-based, but the seek result must not share an error sentinel with a valid offset.
    LARGE_INTEGER sfxOffset;
    sfxOffset.QuadPart = offset;
    if (!SetFilePointerEx(SfxPackage, sfxOffset, NULL, FILE_BEGIN))
        Error(STR_ERRACCESS, Settings.SfxFile);
    else
    {
        if (Read(SfxPackage, buffer, size))
        {
            InPtr = (unsigned char*)buffer;
            InEnd = InPtr + size;
            InflatingTexts = FALSE;

            switch (Inflate())
            {
            case 0:
            {
                if (Crc == (bigSfx ? sfxHead.BigSfxCRC : sfxHead.SmallSfxCRC))
                    success = TRUE;
                else
                    Error(STR_CORRUPTSFX, Settings.SfxFile);
                break;
            }
            case 4:
            case 1:
            case 2:
                Error(STR_CORRUPTSFX, Settings.SfxFile);
                break;
            case 3:
                Error(STR_LOWMEM);
                break;
            case 5:
                break;
            }
        }
        else
            Error(STR_ERRREAD, Settings.SfxFile);
    }
    free(buffer);
    if (!success)
        return FALSE;
    CloseHandle(ExeFile);
    ExeFile = INVALID_HANDLE_VALUE;

    // load appropriate manifest
    DWORD manifestSize;
    void* manifest = LoadRCData((Settings.Flags & SE_REQUIRESADMIN)
                                    ? ID_SFX_MANIFEST_ADMIN
                                    : ID_SFX_MANIFEST_REGULAR,
                                manifestSize);
    if (!manifest)
        return Error(STR_ERRMANIFEST); // this should never happen

    //change icon
    if (!ChangeSfxIconAndAddManifest(ExeName, Icons, IconsCount, manifest,
                                     manifestSize))
        return Error(STR_ERRICON);

    ExeFile = CreateFile(ExeName, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
    if (ExeFile == INVALID_HANDLE_VALUE)
        return Error(STR_ERROPEN, ExeName);

    // Seeking to the generated executable's end needs an unambiguous success result before appending metadata.
    LARGE_INTEGER endOffset = {};
    if (!SetFilePointerEx(ExeFile, endOffset, NULL, FILE_END))
        return Error(STR_ERRACCESS, ExeName);

    if (!WriteSFXHeader())
        return Error(STR_ERRWRITE, ExeName);

    return TRUE;
}

BOOL AppendArchive()
{
    printf(StringTable[STR_WRITINGARC]);
    // Restart archive copying with SetFilePointerEx so offset zero cannot be confused with legacy failure handling.
    LARGE_INTEGER startOffset = {};
    if (!SetFilePointerEx(ZipFile, startOffset, NULL, FILE_BEGIN))
        return Error(STR_ERRACCESS, ZipName);
    DWORD left = ArcSize;
    DWORD toRead;
    while (left)
    {
        toRead = left > 0xFFFF ? 0xFFFF : left;
        if (!Read(ZipFile, IOBuffer, toRead))
            return Error(STR_ERRREAD, ZipName);
        if (!Write(ExeFile, IOBuffer, toRead))
            return Error(STR_ERRWRITE, ExeName);
        left -= toRead;
    }
    return TRUE;
}
