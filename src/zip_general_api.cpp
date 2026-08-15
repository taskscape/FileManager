// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "menu.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "mainwnd.h"
#include "plugins.h"
#include "filesbox.h"
#include "fileswnd.h"
#include "stswnd.h"
#include "editwnd.h"
#include "zip.h"
#include "cache.h"
#include "viewer.h"
#include "codetbl.h"
#include "shellib.h"
#include "gui.h"
#include "tasklist.h"
#include "olespy.h"
#include "md5.h"
#include "geticon.h"
#include "pack.h"
extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "crypt\fileenc.h"
#include "crypt\sha1.h"
#include "pwdmngr.h"

#include <strsafe.h>

// Globals defined in zip_progress.cpp
extern const char* STR_NONE;
extern CSalamanderDirectory GlobalEmptySalDir;
extern HWND ProgressDialogActivateDrop;

// Forward declaration for TestFreeSpace defined in zip_directory.cpp
BOOL TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize, const char* messageTitle);

//
// ****************************************************************************
// CSalamanderGeneral
//

CSalamanderGeneral::CSalamanderGeneral()
{
    Plugin = NULL;
    LanguageModule = NULL;
    HelpFileName[0] = 0;
}

CSalamanderGeneral::~CSalamanderGeneral()
{
    if (LanguageModule != NULL)
    {
        TRACE_E("CSalamanderGeneral::~CSalamanderGeneral(): unexpected situation!");
        HANDLES(FreeLibrary(LanguageModule));
    }
}

void CSalamanderGeneral::Clear()
{
    if (LanguageModule != NULL)
        HANDLES(FreeLibrary(LanguageModule));
    LanguageModule = NULL;
    HelpFileName[0] = 0;
}

int CSalamanderGeneral::ShowMessageBox(const char* text, const char* title, int type)
{
    if (MainThreadID != GetCurrentThreadId()) // Petr: just close; I do not have the energy to track down every wrong call
        TRACE_E("You can call CSalamanderGeneral::ShowMessageBox() only from main thread!");
    HWND parent = GetMsgBoxParent();
    switch (type)
    {
    case MSGBOX_INFO:
    {
        return SalMessageBox(parent, text, title, MB_OK | MB_ICONINFORMATION);
    }

    case MSGBOX_ERROR:
    {
        return SalMessageBox(parent, text, title, MB_OK | MB_ICONEXCLAMATION);
    }

    case MSGBOX_EX_ERROR:
    {
        return SalMessageBox(parent, text, title, MB_OKCANCEL | MB_ICONEXCLAMATION);
    }

    case MSGBOX_QUESTION:
    {
        return SalMessageBox(parent, text, title, MB_YESNO | MB_ICONQUESTION);
    }

    case MSGBOX_EX_QUESTION:
    {
        return SalMessageBox(parent, text, title, MB_YESNOCANCEL | MB_ICONQUESTION);
    }

    case MSGBOX_WARNING:
    {
        return SalMessageBox(parent, text, title, MB_OK | MB_ICONWARNING);
    }

    case MSGBOX_EX_WARNING:
    {
        return SalMessageBox(parent, text, title, MB_YESNOCANCEL | MB_ICONWARNING);
    }

    default:
    {
        TRACE_E("Unknown type of box in CSalamanderGeneral::ShowMessageBox().");
        return 0;
    }
    }
}

int CSalamanderGeneral::SalMessageBox(HWND hParent, LPCTSTR lpText, LPCTSTR lpCaption, UINT uType)
{
    return ::SalMessageBox(hParent, lpText, lpCaption, uType);
}

int CSalamanderGeneral::SalMessageBoxEx(const MSGBOXEX_PARAMS* params)
{
    return ::SalMessageBoxEx(params);
}

HWND CSalamanderGeneral::GetMsgBoxParent()
{
    if (MainThreadID != GetCurrentThreadId()) // Petr: just close; I do not have the energy to track down every wrong call
        TRACE_E("You can call CSalamanderGeneral::GetMsgBoxParent() only from main thread!");
    // if the following code should change, it must also be updated in EnterPlugin - so the check keeps working
    return PluginProgressDialog != NULL ? PluginProgressDialog : PluginMsgBoxParent;
}

int DialogError(HWND parent, DWORD flags, const char* fileName,
                const char* error, const char* title)
{
    HWND mainWnd = GetWndToFlash(parent);
    DWORD resID;
    BOOL noSkip;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_OK:
    {
        resID = IDD_ERROR3;
        noSkip = FALSE; // does not apply; something about resID == 0
        break;
    }

    case BUTTONS_RETRYCANCEL:
    {
        resID = 0;
        noSkip = TRUE;
        break;
    }

    case BUTTONS_SKIPCANCEL:
    {
        resID = IDD_ERROR2;
        noSkip = FALSE; // does not apply; something about resID == 0
        break;
    }

    case BUTTONS_RETRYSKIPCANCEL:
    {
        resID = 0;
        noSkip = FALSE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogError: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }
    int res = (int)CFileErrorDlg(parent, title != NULL ? title : ::LoadStr(IDS_ERRORTITLE),
                                 fileName, error, noSkip, resID)
                  .Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDOK:
        return DIALOG_OK;
    case IDRETRY:
        return DIALOG_RETRY;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogError(HWND parent, DWORD flags, const char* fileName,
                                    const char* error, const char* title)
{
    if (fileName == NULL || error == NULL)
    {
        TRACE_E("Invalid parametr (fileName == NULL || error == NULL) in CSalamanderGeneral::DialogError!");
        if (fileName == NULL)
            fileName = "";
        if (error == NULL)
            error = "";
    }
    return ::DialogError(parent, flags, fileName, error, title);
}

int DialogOverwrite(HWND parent, DWORD flags, const char* fileName1, const char* fileData1,
                    const char* fileName2, const char* fileData2)
{
    HWND mainWnd = GetWndToFlash(parent);
    BOOL yesnocancel;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_YESALLSKIPCANCEL:
    {
        yesnocancel = FALSE;
        break;
    }

    case BUTTONS_YESNOCANCEL:
    {
        yesnocancel = TRUE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogOverwrite: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }

    int res = (int)COverwriteDlg(parent, fileName1, fileData1, fileName2, fileData2, yesnocancel).Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDYES:
        return DIALOG_YES;
    case IDNO:
        return DIALOG_NO;
    case IDB_ALL:
        return DIALOG_ALL;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogOverwrite(HWND parent, DWORD flags, const char* fileName1, const char* fileData1,
                                        const char* fileName2, const char* fileData2)
{
    if (fileName1 == NULL || fileData1 == NULL || fileName2 == NULL || fileData2 == NULL)
    {
        TRACE_E("Invalid parametr (fileName1 == NULL || fileData1 == NULL || fileName2 == NULL || fileData2 == NULL) in CSalamanderGeneral::DialogOverwrite!");
        if (fileName1 == NULL)
            fileName1 = "";
        if (fileData1 == NULL)
            fileData1 = "";
        if (fileName2 == NULL)
            fileName2 = "";
        if (fileData2 == NULL)
            fileData2 = "";
    }
    return ::DialogOverwrite(parent, flags, fileName1, fileData1, fileName2, fileData2);
}

int DialogQuestion(HWND parent, DWORD flags, const char* fileName,
                   const char* question, const char* title)
{
    HWND mainWnd = GetWndToFlash(parent);
    BOOL yesnocancel, yesallcancel;
    switch (flags & BUTTONS_MASK)
    {
    case BUTTONS_YESALLSKIPCANCEL:
    {
        yesnocancel = FALSE;
        yesallcancel = FALSE;
        break;
    }

    case BUTTONS_YESNOCANCEL:
    {
        yesnocancel = TRUE;
        yesallcancel = FALSE;
        break;
    }

    case BUTTONS_YESALLCANCEL:
    {
        yesnocancel = TRUE;
        yesallcancel = TRUE;
        break;
    }

    default:
    {
        TRACE_E("CSalamanderGeneral::DialogQuestion: unknow flags=0x" << std::hex << flags << std::dec);
        return DIALOG_FAIL;
    }
    }
    int res = (int)CHiddenOrSystemDlg(parent, title != NULL ? title : ::LoadStr(IDS_QUESTION), fileName,
                                      question, yesnocancel, yesallcancel)
                  .Execute();
    if (mainWnd != NULL)
        FlashWindow(mainWnd, FALSE);
    switch (res)
    {
    case IDYES:
        return DIALOG_YES;
    case IDNO:
        return DIALOG_NO;
    case IDB_ALL:
        return DIALOG_ALL;
    case IDB_SKIP:
        return DIALOG_SKIP;
    case IDB_SKIPALL:
        return DIALOG_SKIPALL;
    case IDCANCEL:
        return DIALOG_CANCEL;
    default:
        return DIALOG_FAIL;
    }
}

int CSalamanderGeneral::DialogQuestion(HWND parent, DWORD flags, const char* fileName,
                                       const char* question, const char* title)
{
    if (fileName == NULL || question == NULL)
    {
        TRACE_E("Invalid parametr (fileName == NULL || question == NULL) in CSalamanderGeneral::DialogQuestion!");
        if (fileName == NULL)
            fileName = "";
        if (question == NULL)
            question = "";
    }
    return ::DialogQuestion(parent, flags, fileName, question, title);
}

HWND CSalamanderGeneral::GetMainWindowHWND()
{
    return MainWindow != NULL ? MainWindow->HWindow : NULL;
}

void RestoreFocusInSourcePanel()
{
    if (MainWindow != NULL)
    {
        CFilesWindow* p1 = MainWindow->GetActivePanel();
        if (p1 != NULL)
        {
            if (!MainWindow->EditMode)
                MainWindow->FocusPanel(p1);
            else
            {
                if (MainWindow->EditWindow != NULL && MainWindow->EditWindow->HWindow != NULL)
                    SetFocus(MainWindow->EditWindow->HWindow);
            }
        }
    }
}

void CSalamanderGeneral::RestoreFocusInSourcePanel()
{
    ::RestoreFocusInSourcePanel();
}

BOOL CSalamanderGeneral::CheckAndCreateDirectory(const char* dir, HWND parent, BOOL quiet,
                                                 char* errBuf, int errBufSize, char* firstCreatedDir,
                                                 BOOL manualCrDir)
{
    return ::CheckAndCreateDirectory(dir, parent, quiet, errBuf, errBufSize, firstCreatedDir, FALSE, manualCrDir);
}

BOOL CSalamanderGeneral::TestFreeSpace(HWND parent, const char* path, const CQuadWord& totalSize,
                                       const char* messageTitle)
{
    return ::TestFreeSpace(parent, path, totalSize, messageTitle);
}

void CSalamanderGeneral::GetDiskFreeSpace(CQuadWord* retValue, const char* path, CQuadWord* total)
{
    if (retValue == NULL)
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetDiskFreeSpace(): retValue is NULL!");
        return;
    }
    *retValue = MyGetDiskFreeSpace(path, total);
}

BOOL CSalamanderGeneral::SalGetDiskFreeSpace(const char* path, LPDWORD lpSectorsPerCluster,
                                             LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
                                             LPDWORD lpTotalNumberOfClusters)
{
    return MyGetDiskFreeSpace(path, lpSectorsPerCluster, lpBytesPerSector,
                              lpNumberOfFreeClusters, lpTotalNumberOfClusters);
}

BOOL CSalamanderGeneral::SalGetVolumeInformation(const char* path, char* rootOrCurReparsePoint, LPTSTR lpVolumeNameBuffer,
                                                 DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber,
                                                 LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags,
                                                 LPTSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize)
{
    return MyGetVolumeInformation(path, rootOrCurReparsePoint, NULL, NULL, lpVolumeNameBuffer, nVolumeNameSize,
                                  lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags,
                                  lpFileSystemNameBuffer, nFileSystemNameSize);
}

UINT CSalamanderGeneral::SalGetDriveType(const char* path)
{
    return MyGetDriveType(path);
}

void CSalamanderGeneral::RemoveTemporaryDir(const char* dir)
{
    ::RemoveTemporaryDir(dir);
}

void CSalamanderGeneral::PrepareMask(char* mask, const char* src)
{
    ::PrepareMask(mask, src);
}

BOOL CSalamanderGeneral::AgreeMask(const char* filename, const char* mask, BOOL hasExtension)
{
    return ::AgreeMask(filename, mask, hasExtension, FALSE);
}

char* CSalamanderGeneral::MaskName(char* buffer, int bufSize, const char* name, const char* mask)
{
    return ::MaskName(buffer, bufSize, name, mask);
}

void CSalamanderGeneral::PrepareExtMask(char* mask, const char* src)
{
    ::PrepareMask(mask, src);
}

BOOL CSalamanderGeneral::AgreeExtMask(const char* filename, const char* mask, BOOL hasExtension)
{
    return ::AgreeMask(filename, mask, hasExtension, TRUE);
}

void* CSalamanderGeneral::Alloc(int size)
{
    return malloc(size);
}

void* CSalamanderGeneral::Realloc(void* ptr, int size)
{
    return realloc(ptr, size);
}

void CSalamanderGeneral::Free(void* ptr)
{
    free(ptr);
}

char* CSalamanderGeneral::DupStr(const char* str)
{
    return ::DupStr(str);
}

void CSalamanderGeneral::GetLowerAndUpperCase(unsigned char** lowerCase, unsigned char** upperCase)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetLowerAndUpperCase(,)");
    if (lowerCase != NULL)
        *lowerCase = LowerCase;
    if (upperCase != NULL)
        *upperCase = UpperCase;
}

