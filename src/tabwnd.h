// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

//
// ****************************************************************************

class CTabWindow : public CWindow
{
public:
    CFilesWindow* FilesWindow;

    //  protected:
    //    TDirectArray<CTabItem> TabItems;

public:
    CTabWindow(CFilesWindow* filesWindow);
    ~CTabWindow();

    void DestroyWindow();
    int GetNeededHeight();

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};
