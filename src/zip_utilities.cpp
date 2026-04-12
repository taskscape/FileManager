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

// Globals defined in zip_progress.cpp
extern const char* STR_NONE;
extern CSalamanderDirectory GlobalEmptySalDir;
extern HWND ProgressDialogActivateDrop;

class CSalamanderBMSearchDataImp : public CSalamanderBMSearchData
{
protected:
    CSearchData Moore;

public:
    CSalamanderBMSearchDataImp() : Moore() {}

    virtual void WINAPI Set(const char* pattern, WORD flags) { Moore.Set(pattern, flags); }
    virtual void WINAPI Set(const char* pattern, const int length, WORD flags) { Moore.Set(pattern, length, flags); }
    virtual void WINAPI SetFlags(WORD flags) { Moore.SetFlags(flags); }
    virtual int WINAPI GetLength() const { return Moore.GetLength(); }
    virtual const char* WINAPI GetPattern() const { return Moore.GetPattern(); }
    virtual BOOL WINAPI IsGood() const { return Moore.IsGood(); }
    virtual int WINAPI SearchForward(const char* text, int length, int start) { return Moore.SearchForward(text, length, start); }
    virtual int WINAPI SearchBackward(const char* text, int length) { return Moore.SearchBackward(text, length); }
};

CSalamanderBMSearchData*
CSalamanderGeneral::AllocSalamanderBMSearchData()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderBMSearchData()");
    CSalamanderBMSearchData* ret = new CSalamanderBMSearchDataImp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderBMSearchData(CSalamanderBMSearchData* data)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderBMSearchData()");
    if (data != NULL)
        delete ((CSalamanderBMSearchDataImp*)data);
}

class CSalamanderREGEXPSearchDataImp : public CSalamanderREGEXPSearchData
{
protected:
    CRegularExpression REGEXP;

public:
    CSalamanderREGEXPSearchDataImp() : REGEXP() {}

    virtual BOOL WINAPI Set(const char* pattern, WORD flags) { return REGEXP.Set(pattern, flags); }
    virtual BOOL WINAPI SetFlags(WORD flags) { return REGEXP.SetFlags(flags); }
    virtual const char* WINAPI GetLastErrorText() const { return REGEXP.GetLastErrorText(); }
    virtual const char* WINAPI GetPattern() const { return REGEXP.GetPattern(); }
    virtual BOOL WINAPI SetLine(const char* start, const char* end)
    {
        REGEXP.SetLine(start, end);
        return TRUE;
    }
    virtual int WINAPI SearchForward(int start, int& foundLen) { return REGEXP.SearchForward(start, foundLen); }
    virtual int WINAPI SearchBackward(int length, int& foundLen) { return REGEXP.SearchBackward(length, foundLen); }
};

CSalamanderREGEXPSearchData*
CSalamanderGeneral::AllocSalamanderREGEXPSearchData()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderREGEXPSearchData()");
    CSalamanderREGEXPSearchData* ret = new CSalamanderREGEXPSearchDataImp;
    return ret;
}

void CSalamanderGeneral::FreeSalamanderREGEXPSearchData(CSalamanderREGEXPSearchData* data)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderREGEXPSearchData()");
    if (data != NULL)
        delete ((CSalamanderREGEXPSearchDataImp*)data);
}

struct CSalCommandsAux
{
    int SalCmd;
    int Cmd;
    int TextID;
    DWORD* Enabled; // NULL == "always TRUE"
    int Type;
};

CSalCommandsAux SalCommandsArray[] = // ends with an item whose 'SalCmd' == -1
    {
        {SALCMD_VIEW, CM_VIEW, IDS_MENU_FILES_VIEW, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_ALTVIEW, CM_ALTVIEW, IDS_MENU_FILES_ALTVIEW, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_VIEWWITH, CM_VIEW_WITH, IDS_SALCMD_VIEWWITH, &EnablerViewFile, sctyForFocusedFile},
        {SALCMD_EDIT, CM_EDIT, IDS_MENU_FILES_EDIT, &EnablerFileOnDiskOrArchive, sctyForFocusedFile},
        {SALCMD_EDITWITH, CM_EDIT_WITH, IDS_SALCMD_EDITWITH, &EnablerFileOnDiskOrArchive, sctyForFocusedFile},

        {SALCMD_OPEN, CM_OPEN, IDS_SALCMD_OPEN, NULL, sctyForFocusedFileOrDirectory},
        {SALCMD_QUICKRENAME, CM_RENAMEFILE, IDS_MENU_FILES_RENAME, &EnablerQuickRename, sctyForFocusedFileOrDirectory},

        {SALCMD_COPY, CM_COPYFILES, IDS_MENU_FILES_COPY, &EnablerFilesCopy, sctyForSelectedFilesAndDirectories},
        {SALCMD_MOVE, CM_MOVEFILES, IDS_MENU_FILES_MOVE, &EnablerFilesMove, sctyForSelectedFilesAndDirectories},
        {SALCMD_EMAIL, CM_EMAILFILES, IDS_MENU_FILES_EMAIL, &EnablerFilesOnDisk, sctyForSelectedFilesAndDirectories},
        {SALCMD_DELETE, CM_DELETEFILES, IDS_MENU_FILES_DELETE, &EnablerFilesDelete, sctyForSelectedFilesAndDirectories},
        {SALCMD_PROPERTIES, CM_PROPERTIES, IDS_MENU_FILES_PROPERTIES, &EnablerShowProperties, sctyForSelectedFilesAndDirectories},
        {SALCMD_CHANGECASE, CM_CHANGECASE, IDS_MENU_FILES_CHANGECASE, &EnablerFilesOnDisk, sctyForSelectedFilesAndDirectories},
        {SALCMD_CHANGEATTRS, CM_CHANGEATTR, IDS_MENU_FILES_CHANGEATTR, &EnablerChangeAttrs, sctyForSelectedFilesAndDirectories},
        {SALCMD_OCCUPIEDSPACE, CM_OCCUPIEDSPACE, IDS_MENU_CMD_OCCUPIED, &EnablerOccupiedSpace, sctyForSelectedFilesAndDirectories},

        {SALCMD_EDITNEWFILE, CM_EDITNEW, IDS_MENU_FILES_EDITNEW, &EnablerOnDisk, sctyForCurrentPath},
        {SALCMD_REFRESH, CM_ACTIVEREFRESH, IDS_MENU_LEFT_REFRESH, NULL, sctyForCurrentPath},
        {SALCMD_CREATEDIRECTORY, CM_CREATEDIR, IDS_MENU_CMD_CREATEDIR, &EnablerCreateDir, sctyForCurrentPath},
        {SALCMD_DRIVEINFO, CM_DRIVEINFO, IDS_MENU_CMD_DRIVEINFO, &EnablerDriveInfo, sctyForCurrentPath},
        {SALCMD_CALCDIRSIZES, CM_CALCDIRSIZES, IDS_MENU_CMD_CALCDIRSIZES, &EnablerCalcDirSizes, sctyForCurrentPath},

        {SALCMD_DISCONNECT, CM_DISCONNECTNET, IDS_MENU_CMD_DISCONNECTNET, NULL, sctyForConnectedDrivesAndFS},

        {-1, -1, -1, NULL, sctyUnknown} // terminator
};

int GetWMCommandFromSalCmd(int salCmd)
{
    int index = 0;
    while (SalCommandsArray[index].SalCmd != -1)
    {
        if (SalCommandsArray[index].SalCmd == salCmd)
            return SalCommandsArray[index].Cmd;
        index++;
    }
    TRACE_E("You have used CSalamanderGeneral::PostSalamanderCommand for invalid command (" << salCmd << ")!");
    return -1;
}