void CSalamanderGeneral::ToLowerCase(char* str)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ToLowerCase()");
    char* toLow = str;
    while (*toLow != 0)
    {
        *toLow = LowerCase[*toLow];
        toLow++;
    }
}

void CSalamanderGeneral::ToUpperCase(char* str)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ToUpperCase()");
    char* toUpp = str;
    while (*toUpp != 0)
    {
        *toUpp = UpperCase[*toUpp];
        toUpp++;
    }
}

int CSalamanderGeneral::StrCmpEx(const char* s1, int l1, const char* s2, int l2)
{
    return ::StrCmpEx(s1, l1, s2, l2);
}

int CSalamanderGeneral::StrICpy(char* dest, const char* src)
{
    return ::StrICpy(dest, src);
}

int CSalamanderGeneral::StrICmp(const char* s1, const char* s2)
{
    return ::StrICmp(s1, s2);
}

int CSalamanderGeneral::StrICmpEx(const char* s1, int l1, const char* s2, int l2)
{
    return ::StrICmpEx(s1, l1, s2, l2);
}

int CSalamanderGeneral::StrNICmp(const char* s1, const char* s2, int n)
{
    return ::StrNICmp(s1, s2, n);
}

int CSalamanderGeneral::MemICmp(const void* buf1, const void* buf2, int n)
{
    return ::MemICmp(buf1, buf2, n);
}

int CSalamanderGeneral::RegSetStrICmp(const char* s1, const char* s2)
{
    return ::RegSetStrICmp(s1, s2);
}

int CSalamanderGeneral::RegSetStrICmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual)
{
    return ::RegSetStrICmpEx(s1, l1, s2, l2, numericalyEqual);
}

int CSalamanderGeneral::RegSetStrCmp(const char* s1, const char* s2)
{
    return ::RegSetStrCmp(s1, s2);
}

int CSalamanderGeneral::RegSetStrCmpEx(const char* s1, int l1, const char* s2, int l2, BOOL* numericalyEqual)
{
    return ::RegSetStrCmpEx(s1, l1, s2, l2, numericalyEqual);
}

CFilesWindow*
CSalamanderGeneral::GetPanel(int panel)
{
    return MainWindow->GetPanel(panel);
}

BOOL CSalamanderGeneral::GetPanelPath(int panel, char* buffer, int bufferSize, int* type,
                                      char** archiveOrFS, BOOL convertFSPathToExternal)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetPanelPath(%d, , %d, ,)", panel, bufferSize);
    if (type != NULL)
        *type = 0; // unknown
    if (archiveOrFS != NULL)
        *archiveOrFS = NULL;
    if (bufferSize > 0)
        buffer[0] = 0;
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPath() only from main thread!");
        if (type != NULL)
            *type = 0;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        char buf[2 * MAX_PATH];
        int offset = -1; // offset into the buffer for computing archiveOrFS (-1 means NULL)
        if (p->Is(ptZIPArchive))
        {
            if (type != NULL)
                *type = PATH_TYPE_ARCHIVE;
            offset = (int)strlen(p->GetZIPArchive());
            memcpy(buf, p->GetZIPArchive(), offset + 1);
            if (p->GetZIPPath()[0] != 0)
            {
                if (p->GetZIPPath()[0] != '\\')
                    strcpy(buf + offset, "\\");
                strcat(buf + offset, p->GetZIPPath());
            }
        }
        else
        {
            if (p->Is(ptPluginFS))
            {
                if (type != NULL)
                    *type = PATH_TYPE_FS;
                offset = (int)strlen(p->GetPluginFS()->GetPluginFSName());
                memcpy(buf, p->GetPluginFS()->GetPluginFSName(), offset);
                buf[offset] = ':';
                if (!p->GetPluginFS()->NotEmpty() || !p->GetPluginFS()->GetCurrentPath(buf + offset + 1))
                {
                    return FALSE; // error
                }
                if (convertFSPathToExternal)
                {
                    p->GetPluginFS()->GetPluginInterfaceForFS()->ConvertPathToExternal(p->GetPluginFS()->GetPluginFSName(),
                                                                                       p->GetPluginFS()->GetPluginFSNameIndex(),
                                                                                       buf + offset + 1);
                }
            }
            else
            {
                if (p->Is(ptDisk))
                {
                    if (type != NULL)
                        *type = PATH_TYPE_WINDOWS;
                    strcpy(buf, p->GetPath());
                }
                else
                {
                    TRACE_E("Unexpected situation in CSalamanderGeneral::GetPanelPath()");
                    return FALSE;
                }
            }
        }

        int l = (int)strlen(buf) + 1;
        if (l > bufferSize)
            return bufferSize == 0; // if the user does not want the path back, we do not treat it as an error
        memcpy(buffer, buf, l);

        if (archiveOrFS != NULL && offset != -1)
            *archiveOrFS = buffer + offset;

        return TRUE;
    }
    return FALSE;
}

BOOL CSalamanderGeneral::GetLastWindowsPanelPath(int panel, char* buffer, int bufferSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetLastWindowsPanelPath(%d, , %d)", panel, bufferSize);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetLastWindowsPanelPath() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && buffer != NULL)
    {
        int l = (int)strlen(p->GetPath()) + 1;
        if (l > bufferSize)
            return FALSE;
        memcpy(buffer, p->GetPath(), l);
        return TRUE;
    }
    return FALSE;
}

CPluginDataInterfaceAbstract*
CSalamanderGeneral::GetPanelPluginData(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelPluginData(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPluginData() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        CPluginDataInterfaceAbstract* iface = p->PluginData.GetInterface();
        if (iface != NULL && p->PluginData.GetPluginInterface() != Plugin)
            iface = NULL; // the object is not from this plugin -> it gets nothing
        return iface;
    }
    return NULL;
}

CPluginFSInterfaceAbstract*
CSalamanderGeneral::GetPanelPluginFS(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelPluginFS(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelPluginFS() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->Is(ptPluginFS))
    {
        CPluginFSInterfaceAbstract* iface = p->GetPluginFS()->GetInterface();
        if (iface != NULL && p->GetPluginFS()->GetPluginInterface() != Plugin)
            iface = NULL; // the object is not from this plugin -> it gets nothing
        return iface;
    }
    return NULL;
}

const CFileData*
CSalamanderGeneral::GetPanelFocusedItem(int panel, BOOL* isDir)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelFocusedItem(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelFocusedItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int caret = p->GetCaretIndex();
        if (caret >= 0 && caret < p->Files->Count + p->Dirs->Count)
        {
            if (isDir != NULL)
                *isDir = caret < p->Dirs->Count;
            return (caret < p->Dirs->Count) ? &p->Dirs->At(caret) : &p->Files->At(caret - p->Dirs->Count);
        }
    }
    return NULL;
}

const CFileData*
CSalamanderGeneral::GetPanelItem(int panel, int* index, BOOL* isDir)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelItem(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && index != NULL)
    {
        int i = *index;
        if (i < 0)
            return NULL;                          // enumeration already finished
        if (i < p->Files->Count + p->Dirs->Count) // enumerate more items
        {
            *index = i + 1; // next time move to the following item
            if (isDir != NULL)
                *isDir = i < p->Dirs->Count;
            return (i < p->Dirs->Count) ? &p->Dirs->At(i) : &p->Files->At(i - p->Dirs->Count);
        }
        else
        {
            *index = -1; // end of enumeration
            return NULL;
        }
    }
    return NULL;
}

BOOL CSalamanderGeneral::GetPanelSelection(int panel, int* selectedFiles, int* selectedDirs)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelSelection(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelSelection() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int count = p->GetSelCount();
        int selDirs = 0;
        if (count > 0)
        {
            CFilesArray* dirs = p->Dirs;
            // count how many directories are selected (the rest of the selected items are files)
            int i;
            for (i = 0; i < dirs->Count; i++) // ".." cannot be selected; the test would be pointless
            {
                if (dirs->At(i).Selected)
                    selDirs++;
            }
        }
        else
            count = 0;

        if (selectedDirs != NULL)
            *selectedDirs = selDirs;
        if (selectedFiles != NULL)
            *selectedFiles = count - selDirs;

        int i = p->GetCaretIndex();
        return p->Dirs->Count + p->Files->Count > 0 && // the panel is not empty
               (i != 0 || count > 0 || p->Dirs->Count == 0 ||
                strcmp(p->Dirs->At(0).Name, "..") != 0); // the focus is not on the up-dir, or at least one item is selected
    }
    return FALSE;
}

const CFileData*
CSalamanderGeneral::GetPanelSelectedItem(int panel, int* index, BOOL* isDir)
{
    SLOW_CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelSelectedItem(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelSelectedItem() only from main thread!");
        return NULL;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && index != NULL)
    {
        int i = *index;
        if (i < 0)
            return NULL;                             // enumeration already finished
        while (i < p->Files->Count + p->Dirs->Count) // searching for the next selected item
        {
            CFileData* data = (i < p->Dirs->Count) ? &p->Dirs->At(i) : &p->Files->At(i - p->Dirs->Count);
            if (data->Selected) // selected item?
            {
                *index = i + 1; // next time start searching from the following item
                if (isDir != NULL)
                    *isDir = i < p->Dirs->Count;
                return data; // return the found selected item
            }
            i++;
        }
        *index = -1; // end of enumeration; no selected items remain
    }
    return NULL;
}

void CSalamanderGeneral::SelectPanelItem(int panel, const CFileData* file, BOOL select)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SelectPanelItem(%d, , %d)", panel, select);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SelectPanelItem() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int index = -1; // index of 'file' in the panel
        if (p->Dirs->Count > 0)
        {
            CFileData* first = &p->Dirs->At(0);
            CFileData* last = &p->Dirs->At(p->Dirs->Count - 1);
            if (first <= file && file <= last)
                index = (int)(file - first); // it is a directory
        }
        if (index == -1 && p->Files->Count > 0)
        {
            CFileData* first = &p->Files->At(0);
            CFileData* last = &p->Files->At(p->Files->Count - 1);
            if (first <= file && file <= last)
                index = p->Dirs->Count + (int)(file - first); // it is a directory
        }
        if (index != -1)
            p->SetSel(select, index, FALSE); // change selection
        else
            TRACE_E("Invalid parameter 'file' in CSalamanderGeneral::SelectPanelItem().");
    }
}

