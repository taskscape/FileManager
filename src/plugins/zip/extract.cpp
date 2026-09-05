// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <tchar.h>
#include <crtdbg.h>
#include <ostream>
#include <commctrl.h>
#include <stdio.h>
#include <strsafe.h> // counted copies for fixed extraction-path buffers
#include "spl_com.h"
#include "spl_base.h"
#include "spl_file.h"
#include "spl_gen.h"
#include "spl_arc.h"
#include "spl_menu.h"
#include "dbg.h"

#include "config.h"
#include "common.h"
#include "zip.rh"
#include "zip.rh2"
#include "lang\lang.rh"
#include "extract.h"
#include "inflate.h"
#include "explode.h"
#include "unshrink.h"
#include "unreduce.h"
#include "unbzip2.h"
#include "crypt.h"
#include "add_del.h"
#include "dialogs.h"
#include "..\\..\\common\\checked_arithmetic.h"

// Used in CZipUnpack::ExtractFiles & CZipUnpack::ExtractSingleFile when testing archive. For simplicity reasons we assume this is enough ;-)

#define ZIP_MAX_PATH 1024

// Archive entry names are untrusted; reject every spelling that Win32 could resolve outside the selected root.
static BOOL IsUnsafeArchiveEntryPath(const char* entryPath)
{
    if (entryPath == NULL || *entryPath == 0 || *entryPath == '\\' || *entryPath == '/')
        return TRUE;

    const char* component = entryPath;
    for (const char* cursor = entryPath;; cursor++)
    {
        if (*cursor == ':')
            return TRUE; // Drive-qualified paths and alternate streams are never archive-relative names.

        if (*cursor != '\\' && *cursor != '/' && *cursor != 0)
            continue;

        size_t componentLength = (size_t)(cursor - component);
        if (componentLength == 0 ||
            componentLength == 1 && component[0] == '.' ||
            componentLength == 2 && component[0] == '.' && component[1] == '.')
            return TRUE;

        // Win32 device aliases bypass normal directory traversal even when a component looks relative.
        while (componentLength != 0 && (component[componentLength - 1] == '.' || component[componentLength - 1] == ' '))
            componentLength--;
        size_t baseLength = 0;
        while (baseLength < componentLength && component[baseLength] != '.')
            baseLength++;
        if ((baseLength == 3 && (_strnicmp(component, "CON", 3) == 0 || _strnicmp(component, "PRN", 3) == 0 ||
                                 _strnicmp(component, "AUX", 3) == 0 || _strnicmp(component, "NUL", 3) == 0)) ||
            (baseLength == 4 && ((_strnicmp(component, "COM", 3) == 0 || _strnicmp(component, "LPT", 3) == 0) &&
                                 component[3] >= '1' && component[3] <= '9')))
            return TRUE;

        if (*cursor == 0)
            return FALSE;
        component = cursor + 1;
    }
}

static BOOL IsReparsePointExtractionRoot(const char* targetDir)
{
    // A junction/symlink root makes later relative path checks meaningless because it can redirect the entire extraction.
    DWORD attributes = targetDir != NULL ? GetFileAttributesA(targetDir) : INVALID_FILE_ATTRIBUTES;
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

static BOOL HasReparsePointInExtractionPath(char* targetPath, int rootLength)
{
    // Recheck every existing prefix because an attacker can redirect a child after the root was approved.
    if (targetPath == NULL || rootLength < 0)
        return TRUE;

    for (char* componentEnd = targetPath + rootLength;; componentEnd++)
    {
        if (*componentEnd != '\\' && *componentEnd != '/' && *componentEnd != 0)
            continue;

        const char saved = *componentEnd;
        *componentEnd = 0;
        const DWORD attributes = GetFileAttributesA(targetPath);
        const DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        *componentEnd = saved;

        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            // Missing suffixes will be created by the extraction operation; inaccessible prefixes are unsafe to trust.
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? FALSE : TRUE;
        }
        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return TRUE;
        if (saved == 0)
            return FALSE;
    }
}

