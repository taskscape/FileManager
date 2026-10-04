// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <shobjidl.h>
#include "reorganize.h"
#include "session.h"
#include "revert.h"

HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;
CSalamanderDebugAbstract* SalamanderDebug = NULL;
int SalamanderVersion = 0;
CSalamanderGUIAbstract* SalGUI = NULL;

char* TransferBuffer = NULL;
int* TransferLen = NULL;
const CFileData** TransferFileData = NULL;
int* TransferIsDir = NULL;
DWORD* TransferRowData = NULL;
CPluginDataInterfaceAbstract** TransferPluginDataIface = NULL;
DWORD* TransferActCustomData = NULL;

int ColumnWidth[4] = {120, 180, 80, 90};
int ColumnFixed[4] = {0, 0, 0, 0};

static CPluginInterface g_Plugin;
static CPluginInterfaceForMenuExt g_Menu;
static CPluginInterfaceForFS g_FS;

char* LoadStr(int id)
{
    return Salamander()->LoadStr(HLanguage, id);
}

static void ShowError(HWND parent, const std::wstring& error)
{
    std::string text = ToUtf8(error);
    Salamander()->SalMessageBox(parent, text.c_str(), LoadStr(IDS_PLUGINNAME), MB_ICONERROR);
}

static void ShowInfo(HWND parent, const char* text)
{
    Salamander()->SalMessageBox(parent, text, LoadStr(IDS_PLUGINNAME), MB_ICONINFORMATION);
}

static BOOL GetDialogText(HWND dlg, int id, char* buf, int size)
{
    GetDlgItemTextA(dlg, id, buf, size);
    return buf[0] != 0;
}

