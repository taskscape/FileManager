// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Library initialization.
BOOL InitializeShellib();

// Library cleanup.
void ReleaseShellib();

// Safe IContextMenu2::GetCommandString() call where Microsoft's implementation sometimes crashes.
HRESULT AuxGetCommandString(IContextMenu2* menu, UINT_PTR idCmd, UINT uType, UINT* pReserved, LPSTR pszName, UINT cchMax);

// Callback that returns selected file names for creating the following interfaces.
typedef const char* (*CEnumFileNamesFunction)(int index, void* param);

// Creates a data object for drag&drop operations over selected files and directories from rootDir.
IDataObject* CreateIDataObject(HWND hOwnerWindow, const char* rootDir, int files,
                               CEnumFileNamesFunction nextFile, void* param);

// Creates a context menu interface for selected files and directories from rootDir.
IContextMenu2* CreateIContextMenu2(HWND hOwnerWindow, const char* rootDir, int files,
                                   CEnumFileNamesFunction nextFile, void* param);

// Creates a context menu interface for the specified directory.
IContextMenu2* CreateIContextMenu2(HWND hOwnerWindow, const char* dir);

// Does the specified directory or file have a drop target?
BOOL HasDropTarget(const char* dir);

// Creates a drop target for drag&drop operations into the specified directory or file.
IDropTarget* CreateIDropTarget(HWND hOwnerWindow, const char* dir);

// Opens the special folder window.
void OpenSpecFolder(HWND hOwnerWindow, int specFolder);

// Opens folder window 'dir' and focuses 'item'.
void OpenFolderAndFocusItem(HWND hOwnerWindow, const char* dir, const char* item);

// Opens the browse dialog and selects a path (can be limited to network paths only).
// hCenterWindow - window to center the dialog on.
BOOL GetTargetDirectory(HWND parent, HWND hCenterWindow, const char* title, const char* comment,
                        char* path, BOOL onlyNet = FALSE, const char* initDir = NULL);

// Detects whether this is a NetHood path (directory with target.lnk),
// optionally resolves where target.lnk points and returns the path in 'path'; 'path' is an in/out path
// (at least MAX_PATH characters).
void ResolveNetHoodPath(char* path);

class CMenuNew;

// Returns the New menu - popup-menu handle and IContextMenu through which commands are executed.
void GetNewOrBackgroundMenu(HWND hOwnerWindow, const char* dir, CMenuNew* menu,
                            int minCmd, int maxCmd, BOOL backgoundMenu);

struct CDragDropOperData
{
    char SrcPath[MAX_PATH];     // source path common to all files/directories from Names ("" == path conversion from Unicode failed)
    TIndirectArray<char> Names; // sorted allocated file/directory names (CF_HDROP does not distinguish file vs. directory) ("" == path conversion from Unicode failed)

    CDragDropOperData() : Names(200, 200) { SrcPath[0] = 0; }
};

// Checks whether 'pDataObject' contains files and directories from disk and from only one path,
// optionally stores their names in 'namesList' (if not NULL).
BOOL IsSimpleSelection(IDataObject* pDataObject, CDragDropOperData* namesList);

// Extracts the name for 'pidl' via GetDisplayNameOf(flags) (shortens the ID-list by one ID, gets
// the folder for the shortened ID-list from the desktop and calls GetDisplayNameOf on that folder
// for the last ID with the specified 'flags'); on success returns TRUE + name in 'name' (buffer of size 'nameSize');
// does not deallocate 'pidl'; 'alloc' is the interface obtained via CoGetMalloc.
BOOL GetSHObjectName(ITEMIDLIST* pidl, DWORD flags, char* name, int nameSize, IMalloc* alloc);

// TRUE = drag&drop effect was computed in the plugin FS, so Copy does not need to be forced
// v CImpIDropSource::GiveFeedback
extern BOOL DragFromPluginFSEffectIsFromPlugin;

//*****************************************************************************
//
// CImpIDropSource
//
// Basic object version, behaves normally (default cursors, etc.).
//
// Exception: when dragging from a plugin FS (with possible Copy and Move effects) to Explorer
// onto a disk with the TEMP directory, Move is offered by default instead of Copy (which is
// logically wrong; users expect Copy). Therefore this case is forced so we show a different
// cursor than dwEffect in GiveFeedback and then take the resulting effect from the last cursor
// shape instead of from the DoDragDrop result.

