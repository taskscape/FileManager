// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h> // The general SDK interface exposes common-dialog structures.
#include <winhttp.h>
#include <objbase.h> // MIME boundaries use GUIDs without requiring COM initialization.
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
// This standalone plug-in owns its Win32 handles directly, without SDK debug wrappers.
#undef TRACE_ENABLE
#undef MHANDLES_ENABLE
#include "spl_com.h" // SDK common declarations must precede the plug-in and general interfaces.
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_menu.h"
#include "spl_vers.h"
#undef lstrcpynA // The standalone owner uses Win32 rather than the optional SDK string shim.
#include "plugin_thread_owner.h" // Retain the SDK stop/completion/handle ownership contract.
#include "resource.h"
#include "../handoff/engine/json.h"
#include "../handoff/engine/text_util.h"

namespace
{
HINSTANCE Instance, Language;
CSalamanderGeneralAbstract* General;
constexpr uint64_t MaxBytes = 100ULL * 1024 * 1024;
constexpr size_t MaxResponse = 64 * 1024;

std::wstring Text(int id)
{
    const wchar_t* value = nullptr;
    int length = LoadStringW(Language, id, (LPWSTR)&value, 0);
    return length > 0 ? std::wstring(value, length) : L"Infostacker";
}

std::wstring Wide(const std::string& value)
{
    std::wstring result;
    if (!handoff::Utf8ToWide(value, result)) throw Text(IDS_ENCODING);
    return result;
}

struct File
{
    HANDLE Handle = INVALID_HANDLE_VALUE;
    uint64_t Size = 0;
    std::wstring Name;
    explicit File(const std::wstring& path)
    {
        // Deny writers and renames for the whole request so length and uploaded bytes agree.
        Handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        LARGE_INTEGER size;
        BY_HANDLE_FILE_INFORMATION info;
        if (Handle == INVALID_HANDLE_VALUE || !GetFileSizeEx(Handle, &size) ||
            !GetFileInformationByHandle(Handle, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle);
            Handle = INVALID_HANDLE_VALUE;
            throw Text(IDS_READ) + path;
        }
        Size = size.QuadPart;
        Name = std::filesystem::path(path).filename().wstring();
    }
    ~File() { if (Handle != INVALID_HANDLE_VALUE) CloseHandle(Handle); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

struct Internet
{
    HINTERNET Handle;
    explicit Internet(HINTERNET handle) : Handle(handle)
    {
        if (!Handle) throw std::wstring(L"WinHTTP: ") + std::to_wstring(GetLastError());
    }
    ~Internet() { WinHttpCloseHandle(Handle); }
    operator HINTERNET() const { return Handle; }
};

void Check(BOOL result)
{
    if (!result) throw std::wstring(L"WinHTTP: ") + std::to_wstring(GetLastError());
}

struct Publication
{
    std::wstring Path, Url, Error;
    std::atomic<bool> Cancel{false}, Done{false}, Waiting{false};
    std::atomic<unsigned> Percent{0};
    CPluginThreadOwner Worker;
    HANDLE StopEvent = nullptr;
    bool Running = false;
    ULONGLONG Started = 0;
    void CheckCancelled() const
    {
        if (Cancel || (StopEvent && WaitForSingleObject(StopEvent, 0) == WAIT_OBJECT_0)) throw Text(IDS_CANCELLED);
        if (GetTickCount64() - Started > 120000) throw std::wstring(L"WinHTTP: timeout (120 s)");
    }
};

std::wstring FinalPath(HANDLE handle)
{
    DWORD length = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    std::wstring path(length, L'\0');
    if (!length || GetFinalPathNameByHandleW(handle, &path[0], length, FILE_NAME_NORMALIZED) >= length)
        throw Text(IDS_READ);
    path.resize(wcslen(path.c_str()));
    return path;
}

std::string ReadMarkdown(File& file, Publication& job)
{
    if (file.Size > MaxBytes) throw Text(IDS_SIZE);
    std::string source;
    char buffer[64 * 1024];
    for (;;)
    {
        job.CheckCancelled();
        DWORD count = 0;
        if (!ReadFile(file.Handle, buffer, sizeof(buffer), &count, nullptr)) throw Text(IDS_READ) + file.Name;
        if (!count) break;
        source.append(buffer, count);
    }
    // Match NoteManager's UTF-8 reader and title prefix, without transmitting a BOM.
    if (source.compare(0, 3, "\xEF\xBB\xBF") == 0) source.erase(0, 3);
    Wide(source);
    return source;
}

std::string EscapeName(const std::wstring& name)
{
    // Quoted MIME parameters must never introduce another header or part.
    std::string escaped;
    for (unsigned char c : handoff::WideToUtf8(name))
    {
        if (c == '"' || c == '\r' || c == '\n')
        {
            const char* hex = "0123456789ABCDEF";
            escaped += '%'; escaped += hex[c >> 4]; escaped += hex[c & 15];
        }
        else escaped += c;
    }
    return escaped;
}

std::string EmbedName(const std::wstring& name)
{
    // Match NoteManager's embed escaping while preserving the original MIME filename.
    std::string escaped;
    for (unsigned char c : handoff::WideToUtf8(name))
    {
        if (c == '%' || c == '#' || c == '|' || c == '[' || c == ']')
        {
            const char* hex = "0123456789ABCDEF";
            escaped += '%'; escaped += hex[c >> 4]; escaped += hex[c & 15];
        }
        else escaped += c;
    }
    return escaped;
}

std::wstring EmbedTarget(const std::string& source)
{
    // Obsidian embeds may contain URI-escaped UTF-8 filenames, as in NoteManager.
    auto hex = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string decoded;
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (source[i] == '%' && i + 2 < source.size() && hex(source[i + 1]) >= 0 && hex(source[i + 2]) >= 0)
        {
            decoded += (char)(hex(source[i + 1]) * 16 + hex(source[i + 2])); i += 2;
        }
        else decoded += source[i];
    }
    if (decoded.find('\0') != std::string::npos) throw Text(IDS_ATTACHMENT);
    return handoff::Trim(Wide(decoded));
}

std::wstring EncodeId(const std::wstring& id)
{
    std::wstring encoded;
    for (unsigned char c : handoff::WideToUtf8(id))
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') encoded += c;
        else
        {
            const wchar_t* hex = L"0123456789ABCDEF";
            encoded += L'%'; encoded += hex[c >> 4]; encoded += hex[c & 15];
        }
    }
    // Dot segments have URI semantics even though each character is unreserved.
    if (encoded == L"." || encoded == L"..") throw Text(IDS_RESPONSE);
    return encoded;
}

std::wstring BaseUrl()
{
    // The HTTP fixture override is effective only inside the existing isolated UI profile,
    // and can never redirect a normal publication to an arbitrary server.
    wchar_t isolated[8] = {}, root[256] = {}, url[256] = {};
    GetEnvironmentVariableW(L"FILEMANAGER_UI_ISOLATED", isolated, _countof(isolated));
    GetEnvironmentVariableW(L"FILEMANAGER_UI_CONFIG_ROOT", root, _countof(root));
    DWORD length = GetEnvironmentVariableW(L"FILEMANAGER_UI_INFOSTACKER_URL", url, _countof(url));
    if (wcscmp(isolated, L"1") == 0 &&
        wcscmp(root, L"Software\\File Manager\\6.0-filemanager-testdata") == 0 && length && length < _countof(url))
    {
        std::wstring value(url);
        const std::wstring prefix = L"http://127.0.0.1:";
        if (value.compare(0, prefix.size(), prefix) == 0 && value.back() == L'/' &&
            value.size() > prefix.size() + 1 &&
            value.find_first_not_of(L"0123456789", prefix.size()) == value.size() - 1)
            return value;
    }
    return L"https://shr.infostacker.com/";
}

void Publish(Publication& job)
{
    try
    {
        job.Started = GetTickCount64();
        auto selected = std::make_unique<File>(job.Path);
        if (selected->Size > MaxBytes) throw Text(IDS_SIZE);
        const auto folder = std::filesystem::path(job.Path).parent_path();
        std::vector<std::unique_ptr<File>> attachments;
        std::wstring extension = handoff::LowerInvariant(std::filesystem::path(job.Path).extension().wstring());
        std::string markdown = handoff::WideToUtf8(std::filesystem::path(job.Path).stem().wstring()) + "\n\n";
        if (extension == L".md" || extension == L".markdown")
        {
            std::string source = ReadMarkdown(*selected, job);
            markdown += source;
            // Resolve only explicit embeds inside this folder. Final handle paths also reject
            // junction/symlink escapes, and missing or ambiguous references fail before sending.
            HANDLE directory = CreateFileW(folder.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (directory == INVALID_HANDLE_VALUE) throw Text(IDS_READ) + folder.wstring();
            std::wstring root;
            try { root = FinalPath(directory) + L"\\"; }
            catch (...) { CloseHandle(directory); throw; }
            CloseHandle(directory);
            std::vector<std::wstring> included;
            size_t position = 0;
            while ((position = source.find("![[", position)) != std::string::npos)
            {
                job.CheckCancelled();
                size_t end = source.find("]]", position + 3);
                if (end == std::string::npos) break;
                auto target = EmbedTarget(source.substr(position + 3, end - position - 3));
                position = end + 2;
                size_t suffix = target.find_first_of(L"|#");
                if (suffix != std::wstring::npos) target.resize(suffix);
                target = handoff::Trim(target);
                if (target.empty()) continue;
                auto candidate = (folder / target).lexically_normal();
                if (!std::filesystem::exists(candidate) && std::filesystem::path(target).filename() == target)
                {
                    std::filesystem::path found;
                    size_t scanned = 0;
                    for (auto it = std::filesystem::recursive_directory_iterator(folder); it != std::filesystem::recursive_directory_iterator(); ++it)
                    {
                        job.CheckCancelled();
                        if (++scanned > 10000) throw Text(IDS_ATTACHMENT) + target;
                        if (GetFileAttributesW(it->path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
                        { it.disable_recursion_pending(); continue; }
                        if (!it->is_regular_file() || !handoff::EqualsNoCase(it->path().filename().wstring(), target)) continue;
                        if (!found.empty()) throw Text(IDS_ATTACHMENT) + target;
                        found = it->path();
                    }
                    if (!found.empty()) candidate = found;
                }
                auto attachment = std::make_unique<File>(candidate.wstring());
                auto actual = FinalPath(attachment->Handle);
                if (!handoff::StartsWithNoCase(actual, root)) throw Text(IDS_ATTACHMENT) + target;
                if (std::any_of(included.begin(), included.end(), [&](const std::wstring& p) { return handoff::EqualsNoCase(p, actual); })) continue;
                if (std::any_of(attachments.begin(), attachments.end(), [&](const std::unique_ptr<File>& f) { return handoff::EqualsNoCase(f->Name, attachment->Name); }))
                    throw Text(IDS_ATTACHMENT) + target;
                included.push_back(actual);
                attachments.push_back(std::move(attachment));
            }
        }
        else
        {
            // Infostacker uses Obsidian embeds to associate the original binary with its public page.
            markdown += "![[" + EmbedName(selected->Name) + "]]\n";
            attachments.push_back(std::move(selected));
        }
        uint64_t totalBytes = markdown.size();
        for (const auto& file : attachments)
        {
            totalBytes += file->Size;
            if (totalBytes > MaxBytes) throw Text(IDS_SIZE);
        }
        if (totalBytes > MaxBytes) throw Text(IDS_SIZE);

        // A fresh boundary prevents binary content from being interpreted as a MIME delimiter.
        GUID guid;
        if (FAILED(CoCreateGuid(&guid))) throw std::wstring(L"Cannot create upload boundary.");
        wchar_t guidText[40]; StringFromGUID2(guid, guidText, _countof(guidText));
        std::string boundary = "FileManager" + handoff::WideToUtf8(std::wstring(guidText + 1, 36));
        std::string head = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"markdown\"\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n" + markdown + "\r\n";
        std::vector<std::string> headers;
        uint64_t wireSize = head.size();
        for (const auto& file : attachments)
        {
            headers.push_back("--" + boundary + "\r\nContent-Disposition: form-data; name=\"files\"; filename=\"" + EscapeName(file->Name) + "\"\r\nContent-Type: application/octet-stream\r\n\r\n");
            wireSize += headers.back().size() + file->Size + 2;
        }
        std::string tail = "--" + boundary + "--\r\n";
        wireSize += tail.size();
        auto base = BaseUrl();
        URL_COMPONENTS parts = {sizeof(parts)};
        parts.dwHostNameLength = parts.dwUrlPathLength = (DWORD)-1;
        Check(WinHttpCrackUrl(base.c_str(), 0, 0, &parts));
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
        Internet session(WinHttpOpen(L"FileManager Infostacker/1.0", secure ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        Check(WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000));
        Internet connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
        Internet request(WinHttpOpenRequest(connection, L"POST", L"/sharing/uploadmarkdownwithfiles", nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
        // A redirect must not silently replay a public upload to another endpoint.
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        Check(WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)));
        std::wstring contentType = L"Content-Type: multipart/form-data; boundary=" + Wide(boundary) + L"\r\nAccept: application/json\r\n";
        job.CheckCancelled();
        Check(WinHttpSendRequest(request, contentType.c_str(), (DWORD)-1, nullptr, 0, (DWORD)wireSize, 0));
        uint64_t sent = 0;
        auto write = [&](const char* data, size_t length)
        {
            while (length)
            {
                job.CheckCancelled();
                DWORD written = 0;
                Check(WinHttpWriteData(request, data, (DWORD)std::min<size_t>(length, 64 * 1024), &written));
                if (!written) throw std::wstring(L"WinHTTP: empty write");
                data += written; length -= written; sent += written;
                job.Percent = (unsigned)(sent * 100 / wireSize);
            }
        };
        write(head.data(), head.size());
        for (size_t i = 0; i < attachments.size(); ++i)
        {
            write(headers[i].data(), headers[i].size());
            char buffer[64 * 1024];
            for (uint64_t remaining = attachments[i]->Size; remaining;)
            {
                DWORD count = 0;
                if (!ReadFile(attachments[i]->Handle, buffer, (DWORD)std::min<uint64_t>(remaining, sizeof(buffer)), &count, nullptr) || !count)
                    throw Text(IDS_READ) + attachments[i]->Name;
                write(buffer, count); remaining -= count;
            }
            write("\r\n", 2);
        }
        write(tail.data(), tail.size());
        job.Waiting = true;
        Check(WinHttpReceiveResponse(request, nullptr));
        DWORD status = 0, statusSize = sizeof(status);
        Check(WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX));
        if (status == 413) throw Text(IDS_SIZE);
        if (status < 200 || status >= 300) throw std::wstring(L"HTTP ") + std::to_wstring(status);
        std::string response;
        for (;;)
        {
            job.CheckCancelled();
            char buffer[4096]; DWORD count = 0;
            Check(WinHttpReadData(request, buffer, sizeof(buffer), &count));
            if (!count) break;
            if (response.size() + count > MaxResponse) throw Text(IDS_RESPONSE);
            response.append(buffer, count);
        }
        handoff::JsonValue result;
        handoff::JsonError error;
        const handoff::JsonLimits limits = {MaxResponse, 16, 4096, 128, 128, 32, 2048};
        if (!handoff::ParseJson((const unsigned char*)response.data(), response.size(), limits, result, error)) throw Text(IDS_RESPONSE);
        const auto* id = result.Find(L"id");
        if (!id || !id->IsString() || handoff::Trim(id->String).empty()) throw Text(IDS_RESPONSE);
        job.Url = base + L"sharing/" + EncodeId(id->String);
    }
    catch (const std::wstring& error) { job.Error = job.Cancel ? Text(IDS_CANCELLED) : Text(IDS_ERROR) + error; }
    catch (const std::exception&) { job.Error = Text(IDS_ERROR) + Text(IDS_READ); }
    catch (...) { job.Error = Text(IDS_ERROR); }
    // Publish final strings before the UI reads them and joins the owned worker.
    job.Done = true;
}

bool CopyUrl(HWND owner, const std::wstring& url)
{
    size_t bytes = (url.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return false;
    void* target = GlobalLock(memory);
    if (!target) { GlobalFree(memory); return false; }
    memcpy(target, url.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) { GlobalFree(memory); return false; }
    bool copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    if (!copied) GlobalFree(memory);
    return copied;
}

DWORD WINAPI PublishWorker(void* parameter, HANDLE stopEvent)
{
    auto& job = *static_cast<Publication*>(parameter);
    job.StopEvent = stopEvent;
    SetThreadDescription(GetCurrentThread(), L"Infostacker publisher");
    Publish(job);
    return 0;
}

INT_PTR CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* job = reinterpret_cast<Publication*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG)
    {
        job = reinterpret_cast<Publication*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        SetDlgItemTextW(window, IDC_FILE, job->Path.c_str());
        SetDlgItemTextW(window, IDC_STATUS, Text(IDS_READY).c_str());
        EnableWindow(GetDlgItem(window, IDC_COPY), FALSE);
        SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETRANGE32, 0, 100);
        RECT parent, rect; GetWindowRect(GetParent(window), &parent); GetWindowRect(window, &rect);
        SetWindowPos(window, nullptr, parent.left + (parent.right - parent.left - rect.right + rect.left) / 2,
                     parent.top + (parent.bottom - parent.top - rect.bottom + rect.top) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        return TRUE;
    }
    if (!job) return FALSE;
    if (message == WM_TIMER && job->Running)
    {
        SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETPOS, job->Percent, 0);
        if (job->Done)
        {
            job->Worker.StopAndJoin(INFINITE); job->Running = false; KillTimer(window, 1);
            // Completion may follow the earlier progress read; publish the final value after joining.
            SendDlgItemMessageW(window, IDC_PROGRESS, PBM_SETPOS, job->Percent, 0);
            if (!job->Url.empty())
            {
                SetDlgItemTextW(window, IDC_URL, job->Url.c_str());
                EnableWindow(GetDlgItem(window, IDC_COPY), TRUE);
                SetDlgItemTextW(window, IDC_STATUS, Text(CopyUrl(window, job->Url) ? IDS_SUCCESS : IDS_COPY_FAILED).c_str());
            }
            else
            {
                SetDlgItemTextW(window, IDC_STATUS, job->Error.c_str());
                EnableWindow(GetDlgItem(window, IDOK), TRUE);
            }
        }
        else if (!job->Cancel && job->Waiting) SetDlgItemTextW(window, IDC_STATUS, Text(IDS_WAITING).c_str());
        return TRUE;
    }
    if (message == WM_COMMAND)
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
            if (job->Running || !job->Url.empty()) return TRUE;
            job->Cancel = job->Done = job->Waiting = false; job->Percent = 0; job->Error.clear();
            EnableWindow(GetDlgItem(window, IDOK), FALSE);
            SetDlgItemTextW(window, IDC_STATUS, Text(IDS_UPLOADING).c_str());
            if (!SetTimer(window, 1, 100, nullptr))
            {
                EnableWindow(GetDlgItem(window, IDOK), TRUE);
                SetDlgItemTextW(window, IDC_STATUS, Text(IDS_ERROR).c_str());
                return TRUE;
            }
            try
            {
                if (!job->Worker.Start(PublishWorker, job, "Infostacker publisher", nullptr, 0)) throw std::bad_alloc();
                job->Running = true;
            }
            catch (...) { KillTimer(window, 1); EnableWindow(GetDlgItem(window, IDOK), TRUE); SetDlgItemTextW(window, IDC_STATUS, Text(IDS_ERROR).c_str()); }
            return TRUE;
        case IDC_COPY:
            if (!job->Url.empty()) SetDlgItemTextW(window, IDC_STATUS, Text(CopyUrl(window, job->Url) ? IDS_SUCCESS : IDS_COPY_FAILED).c_str());
            return TRUE;
        case IDCANCEL:
            SendMessageW(window, WM_CLOSE, 0, 0); return TRUE;
        }
    }
    if (message == WM_CLOSE)
    {
        // Keep the DLL and job alive until the bounded network call acknowledges cancellation.
        if (job->Running) { job->Cancel = true; SetDlgItemTextW(window, IDC_STATUS, Text(IDS_CANCELLING).c_str()); }
        else EndDialog(window, 0);
        return TRUE;
    }
    return FALSE;
}

