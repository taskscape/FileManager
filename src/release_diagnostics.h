// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Release diagnostics retain only fixed, sanitized event labels.  They make
// field failures diagnosable without retaining file names, paths, or data.
typedef void (*FReleaseDiagnosticPrintLine)(void* param, const char* txt, BOOL tab);

void RecordReleaseDiagnosticOperationTransition(int fromState, int toState);
void RecordReleaseDiagnosticWait(const char* waitName, DWORD result);
void RecordReleaseDiagnosticRetry(const char* operationName);
void RecordReleaseDiagnosticPluginIdentity(const char* pluginName);

void PrintReleaseDiagnosticRingBuffer(FReleaseDiagnosticPrintLine printLine, void* param);
BOOL ExportReleaseDiagnosticRingBuffer(const char* fileName);