static INT_PTR CALLBACK NewPlanProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG)
    {
        SetWindowLongPtr(dlg, GWLP_USERDATA, lParam);
        char scope[4096];
        char dest[4096];
        int type = 0;
        char* extra = NULL;
        if (Salamander()->GetPanelPath(PANEL_SOURCE, scope, (int)sizeof(scope), &type, &extra, FALSE) && type == PATH_TYPE_WINDOWS)
            SetDlgItemTextA(dlg, IDE_SCOPE, scope);
        if (Salamander()->GetPanelPath(PANEL_TARGET, dest, (int)sizeof(dest), &type, &extra, FALSE) && type == PATH_TYPE_WINDOWS)
            SetDlgItemTextA(dlg, IDE_DEST, dest);
        SetDlgItemTextA(dlg, IDE_NAME, "Reorganization");
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        char name[256];
        char scope[4096];
        char dest[4096];
        GetDialogText(dlg, IDE_NAME, name, (int)sizeof(name));
        GetDialogText(dlg, IDE_SCOPE, scope, (int)sizeof(scope));
        GetDialogText(dlg, IDE_DEST, dest, (int)sizeof(dest));
        std::wstring error;
        if (!CreatePlan(ToWide(name), ToWide(scope), ToWide(dest), error))
        {
            ShowError(dlg, error);
            return TRUE;
        }
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDCANCEL)
    {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static std::wstring PickPlanFile(HWND parent, BOOL save)
{
    IFileDialog* dialog = NULL;
    CLSID clsid = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
        return std::wstring();
    COMDLG_FILTERSPEC filter;
    filter.pszName = L"Reorganization plan";
    filter.pszSpec = L"*.reorgplan";
    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(L"reorgplan");
    std::wstring result;
    if (SUCCEEDED(dialog->Show(parent)))
    {
        IShellItem* item = NULL;
        if (SUCCEEDED(dialog->GetResult(&item)))
        {
            PWSTR path = NULL;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != NULL)
            {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
    return result;
}

static void OpenProposedPanel(const std::wstring& path)
{
    std::string user = ToUtf8(path);
    Salamander()->ChangePanelPathToPluginFS(PANEL_TARGET, "reorg", user.c_str(), NULL, -1, NULL);
}

class CApplyObserver : public CSalamanderOperationStepObserverAbstract
{
public:
    virtual BOOL WINAPI BeforeStep(int index, DWORD_PTR userData)
    {
        (void)index;
        (void)userData;
        return TRUE;
    }
    virtual void WINAPI AfterStep(int index, DWORD_PTR userData, DWORD result, DWORD error,
                                  DWORD resultFlags, DWORD metadataLosses,
                                  const CSalamanderFileIdentity* targetIdentity)
    {
        (void)index;
        (void)userData;
        (void)result;
        (void)error;
        (void)resultFlags;
        (void)metadataLosses;
        (void)targetIdentity;
    }
    virtual void WINAPI Finished(const CSalamanderOperationStepsSummary* summary, const char* operationId)
    {
        (void)operationId;
        char buf[256];
        sprintf_s(buf, LoadStr(IDS_APPLY_DONE), summary->Done, summary->Skipped, summary->Failed,
                  summary->Cancelled, summary->NotStarted);
        ShowInfo(Salamander()->GetMsgBoxParent(), buf);
        delete this;
    }
};

static INT_PTR CALLBACK ApplyProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG)
    {
        // Cleanup changes the compiled steps, so it is chosen in Plan Review and covered by the review
        // hash Apply has already checked; here it is shown read-only and can never diverge from it.
        HWND cleanup = GetDlgItem(dlg, IDC_CLEANUP);
        if (cleanup != NULL)
        {
            CheckDlgButton(dlg, IDC_CLEANUP, lParam != 0 ? BST_CHECKED : BST_UNCHECKED);
            EnableWindow(cleanup, FALSE);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        EndDialog(dlg, LOWORD(wParam));
        return TRUE;
    }
    return FALSE;
}

static void ApplyPlanBody(HWND parent)
{
    if (!Session().Open)
    {
        ShowInfo(parent, LoadStr(IDS_NO_PLAN));
        return;
    }
    // A scan failure must stop Apply even though it leaves the last complete analysis in place.
    std::wstring analysisError;
    if (!RefreshAnalysis(&analysisError))
    {
        ShowError(parent, analysisError);
        return;
    }
    if (reorg::HasBlockingErrors(Session().Issues))
    {
        ShowInfo(parent, LoadStr(IDS_APPLY_BLOCKED));
        return;
    }
    reorg::CCompiledPlan compiled = reorg::CompilePlan(Session().Overlay, Session().Plan, Session().Issues);
    if (!compiled.Ok || compiled.Steps.empty())
    {
        ShowError(parent, compiled.Error.empty() ? L"The plan did not compile." : compiled.Error);
        return;
    }
    // Apply runs only the exact step list a Plan Review confirmed (PLN-003): the same compiled hash
    // and step count. Any edit, rule, resolution, or disk change since then requires a new review.
    const reorg::CReviewMark& review = Session().Plan.Review;
    if (!review.Present || review.CompiledSha256 != compiled.Hash || review.StepCount != (int)compiled.Steps.size())
    {
        ShowInfo(parent, LoadStr(IDS_APPLY_NOT_REVIEWED));
        return;
    }
    if (DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_REORG_APPLY), parent, ApplyProc,
                       (LPARAM)(Session().Plan.Options.CleanupEmptiedFolders ? 1 : 0)) != IDOK)
        return;
    // Store paths stay placeholders until now so the reviewed hash does not depend on the apply id.
    std::wstring storeError;
    if (!reorg::MaterializeRecoveryStores(compiled, Session().Plan, Session().Overlay, reorg::NewGuid(), storeError))
    {
        ShowError(parent, storeError);
        return;
    }
    std::vector<CSalamanderOperationStep> steps(compiled.Steps.size());
    for (size_t i = 0; i < compiled.Steps.size(); ++i)
    {
        const reorg::CCompiledStep& src = compiled.Steps[i];
        if (src.Source.find(L"{store") != std::wstring::npos || src.Target.find(L"{store") != std::wstring::npos)
        {
            ShowError(parent, L"A recovery store path is still a placeholder.");
            return;
        }
        CSalamanderOperationStep& dst = steps[i];
        memset(&dst, 0, sizeof(dst));
        dst.StructSize = sizeof(dst);
        dst.Kind = (DWORD)src.Kind;
        dst.Flags = src.Flags;
        if (src.VerifyIdentity)
            dst.Flags |= SALOPSTEPF_VERIFY_SOURCE_IDENTITY;
        if (src.Dir)
            dst.Flags |= SALOPSTEPF_SOURCE_IS_DIR;
        if (src.CrossVolume)
            dst.Flags |= SALOPSTEPF_ALLOW_CROSS_VOLUME;
        dst.Source = src.Source.c_str();
        dst.Target = src.Target.c_str();
        dst.ExpectedIdentity.Valid = src.ExpectedIdentity.Valid ? 1 : 0;
        dst.ExpectedIdentity.VolumeSerial = src.ExpectedIdentity.VolumeSerial;
        memcpy(dst.ExpectedIdentity.FileId, src.ExpectedIdentity.FileId, 16);
        dst.ExpectedIdentity.Size.SetUI64(src.ExpectedIdentity.Size);
        ULARGE_INTEGER write;
        write.QuadPart = src.ExpectedIdentity.LastWrite;
        dst.ExpectedIdentity.LastWrite.dwLowDateTime = write.LowPart;
        dst.ExpectedIdentity.LastWrite.dwHighDateTime = write.HighPart;
        dst.ExpectedMetadataLosses = src.ExpectedMetadataLosses;
        dst.UserData = (DWORD_PTR)i;
    }
    char operationId[64];
    operationId[0] = 0;
    CApplyObserver* observer = new CApplyObserver();
    // The review was recorded before execution by Plan Review; nothing is marked after the host starts.
    if (!Salamander()->ExecuteOperationSteps(parent, LoadStr(IDS_PLUGINNAME), &steps[0], (int)steps.size(),
                                            SALEXECF_STOP_ON_ERROR, observer, operationId, (int)sizeof(operationId)))
    {
        delete observer;
        ShowError(parent, L"The host did not accept the steps.");
    }
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DLLInstance = instance;
    return TRUE;
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    {
        MessageBoxA(salamander->GetParentWindow(), REQUIRE_LAST_VERSION_OF_SALAMANDER, "Reorganize", MB_OK | MB_ICONERROR);
        return NULL;
    }
    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), "Reorganize");
    if (HLanguage == NULL)
        return NULL;
    SetSalamander(salamander->GetSalamanderGeneral());
    SalGUI = salamander->GetSalamanderGUI();
    Salamander()->SetHelpFileName("reorganize.chm");
    salamander->SetBasicPluginData(LoadStr(IDS_PLUGINNAME),
                                   FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION | FUNCTION_FILESYSTEM,
                                   "1.0", VERSINFO_COPYRIGHT, LoadStr(IDS_DESCRIPTION), "REORGANIZE", NULL, "reorg");
    salamander->SetPluginHomePageURL("www.taskscape.com");
    return &g_Plugin;
}

