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
#pragma pack(push, enter_include_spl_com) // to make structures independent of the set alignment
#pragma pack(4)
#pragma warning(3 : 4706) // warning C4706: assignment within conditional expression
#endif                    // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

// in the plugin it is necessary to define the variable SalamanderVersion (int) and in SalamanderPluginEntry
// initialize this variable:
// SalamanderVersion = salamander->GetVersion();

// global variable with the version of Salamander in which this plugin is loaded
extern int SalamanderVersion;

//
// ****************************************************************************
// CSalamanderDirectoryAbstract
//
// class represents directory structure - files and directories on required paths, root path is "",
// separators in the path are backslashes ('\\')
//

// CQuadWord - 64-bit unsigned integer for file sizes
// tricks:
//  -faster passing of input parameter of type CQuadWord: const CQuadWord &
//  -assign 64-bit integer: quadWord.Value = XXX;
//  -calculate size ratio: quadWord1.GetDouble() / quadWord2.GetDouble()  // loss of precision before division manifests minimally (max. 1e-15)
//  -truncate to DWORD: (DWORD)quadWord.Value
//  -convert (unsigned) __int64 to CQuadWord: CQuadWord().SetUI64(XXX)

struct CQuadWord
{
    union
    {
        struct
        {
            DWORD LoDWord;
            DWORD HiDWord;
        };
        unsigned __int64 Value;
    };

    // WARNING: assignment operator or constructor for single DWORD must not come here,
    //        otherwise use of 8-byte numbers will be completely uncontrollable (C++ will convert
    //        everything mutually, which may not always be exactly right)

    CQuadWord() {}
    CQuadWord(DWORD lo, DWORD hi)
    {
        LoDWord = lo;
        HiDWord = hi;
    }
    CQuadWord(const CQuadWord& qw)
    {
        LoDWord = qw.LoDWord;
        HiDWord = qw.HiDWord;
    }

    CQuadWord& Set(DWORD lo, DWORD hi)
    {
        LoDWord = lo;
        HiDWord = hi;
        return *this;
    }
    CQuadWord& SetUI64(unsigned __int64 val)
    {
        Value = val;
        return *this;
    }
    CQuadWord& SetDouble(double val)
    {
        Value = (unsigned __int64)val;
        return *this;
    }

    CQuadWord& operator++()
    {
        ++Value;
        return *this;
    } // prefix ++
    CQuadWord& operator--()
    {
        --Value;
        return *this;
    } // prefix --

    CQuadWord operator+(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value + qw.Value;
        return qwr;
    }
    CQuadWord operator-(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value - qw.Value;
        return qwr;
    }
    CQuadWord operator*(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value * qw.Value;
        return qwr;
    }
    CQuadWord operator/(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value / qw.Value;
        return qwr;
    }
    CQuadWord operator%(const CQuadWord& qw) const
    {
        CQuadWord qwr;
        qwr.Value = Value % qw.Value;
        return qwr;
    }
    CQuadWord operator<<(const int num) const
    {
        CQuadWord qwr;
        qwr.Value = Value << num;
        return qwr;
    }
    CQuadWord operator>>(const int num) const
    {
        CQuadWord qwr;
        qwr.Value = Value >> num;
        return qwr;
    }

    CQuadWord& operator+=(const CQuadWord& qw)
    {
        Value += qw.Value;
        return *this;
    }
    CQuadWord& operator-=(const CQuadWord& qw)
    {
        Value -= qw.Value;
        return *this;
    }
    CQuadWord& operator*=(const CQuadWord& qw)
    {
        Value *= qw.Value;
        return *this;
    }
    CQuadWord& operator/=(const CQuadWord& qw)
    {
        Value /= qw.Value;
        return *this;
    }
    CQuadWord& operator%=(const CQuadWord& qw)
    {
        Value %= qw.Value;
        return *this;
    }
    CQuadWord& operator<<=(const int num)
    {
        Value <<= num;
        return *this;
    }
    CQuadWord& operator>>=(const int num)
    {
        Value >>= num;
        return *this;
    }

    BOOL operator==(const CQuadWord& qw) const { return Value == qw.Value; }
    BOOL operator!=(const CQuadWord& qw) const { return Value != qw.Value; }
    BOOL operator<(const CQuadWord& qw) const { return Value < qw.Value; }
    BOOL operator>(const CQuadWord& qw) const { return Value > qw.Value; }
    BOOL operator<=(const CQuadWord& qw) const { return Value <= qw.Value; }
    BOOL operator>=(const CQuadWord& qw) const { return Value >= qw.Value; }

    // conversion to double (beware of loss of precision with large numbers - double has only 15 significant digits)
    double GetDouble() const
    { // MSVC cannot convert unsigned __int64 to double, so we must help ourselves
        if (Value < CQuadWord(0, 0x80000000).Value)
            return (double)(__int64)Value; // positive number
        else
            return 9223372036854775808.0 + (double)(__int64)(Value - CQuadWord(0, 0x80000000).Value);
    }
};

#define QW_MAX CQuadWord(0xFFFFFFFF, 0xFFFFFFFF)

#define ICONOVERLAYINDEX_NOTUSED 15 // value for CFileData::IconOverlayIndex in case icon has no overlay

