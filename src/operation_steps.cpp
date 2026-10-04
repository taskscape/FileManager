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
    // The host copies the caller's steps, so it must also keep each step's UserData:
    // observers correlate callbacks with their own step objects through it.
    DWORD_PTR* UserData;

    CStepBridge()
        : Observer(NULL), Plugin(NULL), Flags(0), Count(0), Done(0), Skipped(0), Failed(0),
          Cancelled(0), NotStarted(0), UserCancelled(FALSE), FinishedPosted(FALSE), Notify(NULL),
          UserData(NULL)
    {
        OperationId[0] = 0;
        InitializeCriticalSection(&Guard);
    }

    ~CStepBridge()
    {
        if (Notify)
            DestroyWindow(Notify);
        delete[] UserData;
        DeleteCriticalSection(&Guard);
    }

    DWORD_PTR UserDataFor(int index) const
    {
        return UserData != NULL && index >= 0 && index < Count ? UserData[index] : 0;
    }

    virtual BOOL BeforeStep(int index)
    {
        if (Observer == NULL || index < 0)
            return TRUE;
        return Observer->BeforeStep(index, UserDataFor(index));
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
            Observer->AfterStep(index, UserDataFor(index), result, error, resultFlags, MapLosses(metadataLosses), NULL);
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

BOOL IsStepSlash(WCHAR ch)
{
    return ch == L'\\' || ch == L'/';
}

BOOL IsDriveAbsolute(const WCHAR* path)
{
    return ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && IsStepSlash(path[2]);
}

// Accepts "\\server\share" followed by the end of the path or a separator.
BOOL IsUncShareAbsolute(const WCHAR* afterSlashes)
{
    const WCHAR* server = afterSlashes;
    const WCHAR* p = server;
    while (*p != 0 && !IsStepSlash(*p))
        ++p;
    if (p == server || *p == 0)
        return FALSE;
    const WCHAR* share = p + 1;
    p = share;
    while (*p != 0 && !IsStepSlash(*p))
        ++p;
    return p != share;
}

// A reviewed step must name one absolute object. Drive-relative forms such as "C:foo",
// rooted-relative "\foo" and device namespaces resolve through process state instead.
BOOL ValidDiskStepPath(const WCHAR* path)
{
    if (path == NULL || path[0] == 0)
        return FALSE;
    if (IsDriveAbsolute(path))
        return TRUE;
    if (!IsStepSlash(path[0]) || !IsStepSlash(path[1]))
        return FALSE;
    if (path[2] == L'?' && path[3] == L'\\')
    {
        const WCHAR* rest = path + 4;
        if (IsDriveAbsolute(rest))
            return TRUE;
        return _wcsnicmp(rest, L"UNC\\", 4) == 0 && IsUncShareAbsolute(rest + 4);
    }
    if (path[2] == L'.' || path[2] == L'?')
        return FALSE;
    return IsUncShareAbsolute(path + 2);
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
        // Without FileIdInfo the 64-bit file index is the only identity. It is laid out as the
        // plug-in probe stores it (low DWORD, high DWORD, zero padding) so a replacement object,
        // including a directory that skips the size/time check below, cannot match.
        BYTE legacyId[16];
        memset(legacyId, 0, sizeof(legacyId));
        memcpy(legacyId, &basic.nFileIndexLow, 4);
        memcpy(legacyId + 4, &basic.nFileIndexHigh, 4);
        const BYTE* id = ok ? info.Identifier : legacyId;
        if (serial != op->ExpectedVolumeSerial || memcmp(id, op->ExpectedFileId, 16) != 0)
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

// Drive letters do not identify volumes: another volume can be mounted on a folder below
// a drive-letter path. Both paths are therefore always resolved to their mount roots.
// The mount root is a prefix of the absolute path (plus a separator, or a \\?\Volume{...}\ name),
// so a buffer sized from the path itself also serves long paths without a fixed MAX_PATH limit.
static WCHAR* AllocVolumeRoot(CWidePath& path)
{
    const WCHAR* apiPath = path.GetPathForWin32Api();
    if (apiPath == NULL)
        return NULL;
    size_t capacity = wcslen(apiPath) + 64;
    if (capacity > MAXDWORD)
        return NULL;
    WCHAR* root = (WCHAR*)malloc(capacity * sizeof(WCHAR));
    if (root != NULL && !GetVolumePathNameW(apiPath, root, (DWORD)capacity))
    {
        free(root);
        root = NULL;
    }
    return root;
}

static BOOL PlanPathsShareVolume(const char* source, const char* target)
{
    CWidePath sourcePath(source);
    CWidePath targetPath(target);
    WCHAR* sourceRoot = AllocVolumeRoot(sourcePath);
    WCHAR* targetRoot = AllocVolumeRoot(targetPath);
    BOOL same = FALSE;
    if (sourceRoot != NULL && targetRoot != NULL)
    {
        WCHAR sourceVolume[64];
        WCHAR targetVolume[64];
        if (GetVolumeNameForVolumeMountPointW(sourceRoot, sourceVolume, _countof(sourceVolume)) &&
            GetVolumeNameForVolumeMountPointW(targetRoot, targetVolume, _countof(targetVolume)))
        {
            // Local volumes have unique GUID names, which also distinguishes volumes whose serials collide.
            same = _wcsicmp(sourceVolume, targetVolume) == 0;
        }
        else
        {
            // Network shares have no volume GUID name; the serial is the best identity available.
            DWORD sourceSerial = 0;
            DWORD targetSerial = 0;
            DWORD component = 0;
            DWORD flags = 0;
            if (GetVolumeInformationW(sourceRoot, NULL, 0, &sourceSerial, &component, &flags, NULL, 0) &&
                GetVolumeInformationW(targetRoot, NULL, 0, &targetSerial, &component, &flags, NULL, 0))
                same = sourceSerial == targetSerial;
        }
    }
    free(sourceRoot);
    free(targetRoot);
    return same;
}

BOOL PlanStepCrossesVolume(const COperation* op)
{
    if (op == NULL || (op->OpFlags & OPFL_PLAN_STEP) == 0 || op->SourceName == NULL || op->TargetName == NULL)
        return FALSE;
    if (op->Opcode != ocMoveFile && op->Opcode != ocMoveDir)
        return FALSE;
    // A step that forbids crossing was already proven same-volume by its precondition.
    if ((op->OpFlags & OPFL_NO_CROSS_VOLUME) != 0)
        return FALSE;
    return !PlanPathsShareVolume(op->SourceName, op->TargetName);
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
    bridge->UserData = new DWORD_PTR[count];
    for (int i = 0; i < count; ++i)
        bridge->UserData[i] = steps[i].UserData;
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
