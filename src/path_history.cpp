// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <shobjidl.h>

#include "common/thread_owner.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "pack.h"
#include "execute.h"
#include "shellib.h"
#include "menu.h"

// Path/file history and scroll-position memory extracted from path_utils.cpp as a
// mechanical move; all declarations already live in salamand.h/fileswnd.h/mainwnd.h.
//
// ****************************************************************************
// CPathHistoryItem
//

CPathHistoryItem::CPathHistoryItem(int type, const char* pathOrArchiveOrFSName,
                                   const char* archivePathOrFSUserPart, HICON hIcon,
                                   CPluginFSInterfaceAbstract* pluginFS)
{
    Type = type;
    HIcon = hIcon;
    PluginFS = NULL;

    TopIndex = -1;
    FocusedName = NULL;

    if (Type == 0) // disk
    {
        char root[MAX_PATH];
        GetRootPath(root, pathOrArchiveOrFSName);
        const char* e = pathOrArchiveOrFSName + strlen(pathOrArchiveOrFSName);
        if ((int)strlen(root) < e - pathOrArchiveOrFSName || // not a root path
            pathOrArchiveOrFSName[0] == '\\')                // it is a UNC path
        {
            if (*(e - 1) == '\\')
                e--;
            PathOrArchiveOrFSName = (char*)malloc((e - pathOrArchiveOrFSName) + 1);
            if (PathOrArchiveOrFSName != NULL)
            {
                memcpy(PathOrArchiveOrFSName, pathOrArchiveOrFSName, e - pathOrArchiveOrFSName);
                PathOrArchiveOrFSName[e - pathOrArchiveOrFSName] = 0;
            }
        }
        else // it is a normal root path (c:\)
        {
            PathOrArchiveOrFSName = DupStr(root);
        }
        if (PathOrArchiveOrFSName == NULL)
        {
            TRACE_E(LOW_MEMORY);
            if (PathOrArchiveOrFSName != NULL)
                free(PathOrArchiveOrFSName);
            PathOrArchiveOrFSName = NULL;
            HIcon = NULL;
        }
        ArchivePathOrFSUserPart = NULL;
    }
    else
    {
        if (Type == 1 || Type == 2) // archive or FS (only copies of both strings)
        {
            if (Type == 2)
                PluginFS = pluginFS;
            PathOrArchiveOrFSName = DupStr(pathOrArchiveOrFSName);
            ArchivePathOrFSUserPart = DupStr(archivePathOrFSUserPart);
            if (PathOrArchiveOrFSName == NULL || ArchivePathOrFSUserPart == NULL)
            {
                TRACE_E(LOW_MEMORY);
                if (PathOrArchiveOrFSName != NULL)
                    free(PathOrArchiveOrFSName);
                if (ArchivePathOrFSUserPart != NULL)
                    free(ArchivePathOrFSUserPart);
                PathOrArchiveOrFSName = NULL;
                ArchivePathOrFSUserPart = NULL;
                HIcon = NULL;
            }
        }
        else
            TRACE_E("CPathHistoryItem::CPathHistoryItem(): unknown 'type'");
    }
}

CPathHistoryItem::~CPathHistoryItem()
{
    if (FocusedName != NULL)
        free(FocusedName);
    if (PathOrArchiveOrFSName != NULL)
        free(PathOrArchiveOrFSName);
    if (ArchivePathOrFSUserPart != NULL)
        free(ArchivePathOrFSUserPart);
    if (HIcon != NULL)
        HANDLES(DestroyIcon(HIcon));
}

void CPathHistoryItem::ChangeData(int topIndex, const char* focusedName)
{
    TopIndex = topIndex;
    if (FocusedName != NULL)
    {
        if (focusedName != NULL && strcmp(FocusedName, focusedName) == 0)
            return; // no change -> end
        free(FocusedName);
    }
    if (focusedName != NULL)
        FocusedName = DupStr(focusedName);
    else
        FocusedName = NULL;
}

void CPathHistoryItem::GetPath(char* buffer, int bufferSize)
{
    char* origBuffer = buffer;
    if (bufferSize == 0)
        return;
    if (PathOrArchiveOrFSName == NULL)
    {
        buffer[0] = 0;
        return;
    }
    int l = (int)strlen(PathOrArchiveOrFSName) + 1;
    if (l > bufferSize)
        l = bufferSize;
    memcpy(buffer, PathOrArchiveOrFSName, l - 1);
    buffer[l - 1] = 0;
    if (Type == 1 || Type == 2) // archive or FS
    {
        buffer += l - 1;
        bufferSize -= l - 1;
        char* s = ArchivePathOrFSUserPart;
        if (*s != 0 || Type == 2)
        {
            if (bufferSize >= 2) // append '\\' or ':'
            {
                *buffer++ = Type == 1 ? '\\' : ':';
                *buffer = 0;
                bufferSize--;
            }
            l = (int)strlen(s) + 1;
            if (l > bufferSize)
                l = bufferSize;
            memcpy(buffer, s, l - 1);
            buffer[l - 1] = 0;
        }
    }
}

