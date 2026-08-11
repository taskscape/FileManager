// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "thread_owner.h"

#include <stdarg.h>
#include <string>
#include <vector>

namespace
{
const size_t kMaximumCrashReportPathLength = 32767;
const char* const kCompressionWrapperName = "plugins\\7zip\\7zwrapper.dll";

void SetCompressionError(CCompressParams* compressParams, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = _vsnprintf_s(compressParams->ErrorMessage, sizeof(compressParams->ErrorMessage),
                               _TRUNCATE, format, arguments);
    va_end(arguments);
    if (written < 0)
        _snprintf_s(compressParams->ErrorMessage, sizeof(compressParams->ErrorMessage), _TRUNCATE,
                    "Crash report compression error could not be formatted.");
}

BOOL CopyBoundedCrashReportText(const char* source, size_t sourceCapacity, std::string* destination)
{
    destination->clear();
    if (source == NULL || sourceCapacity == 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    const char* terminator = (const char*)memchr(source, 0, sourceCapacity);
    if (terminator == NULL)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    const size_t length = terminator - source;
    if (length > kMaximumCrashReportPathLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    destination->assign(source, length);
    return TRUE;
}

BOOL GetModuleFileNameOwned(std::wstring* moduleFileName)
{
    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::vector<WCHAR> buffer(capacity);
        DWORD length = GetModuleFileNameW(NULL, &buffer[0], capacity);
        if (length == 0)
            return FALSE;
        if (length < capacity && buffer[length] == 0)
        {
            moduleFileName->assign(&buffer[0], length);
            return TRUE;
        }
        if (capacity >= kMaximumCrashReportPathLength + 1)
            break;
        DWORD nextCapacity = capacity * 2;
        capacity = nextCapacity > kMaximumCrashReportPathLength + 1
                       ? (DWORD)(kMaximumCrashReportPathLength + 1)
                       : nextCapacity;
    }
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
}

BOOL BuildCompressionWrapperPath(std::wstring* wrapperPath)
{
    std::wstring executablePath;
    if (!GetModuleFileNameOwned(&executablePath))
        return FALSE;

    const std::wstring::size_type separator = executablePath.find_last_of(L"\\\\/");
    if (separator == std::wstring::npos)
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }

    // Salmon is installed in utils, while the trusted wrapper is a sibling
    // plugin; canonicalization below removes this explicit parent traversal.
    wrapperPath->assign(executablePath, 0, separator + 1);
    wrapperPath->append(L"..\\plugins\\7zip\\7zwrapper.dll");
    return TRUE;
}

class CScopedCompressionLibrary
{
public:
    explicit CScopedCompressionLibrary(HMODULE module) : Module(module)
    {
    }

    ~CScopedCompressionLibrary()
    {
        // Keep the wrapper loaded until its exported compression callback has returned.
        if (Module != NULL)
            HANDLES(FreeLibrary(Module));
    }

    HMODULE Get() const { return Module; }

private:
    CScopedCompressionLibrary(const CScopedCompressionLibrary&);
    CScopedCompressionLibrary& operator=(const CScopedCompressionLibrary&);

private:
    HMODULE Module;
};

HMODULE LoadCompressionWrapper()
{
    std::wstring wrapperPath;
    if (!BuildCompressionWrapperPath(&wrapperPath))
        return NULL;

    CWidePath wideWrapperPath(wrapperPath.c_str());
    const WCHAR* fullPath = wideWrapperPath.GetFullPathForWin32Api();
    if (fullPath == NULL)
        return NULL;

    // An absolute application-owned path and constrained dependency search
    // prevent a crash directory, CWD, or PATH entry from supplying this DLL.
    const DWORD loadFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                            LOAD_LIBRARY_SEARCH_SYSTEM32 |
                            LOAD_LIBRARY_SEARCH_USER_DIRS;
    HMODULE module = LoadLibraryExW(fullPath, NULL, loadFlags);
    const DWORD error = GetLastError();
    HANDLES_ADD_EX(__otMessages, module != NULL, __htLibrary, __hoLoadLibraryEx,
                   module, error, TRUE);
    SetLastError(error);
    return module;
}

BOOL IsCompressionStopRequested(HANDLE stopEvent)
{
    return stopEvent != NULL && WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0;
}

class CCrashCompressionWorker
{
public:
    CCrashCompressionWorker() : CorrelationId(0)
    {
    }

    BOOL Start(CCompressParams* params, CThreadOwnerEntry entry)
    {
        if (params == NULL)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (Thread.HasThread() && Thread.WaitForCompletion(0) == WAIT_TIMEOUT)
        {
            SetLastError(ERROR_BUSY);
            return FALSE;
        }
        if (Thread.HasThread())
            Thread.StopAndJoin(0); // reap a completed attempt before its parameters can be reused

        const LONG nextCorrelationId = InterlockedIncrement(&NextCorrelationId);
        CorrelationId = nextCorrelationId == 0 ? (DWORD)InterlockedIncrement(&NextCorrelationId) : (DWORD)nextCorrelationId;
        params->CorrelationId = CorrelationId;
        TRACE_I("Crash-report compression " << CorrelationId << " starting.");
        if (!Thread.Start(entry, params, "CrashReportCompression"))
        {
            TRACE_E("Crash-report compression " << CorrelationId << " could not start: " << GetErrorText(GetLastError()));
            CorrelationId = 0;
            return FALSE;
        }
        return TRUE;
    }

