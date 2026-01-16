// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: FULLY_TRANSLATED

//****************************************************************************
//
// Copyright (c) 2023 Open Salamander Authors
//
// This is a part of the Open Salamander SDK library.
//
//****************************************************************************

#pragma once

#ifdef _MSC_VER
#pragma pack(push, enter_include_spl_file) // so that structures are independent of the set alignment
#pragma pack(4)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a4
#endif // __BORLANDC__

//****************************************************************************
//
// CSalamanderSafeFileAbstract
//
// The SafeFile family of methods is used for protected file operations. The methods check
// error states of API calls and display corresponding error messages. Error messages
// can contain various combinations of buttons. From OK, through Retry/Cancel, up to
// Retry/Skip/Skip all/Cancel. The button combination is determined by the calling function
// with one of the parameters.
//
// The methods need to know the file name during problem state resolution, so they can
// display a solid error message. They also need to know the parameters of the opened
// file (such as dwDesiredAccess, dwShareMode, etc.), so that in case of an error they can
// close the handle and reopen it. If, for example, there is an interruption at the
// network layer level during a ReadFile or WriteFile operation and the user removes the cause
// of the problem and presses Retry, the old file handle cannot be reused. It is necessary
// to close the old handle, reopen the file, set the pointer and repeat the operation.
// Therefore CAUTION: the SafeFileRead and SafeFileWrite methods may change the
// SAFE_FILE::HFile value when resolving error states.
//
// For the reasons described, a classic HANDLE was not sufficient to hold the context and is replaced
// by the SAFE_FILE structure. In the case of the SafeFileOpen method, this is a necessary parameter,
// while for SafeFileCreate methods this parameter is only [optional]. This is due to
// the need to maintain compatible behavior of the SafeFileCreate method for older plugins.
//
// Methods supporting Skip all/Overwrite all buttons have a 'silentMask' parameter.
// This is a pointer to a bit field composed of SILENT_SKIP_xxx and SILENT_OVERWRITE_xxx.
// If the pointer is not NULL, the bit field serves two functions:
// (1) input: if the corresponding bit is set, the method does not display
//            an error message in case of an error and silently responds without user interaction.
// (2) output: If the user responds to a query in case of an error with Skip all
//             or Overwrite all button, the method sets the corresponding bit in the bit field.
// This bit field serves as context passed to individual methods. For one
// logical group of operations (for example, unpacking multiple files from an archive), the
// caller passes the same bit field, which it initializes to 0 at the beginning.
// It can also explicitly set some bits in the bit field to suppress
// corresponding queries.
// Salamander reserves part of the bit field for internal plugin states.
// These are the one bits in SILENT_RESERVED_FOR_PLUGINS.
//
// Unless otherwise specified for pointers passed to the interface method,
// they must not be NULL.
//

struct SAFE_FILE
{
    HANDLE HFile;                // handle of the opened file (caution, it is under Salamander core HANDLES)
    char* FileName;              // name of the opened file with full path
    HWND HParentWnd;             // window handle hParent from SafeFileOpen/SafeFileCreate call; used
                                 // if hParent in subsequent calls is set to HWND_STORED
    DWORD dwDesiredAccess;       // > backup of parameters for API CreateFile
    DWORD dwShareMode;           // > for its possible repeated call
    DWORD dwCreationDisposition; // > in case of errors during reading or writing
    DWORD dwFlagsAndAttributes;  // >
    BOOL WholeFileAllocated;     // TRUE if SafeFileCreate function pre-allocated the entire file
};

#define HWND_STORED ((HWND) - 1)

#define SAFE_FILE_CHECK_SIZE 0x00010000 // FIXME: verify that it doesn't conflict with BUTTONS_xxx

// silentMask mask bits
// skip section
#define SILENT_SKIP_FILE_NAMEUSED 0x00000001 // skips files that cannot be created because \
                                             // a directory with the same name already exists (old CNFRM_MASK_NAMEUSED)
#define SILENT_SKIP_DIR_NAMEUSED 0x00000002  // skips directories that cannot be created because \
                                             // a file with the same name already exists (old CNFRM_MASK_NAMEUSED)