class CImpIDropSource : public IDropSource
{
private:
    long RefCount;
    DWORD MouseButton; // -1 = uninitialized value, otherwise MK_LBUTTON or MK_RBUTTON

public:
    // Last effect returned by the GiveFeedback method - introduced because
    // DoDragDrop does not return dwEffect == DROPEFFECT_MOVE; for MOVE it returns dwEffect == 0,
    // for reasons see "Handling Shell Data Transfer Scenarios", section "Handling Optimized Move Operations":
    // http://msdn.microsoft.com/en-us/library/windows/desktop/bb776904%28v=vs.85%29.aspx
    // (in short: an optimized Move is performed, meaning no copy to the target followed by deletion
    //            of the original is done; to prevent the source from unintentionally deleting the
    //            original (which may not have moved yet), it gets DROPEFFECT_NONE or DROPEFFECT_COPY)
    DWORD LastEffect;

    BOOL DragFromPluginFSWithCopyAndMove; // dragging from a plugin FS with possible Copy and Move, details above

public:
    CImpIDropSource(BOOL dragFromPluginFSWithCopyAndMove)
    {
        RefCount = 1;
        MouseButton = -1;
        LastEffect = -1;
        DragFromPluginFSWithCopyAndMove = dragFromPluginFSWithCopyAndMove;
    }

    virtual ~CImpIDropSource()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
    }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR* FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // must not touch the object, it no longer exists
        }
        return RefCount;
    }

    STDMETHOD(GiveFeedback)
    (DWORD dwEffect)
    {
        if (DragFromPluginFSWithCopyAndMove && !DragFromPluginFSEffectIsFromPlugin)
        {
            BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if ((!shiftPressed || controlPressed) && (dwEffect & DROPEFFECT_MOVE))
            { // Copy should be done, but Move is offered -> force this situation, show Copy cursor and put Copy into LastEffect
                LastEffect = DROPEFFECT_COPY;
                SetCursor(LoadCursor(HInstance, MAKEINTRESOURCE(IDC_DRAGCOPYEFFECT)));
                return S_OK;
            }
        }
        DragFromPluginFSEffectIsFromPlugin = FALSE;
        LastEffect = dwEffect;
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

    STDMETHOD(QueryContinueDrag)
    (BOOL fEscapePressed, DWORD grfKeyState)
    {
        DWORD mb = grfKeyState & (MK_LBUTTON | MK_RBUTTON);
        if (mb == 0)
            return DRAGDROP_S_DROP;
        if (MouseButton == -1)
            MouseButton = mb;
        if (fEscapePressed || MouseButton != mb)
            return DRAGDROP_S_CANCEL;
        return S_OK;
    }
};

//*****************************************************************************
//
// CImpDropTarget
//
// Calls defined callbacks to get the drop target (directory),
// reports drop or ESC,
// leaves the rest of the operations to the system IDropTarget object from IShellFolder.

// Record used in data for copy and move callbacks.
struct CCopyMoveRecord
{
    char* FileName;
    char* MapName;

    CCopyMoveRecord(const char* fileName, const char* mapName);
    CCopyMoveRecord(const wchar_t* fileName, const char* mapName);
    CCopyMoveRecord(const char* fileName, const wchar_t* mapName);
    CCopyMoveRecord(const wchar_t* fileName, const wchar_t* mapName);

    char* AllocChars(const char* name);
    char* AllocChars(const wchar_t* name);
};

// Data for copy and move callbacks.
class CCopyMoveData : public TIndirectArray<CCopyMoveRecord>
{
public:
    BOOL MakeCopyOfName; // TRUE if it should try "Copy of..." when the target already exists

public:
    CCopyMoveData(int base, int delta) : TIndirectArray<CCopyMoveRecord>(base, delta)
    {
        MakeCopyOfName = FALSE;
    }
};

// Callback for copy and move operations, handles destruction of 'data'.
typedef BOOL (*CDoCopyMove)(BOOL copy, char* targetDir, CCopyMoveData* data,
                            void* param);

// Callback for drag&drop operations, 'copy' is TRUE/FALSE (copy/move), 'toArchive' is TRUE/FALSE
// (to archive/FS), 'archiveOrFSName' (may be NULL if information should be obtained from the panel)
// is the archive file name or FS-name, 'archivePathOrUserPart' is the path in archive or
// user part of the FS path, 'data' contains a description of source files/directories, the function
// handles destruction of the 'data' object, 'param' is the parameter passed to the CImpDropTarget constructor.
typedef void (*CDoDragDropOper)(BOOL copy, BOOL toArchive, const char* archiveOrFSName,
                                const char* archivePathOrUserPart, CDragDropOperData* data,
                                void* param);