void WINAPI CPluginInterface::About(HWND parent)
{
    ShowInfo(parent, LoadStr(IDS_DESCRIPTION));
}

BOOL WINAPI CPluginInterface::Release(HWND parent, BOOL force)
{
    (void)parent;
    if (!force && Session().Open && Session().Plan.Dirty)
    {
        if (Salamander()->SalMessageBox(parent, LoadStr(IDS_DIRTY_CLOSE), LoadStr(IDS_PLUGINNAME), MB_YESNO | MB_ICONQUESTION) != IDYES)
            return FALSE;
    }
    ClosePlan();
    return TRUE;
}

void WINAPI CPluginInterface::LoadConfiguration(HWND, HKEY key, CSalamanderRegistryAbstract* registry)
{
    // A NULL key means "use defaults" (no saved configuration yet). Reading values from it fails with
    // an invalid handle and makes the host show "Error Loading Configuration" on every fresh start.
    if (key == NULL)
        return;
    char path[4096];
    for (int i = 0; i < 10; ++i)
    {
        char name[32];
        sprintf_s(name, "Recent%d", i);
        if (registry->GetValue(key, name, REG_SZ, path, sizeof(path)))
            Session().Recent.push_back(ToWide(path));
    }
    for (int i = 0; i < 4; ++i)
    {
        char name[32];
        sprintf_s(name, "ColWidth%d", i);
        registry->GetValue(key, name, REG_DWORD, &ColumnWidth[i], sizeof(int));
        sprintf_s(name, "ColFixed%d", i);
        registry->GetValue(key, name, REG_DWORD, &ColumnFixed[i], sizeof(int));
    }
}

