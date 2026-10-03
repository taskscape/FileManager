// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "config.h"
#include "handoff.h"
#include "ui/handoff_ui.h"
#include "ui/sdk_strings.h"
#include "ui/session.h"
#include "ui/ui_thread.h"
#include "ui/worker_bridge.h"
#include "text_util.h"

// Delivery Handoff plug-in entry and host integration (handoff-spec.md C.3).

HINSTANCE DLLInstance = NULL;
HINSTANCE HLanguage = NULL;

CPluginInterface PluginInterface;
CPluginInterfaceForMenuExt InterfaceForMenuExt;

CSalamanderGeneralAbstract* SalamanderGeneral = NULL;
// variable definitions for "dbg.h" and "spl_com.h"
CSalamanderDebugAbstract* SalamanderDebug = NULL;
int SalamanderVersion = 0;

CWindowQueue ModelessQueue("Handoff Windows");
CThreadQueue ThreadQueue("Handoff UI Threads and Workers");

namespace
{

const char* const kInternalName = "Handoff"; /* do not translate! */

std::string Utf8Text(int id)
{
    std::string text;
    WideToSdk(Text(id), text);
    return text;
}

void ShowMainMessage(HWND parent, const std::wstring& text, UINT flags)
{
    // Main-thread messages go through the host so they follow its parenting rules.
    std::string utf8Text, utf8Title;
    WideToSdk(text, utf8Text);
    WideToSdk(Text(IDS_PLUGINNAME), utf8Title);
    SalamanderGeneral->SalMessageBox(parent, utf8Text.c_str(), utf8Title.c_str(), flags);
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        DLLInstance = hinstDLL;
        INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&controls);
    }
    return TRUE;
}

int WINAPI SalamanderPluginGetReqVer()
{
    return LAST_VERSION_OF_SALAMANDER;
}

void WINAPI HTMLHelpCallback(HWND hWindow, UINT helpID)
{
    SalamanderGeneral->OpenHtmlHelp(hWindow, HHCDisplayContext, helpID, FALSE);
}

void OpenHelpTopic(HWND parent, int helpId)
{
    SalamanderGeneral->OpenHtmlHelp(parent, HHCDisplayContext, helpId, FALSE);
}

CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* salamander)
{
    SalamanderDebug = salamander->GetSalamanderDebug();
    SalamanderVersion = salamander->GetVersion();
    HANDLES_CAN_USE_TRACE();
    CALL_STACK_MESSAGE1("SalamanderPluginEntry()");

    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER)
    {
        MessageBoxA(salamander->GetParentWindow(), REQUIRE_LAST_VERSION_OF_SALAMANDER, kInternalName, MB_OK | MB_ICONERROR);
        return NULL;
    }

    HLanguage = salamander->LoadLanguageModule(salamander->GetParentWindow(), kInternalName);
    if (HLanguage == NULL)
        return NULL;

    SalamanderGeneral = salamander->GetSalamanderGeneral();
    SalamanderGeneral->SetHelpFileName("handoff.chm");

    if (!InitializeWinLib(kInternalName, DLLInstance))
        return NULL;
    SetupWinLibHelp(HTMLHelpCallback);

    std::string name = Utf8Text(IDS_PLUGINNAME);
    std::string description = Utf8Text(IDS_PLUGIN_DESCRIPTION);
    salamander->SetBasicPluginData(name.c_str(), FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                   VERSINFO_VERSION_NO_PLATFORM, VERSINFO_COPYRIGHT, description.c_str(), kInternalName);
    salamander->SetPluginHomePageURL("www.taskscape.com");
    return &PluginInterface;
}

// ****************************************************************************
//
//  CPluginInterface
//

void CPluginInterface::About(HWND parent)
{
    std::wstring text = Text(IDS_PLUGINNAME) + L" " + SdkToWide(VERSINFO_VERSION) + L"\n\n" + SdkToWide(VERSINFO_COPYRIGHT) +
                        L"\n\n" + Text(IDS_PLUGIN_DESCRIPTION);
    std::string utf8Text, utf8Title;
    WideToSdk(text, utf8Text);
    WideToSdk(Text(IDS_ABOUT_TITLE), utf8Title);
    SalamanderGeneral->SalMessageBox(parent, utf8Text.c_str(), utf8Title.c_str(), MB_OK | MB_ICONINFORMATION);
}

BOOL CPluginInterface::Release(HWND parent, BOOL force)
{
    CALL_STACK_MESSAGE2("CPluginInterface::Release(, %d)", force);
    bool critical = SalamanderGeneral->IsCriticalShutdown() != FALSE;
    // A running build is never abandoned silently: a non-forced unload is
    // refused, so no partial folder is left without the user's knowledge (C.3.3 step 5).
    if (!force && !critical && IsBuildRunning())
    {
        ShowMainMessage(parent, Text(IDS_UNLOAD_BUSY), MB_OK | MB_ICONINFORMATION);
        return FALSE;
    }
    bool hurry = force || critical;
    SetShuttingDown(true);
    CancelAllWorkers();
    BOOL ret = ModelessQueue.Empty() || ModelessQueue.CloseAllWindows(hurry) || force;
    if (ret && !ThreadQueue.KillAll(hurry, critical ? 1000 : 5000, 2000) && !force)
        ret = FALSE;
    if (ret)
        ReleaseWinLib(DLLInstance);
    else
        SetShuttingDown(false);
    return ret;
}

void CPluginInterface::LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::LoadConfiguration(, ,)");
    LoadHandoffConfig(regKey, registry);
}

void CPluginInterface::SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE1("CPluginInterface::SaveConfiguration(, ,)");
    SaveHandoffConfig(regKey, registry);
}