void CSalamanderGeneral::RepaintChangedItems(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RepaintChangedItems(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RepaintChangedItems() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->RepaintListBox(DRAWFLAG_DIRTY_ONLY | DRAWFLAG_SKIP_VISTEST);
        PostMessage(p->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
    }
}

void CSalamanderGeneral::SelectAllPanelItems(int panel, BOOL select, BOOL repaint)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SelectAllPanelItems(%d, %d, %d)", panel, select, repaint);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SelectAllPanelItems() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->SetSel(select, -1, repaint); // change selection
        if (repaint)
            PostMessage(p->HWindow, WM_USER_SELCHANGED, 0, 0); // sel-change notify
    }
}

void CSalamanderGeneral::SetPanelFocusedItem(int panel, const CFileData* file, BOOL partVis)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SetPanelFocusedItem(%d, , %d)", panel, partVis);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetPanelFocusedItem() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int index = -1; // index of 'file' in the panel
        if (p->Dirs->Count > 0)
        {
            CFileData* first = &p->Dirs->At(0);
            CFileData* last = &p->Dirs->At(p->Dirs->Count - 1);
            if (first <= file && file <= last)
                index = (int)(file - first); // it is a directory
        }
        if (index == -1 && p->Files->Count > 0)
        {
            CFileData* first = &p->Files->At(0);
            CFileData* last = &p->Files->At(p->Files->Count - 1);
            if (first <= file && file <= last)
                index = p->Dirs->Count + (int)(file - first); // it is a directory
        }
        if (index != -1)
            p->SetCaretIndex(index, partVis); // change focus
        else
            TRACE_E("Invalid parameter 'file' in CSalamanderGeneral::SetPanelFocusedItem().");
    }
}

BOOL CSalamanderGeneral::GetFilterFromPanel(int panel, char* masks, int masksBufSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetFilterFromPanel(%d, , %d)", panel, masksBufSize);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetFilterFromPanel() only from main thread!");
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    BOOL ret = FALSE;
    if (p != NULL && p->FilterEnabled)
    {
        int len = (int)strlen(p->Filter.GetMasksString());
        if (len < masksBufSize)
        {
            memcpy(masks, p->Filter.GetMasksString(), len + 1);
            ret = TRUE;
        }
    }
    return ret;
}

// returns the position of the source panel (is it on the left or on the right?), returns PANEL_LEFT or PANEL_RIGHT
int CSalamanderGeneral::GetSourcePanel()
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSourcePanel() only from main thread!");
        return PANEL_LEFT;
    }
    if (MainWindow->GetActivePanel() == MainWindow->LeftPanel)
        return PANEL_LEFT;
    else
        return PANEL_RIGHT;
}

// activates the other panel (like the TAB key); panels marked through PANEL_SOURCE and PANEL_TARGET
// swap naturally as a result
void CSalamanderGeneral::ChangePanel()
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanel() only from main thread!");
        return;
    }
    MainWindow->ChangePanel();
}

void CSalamanderGeneral::SkipOneActivateRefresh()
{
    ::SkipOneActivateRefresh = TRUE;
    PostMessage(MainWindow->HWindow, WM_USER_SKIPONEREFRESH, 0, 0);
}

BOOL CSalamanderGeneral::SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetTempFileName()");
    BOOL ret = ::SalGetTempFileName(path, prefix, tmpName, file);
    if (err != NULL)
        *err = GetLastError();
    return ret;
}

char* CSalamanderGeneral::NumberToStr(char* buffer, const CQuadWord& number)
{
    return ::NumberToStr(buffer, number);
}

char* CSalamanderGeneral::PrintDiskSize(char* buf, const CQuadWord& size, int mode)
{
    return ::PrintDiskSize(buf, size, mode);
}

char* CSalamanderGeneral::PrintTimeLeft(char* buf, const CQuadWord& secs)
{
    return ::PrintTimeLeft(buf, secs);
}

BOOL CSalamanderGeneral::HasTheSameRootPath(const char* path1, const char* path2)
{
    return ::HasTheSameRootPath(path1, path2);
}

int CSalamanderGeneral::CommonPrefixLength(const char* path1, const char* path2)
{
    return ::CommonPrefixLength(path1, path2);
}

BOOL CSalamanderGeneral::PathIsPrefix(const char* prefix, const char* path)
{
    return ::SalPathIsPrefix(prefix, path);
}

BOOL CSalamanderGeneral::IsTheSamePath(const char* path1, const char* path2)
{
    return ::IsTheSamePath(path1, path2);
}

int CSalamanderGeneral::GetRootPath(char* root, const char* path)
{
    return ::GetRootPath(root, path);
}

BOOL CSalamanderGeneral::CutDirectory(char* path, char** cutDir)
{
    return ::CutDirectory(path, cutDir);
}

BOOL CSalamanderGeneral::SalPathAppend(char* path, const char* name, int pathSize)
{
    return ::SalPathAppend(path, name, pathSize);
}

BOOL CSalamanderGeneral::SalPathAddBackslash(char* path, int pathSize)
{
    return ::SalPathAddBackslash(path, pathSize);
}

void CSalamanderGeneral::SalPathRemoveBackslash(char* path)
{
    ::SalPathRemoveBackslash(path);
}

void CSalamanderGeneral::SalPathStripPath(char* path)
{
    ::SalPathStripPath(path);
}

void CSalamanderGeneral::SalPathRemoveExtension(char* path)
{
    ::SalPathRemoveExtension(path);
}

BOOL CSalamanderGeneral::SalPathAddExtension(char* path, const char* extension, int pathSize)
{
    return ::SalPathAddExtension(path, extension, pathSize);
}

BOOL CSalamanderGeneral::SalPathRenameExtension(char* path, const char* extension, int pathSize)
{
    return ::SalPathRenameExtension(path, extension, pathSize);
}

const char*
CSalamanderGeneral::SalPathFindFileName(const char* path)
{
    return ::SalPathFindFileName(path);
}

BOOL CSalamanderGeneral::SalGetFullName(char* name, int* errTextID, const char* curDir,
                                        char* nextFocus, int nameBufSize)
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalGetFullName() only from main thread!");
        if (errTextID != NULL)
            *errTextID = GFN_PATHISINVALID;
        return FALSE;
    }
    BOOL ret = ::SalGetFullName(name, errTextID, curDir, nextFocus, NULL, nameBufSize);
    if (errTextID != NULL)
    {
        switch (*errTextID)
        {
        case IDS_SERVERNAMEMISSING:
            *errTextID = GFN_SERVERNAMEMISSING;
            break;
        case IDS_SHARENAMEMISSING:
            *errTextID = GFN_SHARENAMEMISSING;
            break;
        case IDS_TOOLONGPATH:
            *errTextID = GFN_TOOLONGPATH;
            break;
        case IDS_INVALIDDRIVE:
            *errTextID = GFN_INVALIDDRIVE;
            break;
        case IDS_INCOMLETEFILENAME:
            *errTextID = GFN_INCOMLETEFILENAME;
            break;
        case IDS_EMPTYNAMENOTALLOWED:
            *errTextID = GFN_EMPTYNAMENOTALLOWED;
            break;
        case IDS_PATHISINVALID:
            *errTextID = GFN_PATHISINVALID;
            break;
        }
    }

    return ret;
}

void CSalamanderGeneral::SalUpdateDefaultDir(BOOL activePrefered)
{
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalUpdateDefaultDir() only from main thread!");
        return;
    }
    if (MainWindow != NULL)
        MainWindow->UpdateDefaultDir(activePrefered);
}

char* CSalamanderGeneral::GetGFNErrorText(int GFN, char* buf, int bufSize)
{
    char* s = NULL;
    switch (GFN)
    {
    case GFN_SERVERNAMEMISSING:
        s = ::LoadStr(IDS_SERVERNAMEMISSING);
        break;
    case GFN_SHARENAMEMISSING:
        s = ::LoadStr(IDS_SHARENAMEMISSING);
        break;
    case GFN_TOOLONGPATH:
        s = ::LoadStr(IDS_TOOLONGPATH);
        break;
    case GFN_INVALIDDRIVE:
        s = ::LoadStr(IDS_INVALIDDRIVE);
        break;
    case GFN_INCOMLETEFILENAME:
        s = ::LoadStr(IDS_INCOMLETEFILENAME);
        break;
    case GFN_EMPTYNAMENOTALLOWED:
        s = ::LoadStr(IDS_EMPTYNAMENOTALLOWED);
        break;
    case GFN_PATHISINVALID:
        s = ::LoadStr(IDS_PATHISINVALID);
        break;
    }
    if (s != NULL)
    {
        // Caller-provided error text is a compact presentation field with deliberate clipping.
        StringCchCopyNA(buf, bufSize, s, bufSize - 1);
    }
    else
        buf[0] = 0;
    return buf;
}

char* CSalamanderGeneral::GetErrorText(int err, char* buf, int bufSize)
{
    if (buf == NULL || bufSize == 0)
        return ::GetErrorText(err);

    int l = 0;
    if (bufSize > 20)
        l = sprintf(buf, "(%d) ", err);
    if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL,
                      err,
                      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                      buf + l,
                      bufSize - l,
                      NULL) == 0 ||
        bufSize > l && *(buf + l) == 0)
    {
        char txt[100];
        sprintf(txt, "System error %d, text description is not available.", err);
        // Caller-provided error text is a compact presentation field with deliberate clipping.
        StringCchCopyNA(buf, bufSize, txt, bufSize - 1);
    }
    return buf;
}

char* CSalamanderGeneral::LoadStr(HINSTANCE module, int resID)
{
    if (module == NULL)
    {
        TRACE_E("CSalamanderGeneral::LoadStr(): module == NULL");
        static char buffEmpty[] = "ERROR LOADING STRING";
        return buffEmpty;
    }
    return ::LoadStr(resID, module);
}

WCHAR*
CSalamanderGeneral::LoadStrW(HINSTANCE module, int resID)
{
    if (module == NULL)
    {
        TRACE_E("CSalamanderGeneral::LoadStrW(): module == NULL");
        static wchar_t buffEmpty[] = L"ERROR LOADING WIDE STRING";
        return buffEmpty;
    }
    return ::LoadStrW(resID, module);
}