void WINAPI CPluginInterface::SaveConfiguration(HWND, HKEY key, CSalamanderRegistryAbstract* registry)
{
    for (int i = 0; i < (int)Session().Recent.size() && i < 10; ++i)
    {
        char name[32];
        sprintf_s(name, "Recent%d", i);
        std::string path = ToUtf8(Session().Recent[i]);
        registry->SetValue(key, name, REG_SZ, path.c_str(), (DWORD)path.size() + 1);
    }
    for (int i = 0; i < 4; ++i)
    {
        char name[32];
        sprintf_s(name, "ColWidth%d", i);
        registry->SetValue(key, name, REG_DWORD, &ColumnWidth[i], sizeof(int));
        sprintf_s(name, "ColFixed%d", i);
        registry->SetValue(key, name, REG_DWORD, &ColumnFixed[i], sizeof(int));
    }
}

void WINAPI CPluginInterface::Configuration(HWND parent)
{
    DialogBox(HLanguage, MAKEINTRESOURCE(IDD_REORG_CONFIG), parent, ApplyProc);
}

void WINAPI CPluginInterface::Connect(HWND, CSalamanderConnectAbstract* salamander)
{
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_NEW), 0, CMD_NEW, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_OPEN), 0, CMD_OPEN, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_SAVE), 0, CMD_SAVE, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_SAVEAS), 0, CMD_SAVEAS, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_REVIEW), 0, CMD_REVIEW, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_ADDRULE), 0, CMD_ADDRULE, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_RULES), 0, CMD_RULES, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_MAPPING), 0, CMD_MAPPING, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_PROPOSED), 0, CMD_PROPOSED, TRUE, MENU_EVENT_TRUE, MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_ORIGINAL), 0, CMD_ORIGINAL, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_SELECTCHANGED), 0, CMD_SELECTCHANGED, TRUE, MENU_EVENT_TRUE, MENU_EVENT_DISK, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_UNDO), 0, CMD_UNDO, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_REDO), 0, CMD_REDO, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_VALIDATE), 0, CMD_VALIDATE, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_APPLY), 0, CMD_APPLY, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_RECOVERY), 0, CMD_RECOVERY, FALSE, 0, 0, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, LoadStr(IDS_MENU_CLOSE), 0, CMD_CLOSE, TRUE, MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    salamander->SetChangeDriveMenuItem(LoadStr(IDS_CHANGEDRIVE), 0);
}

void WINAPI CPluginInterface::ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData)
{
    delete pluginData;
}

