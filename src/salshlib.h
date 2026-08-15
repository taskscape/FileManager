// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <strsafe.h>

// Mutex for access to shared memory.
extern HANDLE SalShExtSharedMemMutex;
// Shared memory - see CSalShExtSharedMem structure.
extern HANDLE SalShExtSharedMem;
// Event for sending a request to perform Paste in the source Salamander (used only on Vista+).
extern HANDLE SalShExtDoPasteEvent;
// Mapped shared memory - see CSalShExtSharedMem structure.
extern CSalShExtSharedMem* SalShExtSharedMemView;

// TRUE if SalShExt/SalExten/SalamExt/SalExtX86/SalExtX64.DLL was registered successfully or was already registered.
extern BOOL SalShExtRegistered;

// Major hack: we need to find out which window the Drop will go to; we detect it
// in GetData by mouse position, and this variable holds the last test result.
extern HWND LastWndFromGetData;

// Major hack: we need to find out which window the Paste will go to; we detect it
// in GetData by foreground window, and this variable holds the last test result.
extern HWND LastWndFromPasteGetData;

extern BOOL OurDataOnClipboard; // TRUE = our data object is on the clipboard (copy&paste from archive)

//*****************************************************************************

// Call before using the library.
void InitSalShLib();

// Call to release the library.
void ReleaseSalShLib();

// Returns TRUE if the data object contains only a "fake" directory; in 'fakeType' (if not NULL) returns
// 1 if the source is archive and 2 if the source is FS; if the source is FS and 'srcFSPathBuf' is not NULL,
// returns the source FS path ('srcFSPathBufSize' is the size of buffer 'srcFSPathBuf').
BOOL IsFakeDataObject(IDataObject* pDataObject, int* fakeType, char* srcFSPathBuf, int srcFSPathBufSize);

//
//*****************************************************************************
// CFakeDragDropDataObject
//
// Data object used to determine the target of a drag&drop operation (used when
// unpacking from an archive and copying from a plugin file-system).
// Wraps the Windows data object obtained for the "fake" directory and adds
// format SALCF_FAKE_REALPATH (determines the path that should appear after drop
// in the directory line, command line + blocks drop into the usermenu toolbar),
// SALCF_FAKE_SRCTYPE (source type - 1=archive, 2=FS) and, for FS, also
// SALCF_FAKE_SRCFSPATH (source FS path) to GetData().

class CFakeDragDropDataObject : public IDataObject
{
private:
    long RefCount;
    IDataObject* WinDataObject;   // wrapped data object
    char RealPath[2 * MAX_PATH];  // path for drop into directory and command line
    int SrcType;                  // source type (1=archive, 2=FS)
    char SrcFSPath[2 * MAX_PATH]; // only for FS source type: source FS path
    UINT CFSalFakeRealPath;       // clipboard format for sal-fake-real-path
    UINT CFSalFakeSrcType;        // clipboard format for sal-fake-src-type
    UINT CFSalFakeSrcFSPath;      // clipboard format for sal-fake-src-fs-path

public:
    CFakeDragDropDataObject(IDataObject* winDataObject, const char* realPath, int srcType,
                            const char* srcFSPath)
    {
        RefCount = 1;
        WinDataObject = winDataObject;
        WinDataObject->AddRef();
        // Drag/drop paths are operation identities and must not be retained truncated.
        if (FAILED(StringCchCopyA(RealPath, _countof(RealPath), realPath)))
            RealPath[0] = 0;
        if (srcFSPath != NULL && srcType == 2 /* FS */)
        {
            if (FAILED(StringCchCopyA(SrcFSPath, _countof(SrcFSPath), srcFSPath)))
                SrcFSPath[0] = 0;
        }
        else
            SrcFSPath[0] = 0;
        SrcType = srcType;
        CFSalFakeRealPath = RegisterClipboardFormat(SALCF_FAKE_REALPATH);
        CFSalFakeSrcType = RegisterClipboardFormat(SALCF_FAKE_SRCTYPE);
        CFSalFakeSrcFSPath = RegisterClipboardFormat(SALCF_FAKE_SRCFSPATH);
    }