void CPathHistoryItem::GetPathW(WCHAR* buffer, int bufferSizeInChars)
{
    if (buffer == NULL || bufferSizeInChars <= 0)
        return;
    buffer[0] = 0;
    char utf8Buf[2 * MAX_PATH];
    GetPath(utf8Buf, sizeof(utf8Buf));
    MultiByteToWideChar(CP_UTF8, 0, utf8Buf, -1, buffer, bufferSizeInChars);
    buffer[bufferSizeInChars - 1] = 0;
}

void CPathHistoryItem::GetPathW(CPathW& path)
{
    char utf8Buf[2 * MAX_PATH];
    GetPath(utf8Buf, sizeof(utf8Buf));
    path.Set(utf8Buf);
}

HICON
CPathHistoryItem::GetIcon()
{
    return HIcon;
}

BOOL DuplicateAmpersands(char* buffer, int bufferSize, BOOL skipFirstAmpersand)
{
    if (buffer == NULL)
    {
        TRACE_E("Unexpected situation (1) in DuplicateAmpersands()");
        return FALSE;
    }
    char* s = buffer;
    int l = (int)strlen(buffer);
    if (l >= bufferSize)
    {
        TRACE_E("Unexpected situation (2) in DuplicateAmpersands()");
        return FALSE;
    }
    BOOL ret = TRUE;
    BOOL first = TRUE;
    while (*s != 0)
    {
        if (*s == '&')
        {
            if (!(skipFirstAmpersand && first))
            {
                if (l + 1 < bufferSize)
                {
                    memmove(s + 1, s, l - (s - buffer) + 1); // zdvojime '&'
                    l++;
                    s++;
                }
                else // nevejde se, orizneme buffer
                {
                    ret = FALSE;
                    memmove(s + 1, s, l - (s - buffer)); // zdvojime '&', orizneme o znak
                    buffer[l] = 0;
                    s++;
                }
            }
            first = FALSE;
        }
        s++;
    }
    return ret;
}

void RemoveAmpersands(char* text)
{
    if (text == NULL)
    {
        TRACE_E("Unexpected situation in RemoveAmpersands().");
        return;
    }
    char* s = text;
    while (*s != 0 && *s != '&')
        s++;
    if (*s != 0)
    {
        char* d = s;
        while (*s != 0)
        {
            if (*s != '&')
                *d++ = *s++;
            else
            {
                if (*(s + 1) == '&')
                    *d++ = *s++; // dvojice "&&" -> nahradime '&'
                s++;
            }
        }
        *d = 0;
    }
}