// record of each file and directory in Salamander (basic data about file/directory)
struct CFileData // destructor must not be added here!
{
    char* Name;                    // allocated file name (without path), must be allocated on Salamander's heap
                                   // (see CSalamanderGeneralAbstract::Alloc/Realloc/Free)
    char* Ext;                     // pointer to Name after the first dot from right (including dot at beginning of name,
                                   // on Windows is understood as extension, unlike on UNIX) or at end
                                   // of Name, if extension does not exist; if FALSE is set in configuration
                                   // for SALCFG_SORTBYEXTDIRSASFILES, Ext for directories is a pointer to the end of
                                   // Name (directories have no extensions)
    CQuadWord Size;                // file size in bytes
    DWORD Attr;                    // file attributes - ORed FILE_ATTRIBUTE_XXX constants
    FILETIME LastWrite;            // time of last write to file (UTC-based time)
    char* DosName;                 // allocated DOS 8.3 file name, if not needed it is NULL, must be
                                   // allocated on Salamander's heap (see CSalamanderGeneralAbstract::Alloc/Realloc/Free)
    DWORD_PTR PluginData;          // used by plugin through CPluginDataInterfaceAbstract, Salamander ignores it
    unsigned NameLen : 9;          // length of Name string (strlen(Name)) - WARNING: maximum name length is (MAX_PATH - 5)
    unsigned Hidden : 1;           // is hidden? (if 1, icon is 50% more transparent - ghosted)
    unsigned IsLink : 1;           // is link? (if 1, icon has link overlay) - standard filling see CSalamanderGeneralAbstract::IsFileLink(CFileData::Ext), when displaying has priority over IsOffline, but IconOverlayIndex has priority
    unsigned IsOffline : 1;        // is offline? (if 1, icon has offline overlay - black clock), when displaying both IsLink and IconOverlayIndex have priority
    unsigned IconOverlayIndex : 4; // icon overlay index (if icon has no overlay, there is value ICONOVERLAYINDEX_NOTUSED), when displaying has priority over IsLink and IsOffline

    // flags for internal use in Salamander: zeroed when added to CSalamanderDirectoryAbstract
    unsigned Association : 1;     // meaning only for displaying 'simple icons' - icon of associated file, otherwise 0
    unsigned Selected : 1;        // read-only selection flag (0 - item not selected, 1 - item selected)
    unsigned Shared : 1;          // is directory shared? not used for files
    unsigned Archive : 1;         // is it an archive? used for displaying archive icon in panel
    unsigned SizeValid : 1;       // is the directory's size calculated?
    unsigned Dirty : 1;           // does this item need repainting? (only temporary validity; between setting the bit and repainting the panel, message queue must not be pumped, otherwise icon repainting (icon reader) can occur and thus bit reset! consequently the item won't be repainted)
    unsigned CutToClip : 1;       // is CUT to clipboard? (if 1, icon is 50% more transparent - ghosted)
    unsigned IconOverlayDone : 1; // only for icon-reader-thread needs: are we getting or have we already gotten icon-overlay? (0 - no, 1 - yes)
};

// constants determining validity of data that is directly stored in CFileData (size, extension, etc.)
// or generated from directly stored data automatically (file-type is generated from extension);
// Name + NameLen are mandatory (must be valid always); validity of PluginData is managed by plugin itself
// (Salamander ignores this attribute)
#define VALID_DATA_EXTENSION 0x0001   // extension is stored in Ext (without: all Ext = end of Name)
#define VALID_DATA_DOSNAME 0x0002     // DOS name is stored in DosName (without: all DosName = NULL)
#define VALID_DATA_SIZE 0x0004        // size in bytes is stored in Size (without: all Size = 0)
#define VALID_DATA_TYPE 0x0008        // file-type can be generated from Ext (without: not generated)
#define VALID_DATA_DATE 0x0010        // modification date (UTC-based) is stored in LastWrite (without: all dates in LastWrite are 1.1.1602 in local time)
#define VALID_DATA_TIME 0x0020        // modification time (UTC-based) is stored in LastWrite (without: all times in LastWrite are 0:00:00 in local time)
#define VALID_DATA_ATTRIBUTES 0x0040  // attributes are stored in Attr (ORed Win32 API constants FILE_ATTRIBUTE_XXX) (without: all Attr = 0)
#define VALID_DATA_HIDDEN 0x0080      // "ghosted" icon flag is stored in Hidden (without: all Hidden = 0)
#define VALID_DATA_ISLINK 0x0100      // IsLink contains 1 if it is a link, icon has link overlay (without: all IsLink = 0)
#define VALID_DATA_ISOFFLINE 0x0200   // IsOffline contains 1 if it is an offline file/directory, icon has offline overlay (without: all IsOffline = 0)
#define VALID_DATA_PL_SIZE 0x0400     // makes sense only without using VALID_DATA_SIZE: plugin has stored size in bytes for at least some files/directories (somewhere in PluginData), to get this size Salamander calls CPluginDataInterfaceAbstract::GetByteSize()
#define VALID_DATA_PL_DATE 0x0800     // makes sense only without using VALID_DATA_DATE: plugin has stored modification date for at least some files/directories (somewhere in PluginData), to get this size Salamander calls CPluginDataInterfaceAbstract::GetLastWriteDate()
#define VALID_DATA_PL_TIME 0x1000     // makes sense only without using VALID_DATA_TIME: plugin has stored modification time for at least some files/directories (somewhere in PluginData), to get this size Salamander calls CPluginDataInterfaceAbstract::GetLastWriteTime()
#define VALID_DATA_ICONOVERLAY 0x2000 // IconOverlayIndex is icon-overlay index (no overlay = value ICONOVERLAYINDEX_NOTUSED) (without: all IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED), icon specification see CSalamanderGeneralAbstract::SetPluginIconOverlays

#define VALID_DATA_NONE 0 // helper constant - only Name and NameLen are valid

#ifdef INSIDE_SALAMANDER
// VALID_DATA_ALL and VALID_DATA_ALL_FS_ARC are only for internal use in Salamander (core),
// plugins only OR constants corresponding to plugin-supplied data (this prevents problems
// when introducing additional constants and their corresponding data)
#define VALID_DATA_ALL 0xFFFF
#define VALID_DATA_ALL_FS_ARC (0xFFFF & ~VALID_DATA_ICONOVERLAY) // for FS and archives: everything except icon-overlays
#endif                                                           // INSIDE_SALAMANDER

// If hiding of hidden and system files and directories is enabled, items with
// Hidden==1 and Attr containing FILE_ATTRIBUTE_HIDDEN and/or FILE_ATTRIBUTE_SYSTEM are not displayed in panels.

// flag constants for CSalamanderDirectoryAbstract:
// file and directory names (including in paths) should be compared case-sensitive (without this flag
// comparison is case-insensitive - standard behavior in Windows)
#define SALDIRFLAG_CASESENSITIVE 0x0001
// subdirectory names within each directory will not be tested for duplicity (this
// test is time-consuming and is only necessary in archives, if items are added not only
// to root - so that for example adding "file1" to "dir1" followed by adding
// "dir1" works - "dir1" is added in first operation (non-existing path is automatically added),
// second operation only updates data about "dir1" (must not add it again))
#define SALDIRFLAG_IGNOREDUPDIRS 0x0002

