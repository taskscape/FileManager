// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
// The crash-dialog parameters remain valid until the dump callback completes.
#include "..\\common\\thread_owner.h"

#include <stdarg.h>
#include <string>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4091) // disable typedef warning without variable declaration
#include <dbghelp.h>
#pragma warning(pop)

namespace
{
const size_t kMaximumCrashReportFieldLength = MAX_PATH - 1;
const size_t kMaximumCrashReportPathLength = 32767;

void SetMiniDumpError(CMinidumpParams* minidumpParams, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int written = _vsnprintf_s(minidumpParams->ErrorMessage, sizeof(minidumpParams->ErrorMessage),
                               _TRUNCATE, format, arguments);
    va_end(arguments);
    if (written < 0)
        _snprintf_s(minidumpParams->ErrorMessage, sizeof(minidumpParams->ErrorMessage), _TRUNCATE,
                    "Crash report error could not be formatted.");
}

BOOL CopyBoundedExternalText(const char* source, size_t sourceCapacity, size_t maximumAcceptedLength,
                             std::string* destination)
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

    size_t length = terminator - source;
    if (length > maximumAcceptedLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    destination->assign(source, length);
    return TRUE;
}

BOOL GetModuleFileNameOwned(std::string* moduleFileName)
{
    DWORD capacity = MAX_PATH;
    for (;;)
    {
        std::vector<char> buffer(capacity);
        DWORD length = GetModuleFileNameA(NULL, &buffer[0], capacity);
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

BOOL BuildMiniDumpFileName(CSalmonSharedMemory* mem, std::string* dumpFileName)
{
    std::string bugPath;
    std::string baseName;
    if (!CopyBoundedExternalText(mem->BugPath, sizeof(mem->BugPath), kMaximumCrashReportFieldLength, &bugPath) ||
        !CopyBoundedExternalText(mem->BaseName, sizeof(mem->BaseName), kMaximumCrashReportFieldLength, &baseName) ||
        baseName.empty())
    {
        return FALSE;
    }

    dumpFileName->assign(bugPath);
    if (!dumpFileName->empty() && (*dumpFileName)[dumpFileName->size() - 1] != '\\')
        dumpFileName->append("\\");
    dumpFileName->append(baseName);
    dumpFileName->append(".DMP");
    if (dumpFileName->size() > kMaximumCrashReportPathLength)
    {
        dumpFileName->clear();
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK FilterMiniDumpCallback(PVOID, const PMINIDUMP_CALLBACK_INPUT input,
                                    PMINIDUMP_CALLBACK_OUTPUT output)
{
    // Keep DbgHelp callbacks aligned with the minimal dump policy even when a
    // future dump type starts requesting VM ranges or module data segments.
    switch (input->CallbackType)
    {
    case IncludeVmRegionCallback:
        return FALSE;

    case ModuleCallback:
        output->ModuleWriteFlags &= ~(ModuleWriteDataSeg | ModuleWriteTlsData);
        return TRUE;

    default:
        return TRUE;
    }
}
}

// The dump policy is fixed at this boundary so callers cannot accidentally request sensitive-memory capture.
BOOL GenerateMiniDump(CMinidumpParams* minidumpParams, CSalmonSharedMemory* mem, BOOL* overSize)
{
    BOOL ret = FALSE;
    *overSize = FALSE;
    std::string moduleFileName;
    if (!GetModuleFileNameOwned(&moduleFileName))
    {
        SetMiniDumpError(minidumpParams, "Unable to determine the crash reporter path (%lu).", GetLastError());
        return FALSE;
    }
    std::string::size_type slash = moduleFileName.find_last_of('\\');
    if (slash == std::string::npos)
    {
        SetLastError(ERROR_INVALID_DATA);
        SetMiniDumpError(minidumpParams, "The crash reporter path is invalid.");
        return FALSE;
    }
    std::string dbgHelpPath = moduleFileName.substr(0, slash + 1) + "dbghelp.dll";
    static HMODULE hDbgHelp;
    hDbgHelp = LoadLibraryA(dbgHelpPath.c_str());
    if (hDbgHelp != NULL)
    {
        typedef BOOL(WINAPI * MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, CONST PMINIDUMP_EXCEPTION_INFORMATION,
                                                   CONST PMINIDUMP_USER_STREAM_INFORMATION, CONST PMINIDUMP_CALLBACK_INFORMATION);
        static MiniDumpWriteDump_t funcMiniDumpWriteDump;
        typedef BOOL(WINAPI * MakeSureDirectoryPathExists_t)(PCSTR);
        static MakeSureDirectoryPathExists_t funcMakeSureDirectoryPathExists;
        funcMiniDumpWriteDump = (MiniDumpWriteDump_t)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
        funcMakeSureDirectoryPathExists = (MakeSureDirectoryPathExists_t)GetProcAddress(hDbgHelp, "MakeSureDirectoryPathExists");
        if (funcMiniDumpWriteDump != NULL && funcMakeSureDirectoryPathExists != NULL)
        {
            std::string dumpFileName;
            if (!BuildMiniDumpFileName(mem, &dumpFileName))
            {
                SetMiniDumpError(minidumpParams, "Crash report fields are invalid or exceed the accepted size (%lu).",
                                 GetLastError());
                return FALSE;
            }

            // the path may not exist yet - create it
            funcMakeSureDirectoryPathExists(dumpFileName.c_str()); // the file name is ignored

            if (!EnsureCrashReportDirectoryEncrypted(dumpFileName.c_str()))
            {
                SetMiniDumpError(minidumpParams, "Unable to encrypt the crash-report directory (%lu).", GetLastError());
                return FALSE;
            }

            HANDLE hDumpFile;
            hDumpFile = CreateFileA(dumpFileName.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
            if (hDumpFile != INVALID_HANDLE_VALUE)
            {
                EXCEPTION_POINTERS ePtrs;
                MINIDUMP_EXCEPTION_INFORMATION expParam;
                ePtrs.ContextRecord = &mem->ContextRecord;
                ePtrs.ExceptionRecord = &mem->ExceptionRecord;
                expParam.ThreadId = mem->ThreadId;
                expParam.ExceptionPointers = &ePtrs;
                expParam.ClientPointers = FALSE;

                // Keep crash reports diagnostically useful without serializing arbitrary private writable memory.
                static MINIDUMP_TYPE dumpType = (MINIDUMP_TYPE)(MiniDumpWithProcessThreadData |
                                                                MiniDumpWithFullMemoryInfo |
                                                                MiniDumpWithThreadInfo |
                                                                MiniDumpWithUnloadedModules |
                                                                MiniDumpIgnoreInaccessibleMemory);
                MINIDUMP_CALLBACK_INFORMATION callbackInfo;
                callbackInfo.CallbackRoutine = FilterMiniDumpCallback;
                callbackInfo.CallbackParam = NULL;

                BOOL bMiniDumpSuccessful;
                bMiniDumpSuccessful = funcMiniDumpWriteDump(mem->Process, mem->ProcessId,
                                                            hDumpFile, dumpType,
                                                            &expParam,
                                                            NULL, &callbackInfo);
                if (bMiniDumpSuccessful)
                {
                    ret = TRUE;
                }
                else
                {
                    // generation fails on W7 with the x64/Debug build launched from MSVC; if I run it outside MSVC, everything works fine
                    DWORD err = GetLastError();
                    SetMiniDumpError(minidumpParams, LoadStr(IDS_SALMON_MINIDUMP_CALL, HLanguage), err);
                }
                // Keep the dump-size policy on the complete 64-bit value, including dumps beyond the old DWORD boundary.
                LARGE_INTEGER dumpSize;
                if (GetFileSizeEx(hDumpFile, &dumpSize) && dumpSize.QuadPart > 50LL * 1000 * 1024)
                    *overSize = TRUE; // if the result exceeds 50 MB, report it so a smaller version can be tried
                CloseHandle(hDumpFile);
            }
            else
            {
                DWORD err = GetLastError();
                SetMiniDumpError(minidumpParams, LoadStr(IDS_SALMON_MINIDUMP_CREATE, HLanguage),
                                 dumpFileName.c_str(), err);
            }
        }
        else
        {
            SetMiniDumpError(minidumpParams, LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage), dbgHelpPath.c_str());
        }
    }
    else
    {
        SetMiniDumpError(minidumpParams, LoadStr(IDS_SALMON_LOAD_FAILED, HLanguage), dbgHelpPath.c_str());
    }

    return ret;
}

extern BOOL DirExists(const char* dirName);

// based on the current time and the short Salamander version, generate a name (without an extension)
// from which the names for the text bug report and for the minidump are derived. The output array is
// a compatibility boundary; parsing and collision probing remain in owned, bounded dynamic strings.
BOOL GetReportBaseName(char* name, int nameSize, const char* targetPath, int targetPathSize,
                       const char* shortName, int shortNameSize, DWORD64 uid, SYSTEMTIME lt)
{
    if (name == NULL || nameSize <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    name[0] = 0;

    if (shortNameSize <= 0 || targetPath != NULL && targetPathSize <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    std::string boundedTargetPath;
    std::string boundedShortName;
    if ((targetPath != NULL &&
         !CopyBoundedExternalText(targetPath, targetPathSize, kMaximumCrashReportFieldLength, &boundedTargetPath)) ||
        !CopyBoundedExternalText(shortName, shortNameSize, kMaximumCrashReportFieldLength, &boundedShortName))
    {
        return FALSE;
    }

    char year[5];
    if (lt.wYear >= 2000 && lt.wYear < 2100)
        sprintf_s(year, "%02u", (BYTE)(lt.wYear - 2000));
    else
        sprintf_s(year, "%04u", lt.wYear);

    char uidText[17];
    char timestamp[16];
    sprintf_s(uidText, "%I64X", uid);
    sprintf_s(timestamp, "%s%02u%02u-%02u%02u%02u", year, lt.wMonth, lt.wDay,
              lt.wHour, lt.wMinute, lt.wSecond);
    std::string baseName = std::string(uidText) + "-" + boundedShortName + "-" + timestamp;
    if (baseName.size() > kMaximumCrashReportPathLength)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    CharUpperBuffA(&baseName[0], (DWORD)baseName.size()); // x64/x86 is lowercase, we want everything uppercased

    // If the target path exists, there could be a collision (unlikely thanks to the timestamp in the name).
    if (!boundedTargetPath.empty() && DirExists(boundedTargetPath.c_str()))
    {
        int i;
        for (i = 0; i < 100; i++) // cover 1 - 99, then give up
        {
            std::string findMask(baseName);
            if (i > 0)
            {
                char suffix[5];
                sprintf_s(suffix, "-%d", i);
                findMask.append(suffix);
            }
            findMask.append("*");
            std::string findPath(boundedTargetPath);
            if (findPath[findPath.size() - 1] != '\\')
                findPath.append("\\");
            findPath.append(findMask);
            if (findPath.size() > kMaximumCrashReportPathLength)
            {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return FALSE;
            }
            WIN32_FIND_DATA find;
            HANDLE hFind = NOHANDLES(FindFirstFileA(findPath.c_str(), &find));
            if (hFind != INVALID_HANDLE_VALUE)
                NOHANDLES(FindClose(hFind));
            else
                break; // no conflict found
        }
        if (i > 0)
        {
            char suffix[5];
            sprintf_s(suffix, "-%d", i);
            baseName.append(suffix);
        }
    }

    if (baseName.size() >= (size_t)nameSize)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(name, baseName.c_str(), baseName.size() + 1);
    return TRUE;
}

DWORD WINAPI MinidumpThreadF(void* param, HANDLE stopEvent)
{
    // Dump generation must finish its Done/process handshake; the owner event
    // therefore governs handle lifetime only and cannot skip that protocol.
    (void)stopEvent;
    CMinidumpParams* minidumpParams = (CMinidumpParams*)param;

    SYSTEMTIME lt;
    GetLocalTime(&lt);

    BOOL overSize = FALSE;
    BOOL ret = FALSE;
    if (!GetReportBaseName(SalmonSharedMemory->BaseName, sizeof(SalmonSharedMemory->BaseName),
                           SalmonSharedMemory->BugPath, sizeof(SalmonSharedMemory->BugPath),
                           SalmonSharedMemory->BugName, sizeof(SalmonSharedMemory->BugName),
                           SalmonSharedMemory->UID, lt))
    {
        SetMiniDumpError(minidumpParams, "Crash report fields are invalid or exceed the accepted size (%lu).",
                         GetLastError());
    }
    else
    {
        // Privacy is the default: never retry a failed minimal dump with a memory-rich variant.
        ret = GenerateMiniDump(minidumpParams, SalmonSharedMemory, &overSize);
    }

    // let Salamander know that the minidump has been created
    // at this moment Salamander attempts to write the text bug report to disk and then exits
    SetEvent(SalmonSharedMemory->Done);

    // wait until Salamander saves the report or terminates; it may be in a bad state, so wait only for a limited time
    DWORD res = WaitForSingleObject(SalmonSharedMemory->Process, 10000);

    minidumpParams->Result = ret;
    return EXIT_SUCCESS;
}

// The owner retains the actual handle while the dialog keeps this borrowed
// completion probe for its existing running-state API.
CThreadOwner* MinidumpThreadOwner = NULL;
HANDLE HMinidumpThread = NULL;

BOOL StartMinidumpThread(CMinidumpParams* params)
{
    if (HMinidumpThread != NULL)
        return FALSE;
    MinidumpThreadOwner = new CThreadOwner;
    if (MinidumpThreadOwner == NULL ||
        !MinidumpThreadOwner->Start(MinidumpThreadF, params, "crash-report minidump"))
    {
        delete MinidumpThreadOwner;
        MinidumpThreadOwner = NULL;
        return FALSE;
    }
    HMinidumpThread = MinidumpThreadOwner->GetThreadHandle();
    return TRUE;
}

BOOL IsMinidumpThreadRunning()
{
    if (HMinidumpThread == NULL)
        return FALSE;
    DWORD res = WaitForSingleObject(HMinidumpThread, 0);
    if (res != WAIT_TIMEOUT)
    {
        // The completion poll guarantees the dump no longer accesses dialog data.
        MinidumpThreadOwner->StopAndJoin(0);
        delete MinidumpThreadOwner;
        MinidumpThreadOwner = NULL;
        HMinidumpThread = NULL;
        return FALSE;
    }
    return TRUE;
}