BOOL CSalamanderGeneral::GetSalamanderCommand(int salCmd, char* nameBuf, int nameBufSize, BOOL* enabled,
                                              int* type)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetSalamanderCommand(%d, , %d, ,)", salCmd, nameBufSize);
    int index = 0;
    while (SalCommandsArray[index].SalCmd != -1)
    {
        if (SalCommandsArray[index].SalCmd == salCmd) // found it
        {
            // need to compute the command states; SalCommandsArray uses them
            MainWindow->OnEnterIdle();

            if (nameBuf != NULL && nameBufSize > 0)
            {
                lstrcpyn(nameBuf, ::LoadStr(SalCommandsArray[index].TextID), nameBufSize);
            }
            if (SalCommandsArray[index].Enabled != NULL)
            {
                if (enabled != NULL)
                    *enabled = *(SalCommandsArray[index].Enabled);
            }
            if (type != NULL)
                *type = SalCommandsArray[index].Type;

            return TRUE;
        }
        index++;
    }
    return FALSE;
}

BOOL CSalamanderGeneral::EnumSalamanderCommands(int* index, int* salCmd, char* nameBuf, int nameBufSize,
                                                BOOL* enabled, int* type)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::EnumSalamanderCommands(, , , %d, ,)", nameBufSize);
    if (salCmd != NULL)
        *salCmd = -1;
    if (nameBuf != NULL && nameBufSize > 0)
        nameBuf[0] = 0;
    if (enabled != NULL)
        *enabled = TRUE;
    if (type != NULL)
        *type = sctyUnknown;

    if (index != NULL && *index >= 0 && SalCommandsArray[*index].SalCmd != -1)
    {
        if (*index == 0)
        {
            // need to compute the command states; SalCommandsArray uses them
            MainWindow->OnEnterIdle();
        }

        if (salCmd != NULL)
            *salCmd = SalCommandsArray[*index].SalCmd;
        if (nameBuf != NULL && nameBufSize > 0)
        {
            lstrcpyn(nameBuf, ::LoadStr(SalCommandsArray[*index].TextID), nameBufSize);
        }
        if (SalCommandsArray[*index].Enabled != NULL)
        {
            if (enabled != NULL)
                *enabled = *(SalCommandsArray[*index].Enabled);
        }
        if (type != NULL)
            *type = SalCommandsArray[*index].Type;

        (*index)++;
        return TRUE;
    }
    return FALSE;
}

void CSalamanderGeneral::PostSalamanderCommand(int salCmd)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostSalamanderCommand(%d)", salCmd);
    if (salCmd < 0 || salCmd >= 500)
    {
        TRACE_E("CSalamanderGeneral::PostSalamanderCommand: salCmd is invalid (" << salCmd << " is not in range 0-499).");
        return;
    }

    if (MainThreadID == GetCurrentThreadId())
    { // because of calls from the entry point where Plugin is set to -1 (just to look up plugin data)
        // before WM_USER_POSTCMDORUNLOADPLUGIN would arrive, Plugin would be reset (according to the entry point's return value)
        CPluginData* data = Plugins.GetPluginData(Plugin);
        if (data != NULL)
        {
            data->Commands.Add(salCmd);
            ExecCmdsOrUnloadMarkedPlugins = TRUE;
        }
        else
        {
            TRACE_E("Unexpected situation in CSalamanderGeneral::PostSalamanderCommand().");
        }
    }
    else // outside the entry point the Plugin is certainly set...
    {
        if (MainWindow != NULL && MainWindow->HWindow != NULL)
        {
            // check for a call while the entry point is starting (Plugin is set to -1)
            if ((INT_PTR)Plugin == -1)
            {
                TRACE_E("You can call CSalamanderGeneral::PostSalamanderCommand only from main "
                        "thread when plugin entry-point is not finished yet!");
            }
            else
            { // 0 - unload, 1 - rebuild menu, 2-501 salCmd, 502-1000501 menuCmd
                PostMessage(MainWindow->HWindow, WM_USER_POSTCMDORUNLOADPLUGIN, (WPARAM)Plugin, 2 + salCmd);
            }
        }
        else
        {
            TRACE_E("Unexpected situation (2) in CSalamanderGeneral::PostSalamanderCommand().");
        }
    }
}

void CSalamanderGeneral::SetUserWorkedOnPanelPath(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetUserWorkedOnPanelPath(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetUserWorkedOnPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->UserWorkedOnThisPath = TRUE;
}

void CSalamanderGeneral::StoreSelectionOnPanelPath(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::StoreSelectionOnPanelPath(%d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StoreSelectionOnPanelPath() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->StoreSelection();
}

class CSalamanderMaskGroupImp : public CSalamanderMaskGroup
{
protected:
    CMaskGroup maskGroup;

public:
    CSalamanderMaskGroupImp() : maskGroup() {}

    virtual void WINAPI SetMasksString(const char* masks, BOOL extendedMode) { maskGroup.SetMasksString(masks, extendedMode); }
    virtual void WINAPI GetMasksString(char* buffer) { lstrcpyn(buffer, maskGroup.GetMasksString(), MAX_GROUPMASK); }
    virtual BOOL WINAPI GetExtendedMode() { return maskGroup.GetExtendedMode(); }
    virtual BOOL WINAPI PrepareMasks(int& errorPos) { return maskGroup.PrepareMasks(errorPos); }
    virtual BOOL WINAPI AgreeMasks(const char* fileName, const char* fileExt) { return maskGroup.AgreeMasks(fileName, fileExt); }
};

CSalamanderMaskGroup*
CSalamanderGeneral::AllocSalamanderMaskGroup()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderMaskGroup()");
    CSalamanderMaskGroup* ret = new CSalamanderMaskGroupImp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderMaskGroup(CSalamanderMaskGroup* maskGroup)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderMaskGroup()");
    if (maskGroup != NULL)
        delete ((CSalamanderMaskGroupImp*)maskGroup);
}

DWORD
CSalamanderGeneral::UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal)
{
    CALL_STACK_MESSAGE_NONE
    return ::UpdateCrc32(buffer, count, crcVal);
}

class CSalamanderMD5Imp : public CSalamanderMD5
{
protected:
    MD5 md5;

public:
    CSalamanderMD5Imp() : md5() {}

    virtual void WINAPI Init() { md5.init(); }
    virtual void WINAPI Update(const void* input, DWORD input_length) { md5.update((unsigned char*)input, input_length); }
    virtual void WINAPI Finalize() { md5.finalize(); }
    virtual void WINAPI GetDigest(void* dest) { memcpy(dest, md5.digest, 16); }
};

CSalamanderMD5*
CSalamanderGeneral::AllocSalamanderMD5()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AllocSalamanderMD5()");
    CSalamanderMD5Imp* ret = new CSalamanderMD5Imp;
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    return ret;
}

void CSalamanderGeneral::FreeSalamanderMD5(CSalamanderMD5* md5)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderMD5()");
    if (md5 != NULL)
        delete ((CSalamanderMD5Imp*)md5);
}

BOOL CSalamanderGeneral::LookForSubTexts(char* text, DWORD* varPlacements, int* varPlacementsCount)
{
    CALL_STACK_MESSAGE_NONE
    return ::LookForSubTexts(text, varPlacements, varPlacementsCount);
}

void CSalamanderGeneral::WaitForESCRelease()
{
    CALL_STACK_MESSAGE_NONE
    ::WaitForESCRelease();
}

DWORD
CSalamanderGeneral::GetMouseWheelScrollLines()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetMouseWheelScrollLines();
}

DWORD
CSalamanderGeneral::GetMouseWheelScrollChars()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetMouseWheelScrollChars();
}

HWND CSalamanderGeneral::GetTopVisibleParent(HWND hParent)
{
    CALL_STACK_MESSAGE_NONE
    return ::GetTopVisibleParent(hParent);
}

BOOL CSalamanderGeneral::MultiMonGetDefaultWindowPos(HWND hByWnd, POINT* p)
{
    CALL_STACK_MESSAGE_NONE
    return ::MultiMonGetDefaultWindowPos(hByWnd, p);
}

void CSalamanderGeneral::MultiMonGetClipRectByRect(const RECT* rect, RECT* workClipRect, RECT* monitorClipRect)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonGetClipRectByRect(rect, workClipRect, monitorClipRect);
}

void CSalamanderGeneral::MultiMonGetClipRectByWindow(HWND hByWnd, RECT* workClipRect, RECT* monitorClipRect)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonGetClipRectByWindow(hByWnd, workClipRect, monitorClipRect);
}

