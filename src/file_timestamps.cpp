// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <shobjidl.h>

#include "common/thread_owner.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "pack.h"
#include "execute.h"
#include "shellib.h"
#include "menu.h"

// Archive file-time stamp tracking extracted from path_utils.cpp as a mechanical
// move; CDynamicStringImp method definitions move along because they sat inside
// this section (the class declaration itself is untouched).
//
// *****************************************************************************
// CFileTimeStampsItem
//

CFileTimeStampsItem::CFileTimeStampsItem()
{
    DosFileName = FileName = SourcePath = ZIPRoot = NULL;
    memset(&LastWrite, 0, sizeof(LastWrite));
    FileSize = CQuadWord(0, 0);
    Attr = 0;
}

CFileTimeStampsItem::~CFileTimeStampsItem()
{
    if (ZIPRoot != NULL)
        free(ZIPRoot);
    if (SourcePath != NULL)
        free(SourcePath);
    if (FileName != NULL)
        free(FileName);
    if (DosFileName != NULL)
        free(DosFileName);
    DosFileName = FileName = SourcePath = ZIPRoot = NULL;
}

BOOL CFileTimeStampsItem::Set(const char* zipRoot, const char* sourcePath, const char* fileName,
                              const char* dosFileName, const FILETIME& lastWrite, const CQuadWord& fileSize,
                              DWORD attr)
{
    if (*zipRoot == '\\')
        zipRoot++;
    ZIPRoot = DupStr(zipRoot);
    if (ZIPRoot != NULL) // zip-root has no '\\' at the beginning or end
    {
        int l = (int)strlen(ZIPRoot);
        if (l > 0 && ZIPRoot[l - 1] == '\\')
            ZIPRoot[l - 1] = 0;
    }
    SourcePath = DupStr(sourcePath);
    if (SourcePath != NULL) // source-path has no trailing '\\'
    {
        int l = (int)strlen(SourcePath);
        if (l > 0 && SourcePath[l - 1] == '\\')
            SourcePath[l - 1] = 0;
    }
    FileName = DupStr(fileName);
    if (dosFileName[0] != 0)
        DosFileName = DupStr(dosFileName);
    LastWrite = lastWrite;
    FileSize = fileSize;
    Attr = attr;
    return ZIPRoot != NULL && SourcePath != NULL && FileName != NULL &&
           (DosFileName != NULL || dosFileName[0] == 0);
}

//
// *****************************************************************************
// CFileTimeStamps
//

