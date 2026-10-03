// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <oleidl.h>
#include "reorganize.h"
#include "session.h"

static char* DupText(const char* text)
{
    return Salamander()->DupStr(text != NULL ? text : "");
}

static void FreeNode(CNodeData* node)
{
    if (node == NULL)
        return;
    free(node->Change);
    free(node->From);
    free(node->Issues);
    free(node->Reversible);
    free(node->Key);
    free(node);
}

static CNodeData* MakeNode(const reorg::COverlayNode& item)
{
    CNodeData* node = (CNodeData*)calloc(1, sizeof(CNodeData));
    if (node == NULL)
        return NULL;
    node->Change = _strdup(ChangeText(item.Change));
    node->From = _strdup(ToUtf8(item.OriginalPath).c_str());
    char issues[64];
    sprintf_s(issues, "%d", item.IssueErrors + item.IssueWarnings);
    node->Issues = _strdup(issues);
    node->Reversible = _strdup(ReversibleText(item.Reversible));
    std::string key = ToUtf8(item.Key);
    node->Key = (wchar_t*)malloc((item.Key.size() + 1) * sizeof(wchar_t));
    if (node->Key != NULL)
        wcscpy_s(node->Key, item.Key.size() + 1, item.Key.c_str());
    (void)key;
    return node;
}

static void WINAPI ColumnText()
{
    CNodeData* node = NULL;
    if (TransferFileData != NULL && *TransferFileData != NULL)
        node = (CNodeData*)(*TransferFileData)->PluginData;
    const char* text = "";
    if (node != NULL && TransferActCustomData != NULL)
    {
        if (*TransferActCustomData == 1 && node->Change != NULL)
            text = node->Change;
        else if (*TransferActCustomData == 2 && node->From != NULL)
            text = node->From;
        else if (*TransferActCustomData == 3 && node->Issues != NULL)
            text = node->Issues;
        else if (*TransferActCustomData == 4 && node->Reversible != NULL)
            text = node->Reversible;
    }
    if (TransferBuffer != NULL && TransferLen != NULL)
    {
        lstrcpynA(TransferBuffer, text, 1000);
        *TransferLen = (int)strlen(TransferBuffer);
    }
}

static void AddColumn(CSalamanderViewAbstract* view, int index, int id, const char* name, const char* tip, DWORD custom)
{
    CColumn column;
    memset(&column, 0, sizeof(column));
    lstrcpynA(column.Name, name, COLUMN_NAME_MAX);
    lstrcpynA(column.Description, tip, COLUMN_DESCRIPTION_MAX);
    column.GetText = ColumnText;
    column.CustomData = custom;
    column.SupportSorting = 0;
    column.LeftAlignment = 1;
    column.ID = COLUMN_ID_CUSTOM;
    column.Width = ColumnWidth[id];
    column.FixedWidth = ColumnFixed[id];
    view->InsertColumn(index, &column);
}

CPluginFSInterface::CPluginFSInterface()
{
    Path[0] = 0;
}