    virtual ~CFakeDragDropDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        WinDataObject->Release();
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

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        return WinDataObject->GetDataHere(pFormatetc, pmedium);
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        if (formatEtc->cfFormat == CF_HDROP)
            return DV_E_FORMATETC; // ensures "NO" drop in simpler software (BOSS, WinCmd, SpeedCommander, MSIE, Word, etc.)
        return WinDataObject->QueryGetData(formatEtc);
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        return WinDataObject->GetCanonicalFormatEtc(pFormatetcIn, pFormatetcOut);
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        return WinDataObject->SetData(pFormatetc, pmedium, fRelease);
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        return WinDataObject->EnumFormatEtc(dwDirection, ppenumFormatetc);
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        return WinDataObject->DAdvise(pFormatetc, advf, pAdvSink, pdwConnection);
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        return WinDataObject->DUnadvise(dwConnection);
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        return WinDataObject->EnumDAdvise(ppenumAdvise);
    }
};

//
//*****************************************************************************
// CSalShExtPastedData
//
// Data for Paste from clipboard stored inside the "source" Salamander.

class CSalamanderDirectory;

class CSalShExtPastedData
{
protected:
    DWORD DataID; // version of data stored for Paste from clipboard

    BOOL Lock; // TRUE = locked against cancellation, FALSE = not locked

    char ArchiveFileName[MAX_PATH]; // full path to the archive
    char PathInArchive[MAX_PATH];   // path inside the archive where Copy to clipboard occurred
    CNames SelFilesAndDirs;         // names of files and directories from PathInArchive that will be unpacked

    CSalamanderDirectory* StoredArchiveDir;             // stored archive structure (used if the archive is not open in a panel)
    CPluginDataInterfaceEncapsulation StoredPluginData; // stored archive plugin-data interface (used if the archive is not open in a panel)
    FILETIME StoredArchiveDate;                         // archive file date (for archive listing validity tests)
    CQuadWord StoredArchiveSize;                        // archive file size (for archive listing validity tests)

public:
    CSalShExtPastedData();
    ~CSalShExtPastedData();

    DWORD GetDataID() { return DataID; }
    void SetDataID(DWORD dataID) { DataID = dataID; }

    BOOL IsLocked() { return Lock; }
    void SetLock(BOOL lock) { Lock = lock; }

    // Sets object data, returns TRUE on success, leaves the object empty on failure
    // and returns FALSE.
    BOOL SetData(const char* archiveFileName, const char* pathInArchive, CFilesArray* files,
                 CFilesArray* dirs, BOOL namesAreCaseSensitive, int* selIndexes,
                 int selIndexesCount);

    // Clears data stored in StoredArchiveDir and StoredPluginData.
    void ReleaseStoredArchiveData();

    // Clears the object (destroys all its data, object remains ready for further use).
    void Clear();

    // Performs the paste operation with current data; 'copy' is TRUE if data should be copied,
    // FALSE if it should be moved; 'tgtPath' is the target disk path of the operation.
    void DoPasteOperation(BOOL copy, const char* tgtPath);

    // If the supplied data suits the object, keeps it and returns TRUE, otherwise returns
    // FALSE (the supplied data will then be released).
    BOOL WantData(const char* archiveFileName, CSalamanderDirectory* archiveDir,
                  CPluginDataInterfaceEncapsulation pluginData,
                  FILETIME archiveDate, CQuadWord archiveSize);

    // Returns TRUE if plugin 'plugin' can be unloaded; if the object contains data
    // from plugin 'plugin', tries to get rid of it so it can return TRUE.
    BOOL CanUnloadPlugin(HWND parent, CPluginInterfaceAbstract* plugin);
};

// Data for Paste from clipboard stored inside the "source" Salamander.
extern CSalShExtPastedData SalShExtPastedData;

//
//*****************************************************************************
// CFakeCopyPasteDataObject
//
// Data object used to determine the target of a copy&paste operation (used when
// unpacking from an archive), wraps the Windows data object obtained for the "fake"
// directory and ensures deletion of the "fake" directory from disk after releasing the object from
// the clipboard.