void CSalamanderGeneral::MultiMonCenterWindow(HWND hWindow, HWND hByWnd, BOOL findTopWindow)
{
    CALL_STACK_MESSAGE_NONE
    ::MultiMonCenterWindow(hWindow, hByWnd, findTopWindow);
}

BOOL CSalamanderGeneral::MultiMonEnsureRectVisible(RECT* rect, BOOL partialOK)
{
    CALL_STACK_MESSAGE_NONE
    return ::MultiMonEnsureRectVisible(rect, partialOK);
}

BOOL CSalamanderGeneral::InstallWordBreakProc(HWND hWindow)
{
    CALL_STACK_MESSAGE_NONE
    return ::InstallWordBreakProc(hWindow);
}

BOOL CSalamanderGeneral::IsFirstInstance3OrLater()
{
    CALL_STACK_MESSAGE_NONE
    return FirstInstance_3_or_later;
}

int CSalamanderGeneral::ExpandPluralString(char* buffer, int bufferSize, const char* format,
                                           int parametersCount, const CQuadWord* parametersArray)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::ExpandPluralString(, %d, %s, %d, )",
                        bufferSize, format, parametersCount);
    return ::ExpandPluralString(buffer, bufferSize, format, parametersCount, parametersArray);
}

int CSalamanderGeneral::ExpandPluralFilesDirs(char* buffer, int bufferSize, int files, int dirs,
                                              int mode, BOOL forDlgCaption)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::ExpandPluralFilesDirs(, %d, %d, %d, %d, %d)",
                        bufferSize, files, dirs, (int)mode, forDlgCaption);
    return ::ExpandPluralFilesDirs(buffer, bufferSize, files, dirs, mode, forDlgCaption);
}

int CSalamanderGeneral::ExpandPluralBytesFilesDirs(char* buffer, int bufferSize,
                                                   const CQuadWord& selectedBytes, int files, int dirs,
                                                   BOOL useSubTexts)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::FreeSalamanderMaskGroup(, %d, %g, %d, %d, %d)",
                        bufferSize, selectedBytes.GetDouble(), files, dirs, useSubTexts);
    return ::ExpandPluralBytesFilesDirs(buffer, bufferSize, selectedBytes, files, dirs, useSubTexts);
}

void CSalamanderGeneral::GetCommonFSOperSourceDescr(char* sourceDescr, int sourceDescrSize,
                                                    int panel, int selectedFiles, int selectedDirs,
                                                    const char* fileOrDirName, BOOL isDir,
                                                    BOOL forDlgCaption)
{
    CALL_STACK_MESSAGE8("CSalamanderGeneral::GetCommonFSOperSourceDescr(, %d, %d, %d, %d, %s, %d, %d)",
                        sourceDescrSize, panel, selectedFiles, selectedDirs, fileOrDirName,
                        isDir, forDlgCaption);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetCommonFSOperSourceDescr() only from main thread!");
        return;
    }
    if (sourceDescrSize <= 0)
        return;
    if (sourceDescr == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetCommonFSOperSourceDescr(): 'sourceDescr' may not be NULL!");
        return;
    }
    if (selectedFiles + selectedDirs <= 1 && panel == -1 && fileOrDirName == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetCommonFSOperSourceDescr(): 'fileOrDirName' may not be NULL!");
        sourceDescr[0] = 0;
        return;
    }
    if (selectedFiles + selectedDirs <= 1) // one selected item or the focus
    {
        BOOL nameIsDir;
        char* name;
        char nameBuf[MAX_PATH];
        if (panel != -1)
        {
            const CFileData* f;
            if (selectedFiles == 0 && selectedDirs == 0)
                f = GetPanelFocusedItem(panel, &nameIsDir);
            else
            {
                int index = 0;
                f = GetPanelSelectedItem(panel, &index, &nameIsDir);
            }
            if (f != NULL && f->Name != NULL)
                name = f->Name;
            else
            {
                TRACE_E("Unexpected situation in CSalamanderGeneral::GetCommonFSOperSourceDescr()!");
                sourceDescr[0] = 0;
                return;
            }
        }
        else
        {
            lstrcpyn(nameBuf, fileOrDirName, MAX_PATH);
            name = nameBuf;
            nameIsDir = isDir;
        }
        int fileNameFormat;
        GetConfigParameter(SALCFG_FILENAMEFORMAT, &fileNameFormat,
                           sizeof(fileNameFormat), NULL);
        char formatedFileName[MAX_PATH]; // CFileData::Name is at most MAX_PATH-5 characters long - Salamander's limit
        ::AlterFileName(formatedFileName, name, -1, fileNameFormat, 0, nameIsDir);
        _snprintf_s(sourceDescr, sourceDescrSize, _TRUNCATE,
                    ::LoadStr(nameIsDir ? (forDlgCaption ? IDS_DLG_QUESTION_DIRECTORY : IDS_QUESTION_DIRECTORY) : (forDlgCaption ? IDS_DLG_QUESTION_FILE : IDS_QUESTION_FILE)),
                    formatedFileName);
    }
    else // multiple directories and files
    {
        ExpandPluralFilesDirs(sourceDescr, sourceDescrSize, selectedFiles, selectedDirs,
                              epfdmNormal, forDlgCaption);
    }
}

void CSalamanderGeneral::AddStrToStr(char* dstStr, int dstBufSize, const char* srcStr)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::AddStrToStr(, ,)");
    if (dstBufSize < 2)
    {
        TRACE_E("CSalamanderGeneral::AddStrToStr(): dstBufSize must be greater or equal to 2");
        return;
    }
    ::AddStrToStr(dstStr, dstBufSize, srcStr);
}

BOOL CSalamanderGeneral::SalIsValidFileNameComponent(const char* fileNameComponent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalIsValidFileNameComponent()");
    return ::SalIsValidFileNameComponent(fileNameComponent);
}

void CSalamanderGeneral::SalMakeValidFileNameComponent(char* fileNameComponent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalMakeValidFileNameComponent()");
    ::SalMakeValidFileNameComponent(fileNameComponent);
}

BOOL CSalamanderGeneral::IsFileEnumSourcePanel(int srcUID, int* panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsFileEnumSourcePanel(%d,)", srcUID);
    return ::IsFileEnumSourcePanel(srcUID, panel);
}

BOOL CSalamanderGeneral::GetNextFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                  BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                  char* fileName, BOOL* noMoreFiles, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetNextFileNameForViewer(%d, , , %d, %d, %s, ,)",
                        srcUID, preferSelected, onlyAssociatedExtensions, fileName);
    if (fileName == NULL || lastFileIndex == NULL)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetNextFileNameForViewer(): invalid parameters (fileName == NULL || lastFileIndex == NULL)!");
        return FALSE;
    }
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetNextFileNameForViewer(): unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    return ::GetNextFileNameForViewer(srcUID, lastFileIndex, lastFileName, preferSelected,
                                      onlyAssociatedExtensions, fileName,
                                      noMoreFiles, srcBusy, Plugin);
}

BOOL CSalamanderGeneral::GetPreviousFileNameForViewer(int srcUID, int* lastFileIndex, const char* lastFileName,
                                                      BOOL preferSelected, BOOL onlyAssociatedExtensions,
                                                      char* fileName, BOOL* noMoreFiles, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetPreviousFileNameForViewer(%d, , , %d, %d, %s, ,)",
                        srcUID, preferSelected, onlyAssociatedExtensions, fileName);
    if (fileName == NULL || lastFileIndex == NULL)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetPreviousFileNameForViewer(): invalid parameters (fileName == NULL || lastFileIndex == NULL)!");
        return FALSE;
    }
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        if (noMoreFiles != NULL)
            *noMoreFiles = FALSE;
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::GetPreviousFileNameForViewer(): unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    return ::GetPreviousFileNameForViewer(srcUID, lastFileIndex, lastFileName, preferSelected,
                                          onlyAssociatedExtensions, fileName,
                                          noMoreFiles, srcBusy, Plugin);
}