BOOL CPathHistoryItem::Execute(CFilesWindow* panel)
{
    BOOL ret = TRUE; // standardne vracime uspech
    char errBuf[MAX_PATH + 200];
    if (PathOrArchiveOrFSName != NULL) // jsou platna data
    {
        int failReason;
        BOOL clear = TRUE;
        if (Type == 0) // disk
        {
            if (!panel->ChangePathToDisk(panel->HWindow, PathOrArchiveOrFSName, TopIndex, FocusedName, NULL,
                                         TRUE, FALSE, FALSE, &failReason))
            {
                if (failReason == CHPPFR_CANNOTCLOSEPATH)
                {
                    ret = FALSE;   // zustavame na miste
                    clear = FALSE; // zadny skok, neni treba mazat top-indexy
                }
            }
        }
        else
        {
            if (Type == 1) // archiv
            {
                if (!panel->ChangePathToArchive(PathOrArchiveOrFSName, ArchivePathOrFSUserPart, TopIndex,
                                                FocusedName, FALSE, NULL, TRUE, &failReason, FALSE, FALSE, TRUE))
                {
                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                    {
                        ret = FALSE;   // zustavame na miste
                        clear = FALSE; // zadny skok, neni treba mazat top-indexy
                    }
                    else
                    {
                        if (failReason == CHPPFR_SHORTERPATH || failReason == CHPPFR_FILENAMEFOCUSED)
                        {
                            _snprintf_s(errBuf, _countof(errBuf), _TRUNCATE, LoadStr(IDS_PATHINARCHIVENOTFOUND), ArchivePathOrFSUserPart);
                            SalMessageBox(panel->HWindow, errBuf, LoadStr(IDS_ERRORCHANGINGDIR),
                                          MB_OK | MB_ICONEXCLAMATION);
                        }
                    }
                }
            }
            else
            {
                if (Type == 2) // FS
                {
                    BOOL done = FALSE;
                    // if the FS interface where the path was last opened is known, try
                    // to find it among detached ones and use it
                    if (MainWindow != NULL && PluginFS != NULL && // if the FS interface is known
                        (!panel->Is(ptPluginFS) ||                // and if it is not currently in the panel
                         !panel->GetPluginFS()->Contains(PluginFS)))
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int i;
                        for (i = 0; i < list->Count; i++)
                        {
                            if (list->At(i)->Contains(PluginFS))
                            {
                                done = TRUE;
                                // try changing to the requested path (it was there last time, no need to test IsOurPath),
                                // and attach the detached FS
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName, TRUE, &failReason,
                                                                   PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
                                {
                                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                                    {
                                        ret = FALSE;   // stay in place
                                        clear = FALSE; // no jump, no need to delete top-indexes
                                    }
                                }

                                break; // done, another match with PluginFS is no longer possible
                            }
                        }
                    }

                    // if the previous part failed and the path cannot be listed in the FS interface in the panel,
                    // try to find a detached FS interface that can list the path (so a new FS
                    // does not open unnecessarily)
                    int fsNameIndex;
                    BOOL convertPathToInternalDummy = FALSE;
                    if (!done && MainWindow != NULL &&
                        (!panel->Is(ptPluginFS) || // FS interface in panel cannot list the path
                         !panel->GetPluginFS()->Contains(PluginFS) &&
                             !panel->IsPathFromActiveFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart,
                                                        fsNameIndex, convertPathToInternalDummy)))
                    {
                        CDetachedFSList* list = MainWindow->DetachedFSList;
                        int i;
                        for (i = 0; i < list->Count; i++)
                        {
                            if (list->At(i)->IsPathFromThisFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
                            {
                                done = TRUE;
                                // try changing to the requested path and attach the detached FS
                                if (!panel->ChangePathToDetachedFS(i, TopIndex, FocusedName, TRUE, &failReason,
                                                                   PathOrArchiveOrFSName, ArchivePathOrFSUserPart))
                                {
                                    if (failReason == CHPPFR_SHORTERPATH) // almost success (path is only shortened) (CHPPFR_FILENAMEFOCUSED is not possible here)
                                    {                                     // restore record about FS interface
                                        if (panel->Is(ptPluginFS))
                                            PluginFS = panel->GetPluginFS()->GetInterface();
                                    }
                                    if (failReason == CHPPFR_CANNOTCLOSEPATH)
                                    {
                                        ret = FALSE;   // stay in place
                                        clear = FALSE; // no jump, no need to delete top-indexes
                                    }
                                }
                                else // complete success
                                {    // restore record about FS interface
                                    if (panel->Is(ptPluginFS))
                                        PluginFS = panel->GetPluginFS()->GetInterface();
                                }

                                break;
                            }
                        }
                    }

                    // when there is no other way, open a new FS interface or only change the path on the active FS interface
                    if (!done)
                    {
                        if (!panel->ChangePathToPluginFS(PathOrArchiveOrFSName, ArchivePathOrFSUserPart, TopIndex,
                                                         FocusedName, FALSE, 2, NULL, TRUE, &failReason))
                        {
                            if (failReason == CHPPFR_SHORTERPATH ||   // almost success (path is only shortened)
                                failReason == CHPPFR_FILENAMEFOCUSED) // almost success (path only changed to a file and it was focused)
                            {                                         // restore record about FS interface
                                if (panel->Is(ptPluginFS))
                                    PluginFS = panel->GetPluginFS()->GetInterface();
                            }
                            if (failReason == CHPPFR_CANNOTCLOSEPATH)
                            {
                                ret = FALSE;   // stay in place
                                clear = FALSE; // no jump, no need to delete top-indexes
                            }
                        }
                        else // complete success
                        {    // restore record about FS interface
                            if (panel->Is(ptPluginFS))
                                PluginFS = panel->GetPluginFS()->GetInterface();
                        }
                    }
                }
            }
        }
        if (clear)
            panel->TopIndexMem.Clear(); // long jump
    }
    UpdateWindow(MainWindow->HWindow);
    return ret;
}

BOOL CPathHistoryItem::IsTheSamePath(CPathHistoryItem& item, CPluginFSInterfaceEncapsulation* curPluginFS)
{
    char buf1[2 * MAX_PATH];
    char buf2[2 * MAX_PATH];
    if (Type == item.Type)
    {
        if (Type == 0) // disk
        {
            GetPath(buf1, 2 * MAX_PATH);
            item.GetPath(buf2, 2 * MAX_PATH);
            if (StrICmp(buf1, buf2) == 0)
                return TRUE;
        }
        else
        {
            if (Type == 1) // archiv
            {
                if (StrICmp(PathOrArchiveOrFSName, item.PathOrArchiveOrFSName) == 0 &&  // archive file is "case-insensitive"
                    strcmp(ArchivePathOrFSUserPart, item.ArchivePathOrFSUserPart) == 0) // path in archive is "case-sensitive"
                {
                    return TRUE;
                }
            }
            else
            {
                if (Type == 2) // FS
                {
                    if (StrICmp(PathOrArchiveOrFSName, item.PathOrArchiveOrFSName) == 0) // fs-name je "case-insensitive"
                    {
                        if (strcmp(ArchivePathOrFSUserPart, item.ArchivePathOrFSUserPart) == 0) // fs-user-part je "case-sensitive"
                            return TRUE;
                        if (curPluginFS != NULL && // resime jeste pripad, kdy jsou obe fs-user-part shodne z duvodu, ze pro ne FS vrati TRUE z IsCurrentPath (obecne bysme museli zavest metodu pro porovnani dvou fs-user-part, coz se mi ale jen kvuli historiim nechce, treba casem...)
                            StrICmp(PathOrArchiveOrFSName, curPluginFS->GetPluginFSName()) == 0)
                        {
                            int fsNameInd = curPluginFS->GetPluginFSNameIndex();
                            if (curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, ArchivePathOrFSUserPart) &&
                                curPluginFS->IsCurrentPath(fsNameInd, fsNameInd, item.ArchivePathOrFSUserPart))
                            {
                                return TRUE;
                            }
                        }
                    }
                }
            }
        }
    }
    return FALSE;
}