#define SILENT_SKIP_FILE_CREATE 0x00000004   // skips files that cannot be created for another reason (old CNFRM_MASK_ERRCREATEFILE)
#define SILENT_SKIP_DIR_CREATE 0x00000008    // skips directories that cannot be created for another reason (old CNFRM_MASK_ERRCREATEDIR)
#define SILENT_SKIP_FILE_EXIST 0x00000010    // skips files that already exist (old CNFRM_MASK_FILEOVERSKIP) \
                                             // mutually exclusive with SILENT_OVERWRITE_FILE_EXIST
#define SILENT_SKIP_FILE_SYSHID 0x00000020   // skips System/Hidden files that already exist (old CNFRM_MASK_SHFILEOVERSKIP) \
                                             // mutually exclusive with SILENT_OVERWRITE_FILE_SYSHID
#define SILENT_SKIP_FILE_READ 0x00000040     // skips files whose reading resulted in an error
#define SILENT_SKIP_FILE_WRITE 0x00000080    // skips files whose writing resulted in an error
#define SILENT_SKIP_FILE_OPEN 0x00000100     // skips files that cannot be opened

// overwrite section
#define SILENT_OVERWRITE_FILE_EXIST 0x00001000  // overwrites files that already exist (old CNFRM_MASK_FILEOVERYES) \
                                                // mutually exclusive with SILENT_SKIP_FILE_EXIST
#define SILENT_OVERWRITE_FILE_SYSHID 0x00002000 // overwrites System/Hidden files that already exist (old CNFRM_MASK_SHFILEOVERYES) \
                                                // mutually exclusive with SILENT_SKIP_FILE_SYSHID
#define SILENT_RESERVED_FOR_PLUGINS 0xFFFF0000  // this space is available to plugins for their own flags

class CSalamanderSafeFileAbstract
{
public:
    //
    // SafeFileOpen
    //   Opens an existing file.
    //
    // Parameters
    //   'file'
    //      [out] Pointer to the 'SAFE_FILE' structure that will receive information about the opened
    //      file. This structure serves as context for other methods from the
    //      SafeFile family. Structure values have meaning only if SafeFileOpen
    //      returned TRUE. To close the file, SafeFileClose method must be called.
    //
    //   'fileName'
    //      [in] Pointer to zero-terminated string that contains name of file being opened.
    //
    //   'dwDesiredAccess'
    //   'dwShareMode'
    //   'dwCreationDisposition'
    //   'dwFlagsAndAttributes'
    //      [in] see API CreateFile.
    //
    //   'hParent'
    //      [in] Handle of the window to which error messages will be displayed modally.
    //
    //   'flags'
    //      [in] One of BUTTONS_xxx values, determines buttons displayed in error messages.
    //
    //   'pressedButton'
    //      [out] Pointer to variable that will receive the button pressed during the error
    //      message. Variable has meaning only if SafeFileOpen method returns FALSE,
    //      otherwise its value is undefined. Returns one of DIALOG_xxx values.
    //      In case of errors returns DIALOG_CANCEL value.
    //      If an error message is ignored due to 'silentMask', returns the value
    //      of the corresponding button (for example DIALOG_SKIP or DIALOG_YES).
    //
    //      'pressedButton' may be NULL (for example for BUTTONS_OK or BUTTONS_RETRYCANCEL
    //      there is no point in testing the pressed button).
    //
    //   'silentMask'
    //      [in/out] Pointer to variable containing bit field of SILENT_xxx values.
    //      For the SafeFileOpen method, only SILENT_SKIP_FILE_OPEN value has meaning.
    //
    //      If the SILENT_SKIP_FILE_OPEN bit is set in the bit field, and the
    //      displayed message should have Skip button (controlled by 'flags' parameter), and
    //      an error occurs during file opening, the error message will be suppressed.
    //      SafeFileOpen will then return FALSE and if 'pressedButton' is different from NULL,
    //      sets it to DIALOG_SKIP value.
    //
    // Return Values
    //   Returns TRUE if the file was successfully opened. The 'file' structure is initialized
    //   and SafeFileClose must be called to close the file.
    //
    //   In case of error returns FALSE and sets values of 'pressedButton'
    //   and 'silentMask' variables, if they are different from NULL.
    //
    // Remarks
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileOpen(SAFE_FILE* file,
                                     const char* fileName,
                                     DWORD dwDesiredAccess,
                                     DWORD dwShareMode,
                                     DWORD dwCreationDisposition,
                                     DWORD dwFlagsAndAttributes,
                                     HWND hParent,
                                     DWORD flags,
                                     DWORD* pressedButton,
                                     DWORD* silentMask) = 0;