BOOL CSalamanderGeneral::IsFileNameForViewerSelected(int srcUID, int lastFileIndex,
                                                     const char* lastFileName,
                                                     BOOL* isFileSelected, BOOL* srcBusy)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::IsFileNameForViewerSelected(%d, %d, , , ,)",
                        srcUID, lastFileIndex);
    if (isFileSelected == NULL)
    {
        if (srcBusy != NULL)
            *srcBusy = FALSE;
        TRACE_E("CSalamanderGeneral::IsFileNameForViewerSelected(): invalid parameters (isFileSelected == NULL)!");
        return FALSE;
    }
    return ::IsFileNameForViewerSelected(srcUID, lastFileIndex, lastFileName,
                                         isFileSelected, srcBusy);
}

BOOL CSalamanderGeneral::SetSelectionOnFileNameForViewer(int srcUID, int lastFileIndex,
                                                         const char* lastFileName, BOOL select,
                                                         BOOL* srcBusy)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::SetSelectionOnFileNameForViewer(%d, %d, , %d,)",
                        srcUID, lastFileIndex, select);
    return ::SetSelectionOnFileNameForViewer(srcUID, lastFileIndex, lastFileName, select, srcBusy);
}

BOOL CSalamanderGeneral::GetStdHistoryValues(int historyID, char*** historyArr, int* historyItemsCount)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetStdHistoryValues(%d, ,)", historyID);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetStdHistoryValues() only from main thread!");
        if (historyArr != NULL)
            *historyArr = NULL;
        if (historyItemsCount != NULL)
            *historyItemsCount = 0;
        return FALSE;
    }
    if (historyArr == NULL || historyItemsCount == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetStdHistoryValues(): invalid parameters!");
        return FALSE;
    }
    switch (historyID)
    {
    case SALHIST_COPYMOVETGT:
    {
        *historyArr = Configuration.CopyHistory;
        *historyItemsCount = COPY_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CREATEDIR:
    {
        *historyArr = Configuration.CreateDirHistory;
        *historyItemsCount = CREATEDIR_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CHANGEDIR:
    {
        *historyArr = Configuration.ChangeDirHistory;
        *historyItemsCount = CHANGEDIR_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_QUICKRENAME:
    {
        *historyArr = Configuration.QuickRenameHistory;
        *historyItemsCount = QUICKRENAME_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_EDITNEW:
    {
        *historyArr = Configuration.EditNewHistory;
        *historyItemsCount = EDITNEW_HISTORY_SIZE;
        return TRUE;
    }

    case SALHIST_CONVERT:
    {
        *historyArr = Configuration.ConvertHistory;
        *historyItemsCount = CONVERT_HISTORY_SIZE;
        return TRUE;
    }

    default:
    {
        *historyArr = NULL;
        *historyItemsCount = 0;
        return FALSE;
    }
    }
}

void CSalamanderGeneral::AddValueToStdHistoryValues(char** historyArr, int historyItemsCount,
                                                    const char* value, BOOL caseSensitiveValue)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::AddValueToStdHistoryValues(, %d, %s, %d)",
                        historyItemsCount, value, caseSensitiveValue);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::AddValueToStdHistoryValues() only from main thread!");
        return;
    }
    if (historyArr == NULL || value == NULL)
    {
        TRACE_E("CSalamanderGeneral::AddValueToStdHistoryValues(): 'historyArr' and 'value' may not be NULL!");
        return;
    }
    ::AddValueToStdHistoryValues(historyArr, historyItemsCount, value, caseSensitiveValue);
}

void CSalamanderGeneral::LoadComboFromStdHistoryValues(HWND combo, char** historyArr, int historyItemsCount)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::LoadComboFromStdHistoryValues(, , %d)",
                        historyItemsCount);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::LoadComboFromStdHistoryValues() only from main thread!");
        return;
    }
    if (historyArr == NULL)
    {
        TRACE_E("CSalamanderGeneral::LoadComboFromStdHistoryValues(): 'historyArr' may not be NULL!");
        return;
    }
    ::LoadComboFromStdHistoryValues(combo, historyArr, historyItemsCount);
}

BOOL CSalamanderGeneral::CanUse256ColorsBitmap()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::CanUse256ColorsBitmap()");
    return ::Use256ColorsBitmap();
}

HWND CSalamanderGeneral::GetWndToFlash(HWND parent)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetWndToFlash()");
    return ::GetWndToFlash(parent);
}

void ActivateDropTarget(HWND dropTarget, HWND progressWnd)
{
    if (dropTarget != NULL)
    { // this dirty hack removes the activated state without an active application (visually inactive, but WM_ACTIVATEAPP with "activate" already arrived)
        HWND tgtWnd = dropTarget;
        HWND tmp;
        while ((tmp = ::GetParent(tgtWnd)) != NULL && IsWindowEnabled(tmp))
            tgtWnd = tmp;
        if (MainWindow != NULL && tgtWnd != MainWindow->HWindow)
        { // perform it only if it is not an operation inside our Salamander
            SetForegroundWindow(progressWnd);
            SetForegroundWindow(tgtWnd);
            //        TRACE_I("SetForegroundWindow: " << hex << tgtWnd);
        }
    }
}

void CSalamanderGeneral::ActivateDropTarget(HWND dropTarget, HWND progressWnd)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ActivateDropTarget(,)");
    ::ActivateDropTarget(dropTarget, progressWnd);
}

void CSalamanderGeneral::PostOpenPackDlgForThisPlugin(int delFilesAfterPacking)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostOpenPackDlgForThisPlugin(%d)", delFilesAfterPacking);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PostOpenPackDlgForThisPlugin() only from main thread!");
        return;
    }

    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->OpenPackDlg = TRUE;
        data->PackDlgDelFilesAfterPacking = delFilesAfterPacking;
        OpenPackOrUnpackDlgForMarkedPlugins = TRUE;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::PostOpenPackDlgForThisPlugin().");
    }
}

void CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin(const char* unpackMask)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin(%s)", unpackMask);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin() only from main thread!");
        return;
    }

    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->OpenUnpackDlg = TRUE;
        if (data->UnpackDlgUnpackMask != NULL)
            free(data->UnpackDlgUnpackMask);
        if (unpackMask != NULL)
            data->UnpackDlgUnpackMask = DupStr(unpackMask);
        else
            data->UnpackDlgUnpackMask = NULL;
        OpenPackOrUnpackDlgForMarkedPlugins = TRUE;
    }
    else
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::PostOpenUnpackDlgForThisPlugin().");
    }
}

HANDLE
CSalamanderGeneral::SalCreateFileEx(const char* fileName, DWORD desiredAccess, DWORD shareMode,
                                    DWORD flagsAndAttributes, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalCreateFileEx()");
    HANDLE ret = ::SalCreateFileEx(fileName, desiredAccess, shareMode, flagsAndAttributes, NULL);
    if (err != NULL)
        *err = GetLastError();
    return ret;
}

BOOL CSalamanderGeneral::SalCreateDirectoryEx(const char* name, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalCreateDirectoryEx()");
    return ::SalCreateDirectoryEx(name, err);
}

void CSalamanderGeneral::PanelStopMonitoring(int panel, BOOL stopMonitoring)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::PanelStopMonitoring(%d, %d)", panel, stopMonitoring);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::PanelStopMonitoring() only from main thread!");
        return;
    }
    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
        p->HandsOff(stopMonitoring);
}

CSalamanderDirectoryAbstract*
CSalamanderGeneral::AllocSalamanderDirectory(BOOL isForFS)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::AllocSalamanderDirectory(%d)", isForFS);
    CSalamanderDirectory* ret = new CSalamanderDirectory(isForFS);
    if (ret == NULL)
        TRACE_E(LOW_MEMORY);
    else
        ret->AllocAddCache();
    return ret;
}

void CSalamanderGeneral::FreeSalamanderDirectory(CSalamanderDirectoryAbstract* salDir)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::FreeSalamanderDirectory()");
    if (salDir != NULL)
        delete ((CSalamanderDirectory*)salDir);
}