class CPluginDataInterfaceAbstract;

class CSalamanderDirectoryAbstract
{
public:
    // clears the entire object, prepares it for next use; if 'pluginData' is not NULL, it is used
    // for files and directories to release plugin-specific data (CFileData::PluginData);
    // sets the standard value of valid data mask (sum of all VALID_DATA_XXX except
    // VALID_DATA_ICONOVERLAY) and object flags (see SetFlags method)
    virtual void WINAPI Clear(CPluginDataInterfaceAbstract* pluginData) = 0;

    // specification of valid data mask, according to which it is determined which data from CFileData is valid
    // and which should only be "zeroed" (see comment to VALID_DATA_XXX); 'validData' mask
    // contains ORed VALID_DATA_XXX values; standard mask value is sum of all
    // VALID_DATA_XXX except VALID_DATA_ICONOVERLAY; valid data mask needs to be set
    // before calling AddFile/AddDir
    virtual void WINAPI SetValidData(DWORD validData) = 0;

    // setting flags for this object; 'flags' is combination of ORed SALDIRFLAG_XXX flags,
    // standard object flag value is zero for archivers (no flag is set)
    // and SALDIRFLAG_IGNOREDUPDIRS for file-systems (only root can be added to, test for duplicity
    // of directories is unnecessary)
    virtual void WINAPI SetFlags(DWORD flags) = 0;

    // adds file at specified path (relative to this "salamander-directory"), returns success
    // path string is used only inside function, content of file structure is used also outside function
    // (do not release memory allocated for variables inside structure)
    // in case of failure, the content of file structure must be released;
    // parameter 'pluginData' is not NULL only for archives (FS use only empty 'path' (==NULL));
    // if 'pluginData' is not NULL, 'pluginData' is used when creating new directories (if
    // 'path' does not exist), see CPluginDataInterfaceAbstract::GetFileDataForNewDir;
    // check for uniqueness of file name on path 'path' is not performed
    virtual BOOL WINAPI AddFile(const char* path, CFileData& file, CPluginDataInterfaceAbstract* pluginData) = 0;

    // adds directory at specified path (relative to this "salamander-directory"), returns success
    // path string is used only inside function, content of file structure is used also outside function
    // (do not release memory allocated for variables inside structure)
    // in case of failure, the content of file structure must be released;
    // parameter 'pluginData' is not NULL only for archives (FS use only empty 'path' (==NULL));
    // if 'pluginData' is not NULL, it is used when creating new directories (if 'path' does not exist),
    // see CPluginDataInterfaceAbstract::GetFileDataForNewDir;
    // check for uniqueness of directory name on path 'path' is performed, if adding
    // already existing directory, original data is released (if 'pluginData' is not NULL,
    // CPluginDataInterfaceAbstract::ReleasePluginData is also called for data release) and data from 'dir' is stored
    // (necessary for restoring data of directories that are created automatically when 'path' does not exist);
    // special feature for FS (or object allocated via CSalamanderGeneralAbstract::AllocSalamanderDirectory
    // with 'isForFS'==TRUE): if dir.Name is "..", directory is added as up-dir (there can be only one,
    // always displayed at the beginning of listing and has special icon)
    virtual BOOL WINAPI AddDir(const char* path, CFileData& dir, CPluginDataInterfaceAbstract* pluginData) = 0;

    // returns number of files in object
    virtual int WINAPI GetFilesCount() const = 0;

    // returns number of directories in object
    virtual int WINAPI GetDirsCount() const = 0;

    // returns file from index 'index', returned data can be used only for reading
    virtual CFileData const* WINAPI GetFile(int index) const = 0;

    // returns directory from index 'index', returned data can be used only for reading
    virtual CFileData const* WINAPI GetDir(int index) const = 0;

    // returns CSalamanderDirectory object for directory from index 'index', returned object can be
    // used only for reading (objects for empty directories are not allocated, one
    // global empty object is returned - change of this object would manifest globally)
    virtual CSalamanderDirectoryAbstract const* WINAPI GetSalDir(int index) const = 0;

    // Allows plugin to report in advance the expected number of files and directories in this directory.
    // Salamander adjusts reallocation strategy so that adding elements does not slow down too much.
    // Makes sense to call for directories containing thousands of files or directories. In case of tens of
    // thousands, calling this method is almost a necessity, otherwise reallocations will take several seconds.
    // 'files' and 'dirs' thus express approximate total number of files and directories.
    // If any of the values is -1, Salamander will ignore it.
    // Method makes sense to call only if directory is empty, i.e. AddFile or AddDir was not called.
    virtual void WINAPI SetApproximateCount(int files, int dirs) = 0;
};

//
// ****************************************************************************
// SalEnumSelection a SalEnumSelection2
//

// constants returned from SalEnumSelection and SalEnumSelection2 in parameter 'errorOccured'
#define SALENUM_SUCCESS 0 // error did not occur
#define SALENUM_ERROR 1   // error occurred and user wishes to continue operation (only erroneous files/directories were skipped)
#define SALENUM_CANCEL 2  // error occurred and user wishes to cancel operation

