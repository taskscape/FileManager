// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#include "precomp.h"
#include "operation_journal.h"
#include <time.h>
// Use StrSafe for the bounded process command line built below.
#include <strsafe.h>
//#ifdef MSVC_RUNTIME_CHECKS
#include <rtcapi.h>
//#endif // MSVC_RUNTIME_CHECKS

#include "allochan.h"
#include "menu.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "shellib.h"
#include "worker.h"
#include "iconpool.h"
#include "snooper.h"
#include "viewer.h"
#include "editwnd.h"
#include "find.h"
#include "zip.h"
#include "pack.h"
#include "cache.h"
#include "dialogs.h"
#include "gui.h"
#include "tasklist.h"
#include <uxtheme.h>
#include "olespy.h"
#include "geticon.h"
#include "logo.h"
#include "color.h"
#include "toolbar.h"

#include "svg.h"

extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "shiconov.h"
#include "salmoncl.h"
#include "jumplist.h"
#include "usermenu.h"
#include "execute.h"
#include "drivelst.h"

#include "app_shutdown.h"

// Whole-application teardown extracted from the tail of WinMainBody in
// app_entry.cpp. The sequence runs exactly once after the message loop
// finishes, which lets the startup file end at the loop instead of mixing
// startup with shutdown ordering constraints.
void ReleasePreloadedStrings()
{
    if (DirColumnStr != NULL)
        free((void*)DirColumnStr);
    if (ColExtStr != NULL)
        free((void*)ColExtStr);

    if (UpDirTypeName != NULL)
        free((void*)UpDirTypeName);

    if (CommonFileTypeName != NULL)
        free((void*)CommonFileTypeName);
    if (CommonFileTypeName2 != NULL)
        free((void*)CommonFileTypeName2);

    if (ProgDlgHoursStr != NULL)
        free((void*)ProgDlgHoursStr);
    if (ProgDlgMinutesStr != NULL)
        free((void*)ProgDlgMinutesStr);
    if (ProgDlgSecsStr != NULL)
        free((void*)ProgDlgSecsStr);

    DirColumnStr = NULL;
    ColExtStr = NULL;

    UpDirTypeName = NULL;

    CommonFileTypeName = NULL;
    CommonFileTypeName2 = NULL;

    ProgDlgHoursStr = NULL;
    ProgDlgMinutesStr = NULL;
    ProgDlgSecsStr = NULL;
}

void ShutdownSalamander()
{
    //--- vsem oknum dame 1 sekundu na to, aby se uzavreli, pak je nechame odpojit
    int timeOut = 10;
    int winsCount = WindowsManager.GetCount();
    while (timeOut-- && winsCount > 0)
    {
        Sleep(100);
        int c = WindowsManager.GetCount();
        if (winsCount > c) // zatim jeste ubyvaji okna, budeme cekat dale aspon 1 sekundu
        {
            winsCount = c;
            timeOut = 10;
        }
    }

//--- informace
#ifdef __DEBUG_WINLIB
    TRACE_I("WindowsManager: " << WindowsManager.maxWndCount << " windows, " << WindowsManager.search << " searches, " << WindowsManager.cache << " cached searches.");
#endif
    //---
    DestroySafeWaitWindow(TRUE); // povel "terminate" safe-wait-message threadu
    Sleep(1000);                 // nechame vsem threadum viewru cas, aby se ukoncili
    // Transfer the network-browser worker to the deadline-aware shutdown owner.
    NBWNetAC3Thread.Close(TRUE);
    ShutdownAuxThreads(); // retain shared state until legacy workers have joined
                                 //---
    TerminateThread();
    ReleaseFileNamesEnumForViewers();
    ReleaseShellIconOverlays();
    ReleaseSalShLib();
    IconPool.Shutdown(); // shutdown icon thread pool before worker
    ReleaseWorker();
    ReleaseViewer();
    ReleaseWinLib();
    ReleaseMenuWheelHook();
    ReleaseFind();
    ReleaseCheckThreads();
    ReleasePreloadedStrings();
    ReleaseShellib();
    ReleaseGraphics(FALSE);
    ReleaseConstGraphics();

    HANDLES(FreeLibrary(HLanguage));
    HLanguage = NULL;

    // pro jistotu zavreme az jako posledni, ale asi zbytecne obavy
    ReleaseSalOpen();

    if (NtDLL != NULL)
    {
        HANDLES(FreeLibrary(NtDLL));
        NtDLL = NULL;
    }
    if (User32DLL != NULL)
    {
        NOHANDLES(FreeLibrary(User32DLL));
        User32DLL = NULL;
    }

    //OleSpyStressTest(); // multi-threaded stress test
    // OleSpyRevoke();     // odpojime OLESPY
    OleUninitialize(); // deinicializace OLE
    // OleSpyDump();       // vypiseme leaky

    TRACE_I("End");
}