    BOOL IsRunning()
    {
        if (!Thread.HasThread())
            return FALSE;
        if (Thread.WaitForCompletion(0) == WAIT_TIMEOUT)
            return TRUE;

        // Completion owns the last close: reap it before the next report can reuse its params.
        Thread.StopAndJoin(0);
        CorrelationId = 0;
        return FALSE;
    }

    void Stop()
    {
        if (!Thread.HasThread())
            return;

        // Crash-dialog teardown keeps its input parameters alive until this
        // deadline-governed join has proved the wrapper callback is finished.
        const DWORD result = Thread.StopAndJoin(CThreadShutdownDeadline("crash-report compression"));
        if (result == WAIT_TIMEOUT)
            TRACE_E("Crash-report compression " << CorrelationId << " exceeded a shutdown deadline.");
        else
            TRACE_I("Crash-report compression " << CorrelationId << " stopped.");
        CorrelationId = 0;
    }

private:
    CThreadOwner Thread;
    DWORD CorrelationId;
    static volatile LONG NextCorrelationId;
};

volatile LONG CCrashCompressionWorker::NextCorrelationId = 0;
CCrashCompressionWorker CompressionWorker;
} // namespace

//------------------------------------------------------------------------------------------------
//
// CompresBugReports()
//

BOOL CompresBugReports(CCompressParams* compressParams, HANDLE stopEvent)
{
    BOOL ret = FALSE;
    compressParams->ErrorMessage[0] = 0;
    std::string sourceDirectory;
    if (!CopyBoundedCrashReportText(BugReportPath, sizeof(BugReportPath), &sourceDirectory) || sourceDirectory.empty())
    {
        SetCompressionError(compressParams, "Crash report path is invalid (%lu).", GetLastError());
        return FALSE;
    }

    CScopedCompressionLibrary h7zwrapper(LoadCompressionWrapper());
    if (h7zwrapper.Get() != NULL)
    {
        typedef BOOL(WINAPI * CompressFiles_t)(const char* archiveName7z, const char* sourceDir, const char* filter, char* errorMessage, int errorMessageSize);
        CompressFiles_t CompressFiles;
        CompressFiles = (CompressFiles_t)GetProcAddress(h7zwrapper.Get(), "CompressFiles");
        if (CompressFiles != NULL)
        {
            ret = TRUE;

            std::vector<char> error(10000, 0);
            for (int i = 0; i < BugReports.Count; i++)
            {
                if (IsCompressionStopRequested(stopEvent))
                {
                    TRACE_I("Crash-report compression " << compressParams->CorrelationId << " cancelled before report " << i << ".");
                    SetLastError(ERROR_CANCELLED);
                    return FALSE;
                }

                std::string reportName;
                if (!CopyBoundedCrashReportText(BugReports[i].Name, sizeof(BugReports[i].Name), &reportName) || reportName.empty())
                {
                    SetCompressionError(compressParams, "Crash report name is invalid (%lu).", GetLastError());
                    return FALSE;
                }

                std::string mask = reportName + ".*";
                std::string archive = sourceDirectory;
                if (archive[archive.size() - 1] != '\\')
                    archive.append("\\");
                archive.append(reportName);
                archive.append(".7Z");
                if (archive.size() > kMaximumCrashReportPathLength)
                {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    SetCompressionError(compressParams, "Crash report archive path is too long (%lu).", GetLastError());
                    return FALSE;
                }

                CWidePath archivePath(archive.c_str());
                const WCHAR* archiveApiPath = archivePath.GetFullPathForWin32Api();
                if (archiveApiPath == NULL)
                {
                    SetCompressionError(compressParams, "Crash report archive path is invalid (%lu).", GetLastError());
                    return FALSE;
                }
                DeleteFileW(archiveApiPath); // so the subsequent compression does not fail

                error[0] = 0;
                BOOL res = CompressFiles(archive.c_str(), sourceDirectory.c_str(), mask.c_str(), &error[0], (int)error.size());
                error[error.size() - 1] = 0;
                if (!res)
                    lstrcpyn(compressParams->ErrorMessage, &error[0], _countof(compressParams->ErrorMessage));
                ret &= res;
                if (!ReportOldBugs)
                    break;
            }
        }
        else
        {
            TRACE_E("Crash-report compression " << compressParams->CorrelationId << " could not resolve CompressFiles: " << GetErrorText(GetLastError()));
            SetCompressionError(compressParams, LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage), kCompressionWrapperName);
        }
    }
    else
    {
        const DWORD error = GetLastError();
        TRACE_E("Crash-report compression " << compressParams->CorrelationId << " could not load " << kCompressionWrapperName << ": " << GetErrorText(error));
        SetCompressionError(compressParams, LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage), kCompressionWrapperName);
    }
    return ret;
}

DWORD WINAPI CompressThreadF(void* param, HANDLE stopEvent)
{
    CCompressParams* compressParams = (CCompressParams*)param;
    compressParams->Result = CompresBugReports(compressParams, stopEvent);
    TRACE_I("Crash-report compression " << compressParams->CorrelationId << " completed with result=" << compressParams->Result << ".");
    return compressParams->Result ? ERROR_SUCCESS : GetLastError();
}

BOOL StartCompressThread(CCompressParams* params)
{
    return CompressionWorker.Start(params, CompressThreadF);
}

BOOL IsCompressThreadRunning()
{
    return CompressionWorker.IsRunning();
}

void StopCompressThread()
{
    CompressionWorker.Stop();
}