class CFakeCopyPasteDataObject : public IDataObject
{
private:
    long RefCount;
    IDataObject* WinDataObject; // wrapped data object
    char FakeDir[MAX_PATH];     // "fake" dir
    UINT CFSalFakeRealPath;     // clipboard format for sal-fake-real-path
    UINT CFIdList;              // clipboard format for shell id list (Explorer uses it instead of simpler CF_HDROP)

    DWORD LastGetDataCallTime; // time of last GetData() call
    BOOL CutOrCopyDone;        // FALSE = object is only being placed on clipboard; Release does nothing until CutOrCopyDone is TRUE

public:
    CFakeCopyPasteDataObject(IDataObject* winDataObject, const char* fakeDir)
    {
        RefCount = 1;
        WinDataObject = winDataObject;
        WinDataObject->AddRef();
        // Clipboard cleanup must use a complete temporary-directory identity.
        if (FAILED(StringCchCopyA(FakeDir, _countof(FakeDir), fakeDir)))
            FakeDir[0] = 0;
        CFSalFakeRealPath = RegisterClipboardFormat(SALCF_FAKE_REALPATH);
        CFIdList = RegisterClipboardFormat(CFSTR_SHELLIDLIST);
        LastGetDataCallTime = GetTickCount() - 60000; // initialize to 1 minute before object creation
        CutOrCopyDone = FALSE;
    }

    virtual ~CFakeCopyPasteDataObject()
    {
        if (RefCount != 0)
            TRACE_E("Preliminary destruction of this object.");
        WinDataObject->Release();
    }

    void SetCutOrCopyDone() { CutOrCopyDone = TRUE; }

    STDMETHOD(QueryInterface)
    (REFIID, void FAR* FAR*);
    STDMETHOD_(ULONG, AddRef)
    (void)
    {
        //      TRACE_I("AddRef");
        return ++RefCount;
    }
    STDMETHOD_(ULONG, Release)
    (void);

    STDMETHOD(GetData)
    (FORMATETC* formatEtc, STGMEDIUM* medium);

    STDMETHOD(GetDataHere)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium)
    {
        //      TRACE_I("GetDataHere");
        return WinDataObject->GetDataHere(pFormatetc, pmedium);
    }

    STDMETHOD(QueryGetData)
    (FORMATETC* formatEtc)
    {
        //      TRACE_I("QueryGetData");
        if (formatEtc->cfFormat == CF_HDROP)
            return DV_E_FORMATETC; // timto zajistime "NO" drop u jednodussich softu (BOSS, WinCmd, SpeedCommander, MSIE, Word, atd.)
        return WinDataObject->QueryGetData(formatEtc);
    }

    STDMETHOD(GetCanonicalFormatEtc)
    (FORMATETC* pFormatetcIn, FORMATETC* pFormatetcOut)
    {
        //      TRACE_I("GetCanonicalFormatEtc");
        return WinDataObject->GetCanonicalFormatEtc(pFormatetcIn, pFormatetcOut);
    }

    STDMETHOD(SetData)
    (FORMATETC* pFormatetc, STGMEDIUM* pmedium, BOOL fRelease)
    {
        //      TRACE_I("SetData");
        return WinDataObject->SetData(pFormatetc, pmedium, fRelease);
    }

    STDMETHOD(EnumFormatEtc)
    (DWORD dwDirection, IEnumFORMATETC** ppenumFormatetc)
    {
        //      TRACE_I("EnumFormatEtc");
        return WinDataObject->EnumFormatEtc(dwDirection, ppenumFormatetc);
    }

    STDMETHOD(DAdvise)
    (FORMATETC* pFormatetc, DWORD advf, IAdviseSink* pAdvSink,
     DWORD* pdwConnection)
    {
        //      TRACE_I("DAdvise");
        return WinDataObject->DAdvise(pFormatetc, advf, pAdvSink, pdwConnection);
    }

    STDMETHOD(DUnadvise)
    (DWORD dwConnection)
    {
        //      TRACE_I("DUnadvise");
        return WinDataObject->DUnadvise(dwConnection);
    }

    STDMETHOD(EnumDAdvise)
    (IEnumSTATDATA** ppenumAdvise)
    {
        //      TRACE_I("EnumDAdvise");
        return WinDataObject->EnumDAdvise(ppenumAdvise);
    }
};