CPluginInterfaceForArchiverAbstract* WINAPI CPluginInterface::GetInterfaceForArchiver() { return NULL; }
CPluginInterfaceForViewerAbstract* WINAPI CPluginInterface::GetInterfaceForViewer() { return NULL; }
CPluginInterfaceForMenuExtAbstract* WINAPI CPluginInterface::GetInterfaceForMenuExt() { return &g_Menu; }
CPluginInterfaceForFSAbstract* WINAPI CPluginInterface::GetInterfaceForFS() { return &g_FS; }
CPluginInterfaceForThumbLoaderAbstract* WINAPI CPluginInterface::GetInterfaceForThumbLoader() { return NULL; }
void WINAPI CPluginInterface::Event(int, DWORD) {}
void WINAPI CPluginInterface::ClearHistory(HWND) { Session().Recent.clear(); }
void WINAPI CPluginInterface::AcceptChangeOnPathNotification(const char*, BOOL) {}
void WINAPI CPluginInterface::PasswordManagerEvent(HWND, int) {}

DWORD WINAPI CPluginInterfaceForMenuExt::GetMenuItemState(int id, DWORD)
{
    BOOL needPlan = id == CMD_SAVE || id == CMD_SAVEAS || id == CMD_REVIEW || id == CMD_ADDRULE || id == CMD_RULES ||
                    id == CMD_MAPPING || id == CMD_PROPOSED || id == CMD_ORIGINAL || id == CMD_SELECTCHANGED ||
                    id == CMD_UNDO || id == CMD_REDO || id == CMD_VALIDATE || id == CMD_APPLY || id == CMD_RECOVERY || id == CMD_CLOSE;
    if (needPlan && (!Session().Open || Session().Analyzing))
        return 0;
    if (id == CMD_APPLY && Session().Analyzing)
        return 0;
    return MENU_ITEM_STATE_ENABLED;
}

// Plan Review shows the compiled steps next to the issues so "Mark as Reviewed" confirms exactly the
// step list whose hash and count Apply later requires. Other commands reuse the dialog without steps.
struct CReviewDialogData
{
    reorg::CCompiledPlan* Compiled; // NULL: list issues only and hide the plan controls
    bool CanMark;
    bool Analyzed;           // the last RefreshAnalysis completed
    size_t ValidationIssues; // issues from that analysis; compile notes are appended after them
};

// Compiles the step list Plan Review shows. A plan option that changes the steps (cleanup) is toggled
// in this dialog and recompiles without rescanning, so Mark as Reviewed binds the list on screen.
static void CompileForReview(CReviewDialogData& data)
{
    std::vector<reorg::CIssue>& issues = Session().Issues.Issues;
    if (issues.size() > data.ValidationIssues)
        issues.erase(issues.begin() + (ptrdiff_t)data.ValidationIssues, issues.end());
    *data.Compiled = reorg::CCompiledPlan();
    data.CanMark = false;
    if (data.Analyzed && !reorg::HasBlockingErrors(Session().Issues))
    {
        *data.Compiled = reorg::CompilePlan(Session().Overlay, Session().Plan, Session().Issues);
        data.CanMark = data.Compiled->Ok && !data.Compiled->Steps.empty() && !reorg::HasBlockingErrors(Session().Issues);
    }
}

static std::wstring DescribeStep(size_t index, const reorg::CCompiledStep& step)
{
    const wchar_t* kind = L"Move";
    if (step.Kind == reorg::StepCreateDir)
        kind = L"Create folder";
    else if (step.Kind == reorg::StepCopyDirTime)
        kind = L"Copy folder time";
    else if (step.Kind == reorg::StepRemoveEmptyDir)
        kind = L"Remove empty folder";
    // A cleanup is a move into the recovery store; naming it lets the reviewer see what the
    // Clean up emptied folders option adds before marking the steps as reviewed.
    if (step.Role == reorg::RoleCleanup)
        kind = L"Cleanup";
    wchar_t number[32];
    swprintf_s(number, L"%u. ", (unsigned)(index + 1));
    std::wstring text = std::wstring(number) + kind + L": " + step.Source;
    if (!step.Source.empty() && !step.Target.empty())
        text += L" -> ";
    return text + step.Target;
}