BOOL CFileTimeStamps::AddFile(const char* zipFile, const char* zipRoot, const char* sourcePath,
                              const char* fileName, const char* dosFileName,
                              const FILETIME& lastWrite, const CQuadWord& fileSize, DWORD attr)
{
    if (ZIPFile[0] == 0)
    {
        if (strlen(zipFile) >= _countof(ZIPFile))
        {
            TRACE_E("CFileTimeStamps::AddFile(): ZIP file path is too long.");
            return FALSE;
        }
        // Archive selection must preserve the complete ZIP-file identity.
        if (FAILED(StringCchCopyA(ZIPFile, _countof(ZIPFile), zipFile)))
            return FALSE;
    }
    else
    {
        if (strcmp(zipFile, ZIPFile) != 0)
        {
            TRACE_E("Unexpected situation in CFileTimeStamps::AddFile().");
            return FALSE;
        }
    }

    CFileTimeStampsItem* item = new CFileTimeStampsItem;
    if (item == NULL ||
        !item->Set(zipRoot, sourcePath, fileName, dosFileName, lastWrite, fileSize, attr))
    {
        if (item != NULL)
            delete item;
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    // test whether it is already here (not before item construction because of string adjustment - '\\')
    int i;
    for (i = 0; i < List.Count; i++)
    {
        CFileTimeStampsItem* item2 = List[i];
        if (StrICmp(item->FileName, item2->FileName) == 0 &&
            StrICmp(item->SourcePath, item2->SourcePath) == 0)
        {
            delete item;
            return FALSE; // je uz zde, nepridavat dalsi ...
        }
    }

    List.Add(item);
    if (!List.IsGood())
    {
        delete item;
        List.ResetState();
        return FALSE;
    }
    return TRUE;
}

struct CFileTimeStampsEnum2Info
{
    TIndirectArray<CFileTimeStampsItem>* PackList;
    int Index;
};

const char* WINAPI FileTimeStampsEnum2(HWND parent, int enumFiles, const char** dosName, BOOL* isDir,
                                       CQuadWord* size, DWORD* attr, FILETIME* lastWrite, void* param,
                                       int* errorOccured)
{ // enumerujeme jen soubory, enumFiles lze tedy uplne vynechat
    if (errorOccured != NULL)
        *errorOccured = SALENUM_SUCCESS;
    CFileTimeStampsEnum2Info* data = (CFileTimeStampsEnum2Info*)param;

    if (enumFiles == -1)
    {
        if (dosName != NULL)
            *dosName = NULL;
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = CQuadWord(0, 0);
        if (attr != NULL)
            *attr = 0;
        if (lastWrite != NULL)
            memset(lastWrite, 0, sizeof(FILETIME));
        data->Index = 0;
        return NULL;
    }

    if (data->Index < data->PackList->Count)
    {
        CFileTimeStampsItem* item = data->PackList->At(data->Index++);
        if (dosName != NULL)
            *dosName = (item->DosFileName == NULL) ? item->FileName : item->DosFileName;
        if (isDir != NULL)
            *isDir = FALSE;
        if (size != NULL)
            *size = item->FileSize;
        if (attr != NULL)
            *attr = item->Attr;
        if (lastWrite != NULL)
            *lastWrite = item->LastWrite;
        return item->FileName;
    }
    else
        return NULL;
}

void CFileTimeStamps::AddFilesToListBox(HWND list)
{
    int i;
    for (i = 0; i < List.Count; i++)
    {
        char buf[MAX_PATH];
        // Archive list entries require complete root names before appending files.
        if (FAILED(StringCchCopyA(buf, _countof(buf), List[i]->ZIPRoot)))
            continue;
        if (strlen(List[i]->ZIPRoot) < _countof(buf) && SalPathAppend(buf, List[i]->FileName, _countof(buf)))
            SendMessage(list, LB_ADDSTRING, 0, (LPARAM)buf);
    }
}

void CFileTimeStamps::Remove(int* indexes, int count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        int index = indexes[count - i - 1];   // mazeme zezadu - mene sesouvani + neposouvaji se indexy
        if (index < List.Count && index >= 0) // jen tak pro sychr
        {
            List.Delete(index);
        }
    }
}

BOOL CDynamicStringImp::Add(const char* str, int len)
{
    if (len == -1)
        len = (int)strlen(str);
    else
    {
        if (len == -2)
            len = (int)strlen(str) + 1;
    }
    if (Length + len >= Allocated)
    {
        char* text = (char*)realloc(Text, Length + len + 100);
        if (text == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
        Allocated = Length + len + 100;
        Text = text;
    }
    memcpy(Text + Length, str, len);
    Length += len;
    Text[Length] = 0;
    return TRUE;
}

void CDynamicStringImp::DetachData()
{
    Text = NULL;
    Allocated = 0;
    Length = 0;
}

static HRESULT QueueUtf8CopyItems(IFileOperation* operation, const char* sources, const char* destinations)
{
    while (*sources != 0 && *destinations != 0)
    {
        CStrP sourceW(ConvertAllocUtf8ToWide(sources, -1));
        CStrP destinationW(ConvertAllocUtf8ToWide(destinations, -1));
        if (sourceW == NULL || destinationW == NULL)
            return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);

        WCHAR* copyName = wcsrchr(destinationW, L'\\');
        if (copyName == NULL || copyName[1] == 0)
            return HRESULT_FROM_WIN32(ERROR_INVALID_NAME);
        *copyName++ = 0;

        IShellItem* sourceItem = NULL;
        IShellItem* destinationFolder = NULL;
        HRESULT result = SHCreateItemFromParsingName(sourceW, NULL, IID_PPV_ARGS(&sourceItem));
        if (SUCCEEDED(result))
            result = SHCreateItemFromParsingName(destinationW, NULL, IID_PPV_ARGS(&destinationFolder));
        if (SUCCEEDED(result))
            result = operation->CopyItem(sourceItem, destinationFolder, copyName, NULL);
        if (destinationFolder != NULL)
            destinationFolder->Release();
        if (sourceItem != NULL)
            sourceItem->Release();
        if (FAILED(result))
            return result;

        sources += strlen(sources) + 1;
        destinations += strlen(destinations) + 1;
    }
    return *sources == 0 && *destinations == 0 ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

static HRESULT CopyTimeStampFilesWithShell(HWND parent, const char* sources, const char* destinations)
{
    IFileOperation* operation = NULL;
    HRESULT result = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&operation));
    if (FAILED(result))
        return result;

    CStrP progressMessageW(ConvertAllocUtf8ToWide(LoadStr(IDS_BROWSEARCUPDATE), -1));
    // Individual CopyItem calls preserve the old one-source/one-destination mapping without FOF_MULTIDESTFILES.
    result = operation->SetOwnerWindow(parent);
    if (SUCCEEDED(result))
        result = operation->SetOperationFlags(FOF_SIMPLEPROGRESS | FOF_NOCONFIRMMKDIR);
    if (SUCCEEDED(result) && progressMessageW != NULL)
        result = operation->SetProgressMessage(progressMessageW);
    if (SUCCEEDED(result))
        result = QueueUtf8CopyItems(operation, sources, destinations);
    if (SUCCEEDED(result))
        result = operation->PerformOperations();
    if (SUCCEEDED(result))
    {
        BOOL aborted = FALSE;
        if (SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && aborted)
            TRACE_I("Timestamp-associated file copy was cancelled by the Shell.");
    }
    operation->Release();
    return result;
}