BOOL CSalamanderGeneral::AddPluginFSTimer(int timeout, CPluginFSInterfaceAbstract* timerOwner,
                                          DWORD timerParam) // FIXME_X64 - review the Salamander interface to ensure we do not pass parameters that should hold x64 pointers; for example here 'timerParam'
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::AddPluginFSTimer(%d, , 0x%X)", timeout, timerParam);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::AddPluginFSTimer() only from main thread!");
        return FALSE;
    }
    if (timeout < 0)
    {
        TRACE_E("CSalamanderGeneral::AddPluginFSTimer(): invalid timeout value (" << timeout << ")!");
        return FALSE;
    }
    if (timerOwner == NULL)
    {
        TRACE_E("CSalamanderGeneral::AddPluginFSTimer(): invalid timerOwner (NULL)!");
        return FALSE;
    }
    return Plugins.AddPluginFSTimer(timeout, timerOwner, timerParam);
}

int CSalamanderGeneral::KillPluginFSTimer(CPluginFSInterfaceAbstract* timerOwner, BOOL allTimers,
                                          DWORD timerParam)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::KillPluginFSTimer(, %d, 0x%X)", allTimers, timerParam);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::KillPluginFSTimer() only from main thread!");
        return 0;
    }
    if (timerOwner == NULL)
    {
        TRACE_E("CSalamanderGeneral::KillPluginFSTimer(): invalid timer owner (NULL)!");
        return 0;
    }
    return Plugins.KillPluginFSTimer(timerOwner, allTimers, timerParam);
}

BOOL CSalamanderGeneral::GetChangeDriveMenuItemVisibility()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetChangeDriveMenuItemVisibility()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetChangeDriveMenuItemVisibility() only from main thread!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        return data->ChDrvMenuFSItemVisible;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetChangeDriveMenuItemVisibility().");
    return FALSE;
}

void CSalamanderGeneral::SetChangeDriveMenuItemVisibility(BOOL visible)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetChangeDriveMenuItemVisibility(%d)", visible);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::SetChangeDriveMenuItemVisibility() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->ChDrvMenuFSItemVisible = (visible != FALSE);
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetChangeDriveMenuItemVisibility().");
}

void CSalamanderGeneral::OleSpySetBreak(int alloc)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::OleSpySetBreak(%d)", alloc);
    ::OleSpySetBreak(alloc);
}

HICON
CSalamanderGeneral::GetSalamanderIcon(int icon, int iconSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::GetSalamanderIcon(%d, %d)", icon, iconSize);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSalamanderIcon() only from main thread!");
        return NULL;
    }

    CSymbolsImageListIndexes iconIndex;
    switch (icon)
    {
    case SALICON_EXECUTABLE:
        iconIndex = symbolsExecutable;
        break;
    case SALICON_DIRECTORY:
        iconIndex = symbolsDirectory;
        break;
    case SALICON_NONASSOCIATED:
        iconIndex = symbolsNonAssociated;
        break;
    case SALICON_ASSOCIATED:
        iconIndex = symbolsAssociated;
        break;
    case SALICON_UPDIR:
        iconIndex = symbolsUpDir;
        break;
    case SALICON_ARCHIVE:
        iconIndex = symbolsArchive;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: invalid icon=" << icon << " forcing SALICON_NONASSOCIATED");
        iconIndex = symbolsNonAssociated;
    }
    }

    CIconSizeEnum salIconSize;
    switch (iconSize)
    {
    case SALICONSIZE_16:
        salIconSize = ICONSIZE_16;
        break;
    case SALICONSIZE_32:
        salIconSize = ICONSIZE_32;
        break;
    case SALICONSIZE_48:
        salIconSize = ICONSIZE_48;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: invalid iconSize=" << iconSize << " forcing SALICONSIZE_16");
        salIconSize = ICONSIZE_16;
    }
    }

    CIconList* list = SimpleIconLists[salIconSize];
    if (list == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetSalamanderIcon: list == NULL");
        return NULL;
    }

    return list->GetIcon(iconIndex, FALSE);
}

BOOL CSalamanderGeneral::GetFileIcon(const char* path, BOOL pathIsPIDL, HICON* hIcon, int iconSize,
                                     BOOL fallbackToDefIcon, BOOL defIconIsDir)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::GetFileIcon(, %d, , %d, %d, %d)",
                        pathIsPIDL, iconSize, fallbackToDefIcon, defIconIsDir);
    CIconSizeEnum salIconSize;
    switch (iconSize)
    {
    case SALICONSIZE_16:
        salIconSize = ICONSIZE_16;
        break;
    case SALICONSIZE_32:
        salIconSize = ICONSIZE_32;
        break;
    case SALICONSIZE_48:
        salIconSize = ICONSIZE_48;
        break;
    default:
    {
        TRACE_E("CSalamanderGeneral::GetFileIcon: invalid iconSize=" << iconSize << " forcing SALICONSIZE_16");
        salIconSize = ICONSIZE_16;
    }
    }
    return ::GetFileIcon(path, pathIsPIDL, hIcon, salIconSize, fallbackToDefIcon, defIconIsDir);
}

class CSalamanderPNG : public CSalamanderPNGAbstract
{
public:
    virtual HBITMAP WINAPI LoadPNGBitmap(HINSTANCE hInstance, LPCTSTR lpBitmapName, DWORD flags, COLORREF unused)
    {
        HBITMAP hBitmap = ::LoadPNGBitmap(hInstance, lpBitmapName, flags);
        if (hBitmap != NULL) // the handle is handed over to the plug-in; the plug-in is responsible for destroying it, remove it from Salamander HANDLES
            HANDLES_REMOVE(hBitmap, __htHandle_comp_with_DeleteObject, "DeleteObject");
        return hBitmap;
    }

    virtual HBITMAP WINAPI LoadRawPNGBitmap(const void* rawPNG, DWORD rawPNGSize, DWORD flags, COLORREF unused)
    {
        HBITMAP hBitmap = ::LoadRawPNGBitmap(rawPNG, rawPNGSize, flags);
        if (hBitmap != NULL) // the handle is handed over to the plug-in; the plug-in is responsible for destroying it, remove it from Salamander HANDLES
            HANDLES_REMOVE(hBitmap, __htHandle_comp_with_DeleteObject, "DeleteObject");
        return hBitmap;
    }
};

CSalamanderPNG SalamanderPNG;

CSalamanderPNGAbstract*
CSalamanderGeneral::GetSalamanderPNG()
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderPNG;
}

CSalamanderPasswordManagerAbstract*
CSalamanderGeneral::GetSalamanderPasswordManager()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetSalamanderPasswordManager()");
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetSalamanderPasswordManager() only from main thread!");
        return NULL;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        return &data->SalamanderPasswordManager;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetSalamanderPasswordManager().");
    return NULL;
}

