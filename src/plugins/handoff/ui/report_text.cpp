// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "report_text.h"
#include "sdk_strings.h"
#include "text_util.h"
#include "ui_thread.h"
#include "verify.h"

namespace
{

template <class T> struct ComPtr
{
    T* Ptr = nullptr;
    ~ComPtr()
    {
        if (Ptr != nullptr)
            Ptr->Release();
    }
    T** Out() { return &Ptr; }
    T* operator->() const { return Ptr; }
};

void SetInitialFolder(IFileDialog* dialog, const std::wstring& folder)
{
    if (folder.empty())
        return;
    ComPtr<IShellItem> item;
    if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), NULL, IID_PPV_ARGS(item.Out()))))
        dialog->SetFolder(item.Ptr);
}

bool ResultPath(IFileDialog* dialog, std::wstring& path)
{
    ComPtr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.Out())))
        return false;
    PWSTR name = NULL;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &name)) || name == NULL)
        return false;
    path = name;
    CoTaskMemFree(name);
    return !path.empty();
}

bool ShowDialog(IFileDialog* dialog, HWND owner)
{
    // Unloading the plug-in dismisses the dialog instead of waiting for the user.
    PromptScope scope;
    return SUCCEEDED(dialog->Show(owner));
}

} // namespace

bool BrowseOpenFile(HWND owner, int titleId, int filterNameId, const wchar_t* pattern, const std::wstring& initialFolder,
                    std::wstring& path)
{
    if (ShuttingDown())
        return false;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.Out()))))
        return false;
    std::wstring title = Text(titleId);
    std::wstring filterName = Text(filterNameId);
    std::wstring allName = Text(IDS_SPEC_FILTER_ALL);
    COMDLG_FILTERSPEC filters[2] = {{filterName.c_str(), pattern}, {allName.c_str(), L"*.*"}};
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    SetInitialFolder(dialog.Ptr, initialFolder);
    return ShowDialog(dialog.Ptr, owner) && ResultPath(dialog.Ptr, path);
}

bool BrowseFolder(HWND owner, int titleId, const std::wstring& initialFolder, std::wstring& path)
{
    if (ShuttingDown())
        return false;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.Out()))))
        return false;
    std::wstring title = Text(titleId);
    dialog->SetTitle(title.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    SetInitialFolder(dialog.Ptr, initialFolder);
    return ShowDialog(dialog.Ptr, owner) && ResultPath(dialog.Ptr, path);
}

bool BrowseSaveFile(HWND owner, int filterNameId, const wchar_t* pattern, const wchar_t* extension,
                    const std::wstring& defaultName, const std::wstring& initialFolder, std::wstring& path)
{
    if (ShuttingDown())
        return false;
    ComPtr<IFileSaveDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(dialog.Out()))))
        return false;
    std::wstring filterName = Text(filterNameId);
    COMDLG_FILTERSPEC filters[1] = {{filterName.c_str(), pattern}};
    dialog->SetFileTypes(1, filters);
    dialog->SetDefaultExtension(extension);
    dialog->SetFileName(defaultName.c_str());
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
    SetInitialFolder(dialog.Ptr, initialFolder);
    return ShowDialog(dialog.Ptr, owner) && ResultPath(dialog.Ptr, path);
}

void SaveReport(HWND owner, const std::wstring& defaultName, const std::string& bytes, const std::wstring& forbiddenRoot)
{
    std::wstring path;
    if (!BrowseSaveFile(owner, IDS_FILTER_TEXT, L"*.txt", L"txt", defaultName, std::wstring(), path))
        return;
    std::wstring full = handoff::FullPathOf(path);
    if (!forbiddenRoot.empty() && handoff::IsPathInsideOrEqual(handoff::FullPathOf(forbiddenRoot), full))
    {
        PromptBox(owner, Text(IDS_REPORT_INSIDE_PACKAGE), MB_OK | MB_ICONWARNING);
        return;
    }
    // The save dialog already confirmed replacing an existing report file.
    HANDLE file = CreateFileW(handoff::LongPath(full).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD error = ERROR_SUCCESS;
    if (file == INVALID_HANDLE_VALUE)
        error = GetLastError();
    else
    {
        DWORD written = 0;
        if (!WriteFile(file, bytes.data(), (DWORD)bytes.size(), &written, NULL) || written != bytes.size())
            error = GetLastError() != ERROR_SUCCESS ? GetLastError() : ERROR_WRITE_FAULT;
        if (!CloseHandle(file) && error == ERROR_SUCCESS)
            error = GetLastError();
    }
    if (error != ERROR_SUCCESS)
        PromptBox(owner, TextF(IDS_REPORT_SAVE_FAILED, {full, handoff::Win32ErrorText(error)}), MB_OK | MB_ICONERROR);
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner))
        return false;
    bool copied = false;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != NULL)
    {
        void* data = GlobalLock(memory);
        if (data != NULL)
        {
            memcpy(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            copied = SetClipboardData(CF_UNICODETEXT, memory) != NULL;
        }
        if (!copied)
            GlobalFree(memory); // ownership passes to the clipboard only on success
    }
    CloseClipboard();
    return copied;
}

std::vector<std::wstring> FindingLines(const std::vector<handoff::Finding>& findings)
{
    std::vector<std::wstring> lines;
    for (const handoff::Finding& finding : findings)
        lines.push_back(handoff::FindingLine(finding, Catalog()));
    return lines;
}