COLORREF
CSalamanderGeneral::GetCurrentColor(int color)
{
    int index;
    SALCOLOR* arr = CurrentColors;
    switch (color)
    {
    // CurrentColors
    case SALCOL_FOCUS_ACTIVE_NORMAL:
        index = FOCUS_ACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_ACTIVE_SELECTED:
        index = FOCUS_ACTIVE_SELECTED;
        break;
    case SALCOL_FOCUS_FG_INACTIVE_NORMAL:
        index = FOCUS_FG_INACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_FG_INACTIVE_SELECTED:
        index = FOCUS_FG_INACTIVE_SELECTED;
        break;
    case SALCOL_FOCUS_BK_INACTIVE_NORMAL:
        index = FOCUS_BK_INACTIVE_NORMAL;
        break;
    case SALCOL_FOCUS_BK_INACTIVE_SELECTED:
        index = FOCUS_BK_INACTIVE_SELECTED;
        break;
    case SALCOL_ITEM_FG_NORMAL:
        index = ITEM_FG_NORMAL;
        break;
    case SALCOL_ITEM_FG_SELECTED:
        index = ITEM_FG_SELECTED;
        break;
    case SALCOL_ITEM_FG_FOCUSED:
        index = ITEM_FG_FOCUSED;
        break;
    case SALCOL_ITEM_FG_FOCSEL:
        index = ITEM_FG_FOCSEL;
        break;
    case SALCOL_ITEM_FG_HIGHLIGHT:
        index = ITEM_FG_HIGHLIGHT;
        break;
    case SALCOL_ITEM_BK_NORMAL:
        index = ITEM_BK_NORMAL;
        break;
    case SALCOL_ITEM_BK_SELECTED:
        index = ITEM_BK_SELECTED;
        break;
    case SALCOL_ITEM_BK_FOCUSED:
        index = ITEM_BK_FOCUSED;
        break;
    case SALCOL_ITEM_BK_FOCSEL:
        index = ITEM_BK_FOCSEL;
        break;
    case SALCOL_ITEM_BK_HIGHLIGHT:
        index = ITEM_BK_HIGHLIGHT;
        break;
    case SALCOL_ICON_BLEND_SELECTED:
        index = ICON_BLEND_SELECTED;
        break;
    case SALCOL_ICON_BLEND_FOCUSED:
        index = ICON_BLEND_FOCUSED;
        break;
    case SALCOL_ICON_BLEND_FOCSEL:
        index = ICON_BLEND_FOCSEL;
        break;
    case SALCOL_PROGRESS_FG_NORMAL:
        index = PROGRESS_FG_NORMAL;
        break;
    case SALCOL_PROGRESS_FG_SELECTED:
        index = PROGRESS_FG_SELECTED;
        break;
    case SALCOL_PROGRESS_BK_NORMAL:
        index = PROGRESS_BK_NORMAL;
        break;
    case SALCOL_PROGRESS_BK_SELECTED:
        index = PROGRESS_BK_SELECTED;
        break;
    case SALCOL_HOT_PANEL:
        index = HOT_PANEL;
        break;
    case SALCOL_HOT_ACTIVE:
        index = HOT_ACTIVE;
        break;
    case SALCOL_HOT_INACTIVE:
        index = HOT_INACTIVE;
        break;
    case SALCOL_ACTIVE_CAPTION_FG:
        index = ACTIVE_CAPTION_FG;
        break;
    case SALCOL_ACTIVE_CAPTION_BK:
        index = ACTIVE_CAPTION_BK;
        break;
    case SALCOL_INACTIVE_CAPTION_FG:
        index = INACTIVE_CAPTION_FG;
        break;
    case SALCOL_INACTIVE_CAPTION_BK:
        index = INACTIVE_CAPTION_BK;
        break;
    case SALCOL_THUMBNAIL_NORMAL:
        index = THUMBNAIL_FRAME_NORMAL;
        break;
    case SALCOL_THUMBNAIL_SELECTED:
        index = THUMBNAIL_FRAME_FOCUSED;
        break;
    case SALCOL_THUMBNAIL_FOCUSED:
        index = THUMBNAIL_FRAME_SELECTED;
        break;
    case SALCOL_THUMBNAIL_FOCSEL:
        index = THUMBNAIL_FRAME_FOCSEL;
        break;
    // ViewerColors
    case SALCOL_VIEWER_FG_NORMAL:
        index = VIEWER_FG_NORMAL;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_BK_NORMAL:
        index = VIEWER_BK_NORMAL;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_FG_SELECTED:
        index = VIEWER_FG_SELECTED;
        arr = ViewerColors;
        break;
    case SALCOL_VIEWER_BK_SELECTED:
        index = VIEWER_BK_SELECTED;
        arr = ViewerColors;
        break;

    default:
    {
        TRACE_E("Invalid color constant!");
        return COLORREF(0);
    }
    }
    return GetCOLORREF(arr[index]);
}

void CSalamanderGeneral::GetPluginFSName(char* buf, int fsNameIndex)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPluginFSName(, %d)", fsNameIndex);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPluginFSName() only from main thread!");
        buf[0] = 0;
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL && data->SupportFS && fsNameIndex >= 0 && fsNameIndex < data->FSNames.Count)
    {
        // The plug-in ABI supplies MAX_PATH storage for the complete FS name.
        if (FAILED(StringCchCopyA(buf, MAX_PATH, data->FSNames[fsNameIndex])))
            buf[0] = 0;
    }
    else
    {
        TRACE_E("CSalamanderGeneral::GetPluginFSName(): incorrect call (not supporting FS or 'fsNameIndex' is out of range)!");
        buf[0] = 0;
    }
}

BOOL CSalamanderGeneral::SetFlagLoadOnSalamanderStart(BOOL start)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetFlagLoadOnSalamanderStart(%d)", start);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetFlagLoadOnSalamanderStart() only from main thread!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        BOOL prev = data->LoadOnStart != 0;
        data->LoadOnStart = start != 0;
        return prev;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetFlagLoadOnSalamanderStart().");
        return FALSE;
    }
}

void CSalamanderGeneral::PostUnloadThisPlugin()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PostUnloadThisPlugin()");
    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->ShouldUnload = TRUE;
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostUnloadThisPlugin().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostUnloadThisPlugin only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            {
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 0);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostUnloadThisPlugin().");
        }
    }
}

void CSalamanderGeneral::PostPluginMenuChanged()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PostPluginMenuChanged()");
    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->ShouldRebuildMenu = TRUE;
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostPluginMenuChanged().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostPluginMenuChanged only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            {
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 1);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostPluginMenuChanged().");
        }
    }
}

void CSalamanderGeneral::PostMenuExtCommand(int id, BOOL waitForSalIdle)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostMenuExtCommand(%d, %d)", id, waitForSalIdle);
    if (waitForSalIdle)
    {
        if (id < 0 || id >= 1000000)
        {
            TRACE_E("CSalamanderGeneral::PostMenuExtCommand: id is invalid (" << id << " is not in range 0-999999).");
            return;
        }

        if (MainThreadID == GetCurrentThreadId())
        { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
            // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
            CPluginData* data = Plugins.GetPluginData(Plugin);
            if (data != NULL)
            {
                data->Commands.Add(500 + id); // salCmd values are in the <0, 499> range; 500 is the first free number
                ExecCmdsOrUnloadMarkedPlugins = TRUE;
            }
            else
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::PostMenuExtCommand().");
            }
        }
        else // outside the entry point the Plugin is certainly set...
        {
            if (MainWindow != NULL && MainWindow->HWindow != NULL)
            {
                // check for a call while the entry point is starting (Plugin is set to -1)
                if ((INT_PTR)Plugin == -1)
                {
                    TRACE_E("You can call CSalamanderGeneral::PostMenuExtCommand only from main "
                            "thread when plugin entry-point is not finished yet!");
                }
                else
                { // 0 - unload, 1 - rebuild menu, 2-501 salCmd, 502-1000501 menuCmd
                    PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 502 + id);
                }
            }
            else
            {
                TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostMenuExtCommand().");
            }
        }
    }
    else
    {
        if (MainThreadID == GetCurrentThreadId())
        { // check for a call from the entry point (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You may not call CSalamanderGeneral::PostMenuExtCommand from entry-point!");
                return;
            }
        }
        else
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You may not call CSalamanderGeneral::PostMenuExtCommand when "
                        "entry-point is not finished yet!");
                return;
            }
        }
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            PostMessage(MainWindow->HWindow, WM_USER_POSTMENUEXTCMD, (WPARAM)Plugin, (LPARAM)id);
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostMenuExtCommand().");
        }
    }
}

BOOL CSalamanderGeneral::SalamanderIsNotBusy(DWORD* lastIdleTime)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalamanderIsNotBusy()");
    return ::SalamanderIsNotBusy(lastIdleTime);
}

void CSalamanderGeneral::CallLoadOrSaveConfiguration(BOOL load,
                                                     FSalLoadOrSaveConfiguration loadOrSaveFunc,
                                                     void* param)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CallLoadOrSaveConfiguration(%d, ,)", load);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CallLoadOrSaveConfiguration() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->CallLoadOrSaveConfiguration(load, loadOrSaveFunc, param);
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::CallLoadOrSaveConfiguration().");
    }
}

void CSalamanderGeneral::SetPluginBugReportInfo(const char* message, const char* email)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetPluginBugReportInfo(%s)", message);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginBugReportInfo() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        if (data->BugReportMessage != NULL)
            free(data->BugReportMessage);
        if (message != NULL)
            data->BugReportMessage = ::DupStr(message);
        else
            data->BugReportMessage = NULL;
        if (data->BugReportEMail != NULL)
            free(data->BugReportEMail);
        if (email != NULL)
        {
            data->BugReportEMail = ::DupStr(email);
            if (data->BugReportEMail != NULL && strlen(data->BugReportEMail) > 100)
            {
                data->BugReportEMail[100] = 0;
            }
        }
        else
            data->BugReportEMail = NULL;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginBugReportInfo().");
    }
}

