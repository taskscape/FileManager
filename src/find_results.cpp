// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <strsafe.h>

#include "menu.h"
#include "cfgdlg.h"
#include "mainwnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "viewer.h"
#include "shellib.h"
#include "find.h"
#include "gui.h"
#include "usermenu.h"
#include "execute.h"
#include "tasklist.h"

#include <Shlwapi.h>

//****************************************************************************
//
// CFindTBHeader
//

// Find-result paths are operation identities; comparisons and enumerator output require a complete combined name.
static BOOL BuildFindResultPath(char (&path)[MAX_PATH], const CFoundFilesData* file)
{
    return file != NULL && file->Path != NULL && file->Name != NULL &&
           SUCCEEDED(StringCchCopyA(path, _countof(path), file->Path)) &&
           SalPathAppend(path, file->Name, _countof(path));
}

void CFindOptions::InitMenu(CMenuPopup* popup, BOOL enabled, int originalCount)
{
    int count = popup->GetItemCount();
    if (count > originalCount)
    {
        // remove any previously inserted items
        popup->RemoveItemsRange(originalCount, count - 1);
    }

    if (Items.Count > 0)
    {
        MENU_ITEM_INFO mii;

        // if there are items to append, insert a separator first
        mii.Mask = MENU_MASK_TYPE;
        mii.Type = MENU_TYPE_SEPARATOR;
        popup->InsertItem(-1, TRUE, &mii);

        // append the displayed portion of the items
        int maxCount = CM_FIND_OPTIONS_LAST - CM_FIND_OPTIONS_FIRST;
        int i;
        for (i = 0; i < min(Items.Count, maxCount); i++)
        {
            mii.Mask = MENU_MASK_TYPE | MENU_MASK_STATE | MENU_MASK_STRING | MENU_MASK_ID;
            mii.Type = MENU_TYPE_STRING;
            mii.State = enabled ? 0 : MENU_STATE_GRAYED;
            if (Items[i]->AutoLoad)
                mii.State |= MENU_STATE_DEFAULT;
            mii.ID = CM_FIND_OPTIONS_FIRST + i;
            mii.String = Items[i]->ItemName;
            popup->InsertItem(-1, TRUE, &mii);
        }
    }
}

//****************************************************************************
//
// CFoundFilesData
//

BOOL CFoundFilesData::Set(const char* path, const char* name, const CQuadWord& size, DWORD attr,
                          const FILETIME* lastWrite, BOOL isDir)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE5("CFoundFilesData::Set(%s, %s, %g, 0x%X, )", path, name, size.GetDouble(), attr);
    int l1 = (int)strlen(path), l2 = (int)strlen(name);
    Path = (char*)malloc(l1 + 1);
    Name = (char*)malloc(l2 + 1);
    if (Path == NULL || Name == NULL)
        return FALSE;
    memmove(Path, path, l1 + 1);
    memmove(Name, name, l2 + 1);
    Size = size;
    Attr = attr;
    LastWrite = *lastWrite;
    IsDir = isDir ? 1 : 0;
    return TRUE;
}

char* CFoundFilesData::GetText(int i, char* text, int fileNameFormat)
{
    // several FIND windows may run in parallel, which could overwrite this static buffer
    //  static char text[50];
    switch (i)
    {
    case 0:
    {
        AlterFileName(text, Name, -1, fileNameFormat, 0, IsDir);
        return text;
    }

    case 1:
        return Path;

    case 2:
    {
        if (IsDir)
            CopyMemory(text, DirColumnStr, DirColumnStrLen + 1);
        else
            NumberToStr(text, Size);
        break;
    }

    case 3:
    {
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, text, 50, TRUE) == 0)
                sprintf(text, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);
        }
        else
            strcpy(text, LoadStr(IDS_INVALID_DATEORTIME));
        break;
    }

    case 4:
    {
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&LastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            if (FormatUserDateTimeUtf8(&st, 0, text, 50, FALSE) == 0)
                sprintf(text, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        }
        else
            strcpy(text, LoadStr(IDS_INVALID_DATEORTIME));
        break;
    }

    default:
    {
        GetAttrsString(text, Attr);
        break;
    }
    }
    return text;
}

//****************************************************************************
//
// CFoundFilesListView
//

