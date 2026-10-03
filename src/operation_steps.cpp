// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "plugins.h"
#include "worker.h"
#include "usermenu.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "dialogs.h"
#include "operation_steps.h"

// The host SDK target predates FILE_ID_INFO. The numeric class is stable and
// returns the same 128-bit identity the plan stored.
struct CPlanFileIdInfo
{
    unsigned __int64 VolumeSerialNumber;
    BYTE Identifier[16];
};

enum
{
    kFileIdInfoClass = 18
};

namespace
{

DWORD MapLosses(DWORD hostMask);

struct CStepBridge : public CStepExecutionBridge
{
    CSalamanderOperationStepObserverAbstract* Observer;
    CPluginInterfaceAbstract* Plugin;
    DWORD Flags;
    int Count;
    int Done, Skipped, Failed, Cancelled, NotStarted;
    BOOL UserCancelled;
    BOOL FinishedPosted;
    char OperationId[64];
    HWND Notify;
    CRITICAL_SECTION Guard;

    CStepBridge()
        : Observer(NULL), Plugin(NULL), Flags(0), Count(0), Done(0), Skipped(0), Failed(0),
          Cancelled(0), NotStarted(0), UserCancelled(FALSE), FinishedPosted(FALSE), Notify(NULL)
    {
        OperationId[0] = 0;
        InitializeCriticalSection(&Guard);
    }

    ~CStepBridge()
    {
        if (Notify)
            DestroyWindow(Notify);
        DeleteCriticalSection(&Guard);
    }

    virtual BOOL BeforeStep(int index)
    {
        if (Observer == NULL || index < 0)
            return TRUE;
        return Observer->BeforeStep(index, 0);
    }

    virtual void AfterStep(int index, DWORD result, DWORD error, DWORD resultFlags, DWORD metadataLosses)
    {
        EnterCriticalSection(&Guard);
        if (result == SALOPSTEP_RESULT_DONE)
            ++Done;
        else if (result == SALOPSTEP_RESULT_SKIPPED)
            ++Skipped;
        else if (result == SALOPSTEP_RESULT_CANCELLED)
            ++Cancelled;
        else
            ++Failed;
        LeaveCriticalSection(&Guard);
        // The worker reports host EMetadataLoss bits; the SDK observer receives SALMDLOSS bits.
        if (Observer != NULL)
            Observer->AfterStep(index, 0, result, error, resultFlags, MapLosses(metadataLosses), NULL);
    }

    virtual BOOL StopOnError() const { return (Flags & SALEXECF_STOP_ON_ERROR) != 0; }

    virtual void NoteNotStarted(int count)
    {
        if (count > 0)
            NotStarted += count;
    }

    virtual void QueueFinished(BOOL cancelled)
    {
        UserCancelled = cancelled;
        if (Notify)
            PostMessage(Notify, WM_APP, 0, 0);
    }
};

CStepBridge* g_Bridge = NULL;

LRESULT CALLBACK StepBridgeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_APP)
    {
        CStepBridge* bridge = (CStepBridge*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (bridge != NULL && !bridge->FinishedPosted && bridge->Observer != NULL)
        {
            bridge->FinishedPosted = TRUE;
            CSalamanderOperationStepsSummary summary;
            memset(&summary, 0, sizeof(summary));
            summary.StructSize = sizeof(summary);
            summary.Done = bridge->Done;
            summary.Skipped = bridge->Skipped;
            summary.Failed = bridge->Failed;
            summary.Cancelled = bridge->Cancelled;
            summary.NotStarted = bridge->NotStarted;
            summary.UserCancelled = bridge->UserCancelled;
            bridge->Observer->Finished(&summary, bridge->OperationId);
            if (g_Bridge == bridge)
                g_Bridge = NULL;
            delete bridge;
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

char* DupUtf8FromWide(const WCHAR* text)
{
    if (text == NULL)
        return NULL;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0)
        return NULL;
    char* out = (char*)malloc((size_t)bytes);
    if (out == NULL)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out, bytes, NULL, NULL);
    return out;
}

BOOL ValidDiskStepPath(const WCHAR* path)
{
    if (path == NULL || path[0] == 0)
        return FALSE;
    if (path[0] != L'\\' && !(path[0] != 0 && path[1] == L':'))
        return FALSE;
    if (wcsncmp(path, L"\\\\.\\", 4) == 0 || wcsncmp(path, L"\\\\?\\GLOBALROOT", 14) == 0)
        return FALSE;
    return TRUE;
}

DWORD MapLosses(DWORD hostMask)
{
    DWORD out = 0;
    if (hostMask & 0x00000001)
        out |= SALMDLOSS_LASTWRITE;
    if (hostMask & 0x00000002)
        out |= SALMDLOSS_CREATION_LASTACCESS;
    if (hostMask & 0x00000004)
        out |= SALMDLOSS_ATTRIBUTES;
    if (hostMask & 0x00000008)
        out |= SALMDLOSS_SECURITY;
    if (hostMask & 0x00000010)
        out |= SALMDLOSS_ADS;
    if (hostMask & 0x00000020)
        out |= SALMDLOSS_COMPRESSION_EFS;
    return out;
}

} // namespace