// Callback that returns the target directory for the specified point 'pt'.
typedef const char* (*CGetCurDir)(POINTL& pt, void* param, DWORD* pdwEffect, BOOL rButton,
                                  BOOL& isTgtFile, DWORD keyState, int& tgtType, int srcType);

// callback oznamujici konec drop operace, drop == FALSE pri ESC
typedef void (*CDropEnd)(BOOL drop, BOOL shortcuts, void* param, BOOL ownRutine,
                         BOOL isFakeDataObject, int tgtType);

// callback pro dotaz pred dokoncenim operace (drop)
typedef BOOL (*CConfirmDrop)(DWORD& effect, DWORD& defEffect, DWORD& grfKeyState);

// callback oznamujici vstup a vystup mysi do targetu
typedef void (*CEnterLeaveDrop)(BOOL enter, void* param);

// callback, ktery povoluje pouziti nasich rutin pro copy/move
typedef BOOL (*CUseOwnRutine)(IDataObject* pDataObject);

// callback pro zjisteni default drop effectu pri tazeni z FS na FS
typedef void (*CGetFSToFSDropEffect)(const char* srcFSPath, const char* tgtFSPath,
                                     DWORD allowedEffects, DWORD keyState,
                                     DWORD* dropEffect, void* param);

enum CIDTTgtType
{
    idtttWindows,          // soubory/adresare z windowsove cesty na windowsovou cestu
    idtttArchive,          // soubory/adresare z windowsove cesty do archivu
    idtttPluginFS,         // soubory/adresare z windowsove cesty do FS
    idtttArchiveOnWinPath, // archiv na windowsove ceste (drop=pack to archive)
    idtttFullPluginFSPath, // FS to FS
};

// IDropTarget that routes shell drag-and-drop onto a panel, archive, or plugin filesystem.
class CImpDropTarget : public IDropTarget
{
private:
    long RefCount;
    HWND OwnerWindow;
    IDataObject* OldDataObject;
    BOOL OldDataObjectIsFake;
    int OldDataObjectIsSimple;                 // -1 (neznama hodnota), TRUE/FALSE = je/neni jednoduchy (vsechna jmena na jedne ceste)
    int OldDataObjectSrcType;                  // 0 (neznamy typ), 1/2 = archiv/FS
    char OldDataObjectSrcFSPath[2 * MAX_PATH]; // only for FS type: source FS path

    CDoCopyMove DoCopyMove;
    void* DoCopyMoveParam;

    CDoDragDropOper DoDragDropOper;
    void* DoDragDropOperParam;

    CGetCurDir GetCurDir;
    void* GetCurDirParam;

    CDropEnd DropEnd;
    void* DropEndParam;

    CConfirmDrop ConfirmDrop;
    BOOL* ConfirmDropEnable;

    int TgtType; // hodnoty viz CIDTTgtType; idtttWindows i pro archivy a FS bez moznosti dropnuti aktualniho dataobjectu
    IDropTarget* CurDirDropTarget;
    char CurDir[2 * MAX_PATH];

    CEnterLeaveDrop EnterLeaveDrop;
    void* EnterLeaveDropParam;

    BOOL RButton; // akce pravym tlacitkem mysi?

    CUseOwnRutine UseOwnRutine;

    DWORD LastEffect; // last effect detected in DragEnter or DragOver (-1 => invalid)

