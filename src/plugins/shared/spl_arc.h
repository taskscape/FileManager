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
#pragma pack(push, enter_include_spl_arc) // so that structures are independent of the set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

class CSalamanderDirectoryAbstract;
class CSalamanderForOperationsAbstract;
class CPluginDataInterfaceAbstract;

//
// ****************************************************************************
// CPluginInterfaceForArchiverAbstract
//

class CPluginInterfaceForArchiverAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct method calls (see CPluginInterfaceForArchiverEncapsulation)
    friend class CPluginInterfaceForArchiverEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // function for "panel archiver view"; called to load archive 'fileName' content;
    // content is filled into the 'dir' object; Salamander will find the content
    // columns added by plugin using interface 'pluginData' (if plugin does not add columns
    // returns 'pluginData'==NULL); returns TRUE on successful archive content loading,
    // if returns FALSE, return value 'pluginData' is ignored (data in 'dir' must be
    // freed using 'dir.Clear(pluginData)', otherwise only Salamander part of data is freed);
    // 'salamander' is a set of useful methods exported from Salamander,
    // WARNING: file fileName may not exist (if opened in panel and deleted from elsewhere),
    // ListArchive is not called for zero-length files, they automatically have empty content,
    // when packing into such files, the file is deleted before calling PackToArchive (for
    // compatibility with external packers)
    virtual BOOL WINAPI ListArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                    CSalamanderDirectoryAbstract* dir,
                                    CPluginDataInterfaceAbstract*& pluginData) = 0;

    // function for "panel archiver view", called on request to unpack files/directories
    // from archive 'fileName' to directory 'targetDir' from path in archive 'archiveRoot'; 'pluginData'
    // is interface for working with file/directory information that is plugin-specific
    // (e.g. data from added columns; same interface that ListArchive method returns
    // in parameter 'pluginData' - so it can be NULL); files/directories are specified by enumeration
    // function 'next' with parameter 'nextParam'; returns TRUE on successful unpacking (Cancel was not
    // used, Skip could be used) - operation source in panel is unmarked, otherwise returns
    // FALSE (no unmark is performed); 'salamander' is a set of useful methods exported from
    // Salamander
    virtual BOOL WINAPI UnpackArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const char* targetDir,
                                      const char* archiveRoot, SalEnumSelection next,
                                      void* nextParam) = 0;

    // function for "panel archiver view", called on request to unpack one file for view/edit
    // from archive 'fileName' to directory 'targetDir'; file name in archive is 'nameInArchive';
    // 'pluginData' is interface for working with file information that is plugin-specific
    // (e.g. data from added columns; same interface that ListArchive method returns
    // in parameter 'pluginData' - so it can be NULL); 'fileData' is pointer to CFileData structure
    // of unpacked file (structure was created by plugin when listing archive); 'newFileName' (if not
    // NULL) is new name for unpacked file (used if original name from archive cannot be
    // unpacked to disk (e.g. "aux", "prn", etc.)); write TRUE to 'renamingNotSupported' (only if 'newFileName' is not
    // NULL) if plugin does not support renaming during unpacking (standard error message
    // "renaming not supported" will be displayed from Salamander); returns TRUE on successful file unpacking
    // (file is at specified path, Cancel and Skip were not used), 'salamander' is a set of useful methods
    // exported from Salamander
    virtual BOOL WINAPI UnpackOneFile(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      CPluginDataInterfaceAbstract* pluginData, const char* nameInArchive,
                                      const CFileData* fileData, const char* targetDir,
                                      const char* newFileName, BOOL* renamingNotSupported) = 0;

    // function for "panel archiver edit" and "custom archiver pack", called on request to pack
    // files/directories to archive 'fileName' at path 'archiveRoot', files/directories are specified by
    // source path 'sourcePath' and enumeration function 'next' with parameter 'nextParam',
    // if 'move' is TRUE, packed files/directories should be removed from disk, returns TRUE
    // if all files/directories were successfully packed/removed (Cancel was not used, Skip could be
    // used) - operation source in panel is unmarked, otherwise returns FALSE (no unmark is performed),
    // 'salamander' is a set of useful methods exported from Salamander
    virtual BOOL WINAPI PackToArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                      const char* archiveRoot, BOOL move, const char* sourcePath,
                                      SalEnumSelection2 next, void* nextParam) = 0;

    // function for "panel archiver edit", called on request to delete files/directories from archive
    // 'fileName'; files/directories are specified by path 'archiveRoot' and enumeration function 'next'
    // with parameter 'nextParam'; 'pluginData' is interface for working with file/directory information
    // that is plugin-specific (e.g. data from added columns; same interface that
    // ListArchive method returns in parameter 'pluginData' - so it can be NULL); returns TRUE if
    // all files/directories were successfully deleted (Cancel was not used, Skip could be used) - operation
    // source in panel is unmarked, otherwise returns FALSE (no unmark is performed); 'salamander' is a set of
    // useful methods exported from Salamander
    virtual BOOL WINAPI DeleteFromArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                          CPluginDataInterfaceAbstract* pluginData, const char* archiveRoot,
                                          SalEnumSelection next, void* nextParam) = 0;

    // function for "custom archiver unpack"; called on request to unpack files/directories from archive
    // 'fileName' to directory 'targetDir'; files/directories are specified by mask 'mask'; returns TRUE if
    // all files/directories were successfully unpacked (Cancel was not used, Skip could be used);
    // if 'delArchiveWhenDone' is TRUE, it's necessary to add to 'archiveVolumes' all archive volumes
    // (including null-terminator; if not multi-volume, there will be only 'fileName'), if this function returns
    // TRUE (successful unpacking), all files from 'archiveVolumes' will be subsequently deleted;
    // 'salamander' is a set of useful methods exported from Salamander
    virtual BOOL WINAPI UnpackWholeArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                           const char* mask, const char* targetDir, BOOL delArchiveWhenDone,
                                           CDynamicString* archiveVolumes) = 0;

    // function for "panel archiver view/edit", called before closing panel with archive
    // WARNING: if opening new path fails, archive can stay in panel (regardless of
    //        what CanCloseArchive returns); therefore this method cannot be used for context destruction;
    //        it's intended for example for optimizing Delete operation from archive, when it can
    //        offer "compacting" of archive when leaving it
    //        for context destruction use CPluginInterfaceAbstract::ReleasePluginDataInterface method,
    //        see document archivatory.txt
    // 'fileName' is archive name; 'salamander' is a set of useful methods exported from Salamander;
    // 'panel' indicates panel in which archive is open (PANEL_LEFT or PANEL_RIGHT);
    // returns TRUE if closing is possible, if 'force' is TRUE, returns TRUE always; if
    // critical shutdown is in progress (see CSalamanderGeneralAbstract::IsCriticalShutdown for more), there's
    // no point in asking user anything
    virtual BOOL WINAPI CanCloseArchive(CSalamanderForOperationsAbstract* salamander, const char* fileName,
                                        BOOL force, int panel) = 0;

    // determines required disk-cache settings (disk-cache is used for temporary copies
    // of files when opening files from archive in viewers, editors and through system
    // associations); normally (if copying 'tempPath' succeeds after calling) it's called
    // only once before first use of disk-cache (e.g. before first opening
    // file from archive in viewer/editor); if returns FALSE, uses
    // standard settings (files in TEMP directory, copies are deleted using Win32
    // API function DeleteFile() only when cache size limit is exceeded or when archive is closed)
    // and all other return values are ignored; if returns TRUE, following
    // return values are used: if 'tempPath' (buffer of size MAX_PATH) is not empty string,
    // all temporary copies unpacked by plugin from archive will be stored in subdirectories of this path
    // (these subdirectories are removed by disk-cache when Salamander closes, but nothing prevents plugin
    // from deleting them earlier, e.g. during its unload; also it's recommended when loading first instance
    // of plugin (not just within one running Salamander) to clean up "SAL*.tmp" subdirectories on this
    // path - solves problems caused by locked files and software crashes) + if 'ownDelete' is TRUE,
    // DeleteTmpCopy and PrematureDeleteTmpCopy methods will be called for deleting copies + if
    // 'cacheCopies' is FALSE, copies will be deleted as soon as they're released (e.g. as soon as
    // viewer is closed), if 'cacheCopies' is TRUE, copies will be deleted only when cache size limit is exceeded
    // or when archive is closed
    virtual BOOL WINAPI GetCacheInfo(char* tempPath, BOOL* ownDelete, BOOL* cacheCopies) = 0;

    // used only if GetCacheInfo method returns TRUE in parameter 'ownDelete':
    // deletes temporary copy unpacked from this archive (beware of read-only files,
    // for them attributes must be changed first, only then can they be deleted), if possible
    // should not display any windows (user didn't directly invoke action, could disturb him
    // in other activity), for longer actions it's useful to use wait-window (see
    // CSalamanderGeneralAbstract::CreateSafeWaitWindow); 'fileName' is name of file
    // with copy; if multiple files are deleted at once (can happen e.g. after closing
    // edited archive), 'firstFile' is TRUE for first file and FALSE for other
    // files (used for correct wait-window display - see DEMOPLUG)
    //
    // WARNING: called in main thread based on message delivery from disk-cache to main window - sends
    // message about need to release temporary copy (typically when viewer is closed or
    // "edited" archive in panel), so repeated entry to plugin can occur
    // (if message is distributed by message-loop inside plugin), further entry to DeleteTmpCopy
    // is excluded, because until DeleteTmpCopy call finishes, disk-cache doesn't send any more messages
    virtual void WINAPI DeleteTmpCopy(const char* fileName, BOOL firstFile) = 0;

    // used only if GetCacheInfo method returns TRUE in parameter 'ownDelete':
    // during plugin unload determines whether DeleteTmpCopy should be called for copies that are
    // still in use (e.g. open in viewer) - called only if such copies
    // exist; 'parent' is parent of potential messagebox with user query (possibly
    // recommendation that user should close all files from archive so plugin can delete them);
    // 'copiesCount' is number of used copies of files from archive; returns TRUE if
    // DeleteTmpCopy should be called, if returns FALSE, copies remain on disk; if
    // critical shutdown is in progress (see CSalamanderGeneralAbstract::IsCriticalShutdown for more), there's
    // no point in asking user anything and performing lengthy actions (e.g. file shredding)
    // NOTE: during PrematureDeleteTmpCopy execution it's ensured that
    // DeleteTmpCopy is not called
    virtual BOOL WINAPI PrematureDeleteTmpCopy(HWND parent, int copiesCount) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_arc)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
