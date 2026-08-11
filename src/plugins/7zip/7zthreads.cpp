// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "7zip.h"
#include "7zclient.h"

#include "7zthreads.h"
#include "dialogs.h"
#include "7zip.rh"
#include "7zip.rh2"
#include "lang\lang.rh"
#include "..\shared\plugin_thread_owner.h"

WNDPROC OldProgressDlgProc;

CSalamanderForOperationsAbstract* Salamander;

// The completion notification is private to the temporary progress-dialog
// subclass, so it cannot collide with the host's progress-dialog protocol.
const WPARAM WM_7ZIP_TASKCOMPLETE = WM_7ZIP_PASSWORD + 1;
const DWORD SEVEN_ZIP_TASK_PUMP_WAIT = 250;
const DWORD SEVEN_ZIP_TASK_CANCEL_DEADLINE = 5000;

class C7ZipTaskOperation;
static C7ZipTaskOperation* Active7ZipTaskOperation = NULL;

struct C7ZipTaskCompletion
{
    C7ZipTaskOperation* Operation;
    HRESULT Result;
};

// This object owns the worker and retains the caller's arguments until the
// UI has consumed the posted result, preventing a late archive callback from
// observing state released by LaunchAndDo7ZipTask.
class C7ZipTaskOperation
{
public:
    C7ZipTaskOperation(HWND progressWindow, LPTHREAD_START_ROUTINE threadProc, LPVOID arguments)
        : ProgressWindow(progressWindow), ThreadProc(threadProc), Arguments(arguments),
          CancellationRequested(FALSE), CompletionDelivered(FALSE), CompletionPostFailed(FALSE),
          CancellationDeadlineReported(FALSE), CancellationStartedAt(0), Result(E_FAIL)
    {
    }

    BOOL Start()
    {
        return Worker.Start(WorkerProc, this, "7-Zip archive task", NULL, 0);
    }

    void RequestCancellation()
    {
        if (InterlockedExchange(&CancellationRequested, TRUE) == FALSE)
        {
            CancellationStartedAt = GetTickCount64();
            Worker.RequestStop();
        }
    }

    BOOL IsCancellationRequested() const
    {
        return InterlockedCompareExchange((volatile LONG*)&CancellationRequested, FALSE, FALSE) != FALSE;
    }

    void DeliverCompletion(HRESULT result)
    {
        Result = result;
        InterlockedExchange(&CompletionDelivered, TRUE);
    }

    BOOL HasCompletion() const
    {
        return InterlockedCompareExchange((volatile LONG*)&CompletionDelivered, FALSE, FALSE) != FALSE;
    }

    BOOL WorkerFinished() const
    {
        return Worker.WaitForCompletion(0) != WAIT_TIMEOUT;
    }

    HRESULT GetResult() const
    {
        return Result;
    }

    BOOL CompletionCouldNotBePosted() const
    {
        return InterlockedCompareExchange((volatile LONG*)&CompletionPostFailed, FALSE, FALSE) != FALSE;
    }

    void ReportCancellationDeadline()
    {
        if (!IsCancellationRequested() ||
            GetTickCount64() - CancellationStartedAt < SEVEN_ZIP_TASK_CANCEL_DEADLINE ||
            InterlockedExchange(&CancellationDeadlineReported, TRUE) != FALSE)
        {
            return;
        }

        TRACE_E("7-Zip archive task did not stop within " << SEVEN_ZIP_TASK_CANCEL_DEADLINE
                                                            << " ms; continuing to pump until its owned completion arrives.");
    }

private:
    static DWORD WINAPI WorkerProc(void* parameter, HANDLE stopEvent)
    {
        C7ZipTaskOperation* operation = (C7ZipTaskOperation*)parameter;
        HRESULT result = E_ABORT;

        if (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0)
        {
            try
            {
                result = (HRESULT)operation->ThreadProc(operation->Arguments);
            }
            catch (...)
            {
                result = E_UNEXPECTED;
            }
        }
        if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0 && SUCCEEDED(result))
            result = E_ABORT;

        operation->Result = result;
        C7ZipTaskCompletion* completion = new C7ZipTaskCompletion;
        if (completion == NULL)
        {
            InterlockedExchange(&operation->CompletionPostFailed, TRUE);
            return result;
        }

        completion->Operation = operation;
        completion->Result = result;
        if (!PostMessage(operation->ProgressWindow, WM_7ZIP, WM_7ZIP_TASKCOMPLETE, (LPARAM)completion))
        {
            delete completion;
            InterlockedExchange(&operation->CompletionPostFailed, TRUE);
        }
        return result;
    }

private:
    HWND ProgressWindow;
    LPTHREAD_START_ROUTINE ThreadProc;
    LPVOID Arguments;
    CPluginThreadOwner Worker;
    volatile LONG CancellationRequested;
    volatile LONG CompletionDelivered;
    volatile LONG CompletionPostFailed;
    volatile LONG CancellationDeadlineReported;
    ULONGLONG CancellationStartedAt;
    HRESULT Result;
};