CFoundFilesListView::CFoundFilesListView(HWND dlg, int ctrlID, CFindDialog* findDialog)
    : Data(1000, 500), DataForRefine(1, 1000), CWindow(dlg, ctrlID)
{
    FindDialog = findDialog;
    HANDLES(InitializeCriticalSection(&DataCriticalSection));
    // use Unicode notifications if available
    ListView_SetUnicodeFormat(HWindow, TRUE);

    // add this panel to the array of sources for enumerating files in viewers
    EnumFileNamesAddSourceUID(HWindow, &EnumFileNamesSourceUID);
}

CFoundFilesListView::~CFoundFilesListView()
{
    // remove this panel from the array of sources for enumerating files in viewers
    EnumFileNamesRemoveSourceUID(HWindow);

    HANDLES(DeleteCriticalSection(&DataCriticalSection));
}

CFoundFilesData*
CFoundFilesListView::At(int index)
{
    CFoundFilesData* ptr;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    ptr = Data[index];
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return ptr;
}

void CFoundFilesListView::DestroyMembers()
{
    //  HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.DestroyMembers();
    //  HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

void CFoundFilesListView::Delete(int index)
{
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.Delete(index);
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

int CFoundFilesListView::GetCount()
{
    int count;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    count = Data.Count;
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return count;
}

int CFoundFilesListView::Add(CFoundFilesData* item)
{
    int index;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    index = Data.Add(item);
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return index;
}

BOOL CFoundFilesListView::TakeDataForRefine()
{
    DataForRefine.DestroyMembers();
    int i;
    for (i = 0; i < Data.Count; i++)
    {
        CFoundFilesData* refineData = Data[i];
        DataForRefine.Add(refineData);
        if (!DataForRefine.IsGood())
        {
            DataForRefine.ResetState();
            DataForRefine.DetachMembers();
            return FALSE;
        }
    }
    Data.DetachMembers();
    return TRUE;
}

void CFoundFilesListView::DestroyDataForRefine()
{
    DataForRefine.DestroyMembers();
}

int CFoundFilesListView::GetDataForRefineCount()
{
    return DataForRefine.Count;
}

CFoundFilesData*
CFoundFilesListView::GetDataForRefine(int index)
{
    CFoundFilesData* ptr;
    ptr = DataForRefine[index];
    return ptr;
}

DWORD
CFoundFilesListView::GetSelectedListSize()
{
    // this method is invoked only from the main thread
    DWORD size = 0;
    int index = -1;
    do
    {
        index = ListView_GetNextItem(HWindow, index, LVIS_SELECTED);
        if (index != -1)
        {
            CFoundFilesData* ptr = Data[index];
            int pathLen = (int)strlen(ptr->Path);
            if (ptr->Path[pathLen - 1] != '\\')
                pathLen++; // if the path does not contain a backslash, reserve space for it
            int nameLen = (int)strlen(ptr->Name);
            size += pathLen + nameLen + 1; // reserve space for the terminator
        }
    } while (index != -1);
    if (size == 0)
        size = 2;
    else
        size++;

    return size;
}

BOOL CFoundFilesListView::GetSelectedList(char* list, DWORD maxSize)
{
    DWORD size = 0;
    int index = -1;
    do
    {
        index = ListView_GetNextItem(HWindow, index, LVIS_SELECTED);
        if (index != -1)
        {
            CFoundFilesData* ptr = Data[index];
            int pathLen = (int)strlen(ptr->Path);
            if (ptr->Path[pathLen - 1] != '\\')
                size++; // if the path does not contain a backslash, reserve space for it
            size += pathLen;
            if (size > maxSize)
            {
                TRACE_E("Buffer is too short");
                return FALSE;
            }
            memmove(list, ptr->Path, pathLen);
            list += pathLen;
            if (ptr->Path[pathLen - 1] != '\\')
                *list++ = '\\';
            int nameLen = (int)strlen(ptr->Name);
            size += nameLen + 1; // reserve space for the terminator
            if (size > maxSize)
            {
                TRACE_E("Buffer is too short");
                return FALSE;
            }
            memmove(list, ptr->Name, nameLen + 1);
            list += nameLen + 1;
        }
    } while (index != -1);
    if (size == 0)
    {
        if (size + 2 > maxSize)
        {
            TRACE_E("Buffer is too short");
            return FALSE;
        }
        *list++ = '\0';
        *list++ = '\0';
    }
    else
    {
        if (size + 1 > maxSize)
        {
            TRACE_E("Buffer is too short");
            return FALSE;
        }
        *list++ = '\0';
    }
    return TRUE;
}

void CFoundFilesListView::CheckAndRemoveSelectedItems(BOOL forceRemove, int lastFocusedIndex, const CFoundFilesData* lastFocusedItem)
{
    int removedItems = 0;

    int totalCount = ListView_GetItemCount(HWindow);
    int i;
    for (i = totalCount - 1; i >= 0; i--)
    {
        if (ListView_GetItemState(HWindow, i, LVIS_SELECTED) & LVIS_SELECTED)
        {
            CFoundFilesData* ptr = Data[i];
            BOOL remove = forceRemove;
            if (!forceRemove)
            {
                char fullPath[MAX_PATH];
                HRESULT pathResult = StringCchCopyA(fullPath, _countof(fullPath), ptr->Path);
                if (SUCCEEDED(pathResult) && fullPath[0] != 0 && fullPath[strlen(fullPath) - 1] != '\\')
                    pathResult = StringCchCatA(fullPath, _countof(fullPath), "\\");
                if (SUCCEEDED(pathResult))
                    pathResult = StringCchCatA(fullPath, _countof(fullPath), ptr->Name);
                // Never discard a result based on a truncated path assembled during cleanup.
                remove = SUCCEEDED(pathResult) && SalGetFileAttributes(fullPath) == -1;
            }
            if (remove)
            {
                Delete(i);
                removedItems++;
            }
        }
    }
    if (removedItems > 0)
    {
        // inform the listview about the new item count
        totalCount = totalCount - removedItems;
        ListView_SetItemCount(HWindow, totalCount);
        if (totalCount > 0)
        {
            // clear selection of all items
            ListView_SetItemState(HWindow, -1, 0, LVIS_SELECTED);

            // try to locate the previously selected item and select it again if it still exists
            int selectIndex = -1;
            if (lastFocusedIndex != -1)
            {
                for (i = 0; i < totalCount; i++)
                {
                    CFoundFilesData* ptr = Data[i];
                    if (lastFocusedItem != NULL &&
                        lastFocusedItem->Name != NULL && strcmp(ptr->Name, lastFocusedItem->Name) == 0 &&
                        lastFocusedItem->Path != NULL && strcmp(ptr->Path, lastFocusedItem->Path) == 0)
                    {
                        selectIndex = i;
                        break;
                    }
                }
                if (selectIndex == -1)
                    selectIndex = min(lastFocusedIndex, totalCount - 1); // if we did not find it, keep the cursor in place but within item count
            }
            if (selectIndex == -1) // fallback -- first item
                selectIndex = 0;
            ListView_SetItemState(HWindow, selectIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(HWindow, selectIndex, FALSE);
        }
        else
            FindDialog->UpdateStatusBar = TRUE;
        FindDialog->UpdateListViewItems();
    }
}

BOOL CFoundFilesListView::IsGood()
{
    BOOL isGood;
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    isGood = Data.IsGood();
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    return isGood;
}

void CFoundFilesListView::ResetState()
{
    HANDLES(EnterCriticalSection(&DataCriticalSection));
    Data.ResetState();
    HANDLES(LeaveCriticalSection(&DataCriticalSection));
}

void CFoundFilesListView::StoreItemsState()
{
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
    {
        DWORD state = ListView_GetItemState(HWindow, i, LVIS_FOCUSED | LVIS_SELECTED);
        Data[i]->Selected = (state & LVIS_SELECTED) != 0 ? 1 : 0;
        Data[i]->Focused = (state & LVIS_FOCUSED) != 0 ? 1 : 0;
    }
}

void CFoundFilesListView::RestoreItemsState()
{
    int count = GetCount();
    int i;
    for (i = 0; i < count; i++)
    {
        DWORD state = 0;
        if (Data[i]->Selected)
            state |= LVIS_SELECTED;
        if (Data[i]->Focused)
            state |= LVIS_FOCUSED;
        ListView_SetItemState(HWindow, i, state, LVIS_FOCUSED | LVIS_SELECTED);
    }
}

void CFoundFilesListView::SortItems(int sortBy)
{
    if (sortBy == 5)
        return; // sorting by attributes is unsupported

    BOOL enabledNameSize = TRUE;
    BOOL enabledPathTime = TRUE;
    if (FindDialog->GrepData.FindDuplicates)
    {
        enabledPathTime = FALSE; // path and time are irrelevant for duplicates
        // sorting by name and size works for duplicates only
        // when searching for identical name and size
        enabledNameSize = (FindDialog->GrepData.FindDupFlags & FIND_DUPLICATES_NAME) &&
                          (FindDialog->GrepData.FindDupFlags & FIND_DUPLICATES_SIZE);
    }

    if (!enabledNameSize && (sortBy == 0 || sortBy == 2))
        return;
    if (!enabledPathTime && (sortBy == 1 || sortBy == 3 || sortBy == 4))
        return;

    HCURSOR hCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    HANDLES(EnterCriticalSection(&DataCriticalSection));

    //   EnumFileNamesChangeSourceUID(HWindow, &EnumFileNamesSourceUID);  // commented out, not sure why it is here: Petr

    // if some items are still in data but not in the listview, transfer them
    FindDialog->UpdateListViewItems();

    if (Data.Count > 0)
    {
        // save the selected and focused item state
        StoreItemsState();

        // sort the array by the requested criterion
        QuickSort(0, Data.Count - 1, sortBy);
        if (FindDialog->GrepData.FindDuplicates)
        {
            QuickSortDuplicates(0, Data.Count - 1, sortBy == 0);
            SetDifferentByGroup();
        }
        else
        {
            QuickSort(0, Data.Count - 1, sortBy);
        }

        // restore the item states
        RestoreItemsState();

        int focusIndex = ListView_GetNextItem(HWindow, -1, LVNI_FOCUSED);
        if (focusIndex != -1)
            ListView_EnsureVisible(HWindow, focusIndex, FALSE);
        ListView_RedrawItems(HWindow, 0, Data.Count - 1);
        UpdateWindow(HWindow);
    }

    HANDLES(LeaveCriticalSection(&DataCriticalSection));
    SetCursor(hCursor);
}

void CFoundFilesListView::SetDifferentByGroup()
{
    CFoundFilesData* lastData = NULL;
    int different = 0;
    if (Data.Count > 0)
    {
        lastData = Data.At(0);
        lastData->Different = different;
    }
    int i;
    for (i = 1; i < Data.Count; i++)
    {
        CFoundFilesData* data = Data.At(i);
        if (data->Group == lastData->Group)
        {
            data->Different = different;
        }
        else
        {
            different++;
            if (different > 1)
                different = 0;
            lastData = data;
            lastData->Different = different;
        }
    }
}

void CFoundFilesListView::QuickSort(int left, int right, int sortBy)
{

LABEL_QuickSort2:

    int i = left, j = right;
    CFoundFilesData* pivot = Data[(i + j) / 2];

    do
    {
        while (CompareFunc(Data[i], pivot, sortBy) < 0 && i < right)
            i++;
        while (CompareFunc(pivot, Data[j], sortBy) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with a code that is much more stack-efficient  (max. log(N) recursion depth)
    //  if (left < j) QuickSort(left, j, sortBy);
    //  if (i < right) QuickSort(i, right, sortBy);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both halves need sorting: recurse on the smaller one and process the other via 'goto'
            {
                QuickSort(left, j, sortBy);
                left = i;
                goto LABEL_QuickSort2;
            }
            else
            {
                QuickSort(i, right, sortBy);
                right = j;
                goto LABEL_QuickSort2;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSort2;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSort2;
        }
    }
}

int CFoundFilesListView::CompareFunc(CFoundFilesData* f1, CFoundFilesData* f2, int sortBy)
{
    int res;
    int next = sortBy;
    do
    {
        if (f1->IsDir == f2->IsDir) // are the items from the same group (directories/files)?
        {
            switch (next)
            {
            case 0:
            {
                res = RegSetStrICmp(f1->Name, f2->Name);
                break;
            }

            case 1:
            {
                res = RegSetStrICmp(f1->Path, f2->Path);
                break;
                break;
            }

            case 2:
            {
                if (f1->Size < f2->Size)
                    res = -1;
                else
                {
                    if (f1->Size == f2->Size)
                        res = 0;
                    else
                        res = 1;
                }
                break;
            }

            default:
            {
                res = CompareFileTime(&f1->LastWrite, &f2->LastWrite);
                break;
            }
            }
        }
        else
            res = f1->IsDir ? -1 : 1;

        if (next == sortBy)
        {
            if (sortBy != 0)
                next = 0;
            else
                next = 1;
        }
        else if (next + 1 != sortBy)
            next++;
        else
            next += 2;
    } while (res == 0 && next <= 3);

    return res;
}

// quick sort routine for duplicate mode; it uses a special comparator
void CFoundFilesListView::QuickSortDuplicates(int left, int right, BOOL byName)
{

LABEL_QuickSortDuplicates:

    int i = left, j = right;
    CFoundFilesData* pivot = Data[(i + j) / 2];

    do
    {
        while (CompareDuplicatesFunc(Data[i], pivot, byName) < 0 && i < right)
            i++;
        while (CompareDuplicatesFunc(pivot, Data[j], byName) < 0 && j > left)
            j--;

        if (i <= j)
        {
            CFoundFilesData* swap = Data[i];
            Data[i] = Data[j];
            Data[j] = swap;
            i++;
            j--;
        }
    } while (i <= j);

    // the following "nice" code was replaced with a code that is much more stack-efficient (max. log(N) recursion depth)
    //  if (left < j) QuickSortDuplicates(left, j, byName);
    //  if (i < right) QuickSortDuplicates(i, right, byName);

    if (left < j)
    {
        if (i < right)
        {
            if (j - left < right - i) // both halves need sorting: recurse on the smaller one and use 'goto' for the other
            {
                QuickSortDuplicates(left, j, byName);
                left = i;
                goto LABEL_QuickSortDuplicates;
            }
            else
            {
                QuickSortDuplicates(i, right, byName);
                right = j;
                goto LABEL_QuickSortDuplicates;
            }
        }
        else
        {
            right = j;
            goto LABEL_QuickSortDuplicates;
        }
    }
    else
    {
        if (i < right)
        {
            left = i;
            goto LABEL_QuickSortDuplicates;
        }
    }
}

// comparator for displayed duplicates; if 'byName', sorting is primarily by name, otherwise by size
int CFoundFilesListView::CompareDuplicatesFunc(CFoundFilesData* f1, CFoundFilesData* f2, BOOL byName)
{
    int res;
    if (byName)
    {
        // by name
        res = RegSetStrICmp(f1->Name, f2->Name);
        if (res == 0)
        {
            // by size
            if (f1->Size < f2->Size)
                res = -1;
            else
            {
                if (f1->Size == f2->Size)
                {
                    // by group
                    if (f1->Group < f2->Group)
                        res = -1;
                    else
                    {
                        if (f1->Group == f2->Group)
                            res = 0;
                        else
                            res = 1;
                    }
                }
                else
                    res = 1;
            }
        }
    }
    else
    {
        // by size
        if (f1->Size < f2->Size)
            res = -1;
        else
        {
            if (f1->Size == f2->Size)
            {
                // by name
                res = RegSetStrICmp(f1->Name, f2->Name);
                if (res == 0)
                {
                    // by group
                    if (f1->Group < f2->Group)
                        res = -1;
                    else
                    {
                        if (f1->Group == f2->Group)
                            res = 0;
                        else
                            res = 1;
                    }
                }
            }
            else
                res = 1;
        }
    }
    if (res == 0)
        res = RegSetStrICmp(f1->Path, f2->Path);
    return res;
}

// CUMDataFromFind and GetNextItemFromFind moved to find_dialog_ui.cpp
// (they are only called from CFindDialog methods)

LRESULT
CFoundFilesListView::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    SLOW_CALL_STACK_MESSAGE4("CFoundFilesListView::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_GETDLGCODE:
    {
        if (lParam != NULL)
        {
            // if it is the Enter key, we want to process it, otherwise the Enter would not be delivered
            MSG* msg = (LPMSG)lParam;
            if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN &&
                ListView_GetItemCount(HWindow) > 0)
                return DLGC_WANTMESSAGE;
        }
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
        BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        /* it seems this code is no longer needed; handled in the dialog wndproc
      if (wParam == VK_RETURN)
      {
        if (altPressed)
        {
          FindDialog->OnProperties();
          FindDialog->SkipCharacter = TRUE;
        }
        else
          FindDialog->OnOpen();
        return TRUE;
      }
*/
        if ((wParam == VK_F10 && shiftPressed || wParam == VK_APPS))
        {
            POINT p;
            GetListViewContextMenuPos(HWindow, &p);
            FindDialog->OnContextMenu(p.x, p.y);
            return TRUE;
        }
        break;
    }

    case WM_MOUSEACTIVATE:
    {
        // if Find is inactive and the user tries to drag & drop one of the items, the dialog must not pop up to the foreground
        return MA_NOACTIVATE;
    }

    case WM_SETFOCUS:
    {
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, 0, 0);
        break;
    }

    case WM_KILLFOCUS:
    {
        HWND next = (HWND)wParam;
        BOOL nextIsButton;
        if (next != NULL)
        {
            char className[30];
            WORD wl = LOWORD(GetWindowLongPtr(next, GWL_STYLE)); // only BS_ styles
            nextIsButton = (GetClassName(next, className, 30) != 0 &&
                            StrICmp(className, "BUTTON") == 0 &&
                            (wl == BS_PUSHBUTTON || wl == BS_DEFPUSHBUTTON));
        }
        else
            nextIsButton = FALSE;
        SendMessage(GetParent(HWindow), WM_USER_BUTTONS, nextIsButton ? wParam : 0, 0);
        break;
    }

    case WM_USER_ENUMFILENAMES: // searching for the next/previous name for the viewer
    {
        HANDLES(EnterCriticalSection(&FileNamesEnumDataSect));
        if ((int)wParam /* reqUID */ == FileNamesEnumData.RequestUID && // no further request was issued (this one would be pointless)
            EnumFileNamesSourceUID == FileNamesEnumData.SrcUID &&       // the source hasn't changed
            !FileNamesEnumData.TimedOut)                                // someone is still waiting for the result
        {
            HANDLES(EnterCriticalSection(&DataCriticalSection));

            BOOL selExists = FALSE;
            if (FileNamesEnumData.PreferSelected) // if needed, check whether there is a selection
            {
                int i = -1;
                int selCount = 0; // ignore the state where the only marked item is the focused one (this cannot logically be considered as selected items)
                while (1)
                {
                    i = ListView_GetNextItem(HWindow, i, LVNI_SELECTED);
                    if (i == -1)
                        break;
                    else
                    {
                        selCount++;
                        if (!Data[i]->IsDir)
                            selExists = TRUE;
                        if (selCount > 1 && selExists)
                            break;
                    }
                }
                if (selExists && selCount <= 1)
                    selExists = FALSE;
            }

            int index = FileNamesEnumData.LastFileIndex;
            int count = Data.Count;
            BOOL indexNotFound = TRUE;
            if (index == -1) // searching from the first or last item
            {
                if (FileNamesEnumData.RequestType == fnertFindPrevious)
                    index = count; // looking for the previous item, start at the end
                                   // else  // looking for the next item, start at the beginning
            }
            else
            {
                if (FileNamesEnumData.LastFileName[0] != 0) // the full name at 'index' is known; check for shifts and search for a new index if needed
                {
                    BOOL ok = FALSE;
                    CFoundFilesData* f = (index >= 0 && index < count) ? Data[index] : NULL;
                    char fileName[MAX_PATH];
                    if (BuildFindResultPath(fileName, f))
                    {
                        if (StrICmp(fileName, FileNamesEnumData.LastFileName) == 0)
                        {
                            ok = TRUE;
                            indexNotFound = FALSE;
                        }
                    }
                    if (!ok)
                    { // the name at index 'index' isn't FileNamesEnumData.LastFileName, try to find a new index for that name
                        int i;
                        for (i = 0; i < count; i++)
                        {
                            f = Data[i];
                            if (BuildFindResultPath(fileName, f))
                            {
                                if (StrICmp(fileName, FileNamesEnumData.LastFileName) == 0)
                                    break;
                            }
                        }
                        if (i != count) // new index found
                        {
                            index = i;
                            indexNotFound = FALSE;
                        }
                    }
                }
                if (index >= count)
                {
                    if (FileNamesEnumData.RequestType == fnertFindNext)
                        index = count - 1;
                    else
                        index = count;
                }
                if (index < 0)
                    index = 0;
            }

            int wantedViewerType = 0;
            BOOL onlyAssociatedExtensions = FALSE;
            if (FileNamesEnumData.OnlyAssociatedExtensions) // does the viewer request filtering by associated extensions?
            {
                if (FileNamesEnumData.Plugin != NULL) // viewer from a plugin
                {
                    int pluginIndex = Plugins.GetIndex(FileNamesEnumData.Plugin);
                    if (pluginIndex != -1) // "always true"
                    {
                        wantedViewerType = -1 - pluginIndex;
                        onlyAssociatedExtensions = TRUE;
                    }
                }
                else // internal viewer
                {
                    wantedViewerType = VIEWER_INTERNAL;
                    onlyAssociatedExtensions = TRUE;
                }
            }

            BOOL preferSelected = selExists && FileNamesEnumData.PreferSelected;
            switch (FileNamesEnumData.RequestType)
            {
            case fnertFindNext: // next
            {
                CDynString strViewerMasks;
                if (MainWindow->GetViewersAssoc(wantedViewerType, &strViewerMasks))
                {
                    CMaskGroup masks;
                    int errorPos;
                    if (masks.PrepareMasks(errorPos, strViewerMasks.GetString()))
                    {
                        while (index + 1 < count)
                        {
                            index++;
                            if (preferSelected)
                            {
                                int i = ListView_GetNextItem(HWindow, index - 1, LVNI_SELECTED);
                                if (i != -1)
                                {
                                    index = i;
                                    if (!Data[index]->IsDir) // we only search for files
                                    {
                                        if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->Name, NULL))
                                        {
                                            FileNamesEnumData.Found = TRUE;
                                            break;
                                        }
                                    }
                                }
                                else
                                    index = count - 1;
                            }
                            else
                            {
                                if (!Data[index]->IsDir)
                                {
                                    if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->Name, NULL))
                                    {
                                        FileNamesEnumData.Found = TRUE;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    else
                        TRACE_E("Unexpected situation in Find::WM_USER_ENUMFILENAMES: grouped viewer's masks can't be prepared for use!");
                }
                break;
            }

            case fnertFindPrevious: // previous
            {
                CDynString strViewerMasks;
                if (MainWindow->GetViewersAssoc(wantedViewerType, &strViewerMasks))
                {
                    CMaskGroup masks;
                    int errorPos;
                    if (masks.PrepareMasks(errorPos, strViewerMasks.GetString()))
                    {
                        while (index - 1 >= 0)
                        {
                            index--;
                            if (!Data[index]->IsDir &&
                                (!preferSelected ||
                                 (ListView_GetItemState(HWindow, index, LVIS_SELECTED) & LVIS_SELECTED)))
                            {
                                if (!onlyAssociatedExtensions || masks.AgreeMasks(Data[index]->Name, NULL))
                                {
                                    FileNamesEnumData.Found = TRUE;
                                    break;
                                }
                            }
                        }
                    }
                    else
                        TRACE_E("Unexpected situation in Find::WM_USER_ENUMFILENAMES: grouped viewer's masks can't be prepared for use!");
                }
                break;
            }

            case fnertIsSelected: // check selection state
            {
                if (!indexNotFound && index >= 0 && index < Data.Count)
                {
                    FileNamesEnumData.IsFileSelected = (ListView_GetItemState(HWindow, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
                    FileNamesEnumData.Found = TRUE;
                }
                break;
            }

            case fnertSetSelection: // set selection
            {
                if (!indexNotFound && index >= 0 && index < Data.Count)
                {
                    ListView_SetItemState(HWindow, index, FileNamesEnumData.Select ? LVIS_SELECTED : 0, LVIS_SELECTED);
                    FileNamesEnumData.Found = TRUE;
                }
                break;
            }
            }

            if (FileNamesEnumData.Found)
            {
                CFoundFilesData* f = Data[index];
                if (BuildFindResultPath(FileNamesEnumData.FileName, f))
                {
                    FileNamesEnumData.LastFileIndex = index;
                }
                else // should never happen
                {
                    TRACE_E("Unexpected situation in CFoundFilesListView::WindowProc(): handling of WM_USER_ENUMFILENAMES");
                    FileNamesEnumData.Found = FALSE;
                    FileNamesEnumData.NoMoreFiles = TRUE;
                }
            }
            else
                FileNamesEnumData.NoMoreFiles = TRUE;

            HANDLES(LeaveCriticalSection(&DataCriticalSection));
            SetEvent(FileNamesEnumDone);
        }
        HANDLES(LeaveCriticalSection(&FileNamesEnumDataSect));
        return 0;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CFoundFilesListView::InitColumns()
{
    CALL_STACK_MESSAGE1("CFoundFilesListView::InitColumns()");
    LV_COLUMN lvc;
    int header[] = {IDS_FOUNDFILESCOLUMN1, IDS_FOUNDFILESCOLUMN2,
                    IDS_FOUNDFILESCOLUMN3, IDS_FOUNDFILESCOLUMN4,
                    IDS_FOUNDFILESCOLUMN5, IDS_FOUNDFILESCOLUMN6,
                    -1};

    lvc.mask = LVCF_FMT | LVCF_TEXT | LVCF_SUBITEM;
    lvc.fmt = LVCFMT_LEFT;
    int i;
    for (i = 0; header[i] != -1; i++) // create columns
    {
        if (i == 2)
            lvc.fmt = LVCFMT_RIGHT;
        lvc.pszText = LoadStr(header[i]);
        lvc.iSubItem = i;
        if (ListView_InsertColumn(HWindow, i, &lvc) == -1)
            return FALSE;
    }

    RECT r;
    GetClientRect(HWindow, &r);
    DWORD cx = r.right - r.left - 1;
    ListView_SetColumnWidth(HWindow, 5, ListView_GetStringWidth(HWindow, "ARH") + 20);

    char format1[200];
    char format2[200];
    SYSTEMTIME st;
    ZeroMemory(&st, sizeof(st));
    st.wYear = 2000; // the longest possible value
    st.wMonth = 12;  // the longest possible value
    st.wDay = 30;    // the longest possible value
    st.wHour = 10;   // morning (not sure whether AM or PM will be shorter, so try both)
    st.wMinute = 59; // the longest possible value
    st.wSecond = 59; // the longest possible value
    if (FormatUserDateTimeUtf8(&st, 0, format1, _countof(format1), FALSE) == 0)
        sprintf(format1, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    st.wHour = 20; // afternoon
    if (FormatUserDateTimeUtf8(&st, 0, format2, _countof(format2), FALSE) == 0)
        sprintf(format2, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);

    int maxWidth = ListView_GetStringWidth(HWindow, format1);
    int w = ListView_GetStringWidth(HWindow, format2);
    if (w > maxWidth)
        maxWidth = w;
    ListView_SetColumnWidth(HWindow, 4, maxWidth + 20);

    maxWidth = 0;
    if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, format1, _countof(format1), TRUE) == 0)
        sprintf(format1, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    else
    {
        // verify that the short date format does not contain alphabetic characters
        const char* p = format1;
        while (*p != 0 && !IsAlpha[*p])
            p++;
        if (IsAlpha[*p])
        {
            // contains alphabetic characters -- we must find the longest month and day text
            int maxMonth = 0;
            int sats[] = {1, 5, 4, 1, 6, 3, 1, 5, 2, 7, 4, 2};
            int mo;
            for (mo = 0; mo < 12; mo++) // iterate over all months starting from January; the weekday stays the same so its width doesn't influence the result, wDay is single digit for the same reason
            {
                st.wDay = sats[mo];
                st.wMonth = 1 + mo;
                if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, format1, _countof(format1), TRUE) != 0)
                {
                    w = ListView_GetStringWidth(HWindow, format1);
                    if (w > maxWidth)
                    {
                        maxWidth = w;
                        maxMonth = st.wMonth;
                    }
                }
            }
            if (maxWidth > 0)
            {
                st.wMonth = maxMonth;
                for (st.wDay = 21; st.wDay < 28; st.wDay++) // all possible weekdays (doesn't have to start on Monday)
                {
                    if (FormatUserDateTimeUtf8(&st, DATE_SHORTDATE, format1, _countof(format1), TRUE) != 0)
                    {
                        w = ListView_GetStringWidth(HWindow, format1);
                        if (w > maxWidth)
                        {
                            maxWidth = w;
                        }
                    }
                }
            }
        }
    }

    ListView_SetColumnWidth(HWindow, 3, (maxWidth > 0 ? maxWidth : ListView_GetStringWidth(HWindow, format1)) + 20);
    ListView_SetColumnWidth(HWindow, 2, ListView_GetStringWidth(HWindow, "000 000 000 000") + 20); // up to 1TB fits here
    int width;
    if (Configuration.FindColNameWidth != -1)
        width = Configuration.FindColNameWidth;
    else
        width = 20 + ListView_GetStringWidth(HWindow, "XXXXXXXX.XXX") + 20;
    ListView_SetColumnWidth(HWindow, 0, width);
    cx -= ListView_GetColumnWidth(HWindow, 0) + ListView_GetColumnWidth(HWindow, 2) +
          ListView_GetColumnWidth(HWindow, 3) + ListView_GetColumnWidth(HWindow, 4) +
          ListView_GetColumnWidth(HWindow, 5) + GetSystemMetrics(SM_CXHSCROLL) - 1;
    ListView_SetColumnWidth(HWindow, 1, cx);
    ListView_SetImageList(HWindow, HFindSymbolsImageList, LVSIL_SMALL);

    return TRUE;
}