// enumerator, returns file names, ends by returning NULL;
// 'enumFiles' == -1 -> reset enumeration (after this call enumeration starts again from beginning), all
//                      other parameters (except 'param') are ignored, has no return values (sets
//                      everything to zero)
// 'enumFiles' == 0 -> enumeration of files and subdirectories only from root
// 'enumFiles' == 1 -> enumeration of all files and subdirectories
// 'enumFiles' == 2 -> enumeration of all subdirectories, files only from root;
// error can occur only with 'enumFiles' == 1 or 'enumFiles' == 2 ('enumFiles' == 0 does not complete
// names and paths); 'parent' is parent of possible error messageboxes (NULL means do not display
// errors); in 'isDir' (if not NULL) returns TRUE if it is a directory; in 'size' (if not NULL) returns
// file size (for directories size is returned only with 'enumFiles' == 0 - otherwise it is zero);
// if 'fileData' is not NULL, pointer to CFileData structure of returned
// file/directory is returned in it (if enumerator returns NULL, NULL is also returned in 'fileData');
// 'param' is parameter 'nextParam' passed along with pointer to function of this
// type; in 'errorOccured' (if not NULL) SALENUM_ERROR is returned, if during building of returned
// names a too long name was encountered and user decided to skip only erroneous files/directories,
// WARNING: error does not concern just returned name, that is OK; in 'errorOccured' (if not NULL)
// SALENUM_CANCEL is returned if user decided to cancel operation during error (cancel), at the same time
// enumerator returns NULL (ends); in 'errorOccured' (if not NULL) SALENUM_SUCCESS is returned if
// no error occurred
typedef const char*(WINAPI* SalEnumSelection)(HWND parent, int enumFiles, BOOL* isDir, CQuadWord* size,
                                              const CFileData** fileData, void* param, int* errorOccured);

// enumerator, returns file names, ends by returning NULL;
// 'enumFiles' == -1 -> reset enumeration (after this call enumeration starts again from beginning), all
//                      other parameters (except 'param') are ignored, has no return values (sets
//                      everything to zero)
// 'enumFiles' == 0 -> enumeration of files and subdirectories only from root
// 'enumFiles' == 1 -> enumeration of all files and subdirectories
// 'enumFiles' == 2 -> enumeration of all subdirectories, files only from root;
// 'enumFiles' == 3 -> enumeration of all files and subdirectories + symbolic links to files have
//                     size of target file (with 'enumFiles' == 1 they have size of link, which is probably
//                     always zero); WARNING: 'enumFiles' must remain 3 for all enumerator calls;
// error can occur only with 'enumFiles' == 1, 2 or 3 ('enumFiles' == 0 does not
// work with disk at all nor does it complete names and paths); 'parent' is parent of possible messageboxes
// with errors (NULL means do not display errors); in 'dosName' (if not NULL) returns DOS name
// (8.3; only if exists, otherwise NULL); in 'isDir' (if not NULL) returns TRUE if it is a directory;
// in 'size' (if not NULL) returns file size (zero for directories); in 'attr' (if not NULL)
// returns file/directory attributes; in 'lastWrite' (if not NULL) returns time of last write
// to file/directory; 'param' is parameter 'nextParam' passed along with pointer to function
// of this type; in 'errorOccured' (if not NULL) SALENUM_ERROR is returned, if during reading
// data from disk an error occurred or during building of returned names a too long name was encountered
// and user decided to skip only erroneous files/directories, WARNING: error does not concern just
// returned name, that is OK; in 'errorOccured' (if not NULL) SALENUM_CANCEL is returned if
// user decided to cancel operation during error (cancel), at the same time enumerator returns NULL (ends);
// in 'errorOccured' (if not NULL) SALENUM_SUCCESS is returned if no error occurred
typedef const char*(WINAPI* SalEnumSelection2)(HWND parent, int enumFiles, const char** dosName,
                                               BOOL* isDir, CQuadWord* size, DWORD* attr,
                                               FILETIME* lastWrite, void* param, int* errorOccured);

//
// ****************************************************************************
// CSalamanderViewAbstract
//
// set of Salamander methods for working with columns in panel (disabling/enabling/adding/setting)

// panel view modes
#define VIEW_MODE_TREE 1
#define VIEW_MODE_BRIEF 2
#define VIEW_MODE_DETAILED 3
#define VIEW_MODE_ICONS 4
#define VIEW_MODE_THUMBNAILS 5
#define VIEW_MODE_TILES 6

#define TRANSFER_BUFFER_MAX 1024 // buffer size for transferring column contents from plugin to Salamander
#define COLUMN_NAME_MAX 30
#define COLUMN_DESCRIPTION_MAX 100

// Column identifiers. Columns inserted by plugin have ID==COLUMN_ID_CUSTOM set.
// Standard Salamander columns have other IDs.
#define COLUMN_ID_CUSTOM 0 // column is provided by plugin - plugin takes care of storing its data
#define COLUMN_ID_NAME 1   // left aligned, supports FixedWidth
// left aligned, supports FixedWidth; separate "Ext" column, can only be at index==1;
// if column does not exist and in panel data (see CSalamanderDirectoryAbstract::SetValidData())
// VALID_DATA_EXTENSION is set, "Ext" column is displayed in "Name" column
#define COLUMN_ID_EXTENSION 2
#define COLUMN_ID_DOSNAME 3     // left aligned
#define COLUMN_ID_SIZE 4        // right aligned
#define COLUMN_ID_TYPE 5        // left aligned, supports FixedWidth
#define COLUMN_ID_DATE 6        // right aligned
#define COLUMN_ID_TIME 7        // right aligned
#define COLUMN_ID_ATTRIBUTES 8  // right aligned
#define COLUMN_ID_DESCRIPTION 9 // left aligned, supports FixedWidth

// Callback for filling buffer with characters to be displayed in corresponding column.
// For optimization reasons function does not receive/return variables through parameters,
// but through global variables (CSalamanderViewAbstract::GetTransferVariables).
typedef void(WINAPI* FColumnGetText)();

// Callback for getting index of simple icons for FS with own icons (pitFromPlugin).
// For optimization reasons function does not receive/return variables through parameters,
// but through global variables (CSalamanderViewAbstract::GetTransferVariables).
// From global variables callback uses only TransferFileData and TransferIsDir.
typedef int(WINAPI* FGetPluginIconIndex)();

// column can be created in two ways:
// 1) Column was created by Salamander based on current view template.
//    In this case 'GetText' pointer (to filling function) points to Salamander
//    and gets texts standardly from CFileData.
//    Value of 'ID' variable is different from COLUMN_ID_CUSTOM.
//
// 2) Column was added by plugin based on its needs.
//    'GetText' points to plugin and 'ID' equals COLUMN_ID_CUSTOM.

