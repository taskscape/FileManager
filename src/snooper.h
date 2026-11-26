// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class CFilesWindow;

extern HANDLE RefreshFinishedEvent;
extern int SnooperSuspended;

// Refresh notifications are deliberately admitted one at a time; expose that
// pressure so a slow panel can be distinguished from missed directory events.
struct CDirectoryRefreshMetrics
{
    LONGLONG Posted;
    LONGLONG Awaited;
    LONGLONG SuspendBatched;
};

CDirectoryRefreshMetrics GetDirectoryRefreshMetrics();

void AddDirectory(CFilesWindow* win, const char* path, BOOL registerDevNotification);                           // new directory for snooper
void ChangeDirectory(CFilesWindow* win, const char* newPath, BOOL registerDevNotification);                     // change of the specified directory
void DetachDirectory(CFilesWindow* win, BOOL waitForHandleClosure = FALSE, BOOL closeDevNotifification = TRUE); // snooping is no longer needed

BOOL InitializeThread();
void TerminateThread();

void BeginSuspendMode(BOOL debugDoNotTestCaller = FALSE);
void EndSuspendMode(BOOL debugDoNotTestCaller = FALSE);

typedef TDirectArray<CFilesWindow*> CWindowArray; // (CFilesWindow *)
typedef TDirectArray<HANDLE> CObjectArray;        // (HANDLE)

extern CWindowArray WindowArray; // identically indexed arrays
extern CObjectArray ObjectArray; // the MainWindow corresponds to the ObjectHandle