static BOOL PlanPathsShareVolume(const char* source, const char* target);

static DWORD SalLossesToHost(DWORD sal)
{
    DWORD host = 0;
    if (sal & SALMDLOSS_LASTWRITE)
        host |= mmlLastWriteTime;
    if (sal & SALMDLOSS_CREATION_LASTACCESS)
        host |= mmlCreationAndAccessTimes;
    if (sal & SALMDLOSS_ATTRIBUTES)
        host |= mmlAttributes;
    if (sal & SALMDLOSS_SECURITY)
        host |= mmlSecurity;
    if (sal & SALMDLOSS_ADS)
        host |= mmlAlternateDataStreams;
    if (sal & SALMDLOSS_COMPRESSION_EFS)
        host |= mmlCompressionAndEncryption;
    return host;
}

DWORD OperationStepsFilterAcceptedLosses(DWORD hostLossMask, DWORD salExpected, BOOL accepted)
{
    if (!accepted)
        return hostLossMask;
    return hostLossMask & ~SalLossesToHost(salExpected);
}

BOOL OperationStepsBlockUnload(CPluginInterfaceAbstract* plugin)
{
    return g_Bridge != NULL && g_Bridge->Plugin == plugin;
}

BOOL RejectPlanStepPrecondition(COperation* op, BOOL creatingDirectory, BOOL* acceptExistingDirectory, DWORD* error)
{
    if (acceptExistingDirectory)
        *acceptExistingDirectory = FALSE;
    if (op == NULL || (op->OpFlags & OPFL_PLAN_STEP) == 0)
        return TRUE;
    if ((op->OpFlags & OPFL_VERIFY_SOURCE_IDENTITY) != 0 && op->ExpectedIdentityValid && op->SourceName != NULL)
    {
        // Identity is taken from the opened object, not from the path string, so a
        // replacement that reuses the planned path cannot pass a plan step.
        HANDLE file = CreateFileUtf8(op->SourceName, FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
        if (file == INVALID_HANDLE_VALUE)
        {
            if (error)
                *error = GetLastError();
            return FALSE;
        }
        CPlanFileIdInfo info;
        memset(&info, 0, sizeof(info));
        BOOL ok = GetFileInformationByHandleEx(file, (FILE_INFO_BY_HANDLE_CLASS)kFileIdInfoClass, &info, sizeof(info));
        BY_HANDLE_FILE_INFORMATION basic;
        memset(&basic, 0, sizeof(basic));
        BOOL basicOk = GetFileInformationByHandle(file, &basic);
        CloseHandle(file);
        if (!ok && !basicOk)
        {
            if (error)
                *error = GetLastError();
            return FALSE;
        }
        DWORD serial = ok ? (DWORD)info.VolumeSerialNumber : basic.dwVolumeSerialNumber;
        const BYTE* id = ok ? info.Identifier : NULL;
        if (serial != op->ExpectedVolumeSerial || (id != NULL && memcmp(id, op->ExpectedFileId, 16) != 0))
        {
            if (error)
                *error = ERROR_FILE_INVALID;
            return FALSE;
        }
        BOOL isDir = basicOk && (basic.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDir)
        {
            unsigned __int64 size = basicOk ? (((unsigned __int64)basic.nFileSizeHigh << 32) | basic.nFileSizeLow) : 0;
            ULARGE_INTEGER write;
            write.LowPart = basic.ftLastWriteTime.dwLowDateTime;
            write.HighPart = basic.ftLastWriteTime.dwHighDateTime;
            ULARGE_INTEGER expected;
            expected.LowPart = op->ExpectedLastWrite.dwLowDateTime;
            expected.HighPart = op->ExpectedLastWrite.dwHighDateTime;
            if (size != op->ExpectedSize.Value || write.QuadPart != expected.QuadPart)
            {
                if (error)
                    *error = ERROR_FILE_INVALID;
                return FALSE;
            }
        }
    }
    const char* target = op->TargetName;
    if ((op->OpFlags & OPFL_FAIL_IF_TARGET_EXISTS) != 0 && target != NULL && target[0] != 0)
    {
        DWORD attr = GetFileAttributesUtf8(target);
        if (attr != INVALID_FILE_ATTRIBUTES)
        {
            if (creatingDirectory && (attr & FILE_ATTRIBUTE_DIRECTORY) && (op->OpFlags & OPFL_CREATEDIR_ACCEPT_EXISTING))
            {
                if (acceptExistingDirectory)
                    *acceptExistingDirectory = TRUE;
                return TRUE;
            }
            if (error)
                *error = ERROR_ALREADY_EXISTS;
            return FALSE;
        }
    }
    if ((op->OpFlags & OPFL_NO_CROSS_VOLUME) != 0 && op->SourceName != NULL && op->TargetName != NULL)
    {
        if (!PlanPathsShareVolume(op->SourceName, op->TargetName))
        {
            if (error)
                *error = ERROR_NOT_SAME_DEVICE;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL IsDrivePath(const char* path)
{
    return path[0] != 0 && path[1] == ':' &&
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
}

static BOOL PlanPathsShareVolume(const char* source, const char* target)
{
    if (IsDrivePath(source) && IsDrivePath(target))
        return (source[0] & ~0x20) == (target[0] & ~0x20);
    WCHAR* sourceW = ConvertAllocUtf8ToWide(source, -1);
    WCHAR* targetW = ConvertAllocUtf8ToWide(target, -1);
    WCHAR sourceRoot[MAX_PATH];
    WCHAR targetRoot[MAX_PATH];
    BOOL same = FALSE;
    if (sourceW != NULL && targetW != NULL &&
        GetVolumePathNameW(sourceW, sourceRoot, MAX_PATH) &&
        GetVolumePathNameW(targetW, targetRoot, MAX_PATH))
    {
        DWORD sourceSerial = 0;
        DWORD targetSerial = 0;
        DWORD component = 0;
        DWORD flags = 0;
        if (GetVolumeInformationW(sourceRoot, NULL, 0, &sourceSerial, &component, &flags, NULL, 0) &&
            GetVolumeInformationW(targetRoot, NULL, 0, &targetSerial, &component, &flags, NULL, 0))
            same = sourceSerial == targetSerial;
    }
    free(sourceW);
    free(targetW);
    return same;
}

BOOL WINAPI CSalamanderGeneral::ExecuteOperationSteps(HWND parent, const char* caption,
                                              const CSalamanderOperationStep* steps, int count,
                                              DWORD flags,
                                              CSalamanderOperationStepObserverAbstract* observer,
                                              char* operationIdBuf, int operationIdBufSize)
{
    CALL_STACK_MESSAGE2("CSalamanderGeneral::ExecuteOperationSteps(, , %d)", count);
    if (count <= 0 || count > 10000000 || steps == NULL || observer == NULL || operationIdBuf == NULL || operationIdBufSize < 64)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    const DWORD knownFlags = SALOPSTEPF_SOURCE_IS_DIR | SALOPSTEPF_TARGET_MUST_NOT_EXIST | SALOPSTEPF_ALLOW_CROSS_VOLUME |
                             SALOPSTEPF_VERIFY_SOURCE_IDENTITY | SALOPSTEPF_METADATA_LOSS_ACCEPTED | SALOPSTEPF_CREATEDIR_ACCEPT_EXISTING;
    for (int i = 0; i < count; ++i)
    {
        const CSalamanderOperationStep& step = steps[i];
        if (step.StructSize < sizeof(CSalamanderOperationStep) || step.Kind < SALOPSTEP_CREATEDIR || step.Kind > SALOPSTEP_COPYDIRTIME ||
            (step.Flags & ~knownFlags) != 0)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if ((step.Kind == SALOPSTEP_COPYDIRTIME || step.Kind == SALOPSTEP_REMOVEEMPTYDIR) && (step.Flags & SALOPSTEPF_SOURCE_IS_DIR) == 0)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (step.Kind != SALOPSTEP_CREATEDIR && !ValidDiskStepPath(step.Source))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (step.Kind != SALOPSTEP_REMOVEEMPTYDIR && !ValidDiskStepPath(step.Target))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    if (g_Bridge != NULL)
    {
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    CStepBridge* bridge = new CStepBridge();
    bridge->Observer = observer;
    bridge->Plugin = Plugin;
    bridge->Flags = flags;
    bridge->Count = count;
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = StepBridgeProc;
    wc.hInstance = HInstance;
    wc.lpszClassName = "SalReorgStepBridge";
    RegisterClass(&wc);
    bridge->Notify = CreateWindowEx(0, wc.lpszClassName, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, HInstance, NULL);
    if (bridge->Notify == NULL)
    {
        delete bridge;
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    SetWindowLongPtr(bridge->Notify, GWLP_USERDATA, (LONG_PTR)bridge);
    char* subject = DupStr(caption != NULL ? caption : "Reorganize");
    COperations* script = new COperations(10, 400, subject, NULL, NULL);
    script->StepBridge = bridge;
    script->IsCopyOperation = FALSE;
    script->IsCopyOrMoveOperation = TRUE;
    // Plan moves keep the same metadata the Move command preserves when those options are on.
    script->CopySecurity = TRUE;
    script->CopyAttrs = TRUE;
    for (int i = 0; i < count; ++i)
    {
        const CSalamanderOperationStep& step = steps[i];
        COperation op;
        memset(&op, 0, sizeof(op));
        op.PlanStepIndex = i;
        op.OpFlags = OPFL_PLAN_STEP;
        op.ExpectedIdentityValid = step.ExpectedIdentity.Valid != 0;
        op.ExpectedVolumeSerial = step.ExpectedIdentity.VolumeSerial;
        memcpy(op.ExpectedFileId, step.ExpectedIdentity.FileId, 16);
        op.ExpectedSize = step.ExpectedIdentity.Size;
        op.ExpectedLastWrite = step.ExpectedIdentity.LastWrite;
        op.PlanMetadataLossAccepted = (step.Flags & SALOPSTEPF_METADATA_LOSS_ACCEPTED) != 0;
        op.ExpectedMetadataLosses = step.ExpectedMetadataLosses;
        if (op.PlanMetadataLossAccepted)
            script->PlannedMetadataLosses |= SalLossesToHost(step.ExpectedMetadataLosses);
        if (step.Flags & SALOPSTEPF_TARGET_MUST_NOT_EXIST)
            op.OpFlags |= OPFL_FAIL_IF_TARGET_EXISTS;
        if ((step.Flags & SALOPSTEPF_ALLOW_CROSS_VOLUME) == 0 && step.Kind == SALOPSTEP_MOVE)
            op.OpFlags |= OPFL_NO_CROSS_VOLUME;
        if (step.Flags & SALOPSTEPF_VERIFY_SOURCE_IDENTITY)
            op.OpFlags |= OPFL_VERIFY_SOURCE_IDENTITY;
        if (step.Flags & SALOPSTEPF_CREATEDIR_ACCEPT_EXISTING)
            op.OpFlags |= OPFL_CREATEDIR_ACCEPT_EXISTING;
        if (step.Kind == SALOPSTEP_CREATEDIR)
            op.Opcode = ocCreateDir;
        else if (step.Kind == SALOPSTEP_MOVE)
            op.Opcode = (step.Flags & SALOPSTEPF_SOURCE_IS_DIR) ? ocMoveDir : ocMoveFile;
        else if (step.Kind == SALOPSTEP_REMOVEEMPTYDIR)
            op.Opcode = ocDeleteDir;
        else
            op.Opcode = ocCopyDirTime;
        op.SourceName = DupUtf8FromWide(step.Source);
        op.TargetName = DupUtf8FromWide(step.Target);
        op.Size = CQuadWord(1, 0);
        script->Add(op);
        script->TotalSize += op.Size;
    }
    lstrcpyn(bridge->OperationId, script->GetCorrelationId(), (int)_countof(bridge->OperationId));
    lstrcpyn(operationIdBuf, bridge->OperationId, operationIdBufSize);
    g_Bridge = bridge;
    if (!StartProgressDialog(script, caption != NULL ? caption : "Reorganize", NULL, NULL))
    {
        g_Bridge = NULL;
        script->StepBridge = NULL;
        FreeScript(script);
        delete bridge;
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }
    (void)parent;
    return TRUE;
}

void WINAPI CSalamanderGeneral::EnumApplicationPathReferences(SalEnumPathReferenceCallback callback, void* param)
{
    CALL_STACK_MESSAGE1("CSalamanderGeneral::EnumApplicationPathReferences()");
    if (callback == NULL || MainWindow == NULL)
        return;
    char name[MAX_PATH];
    char path[MAX_PATH];
    for (int i = 0; i < HOT_PATHS_COUNT; ++i)
    {
        MainWindow->HotPaths.GetPath(i, path, MAX_PATH);
        if (path[0] == 0)
            continue;
        MainWindow->HotPaths.GetName(i, name, MAX_PATH);
        if (!callback(SALPATHREF_HOTPATH, i, name, path, param))
            return;
    }
    if (MainWindow->UserMenuItems != NULL)
    {
        for (int i = 0; i < MainWindow->UserMenuItems->Count; ++i)
        {
            CUserMenuItem* item = MainWindow->UserMenuItems->At(i);
            if (item->InitDir != NULL && item->InitDir[0] != 0)
            {
                if (!callback(SALPATHREF_USERMENU_DIR, i, item->ItemName, item->InitDir, param))
                    return;
            }
            if (item->Arguments != NULL && item->Arguments[0] != 0)
            {
                if (!callback(SALPATHREF_USERMENU_ARGS, i, item->ItemName, item->Arguments, param))
                    return;
            }
        }
    }
    if (MainWindow->LeftPanel != NULL && !callback(SALPATHREF_PANEL, 0, "Left", MainWindow->LeftPanel->GetPath(), param))
        return;
    if (MainWindow->RightPanel != NULL)
        callback(SALPATHREF_PANEL, 1, "Right", MainWindow->RightPanel->GetPath(), param);
}

BOOL WINAPI CSalamanderGeneral::GetApplicationDataDirectory(char* buf, int bufSize, BOOL create)
{
    // The plug-in must not resolve %APPDATA% itself: this path follows the UI-test sandbox.
    if (buf == NULL || bufSize <= 1)
        return FALSE;
    if (create)
    {
        if (!CreateOurPathInRoamingAPPDATA(buf, bufSize))
            return FALSE;
    }
    else if (!GetOurPathInRoamingAPPDATA(buf, bufSize))
        return FALSE;
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\\' || buf[len - 1] == '/'))
    {
        buf[len - 1] = 0;
        --len;
    }
    return TRUE;
}
