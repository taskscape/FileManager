// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

// Recycle-bin delete path extracted from async_copy.cpp: IFileOperation is STA-only,
// so the delete runs behind CThreadOwner on a dedicated STA thread isolated from the
// MTA copy worker. Exposed to async_copy.cpp via async_copy_internals.h.
struct CRecycleBinDeleteRequest
{
    HWND Owner;
    const char* Path;
    DWORD Error;
};

static DWORD GetFileOperationError(HRESULT result)
{
    if (SUCCEEDED(result))
        return ERROR_SUCCESS;
    if (result == E_OUTOFMEMORY)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (HRESULT_FACILITY(result) == FACILITY_WIN32)
        return HRESULT_CODE(result);
    // The copy engine reports a locked file with its own code rather than a
    // wrapped Win32 error, and the error dialog has no text for it.
    if (result == COPYENGINE_E_SHARING_VIOLATION_SRC ||
        result == COPYENGINE_E_SHARING_VIOLATION_DEST)
        return ERROR_SHARING_VIOLATION;
    return ERROR_GEN_FAILURE;
}

// Captures the per-item result of a shell delete. Without a sink the only
// failure signal IFileOperation offers is "some operation was aborted", which
// would report every cause as a cancellation.
class CRecycleBinDeleteSink : public IFileOperationProgressSink
{
public:
    CRecycleBinDeleteSink() : RefCount(1), ItemResult(S_OK) {}

    HRESULT GetItemResult() const { return ItemResult; }

    STDMETHOD(QueryInterface)(REFIID riid, void** object)
    {
        if (object == NULL)
            return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(IFileOperationProgressSink))
        {
            *object = static_cast<IFileOperationProgressSink*>(this);
            AddRef();
            return S_OK;
        }
        *object = NULL;
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() { return InterlockedIncrement(&RefCount); }

    STDMETHOD_(ULONG, Release)()
    {
        LONG count = InterlockedDecrement(&RefCount);
        if (count == 0)
            delete this;
        return count;
    }

    STDMETHOD(PostDeleteItem)(DWORD, IShellItem*, HRESULT hrDelete, IShellItem*)
    {
        ItemResult = hrDelete;
        return S_OK;
    }

    // A single-item delete uses none of the remaining notifications.
    STDMETHOD(StartOperations)() { return S_OK; }
    STDMETHOD(FinishOperations)(HRESULT) { return S_OK; }
    STDMETHOD(PreRenameItem)(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    STDMETHOD(PostRenameItem)(DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    STDMETHOD(PreMoveItem)(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    STDMETHOD(PostMoveItem)(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    STDMETHOD(PreCopyItem)(DWORD, IShellItem*, IShellItem*, LPCWSTR) { return S_OK; }
    STDMETHOD(PostCopyItem)(DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT, IShellItem*) { return S_OK; }
    STDMETHOD(PreDeleteItem)(DWORD, IShellItem*) { return S_OK; }
    STDMETHOD(PreNewItem)(DWORD, IShellItem*, LPCWSTR) { return S_OK; }
    STDMETHOD(PostNewItem)(DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT, IShellItem*) { return S_OK; }
    STDMETHOD(UpdateProgress)(UINT, UINT) { return S_OK; }
    STDMETHOD(ResetTimer)() { return S_OK; }
    STDMETHOD(PauseTimer)() { return S_OK; }
    STDMETHOD(ResumeTimer)() { return S_OK; }

private:
    ~CRecycleBinDeleteSink() {}

    LONG RefCount;
    HRESULT ItemResult;
};

static DWORD WINAPI RunRecycleBinDeleteOnSta(void* parameter, HANDLE stopEvent)
{
    // IFileOperation is an STA-only Shell API; this executor isolates it from the MTA copy worker.
    UNREFERENCED_PARAMETER(stopEvent);
    CRecycleBinDeleteRequest* request = (CRecycleBinDeleteRequest*)parameter;
    request->Error = ERROR_GEN_FAILURE;

    CStrP pathW(ConvertAllocUtf8ToWide(request->Path, -1));
    if (pathW == NULL)
    {
        request->Error = ERROR_NO_UNICODE_TRANSLATION;
        return request->Error;
    }

    IFileOperation* operation = NULL;
    IShellItem* item = NULL;
    CRecycleBinDeleteSink* sink = new CRecycleBinDeleteSink();
    HRESULT result = sink == NULL ? E_OUTOFMEMORY : S_OK;
    if (SUCCEEDED(result))
        result = CoCreateInstance(CLSID_FileOperation, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&operation));
    if (SUCCEEDED(result))
        result = operation->SetOwnerWindow(request->Owner);
    if (SUCCEEDED(result))
    {
        // FOF_NOERRORUI as well as FOF_SILENT: FOF_SILENT hides only the progress
        // dialog, so a locked file used to raise the shell's own "File In Use"
        // window. That took the failure out of the operation's Retry/Skip/Skip
        // All contract and blocked the worker on a modal the application does
        // not own.
        result = operation->SetOperationFlags(FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION |
                                              FOF_NOERRORUI);
    }
    if (SUCCEEDED(result))
        result = SHCreateItemFromParsingName(pathW, NULL, IID_PPV_ARGS(&item));
    if (SUCCEEDED(result))
        result = operation->DeleteItem(item, sink);
    if (SUCCEEDED(result))
        result = operation->PerformOperations();
    if (SUCCEEDED(result))
    {
        // PerformOperations reports the batch, not the item, so a delete that
        // failed still succeeds here; the sink carries the real reason.
        HRESULT itemResult = sink->GetItemResult();
        BOOL aborted = FALSE;
        if (FAILED(itemResult))
            result = itemResult;
        else if (SUCCEEDED(operation->GetAnyOperationsAborted(&aborted)) && aborted)
            result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    }
    if (item != NULL)
        item->Release();
    if (operation != NULL)
        operation->Release();
    if (sink != NULL)
        sink->Release();
    request->Error = GetFileOperationError(result);
    return request->Error;
}

// Non-static on purpose: callers live in async_copy.cpp (declaration in
// async_copy_internals.h), so this must keep external linkage.
BOOL DeleteThroughRecycleBin(HWND owner, const char* path, DWORD* error)
{
    CRecycleBinDeleteRequest request;
    request.Owner = owner;
    request.Path = path;
    request.Error = ERROR_GEN_FAILURE;
    CThreadOwner executor;
    // The owner initializes an STA before invoking the executor and keeps the stack request alive until it completes.
    if (!executor.Start(RunRecycleBinDeleteOnSta, &request, "recycle bin operation", TRUE))
    {
        *error = GetLastError();
        return FALSE;
    }
    DWORD completed = executor.WaitForCompletion(INFINITE);
    if (completed != WAIT_OBJECT_0)
    {
        *error = completed == WAIT_FAILED ? GetLastError() : ERROR_GEN_FAILURE;
        return FALSE;
    }
    *error = request.Error;
    return request.Error == ERROR_SUCCESS;
}