bool SelectedPath(std::wstring& path)
{
    int files = 0, directories = 0, type = 0;
    if (!General->GetPanelSelection(PANEL_SOURCE, &files, &directories) || files > 1 || directories) return false;
    BOOL directory = FALSE;
    int index = 0;
    const CFileData* item = files ? General->GetPanelSelectedItem(PANEL_SOURCE, &index, &directory) : General->GetPanelFocusedItem(PANEL_SOURCE, &directory);
    if (!item || directory) return false;
    // Copy SDK item text before the next SDK call can invalidate its pointer.
    std::string name(item->Name);
    std::vector<char> folder(32768);
    if (!General->GetPanelPath(PANEL_SOURCE, folder.data(), (int)folder.size(), &type, nullptr) || type != PATH_TYPE_WINDOWS) return false;
    path = (std::filesystem::path(Wide(folder.data())) / Wide(name)).wstring();
    return true;
}

class Menu : public CPluginInterfaceForMenuExtAbstract
{
public:
    DWORD WINAPI GetMenuItemState(int, DWORD) override { std::wstring path; return SelectedPath(path) ? MENU_ITEM_STATE_ENABLED : 0; }
    BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract*, HWND parent, int id, DWORD) override
    {
        if (id != 1) return FALSE;
        Publication job;
        if (!SelectedPath(job.Path)) MessageBoxW(parent, Text(IDS_SELECTION).c_str(), L"Infostacker", MB_OK | MB_ICONINFORMATION);
        else if (DialogBoxParamW(Language, MAKEINTRESOURCEW(IDD_PUBLISH), parent, DialogProc, (LPARAM)&job) == -1)
            MessageBoxW(parent, Text(IDS_ERROR).c_str(), L"Infostacker", MB_OK | MB_ICONERROR);
        return FALSE;
    }
    BOOL WINAPI HelpForMenuItem(HWND, int) override { return FALSE; }
    void WINAPI BuildMenu(HWND, CSalamanderBuildMenuAbstract*) override {}
} PluginMenu;