void CSalamanderGeneral::FocusNameInPanel(int panel, const char* path, const char* name)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::FocusNameInPanel(%d, %s, %s)", panel, path, name);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::FocusNameInPanel() only from main thread!");
        return;
    }
    if (name == NULL || path == NULL)
    {
        TRACE_E("CSalamanderGeneral::FocusNameInPanel(): incorrect parameters (name == NULL || path == NULL)!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    char pathBackup[MAX_PATH + 200];
    char nameBackup[MAX_PATH + 200];
    // Synchronous focus delivery must never receive a truncated path or item name.
    if (FAILED(StringCchCopyA(pathBackup, _countof(pathBackup), path)) ||
        FAILED(StringCchCopyA(nameBackup, _countof(nameBackup), name)))
        return;
    if (p != NULL)
        SendMessage(p->HWindow, WM_USER_FOCUSFILE, (WPARAM)nameBackup, (LPARAM)pathBackup);
}

BOOL CSalamanderGeneral::ChangePanelPath(int panel, const char* path, int* failReason,
                                         int suggestedTopIndex, const char* suggestedFocusName,
                                         BOOL convertFSPathToInternal)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::ChangePanelPath(%d, %s, , %d, %s, %d)",
                        panel, path, suggestedTopIndex, suggestedFocusName, convertFSPathToInternal);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPath() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeDir(path, suggestedTopIndex, suggestedFocusName, 3 /*change-dir*/,
                            failReason, convertFSPathToInternal);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToDisk(int panel, const char* path, int* failReason,
                                               int suggestedTopIndex, const char* suggestedFocusName)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ChangePanelPathToDisk(%d, %s, , %d, %s)",
                        panel, path, suggestedTopIndex, suggestedFocusName);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToDisk() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToDisk(GetMsgBoxParent(), path, suggestedTopIndex, suggestedFocusName,
                                   NULL, TRUE, FALSE, FALSE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToArchive(int panel, const char* archive, const char* archivePath,
                                                  int* failReason, int suggestedTopIndex,
                                                  const char* suggestedFocusName, BOOL forceUpdate)
{
    CALL_STACK_MESSAGE7("CSalamanderGeneral::ChangePanelPathToArchive(%d, %s, %s, , %d, %s, %d)",
                        panel, archive, archivePath, suggestedTopIndex, suggestedFocusName, forceUpdate);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToArchive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToArchive(archive, archivePath, suggestedTopIndex, suggestedFocusName,
                                      forceUpdate, NULL, TRUE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToPluginFS(int panel, const char* fsName, const char* fsUserPart,
                                                   int* failReason, int suggestedTopIndex,
                                                   const char* suggestedFocusName, BOOL forceUpdate,
                                                   BOOL convertPathToInternal)
{
    CALL_STACK_MESSAGE8("CSalamanderGeneral::ChangePanelPathToPluginFS(%d, %s, %s, , %d, %s, %d, %d)",
                        panel, fsName, fsUserPart, suggestedTopIndex, suggestedFocusName, forceUpdate,
                        convertPathToInternal);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToPluginFS() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangePathToPluginFS(fsName, fsUserPart, suggestedTopIndex, suggestedFocusName,
                                       forceUpdate, 2 /*report all errors*/, NULL, TRUE, failReason,
                                       FALSE, FALSE, convertPathToInternal);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToDetachedFS(int panel, CPluginFSInterfaceAbstract* detachedFS,
                                                     int* failReason, int suggestedTopIndex,
                                                     const char* suggestedFocusName)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::ChangePanelPathToDetachedFS(%d, , , %d, %s)",
                        panel, suggestedTopIndex, suggestedFocusName);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToDetachedFS() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        int fsIndex = -1;
        CDetachedFSList* list = MainWindow->DetachedFSList;
        int i;
        for (i = 0; i < list->Count; i++)
        {
            if (list->At(i)->GetInterface() == detachedFS)
            {
                fsIndex = i;
                break;
            }
        }
        if (fsIndex != -1)
        {
            return p->ChangePathToDetachedFS(fsIndex, suggestedTopIndex, suggestedFocusName, TRUE, failReason);
        }
        else
        {
            TRACE_E("Parameter 'detachedFS' is not detached FS in "
                    "CSalamanderGeneral::ChangePanelPathToDetachedFS().");
        }
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToFixedDrive(int panel, int* failReason)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ChangePanelPathToFixedDrive(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToFixedDrive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeToFixedDrive(GetMsgBoxParent(), NULL, TRUE, FALSE, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

BOOL CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive(int panel, int* failReason)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive(%d,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ChangePanelPathToRescuePathOrFixedDrive() only from main thread!");
        if (failReason != NULL)
            *failReason = CHPPFR_INVALIDPATH;
        return FALSE;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        return p->ChangeToRescuePathOrFixedDrive(GetMsgBoxParent(), NULL, TRUE, FALSE, FSTRYCLOSE_CHANGEPATH, failReason);
    }
    if (failReason != NULL)
        *failReason = CHPPFR_INVALIDPATH;
    return FALSE;
}

void CSalamanderGeneral::RefreshPanelPath(int panel, BOOL forceRefresh, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::RefreshPanelPath(%d, %d, %d)",
                        panel, forceRefresh, focusFirstNewItem);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RefreshPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        if (forceRefresh && p->Is(ptZIPArchive))
        { // for archives ensure a hard refresh by invalidating the archive stamp
            p->SetZIPArchiveSize(CQuadWord(-1, -1));
        }
        p->FocusFirstNewItem = focusFirstNewItem;
        p->RefreshDirectory(FALSE, forceRefresh);
    }
}

void CSalamanderGeneral::PostRefreshPanelPath(int panel, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostRefreshPanelPath(%d, %d)", panel, focusFirstNewItem);
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        // post a hard refresh
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        p->FocusFirstNewItem = focusFirstNewItem; // not synchronized (may be called outside the main thread) but should not matter
        PostMessage(p->HWindow, WM_USER_REFRESH_DIR, 0, t1);
    }
}

void CSalamanderGeneral::PostRefreshPanelFS(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostRefreshPanelFS(, %d)", focusFirstNewItem);
    PostRefreshPanelFS2(modifiedFS, focusFirstNewItem);
}

BOOL CSalamanderGeneral::PostRefreshPanelFS2(CPluginFSInterfaceAbstract* modifiedFS, BOOL focusFirstNewItem)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostRefreshPanelFS2(, %d)", focusFirstNewItem);
    CFilesWindow* p = NULL;
    if (MainWindow != NULL)
    {
        // no synchronization issue, because PluginFS is cleared only after CloseFS, which
        // should terminate the thread monitoring FS changes (after CloseFS there should be no call to
        // PostRefreshPanelFS2)
        if (MainWindow->LeftPanel != NULL && MainWindow->LeftPanel->Is(ptPluginFS) &&
            MainWindow->LeftPanel->GetPluginFS()->Contains(modifiedFS))
        {
            p = MainWindow->LeftPanel;
        }
        if (MainWindow->RightPanel != NULL && MainWindow->RightPanel->Is(ptPluginFS) &&
            MainWindow->RightPanel->GetPluginFS()->Contains(modifiedFS))
        {
            p = MainWindow->RightPanel;
        }
    }
    if (p != NULL)
    {
        // post a hard refresh
        HANDLES(EnterCriticalSection(&TimeCounterSection));
        int t1 = MyTimeCounter++;
        HANDLES(LeaveCriticalSection(&TimeCounterSection));
        p->FocusFirstNewItem = focusFirstNewItem; // not synchronized (may be called outside the main thread) but should not matter
        PostMessage(p->HWindow, WM_USER_REFRESH_DIR, 0, t1);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL CSalamanderGeneral::CloseDetachedFS(HWND parent, CPluginFSInterfaceAbstract* detachedFS)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::CloseDetachedFS(,)");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CloseDetachedFS() only from main thread!");
        return FALSE;
    }
    if (MainWindow->DetachedFSList->IsGood()) // to guarantee Delete succeeds
    {
        CDetachedFSList* list = MainWindow->DetachedFSList;
        int i;
        for (i = 0; i < list->Count; i++)
        {
            if (list->At(i)->GetInterface() == detachedFS)
            {
                CPluginFSInterfaceEncapsulation* fs = list->At(i);
                BOOL dummy;
                if (fs->TryCloseOrDetach(FALSE, FALSE, dummy, FSTRYCLOSE_PLUGINCLOSEDETACHEDFS)) // the FS has no objection to closing
                {
                    CPluginInterfaceForFSEncapsulation plugin(fs->GetPluginInterfaceForFS()->GetInterface(),
                                                              fs->GetPluginInterfaceForFS()->GetBuiltForVersion());
                    if (plugin.NotEmpty())
                    {
                        fs->ReleaseObject(parent);
                        plugin.CloseFS(fs->GetInterface());
                        list->Delete(i);
                        if (!list->IsGood())
                            list->ResetState();
                        return TRUE;
                    }
                    else
                        TRACE_E("Unexpected situation in CSalamanderGeneral::CloseDetachedFS()");
                }
                break;
            }
        }
    }
    return FALSE;
}

BOOL CSalamanderGeneral::DuplicateAmpersands(char* buffer, int bufferSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::DuplicateAmpersands(%s, %d)", buffer, bufferSize);
    return ::DuplicateAmpersands(buffer, bufferSize);
}

void CSalamanderGeneral::RemoveAmpersands(char* text)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveAmpersands(%s)", text);
    ::RemoveAmpersands(text);
}

BOOL CSalamanderGeneral::ValidateVarString(HWND msgParent, const char* varText, int& errorPos1, int& errorPos2,
                                           const CSalamanderVarStrEntry* variables)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ValidateVarString(, %s, , ,)", varText);
    if (varText == NULL || variables == NULL)
    {
        TRACE_E("CSalamanderGeneral::ValidateVarString(): invalid parameters!");
        return FALSE;
    }
    return ::ValidateVarString(msgParent, varText, errorPos1, errorPos2, variables);
}

BOOL CSalamanderGeneral::ExpandVarString(HWND msgParent, const char* varText, char* buffer, int bufferLen,
                                         const CSalamanderVarStrEntry* variables, void* param,
                                         BOOL ignoreEnvVarNotFoundOrTooLong,
                                         DWORD* varPlacements, int* varPlacementsCount,
                                         BOOL detectMaxVarWidths, int* maxVarWidths,
                                         int maxVarWidthsCount)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::ExpandVarString(, %s, , %d, , , %d, , , %d, , %d)",
                        varText, bufferLen, ignoreEnvVarNotFoundOrTooLong, detectMaxVarWidths,
                        maxVarWidthsCount);
    if (bufferLen <= 0 || buffer == NULL || varText == NULL || variables == NULL)
    {
        TRACE_E("CSalamanderGeneral::ExpandVarString(): invalid parameters!");
        return FALSE;
    }
    return ::ExpandVarString(msgParent, varText, buffer, bufferLen, variables, param,
                             ignoreEnvVarNotFoundOrTooLong, varPlacements, varPlacementsCount,
                             detectMaxVarWidths, maxVarWidths, maxVarWidthsCount);
}

BOOL CSalamanderGeneral::EnumInstalledModules(int* index, char* module, char* version)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::EnumInstalledModules(, ,)");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::EnumInstalledModules() only from main thread!");
        return FALSE;
    }
    return Plugins.EnumInstalledModules(index, module, version);
}

BOOL CSalamanderGeneral::CopyTextToClipboard(const char* text, int textLen, BOOL showEcho, HWND echoParent)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::CopyTextToClipboard(, %d, %d,)", textLen, showEcho);
    // j.r. threw the text parameter, which did not have to be null-terminated
    if (text == NULL)
    {
        TRACE_E("Unexpected parameter (NULL) in CSalamanderGeneral::CopyTextToClipboard().");
        return FALSE;
    }
    return ::CopyTextToClipboard(text, textLen, showEcho, echoParent);
}

BOOL CSalamanderGeneral::CopyTextToClipboardW(const wchar_t* text, int textLen, BOOL showEcho, HWND echoParent)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::CopyTextToClipboardW(, %d, %d,)", textLen, showEcho);
    // j.r. threw the text parameter, which did not have to be null-terminated
    if (text == NULL)
    {
        TRACE_E("Unexpected parameter (NULL) in CSalamanderGeneral::CopyTextToClipboardW().");
        return FALSE;
    }
    return ::CopyTextToClipboardW(text, textLen, showEcho, echoParent);
}

BOOL CSalamanderGeneral::IsPluginInstalled(const char* pluginSPL)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsPluginInstalled(%s)", pluginSPL);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::IsPluginInstalled() only from main thread!");
        return FALSE;
    }
    if (pluginSPL != NULL)
    {
        CPluginData* data = Plugins.GetPluginDataFromSuffix(pluginSPL);
        return data != NULL;
    }
    else
    {
        TRACE_E("Unexpected parameter 'pluginSPL' (NULL) in CSalamanderGeneral::IsPluginInstalled().");
        return FALSE;
    }
}