    //
    // SafeFileCreate
    //   Creates a new file including path, if it does not already exist. If file already exists,
    //   offers to overwrite it. The method is primarily intended for creating files and directories
    //   extracted from archives.
    //
    // Parameters
    //   'fileName'
    //      [in] Pointer to zero-terminated string that specifies the name
    //      of the file being created.
    //
    //   'dwDesiredAccess'
    //   'dwShareMode'
    //   'dwFlagsAndAttributes'
    //      [in] see API CreateFile.
    //
    //   'isDir'
    //      [in] Determines whether the last component of 'fileName' path should be a directory (TRUE)
    //      or a file (FALSE). If 'isDir' is TRUE, variables
    //      'dwDesiredAccess', 'dwShareMode', 'dwFlagsAndAttributes', 'srcFileName',
    //      'srcFileInfo' and 'file' will be ignored.
    //
    //   'hParent'
    //      [in] Handle of the window to which error messages will be displayed modally.
    //
    //   'srcFileName'
    //      [in] Pointer to zero-terminated string that specifies the name
    //      of the source file. This name will be displayed together with size
    //      and time ('srcFileInfo') in the prompt to overwrite existing file,
    //      if file 'fileName' already exists.
    //      'srcFileName' may be NULL, then 'srcFileInfo' is ignored.
    //      In this case, any overwrite prompt will contain the text
    //      "a newly created file" in place of the source file.
    //
    //   'srcFileInfo'
    //      [in] Pointer to zero-terminated string that contains size, date
    //      and time of the source file. This information will be displayed together with
    //      the source file name 'srcFileName' in the prompt to overwrite existing file.
    //      Format: "size, date, time".
    //      Size is obtained using CSalamanderGeneralAbstract::NumberToStr,
    //      date using GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, ...
    //      and time using GetTimeFormat(LOCALE_USER_DEFAULT, 0, ...
    //      See implementation of GetFileInfo method in UnFAT plugin.
    //      'srcFileInfo' may be NULL, if 'srcFileName' is also NULL.
    //
    //    'silentMask'
    //      [in/out] Pointer to bit field composed of SILENT_SKIP_xxx and SILENT_OVERWRITE_xxx,
    //      see introduction at the beginning of this file. If 'silentMask' is NULL, it will be ignored.
    //      The SafeFileCreate method tests and sets these constants:
    //        SILENT_SKIP_FILE_NAMEUSED
    //        SILENT_SKIP_DIR_NAMEUSED
    //        SILENT_OVERWRITE_FILE_EXIST
    //        SILENT_SKIP_FILE_EXIST
    //        SILENT_OVERWRITE_FILE_SYSHID
    //        SILENT_SKIP_FILE_SYSHID
    //        SILENT_SKIP_DIR_CREATE
    //        SILENT_SKIP_FILE_CREATE
    //
    //      If 'srcFileName' is different from NULL, i.e., it is a COPY/MOVE operation, the following applies:
    //        If the option "Confirm on file overwrite" is disabled in Salamander's configuration (Confirmations page),
    //        the method behaves as if 'silentMask' contained SILENT_OVERWRITE_FILE_EXIST.
    //        If "Confirm on system or hidden file overwrite" is disabled, the method behaves
    //        as if 'silentMask' contained SILENT_OVERWRITE_FILE_SYSHID.
    //
    //    'allowSkip'
    //      [in] Specifies whether prompts and error messages will also contain "Skip"
    //      and "Skip all" buttons
    //
    //    'skipped'
    //      [out] Returns TRUE if user clicked on "Skip" or "Skip all" button
    //      in a prompt or error message. Otherwise returns FALSE. Variable 'skipped' may be NULL.
    //      Variable has meaning only if SafeFileCreate returns INVALID_HANDLE_VALUE.
    //
    //    'skipPath'
    //      [out] Pointer to buffer that will receive the path that user wanted to skip
    //      in one of the prompts using "Skip" or "Skip all" button. Buffer size is
    //      determined by skipPathMax variable, which will not be exceeded. Path will be zero-terminated.
    //      At the beginning of SafeFileCreate method, an empty string is set in the buffer.
    //      'skipPath' may be NULL, 'skipPathMax' is then ignored.
    //
    //    'skipPathMax'
    //      [in] Size of 'skipPath' buffer in characters. Must be set if 'skipPath'
    //      is different from NULL.
    //
    //    'allocateWholeFile'
    //      [in/out] Pointer to CQuadWord specifying the size to which the file should be
    //      pre-allocated using SetEndOfFile function. If the pointer is NULL, it will be ignored
    //      and SafeFileCreate will not attempt pre-allocation. If the pointer is different from
    //      NULL, the function will attempt pre-allocation. Requested size must be greater than
    //      CQuadWord(2, 0) and less than CQuadWord(0, 0x80000000) (8EB).
    //
    //      If SafeFileCreate should also perform a test (pre-allocation mechanism may not always
    //      work), the highest bit of size must be set, i.e., CQuadWord(0, 0x80000000) added to the value.
    //
    //      If file is successfully created (SafeFileCreate function returns handle different from
    //      INVALID_HANDLE_VALUE), variable 'allocateWholeFile' will be set to one of
    //      the following values:
    //       CQuadWord(0, 0x80000000): file pre-allocation failed and during next
    //                                 SafeFileCreate call for files to the same destination
    //                                 'allocateWholeFile' should be NULL
    //       CQuadWord(0, 0):          file pre-allocation failed, but it is not
    //                                 fatal and on next SafeFileCreate call for
    //                                 files with this destination you can specify their pre-allocation
    //       other:                    pre-allocation completed correctly
    //                                 In this case SAFE_FILE::WholeFileAllocated is set
    //                                 to TRUE and during SafeFileClose, SetEndOfFile will be called to
    //                                 truncate the file and prevent saving unnecessary data.
    //
    //    'file'
    //      [out] Pointer to 'SAFE_FILE' structure that will receive information about the opened
    //      file. This structure serves as context for other methods from the
    //      SafeFile. Structure values have meaning only if SafeFileCreate
    //      returned value different from INVALID_HANDLE_VALUE. To close the file,
    //      SafeFileClose method must be called. If 'file' is different from NULL,
    //      SafeFileCreate adds created handle to Salamander's HANDLES. If 'file' is NULL,
    //      handle will not be added to HANDLES. If 'isDir' is TRUE, variable 'file'
    //      is ignored.
    //
    // Return Values
    //   If 'isDir' is TRUE, returns value different from INVALID_HANDLE_VALUE on success.
    //   Note, this is not a valid handle of created directory. On failure returns
    //   INVALID_HANDLE_VALUE and sets variables 'silentMask', 'skipped' and 'skipPath'.
    //
    //   If 'isDir' is FALSE, returns handle of created file on success and if
    //   'file' is different from NULL, fills SAFE_FILE structure.
    //   On failure returns INVALID_HANDLE_VALUE and sets variables 'silentMask',
    //   'skipped' and 'skipPath'.
    //
    // Remarks
    //   The method can only be called from the main thread. (may call API FlashWindow(MainWindow),
    //   which must be called from the window thread, otherwise it causes deadlock)
    //
    virtual HANDLE WINAPI SafeFileCreate(const char* fileName,
                                         DWORD dwDesiredAccess,
                                         DWORD dwShareMode,
                                         DWORD dwFlagsAndAttributes,
                                         BOOL isDir,
                                         HWND hParent,
                                         const char* srcFileName,
                                         const char* srcFileInfo,
                                         DWORD* silentMask,
                                         BOOL allowSkip,
                                         BOOL* skipped,
                                         char* skipPath,
                                         int skipPathMax,
                                         CQuadWord* allocateWholeFile,
                                         SAFE_FILE* file) = 0;