BOOL CALLBACK SubClassedProgressDlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_COMMAND && LOWORD(wParam) == IDCANCEL && Active7ZipTaskOperation != NULL)
    {
        // Preserve the dialog's cancel behavior while also waking the owned worker.
        Active7ZipTaskOperation->RequestCancellation();
    }

    if (WM_7ZIP == uMsg)
    {
        switch (wParam)
        {
        case WM_7ZIP_PROGRESS:
            if (Active7ZipTaskOperation != NULL && Active7ZipTaskOperation->IsCancellationRequested())
                return E_ABORT;
            if (!Salamander->ProgressSetSize(*(CQuadWord*)lParam, CQuadWord(-1, -1), TRUE))
            { // Canceled by the user
                if (Active7ZipTaskOperation != NULL)
                    Active7ZipTaskOperation->RequestCancellation();
                Salamander->ProgressDialogAddText(LoadStr(IDS_CANCELING_OPERATION), FALSE);
                Salamander->ProgressEnableCancel(FALSE);
                return E_ABORT;
            }
            return S_OK;

        case WM_7ZIP_TASKCOMPLETE:
        {
            C7ZipTaskCompletion* completion = (C7ZipTaskCompletion*)lParam;
            if (completion != NULL)
            {
                if (completion->Operation == Active7ZipTaskOperation)
                    Active7ZipTaskOperation->DeliverCompletion(completion->Result);
                delete completion;
            }
            return S_OK;
        }

        case WM_7ZIP_SETTOTAL:
            Salamander->ProgressSetTotalSize(*(CQuadWord*)lParam, CQuadWord(-1, -1));
            return S_OK;

        case WM_7ZIP_ADDTEXT:
            Salamander->ProgressDialogAddText((char*)lParam, TRUE); // delayed paint, to avoid slow down by frequent refresh
            return S_OK;

        case WM_7ZIP_CREATEFILE:
        {
            // Ask whether to overwrite existing file
            CCreateFileParams* cfp = (CCreateFileParams*)lParam;

            HANDLE file = SalamanderSafeFile->SafeFileCreate(cfp->FileName, GENERIC_WRITE, FILE_SHARE_READ,
                                                             FILE_ATTRIBUTE_NORMAL, FALSE, hWnd, cfp->Name,
                                                             cfp->FileInfo, cfp->pSilent, TRUE, cfp->pSkip, NULL, 0, NULL, NULL);

            if (file == INVALID_HANDLE_VALUE)
            {
                return 1;
            }
            ::CloseHandle(file);
            return 0;
        }

        case WM_7ZIP_SHOWMBOXEX:
            return SalamanderGeneral->SalMessageBoxEx((MSGBOXEX_PARAMS*)lParam);

        case WM_7ZIP_DIALOGERROR:
        {
            CDialogErrorParams* dep = (CDialogErrorParams*)lParam;

            return SalamanderGeneral->DialogError(hWnd, dep->Flags, dep->FileName,
                                                  dep->Error, LoadStr(IDS_PACK_UPDATE_ERROR));
        }

        case WM_7ZIP_PASSWORD:
        {
            CEnterPasswordDialog dlg(hWnd);
            int res = (int)dlg.Execute();

            if (IDOK == res)
                strcpy((char*)lParam, dlg.GetPassword());
            return res;
        }
        }

        return 0;
    }

    return (BOOL)CallWindowProc(OldProgressDlgProc, hWnd, uMsg, wParam, lParam);
}

// Restoring the original procedure is mandatory even when worker startup or a
// later wait fails; otherwise subsequent progress dialogs call a stale hook.
class CProgressDialogSubclassScope
{
public:
    CProgressDialogSubclassScope(HWND window, C7ZipTaskOperation* operation)
        : Window(window), Operation(operation), PreviousProcedure(NULL), SavedOldProcedure(NULL),
          PreviousOperation(NULL), Installed(FALSE)
    {
    }

    BOOL Install()
    {
        SetLastError(ERROR_SUCCESS);
        PreviousProcedure = (WNDPROC)GetWindowLongPtr(Window, GWLP_WNDPROC);
        if (PreviousProcedure == NULL && GetLastError() != ERROR_SUCCESS)
            return FALSE;

        SavedOldProcedure = OldProgressDlgProc;
        PreviousOperation = Active7ZipTaskOperation;
        OldProgressDlgProc = PreviousProcedure;
        Active7ZipTaskOperation = Operation;

        SetLastError(ERROR_SUCCESS);
        SetWindowLongPtr(Window, GWLP_WNDPROC, (LONG_PTR)SubClassedProgressDlgProc);
        if (GetLastError() != ERROR_SUCCESS)
        {
            OldProgressDlgProc = SavedOldProcedure;
            Active7ZipTaskOperation = PreviousOperation;
            return FALSE;
        }

        Installed = TRUE;
        return TRUE;
    }

