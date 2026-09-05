// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "..\\..\\common\\monotonic_time.h"

// Progress dialog shown while scanning a volume for deleted files.
class CSnapshotProgressDlg : public CDialog
{
protected:
    CGUIProgressBarAbstract* ProgressBar;
    BOOL WantCancel; // TRUE when user wants Cancel

public:
    CSnapshotProgressDlg(HWND parent, CObjectOrigin origin = ooStandard);
    void SetProgressText(int resID);
    void SetProgressText(int resID, int number);
    void SetProgress(DWORD progress);
    BOOL GetWantCancel();

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Dual-progress dialog used while copying recovered files to disk.
class CCopyProgressDlg : public CDialog
{
protected:
    CGUIProgressBarAbstract *ProgressBar1, *ProgressBar2;
    CGUIStaticTextAbstract *Label1, *Label2;
    BOOL WantCancel;
    char SrcName[MAX_PATH], DestName[MAX_PATH];
    CMonotonicTimePoint LastTick;
    DWORD FileProgress, TotalProgress;
    BOOL Changed[4];

public:
    CCopyProgressDlg(HWND parent, CObjectOrigin origin = ooStandard);
    void SetSourceFileName(const char* fileName);
    void SetDestFileName(const char* fileName);
    void SetFileProgress(DWORD progress);
    void SetTotalProgress(DWORD progress);
    BOOL GetWantCancel();
    void UpdateControls(BOOL now = FALSE);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Connect dialog that picks a volume or image to scan for deleted files.
class CConnectDialog : public CDialog
{
public:
    CConnectDialog(HWND parent, int panel);
    char Volume[MAX_PATH];
    int Panel;

protected:
    HWND hList;
    HIMAGELIST hDrivesImg;

    void AddVolumeDetails(const char* root, const char* volumeID, const char* volumeFS,
                          const CQuadWord& bytesTotal, const CQuadWord& bytesFree,
                          const char* volumeName, int serial, BOOL selected);
    void InitDrives();
    BOOL OnDialogOK();
    void OnImageBrowse();

public:
    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Prompts for a new file name when recovering a file with an invalid or duplicate name.
class CFileNameDialog : public CDialog
{
public:
    CFileNameDialog(HWND parent, char* filename);
    BOOL AllPressed;

protected:
    char* FileName;

    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Undelete plugin configuration dialog (scan options, temp path, warnings).
class CConfigDialog : public CDialog
{
public:
    CConfigDialog(HWND parent);

protected:
    virtual void Transfer(CTransferInfo& ti);
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Asks for the target folder when restoring encrypted files.
class CRestoreDialog : public CDialog
{
public:
    CRestoreDialog(HWND parent);

    char TargetPath[MAX_PATH];

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// Progress dialog shown while restoring encrypted files.
class CRestoreProgressDlg : public CCopyProgressDlg
{
public:
    CRestoreProgressDlg(HWND parent, CObjectOrigin origin = ooStandard)
        : CCopyProgressDlg(parent, origin) {}

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
