// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// SalmonClient
// The SALMON.EXE module is used for out-of-process minidump generation, packing it, and uploading it to the server
// SALMON must run from Salamander startup so it can react to crashes. Crashes before SALMON starts
// pass silently and SALMON processes them "next time"
//
// this header is shared between the SALMON and SALAMAND projects because of the memory through which they communicate
//
// out of process minidumps
// http://www.nynaeve.net/?p=128
// http://social.msdn.microsoft.com/Forums/en-US/windbg/thread/2dfd711f-e81e-466f-a566-4605e78075f6
//
// http://www.voyce.com/index.php/2008/06/11/creating-a-featherweight-debugger/
// http://social.msdn.microsoft.com/Forums/en-US/windbg/thread/2dfd711f-e81e-466f-a566-4605e78075f6
// http://social.msdn.microsoft.com/Forums/en-US/vsdebug/thread/b290b7bd-1ec8-4302-8e3a-8ee0dc134683/
// http://www.ms-news.info/f3682/minidumpwritedump-fails-after-writing-partial-dump-access-denied-1843614.html
//
// debugging handles
// http://www.codeproject.com/Articles/6988/Debug-Tutorial-Part-5-Handle-Leaks

#define SALMON_FILEMAPPIN_NAME_SIZE 20

// x64 and x86 versions of Salamander/Salmon are not compatible
#ifdef _WIN64
#define SALMON_SHARED_MEMORY_VERSION_PLATFORM 0x10000000
#else
#define SALMON_SHARED_MEMORY_VERSION_PLATFORM 0x00000000
#endif
#define SALMON_SHARED_MEMORY_VERSION (SALMON_SHARED_MEMORY_VERSION_PLATFORM | 4)

#pragma pack(push)
#pragma pack(4)
struct CSalmonSharedMemory
{
    DWORD Version;           // SALMON_SHARED_MEMORY_VERSION (if SALAM/SALMON disagree, yell and do not communicate...)
    HANDLE Process;          // Handle of the parent process (so we can wait for its termination); we therefore let this value leak
    DWORD ProcessId;         // ID of the crashed parent process
    DWORD ThreadId;          // ID of the crashed thread
    HANDLE Fire;             // AS signals SALMON that it should send the reports
    HANDLE Done;             // SALMON returns to AS that it is done
    HANDLE SetSLG;           // AS signals SALMON to load SLG according to buffer SLGName, which it sets before setting the event
    HANDLE CheckBugs;        // AS signals SALMON to check the bug report directory and, if it finds any (from a previous crash), offer their upload
    char SLGName[MAX_PATH];  // meaningful at the moment when AS sets SetSLG and says which SLG should be loaded
    char BugPath[MAX_PATH];  // set by Salamander, specifies path where bug reports will land (path need not exist, created only on crash);
    char BugName[MAX_PATH];  // set by Salamander; specifies the internal file name of the minidump/bug report
    char BaseName[MAX_PATH]; // set by salmon; it is the composition "UID-BugName-DATE-TIME"; for a minidump it then appends ".DMP"
    DWORD64 UID;             // unique machine ID, created by XORing a GUID; stored in the registry under the Bug Reporter key; set by Salamander, salmon only reads it and inserts it into the bug report name

    // passing EXCEPTION_POINTERS by its parts; we set it before setting the Fire event
    EXCEPTION_RECORD ExceptionRecord;
    CONTEXT ContextRecord;
};
#pragma pack(pop)

//-----------------------------------------------------------------------
#ifdef INSIDE_SALAMANDER

BOOL SalmonInit();
void SalmonSetSLG(const char* slgName); // sets the language in salmon
void SalmonCheckBugs();

// stores exception info in the shared memory and asks salmon to create a minidump; then waits until it finishes
// Returns TRUE on success, FALSE if Salmon could not be called for some reason.
BOOL SalmonFireAndWait(const EXCEPTION_POINTERS* e, char* bugReportPath);

#endif //INSIDE_SALAMANDER
