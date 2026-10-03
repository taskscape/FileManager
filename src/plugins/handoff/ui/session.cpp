// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "sdk_strings.h"
#include "session.h"
#include "text_util.h"

namespace
{

struct FocusRequestData
{
    int Panel = 0;
    std::string Directory, Name;
    std::wstring WideDirectory;
    bool OnlyIfShowing = false;
};

std::mutex FocusLock;
std::vector<FocusRequestData> FocusQueue;

PaneSnapshot CapturePane(int panel, int physicalPanel)
{
    PaneSnapshot pane;
    pane.Panel = physicalPanel;
    // Heap buffer sized for any long path; GetPanelPath fails rather than truncating.
    std::vector<char> buffer((size_t)kSdkPathBufferBytes, 0);
    int type = 0;
    if (!SalamanderGeneral->GetPanelPath(panel, buffer.data(), (int)buffer.size(), &type, NULL))
    {
        pane.PathFits = false;
        return pane;
    }
    pane.IsWindowsPath = type == PATH_TYPE_WINDOWS;
    if (pane.IsWindowsPath)
        pane.PathFits = SdkToWide(buffer.data(), pane.Path) && !pane.Path.empty();
    return pane;
}

std::wstring WithoutTrailingSlash(std::wstring path)
{
    while (path.size() > 3 && (path.back() == L'\\' || path.back() == L'/'))
        path.pop_back();
    return path;
}

} // namespace

SessionInput CaptureSession(bool withSelection)
{
    SessionInput input;
    int source = SalamanderGeneral->GetSourcePanel();
    int target = source == PANEL_LEFT ? PANEL_RIGHT : PANEL_LEFT;
    input.Source = CapturePane(PANEL_SOURCE, source);
    input.Target = CapturePane(PANEL_TARGET, target);
    BOOL alwaysOnTop = FALSE;
    SalamanderGeneral->GetConfigParameter(SALCFG_ALWAYSONTOP, &alwaysOnTop, sizeof(alwaysOnTop), NULL);
    input.AlwaysOnTop = alwaysOnTop != FALSE;
    if (!input.Source.IsWindowsPath)
        return input;

    BOOL isDir = FALSE;
    const CFileData* focused = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
    if (focused != NULL && focused->Name != NULL && strcmp(focused->Name, "..") != 0)
    {
        SdkToWide(focused->Name, input.FocusedName);
        input.FocusedIsDir = isDir != FALSE;
    }
    if (withSelection)
    {
        int files = 0, dirs = 0;
        SalamanderGeneral->GetPanelSelection(PANEL_SOURCE, &files, &dirs);
        if (files + dirs > 0)
        {
            int index = 0;
            const CFileData* item;
            while ((item = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir)) != NULL)
            {
                std::wstring name;
                // A name that is not valid UTF-8 cannot be addressed safely; it is left out of the scope.
                if (item->Name != NULL && strcmp(item->Name, "..") != 0 && SdkToWide(item->Name, name))
                    input.Selected.push_back(name);
            }
        }
    }
    return input;
}

bool RequestFocus(int panel, const std::wstring& directory, const std::wstring& name, bool onlyIfShowing)
{
    FocusRequestData request;
    request.Panel = panel;
    request.WideDirectory = WithoutTrailingSlash(handoff::StripLongPrefix(directory));
    request.OnlyIfShowing = onlyIfShowing;
    if (!WideToSdk(request.WideDirectory, request.Directory) || !WideToSdk(name, request.Name) ||
        request.Directory.size() >= kSdkFocusBytes || request.Name.size() >= kSdkFocusBytes)
        return false;
    {
        std::lock_guard<std::mutex> lock(FocusLock);
        FocusQueue.push_back(request);
    }
    // Delivered when the main thread is idle, as FocusNameInPanel is main-thread only.
    SalamanderGeneral->PostMenuExtCommand(CMD_INTERNAL_FOCUS, TRUE);
    return true;
}

void ProcessFocusRequests()
{
    FocusRequestData request;
    {
        std::lock_guard<std::mutex> lock(FocusLock);
        if (FocusQueue.empty())
            return;
        // Only the latest request reflects what the user wants to see now.
        request = FocusQueue.back();
        FocusQueue.clear();
    }
    if (request.OnlyIfShowing)
    {
        std::vector<char> buffer((size_t)kSdkPathBufferBytes, 0);
        int type = 0;
        std::wstring current;
        if (!SalamanderGeneral->GetPanelPath(request.Panel, buffer.data(), (int)buffer.size(), &type, NULL) ||
            type != PATH_TYPE_WINDOWS || !SdkToWide(buffer.data(), current) ||
            !handoff::EqualsNoCase(WithoutTrailingSlash(current), request.WideDirectory))
            return;
    }
    SalamanderGeneral->SkipOneActivateRefresh();
    SalamanderGeneral->FocusNameInPanel(request.Panel, request.Directory.c_str(), request.Name.c_str());
}

void NotifyPathChanged(const std::wstring& path)
{
    std::string utf8;
    if (WideToSdk(WithoutTrailingSlash(handoff::StripLongPrefix(path)), utf8))
        SalamanderGeneral->PostChangeOnPathNotification(utf8.c_str(), FALSE);
}
