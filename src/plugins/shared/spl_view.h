// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_view) // aby byly struktury nezavisle na nastavenem zarovnavani
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
    // 'name', 'left'+'right'+'width'+'height'+'showCmd'+'alwaysOnTop' je doporucene umisteni
    // okna, je-li 'returnLock' FALSE nemaji 'lock'+'lockOwner' zadny vyznam, je-li 'returnLock'
    // TRUE, mel by viewer vratit system-event 'lock' v nonsignaled stavu, do signaled stavu 'lock'
    // passes at moment of finishing viewing file 'name' (file is at this moment removed
    // from temporary directory), further should return TRUE in 'lockOwner' if object 'lock' should be closed
    // by caller (FALSE means viewer cancels 'lock' itself - in this case viewer must for
    // prechod 'lock' do signaled stavu pouzit metodu CSalamanderGeneralAbstract::UnlockFileInCache);
    // if viewer does not set 'lock' (remains NULL) file 'name' is valid only until end of calling this
    // method ViewFile; if 'viewerData' is not NULL, it is passing extended viewer parameters (see
    // CSalamanderGeneralAbstract::ViewFileInPluginViewer); 'enumFilesSourceUID' je UID zdroje (panelu
    // or Find window), from which viewer is opened, if -1, source is unknown (archives and
    // file_systems or Alt+F11, etc.) - see e.g. CSalamanderGeneralAbstract::GetNextFileNameForViewer;
    // 'enumFilesCurrentIndex' is index of opened file in source (panel or Find window), if -1,
    // neni zdroj nebo index znamy; vraci TRUE pri uspechu (FALSE znamena neuspech, 'lock' a
    // 'lockOwner' v tomto pripade nemaji zadny vyznam)
    virtual BOOL WINAPI ViewFile(const char* name, int left, int top, int width, int height,
                                 UINT showCmd, BOOL alwaysOnTop, BOOL returnLock, HANDLE* lock,
                                 BOOL* lockOwner, CSalamanderPluginViewerData* viewerData,
                                 int enumFilesSourceUID, int enumFilesCurrentIndex) = 0;

    // function for "file viewer", called on request to open viewer and load file
    // 'name'; tato fuknce by nemela zobrazovat zadna okna typu "invalid file format", tato
    // okna se zobrazi az pri volani metody ViewFile tohoto rozhrani; zjisti jestli je
    // soubor 'name' zobrazitelny (napr. soubor ma odpovidajici signaturu) ve vieweru
    // a pokud je, vraci TRUE; pokud vrati FALSE, zkusi Salamander pro 'name' najit jiny
    // viewer (v prioritnim seznamu vieweru, viz konfiguracni stranka Viewers)
    virtual BOOL WINAPI CanViewFile(const char* name) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_view)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