BOOL ViewFileInPluginViewer(const char* pluginSPL,
                            CSalamanderPluginViewerData* pluginData,
                            BOOL useCache, const char* rootTmpPath,
                            const char* fileNameInCache, int& error)
{
    error = -1; // unknown
    if (pluginData == NULL || pluginData->Size < sizeof(CSalamanderPluginViewerData) ||
        pluginData->FileName == NULL || pluginData->FileName[0] == 0)
    {
        TRACE_E("Unexpected value of 'pluginData' in CSalamanderGeneral::ViewFileInPluginViewer!");
        return FALSE;
    }

    CALL_STACK_MESSAGE7("CSalamanderGeneral::ViewFileInPluginViewer(%s, %d, %s, %d, %s, %s,)",
                        pluginSPL, pluginData->Size, pluginData->FileName, useCache,
                        (useCache ? rootTmpPath : "(ignored)"),
                        (useCache ? fileNameInCache : "(ignored)"));

    char viewUniqueName[50]; // we need a unique name for the viewed file in the cache
    viewUniqueName[0] = 0;
    const char* fileName; // name of the file we will pass to the viewer
    if (useCache)
    {
        // verify that 'fileNameInCache' is valid (a name without path)
        const char* s = NULL;
        if (fileNameInCache != NULL)
        {
            s = fileNameInCache;
            while (*s != 0 && *s != '\\' && *s != '/' && *s != ':' &&
                   *s >= 32 && *s != '<' && *s != '>' && *s != '|' && *s != '"')
                s++;
        }
        if (fileNameInCache == NULL || fileNameInCache[0] == 0 || *s != 0)
        {
            TRACE_E("Unexpected value of 'fileNameInCache' in CSalamanderGeneral::ViewFileInPluginViewer!");
            error = 3;
            ::DeleteFileUtf8(pluginData->FileName);
            return FALSE;
        }

        // insert the file 'pluginData->FileName' into the disk cache under the name 'fileNameInCache'
        while (1)
        {
            // Keep the existing cache-key shape while folding the 64-bit uptime past the old tick-wrap boundary.
            const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
            // The cache key has a fixed local buffer, so preserve its format with bounded output.
            _snprintf_s(viewUniqueName, _countof(viewUniqueName), _TRUNCATE, "ViewFile %X", (DWORD)(timeSeed ^ (timeSeed >> 32)));
            BOOL exists;
            fileName = DiskCache.GetName(viewUniqueName, fileNameInCache, &exists, TRUE, rootTmpPath, FALSE, NULL, NULL);
            if (fileName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
            {
                if (!exists)
                    Sleep(100); // the file exists -> almost impossible, still handle it
                else            // fatal error
                {
                    error = 3;
                    ::DeleteFileUtf8(pluginData->FileName);
                    return FALSE; // fatal error
                }
            }
            else
                break; // we have the name in the disk cache, all OK
        }
        if (!::SalMoveFile(pluginData->FileName, fileName))
        {
            DWORD err = GetLastError();
            TRACE_E("Unable to move file to disk cache! (error " << ::GetErrorText(err) << ")");
            ::DeleteFileUtf8(pluginData->FileName);
            DiskCache.ReleaseName(viewUniqueName, FALSE);
            error = 3;
            return FALSE;
        }
        else // successfully obtained a temp file; we must call NamePrepared()
        {
            CQuadWord size(0, 0);
            HANDLE file = HANDLES_Q(CreateFileUtf8(fileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               NULL, OPEN_EXISTING, 0, NULL));
            if (file != INVALID_HANDLE_VALUE)
            { // ignore the error; the file size is not that important
                DWORD err;
                ::SalGetFileSize(file, size, err);
                HANDLES(CloseHandle(file));
            }

            DiskCache.NamePrepared(viewUniqueName, size);
        }
    }
    else
        fileName = pluginData->FileName;

    // position for viewers
    WINDOWPLACEMENT place;
    place.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(MainWindow->HWindow, &place);
    // GetWindowPlacement accounts for the taskbar, so if the taskbar is at the top or left,
    // the values are shifted by its dimensions. Apply a correction.
    RECT monitorRect;
    RECT workRect;
    MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect, &monitorRect);
    OffsetRect(&place.rcNormalPosition, workRect.left - monitorRect.left,
               workRect.top - monitorRect.top);
    //we do not want a minimized viewer, even if the main window is minimized
    if (place.showCmd == SW_MINIMIZE || place.showCmd == SW_SHOWMINIMIZED ||
        place.showCmd == SW_SHOWMINNOACTIVE)
        place.showCmd = SW_SHOWNORMAL;

    // finally open the viewer itself
    BOOL diskCacheNameClosed = FALSE;
    error = 0;
    if (pluginSPL != NULL) // viewer from a plug-in
    {
        CPluginData* data = Plugins.GetPluginDataFromSuffix(pluginSPL);
        if (data != NULL && data->SupportViewer)
        {
            if (data->InitDLL(MainWindow->HWindow)
                /*&& PluginIfaceForViewer.NotEmpty()*/) // redundant, because downgrade is impossible and InitDLL checks the interfaces
            {
                HANDLE lock = NULL;
                BOOL lockOwner = FALSE;
                BOOL ret = data->GetPluginInterfaceForViewer()->ViewFile(fileName, place.rcNormalPosition.left,
                                                                         place.rcNormalPosition.top,
                                                                         place.rcNormalPosition.right - place.rcNormalPosition.left,
                                                                         place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                                                                         place.showCmd, Configuration.AlwaysOnTop,
                                                                         useCache, &lock, &lockOwner, pluginData, -1, -1);
                if (!ret)
                {
                    TRACE_E("PluginIfaceForViewer.ViewFile() returns error.");
                    error = 2;
                }
                else
                {
                    if (useCache && lock != NULL)
                    {
                        if (lockOwner) // add the handle for 'lock' to HANDLES (the disk cache will want to close it and will look for it)
                            HANDLES_ADD(__htEvent, __hoCreateEvent, lock);
                        DiskCache.AssignName(viewUniqueName, lock, lockOwner, crtDirect);
                        diskCacheNameClosed = TRUE;
                    }
                }
            }
            else
                error = 1;
        }
        else
            error = 1;
        if (error == 1)
            TRACE_E("Unable to load plugin.");
    }
    else // internal viewer
    {
        if (Configuration.SavePosition &&
            Configuration.WindowPlacement.length != 0)
        {
            place = Configuration.WindowPlacement;
            // GetWindowPlacement accounts for the taskbar, so if the taskbar is at the top or left,
            // the values are shifted by its dimensions. Apply a correction.
            RECT monitorRect2;
            RECT workRect2;
            MultiMonGetClipRectByRect(&place.rcNormalPosition, &workRect2, &monitorRect2);
            OffsetRect(&place.rcNormalPosition, workRect2.left - monitorRect2.left,
                       workRect2.top - monitorRect2.top);
            MultiMonEnsureRectVisible(&place.rcNormalPosition, TRUE);
        }

        HANDLE lock = NULL;
        BOOL lockOwner = FALSE;
        if (OpenViewer(fileName, vtText,
                       place.rcNormalPosition.left,
                       place.rcNormalPosition.top,
                       place.rcNormalPosition.right - place.rcNormalPosition.left,
                       place.rcNormalPosition.bottom - place.rcNormalPosition.top,
                       place.showCmd, useCache, &lock, &lockOwner, pluginData, -1, -1))
        {
            if (useCache && lock != NULL)
            {
                DiskCache.AssignName(viewUniqueName, lock, lockOwner, crtDirect);
                diskCacheNameClosed = TRUE;
            }
        }
        else
        {
            TRACE_E("OpenViewer() returns error.");
            error = 2;
        }
    }

    // if we did not assign a name in the disk cache, release the record...
    if (useCache && !diskCacheNameClosed)
    {
        DiskCache.ReleaseName(viewUniqueName, FALSE);
        //    ::DeleteFileUtf8(fileName);   // the cache already removed the file and deallocated fileName
    }
    return error == 0; // returning success?
}

BOOL CSalamanderGeneral::ViewFileInPluginViewer(const char* pluginSPL,
                                                CSalamanderPluginViewerData* pluginData,
                                                BOOL useCache, const char* rootTmpPath,
                                                const char* fileNameInCache, int& error)
{
    error = -1; // unknown

    // guard against calls from outside the main thread and from the entry point
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin == -1)
    {
        if (MainThreadID == GetCurrentThreadId()) // if both errors occur (different thread + unfinished entry point), the entry point takes priority
            TRACE_E("You may not call CSalamanderGeneral::ViewFileInPluginViewer from entry-point!");
        else
            TRACE_E("You can call CSalamanderGeneral::ViewFileInPluginViewer only from main thread!");
        return FALSE;
    }

    return ::ViewFileInPluginViewer(pluginSPL, pluginData, useCache,
                                    rootTmpPath, fileNameInCache, error);
}

void CSalamanderGeneral::ExecuteAssociation(HWND parent, const char* path, const char* name)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::ExecuteAssociation(0x%p, %s, %s)", parent, path, name);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ExecuteAssociation() only from main thread!");
        return;
    }
    MainWindow->SetDefaultDirectories(); // so the starting process inherits the correct current directories
    ::ExecuteAssociation(parent, path, name);
}

int CSalamanderGeneral::GetPanelTopIndex(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelTopIndex(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelTopIndex() only from main thread!");
        return 0;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        return p->ListBox->GetTopIndex();
    return 0; // error; should not happen...
}

void CSalamanderGeneral::GetPanelEnumFilesParams(int panel, int* enumFilesSourceUID, int* enumFilesCurrentIndex)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetPanelEnumFilesParams(%d, ,)", panel);
    if (enumFilesCurrentIndex != NULL)
        *enumFilesCurrentIndex = -1;
    if (enumFilesSourceUID != NULL)
        *enumFilesSourceUID = -1;
    else
    {
        TRACE_E("CSalamanderGeneral::GetPanelEnumFilesParams(): 'enumFilesSourceUID' cannot be NULL!");
        return;
    }
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelEnumFilesParams() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->Is(ptDisk))
    {
        *enumFilesSourceUID = p->EnumFileNamesSourceUID;
        if (enumFilesCurrentIndex != NULL)
        {
            int i = p->GetCaretIndex();
            if (i >= p->Dirs->Count && i < p->Dirs->Count + p->Files->Count)
                *enumFilesCurrentIndex = i - p->Dirs->Count;
        }
    }
}

BOOL CSalamanderGeneral::GetPanelWithPluginFS(CPluginFSInterfaceAbstract* pluginFS, int& panel)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetPanelWithPluginFS(, )");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetPanelWithPluginFS() only from main thread!");
        return FALSE;
    }
    if (pluginFS == NULL)
        return FALSE;
    if (MainWindow->LeftPanel->Is(ptPluginFS) &&
        MainWindow->LeftPanel->GetPluginFS()->GetInterface() == pluginFS)
    {
        panel = PANEL_LEFT;
        return TRUE;
    }
    if (MainWindow->RightPanel->Is(ptPluginFS) &&
        MainWindow->RightPanel->GetPluginFS()->GetInterface() == pluginFS)
    {
        panel = PANEL_RIGHT;
        return TRUE;
    }
    return FALSE;
}

void CSalamanderGeneral::PostChangeOnPathNotification(const char* path, BOOL includingSubdirs)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PostChangeOnPathNotification(%s, %d)", path, includingSubdirs);
    MainWindow->PostChangeOnPathNotification(path, includingSubdirs);
}

DWORD
CSalamanderGeneral::SalCheckPath(BOOL echo, const char* path, DWORD err, HWND parent)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SalCheckPath(%d, %s, %u,)", echo, path, err);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckPath() only from main thread!");
        return ERROR_SUCCESS;
    }
    return ::SalCheckPath(echo, path, err, TRUE, parent); // the value of 'postRefresh' does not matter (StopRefresh is surely > 0)
}

BOOL CSalamanderGeneral::SalCheckAndRestorePath(HWND parent, const char* path, BOOL tryNet)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SalCheckAndRestorePath(, %s, %d)", path, tryNet);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckAndRestorePath() only from main thread!");
        return FALSE;
    }
    return ::SalCheckAndRestorePath(parent, path, tryNet);
}

BOOL CSalamanderGeneral::SalCheckAndRestorePathWithCut(HWND parent, char* path, BOOL& tryNet, DWORD& err,
                                                       DWORD& lastErr, BOOL& pathInvalid, BOOL& cut,
                                                       BOOL donotReconnect)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SalCheckAndRestorePathWithCut(, %s, %d, , , , , %d)",
                        path, tryNet, donotReconnect);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalCheckAndRestorePathWithCut() only from main thread!");
        lastErr = err = ERROR_SUCCESS;
        pathInvalid = TRUE;
        cut = FALSE;
        return FALSE;
    }
    return ::SalCheckAndRestorePathWithCut(parent, path, tryNet, err, lastErr, pathInvalid, cut,
                                           donotReconnect);
}