// Refilled after a cleanup toggle, so the list never shows steps from an earlier compile.
static void FillReviewList(HWND dlg, const CReviewDialogData* data)
{
    HWND list = GetDlgItem(dlg, IDC_ISSUES);
    SendMessageA(list, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < Session().Issues.Issues.size(); ++i)
    {
        std::string line = ToUtf8(Session().Issues.Issues[i].Code + L" " + Session().Issues.Issues[i].Text);
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    if (data == NULL || data->Compiled == NULL)
        return;
    for (size_t i = 0; i < data->Compiled->Steps.size(); ++i)
    {
        std::string line = ToUtf8(DescribeStep(i, data->Compiled->Steps[i]));
        SendMessageA(list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    EnableWindow(GetDlgItem(dlg, IDC_MARK_REVIEWED), data->CanMark ? TRUE : FALSE);
}

static INT_PTR CALLBACK ReviewProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_INITDIALOG)
    {
        CReviewDialogData* data = (CReviewDialogData*)lParam;
        SetWindowLongPtr(dlg, GWLP_USERDATA, lParam);
        FillReviewList(dlg, data);
        if (data == NULL || data->Compiled == NULL)
        {
            ShowWindow(GetDlgItem(dlg, IDC_MARK_REVIEWED), SW_HIDE);
            ShowWindow(GetDlgItem(dlg, IDC_CLEANUP), SW_HIDE);
        }
        else
            CheckDlgButton(dlg, IDC_CLEANUP, Session().Plan.Options.CleanupEmptiedFolders ? BST_CHECKED : BST_UNCHECKED);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDC_CLEANUP && HIWORD(wParam) == BN_CLICKED)
    {
        // The option is part of the plan, and the recompiled steps replace the listed ones, so a later
        // Mark as Reviewed (and therefore Apply) covers exactly the cleanup choice made here.
        CReviewDialogData* data = (CReviewDialogData*)GetWindowLongPtr(dlg, GWLP_USERDATA);
        bool cleanup = IsDlgButtonChecked(dlg, IDC_CLEANUP) == BST_CHECKED;
        if (data != NULL && data->Compiled != NULL && cleanup != Session().Plan.Options.CleanupEmptiedFolders)
        {
            Session().Plan.Options.CleanupEmptiedFolders = cleanup;
            Session().Plan.Dirty = true;
            CompileForReview(*data);
            FillReviewList(dlg, data);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDC_MARK_REVIEWED)
    {
        EndDialog(dlg, IDC_MARK_REVIEWED);
        return TRUE;
    }
    if (msg == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL))
    {
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    return FALSE;
}

static void ReviewPlanBody(HWND parent)
{
    if (!Session().Open)
    {
        ShowInfo(parent, LoadStr(IDS_NO_PLAN));
        return;
    }
    reorg::CCompiledPlan compiled;
    CReviewDialogData data;
    data.Compiled = &compiled;
    data.CanMark = false;
    data.Analyzed = RefreshAnalysis();
    data.ValidationIssues = Session().Issues.Issues.size();
    CompileForReview(data);
    // The dialog may recompile after a cleanup toggle, so the mark uses the final compile and state.
    if (DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_REORG_REVIEW), parent, ReviewProc, (LPARAM)&data) == IDC_MARK_REVIEWED && data.CanMark)
        reorg::MarkReviewed(Session().Plan, compiled.Hash, (int)compiled.Steps.size());
}