    CGetFSToFSDropEffect GetFSToFSDropEffect;
    void* GetFSToFSDropEffectParam;

public:
    CImpDropTarget(HWND ownerWindow, CDoCopyMove doCopyMove, void* doCopyMoveParam,
                   CGetCurDir getCurDir, void* getCurDirParam, CDropEnd dropEnd,
                   void* dropEndParam, CConfirmDrop confirmDrop, BOOL* confirmDropEnable,
                   CEnterLeaveDrop enterLeaveDrop, void* enterLeaveDropParam,
                   CUseOwnRutine useOwnRutine, CDoDragDropOper doDragDropOper,
                   void* doDragDropOperParam, CGetFSToFSDropEffect getFSToFSDropEffect,
                   void* getFSToFSDropEffectParam)
    {
        RefCount = 1;
        OwnerWindow = ownerWindow;
        DoCopyMove = doCopyMove;
        DoCopyMoveParam = doCopyMoveParam;
        DoDragDropOper = doDragDropOper;
        DoDragDropOperParam = doDragDropOperParam;
        GetCurDir = getCurDir;
        GetCurDirParam = getCurDirParam;
        TgtType = idtttWindows;
        CurDirDropTarget = NULL;
        CurDir[0] = 0;
        DropEnd = dropEnd;
        DropEndParam = dropEndParam;
        OldDataObject = NULL;
        OldDataObjectIsFake = FALSE;
        OldDataObjectIsSimple = -1; // neznama hodnota
        OldDataObjectSrcType = 0;   // neznamy typ
        OldDataObjectSrcFSPath[0] = 0;
        ConfirmDrop = confirmDrop;
        ConfirmDropEnable = confirmDropEnable;
        RButton = FALSE;
        EnterLeaveDrop = enterLeaveDrop;
        EnterLeaveDropParam = enterLeaveDropParam;
        UseOwnRutine = useOwnRutine;
        LastEffect = -1;
        GetFSToFSDropEffect = getFSToFSDropEffect;
        GetFSToFSDropEffectParam = getFSToFSDropEffectParam;
    }
    virtual ~CImpDropTarget()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        if (CurDirDropTarget != NULL)
            CurDirDropTarget->Release();
    }

    void SetDirectory(const char* path, DWORD grfKeyState, POINTL pt,
                      DWORD* effect, IDataObject* dataObject, BOOL tgtIsFile, int tgtType);
    BOOL TryCopyOrMove(BOOL copy, IDataObject* pDataObject, UINT CF_FileMapA,
                       UINT CF_FileMapW, BOOL cfFileMapA, BOOL cfFileMapW);
    BOOL ProcessClipboardData(BOOL copy, const DROPFILES* data, const char* mapA,
                              const wchar_t* mapW);

    STDMETHOD(QueryInterface)
    (REFIID, void FAR* FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // nesmime sahnout do objektu, uz neexistuje
        }
        return RefCount;
    }

    STDMETHOD(DragEnter)
    (IDataObject* pDataObject, DWORD grfKeyState,
     POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragOver)
    (DWORD grfKeyState, POINTL pt, DWORD* pdwEffect);
    STDMETHOD(DragLeave)
    ();
    STDMETHOD(Drop)
    (IDataObject* pDataObject, DWORD grfKeyState, POINTL pt,
     DWORD* pdwEffect);
};

struct IShellFolder;
struct IContextMenu;
struct IContextMenu2;

// Holds the Explorer "New" submenu (IContextMenu2 + HMENU) for creating files from templates.
class CMenuNew
{
protected:
    IContextMenu2* Menu2; // menu-interface 2
    HMENU Menu;           // submenu New

public:
    CMenuNew() { Init(); }
    ~CMenuNew() { Release(); }

    void Init()
    {
        Menu2 = NULL;
        Menu = NULL;
    }

    void Set(IContextMenu2* menu2, HMENU menu)
    {
        if (menu == NULL)
            return; // is-not-set
        Menu2 = menu2;
        Menu = menu;
    }

    BOOL MenuIsAssigned() { return Menu != NULL; }

    HMENU GetMenu() { return Menu; }
    IContextMenu2* GetMenu2() { return Menu2; }

    void Release();
    void ReleaseBody();
};

//
//*****************************************************************************
// CTextDataObject
//

class CTextDataObject : public IDataObject
{
private:
    long RefCount;
    HGLOBAL Data;

public:
    CTextDataObject(HGLOBAL data)
    {
        RefCount = 1;
        Data = data;
    }
    virtual ~CTextDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        NOHANDLES(GlobalFree(Data));
    }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR* FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void) { return ++RefCount; }
    STDMETHOD_(ULONG, Release)
    (void)
    {
        if (--RefCount == 0)
        {
            delete this;
            return 0; // nesmime sahnout do objektu, uz neexistuje
        }
        return RefCount;
    }

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        if (formatEtc == NULL)
            return E_INVALIDARG;
        if ((formatEtc->cfFormat == CF_TEXT || formatEtc->cfFormat == CF_UNICODETEXT) &&
            (formatEtc->tymed & TYMED_HGLOBAL))
        {
            return S_OK;
        }
        return (formatEtc->tymed & TYMED_HGLOBAL) ? DV_E_FORMATETC : DV_E_TYMED;
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        return OLE_E_ADVISENOTSUPPORTED;
    }
};

// uvolni CopyMoveData
void DestroyCopyMoveData(CCopyMoveData* data);