// Holds the extraction root directory so later path checks cannot follow a replaced junction.
class CExtractionRootHandle
{
public:
    CExtractionRootHandle(const char* targetDir) : Handle(INVALID_HANDLE_VALUE)
    {
        // Hold the original directory object so path replacement cannot make a later string check trust a new root.
        Handle = CreateFileA(targetDir, FILE_READ_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    }

    ~CExtractionRootHandle()
    {
        if (Handle != INVALID_HANDLE_VALUE)
            CloseHandle(Handle);
    }

    BOOL IsValid() const
    {
        return Handle != INVALID_HANDLE_VALUE;
    }

    BOOL Contains(HANDLE outputHandle) const
    {
        char rootPath[ZIP_MAX_PATH + 8];
        char outputPath[ZIP_MAX_PATH + 8];
        DWORD rootLength = GetFinalPathNameByHandleA(Handle, rootPath, _countof(rootPath), FILE_NAME_NORMALIZED);
        DWORD outputLength = GetFinalPathNameByHandleA(outputHandle, outputPath, _countof(outputPath), FILE_NAME_NORMALIZED);
        if (rootLength == 0 || rootLength >= _countof(rootPath) ||
            outputLength == 0 || outputLength >= _countof(outputPath))
            return FALSE;

        while (rootLength != 0 && (rootPath[rootLength - 1] == '\\' || rootPath[rootLength - 1] == '/'))
            rootLength--;
        // Compare final object paths, not input spellings, so a junction swap cannot escape the held root.
        return outputLength > rootLength &&
               _strnicmp(rootPath, outputPath, rootLength) == 0 &&
               (outputPath[rootLength] == '\\' || outputPath[rootLength] == '/');
    }

private:
    HANDLE Handle;
};

CZipUnpack::CZipUnpack(const char* zipName, const char* zipRoot, CSalamanderForOperationsAbstract* salamander,
                       TIndirectArray2<char>* archiveVolumes) : CZipCommon(zipName, zipRoot, salamander, archiveVolumes), Passwords(8)
{
    CALL_STACK_MESSAGE3("CZipUnpack::CZipUnpack(%s, %s, )", zipName, zipRoot);
    Heap = HeapCreate(HEAP_NO_SERIALIZE, INITIAL_HEAP_SIZE, MAXIMUM_HEAP_SIZE);
    if (!Heap)
        ErrorID = IDS_LOWMEM;

    OutputBuffer = NULL;
    Extract = true;
    Unshrinking = false;
    Test = false;
    AllocateWholeFile = true;
    TestAllocateWholeFile = true;
}

int CZipUnpack::UnpackArchive(const char* targetDir, SalEnumSelection next, void* param)
{
    CALL_STACK_MESSAGE2("CZipUnpack::UnpackArchive(%s, , )", targetDir);
    TIndirectArray2<CExtInfo> extrNames(256);  //file names to be extracted
    TIndirectArray2<CFileInfo> extrFiles(256); //file header of files to be extracted
    int dirs;

    int ret = CreateCFile(&ZipFile, ZipName, GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                          true, false);
    if (ret)
        if (ret == ERR_LOWMEM)
            return ErrorID = IDS_LOWMEM;
        else
            return ErrorID = IDS_NODISPLAY;
    char title[1024];
    sprintf(title, LoadStr(IDS_EXTRPROGTITLE), SalamanderGeneral->SalPathFindFileName(ZipName));
    Salamander->OpenProgressDialog(title, TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LoadStr(IDS_PREPAREDATA), FALSE);
    ErrorID = CheckZip();
    if (!ErrorID && !ZeroZip)
    {
        MatchedTotalSize = CQuadWord(0, 0);
        ExtrFiles = &extrFiles;
        ErrorID = EnumFiles(extrNames, dirs, next, param);
        if (!ErrorID)
        {
            ErrorID = MatchFiles(extrFiles, extrNames, dirs, NULL);
            if (ErrorID && extrFiles.Count)
            {
                if (ErrorID != IDS_NODISPLAY)
                    SalamanderGeneral->ShowMessageBox(LoadStr(ErrorID), LoadStr(IDS_PLUGINNAME), MSGBOX_ERROR);
                ErrorID = 0;
            }
            ProgressTotalSize = MatchedTotalSize;
            if (!ErrorID && extrFiles.Count)
            {
                if (MultiVol)
                {
                    int left = 0, right = 0, i = 0;

                    QuickSortHeaders2(0, extrFiles.Count - 1, extrFiles);
                    while (i < extrFiles.Count)
                    {
                        while (i < extrFiles.Count &&
                               extrFiles[i]->StartDisk == extrFiles[left]->StartDisk)
                        {
                            right = i;
                            i++;
                        }
                        QuickSortHeaders(left, right, extrFiles);
                        left = ++right;
                    }
                }
                else
                {
                    QuickSortHeaders(0, extrFiles.Count - 1, extrFiles);
                }
                if (IsReparsePointExtractionRoot(targetDir))
                    ErrorID = IDS_UNSAFEEXTRACTPATH;
                else
                {
                    if (*OriginalCurrentDir)
                        SetCurrentDirectory(targetDir);
                    ErrorID = ExtractFiles(targetDir);
                }
            }
        }
    }
    Salamander->CloseProgressDialog();
    return ErrorID;
}

int CZipUnpack::UnpackOneFile(const char* nameInZip, const CFileData* fileData, const char* targetPath, const char* newFileName)
{
    CALL_STACK_MESSAGE3("CZipUnpack::UnpackOneFile(%s, , %s)", nameInZip, targetPath);
    CFileInfo fileInfo;
    TCHAR targetDir[MAX_PATH + 1];
    int targetDirLen;
    char* sour;
    CZIPFileData* zipFileData = (CZIPFileData*)fileData->PluginData;

    Unix = zipFileData->Unix;

    int ret = CreateCFile(&ZipFile, ZipName, GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL, true, false);
    if (ret)
    {
        if (ret == ERR_LOWMEM)
            return ErrorID = IDS_LOWMEM;
        else
            return ErrorID = IDS_NODISPLAY;
    }
    ErrorID = CheckZip();
    if (!ErrorID && !ZeroZip)
    {
        if (IsReparsePointExtractionRoot(targetPath))
            ErrorID = IDS_UNSAFEEXTRACTPATH;
        else
            ErrorID = FindFile(nameInZip, &fileInfo, zipFileData->ItemNumber);
        if (!ErrorID)
        {
            // counted copy of the extraction root into its fixed buffer
            StringCchCopyA(targetDir, _countof(targetDir), targetPath);
            // Extraction paths use the plugin's legacy int length contract.
            targetDirLen = static_cast<int>(strlen(targetDir));
            if (targetDirLen && targetDir[targetDirLen - 1] == '\\')
            {
                targetDir[targetDirLen - 1] = 0;
                targetDirLen--;
            }
            fixed_tl64 = NULL;
            fixed_td64 = NULL;
            fixed_tl32 = NULL;
            fixed_td32 = NULL;
            SkipAllIOErrors = 0;
            SkipAllLongNames = 0;
            SkipAllEncrypted = 0;
            SkipAllDataErr = 0;
            SkipAllBadMathods = 0;
            Silent = 0;
            DialogFlags = PE_NOSKIP;
            //fileInfo.FileAttr |= FILE_ATTRIBUTE_TEMPORARY;
            sour = fileInfo.Name + fileInfo.NameLen;
            while (sour > fileInfo.Name)
            {
                if (*sour == '\\')
                    break;
                sour--;
            }
            if (sour > fileInfo.Name)
                RootLen = (int)(sour - fileInfo.Name);
            else
                RootLen = 0;
            InputBuffer = (char*)malloc(DECOMPRESS_INBUFFER_SIZE);
            InBufSize = DECOMPRESS_INBUFFER_SIZE;
            SlideWindow = (char*)malloc(SLIDE_WINDOW_SIZE);
            WinSize = SLIDE_WINDOW_SIZE;
            if (InputBuffer && SlideWindow)
            {
                ErrorID = ExtractSingleFile(targetDir, targetDirLen, &fileInfo, NULL, newFileName);
                InflateFreeFixedHufman();
                free(InputBuffer);
                free(SlideWindow);
            }
            else
            {
                if (InputBuffer)
                    free(InputBuffer);
                if (SlideWindow)
                    free(SlideWindow);
                ErrorID = IDS_LOWMEM;
            }
            //free(fileInfo.Name); the destructor releases it
        }
    }
    return ErrorID;
}

int CZipUnpack::UnpackWholeArchive(const char* mask, const char* targetDir)
{
    CALL_STACK_MESSAGE3("CZipUnpack::UnpackWholeArchive(%s, %s)", mask, targetDir);
    TIndirectArray2<char> maskArray(16);
    TIndirectArray2<CFileInfo> extrFiles(256); //file header of files to be extracted

    int ret = CreateCFile(&ZipFile, ZipName, GENERIC_READ, FILE_SHARE_READ,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, PE_NOSKIP, NULL,
                          true, false);
    if (ret)
        if (ret == ERR_LOWMEM)
            return ErrorID = IDS_LOWMEM;
        else
            return ErrorID = IDS_NODISPLAY;
    char title[1024];
    sprintf(title, LoadStr(Test ? IDS_TESTPROGTITLE : IDS_EXTRPROGTITLE), SalamanderGeneral->SalPathFindFileName(ZipName));
    Salamander->OpenProgressDialog(title, TRUE, NULL, FALSE);
    Salamander->ProgressDialogAddText(LoadStr(IDS_PREPAREDATA), FALSE);
    ErrorID = CheckZip();
    if (!ErrorID && ArchiveVolumes != NULL)
        ArchiveVolumes->Add(_strdup(ZipName));
    if (!ErrorID && !ZeroZip)
    {
        MatchedTotalSize = CQuadWord(0, 0);
        ExtrFiles = &extrFiles;
        //errorID = EnumFiles(&extrNames, next, param, &extrInfo._CommonInfo);
        ErrorID = PrepareMaskArray(maskArray, mask);
        if (!ErrorID && maskArray.Count)
        {
            ErrorID = MatchFilesToMask(maskArray);
            if (ErrorID && extrFiles.Count)
            {
                if (ErrorID != IDS_NODISPLAY)
                    SalamanderGeneral->ShowMessageBox(LoadStr(ErrorID), LoadStr(IDS_PLUGINNAME), MSGBOX_ERROR);
                ErrorID = 0;
            }
            ProgressTotalSize = MatchedTotalSize;
            if (!ErrorID && extrFiles.Count)
            {
                if (MultiVol)
                {
                    int left = 0,
                        right = 0,
                        i = 0;

                    QuickSortHeaders2(0, extrFiles.Count - 1, extrFiles);
                    while (i < extrFiles.Count)
                    {
                        while (i < extrFiles.Count &&
                               extrFiles[i]->StartDisk == extrFiles[left]->StartDisk)
                        {
                            right = i;
                            i++;
                        }
                        QuickSortHeaders(left, right, extrFiles);
                        left = ++right;
                    }
                }
                else
                {
                    QuickSortHeaders(0, extrFiles.Count - 1, extrFiles);
                }
                if (IsReparsePointExtractionRoot(targetDir))
                    ErrorID = IDS_UNSAFEEXTRACTPATH;
                else
                {
                    if (*OriginalCurrentDir && !Test)
                        SetCurrentDirectory(targetDir);
                    ErrorID = ExtractFiles(targetDir);
                }
            }
        }
    }
    Salamander->CloseProgressDialog();
    return ErrorID;
}

int CZipUnpack::FindFile(LPCTSTR name, CFileInfo* fileInfo, int nItem)
{
    CALL_STACK_MESSAGE2("CZipUnpack::FindFile(%s, )", name);
    CFileHeader* centralHeader;
    char* tempName;
    int errorID = 0;
    QWORD readOffset;
    QWORD readSize;
    DWORD pathFlag = Unix ? 0 : NORM_IGNORECASE;
    BOOL found = FALSE;

    centralHeader = (CFileHeader*)malloc(MAX_HEADER_SIZE);
    tempName = (char*)malloc(MAX_HEADER_SIZE);
    if (!centralHeader || !tempName)
    {
        if (centralHeader)
            free(centralHeader);
        if (tempName)
            free(tempName);
        return IDS_LOWMEM;
    }
    // Refuse a wrapped central-directory cursor before archive names drive extraction selection.
    if (!CheckedAddUInt64(CentrDirOffs, ExtraBytes, &readOffset))
    {
        free(centralHeader);
        free(tempName);
        return IDS_ERRFORMAT;
    }
    readSize = 0;
    if (DiskNum != CentrDirStartDisk && MultiVol)
    {
        DiskNum = CentrDirStartDisk;
        errorID = ChangeDisk();
    }
    int i;
    for (i = 0; readSize < CentrDirSize && !errorID; i++)
    {
        unsigned int s;

        errorID = ReadCentralHeader(centralHeader, &readOffset, &s);
        if (errorID)
            break;
        readSize += s;
        if (i == nItem)
        {
            unsigned int tempNameLen = ProcessName(centralHeader, tempName);
            // Central-directory name lengths are stored as unsigned 32-bit values.
            unsigned int nameLen = static_cast<unsigned int>(strlen(name));

            if (tempNameLen == nameLen)
            {
                LPCTSTR str = _tcsrchr(name, '\\');
                int pathLen;
                pathLen = str ? (int)(str - name + 1) : 0;
                if ((CompareString(LOCALE_USER_DEFAULT, pathFlag,
                                   name, pathLen,
                                   tempName, pathLen) == CSTR_EQUAL) &&
                    (CompareString(LOCALE_USER_DEFAULT, 0,
                                   name + pathLen, nameLen - pathLen,
                                   tempName + pathLen, nameLen - pathLen) == CSTR_EQUAL))
                {
                    ProcessHeader(centralHeader, fileInfo);
                    if (!fileInfo->IsDir)
                    {
                        fileInfo->NameLen = tempNameLen;
                        fileInfo->Name = _tcsdup(tempName);
                        if (!fileInfo->Name)
                        {
                            errorID = IDS_LOWMEM;
                            break;
                        }
                        found = TRUE;
                        break;
                    }
                }
            }
        }
    }
    if (!found)
        errorID = IDS_FILENOTFOUND;
    free(centralHeader);
    free(tempName);
    return errorID;
}

int CZipUnpack::PrepareMaskArray(TIndirectArray2<char>& maskArray, const char* masks)
{
    CALL_STACK_MESSAGE2("CZipUnpack::PrepareMaskArray(, %s)", masks);
    const char* sour;
    char* dest;
    char* newMask;
    int newMaskLen;
    char buffer[MAX_PATH + 1];

    sour = masks;
    while (*sour)
    {
        dest = buffer;
        while (*sour)
        {
            if (*sour == ';')
            {
                if (*(sour + 1) == ';')
                    sour++;
                else
                    break;
            }
            if (dest == buffer + MAX_PATH)
                return IDS_TOOLONGMASK;
            *dest++ = *sour++;
        }
        while (--dest >= buffer && *dest <= ' ')
            ;
        *(dest + 1) = 0;
        dest = buffer;
        while (*dest != 0 && *dest <= ' ')
            dest++;
        newMaskLen = (int)strlen(dest);
        if (newMaskLen)
        {
            newMask = new char[newMaskLen + 1];
            if (!newMask)
                return IDS_LOWMEM;
            SalamanderGeneral->PrepareMask(newMask, dest);
            if (!maskArray.Add(newMask))
            {
                delete newMask;
                return IDS_LOWMEM;
            }
        }
        if (*sour)
            sour++;
    }
    return 0;
}

int CZipUnpack::MatchFilesToMask(TIndirectArray2<char>& maskArray)
{
    CALL_STACK_MESSAGE1("CZipUnpack::MatchFilesToMask()");
    CFileHeader* centralHeader;
    CFileInfo* fileInfo;
    char* tempName;
    unsigned tempNameLen;
    char* sour;
    int errorID = 0;
    QWORD readOffset;
    QWORD readSize;
    int j; //temporary variable
    bool hasExtension;

    centralHeader = (CFileHeader*)malloc(MAX_HEADER_SIZE);
    tempName = (char*)malloc(MAX_HEADER_SIZE);
    if (!centralHeader || !tempName)
    {
        if (centralHeader)
            free(centralHeader);
        if (tempName)
            free(tempName);
        return IDS_LOWMEM;
    }
    // Mask matching uses the same untrusted central-directory cursor as single-file lookup.
    if (!CheckedAddUInt64(CentrDirOffs, ExtraBytes, &readOffset))
    {
        free(centralHeader);
        free(tempName);
        return IDS_ERRFORMAT;
    }
    readSize = 0;
    if (DiskNum != CentrDirStartDisk && MultiVol)
    {
        DiskNum = CentrDirStartDisk;
        errorID = ChangeDisk();
    }
    for (; readSize < CentrDirSize && !errorID;)
    {
        unsigned int s;
        errorID = ReadCentralHeader(centralHeader, &readOffset, &s);
        if (errorID)
            break;
        readSize += s;
        tempNameLen = ProcessName(centralHeader, tempName);
        sour = tempName + tempNameLen;
        hasExtension = false;
        while (sour >= tempName && *sour != '\\')
            if (*sour-- == '.')
                hasExtension = true; // ".cvspass" is treated as an extension in Windows
        sour++;
        for (j = 0; j < maskArray.Count; j++)
        {
            if (SalamanderGeneral->AgreeMask(sour, maskArray[j], hasExtension))
            {
                fileInfo = new CFileInfo;
                if (!fileInfo)
                {
                    errorID = IDS_LOWMEM;
                    break;
                }
                ProcessHeader(centralHeader, fileInfo);
                fileInfo->NameLen = tempNameLen;
                size_t nameAllocationSize;
                // Normalized archive names still need a checked terminator allocation before they become persistent records.
                if (!CheckedAddSize((size_t)tempNameLen, 1, &nameAllocationSize))
                {
                    delete fileInfo;
                    errorID = IDS_ERRFORMAT;
                    break;
                }
                fileInfo->Name = (char*)malloc(nameAllocationSize);
                if (!fileInfo->Name)
                {
                    delete fileInfo;
                    errorID = IDS_LOWMEM;
                    break;
                }
                // the allocation above is sized from the same measured length, so copy that exact span
                memcpy(fileInfo->Name, tempName, tempNameLen + 1);
                if (!ExtrFiles->Add(fileInfo))
                {
                    delete fileInfo;
                    errorID = IDS_LOWMEM;
                };
                if (!ErrorID)
                    MatchedTotalSize += CQuadWord().SetUI64(fileInfo->Size);
                break;
            }
        }
    }
    free(centralHeader);
    free(tempName);
    return errorID;
}

//inflate hi-level routines
#define CZIPUNPACK(decompress) ((CZipUnpack*)decompress->UserData)

void Refill(CDecompressionObject* decompress)
{
    CALL_STACK_MESSAGE1("Refill()");

    if (CZIPUNPACK(decompress)->BytesLeft == 0)
    {
        TRACE_E("Pozadavek na doplneni vstupniho bufferu za hranici zkomprimovanych dat");
        decompress->Input->Error = IDS_EOF;
        return;
    }

    int s = (int)min(CZIPUNPACK(decompress)->InBufSize, CZIPUNPACK(decompress)->BytesLeft);
    decompress->Input->Error =
        CZIPUNPACK(decompress)->SafeRead(CZIPUNPACK(decompress)->InputBuffer, s, NULL);

    if (decompress->Input->Error)
        return;

    CZIPUNPACK(decompress)->BytesLeft -= s;

    if (CZIPUNPACK(decompress)->Encrypted)
    {
        if (CZIPUNPACK(decompress)->AESContextValid)
        {
            SalamanderCrypt->AESDecrypt(&CZIPUNPACK(decompress)->AESContext, CZIPUNPACK(decompress)->InputBuffer, s);
            if (CZIPUNPACK(decompress)->BytesLeft == 0)
            {
                // final chunk, verify the authentication code
                unsigned char mac[AES_MAXHMAC];
                unsigned char macFile[AES_MAXHMAC];
                int mode = CZIPUNPACK(decompress)->AESContext.mode;
                DWORD macLen = SAL_AES_MAC_LENGTH(mode);

                decompress->Input->Error =
                    CZIPUNPACK(decompress)->SafeRead(macFile, macLen, NULL);
                if (decompress->Input->Error)
                    return;

                SalamanderCrypt->AESEnd(&CZIPUNPACK(decompress)->AESContext, mac, &macLen);
                CZIPUNPACK(decompress)->AESContextValid = FALSE;

                if (memcmp(mac, macFile, macLen) != 0)
                {
                    decompress->Input->Error = IDS_MACERROR;
                    return;
                }
            }
        }
        else
            Decrypt(CZIPUNPACK(decompress)->InputBuffer, s, CZIPUNPACK(decompress)->Keys);
    }

    decompress->Input->NextByte = (__UINT8*)CZIPUNPACK(decompress)->InputBuffer;
    decompress->Input->BytesLeft = s;
}

int ExtractFlush(unsigned bytes, CDecompressionObject* decompress)
{
    CALL_STACK_MESSAGE2("ExtractFlush(0x%X, )", bytes);
    uch* buf = CZIPUNPACK(decompress)->Unshrinking ? (decompress->Output->OutBuf) : (decompress->Output->SlideWin);
    //  TRACE_I("Flush");
    CZIPUNPACK(decompress)->Crc = SalamanderGeneral->UpdateCrc32(buf,
                                                                 bytes,
                                                                 CZIPUNPACK(decompress)->Crc);
    if (!CZIPUNPACK(decompress)->Test)
    {
        CZIPUNPACK(decompress)->OutputError =
            CZIPUNPACK(decompress)->Write(CZIPUNPACK(decompress)->OutputFile, buf, bytes, &CZIPUNPACK(decompress)->SkipAllIOErrors);
    }
    else
    {
        CZIPUNPACK(decompress)->ExtractedBytes += bytes;
        CZIPUNPACK(decompress)->OutputError = 0;
    }
    if (!CZIPUNPACK(decompress)->OutputError)
    {
        //CZIPUNPACK(decompress)->OutputPos += bytes;
        if (CZIPUNPACK(decompress)->Salamander->ProgressAddSize(bytes, TRUE))
            return 0;
        else
        {
            CZIPUNPACK(decompress)->UserBreak = true;
            CZIPUNPACK(decompress)->OutputError = 0;
            return 1;
        }
    }
    else
        return 1; //error
}

int CZipUnpack::GetCompressedPayloadSize(CFileInfo* fileInfo, QWORD* payloadSize, int* errorID)
{
    const QWORD encryptionOverhead = Encrypted
                                        ? (AESContextValid ? SAL_AES_SALT_LENGTH(AESContext.mode) + SAL_AES_PWD_VER_LENGTH + AES_MAXHMAC
                                                           : ENCRYPT_HEADER_SIZE)
                                        : 0;
    if (fileInfo->CompSize >= encryptionOverhead)
    {
        *payloadSize = fileInfo->CompSize - encryptionOverhead;
        return DEC_NOERROR;
    }

    // The compressed size is archive input; never let an undersized encrypted entry underflow into a huge decoder read.
    switch (ProcessError(IDS_ERRCOMPDATA, 0, FileNameDisp, PE_NORETRY | DialogFlags, &SkipAllDataErr))
    {
    case ERR_SKIP:
        return DEC_SKIP;
    case ERR_CANCEL:
        *errorID = IDS_NODISPLAY;
        return DEC_CANCEL;
    default:
        *errorID = IDS_NODISPLAY;
        return DEC_CANCEL;
    }
}

int CZipUnpack::InflateFile(CFileInfo* fileInfo, BOOL deflate64, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::InflateFile(, )");
    CDecompressionObject decompress;
    COutputManager output;
    CInputManager input;
    int exitCode = DEC_NOERROR;
    //int                  result;

    if ((exitCode = GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    Crc = INIT_CRC;
    input.NextByte = (__UINT8*)InputBuffer;
    input.BytesLeft = 0;
    input.Error = 0;
    input.Refill = Refill;
    output.SlideWin = (__UINT8*)SlideWindow;
    output.WinSize = WinSize;
    output.Flush = ExtractFlush;
    decompress.Input = &input;
    decompress.Output = &output;
    decompress.UserData = this;
    decompress.HeapInfo = (void*)Heap;
    decompress.fixed_tl64 = (huft*)fixed_tl64;
    decompress.fixed_td64 = (huft*)fixed_td64;
    decompress.fixed_bl64 = fixed_bl64;
    decompress.fixed_bd64 = fixed_bd64;
    decompress.fixed_tl32 = (huft*)fixed_tl32;
    decompress.fixed_td32 = (huft*)fixed_td32;
    decompress.fixed_bl32 = fixed_bl32;
    decompress.fixed_bd32 = fixed_bd32;
    switch (Inflate(&decompress, deflate64))
    {
    case 1:;
    case 2:
    {
    BadData:
        switch (ProcessError(IDS_ERRCOMPDATA, 0, FileNameDisp,
                             PE_NORETRY | DialogFlags, &SkipAllDataErr))
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
        break;
    }

    case 3:
        exitCode = DEC_CANCEL;
        *errorID = IDS_LOWMEM;
        break;
    case 4:
    {
        if (decompress.Input->Error == IDS_EOF)
            goto BadData;
        if (decompress.Input->Error == IDS_MACERROR)
        {
            switch (ProcessError(IDS_MACERROR, 0, FileNameDisp,
                                 PE_NORETRY | DialogFlags, &SkipAllDataErr))
            {
            case ERR_SKIP:
                exitCode = DEC_SKIP;
                break;
            case ERR_CANCEL:
                exitCode = DEC_CANCEL;
                *errorID = IDS_NODISPLAY;
            }
        }
        else
        {
            exitCode = DEC_CANCEL;
            *errorID = decompress.Input->Error;
        }
        break;
    }

    case 5:
    {
        switch (OutputError)
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
    }
    }
    fixed_tl64 = decompress.fixed_tl64;
    fixed_td64 = decompress.fixed_td64;
    fixed_bl64 = decompress.fixed_bl64;
    fixed_bd64 = decompress.fixed_bd64;
    fixed_tl32 = decompress.fixed_tl32;
    fixed_td32 = decompress.fixed_td32;
    fixed_bl32 = decompress.fixed_bl32;
    fixed_bd32 = decompress.fixed_bd32;
    return exitCode;
}

void CZipUnpack::InflateFreeFixedHufman()
{
    CALL_STACK_MESSAGE1("CZipUnpack::InflateFreeFixedHufman()");
    CDecompressionObject decompress;

    decompress.HeapInfo = (void*)Heap;
    decompress.fixed_tl64 = (huft*)fixed_tl64;
    decompress.fixed_td64 = (huft*)fixed_td64;
    decompress.fixed_tl32 = (huft*)fixed_tl32;
    decompress.fixed_td32 = (huft*)fixed_td32;
    FreeFixedHufman(&decompress);
}

int CZipUnpack::UnStoreFile(CFileInfo* fileInfo, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::UnStoreFile(, )");
    int exitCode = DEC_NOERROR;
    unsigned readBytes;
    QWORD bytesLeft;
    int result;

    if ((exitCode = GetCompressedPayloadSize(fileInfo, &bytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    Crc = INIT_CRC;
    while (bytesLeft && !UserBreak)
    {
        readBytes = (unsigned)min(InBufSize, bytesLeft);
        *errorID = SafeRead(InputBuffer, readBytes, NULL);
        if (*errorID)
        {
            if (*errorID == IDS_EOF)
            {
                switch (ProcessError(IDS_ERRCOMPDATA, 0, FileNameDisp,
                                     PE_NORETRY | DialogFlags, &SkipAllDataErr))
                {
                case ERR_SKIP:
                    exitCode = DEC_SKIP;
                    *errorID = 0;
                    break;
                case ERR_CANCEL:
                    exitCode = DEC_CANCEL;
                    *errorID = IDS_NODISPLAY;
                }
            }
            else
            {
                exitCode = DEC_CANCEL;
            }
            break;
        }
        else
        {
            bytesLeft -= readBytes;
            if (Encrypted)
            {
                if (AESContextValid)
                {
                    SalamanderCrypt->AESDecrypt(&AESContext, InputBuffer, readBytes);
                    if (bytesLeft == 0)
                    {
                        // final chunk, verify the authentication code
                        unsigned char mac[AES_MAXHMAC];
                        unsigned char macFile[AES_MAXHMAC];
                        int mode = AESContext.mode;

                        *errorID = SafeRead(macFile, SAL_AES_MAC_LENGTH(mode), NULL);
                        if (*errorID)
                        {
                            if (*errorID == IDS_EOF)
                            {
                                switch (ProcessError(IDS_ERRCOMPDATA, 0, FileNameDisp,
                                                     PE_NORETRY | DialogFlags, &SkipAllDataErr))
                                {
                                case ERR_SKIP:
                                    exitCode = DEC_SKIP;
                                    *errorID = 0;
                                    break;
                                case ERR_CANCEL:
                                    exitCode = DEC_CANCEL;
                                    *errorID = IDS_NODISPLAY;
                                }
                            }
                            else
                            {
                                exitCode = DEC_CANCEL;
                            }
                            break;
                        }

                        DWORD macLen;
                        SalamanderCrypt->AESEnd(&AESContext, mac, &macLen);
                        AESContextValid = FALSE;

                        if (memcmp(mac, macFile, macLen) != 0)
                        {
                            switch (ProcessError(IDS_MACERROR, 0, FileNameDisp,
                                                 PE_NORETRY | DialogFlags, &SkipAllDataErr))
                            {
                            case ERR_SKIP:
                                exitCode = DEC_SKIP;
                                *errorID = 0;
                                break;
                            case ERR_CANCEL:
                                exitCode = DEC_CANCEL;
                                *errorID = IDS_NODISPLAY;
                            }
                            break;
                        }
                    }
                }
                else
                    Decrypt(InputBuffer, readBytes, Keys);
            }
            Crc = SalamanderGeneral->UpdateCrc32(InputBuffer, readBytes, Crc);
            if (!Test)
            {
                result = Write(OutputFile, InputBuffer, readBytes, &SkipAllIOErrors);
                if (result)
                {
                    switch (result)
                    {
                    case ERR_SKIP:
                        exitCode = DEC_SKIP;
                        break;
                    case ERR_CANCEL:
                        exitCode = DEC_CANCEL;
                        *errorID = IDS_NODISPLAY;
                    }
                    break;
                }
            }
            if (!Salamander->ProgressAddSize(readBytes, TRUE))
                UserBreak = true;
        }
    }
    return exitCode;
}

int CZipUnpack::ExplodeFile(CFileInfo* fileInfo, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::ExplodeFile(, )");
    CDecompressionObject decompress;
    COutputManager output;
    CInputManager input;
    int exitCode = DEC_NOERROR;

    if ((exitCode = GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    decompress.csize = BytesLeft;
    Crc = INIT_CRC;
    input.NextByte = (__UINT8*)InputBuffer;
    input.BytesLeft = 0;
    input.Error = 0;
    input.Refill = Refill;
    output.SlideWin = (__UINT8*)SlideWindow;
    output.WinSize = WinSize;
    output.Flush = ExtractFlush;
    decompress.Input = &input;
    decompress.Output = &output;
    decompress.UserData = this;
    decompress.HeapInfo = (void*)Heap;
    decompress.ucsize = fileInfo->Size;
    decompress.Flag = fileInfo->Flag;
    switch (Explode(&decompress))
    {
    case 1:;
    case 2:
    {
    BadData:
        switch (ProcessError(IDS_ERRCOMPDATA, 0, FileNameDisp,
                             PE_NORETRY | DialogFlags, &SkipAllDataErr))
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
        break;
    }

    case 3:
        exitCode = DEC_CANCEL;
        *errorID = IDS_LOWMEM;
        break;

    case 4:
    {
        if (decompress.Input->Error == IDS_EOF)
            goto BadData;
        if (decompress.Input->Error == IDS_MACERROR)
        {
            switch (ProcessError(IDS_MACERROR, 0, FileNameDisp,
                                 PE_NORETRY | DialogFlags, &SkipAllDataErr))
            {
            case ERR_SKIP:
                exitCode = DEC_SKIP;
                break;
            case ERR_CANCEL:
                exitCode = DEC_CANCEL;
                *errorID = IDS_NODISPLAY;
            }
        }
        else
        {
            exitCode = DEC_CANCEL;
            *errorID = decompress.Input->Error;
        }
        break;
    }

    case 5:
    {
        switch (OutputError)
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
    }
    }
    return exitCode;
}

int CZipUnpack::UnShrinkFile(CFileInfo* fileInfo, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::UnShrinkFile(, )");
    CDecompressionObject decompress;
    COutputManager output;
    CInputManager input;
    int exitCode = DEC_NOERROR;
    //int                  result;

    if ((exitCode = GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    decompress.CompBytesLeft = BytesLeft;
    Crc = INIT_CRC;
    input.NextByte = (__UINT8*)InputBuffer;
    input.BytesLeft = 0;
    input.Error = 0;
    input.Refill = Refill;
    output.SlideWin = (__UINT8*)SlideWindow;
    output.WinSize = WinSize;
    output.Flush = ExtractFlush;
    if (!OutputBuffer)
    {
        OutputBuffer = (char*)malloc(OUTPUT_BUFFER_SIZE);
        if (!OutputBuffer)
        {
            *errorID = IDS_LOWMEM;
            return DEC_CANCEL;
        }
        OutBufSize = OUTPUT_BUFFER_SIZE;
    }
    output.OutBuf = (uch*)OutputBuffer;
    output.BufSize = OutBufSize;
    decompress.Input = &input;
    decompress.Output = &output;
    decompress.UserData = this;
    decompress.HeapInfo = (void*)Heap;
    Unshrinking = true;
    switch (Unshrink(&decompress))
    {
    case 1:
    {
        if (decompress.Input->Error == IDS_EOF ||
            decompress.Input->Error == IDS_MACERROR)
        {
            switch (ProcessError(
                decompress.Input->Error == IDS_MACERROR ? IDS_MACERROR : IDS_ERRCOMPDATA,
                0, FileNameDisp,
                PE_NORETRY | DialogFlags, &SkipAllDataErr))
            {
            case ERR_SKIP:
                exitCode = DEC_SKIP;
                break;
            case ERR_CANCEL:
                exitCode = DEC_CANCEL;
                *errorID = IDS_NODISPLAY;
            }
        }
        else
        {
            exitCode = DEC_CANCEL;
            *errorID = decompress.Input->Error;
        }
        break;
    }

    case 2:
    {
        switch (OutputError)
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
    }
    }
    Unshrinking = false;
    return exitCode;
}

int CZipUnpack::UnReduceFile(CFileInfo* fileInfo, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::UnReduceFile(, )");
    CDecompressionObject decompress;
    COutputManager output;
    CInputManager input;
    int exitCode = DEC_NOERROR;

    if ((exitCode = GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    decompress.CompBytesLeft = BytesLeft;
    Crc = INIT_CRC;
    input.NextByte = (__UINT8*)InputBuffer;
    input.BytesLeft = 0;
    input.Error = 0;
    input.Refill = Refill;
    output.SlideWin = (__UINT8*)SlideWindow;
    output.WinSize = WinSize;
    output.Flush = ExtractFlush;
    decompress.Input = &input;
    decompress.Output = &output;
    decompress.UserData = this;
    decompress.HeapInfo = (void*)Heap;
    decompress.ucsize = fileInfo->Size;
    decompress.Method = fileInfo->Method;
    switch (Unreduce(&decompress))
    {
    case 1:
    {
        if (decompress.Input->Error == IDS_EOF ||
            decompress.Input->Error == IDS_MACERROR)
        {
            switch (ProcessError(
                decompress.Input->Error == IDS_MACERROR ? IDS_MACERROR : IDS_ERRCOMPDATA,
                0, FileNameDisp,
                PE_NORETRY | DialogFlags, &SkipAllDataErr))
            {
            case ERR_SKIP:
                exitCode = DEC_SKIP;
                break;
            case ERR_CANCEL:
                exitCode = DEC_CANCEL;
                *errorID = IDS_NODISPLAY;
            }
        }
        else
        {
            exitCode = DEC_CANCEL;
            *errorID = decompress.Input->Error;
        }
        break;
    }

    case 2:
    {
        switch (OutputError)
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
        }
    }
    }
    return exitCode;
}

int CZipUnpack::UnBZIP2File(CFileInfo* fileInfo, int* errorID)
{
    CALL_STACK_MESSAGE1("CZipUnpack::UnBZIP2File(, )");
    CDecompressionObject decompress;
    COutputManager output;
    CInputManager input;
    int ret, exitCode = DEC_NOERROR;

    memset(&decompress, 0, sizeof(decompress));
    if ((exitCode = GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)) != DEC_NOERROR)
        return exitCode;
    ZipFile->FilePointer = fileInfo->DataOffset;
    decompress.CompBytesLeft = BytesLeft;
    Crc = INIT_CRC;
    input.NextByte = (__UINT8*)InputBuffer;
    input.BytesLeft = 0;
    input.Error = 0;
    input.Refill = Refill;
    output.SlideWin = (__UINT8*)SlideWindow;
    output.WinSize = WinSize;
    output.Flush = ExtractFlush;
    decompress.Input = &input;
    decompress.Output = &output;
    decompress.UserData = this;
    decompress.ucsize = fileInfo->Size;
    switch (ret = UnBZIP2(&decompress))
    {
    case 1:
    {
        if (decompress.Input->Error == IDS_EOF ||
            decompress.Input->Error == IDS_MACERROR)
        {
            switch (ProcessError(
                decompress.Input->Error == IDS_MACERROR ? IDS_MACERROR : IDS_ERRCOMPDATA,
                0, FileNameDisp,
                PE_NORETRY | DialogFlags, &SkipAllDataErr))
            {
            case ERR_SKIP:
                exitCode = DEC_SKIP;
                break;
            case ERR_CANCEL:
                exitCode = DEC_CANCEL;
                *errorID = IDS_NODISPLAY;
            }
        }
        else
        {
            exitCode = DEC_CANCEL;
            *errorID = decompress.Input->Error;
        }
        break;
    }

    case 2:
    {
        switch (OutputError)
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
            break;
        }
        break;
    }
    case 3: // Out of memory
    case 4: // Error uncompressing BZIP2 stream
        switch (ProcessError(
            ret == 3 ? IDS_LOWMEM : IDS_ERRBZIP2,
            0, FileNameDisp,
            PE_NORETRY | DialogFlags, &SkipAllDataErr))
        {
        case ERR_SKIP:
            exitCode = DEC_SKIP;
            break;
        case ERR_CANCEL:
            exitCode = DEC_CANCEL;
            *errorID = IDS_NODISPLAY;
            break;
        }
        break;
    }
    return exitCode;
}

int CZipUnpack::ExtractSingleFile(char* targetDir, int targetDirLen,
                                  CFileInfo* fileInfo, BOOL* success, const char* newFileName)
{
    CALL_STACK_MESSAGE2("CZipUnpack::ExtractSingleFile(, %d, , )", targetDirLen);
    CLocalFileHeader* localHeader;
    LPTSTR pathBuf;
    LPTSTR path;
    LPTSTR name;
    LPCTSTR sour;
    LPTSTR dest;
    int errorID = 0;
    //bool                retry;
    //bool                reopenZipFile;
    int result;
    bool skip, bCheckCRC = true;
    char errBuf[128];
    CAESExtraField aesExtraField;
    /*
  TRACE_I("Unpacking file: " << fileInfo->Name <<
          ", method: " << fileInfo->Method <<
          ", flag: " << fileInfo->Flag <<
          ", isdir: " << fileInfo->IsDir <<
          ", file attr:" << fileInfo->FileAttr);
*/
    AESContextValid = FALSE; // initialization
    if (success)
        *success = FALSE;
    const char* entryPath = fileInfo->Name + RootLen + (RootLen ? 1 : 0);
    const int extractionRootLength = targetDirLen;
    if (IsUnsafeArchiveEntryPath(entryPath))
    {
        // Validate before appending anything to targetDir so a hostile name never reaches a filesystem API.
        TRACE_E("Refusing archive entry outside extraction root: " << entryPath);
        return IDS_UNSAFEEXTRACTPATH;
    }
    CExtractionRootHandle extractionRoot(targetDir);
    if (!extractionRoot.IsValid())
    {
        // Fail closed when the original root cannot be pinned for post-open containment verification.
        TRACE_E("Unable to hold archive extraction root for containment verification: " << targetDir);
        return IDS_UNSAFEEXTRACTPATH;
    }
    localHeader = (CLocalFileHeader*)malloc(MAX_HEADER_SIZE);
    pathBuf = (LPTSTR)malloc(sizeof(TCHAR) * (ZIP_MAX_PATH + 1));
    if (!localHeader || !pathBuf)
    {
        if (localHeader)
            free(localHeader);
        if (pathBuf)
            free(pathBuf);
        return IDS_LOWMEM;
    }
    *(targetDir + targetDirLen++) = '\\';
    if (DiskNum != fileInfo->StartDisk && MultiVol)
    {
        DiskNum = fileInfo->StartDisk;
        errorID = ChangeDisk();
    }
    if (!errorID)
    {
        errorID = ReadLocalHeader(localHeader, fileInfo->LocHeaderOffs);
        if (!errorID)
        {
            if (!ProcessLocalHeader(localHeader, fileInfo, &aesExtraField))
            {
                // Restore the caller buffer before returning the malformed archive boundary failure.
                *(targetDir + --targetDirLen) = 0;
                free(localHeader);
                free(pathBuf);
                return IDS_ERRFORMAT;
            }
            path = pathBuf;
            SplitPath(&path, &name, entryPath);
            if (newFileName)
                name = (LPTSTR)newFileName;
            sour = path;
            dest = targetDir + targetDirLen;
            if (*sour)
            {
                while (*sour)
                    *dest++ = *sour++;
                *dest++ = '\\';
            }
            if (fileInfo->IsDir)
            {
                if (!Test)
                {
                    //*dest++ = '\\';
                    sour = name;
                    while (*sour)
                        *dest++ = *sour++;
                    *dest = 0;
                    bool retry;
                    do
                    {
                        retry = false;
                        if (HasReparsePointInExtractionPath(targetDir, extractionRootLength))
                            errorID = IDS_UNSAFEEXTRACTPATH;
                        else if (!SalamanderGeneral->CheckAndCreateDirectory(targetDir, NULL, TRUE, errBuf, 128))
                        {
                            switch (ProcessError(IDS_ERRCREATEDIR, 0, targetDir, DialogFlags,
                                                 &SkipAllIOErrors, errBuf))
                            {
                            case ERR_RETRY:
                                retry = true;
                                break;
                            case ERR_CANCEL:
                                errorID = IDS_NODISPLAY;
                            }
                        }
                    } while (retry);
                    if (!errorID && HasReparsePointInExtractionPath(targetDir, extractionRootLength))
                        errorID = IDS_UNSAFEEXTRACTPATH;
                    if (!errorID)
                    {
                        SetFileAttributes(targetDir, fileInfo->FileAttr & FILE_ATTTRIBUTE_MASK);
                        if (success)
                        {
                            *success = TRUE;
                        }
                    }
                }
                else if (success)
                {
                    *success = TRUE;
                }
            }
            else
            {
                *dest = 0;
                skip = false;
                bool retry;
                if (!Test)
                {
                    do
                    {
                        retry = false;
                        if (HasReparsePointInExtractionPath(targetDir, extractionRootLength))
                            errorID = IDS_UNSAFEEXTRACTPATH;
                        else if (!SalamanderGeneral->CheckAndCreateDirectory(targetDir, NULL, TRUE, errBuf, 128))
                        {
                            switch (ProcessError(IDS_ERRCREATEDIR, 0, targetDir, DialogFlags,
                                                 &SkipAllIOErrors, errBuf))
                            {
                            case ERR_RETRY:
                                retry = true;
                                break;
                            case ERR_CANCEL:
                                errorID = IDS_NODISPLAY;
                            }
                        }
                    } while (retry);
                }
                if (!errorID && !Test && HasReparsePointInExtractionPath(targetDir, extractionRootLength))
                    errorID = IDS_UNSAFEEXTRACTPATH;
                if (!errorID)
                {
                    sour = name;
                    while (*sour)
                        *dest++ = *sour++;
                    *dest = 0;
                    FileNameDisp = fileInfo->Name + RootLen + (RootLen ? 1 : 0);
                    skip = false;
                    if (fileInfo->Flag & GPF_ENCRYPTED)
                    {
                        if (SkipAllEncrypted)
                            skip = true;
                        else
                        {
                            if (fileInfo->Method == CM_AES)
                            {
                                if (aesExtraField.HeaderID != AES_HEADER_ID ||
                                    aesExtraField.DataSize < sizeof(CAESExtraField) - 4 ||
                                    aesExtraField.Strength < 1 ||
                                    aesExtraField.Strength > 3)
                                {
                                    switch (ProcessError(IDS_BADAES, 0, FileNameDisp,
                                                         PE_NORETRY | DialogFlags, &SkipAllEncrypted))
                                    {
                                    case ERR_SKIP:
                                        skip = true;
                                        break;
                                    default:
                                        errorID = IDS_NODISPLAY;
                                        break;
                                    }
                                }
                                else
                                {
                                    if (aesExtraField.VendorID != AES_NONVENDOR_ID ||
                                        ((AES_VERSION_1 != aesExtraField.Version) && (AES_VERSION_2 != aesExtraField.Version)))
                                        TRACE_E("WARNING: file '" << FileNameDisp << "' is encrypted with unknown AES version, possible complications");

                                    char pwd[MAX_PASSWORD];
                                    unsigned char salt[SAL_AES_MAX_SALT_LENGTH];
                                    WORD pwdVer;
                                    WORD pwdVerFile;
                                    bool repeat;

                                    // AES v2 doesn't store CRC, seems to be created by TC
                                    if (AES_VERSION_2 == aesExtraField.Version)
                                        bCheckCRC = false;
                                    QWORD aesPayloadOffset;
                                    const QWORD aesHeaderSize = SAL_AES_SALT_LENGTH(aesExtraField.Strength) + sizeof(pwdVer);
                                    // Archive-controlled local offsets must not wrap while skipping the AES salt and verifier.
                                    if (!CheckedAddUInt64(fileInfo->DataOffset, aesHeaderSize, &aesPayloadOffset))
                                        errorID = IDS_ERRFORMAT;
                                    else
                                    {
                                        ZipFile->FilePointer = fileInfo->DataOffset;
                                        errorID = SafeRead(salt, SAL_AES_SALT_LENGTH(aesExtraField.Strength), NULL);
                                    }
                                    if (!errorID)
                                        SafeRead(&pwdVerFile, sizeof(pwdVerFile), NULL);
                                    if (!errorID)
                                    {
                                        // try the cached passwords
                                        int i;
                                        for (i = 0; i < Passwords.Count; i++)
                                        {
                                            if (SalamanderCrypt->AESInit(&AESContext, aesExtraField.Strength,
                                                                         Passwords[i], strlen(Passwords[i]),
                                                                         salt, &pwdVer) == SAL_AES_ERR_GOOD_RETURN)
                                            {
                                                if (memcmp(&pwdVer, &pwdVerFile, sizeof(pwdVerFile)) == 0)
                                                {
                                                    fileInfo->DataOffset = aesPayloadOffset;
                                                    Encrypted = true;
                                                    AESContextValid = TRUE;
                                                    fileInfo->Method = aesExtraField.Method;
                                                    break;
                                                }
                                                else
                                                {
                                                    unsigned char dummy[AES_MAXHMAC];
                                                    SalamanderCrypt->AESEnd(&AESContext, dummy, NULL);
                                                }
                                            }
                                        }
                                        if (!AESContextValid /*i >= Passwords.Count*/) // the password was not found in the cache
                                            do
                                            {
                                                repeat = false;
                                                switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(),
                                                                       FileNameDisp, pwd))
                                                {
                                                case IDOK:
                                                {
                                                    int err = 0;
                                                    switch (SalamanderCrypt->AESInit(&AESContext, aesExtraField.Strength,
                                                                                     pwd, strlen(pwd), salt, &pwdVer))
                                                    {
                                                    case SAL_AES_ERR_GOOD_RETURN:
                                                        if (memcmp(&pwdVer, &pwdVerFile, sizeof(pwdVerFile)) == 0)
                                                        {
                                                            Passwords.Add(_strdup(pwd));
                                                            fileInfo->DataOffset = aesPayloadOffset;
                                                            Encrypted = true;
                                                            AESContextValid = TRUE;
                                                            fileInfo->Method = aesExtraField.Method;
                                                        }
                                                        else
                                                        {
                                                            unsigned char dummy[AES_MAXHMAC];
                                                            SalamanderCrypt->AESEnd(&AESContext, dummy, NULL);
                                                            err = IDS_BADPWD;
                                                        }
                                                        break;
                                                    case SAL_AES_ERR_PASSWORD_TOO_LONG:
                                                        err = IDS_PWDTOOLONG;
                                                        break;
                                                    default:
                                                        err = IDS_AESERROR;
                                                        break;
                                                    }

                                                    if (err)
                                                    {
                                                        SalamanderGeneral->ShowMessageBox(LoadStr(err),
                                                                                          LoadStr(IDS_BADPWDTITLE), MSGBOX_ERROR);
                                                        repeat = true;
                                                    }
                                                    break;
                                                }

                                                case IDC_SKIPALL:
                                                    SkipAllEncrypted = true;
                                                case IDC_SKIP:
                                                    skip = true;
                                                    break;
                                                case IDCANCEL:
                                                default:
                                                    errorID = IDS_NODISPLAY;
                                                    break;
                                                }
                                            } while (repeat);
                                    }
                                }
                            }
                            else
                            {
                                char pwd[MAX_PASSWORD];
                                char check;
                                char header[ENCRYPT_HEADER_SIZE];
                                bool repeat;

                                QWORD encryptedPayloadOffset;
                                // Legacy encryption has a fixed header, but its base offset still originates in the archive.
                                if (!CheckedAddUInt64(fileInfo->DataOffset, ENCRYPT_HEADER_SIZE, &encryptedPayloadOffset))
                                    errorID = IDS_ERRFORMAT;
                                else
                                {
                                    ZipFile->FilePointer = fileInfo->DataOffset;
                                    errorID = SafeRead(header, ENCRYPT_HEADER_SIZE, NULL);
                                }
                                if (!errorID)
                                {
                                    check = fileInfo->Flag & GPF_DATADESCR ? localHeader->Time >> 8 : fileInfo->Crc >> 24;
                                    bool bFound = false;
                                    int i;
                                    for (i = 0; i < Passwords.Count; i++)
                                        if (!InitKeys(Passwords[i], header, check, Keys))
                                        {
                                            fileInfo->DataOffset = encryptedPayloadOffset;
                                            Encrypted = bFound = true;
                                            break;
                                        }
                                    //                    if (i >= Passwords.Count)// pwd not found in cache
                                    if (!bFound) // pwd not found in cache
                                        do
                                        {
                                            repeat = false;
                                            switch (PasswordDialog(SalamanderGeneral->GetMsgBoxParent(), FileNameDisp, pwd))
                                            {
                                            case IDOK:
                                                if (InitKeys(pwd, header, check, Keys))
                                                {
                                                    SalamanderGeneral->ShowMessageBox(LoadStr(IDS_BADPWD), LoadStr(IDS_BADPWDTITLE), MSGBOX_ERROR);
                                                    repeat = true;
                                                }
                                                else
                                                {
                                                    Passwords.Add(_strdup(pwd));
                                                    fileInfo->DataOffset = encryptedPayloadOffset;
                                                    Encrypted = true;
                                                }
                                                break;
                                            case IDC_SKIPALL:
                                                SkipAllEncrypted = true;
                                            case IDC_SKIP:
                                                skip = true;
                                                break;
                                            case IDCANCEL:
                                            default:
                                                errorID = IDS_NODISPLAY;
                                                break;
                                            }
                                        } while (repeat);
                                }
                            }
                        }
                        if (skip)
                            UserBreak = !ProgressAddSize(fileInfo->Size);
                    }
                    else
                        Encrypted = false;
                    if (!errorID && !skip)
                    {
                        if (!Test)
                        {
                            char attr[101];
                            char buf[MAX_PATH];
                            // The fixed MAX_PATH composition below uses an int length contract.
                            int len = static_cast<int>(strlen(ZipName));

                            // counted composition of the target path inside its fixed buffer
                            if (len >= _countof(buf) - 2 ||
                                FAILED(StringCchCopyA(buf, _countof(buf), ZipName)))
                            {
                                errorID = IDS_TOOLONGNAME3;
                            }
                            else
                            {
                                *(buf + len++) = '\\';
                                StringCchCopyNA(buf + len, _countof(buf) - len, fileInfo->Name, _countof(buf) - len); // counted bounded copy instead of lstrcpyn
                            }
                            if (!errorID)
                                GetInfo(attr, &fileInfo->LastWrite, fileInfo->Size);
                            if (HasReparsePointInExtractionPath(targetDir, extractionRootLength))
                                errorID = IDS_UNSAFEEXTRACTPATH;
                            else
                                result = SafeCreateCFile(&OutputFile, targetDir, buf, attr, GENERIC_WRITE,
                                                         FILE_SHARE_READ, fileInfo->FileAttr & ~FILE_ATTRIBUTE_READONLY | FILE_FLAG_SEQUENTIAL_SCAN,
                                                         DialogFlags, &Silent, &SkipAllIOErrors, fileInfo->Size);
                            if (!errorID && !result && !extractionRoot.Contains(OutputFile->File))
                            {
                                // Do not write archive bytes when the opened object resolved outside the pinned root.
                                CloseCFile(OutputFile);
                                OutputFile = NULL;
                                errorID = IDS_UNSAFEEXTRACTPATH;
                            }
                        }
                        else
                        {
                            result = 0;
                            ExtractedBytes = 0;
                        }
                        if (!errorID && result)
                        {
                            switch (result)
                            {
                            case ERR_LOWMEM:
                                errorID = IDS_LOWMEM;
                                break;
                            case ERR_SKIP:
                                UserBreak = !ProgressAddSize(fileInfo->Size);
                                break;
                            case ERR_CANCEL:
                                errorID = IDS_NODISPLAY;
                            }
                        }
                        else if (!errorID)
                        {
                            switch (fileInfo->Method)
                            {
                            case CM_DEFLATE64:
                                result = InflateFile(fileInfo, TRUE, &errorID);
                                break;
                            case CM_DEFLATED:
                                result = InflateFile(fileInfo, FALSE, &errorID);
                                break;
                            case CM_STORED:
                                result = UnStoreFile(fileInfo, &errorID);
                                break;
                            case CM_IMPLODED:
                                result = ExplodeFile(fileInfo, &errorID);
                                break;
                            case CM_SHRINKED:
                                result = UnShrinkFile(fileInfo, &errorID);
                                break;
                            case CM_REDUCED1:
                            case CM_REDUCED2:
                            case CM_REDUCED3:
                            case CM_REDUCED4:
                                result = UnReduceFile(fileInfo, &errorID);
                                break;
                            case CM_BZIP2:
                                result = UnBZIP2File(fileInfo, &errorID);
                                break;
                            default:
                            {
                                switch (ProcessError(IDS_BADMETHOD, 0, FileNameDisp,
                                                     PE_NORETRY | DialogFlags, &SkipAllBadMathods))
                                {
                                case ERR_SKIP:
                                    result = DEC_SKIP;
                                    break;
                                case ERR_CANCEL:
                                    result = DEC_CANCEL;
                                    errorID = IDS_NODISPLAY;
                                }
                            }
                            } //switch (fileHeader->Method)
                            QWORD remain;
                            if (!Test)
                            {
                                remain = fileInfo->Size - OutputFile->FilePointer;
                                if (result == DEC_NOERROR)
                                {
                                    switch (Flush(OutputFile, OutputFile->OutputBuffer, OutputFile->BufferPosition, &SkipAllIOErrors))
                                    {
                                    case ERR_NOERROR:
                                        result = DEC_NOERROR;
                                        break;
                                    case ERR_SKIP:
                                        result = DEC_SKIP;
                                        break;
                                    case ERR_CANCEL:
                                        result = DEC_CANCEL;
                                        break;
                                    }
                                }
                                SetFileTime(OutputFile->File, &fileInfo->LastWrite,
                                            NULL, &fileInfo->LastWrite);
                                CloseCFile(OutputFile);
                                if (result || UserBreak)
                                    DeleteFile(targetDir);
                                else
                                    SetFileAttributes(targetDir, fileInfo->FileAttr & FILE_ATTTRIBUTE_MASK);
                            }
                            else
                                remain = fileInfo->Size - ExtractedBytes;
                            if (!UserBreak)
                            {
                                switch (result)
                                {
                                case DEC_NOERROR:
                                {
                                    // sometimes the local header stores different data than the
                                    // central header, so compare the CRC with both
                                    if (bCheckCRC && ((Crc != fileInfo->Crc) &&
                                                      ((fileInfo->Flag & GPF_DATADESCR) || Crc != localHeader->Crc)))
                                    {
                                        if (ProcessError(IDS_ERRCRC, 0, FileNameDisp, PE_NORETRY | DialogFlags,
                                                         &SkipAllDataErr) == ERR_CANCEL)
                                        {
                                            result = DEC_CANCEL;
                                            errorID = IDS_NODISPLAY;
                                        }
                                        if (!Test)
                                        {
                                            SalamanderGeneral->ClearReadOnlyAttr(targetDir);
                                            DeleteFile(targetDir);
                                        }
                                    }
                                    else
                                    {
                                        if (success)
                                        {
                                            *success = TRUE;
                                        }
                                    }
                                    break;
                                }
                                case DEC_SKIP:
                                    UserBreak = !ProgressAddSize(remain);
                                    break;
                                case DEC_CANCEL:
                                    break;
                                }
                            }
                        }
                    }
                }
            }
#ifdef TRACE_ENABLE
            if (dest - targetDir >= ZIP_MAX_PATH)
                TRACE_E("Max path length exceeded");
#endif
        }
    }
    *(targetDir + --targetDirLen) = 0;
    free(localHeader);
    free(pathBuf);
    if (AESContextValid)
    {
        unsigned char dummy[AES_MAXHMAC];
        SalamanderCrypt->AESEnd(&AESContext, dummy, NULL);
    }
    return errorID;
} /* CZipUnpack::ExtractSingleFile */

int CZipUnpack::ExtractFiles(const char* targetDir)
{
    CALL_STACK_MESSAGE2("CZipUnpack::ExtractFiles(%s)", targetDir);
    CLocalFileHeader* localHeader;
    CFileInfo* fileInfo;
    char* tempDir;
    int tempDirLen;
    LPTSTR progrTextBuf;
    LPTSTR progrText;
    const char* sour;
    //int                 rootLen = lstrlen(ZipRoot);
    int errorID = 0;
    int i;

    InputBuffer = (char*)malloc(DECOMPRESS_INBUFFER_SIZE);
    InBufSize = DECOMPRESS_INBUFFER_SIZE;
    SlideWindow = (char*)malloc(SLIDE_WINDOW_SIZE);
    WinSize = SLIDE_WINDOW_SIZE;
    localHeader = (CLocalFileHeader*)malloc(MAX_HEADER_SIZE);
    tempDir = (char*)malloc(sizeof(TCHAR) * (ZIP_MAX_PATH + 1));
    progrTextBuf = (LPTSTR)malloc(sizeof(TCHAR) * (ZIP_MAX_PATH + 32));
    if (!localHeader || !tempDir || !progrTextBuf ||
        !InputBuffer || !SlideWindow)
    {
        if (localHeader)
            free(localHeader);
        if (tempDir)
            free(tempDir);
        if (progrTextBuf)
            free(progrTextBuf);
        if (InputBuffer)
            free(InputBuffer);
        if (SlideWindow)
            free(SlideWindow);
        return IDS_LOWMEM;
    }
    /*
  if (!SalGetTempFileName(targetDir, "Sal", tempDir, FALSE, NULL))
    errorID = IDS_ERRTEMPDIR;
  else
  {
  */
    // the allocation is sized ZIP_MAX_PATH+1, so bound the copy of the extraction root by it
    StringCchCopyA(tempDir, ZIP_MAX_PATH + 1, targetDir);
    // Temporary extraction paths use the plugin's legacy int length contract.
    tempDirLen = static_cast<int>(strlen(tempDir));
    if (tempDirLen && tempDir[tempDirLen - 1] == '\\')
    {
        tempDir[tempDirLen - 1] = 0;
        tempDirLen--;
    }
    if (!Test && !SalamanderGeneral->TestFreeSpace(SalamanderGeneral->GetMsgBoxParent(),
                                                   targetDir, ProgressTotalSize, LoadStr(IDS_PLUGINNAME)))
        errorID = IDS_NODISPLAY;
    {
        progrText = progrTextBuf;
        sour = LoadStr(Test ? IDS_TESTING : IDS_EXTRACTING);
        while (*sour)
            *progrText++ = *sour++;
        fixed_tl64 = NULL; //for
        fixed_td64 = NULL;
        fixed_tl32 = NULL; //for
        fixed_td32 = NULL;
        DialogFlags = 0;
        SkipAllIOErrors = 0;
        SkipAllLongNames = 0;
        SkipAllEncrypted = 0;
        SkipAllDataErr = 0;
        SkipAllBadMathods = 0;
        Silent = 0;
        ProgressTotalSize += CQuadWord(ExtrFiles->Count, 0);
        Salamander->ProgressDialogAddText(LoadStr(Test ? IDS_TESTFILES : IDS_EXTRACTFILES), FALSE);
        for (i = 0; i < ExtrFiles->Count && !errorID && !UserBreak; i++)
        {
            fileInfo = (*ExtrFiles)[i];
            if (tempDirLen + 1 + fileInfo->NameLen - RootLen - (RootLen ? 1 : 0) >=
                (DWORD)(Test ? ZIP_MAX_PATH : MAX_PATH - (fileInfo->IsDir ? 12 : 0)))
            {
                switch (ProcessError(IDS_TOOLONGNAME3, 0, fileInfo->Name + RootLen + (RootLen ? 1 : 0),
                                     PE_NORETRY | DialogFlags, &SkipAllLongNames))
                {
                case ERR_SKIP:
                    UserBreak = !ProgressAddSize(fileInfo->Size + 1);
                    continue;
                case ERR_CANCEL:
                    errorID = IDS_NODISPLAY;
                }
                break;
            }
            // bounded by the length guard above; copy with an explicit capacity anyway
            StringCchCopyA(progrText, ZIP_MAX_PATH + 32, fileInfo->Name + RootLen + (RootLen ? 1 : 0));
            Salamander->ProgressDialogAddText(progrTextBuf, TRUE);
            if (Salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE))
            {
                Salamander->ProgressSetTotalSize(CQuadWord().SetUI64(fileInfo->Size), ProgressTotalSize);
                BOOL ok;
                errorID = ExtractSingleFile(tempDir, tempDirLen, fileInfo, &ok);
                if (!ok)
                    AllFilesOK = FALSE; // for archive testing
                UserBreak = !Salamander->ProgressAddSize(1, TRUE);
            }
            else
                UserBreak = true;
        }
        InflateFreeFixedHufman();
    }
    /*
    Salamander->CloseProgressDialog();
    if (!errorID && !UserBreak)
    {
      char  buf[MAX_PATH + 1];
      int   len;

      StringCchCopyNA(buf, MAX_PATH + 1, ZipName, MAX_PATH + 1); // counted bounded copy instead of lstrcpyn
      len = strlen(buf); // CRT length instead of the legacy Win32 length API
      if (RootLen)
      {
        *(buf + len) = '\\';
        len++;
      }
      StringCchCopyNA(buf + len, MAX_PATH + 1 - strlen(buf), ZipRoot, MAX_PATH + 1 - strlen(buf)); // counted bounded copy instead of lstrcpyn // CRT length instead of the legacy Win32 length API
      Salamander->MoveFiles(tempDir, targetDir, tempDir, buf);
    }
    SalamanderGeneral->RemoveTemporaryDir(tempDir);
  }
  */
    free(localHeader);
    free(tempDir);
    free(progrTextBuf);
    free(InputBuffer);
    free(SlideWindow);
    return errorID;
}

int CZipUnpack::SafeRead(void* buffer, unsigned bytesToRead, bool* skipAll)
{
    CALL_STACK_MESSAGE2("CZipUnpack::SafeRead(, 0x%X, )", bytesToRead);
    unsigned read, readTotal = 0;
    int err = 0;

    while (!err)
    {
        if (Read(ZipFile, (char*)buffer + readTotal, bytesToRead, &read, skipAll))
            return IDS_NODISPLAY;

        readTotal += read;
        bytesToRead -= read;

        if (bytesToRead == 0)
            break;

        if (!MultiVol)
        {
            Fatal = true;
            return IDS_EOF;
        }

        DiskNum++;
        err = ChangeDisk();
    }

    return err;
}

/*
int CZipUnpack::SafeRead(void * buffer, unsigned bytesToRead,
                         unsigned * bytesRead, bool * skipAll)
{
  CALL_STACK_MESSAGE2("CZipUnpack::SafeRead(, 0x%X, , )", bytesToRead);
  unsigned  read;
  int       err = 0;
  
  if (Read(ZipFile, buffer, bytesToRead, &read, skipAll))
  {
    err = IDS_NODISPLAY;
  }
  else
  {
    if (!read)
    {
      if (MultiVol)
      {
        DiskNum++;
        err = ChangeDisk();
        if (!err)
        {
          if (Read(ZipFile, buffer, bytesToRead, &read, skipAll))
          {
            err = IDS_NODISPLAY;
          }
          else
          {
            if (!read)
            {
              err = IDS_EOF;
            }
          }
        }
      }
      else
      {
        Fatal = true;
        err = IDS_EOF;
      }
    }
  }
  *bytesRead = read;
  return err;
}
*/

int CZipUnpack::SafeCreateCFile(CFile** file, const char* fileName, const char* arcName,
                                const char* fileData, unsigned int access, unsigned int share,
                                unsigned int attributes, int flags, DWORD* silent,
                                bool* skipAll, QWORD size)
{
    CALL_STACK_MESSAGE8("CZipUnpack::SafeCreateCFile(, %s, %s, %s, 0x%X, 0x%X, "
                        "0x%X, %d, , )",
                        fileName, arcName, fileData, access,
                        share, attributes, flags);
    int result; //temp variable
    int errorID = 0;
    int lastError; //value returned by GetLastError()
    BOOL toSkip = FALSE;
    int flagsNoRetry;
    size_t fileNameBytes;

    // The output name may include an archive entry; prove its terminator allocation before creating any file state.
    if (fileName == NULL || !CheckedAddSize(strlen(fileName), 1, &fileNameBytes))
        return ERR_LOWMEM;

    *file = (CFile*)malloc(sizeof(CFile));
    if (*file == NULL)
        return ERR_LOWMEM;
    (*file)->FileName = (char*)malloc(fileNameBytes);
    if ((*file)->FileName == NULL)
    {
        free(*file);
        *file = NULL;
        return ERR_LOWMEM;
    }
    (*file)->OutputBuffer = NULL;
    (*file)->InputBuffer = NULL;
    if (access & GENERIC_WRITE &&
        ((*file)->OutputBuffer = (char*)malloc(OUTPUT_BUFFER_SIZE)) == NULL)
    {
        free((*file)->FileName);
        free(*file);
        *file = NULL;
        return ERR_LOWMEM;
    }
    else
    {
        flagsNoRetry = 0;
        while (1)
        {
            CQuadWord q = CQuadWord().SetUI64(size);
            bool allocate = AllocateWholeFile &&
                            CQuadWord(2, 0) < q && q < CQuadWord(0, 0x80000000);
            if (TestAllocateWholeFile)
                q += CQuadWord(0, 0x80000000);

            (*file)->File = SalamanderSafeFile->SafeFileCreate(fileName, access, share, attributes,
                                                               FALSE, SalamanderGeneral->GetMsgBoxParent(), arcName, fileData,
                                                               silent, TRUE, &toSkip, NULL, 0, allocate ? &q : NULL, NULL);
            if ((*file)->File != INVALID_HANDLE_VALUE)
            {
                // the allocation is sized from the same measured length, so copy that exact span
                memcpy((*file)->FileName, fileName, fileNameBytes);
                (*file)->FilePointer = 0;
                (*file)->RealFilePointer = 0;
                (*file)->Flags = flags;
                (*file)->BufferPosition = 0;
                (*file)->Size = 0;
                (*file)->BigFile = 1;

                if (allocate)
                {
                    if (q == CQuadWord(0, 0x80000000))
                    {
                        // allocation failed and we will not attempt it again
                        AllocateWholeFile = false;
                        TestAllocateWholeFile = false;
                    }
                    else if (q == CQuadWord(0, 0x00000000))
                    {
                        // allocation failed, but we will try again next time
                    }
                    else
                    {
                        // allocation succeeded
                        TestAllocateWholeFile = false;
                    }
                }

                // // to avoid fragmenting the disk, preallocate the target file size
                // LONG distHi = HIDWORD(size);
                // if (SetFilePointer((*file)->File, LODWORD(size), &distHi, FILE_BEGIN) != 0xFFFFFFFF &&
                //     GetLastError() == NO_ERROR)
                // {
                //   SetEndOfFile((*file)->File);
                //   // move the file pointer back to the beginning
                //   if (SetFilePointer((*file)->File, 0, NULL, FILE_BEGIN) == 0xFFFFFFFF &&
                //       GetLastError() != NO_ERROR)
                //   {
                //     errorID = IDS_ERRACCESS;
                //   }
                // }
                if (errorID == 0)
                    return 0; //OK
            }
            else
            {
                if (toSkip)
                    result = ERR_SKIP;
                else
                    result = ERR_CANCEL;
                goto SCF_ABORT;
            }
            lastError = GetLastError();
            if ((*file)->File != INVALID_HANDLE_VALUE)
                CloseHandle((*file)->File);
            result = ProcessError(errorID, lastError, fileName, flags | flagsNoRetry, skipAll);
            if (result != ERR_RETRY)
            {

            SCF_ABORT:
                free((*file)->FileName);
                if (access & GENERIC_WRITE)
                    free((*file)->OutputBuffer);
                free(*file);
                *file = NULL;
                return result;
            }
        }
    }
}

void CZipUnpack::QuickSortHeaders2(int left, int right, TIndirectArray2<CFileInfo>& headers)
{
    CALL_STACK_MESSAGE_NONE

LABEL_QuickSortHeaders2:

    int i = left, j = right;
    int pivotDiskNum = headers[(i + j) / 2]->StartDisk;
    do
    {
        while (headers[i]->StartDisk <= pivotDiskNum && i < right)
            i++;
        while (pivotDiskNum <= headers[j]->StartDisk && j > left)
            j--;
        if (i <= j)
        {
            CFileInfo* tmp = headers[i];
            headers[i] = headers[j];
            headers[j] = tmp;
            i++;
            j--;
        }
    } while (i <= j); // should they be the same?

    // the following "nice" code was replaced with a stack-saving variant (maximum log(N) recursion depth)
    //  if (left < j) QuickSortHeaders2(left, j, headers);
    //  if (i < right) QuickSortHeaders2(i, right, headers);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both "halves" need sorting; recurse into the smaller one and handle the other via goto
            {
                QuickSortHeaders2(left, j, headers);
                left = i;
                goto LABEL_QuickSortHeaders2;
            }
            else
            {
                QuickSortHeaders2(i, right, headers);
                right = j;
                goto LABEL_QuickSortHeaders2;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSortHeaders2;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSortHeaders2;
        }
    }
}

BOOL CZipUnpack::ProgressAddSize(QWORD size)
{
    CALL_STACK_MESSAGE_NONE
    while ((__int64)size > 0)
    {
        int s = (int)min(size, INT_MAX);
        if (!Salamander->ProgressAddSize(s, TRUE))
            return FALSE;
        size -= s;
    }
    return TRUE;
}