struct CColumn
{
    char Name[COLUMN_NAME_MAX]; // "Name", "Ext", "Size", ... column name, under
                                // which column appears in view and in menu
                                // Must not contain empty string.
                                // WARNING: Can contain (after first null-terminator)
                                // also name of "Ext" column - this happens if there is no
                                // separate "Ext" column and in panel data (see
                                // CSalamanderDirectoryAbstract::SetValidData())
                                // VALID_DATA_EXTENSION is set. For joining two
                                // strings use CSalamanderGeneralAbstract::AddStrToStr().

    char Description[COLUMN_DESCRIPTION_MAX]; // Tooltip in header line
                                              // Must not contain empty string.
                                              // WARNING: Can contain (after first null-terminator)
                                              // also description of "Ext" column - this happens if there is no
                                              // separate "Ext" column and in panel data (see
                                              // CSalamanderDirectoryAbstract::SetValidData())
                                              // VALID_DATA_EXTENSION is set. For joining two
                                              // strings use CSalamanderGeneralAbstract::AddStrToStr().

    FColumnGetText GetText; // callback for getting text (description at FColumnGetText type declaration)

    // FIXME_X64 - small for pointer, isn't it sometimes needed?
    DWORD CustomData; // Not used by Salamander; plugin can
                      // use it to distinguish its added columns.

    unsigned SupportSorting : 1; // is column sortable?

    unsigned LeftAlignment : 1; // for TRUE column is left aligned; otherwise right

    unsigned ID : 4; // column identifier
                     // For standard columns provided by Salamander
                     // contains values different from COLUMN_ID_CUSTOM.
                     // For columns added by plugin always contains
                     // value COLUMN_ID_CUSTOM.

    // Variables Width and FixedWidth can be changed by user during work with panel.
    // Standard columns provided by Salamander have ensured storing/loading
    // of these values.
    // Values of these variables for columns provided by plugin need to be stored/loaded
    // within plugin.
    // Columns whose width is calculated by Salamander based on content and user cannot
    // change it, we call 'elastic'. Columns for which user can set width we call
    // 'fixed'.
    unsigned Width : 16;     // Column width in case it is in fixed (adjustable) width mode.
    unsigned FixedWidth : 1; // Is column in fixed (adjustable) width mode?

    // working variables (not stored anywhere and do not need to be initialized)
    // are intended for internal needs of Salamander and plugins ignore them,
    // because their content is not guaranteed when calling plugin
    unsigned MinWidth : 16; // Minimum width to which column can be shrunk.
                            // Is calculated based on column name and its sortability
                            // so that column header is always visible
};

// Plugin through this interface can change display mode
// in panel when path changes. All work with columns concerns only all detailed modes
// (Detailed + Types + three optional modes Alt+8/9/0). When path changes
// plugin gets standard set of columns generated based on template of current
// view. Plugin can modify this set. Modification is not permanent
// and on next path change plugin will receive standard set of columns again. It can thus
// for example remove some of std. columns. Before new filling with std. columns
// plugin gets opportunity to save information about its columns (COLUMN_ID_CUSTOM).
// It can thus save their 'Width' and 'FixedWidth', which user could have
// set in panel (see ColumnFixedWidthShouldChange() and ColumnWidthWasChanged() in interface
// CPluginDataInterfaceAbstract). If plugin changes view mode, change is permanent
// (e.g. switching to Thumbnails mode remains even after leaving plugin path).

class CSalamanderViewAbstract
{
public:
    // -------------- panel ----------------

    // returns mode in which panel is displayed (tree/brief/detailed/icons/thumbnails/tiles)
    // returns one of VIEW_MODE_xxxx values (Detailed mode, Types and three optional modes are
    // all VIEW_MODE_DETAILED)
    virtual DWORD WINAPI GetViewMode() = 0;

    // Sets panel mode to 'viewMode'. If it is one of detailed modes, it can
    // remove some of standard columns (see 'validData'). Therefore it is advisable to call this
    // function as first - before other functions from this interface that modify
    // columns.
    //
    // 'viewMode' is one of VIEW_MODE_xxxx values
    // Panel mode cannot be changed to Types nor to one of three optional detailed modes
    // (all are represented by constant VIEW_MODE_DETAILED used for Detailed panel mode).
    // However if one of these four modes is currently selected in panel and 'viewMode' is
    // VIEW_MODE_DETAILED, this mode remains selected (i.e. does not switch to Detailed mode).
    // Panel mode change is permanent (persists even after leaving plugin path).
    //
    // 'validData' informs about what data plugin wishes to display in detailed mode, value
    // is ANDed with valid data mask specified using CSalamanderDirectoryAbstract::SetValidData
    // (makes no sense to display columns with "zeroed" values).
    virtual void WINAPI SetViewMode(DWORD viewMode, DWORD validData) = 0;

    // Retrieves from Salamander location of variables that replace callback parameters
    // CColumn::GetText. On Salamander side these are global variables. Plugin
    // stores pointers to them in its own global variables.
    //
    // variables:
    //   transferFileData        [IN]     data based on which item should be drawn
    //   transferIsDir           [IN]     equals 0 if it is a file (lies in Files array),
    //                                    equals 1 if it is a directory (lies in Dirs array),
    //                                    equals 2 if it is up-dir symbol
    //   transferBuffer          [OUT]    data is poured here, maximum TRANSFER_BUFFER_MAX characters
    //                                    does not need to be null-terminated
    //   transferLen             [OUT]    before returning from callback this variable is set to
    //                                    number of filled characters without terminator (terminator does not
    //                                    need to be written to buffer)
    //   transferRowData         [IN/OUT] points to DWORD which is always zeroed before drawing columns
    //                                    for each row; can be used for optimizations
    //                                    Salamander has reserved bits 0x00000001 to 0x00000008.
    //                                    Other bits are available for plugin.
    //   transferPluginDataIface [IN]     plugin-data-interface of panel into which item
    //                                    is drawn (belongs to (*transferFileData)->PluginData)
    //   transferActCustomData   [IN]     CustomData of column for which text is being obtained (for which
    //                                    callback is called)
    virtual void WINAPI GetTransferVariables(const CFileData**& transferFileData,
                                             int*& transferIsDir,
                                             char*& transferBuffer,
                                             int*& transferLen,
                                             DWORD*& transferRowData,
                                             CPluginDataInterfaceAbstract**& transferPluginDataIface,
                                             DWORD*& transferActCustomData) = 0;

