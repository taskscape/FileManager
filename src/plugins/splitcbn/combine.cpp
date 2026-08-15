// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <tchar.h>
#include "splitcbn.h"
#include "splitcbn.rh"
#include "splitcbn.rh2"
#include "lang\lang.rh"
#include "combine.h"
#include "dialogs.h"
#include "..\..\operation_result.h"
#include "..\..\common\checked_arithmetic.h"

#include <memory>

// *****************************************************************************
//
//  Combine Files
//

#define BUFSIZE (512 * 1024)

namespace
{
// Keep staged output beside the requested name so promotion never degrades into
// a cross-volume copy that could expose a partial destination.
static BOOL PromoteFileUtf8Local(const char* stagedName, const char* targetName)
{
    WCHAR* stagedNameW = Utf8AllocWide(stagedName);
    WCHAR* targetNameW = Utf8AllocWide(targetName);
    if (stagedNameW == NULL || targetNameW == NULL)
    {
        free(stagedNameW);
        free(targetNameW);
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    // ReplaceFile preserves destination metadata; MoveFileEx handles a target removed after selection.
    BOOL ok = ReplaceFileW(targetNameW, stagedNameW, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && error == ERROR_FILE_NOT_FOUND)
    {
        ok = MoveFileExW(stagedNameW, targetNameW, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        error = ok ? ERROR_SUCCESS : GetLastError();
    }
    free(stagedNameW);
    free(targetNameW);
    if (!ok)
        SetLastError(error);
    return ok;
}

class CScopedSafeFile
{
public:
    CScopedSafeFile() : IsOpen(FALSE) { ZeroMemory(&File, sizeof(File)); }
    ~CScopedSafeFile() { Close(); }

    SAFE_FILE* Get() { return &File; }
    void Opened() { IsOpen = TRUE; }
    void Close()
    {
        if (IsOpen)
        {
            SalamanderSafeFile->SafeFileClose(&File);
            IsOpen = FALSE;
        }
    }

private:
    SAFE_FILE File;
    BOOL IsOpen;
};

class CCombineTemporaryOutput
{
public:
    CCombineTemporaryOutput() : Reserved(FALSE) { Name[0] = 0; }
    ~CCombineTemporaryOutput()
    {
        if (Reserved)
            DeleteFileUtf8Local(Name);
    }

    COperationResult Reserve(const char* targetName)
    {
        char targetDirectory[MAX_PATH];
        strncpy_s(targetDirectory, targetName, _TRUNCATE);
        if (!SalamanderGeneral->CutDirectory(targetDirectory))
            return COperationResult::Failure(orpPrepareTransactionalTarget, ERROR_INVALID_NAME, NULL, targetName, FALSE);

        DWORD error = ERROR_SUCCESS;
        // SalGetTempFileName creates the reservation, closing the race before the writer opens it.
        if (!SalamanderGeneral->SalGetTempFileName(targetDirectory, "SCB", Name, TRUE, &error))
            return COperationResult::Failure(orpPrepareTransactionalTarget,
                                             error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT,
                                             NULL, targetName, FALSE);

        Reserved = TRUE;
        return COperationResult::Success(orpPrepareTransactionalTarget, Name, targetName, opeTemporaryTargetReady);
    }

    const char* GetName() const { return Name; }
    void Commit() { Reserved = FALSE; }

    void Cleanup(COperationResult* result)
    {
        if (Reserved && !DeleteFileUtf8Local(Name))
        {
            // The primary combine failure remains actionable even if temporary cleanup also fails.
            result->AppendCleanupError(orcpDeleteUnverifiedTarget, GetLastError(), Name);
            return;
        }
        Reserved = FALSE;
    }

private:
    char Name[MAX_PATH];
    BOOL Reserved;
};

static COperationResult VerifyCombinedOutput(const char* outputName, const CQuadWord& expectedSize)
{
    HANDLE output = NOHANDLES(CreateFileUtf8Local(outputName, GENERIC_READ,
                                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                                    NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (output == INVALID_HANDLE_VALUE)
        return COperationResult::Failure(orpVerifyDurableCopy, GetLastError(), NULL, outputName, FALSE,
                                         opeTemporaryTargetReady);

    BY_HANDLE_FILE_INFORMATION information;
    BOOL verified = GetFileInformationByHandle(output, &information);
    DWORD error = verified ? ERROR_SUCCESS : GetLastError();
    if (verified && ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
                     information.nFileSizeLow != expectedSize.LoDWord ||
                     information.nFileSizeHigh != expectedSize.HiDWord))
    {
        verified = FALSE;
        error = ERROR_WRITE_FAULT;
    }

    // Capture verification before closing; CloseHandle is only cleanup evidence on failure.
    COperationResult result = verified ? COperationResult::Success(orpVerifyDurableCopy, NULL, outputName,
                                                                    opeTemporaryTargetReady) :
                                         COperationResult::Failure(orpVerifyDurableCopy, error, NULL, outputName,
                                                                   FALSE, opeTemporaryTargetReady);
    if (!NOHANDLES(CloseHandle(output)))
    {
        DWORD closeError = GetLastError();
        if (result.Succeeded())
            result = COperationResult::Failure(orpVerifyDurableCopy, closeError, NULL, outputName, FALSE,
                                               opeTemporaryTargetReady);
        else
            result.AppendCleanupError(orcpCloseVerificationHandle, closeError, outputName);
    }
    return result;
}

static BOOL ReportCombineFailure(const COperationResult& result, int idTitle, int idMessage)
{
    DWORD error = ERROR_SUCCESS;
    result.ToLegacyBool(&error); // The plug-in keeps its BOOL contract while retaining the causal Win32 error.
    SetLastError(error);

    char diagnostic[512];
    result.BuildDiagnosticSummary(diagnostic, _countof(diagnostic));
    TRACE_E("CombineFiles(): " << diagnostic);
    SalamanderGeneral->ShowMessageBox(LoadStr(idMessage), LoadStr(idTitle), MSGBOX_ERROR);
    return FALSE;
}
}

BOOL CombineFiles(TIndirectArray<char>& files, LPTSTR targetName,
                  BOOL bOnlyCrc, BOOL bTestCrc, UINT32& Crc,
                  BOOL bTime, FILETIME* origTime, HWND parent,
                  CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE4("CombineFiles( , %s, %ld, %X, , )", targetName, bTestCrc, Crc);

    if (!bOnlyCrc && !files.Count)
    {
        SalamanderGeneral->ShowMessageBox(LoadStr(IDS_ZEROFILES), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
        return FALSE;
    }

    int idTitle = bOnlyCrc ? IDS_CRCTITLE : IDS_COMBINE;

    // Sum checked 64-bit sizes while simultaneously checking each partial file's accessibility.
    CQuadWord totalSize = CQuadWord(0, 0);
    char text[MAX_PATH + 50];
    int i;
    for (i = 0; i < files.Count; i++)
    {
        CScopedSafeFile file;
        if (!SalamanderSafeFile->SafeFileOpen(file.Get(), files[i], GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                              0, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
        {
            return FALSE;
        }
        file.Opened();
        CFileOffsetResult sizeResult = SalGetPluginFileSizeEx(SalamanderGeneral, file.Get()->HFile);
        if (!sizeResult.Succeeded)
        {
            COperationResult result = COperationResult::Failure(orpVerifyDurableCopy, sizeResult.Error,
                                                                 files[i], targetName, FALSE);
            return ReportCombineFailure(result, idTitle, IDS_READERROR);
        }
        CQuadWord size = sizeResult.Value;
        unsigned __int64 combinedSize;
        // Part sizes come from external files; use the shared guard before the progress/free-space total can wrap.
        if (!CheckedAddUInt64(totalSize.Value, size.Value, &combinedSize))
        {
            COperationResult result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_ARITHMETIC_OVERFLOW,
                                                                 files[i], targetName, FALSE);
            return ReportCombineFailure(result, idTitle, IDS_READERROR);
        }
        totalSize.Value = combinedSize;
    }

    // check available free space
    if (!bOnlyCrc)
    {
        char dir[MAX_PATH];
        strncpy_s(dir, targetName, _TRUNCATE);
        SalamanderGeneral->CutDirectory(dir);
        if (!SalamanderGeneral->TestFreeSpace(parent, dir, totalSize, LoadStr(IDS_COMBINE)))
            return FALSE;
    }

    CCombineTemporaryOutput temporaryOutput;
    CScopedSafeFile outfile;
    if (!bOnlyCrc)
    {
        COperationResult reserveResult = temporaryOutput.Reserve(targetName);
        if (!reserveResult.Succeeded())
            return ReportCombineFailure(reserveResult, idTitle, IDS_WRITEERROR);

        if (!SalamanderSafeFile->SafeFileOpen(outfile.Get(), temporaryOutput.GetName(), GENERIC_WRITE, FILE_SHARE_READ,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH,
                                              parent, BUTTONS_RETRYCANCEL, NULL, NULL))
            return FALSE;
        outfile.Opened();
    }

    // The buffer is owned for every return path, including cancellation during a SafeFile retry dialog.
    std::unique_ptr<char, decltype(&free)> pBuffer(static_cast<char*>(malloc(BUFSIZE)), free);
    if (pBuffer.get() == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LoadStr(IDS_OUTOFMEM), LoadStr(idTitle), MSGBOX_ERROR);
        return FALSE;
    }

    UINT32 CrcVal = 0;

    // open the progress dialog
    salamander->OpenProgressDialog(LoadStr(idTitle), TRUE, parent, FALSE);
    salamander->ProgressSetTotalSize(CQuadWord(-1, -1), totalSize);
    salamander->ProgressSetSize(CQuadWord(-1, -1), CQuadWord(0, 0), FALSE);
    CQuadWord totalProgress = CQuadWord(0, 0);

    int ret = TRUE;
    BOOL reportFailure = FALSE;
    COperationResult result = COperationResult::Success(orpVerifyDurableCopy, NULL,
                                                         bOnlyCrc ? NULL : temporaryOutput.GetName(),
                                                         bOnlyCrc ? opeNone : opeTemporaryTargetReady);
    int j;
    for (j = 0; j < files.Count; j++)
    {
        sprintf(text, "%s %s...", LoadStr(IDS_PROCESSING), files[j]);
        salamander->ProgressDialogAddText(text, TRUE);

        CScopedSafeFile file;
        if (!SalamanderSafeFile->SafeFileOpen(file.Get(), files[j], GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING,
                                              FILE_FLAG_SEQUENTIAL_SCAN, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
        {
            result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_CANCELLED, files[j],
                                               bOnlyCrc ? NULL : temporaryOutput.GetName(), FALSE,
                                               bOnlyCrc ? opeNone : opeTemporaryTargetReady);
            ret = FALSE;
            break;
        }
        file.Opened();

        DWORD numread, numwr;
        CQuadWord currentProgress = CQuadWord(0, 0);
        CFileOffsetResult sizeResult = SalGetPluginFileSizeEx(SalamanderGeneral, file.Get()->HFile);
        if (!sizeResult.Succeeded)
        {
            result = COperationResult::Failure(orpVerifyDurableCopy, sizeResult.Error, files[j],
                                               bOnlyCrc ? NULL : temporaryOutput.GetName(), FALSE,
                                               bOnlyCrc ? opeNone : opeTemporaryTargetReady);
            reportFailure = TRUE;
            ret = FALSE;
            break;
        }
        CQuadWord size = sizeResult.Value;
        salamander->ProgressSetTotalSize(size, CQuadWord(-1, -1));
        salamander->ProgressSetSize(CQuadWord(0, 0), CQuadWord(-1, -1), TRUE);
        do
        {
            if (!SalamanderSafeFile->SafeFileRead(file.Get(), pBuffer.get(), BUFSIZE, &numread, parent, BUTTONS_RETRYCANCEL, NULL, NULL))
            {
                result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_CANCELLED, files[j],
                                                   bOnlyCrc ? NULL : temporaryOutput.GetName(), FALSE,
                                                   bOnlyCrc ? opeNone : opeTemporaryTargetReady);
                ret = FALSE;
                break;
            }
            if (!bOnlyCrc && numread)
            {
                if (!SalamanderSafeFile->SafeFileWrite(outfile.Get(), pBuffer.get(), numread, &numwr,
                                                       parent, BUTTONS_RETRYCANCEL, NULL, NULL))
                {
                    result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_CANCELLED, files[j],
                                                       temporaryOutput.GetName(), FALSE, opeTemporaryTargetReady);
                    ret = FALSE;
                    break;
                }
            }
            CrcVal = SalamanderGeneral->UpdateCrc32(pBuffer.get(), numread, CrcVal);
            currentProgress += CQuadWord(numread, 0);
            if (!salamander->ProgressSetSize(currentProgress, totalProgress + currentProgress, TRUE))
            {
                result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_CANCELLED, files[j],
                                                   bOnlyCrc ? NULL : temporaryOutput.GetName(), FALSE,
                                                   bOnlyCrc ? opeNone : opeTemporaryTargetReady);
                ret = FALSE;
                break;
            }
        } while (numread == BUFSIZE);

        totalProgress += currentProgress;
        if (ret == FALSE)
            break;
    }

    salamander->CloseProgressDialog();
    if (!bOnlyCrc)
    {
        // Reject a known-bad assembly while it is still staged, before it can replace the destination.
        if (ret && bTestCrc && Crc != CrcVal)
        {
            SalamanderGeneral->ShowMessageBox(LoadStr(IDS_CRCERROR), LoadStr(idTitle), MSGBOX_ERROR);
            result = COperationResult::Failure(orpVerifyDurableCopy, ERROR_CRC, NULL, temporaryOutput.GetName(),
                                               FALSE, opeTemporaryTargetReady);
            ret = FALSE;
        }
        if (ret && bTime && !SetFileTime(outfile.Get()->HFile, NULL, NULL, origTime))
        {
            DWORD error = GetLastError();
            result = COperationResult::Failure(orpVerifyDurableCopy, error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT,
                                               NULL, temporaryOutput.GetName(),
                                               FALSE, opeTemporaryTargetReady);
            reportFailure = TRUE;
            ret = FALSE;
        }
        if (ret && !FlushFileBuffers(outfile.Get()->HFile))
        {
            DWORD error = GetLastError();
            result = COperationResult::Failure(orpVerifyDurableCopy, error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT,
                                               NULL, temporaryOutput.GetName(),
                                               FALSE, opeTemporaryTargetReady);
            reportFailure = TRUE;
            ret = FALSE;
        }
        // Closing before reopening prevents a successful buffered write from becoming a premature commit.
        outfile.Close();
        if (ret)
        {
            result = VerifyCombinedOutput(temporaryOutput.GetName(), totalSize);
            ret = result.Succeeded();
            reportFailure = !ret;
        }
        if (ret && !PromoteFileUtf8Local(temporaryOutput.GetName(), targetName))
        {
            DWORD error = GetLastError();
            result = COperationResult::Failure(orpCommitTransactionalTarget,
                                               error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT,
                                               temporaryOutput.GetName(), targetName, FALSE,
                                               opeTemporaryTargetReady);
            reportFailure = TRUE;
            ret = FALSE;
        }
        if (ret)
        {
            temporaryOutput.Commit();

            char* name = (char*)SalamanderGeneral->SalPathFindFileName(targetName);
            if (name > targetName)
            {
                name[-1] = 0;
                SalamanderGeneral->PostChangeOnPathNotification(targetName, FALSE);
            }
        }
        else
        {
            temporaryOutput.Cleanup(&result);
            if (reportFailure)
                ReportCombineFailure(result, idTitle, IDS_WRITEERROR);
        }
    }

    if (bOnlyCrc)
        Crc = CrcVal;

    return ret;
}

// *****************************************************************************
//
//  CalculateFileCRC
//

/*BOOL CalculateFileCRC(UINT32& Crc, HWND parent, CSalamanderForOperationsAbstract* salamander)
{      
  CALL_STACK_MESSAGE1("CalculateFileCRC()");
  HANDLE hFile;
  const CFileData* pfd = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, NULL);
  char path[MAX_PATH];
  SalamanderGeneral->GetPanelPath(PANEL_SOURCE, path, MAX_PATH, NULL, NULL);
  if (!SalamanderGeneral->SalPathAppend(path, pfd->Name, MAX_PATH))
  {
    SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_CALCCRC), MSGBOX_ERROR);
    return FALSE;
  }
  if ((hFile = CreateFileUtf8Local(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
    FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
  {
    return Error(IDS_CALCCRC, IDS_OPENERROR);
  }

  char* pBuffer = new char[BUFSIZE];
  if (pBuffer == NULL)
  {
    CloseHandle(hFile);
    SalamanderGeneral->ShowMessageBox(LoadStr(IDS_OUTOFMEM), LoadStr(IDS_CALCCRC), MSGBOX_ERROR);
    return FALSE;
  }

  Crc = 0;

  salamander->OpenProgressDialog(LoadStr(IDS_CALCCRC), FALSE, NULL, FALSE);
  LARGE_INTEGER fileSize;
  CQuadWord totalSize(-1, -1);
  if (GetFileSizeEx(hFile, &fileSize))
    totalSize.SetUI64((unsigned __int64)fileSize.QuadPart);
  // Report an unknown total on lookup failure instead of treating the legacy sentinel as a real byte count.
  salamander->ProgressSetTotalSize(totalSize, CQuadWord(-1, -1));

  DWORD numread;
  int ret = TRUE;
  CQuadWord progress = CQuadWord(0, 0);
  do
  {
    if (!SafeReadFile(hFile, pBuffer, BUFSIZE, &numread, path))
    {
      ret = FALSE;
      break;
    }
    Crc = SalamanderGeneral->UpdateCrc32(pBuffer, numread, Crc);
    progress += CQuadWord(numread, 0);
    if (!salamander->ProgressSetSize(progress, CQuadWord(-1, -1), TRUE)) 
    {
      ret = FALSE;
      break;
    }
  }
  while (numread == BUFSIZE);

  salamander->CloseProgressDialog();
  delete [] pBuffer;
  CloseHandle(hFile);
  return ret;
}*/

// *****************************************************************************
//
//  CombineCommand
//

static BOOL AddFile(TIndirectArray<char>& files, LPTSTR sourceDir, LPTSTR name, BOOL bReverse)
{
    CALL_STACK_MESSAGE1("AllocName( , , )");
    char* str = (char*)malloc(strlen(sourceDir) + 2 + strlen(name));
    if (str == NULL)
    {
        SalamanderGeneral->ShowMessageBox(LoadStr(IDS_OUTOFMEM), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
        return FALSE;
    }
    strcpy(str, sourceDir);
    if (!SalamanderGeneral->SalPathAppend(str, name, MAX_PATH))
    {
        SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
        return FALSE;
    }
    if (bReverse)
        files.Insert(0, str);
    else
        files.Add(str);
    return TRUE;
}

static BOOL IsInPanel(LPTSTR fileName)
{
    CALL_STACK_MESSAGE1("IsInPanel()");
    int index = 0;
    const CFileData* pfd;
    BOOL isDir;
    while ((pfd = SalamanderGeneral->GetPanelItem(PANEL_SOURCE, &index, &isDir)) != NULL)
        if (!isDir)
            if (!lstrcmpi(fileName, pfd->Name))
                return TRUE;
    return FALSE;
}

static BOOL FindValue(const char*& p)
{
    CALL_STACK_MESSAGE1("FindValue()");
    while (*p && *p != '\r' && *p != '\n' && (*p == ' ' || *p == '\t'))
        p++;
    if (*p != '=' && *p != ':')
        return FALSE;
    p++;
    while (*p && *p != '\r' && *p != '\n' && (*p == ' ' || *p == '\t'))
        p++;
    return *p && *p != '\r' && *p != '\n';
}

static BOOL FindCrc(const char* text, LPCTSTR searchstring, UINT32& crc)
{
    CALL_STACK_MESSAGE2("FindCrc( , %s, )", searchstring);
    const char* p = strstr(text, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
            return sscanf(p, "%x", &crc) == 1;
        else
            return FALSE;
    }
    else
        return FALSE;
}

static BOOL FindName(const char* text, const char* text_locase, LPCTSTR searchstring, LPTSTR name)
{
    CALL_STACK_MESSAGE2("FindName( , %s, )", searchstring);
    const char* p = strstr(text_locase, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
        {
            p += text - text_locase;
            if (*p == '\"')
                p++;
            char* q = name;
            while (*p && *p != '\r' && *p != '\n' && *p != '\"' && (q - name < MAX_PATH))
                *q++ = *p++;
            *q = 0;
            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}

static BOOL FindTime(const char* text, LPCTSTR searchstring, FILETIME* ft)
{
    CALL_STACK_MESSAGE2("FindTime( , %s, )", searchstring);
    const char* p = strstr(text, searchstring);
    if (p != NULL)
    {
        p += strlen(searchstring);
        if (FindValue(p))
        {
            SYSTEMTIME st;
            st.wMilliseconds = 0;
            if (sscanf(p, "%hu-%hu-%hu %hu:%hu:%hu", &st.wYear, &st.wMonth, &st.wDay, &st.wHour, &st.wMinute, &st.wSecond) != 6)
                return FALSE;
            return SystemTimeToFileTime(&st, ft);
        }
        else
            return FALSE;
    }
    else
        return FALSE;
}

static void AnalyzeFile(LPTSTR fileName, LPTSTR origName, UINT32& origCrc, FILETIME* origTime,
                        BOOL& bNameAcquired, BOOL& bCrcAcquired, BOOL& bTimeAcquired)
{
    CALL_STACK_MESSAGE2("AnalyzeFile(%s, , , , )", fileName);
    bNameAcquired = bCrcAcquired = FALSE;
    // load the file into a buffer
    HANDLE hFile;
    if ((hFile = CreateFileUtf8Local(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_FLAG_SEQUENTIAL_SCAN, NULL)) == INVALID_HANDLE_VALUE)
        return;
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart < 0 || fileSize.QuadPart > 200000)
    {
        CloseHandle(hFile);
        return;
    }
    // AnalyzeFile allocates a DWORD-sized text buffer only for its intentionally small batch-file heuristic.
    DWORD size = (DWORD)fileSize.QuadPart, numread;
    // j.r. 21.1.2003: when splitting to 999 parts I received a batch of 30KB
    //  if (size > 10000) { CloseHandle(hFile); return; } // skip such large files
    char* text = new char[size + 1];
    char* text_locase = new char[size + 1];
    if (text == NULL || text_locase == NULL)
    {
        CloseHandle(hFile);
        return;
    }
    BOOL ok = ReadFile(hFile, text, size, &numread, NULL);
    CloseHandle(hFile);
    if (!ok || size != numread)
    {
        delete[] text;
        delete[] text_locase;
        return;
    }
    text[size] = 0;

    strcpy(text_locase, text);
    CharLower(text_locase);
    // try to find "crc32" or "crc"
    if (!bCrcAcquired)
    {
        bCrcAcquired = FindCrc(text_locase, "crc32", origCrc);
        if (!bCrcAcquired)
            bCrcAcquired = FindCrc(text_locase, "crc", origCrc);
    }
    // "filename" or "name"
    if (!bNameAcquired)
    {
        bNameAcquired = FindName(text, text_locase, "filename", origName);
        if (!bNameAcquired)
            bNameAcquired = FindName(text, text_locase, "name", origName);
    }
    // "time"
    if (!bTimeAcquired)
    {
        bTimeAcquired = FindTime(text_locase, "time", origTime);
    }

    delete[] text;
    delete[] text_locase;
}

BOOL CombineCommand(DWORD eventMask, HWND parent, CSalamanderForOperationsAbstract* salamander)
{
    CALL_STACK_MESSAGE2("CombineCommand(%X, , )", eventMask);

    TIndirectArray<char> files(100, 100, dtDelete);

    char sourceDir[MAX_PATH];
    SalamanderGeneral->GetPanelPath(PANEL_SOURCE, sourceDir, MAX_PATH, NULL, NULL);

    BOOL bTestCompanionFile = FALSE;
    char companionFile[MAX_PATH];
    char name1[MAX_PATH], name2[MAX_PATH];
    const CFileData* pfd;
    BOOL isDir;

    if (eventMask & MENU_EVENT_FILES_SELECTED)
    { // files are selected
        int index = 0;
        BOOL bAllSameNames = TRUE;
        BOOL bFirst = TRUE;

        // load selected items into the array (except directories)
        while ((pfd = SalamanderGeneral->GetPanelSelectedItem(PANEL_SOURCE, &index, &isDir)) != NULL)
        {
            if (!isDir)
            {
                if (bFirst)
                {
                    strcpy(name1, pfd->Name);
                    StripExtension(name1);
                    bFirst = FALSE;
                }
                else if (bAllSameNames)
                {
                    strcpy(name2, pfd->Name);
                    StripExtension(name2);
                    if (lstrcmpi(name1, name2))
                        bAllSameNames = FALSE;
                }
                if (!AddFile(files, sourceDir, pfd->Name, FALSE))
                    return FALSE;
            }
        }

        // if all names were identical, there is a chance we will find a companion .BAT or .CRC
        bTestCompanionFile = bAllSameNames;
        if (!bAllSameNames)
            strcpy(name1, "combinedfile");
        strcpy(companionFile, sourceDir);
        if (!SalamanderGeneral->SalPathAppend(companionFile, name1, MAX_PATH) ||
            strlen(companionFile) + 1 >= MAX_PATH) // safety check for the strcat() below
        {
            SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
            return FALSE;
        }
        strcat(companionFile, ".");
    }
    else // only the focus is on a file - try to extend the selection with "higher" files
    {
        pfd = SalamanderGeneral->GetPanelFocusedItem(PANEL_SOURCE, &isDir);
        if (pfd == NULL || isDir)
        {
            TRACE_E("CombineCommand(): No focus on a file?!?");
            return FALSE;
        }
        BOOL bJustOneFile = FALSE;
        // first analyze the extension
        char* ext = _tcsrchr(pfd->Name, '.');
        if (ext != NULL) // ".cvspass" is an extension in Windows
        {
            BOOL bZeroPadded, bAddThisFile = TRUE;
            int nextIndex;
            if (!lstrcmpi(ext, ".tns"))
            { // tns = Turbo Navigator Split - consider it as "000"
                bZeroPadded = FALSE;
                nextIndex = 2;
            }
            else if (!lstrcmpi(ext, ".bat") || !lstrcmpi(ext, ".crc"))
            { // Salamander's BAT or WinCommander CRC; this will not work with TN files here
                bZeroPadded = TRUE;
                nextIndex = 1;
                bAddThisFile = FALSE;
            }
            else
            { // is the extension composed of digits?
                BOOL bNumbers = TRUE;
                int numberCount = 0, i = 1;
                while (ext[i])
                    if (ext[i] < '0' || ext[i] > '9')
                    {
                        bNumbers = FALSE;
                        break;
                    }
                    else
                    {
                        numberCount++;
                        i++;
                    }
                if (!bNumbers || numberCount > 3)
                    bJustOneFile = TRUE; // strange extension - we will not extend anything
                else
                {
                    bZeroPadded = (ext[1] == '0');
                    nextIndex = atol(ext + 1) + 1;
                }
            }

            const size_t namePrefixLength = ext - pfd->Name + 1;
            // The extension pointer defines the exact prefix we need; terminate that counted copy explicitly.
            memcpy(name1, pfd->Name, namePrefixLength);
            name1[namePrefixLength] = 0;
            strcpy(companionFile, sourceDir);
            if (!SalamanderGeneral->SalPathAppend(companionFile, name1, MAX_PATH))
            {
                SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
                return FALSE;
            }

            if (!bJustOneFile)
            {
                if (nextIndex > 1)
                {
                    int prevIndex = nextIndex - 2;
                    while (1)
                    {
                        sprintf(name2, bZeroPadded ? "%s%#03ld" : "%s%ld", name1, prevIndex--);
                        if (!IsInPanel(name2))
                            break;
                        if (!AddFile(files, sourceDir, name2, TRUE))
                            return FALSE;
                    }
                }

                strcpy(name2, pfd->Name);
                bTestCompanionFile = TRUE;
                do
                {
                    if (bAddThisFile)
                        if (!AddFile(files, sourceDir, name2, FALSE))
                            return FALSE;
                    sprintf(name2, bZeroPadded ? "%s%#03ld" : "%s%ld", name1, nextIndex++);
                    bAddThisFile = TRUE;
                } while (IsInPanel(name2));
            }
        }
        else
        { // missing extension - skip it, we will not extend anything
            bJustOneFile = TRUE;
            strcpy(companionFile, sourceDir);
            if (!SalamanderGeneral->SalPathAppend(companionFile, "combinedfile", MAX_PATH))
            {
                SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
                return FALSE;
            }
        }

        if (bJustOneFile)
            if (!AddFile(files, sourceDir, pfd->Name, FALSE))
                return FALSE;
    }

    BOOL bName = FALSE, bCrc = FALSE, bTime = FALSE;
    UINT32 origCrc;
    FILETIME origTime;

    if (bTestCompanionFile)
    { // inspect a potential BAT or CRC
        size_t ext = strlen(companionFile);
        if (ext + 3 >= MAX_PATH) // safety check for the strcat() below
        {
            SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
            return FALSE;
        }
        strcat(companionFile, "bat");
        AnalyzeFile(companionFile, name2, origCrc, &origTime, bName, bCrc, bTime);
        if (!bName || !bCrc)
        {
            companionFile[ext] = 0;
            strcat(companionFile, "crc");
            AnalyzeFile(companionFile, name2, origCrc, &origTime, bName, bCrc, bTime);
        }
        companionFile[ext] = 0;
        if (bName)
        {
            strcpy(name1, sourceDir);
            if (!SalamanderGeneral->SalPathAppend(name1, name2, MAX_PATH))
            {
                SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
                return FALSE;
            }
        }
    }

    if (!bName)
    { // set a default name
        strcpy(name1, companionFile);
        name1[strlen(name1) - 1] = 0;
        char* dot = _tcsrchr(name1, '.');
        if (dot == NULL) // ".cvspass" is an extension in Windows
        {
            if (strlen(name1) + 4 >= MAX_PATH) // safety check for the strcat() below
            {
                SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
                return FALSE;
            }
            strcat(name1, ".EXT");
        }
    }

    if (configCombineToOther)
    { // JC - February 2002 - adjustment for combining to the other panel, sorry, a bit of a hack
        // but the previous code did not anticipate it, so I would have had to rewrite it all...
        // This code replaces the path in "name1", which points to the source panel, with the target panel path
        SalamanderGeneral->SalPathStripPath(name1);
        GetTargetDir(name2, NULL, FALSE);
        if (!SalamanderGeneral->SalPathAppend(name2, name1, MAX_PATH))
        {
            SalamanderGeneral->ShowMessageBox(LoadStr(IDS_TOOLONGNAME2), LoadStr(IDS_COMBINE), MSGBOX_ERROR);
            return FALSE;
        }
        strcpy(name1, name2);
    }

    if (!CombineDialog(files, name1, bCrc, origCrc, parent, salamander))
        return FALSE;

    GetTargetDir(sourceDir, NULL, FALSE);
    if (!MakePathAbsolute(name1, FALSE, sourceDir, !configCombineToOther, IDS_COMBINE))
        return FALSE;

    return CombineFiles(files, name1, FALSE, bCrc, origCrc, bTime, &origTime, parent, salamander);
}
