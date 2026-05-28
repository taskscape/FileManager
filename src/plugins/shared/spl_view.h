// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Taskscape Ltd
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_view) // keep structures independent of the current alignment setting
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

struct CSalamanderPluginViewerData;

//
// ****************************************************************************
// CPluginInterfaceForViewerAbstract
//

class CPluginInterfaceForViewerAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForViewerEncapsulation)
    friend class CPluginInterfaceForViewerEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "file viewer", called on request to open viewer and load file
    // 'name', 'left'+'right'+'width'+'height'+'showCmd'+'alwaysOnTop' is the recommended
    // window placement; if 'returnLock' is FALSE, 'lock'+'lockOwner' have no meaning; if
    // 'returnLock' is TRUE, viewer should return system event 'lock' in a nonsignaled state;
    // passes at moment of finishing viewing file 'name' (file is at this moment removed
    // from temporary directory), further should return TRUE in 'lockOwner' if object 'lock' should be closed
    // by caller (FALSE means viewer cancels 'lock' itself - in this case viewer must for
    // switching 'lock' to the signaled state, use CSalamanderGeneralAbstract::UnlockFileInCache);
    // if viewer does not set 'lock' (remains NULL) file 'name' is valid only until end of calling this
    // method ViewFile; if 'viewerData' is not NULL, it is passing extended viewer parameters (see
    // CSalamanderGeneralAbstract::ViewFileInPluginViewer); 'enumFilesSourceUID' is the UID of the source (panel
    // or Find window), from which viewer is opened, if -1, source is unknown (archives and
    // file_systems or Alt+F11, etc.) - see e.g. CSalamanderGeneralAbstract::GetNextFileNameForViewer;
    // 'enumFilesCurrentIndex' is index of opened file in source (panel or Find window), if -1,
    // source or index is not known; returns TRUE on success (FALSE means failure, and 'lock'
    // and 'lockOwner' have no meaning in this case)
    virtual BOOL WINAPI ViewFile(const char* name, int left, int top, int width, int height,
                                 UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
                                 BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                                 int enumFilesSourceUID, int enumFilesCurrentIndex) = 0;

    // function for "file viewer", called on request to open viewer and load file
    // 'name'; this function should not display any "invalid file format" windows; those
    // windows are displayed only when calling ViewFile from this interface; detects whether
    // file 'name' is viewable (for example, file has a matching signature) in the viewer
    // and if it is, returns TRUE; if it returns FALSE, Salamander tries to find another
    // viewer (v prioritnim seznamu vieweru, viz konfiguracni stranka Viewers)
    virtual BOOL WINAPI CanViewFile(const char* name) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_view)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