    ~CProgressDialogSubclassScope()
    {
        if (Installed && IsWindow(Window))
            SetWindowLongPtr(Window, GWLP_WNDPROC, (LONG_PTR)PreviousProcedure);
        Active7ZipTaskOperation = PreviousOperation;
        OldProgressDlgProc = SavedOldProcedure;
    }

private:
    HWND Window;
    C7ZipTaskOperation* Operation;
    WNDPROC PreviousProcedure;
    WNDPROC SavedOldProcedure;
    C7ZipTaskOperation* PreviousOperation;
    BOOL Installed;
};

//
// worker threads
//

unsigned WINAPI DecompressThreadProcBody(LPVOID lpParameter)
{
    CDecompressParamObject* dpo = (CDecompressParamObject*)lpParameter;

    HRESULT result = (dpo->Archive)->Extract(dpo->FileIndex, dpo->Count, BoolToInt(dpo->Test), dpo->Callback);

    return result;
}

DWORD WINAPI DecompressThreadProc(LPVOID lpParameter)
{
    return SalamanderDebug->CallWithCallStack(DecompressThreadProcBody, lpParameter);
}

HRESULT LaunchAndDo7ZipTask(LPTHREAD_START_ROUTINE threadProc, LPVOID args)
{
    HWND progressWindow = Salamander->ProgressGetHWND();
    C7ZipTaskOperation operation(progressWindow, threadProc, args);
    CProgressDialogSubclassScope subclassScope(progressWindow, &operation);

    if (!subclassScope.Install())
        return GetLastError();
    if (!operation.Start())
        return GetLastError();

    // Do not release the caller's archive arguments until both the UI payload
    // and the owner's completion signal confirm that no worker can use them.
    while (!operation.HasCompletion() || !operation.WorkerFinished())
    {
        DWORD waitResult = MsgWaitForMultipleObjects(0, NULL, FALSE, SEVEN_ZIP_TASK_PUMP_WAIT,
                                                      QS_ALLEVENTS | QS_SENDMESSAGE);
        if (waitResult == WAIT_FAILED)
            return HRESULT_FROM_WIN32(GetLastError());

        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!operation.HasCompletion() && operation.WorkerFinished() && operation.CompletionCouldNotBePosted())
        {
            // A destroyed dialog cannot accept the payload, but the owner has
            // already retained the final result and safe worker lifetime.
            TRACE_E("7-Zip archive task could not post its completion to the progress dialog.");
            return operation.GetResult();
        }

        operation.ReportCancellationDeadline();
    }

    return operation.GetResult();
}

HRESULT DoDecompress(CSalamanderForOperationsAbstract* salamander, CDecompressParamObject* dpo)
{
    Salamander = salamander;
    HRESULT result = LaunchAndDo7ZipTask(DecompressThreadProc, dpo);

    if (result == E_STOPEXTRACTION)
    {
        result = S_OK;
    }
    if ((result != E_ABORT) && (result != S_OK))
    {
        int label = dpo->Test ? IDS_TEST_ERROR : IDS_UNPACK_ERROR;

        if (FAILED(result) && ((FACILITY_WIN32 << 16) == (result & 0x7FFF0000)))
        {
            // LastError error encoded into HRESULT
            // There is something strange: E_OUTOFMEMORY as 0x8007000EL prints as "Not enough storage is available to complete this operation"
            // even when not truncated to 16 bits while 0x80000002L prints as "Ran out of memory"
            SysError(label, (result == E_OUTOFMEMORY) ? 0x80000002L : (result & 0xFFFF), FALSE);
        }
        else
        {
            Error(label, FALSE, result);
        }
        result = E_ABORT; // Do not display any other dialog
    }
    return result;
}

unsigned WINAPI UpdateThreadProcBody(LPVOID lpParameter)
{
    CUpdateParamObject* upo = (CUpdateParamObject*)lpParameter;

    HRESULT result = (upo->Archive)->UpdateItems(upo->Stream, upo->Count, upo->Callback);

    return result;
}

DWORD WINAPI UpdateThreadProc(LPVOID lpParameter)
{
    return SalamanderDebug->CallWithCallStack(UpdateThreadProcBody, lpParameter);
}

HRESULT DoUpdate(CSalamanderForOperationsAbstract* salamander, CUpdateParamObject* upo)
{
    Salamander = salamander;

    return LaunchAndDo7ZipTask(UpdateThreadProc, upo);
}

////////////////////////////////

// AddCallStackObject gets called by our code embedded in 7za.dll

struct AddCallStackObjectParam
{
    unsigned int(__stdcall* StartAddress)(void*);
    LPVOID Parameter;
};

unsigned __stdcall AddCallStackObject(void* param)
{
    AddCallStackObjectParam* p = (AddCallStackObjectParam*)param;
    return SalamanderDebug->CallWithCallStack(p->StartAddress, p->Parameter);
}