//
// ****************************************************************************
// CPathHistory
//

CPathHistory::CPathHistory(BOOL dontChangeForwardIndex) : Paths(10, 5)
{
    ForwardIndex = -1;
    Lock = FALSE;
    DontChangeForwardIndex = dontChangeForwardIndex;
    NewItem = NULL;
}

CPathHistory::~CPathHistory()
{
    if (NewItem != NULL)
        delete NewItem;
}

void CPathHistory::ClearHistory()
{
    Paths.DestroyMembers();

    if (NewItem != NULL)
    {
        delete NewItem;
        NewItem = NULL;
    }
}

void CPathHistory::ClearPluginFSFromHistory(CPluginFSInterfaceAbstract* fs)
{
    if (NewItem != NULL && NewItem->PluginFS == fs)
    {
        NewItem->PluginFS = NULL; // fs byl prave zavren -> NULLovani
    }
    int i;
    for (i = 0; i < Paths.Count; i++)
    {
        CPathHistoryItem* item = Paths[i];
        if (item->Type == 2 && item->PluginFS == fs)
            item->PluginFS = NULL; // fs byl prave zavren -> NULLovani
    }
}

void CPathHistory::FillBackForwardPopupMenu(CMenuPopup* popup, BOOL forward)
{
    // item IDs must be in interval <1..?>
    char buffer[2 * MAX_PATH];

    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;

    if (forward)
    {
        if (ForwardIndex != -1)
        {
            int id = 1;
            int i;
            for (i = ForwardIndex; i < Paths.Count; i++)
            {
                Paths[i]->GetPath(buffer, 2 * MAX_PATH);
                mii.String = buffer;
                mii.ID = id++;
                popup->InsertItem(-1, TRUE, &mii);
            }
        }
    }
    else
    {
        int id = 2;
        int count = (ForwardIndex == -1) ? Paths.Count : ForwardIndex;
        int i;
        for (i = count - 2; i >= 0; i--)
        {
            Paths[i]->GetPath(buffer, 2 * MAX_PATH);
            mii.String = buffer;
            mii.ID = id++;
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
}

void CPathHistory::FillHistoryPopupMenu(CMenuPopup* popup, DWORD firstID, int maxCount,
                                        BOOL separator)
{
    char buffer[2 * MAX_PATH];

    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING | MENU_MASK_ICON;
    mii.Type = MENU_TYPE_STRING;

    int firstIndex = popup->GetItemCount();

    int added = 0; // pocet pridanych polozek

    int id = firstID;
    int count = (ForwardIndex == -1) ? Paths.Count : ForwardIndex;
    int i;
    for (i = count - 1; i >= 0; i--)
    {
        if (maxCount != -1 && added >= maxCount)
            break;
        Paths[i]->GetPath(buffer, 2 * MAX_PATH);
        mii.String = buffer;
        mii.HIcon = Paths[i]->GetIcon();
        mii.ID = id++;
        popup->InsertItem(-1, TRUE, &mii);
        added++;
    }

    if (added > 0)
        popup->AssignHotKeys();

    if (separator && added > 0)
    {
        // vlozime separator
        mii.Mask = MENU_MASK_TYPE;
        mii.Type = MENU_TYPE_SEPARATOR;
        popup->InsertItem(firstIndex, TRUE, &mii);
    }
}

void CPathHistory::Execute(int index, BOOL forward, CFilesWindow* panel, BOOL allItems, BOOL removeItem)
{
    if (Lock)
        return;

    CPathHistoryItem* item = NULL; // if we should remove the path, store a pointer to it for lookup

    BOOL change = TRUE;
    if (forward)
    {
        if (HasForward())
        {
            if (ForwardIndex + index - 1 < Paths.Count)
            {
                Lock = TRUE;
                item = Paths[ForwardIndex + index - 1];
                change = item->Execute(panel);
                if (!change)
                    item = NULL; // nepodarilo se zmenit cestu => nechame ji v historii
                Lock = FALSE;
            }
            if (change && !DontChangeForwardIndex)
                ForwardIndex = ForwardIndex + index;
            if (ForwardIndex >= Paths.Count)
                ForwardIndex = -1;
        }
    }
    else
    {
        index--; // z duvodu pocatku cislovani od 2 v FillPopupMenu
        if (HasBackward() || allItems && HasPaths())
        {
            int count = ((ForwardIndex == -1) ? Paths.Count : ForwardIndex) - 1;
            if (count - index >= 0) // mame kam jit (neni to posledni polozka)
            {
                if (count - index < Paths.Count)
                {
                    Lock = TRUE;
                    item = Paths[count - index];
                    change = item->Execute(panel);
                    if (!change)
                        item = NULL; // nepodarilo se zmenit cestu => nechame ji v historii
                    Lock = FALSE;
                }
                if (change && !DontChangeForwardIndex)
                    ForwardIndex = count - index + 1;
            }
        }
    }
    IdleRefreshStates = TRUE; // pri pristim Idle vynutime kontrolu stavovych promennych

    if (NewItem != NULL)
    {
        AddPathUnique(NewItem->Type, NewItem->PathOrArchiveOrFSName, NewItem->ArchivePathOrFSUserPart,
                      NewItem->HIcon, NewItem->PluginFS, NULL);
        NewItem->HIcon = NULL; // zodpovednost za destrukci ikony si prebrala metoda AddPathUnique
        delete NewItem;
        NewItem = NULL;
    }
    if (removeItem && item != NULL)
    {
        if (DontChangeForwardIndex)
        {
            // vyradime execlou polozku ze seznamu
            Lock = TRUE;
            int i;
            for (i = 0; i < Paths.Count; i++)
            {
                if (Paths[i] == item)
                {
                    Paths.Delete(i);
                    break;
                }
            }
            Lock = FALSE;
        }
        else
        {
            TRACE_E("Path removing is not supported for this setting.");
        }
    }
}

void CPathHistory::ChangeActualPathData(int type, const char* pathOrArchiveOrFSName,
                                        const char* archivePathOrFSUserPart,
                                        CPluginFSInterfaceAbstract* pluginFS,
                                        CPluginFSInterfaceEncapsulation* curPluginFS,
                                        int topIndex, const char* focusedName)
{
    if (Paths.Count > 0)
    {
        CPathHistoryItem n(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, pluginFS);
        CPathHistoryItem* n2 = NULL;
        if (ForwardIndex != -1)
        {
            if (ForwardIndex > 0)
                n2 = Paths[ForwardIndex - 1];
            else
                TRACE_E("Unexpected situation in CPathHistory::ChangeActualPathData");
        }
        else
            n2 = Paths[Paths.Count - 1];

        if (n2 != NULL && n.IsTheSamePath(*n2, curPluginFS)) // stejne cesty -> zmenime data
            n2->ChangeData(topIndex, focusedName);
    }
}

void CPathHistory::RemoveActualPath(int type, const char* pathOrArchiveOrFSName,
                                    const char* archivePathOrFSUserPart,
                                    CPluginFSInterfaceAbstract* pluginFS,
                                    CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (Lock)
        return;
    if (Paths.Count > 0)
    {
        if (ForwardIndex == -1)
        {
            CPathHistoryItem n(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, pluginFS);
            CPathHistoryItem* n2 = Paths[Paths.Count - 1];
            if (n.IsTheSamePath(*n2, curPluginFS)) // stejne cesty -> smazeme zaznam
                Paths.Delete(Paths.Count - 1);
        }
        else
            TRACE_E("Unexpected situation in CPathHistory::RemoveActualPath(): ForwardIndex != -1");
    }
}

void CPathHistory::AddPath(int type, const char* pathOrArchiveOrFSName, const char* archivePathOrFSUserPart,
                           CPluginFSInterfaceAbstract* pluginFS, CPluginFSInterfaceEncapsulation* curPluginFS)
{
    if (Lock)
        return;

    CPathHistoryItem* n = new CPathHistoryItem(type, pathOrArchiveOrFSName, archivePathOrFSUserPart,
                                               NULL, pluginFS);
    if (n == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    if (Paths.Count > 0)
    {
        CPathHistoryItem* n2 = NULL;
        if (ForwardIndex != -1)
        {
            if (ForwardIndex > 0)
                n2 = Paths[ForwardIndex - 1];
            else
                TRACE_E("Unexpected situation in CPathHistory::AddPath");
        }
        else
            n2 = Paths[Paths.Count - 1];

        if (n2 != NULL && n->IsTheSamePath(*n2, curPluginFS))
        {
            delete n;
            return; // stejne cesty -> neni co delat
        }
    }

    // cestu je opravdu potreba pridat ...
    if (ForwardIndex != -1)
    {
        while (Paths.IsGood() && ForwardIndex < Paths.Count)
        {
            Paths.Delete(ForwardIndex);
        }
        ForwardIndex = -1;
    }
    while (Paths.IsGood() && Paths.Count > PATH_HISTORY_SIZE)
    {
        Paths.Delete(0);
    }
    Paths.Add(n);
    if (!Paths.IsGood())
    {
        delete n;
        Paths.ResetState();
    }
}

void CPathHistory::AddPathW(int type, const WCHAR* pathOrArchiveOrFSName, const WCHAR* archivePathOrFSUserPart,
                            CPluginFSInterfaceAbstract* pluginFS, CPluginFSInterfaceEncapsulation* curPluginFS)
{
    CPathW name1(pathOrArchiveOrFSName);
    CPathW name2(archivePathOrFSUserPart);
    char buf1[2 * MAX_PATH];
    char buf2[2 * MAX_PATH];
    name1.ToUtf8(buf1, sizeof(buf1));
    name2.ToUtf8(buf2, sizeof(buf2));
    AddPath(type, buf1, buf2, pluginFS, curPluginFS);
}

void CPathHistory::AddPathUnique(int type, const char* pathOrArchiveOrFSName, const char* archivePathOrFSUserPart,
                                 HICON hIcon, CPluginFSInterfaceAbstract* pluginFS,
                                 CPluginFSInterfaceEncapsulation* curPluginFS)
{
    CPathHistoryItem* n = new CPathHistoryItem(type, pathOrArchiveOrFSName, archivePathOrFSUserPart,
                                               hIcon, pluginFS);
    if (Lock)
    {
        if (NewItem != NULL)
        {
            TRACE_E("Unexpected situation in CPathHistory::AddPathUnique()");
            delete NewItem;
        }
        NewItem = n;
        return;
    }

    if (n == NULL)
    {
        TRACE_E(LOW_MEMORY);
        if (hIcon != NULL)
            HANDLES(DestroyIcon(hIcon)); // musime sestrelit ikonu
        return;
    }
    if (Paths.Count > 0)
    {
        int i;
        for (i = 0; i < Paths.Count; i++)
        {
            CPathHistoryItem* item = Paths[i];

            if (n->IsTheSamePath(*item, curPluginFS))
            {
                if (type == 2 && pluginFS != NULL)
                { // this is FS, replace pluginFS (so the path opens on the last FS of this path)
                    item->PluginFS = pluginFS;
                }
                delete n;
                if (i < Paths.Count - 1)
                {
                    // vytahneme cestu v seznamu nahoru
                    Paths.Add(item);
                    if (Paths.IsGood())
                        Paths.Detach(i); // if adding succeeded, remove the source
                    if (!Paths.IsGood())
                        Paths.ResetState();
                }
                return; // stejne cesty -> neni co delat
            }
        }
    }

    // cestu je opravdu potreba pridat ...
    if (ForwardIndex != -1)
    {
        while (Paths.IsGood() && ForwardIndex < Paths.Count)
        {
            Paths.Delete(ForwardIndex);
        }
        ForwardIndex = -1;
    }
    while (Paths.IsGood() && Paths.Count > PATH_HISTORY_SIZE)
    {
        Paths.Delete(0);
    }
    Paths.Add(n);
    if (!Paths.IsGood())
    {
        delete n;
        Paths.ResetState();
    }
}

void CPathHistory::AddPathUniqueW(int type, const WCHAR* pathOrArchiveOrFSName, const WCHAR* archivePathOrFSUserPart,
                                  HICON hIcon, CPluginFSInterfaceAbstract* pluginFS,
                                  CPluginFSInterfaceEncapsulation* curPluginFS)
{
    CPathW name1(pathOrArchiveOrFSName);
    CPathW name2(archivePathOrFSUserPart);
    char buf1[2 * MAX_PATH];
    char buf2[2 * MAX_PATH];
    name1.ToUtf8(buf1, sizeof(buf1));
    name2.ToUtf8(buf2, sizeof(buf2));
    AddPathUnique(type, buf1, buf2, hIcon, pluginFS, curPluginFS);
}

void CPathHistory::SaveToRegistry(HKEY hKey, const char* name, BOOL onlyClear)
{
    HKEY historyKey;
    if (CreateKey(hKey, name, historyKey))
    {
        ClearKey(historyKey);

        if (!onlyClear) // if the key should not only be cleared, store values from history
        {
            int index = 0;
            char buf[10];
            char path[2 * MAX_PATH];
            int i;
            for (i = 0; i < Paths.Count; i++)
            {
                CPathHistoryItem* item = Paths[i];
                switch (item->Type)
                {
                case 0: // disk
                {
                    if (strlen(item->PathOrArchiveOrFSName) >= _countof(path))
                    {
                        TRACE_E("CPathHistory::SaveToRegistry(): path is too long, skipping.");
                        continue;
                    }
                    // History navigation requires the complete stored identity.
                    if (FAILED(StringCchCopyA(path, _countof(path), item->PathOrArchiveOrFSName)))
                        continue;
                    break;
                }

                // archive & FS: pouzijeme znak ':' jako oddelovac dvou casti cesty
                // during load, determine the path type according to this character
                case 1: // archive
                case 2: // FS
                {
                    if (strlen(item->PathOrArchiveOrFSName) >= _countof(path))
                    {
                        TRACE_E("CPathHistory::SaveToRegistry(): path is too long, skipping.");
                        continue;
                    }
                    // History navigation requires the complete stored identity.
                    if (FAILED(StringCchCopyA(path, _countof(path), item->PathOrArchiveOrFSName)))
                        continue;
                    StrNCat(path, ":", 2 * MAX_PATH);
                    if (item->ArchivePathOrFSUserPart != NULL)
                        StrNCat(path, item->ArchivePathOrFSUserPart, 2 * MAX_PATH);
                    break;
                }
                default:
                {
                    TRACE_E("CPathHistory::SaveToRegistry() uknown path type");
                    continue;
                }
                }
                itoa(index + 1, buf, 10);
                SetValue(historyKey, buf, REG_SZ, path, (DWORD)strlen(path) + 1);
                index++;
            }
        }
        CloseKey(historyKey);
    }
}

void CPathHistory::LoadFromRegistry(HKEY hKey, const char* name)
{
    ClearHistory();
    HKEY historyKey;
    if (OpenKey(hKey, name, historyKey))
    {
        char path[2 * MAX_PATH];
        char fsName[MAX_PATH];
        const char* pathOrArchiveOrFSName = path;
        const char* archivePathOrFSUserPart = NULL;
        char buf[10];
        int type;
        int i;
        for (i = 0;; i++)
        {
            itoa(i + 1, buf, 10);
            if (GetValue(historyKey, buf, REG_SZ, path, 2 * MAX_PATH))
            {
                if (strlen(path) >= 2)
                {
                    // path may be of type
                    // 0 (disk): "C:\???" or "\\server\???"
                    // 1 (archive): "C:\???:" or "\\server\???:"
                    // 2 (FS): "XY:???"
                    type = -1; // nepridavat
                    if ((path[0] == '\\' && path[1] == '\\') || path[1] == ':')
                    {
                        // this is type==0 (disk) or type==1 (archive)
                        pathOrArchiveOrFSName = path;
                        char* separator = strchr(path + 2, ':');
                        if (separator == NULL)
                        {
                            type = 0;
                            archivePathOrFSUserPart = NULL;
                        }
                        else
                        {
                            *separator = 0;
                            type = 1;
                            archivePathOrFSUserPart = separator + 1;
                        }
                    }
                    else
                    {
                        // kandidat na FS path
                        if (IsPluginFSPath(path, fsName, _countof(fsName), &archivePathOrFSUserPart))
                        {
                            pathOrArchiveOrFSName = fsName;
                            type = 2;
                        }
                    }
                    if (type != -1)
                        AddPath(type, pathOrArchiveOrFSName, archivePathOrFSUserPart, NULL, NULL);
                    else
                        TRACE_E("CPathHistory::LoadFromRegistry() invalid path: " << path);
                }
            }
            else
                break;
        }
        CloseKey(historyKey);
    }
}

//****************************************************************************
//
// CScrollPositionMemory
//

void CScrollPositionMemory::Push(const char* path, int topIndex)
{
    if (strlen(path) >= _countof(Path))
    {
        TRACE_E("CScrollPositionMemory::Push(): path is too long.");
        Clear();
        return;
    }
    // find out whether path follows Path (path==Path+"\\name")
    const char* s = path + strlen(path);
    if (s > path && *(s - 1) == '\\')
        s--;
    BOOL ok;
    if (s == path)
        ok = FALSE;
    else
    {
        if (s > path && *s == '\\')
            s--;
        while (s > path && *s != '\\')
            s--;

        int l = (int)strlen(Path);
        if (l > 0 && Path[l - 1] == '\\')
            l--;
        ok = s - path == l && StrNICmp(path, Path, l) == 0;
    }

    if (ok) // navazuje -> zapamatujeme si dalsi top-index
    {
        if (TopIndexesCount == TOP_INDEX_MEM_SIZE) // je potreba vyhodit z pameti prvni top-index
        {
            int i;
            for (i = 0; i < TOP_INDEX_MEM_SIZE - 1; i++)
                TopIndexes[i] = TopIndexes[i + 1];
            TopIndexesCount--;
        }
        // Directory history stores a bounded presentation path.
        StringCchCopyNA(Path, _countof(Path), path, _countof(Path) - 1);
        TopIndexes[TopIndexesCount++] = topIndex;
    }
    else // nenavazuje -> prvni top-index v rade
    {
        StringCchCopyNA(Path, _countof(Path), path, _countof(Path) - 1);
        TopIndexesCount = 1;
        TopIndexes[0] = topIndex;
    }
}

BOOL CScrollPositionMemory::FindAndPop(const char* path, int& topIndex)
{
    // find out whether path matches Path (path==Path)
    int l1 = (int)strlen(path);
    if (l1 > 0 && path[l1 - 1] == '\\')
        l1--;
    int l2 = (int)strlen(Path);
    if (l2 > 0 && Path[l2 - 1] == '\\')
        l2--;
    if (l1 == l2 && StrNICmp(path, Path, l1) == 0)
    {
        if (TopIndexesCount > 0)
        {
            char* s = Path + strlen(Path);
            if (s > Path && *(s - 1) == '\\')
                s--;
            if (s > Path && *s == '\\')
                s--;
            while (s > Path && *s != '\\')
                s--;
            *s = 0;
            topIndex = TopIndexes[--TopIndexesCount];
            return TRUE;
        }
        else // we no longer have this value (not stored or low memory -> discarded)
        {
            Clear();
            return FALSE;
        }
    }
    else // dotaz na jinou cestu -> vycistime pamet, doslo k dlouhemu skoku
    {
        Clear();
        return FALSE;
    }
}

//*****************************************************************************

CFileHistory::CFileHistory()
    : Files(10, 10)
{
}

void CFileHistory::ClearHistory()
{
    Files.DestroyMembers();
}

BOOL CFileHistory::AddFile(CFileHistoryItemTypeEnum type, DWORD handlerID, const char* fileName)
{
    CALL_STACK_MESSAGE4("CFileHistory::AddFile(%d, %u, %s)", type, handlerID, fileName);

    // search existing items to see whether the added item is already present
    int i;
    for (i = 0; i < Files.Count; i++)
    {
        CFileHistoryItem* item = Files[i];
        if (item->Equal(type, handlerID, fileName))
        {
            // if yes, only move it to the top position
            if (i > 0)
            {
                Files.Detach(i);
                if (!Files.IsGood())
                    Files.ResetState(); // nemuze se nepovest, hlasi jedine nedostatek pameti pro zmenseni pole
                Files.Insert(0, item);
                if (!Files.IsGood())
                {
                    Files.ResetState();
                    delete item;
                    return FALSE;
                }
            }
            return TRUE;
        }
    }

    // polozka neexistuje - vlozime ji na horni pozici
    CFileHistoryItem* item = new CFileHistoryItem(type, handlerID, fileName);
    if (item == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }
    if (!item->IsGood())
    {
        delete item;
        return FALSE;
    }
    Files.Insert(0, item);
    if (!Files.IsGood())
    {
        Files.ResetState();
        delete item;
        return FALSE;
    }
    // zarizneme na 30 polozek
    if (Files.Count > 30)
        Files.Delete(30);

    return TRUE;
}

BOOL CFileHistory::FillPopupMenu(CMenuPopup* popup)
{
    CALL_STACK_MESSAGE1("CFileHistory::FillPopupMenu()");

    // nalejeme polozky
    char name[2 * MAX_PATH];
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_ICON | MENU_MASK_STRING;
    mii.Type = MENU_TYPE_STRING;
    mii.String = name;
    int i;
    int inserted = 0;
    for (i = 0; i < Files.Count; i++)
    {
        CFileHistoryItem* item = Files[i];

        // separate name from path with '\t' - it will then be in a separate column
        // File-list rows retain their fixed display-name allocation.
        StringCchCopyNA(name, _countof(name), item->FileName, _countof(name) - 1);
        if (strlen(item->FileName) >= _countof(name))
            continue;
        char* ptr = strrchr(name, '\\');
        if (ptr == NULL)
            return FALSE;
        memmove(ptr + 1, ptr, strlen(ptr) + 1);
        *(ptr + 1) = '\t';
        const char* text = "";
        // zdvojime '&', aby se nezobrazovalo jako podtrzeni
        DuplicateAmpersands(name, 2 * MAX_PATH);

        mii.HIcon = item->HIcon;
        switch (item->Type)
        {
        case fhitView:
            text = LoadStr(IDS_FILEHISTORY_VIEW);
            break;
        case fhitEdit:
            text = LoadStr(IDS_FILEHISTORY_EDIT);
            break;
        case fhitOpen:
            text = LoadStr(IDS_FILEHISTORY_OPEN);
            break;
        default:
            TRACE_E("Unknown Type=" << item->Type);
        }
        StrNCat(name, "\t(", _countof(name));
        StrNCat(name, text, _countof(name));
        StrNCat(name, ")", _countof(name)); // append the way the file is opened
        mii.ID = i + 1;
        popup->InsertItem(-1, TRUE, &mii);
        inserted++;
    }
    if (inserted > 0)
    {
        popup->SetStyle(MENU_POPUP_THREECOLUMNS); // prvni dva sloupce jsou zarovnane doleva
        popup->AssignHotKeys();
    }
    return TRUE;
}

BOOL CFileHistory::Execute(int index)
{
    CALL_STACK_MESSAGE2("CFileHistory::Execute(%d)", index);
    if (index < 1 || index > Files.Count)
    {
        TRACE_E("Index is out of range");
        return FALSE;
    }
    return Files[index - 1]->Execute();
    return TRUE;
}

BOOL CFileHistory::HasItem()
{
    return Files.Count > 0;
}