void CFileTimeStamps::CopyFilesTo(HWND parent, int* indexes, int count, const char* initPath)
{
    CALL_STACK_MESSAGE3("CFileTimeStamps::CopyFilesTo(, , %d, %s)", count, initPath);
    char path[MAX_PATH];
    if (count > 0 &&
        GetTargetDirectory(parent, parent, LoadStr(IDS_BROWSEARCUPDATE),
                           LoadStr(IDS_BROWSEARCUPDATETEXT), path, FALSE, initPath))
    {
        CDynamicStringImp fromStr, toStr;
        BOOL ok = TRUE;
        BOOL tooLongName = FALSE;
        int i;
        for (i = 0; i < count; i++)
        {
            int index = indexes[i];
            if (index < List.Count && index >= 0) // jen tak pro sychr
            {
                CFileTimeStampsItem* item = List[index];
                char name[MAX_PATH];
                // Timestamp operations retain complete source and target identities.
                StringCchCopyA(name, _countof(name), item->SourcePath);
                tooLongName |= strlen(item->SourcePath) >= _countof(name);
                tooLongName |= !SalPathAppend(name, item->FileName, _countof(name));
                ok &= fromStr.Add(name, (int)strlen(name) + 1);

                StringCchCopyA(name, _countof(name), path);
                tooLongName |= strlen(path) >= _countof(name);
                tooLongName |= !SalPathAppend(name, item->ZIPRoot, _countof(name));
                tooLongName |= !SalPathAppend(name, item->FileName, _countof(name));
                ok &= toStr.Add(name, (int)strlen(name) + 1);
            }
        }
        fromStr.Add("\0", 2); // pro jistotu dame na konec dalsi dve nuly (zadne Add, taky o.k.)
        toStr.Add("\0", 2);   // pro jistotu dame na konec dalsi dve nuly (zadne Add, taky o.k.)

        if (ok && !tooLongName)
        {
            CALL_STACK_MESSAGE1("CFileTimeStamps::CopyFilesTo::IFileOperation");
            HRESULT result = CopyTimeStampFilesWithShell(parent, fromStr.Text, toStr.Text);
            if (FAILED(result))
                TRACE_E("Unable to copy timestamp-associated files through the Shell: " << GetErrorText(HRESULT_CODE(result)));
        }
        else
        {
            if (tooLongName)
            {
                SalMessageBox(parent, LoadStr(IDS_TOOLONGNAME), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            }
        }
    }
}

void CFileTimeStamps::CheckAndPackAndClear(HWND parent, BOOL* someFilesChanged, BOOL* archMaybeUpdated)
{
    CALL_STACK_MESSAGE1("CFileTimeStamps::CheckAndPackAndClear()");
    //---  vyhodime ze seznamu soubory, ktere nebyly zmeneny
    BeginStopRefresh();
    if (someFilesChanged != NULL)
        *someFilesChanged = FALSE;
    if (archMaybeUpdated != NULL)
        *archMaybeUpdated = FALSE;
    char buf[MAX_PATH + 100];
    WIN32_FIND_DATAW dataW;
    WIN32_FIND_DATA data;
    int i;
    for (i = List.Count - 1; i >= 0; i--)
    {
        CFileTimeStampsItem* item = List[i];
        BOOL kill = TRUE;
        // Verification rejects items whose complete source path cannot fit.
        StringCchCopyA(buf, _countof(buf), item->SourcePath);
        if (strlen(item->SourcePath) >= _countof(buf) || !SalPathAppend(buf, item->FileName, _countof(buf)))
            kill = FALSE; // keep the item, we cannot safely verify it with truncated path
        HANDLE find = INVALID_HANDLE_VALUE;
        if (kill)
        {
            CStrP bufW(ConvertAllocUtf8ToWide(buf, -1));
            find = bufW != NULL ? HANDLES_Q(FindFirstFileW(bufW, &dataW)) : INVALID_HANDLE_VALUE;
        }
        if (find != INVALID_HANDLE_VALUE)
        {
            HANDLES(FindClose(find));
            ConvertFindDataWToUtf8(dataW, &data);
            if (CompareFileTime(&data.ftLastWriteTime, &item->LastWrite) != 0 ||    // lisi se casy
                CQuadWord(data.nFileSizeLow, data.nFileSizeHigh) != item->FileSize) // lisi se velikosti
            {
                item->FileSize = CQuadWord(data.nFileSizeLow, data.nFileSizeHigh); // take the new size
                item->LastWrite = data.ftLastWriteTime;
                item->Attr = data.dwFileAttributes;
                kill = FALSE;
            }
        }
        if (kill)
        {
            List.Delete(i);
        }
    }

    if (List.Count > 0)
    {
        if (someFilesChanged != NULL)
            *someFilesChanged = TRUE;
        // pri critical shutdown se tvarime, ze updatle soubory neexistuji, zabalit zpet do archivu je
        // we will not make it, but must not delete them; after startup the user must still have a chance to update files
        // rucne do archivu zabalit
        if (!CriticalShutdown)
        {
            CArchiveUpdateDlg dlg(parent, this, Panel);
            BOOL showDlg = TRUE;
            while (showDlg)
            {
                showDlg = FALSE;
                if (dlg.Execute() == IDOK)
                {
                    if (archMaybeUpdated != NULL)
                        *archMaybeUpdated = TRUE;
                    //--- zapakujeme zmenene soubory, po skupinach se stejnym zip-root a source-path
                    TIndirectArray<CFileTimeStampsItem> packList(10, 5); // seznam vsech se stejnym zip-root a source-path
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                    while (!showDlg && List.Count > 0)
                    {
                        CFileTimeStampsItem* item1 = List[0];
                        char *r1, *s1;
                        if (item1 != NULL)
                        {
                            r1 = item1->ZIPRoot;
                            s1 = item1->SourcePath;
                            packList.Add(item1);
                            List.Detach(0);
                        }
                        for (i = List.Count - 1; i >= 0; i--) // kvadraticka slozitost zde snad nebude na obtiz
                        {                                     // jedeme odzadu, protoze Detach je tak "jednodusi"
                            CFileTimeStampsItem* item2 = List[i];
                            char* r2 = item2->ZIPRoot;
                            char* s2 = item2->SourcePath;
                            if (strcmp(r1, r2) == 0 && // equal zip-root (case-sensitive comparison required - update test\A.txt and Test\b.txt must not run simultaneously)
                                StrICmp(s1, s2) == 0)  // shodny source-path
                            {
                                packList.Add(item2);
                                List.Detach(i);
                            }
                        }

                        // call pack for packList
                        BOOL loop = TRUE;
                        while (loop)
                        {
                            CFileTimeStampsEnum2Info data2;
                            data2.PackList = &packList;
                            data2.Index = 0;
                            SetCurrentDirectory(s1);
                            if (Panel->CheckPath(TRUE, NULL, ERROR_SUCCESS, TRUE, parent) == ERROR_SUCCESS &&
                                PackCompress(parent, Panel, ZIPFile, r1, FALSE, s1, FileTimeStampsEnum2, &data2))
                                loop = FALSE;
                            else
                            {
                                loop = SalMessageBox(parent, LoadStr(IDS_UPDATEFAILED),
                                                     LoadStr(IDS_QUESTION), MB_YESNO | MB_ICONQUESTION) == IDYES;
                                if (!loop) // "cancel", detach files from disk-cache, otherwise it deletes them
                                {
                                    List.Add(packList.GetData(), packList.Count);
                                    packList.DetachMembers();
                                    showDlg = TRUE; // zobrazime znovu Archive Update dialog (se zbylymi soubory)
                                }
                            }
                            SetCurrentDirectoryToSystem();
                        }

                        packList.DestroyMembers();
                    }
                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                }
            }
        }
    }

    List.DestroyMembers();
    ZIPFile[0] = 0;
    EndStopRefresh();
}
