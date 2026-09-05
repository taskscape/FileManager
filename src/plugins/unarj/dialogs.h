// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <strsafe.h> // counted bounded copies (StringCchCopyNA)

// Shared unpacker dialog helper that owns the HWND, centers the dialog, and subclasses statics.
class CDlgRoot
{
public:
    HWND Parent;
    HWND Dlg;

    CDlgRoot(HWND parent)
    {
        Parent = parent;
        Dlg = NULL;
    }

    void CenterDlgToParent();
    void SubClassStatic(DWORD wID, BOOL subclass);
};

// Prompts for the next ARJ volume when a multi-volume unpack continues.
class CNextVolumeDialog : public CDlgRoot
{
    char* VolumeName;
    char* PrevName;
    char CurrentPath[MAX_PATH];

public:
    CNextVolumeDialog(HWND parent, char* volumeName, char* prevName) : CDlgRoot(parent)
    {
        VolumeName = volumeName;
        PrevName = prevName;
        StringCchCopyNA(CurrentPath, MAX_PATH, VolumeName, MAX_PATH); // counted bounded copy instead of lstrcpyn
        SalamanderGeneral->CutDirectory(CurrentPath);
    }
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnBrowse(WORD wNotifyCode, WORD wID, HWND hwndCtl);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR NextVolumeDialog(HWND parent, char* volumeName, char* prevName);

// Warns that a file continues from a previous ARJ volume and asks how to proceed.
class CContinuedFileDialog : public CDlgRoot
{
    const char* File;

public:
    CContinuedFileDialog(HWND parent, const char* file) : CDlgRoot(parent)
    {
        File = file;
    }
    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ContinuedFileDialog(HWND parent, const char* file);

// UnARJ configuration dialog (skip continued files, volume warnings).
class CConfigDialog : public CDlgRoot
{

public:
    CConfigDialog(HWND parent) : CDlgRoot(parent) { ; }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnInit(WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR ConfigDialog(HWND parent);

// Warns that the ARJ archive cannot be listed completely (missing volumes).
class CAttentionDialog : public CDlgRoot
{
public:
    CAttentionDialog(HWND parent) : CDlgRoot(parent) { ; }

    INT_PTR Proceed();

    INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL OnOK(WORD wNotifyCode, WORD wID, HWND hwndCtl);
};

INT_PTR AttentionDialog(HWND parent);