class Plugin : public CPluginInterfaceAbstract
{
public:
    void WINAPI About(HWND parent) override { MessageBoxW(parent, Text(IDS_DESCRIPTION).c_str(), L"Infostacker 1.0", MB_OK | MB_ICONINFORMATION); }
    BOOL WINAPI Release(HWND, BOOL) override { return TRUE; }
    void WINAPI LoadConfiguration(HWND, HKEY, CSalamanderRegistryAbstract*) override {}
    void WINAPI SaveConfiguration(HWND, HKEY, CSalamanderRegistryAbstract*) override {}
    void WINAPI Configuration(HWND) override {}
    void WINAPI Connect(HWND, CSalamanderConnectAbstract* host) override
    {
        // Dynamic state validates the selected set; execution repeats validation before opening a window.
        host->AddMenuItem(-1, handoff::WideToUtf8(Text(IDS_COMMAND)).c_str(), 0, 1, TRUE, MENU_EVENT_TRUE, 0, MENU_SKILLLEVEL_ALL);
        // The host renders this reserved slot through GetPluginSVGName's Fluent SVG mapping.
        host->SetPluginIcon(0);
        host->SetPluginMenuAndToolbarIcon(0);
    }
    void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract*) override {}
    CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() override { return nullptr; }
    CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() override { return nullptr; }
    CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() override { return &PluginMenu; }
    CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override { return nullptr; }
    CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() override { return nullptr; }
    void WINAPI Event(int, DWORD) override {}
    void WINAPI ClearHistory(HWND) override {}
    void WINAPI AcceptChangeOnPathNotification(const char*, BOOL) override {}
    void WINAPI PasswordManagerEvent(HWND, int) override {}
} PluginInterface;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) Instance = instance;
    return TRUE;
}

int WINAPI SalamanderPluginGetReqVer() { return LAST_VERSION_OF_SALAMANDER; }

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* host)
{
    if (host->GetVersion() < LAST_VERSION_OF_SALAMANDER) return nullptr;
    General = host->GetSalamanderGeneral();
    Language = host->LoadLanguageModule(host->GetParentWindow(), "Infostacker");
    if (!Language) return nullptr;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    host->SetBasicPluginData("Infostacker", 0, "1.0", "Copyright (c) 2026 Taskscape Ltd",
                              handoff::WideToUtf8(Text(IDS_DESCRIPTION)).c_str(), "INFOSTACKER", nullptr, nullptr);
    host->SetPluginHomePageURL("https://shr.infostacker.com/");
    return &PluginInterface;
}