class CSalamanderCrypt : public CSalamanderCryptAbstract
{
public:
    /* AES functions */
    virtual int WINAPI AESInit(CSalAES* aes,
                               int mode,       /* Mode (key size) to be used (input) */
                               LPCSTR pwd,     /* User specified password (input)    */
                               size_t pwd_len, /* Password length (input)            */
                               LPBYTE salt,    /* Salt (input)                       */
                               LPWORD pwd_ver) /* 2 byte password verifier (output)  */
    {
        _ASSERT(sizeof(aes->nonce) == sizeof(((fcrypt_ctx*)aes)->nonce));
        _ASSERT(sizeof(aes->encr_bfr) == sizeof(((fcrypt_ctx*)aes)->encr_bfr));
        _ASSERT(sizeof(aes->encr_ctx) == sizeof(((fcrypt_ctx*)aes)->encr_ctx));
        _ASSERT(sizeof(aes->auth_ctx) == sizeof(((fcrypt_ctx*)aes)->auth_ctx));
        _ASSERT(sizeof(aes->nonce) == sizeof(((fcrypt_ctx*)aes)->nonce));
        _ASSERT(sizeof(CSalAES) == sizeof(fcrypt_ctx));
        return fcrypt_init(mode, (unsigned char*)pwd, (unsigned int)pwd_len, salt, (unsigned char*)pwd_ver, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESEncrypt(CSalAES* aes, LPVOID data, size_t dataLen)
    {
        fcrypt_encrypt((unsigned char*)data, (unsigned int)dataLen, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESDecrypt(CSalAES* aes, LPVOID data, size_t dataLen)
    {
        fcrypt_decrypt((unsigned char*)data, (unsigned int)dataLen, (fcrypt_ctx*)aes);
    }

    virtual void WINAPI AESEnd(CSalAES* aes, LPBYTE mac, LPDWORD pMacLen)
    {
        if (pMacLen)
            *pMacLen = SAL_AES_MAC_LENGTH(aes->mode);
        fcrypt_end(mac, (fcrypt_ctx*)aes);
    }

    /* SHA1 functions */
    virtual void WINAPI SHA1Init(CSalSHA1* sha1)
    {
        _ASSERT(sizeof(CSalSHA1) == sizeof(SHA1_Context));
        ::SHA1Init((SHA1_CTX*)sha1);
    }

    virtual void WINAPI SHA1Update(CSalSHA1* sha1, const LPBYTE data, size_t dataLen)
    {
        ::SHA1Update((SHA1_CTX*)sha1, data, (unsigned int)dataLen);
    }

    virtual void WINAPI SHA1Final(CSalSHA1* sha1, BYTE digest[20])
    {
        ::SHA1Final(digest, (SHA1_CTX*)sha1);
    }
};

CSalamanderCrypt SalamanderCrypt;

CSalamanderCryptAbstract* GetSalamanderCrypt() // for Salamander's internal use
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderCrypt;
}

CSalamanderCryptAbstract*
CSalamanderGeneral::GetSalamanderCrypt()
{
    CALL_STACK_MESSAGE_NONE
    return &SalamanderCrypt;
}

BOOL CSalamanderGeneral::FileExists(const char* fileName)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::FileExists(%s)", fileName);
    return ::FileExists(fileName);
}

void CSalamanderGeneral::DisconnectFSFromPanel(HWND parent, int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::DisconnectFSFromPanel(, %d)", panel);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::DisconnectFSFromPanel() only from main thread!");
        return;
    }
    char buf[MAX_PATH];
    BOOL rescueOrFixed = TRUE;
    if (GetLastWindowsPanelPath(panel, buf, MAX_PATH))
    { // change the path to the last visited Windows path
        BOOL tryNet = FALSE;
        DWORD err;
        DWORD lastErr;
        BOOL pathInvalid;
        BOOL cut;
        if (::SalCheckAndRestorePathWithCut(parent, buf, tryNet, err, lastErr,
                                            pathInvalid, cut, TRUE))
        {
            int failReason;
            if (ChangePanelPathToDisk(panel, buf, &failReason) ||
                failReason != CHPPFR_INVALIDPATH) // except for the "bad path" error (closing the FS was refused, etc.)
            {
                rescueOrFixed = FALSE;
            }
        }
    }
    if (rescueOrFixed)
        ChangePanelPathToRescuePathOrFixedDrive(panel); // "always false"
}

BOOL CSalamanderGeneral::IsArchiveHandledByThisPlugin(const char* name)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::IsArchiveHandledByThisPlugin(%s)", name);
    if (Plugin == NULL || (INT_PTR)Plugin == -1)
    {
        TRACE_E("CSalamanderGeneral::IsArchiveHandledByThisPlugin() unexpected call, plugin is not initialized yet!");
        return FALSE;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data == NULL)
    {
        TRACE_E("Unexpected situation in CSalamanderGeneral::IsArchiveHandledByThisPlugin()");
        return FALSE;
    }
    if (!data->SupportPanelView)
        return FALSE;

    int format = PackerFormatConfig.PackIsArchive(name);
    if (format != 0) // found a supported archive
    {
        format--;
        int index = PackerFormatConfig.GetUnpackerIndex(format);
        if (index < 0) // view: is it internal processing (plug-in)?
        {
            CPluginData* foundData = Plugins.Get(-index - 1);
            if (foundData == data) // is it us?
                return TRUE;
        }
    }
    return FALSE;
}

DWORD
CSalamanderGeneral::GetIconLRFlags()
{
    return IconLRFlags;
}

int CSalamanderGeneral::IsFileLink(const char* fileExtension)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("CSalamanderGeneral::IsFileLink()");
    if (fileExtension == NULL)
        return 0;
    return ::IsFileLink(fileExtension);
}

DWORD
CSalamanderGeneral::GetImageListColorFlags()
{
    CALL_STACK_MESSAGE_NONE
    return ::GetImageListColorFlags();
}

void CSalamanderGeneral::SetHelpFileName(const char* chmName)
{
    CALL_STACK_MESSAGE_NONE
    if (chmName == NULL || *chmName == 0)
        TRACE_E("CSalamanderGeneral::SetHelpFileName(): invalid parameter 'chmName'.");
    else
        lstrcpyn(HelpFileName, chmName, MAX_PATH);
}

BOOL CSalamanderGeneral::OpenHtmlHelp(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::OpenHtmlHelp(, %d, %Iu, %d)", command, dwData, quiet);
    if (HelpFileName[0] == 0)
    {
        TRACE_E("CSalamanderGeneral::OpenHtmlHelp(): plugin must call CSalamanderGeneral::SetHelpFileName() first!");
        return FALSE;
    }
    else
    {
        return ::OpenHtmlHelp(HelpFileName, parent, command, dwData, quiet);
    }
}

BOOL CSalamanderGeneral::OpenHtmlHelpForSalamander(HWND parent, CHtmlHelpCommand command, DWORD_PTR dwData, BOOL quiet)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::OpenHtmlHelpForSalamander(, %d, %Iu, %d)", command, dwData, quiet);
    DWORD_PTR newData = dwData;
    if (command == HHCDisplayContext)
    {
        switch (dwData)
        {
        case HTMLHELP_SALID_PWDMANAGER:
            newData = IDH_PWDMANAGER;
            break; // password manager help
        default:
        {
            TRACE_E("CSalamanderGeneral::OpenHtmlHelpForSalamander(): invalid dwData parameter, see allowed HTMLHELP_SALID_XXX constants.");
            return FALSE;
        }
        }
    }
    return ::OpenHtmlHelp(NULL, parent, command, newData, quiet);
}

BOOL CSalamanderGeneral::PathsAreOnTheSameVolume(const char* path1, const char* path2,
                                                 BOOL* resIsOnlyEstimation)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PathsAreOnTheSameVolume(, ,)");
    return ::PathsAreOnTheSameVolume(path1, path2, resIsOnlyEstimation);
}

BOOL CSalamanderGeneral::SafeGetOpenFileName(LPOPENFILENAME lpofn)
{
    CALL_STACK_MESSAGE_NONE
    return ::SafeGetOpenFileName(lpofn);
}

BOOL CSalamanderGeneral::SafeGetSaveFileName(LPOPENFILENAME lpofn)
{
    CALL_STACK_MESSAGE_NONE
    return ::SafeGetSaveFileName(lpofn);
}

void CSalamanderGeneral::SetPluginIsNethood()
{
    CALL_STACK_MESSAGE_NONE
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin != -1)
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginIsNethood() only from entry-point!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->PluginIsNethood = TRUE;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginIsNethood().");
}