BOOL CSalamanderGeneral::SalParsePath(HWND parent, char* path, int& type, BOOL& isDir, char*& secondPart,
                                      const char* errorTitle, char* nextFocus, BOOL curPathIsDiskOrArchive,
                                      const char* curPath, const char* curArchivePath, int* error,
                                      int pathBufSize)
{
    CALL_STACK_MESSAGE7("CSalamanderGeneral::SalParsePath(, %s, , , , %s, , %d, %s, %s, , %d)",
                        path, errorTitle, curPathIsDiskOrArchive, curPath, curArchivePath,
                        pathBufSize);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SalParsePath() only from main thread!");
        if (error != NULL)
            *error = SPP_WINDOWSPATHERROR;
        return FALSE;
    }
    return ::SalParsePath(parent, path, type, isDir, secondPart, errorTitle, nextFocus,
                          curPathIsDiskOrArchive, curPath, curArchivePath, error, pathBufSize);
}

BOOL CSalamanderGeneral::SalSplitWindowsPath(HWND parent, const char* title, const char* errorTitle,
                                             int selCount, char* path, char* secondPart, BOOL pathIsDir,
                                             BOOL backslashAtEnd, const char* dirName,
                                             const char* curDiskPath, char*& mask)
{
    CALL_STACK_MESSAGE10("CSalamanderGeneral::SalSplitWindowsPath(, %s, %s, %d, %s, %s, %d, %d, %s, %s,)",
                         title, errorTitle, selCount, path, secondPart, pathIsDir, backslashAtEnd,
                         dirName, curDiskPath);
    return ::SalSplitWindowsPath(parent, title, errorTitle, selCount, path, secondPart,
                                 pathIsDir, backslashAtEnd, dirName, curDiskPath, mask);
}

BOOL CSalamanderGeneral::SalSplitGeneralPath(HWND parent, const char* title, const char* errorTitle,
                                             int selCount, char* path, char* afterRoot, char* secondPart,
                                             BOOL pathIsDir, BOOL backslashAtEnd, const char* dirName,
                                             const char* curPath, char*& mask, char* newDirs,
                                             SGP_IsTheSamePathF isTheSamePathF)
{
    CALL_STACK_MESSAGE11("CSalamanderGeneral::SalSplitGeneralPath(, %s, %s, %d, %s, %s, %s, %d, %d, %s, %s, , ,)",
                         title, errorTitle, selCount, path, afterRoot, secondPart, pathIsDir, backslashAtEnd,
                         dirName, curPath);
    return ::SalSplitGeneralPath(parent, title, errorTitle, selCount, path, afterRoot, secondPart,
                                 pathIsDir, backslashAtEnd, dirName, curPath, mask, newDirs,
                                 isTheSamePathF);
}

BOOL CSalamanderGeneral::SalRemovePointsFromPath(char* afterRoot)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SalRemovePointsFromPath(%s)", afterRoot);
    return ::SalRemovePointsFromPath(afterRoot);
}

BOOL CSalamanderGeneral::GetConfigParameter(int paramID, void* buffer, int bufferSize, int* type)
{
    SLOW_CALL_STACK_MESSAGE3("CSalamanderGeneral::GetConfigParameter(%d, , %d,)", paramID, bufferSize);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetConfigParameter() only from main thread!");
        if (type != NULL)
            *type = SALCFGTYPE_NOTFOUND;
        return FALSE;
    }
    char auxBuf[500];
    int auxType = SALCFGTYPE_BOOL;
    int auxDataSize = 4;
    BOOL ret = TRUE;
    switch (paramID)
    {
    case SALCFG_SELOPINCLUDEDIRS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.IncludeDirs;
        break;
    case SALCFG_SAVEONEXIT:
        // Preserve the plug-in ABI while reporting the new invariant: configuration persistence is always enabled.
        *((DWORD*)auxBuf) = TRUE;
        break;
    case SALCFG_MINBEEPWHENDONE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.MinBeepWhenDone;
        break;
    case SALCFG_HIDEHIDDENORSYSTEMFILES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.NotHiddenSystemFiles;
        break;
    case SALCFG_ALWAYSONTOP:
        *((DWORD*)auxBuf) = (DWORD)Configuration.AlwaysOnTop;
        break;
        //    case SALCFG_FASTDIRMOVE: *((DWORD *)auxBuf) = (DWORD)Configuration.FastDirectoryMove; break;
    case SALCFG_SORTUSESLOCALE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortUsesLocale;
        break;
    case SALCFG_SORTDETECTNUMBERS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortDetectNumbers;
        break;
    case SALCFG_SORTBYEXTDIRSASFILES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SortDirsByExt;
        break;
    case SALCFG_SINGLECLICK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SingleClick;
        break;
    case SALCFG_TOPTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.TopToolBarVisible;
        break;
    case SALCFG_MIDDLETOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.MiddleToolBarVisible;
        break;
    case SALCFG_BOTTOMTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.BottomToolBarVisible;
        break;
    case SALCFG_USERMENUTOOLBARVISIBLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UserMenuToolBarVisible;
        break;
    case SALCFG_SAVEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SaveHistory;
        break;
    case SALCFG_ENABLECMDLINEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EnableCmdLineHistory;
        break;
    case SALCFG_SAVECMDLINEHISTORY:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SaveCmdLineHistory;
        break;
    case SALCFG_SIZEFORMAT:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SizeFormat;
        break;
    case SALCFG_SELECTWHOLENAME:
        *((DWORD*)auxBuf) = (DWORD)Configuration.QuickRenameSelectAll;
        break;

    case SALCFG_FILENAMEFORMAT:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.FileNameFormat;
        break;
    }

    case SALCFG_INFOLINECONTENT:
    {
        auxType = SALCFGTYPE_STRING;
        auxDataSize = (int)strlen(Configuration.InfoLineContent) + 1;
        if (auxDataSize > 200)
            auxDataSize = 200; // we limited the required buffer to 200 characters
        memcpy(auxBuf, Configuration.InfoLineContent, auxDataSize);
        auxBuf[auxDataSize - 1] = 0;
        break;
    }

    case SALCFG_USERECYCLEBIN:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseRecycleBin;
        break;
    }

    case SALCFG_RECYCLEBINMASKS:
    {
        auxType = SALCFGTYPE_STRING;
        auxDataSize = (int)strlen(Configuration.RecycleMasks.GetMasksString()) + 1;
        if (auxDataSize > MAX_PATH)
            auxDataSize = MAX_PATH; // we limited the required buffer to MAX_PATH characters
        memcpy(auxBuf, Configuration.RecycleMasks.GetMasksString(), auxDataSize);
        auxBuf[auxDataSize - 1] = 0;
        break;
    }

    case SALCFG_COMPDIRSUSETIMERES:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseTimeResolution;
        break;

    case SALCFG_COMPDIRTIMERES:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.TimeResolution;
        break;
    }

    case SALCFG_CNFRMFILEDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmFileDirDel;
        break;
    case SALCFG_CNFRMNEDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmNEDirDel;
        break;
    case SALCFG_CNFRMFILEOVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmFileOver;
        break;
    case SALCFG_CNFRMDIROVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmDirOver;
        break;
    case SALCFG_CNFRMSHFILEDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHFileDel;
        break;
    case SALCFG_CNFRMSHDIRDEL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHDirDel;
        break;
    case SALCFG_CNFRMSHFILEOVER:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmSHFileOver;
        break;
    case SALCFG_CNFRMCREATEPATH:
        *((DWORD*)auxBuf) = (DWORD)Configuration.CnfrmCreatePath;
        break;
    case SALCFG_DRVSPECFLOPPYMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFloppyMon;
        break;
    case SALCFG_DRVSPECFLOPPYSIM:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFloppySimple;
        break;
    case SALCFG_DRVSPECREMOVABLEMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemovableMon;
        break;
    case SALCFG_DRVSPECREMOVABLESIM:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemovableSimple;
        break;
    case SALCFG_DRVSPECFIXEDMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFixedMon;
        break;
    case SALCFG_DRVSPECFIXEDSIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecFixedSimple;
        break;
    case SALCFG_DRVSPECREMOTEMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteMon;
        break;
    case SALCFG_DRVSPECREMOTESIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteSimple;
        break;
    case SALCFG_DRVSPECREMOTEDONOTREF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecRemoteDoNotRefreshOnAct;
        break;
    case SALCFG_DRVSPECCDROMMON:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecCDROMMon;
        break;
    case SALCFG_DRVSPECCDROMSIMPLE:
        *((DWORD*)auxBuf) = (DWORD)Configuration.DrvSpecCDROMSimple;
        break;

    case SALCFG_IFPATHISINACCESSIBLEGOTO:
    {
        auxType = SALCFGTYPE_STRING;
        char ifPathIsInaccessibleGoTo[MAX_PATH];
        GetIfPathIsInaccessibleGoTo(ifPathIsInaccessibleGoTo, _countof(ifPathIsInaccessibleGoTo), FALSE);
        auxDataSize = (int)strlen(ifPathIsInaccessibleGoTo) + 1;
        if (auxDataSize > MAX_PATH)
            auxDataSize = MAX_PATH; // we limited the required buffer to MAX_PATH characters
        memcpy(auxBuf, ifPathIsInaccessibleGoTo, auxDataSize);
        auxBuf[auxDataSize - 1] = 0;
        break;
    }

    case SALCFG_VIEWEREOLCRLF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_CRLF;
        break;
    case SALCFG_VIEWEREOLCR:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_CR;
        break;
    case SALCFG_VIEWEREOLLF:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_LF;
        break;
    case SALCFG_VIEWEREOLNULL:
        *((DWORD*)auxBuf) = (DWORD)Configuration.EOL_NULL;
        break;
    case SALCFG_VIEWERSAVEPOSITION:
        *((DWORD*)auxBuf) = (DWORD)Configuration.SavePosition;
        break;
    case SALCFG_VIEWERWRAPTEXT:
        *((DWORD*)auxBuf) = (DWORD)Configuration.WrapText;
        break;
    case SALCFG_AUTOCOPYSELTOCLIPBOARD:
        *((DWORD*)auxBuf) = (DWORD)Configuration.AutoCopySelection;
        break;

    case SALCFG_VIEWERTABSIZE:
    {
        auxType = SALCFGTYPE_INT;
        auxDataSize = 4;
        *((DWORD*)auxBuf) = (DWORD)Configuration.TabSize;
        break;
    }

    case SALCFG_VIEWERFONT:
    {
        auxType = SALCFGTYPE_LOGFONT;
        auxDataSize = sizeof(LOGFONT);
        if (UseCustomViewerFont)
            *((LOGFONT*)auxBuf) = ViewerLogFont;
        else
            GetDefaultViewerLogFont((LOGFONT*)auxBuf);
        break;
    }

    case SALCFG_ARCOTHERPANELFORPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseAnotherPanelForPack;
        break;
    case SALCFG_ARCOTHERPANELFORUNPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseAnotherPanelForUnpack;
        break;
    case SALCFG_ARCSUBDIRBYARCFORUNPACK:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseSubdirNameByArchiveForUnpack;
        break;
    case SALCFG_ARCUSESIMPLEICONS:
        *((DWORD*)auxBuf) = (DWORD)Configuration.UseSimpleIconsInArchives;
        break;

    default:
    {
        auxType = SALCFGTYPE_NOTFOUND;
        auxDataSize = 0;
        TRACE_E("Unknown parameter ID (" << paramID << ") in CSalamanderGeneral::GetConfigParameter().");
        ret = FALSE;
    }
    }
    if (type != NULL)
        *type = auxType;
    if (auxDataSize > 0 && auxDataSize <= bufferSize)
        memcpy(buffer, auxBuf, auxDataSize);
    else
    {
        if (bufferSize > 0 && auxDataSize > 0) // copy at least what fits
        {
            memcpy(buffer, auxBuf, bufferSize);
            if (auxType == SALCFGTYPE_STRING)
                ((char*)buffer)[bufferSize - 1] = 0; // trim the string with a zero
        }
        ret = FALSE;
    }
    return ret;
}

