// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "execlog.h"

#ifdef _DEBUG

static const char* ExecLogSafeStr(const char* value)
{
    return value != NULL ? value : "";
}

static void ExecLogGetTimestamp(char* buffer, size_t bufferSize)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(buffer, bufferSize, _TRUNCATE,
                "%04u-%02u-%02u %02u:%02u:%02u.%03u",
                st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

static void ExecLogWrite(BOOL success, const char* message)
{
    char timestamp[32];
    char line[1024];
    ExecLogGetTimestamp(timestamp, sizeof(timestamp));
    _snprintf_s(line, _TRUNCATE, "ExecLog [%s] %s", timestamp, ExecLogSafeStr(message));

    if (success)
        TRACE_I(line);
    else
        TRACE_E(line);
}

void ExecLogStartupBegin()
{
    ExecLogWrite(TRUE, "startup begin");
}

void ExecLogStartupPhase(const char* phase)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE, "startup phase: %s", ExecLogSafeStr(phase));
    ExecLogWrite(TRUE, message);
}

void ExecLogStartupComplete()
{
    ExecLogWrite(TRUE, "startup complete");
}

void ExecLogPluginLoadStart(const char* dllName, const char* pluginName)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "plugin load start: name=%s, dll=%s",
                ExecLogSafeStr(pluginName), ExecLogSafeStr(dllName));
    ExecLogWrite(TRUE, message);
}

void ExecLogPluginLoadResult(const char* dllName, const char* pluginName, BOOL success)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "plugin load result: name=%s, dll=%s, success=%d",
                ExecLogSafeStr(pluginName), ExecLogSafeStr(dllName), success ? 1 : 0);
    ExecLogWrite(success, message);
}

void ExecLogFileListingStart(const char* path, BOOL isPlugin, const char* fsName)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "list start: path=%s, source=%s, fs=%s",
                ExecLogSafeStr(path), isPlugin ? "plugin" : "disk", ExecLogSafeStr(fsName));
    ExecLogWrite(TRUE, message);
}

void ExecLogFileListingResult(const char* path, BOOL success, int fileCount, int dirCount,
                              BOOL isPlugin, const char* fsName)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "list result: path=%s, source=%s, fs=%s, success=%d, files=%d, dirs=%d",
                ExecLogSafeStr(path), isPlugin ? "plugin" : "disk", ExecLogSafeStr(fsName),
                success ? 1 : 0, fileCount, dirCount);
    ExecLogWrite(success, message);
}

void ExecLogFileOperationStart(const char* operation, const char* source, const char* target)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "op start: op=%s, source=%s, target=%s",
                ExecLogSafeStr(operation), ExecLogSafeStr(source), ExecLogSafeStr(target));
    ExecLogWrite(TRUE, message);
}

void ExecLogFileOperationResult(const char* operation, const char* source, const char* target, BOOL success)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "op result: op=%s, source=%s, target=%s, success=%d",
                ExecLogSafeStr(operation), ExecLogSafeStr(source), ExecLogSafeStr(target),
                success ? 1 : 0);
    ExecLogWrite(success, message);
}

void ExecLogFeatureStart(const char* feature, const char* detail)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "feature start: %s, detail=%s",
                ExecLogSafeStr(feature), ExecLogSafeStr(detail));
    ExecLogWrite(TRUE, message);
}

void ExecLogFeatureResult(const char* feature, const char* detail, BOOL success)
{
    char message[512];
    _snprintf_s(message, _TRUNCATE,
                "feature result: %s, detail=%s, success=%d",
                ExecLogSafeStr(feature), ExecLogSafeStr(detail), success ? 1 : 0);
    ExecLogWrite(success, message);
}

#endif // _DEBUG