    //
    // SafeFileClose
    //   Closes file and releases allocated data in 'file' structure.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to 'SAFE_FILE' structure that was initialized by successful
    //      call to SafeFileCreate or SafeFileOpen method.
    //
    // Remarks
    //   The method can be called from any thread.
    //
    virtual void WINAPI SafeFileClose(SAFE_FILE* file) = 0;

    //
    // SafeFileSeek
    //   Sets file pointer in an open file.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to 'SAFE_FILE' structure that was initialized
    //      by call to SafeFileOpen or SafeFileCreate method.
    //
    //   'distance'
    //      [in/out] Number of bytes by which to move the file pointer.
    //      On success receives the value of new pointer position.
    //
    //      CQuadWord::Value is interpreted as signed for
    //      all three 'moveMethod' values (beware of error in MSDN for SetFilePointerEx,
    //      which claims value is unsigned for FILE_BEGIN). So if we want to
    //      move backwards from current position (FILE_CURRENT) or from end (FILE_END) of file,
    //      we set CQuadWord::Value to a negative number. The CQuadWord::Value variable
    //      can be directly assigned e.g., __int64.
    //
    //      Returned value is absolute position from beginning of file and its values will be
    //      from 0 to 2^63. Files above 2^63 are not supported by any current Windows.
    //
    //   'moveMethod'
    //      [in] Starting position for pointer. Can be one of values:
    //           FILE_BEGIN, FILE_CURRENT or FILE_END.
    //
    //   'error'
    //      [out] Pointer to DWORD variable that will contain the value
    //      returned from GetLastError() in case of error. 'error' may be NULL.
    //
    // Return Values
    //   On success returns TRUE and 'distance' variable value is set
    //   to new file pointer position.
    //
    //   On error returns FALSE and sets 'error' value to GetLastError,
    //   if 'error' is different from NULL. Does not display error, SafeFileSeekMsg is for that.
    //
    // Remarks
    //   Method calls API SetFilePointer, so limitations of this function apply.
    //
    //   It is not an error to set pointer beyond end of file. File size does not
    //   increase until you call SetEndOfFile or SafeFileWrite. See API SetFilePointer.
    //
    //   Method can be used to get file size, if we set 'distance'
    //   value to 0 and 'moveMethod' to FILE_END. Returned 'distance' value will be
    //   file size.
    //
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileSeek(SAFE_FILE* file,
                                     CQuadWord* distance,
                                     DWORD moveMethod,
                                     DWORD* error) = 0;