void CSalamanderGeneral::SetPluginUsesPasswordManager()
{
    CALL_STACK_MESSAGE_NONE
    if (MainThreadID != GetCurrentThreadId() || (INT_PTR)Plugin != -1)
    {
        TRACE_E("You can call CSalamanderGeneral::SetPluginUsesPasswordManager() only from entry-point!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        data->PluginUsesPasswordManager = TRUE;
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginUsesPasswordManager().");
}

void CSalamanderGeneral::OpenNetworkContextMenu(HWND parent, int panel, BOOL forItems, int menuX,
                                                int menuY, const char* netPath, char* newlyMappedDrive)
{
    CALL_STACK_MESSAGE6("CSalamanderGeneral::OpenNetworkContextMenu(, %d, %d, %d, %d, %s,)",
                        panel, forItems, menuX, menuY, netPath);

    if (newlyMappedDrive != NULL)
        *newlyMappedDrive = 0;

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::OpenNetworkContextMenu() only from main thread!");
        return;
    }

    if (netPath == NULL || netPath[0] != '\\' || netPath[1] != '\\' || strchr(netPath + 2, '\\') != NULL)
    {
        TRACE_E("CSalamanderGeneral::OpenNetworkContextMenu(): invalid netPath: " << (netPath != NULL ? netPath : "(null)"));
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        BeginStopRefresh(); // no refreshes needed (formality: the call comes from a plug-in, so refreshes are already disabled by EnterPlugin)

        int* indexes = NULL;
        int index = 0;
        int count = 0;
        if (forItems)
        {
            BOOL subDir;
            if (p->Dirs->Count > 0)
                subDir = (strcmp(p->Dirs->At(0).Name, "..") == 0);
            else
                subDir = FALSE;

            count = p->GetSelCount();
            if (count != 0)
            {
                indexes = new int[count];
                p->GetSelItems(count, indexes, TRUE); // we stepped back from this (see GetSelItems): for context menus we start from the focused item and end with the item before the focus (there is an intermediate wrap to the start of the name list) (the system does the same, e.g., Add To Windows Media Player List on MP3 files)
            }
            else
            {
                index = p->GetCaretIndex();
                if (subDir && index == 0)
                {
                    EndStopRefresh();
                    return;
                }
            }
        }
        else
            index = -1;

        if (p->ContextMenu != NULL)
        {
            TRACE_E("CSalamanderGeneral::OpenNetworkContextMenu: p->ContextMenu must be NULL (probably forbidden recursive call)!");
        }
        else
        {
            if (forItems)
            {
                CTmpEnumData data;
                data.Indexes = (count == 0) ? &index : indexes;
                data.Panel = p;
                p->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, netPath, (count == 0) ? 1 : count,
                                                     EnumFileNames, &data);
            }
            else
            {
                p->ContextMenu = CreateIContextMenu2(MainWindow->HWindow, netPath);
            }

            HMENU h = CreatePopupMenu();
            if (p->ContextMenu != NULL && h != NULL)
            {
                UINT flags = CMF_NORMAL | CMF_EXPLORE;
                // handle the pressed Shift key - extended context menu; under W2K it contains, for example, Run as...
#define CMF_EXTENDEDVERBS 0x00000100 // rarely used verbs
                BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (shiftPressed)
                    flags |= CMF_EXTENDEDVERBS;

                ShellActionAux5(flags, p, h);
                RemoveUselessSeparatorsFromMenu(h);

                int cmd = 0;
                if (GetMenuItemCount(h) > 0) // guard against a completely trimmed menu
                {
                    CMenuPopup contextPopup;
                    contextPopup.SetTemplateMenu(h);
                    cmd = contextPopup.Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON,
                                             menuX, menuY, parent, NULL);
                }
                if (cmd != 0)
                {
                    CALL_STACK_MESSAGE1("CSalamanderGeneral::OpenNetworkContextMenu::exec");

                    char cmdName[2000]; // deliberately 2000 instead of 200; shell extensions sometimes write double (considering: Unicode = 2 * "number of characters"), etc.
                    if (AuxGetCommandString(p->ContextMenu, cmd, GCS_VERB, NULL, cmdName, 200) != NOERROR)
                        cmdName[0] = 0;

                    // the Map Network Drive command is 40 on XP, 43 on W2K, and only under Vista has a defined cmdName
                    if ((stricmp(cmdName, "connectNetworkDrive") == 0 ||
                         !WindowsVistaAndLater && cmd == 40) &&
                        forItems && netPath[2] != 0)
                    {
                        char root[MAX_PATH];
                        strcpy(root, netPath);
                        int focus = p->GetCaretIndex();
                        if (SalPathAppend(root, (focus < p->Dirs->Count ? p->Dirs->At(focus) : p->Files->At(focus - p->Dirs->Count)).Name, MAX_PATH))
                        {
                            char newDrive = 0;
                            p->ConnectNet(TRUE, root, FALSE /* called from a plug-in; must not change the panel path, otherwise
                                                we would return to a deallocated FS object */
                                          ,
                                          &newDrive);
                            if (newlyMappedDrive != NULL)
                                *newlyMappedDrive = newDrive;
                        }
                    }
                    else
                    {
                        CShellExecuteWnd shellExecuteWnd;
                        CMINVOKECOMMANDINFOEX ici;
                        ZeroMemory(&ici, sizeof(CMINVOKECOMMANDINFOEX));
                        ici.cbSize = sizeof(CMINVOKECOMMANDINFOEX);
                        ici.fMask = CMIC_MASK_PTINVOKE;
                        if (CanUseShellExecuteWndAsParent(cmdName))
                            ici.hwnd = shellExecuteWnd.Create(MainWindow->HWindow, "SEW: CSalamanderGeneral::OpenNetworkContextMenu cmd=%d", cmd);
                        else
                            ici.hwnd = MainWindow->HWindow;
                        ici.lpVerb = MAKEINTRESOURCE(cmd);
                        ici.nShow = SW_SHOWNORMAL;
                        ici.ptInvoke.x = menuX;
                        ici.ptInvoke.y = menuY;

                        AuxInvokeCommand(p, (CMINVOKECOMMANDINFO*)&ici);

                        IdleRefreshStates = TRUE;  // during the next Idle force checking the status variables
                        IdleCheckClipboard = TRUE; // also request checking the clipboard
                    }
                }
            }
            {
                CALL_STACK_MESSAGE1("CSalamanderGeneral::OpenNetworkContextMenu::release");
                ShellActionAux6(p);
                if (h != NULL)
                    DestroyMenu(h);
            }
        }

        if (count != 0)
            delete[] (indexes);

        EndStopRefresh();
    }
}

BOOL CSalamanderGeneral::DuplicateBackslashes(char* buffer, int bufferSize)
{
    CALL_STACK_MESSAGE3("CSalamanderGeneral::DuplicateBackslashes(%s, %d)", buffer, bufferSize);
    return ::DuplicateBackslashes(buffer, bufferSize);
}

int CSalamanderGeneral::StartThrobber(int panel, const char* tooltip, int delay)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::StartThrobber(%d, %s, %d)", panel, tooltip, delay);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StartThrobber() only from main thread!");
        return -1;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->DirectoryLine != NULL)
    {
        p->DirectoryLine->SetThrobber(TRUE, delay);
        p->DirectoryLine->SetThrobberTooltip(tooltip);
        return p->DirectoryLine->ChangeThrobberID();
    }
    return -1;
}

BOOL CSalamanderGeneral::StopThrobber(int id)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::StopThrobber(%d)", id);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::StopThrobber() only from main thread!");
        return FALSE;
    }

    if (MainWindow->LeftPanel->DirectoryLine != NULL &&
        MainWindow->LeftPanel->DirectoryLine->IsThrobberVisible(id))
    {
        MainWindow->LeftPanel->DirectoryLine->SetThrobber(FALSE);
        return TRUE;
    }
    if (MainWindow->RightPanel->DirectoryLine != NULL &&
        MainWindow->RightPanel->DirectoryLine->IsThrobberVisible(id))
    {
        MainWindow->RightPanel->DirectoryLine->SetThrobber(FALSE);
        return TRUE;
    }
    return FALSE;
}

void CSalamanderGeneral::ShowSecurityIcon(int panel, BOOL showIcon, BOOL isLocked,
                                          const char* tooltip)
{
    CALL_STACK_MESSAGE5("CSalamanderGeneral::ShowSecurityIcon(%d, %d, %d, %s)",
                        panel, showIcon, isLocked, tooltip);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::ShowSecurityIcon() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL && p->DirectoryLine != NULL)
    {
        p->DirectoryLine->SetSecurity(showIcon ? (isLocked ? sisSecured : sisUnsecured) : sisNone);
        p->DirectoryLine->SetSecurityTooltip(tooltip);
    }
}

void CSalamanderGeneral::RemoveCurrentPathFromHistory(int panel)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::RemoveCurrentPathFromHistory(%d)", panel);

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::RemoveCurrentPathFromHistory() only from main thread!");
        return;
    }

    CFilesWindow* p = GetPanel(panel);
    if (p != NULL)
    {
        p->RemoveCurrentPathFromHistory();
        if (MainWindow != NULL)
            MainWindow->DirHistoryRemoveActualPath(p);
        p->UserWorkedOnThisPath = FALSE;
    }
}