void CPluginInterface::Configuration(HWND parent)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Configuration()");
    static bool open = false; // main thread only; a second request while the dialog is open is ignored
    if (open)
        return;
    open = true;
    RunConfigurationDialog(parent);
    open = false;
}

void CPluginInterface::ClearHistory(HWND parent)
{
    // "Clear history" in the host also forgets recently used specification paths.
    UpdateConfig([](HandoffConfig& config) { config.RecentSpecs.clear(); });
}

void CPluginInterface::Connect(HWND parent, CSalamanderConnectAbstract* salamander)
{
    CALL_STACK_MESSAGE1("CPluginInterface::Connect(,)");

    /* used by the export_mnu.py script, which generates salmenu.mnu for Translator
   keep it in sync with the salamander->AddMenuItem() calls below...
MENU_TEMPLATE_ITEM PluginMenu[] =
{
        {MNTT_PB, 0
        {MNTT_IT, IDS_MENU_BUILD
        {MNTT_IT, IDS_MENU_VERIFY
        {MNTT_SP, 0
        {MNTT_IT, IDS_MENU_VALIDATE
        {MNTT_IT, IDS_MENU_NEWSPEC
        {MNTT_PE, 0
};
*/

    // Enablement masks of handoff-spec.md C.3.2: building needs Windows paths in
    // both panes, verifying and creating need one in the active pane.
    salamander->AddMenuItem(-1, Utf8Text(IDS_MENU_BUILD).c_str(), 0, CMD_BUILD, FALSE, MENU_EVENT_DISK,
                            MENU_EVENT_DISK | MENU_EVENT_TARGET_DISK, MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, Utf8Text(IDS_MENU_VERIFY).c_str(), 0, CMD_VERIFY, FALSE, MENU_EVENT_DISK, MENU_EVENT_DISK,
                            MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, NULL, 0, 0, FALSE, 0, 0, MENU_SKILLLEVEL_ALL); // separator
    salamander->AddMenuItem(-1, Utf8Text(IDS_MENU_VALIDATE).c_str(), 0, CMD_VALIDATE_SPEC, FALSE, MENU_EVENT_TRUE, 0,
                            MENU_SKILLLEVEL_ALL);
    salamander->AddMenuItem(-1, Utf8Text(IDS_MENU_NEWSPEC).c_str(), 0, CMD_NEW_SPEC, FALSE, MENU_EVENT_DISK, MENU_EVENT_DISK,
                            MENU_SKILLLEVEL_ALL);

    HBITMAP bitmap = (HBITMAP)LoadImageW(DLLInstance, MAKEINTRESOURCEW(IDB_HANDOFF), IMAGE_BITMAP, 16, 16,
                                         SalamanderGeneral->GetIconLRFlags());
    salamander->SetBitmapWithIcons(bitmap);
    if (bitmap != NULL)
        DeleteObject(bitmap);
    salamander->SetPluginIcon(0);
    salamander->SetPluginMenuAndToolbarIcon(0);
}

CPluginInterfaceForMenuExtAbstract* CPluginInterface::GetInterfaceForMenuExt()
{
    return &InterfaceForMenuExt;
}

// ****************************************************************************
//
//  CPluginInterfaceForMenuExt
//

BOOL CPluginInterfaceForMenuExt::ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
                                                 DWORD eventMask)
{
    CALL_STACK_MESSAGE3("CPluginInterfaceForMenuExt::ExecuteMenuItem( , , %d, 0x%X)", id, eventMask);
    if (id == CMD_INTERNAL_FOCUS)
    {
        ProcessFocusRequests();
        return FALSE;
    }
    if (id == CMD_BUILD || id == CMD_VERIFY || id == CMD_NEW_SPEC)
        SalamanderGeneral->SetUserWorkedOnPanelPath(PANEL_SOURCE);

    // Everything the commands need from the panes is captured here, on the
    // main thread; the windows themselves run on plug-in UI threads.
    SessionInput input = CaptureSession(id == CMD_BUILD);
    bool started = true;
    switch (id)
    {
    case CMD_BUILD:
        if (!input.Source.IsWindowsPath || !input.Source.PathFits)
        {
            ShowMainMessage(parent, handoff::FindingMessage(handoff::MakeFinding(L"HO-SES-001", L""), Catalog()), MB_OK | MB_ICONINFORMATION);
            return FALSE;
        }
        if (!input.Target.IsWindowsPath || !input.Target.PathFits)
        {
            ShowMainMessage(parent, handoff::FindingMessage(handoff::MakeFinding(L"HO-SES-002", L""), Catalog()), MB_OK | MB_ICONINFORMATION);
            return FALSE;
        }
        started = StartBuildSession(input);
        break;
    case CMD_VERIFY:
        started = StartVerify(input);
        break;
    case CMD_VALIDATE_SPEC:
        started = StartValidate(input);
        break;
    case CMD_NEW_SPEC:
        started = StartNewSpec(input);
        break;
    }
    if (!started)
        ShowMainMessage(parent, Text(IDS_ERR_START), MB_OK | MB_ICONERROR);
    // The panel selection is never cleared: it may be the scope of a build.
    return FALSE;
}

BOOL CPluginInterfaceForMenuExt::HelpForMenuItem(HWND parent, int id)
{
    int helpId = 0;
    switch (id)
    {
    case CMD_BUILD:
        helpId = IDH_HANDOFF_BUILD;
        break;
    case CMD_VERIFY:
        helpId = IDH_HANDOFF_VERIFY;
        break;
    case CMD_VALIDATE_SPEC:
        helpId = IDH_HANDOFF_VALIDATE;
        break;
    case CMD_NEW_SPEC:
        helpId = IDH_HANDOFF_NEWSPEC;
        break;
    }
    if (helpId != 0)
        OpenHelpTopic(parent, helpId);
    return helpId != 0;
}