    // only for FS with own icons (pitFromPlugin):
    // Sets callback for getting index of simple icons (see
    // CPluginDataInterfaceAbstract::GetSimplePluginIcons). If plugin does not set this callback,
    // only icon from index 0 will always be drawn.
    // From global variables callback uses only TransferFileData and TransferIsDir.
    virtual void WINAPI SetPluginSimpleIconCallback(FGetPluginIconIndex callback) = 0;

    // ------------- columns ---------------

    // returns number of columns in panel (always at least one, because name will always be displayed)
    virtual int WINAPI GetColumnsCount() = 0;

    // returns pointer to column (for reading only)
    // 'index' specifies which column will be returned; if column 'index' does not exist, returns NULL
    virtual const CColumn* WINAPI GetColumn(int index) = 0;

    // Inserts column at position 'index'. At position 0 there is always Name column,
    // if Ext column is displayed, it will be at position 1. Otherwise column can be placed
    // arbitrarily. 'column' structure will be copied to internal structures
    // of Salamander. Returns TRUE if column was inserted.
    virtual BOOL WINAPI InsertColumn(int index, const CColumn* column) = 0;

    // Inserts standard column with ID 'id' at position 'index'. At position 0 there is always
    // Name column, if Ext column is being inserted, it must be at position 1.
    // Otherwise column can be placed arbitrarily. 'id' is one of COLUMN_ID_xxxx values,
    // except COLUMN_ID_CUSTOM and COLUMN_ID_NAME.
    virtual BOOL WINAPI InsertStandardColumn(int index, DWORD id) = 0;

    // Sets name and description of column (must not be empty strings or NULL). Lengths
    // of strings are limited to COLUMN_NAME_MAX and COLUMN_DESCRIPTION_MAX. Returns success.
    // WARNING: Name and description of "Name" column can contain (always after first
    // null-terminator) also name and description of "Ext" column - this happens if
    // there is no separate "Ext" column and in panel data (see
    // CSalamanderDirectoryAbstract::SetValidData()) VALID_DATA_EXTENSION is set.
    // In this case it is necessary to set double strings (with two
    // null-terminators) - see CSalamanderGeneralAbstract::AddStrToStr().
    virtual BOOL WINAPI SetColumnName(int index, const char* name, const char* description) = 0;

    // Removes column at position 'index'. Both columns added by plugin can be removed,
    // as well as standard Salamander columns. Cannot remove 'Name' column, which is always
    // at index 0. Beware when removing 'Ext' column, if in plugin data
    // (see CSalamanderDirectoryAbstract::SetValidData()) there is VALID_DATA_EXTENSION,
    // name+description of 'Ext' column must appear in 'Name' column.
    virtual BOOL WINAPI DeleteColumn(int index) = 0;
};

//
// ****************************************************************************
// CPluginDataInterfaceAbstract
//
// set of plugin methods that Salamander needs to get plugin-specific data
// into plugin-added columns (works with CFileData::PluginData)

class CPluginInterfaceAbstract;

class CPluginDataInterfaceAbstract
{
#ifdef INSIDE_SALAMANDER
private: // protection against incorrect direct calling of methods (see CPluginDataInterfaceEncapsulation)
    friend class CPluginDataInterfaceEncapsulation;
#else  // INSIDE_SALAMANDER
public:
#endif // INSIDE_SALAMANDER

    // returns TRUE if ReleasePluginData method should be called for all files bound
    // to this interface, otherwise returns FALSE
    virtual BOOL WINAPI CallReleaseForFiles() = 0;

    // returns TRUE if ReleasePluginData method should be called for all directories bound
    // to this interface, otherwise returns FALSE
    virtual BOOL WINAPI CallReleaseForDirs() = 0;

    // releases plugin-specific data (CFileData::PluginData) for 'file' (file or
    // directory - 'isDir' FALSE or TRUE; structure inserted into CSalamanderDirectoryAbstract
    // during archive or FS listing); called for all files, if CallReleaseForFiles
    // returns TRUE, and for all directories, if CallReleaseForDirs returns TRUE
    virtual void WINAPI ReleasePluginData(CFileData& file, BOOL isDir) = 0;

    // only for archive data (for FS up-dir symbol is not filled):
    // modifies proposed content of up-dir symbol (".." at top of panel); 'archivePath'
    // is path in archive for which symbol is intended; in 'upDir' enter proposed
    // symbol data: name ".." (do not change), date&time of archive, rest zeroed;
    // in 'upDir' exit plugin changes, primarily it should change 'upDir.PluginData',
    // which will be used on up-dir symbol when getting content of added columns;
    // for 'upDir' ReleasePluginData will not be called, any necessary release
    // can be done always at next call of GetFileDataForUpDir or when releasing
    // entire interface (in its destructor - called from
    // CPluginInterfaceAbstract::ReleasePluginDataInterface)
    virtual void WINAPI GetFileDataForUpDir(const char* archivePath, CFileData& upDir) = 0;

    // only for archive data (FS uses only root path in CSalamanderDirectoryAbstract):
    // when adding file/directory to CSalamanderDirectoryAbstract it can happen that
    // specified path does not exist and it is therefore necessary to create it, individual directories of this
    // path are created automatically and this method allows plugin to add its specific
    // data (for its columns) to these created directories; 'dirName' is full path
    // of added directory in archive; in 'dir' enter proposed data: directory name
    // (allocated on Salamander heap), date&time taken from added file/directory,
    // rest zeroed; in 'dir' exit plugin changes, primarily it should change
    // 'dir.PluginData'; returns TRUE if adding plugin data succeeded, otherwise FALSE;
    // if returns TRUE, 'dir' will be released by standard way (Salamander part +
    // ReleasePluginData) either when completely releasing listing or even during
    // its creation in case the same directory is added using
    // CSalamanderDirectoryAbstract::AddDir (overwriting automatic creation by later
    // normal addition); if returns FALSE, only Salamander part will be released from 'dir'
    virtual BOOL WINAPI GetFileDataForNewDir(const char* dirName, CFileData& dir) = 0;