static INT_PTR CALLBACK RuleProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM)
{
    if (msg == WM_COMMAND && LOWORD(wParam) == IDOK)
    {
        char mask[256];
        char templ[1024];
        GetDlgItemTextA(dlg, IDE_MASK, mask, (int)sizeof(mask));
        GetDlgItemTextA(dlg, IDE_TEMPLATE, templ, (int)sizeof(templ));
        reorg::CRule rule;
        rule.Id = reorg::NewGuid();
        rule.Name = ToWide(mask);
        rule.Enabled = true;
        rule.Match.NameMask = ToWide(mask);
        rule.Destination = ToWide(templ);
        // Rules select items to move, so they match under the plan scope; MatchRule only resolves scope roots.
        if (!Session().Plan.ScopeRoots.empty())
            rule.Match.ScopeRoot = Session().Plan.ScopeRoots[0].Path;
        std::wstring ignored;
        reorg::AddRule(Session().Plan, Session().History, rule);
        std::vector<reorg::CRuleHit> hits = reorg::ApplyRules(Session().Plan, Session().Snapshot);
        for (size_t i = 0; i < hits.size(); ++i)
            reorg::StageMove(Session().Plan, Session().History, Session().Snapshot, hits[i].Source, hits[i].DestinationDir, hits[i].NewName, L"rule");
        RefreshAnalysis();
        EndDialog(dlg, IDOK);
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wParam) == IDCANCEL)
    {
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return msg == WM_INITDIALOG;
}

BOOL WINAPI CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent, int id, DWORD)
{
    (void)salamander;
    std::wstring error;
    if (id == CMD_NEW)
    {
        if (DialogBox(HLanguage, MAKEINTRESOURCE(IDD_REORG_NEWPLAN), parent, NewPlanProc) == IDOK && !Session().Plan.DestinationRoots.empty())
            OpenProposedPanel(Session().Plan.DestinationRoots[0].Path);
    }
    else if (id == CMD_OPEN)
    {
        std::wstring path = PickPlanFile(parent, FALSE);
        if (!path.empty() && OpenPlanFile(path, error))
        {
            if (!Session().Plan.DestinationRoots.empty())
                OpenProposedPanel(Session().Plan.DestinationRoots[0].Path);
        }
        else if (!path.empty())
            ShowError(parent, error);
    }
    else if (id == CMD_SAVE || id == CMD_SAVEAS)
    {
        std::wstring path = Session().Plan.FilePath;
        if (id == CMD_SAVEAS || path.empty())
            path = PickPlanFile(parent, TRUE);
        if (!path.empty() && SavePlanFileAs(path, error))
            ShowInfo(parent, LoadStr(IDS_SAVED));
        else if (!path.empty())
            ShowError(parent, error);
    }
    else if (id == CMD_REVIEW)
        ReviewPlanBody(parent);
    else if (id == CMD_RULES || id == CMD_RECOVERY)
        DialogBoxParam(HLanguage, MAKEINTRESOURCE(IDD_REORG_REVIEW), parent, ReviewProc, 0);
    else if (id == CMD_ADDRULE)
        DialogBox(HLanguage, MAKEINTRESOURCE(IDD_REORG_RULE), parent, RuleProc);
    else if (id == CMD_MAPPING)
    {
        std::wstring path = PickPlanFile(parent, FALSE);
        if (!path.empty())
        {
            std::vector<BYTE> data;
            DWORD readError = 0;
            if (Session().Probe.ReadFile(path, 32ull * 1024ull * 1024ull, data, readError))
            {
                std::string csv(data.begin(), data.end());
                std::wstring scope = Session().Plan.ScopeRoots.empty() ? L"" : Session().Plan.ScopeRoots[0].Path;
                std::wstring dest = Session().Plan.DestinationRoots.empty() ? L"" : Session().Plan.DestinationRoots[0].Path;
                reorg::CMappingResult mapping = reorg::ParseMapping(csv, scope, dest, Session().Snapshot);
                for (size_t i = 0; i < mapping.Rows.size(); ++i)
                {
                    if (mapping.Rows[i].Accepted)
                        reorg::StageMove(Session().Plan, Session().History, Session().Snapshot, mapping.Rows[i].Source,
                                         mapping.Rows[i].DestinationDir, mapping.Rows[i].NewName, L"mapping");
                }
                RefreshAnalysis();
            }
        }
    }
    else if (id == CMD_UNDO)
    {
        Session().History.Undo(Session().Plan);
        RefreshAnalysis();
    }
    else if (id == CMD_REDO)
    {
        Session().History.Redo(Session().Plan);
        RefreshAnalysis();
    }
    else if (id == CMD_VALIDATE)
        RefreshAnalysis();
    else if (id == CMD_APPLY)
        ApplyPlanBody(parent);
    else if (id == CMD_CLOSE)
    {
        if (!Session().Plan.Dirty || Salamander()->SalMessageBox(parent, LoadStr(IDS_DIRTY_CLOSE), LoadStr(IDS_PLUGINNAME), MB_YESNO) == IDYES)
            ClosePlan();
    }
    return FALSE;
}