BOOL WINAPI CPluginFSInterface::GetCurrentPath(char* userPart)
{
    lstrcpyA(userPart, Path);
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetFullName(CFileData& file, int, char* buf, int bufSize)
{
    lstrcpynA(buf, Path, bufSize);
    Salamander()->SalPathAppend(buf, file.Name, bufSize);
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetFullFSPath(HWND, const char*, char* path, int pathSize, BOOL& success)
{
    lstrcpynA(path, Path, pathSize);
    success = TRUE;
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::GetRootPath(char* userPart)
{
    if (Session().Open && !Session().Plan.DestinationRoots.empty())
    {
        std::string root = ToUtf8(Session().Plan.DestinationRoots[0].Path);
        lstrcpyA(userPart, root.c_str());
    }
    else
        userPart[0] = 0;
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::IsCurrentPath(int, int, const char* userPart)
{
    return Salamander()->StrICmp(Path, userPart) == 0;
}

BOOL WINAPI CPluginFSInterface::IsOurPath(int, int, const char*)
{
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::ChangePath(int, char*, int, const char* userPart, char*, BOOL*, BOOL, int)
{
    if (!Session().Open)
    {
        Salamander()->SalMessageBox(Salamander()->GetMsgBoxParent(), LoadStr(IDS_NO_PLAN), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
        return FALSE;
    }
    if (userPart == NULL || userPart[0] == 0)
    {
        if (Session().Plan.DestinationRoots.empty())
            return FALSE;
        std::string root = ToUtf8(Session().Plan.DestinationRoots[0].Path);
        lstrcpynA(Path, root.c_str(), (int)sizeof(Path));
    }
    else
        lstrcpynA(Path, userPart, (int)sizeof(Path));
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::ListCurrentPath(CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData, int& iconsType, BOOL)
{
    iconsType = pitSimple;
    pluginData = new CPluginDataInterface();
    dir->SetValidData(VALID_DATA_SIZE | VALID_DATA_DATE | VALID_DATA_TIME | VALID_DATA_ATTRIBUTES | VALID_DATA_EXTENSION);
    if (!Session().Open)
        return TRUE;
    std::vector<const reorg::COverlayNode*> items = Session().Overlay.List(ToWide(Path));
    for (size_t i = 0; i < items.size(); ++i)
    {
        const reorg::COverlayNode* item = items[i];
        if (item->Excluded)
            continue;
        CFileData file;
        memset(&file, 0, sizeof(file));
        std::string name = ToUtf8(item->Name);
        file.Name = DupText(name.c_str());
        if (file.Name == NULL)
            return FALSE;
        file.NameLen = (unsigned)strlen(file.Name);
        file.Ext = file.Name + file.NameLen;
        char* dot = strrchr(file.Name, '.');
        if (dot != NULL && dot != file.Name && !item->IsDir)
            file.Ext = dot;
        file.Size.SetUI64(item->Info.Size);
        file.Attr = item->Info.Attributes;
        if (item->IsDir)
            file.Attr |= FILE_ATTRIBUTE_DIRECTORY;
        ULARGE_INTEGER write;
        write.QuadPart = item->Info.LastWrite;
        file.LastWrite.dwLowDateTime = write.LowPart;
        file.LastWrite.dwHighDateTime = write.HighPart;
        file.PluginData = (DWORD_PTR)MakeNode(*item);
        if (item->IsDir)
            dir->AddDir(NULL, file, pluginData);
        else
            dir->AddFile(NULL, file, pluginData);
    }
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::TryCloseOrDetach(BOOL, BOOL, BOOL& detach, int)
{
    detach = FALSE;
    return TRUE;
}

void WINAPI CPluginFSInterface::Event(int, DWORD) {}
void WINAPI CPluginFSInterface::ReleaseObject(HWND) {}

DWORD WINAPI CPluginFSInterface::GetSupportedServices()
{
    return FS_SERVICE_MOVEFROMDISKTOFS | FS_SERVICE_MOVEFROMFS | FS_SERVICE_QUICKRENAME | FS_SERVICE_CREATEDIR |
           FS_SERVICE_DELETE | FS_SERVICE_SHOWINFO | FS_SERVICE_GETFSICON | FS_SERVICE_ACCEPTSCHANGENOTIF |
           FS_SERVICE_GETPATHFORMAINWNDTITLE | FS_SERVICE_CONTEXTMENU;
}

BOOL WINAPI CPluginFSInterface::GetChangeDriveOrDisconnectItem(const char*, char*& title, HICON& icon, BOOL& destroyIcon)
{
    title = DupText(LoadStr(IDS_CHANGEDRIVE));
    icon = NULL;
    destroyIcon = FALSE;
    return TRUE;
}

HICON WINAPI CPluginFSInterface::GetFSIcon(BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return NULL;
}

void WINAPI CPluginFSInterface::GetDropEffect(const char*, const char*, DWORD allowedEffects, DWORD, DWORD* dropEffect)
{
    *dropEffect = (allowedEffects & DROPEFFECT_MOVE) != 0 ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
}

void WINAPI CPluginFSInterface::GetFSFreeSpace(CQuadWord* retValue)
{
    *retValue = CQuadWord(-1, -1);
}

BOOL WINAPI CPluginFSInterface::GetNextDirectoryLineHotPath(const char*, int, int&) { return FALSE; }
void WINAPI CPluginFSInterface::CompleteDirectoryLineHotPath(char*, int) {}

BOOL WINAPI CPluginFSInterface::GetPathForMainWindowTitle(const char*, int, char* buf, int bufSize)
{
    std::string name = Session().Open ? ToUtf8(Session().Plan.Name) : std::string();
    _snprintf_s(buf, bufSize, _TRUNCATE, "Reorganize: %s - %s", name.c_str(), Path);
    return TRUE;
}

void WINAPI CPluginFSInterface::ShowInfoDialog(const char*, HWND parent)
{
    Salamander()->SalMessageBox(parent, LoadStr(IDS_DESCRIPTION), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
}

BOOL WINAPI CPluginFSInterface::ExecuteCommandLine(HWND, char*, int&, int&) { return FALSE; }

BOOL WINAPI CPluginFSInterface::QuickRename(const char*, int mode, HWND parent, CFileData& file, BOOL, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return TRUE;
    CNodeData* node = (CNodeData*)file.PluginData;
    if (node == NULL || node->Key == NULL)
        return FALSE;
    std::wstring error;
    reorg::CStageResult staged = reorg::StageRename(Session().Plan, Session().History, Session().Snapshot, node->Key, ToWide(newName), L"rename");
    if (!staged.Ok)
    {
        Salamander()->SalMessageBox(parent, ToUtf8(staged.Error).c_str(), LoadStr(IDS_PLUGINNAME), MB_ICONERROR);
        cancel = TRUE;
        return FALSE;
    }
    RefreshAnalysis();
    return TRUE;
}

void WINAPI CPluginFSInterface::AcceptChangeOnPathNotification(const char*, const char*, BOOL)
{
    RefreshAnalysis();
}

BOOL WINAPI CPluginFSInterface::CreateDir(const char*, int mode, HWND parent, char* newName, BOOL& cancel)
{
    cancel = FALSE;
    if (mode == 1)
        return TRUE;
    std::wstring path = ToWide(Path);
    if (!path.empty() && path[path.size() - 1] != L'\\')
        path += L'\\';
    path += ToWide(newName);
    std::wstring error;
    reorg::CStageResult staged = reorg::StageCreateFolder(Session().Plan, Session().History, path, L"create");
    if (!staged.Ok)
    {
        Salamander()->SalMessageBox(parent, ToUtf8(staged.Error).c_str(), LoadStr(IDS_PLUGINNAME), MB_ICONERROR);
        cancel = TRUE;
        return FALSE;
    }
    RefreshAnalysis();
    return TRUE;
}

void WINAPI CPluginFSInterface::ViewFile(const char*, HWND, CSalamanderForViewFileOnFSAbstract*, CFileData&) {}

static INT_PTR CALLBACK RemoveProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
{
    if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        EndDialog(dlg, LOWORD(wParam));
        return TRUE;
    }
    return msg == WM_INITDIALOG;
}

BOOL WINAPI CPluginFSInterface::Delete(const char*, int mode, HWND parent, int, int, int, BOOL& cancelOrError)
{
    cancelOrError = FALSE;
    if (mode == 1)
    {
        if (DialogBox(HLanguage, MAKEINTRESOURCE(IDD_REORG_REMOVEFROMPLAN), parent, RemoveProc) != IDOK)
        {
            cancelOrError = TRUE;
            return FALSE;
        }
    }
    int index = 0;
    BOOL isDir = FALSE;
    int selectedFiles = 0;
    int selectedDirs = 0;
    BOOL any = Salamander()->GetPanelSelection(PANEL_SOURCE, &selectedFiles, &selectedDirs);
    if (any && (selectedFiles + selectedDirs) > 0)
    {
        const CFileData* file = Salamander()->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
        while (file != NULL)
        {
            CNodeData* node = (CNodeData*)file->PluginData;
            if (node != NULL && node->Key != NULL)
                reorg::StageUnstage(Session().Plan, Session().History, node->Key, L"remove");
            file = Salamander()->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir);
        }
    }
    else
    {
        const CFileData* file = Salamander()->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
        if (file != NULL)
        {
            CNodeData* node = (CNodeData*)file->PluginData;
            if (node != NULL && node->Key != NULL)
                reorg::StageUnstage(Session().Plan, Session().History, node->Key, L"remove");
        }
    }
    RefreshAnalysis();
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::CopyOrMoveFromFS(BOOL copy, int, const char*, HWND parent, int, int, int, char* targetPath, BOOL& operationMask, BOOL& cancelOrHandlePath, HWND)
{
    operationMask = FALSE;
    cancelOrHandlePath = FALSE;
    if (copy)
    {
        Salamander()->SalMessageBox(parent, LoadStr(IDS_COPY_UNSUPPORTED), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
    if (targetPath != NULL && _strnicmp(targetPath, "reorg:", 6) != 0)
    {
        Salamander()->SalMessageBox(parent, LoadStr(IDS_NOT_DISK), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
        cancelOrHandlePath = TRUE;
        return FALSE;
    }
    Salamander()->SetUserWorkedOnPanelPath(PANEL_SOURCE);
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::CopyOrMoveFromDiskToFS(BOOL copy, int mode, const char*, HWND parent, const char* sourcePath, SalEnumSelection2 next,
                                                      void* nextParam, int, int, char* targetPath, BOOL* invalidPathOrCancel)
{
    if (invalidPathOrCancel != NULL)
        *invalidPathOrCancel = FALSE;
    if (copy)
    {
        Salamander()->SalMessageBox(parent, LoadStr(IDS_COPY_UNSUPPORTED), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
        if (invalidPathOrCancel != NULL)
            *invalidPathOrCancel = TRUE;
        return FALSE;
    }
    if (mode == 1)
    {
        if (targetPath != NULL)
            lstrcpynA(targetPath, Path, MAX_PATH);
        return TRUE;
    }
    const char* target = (targetPath != NULL && targetPath[0] != 0) ? targetPath : Path;
    int error = 0;
    const char* dos = NULL;
    BOOL isDir = FALSE;
    CQuadWord size;
    DWORD attr = 0;
    FILETIME time;
    const char* name = next(parent, 1, &dos, &isDir, &size, &attr, &time, nextParam, &error);
    while (name != NULL)
    {
        std::wstring source = ToWide(sourcePath);
        if (!source.empty() && source[source.size() - 1] != L'\\')
            source += L'\\';
        source += ToWide(name);
        std::wstring errorText;
        if (!StageDiskMove(source, ToWide(target), ToWide(name), errorText))
        {
            Salamander()->SalMessageBox(parent, ToUtf8(errorText).c_str(), LoadStr(IDS_PLUGINNAME), MB_ICONERROR);
            if (invalidPathOrCancel != NULL)
                *invalidPathOrCancel = TRUE;
            return FALSE;
        }
        name = next(parent, 1, &dos, &isDir, &size, &attr, &time, nextParam, &error);
    }
    Salamander()->SetUserWorkedOnPanelPath(PANEL_SOURCE);
    return TRUE;
}

BOOL WINAPI CPluginFSInterface::ChangeAttributes(const char*, HWND, int, int, int) { return FALSE; }
void WINAPI CPluginFSInterface::ShowProperties(const char*, HWND, int, int, int) {}
void WINAPI CPluginFSInterface::ContextMenu(const char*, HWND parent, int, int, int, int, int, int)
{
    Salamander()->SalMessageBox(parent, LoadStr(IDS_DESCRIPTION), LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
}
BOOL WINAPI CPluginFSInterface::OpenFindDialog(const char*, int) { return FALSE; }
void WINAPI CPluginFSInterface::OpenActiveFolder(const char*, HWND) {}
void WINAPI CPluginFSInterface::GetAllowedDropEffects(int, const char*, DWORD* allowedEffects)
{
    *allowedEffects = DROPEFFECT_MOVE;
}
BOOL WINAPI CPluginFSInterface::HandleMenuMsg(UINT, WPARAM, LPARAM, LRESULT*) { return FALSE; }
BOOL WINAPI CPluginFSInterface::GetNoItemsInPanelText(char* textBuf, int textBufSize)
{
    lstrcpynA(textBuf, LoadStr(IDS_EMPTY), textBufSize);
    return TRUE;
}
void WINAPI CPluginFSInterface::ShowSecurityInfo(HWND) {}

BOOL WINAPI CPluginDataInterface::CallReleaseForFiles() { return TRUE; }
BOOL WINAPI CPluginDataInterface::CallReleaseForDirs() { return TRUE; }
void WINAPI CPluginDataInterface::ReleasePluginData(CFileData& file, BOOL)
{
    FreeNode((CNodeData*)file.PluginData);
    file.PluginData = 0;
}
void WINAPI CPluginDataInterface::GetFileDataForUpDir(const char*, CFileData&) {}
BOOL WINAPI CPluginDataInterface::GetFileDataForNewDir(const char*, CFileData&) { return TRUE; }
HIMAGELIST WINAPI CPluginDataInterface::GetSimplePluginIcons(int) { return NULL; }
BOOL WINAPI CPluginDataInterface::HasSimplePluginIcon(CFileData&, BOOL) { return TRUE; }
HICON WINAPI CPluginDataInterface::GetPluginIcon(const CFileData*, int, BOOL& destroyIcon)
{
    destroyIcon = FALSE;
    return NULL;
}
int WINAPI CPluginDataInterface::CompareFilesFromFS(const CFileData* file1, const CFileData* file2)
{
    return strcmp(file1->Name, file2->Name);
}
void WINAPI CPluginDataInterface::SetupView(BOOL, CSalamanderViewAbstract* view, const char*, const CFileData*)
{
    view->GetTransferVariables(TransferFileData, TransferIsDir, TransferBuffer, TransferLen, TransferRowData, TransferPluginDataIface, TransferActCustomData);
    int index = view->GetColumnsCount();
    AddColumn(view, index++, 0, LoadStr(IDS_COL_CHANGE), LoadStr(IDS_COL_CHANGE_TIP), 1);
    AddColumn(view, index++, 1, LoadStr(IDS_COL_FROM), LoadStr(IDS_COL_FROM_TIP), 2);
    AddColumn(view, index++, 2, LoadStr(IDS_COL_ISSUES), LoadStr(IDS_COL_ISSUES_TIP), 3);
    AddColumn(view, index++, 3, LoadStr(IDS_COL_REVERSIBLE), LoadStr(IDS_COL_REVERSIBLE_TIP), 4);
}
void WINAPI CPluginDataInterface::ColumnFixedWidthShouldChange(BOOL, const CColumn* column, int newFixedWidth)
{
    if (column != NULL && column->CustomData >= 1 && column->CustomData <= 4)
        ColumnFixed[column->CustomData - 1] = newFixedWidth;
}
void WINAPI CPluginDataInterface::ColumnWidthWasChanged(BOOL, const CColumn* column, int newWidth)
{
    if (column != NULL && column->CustomData >= 1 && column->CustomData <= 4)
        ColumnWidth[column->CustomData - 1] = newWidth;
}
BOOL WINAPI CPluginDataInterface::GetInfoLineContent(int, const CFileData* file, BOOL, int, int, BOOL, const CQuadWord&, char* buffer, DWORD*, int& hotTextsCount)
{
    hotTextsCount = 0;
    if (file == NULL || file->PluginData == 0)
        return FALSE;
    CNodeData* node = (CNodeData*)file->PluginData;
    _snprintf_s(buffer, 1000, _TRUNCATE, "%s %s", node->Change != NULL ? node->Change : "", node->From != NULL ? node->From : "");
    return TRUE;
}
BOOL WINAPI CPluginDataInterface::CanBeCopiedToClipboard() { return FALSE; }
BOOL WINAPI CPluginDataInterface::GetByteSize(const CFileData* file, BOOL, CQuadWord* size)
{
    *size = file->Size;
    return TRUE;
}
BOOL WINAPI CPluginDataInterface::GetLastWriteDate(const CFileData*, BOOL, SYSTEMTIME*) { return FALSE; }
BOOL WINAPI CPluginDataInterface::GetLastWriteTime(const CFileData*, BOOL, SYSTEMTIME*) { return FALSE; }