    // only for FS with own icons (pitFromPlugin):
    // returns image-list with simple icons, during drawing of items in panel
    // icon-index into this image-list is obtained using callback; called always after
    // obtaining new listing (after calling CPluginFSInterfaceAbstract::ListCurrentPath),
    // so it is possible to rebuild image-list for each new listing;
    // 'iconSize' specifies required icon size and is one of SALICONSIZE_xxx values
    // destruction of image-list is handled by plugin at next call of GetSimplePluginIcons
    // or when releasing entire interface (in its destructor - called from
    // CPluginInterfaceAbstract::ReleasePluginDataInterface)
    // if image-list cannot be created, returns NULL and current plugin-icons-type
    // degrades to pitSimple
    virtual HIMAGELIST WINAPI GetSimplePluginIcons(int iconSize) = 0;

    // only for FS with own icons (pitFromPlugin):
    // returns TRUE, if for given file/directory ('isDir' FALSE/TRUE) 'file'
    // simple icon should be used; returns FALSE, if for obtaining icon method
    // GetPluginIcon should be called from thread for loading icons (loading icon "in background");
    // at the same time in this method icon-index for simple icon can be precalculated
    // (for icons read "in background", simple icons are also used until the moment of loading)
    // and stored in CFileData (most likely in CFileData::PluginData);
    // limitation: from CSalamanderGeneralAbstract only methods can be used that can be
    // called from any thread (methods independent of panel state)
    virtual BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL isDir) = 0;

    // only for FS with own icons (pitFromPlugin):
    // returns icon for file or directory 'file' or NULL if icon cannot be obtained; if returns
    // TRUE in 'destroyIcon', Win32 API function DestroyIcon is called to release returned icon;
    // 'iconSize' specifies size of requested icon and is one of SALICONSIZE_xxx values
    // limitation: since it is called from thread for loading icons (not main thread), from
    // CSalamanderGeneralAbstract only methods can be used that can be called from any thread
    virtual HICON WINAPI GetPluginIcon(const CFileData* file, int iconSize, BOOL& destroyIcon) = 0;

    // only for FS with own icons (pitFromPlugin):
    // compares 'file1' (can be file or directory) and 'file2' (can be file or directory),
    // must not return that any two items in listing are identical (ensures unique
    // assignment of own icon to file/directory); if duplicate names in path listing
    // are not a concern (common case), can be simply implemented as:
    // {return strcmp(file1->Name, file2->Name);}
    // returns number less than zero if 'file1' < 'file2', zero if 'file1' == 'file2' and
    // number greater than zero if 'file1' > 'file2';
    // limitation: since it is called also from thread for loading icons (not only from main thread),
    // from CSalamanderGeneralAbstract only methods can be used that can be called from any thread
    virtual int WINAPI CompareFilesFromFS(const CFileData* file1, const CFileData* file2) = 0;

    // serves to set view parameters, this method is called always before displaying new
    // panel content (when path changes) and when current view changes (including manual change of column
    // width); 'leftPanel' is TRUE if it concerns left panel (FALSE if it concerns right panel);
    // 'view' is interface for modifying view (setting mode, working with
    // columns); if it concerns archive data, 'archivePath' contains current path in archive,
    // for FS data 'archivePath' is NULL; if it concerns archive data, 'upperDir' is pointer to
    // parent directory (if current path is archive root, 'upperDir' is NULL), for FS
    // data it is always NULL;
    // WARNING: during calling this method panel must not be repainted (icon size can change
    //          here, etc.), so no messageloops (no dialogs, etc.)!
    // limitation: from CSalamanderGeneralAbstract only methods can be used that can be
    //             called from any thread (methods independent of panel state)
    virtual void WINAPI SetupView(BOOL leftPanel, CSalamanderViewAbstract* view,
                                  const char* archivePath, const CFileData* upperDir) = 0;

    // setting new value of "column->FixedWidth" - user used context menu
    // on plugin-added column in header-line > "Automatic Column Width"; plugin
    // should save new value of column->FixedWidth stored in 'newFixedWidth'
    // (it is always negation of column->FixedWidth), so that in following calls of SetupView() it can
    // add column already with correctly set FixedWidth; at the same time if fixed
    // column width is being enabled, plugin should set current value of "column->Width" (so that
    // this enabling of fixed width does not change column width) - ideally call
    // "ColumnWidthWasChanged(leftPanel, column, column->Width)"; 'column' identifies
    // column that should be changed; 'leftPanel' is TRUE if it concerns column from left
    // panel (FALSE if it concerns column from right panel)
    virtual void WINAPI ColumnFixedWidthShouldChange(BOOL leftPanel, const CColumn* column,
                                                     int newFixedWidth) = 0;

    // setting new value of "column->Width" - user changed width of plugin-added column
    // in header-line with mouse; plugin should save new value of column->Width (it is also stored
    // in 'newWidth'), so that in following calls of SetupView() it can add column already with
    // correctly set Width; 'column' identifies column that was changed; 'leftPanel'
    // is TRUE if it concerns column from left panel (FALSE if it concerns column from right panel)
    virtual void WINAPI ColumnWidthWasChanged(BOOL leftPanel, const CColumn* column,
                                              int newWidth) = 0;

    // obtains content of Information Line for file/directory ('isDir' TRUE/FALSE) 'file'
    // or selected files and directories ('file' is NULL and counts of selected files/directories
    // are in 'selectedFiles'/'selectedDirs') in panel ('panel' is one of PANEL_XXX);
    // called also with empty listing (concerns only FS, for archives cannot occur, 'file' is NULL,
    // 'selectedFiles' and 'selectedDirs' are 0); if 'displaySize' is TRUE, size is known for
    // all selected directories (see CFileData::SizeValid; if nothing is selected, it is
    // TRUE here); in 'selectedSize' is sum of CFileData::Size numbers of selected files and directories
    // (if nothing is selected, there is zero here); 'buffer' is buffer for returned text (size
    // 1000 bytes); 'hotTexts' is array (size 100 DWORDs), in which information about position
    // of hot-text is returned, always lower WORD contains position of hot-text in 'buffer', upper WORD contains
    // length of hot-text; in 'hotTextsCount' is size of array 'hotTexts' (100) and number of
    // written hot-texts in array 'hotTexts' is returned in it; returns TRUE if 'buffer' + 'hotTexts' +
    // 'hotTextsCount' is set, returns FALSE if Information Line should be filled in standard
    // way (as on disk)
    virtual BOOL WINAPI GetInfoLineContent(int panel, const CFileData* file, BOOL isDir, int selectedFiles,
                                           int selectedDirs, BOOL displaySize, const CQuadWord& selectedSize,
                                           char* buffer, DWORD* hotTexts, int& hotTextsCount) = 0;

    // only for archives: user saved files/directories from archive to clipboard, now closing
    // archive in panel: if method returns TRUE, this object remains open (optimization
    // of potential Paste from clipboard - archive is already listed), if method returns FALSE,
    // this object is released (potential Paste from clipboard will cause listing of archive, then
    // only extraction of selected files/directories will occur); NOTE: if during lifetime
    // of object archive file is open, method should return FALSE, otherwise during entire
    // "stay" of data on clipboard archive file will be open (cannot be deleted, etc.)
    virtual BOOL WINAPI CanBeCopiedToClipboard() = 0;

    // only when VALID_DATA_PL_SIZE is specified in CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if size of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns size in 'size'
    virtual BOOL WINAPI GetByteSize(const CFileData* file, BOOL isDir, CQuadWord* size) = 0;

    // only when VALID_DATA_PL_DATE is specified in CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if date of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns date in "date" part of structure 'date' ("time" part
    // should remain untouched)
    virtual BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL isDir, SYSTEMTIME* date) = 0;

    // only when VALID_DATA_PL_TIME is specified in CSalamanderDirectoryAbstract::SetValidData():
    // returns TRUE if time of file/directory ('isDir' TRUE/FALSE) 'file' is known,
    // otherwise returns FALSE; returns time in "time" part of structure 'time' ("date" part
    // should remain untouched)
    virtual BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL isDir, SYSTEMTIME* time) = 0;
};