    //
    // SafeFileSeekMsg
    //   Sets file pointer in an open file. If error occurs, displays it.
    //
    // Parameters
    //   'file'
    //   'distance'
    //   'moveMethod'
    //      See comment at SafeFileSeek.
    //
    //   'hParent'
    //      [in] Handle of window to which error messages will be displayed modally.
    //      If equal to HWND_STORED, 'hParent' from SafeFileOpen/SafeFileCreate call is used.
    //
    //   'flags'
    //      [in] One of BUTTONS_xxx values, determines buttons displayed in error message.
    //
    //   'pressedButton'
    //      [out] Pointer to variable that will receive the pressed button during error
    //      message. Variable has meaning only if SafeFileSeekMsg method returns FALSE.
    //      'pressedButton' may be NULL (for example for BUTTONS_OK there is no point testing
    //      pressed button)
    //
    //   'silentMask'
    //      [in/out] Pointer to variable containing bit field of SILENT_SKIP_xxx values.
    //      For details see comment at SafeFileOpen.
    //      SafeFileSeekMsg tests and sets SILENT_SKIP_FILE_READ bit if
    //      'seekForRead' is TRUE or SILENT_SKIP_FILE_WRITE, if 'seekForRead' is FALSE;
    //
    //   'seekForRead'
    //      [in] Tells method for what purpose we performed seek in file. Method uses
    //      this variable only in case of error. Determines which bit will be used for
    //      'silentMask' and what will be the error message title: "Error Reading File" or
    //      "Error Writing File".
    //
    // Return Values
    //   On success returns TRUE and 'distance' variable value is set
    //   to new file pointer position.
    //
    //   On error returns FALSE and sets values of 'pressedButton'
    //   and silentMask variables, if they are different from NULL.
    //
    // Remarks
    //   See SafeFileSeek method.
    //
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileSeekMsg(SAFE_FILE* file,
                                        CQuadWord* distance,
                                        DWORD moveMethod,
                                        HWND hParent,
                                        DWORD flags,
                                        DWORD* pressedButton,
                                        DWORD* silentMask,
                                        BOOL seekForRead) = 0;

    //
    // SafeFileGetSize
    //   Returns file size.
    //
    //   'file'
    //      [in] Pointer to 'SAFE_FILE' structure that was initialized
    //      by call to SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [out] Pointer to CQuadWord structure that will receive file size.
    //
    //   'error'
    //      [out] Pointer to DWORD variable that will contain the value
    //      returned from GetLastError() in case of error. 'error' may be NULL.
    //
    // Return Values
    //   On success returns TRUE and sets 'fileSize' variable.
    //   On error returns FALSE and sets 'error' variable value, if it is different from NULL.
    //
    // Remarks
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileGetSize(SAFE_FILE* file,
                                        CQuadWord* fileSize,
                                        DWORD* error) = 0;