void CSalamanderGeneral::AlterFileName(char* tgtName, char* srcName, int format, int changedParts,
                                       BOOL isDir)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::AlterFileName(, %s, %d, %d, %d)",
                        srcName, format, changedParts, isDir);
    ::AlterFileName(tgtName, srcName, -1, format, changedParts, isDir);
}

void CSalamanderGeneral::CreateSafeWaitWindow(const char* message, const char* caption,
                                              int delay, BOOL showCloseButton, HWND hForegroundWnd)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::CreateSafeWaitWindow(%s, , %d, %d, 0x%p)", message, delay, showCloseButton, hForegroundWnd);
    ::CreateSafeWaitWindow(message, caption, delay, showCloseButton, hForegroundWnd);
}

void CSalamanderGeneral::DestroySafeWaitWindow()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DestroySafeWaitWindow()");
    ::DestroySafeWaitWindow();
}

void CSalamanderGeneral::ShowSafeWaitWindow(BOOL show)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ShowSafeWaitWindow(%d)", show);
    ::ShowSafeWaitWindow(show);
}

BOOL CSalamanderGeneral::GetSafeWaitWindowClosePressed()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetSafeWaitWindowClosePressed()");
    return ::GetSafeWaitWindowClosePressed();
}

void CSalamanderGeneral::SetSafeWaitWindowText(const char* message)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetSafeWaitWindowText(%s)", message);
    ::SetSafeWaitWindowText(message);
}

BOOL CSalamanderGeneral::GetFileFromCache(const char* uniqueFileName, const char*& tmpName,
                                          HANDLE fileLock)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetFileFromCache(%s, ,)", uniqueFileName);
    tmpName = NULL;
    if (uniqueFileName == NULL || fileLock == NULL)
    {
        TRACE_E("Invalid parameter in CSalamanderGeneral::GetFileFromCache!");
        return FALSE;
    }

    BOOL fileExists;
    const char* name = DiskCache.GetName(uniqueFileName, NULL, &fileExists, FALSE, NULL, FALSE, NULL, NULL);
    if (name != NULL) // file found
    {
        if (!fileExists) // some helpful soul deleted it straight from the disk
        {
            // cannot prepare the file; tell the disk cache we give up
            DiskCache.ReleaseName(uniqueFileName, FALSE);
        }
        else
        {
            DiskCache.AssignName(uniqueFileName, fileLock, FALSE, crtCache);
            tmpName = name;
            return TRUE;
        }
    }
    return FALSE;
}

void CSalamanderGeneral::UnlockFileInCache(HANDLE fileLock)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::UnlockFileInCache(0x%p)", fileLock);

    SetEvent(fileLock); // start cleaning up the file
    DiskCache.WaitForIdle();
    ResetEvent(fileLock); // finish cleaning up the file
}

BOOL CSalamanderGeneral::MoveFileToCache(const char* uniqueFileName, const char* nameInCache,
                                         const char* rootTmpPath, const char* newFileName,
                                         const CQuadWord& newFileSize, BOOL* alreadyExists)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::MoveFileToCache(%s, %s, %s, %s, %g, )",
                        uniqueFileName, nameInCache, rootTmpPath, newFileName, newFileSize.GetDouble());
    if (alreadyExists != NULL)
        *alreadyExists = FALSE;
    if (uniqueFileName == NULL || newFileName == NULL || nameInCache == NULL)
    {
        TRACE_E("Invalid parameter in CSalamanderGeneral::GetFileFromCache!");
        return FALSE;
    }

    // verify that 'nameInCache' is valid (a name without a path)
    const char* s = nameInCache;
    while (*s != 0 && *s != '\\' && *s != '/' && *s != ':' &&
           *s >= 32 && *s != '<' && *s != '>' && *s != '|' && *s != '"')
        s++;
    if (nameInCache[0] == 0 || *s != 0)
    {
        TRACE_E("Unexpected value of 'nameInCache' in CSalamanderGeneral::MoveFileToCache!");
        return FALSE;
    }

    // add the file 'newFileName' to the disk cache under the name 'uniqueFileName'
    BOOL exists;
    const char* fileName = DiskCache.GetName(uniqueFileName, nameInCache, &exists, TRUE, rootTmpPath, FALSE, NULL, NULL);
    if (fileName == NULL) // error (if 'exists' is TRUE -> fatal, otherwise "file already exists")
    {
        if (alreadyExists != NULL)
            *alreadyExists = !exists;
        return FALSE;
    }

    if (!::SalMoveFile(newFileName, fileName))
    {
        DWORD err = GetLastError();
        TRACE_E("Unable to move file to disk cache! (error " << ::GetErrorText(err) << ")");
        DiskCache.ReleaseName(uniqueFileName, FALSE); // nothing to keep in the cache
        return FALSE;
    }
    else // successfully obtained a temp file; we must call NamePrepared()
    {
        DiskCache.NamePrepared(uniqueFileName, newFileSize);
        DiskCache.ReleaseName(uniqueFileName, TRUE); // leave the prepared file in the cache (even if it is not locked)
        return TRUE;
    }
}

void CSalamanderGeneral::RemoveOneFileFromCache(const char* uniqueFileName)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveOneFileFromCache(%s)", uniqueFileName);
    if (uniqueFileName == NULL)
    {
        TRACE_E("Invalid parametr (NULL) in CSalamanderGeneral::RemoveOneFileFromCache!");
        return;
    }
    DiskCache.FlushOneFile(uniqueFileName);
}

void CSalamanderGeneral::RemoveFilesFromCache(const char* fileNamesRoot)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveFilesFromCache(%s)", fileNamesRoot);
    if (fileNamesRoot == NULL)
    {
        TRACE_E("Invalid parametr (NULL) in CSalamanderGeneral::RemoveFilesFromCache!");
        return;
    }
    DiskCache.FlushCache(fileNamesRoot);
}

BOOL CSalamanderGeneral::EnumConversionTables(HWND parent, int* index, const char** name, const char** table)
{
    if (index == NULL)
    {
        TRACE_E("Unexpected value of 'index' (NULL) in CSalamanderGeneral::EnumConversionTables().");
        return FALSE;
    }
    CALL_STACK_MESSAGE2("CSalamanderGeneral::EnumConversionTables(, %d, ,)", *index);
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    return CodeTables.EnumCodeTables(parent, index, name, table);
}

BOOL CSalamanderGeneral::GetConversionTable(HWND parent, char* table, const char* conversion)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetConversionTable(, , %s)", conversion);
    if (table == NULL)
    {
        TRACE_E("Invalid parametr (table==NULL) in CSalamanderGeneral::GetConversionTable!");
        return FALSE;
    }
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    BOOL ret = CodeTables.Init(parent);
    int codeType;
    if (ret)
        ret &= CodeTables.GetCodeType(conversion, codeType);
    if (ret)
        ret &= CodeTables.GetCode(table, codeType);
    return ret;
}

void CSalamanderGeneral::GetWindowsCodePage(HWND parent, char* codePage)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetWindowsCodePage(,)");
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    CodeTables.Init(parent);
    CodeTables.GetWinCodePage(codePage);
}

void CSalamanderGeneral::RecognizeFileType(HWND parent, const char* pattern, int patternLen, BOOL forceText,
                                           BOOL* isText, char* codePage)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::RecognizeFileType(, , %d, %d, ,)", patternLen, forceText);
    parent = (parent == NULL ? MainWindow->HWindow : parent);
    ::RecognizeFileType(parent, pattern, patternLen, forceText, isText, codePage);
}

BOOL CSalamanderGeneral::IsANSIText(const char* text, int textLen)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsANSIText(, %d)", textLen);

    const unsigned char* s = (const unsigned char*)text;
    const unsigned char* end = s + textLen;
    while (s < end)
    {
        if (*s < ' ' && *s != '\a' && *s != '\b' && *s != '\r' && // *s != 0 must not be here because of Unicode files ("0A 00" cannot be expanded to "0D 0A 00")
            *s != '\f' && *s != '\n' && *s != '\t' && *s != '\v' &&
            *s != '\x1a' && *s != '\x04' && *s != '\x06')
        { // disallowed character
            break;
        }
        s++;
    }
    return s == end;
}

BOOL CSalamanderGeneral::SalMoveFile(const char* srcName, const char* destName, DWORD* err)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::SalMoveFile(%s, %s,)", srcName, destName);
    BOOL ret = ::SalMoveFile(srcName, destName);
    if (err != NULL)
        *err = GetLastError();
    return ret;
}

BOOL CSalamanderGeneral::SalGetFileSize(HANDLE file, CQuadWord& size, DWORD& err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileSize(, ,)");
    return ::SalGetFileSize(file, size, err);
}

BOOL CSalamanderGeneral::GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title,
                                            const char* comment, char* path, BOOL onlyNet,
                                            const char* initDir)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetTargetDirectory(, , %s, %s, , %d, %s)",
                        title, comment, onlyNet, initDir);
    return ::GetTargetDirectory(parent, hCenterWindow, title, comment, path, onlyNet, initDir);
}

void CSalamanderGeneral::CallPluginOperationFromDisk(int panel, SalPluginOperationFromDisk callback,
                                                     void* param)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CallPluginOperationFromDisk(%d, ,)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::CallPluginOperationFromDisk() only from main thread!");
        return;
    }
    if (callback == NULL)
    {
        TRACE_E("Unexpected value of parameter 'callback' (NULL) in CSalamanderGeneral::CallPluginOperationFromDisk().");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        if (!p->Is(ptDisk))
        {
            TRACE_E("CSalamanderGeneral::CallPluginOperationFromDisk(): there must be windows (disk) path in panel!");
            return;
        }
        if (p->Files->Count + p->Dirs->Count <= 0)
        {
            TRACE_I("CSalamanderGeneral::CallPluginOperationFromDisk(): no items in panel!");
            return;
        }
        // prepare data for enumerating files and directories from the panel
        CPanelTmpEnumData data;
        int oneIndex = -1;
        int count = p->GetSelCount();
        if (count > 0) // some files are selected
        {
            data.IndexesCount = count;
            data.Indexes = new int[count];
            if (data.Indexes == NULL)
            {
                TRACE_E(LOW_MEMORY);
                return;
            }
            else
                p->GetSelItems(count, data.Indexes);
        }
        else // take the focus
        {
            oneIndex = p->GetCaretIndex();

            BOOL subDir;
            if (p->Dirs->Count > 0)
                subDir = (strcmp(p->Dirs->At(0).Name, "..") == 0);
            else
                subDir = FALSE;
            if (oneIndex == 0 && subDir)
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::CallPluginOperationFromDisk(): no files nor directories selected and focus is on up-dir symbol.");
                return;
            }

            data.IndexesCount = 1;
            data.Indexes = &oneIndex; // not deallocated
        }
        data.CurrentIndex = 0;
        data.ZIPPath = p->GetZIPPath();
        data.Dirs = p->Dirs;
        data.Files = p->Files;
        data.ArchiveDir = p->GetArchiveDir();
        // The disk-operation callback must enumerate from a complete working path.
        if (FAILED(StringCchCopyA(data.WorkPath, _countof(data.WorkPath), p->GetPath())))
        {
            if (count > 0)
                delete[] (data.Indexes);
            return;
        }
        data.EnumLastDir = NULL;
        data.EnumLastIndex = -1;

        callback(p->GetPath(), PanelEnumDiskSelection, &data, param);

        if (count > 0)
            delete[] (data.Indexes);
    }
}

BYTE CSalamanderGeneral::GetUserDefaultCharset()
{
    return (BYTE)UserCharset;
}