BOOL CSalamanderGeneral::IsUserAdmin()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsUserAdmin()");
    return ::IsUserAdmin();
}

BOOL CSalamanderGeneral::IsRemoteSession()
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsRemoteSession()");
    return ::IsRemoteSession();
}

DWORD
CSalamanderGeneral::SalWNetAddConnection2Interactive(LPNETRESOURCE lpNetResource)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalWNetAddConnection2Interactive()");
    DWORD err;
    RestoreNetworkConnection(NULL, NULL, NULL, &err, lpNetResource);
    return err;
}

void CSalamanderGeneral::GetFocusedItemMenuPos(POINT* pos)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetFocusedItemMenuPos()");

    if (pos == NULL)
    {
        TRACE_E("CSalamanderGeneral::GetFocusedItemMenuPos(): invalid 'pos' (NULL)!");
        return;
    }

    pos->x = 0;
    pos->y = 0;

    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetFocusedItemMenuPos() only from main thread!");
        return;
    }

    if (MainWindow != NULL)
    {
        CFilesWindow* activePanel = MainWindow->GetActivePanel();
        if (activePanel != NULL)
        {
            activePanel->GetContextMenuPos(pos);
            return;
        }
    }

    pos->x = 0;
    pos->y = 0;
}

void CSalamanderGeneral::LockMainWindow(BOOL lock, HWND hToolWnd, const char* lockReason)
{
    CALL_STACK_MESSAGE4("CSalamanderGeneral::LockMainWindow(%d, 0x%p, %s)", lock, hToolWnd, lockReason);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::LockMainWindow() only from main thread!");
        return;
    }
    if (MainWindow != NULL)
        MainWindow->LockUI(lock, hToolWnd, lockReason);
}

BOOL CSalamanderGeneral::GetMenuItemHotKey(int id, WORD* hotKey, char* hotKeyText, int hotKeyTextSize)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetMenuItemHotKey(%d, , , )", id);
    if (MainThreadID != GetCurrentThreadId())
    {
        TRACE_E("You can call CSalamanderGeneral::GetMenuItemHotKey() only from main thread!");
        return FALSE;
    }
    BOOL ret = FALSE;
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
        ret = data->GetMenuItemHotKey(id, hotKey, hotKeyText, hotKeyTextSize);
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::GetMenuItemHotKey().");
    return ret;
}

LONG CSalamanderGeneral::SalRegQueryValue(HKEY hKey, LPCSTR lpSubKey, LPSTR lpData, PLONG lpcbData)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalRegQueryValue(, , ,)");
    return ::SalRegQueryValue(hKey, lpSubKey, lpData, lpcbData);
}

LONG CSalamanderGeneral::SalRegQueryValueEx(HKEY hKey, LPCSTR lpValueName, LPDWORD lpReserved,
                                            LPDWORD lpType, LPBYTE lpData, LPDWORD lpcbData)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalRegQueryValueEx(, , , , ,)");
    return ::SalRegQueryValueEx(hKey, lpValueName, lpReserved, lpType, lpData, lpcbData);
}

DWORD
CSalamanderGeneral::SalGetFileAttributes(const char* fileName)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileAttributes()");
    return ::SalGetFileAttributes(fileName);
}

BOOL CSalamanderGeneral::IsPathOnSSD(const char* path)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsPathOnSSD()");
    return ::IsPathOnSSD(path);
}

BOOL CSalamanderGeneral::IsUNCPath(const char* path)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::IsUNCPath()");
    return ::IsUNCPath(path);
}

BOOL CSalamanderGeneral::ResolveSubsts(char* resPath)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ResolveSubsts()");
    return ::ResolveSubsts(resPath);
}

void CSalamanderGeneral::ResolveLocalPathWithReparsePoints(char* resPath, const char* path, BOOL* cutResPathIsPossible,
                                                           BOOL* rootOrCurReparsePointSet, char* rootOrCurReparsePoint,
                                                           char* junctionOrSymlinkTgt, int* linkType, char* netPath)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ResolveLocalPathWithReparsePoints()");
    ::ResolveLocalPathWithReparsePoints(resPath, path, cutResPathIsPossible,
                                        rootOrCurReparsePointSet, rootOrCurReparsePoint,
                                        junctionOrSymlinkTgt, linkType, netPath);
}

BOOL CSalamanderGeneral::GetResolvedPathMountPointAndGUID(const char* path, char* mountPoint, char* guidPath)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::GetResolvedPathMountPointAndGUID(%s, ,)", path);
    return ::GetResolvedPathMountPointAndGUID(path, mountPoint, guidPath);
}

BOOL CSalamanderGeneral::PointToLocalDecimalSeparator(char* buffer, int bufferSize)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::PointToLocalDecimalSeparator()");
    return ::PointToLocalDecimalSeparator(buffer, bufferSize);
}

void CSalamanderGeneral::SetPluginIconOverlays(int iconOverlaysCount, HICON* iconOverlays)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::SetPluginIconOverlays(%d,)", iconOverlaysCount);
    if (MainThreadID != GetCurrentThreadId())
    { // this is a call error; we do not release the icons, not interested...
        TRACE_E("You can call CSalamanderGeneral::SetPluginIconOverlays() only from main thread!");
        return;
    }
    CPluginData* data = Plugins.GetPluginData(Plugin);
    if (data != NULL)
    {
        data->ReleaseIconOverlays();
        if (iconOverlaysCount > 0)
        {
            if (iconOverlays != NULL)
            {
                data->IconOverlays = (HICON*)malloc(3 * iconOverlaysCount * sizeof(HICON));
                memcpy(data->IconOverlays, iconOverlays, 3 * iconOverlaysCount * sizeof(HICON));
                data->IconOverlaysCount = iconOverlaysCount;

                BOOL err = FALSE;
                for (int i = 0; i < 3 * iconOverlaysCount; i++)
                {
                    if (data->IconOverlays[i] == NULL)
                    {
                        if (!err)
                            TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlays' (contains at least one NULL instead of icon handle)!");
                        err = TRUE;
                    }
                    else
                        HANDLES_ADD(__htIcon, __hoLoadImage, data->IconOverlays[i]);
                }
                if (err)
                    data->ReleaseIconOverlays();
            }
            else
                TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlays' (NULL)!");
        }
        else
        {
            if (iconOverlaysCount < 0)
                TRACE_E("CSalamanderGeneral::SetPluginIconOverlays(): invalid 'iconOverlaysCount' (negative value)!");
        }
    }
    else
        TRACE_E("Unexpected situation in CSalamanderGeneral::SetPluginIconOverlays().");
}

BOOL CSalamanderGeneral::SalGetFileSize2(const char* fileName, CQuadWord& size, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::SalGetFileSize2()");
    return ::SalGetFileSize2(fileName, size, err);
}

BOOL CSalamanderGeneral::GetLinkTgtFileSize(HWND parent, const char* fileName, CQuadWord* size,
                                            BOOL* cancel, BOOL* ignoreAll)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::GetLinkTgtFileSize()");
    return ::GetLinkTgtFileSize(parent, fileName, NULL, size, cancel, ignoreAll);
}

BOOL CSalamanderGeneral::DeleteDirLink(const char* name, DWORD* err)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::DeleteDirLink()");
    return ::DeleteDirLink(name, err);
}

BOOL CSalamanderGeneral::ClearReadOnlyAttr(const char* name, DWORD attr)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::ClearReadOnlyAttr()");
    return ::ClearReadOnlyAttr(name, attr);
}

BOOL CSalamanderGeneral::IsCriticalShutdown()
{
    return CriticalShutdown;
}

void CSalamanderGeneral::CloseAllOwnedEnabledDialogs(HWND parent, DWORD tid)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::CloseAllOwnedEnabledDialogs(, %d)", tid);
    ::CloseAllOwnedEnabledDialogs(parent, tid);
}

//