    //
    // SafeFileRead
    //   Reads data from file starting at pointer position. After operation completes, pointer is
    //   moved by number of bytes read. Method supports only synchronous reading, i.e., does not return
    //   until data is read or error occurs.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to 'SAFE_FILE' structure that was initialized
    //      by call to SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [out] Pointer to buffer that will receive data read from file.
    //
    //   'nNumberOfBytesToRead'
    //      [in] Specifies how many bytes should be read from file.
    //
    //   'lpNumberOfBytesRead'
    //      [out] Points to variable that will receive number of bytes actually read into buffer.
    //
    //   'hParent'
    //      [in] Handle of window to which error messages will be displayed modally.
    //      If equal to HWND_STORED, 'hParent' from SafeFileOpen/SafeFileCreate call is used.
    //
    //   'flags'
    //      [in] One of BUTTONS_xxx values optionally with SAFE_FILE_CHECK_SIZE, determines buttons
    //      displayed in error messages. If SAFE_FILE_CHECK_SIZE bit is set, SafeFileRead method
    //      considers it an error if it fails to read requested number of bytes and displays error
    //      message. Without this bit behaves the same as API ReadFile.
    //
    //   'pressedButton'
    //   'silentMask'
    //      See SafeFileOpen.
    //
    // Return Values
    //   On success returns TRUE and 'lpNumberOfBytesRead' variable value is set
    //   to number of bytes read.
    //
    //   On error returns FALSE and sets values of 'pressedButton' and 'silentMask' variables,
    //   if they are different from NULL.
    //
    // Remarks
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileRead(SAFE_FILE* file,
                                     LPVOID lpBuffer,
                                     DWORD nNumberOfBytesToRead,
                                     LPDWORD lpNumberOfBytesRead,
                                     HWND hParent,
                                     DWORD flags,
                                     DWORD* pressedButton,
                                     DWORD* silentMask) = 0;

    //
    // SafeFileWrite
    //   Writes data to file starting from pointer position. After operation completes, pointer is
    //   moved by number of bytes written. Method supports only synchronous writing, i.e., does not return
    //   until data is written or error occurs.
    //
    // Parameters
    //   'file'
    //      [in] Pointer to 'SAFE_FILE' structure that was initialized
    //      by call to SafeFileOpen or SafeFileCreate method.
    //
    //   'lpBuffer'
    //      [in] Pointer to buffer containing data to be written to file.
    //
    //   'nNumberOfBytesToWrite'
    //      [in] Specifies how many bytes should be written from buffer to file.
    //
    //   'lpNumberOfBytesWritten'
    //      [out] Points to variable that will receive number of bytes actually written.
    //
    //   'hParent'
    //      [in] Handle of window to which error messages will be displayed modally.
    //      If equal to HWND_STORED, 'hParent' from SafeFileOpen/SafeFileCreate call is used.
    //
    //   'flags'
    //      [in] One of BUTTONS_xxx values, determines buttons displayed in error messages.
    //
    //   'pressedButton'
    //   'silentMask'
    //      See SafeFileOpen.
    //
    // Return Values
    //   On success returns TRUE and 'lpNumberOfBytesWritten' variable value is set
    //   to number of bytes written.
    //
    //   On error returns FALSE and sets values of 'pressedButton' and 'silentMask' variables,
    //   if they are different from NULL.
    //
    // Remarks
    //   The method can be called from any thread.
    //
    virtual BOOL WINAPI SafeFileWrite(SAFE_FILE* file,
                                      LPVOID lpBuffer,
                                      DWORD nNumberOfBytesToWrite,
                                      LPDWORD lpNumberOfBytesWritten,
                                      HWND hParent,
                                      DWORD flags,
                                      DWORD* pressedButton,
                                      DWORD* silentMask) = 0;
};

#ifdef _MSC_VER
#pragma pack(pop, enter_include_spl_file)
#endif // _MSC_VER
#ifdef __BORLANDC__
#pragma option -a
#endif // __BORLANDC__