//
// ****************************************************************************
// CSalamanderForOperationsAbstract
//
// set of methods from Salamander for supporting execution of operations, validity of interface is
// limited to method to which interface is passed as parameter; thus can be called only
// from this thread and in this method (object is on stack, so it ceases to exist after return)

class CSalamanderForOperationsAbstract
{
public:
    // PROGRESS DIALOG: dialog contains one/two ('twoProgressBars' FALSE/TRUE) progress-bars
    // opens progress-dialog with title 'title'; 'parent' is parent window of progress-dialog (if
    // NULL, main window is used); if contains only one progress-bar, it can be described
    // as "File" ('fileProgress' is TRUE) or "Total" ('fileProgress' is FALSE)
    //
    // dialog does not run in its own thread; for its functioning (Cancel button + internal timer)
    // it is necessary to occasionally empty message queue; this is ensured by methods ProgressDialogAddText,
    // ProgressAddSize and ProgressSetSize
    //
    // because real-time displaying of text and changes in progress bar strongly delays, methods
    // ProgressDialogAddText, ProgressAddSize and ProgressSetSize have parameter
    // 'delayedPaint'; it should be TRUE for all rapidly changing texts and values;
    // methods then save texts and display them only after delivery of internal timer of dialog;
    // we set 'delayedPaint' to FALSE for initialization/final texts of type "preparing data..."
    // or "canceling operation...", after whose display we do not give dialog opportunity to distribute
    // messages (timer); if it is probable that such operation will take long time, we should
    // during this time "refresh" dialog by calling ProgressAddSize(CQuadWord(0, 0), TRUE)
    // and according to its return value possibly terminate action prematurely
    virtual void WINAPI OpenProgressDialog(const char* title, BOOL twoProgressBars,
                                           HWND parent, BOOL fileProgress) = 0;
    // writes text 'txt' (even multiple lines - line breaking is performed) to progress-dialog
    virtual void WINAPI ProgressDialogAddText(const char* txt, BOOL delayedPaint) = 0;
    // if 'totalSize1' is not CQuadWord(-1, -1), sets 'totalSize1' as 100 percent of first progress-bar,
    // if 'totalSize2' is not CQuadWord(-1, -1), sets 'totalSize2' as 100 percent of second progress-bar
    // (for progress-dialog with one progress-bar, 'totalSize2' CQuadWord(-1, -1) is mandatory)
    virtual void WINAPI ProgressSetTotalSize(const CQuadWord& totalSize1, const CQuadWord& totalSize2) = 0;
    // if 'size1' is not CQuadWord(-1, -1), sets size 'size1' (size1/total1*100 percent) on first progress-bar,
    // if 'size2' is not CQuadWord(-1, -1), sets size 'size2' (size2/total2*100 percent) on second progress-bar
    // (for progress-dialog with one progress-bar, 'size2' CQuadWord(-1, -1) is mandatory), returns information whether
    // action should continue (FALSE = end)
    virtual BOOL WINAPI ProgressSetSize(const CQuadWord& size1, const CQuadWord& size2, BOOL delayedPaint) = 0;
    // adds (possibly to both progress-bars) size 'size' (size/total*100 percent of progress),
    // returns information whether action should continue (FALSE = end)
    virtual BOOL WINAPI ProgressAddSize(int size, BOOL delayedPaint) = 0;
    // enables/disables Cancel button
    virtual void WINAPI ProgressEnableCancel(BOOL enable) = 0;
    // returns HWND of progress dialog (useful when displaying errors and queries when progress-dialog is open)
    virtual HWND WINAPI ProgressGetHWND() = 0;
    // closes progress-dialog
    virtual void WINAPI CloseProgressDialog() = 0;

    // moves all files from 'source' directory to 'target' directory,
    // additionally remaps prefixes of displayed names ('remapNameFrom' -> 'remapNameTo')
    // returns success of operation
    virtual BOOL WINAPI MoveFiles(const char* source, const char* target, const char* remapNameFrom,
                                  const char* remapNameTo) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_com)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