BOOL WINAPI CPluginInterfaceForMenuExt::HelpForMenuItem(HWND, int) { return FALSE; }

void WINAPI CPluginInterfaceForMenuExt::BuildMenu(HWND, CSalamanderBuildMenuAbstract*) {}

CPluginFSInterfaceAbstract* WINAPI CPluginInterfaceForFS::OpenFS(const char*, int)
{
    return new CPluginFSInterface();
}

void WINAPI CPluginInterfaceForFS::CloseFS(CPluginFSInterfaceAbstract* fs)
{
    delete (CPluginFSInterface*)fs;
}

void WINAPI CPluginInterfaceForFS::ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* pluginFS, const char* pluginFSName, int, CFileData& file, int isDir)
{
    CPluginFSInterface* fs = (CPluginFSInterface*)pluginFS;
    if (isDir == 2)
    {
        // Sized like the FS path itself so going up from a long proposed path is never truncated.
        char cut[sizeof(fs->Path)];
        lstrcpynA(cut, fs->Path, (int)sizeof(cut));
        char* name = NULL;
        if (Salamander()->CutDirectory(cut, &name))
            Salamander()->ChangePanelPathToPluginFS(panel, pluginFSName, cut, NULL, -1, name);
        return;
    }
    if (isDir)
    {
        char next[4096];
        lstrcpynA(next, fs->Path, (int)sizeof(next));
        Salamander()->SalPathAppend(next, file.Name, (int)sizeof(next));
        Salamander()->ChangePanelPathToPluginFS(panel, pluginFSName, next, NULL, -1, NULL);
    }
}

BOOL WINAPI CPluginInterfaceForFS::DisconnectFS(HWND, BOOL, int, CPluginFSInterfaceAbstract*, const char*, int) { return TRUE; }
void WINAPI CPluginInterfaceForFS::ConvertPathToInternal(const char*, int, char*) {}
void WINAPI CPluginInterfaceForFS::ConvertPathToExternal(const char*, int, char*) {}
void WINAPI CPluginInterfaceForFS::ExecuteChangeDriveMenuItem(int panel)
{
    if (!Session().Open)
    {
        if (Salamander()->SalMessageBox(Salamander()->GetMsgBoxParent(), LoadStr(IDS_NEW_OR_OPEN), LoadStr(IDS_PLUGINNAME), MB_YESNO) == IDYES)
            g_Menu.ExecuteMenuItem(NULL, Salamander()->GetMsgBoxParent(), CMD_NEW, 0);
        else
            g_Menu.ExecuteMenuItem(NULL, Salamander()->GetMsgBoxParent(), CMD_OPEN, 0);
    }
    if (Session().Open && !Session().Plan.DestinationRoots.empty())
        Salamander()->ChangePanelPathToPluginFS(panel, "reorg", ToUtf8(Session().Plan.DestinationRoots[0].Path).c_str(), NULL, -1, NULL);
}
BOOL WINAPI CPluginInterfaceForFS::ChangeDriveMenuItemContextMenu(HWND, int, int, int, CPluginFSInterfaceAbstract*, const char*, int, BOOL, BOOL&, BOOL&, int&, void*&) { return FALSE; }
void WINAPI CPluginInterfaceForFS::ExecuteChangeDrivePostCommand(int, int, void*) {}
void WINAPI CPluginInterfaceForFS::EnsureShareExistsOnServer(int, const char*, const char*) {}
