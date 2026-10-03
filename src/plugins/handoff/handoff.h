// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Delivery Handoff plug-in host integration (handoff-spec.md C.3): a static
// Plugins-menu extension whose commands open plug-in-owned windows on their
// own UI threads, so the main window stays usable during long reviews.

#include "handoff.rh2"
#include "lang\lang.rh"

extern HINSTANCE DLLInstance; // handoff.spl - language-independent resources (templates, bitmap)
extern HINSTANCE HLanguage;   // english.slg / polish.slg - dialogs and strings
extern CSalamanderGeneralAbstract* SalamanderGeneral;

// Every plug-in window (modal and modeless) is registered here so unloading
// can close them, and every plug-in thread runs in ThreadQueue so unloading
// can stop and join it (handoff-spec.md C.3.3 step 5).
extern CWindowQueue ModelessQueue;
extern CThreadQueue ThreadQueue;

class CPluginInterface : public CPluginInterfaceAbstract
{
public:
    virtual void WINAPI About(HWND parent);
    virtual BOOL WINAPI Release(HWND parent, BOOL force);
    virtual void WINAPI LoadConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI SaveConfiguration(HWND parent, HKEY regKey, CSalamanderRegistryAbstract* registry);
    virtual void WINAPI Configuration(HWND parent);
    virtual void WINAPI Connect(HWND parent, CSalamanderConnectAbstract* salamander);
    virtual void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* pluginData) {}
    virtual CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() { return NULL; }
    virtual CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() { return NULL; }
    virtual CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt();
    virtual CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() { return NULL; }
    virtual CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() { return NULL; }
    virtual void WINAPI Event(int event, DWORD param) {}
    virtual void WINAPI ClearHistory(HWND parent);
    virtual void WINAPI AcceptChangeOnPathNotification(const char* path, BOOL includingSubdirs) {}
    virtual void WINAPI PasswordManagerEvent(HWND parent, int event) {}
};

class CPluginInterfaceForMenuExt : public CPluginInterfaceForMenuExtAbstract
{
public:
    virtual DWORD WINAPI GetMenuItemState(int id, DWORD eventMask) { return 0; }
    virtual BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract* salamander, HWND parent, int id,
                                        DWORD eventMask);
    virtual BOOL WINAPI HelpForMenuItem(HWND parent, int id);
    virtual void WINAPI BuildMenu(HWND parent, CSalamanderBuildMenuAbstract* salamander) {}
};

// Opens a help topic (IDH_*) from any thread.
void OpenHelpTopic(HWND parent, int helpId);
