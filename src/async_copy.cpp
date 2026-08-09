// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "cfgdlg.h"
#include "worker.h"
#include "execlog.h"

#include <Aclapi.h>
#include <Ntsecapi.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

// CreateFileUtf8 / DeleteFileUtf8 / SetFileAttributesUtf8 / RemoveDirectoryUtf8
// are globally declared in common/strutils.h and defined in common/strutils.cpp.
int GetOptimalSyncCopyBufferSize(COperations* script, DWORD opFlags);
extern NTQUERYINFORMATIONFILE DynNtQueryInformationFile;
extern NTFSCONTROLFILE DynNtFsControlFile;
// ****************************************************************************
// CAsyncCopyParams (declaration in worker.h, implementations below)
//

CAsyncCopyParams::CAsyncCopyParams()
{
    memset(Buffers, 0, sizeof(Buffers));
    memset(Overlapped, 0, sizeof(Overlapped));
    UseAsyncAlg = FALSE;
    HasFailed = FALSE;
}

void CAsyncCopyParams::Init(BOOL useAsyncAlg)
{
    UseAsyncAlg = useAsyncAlg;
    if (UseAsyncAlg && Buffers[0] == NULL)
    {
        for (int i = 0; i < 8; i++)
        {
            Buffers[i] = malloc(ASYNC_COPY_BUF_SIZE);
            Overlapped[i].hEvent = HANDLES(CreateEvent(NULL, TRUE, FALSE, NULL));
            if (Overlapped[i].hEvent == NULL)
            {
                DWORD err = GetLastError();
                TRACE_E("Unable to create synchronization object for Copy rutine: " << GetErrorText(err));
                HasFailed = TRUE;
            }
        }
    }
}

CAsyncCopyParams::~CAsyncCopyParams()
{
    for (int i = 0; i < 8; i++)
    {
        if (Buffers[i] != NULL)
            free(Buffers[i]);
        if (Overlapped[i].hEvent != NULL)
            HANDLES(CloseHandle(Overlapped[i].hEvent));
    }
}

OVERLAPPED*
CAsyncCopyParams::InitOverlapped(int i)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlapped(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = 0;
    Overlapped[i].OffsetHigh = 0;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

OVERLAPPED*
CAsyncCopyParams::InitOverlappedWithOffset(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::InitOverlappedWithOffset(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0;
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    return &Overlapped[i];
}

void CAsyncCopyParams::SetOverlappedToEOF(int i, const CQuadWord& offset)
{
    if (!UseAsyncAlg)
        TRACE_C("CAsyncCopyParams::SetOverlappedToEOF(): unexpected call, UseAsyncAlg is FALSE!");
    Overlapped[i].Internal = 0xC0000011 /* STATUS_END_OF_FILE */; // NTSTATUS code equivalent to system error code ERROR_HANDLE_EOF
    Overlapped[i].InternalHigh = 0;
    Overlapped[i].Offset = offset.LoDWord;
    Overlapped[i].OffsetHigh = offset.HiDWord;
    // Overlapped[i].Pointer = 0;  // this is a union, Pointer overlaps with Offset and OffsetHigh
    SetEvent(Overlapped[i].hEvent);
}

// **********************************************************************************

BOOL HaveWriteOwnerRight = FALSE; // does the process have the WRITE_OWNER right?

void InitWorker()
{
    if (NtDLL != NULL) // "always true"
    {
        DynNtQueryInformationFile = (NTQUERYINFORMATIONFILE)GetProcAddress(NtDLL, "NtQueryInformationFile"); // has no header
        DynNtFsControlFile = (NTFSCONTROLFILE)GetProcAddress(NtDLL, "NtFsControlFile");                      // has no header
    }
}

void ReleaseWorker()
{
    DynNtQueryInformationFile = NULL;
    DynNtFsControlFile = NULL;
}


void SetProgressDialog(HWND hProgressDlg, CProgressData* data, CProgressDlgData& dlgData)
{                                                              // wait for the response; the dialog must be updated
    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (!*dlgData.CancelWorker)                                // we need to stop the main thread
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, (WPARAM)data, 0);
}

int CaclProg(const CQuadWord& progressCurrent, const CQuadWord& progressTotal)
{
    return progressCurrent >= progressTotal ? (progressTotal.Value == 0 ? 0 : 1000) : (int)((progressCurrent * CQuadWord(1000, 0)) / progressTotal).Value;
}

void SetProgress(HWND hProgressDlg, int operation, int summary, CProgressDlgData& dlgData)
{                                                              // notify about the change and continue without waiting for a reply
    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (!*dlgData.CancelWorker &&
        (*dlgData.OperationProgress != operation || *dlgData.SummaryProgress != summary))
    {
        *dlgData.OperationProgress = operation;
        *dlgData.SummaryProgress = summary;
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, 0, 0);
    }
}

void SetProgressWithoutSuspend(HWND hProgressDlg, int operation, int summary, CProgressDlgData& dlgData)
{ // notify about the change and continue without waiting for a reply
    if (!*dlgData.CancelWorker &&
        (*dlgData.OperationProgress != operation || *dlgData.SummaryProgress != summary))
    {
        *dlgData.OperationProgress = operation;
        *dlgData.SummaryProgress = summary;
        SendMessage(hProgressDlg, WM_USER_SETDIALOG, 0, 0);
    }
}

BOOL GetDirTime(const char* dirName, FILETIME* ftModified);
BOOL DoCopyDirTime(HWND hProgressDlg, const char* targetName, FILETIME* modified, CProgressDlgData& dlgData, BOOL quiet);

void GetFileOverwriteInfo(char* buff, int buffLen, HANDLE file, const char* fileName, FILETIME* fileTime, BOOL* getTimeFailed)
{
    FILETIME lastWrite;
    SYSTEMTIME st;
    FILETIME ft;
    char date[50], time[50];
    if (!GetFileTime(file, NULL, NULL, &lastWrite) ||
        !FileTimeToLocalFileTime(&lastWrite, &ft) ||
        !FileTimeToSystemTime(&ft, &st))
    {
        if (getTimeFailed != NULL)
            *getTimeFailed = TRUE;
        date[0] = 0;
        time[0] = 0;
    }
    else
    {
        if (fileTime != NULL)
            *fileTime = ft;
        if (GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, time, 50) == 0)
            _snprintf_s(time, _countof(time), _TRUNCATE, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
        if (GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 50) == 0)
            _snprintf_s(date, _countof(date), _TRUNCATE, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);
    }

    char attr[30];
    lstrcpy(attr, ", ");
    DWORD attrs = SalGetFileAttributes(fileName);
    if (attrs != 0xFFFFFFFF)
        GetAttrsString(attr + 2, attrs);
    if (strlen(attr) == 2)
        attr[0] = 0;

    char number[50];
    CQuadWord size;
    DWORD err;
    if (SalGetFileSize(file, size, err))
        NumberToStr(number, size);
    else
        number[0] = 0; // error - size unknown

    _snprintf_s(buff, buffLen, _TRUNCATE, "%s, %s, %s%s", number, date, time, attr);
}

void GetDirInfo(char* buffer, int bufferLen, const char* dir)
{
    if (bufferLen <= 0)
        return;
    const char* dirFindFirst = dir;
    char dirFindFirstCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(dirFindFirst, dirFindFirstCopy);

    BOOL ok = FALSE;
    FILETIME lastWrite;
    if (NameEndsWithBackslash(dirFindFirst))
    { // FindFirstFile fails for a dir ending with a backslash (used for invalid directory names),
        // so in this situation we handle it through CreateFile and GetFileTime
        CStrP dirFindFirstW(ConvertAllocUtf8ToWide(dirFindFirst, -1));
        HANDLE file = dirFindFirstW != NULL
                          ? HANDLES_Q(CreateFileW(dirFindFirstW, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL))
                          : INVALID_HANDLE_VALUE;
        if (file != INVALID_HANDLE_VALUE)
        {
            if (GetFileTime(file, NULL, NULL, &lastWrite))
                ok = TRUE;
            HANDLES(CloseHandle(file));
        }
    }
    else
    {
        WIN32_FIND_DATAW data;
        CStrP dirFindFirstW(ConvertAllocUtf8ToWide(dirFindFirst, -1));
        HANDLE find = dirFindFirstW != NULL ? HANDLES_Q(FindFirstFileW(dirFindFirstW, &data)) : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE)
        {
            HANDLES(FindClose(find));
            lastWrite = data.ftLastWriteTime;
            ok = TRUE;
        }
    }
    if (ok)
    {
        SYSTEMTIME st;
        FILETIME ft;
        if (FileTimeToLocalFileTime(&lastWrite, &ft) &&
            FileTimeToSystemTime(&ft, &st))
        {
            char date[50], time[50];
            if (GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, time, 50) == 0)
                _snprintf_s(time, _countof(time), _TRUNCATE, "%u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
            if (GetDateFormat(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, date, 50) == 0)
                _snprintf_s(date, _countof(date), _TRUNCATE, "%u.%u.%u", st.wDay, st.wMonth, st.wYear);

            _snprintf_s(buffer, bufferLen, _TRUNCATE, "%s, %s", date, time);
        }
        else
            _snprintf_s(buffer, bufferLen, _TRUNCATE, "%s, %s", LoadStr(IDS_INVALID_DATEORTIME), LoadStr(IDS_INVALID_DATEORTIME));
    }
    else
        buffer[0] = 0;
}

BOOL IsDirectoryEmpty(const char* name) // directories/subdirectories contain no files
{
    char dir[MAX_PATH + 5];
    int len = (int)strlen(name);
    if (len <= 0 || len >= _countof(dir) - 2)
        return FALSE;
    memcpy(dir, name, len);
    dir[len] = 0;
    if (dir[len - 1] != '\\')
        dir[len++] = '\\';
    char* end = dir + len;
    *end++ = '*';
    *end = 0;

    WIN32_FIND_DATAW fileData;
    HANDLE search;
    CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
    search = dirW != NULL ? HANDLES_Q(FindFirstFileW(dirW, &fileData)) : INVALID_HANDLE_VALUE;
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fileData.cFileName[0] == 0 ||
                fileData.cFileName[0] == L'.' && (fileData.cFileName[1] == 0 ||
                                                  fileData.cFileName[1] == L'.' && fileData.cFileName[2] == 0))
                continue;

            if (fileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                int remaining = (int)(_countof(dir) - (end - dir));
                if (remaining <= 1 || ConvertWideToUtf8(fileData.cFileName, -1, end, remaining) == 0)
                    continue;
                if (!IsDirectoryEmpty(dir)) // the subdirectory is not empty
                {
                    HANDLES(FindClose(search));
                    return FALSE;
                }
            }
            else
            {
                HANDLES(FindClose(search)); // a file exists here
                return FALSE;
            }
        } while (FindNextFileW(search, &fileData));
        HANDLES(FindClose(search));
    }
    return TRUE;
}

BOOL CurrentProcessTokenUserValid = FALSE;
char CurrentProcessTokenUserBuf[200];
TOKEN_USER* CurrentProcessTokenUser = (TOKEN_USER*)CurrentProcessTokenUserBuf;

void GainWriteOwnerAccess()
{
    static BOOL firstCall = TRUE;
    if (firstCall)
    {
        firstCall = FALSE;

        HANDLE tokenHandle;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tokenHandle))
        {
            TRACE_E("GainWriteOwnerAccess(): OpenProcessToken failed!");
            return;
        }

        DWORD reqSize;
        if (GetTokenInformation(tokenHandle, TokenUser, CurrentProcessTokenUser, 200, &reqSize))
            CurrentProcessTokenUserValid = TRUE;

        int i;
        for (i = 0; i < 3; i++)
        {
            const char* privName = NULL;
            switch (i)
            {
            case 0:
                privName = SE_RESTORE_NAME;
                break;
            case 1:
                privName = SE_TAKE_OWNERSHIP_NAME;
                break;
            case 2:
                privName = SE_SECURITY_NAME;
                break;
            }

            LUID value;
            if (privName != NULL && LookupPrivilegeValue(NULL, privName, &value))
            {
                TOKEN_PRIVILEGES tokenPrivileges;
                tokenPrivileges.PrivilegeCount = 1;
                tokenPrivileges.Privileges[0].Luid = value;
                tokenPrivileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                AdjustTokenPrivileges(tokenHandle, FALSE, &tokenPrivileges, sizeof(tokenPrivileges), NULL, NULL);
                if (GetLastError() != NO_ERROR)
                {
                    DWORD err = GetLastError();
                    TRACE_E("GainWriteOwnerAccess(): AdjustTokenPrivileges(" << privName << ") failed! error: " << GetErrorText(err));
                }
                else
                {
                    if (i == 0)
                        HaveWriteOwnerRight = TRUE; // successfully obtained SE_RESTORE_NAME, WRITE_OWNER is guaranteed
                }
            }
            else
            {
                DWORD err = GetLastError();
                TRACE_E("GainWriteOwnerAccess(): LookupPrivilegeValue(" << (privName != NULL ? privName : "null") << ") failed! error: " << GetErrorText(err));
            }
        }
        CloseHandle(tokenHandle);
    }
}
/*
//  Purpose:    Determines if the user is a member of the administrators group.
//  Return:     TRUE if user is a admin
//              FALSE if not
#define STATUS_SUCCESS          ((NTSTATUS)0x00000000L) // ntsubauth
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
typedef NTSTATUS (WINAPI *FNtQueryInformationToken)(
    HANDLE TokenHandle,                             // IN
    TOKEN_INFORMATION_CLASS TokenInformationClass,  // IN
    PVOID TokenInformation,                         // OUT
    ULONG TokenInformationLength,                   // IN
    PULONG ReturnLength                             // OUT
    );


BOOL IsUserAdmin()
{
  if (NtDLL == NULL)
    return TRUE;

  GainWriteOwnerAccess();

  FNtQueryInformationToken DynNTNtQueryInformationToken = (FNtQueryInformationToken)GetProcAddress(NtDLL, "NtQueryInformationToken"); // has no header
  if (DynNTNtQueryInformationToken == NULL)
  {
    TRACE_E("Getting NtQueryInformationToken export failed!");
    return FALSE;
  }

  static int fIsUserAnAdmin = -1;  // cache

  if (-1 == fIsUserAnAdmin)
  {
    SID_IDENTIFIER_AUTHORITY authNT = SECURITY_NT_AUTHORITY;
    NTSTATUS                 Status;
    ULONG                    InfoLength;
    PTOKEN_GROUPS            TokenGroupList;
    ULONG                    GroupIndex;
    BOOL                     FoundAdmins;
    PSID                     AdminsDomainSid;
    HANDLE                   hUserToken;

    // Open the user's token
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hUserToken))
        return FALSE;

    // Create Admins domain sid.
    Status = AllocateAndInitializeSid(
               &authNT,
               2,
               SECURITY_BUILTIN_DOMAIN_RID,
               DOMAIN_ALIAS_RID_ADMINS,
               0, 0, 0, 0, 0, 0,
               &AdminsDomainSid
               );

    // Test if user is in the Admins domain

    // Get a list of groups in the token
    Status = DynNTNtQueryInformationToken(
                 hUserToken,               // Handle
                 TokenGroups,              // TokenInformationClass
                 NULL,                     // TokenInformation
                 0,                        // TokenInformationLength
                 &InfoLength               // ReturnLength
                 );

    if ((Status != STATUS_SUCCESS) && (Status != STATUS_BUFFER_TOO_SMALL))
    {
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }

    TokenGroupList = (PTOKEN_GROUPS)GlobalAlloc(GPTR, InfoLength);

    if (TokenGroupList == NULL)
    {
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }

    Status = DynNTNtQueryInformationToken(
                 hUserToken,        // Handle
                 TokenGroups,              // TokenInformationClass
                 TokenGroupList,           // TokenInformation
                 InfoLength,               // TokenInformationLength
                 &InfoLength               // ReturnLength
                 );

    if (!NT_SUCCESS(Status))
    {
      GlobalFree(TokenGroupList);
      FreeSid(AdminsDomainSid);
      CloseHandle(hUserToken);
      return FALSE;
    }


    // Search group list for Admins alias
    FoundAdmins = FALSE;

    for (GroupIndex=0; GroupIndex < TokenGroupList->GroupCount; GroupIndex++ )
    {
      if (EqualSid(TokenGroupList->Groups[GroupIndex].Sid, AdminsDomainSid))
      {
        FoundAdmins = TRUE;
        break;
      }
    }

    // Tidy up
    GlobalFree(TokenGroupList);
    FreeSid(AdminsDomainSid);
    CloseHandle(hUserToken);

    fIsUserAnAdmin = FoundAdmins ? 1 : 0;
  }

  return (BOOL)fIsUserAnAdmin;
}

*/

/* according to http://vcfaq.mvps.org/sdk/21.htm */
#define BUFF_SIZE 1024
BOOL IsUserAdmin()
{
    HANDLE hToken = NULL;
    PSID pAdminSid = NULL;
    BYTE buffer[BUFF_SIZE];
    PTOKEN_GROUPS pGroups = (PTOKEN_GROUPS)buffer;
    DWORD dwSize; // buffer size
    DWORD i;
    BOOL bSuccess;
    SID_IDENTIFIER_AUTHORITY siaNtAuth = SECURITY_NT_AUTHORITY;

    // get token handle
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return FALSE;

    bSuccess = GetTokenInformation(hToken, TokenGroups, (LPVOID)pGroups, BUFF_SIZE, &dwSize);
    CloseHandle(hToken);
    if (!bSuccess)
        return FALSE;

    if (!AllocateAndInitializeSid(&siaNtAuth, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &pAdminSid))
        return FALSE;

    bSuccess = FALSE;
    for (i = 0; (i < pGroups->GroupCount) && !bSuccess; i++)
    {
        if (EqualSid(pAdminSid, pGroups->Groups[i].Sid))
            bSuccess = TRUE;
    }
    FreeSid(pAdminSid);

    return bSuccess;
}

struct CSrcSecurity // helper structure for keeping security info for MoveFile (the source disappears after the operation, its security info must be stored beforehand)
{
    PSID SrcOwner;
    PSID SrcGroup;
    PACL SrcDACL;
    PSECURITY_DESCRIPTOR SrcSD;
    DWORD SrcError;

    CSrcSecurity() { Clear(); }
    ~CSrcSecurity()
    {
        if (SrcSD != NULL)
            LocalFree(SrcSD);
    }
    void Clear()
    {
        SrcOwner = NULL;
        SrcGroup = NULL;
        SrcDACL = NULL;
        SrcSD = NULL;
        SrcError = NO_ERROR;
    }
};

BOOL DoCopySecurity(const char* sourceName, const char* targetName, DWORD* err, CSrcSecurity* srcSecurity)
{
    // if the path ends with a space or dot, we must append '\\', otherwise
    // GetNamedSecurityInfo (and others) trim the spaces/dots and then work
    // with a different path
    const char* sourceNameSec = sourceName;
    char sourceNameSecCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(sourceNameSec, sourceNameSecCopy);
    const char* targetNameSec = targetName;
    char targetNameSecCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(targetNameSec, targetNameSecCopy);

    PSID srcOwner = NULL;
    PSID srcGroup = NULL;
    PACL srcDACL = NULL;
    PSECURITY_DESCRIPTOR srcSD = NULL;
    if (srcSecurity != NULL) // MoveFile: simply take over the security info
    {
        srcOwner = srcSecurity->SrcOwner;
        srcGroup = srcSecurity->SrcGroup;
        srcDACL = srcSecurity->SrcDACL;
        srcSD = srcSecurity->SrcSD;
        *err = srcSecurity->SrcError;
        srcSecurity->Clear();
    }
    else // obtain the security info from the source
    {
        CStrP sourceNameSecW(ConvertAllocUtf8ToWide(sourceNameSec, -1));
        if (sourceNameSecW != NULL)
        {
            *err = GetNamedSecurityInfoW(sourceNameSecW, SE_FILE_OBJECT,
                                        DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                        &srcOwner, &srcGroup, &srcDACL, NULL, &srcSD);
        }
        else
        {
            *err = ERROR_NO_UNICODE_TRANSLATION;
        }
    }
    BOOL ret = *err == ERROR_SUCCESS;

    if (ret)
    {
        SECURITY_DESCRIPTOR_CONTROL srcSDControl;
        DWORD srcSDRevision;
        if (!GetSecurityDescriptorControl(srcSD, &srcSDControl, &srcSDRevision))
        {
            *err = GetLastError();
            ret = FALSE;
        }
        else
        {
            CStrP targetNameSecW(ConvertAllocUtf8ToWide(targetNameSec, -1));
            if (targetNameSecW == NULL)
            {
                *err = ERROR_NO_UNICODE_TRANSLATION;
                ret = FALSE;
            }
            else
            {
                BOOL inheritedDACL = /*(srcSDControl & SE_DACL_AUTO_INHERITED) != 0 &&*/ (srcSDControl & SE_DACL_PROTECTED) == 0; // SE_DACL_AUTO_INHERITED unfortunately is not always set (for example Total Commander clears it after moving a file, so we ignore it)
                DWORD attr = GetFileAttributesUtf8(targetNameSec);
                *err = SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                            DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
                                                (inheritedDACL ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                                            srcOwner, srcGroup, srcDACL, NULL);
                ret = *err == ERROR_SUCCESS;

                if (!ret)
                {
                    // if the owner and group cannot be changed (we do not have the rights in the directory - for example we only have "change" rights),
                    // check whether the owner and group are already set (that would not be an error)
                    PSID tgtOwner = NULL;
                    PSID tgtGroup = NULL;
                    PACL tgtDACL = NULL;
                    PSECURITY_DESCRIPTOR tgtSD = NULL;
                    BOOL tgtRead = GetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                                        DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                                        &tgtOwner, &tgtGroup, &tgtDACL, NULL, &tgtSD) == ERROR_SUCCESS;
                    // if the owner of the target file is not the current user, try to set it ("take ownership") - only
                // provided we have the right to write the owner so that we can write back the original owner afterwards
                BOOL ownerOfFile = FALSE;
                if (!tgtRead ||         // if the security info cannot be read from the target, the owner is most likely not the current user (the owner has unblocked read rights)
                    tgtOwner == NULL || // probably nonsense, the file must have some owner; if it happens, try to set the owner to the current user
                    CurrentProcessTokenUserValid && CurrentProcessTokenUser->User.Sid != NULL &&
                        !EqualSid(tgtOwner, CurrentProcessTokenUser->User.Sid))
                {
                    if (HaveWriteOwnerRight &&
                        CurrentProcessTokenUserValid && CurrentProcessTokenUser->User.Sid != NULL &&
                        SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                             CurrentProcessTokenUser->User.Sid, NULL, NULL, NULL) == ERROR_SUCCESS)
                    { // setting succeeded; we must retrieve 'tgtSD' again
                        ownerOfFile = TRUE;
                        if (tgtSD != NULL)
                            LocalFree(tgtSD);
                        tgtOwner = NULL;
                        tgtGroup = NULL;
                        tgtDACL = NULL;
                        tgtSD = NULL;
                        tgtRead = GetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                                       DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                                       &tgtOwner, &tgtGroup, &tgtDACL, NULL, &tgtSD) == ERROR_SUCCESS;
                    }
                }
                else
                {
                    ownerOfFile = tgtRead && tgtOwner != NULL && CurrentProcessTokenUserValid &&
                                  CurrentProcessTokenUser->User.Sid != NULL;
                }
                BOOL daclOK = FALSE;
                BOOL ownerOK = FALSE;
                BOOL groupOK = FALSE;
                if (ownerOfFile && CurrentProcessTokenUserValid && CurrentProcessTokenUser->User.Sid != NULL)
                { // we are the file owner -> the DACL can be written; try to allow owner/group/DACL write and set the required values
                    int allowChPermDACLSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) - sizeof(ACCESS_ALLOWED_ACE().SidStart) +
                                              GetLengthSid(CurrentProcessTokenUser->User.Sid) + 200 /* +200 bytes is just paranoia */;
                    char buff3[500];
                    PACL allowChPermDACL;
                    if (allowChPermDACLSize > 500)
                        allowChPermDACL = (PACL)malloc(allowChPermDACLSize);
                    else
                        allowChPermDACL = (PACL)buff3;
                    if (allowChPermDACL != NULL && InitializeAcl(allowChPermDACL, allowChPermDACLSize, ACL_REVISION) &&
                        AddAccessAllowedAce(allowChPermDACL, ACL_REVISION, READ_CONTROL | WRITE_DAC | WRITE_OWNER,
                                            CurrentProcessTokenUser->User.Sid) &&
                        SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                             DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                             NULL, NULL, allowChPermDACL, NULL) == ERROR_SUCCESS)
                    {
                        ownerOK = SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                                       OWNER_SECURITY_INFORMATION,
                                                       srcOwner, NULL, NULL, NULL) == ERROR_SUCCESS;
                        groupOK = SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                                       GROUP_SECURITY_INFORMATION,
                                                       NULL, srcGroup, NULL, NULL) == ERROR_SUCCESS;
                        daclOK = SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | (inheritedDACL ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                                                      NULL, NULL, srcDACL, NULL) == ERROR_SUCCESS;
                    }
                    if (allowChPermDACL != (PACL)buff3 && allowChPermDACL != NULL)
                        free(allowChPermDACL);
                }
                if (!ownerOK &&
                    (SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                          srcOwner, NULL, NULL, NULL) == ERROR_SUCCESS ||
                     tgtRead && (srcOwner == NULL && tgtOwner == NULL || // if the owner is already set, ignore a potential error while setting
                                 srcOwner != NULL && tgtOwner != NULL && EqualSid(srcOwner, tgtOwner))))
                {
                    ownerOK = TRUE;
                }
                if (!groupOK &&
                    (SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
                                          NULL, srcGroup, NULL, NULL) == ERROR_SUCCESS ||
                     tgtRead && (srcGroup == NULL && tgtGroup == NULL || // if the group is already set, ignore a potential error while setting
                                 srcGroup != NULL && tgtGroup != NULL && EqualSid(srcGroup, tgtGroup))))
                {
                    groupOK = TRUE;
                }
                if (!daclOK && // the DACL must be set last because it depends on the owner (CREATOR OWNER is replaced with the real owner, etc.)
                    SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | (inheritedDACL ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                                         NULL, NULL, srcDACL, NULL) == ERROR_SUCCESS)
                {
                    daclOK = TRUE;
                }
                if (ownerOK && groupOK && daclOK)
                {
                    ret = TRUE; // all three components are OK -> the whole thing is OK
                    *err = NO_ERROR;
                }
                if (tgtSD != NULL)
                    LocalFree(tgtSD);
            }
            if (attr != INVALID_FILE_ATTRIBUTES)
                SetFileAttributesUtf8(targetNameSec, attr);
            }
        }
    }
    if (srcSD != NULL)
        LocalFree(srcSD);
    return ret;
}

DWORD CompressFile(char* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if (attrs & FILE_ATTRIBUTE_COMPRESSED)
        return ret; // already compressed

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesUtf8(fileNameCrFile, attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    HANDLE file = HANDLES_Q(CreateFileUtf8(fileNameCrFile, FILE_READ_DATA | FILE_WRITE_DATA,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL));
    if (file == INVALID_HANDLE_VALUE)
        ret = GetLastError();
    else
    {
        USHORT state = COMPRESSION_FORMAT_DEFAULT;
        ULONG length;
        if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &state,
                             sizeof(USHORT), NULL, 0, &length, FALSE))
            ret = GetLastError();
        HANDLES(CloseHandle(file));
    }
    if (attrsChange)
        SetFileAttributesUtf8(fileNameCrFile, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}

DWORD UncompressFile(char* fileName, DWORD attrs)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_COMPRESSED) == 0)
        return ret; // not compressed

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~FILE_ATTRIBUTE_READONLY);
    }

    HANDLE file = HANDLES_Q(CreateFileW(fileNameW, FILE_READ_DATA | FILE_WRITE_DATA,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS,
                                        NULL));
    if (file == INVALID_HANDLE_VALUE)
        ret = GetLastError();
    else
    {
        USHORT state = COMPRESSION_FORMAT_NONE;
        ULONG length;
        if (!DeviceIoControl(file, FSCTL_SET_COMPRESSION, &state,
                             sizeof(USHORT), NULL, 0, &length, FALSE))
            ret = GetLastError();
        HANDLES(CloseHandle(file));
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}

DWORD MyEncryptFile(HWND hProgressDlg, char* fileName, DWORD attrs, DWORD finalAttrs,
                    CProgressDlgData& dlgData, BOOL& cancelOper, BOOL preserveDate)
{
    DWORD retEnc = ERROR_SUCCESS;
    cancelOper = FALSE;
    if (attrs & FILE_ATTRIBUTE_ENCRYPTED)
        return retEnc; // already encrypted

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    // if the file has the SYSTEM attribute, the EncryptFile API function reports "access denied"; handle it:
    if ((attrs & FILE_ATTRIBUTE_SYSTEM) && (finalAttrs & FILE_ATTRIBUTE_SYSTEM))
    { // if it has and will keep the SYSTEM attribute, ask the user whether they really mean it
        if (!dlgData.EncryptSystemAll)
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return retEnc;

            if (dlgData.SkipAllEncryptSystem)
                return retEnc;

            int ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_CONFIRMSFILEENCRYPT);
            data[2] = fileName;
            data[3] = LoadStr(IDS_ENCRYPTSFILE);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
            switch (ret)
            {
            case IDB_ALL:
                dlgData.EncryptSystemAll = TRUE;
            case IDYES:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllEncryptSystem = TRUE;
            case IDB_SKIP:
                return retEnc;

            case IDCANCEL:
            {
                cancelOper = TRUE;
                return retEnc;
            }
            }
        }
    }

    BOOL attrsChange = FALSE;
    if (attrs & (FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY))
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~(FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_READONLY));
    }
    if (preserveDate)
    {
        HANDLE file;
        file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING,
                                     (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                     NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME ftCreated, /*ftAccessed,*/ ftModified;
            GetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
            HANDLES(CloseHandle(file));

            if (!EncryptFileW(fileNameW))
                retEnc = GetLastError();

            file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING,
                                         (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                         NULL));
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
                HANDLES(CloseHandle(file));
            }
        }
        else
            retEnc = GetLastError();
    }
    else
    {
        if (!EncryptFileW(fileNameW))
            retEnc = GetLastError();
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return retEnc;
}

DWORD MyDecryptFile(char* fileName, DWORD attrs, BOOL preserveDate)
{
    DWORD ret = ERROR_SUCCESS;
    if ((attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
        return ret; // not encrypted

    // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* fileNameCrFile = fileName;
    char fileNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileNameCrFile, -1));
    if (fileNameW == NULL)
        return ERROR_NO_UNICODE_TRANSLATION;

    BOOL attrsChange = FALSE;
    if (attrs & FILE_ATTRIBUTE_READONLY)
    {
        attrsChange = TRUE;
        SetFileAttributesW(fileNameW, attrs & ~FILE_ATTRIBUTE_READONLY);
    }
    if (preserveDate)
    {
        HANDLE file;
        file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     NULL, OPEN_EXISTING,
                                     (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                     NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            FILETIME ftCreated, /*ftAccessed,*/ ftModified;
            GetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
            HANDLES(CloseHandle(file));

            if (!DecryptFileW(fileNameW, 0))
                ret = GetLastError();

            file = HANDLES_Q(CreateFileW(fileNameW, GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                                         NULL, OPEN_EXISTING,
                                         (attrs & FILE_ATTRIBUTE_DIRECTORY) ? FILE_FLAG_BACKUP_SEMANTICS : 0,
                                         NULL));
            if (file != INVALID_HANDLE_VALUE)
            {
                SetFileTime(file, &ftCreated, NULL /*&ftAccessed*/, &ftModified);
                HANDLES(CloseHandle(file));
            }
        }
        else
            ret = GetLastError();
    }
    else
    {
        if (!DecryptFileW(fileNameW, 0))
            ret = GetLastError();
    }
    if (attrsChange)
        SetFileAttributesW(fileNameW, attrs); // revert to the original attributes (on error the attributes would remain nonsensically changed)
    return ret;
}

BOOL CheckFileOrDirADS(const char* fileName, BOOL isDir, CQuadWord* adsSize, wchar_t*** streamNames,
                       int* streamNamesCount, BOOL* lowMemory, DWORD* winError,
                       DWORD bytesPerCluster, CQuadWord* adsOccupiedSpace,
                       BOOL* onlyDiscardableStreams)
{
    if (adsSize != NULL)
        adsSize->SetUI64(0);
    if (adsOccupiedSpace != NULL)
        adsOccupiedSpace->SetUI64(0);
    if (streamNames != NULL)
        *streamNames = NULL;
    if (streamNamesCount != NULL)
        *streamNamesCount = 0;
    if (lowMemory != NULL)
        *lowMemory = FALSE;
    if (winError != NULL)
        *winError = NO_ERROR;
    if (onlyDiscardableStreams != NULL)
        *onlyDiscardableStreams = TRUE;

    if (DynNtQueryInformationFile != NULL) // "always true"
    {
        // if the path ends with a space or dot, we must append '\\', otherwise CreateFile
        // trims the spaces/dots and works with a different path
        const char* fileNameCrFile = fileName;
        char fileNameCrFileCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(fileNameCrFile, fileNameCrFileCopy);

        HANDLE file = HANDLES_Q(CreateFileUtf8(fileNameCrFile, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                           NULL, OPEN_EXISTING,
                                           isDir ? FILE_FLAG_BACKUP_SEMANTICS : 0, NULL));
        if (file == INVALID_HANDLE_VALUE)
        {
            if (winError != NULL)
                *winError = GetLastError();
            return FALSE;
        }

        // get stream info
        NTSTATUS uStatus;
        IO_STATUS_BLOCK ioStatus;
        BYTE buffer[65535]; // Windows XP cannot handle more than 65535 (no idea why)
        uStatus = DynNtQueryInformationFile(file, &ioStatus, buffer, sizeof(buffer), FileStreamInformation);
        HANDLES(CloseHandle(file));
        if (uStatus != 0 /* anything other than success is an error (including warnings) */)
        {
            if (winError != NULL)
            {
                if (uStatus == STATUS_BUFFER_OVERFLOW)
                    *winError = ERROR_INSUFFICIENT_BUFFER;
                else
                    *winError = LsaNtStatusToWinError(uStatus);
            }
            return FALSE;
        }

        TDirectArray<wchar_t*>* streamNamesAux = NULL;
        if (streamNames != NULL)
        {
            streamNamesAux = new TDirectArray<wchar_t*>(10, 100);
            if (streamNamesAux == NULL)
            {
                if (lowMemory != NULL)
                    *lowMemory = TRUE;
                TRACE_E(LOW_MEMORY);
                return FALSE;
            }
        }

        // iterate through the streams
        PFILE_STREAM_INFORMATION psi = (PFILE_STREAM_INFORMATION)buffer;
        BOOL ret = FALSE;
        BOOL lowMem = FALSE;
        if (ioStatus.Information > 0) // check whether we obtained any data at all
        {
            while (1)
            {
                if (psi->NameLength != 7 * 2 || _memicmp(psi->Name, L"::$DATA", 7 * 2)) // ignore default stream
                {
                    ret = TRUE;
                    if (adsSize != NULL)
                        *adsSize += CQuadWord(psi->Size.LowPart, psi->Size.HighPart); // sum of the total size of all alternate data streams
                    if (adsOccupiedSpace != NULL && bytesPerCluster != 0)
                    {
                        CQuadWord fileSize(psi->Size.LowPart, psi->Size.HighPart);
                        *adsOccupiedSpace += fileSize - ((fileSize - CQuadWord(1, 0)) % CQuadWord(bytesPerCluster, 0)) +
                                             CQuadWord(bytesPerCluster - 1, 0);
                    }

                    if (onlyDiscardableStreams != NULL)
                    {                                                                                                                      // if an ADS appears that is unknown or indispensable, switch 'onlyDiscardableStreams' to FALSE
                        if ((psi->NameLength < 29 * 2 || _memicmp(psi->Name, L":\x05Q30lsldxJoudresxAaaqpcawXc:", 29 * 2) != 0) &&         // Win2K thumbnail in an ADS: 5952 bytes (depends on JPEG compression)
                            (psi->NameLength < 40 * 2 || _memicmp(psi->Name, L":{4c8cc155-6c1e-11d1-8e41-00c04fb9386d}:", 40 * 2) != 0) && // Win2K thumbnail in an ADS: 0 bytes
                            (psi->NameLength < 9 * 2 || _memicmp(psi->Name, L":KAVICHS:", 9 * 2) != 0))                                    // Kaspersky antivirus: 36/68 bytes
                        {
                            *onlyDiscardableStreams = FALSE;
                        }
                    }

                    if (streamNamesAux != NULL) // collecting Unicode names of alternate data streams
                    {
                        wchar_t* str = (wchar_t*)malloc(psi->NameLength + 2);
                        if (str != NULL)
                        {
                            memcpy(str, psi->Name, psi->NameLength);
                            str[psi->NameLength / 2] = 0;
                            streamNamesAux->Add(str);
                            if (!streamNamesAux->IsGood())
                            {
                                free(str);
                                streamNamesAux->ResetState();
                                if (lowMemory != NULL)
                                    *lowMemory = TRUE;
                                lowMem = TRUE;
                                break;
                            }
                        }
                        else
                        {
                            if (lowMemory != NULL)
                                *lowMemory = TRUE;
                            lowMem = TRUE;
                            TRACE_E(LOW_MEMORY);
                            break;
                        }
                    }
                    else
                    {
                        if (adsSize == NULL && adsOccupiedSpace == NULL && onlyDiscardableStreams == NULL)
                            break; // nothing else to find out (no names, stream sizes, or only-discardable-streams collected)
                    }
                }
                if (psi->NextEntry == 0)
                    break;
                psi = (PFILE_STREAM_INFORMATION)((BYTE*)psi + psi->NextEntry); // move to next item
            }
        }
        if (streamNamesAux != NULL)
        {
            if (lowMem || !ret) // lack of memory or no ADS, release all names
            {
                int i;
                for (i = 0; i < streamNamesAux->Count; i++)
                    free(streamNamesAux->At(i));
            }
            else // everything OK, pass the names to the caller
            {
                if (streamNamesCount != NULL)
                    *streamNamesCount = streamNamesAux->Count;
                *streamNames = streamNamesAux->GetData();
                streamNamesAux->DetachArray();
            }
            delete streamNamesAux;
        }
        return ret;
    }
    return FALSE;
}

BOOL DeleteAllADS(HANDLE file, const char* fileName)
{
    if (DynNtQueryInformationFile != NULL) // "always true"
    {
        // get stream info
        NTSTATUS uStatus;
        IO_STATUS_BLOCK ioStatus;
        BYTE buffer[65535]; // Windows XP cannot handle more than 65535 (no idea why)
        uStatus = DynNtQueryInformationFile(file, &ioStatus, buffer, sizeof(buffer), FileStreamInformation);
        if (uStatus != 0 /* anything other than success is an error (including warnings) */)
        {
            DWORD err;
            if (uStatus == STATUS_BUFFER_OVERFLOW)
                err = ERROR_INSUFFICIENT_BUFFER;
            else
                err = LsaNtStatusToWinError(uStatus);
            TRACE_I("DeleteAllADS(" << fileName << "): NtQueryInformationFile failed: " << GetErrorText(err));
            return FALSE;
        }

        // iterate through the streams
        PFILE_STREAM_INFORMATION psi = (PFILE_STREAM_INFORMATION)buffer;
        if (ioStatus.Information > 0) // verify that we received any data at all
        {
            WCHAR adsFullName[2 * MAX_PATH];
            adsFullName[0] = 0;
            WCHAR* adsPart = NULL;
            int adsPartSize = 0;
            while (1)
            {
                if (psi->NameLength != 7 * 2 || _memicmp(psi->Name, L"::$DATA", 7 * 2)) // ignore default stream
                {
                    if (adsFullName[0] == 0) // convert the file name only when needed for the first time to save CPU time
                    {
                        if (ConvertA2U(fileName, -1, adsFullName, 2 * MAX_PATH) == 0)
                            return FALSE; // "always false"
                        adsPart = adsFullName + wcslen(adsFullName);
                        adsPartSize = (int)((adsFullName + 2 * MAX_PATH) - adsPart);
                        if (adsPartSize > 0)
                        {
                            *adsPart++ = L':';
                            adsPartSize--;
                        }
                        else
                            return FALSE; // "always false"
                    }
                    WCHAR* start = (WCHAR*)psi->Name;
                    WCHAR* nameEnd = (WCHAR*)((char*)psi->Name + psi->NameLength);
                    if (start < nameEnd && *start == L':')
                        start++;
                    WCHAR* end = start;
                    while (end < nameEnd && *end != L':')
                        end++;
                    if (end - start >= adsPartSize)
                    {
                        TRACE_I("DeleteAllADS(" << fileName << "): too long ADS name!");
                        return FALSE;
                    }
                    if (end > start)
                    {
                        memcpy(adsPart, start, (end - start) * sizeof(WCHAR));
                        adsPart[end - start] = 0;
                        if (!DeleteFileW(adsFullName))
                        {
                            DWORD err = GetLastError();
                            TRACE_IW(L"DeleteAllADS(" << adsFullName << L"): DeleteFile has failed: " << GetErrorTextW(err));
                            return FALSE;
                        }
                    }
                }
                if (psi->NextEntry == 0)
                    break;
                psi = (PFILE_STREAM_INFORMATION)((BYTE*)psi + psi->NextEntry); // move to next item
            }
        }
    }
    return TRUE;
}

void MyStrCpyNW(wchar_t* s1, wchar_t* s2, int maxChars)
{
    if (maxChars == 0)
        return;
    while (--maxChars && *s2 != 0)
        *s1++ = *s2++;
    *s1 = 0;
}

void CutADSNameSuffix(char* s)
{
    char* end = strrchr(s, ':');
    if (end != NULL && stricmp(end, ":$DATA") == 0)
        *end = 0;
}

// conversion to the extended-path variant, see the MSDN article "File Name Conventions"
void DoLongName(char* buf, const char* name, int bufSize)
{
    if (*name == '\\')
        _snprintf_s(buf, bufSize, _TRUNCATE, "\\\\?\\UNC%s", name + 1); // UNC
    else
        _snprintf_s(buf, bufSize, _TRUNCATE, "\\\\?\\%s", name); // standard path
}

BOOL SalSetFilePointer(HANDLE file, const CQuadWord& offset)
{
    LONG lo = offset.LoDWord;
    LONG hi = offset.HiDWord;
    lo = SetFilePointer(file, lo, &hi, FILE_BEGIN);
    return (lo != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR) &&
           lo == (LONG)offset.LoDWord && hi == (LONG)offset.HiDWord;
}

#define RETRYCOPY_TAIL_MINSIZE (32 * 1024) // at least two blocks of this size are verified at the end of the file tested in CheckTailOfOutFile(); afterwards the block size grows up to ASYNC_COPY_BUF_SIZE (if reading is fast enough); NOTE: must be <= ASYNC_COPY_BUF_SIZE
#define RETRYCOPY_TESTINGTIME 3000         // duration of the CheckTailOfOutFile() test in [ms]

void CheckTailOfOutFileShowErr(const char* txt)
{
    DWORD err = GetLastError();
    TRACE_I("CheckTailOfOutFile(): " << txt << " Error: " << GetErrorText(err));
}

BOOL CheckTailOfOutFile(CAsyncCopyParams* asyncPar, HANDLE in, HANDLE out, const CQuadWord& offset,
                        const CQuadWord& curInOffset, BOOL ignoreReadErrOnOut)
{
    char* bufIn = (char*)malloc(ASYNC_COPY_BUF_SIZE);
    char* bufOut = (char*)malloc(ASYNC_COPY_BUF_SIZE);

    DWORD startTime = GetTickCount();
    DWORD rutineStartTime = startTime;
    CQuadWord lastOffset = offset;
    int roundNum = 1;
    DWORD curBufSize = RETRYCOPY_TAIL_MINSIZE;
    DWORD lastRoundStartTime = 0;
    DWORD lastRoundBufSize = 0;
    BOOL searchLongLastingBlock = TRUE;
    BOOL ok;
    while (1)
    {
        DWORD roundStartTime = GetTickCount();
        ok = FALSE;
        CQuadWord start;
        start.Value = lastOffset.Value > curBufSize ? lastOffset.Value - curBufSize : 0;
        DWORD size = (DWORD)(lastOffset.Value - start.Value);
        if (size == 0)
        {
            ok = TRUE;
            break; // nothing to verify
        }
#ifdef WORKER_COPY_DEBUG_MSG
        TRACE_I("CheckTailOfOutFile(): check: " << start.Value << " - " << lastOffset.Value << ", size: " << size);
#endif // WORKER_COPY_DEBUG_MSG
        if (asyncPar == NULL)
        {
            if (SalSetFilePointer(in, start))
            { // set the 'start' offset in the input
                if (SalSetFilePointer(out, start))
                { // set the 'start' offset in the output
                    DWORD read;
                    if (ReadFile(out, bufOut, size, &read, NULL) && read == size)
                    { // read 'size' bytes into the output buffer (fails if opened without read access)
                        if (ReadFile(in, bufIn, size, &read, NULL) && read == size)
                        {                                         // read 'size' bytes into the input buffer
                            if (memcmp(bufIn, bufOut, size) == 0) // compare whether the input/output buffers match
                                ok = TRUE;
                            else
                                TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file, tail without differences: " << (offset.Value - lastOffset.Value));
                        }
                        else
                            CheckTailOfOutFileShowErr("Unable to read IN file.");
                    }
                    else
                    {
                        if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                        {
                            CheckTailOfOutFileShowErr("Unable to read OUT file, but it was not broken, so it's no problem.");
                            ok = TRUE;
                            break;
                        }
                        else
                            CheckTailOfOutFileShowErr("Unable to read OUT file.");
                    }
                }
                else
                    CheckTailOfOutFileShowErr("Unable to set file pointer to start offset in OUT file.");
            }
            else
                CheckTailOfOutFileShowErr("Unable to set file pointer to start offset in IN file.");
        }
        else
        {
            // asynchronously read the block starting at 'start' of length 'size' bytes from in and out, then compare
            DWORD readOut;
            if ((ReadFile(out, bufOut, size, NULL,
                          asyncPar->InitOverlappedWithOffset(0, start)) ||
                 GetLastError() == ERROR_IO_PENDING) &&
                GetOverlappedResult(out, asyncPar->GetOverlapped(0), &readOut, TRUE))
            {
                DWORD readIn;
                if ((ReadFile(in, bufIn, size, NULL,
                              asyncPar->InitOverlappedWithOffset(1, start)) ||
                     GetLastError() == ERROR_IO_PENDING) &&
                    GetOverlappedResult(in, asyncPar->GetOverlapped(1), &readIn, TRUE))
                {
                    if (readOut != size || readIn != size ||
                        memcmp(bufIn, bufOut, size) != 0) // compare whether the input/output buffers match
                    {
                        TRACE_I("CheckTailOfOutFile(): tail of target file is different from source file (async), tail without differences: " << (offset.Value - lastOffset.Value));
                    }
                    else
                        ok = TRUE;
                }
                else
                    CheckTailOfOutFileShowErr("Unable to read IN file (async).");
            }
            else
            {
                if (ignoreReadErrOnOut) // if the input file failed earlier, ignore that we cannot read the output (input was reopened, output has remained open)
                {
                    CheckTailOfOutFileShowErr("Unable to read OUT file (async), but it was not broken, so it's no problem.");
                    ok = TRUE;
                    break;
                }
                else
                    CheckTailOfOutFileShowErr("Unable to read OUT file (async).");
            }
        }
        if (!ok)
            break;
        lastOffset = start;
        DWORD curBufSizeBackup = curBufSize;
        if (roundNum > 1)
        {
            DWORD ti = GetTickCount();
            if (searchLongLastingBlock)
            {
                DWORD t1 = roundStartTime - lastRoundStartTime;
                DWORD t2 = ti - roundStartTime;
                if (roundNum == 2 && t1 > 300 && 10 * t2 < t1) // first iteration waits for the disk/network to be ready, shift the start time (so we spend the configured time reading instead of just waiting)
                {
#ifdef WORKER_COPY_DEBUG_MSG
                    TRACE_I("CheckTailOfOutFile(): detected long lasting first block, start time shifted by " << ((roundStartTime - startTime) / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                    startTime = roundStartTime;
                }
                else
                {
                    if (t2 > 1000 && ((curBufSize * 10) / lastRoundBufSize) * t1 < t2)
                    { // unexpectedly long block read, likely waiting for disk "verification" or similar; ignore this block once so the overall check still behaves normally
                        searchLongLastingBlock = FALSE;
                        DWORD sh = t2 - ((unsigned __int64)curBufSize * t1) / lastRoundBufSize;
#ifdef WORKER_COPY_DEBUG_MSG
                        TRACE_I("CheckTailOfOutFile(): detected long lasting block, start time shifted by " << (sh / 1000.0) << " secs.");
#endif // WORKER_COPY_DEBUG_MSG
                        startTime += sh;
                    }
                }
            }
            if (ti - startTime > RETRYCOPY_TESTINGTIME)
                break; // we have been reading long enough; stop after the mandatory two rounds
            if (ti - roundStartTime < 300 && curBufSize < ASYNC_COPY_BUF_SIZE)
            { // when reading is fast enough, enlarge the buffer to avoid excessive reverse seeking (toward the beginning of the file)
                curBufSize *= 2;
                if (curBufSize > ASYNC_COPY_BUF_SIZE)
                    curBufSize = ASYNC_COPY_BUF_SIZE;
            }
        }
        roundNum++;
        lastRoundStartTime = roundStartTime;
        lastRoundBufSize = curBufSizeBackup;
    }

    if (ok && asyncPar == NULL) // reposition input/output to required offsets
    {
        if (!SalSetFilePointer(in, curInOffset))
        {
            CheckTailOfOutFileShowErr("Unable to set file pointer back to current offset in IN file.");
            ok = FALSE;
        }
        if (ok && !SalSetFilePointer(out, offset))
        {
            CheckTailOfOutFileShowErr("Unable to set file pointer back to current offset in OUT file.");
            ok = FALSE;
        }
    }
#ifdef WORKER_COPY_DEBUG_MSG
    if (!ok)
        TRACE_I("CheckTailOfOutFile(): aborting Retry...");
    else
    {
        TRACE_I("CheckTailOfOutFile(): " << (offset.Value - lastOffset.Value) / 1024.0 << " KB tested in " << (GetTickCount() - rutineStartTime) / 1000.0 << " secs (clear read time: " << (GetTickCount() - startTime) / 1000.0 << " secs).");
    }
#endif // WORKER_COPY_DEBUG_MSG
    free(bufIn);
    free(bufOut);
    return ok;
}

// copies ADS into the newly created file/directory
// returns FALSE only when cancelled; success + Skip both return TRUE; Skip sets 'skip'
// (when not NULL) to TRUE
// 'optimalBufferSize' is the pre-computed optimal buffer size (0 = use default based on script flags)
BOOL DoCopyADS(HWND hProgressDlg, const char* sourceName, BOOL isDir, const char* targetName,
               CQuadWord const& totalDone, CQuadWord& operDone, CQuadWord const& operTotal,
               CProgressDlgData& dlgData, COperations* script, BOOL* skip, void* buffer,
               int optimalBufferSize = 0)
{
    BOOL doCopyADSRet = TRUE;
    BOOL lowMemory;
    DWORD adsWinError;
    wchar_t** streamNames;
    int streamNamesCount;
    BOOL skipped = FALSE;
    CQuadWord lastTransferredFileSize, finalTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);
    finalTransferredFileSize = lastTransferredFileSize;
    if (operTotal > operDone) // it should always be at least equal, but we play it safe...
        finalTransferredFileSize += (operTotal - operDone);

COPY_ADS_AGAIN:

    if (CheckFileOrDirADS(sourceName, isDir, NULL, &streamNames, &streamNamesCount,
                          &lowMemory, &adsWinError, 0, NULL, NULL) &&
        !lowMemory && streamNames != NULL)
    {                                  // we have the list of ADS, let's try to copy them to the target file/directory
        wchar_t srcName[2 * MAX_PATH]; // MAX_PATH for the file name as well as the ADS name (no idea what the actual maximum lengths are)
        wchar_t tgtName[2 * MAX_PATH];
        char longSourceName[MAX_PATH + 100];
        char longTargetName[MAX_PATH + 100];
        DoLongName(longSourceName, sourceName, MAX_PATH + 100);
        DoLongName(longTargetName, targetName, MAX_PATH + 100);
        if (!ConvertUtf8ToWide(longSourceName, -1, srcName, 2 * MAX_PATH))
            srcName[0] = 0;
        if (!ConvertUtf8ToWide(longTargetName, -1, tgtName, 2 * MAX_PATH))
            tgtName[0] = 0;
        wchar_t* srcEnd = srcName + lstrlenW(srcName);
        if (srcEnd > srcName && *(srcEnd - 1) == L'\\')
            *--srcEnd = 0;
        wchar_t* tgtEnd = tgtName + lstrlenW(tgtName);
        if (tgtEnd > tgtName && *(tgtEnd - 1) == L'\\')
            *--tgtEnd = 0;

        // Use pre-computed buffer size if provided, otherwise fall back to default logic
        int bufferSize = (optimalBufferSize > 0) ? optimalBufferSize :
            (script->RemovableSrcDisk || script->RemovableTgtDisk ? REMOVABLE_DISK_COPY_BUFFER : OPERATION_BUFFER);

        char nameBuf[2 * MAX_PATH];
        BOOL endProcessing = FALSE;
        CQuadWord operationDone;
        int i;
        for (i = 0; i < streamNamesCount; i++)
        {
            MyStrCpyNW(srcEnd, streamNames[i], (int)(2 * MAX_PATH - (srcEnd - srcName)));
            MyStrCpyNW(tgtEnd, streamNames[i], (int)(2 * MAX_PATH - (tgtEnd - tgtName)));

        COPY_AGAIN_ADS:

            operationDone = CQuadWord(0, 0);
            int limitBufferSize = bufferSize;
            script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone, &limitBufferSize, bufferSize);

            BOOL doNextFile = FALSE;
            while (1)
            {
                HANDLE in = CreateFileW(srcName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                               __hoCreateFile, in, GetLastError(), TRUE);
                if (in != INVALID_HANDLE_VALUE)
                {
                    CQuadWord fileSize;
                    fileSize.LoDWord = GetFileSize(in, &fileSize.HiDWord);
                    if (fileSize.LoDWord == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
                    {
                        DWORD err = GetLastError();
                        TRACE_E("GetFileSize(some ADS of " << sourceName << "): unexpected error: " << GetErrorText(err));
                        fileSize.SetUI64(0);
                    }

                    while (1)
                    {
                        HANDLE out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                       __hoCreateFile, out, GetLastError(), TRUE);

                        BOOL canOverwriteMACADSs = TRUE;

                    COPY_OVERWRITE:

                        if (out != INVALID_HANDLE_VALUE)
                        {
                            canOverwriteMACADSs = FALSE;

                            // if possible, pre-allocate the required space (avoids disk fragmentation and smooths writes to floppies)
                            BOOL wholeFileAllocated = FALSE;
                            if (fileSize > CQuadWord(limitBufferSize, 0) && // pointless to pre-allocate below the copy buffer size
                                fileSize < CQuadWord(0, 0x80000000))        // file size must be positive (otherwise seeking fails � values above 8 EB, so practically never)
                            {
                                BOOL fatal = TRUE;
                                BOOL ignoreErr = FALSE;
                                if (SalSetFilePointer(out, fileSize))
                                {
                                    if (SetEndOfFile(out))
                                    {
                                        if (SetFilePointer(out, 0, NULL, FILE_BEGIN) == 0)
                                        {
                                            fatal = FALSE;
                                            wholeFileAllocated = TRUE;
                                        }
                                    }
                                    else
                                    {
                                        if (GetLastError() == ERROR_DISK_FULL)
                                            ignoreErr = TRUE; // low disk space
                                    }
                                }
                                if (fatal)
                                {
                                    if (!ignoreErr)
                                    {
                                        DWORD err = GetLastError();
                                        TRACE_E("DoCopyADS(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! GetLastError(): " << GetErrorText(err));
                                    }

                                    // try truncating the file to zero so closing it does not trigger unnecessary writes
                                    SetFilePointer(out, 0, NULL, FILE_BEGIN);
                                    SetEndOfFile(out);

                                    HANDLES(CloseHandle(out));
                                    out = INVALID_HANDLE_VALUE;
                                    if (DeleteFileW(tgtName))
                                    {
                                        out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, out, GetLastError(), TRUE);
                                        if (out == INVALID_HANDLE_VALUE)
                                            goto CREATE_ERROR_ADS;
                                    }
                                    else
                                        goto CREATE_ERROR_ADS;
                                }
                            }

                            DWORD read;
                            DWORD written;
                            while (1)
                            {
                                if (ReadFile(in, buffer, limitBufferSize, &read, NULL))
                                {
                                    if (read == 0)
                                        break;                                                     // EOF
                                    if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                    {
                                    COPY_ERROR_ADS:

                                        if (in != NULL)
                                            HANDLES(CloseHandle(in));
                                        if (out != NULL)
                                        {
                                            if (wholeFileAllocated)
                                                SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                            HANDLES(CloseHandle(out));
                                        }
                                        DeleteFileW(tgtName);
                                        doCopyADSRet = FALSE;
                                        endProcessing = TRUE;
                                        break;
                                    }

                                    while (1)
                                    {
                                        if (WriteFile(out, buffer, read, &written, NULL) && read == written)
                                            break;

                                    WRITE_ERROR_ADS:

                                        DWORD err;
                                        err = GetLastError();

                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                        if (*dlgData.CancelWorker)
                                            goto COPY_ERROR_ADS;

                                        if (dlgData.SkipAllFileADSWrite)
                                            goto SKIP_COPY_ADS;

                                        int ret;
                                        ret = IDCANCEL;
                                        char* data[4];
                                        data[0] = (char*)&ret;
                                        data[1] = LoadStr(IDS_ERRORWRITINGADS);
                                        ConvertWideToUtf8(tgtName, -1, nameBuf, 2 * MAX_PATH);
                                        nameBuf[2 * MAX_PATH - 1] = 0;
                                        CutADSNameSuffix(nameBuf);
                                        data[2] = nameBuf;
                                        if (err == NO_ERROR && read != written)
                                            err = ERROR_DISK_FULL;
                                        data[3] = GetErrorText(err);
                                        SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                        switch (ret)
                                        {
                                        case IDRETRY: // on a network we must reopen the handle; local access would not allow sharing
                                        {
                                            if (in == NULL && out == NULL)
                                            {
                                                DeleteFileW(tgtName);
                                                goto COPY_AGAIN_ADS;
                                            }
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out);     // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out)); // close the invalid handle
                                            }
                                            out = CreateFileW(tgtName, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_ALWAYS,
                                                              FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                            HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                                           __hoCreateFile, out, GetLastError(), TRUE);
                                            if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                            {
                                                LONG lo, hi;
                                                lo = GetFileSize(out, (DWORD*)&hi);
                                                if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR ||
                                                    CQuadWord(lo, hi) < operationDone ||
                                                    !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                                                { // cannot determine the size or the file is too small; restart the entire copy
                                                    HANDLES(CloseHandle(in));
                                                    HANDLES(CloseHandle(out));
                                                    DeleteFileW(tgtName);
                                                    goto COPY_AGAIN_ADS;
                                                }
                                            }
                                            else // still cannot open; problem persists
                                            {
                                                out = NULL;
                                                goto WRITE_ERROR_ADS;
                                            }
                                            break;
                                        }

                                        case IDB_SKIPALL:
                                            dlgData.SkipAllFileADSWrite = TRUE;
                                        case IDB_SKIP:
                                        {
                                        SKIP_COPY_ADS:

                                            if (in != NULL)
                                                HANDLES(CloseHandle(in));
                                            if (out != NULL)
                                            {
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out));
                                            }
                                            DeleteFileW(tgtName);
                                            if (skip != NULL)
                                                *skip = TRUE;
                                            skipped = TRUE;
                                            endProcessing = TRUE;
                                            break;
                                        }

                                        case IDCANCEL:
                                            goto COPY_ERROR_ADS;
                                        }
                                        if (endProcessing)
                                            break;
                                    }
                                    if (endProcessing)
                                        break;
                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto COPY_ERROR_ADS;

                                    script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

                                    if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    operationDone += CQuadWord(read, 0);
                                    SetProgressWithoutSuspend(hProgressDlg, CaclProg(operDone + operationDone, operTotal),
                                                              CaclProg(totalDone + operDone + operationDone, script->TotalSize),
                                                              dlgData);

                                    if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
                                    {                                                              // worker resumes and fetch a fresh copy buffer size
                                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                        script->GetNewBufSize(&limitBufferSize, bufferSize);
                                    }
                                }
                                else
                                {
                                READ_ERROR_ADS:

                                    DWORD err;
                                    err = GetLastError();
                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto COPY_ERROR_ADS;

                                    if (dlgData.SkipAllFileADSRead)
                                        goto SKIP_COPY_ADS;

                                    int ret = IDCANCEL;
                                    char* data[4];
                                    data[0] = (char*)&ret;
                                    data[1] = LoadStr(IDS_ERRORREADINGADS);
                                    ConvertWideToUtf8(srcName, -1, nameBuf, 2 * MAX_PATH);
                                    nameBuf[2 * MAX_PATH - 1] = 0;
                                    CutADSNameSuffix(nameBuf);
                                    data[2] = nameBuf;
                                    data[3] = GetErrorText(err);
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                                    switch (ret)
                                    {
                                    case IDRETRY:
                                    {
                                        if (in != NULL)
                                            HANDLES(CloseHandle(in)); // close the invalid handle

                                        in = CreateFileW(srcName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        HANDLES_ADD_EX(__otQuiet, in != INVALID_HANDLE_VALUE, __htFile,
                                                       __hoCreateFile, in, GetLastError(), TRUE);
                                        if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                                        {
                                            LONG lo, hi;
                                            lo = GetFileSize(in, (DWORD*)&hi);
                                            if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR ||
                                                CQuadWord(lo, hi) < operationDone ||
                                                !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                                            { // cannot obtain size or the file is too small; restart the entire operation
                                                HANDLES(CloseHandle(in));
                                                if (wholeFileAllocated)
                                                    SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                                                HANDLES(CloseHandle(out));
                                                DeleteFileW(tgtName);
                                                goto COPY_AGAIN_ADS;
                                            }
                                        }
                                        else // still cannot open; problem persists
                                        {
                                            in = NULL;
                                            goto READ_ERROR_ADS;
                                        }
                                        break;
                                    }
                                    case IDB_SKIPALL:
                                        dlgData.SkipAllFileADSRead = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_COPY_ADS;
                                    case IDCANCEL:
                                        goto COPY_ERROR_ADS;
                                    }
                                }
                            }
                            if (endProcessing)
                                break;

                            if (wholeFileAllocated &&     // the entire target layout was pre-allocated
                                operationDone < fileSize) // and the source file shrank
                            {
                                if (!SetEndOfFile(out)) // trim it here
                                {
                                    written = read = 0;
                                    goto WRITE_ERROR_ADS;
                                }
                            }

                            // commented out because it sets the time of the file/directory that owns the ADS instead of the ADS timestamps
                            //              FILETIME creation, lastAccess, lastWrite;
                            //              GetFileTime(in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);
                            //              SetFileTime(out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite);

                            HANDLES(CloseHandle(in));
                            if (!HANDLES(CloseHandle(out))) // even after a failed call we assume the handle is closed,
                            {                               // see /viewtopic.php?f=6&t=8455
                                in = out = NULL;            // (reports that the target file can be deleted, so its handle was not left open)
                                written = read = 0;
                                goto WRITE_ERROR_ADS;
                            }

                            // commented out because it sets the attributes of the file/directory that owns the ADS instead of the ADS attributes
                            //              DWORD attr = DynGetFileAttributesW(srcName);
                            //              if (attr != INVALID_FILE_ATTRIBUTES) DynSetFileAttributesW(tgtName, attr);

                            operDone += operationDone;
                            lastTransferredFileSize += operationDone;
                            doNextFile = TRUE;
                        }
                        else
                        {
                        CREATE_ERROR_ADS:

                            DWORD err = GetLastError();

                            // Macintosh compatibility: NTFS automatically creates ADS entries myFile:Afp_Resource and myFile:Afp_AfpInfo,
                            // overwrite them silently with the versions from the source file
                            if (canOverwriteMACADSs &&
                                (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) &&
                                (_wcsnicmp(streamNames[i], L":Afp_Resource", 13) == 0 &&
                                     (streamNames[i][13] == 0 || streamNames[i][13] == L':') ||
                                 _wcsnicmp(streamNames[i], L":Afp_AfpInfo", 12) == 0 &&
                                     (streamNames[i][12] == 0 || streamNames[i][12] == L':')))
                            {
                                out = CreateFileW(tgtName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                  FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                               __hoCreateFile, out, GetLastError(), TRUE);

                                canOverwriteMACADSs = FALSE;
                                goto COPY_OVERWRITE;
                            }

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN2_ADS;

                            if (dlgData.SkipAllFileADSOpenOut)
                                goto SKIP_OPEN_OUT_ADS;

                            if (dlgData.IgnoreAllADSOpenOutErr)
                                goto IGNORE_OPENOUTADS;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERROROPENINGADS);
                            ConvertWideToUtf8(tgtName, -1, nameBuf, 2 * MAX_PATH);
                            nameBuf[2 * MAX_PATH - 1] = 0;
                            CutADSNameSuffix(nameBuf);
                            data[2] = nameBuf;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllADSOpenOutErr = TRUE; // break is intentionally omitted here
                            case IDB_IGNORE:
                            {
                            IGNORE_OPENOUTADS:

                                HANDLES(CloseHandle(in));
                                operDone += fileSize;
                                lastTransferredFileSize += fileSize;
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone + operDone);
                                doNextFile = TRUE;
                                break;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileADSOpenOut = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OPEN_OUT_ADS:

                                HANDLES(CloseHandle(in));
                                if (skip != NULL)
                                    *skip = TRUE;
                                skipped = TRUE;
                                endProcessing = TRUE;
                                break;
                            }

                            case IDCANCEL:
                            {
                            CANCEL_OPEN2_ADS:

                                HANDLES(CloseHandle(in));
                                doCopyADSRet = FALSE;
                                endProcessing = TRUE;
                                break;
                            }
                            }
                        }
                        if (doNextFile || endProcessing)
                            break;
                    }
                }
                else
                {
                    DWORD err = GetLastError();
                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }

                    if (dlgData.SkipAllFileADSOpenIn)
                        goto SKIP_OPEN_IN_ADS;

                    int ret;
                    ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_ERROROPENINGADS);
                    ConvertWideToUtf8(srcName, -1, nameBuf, 2 * MAX_PATH);
                    nameBuf[2 * MAX_PATH - 1] = 0;
                    CutADSNameSuffix(nameBuf);
                    data[2] = nameBuf;
                    data[3] = GetErrorText(err);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;

                    case IDB_SKIPALL:
                        dlgData.SkipAllFileADSOpenIn = TRUE;
                    case IDB_SKIP:
                    {
                    SKIP_OPEN_IN_ADS:

                        if (skip != NULL)
                            *skip = TRUE;
                        skipped = TRUE;
                        endProcessing = TRUE;
                        break;
                    }

                    case IDCANCEL:
                    {
                        doCopyADSRet = FALSE;
                        endProcessing = TRUE;
                        break;
                    }
                    }
                }
                if (doNextFile || endProcessing)
                    break;
            }
            if (endProcessing)
                break;
        }

        for (i = 0; i < streamNamesCount; i++)
            free(streamNames[i]);
        free(streamNames);
    }
    else
    {
        if (adsWinError != NO_ERROR) // display the Windows error (low-memory warning goes only to TRACE_E)
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.IgnoreAllADSReadErr)
                goto IGNORE_ADS;

            int ret;
            ret = IDCANCEL;
            char* data[3];
            data[0] = (char*)&ret;
            data[1] = (char*)sourceName;
            data[2] = GetErrorText(adsWinError);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 6, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                goto COPY_ADS_AGAIN;

            case IDB_IGNOREALL:
                dlgData.IgnoreAllADSReadErr = TRUE; // break is intentionally omitted here
            case IDB_IGNORE:
            {
            IGNORE_ADS:

                script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone + operTotal, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (lowMemory)
            doCopyADSRet = FALSE; // lack of memory -> cancel the operation
    }
    if (doCopyADSRet && skipped)
    {
        script->SetTFSandProgressSize(finalTransferredFileSize, totalDone + operTotal);

        SetProgress(hProgressDlg, 0, CaclProg(totalDone + operTotal, script->TotalSize), dlgData);
    }
    return doCopyADSRet;
}

HANDLE SalCreateFileEx(const char* fileName, DWORD desiredAccess,
                       DWORD shareMode, DWORD flagsAndAttributes, BOOL* encryptionNotSupported)
{
    CStrP fileNameW(ConvertAllocUtf8ToWide(fileName, -1));
    if (fileNameW == NULL)
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                       CREATE_NEW, flagsAndAttributes, NULL));
    if (out == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (encryptionNotSupported != NULL && (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
        { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
            out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                        CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL));
            if (out != INVALID_HANDLE_VALUE)
            {
                *encryptionNotSupported = TRUE;
                NOHANDLES(CloseHandle(out));
                out = INVALID_HANDLE_VALUE;
                if (!DeleteFileW(fileNameW)) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                    TRACE_I("Unable to delete testing target file: " << fileName);
            }
        }
        if (err == ERROR_FILE_EXISTS || // check whether this is merely overwriting the DOS name
            err == ERROR_ALREADY_EXISTS ||
            err == ERROR_ACCESS_DENIED)
        {
            WIN32_FIND_DATAW data;
            HANDLE find = HANDLES_Q(FindFirstFileW(fileNameW, &data));
            if (find != INVALID_HANDLE_VALUE)
            {
                HANDLES(FindClose(find));
                if (err != ERROR_ACCESS_DENIED || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    const char* tgtName = SalPathFindFileName(fileName);
                    char altName[MAX_PATH];
                    char fullName[MAX_PATH];
                    if (ConvertWideToUtf8(data.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                        altName[0] = 0;
                    if (ConvertWideToUtf8(data.cFileName, -1, fullName, _countof(fullName)) == 0)
                        fullName[0] = 0;
                    if (StrICmp(tgtName, altName) == 0 && // match only for DOS name
                        StrICmp(tgtName, fullName) != 0) // (full name differs)
                    {
                        // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (no extra DOS name needed)
                        char tmpName[MAX_PATH + 20];
                        if (strlen(fileName) >= _countof(tmpName))
                        {
                            TRACE_E("SalCreateFileEx(): path too long for DOS-name collision workaround: " << fileName);
                        }
                        else
                        {
                            lstrcpyn(tmpName, fileName, _countof(tmpName));
                            CutDirectory(tmpName);
                            SalPathAddBackslash(tmpName, _countof(tmpName));
                            char* tmpNamePart = tmpName + strlen(tmpName);
                            char origFullName[MAX_PATH + 20];
                            if (SalPathAppend(tmpName, fullName, _countof(tmpName)))
                            {
                                strcpy(origFullName, tmpName);
                                DWORD num = (GetTickCount() / 10) % 0xFFF;
                                DWORD origFullNameAttr = SalGetFileAttributes(origFullName);
                                while (1)
                                {
                                    sprintf(tmpNamePart, "sal%03X", num++);
                                    if (SalMoveFile(origFullName, tmpName))
                                        break;
                                    DWORD e = GetLastError();
                                    if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                    {
                                        tmpName[0] = 0;
                                        break;
                                    }
                                }
                                if (tmpName[0] != 0) // if we successfully "tidied" the conflicting file, try creating
                                {                    // the target file again, then restore the original name
                                    out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                                                CREATE_NEW, flagsAndAttributes, NULL));
                                    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported != NULL &&
                                        (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
                                    { // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                        out = NOHANDLES(CreateFileW(fileNameW, desiredAccess, shareMode, NULL,
                                                                    CREATE_NEW, (flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY)), NULL));
                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            *encryptionNotSupported = TRUE;
                                            NOHANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            if (!DeleteFileW(fileNameW)) // XP and Vista ignore this scenario, so do the same (at worst warn user that a zero-length file was added on disk and cannot be deleted)
                                                TRACE_E("Unable to delete testing target file: " << fileName);
                                        }
                                    }
                                    if (!SalMoveFile(tmpName, origFullName))
                                    { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of fileName (the DOS name)
                                        TRACE_I("Unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);

                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            NOHANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            DeleteFileW(fileNameW);
                                            if (!SalMoveFile(tmpName, origFullName))
                                                TRACE_E("Fatal unexpected situation in SalCreateFileEx(): unable to rename file from tmp-name to original long file name! " << origFullName);
                                        }
                                    }
                                    else
                                    {
                                        if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                        {
                                            CStrP origFullNameW(ConvertAllocUtf8ToWide(origFullName, -1));
                                            if (origFullNameW != NULL)
                                                SetFileAttributesW(origFullNameW, origFullNameAttr); // leave without extra handling or retry; not critical (normally toggles unpredictably)
                                        }
                                    }
                                }
                            }
                            else
                                TRACE_E("SalCreateFileEx(): Original full file name is too long, unable to bypass only-dos-name-overwrite problem!");
                        }
                    }
                }
            }
        }
        if (out == INVALID_HANDLE_VALUE)
            SetLastError(err);
    }
    return out;
}

// Reserve a unique file in the destination directory.  The reservation is opened with
// CREATE_ALWAYS by DoCopyFile and is never visible under the requested target name.
// Keeping the temporary file beside its final name guarantees that ReplaceFileW is a
// same-volume commit.
static BOOL CreateTransactionalTargetFileName(const char* targetName, char* temporaryName, int temporaryNameLen)
{
    char targetDirectory[3 * MAX_PATH];
    lstrcpyn(targetDirectory, targetName, _countof(targetDirectory));
    if (!CutDirectory(targetDirectory))
    {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    return SalGetTempFileName(targetDirectory, "SALCP", temporaryName, temporaryNameLen, TRUE);
}

static HANDLE OpenTransactionalTargetFile(const char* temporaryName, DWORD desiredAccess,
                                          DWORD flagsAndAttributes, BOOL* encryptionNotSupported)
{
    HANDLE out = HANDLES_Q(CreateFileUtf8(temporaryName, desiredAccess, 0, NULL, CREATE_ALWAYS, flagsAndAttributes, NULL));
    if (out == INVALID_HANDLE_VALUE && (flagsAndAttributes & FILE_ATTRIBUTE_ENCRYPTED))
    {
        out = HANDLES_Q(CreateFileUtf8(temporaryName, desiredAccess, 0, NULL, CREATE_ALWAYS,
                                       flagsAndAttributes & ~(FILE_ATTRIBUTE_ENCRYPTED | FILE_ATTRIBUTE_READONLY), NULL));
        if (out != INVALID_HANDLE_VALUE)
        {
            *encryptionNotSupported = TRUE;
            HANDLES(CloseHandle(out));
            out = INVALID_HANDLE_VALUE;
        }
    }
    return out;
}

// ReplaceFileW preserves the previous destination until the file system commits the
// replacement.  If another actor removed the destination after the overwrite prompt,
// a write-through same-volume rename is the equivalent commit for a newly absent file.
static BOOL CommitTransactionalTargetFile(const char* targetName, const char* temporaryName, DWORD* error)
{
    CStrP targetNameW(ConvertAllocUtf8ToWide(targetName, -1));
    CStrP temporaryNameW(ConvertAllocUtf8ToWide(temporaryName, -1));
    if (targetNameW == NULL || temporaryNameW == NULL)
    {
        *error = ERROR_NO_UNICODE_TRANSLATION;
        return FALSE;
    }

    if (ReplaceFileW(targetNameW, temporaryNameW, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL))
        return TRUE;

    *error = GetLastError();
    if (*error == ERROR_FILE_NOT_FOUND &&
        MoveFileExW(temporaryNameW, targetNameW, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return TRUE;
    }
    if (*error == ERROR_FILE_NOT_FOUND)
        *error = GetLastError();
    return FALSE;
}

// A successful write is not a copy commit.  Reopen the closed destination and
// verify its on-disk file metadata before reporting success, replacing an old
// target, or allowing a cross-volume move to remove its source.
static BOOL VerifyDurableCopyCommit(const char* targetName, const CQuadWord& expectedSize, DWORD* error)
{
    HANDLE target = HANDLES_Q(CreateFileUtf8(targetName, GENERIC_READ,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (target == INVALID_HANDLE_VALUE)
    {
        *error = GetLastError();
        return FALSE;
    }

    BY_HANDLE_FILE_INFORMATION information;
    BOOL verified = GetFileInformationByHandle(target, &information);
    if (!verified)
        *error = GetLastError();
    else if ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
             information.nFileSizeLow != expectedSize.LoDWord ||
             information.nFileSizeHigh != expectedSize.HiDWord)
    {
        *error = ERROR_WRITE_FAULT;
        verified = FALSE;
    }

    if (!HANDLES(CloseHandle(target)) && verified)
    {
        *error = GetLastError();
        verified = FALSE;
    }
    return verified;
}

// A post-close size check establishes a durable destination, but it cannot
// distinguish two equally sized byte streams.  Cross-volume moves retain the
// source until this comparison succeeds after a copy path has had suspicious
// I/O retries.
static BOOL CalculateFileSha256(const char* fileName, BYTE digest[32], DWORD* error)
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    PUCHAR hashObject = NULL;
    BYTE* buffer = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD hashObjectLength = 0;
    DWORD resultLength = 0;
    BOOL calculated = FALSE;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hashObjectLength,
                          sizeof(hashObjectLength), &resultLength, 0) != 0 ||
        (hashObject = (PUCHAR)malloc(hashObjectLength)) == NULL ||
        BCryptCreateHash(algorithm, &hash, hashObject, hashObjectLength, NULL, 0, 0) != 0)
    {
        *error = ERROR_NOT_SUPPORTED;
        goto CLEANUP;
    }

    if ((file = HANDLES_Q(CreateFileUtf8(fileName, GENERIC_READ,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL))) == INVALID_HANDLE_VALUE)
    {
        *error = GetLastError();
        goto CLEANUP;
    }
    if ((buffer = (BYTE*)malloc(64 * 1024)) == NULL)
    {
        *error = ERROR_NOT_ENOUGH_MEMORY;
        goto CLEANUP;
    }

    while (1)
    {
        DWORD bytesRead;
        if (!ReadFile(file, buffer, 64 * 1024, &bytesRead, NULL))
        {
            *error = GetLastError();
            goto CLEANUP;
        }
        if (bytesRead == 0)
            break;
        if (BCryptHashData(hash, buffer, bytesRead, 0) != 0)
        {
            *error = ERROR_READ_FAULT;
            goto CLEANUP;
        }
    }
    if (BCryptFinishHash(hash, digest, 32, 0) != 0)
    {
        *error = ERROR_READ_FAULT;
        goto CLEANUP;
    }
    calculated = TRUE;

CLEANUP:
    if (file != INVALID_HANDLE_VALUE && !HANDLES(CloseHandle(file)) && calculated)
    {
        *error = GetLastError();
        calculated = FALSE;
    }
    if (buffer != NULL)
        free(buffer);
    if (hash != NULL)
        BCryptDestroyHash(hash);
    if (hashObject != NULL)
        free(hashObject);
    if (algorithm != NULL)
        BCryptCloseAlgorithmProvider(algorithm, 0);
    return calculated;
}

static BOOL VerifyFullFileContentSha256(const char* sourceName, const char* targetName, DWORD* error)
{
    BYTE sourceDigest[32];
    BYTE targetDigest[32];
    if (!CalculateFileSha256(sourceName, sourceDigest, error) ||
        !CalculateFileSha256(targetName, targetDigest, error))
    {
        return FALSE;
    }
    if (memcmp(sourceDigest, targetDigest, sizeof(sourceDigest)) != 0)
    {
        *error = ERROR_CRC;
        return FALSE;
    }
    return TRUE;
}

BOOL SyncOrAsyncDeviceIoControl(CAsyncCopyParams* asyncPar, HANDLE hDevice, DWORD dwIoControlCode,
                                LPVOID lpInBuffer, DWORD nInBufferSize, LPVOID lpOutBuffer,
                                DWORD nOutBufferSize, LPDWORD lpBytesReturned, DWORD* err)
{
    if (asyncPar->UseAsyncAlg) // asynchronous variant
    {
        if (!DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                             nOutBufferSize, NULL, asyncPar->InitOverlapped(0)) &&
                GetLastError() != ERROR_IO_PENDING ||
            !GetOverlappedResult(hDevice, asyncPar->GetOverlapped(0), lpBytesReturned, TRUE))
        { // error, return FALSE
            *err = GetLastError();
            return FALSE;
        }
    }
    else // synchronous variant
    {
        if (!DeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer,
                             nOutBufferSize, lpBytesReturned, NULL))
        { // error, return FALSE
            *err = GetLastError();
            return FALSE;
        }
    }
    *err = NO_ERROR;
    return TRUE;
}

void SetCompressAndEncryptedAttrs(const char* name, DWORD attr, HANDLE* out, BOOL openAlsoForRead,
                                  BOOL* encryptionNotSupported, CAsyncCopyParams* asyncPar)
{
    if (*out != INVALID_HANDLE_VALUE)
    {
        DWORD err = NO_ERROR;
        DWORD curAttr = SalGetFileAttributes(name);
        if ((curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) == 0)
        {
            USHORT state = COMPRESSION_FORMAT_NONE;
            ULONG length;
            if (!SyncOrAsyncDeviceIoControl(asyncPar, *out, FSCTL_SET_COMPRESSION, &state,
                                            sizeof(USHORT), NULL, 0, &length, &err))
            {
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << name << "! error=" << GetErrorText(err));
            }
        }
        if (curAttr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_ENCRYPTED) != (curAttr & FILE_ATTRIBUTE_ENCRYPTED))
        { // SalCreateFileEx above likely failed
            err = NO_ERROR;
            HANDLES(CloseHandle(*out)); // close the file; otherwise we cannot change its encrypted attribute
            CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
            if (nameW == NULL)
                err = ERROR_NO_UNICODE_TRANSLATION;
            if (attr & FILE_ATTRIBUTE_ENCRYPTED)
            {
                if (err == NO_ERROR && !EncryptFileW(nameW))
                {
                    err = GetLastError();
                    if (encryptionNotSupported != NULL)
                        *encryptionNotSupported = TRUE;
                }
            }
            else
            {
                if (err == NO_ERROR && !DecryptFileW(nameW, 0))
                    err = GetLastError();
            }
            if (err != NO_ERROR)
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Encrypted attribute for " << name << "! error=" << GetErrorText(err));
            // reopen the existing file to continue writing
            if (err == NO_ERROR)
            {
                *out = HANDLES_Q(CreateFileW(nameW, GENERIC_WRITE | (openAlsoForRead ? GENERIC_READ : 0), 0, NULL, OPEN_ALWAYS,
                                             asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            }
            else
                *out = INVALID_HANDLE_VALUE;
            if (openAlsoForRead && *out == INVALID_HANDLE_VALUE) // problem: reopening failed, try write-only
            {
                *out = HANDLES_Q(CreateFileW(nameW, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                             asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
            }
            if (*out == INVALID_HANDLE_VALUE) // still a problem: cannot reopen; delete it + report an error
            {
                err = GetLastError();
                if (nameW != NULL)
                    DeleteFileW(nameW);
                SetLastError(err);
            }
        }
        if (*out != INVALID_HANDLE_VALUE && // only when reopening succeeded (and we did not delete the file)
            (curAttr == INVALID_FILE_ATTRIBUTES ||
             (attr & FILE_ATTRIBUTE_COMPRESSED) != (curAttr & FILE_ATTRIBUTE_COMPRESSED)) &&
            (attr & FILE_ATTRIBUTE_COMPRESSED) != 0)
        {
            USHORT state = COMPRESSION_FORMAT_DEFAULT;
            ULONG length;
            if (!SyncOrAsyncDeviceIoControl(asyncPar, *out, FSCTL_SET_COMPRESSION, &state,
                                            sizeof(USHORT), NULL, 0, &length, &err))
            {
                TRACE_I("SetCompressAndEncryptedAttrs(): Unable to set Compressed attribute for " << name << "! error=" << GetErrorText(err));
            }
        }
    }
}

void CorrectCaseOfTgtName(char* tgtName, BOOL dataRead, WIN32_FIND_DATAW* data)
{
    if (!dataRead)
    {
        CStrP tgtNameW(ConvertAllocUtf8ToWide(tgtName, -1));
        if (tgtNameW == NULL)
            return;
        HANDLE find = HANDLES_Q(FindFirstFileW(tgtNameW, data));
        if (find != INVALID_HANDLE_VALUE)
            HANDLES(FindClose(find));
        else
            return; // failed to read data for the target file; abort
    }
    char dataName[MAX_PATH];
    if (ConvertWideToUtf8(data->cFileName, -1, dataName, _countof(dataName)) == 0)
        return;
    int len = (int)strlen(dataName);
    int tgtNameLen = (int)strlen(tgtName);
    if (tgtNameLen >= len && StrICmp(tgtName + tgtNameLen - len, dataName) == 0)
        memcpy(tgtName + tgtNameLen - len, dataName, len);
}

void SetTFSandPSforSkippedFile(COperation* op, CQuadWord& lastTransferredFileSize,
                               COperations* script, const CQuadWord& pSize)
{
    if (op->FileSize < COPY_MIN_FILE_SIZE)
    {
        lastTransferredFileSize += op->FileSize;                      // file size
        if (op->Size > COPY_MIN_FILE_SIZE)                            // should always be at least COPY_MIN_FILE_SIZE, but be safe...
            lastTransferredFileSize += op->Size - COPY_MIN_FILE_SIZE; // add the ADS size
    }
    else
        lastTransferredFileSize += op->Size; // file size + ADS
    script->SetTFSandProgressSize(lastTransferredFileSize, pSize);
}

void DoCopyFileLoopOrig(HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                        COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated,
                        COperation* op, const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy,
                        HWND hProgressDlg, CQuadWord& operationDone, CQuadWord& fileSize,
                        int bufferSize, int& allocWholeFileOnStart, BOOL& copyAgain)
{
    int autoRetryAttemptsSNAP = 0;
    DWORD read;
    DWORD written;
    while (1)
    {
        if (ReadFile(in, buffer, limitBufferSize, &read, NULL))
        {
            autoRetryAttemptsSNAP = 0;
            if (read == 0)
                break;                                                     // EOF
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            while (1)
            {
                if (WriteFile(out, buffer, read, &written, NULL) &&
                    read == written)
                {
                    break;
                }

            WRITE_ERROR:

                DWORD err;
                err = GetLastError();

                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }

                if (dlgData.SkipAllFileWrite)
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                int ret;
                ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                data[2] = op->TargetName;
                if (err == NO_ERROR && read != written)
                    err = ERROR_DISK_FULL;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY: // on a network we must reopen the handle; local access forbids it due to sharing
                {
                    if (out != NULL)
                    {
                        if (wholeFileAllocated)
                            SetEndOfFile(out);     // otherwise on a floppy the remaining bytes would be written
                        HANDLES(CloseHandle(out)); // close the invalid handle
                    }
                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                                               OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                    if (out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                    {
                        LONG lo, hi;
                        lo = GetFileSize(out, (DWORD*)&hi);
                        if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR || // cannot obtain the size
                            CQuadWord(lo, hi) < operationDone ||                     // file is too small
                            wholeFileAllocated && CQuadWord(lo, hi) > fileSize &&
                                CQuadWord(lo, hi) > operationDone + CQuadWord(read, 0) || // pre-allocated file is too large (beyond the reserved size and beyond the written portion including the current block) = extra bytes were appended (allocWholeFileOnStart should be 0 /* need-test */)
                            !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone + CQuadWord(read, 0), FALSE))
                        { // restart the whole operation
                            HANDLES(CloseHandle(in));
                            HANDLES(CloseHandle(out));
                            DeleteFileUtf8(op->TargetName);
                            copyAgain = TRUE; // goto COPY_AGAIN;
                            return;
                        }
                    }
                    else // still cannot open; problem persists
                    {
                        out = NULL;
                        goto WRITE_ERROR;
                    }
                    break;
                }

                case IDB_SKIPALL:
                    dlgData.SkipAllFileWrite = TRUE;
                case IDB_SKIP:
                {
                    skipCopy = TRUE; // goto SKIP_COPY
                    return;
                }

                case IDCANCEL:
                {
                    copyError = TRUE; // goto COPY_ERROR
                    return;
                }
                }
            }
            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            script->AddBytesToSpeedMetersAndTFSandPS(read, FALSE, bufferSize, &limitBufferSize);

            if (!script->ChangeSpeedLimit)                                 // when the speed limit can change, this is not a suitable wait point
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            operationDone += CQuadWord(read, 0);
            SetProgressWithoutSuspend(hProgressDlg, CaclProg(operationDone, op->Size),
                                      CaclProg(totalDone + operationDone, script->TotalSize), dlgData);

            if (script->ChangeSpeedLimit)                                  // speed limit may change; this is the right place to wait until the
            {                                                              // worker resumes and fetches a fresh copy buffer size
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                script->GetNewBufSize(&limitBufferSize, bufferSize);
            }
        }
        else
        {
        READ_ERROR:

            DWORD err;
            err = GetLastError();
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }

            if (dlgData.SkipAllFileRead)
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            if (err == ERROR_NETNAME_DELETED && ++autoRetryAttemptsSNAP <= 3)
            { // on SNAP server reading sometimes randomly fails with ERROR_NETNAME_DELETED; Retry button reportedly helps, so trigger it automatically
                Sleep(100);
                goto RETRY_COPY;
            }

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORREADINGFILE);
            data[2] = op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
            {
            RETRY_COPY:

                if (in != NULL)
                    HANDLES(CloseHandle(in)); // close the invalid handle
                in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                          OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                if (in != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
                {
                    LONG lo, hi;
                    lo = GetFileSize(in, (DWORD*)&hi);
                    if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR ||
                        CQuadWord(lo, hi) < operationDone ||
                        !CheckTailOfOutFile(NULL, in, out, operationDone, operationDone, TRUE))
                    { // cannot obtain the size or the file is too small; restart the whole operation
                        HANDLES(CloseHandle(in));
                        if (wholeFileAllocated)
                            SetEndOfFile(out); // otherwise on a floppy the remaining bytes would be written
                        HANDLES(CloseHandle(out));
                        DeleteFileUtf8(op->TargetName);
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                }
                else // still cannot open; problem persists
                {
                    in = NULL;
                    goto READ_ERROR;
                }
                break;
            }

            case IDB_SKIPALL:
                dlgData.SkipAllFileRead = TRUE;
            case IDB_SKIP:
            {
                skipCopy = TRUE; // goto SKIP_COPY
                return;
            }

            case IDCANCEL:
            {
                copyError = TRUE; // goto COPY_ERROR
                return;
            }
            }
        }
    }

    if (wholeFileAllocated) // we pre-allocated the complete file layout (meaning the allocation was useful; for example, the file cannot be empty)
    {
        if (operationDone < fileSize) // and the source file shrank
        {
            if (!SetEndOfFile(out)) // trim it here
            {
                written = read = 0;
                goto WRITE_ERROR;
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CQuadWord curFileSize;
            curFileSize.LoDWord = GetFileSize(out, &curFileSize.HiDWord);
            BOOL getFileSizeSuccess = (curFileSize.LoDWord != INVALID_FILE_SIZE || GetLastError() == NO_ERROR);
            if (getFileSizeSuccess && curFileSize == operationDone)
            { // verify that no extra bytes were appended at the end and that truncation works
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (getFileSizeSuccess)
                {
                    char num1[50];
                    char num2[50];
                    TRACE_E("DoCopyFileLoopOrig(): unable to allocate whole file size before copy operation, please report "
                            "under what conditions this occurs! Error: different file sizes: target="
                            << NumberToStr(num1, curFileSize) << " bytes, source=" << NumberToStr(num2, operationDone) << " bytes");
                }
                else
                {
                    DWORD err = GetLastError();
                    TRACE_E("DoCopyFileLoopOrig(): unable to test result of allocation of whole file size before copy operation, please report "
                            "under what conditions this occurs! GetFileSize("
                            << op->TargetName << ") error: " << GetErrorText(err));
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                HANDLES(CloseHandle(out));
                out = NULL;
                ClearReadOnlyAttr(op->TargetName); // if it somehow became read-only (should never happen), so we know how to handle it
                if (DeleteFileUtf8(op->TargetName))
                {
                    HANDLES(CloseHandle(in));
                    copyAgain = TRUE; // goto COPY_AGAIN;
                    return;
                }
                else
                {
                    written = read = 0;
                    goto WRITE_ERROR;
                }
            }
        }
    }
}

enum CCopy_BlkState
{
    cbsFree,       // block not in use
    cbsRead,       // reading source file blocks - completed (waiting to be written)
    cbsInProgress, // --- states below mean "waiting for the operation to finish" (completed states are above)
    cbsReading,    // reading source file blocks - requested (in progress)
    cbsTestingEOF, // checking the end of the source file
    cbsWriting,    // writing the block to the target file
    cbsDiscarded,  // attempted to read beyond the end of the source file (should only return error: EOF)
};

enum CCopy_ForceOp
{
    fopNotUsed, // free to read or write as needed
    fopReading, // forced to read
    fopWriting  // forced to write
};

struct CCopy_Context
{
    CAsyncCopyParams* AsyncPar;

    CCopy_ForceOp ForceOp;        // TRUE = must read now, FALSE = must write now
    BOOL ReadingDone;             // TRUE = the source file has been fully read
    CCopy_BlkState BlockState[8]; // block state
    DWORD BlockDataLen[8];        // for each block: expected data (cbsReading + cbsTestingEOF), valid data (cbsWriting)
    CQuadWord BlockOffset[8];     // for each block: block offset in the source/target file (also stored in the 'AsyncPar' OVERLAPPED)
    DWORD BlockTime[8];           // for each block: "time" when the last async operation in this block started
    DWORD CurTime;                // "time" counter for 'BlockTime', handles wrap-around (though unlikely)
    int FreeBlocks;               // current number of free blocks (cbsFree)
    int FreeBlockIndex;           // candidate index of a free block (cbsFree); must be verified
    int ReadingBlocks;            // current number of blocks being read(cbsReading and cbsTestingEOF)
    int WritingBlocks;            // current number of blocks being written (cbsWriting)
    CQuadWord ReadOffset;         // offset for reading the next block from the source file (previous one is already in progress)
    CQuadWord WriteOffset;        // offset for writing the next block to the target file (previous one is already in progress)
    int AutoRetryAttemptsSNAP;    // number of automatic Retry attempts (max 3): SNAP servers sporadically return ERROR_NETNAME_DELETED while reading, Retry button reportedly helps, so trigger it automatically

    // selected DoCopyFileLoopAsync parameters to avoid passing a long argument list everywhere
    CProgressDlgData* DlgData;
    COperation* Op;
    HWND HProgressDlg;
    HANDLE* In;
    HANDLE* Out;
    BOOL WholeFileAllocated;
    COperations* Script;
    CQuadWord* OperationDone;
    const CQuadWord* TotalDone;
    const CQuadWord* LastTransferredFileSize;

    CCopy_Context(CAsyncCopyParams* asyncPar, int numOfBlocks, CProgressDlgData* dlgData, COperation* op,
                  HWND hProgressDlg, HANDLE* in, HANDLE* out, BOOL wholeFileAllocated, COperations* script,
                  CQuadWord* operationDone, const CQuadWord* totalDone, const CQuadWord* lastTransferredFileSize)
    {
        AsyncPar = asyncPar;
        ForceOp = fopNotUsed;
        ReadingDone = FALSE;
        CurTime = 0;
        for (int i = 0; i < _countof(BlockState); i++)
            BlockState[i] = cbsFree;
        memset(BlockDataLen, 0, sizeof(BlockDataLen));
        memset(BlockOffset, 0, sizeof(BlockOffset));
        memset(BlockTime, 0, sizeof(BlockTime));
        FreeBlocks = numOfBlocks;
        FreeBlockIndex = 0;
        ReadingBlocks = 0;
        WritingBlocks = 0;
        ReadOffset.SetUI64(0);
        WriteOffset.SetUI64(0);
        AutoRetryAttemptsSNAP = 0;

        DlgData = dlgData;
        Op = op;
        HProgressDlg = hProgressDlg;
        In = in;
        Out = out;
        WholeFileAllocated = wholeFileAllocated;
        Script = script;
        OperationDone = operationDone;
        TotalDone = totalDone;
        LastTransferredFileSize = lastTransferredFileSize;
    }

    BOOL IsOperationDone(int numOfBlocks)
    {
        return ReadingDone && FreeBlocks == numOfBlocks;
    }

    BOOL StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF);
    BOOL StartWriting(int blkIndex, DWORD* err);
    int FindBlock(CCopy_BlkState state);
    void FreeBlock(int blkIndex);
    void DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex);
    void GetNewFileSize(const char* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize);

    BOOL HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain);
    BOOL HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                          const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset);

    // interrupts any pending asynchronous operations
    void CancelOpPhase1();
    // ensures that all asynchronous operations have really finished + positions the pointer at the end of the contiguous
    // portion of the target file so the file is truncated correctly (before a possible closing and deletion)
    // WARNING: frees unnecessary blocks; only those with data read from the input file remain, and they still
    //          follow WriteOffset (usable for retry)
    void CancelOpPhase2(int errBlkIndex);
    BOOL RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain);
    BOOL RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain, const CQuadWord& allocFileSize,
                           const CQuadWord& maxWriteOffset);
    BOOL HandleSuspModeAndCancel(BOOL* copyError);
};

BOOL DisableLocalBuffering(CAsyncCopyParams* asyncPar, HANDLE file, DWORD* err)
{
    CALL_STACK_MESSAGE1("DisableLocalBuffering()");
    if (DynNtFsControlFile != NULL) // "always true"
    {
        IO_STATUS_BLOCK ioStatus;
        ResetEvent(asyncPar->Overlapped[0].hEvent);
        ULONG status = DynNtFsControlFile(file, asyncPar->Overlapped[0].hEvent, NULL,
                                          0, &ioStatus, 0x00140390 /* IOCTL_LMR_DISABLE_LOCAL_BUFFERING */,
                                          NULL, 0, NULL, 0);
        if (status == STATUS_PENDING) // must wait for the operation to finish; it runs asynchronously
        {
            CALL_STACK_MESSAGE1("DisableLocalBuffering(): STATUS_PENDING");
            WaitForSingleObject(asyncPar->Overlapped[0].hEvent, INFINITE);
            status = ioStatus.Status;
        }
        if (status == 0 /* STATUS_SUCCESS */)
            return TRUE;
        *err = LsaNtStatusToWinError(status);
    }
    else
        *err = ERROR_INVALID_FUNCTION;
    return FALSE;
}

BOOL CCopy_Context::StartReading(int blkIndex, DWORD readSize, DWORD* err, BOOL testEOF)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "ReadFile: %d 0x%08X 0x%08X", blkIndex, ReadOffset.LoDWord, readSize);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    if (!ReadFile(*In, AsyncPar->Buffers[blkIndex], readSize, NULL,
                  AsyncPar->InitOverlappedWithOffset(blkIndex, ReadOffset)) &&
        GetLastError() != ERROR_IO_PENDING)
    { // a read error occurred; handle it
        *err = GetLastError();
        if (*err == ERROR_HANDLE_EOF) // synchronously reported EOF; convert it to an asynchronously reported EOF
            AsyncPar->SetOverlappedToEOF(blkIndex, ReadOffset);
        else
            return FALSE;
    }
    // if the read was completed synchronously (or via cache, which we cannot detect),
    // we must write something now; otherwise writing may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = opCompleted ? fopWriting : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("ReadFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    BlockOffset[blkIndex] = ReadOffset;
    BlockDataLen[blkIndex] = readSize;
    if (!testEOF) // block was cbsFree before calling this method
    {
        ReadOffset.Value += readSize;
        BlockState[blkIndex] = cbsReading;
    }
    else
        BlockState[blkIndex] = cbsTestingEOF;
    BlockTime[blkIndex] = CurTime++;
    FreeBlocks--;
    ReadingBlocks++;
    return TRUE;
}

BOOL CCopy_Context::StartWriting(int blkIndex, DWORD* err)
{
#ifdef ASYNC_COPY_DEBUG_MSG
    char sss[1000];
    sprintf(sss, "WriteFile: %d 0x%08X 0x%08X", blkIndex, WriteOffset.LoDWord, BlockDataLen[blkIndex]);
    TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

    if (!WriteFile(*Out, AsyncPar->Buffers[blkIndex], BlockDataLen[blkIndex], NULL,
                   AsyncPar->InitOverlappedWithOffset(blkIndex, WriteOffset)) &&
        GetLastError() != ERROR_IO_PENDING)
    { // a write error occurred; handle it
        *err = GetLastError();
        return FALSE;
    }
    // if the write was completed synchronously (or via cache, which we cannot detect),
    // we must read something now; otherwise reading may idle and slow down the whole operation
    BOOL opCompleted = HasOverlappedIoCompleted(AsyncPar->GetOverlapped(blkIndex));
    ForceOp = !ReadingDone && opCompleted ? fopReading : fopNotUsed;

#ifdef ASYNC_COPY_DEBUG_MSG
    TRACE_I("WriteFile result: " << (opCompleted ? "DONE" : "ASYNC"));
#endif // ASYNC_COPY_DEBUG_MSG

    if (opCompleted && !Script->ChangeSpeedLimit)                   // when the speed limit can change, this is not a suitable wait point
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        *err = ERROR_CANCELLED;
        return FALSE; // cancellation will be handled in the error-handling
    }

    WriteOffset.Value += BlockDataLen[blkIndex];
    BlockState[blkIndex] = cbsWriting; // block was cbsRead before calling this method
    BlockTime[blkIndex] = CurTime++;
    WritingBlocks++;
    return TRUE;
}

int CCopy_Context::FindBlock(CCopy_BlkState state)
{
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == state)
            return i;
    TRACE_C("CCopy_Context::FindBlock(): unable to find block with required state (" << (int)state << ").");
    return -1; // dead code, only for the compiler
}

void CCopy_Context::FreeBlock(int blkIndex)
{
    if (BlockState[blkIndex] == cbsReading || BlockState[blkIndex] == cbsTestingEOF)
        ReadingBlocks--;
    if (BlockState[blkIndex] == cbsWriting)
        WritingBlocks--;
    BlockState[blkIndex] = cbsFree;
    FreeBlockIndex = blkIndex;
    FreeBlocks++;
}

void CCopy_Context::DiscardBlocksBehindEOF(const CQuadWord& fileSize, int excludeIndex)
{
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (i == excludeIndex)
            continue;
        CCopy_BlkState st = BlockState[i];
        if ((st == cbsRead || st == cbsReading) && BlockOffset[i] >= fileSize)
        {
            if (st == cbsRead) // discard data read beyond the end of the file; they are useless
                FreeBlock(i);
            else
            {
                BlockState[i] = cbsDiscarded; // reading past the end of the file is pointless; no reason to adjust BlockTime
                ReadingBlocks--;
            }
        }
    }
}

void CCopy_Context::GetNewFileSize(const char* fileName, HANDLE file, CQuadWord* fileSize, const CQuadWord& minFileSize)
{
    fileSize->LoDWord = GetFileSize(file, &fileSize->HiDWord);
    if (fileSize->LoDWord == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    {
        DWORD err = GetLastError();
        TRACE_E("CCopy_Context::GetNewFileSize(): GetFileSize(" << fileName << "): unexpected error: " << GetErrorText(err));
        *fileSize = minFileSize;
    }
    else
    {
        if (*fileSize < minFileSize) // if GetFileSize happened to return a shorter length than already read
            *fileSize = minFileSize;
    }
}

void CCopy_Context::CancelOpPhase1()
{
    if (!CancelIo(*In))
    {
        DWORD err = GetLastError();
        TRACE_E("CCopy_Context::CancelOpPhase1(): CancelIo(IN) failed, error: " << GetErrorText(err));
    }
    if (*Out != NULL && !CancelIo(*Out))
    {
        DWORD err = GetLastError();
        TRACE_E("CCopy_Context::CancelOpPhase1(): CancelIo(OUT) failed, error: " << GetErrorText(err));
    }
}

void CCopy_Context::CancelOpPhase2(int errBlkIndex)
{
    // NOTE: errBlkIndex == -1 for errors when issuing an async reading (no block assigned),
    //       for errors while truncating the file after the main copy loop finished (no block assigned),
    //       or for Cancel in the progress dialog (no block assigned)

    DWORD bytes;
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] > cbsInProgress)
        { // GetOverlappedResult should return results immediately because CancelIo() was called for both files
            if (GetOverlappedResult(BlockState[i] == cbsWriting ? *Out : *In, AsyncPar->GetOverlapped(i), &bytes, TRUE))
            {
                if (BlockState[i] == cbsReading && BlockDataLen[i] == bytes) // fully read -> convert to cbsRead block
                {
                    BlockState[i] = cbsRead;
                    ReadingBlocks--;
                }
                else
                {
                    if (BlockState[i] == cbsWriting && BlockDataLen[i] == bytes) // fully written -> convert to cbsRead block (might write again, so keep it)
                    {
                        BlockState[i] = cbsRead;
                        WritingBlocks--;
                    }
                }
            }
            else
            {
                DWORD err = GetLastError();
                if (i != errBlkIndex &&             // already reporting the error for this block; no need to repeat it in TRACE
                    err != ERROR_OPERATION_ABORTED) // not an error, merely reports cancellation (CancelIo() call)
                {                                   // log issues in other blocks, usually harmless and best ignored
                    TRACE_I("CCopy_Context::CancelOpPhase2(): GetOverlappedResult(" << (BlockState[i] == cbsWriting ? "OUT" : "IN") << ", " << i << ") returned error: " << GetErrorText(err));
                }
            }
            switch (BlockState[i])
            {
            case cbsReading:    // not fully read
            case cbsTestingEOF: // EOF test not finished
            case cbsDiscarded:
                FreeBlock(i);
                break;

            case cbsWriting:                      // unwritten block
                if (WriteOffset > BlockOffset[i]) // lower WriteOffset if needed
                    WriteOffset = BlockOffset[i];
                BlockState[i] = cbsRead; // not fully written but already read -> convert to cbsRead block (might write again, so keep it)
                WritingBlocks--;
                break;
            }
        }
    }

    ReadOffset = WriteOffset; // determine how far we have contiguous data from the offset where writing should resume
    for (int i = 0; i < _countof(BlockState); i++)
    {
        if (BlockState[i] == cbsRead && BlockOffset[i] == ReadOffset) // block read directly after ReadOffset
        {
            ReadOffset.Value += BlockDataLen[i];
            i = -1; // start the search from the beginning again (with 8 blocks this is affordable, max 36 loop iterations)
        }
    }

    // drop blocks that are already written or too far ahead (not contiguous)
    // so they can be read again later
    for (int i = 0; i < _countof(BlockState); i++)
        if (BlockState[i] == cbsRead && (BlockOffset[i] < WriteOffset || BlockOffset[i] > ReadOffset))
            FreeBlock(i);

    // when deleting the target file, set the file pointer to the end of the written portion;
    // the caller will truncate it with SetEndOfFile before deletion (otherwise zeroes might be written
    // from the end of the written part to the end of the pre-allocated file - pre-allocation is 
    // used to prevent fragmentation)
    if (*Out != NULL) // only if the target file was not closed meanwhile
    {
        if (!SalSetFilePointer(*Out, WriteOffset))
        {
            DWORD err = GetLastError();
            TRACE_E("CCopy_Context::CancelOpPhase2(): unable to set file pointer in OUT file, error: " << GetErrorText(err));
        }
    }
}

BOOL CCopy_Context::RetryCopyReadErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain)
{
    if (*In != NULL)
        HANDLES(CloseHandle(*In)); // close the invalid handle
    *In = HANDLES_Q(CreateFileUtf8(Op->SourceName, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (*In != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        CQuadWord size;
        size.LoDWord = GetFileSize(*In, (DWORD*)&size.HiDWord);
        if ((size.LoDWord != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) && size >= ReadOffset)
        { // size obtained and the file is large enough
            // if the source is on a network: disable local client-side in-memory caching
            // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
            //
            // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
            // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
            if ((Op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *In, err))
                TRACE_E("CCopy_Context::RetryCopyReadErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << Op->SourceName << ", error: " << GetErrorText(*err));
            // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
            // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
            // from AsyncPar may still be in use)
            if (CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, TRUE))
            {
                ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
                *OperationDone = WriteOffset;
                Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
                SetProgressWithoutSuspend(HProgressDlg, CaclProg(*OperationDone, Op->Size),
                                          CaclProg(*TotalDone + *OperationDone, Script->TotalSize), *DlgData);
                return TRUE; // success: proceed with retry
            }
        }
        // cannot obtain the size, the file is too small, or the last written part differs from the source -> restart from scratch
        HANDLES(CloseHandle(*In));
        if (WholeFileAllocated)
            SetEndOfFile(*Out); // otherwise on a floppy the remaining bytes would be written
        HANDLES(CloseHandle(*Out));
        DeleteFileUtf8(Op->TargetName);
        *copyAgain = TRUE; // goto COPY_AGAIN;
        return FALSE;
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *In = NULL;
        *errAgain = TRUE; // goto READ_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleReadingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain)
{
    // NOTE: blkIndex == -1 when the async read request failed (no block assigned)

    CancelOpPhase1();

    while (1)
    {
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        if (*DlgData->CancelWorker)
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileRead)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        if (err == ERROR_NETNAME_DELETED && ++AutoRetryAttemptsSNAP <= 3)
        { // SNAP servers occasionally return ERROR_NETNAME_DELETED while reading; Retry button reportedly helps, so trigger it automatically
            Sleep(100);
            ret = IDRETRY;
        }
        else
        {
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORREADINGFILE);
            data[2] = Op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(HProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        }
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyReadErr(&err, copyAgain, &errAgain))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again; repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileRead = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again; repeat the message
        TRACE_C("CCopy_Context::HandleReadingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::RetryCopyWriteErr(DWORD* err, BOOL* copyAgain, BOOL* errAgain,
                                      const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    if (*Out != NULL)
    {
        if (WholeFileAllocated)
            SetEndOfFile(*Out);     // otherwise on a floppy the remaining bytes would be written
        HANDLES(CloseHandle(*Out)); // close the invalid handle
    }
    *Out = HANDLES_Q(CreateFileUtf8(Op->TargetName, GENERIC_WRITE | GENERIC_READ, 0, NULL,
                                OPEN_ALWAYS, AsyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
    if (*Out != INVALID_HANDLE_VALUE) // opened successfully; now adjust the offset
    {
        BOOL ok = TRUE;
        CQuadWord size;
        size.LoDWord = GetFileSize(*Out, (DWORD*)&size.HiDWord);
        if (size.LoDWord == INVALID_FILE_SIZE && GetLastError() != NO_ERROR ||   // cannot obtain the size
            size < WriteOffset ||                                                // file is too small
            WholeFileAllocated && size > allocFileSize && size > maxWriteOffset) // pre-allocated file is too large (greater than the pre-allocated size and the written portion including the current block) = extra bytes appended (allocWholeFileOnStart should be 0 /* need-test */)
        {                                                                        // restart the entire thing
            ok = FALSE;
        }
        // success (file size matches what we need)
        // if the target is on a network: disable local client-side in-memory caching
        // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
        //
        // using Overlapped[0].hEvent from AsyncPar is OK; nothing is "in-progress" now, the event is unused
        // (but WARNING: for example Buffers[0] from AsyncPar may still be in use)
        if (ok && (Op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(AsyncPar, *Out, err))
            TRACE_E("CCopy_Context::RetryCopyWriteErr(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << Op->TargetName << ", error: " << GetErrorText(*err));
        // using Overlapped[0 and 1].hEvent and Overlapped[0 and 1] from AsyncPar is OK; nothing is
        // "in-progress", the event nor the overlapped structures are used (but WARNING: for example Buffers[0]
        // from AsyncPar may still be in use)
        if (!ok || !CheckTailOfOutFile(AsyncPar, *In, *Out, WriteOffset, WriteOffset, FALSE))
        {
            HANDLES(CloseHandle(*In));
            HANDLES(CloseHandle(*Out));
            DeleteFileUtf8(Op->TargetName);
            *copyAgain = TRUE; // goto COPY_AGAIN;
            return FALSE;
        }
        ForceOp = ReadOffset > WriteOffset ? fopWriting : fopNotUsed; // if the read side is ahead, resume with writing
        *OperationDone = WriteOffset;
        Script->SetTFSandProgressSize(*LastTransferredFileSize + *OperationDone, *TotalDone + *OperationDone);
        SetProgressWithoutSuspend(HProgressDlg, CaclProg(*OperationDone, Op->Size),
                                  CaclProg(*TotalDone + *OperationDone, Script->TotalSize), *DlgData);
        return TRUE; // success: proceed with retry
    }
    else // still cannot open; problem persists
    {
        *err = GetLastError();
        *Out = NULL;
        *errAgain = TRUE; // goto WRITE_ERROR;
        return FALSE;
    }
}

BOOL CCopy_Context::HandleWritingErr(int blkIndex, DWORD err, BOOL* copyError, BOOL* skipCopy, BOOL* copyAgain,
                                     const CQuadWord& allocFileSize, const CQuadWord& maxWriteOffset)
{
    // NOTE: blkIndex == -1 for an error while truncating the file after the main copy loop finishes (it has no assigned block)

    CancelOpPhase1();

    while (1)
    {
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we are supposed to be in suspend mode, wait ...
        if (*DlgData->CancelWorker)
        {
            CancelOpPhase2(blkIndex);
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }

        if (DlgData->SkipAllFileWrite)
        {
            CancelOpPhase2(blkIndex);
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        int ret = IDCANCEL;
        char* data[4];
        data[0] = (char*)&ret;
        data[1] = LoadStr(IDS_ERRORWRITINGFILE);
        data[2] = Op->TargetName;
        data[3] = GetErrorText(err);
        SendMessage(HProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
        CancelOpPhase2(blkIndex);
        BOOL errAgain = FALSE;
        switch (ret)
        {
        case IDRETRY:
        {
            if (RetryCopyWriteErr(&err, copyAgain, &errAgain, allocFileSize, maxWriteOffset))
                return TRUE; // retry
            else
            {
                if (errAgain)
                    break;    // same problem again, repeat the message
                return FALSE; // copyAgain==TRUE, goto COPY_AGAIN;
            }
        }

        case IDB_SKIPALL:
            DlgData->SkipAllFileWrite = TRUE;
        case IDB_SKIP:
        {
            *skipCopy = TRUE; // goto SKIP_COPY
            return FALSE;
        }

        case IDCANCEL:
        {
            *copyError = TRUE; // goto COPY_ERROR
            return FALSE;
        }
        }
        if (errAgain)
            continue; // IDRETRY: same problem again, repeat the message
        TRACE_C("CCopy_Context::HandleWritingErr(): unexpected result of WM_USER_DIALOG(0).");
        return TRUE;
    }
}

BOOL CCopy_Context::HandleSuspModeAndCancel(BOOL* copyError)
{
    if (!Script->ChangeSpeedLimit)                                  // if the speed limit cannot change (otherwise this is not a "suitable" place to wait)
        WaitForSingleObject(DlgData->WorkerNotSuspended, INFINITE); // if we are supposed to be in suspend mode, wait ...
    if (*DlgData->CancelWorker)
    {
        CancelOpPhase1();
        CancelOpPhase2(-1);
        *copyError = TRUE; // goto COPY_ERROR
        return TRUE;
    }
    return FALSE;
}

void DoCopyFileLoopAsync(CAsyncCopyParams* asyncPar, HANDLE& in, HANDLE& out, void* buffer, int& limitBufferSize,
                         COperations* script, CProgressDlgData& dlgData, BOOL wholeFileAllocated, COperation* op,
                         const CQuadWord& totalDone, BOOL& copyError, BOOL& skipCopy, HWND hProgressDlg,
                         CQuadWord& operationDone, CQuadWord& fileSize, int bufferSize,
                         int& allocWholeFileOnStart, BOOL& copyAgain, const CQuadWord& lastTransferredFileSize)
{
    CQuadWord allocFileSize = fileSize;
    DWORD err = NO_ERROR;
    DWORD bytes = 0; // helper DWORD - how many bytes were read/written in the block

    // if the source/target is on the network: disable local client-side in-memory caching
    // http://msdn.microsoft.com/en-us/library/ee210753%28v=vs.85%29.aspx
    if ((op->OpFlags & OPFL_SRCPATH_IS_NET) && !DisableLocalBuffering(asyncPar, in, &err))
        TRACE_E("DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network source file: " << op->SourceName << ", error: " << GetErrorText(err));
    if ((op->OpFlags & OPFL_TGTPATH_IS_NET) && !DisableLocalBuffering(asyncPar, out, &err))
        TRACE_E("DoCopyFileLoopAsync(): IOCTL_LMR_DISABLE_LOCAL_BUFFERING failed for network target file: " << op->TargetName << ", error: " << GetErrorText(err));

    // copy loop parameters
    int numOfBlocks = 8;

    // Copy operation context (prevents passing heaps of parameters to helper functions, now context methods)
    CCopy_Context ctx(asyncPar, numOfBlocks, &dlgData, op, hProgressDlg, &in, &out, wholeFileAllocated, script,
                      &operationDone, &totalDone, &lastTransferredFileSize);
    BOOL doCopy = TRUE;
    while (doCopy)
    {
        if (ctx.ForceOp != fopWriting && ctx.FreeBlocks > 0 && !ctx.ReadingDone && ctx.ReadingBlocks < (numOfBlocks + 1) / 2) // read in parallel at most up to half of the blocks
        {
            DWORD toRead = ctx.ReadOffset + CQuadWord(limitBufferSize, 0) <= fileSize ? limitBufferSize : (fileSize - ctx.ReadOffset).LoDWord;
            BOOL testEOF = toRead == 0;
            if (!testEOF || ctx.ReadingBlocks == 0) // data read or EOF test (the EOF test runs only when all reads are finished)
            {
                if (ctx.BlockState[ctx.FreeBlockIndex] != cbsFree)
                    ctx.FreeBlockIndex = ctx.FindBlock(cbsFree);
                // EOF test = read the entire block, otherwise read the usual 'toRead'
                if (ctx.StartReading(ctx.FreeBlockIndex, testEOF ? limitBufferSize : toRead, &err, testEOF))
                    continue; // success (asynchronous read started), try to start another read
                else
                { // error (starting asynchronous read)
                    if (!ctx.HandleReadingErr(-1, err, &copyError, &skipCopy, &copyAgain))
                        return; // cancel/skip(skip-all)/retry-complete
                    continue;   // retry-resume
                }
            }
        }
        // reading has already been issued or is unnecessary, check whether something is completed
        BOOL shouldWait = TRUE; // TRUE = nothing else can be queued asynchronously, we must wait for some pending operation to finish
        BOOL retryCopy = FALSE; // TRUE = after an error we should run Retry = start over from the beginning of the "doCopy" loop
        // two passes are needed only for synchronous writes (we want to mark it
        // completed immediately and not after another read, mainly for progress reporting)
        for (int afterWriting = 0; afterWriting < 2; afterWriting++)
        {
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress && HasOverlappedIoCompleted(asyncPar->GetOverlapped(i)))
                {
                    shouldWait = FALSE; // in the spirit of "keep it simple" (there are situations where it could remain TRUE, but we ignore them)
                    switch (ctx.BlockState[i])
                    {
                    case cbsReading:    // reading the source file into a block - requested (in progress)
                    case cbsTestingEOF: // testing for the end of the source file
                    {
                        BOOL testingEOF = ctx.BlockState[i] == cbsTestingEOF;

#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("READ done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        BOOL res = GetOverlappedResult(in, asyncPar->GetOverlapped(i), &bytes, TRUE);
                        if (testingEOF && res && bytes == 0)
                        {
                            res = FALSE; // MSDN says it should return FALSE and ERROR_HANDLE_EOF at EOF, so enforce that (Novell Netware 6.5 disk returns TRUE)
                            SetLastError(ERROR_HANDLE_EOF);
                        }
                        if (res || GetLastError() == ERROR_HANDLE_EOF)
                        {
                            ctx.AutoRetryAttemptsSNAP = 0;
                            if (!res) // EOF at the beginning of the block (for cbsReading only: EOF can also be before this block and will be handled later in a block with a lower offset)
                            {
                                // when GetOverlappedResult() returns FALSE it does not have to return bytes==0 
                                // (TRACE_C existed for that and crashes happened), so zero the bytes explicitly
                                bytes = 0;
                                if (testingEOF)
                                    ctx.ReadingDone = TRUE; // confirmed end of the source file, stop reading further
                                // we must not force fopWriting (we have not read anything, there is nothing to write), unless this is an EOF test,
                                // let the other asynchronous reads finish, then perform the EOF test, and only then continue with writing
                                ctx.ForceOp = fopNotUsed;
                            }
                            if (bytes < ctx.BlockDataLen[i]) // the file is shorter than expected -> set the new file size
                            {
                                if (!testingEOF || bytes != 0)
                                    ctx.ReadOffset = fileSize = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                if (!testingEOF)
                                    ctx.DiscardBlocksBehindEOF(fileSize, i);
                                if (bytes == 0) // EOF = no data, free the block
                                {
                                    ctx.FreeBlock(i);
                                    if (testingEOF)
                                        doCopy = !ctx.IsOperationDone(numOfBlocks); // verify whether this finished the copy
                                }
                                else
                                    ctx.BlockDataLen[i] = bytes; // pretend we intended to read exactly this much
                            }
                            else
                            {
                                if (testingEOF) // we were looking for EOF and read a full block; the file probably grew significantly, determine the new size
                                {
                                    ctx.ReadOffset = ctx.BlockOffset[i] + CQuadWord(bytes, 0);
                                    ctx.GetNewFileSize(op->SourceName, in, &fileSize, ctx.ReadOffset);
                                }
                            }
                            if (ctx.BlockState[i] == cbsReading || ctx.BlockState[i] == cbsTestingEOF)
                            {
                                ctx.ReadingBlocks--;
                                ctx.BlockState[i] = cbsRead;
                            }
                        }
                        else // error
                        {
                            if (!ctx.HandleReadingErr(i, GetLastError(), &copyError, &skipCopy, &copyAgain))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                        }
                        break;
                    }

                    case cbsWriting: // writing a block to the target file
                    {
#ifdef ASYNC_COPY_DEBUG_MSG
                        TRACE_I("WRITE done: " << i);
#endif // ASYNC_COPY_DEBUG_MSG

                        BOOL res = GetOverlappedResult(out, asyncPar->GetOverlapped(i), &bytes, TRUE);
                        if (!res || bytes != ctx.BlockDataLen[i]) // error
                        {
                            err = GetLastError();
                            if (err == NO_ERROR && bytes != ctx.BlockDataLen[i])
                                err = ERROR_DISK_FULL;
                            CQuadWord maxWriteOffset = ctx.WriteOffset;
                            if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                return;       // cancel/skip(skip-all)/retry-complete
                            retryCopy = TRUE; // retry-resume
                            break;
                        }

                        if (ctx.HandleSuspModeAndCancel(&copyError))
                            return; // cancel

                        script->AddBytesToSpeedMetersAndTFSandPS(bytes, FALSE, bufferSize, &limitBufferSize);

                        if (!script->ChangeSpeedLimit)                                 // if the speed limit can change, this is not a "suitable" place to wait
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        operationDone += CQuadWord(bytes, 0);
                        SetProgressWithoutSuspend(hProgressDlg, CaclProg(operationDone, op->Size),
                                                  CaclProg(totalDone + operationDone, script->TotalSize), dlgData);

                        if (script->ChangeSpeedLimit)                                  // the speed limit is likely to change, this is a "suitable" place to wait until the
                        {                                                              // worker resumes so we can get the buffer size for copying again
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            script->GetNewBufSize(&limitBufferSize, bufferSize);
                        }

                        // break; // the break is intentionally missing here...
                    }
                    case cbsDiscarded: // reading the source file beyond its end (should only return the EOF error)
                    {
                        ctx.FreeBlock(i);
                        doCopy = !ctx.IsOperationDone(numOfBlocks);
                        break;
                    }
                    }
                }
                if (!doCopy || retryCopy)
                    break;
            }
            if (!doCopy || retryCopy)
                break;

            // we have read data into blocks, check whether they can be written to the target file;
            // written/discarded blocks were freed (we will read into them again at the top of the loop)
            CQuadWord nextReadBlkOffset; // lowest offset of a skipped cbsRead block
            do
            {
                nextReadBlkOffset.SetUI64(0);
                // write in parallel at most up to half of the blocks
                for (int i = 0; ctx.ForceOp != fopReading && i < _countof(ctx.BlockState) && ctx.WritingBlocks < (numOfBlocks + 1) / 2; i++)
                {
                    if (ctx.BlockState[i] == cbsRead)
                    {
                        if (ctx.WriteOffset == ctx.BlockOffset[i])
                        {
                            if (!ctx.StartWriting(i, &err))
                            { // error (asynchronous write)
                                CQuadWord maxWriteOffset = ctx.WriteOffset + CQuadWord(ctx.BlockDataLen[i], 0);
                                if (!ctx.HandleWritingErr(i, err, &copyError, &skipCopy, &copyAgain, allocFileSize, maxWriteOffset))
                                    return;       // cancel/skip(skip-all)/retry-complete
                                retryCopy = TRUE; // retry-resume
                                break;
                            }
                        }
                        else
                        {
                            if (nextReadBlkOffset.Value == 0 || ctx.BlockOffset[i] < nextReadBlkOffset)
                                nextReadBlkOffset = ctx.BlockOffset[i];
                        }
                    }
                } // we have another cbsRead block adjoining the written portion of the target file -> keep writing
            } while (!retryCopy && ctx.ForceOp != fopReading && nextReadBlkOffset.Value != 0 && nextReadBlkOffset == ctx.WriteOffset &&
                     ctx.WritingBlocks < (numOfBlocks + 1) / 2); // write in parallel at most up to half of the blocks
            if (retryCopy || ctx.ForceOp != fopReading)
                break; // we are going to Retry or the write was not synchronous (finished in about 0 ms) or we only write now, so two passes are pointless
        }
        if (!doCopy || retryCopy)
            continue;

        if (shouldWait) // another pass through the loop is pointless, no chance to start a new read or write, wait
        {               // for the oldest asynchronous operation to finish
            DWORD oldestBlockTime = 0;
            int oldestBlockIndex = -1;
            for (int i = 0; i < _countof(ctx.BlockState); i++)
            {
                if (ctx.BlockState[i] > cbsInProgress)
                {
                    DWORD ti = ctx.CurTime - ctx.BlockTime[i];
                    if (oldestBlockTime < ti)
                    {
                        oldestBlockTime = ti;
                        oldestBlockIndex = i;
                    }
                }
            }
            if (oldestBlockIndex == -1)
                TRACE_C("DoCopyFileLoopAsync(): unexpected situation: unable to find any block with operation in progress!");

#ifdef ASYNC_COPY_DEBUG_MSG
            TRACE_I("wait: GetOverlappedResult: " << oldestBlockIndex << (ctx.BlockState[oldestBlockIndex] == cbsWriting ? " WRITE" : " READ"));
#endif // ASYNC_COPY_DEBUG_MSG

            // wait for the oldest pending asynchronous operation to complete here
            // for the source file ('in') this covers: cbsReading, cbsTestingEOF, and cbsDiscarded
            // for the target file ('out') this covers only cbsWriting
            GetOverlappedResult(ctx.BlockState[oldestBlockIndex] == cbsWriting ? out : in,
                                asyncPar->GetOverlapped(oldestBlockIndex), &bytes, TRUE);

#ifdef ASYNC_COPY_DEBUG_MSG
            char sss[1000];
            sprintf(sss, "wait done: 0x%08X 0x%08X", ctx.BlockOffset[oldestBlockIndex].LoDWord, bytes);
            TRACE_I(sss);
#endif // ASYNC_COPY_DEBUG_MSG

            if (ctx.HandleSuspModeAndCancel(&copyError))
                return; // cancel
        }
    }
    if (ctx.ReadOffset != ctx.WriteOffset || operationDone != ctx.WriteOffset)
        TRACE_C("DoCopyFileLoopAsync(): unexpected situation after copy: ReadOffset != WriteOffset || operationDone != ctx.WriteOffset");

    if (wholeFileAllocated) // we allocated the full size of the file (meaning the allocation made sense, e.g. the file cannot be empty)
    {
        if (operationDone < allocFileSize) // and the source file shrank, trim it here
        {
            while (1)
            {
                CQuadWord off = ctx.WriteOffset;
                off.LoDWord = SetFilePointer(out, off.LoDWord, (LONG*)&(off.HiDWord), FILE_BEGIN);
                if (off.LoDWord == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR ||
                    off != ctx.WriteOffset ||
                    !SetEndOfFile(out))
                {
                    DWORD err2 = GetLastError();
                    if ((off.LoDWord != INVALID_SET_FILE_POINTER || err2 == NO_ERROR) && off != ctx.WriteOffset)
                        err2 = ERROR_INVALID_FUNCTION; // successful SetFilePointer, but off != ctx.WriteOffset: will probably never happen, included for completeness
                    if (!ctx.HandleWritingErr(-1, err2, &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                        return; // cancel/skip(skip-all)/retry-complete
                                // retry-resume
                }
                else
                    break; // success
            }
        }

        if (allocWholeFileOnStart == 0 /* need-test */)
        {
            CQuadWord curFileSize;
            curFileSize.LoDWord = GetFileSize(out, &curFileSize.HiDWord);
            BOOL getFileSizeSuccess = (curFileSize.LoDWord != INVALID_FILE_SIZE || GetLastError() == NO_ERROR);
            if (getFileSizeSuccess && curFileSize == operationDone)
            { // verify that no extra bytes were appended to the end of the file + that we can truncate the file
                allocWholeFileOnStart = 1 /* yes */;
            }
            else
            {
#ifdef _DEBUG
                if (getFileSizeSuccess)
                {
                    char num1[50];
                    char num2[50];
                    TRACE_E("DoCopyFileLoopAsync(): unable to allocate whole file size before copy operation, please report "
                            "under what conditions this occurs! Error: different file sizes: target="
                            << NumberToStr(num1, curFileSize) << " bytes, source=" << NumberToStr(num2, operationDone) << " bytes");
                }
                else
                {
                    DWORD err2 = GetLastError();
                    TRACE_E("DoCopyFileLoopAsync(): unable to test result of allocation of whole file size before copy operation, please report "
                            "under what conditions this occurs! GetFileSize("
                            << op->TargetName << ") error: " << GetErrorText(err2));
                }
#endif
                allocWholeFileOnStart = 2 /* no */; // skip further attempts on this target disk

                while (1)
                {
                    HANDLES(CloseHandle(out));
                    out = NULL;
                    ClearReadOnlyAttr(op->TargetName); // in case it was created as read-only (should never happen) so we can handle it
                    if (DeleteFileUtf8(op->TargetName))
                    {
                        HANDLES(CloseHandle(in));
                        copyAgain = TRUE; // goto COPY_AGAIN;
                        return;
                    }
                    else
                    {
                        if (!ctx.HandleWritingErr(-1, GetLastError(), &copyError, &skipCopy, &copyAgain, allocFileSize, CQuadWord(0, 0)))
                            return; // cancel/skip(skip-all)/retry-complete
                                    // retry-resume
                    }
                }
            }
        }
    }
}

BOOL DoCopyFile(COperation* op, HWND hProgressDlg, void* buffer,
                COperations* script, CQuadWord& totalDone,
                DWORD clearReadonlyMask, BOOL* skip, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CProgressDlgData& dlgData, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL isMove, CAsyncCopyParams*& asyncPar, BOOL* suspiciousIoRetry)
{
    if (suspiciousIoRetry != NULL)
        *suspiciousIoRetry = FALSE;
    char* const requestedTargetName = op->TargetName;
    char transactionalTargetName[3 * MAX_PATH];
    BOOL transactionalTarget = FALSE;
    BOOL transactionalTargetCommitted = FALSE;
    struct CRestoreRequestedTargetName
    {
        COperation* Operation;
        char* RequestedTargetName;
        CRestoreRequestedTargetName(COperation* operation, char* requestedTargetName)
            : Operation(operation), RequestedTargetName(requestedTargetName) {}
        ~CRestoreRequestedTargetName() { Operation->TargetName = RequestedTargetName; }
    } RestoreRequestedTargetName(op, requestedTargetName);
    struct CCleanupTransactionalTarget
    {
        char* TargetName;
        BOOL* IsTransactionalTarget;
        BOOL* IsCommitted;
        CCleanupTransactionalTarget(char* targetName, BOOL* isTransactionalTarget, BOOL* isCommitted)
            : TargetName(targetName), IsTransactionalTarget(isTransactionalTarget), IsCommitted(isCommitted) {}
        ~CCleanupTransactionalTarget()
        {
            if (*IsTransactionalTarget && !*IsCommitted)
            {
                ClearReadOnlyAttr(TargetName);
                if (!DeleteFileUtf8(TargetName))
                    TRACE_I("DoCopyFile(): unable to remove uncommitted transactional target " << TargetName << ": " << GetErrorText(GetLastError()));
            }
        }
    } CleanupTransactionalTarget(transactionalTargetName, &transactionalTarget, &transactionalTargetCommitted);

    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoCopyFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not copy it,
    // CreateFile would trim the spaces/dots and copy a different file or under a different name
    BOOL invalidSrcName = FileNameIsInvalid(op->SourceName, TRUE);
    BOOL invalidTgtName = FileNameIsInvalid(op->TargetName, TRUE);

    // optimization: skipping all "older and identical" files is about 4x faster,
    // slowing down when the file is newer is 5%, so it should be well worth it
    // (it is safe to assume the user enables "Overwrite Older" when the skips occur)
    BOOL tgtNameCaseCorrected = FALSE; // TRUE = the letter case in the target name was already adjusted to match the existing target file (so overwriting does not change it)
    WIN32_FIND_DATAW dataIn, dataOut;
    if ((op->OpFlags & OPFL_OVERWROLDERALRTESTED) == 0 &&
        !invalidSrcName && !invalidTgtName && script->OverwriteOlder)
    {
        HANDLE find;
        CStrP targetNameW(ConvertAllocUtf8ToWide(op->TargetName, -1));
        find = targetNameW != NULL ? HANDLES_Q(FindFirstFileW(targetNameW, &dataOut)) : INVALID_HANDLE_VALUE;
        if (find != INVALID_HANDLE_VALUE)
        {
            HANDLES(FindClose(find));

            CorrectCaseOfTgtName(op->TargetName, TRUE, &dataOut);
            tgtNameCaseCorrected = TRUE;

            const char* tgtName = SalPathFindFileName(op->TargetName);
            char outName[MAX_PATH];
            if (ConvertWideToUtf8(dataOut.cFileName, -1, outName, _countof(outName)) == 0)
                outName[0] = 0;
            if (StrICmp(tgtName, outName) == 0 &&                           // ensure it is not just a DOS-name match (that would change the DOS-name instead of overwriting)
                (dataOut.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) // ensure it is not a directory (overwrite-older cannot help there)
            {
                CStrP sourceNameW(ConvertAllocUtf8ToWide(op->SourceName, -1));
                find = sourceNameW != NULL ? HANDLES_Q(FindFirstFileW(sourceNameW, &dataIn)) : INVALID_HANDLE_VALUE;
                if (find != INVALID_HANDLE_VALUE)
                {
                    HANDLES(FindClose(find));

                    // truncate times to seconds (different file systems store timestamps with different precision, leading to "differences" even between "identical" times)
                    *(unsigned __int64*)&dataIn.ftLastWriteTime = *(unsigned __int64*)&dataIn.ftLastWriteTime - (*(unsigned __int64*)&dataIn.ftLastWriteTime % 10000000);
                    *(unsigned __int64*)&dataOut.ftLastWriteTime = *(unsigned __int64*)&dataOut.ftLastWriteTime - (*(unsigned __int64*)&dataOut.ftLastWriteTime % 10000000);

                    if ((dataIn.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&             // verify the source is still a file
                        CompareFileTime(&dataIn.ftLastWriteTime, &dataOut.ftLastWriteTime) <= 0) // source file is not newer than the target file - skip the copy operation
                    {
                        CQuadWord fileSize(op->FileSize);
                        if (fileSize < COPY_MIN_FILE_SIZE)
                        {
                            if (op->Size > COPY_MIN_FILE_SIZE)             // should always be at least COPY_MIN_FILE_SIZE, but play it safe...
                                fileSize += op->Size - COPY_MIN_FILE_SIZE; // add the size of ADS streams
                        }
                        else
                            fileSize = op->Size;
                        totalDone += op->Size;
                        script->AddBytesToTFSandSetProgressSize(fileSize, totalDone);

                        SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                        if (skip != NULL)
                            *skip = TRUE;
                        return TRUE;
                    }
                }
            }
        }
    }

    // decide which algorithm to use for copying: the ancient synchronous one or
    // the asynchronous one inspired by the Windows 7 CopyFileEx version:
    // - under Vista it misbehaved badly; forget Vista, it is almost dead anyway, and
    //   when using the old algorithm against Win7 over the network I saw no speed difference
    //   for uploads, and downloads were only 15% slower (acceptable)
    // - the asynchronous algorithm makes sense only over the network + when source/target is fast or network-based
    // - with the old algorithm, copying on Win7 over the network is easily 2x-3x slower for downloads,
    //   almost 2x slower for uploads, and about 30% slower for network-to-network copies
    BOOL useAsyncAlg = Windows7AndLater && Configuration.UseAsyncCopyAlg &&
                       op->FileSize.Value > 0 && // empty files are copied synchronously (no data)
                       ((op->OpFlags & OPFL_SRCPATH_IS_NET) && ((op->OpFlags & OPFL_TGTPATH_IS_NET) ||
                                                                (op->OpFlags & OPFL_TGTPATH_IS_FAST)) ||
                        (op->OpFlags & OPFL_TGTPATH_IS_NET) && (op->OpFlags & OPFL_SRCPATH_IS_FAST));

    if (asyncPar == NULL)
        asyncPar = new CAsyncCopyParams;

    asyncPar->Init(useAsyncAlg);
    script->EnableProgressBufferLimit(useAsyncAlg);
    struct CDisableProgressBufferLimit // ensure Script->EnableProgressBufferLimit(FALSE) is called on every exit from this function
    {
        COperations* Script;
        CDisableProgressBufferLimit(COperations* script) { Script = script; }
        ~CDisableProgressBufferLimit() { Script->EnableProgressBufferLimit(FALSE); }
    } DisableProgressBufferLimit(script);

    CQuadWord operationDone;
    CQuadWord lastTransferredFileSize;
    script->GetTFSandResetTrSpeedIfNeeded(&lastTransferredFileSize);

COPY_AGAIN:

    operationDone = CQuadWord(0, 0);
    HANDLE in;

    if (skip != NULL)
        *skip = FALSE;

    int bufferSize;
    if (useAsyncAlg)
    {
        if (op->FileSize.Value <= 512 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_512KB;
        else if (op->FileSize.Value <= 2 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_2MB;
        else if (op->FileSize.Value <= 8 * 1024 * 1024)
            bufferSize = ASYNC_COPY_BUF_SIZE_8MB;
        else
            bufferSize = ASYNC_COPY_BUF_SIZE;
    }
    else
        bufferSize = GetOptimalSyncCopyBufferSize(script, op->OpFlags);

    int limitBufferSize = bufferSize;
    script->SetTFSandProgressSize(lastTransferredFileSize, totalDone, &limitBufferSize, bufferSize);

    while (1)
    {
        if (!invalidSrcName && !asyncPar->Failed())
        {
            in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                      OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
        }
        else
        {
            in = INVALID_HANDLE_VALUE;
        }
        if (in != INVALID_HANDLE_VALUE)
        {
            CQuadWord fileSize = op->FileSize;

            HANDLE out;
            BOOL lossEncryptionAttr = FALSE;
            BOOL skipAllocWholeFileOnStart = FALSE;
            while (1)
            {
            OPEN_TGT_FILE:

                BOOL encryptionNotSupported = FALSE;
                DWORD fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_WRITE_THROUGH |
                                  (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) |
                                  (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                if (!invalidTgtName)
                {
                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                    if (transactionalTarget)
                        out = OpenTransactionalTargetFile(op->TargetName, GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0), fileAttrs, &encryptionNotSupported);
                    else
                        out = SalCreateFileEx(op->TargetName, GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0), 0, fileAttrs, &encryptionNotSupported);
                    if (!encryptionNotSupported && script->CopyAttrs && out == INVALID_HANDLE_VALUE) // in case read access to the directory is not allowed (we added it only for setting the Compressed attribute), try creating a write-only file
                    {
                        if (transactionalTarget)
                            out = OpenTransactionalTargetFile(op->TargetName, GENERIC_WRITE, fileAttrs, &encryptionNotSupported);
                        else
                            out = SalCreateFileEx(op->TargetName, GENERIC_WRITE, 0, fileAttrs, &encryptionNotSupported);
                    }

                    if (out == INVALID_HANDLE_VALUE && encryptionNotSupported && dlgData.FileOutLossEncrAll && !lossEncryptionAttr)
                    { // the user agreed to lose the Encrypted attribute for all problematic files, so make that happen here
                        lossEncryptionAttr = TRUE;
                        continue;
                    }
                    HANDLES_ADD_EX(__otQuiet, out != INVALID_HANDLE_VALUE, __htFile,
                                   __hoCreateFile, out, GetLastError(), TRUE);
                    if (script->CopyAttrs)
                    {
                        fileAttrs = lossEncryptionAttr ? (op->Attr & ~FILE_ATTRIBUTE_ENCRYPTED) : op->Attr;
                        SetCompressAndEncryptedAttrs(op->TargetName, fileAttrs, &out, TRUE, NULL, asyncPar);
                    }

                    if (out != INVALID_HANDLE_VALUE && (fileAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                    { // verify that the Encrypted attribute is really set (on FAT it is simply ignored, the system does not return an error (for CreateFile specifically))
                        DWORD attrs;
                        attrs = SalGetFileAttributes(op->TargetName);
                        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_ENCRYPTED) == 0)
                        { // unable to apply the Encrypted attribute, ask the user what to do...
                            if (dlgData.FileOutLossEncrAll)
                                lossEncryptionAttr = TRUE;
                            else
                            {
                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CANCEL_ENCNOTSUP;

                                if (dlgData.SkipAllFileOutLossEncr)
                                    goto SKIP_ENCNOTSUP;

                                int ret;
                                ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = (char*)TRUE;
                                data[2] = op->TargetName;
                                data[3] = (char*)(INT_PTR)isMove;
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                switch (ret)
                                {
                                case IDB_ALL:
                                    dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                case IDYES:
                                    lossEncryptionAttr = TRUE;
                                    break;

                                case IDB_SKIPALL:
                                    dlgData.SkipAllFileOutLossEncr = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_ENCNOTSUP:

                                    HANDLES(CloseHandle(out));
                                    DeleteFileUtf8(op->TargetName);
                                    goto SKIP_OPEN_OUT;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_ENCNOTSUP:

                                    HANDLES(CloseHandle(out));
                                    DeleteFileUtf8(op->TargetName);
                                    goto CANCEL_OPEN2;
                                }
                                }
                            }
                        }
                    }
                }
                else
                {
                    out = INVALID_HANDLE_VALUE;
                }

                if (out != INVALID_HANDLE_VALUE)
                {

                COPY:

                    // if possible, allocate the required space for the file (prevents disk fragmentation + smoother writes to floppies)
                    BOOL wholeFileAllocated = FALSE;
                    if (!skipAllocWholeFileOnStart &&               // last time failed, so the same would probably happen now
                        allocWholeFileOnStart != 2 /* no */ &&      // allocating the whole file is not forbidden
                        fileSize > CQuadWord(limitBufferSize, 0) && // allocation is pointless below the copy buffer size
                        fileSize < CQuadWord(0, 0x80000000))        // file size is positive number (otherwise seeking is impossible - numbers above 8EB, so likely never happens)
                    {
                        BOOL fatal = TRUE;
                        BOOL ignoreErr = FALSE;
                        if (SalSetFilePointer(out, fileSize))
                        {
                            if (SetEndOfFile(out))
                            {
                                if (SetFilePointer(out, 0, NULL, FILE_BEGIN) == 0)
                                {
                                    fatal = FALSE;
                                    wholeFileAllocated = TRUE;
                                }
                            }
                            else
                            {
                                if (GetLastError() == ERROR_DISK_FULL)
                                    ignoreErr = TRUE; // not enough space on the disk
                            }
                        }
                        if (fatal)
                        {
                            if (!ignoreErr)
                            {
                                DWORD err = GetLastError();
                                TRACE_E("DoCopyFile(): unable to allocate whole file size before copy operation, please report under what conditions this occurs! GetLastError(): " << GetErrorText(err));
                                allocWholeFileOnStart = 2 /* no */; // we will forego further attempts on this target disk
                            }

                            // try truncating the file to zero so closing it does not trigger any unnecessary writes
                            SetFilePointer(out, 0, NULL, FILE_BEGIN);
                            SetEndOfFile(out);

                            HANDLES(CloseHandle(out));
                            out = INVALID_HANDLE_VALUE;
                            ClearReadOnlyAttr(op->TargetName); // in case it was created as read-only (should never happen) so we can handle it
                            if (DeleteFileUtf8(op->TargetName))
                            {
                                skipAllocWholeFileOnStart = TRUE;
                                goto OPEN_TGT_FILE;
                            }
                            else
                                goto CREATE_ERROR;
                        }
                    }

                    script->SetFileStartParams();

                    BOOL copyError = FALSE;
                    BOOL skipCopy = FALSE;
                    BOOL copyAgain = FALSE;
                    if (useAsyncAlg)
                    {
                        DoCopyFileLoopAsync(asyncPar, in, out, buffer, limitBufferSize, script, dlgData, wholeFileAllocated, op,
                                            totalDone, copyError, skipCopy, hProgressDlg, operationDone, fileSize,
                                            bufferSize, allocWholeFileOnStart, copyAgain, lastTransferredFileSize);
                        // NOTE: neither 'in' nor 'out' has the file pointer (SetFilePointer) positioned at the end of the file,
                        //       'out' has it set only when (copyError || skipCopy)
                    }
                    else
                    {
                        DoCopyFileLoopOrig(in, out, buffer, limitBufferSize, script, dlgData, wholeFileAllocated, op,
                                           totalDone, copyError, skipCopy, hProgressDlg, operationDone, fileSize,
                                           bufferSize, allocWholeFileOnStart, copyAgain);
                    }

                    if (copyError)
                    {
                    COPY_ERROR:

                        if (in != NULL)
                            HANDLES(CloseHandle(in));
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                SetEndOfFile(out); // otherwise on a floppy the remaining part of the file would be written
                            HANDLES(CloseHandle(out));
                        }
                        DeleteFileUtf8(op->TargetName);
                        return FALSE;
                    }
                    if (skipCopy)
                    {
                    SKIP_COPY:

                        totalDone += op->Size;
                        SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                        if (in != NULL)
                            HANDLES(CloseHandle(in));
                        if (out != NULL)
                        {
                            if (wholeFileAllocated)
                                SetEndOfFile(out); // otherwise on a floppy the remaining part of the file would be written
                            HANDLES(CloseHandle(out));
                        }
                        DeleteFileUtf8(op->TargetName);
                        SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                        if (skip != NULL)
                            *skip = TRUE;
                        return TRUE;
                    }
                    if (copyAgain)
                    {
                        if (suspiciousIoRetry != NULL)
                            *suspiciousIoRetry = TRUE;
                        goto COPY_AGAIN;
                    }

                    if (lantasticCheck)
                    {
                        CQuadWord inSize, outSize;
                        inSize.LoDWord = GetFileSize(in, &inSize.HiDWord);
                        outSize.LoDWord = GetFileSize(out, &outSize.HiDWord);
                        if (inSize != outSize)
                        {                                                              // Lantastic 7.0: everything seems fine, but the result is wrong
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR;

                            if (dlgData.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(ERROR_DISK_FULL);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                if (suspiciousIoRetry != NULL)
                                    *suspiciousIoRetry = TRUE;
                                operationDone = CQuadWord(0, 0);
                                script->SetTFSandProgressSize(lastTransferredFileSize, totalDone);
                                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                                SetFilePointer(in, 0, NULL, FILE_BEGIN);  // read again
                                SetFilePointer(out, 0, NULL, FILE_BEGIN); // write again
                                SetEndOfFile(out);                        // truncate the output file
                                goto COPY;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }
                    }

                    FILETIME /*creation, lastAccess,*/ lastWrite;
                    BOOL ignoreGetFileTimeErr = FALSE;
                    while (!ignoreGetFileTimeErr &&
                           !GetFileTime(in, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite))
                    {
                        DWORD err = GetLastError();

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto COPY_ERROR;

                        if (dlgData.SkipAllGetFileTime)
                            goto SKIP_COPY;

                        if (dlgData.IgnoreAllGetFileTimeErr)
                            goto IGNORE_GETFILETIME;

                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = LoadStr(IDS_ERRORGETTINGFILETIME);
                        data[2] = op->SourceName;
                        data[3] = GetErrorText(err);
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllGetFileTimeErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                        {
                        IGNORE_GETFILETIME:

                            ignoreGetFileTimeErr = TRUE;
                            break;
                        }

                        case IDB_SKIPALL:
                            dlgData.SkipAllGetFileTime = TRUE;
                        case IDB_SKIP:
                            goto SKIP_COPY;

                        case IDCANCEL:
                            goto COPY_ERROR;
                        }
                    }

                    HANDLES(CloseHandle(in));
                    in = NULL;

                    if (operationDone < COPY_MIN_FILE_SIZE) // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        script->AddBytesToSpeedMetersAndTFSandPS((DWORD)(COPY_MIN_FILE_SIZE - operationDone).Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                    DWORD attr = op->Attr & clearReadonlyMask;
                    if (copyADS) // copy ADS streams if needed
                    {
                        SetFileAttributesUtf8(op->TargetName, FILE_ATTRIBUTE_ARCHIVE); // probably unnecessary, it hardly slows copying; reason: the file must not be read-only to work with it
                        CQuadWord operDone = operationDone;                        // the file is already copied
                        if (operDone < COPY_MIN_FILE_SIZE)
                            operDone = COPY_MIN_FILE_SIZE; // zero/small files take at least as long as files of size COPY_MIN_FILE_SIZE
                        BOOL adsSkip = FALSE;
                        // Pass the optimal buffer size computed from op->OpFlags for ADS copy
                        int adsBufferSize = GetOptimalSyncCopyBufferSize(script, op->OpFlags);
                        if (!DoCopyADS(hProgressDlg, op->SourceName, FALSE, op->TargetName, totalDone,
                                       operDone, op->Size, dlgData, script, &adsSkip, buffer, adsBufferSize) ||
                            adsSkip) // user hit cancel or skipped at least one ADS
                        {
                            if (out != NULL)
                                HANDLES(CloseHandle(out));
                            out = NULL;
                            if (DeleteFileUtf8(op->TargetName) == 0)
                            {
                                DWORD err = GetLastError();
                                TRACE_E("DoCopyFile(): Unable to remove newly created file: " << op->TargetName << ", error: " << GetErrorText(err));
                            }
                            if (!adsSkip)
                                return FALSE; // cancel the entire operation
                            if (skip != NULL)
                                *skip = TRUE; // it is a Skip, must report higher up (Move must not delete the source file)
                        }
                    }

                    if (out != NULL)
                    {
                        if (!ignoreGetFileTimeErr) // only if we did not ignore the error while reading the file time (nothing to set otherwise)
                        {
                            BOOL ignoreSetFileTimeErr = FALSE;
                            while (!ignoreSetFileTimeErr &&
                                   !SetFileTime(out, NULL /*&creation*/, NULL /*&lastAccess*/, &lastWrite))
                            {
                                DWORD err = GetLastError();

                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto COPY_ERROR;

                                if (dlgData.SkipAllSetFileTime)
                                    goto SKIP_COPY;

                                if (dlgData.IgnoreAllSetFileTimeErr)
                                    goto IGNORE_SETFILETIME;

                                int ret;
                                ret = IDCANCEL;
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = LoadStr(IDS_ERRORSETTINGFILETIME);
                                data[2] = op->TargetName;
                                data[3] = GetErrorText(err);
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 8, (LPARAM)data);
                                switch (ret)
                                {
                                case IDRETRY:
                                    break;

                                case IDB_IGNOREALL:
                                    dlgData.IgnoreAllSetFileTimeErr = TRUE; // the break; is intentionally missing here
                                case IDB_IGNORE:
                                {
                                IGNORE_SETFILETIME:

                                    ignoreSetFileTimeErr = TRUE;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    dlgData.SkipAllSetFileTime = TRUE;
                                case IDB_SKIP:
                                    goto SKIP_COPY;

                                case IDCANCEL:
                                    goto COPY_ERROR;
                                }
                            }
                        }
                        DWORD closeOrFlushError = NO_ERROR;
                        // This is the durable copy commit boundary.  It applies to new
                        // destinations too: DoMoveFile may delete the source only after
                        // DoCopyFile returns success from this boundary.
                        if (!FlushFileBuffers(out))
                            closeOrFlushError = GetLastError();
                        if (!HANDLES(CloseHandle(out)) && closeOrFlushError == NO_ERROR)
                            closeOrFlushError = GetLastError();
                        out = NULL;
                        if (closeOrFlushError != NO_ERROR)
                        {
                            out = NULL;
                            DWORD err = closeOrFlushError;
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR;

                            if (dlgData.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                            {
                                if (suspiciousIoRetry != NULL)
                                    *suspiciousIoRetry = TRUE;
                                if (DeleteFileUtf8(op->TargetName) == 0)
                                {
                                    DWORD err2 = GetLastError();
                                    TRACE_E("DoCopyFile(): Unable to remove newly created file: " << op->TargetName << ", error: " << GetErrorText(err2));
                                }
                                goto COPY_AGAIN;
                            }

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR;
                            }
                        }

                        SetFileAttributesUtf8(op->TargetName, script->CopyAttrs ? attr : (attr | FILE_ATTRIBUTE_ARCHIVE));
                    }

                    if (script->CopyAttrs) // verify whether the source file attributes were preserved
                    {
                        DWORD curAttrs;
                        curAttrs = SalGetFileAttributes(op->TargetName);
                        if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (attr & DISPLAYED_ATTRIBUTES))
                        {                                                              // attributes probably were not preserved, warn the user
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (dlgData.IgnoreAllSetAttrsErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = op->TargetName;
                                data[2] = (char*)(DWORD_PTR)(attr & DISPLAYED_ATTRIBUTES);
                                data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllSetAttrsErr = TRUE; // break is intentional here; nothing is missing
                            case IDB_IGNORE:
                                break;

                            case IDCANCEL:
                            {
                            COPY_ERROR_2:

                                ClearReadOnlyAttr(op->TargetName); // the file must not be read-only if it is to be deleted
                                DeleteFileUtf8(op->TargetName);
                                return FALSE;
                            }
                            }
                        }
                    }

                    if (script->CopySecurity) // should we copy NTFS security permissions?
                    {
                        DWORD err;
                        if (!DoCopySecurity(op->SourceName, op->TargetName, &err, NULL))
                        {
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR_2;

                            int ret;
                            ret = IDCANCEL;
                            if (dlgData.IgnoreAllCopyPermErr)
                                ret = IDB_IGNORE;
                            else
                            {
                                char* data[4];
                                data[0] = (char*)&ret;
                                data[1] = op->SourceName;
                                data[2] = op->TargetName;
                                data[3] = (char*)(DWORD_PTR)err;
                                SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                            }
                            switch (ret)
                            {
                            case IDB_IGNOREALL:
                                dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                            case IDB_IGNORE:
                                break;

                            case IDCANCEL:
                                goto COPY_ERROR_2;
                            }
                        }
                    }

                    DWORD verificationError = NO_ERROR;
                    while (!VerifyDurableCopyCommit(op->TargetName, op->FileSize, &verificationError))
                    {
                        TRACE_I("DoCopyFile(): durable copy commit verification failed for " << op->TargetName << ": " << GetErrorText(verificationError));
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto COPY_ERROR_2;

                        if (dlgData.SkipAllFileWrite)
                            goto SKIP_COPY;

                        int ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                        data[2] = op->TargetName;
                        data[3] = GetErrorText(verificationError);
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                        switch (ret)
                        {
                        case IDRETRY:
                            if (suspiciousIoRetry != NULL)
                                *suspiciousIoRetry = TRUE;
                            ClearReadOnlyAttr(op->TargetName);
                            if (DeleteFileUtf8(op->TargetName) == 0)
                            {
                                DWORD err = GetLastError();
                                TRACE_E("DoCopyFile(): Unable to remove unverified copy target: " << op->TargetName << ", error: " << GetErrorText(err));
                            }
                            goto COPY_AGAIN;

                        case IDB_SKIPALL:
                            dlgData.SkipAllFileWrite = TRUE;
                        case IDB_SKIP:
                            goto SKIP_COPY;

                        case IDCANCEL:
                            goto COPY_ERROR_2;
                        }
                    }

                    if (transactionalTarget)
                    {
                        DWORD err;
                        while (!CommitTransactionalTargetFile(requestedTargetName, op->TargetName, &err))
                        {
                            TRACE_I("DoCopyFile(): unable to commit transactional overwrite of " << requestedTargetName << ": " << GetErrorText(err));
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto COPY_ERROR_2;

                            if (dlgData.SkipAllFileWrite)
                                goto SKIP_COPY;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERRORWRITINGFILE);
                            data[2] = requestedTargetName;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileWrite = TRUE;
                            case IDB_SKIP:
                                goto SKIP_COPY;

                            case IDCANCEL:
                                goto COPY_ERROR_2;
                            }
                        }
                        transactionalTargetCommitted = TRUE;
                    }

                    totalDone += op->Size;
                    script->SetProgressSize(totalDone);
                    return TRUE;
                }
                else
                {
                    if (!invalidTgtName && encryptionNotSupported)
                    {
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_OPEN2;

                        if (dlgData.SkipAllFileOutLossEncr)
                            goto SKIP_OPEN_OUT;

                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = (char*)TRUE;
                        data[2] = op->TargetName;
                        data[3] = (char*)(INT_PTR)isMove;
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                        case IDYES:
                            lossEncryptionAttr = TRUE;
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllFileOutLossEncr = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN_OUT:

                            totalDone += op->Size;
                            SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                            HANDLES(CloseHandle(in));
                            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                            if (skip != NULL)
                                *skip = TRUE;
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN2:

                            HANDLES(CloseHandle(in));
                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                    CREATE_ERROR:

                        DWORD err = GetLastError();
                        if (invalidTgtName)
                            err = ERROR_INVALID_NAME;
                        BOOL errDeletingFile = FALSE;
                        if (err == ERROR_FILE_EXISTS || // overwrite the file?
                            err == ERROR_ALREADY_EXISTS)
                        {
                            if (!dlgData.OverwriteAll && (dlgData.CnfrmFileOver || script->OverwriteOlder))
                            {
                                char sAttr[101], tAttr[101];
                                BOOL getTimeFailed;
                                getTimeFailed = FALSE;
                                FILETIME sFileTime, tFileTime;
                                GetFileOverwriteInfo(sAttr, _countof(sAttr), in, op->SourceName, &sFileTime, &getTimeFailed);
                                HANDLES(CloseHandle(in));
                                in = NULL;
                                out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                           OPEN_EXISTING, 0, NULL));
                                if (out != INVALID_HANDLE_VALUE)
                                {
                                    GetFileOverwriteInfo(tAttr, _countof(tAttr), out, op->TargetName, &tFileTime, &getTimeFailed);
                                    HANDLES(CloseHandle(out));
                                }
                                else
                                {
                                    getTimeFailed = TRUE;
                                    lstrcpyn(tAttr, LoadStr(IDS_ERR_FILEOPEN), _countof(tAttr));
                                }
                                out = NULL;

                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                if (*dlgData.CancelWorker)
                                    goto CANCEL_OPEN;

                                if (dlgData.SkipAllOverwrite)
                                    goto SKIP_OPEN;

                                int ret;
                                ret = IDCANCEL;

                                if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                                {
                                    // trim times to seconds (different file systems store times with different precision, so "differences" occurred even between "identical" times)
                                    *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                                    *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                                    if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                        ret = IDYES; // overwrite older files without asking
                                    else
                                        ret = IDB_SKIP; // skip other existing files
                                }
                                else
                                {
                                    // show the prompt
                                    char* data[5];
                                    data[0] = (char*)&ret;
                                    data[1] = op->TargetName;
                                    data[2] = tAttr;
                                    data[3] = op->SourceName;
                                    data[4] = sAttr;
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
                                }
                                switch (ret)
                                {
                                case IDB_ALL:
                                    dlgData.OverwriteAll = TRUE;
                                case IDYES:
                                default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                {
                                    in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                              OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    if (in == INVALID_HANDLE_VALUE)
                                        goto OPEN_IN_ERROR;
                                    break;
                                }

                                case IDB_SKIPALL:
                                    dlgData.SkipAllOverwrite = TRUE;
                                case IDB_SKIP:
                                {
                                SKIP_OPEN:

                                    totalDone += op->Size;
                                    SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                                    SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                                    if (skip != NULL)
                                        *skip = TRUE;
                                    return TRUE;
                                }

                                case IDCANCEL:
                                {
                                CANCEL_OPEN:

                                    return FALSE;
                                }
                                }
                            }

                            DWORD attr = SalGetFileAttributes(op->TargetName);
                            if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                            {
                                if (!dlgData.OverwriteHiddenAll && dlgData.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                                {
                                    HANDLES(CloseHandle(in));
                                    in = NULL;

                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto CANCEL_OPEN;

                                    if (dlgData.SkipAllSystemOrHidden)
                                        goto SKIP_OPEN;

                                    int ret = IDCANCEL;
                                    char* data[4];
                                    data[0] = (char*)&ret;
                                    data[1] = LoadStr(IDS_CONFIRMFILEOVERWRITING);
                                    data[2] = op->TargetName;
                                    data[3] = LoadStr(IDS_WANTOVERWRITESHFILE);
                                    SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        dlgData.OverwriteHiddenAll = TRUE;
                                    case IDYES:
                                    default: // for safety (to prevent exiting this block with the 'in' handle closed)
                                    {
                                        in = HANDLES_Q(CreateFileUtf8(op->SourceName, GENERIC_READ,
                                                                  FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                                  OPEN_EXISTING, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                        if (in == INVALID_HANDLE_VALUE)
                                            goto OPEN_IN_ERROR;
                                        attr = SalGetFileAttributes(op->TargetName); // refresh attributes in case the user changed them
                                        break;
                                    }

                                    case IDB_SKIPALL:
                                        dlgData.SkipAllSystemOrHidden = TRUE;
                                    case IDB_SKIP:
                                        goto SKIP_OPEN;

                                    case IDCANCEL:
                                        goto CANCEL_OPEN;
                                    }
                                }
                            }

                            BOOL targetCannotOpenForWrite = FALSE;
                            while (1)
                            {
                                if (!transactionalTarget)
                                {
                                    // The overwrite decision has been made.  From this point on write only
                                    // to a sibling reservation, so retry, low-space, cancellation, and
                                    // metadata failures can remove that file without touching the old target.
                                    if (!CreateTransactionalTargetFileName(requestedTargetName, transactionalTargetName, _countof(transactionalTargetName)))
                                    {
                                        err = GetLastError();
                                        goto NORMAL_ERROR;
                                    }
                                    op->TargetName = transactionalTargetName;
                                    transactionalTarget = TRUE;
                                    goto OPEN_TGT_FILE;
                                }

                                if (targetCannotOpenForWrite || mustDeleteFileBeforeOverwrite == 1 /* yes */)
                                { // the file must be deleted first
                                    BOOL chAttr = ClearReadOnlyAttr(op->TargetName, attr);

                                    if (!tgtNameCaseCorrected)
                                    {
                                        CorrectCaseOfTgtName(op->TargetName, FALSE, &dataOut);
                                        tgtNameCaseCorrected = TRUE;
                                    }

                                    if (DeleteFileUtf8(op->TargetName))
                                        goto OPEN_TGT_FILE; // if it is read-only (clearing the attribute may have failed), it can be deleted only on Samba with "delete readonly" enabled
                                    else                    // cannot delete either, end with an error...
                                    {
                                        err = GetLastError();
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        errDeletingFile = TRUE;
                                        goto NORMAL_ERROR;
                                    }
                                }
                                else // overwrite the file in place
                                {
                                    // if we have not yet tested truncating the file to zero, obtain the current file size
                                    CQuadWord origFileSize(0, 0); // file size before truncation
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                                                   OPEN_EXISTING, 0, NULL));
                                        if (out != INVALID_HANDLE_VALUE)
                                        {
                                            origFileSize.LoDWord = GetFileSize(out, &origFileSize.HiDWord);
                                            if (origFileSize.LoDWord == INVALID_FILE_SIZE && GetLastError() == NO_ERROR)
                                                origFileSize.Set(0, 0); // error => set the size to zero and test it on another file
                                            HANDLES(CloseHandle(out));
                                        }
                                    }

                                    // open the file with ADS removal and truncation to zero
                                    BOOL chAttr = FALSE;
                                    if (attr != INVALID_FILE_ATTRIBUTES &&
                                        (attr & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                                    { // CREATE_ALWAYS does not play well with read-only, hidden, or system attributes, so drop them if needed
                                        chAttr = TRUE;
                                        SetFileAttributesUtf8(op->TargetName, 0);
                                    }
                                    // GENERIC_READ for 'out' slows asynchronous copying from disk to network (measured 95 MB/s instead of 111 MB/s on Win7 x64 GLAN)
                                    DWORD access = GENERIC_WRITE | (script->CopyAttrs ? GENERIC_READ : 0);
                                    fileAttrs = asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN |
                                                (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | // setting attributes during CREATE_ALWAYS works since XP and is the only way to apply Encrypted attribute when the file denies read access
                                                (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0);
                                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, fileAttrs, NULL));
                                    if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    if (script->CopyAttrs && out == INVALID_HANDLE_VALUE)
                                    { // if read access to the directory is denied (we added it only for setting the Compressed attribute), try opening the file for write only
                                        access = GENERIC_WRITE;
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, fileAttrs, NULL));
                                        if (out == INVALID_HANDLE_VALUE && fileAttrs != (asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN)) // when the target disk cannot create an Encrypted file (observed on NTFS network disk (tested on share from XP) while logged in under a different username than we have in the system (on the current console) - the remote machine has a same-named user without a password, so it cannot be used over the network)
                                            out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, CREATE_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                    }
                                    if (out == INVALID_HANDLE_VALUE) // target file cannot be opened for writing, so delete it and create it again
                                    {
                                        // handles the situation when a Samba file must be overwritten:
                                        // the file has mode 440+different_owner and sits in a directory where the current user has write access
                                        // (deletion works, but direct overwrite does not (cannot open for writing) - workaround:
                                        //  delete and recreate the file)
                                        // (Samba can allow deleting read-only files, which enables deleting them,
                                        //  otherwise Windows cannot delete a read-only file and we cannot drop 
                                        //  the "read-only" attribute because the current user is not the owner)
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // on target paths that support ADS also delete ADS on the target file (CREATE_ALWAYS should remove them, but on home W2K and XP they simply stay; no idea why, W2K and XP in VMWare delete ADS normally)
                                    if (script->TargetPathSupADS && !DeleteAllADS(out, op->TargetName))
                                    {
                                        HANDLES(CloseHandle(out));
                                        out = INVALID_HANDLE_VALUE;
                                        if (chAttr)
                                            SetFileAttributesUtf8(op->TargetName, attr);
                                        targetCannotOpenForWrite = TRUE;
                                        continue;
                                    }

                                    // if we have not yet tested truncating the file to zero, obtain the new file size
                                    if (mustDeleteFileBeforeOverwrite == 0 /* need test */)
                                    {
                                        HANDLES(CloseHandle(out));
                                        out = HANDLES_Q(CreateFileUtf8(op->TargetName, access, 0, NULL, OPEN_ALWAYS, asyncPar->GetOverlappedFlag() | FILE_FLAG_SEQUENTIAL_SCAN, NULL));
                                        if (out == INVALID_HANDLE_VALUE) // cannot reopen the target file we just opened, unlikely, try deleting and recreating it
                                        {
                                            targetCannotOpenForWrite = TRUE;
                                            continue;
                                        }
                                        CQuadWord newFileSize(0, 0); // file size after truncation
                                        newFileSize.LoDWord = GetFileSize(out, &newFileSize.HiDWord);
                                        if ((newFileSize.LoDWord != INVALID_FILE_SIZE || GetLastError() == NO_ERROR) && // we have the new size
                                            newFileSize == CQuadWord(0, 0))                                             // file really has 0 bytes
                                        {
                                            if (origFileSize != CQuadWord(0, 0))            // truncation can only be tested on a non-zero file
                                                mustDeleteFileBeforeOverwrite = 2; /* no */ // success (not a SNAP server - NSA drive, truncation does not work there)
                                        }
                                        else
                                        {
                                            HANDLES(CloseHandle(out));
                                            out = INVALID_HANDLE_VALUE;
                                            mustDeleteFileBeforeOverwrite = 1 /* yes */; // on error or when the size is non-zero, play it safe...
                                            continue;
                                        }
                                    }

                                    if (script->CopyAttrs || !lossEncryptionAttr && copyAsEncrypted)
                                    {
                                        encryptionNotSupported = FALSE;
                                        SetCompressAndEncryptedAttrs(op->TargetName, (!lossEncryptionAttr && copyAsEncrypted ? FILE_ATTRIBUTE_ENCRYPTED : 0) | (script->CopyAttrs ? (op->Attr & (FILE_ATTRIBUTE_COMPRESSED | (lossEncryptionAttr ? 0 : FILE_ATTRIBUTE_ENCRYPTED))) : 0),
                                                                     &out, script->CopyAttrs, &encryptionNotSupported, asyncPar);
                                        if (encryptionNotSupported) // unable to apply the Encrypted attribute, ask the user what to do...
                                        {
                                            if (dlgData.FileOutLossEncrAll)
                                                lossEncryptionAttr = TRUE;
                                            else
                                            {
                                                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                                if (*dlgData.CancelWorker)
                                                    goto CANCEL_ENCNOTSUP;

                                                if (dlgData.SkipAllFileOutLossEncr)
                                                    goto SKIP_ENCNOTSUP;

                                                int ret;
                                                ret = IDCANCEL;
                                                char* data[4];
                                                data[0] = (char*)&ret;
                                                data[1] = (char*)TRUE;
                                                data[2] = op->TargetName;
                                                data[3] = (char*)(INT_PTR)isMove;
                                                SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                                switch (ret)
                                                {
                                                case IDB_ALL:
                                                    dlgData.FileOutLossEncrAll = TRUE; // the break; is intentionally missing here
                                                case IDYES:
                                                    lossEncryptionAttr = TRUE;
                                                    break;

                                                case IDB_SKIPALL:
                                                    dlgData.SkipAllFileOutLossEncr = TRUE;
                                                case IDB_SKIP:
                                                    goto SKIP_ENCNOTSUP;

                                                case IDCANCEL:
                                                    goto CANCEL_ENCNOTSUP;
                                                }
                                            }
                                        }
                                    }
                                }
                                break;
                            }

                            goto COPY;
                        }
                        else // regular error
                        {
                        NORMAL_ERROR:

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN2;

                            if (dlgData.SkipAllFileOpenOut)
                                goto SKIP_OPEN_OUT;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(errDeletingFile ? IDS_ERRORDELETINGFILE : IDS_ERROROPENINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllFileOpenOut = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN_OUT;

                            case IDCANCEL:
                                goto CANCEL_OPEN2;
                            }
                        }
                    }
                }
            }
        }
        else
        {
        OPEN_IN_ERROR:

            DWORD err = GetLastError();
            if (invalidSrcName)
                err = ERROR_INVALID_NAME;
            if (asyncPar->Failed())
                err = ERROR_NOT_ENOUGH_MEMORY;                         // cannot create the synchronization event = lack of resources (will probably never happens, so we do not bother)
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllFileOpenIn)
                goto SKIP_OPEN_IN;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERROROPENINGFILE);
            data[2] = op->SourceName;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllFileOpenIn = TRUE;
            case IDB_SKIP:
            {
            SKIP_OPEN_IN:

                totalDone += op->Size;
                SetTFSandPSforSkippedFile(op, lastTransferredFileSize, script, totalDone);

                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                if (skip != NULL)
                    *skip = TRUE;
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoMoveFile(COperation* op, HWND hProgressDlg, void* buffer,
                COperations* script, CQuadWord& totalDone, BOOL dir,
                DWORD clearReadonlyMask, BOOL* novellRenamePatch, BOOL lantasticCheck,
                int& mustDeleteFileBeforeOverwrite, int& allocWholeFileOnStart,
                CProgressDlgData& dlgData, BOOL copyADS, BOOL copyAsEncrypted,
                BOOL* setDirTimeAfterMove, CAsyncCopyParams*& asyncPar,
                BOOL ignInvalidName)
{
    char log_buffer[1024];
    _snprintf_s(log_buffer, sizeof(log_buffer), _TRUNCATE, "DoMoveFile: Source='%s', Target='%s'", op->SourceName ? op->SourceName : "NULL", op->TargetName ? op->TargetName : "NULL");
    OutputDebugStringA(log_buffer);

    if (script->CopyAttrs && copyAsEncrypted)
        TRACE_E("DoMoveFile(): unexpected parameter value: copyAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    // if the path ends with a space/dot, it is invalid and we must not move it,
    // MoveFile would trim the spaces/dots and move a different file or under a different name,
    // directories fare better: appending a backslash helps there, we block the move
    // only when a new directory name would be invalid (when moving under the old
    // name, 'ignInvalidName' is TRUE)
    BOOL invalidName = FileNameIsInvalid(op->SourceName, TRUE, dir) ||
                       FileNameIsInvalid(op->TargetName, TRUE, dir && ignInvalidName);

    if (!copyAsEncrypted && !script->SameRootButDiffVolume && HasTheSameRootPath(op->SourceName, op->TargetName))
    {
        // if the path ends with a space or dot, we must append '\\', otherwise GetNamedSecurityInfo,
        // GetDirTime, SetFileAttributes, and MoveFile trim the spaces/dots and operate on a different path
        const char* sourceNameMvDir = op->SourceName;
        char sourceNameMvDirCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(sourceNameMvDir, sourceNameMvDirCopy);
        const char* targetNameMvDir = op->TargetName;
        char targetNameMvDirCopy[3 * MAX_PATH];
        MakeCopyWithBackslashIfNeeded(targetNameMvDir, targetNameMvDirCopy);

        int autoRetryAttempts = 0;
        CSrcSecurity srcSecurity;
        BOOL srcSecurityErr = FALSE;
        if (!invalidName && script->CopySecurity) // should we copy NTFS security permissions?
        {
            CStrP sourceNameMvDirW(ConvertAllocUtf8ToWide(sourceNameMvDir, -1));
            if (sourceNameMvDirW != NULL)
            {
                srcSecurity.SrcError = GetNamedSecurityInfoW(sourceNameMvDirW, SE_FILE_OBJECT,
                                                            DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                                            &srcSecurity.SrcOwner, &srcSecurity.SrcGroup, &srcSecurity.SrcDACL,
                                                            NULL, &srcSecurity.SrcSD);
            }
            else
            {
                srcSecurity.SrcError = ERROR_NO_UNICODE_TRANSLATION;
            }
            if (srcSecurity.SrcError != ERROR_SUCCESS) // failed to read security info from the source file -> nothing to apply on the target
            {
                srcSecurityErr = TRUE;
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                    return FALSE;

                int ret;
                ret = IDCANCEL;
                if (dlgData.IgnoreAllCopyPermErr)
                    ret = IDB_IGNORE;
                else
                {
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = op->SourceName;
                    data[2] = op->TargetName;
                    data[3] = (char*)(DWORD_PTR)srcSecurity.SrcError;
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                }
                switch (ret)
                {
                case IDB_IGNOREALL:
                    dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                case IDB_IGNORE:
                    break;

                case IDCANCEL:
                    return FALSE;
                }
            }
        }
        FILETIME dirTimeModified;
        BOOL dirTimeModifiedIsValid = FALSE;
        if (!invalidName && dir && !*novellRenamePatch && *setDirTimeAfterMove != 2 /* no */) // the issue apparently does not apply to Novell Netware, so ignore it there (affects e.g. Samba)
            dirTimeModifiedIsValid = GetDirTime(sourceNameMvDir, &dirTimeModified);
        while (1)
        {
            if (!invalidName && !*novellRenamePatch && SalMoveFile(sourceNameMvDir, targetNameMvDir))
            {
                if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // Archive attribute was not set, MoveFile turned it on, clear it again
                    SetFileAttributesUtf8(targetNameMvDir, op->Attr);                  // leave without handling or retry, not important (it normally toggles chaotically)

            OPERATION_DONE:

                if (script->CopyAttrs) // check whether the source file attributes were preserved
                {
                    DWORD curAttrs;
                    curAttrs = SalGetFileAttributes(targetNameMvDir);
                    if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (op->Attr & DISPLAYED_ATTRIBUTES))
                    {                                                              // attributes probably were not preserved, warn the user
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto MOVE_ERROR_2;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllSetAttrsErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = op->TargetName;
                            data[2] = (char*)(DWORD_PTR)(op->Attr & DISPLAYED_ATTRIBUTES);
                            data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllSetAttrsErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                        {
                        MOVE_ERROR_2:

                            return FALSE; // the file was moved to the target + cancel occurred; but we would rather not move it back, nobody should mind much
                        }
                        }
                    }
                }

                if (script->CopySecurity && !srcSecurityErr) // should we copy NTFS security permissions?
                {
                    DWORD err;
                    if (!DoCopySecurity(sourceNameMvDir, targetNameMvDir, &err, &srcSecurity))
                    {
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto MOVE_ERROR_2;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllCopyPermErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = op->SourceName;
                            data[2] = op->TargetName;
                            data[3] = (char*)(DWORD_PTR)err;
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllCopyPermErr = TRUE; // the break; is intentionally missing here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                            goto MOVE_ERROR_2;
                        }
                    }
                }

                if (dir && dirTimeModifiedIsValid && *setDirTimeAfterMove != 2 /* no */)
                {
                    FILETIME movedDirTimeModified;
                    if (GetDirTime(targetNameMvDir, &movedDirTimeModified))
                    {
                        if (CompareFileTime(&dirTimeModified, &movedDirTimeModified) == 0)
                        {
                            if (*setDirTimeAfterMove == 0 /* need test */)
                                *setDirTimeAfterMove = 2 /* no */;
                        }
                        else
                        {
                            if (*setDirTimeAfterMove == 0 /* need test */)
                                *setDirTimeAfterMove = 1 /* yes */;
                            DoCopyDirTime(hProgressDlg, targetNameMvDir, &dirTimeModified, dlgData, TRUE); // ignore any failure, this is just a hack (we already ignore time read errors from the directory); MoveFile should not change times
                        }
                    }
                }

                script->AddBytesToSpeedMetersAndTFSandPS((DWORD)op->Size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

                totalDone += op->Size;
                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }
            else
            {
                DWORD err = GetLastError();
                if (invalidName)
                    err = ERROR_INVALID_NAME;
                // Novell patch - before calling MoveFile we need to drop the read-only attribute
                if (!invalidName && *novellRenamePatch || err == ERROR_ACCESS_DENIED)
                {
                    DWORD attr = SalGetFileAttributes(sourceNameMvDir);
                    BOOL setAttr = ClearReadOnlyAttr(sourceNameMvDir, attr);
                    if (SalMoveFile(sourceNameMvDir, targetNameMvDir))
                    {
                        if (!*novellRenamePatch)
                            *novellRenamePatch = TRUE; // the next operations will go straight through here
                        if (setAttr || script->CopyAttrs && (attr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                        {
                            CStrP targetNameW(ConvertAllocUtf8ToWide(targetNameMvDir, -1));
                            if (targetNameW != NULL)
                                SetFileAttributesW(targetNameW, attr);
                        }

                        goto OPERATION_DONE;
                    }
                    err = GetLastError();
                    if (setAttr)
                    {
                        CStrP sourceNameW(ConvertAllocUtf8ToWide(sourceNameMvDir, -1));
                        if (sourceNameW != NULL)
                            SetFileAttributesW(sourceNameW, attr);
                    }
                }

                if (StrICmp(op->SourceName, op->TargetName) != 0 && // provided this is not just a change of case
                    (err == ERROR_FILE_EXISTS ||                    // verify whether this is only overwriting the DOS name of the file/directory
                     err == ERROR_ALREADY_EXISTS) &&
                    targetNameMvDir == op->TargetName) // no invalid names are allowed here
                {
                    WIN32_FIND_DATAW findData;
                    CStrP targetNameW(ConvertAllocUtf8ToWide(op->TargetName, -1));
                    HANDLE find = targetNameW != NULL ? HANDLES_Q(FindFirstFileW(targetNameW, &findData)) : INVALID_HANDLE_VALUE;
                    if (find != INVALID_HANDLE_VALUE)
                    {
                        HANDLES(FindClose(find));
                        const char* tgtName = SalPathFindFileName(op->TargetName);
                        char altName[MAX_PATH];
                        char fullName[MAX_PATH];
                        if (ConvertWideToUtf8(findData.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                            altName[0] = 0;
                        if (ConvertWideToUtf8(findData.cFileName, -1, fullName, _countof(fullName)) == 0)
                            fullName[0] = 0;
                        if (StrICmp(tgtName, altName) == 0 && // match only on the DOS name
                            StrICmp(tgtName, fullName) != 0)  // (the full name is different)
                        {
                            // rename ("tidy up") the file/directory with the conflicting DOS name to a temporary 8.3 name (does not need an extra DOS name)
                            char tmpName[MAX_PATH + 20];
                            if (strlen(op->TargetName) >= _countof(tmpName))
                            {
                                TRACE_E("DoMoveFile(): target path too long for DOS-name collision workaround: " << op->TargetName);
                            }
                            else
                            {
                                lstrcpyn(tmpName, op->TargetName, _countof(tmpName));
                                CutDirectory(tmpName);
                                SalPathAddBackslash(tmpName, _countof(tmpName));
                                char* tmpNamePart = tmpName + strlen(tmpName);
                                char origFullName[MAX_PATH + 20];
                                if (SalPathAppend(tmpName, fullName, _countof(tmpName)))
                                {
                                    strcpy(origFullName, tmpName);
                                    DWORD num = (GetTickCount() / 10) % 0xFFF;
                                    DWORD origFullNameAttr = SalGetFileAttributes(origFullName);
                                    while (1)
                                    {
                                        sprintf(tmpNamePart, "sal%03X", num++);
                                        if (SalMoveFile(origFullName, tmpName))
                                            break;
                                        DWORD e = GetLastError();
                                        if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                        {
                                            tmpName[0] = 0;
                                            break;
                                        }
                                    }
                                    if (tmpName[0] != 0) // if we managed to "tidy up" the conflicting file/directory, try moving it again
                                    {                    // then restore the original name of the "tidied" file/directory
                                        BOOL moveDone = SalMoveFile(sourceNameMvDir, op->TargetName);
                                        if (script->CopyAttrs && (op->Attr & FILE_ATTRIBUTE_ARCHIVE) == 0) // the Archive attribute was not set; MoveFile turned it on, clear it again
                                        {
                                            CStrP targetNameW2(ConvertAllocUtf8ToWide(op->TargetName, -1));
                                            if (targetNameW2 != NULL)
                                                SetFileAttributesW(targetNameW2, op->Attr); // leave without handling or retry, not important (it normally toggles chaotically)
                                        }
                                        if (!SalMoveFile(tmpName, origFullName))
                                        { // this apparently can happen; inexplicably, Windows creates a file named origFullName instead of op->TargetName (the DOS name)
                                            TRACE_I("DoMoveFile(): Unexpected situation: unable to rename file/dir from tmp-name to original long file name! " << origFullName);
                                            if (moveDone)
                                            {
                                                if (SalMoveFile(op->TargetName, sourceNameMvDir))
                                                    moveDone = FALSE;
                                                if (!SalMoveFile(tmpName, origFullName))
                                                    TRACE_E("DoMoveFile(): Fatal unexpected situation: unable to rename file/dir from tmp-name to original long file name! " << origFullName);
                                            }
                                        }
                                        else
                                        {
                                            if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                                SetFileAttributesUtf8(origFullName, origFullNameAttr); // leave without handling or retry, not important (it normally toggles chaotically)
                                        }

                                        if (moveDone)
                                            goto OPERATION_DONE;
                                    }
                                }
                                else
                                    TRACE_E("DoMoveFile(): Original full file/dir name is too long, unable to bypass only-dos-name-overwrite problem!");
                            }
                        }
                    }
                }

                if ((err == ERROR_ALREADY_EXISTS || // theoretically can happen for directories; prevent that (overwrite prompt is only for files)
                     err == ERROR_FILE_EXISTS) &&
                    !dir && StrICmp(op->SourceName, op->TargetName) != 0 &&
                    sourceNameMvDir == op->SourceName && targetNameMvDir == op->TargetName) // no invalid names allowed here (files only, and their names are validated)
                {
                    HANDLE in, out;
                    in = HANDLES_Q(CreateFileUtf8(op->SourceName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
                    if (in == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        goto NORMAL_ERROR;
                    }
                    out = HANDLES_Q(CreateFileUtf8(op->TargetName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL));
                    if (out == INVALID_HANDLE_VALUE)
                    {
                        err = GetLastError();
                        HANDLES(CloseHandle(in));
                        goto NORMAL_ERROR;
                    }

                    if (!dlgData.OverwriteAll && (dlgData.CnfrmFileOver || script->OverwriteOlder))
                    {
                        char sAttr[101], tAttr[101];
                        BOOL getTimeFailed;
                        getTimeFailed = FALSE;
                        FILETIME sFileTime, tFileTime;
                        GetFileOverwriteInfo(sAttr, _countof(sAttr), in, op->SourceName, &sFileTime, &getTimeFailed);
                        GetFileOverwriteInfo(tAttr, _countof(tAttr), out, op->TargetName, &tFileTime, &getTimeFailed);
                        HANDLES(CloseHandle(in));
                        HANDLES(CloseHandle(out));

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_OPEN;

                        if (dir)
                            TRACE_E("Error in script.");

                        if (dlgData.SkipAllOverwrite)
                            goto SKIP_OPEN;

                        int ret;
                        ret = IDCANCEL;

                        if (!getTimeFailed && script->OverwriteOlder) // option from the Copy/Move dialog
                        {
                            // trim timestamps to seconds (different file systems store times with different precision, leading to "differences" even between "matching" times)
                            *(unsigned __int64*)&sFileTime = *(unsigned __int64*)&sFileTime - (*(unsigned __int64*)&sFileTime % 10000000);
                            *(unsigned __int64*)&tFileTime = *(unsigned __int64*)&tFileTime - (*(unsigned __int64*)&tFileTime % 10000000);

                            if (CompareFileTime(&sFileTime, &tFileTime) > 0)
                                ret = IDYES; // older ones should be overwritten without asking
                            else
                                ret = IDB_SKIP; // skip the other existing ones
                        }
                        else
                        {
                            // display the prompt
                            char* data[5];
                            data[0] = (char*)&ret;
                            data[1] = op->TargetName;
                            data[2] = tAttr;
                            data[3] = op->SourceName;
                            data[4] = sAttr;
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 1, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.OverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllOverwrite = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_OPEN:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                            return TRUE;
                        }

                        case IDCANCEL:
                        {
                        CANCEL_OPEN:

                            return FALSE;
                        }
                        }
                    }
                    else
                    {
                        HANDLES(CloseHandle(in));
                        HANDLES(CloseHandle(out));
                    }

                    DWORD attr = SalGetFileAttributes(op->TargetName);
                    if (attr != INVALID_FILE_ATTRIBUTES && (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)))
                    {
                        if (!dlgData.OverwriteHiddenAll && dlgData.CnfrmSHFileOver) // ignore script->OverwriteOlder here; user wants to see that this is a SYSTEM or HIDDEN file even with the option enabled
                        {
                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                goto CANCEL_OPEN;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (dlgData.SkipAllSystemOrHidden)
                                goto SKIP_OPEN;

                            int ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_CONFIRMFILEOVERWRITING);
                            data[2] = op->TargetName;
                            data[3] = LoadStr(IDS_WANTOVERWRITESHFILE);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                            switch (ret)
                            {
                            case IDB_ALL:
                                dlgData.OverwriteHiddenAll = TRUE;
                            case IDYES:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllSystemOrHidden = TRUE;
                            case IDB_SKIP:
                                goto SKIP_OPEN;

                            case IDCANCEL:
                                goto CANCEL_OPEN;
                            }
                            attr = SalGetFileAttributes(op->TargetName); // may also fail (returns INVALID_FILE_ATTRIBUTES)
                        }
                    }

                    ClearReadOnlyAttr(op->TargetName, attr); // make sure it can be deleted ...
                    while (1)
                    {
                        if (DeleteFileUtf8(op->TargetName))
                            break;
                        else
                        {
                            DWORD err2 = GetLastError();
                            if (err2 == ERROR_FILE_NOT_FOUND)
                                break; // if the user already deleted the file manually, everything is fine

                            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                            if (*dlgData.CancelWorker)
                                return FALSE;

                            if (dir)
                                TRACE_E("Error in script.");

                            if (dlgData.SkipAllOverwriteErr)
                                goto SKIP_OVERWRITE_ERROR;

                            int ret;
                            ret = IDCANCEL;
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = LoadStr(IDS_ERROROVERWRITINGFILE);
                            data[2] = op->TargetName;
                            data[3] = GetErrorText(err2);
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                            switch (ret)
                            {
                            case IDRETRY:
                                break;

                            case IDB_SKIPALL:
                                dlgData.SkipAllOverwriteErr = TRUE;
                            case IDB_SKIP:
                            {
                            SKIP_OVERWRITE_ERROR:

                                totalDone += op->Size;
                                script->SetProgressSize(totalDone);
                                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                                return TRUE;
                            }

                            case IDCANCEL:
                                return FALSE;
                            }
                        }
                    }
                }
                else
                {
                NORMAL_ERROR:

                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllMoveErrors)
                        goto SKIP_MOVE_ERROR;

                    if (err == ERROR_SHARING_VIOLATION && ++autoRetryAttempts <= 2)
                    {               // auto-retry added to handle move errors while directory icons are being read (SHGetFileInfo running in parallel with MoveFile)
                        Sleep(100); // wait a moment before the next attempt
                    }
                    else
                    {
                        int ret;
                        ret = IDCANCEL;
                        char* data[4];
                        data[0] = (char*)&ret;
                        data[1] = op->SourceName;
                        data[2] = op->TargetName;
                        data[3] = GetErrorText(err);
                        SendMessage(hProgressDlg, WM_USER_DIALOG, dir ? 4 : 3, (LPARAM)data);
                        switch (ret)
                        {
                        case IDRETRY:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllMoveErrors = TRUE;
                        case IDB_SKIP:
                        {
                        SKIP_MOVE_ERROR:

                            totalDone += op->Size;
                            script->SetProgressSize(totalDone);
                            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                            return TRUE;
                        }

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                }
            }
        }
    }
    else
    {
        if (dir)
        {
            TRACE_E("Error in script.");
            return FALSE;
        }

        BOOL skip;
        BOOL suspiciousIoRetry;
        DWORD err = NO_ERROR;
        BOOL notError = DoCopyFile(op, hProgressDlg, buffer, script, totalDone,
                                   clearReadonlyMask, &skip, lantasticCheck,
                                   mustDeleteFileBeforeOverwrite, allocWholeFileOnStart,
                                   dlgData, copyADS, copyAsEncrypted, TRUE, asyncPar, &suspiciousIoRetry);
        if (notError && !skip) // still need to clean up the file from the source
        {
            // Retry-resume and retry-copy paths are evidence that the I/O path
            // was unstable.  Re-read both closed files and compare their full
            // SHA-256 digests before this move is allowed to delete its source.
            while (suspiciousIoRetry && !VerifyFullFileContentSha256(op->SourceName, op->TargetName, &err))
            {
                TRACE_I("DoMoveFile(): full SHA-256 verification failed for " << op->TargetName << ": " << GetErrorText(err));
                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE);
                if (*dlgData.CancelWorker)
                    return FALSE;

                if (dlgData.SkipAllDeleteErr)
                    return TRUE; // retain the source; the verified delete precondition was not met

                int ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORDELETINGFILE);
                data[2] = op->SourceName;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDeleteErr = TRUE;
                case IDB_SKIP:
                    return TRUE; // retain the source; the verified delete precondition was not met

                case IDCANCEL:
                    return FALSE;
                }
            }

            ClearReadOnlyAttr(op->SourceName); // ensure it can be deleted
            while (1)
            {
                if (DeleteFileUtf8(op->SourceName))
                    break;
                {
                    DWORD err = GetLastError();

                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllDeleteErr)
                        return TRUE;

                    int ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_ERRORDELETINGFILE);
                    data[2] = op->SourceName;
                    data[3] = GetErrorText(err);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                    switch (ret)
                    {
                    case IDRETRY:
                        break;
                    case IDB_SKIPALL:
                        dlgData.SkipAllDeleteErr = TRUE;
                    case IDB_SKIP:
                        return TRUE;
                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
        }
        return notError;
    }
}

BOOL DoDeleteFile(HWND hProgressDlg, char* name, const CQuadWord& size, COperations* script,
                  CQuadWord& totalDone, DWORD attr, CProgressDlgData& dlgData)
{
    // if the path ends with a space/dot it is invalid and we must not delete it,
    // DeleteFile would trim the spaces/dots and remove a different file
    BOOL invalidName = FileNameIsInvalid(name, TRUE);

    DWORD err;
    while (1)
    {
        if (!invalidName)
        {
            if (attr & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            {
                if (!dlgData.DeleteHiddenAll && dlgData.CnfrmSHFileDel)
                {
                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                    if (*dlgData.CancelWorker)
                        return FALSE;

                    if (dlgData.SkipAllSystemOrHidden)
                        goto SKIP_DELETE;

                    int ret = IDCANCEL;
                    char* data[4];
                    data[0] = (char*)&ret;
                    data[1] = LoadStr(IDS_CONFIRMSHFILEDELETE);
                    data[2] = name;
                    data[3] = LoadStr(IDS_DELETESHFILE);
                    SendMessage(hProgressDlg, WM_USER_DIALOG, 2, (LPARAM)data);
                    switch (ret)
                    {
                    case IDB_ALL:
                        dlgData.DeleteHiddenAll = TRUE;
                    case IDYES:
                        break;

                    case IDB_SKIPALL:
                        dlgData.SkipAllSystemOrHidden = TRUE;
                    case IDB_SKIP:
                        goto SKIP_DELETE;

                    case IDCANCEL:
                        return FALSE;
                    }
                }
            }
            ClearReadOnlyAttr(name, attr); // ensure it can be deleted

            err = ERROR_SUCCESS;
            BOOL useRecycleBin;
            switch (dlgData.UseRecycleBin)
            {
            case 0:
                useRecycleBin = script->CanUseRecycleBin && script->InvertRecycleBin;
                break;
            case 1:
                useRecycleBin = script->CanUseRecycleBin && !script->InvertRecycleBin;
                break;
            case 2:
            {
                if (!script->CanUseRecycleBin || script->InvertRecycleBin)
                    useRecycleBin = FALSE;
                else
                {
                    const char* fileName = strrchr(name, '\\');
                    if (fileName != NULL) // "always true"
                    {
                        fileName++;
                        int tmpLen = lstrlen(fileName);
                        const char* ext = fileName + tmpLen;
                        //            while (ext > fileName && *ext != '.') ext--;
                        while (--ext >= fileName && *ext != '.')
                            ;
                        //            if (ext == fileName)   // ".cvspass" is treated as an extension in Windows ...
                        if (ext < fileName)
                            ext = fileName + tmpLen;
                        else
                            ext++;
                        useRecycleBin = dlgData.AgreeRecycleMasks(fileName, ext);
                    }
                    else
                    {
                        useRecycleBin = TRUE; // choose the safe option on error and delete via the Recycle Bin
                        TRACE_E("DoDeleteFile(): unexpected situation: filename does not contain backslash: " << name);
                    }
                }
                break;
            }
            }
            if (useRecycleBin)
            {
                if (!PathContainsValidComponents((char*)name, FALSE))
                {
                    err = ERROR_INVALID_NAME;
                }
                else
                {
                    CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
                    if (nameW == NULL)
                    {
                        err = ERROR_NO_UNICODE_TRANSLATION;
                    }
                    else
                    {
                        int nameLen = lstrlenW(nameW);
                        WCHAR* nameListW = (WCHAR*)malloc((nameLen + 2) * sizeof(WCHAR));
                        if (nameListW == NULL)
                        {
                            err = ERROR_NOT_ENOUGH_MEMORY;
                        }
                        else
                        {
                            memcpy(nameListW, nameW, (nameLen + 1) * sizeof(WCHAR));
                            nameListW[nameLen + 1] = 0; // double null

                            CShellExecuteWnd shellExecuteWnd;
                            SHFILEOPSTRUCTW opCode;
                            memset(&opCode, 0, sizeof(opCode));

                            opCode.hwnd = shellExecuteWnd.Create(hProgressDlg, "SEW: DoDeleteFile");

                            opCode.wFunc = FO_DELETE;
                            opCode.pFrom = nameListW;
                            opCode.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION;
                            opCode.lpszProgressTitle = L"";
                            err = SHFileOperationW(&opCode);
                            free(nameListW);
                        }
                    }
                }
            }
            else
            {
                if (DeleteFileUtf8(name) == 0)
                    err = GetLastError();
            }
        }
        else
        {
            err = ERROR_INVALID_NAME;
        }
        if (err == ERROR_SUCCESS)
        {
            totalDone += size;
            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDeleteErr)
                goto SKIP_DELETE;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORDELETINGFILE);
            data[2] = name;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllDeleteErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_DELETE:

                totalDone += size;
                SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                return TRUE;
            }

            case IDCANCEL:
                return FALSE;
            }
        }
        if (!invalidName)
        {
            DWORD attr2 = SalGetFileAttributes(name); // get the current attribute state
            if (attr2 != INVALID_FILE_ATTRIBUTES)
                attr = attr2;
        }
    }
}

BOOL SalCreateDirectoryEx(const char* name, DWORD* err)
{
    if (err != NULL)
        *err = 0;
    // if the name ends with a space/dot we must append '\\', otherwise CreateDirectory
    // quietly trims the trailing spaces/dots and creates a different directory
    const char* nameCrDir = name;
    char nameCrDirBuf[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameCrDir, nameCrDirBuf);
    CStrP nameCrDirW(ConvertAllocUtf8ToWide(nameCrDir, -1));
    if (nameCrDirW == NULL)
    {
        if (err != NULL)
            *err = ERROR_NO_UNICODE_TRANSLATION;
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return FALSE;
    }
    if (CreateDirectoryW(nameCrDirW, NULL))
        return TRUE;
    else
    {
        DWORD errLoc = GetLastError();
        if (name == nameCrDir &&            // a name ending with a space/dot cannot collide with a DOS name
            (errLoc == ERROR_FILE_EXISTS || // check whether this is only overwriting the file's DOS name
             errLoc == ERROR_ALREADY_EXISTS))
        {
            WIN32_FIND_DATAW data;
            CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
            HANDLE find = nameW != NULL ? HANDLES_Q(FindFirstFileW(nameW, &data)) : INVALID_HANDLE_VALUE;
            if (find != INVALID_HANDLE_VALUE)
            {
                HANDLES(FindClose(find));
                const char* tgtName = SalPathFindFileName(name);
                char altName[MAX_PATH];
                char fullName[MAX_PATH];
                if (ConvertWideToUtf8(data.cAlternateFileName, -1, altName, _countof(altName)) == 0)
                    altName[0] = 0;
                if (ConvertWideToUtf8(data.cFileName, -1, fullName, _countof(fullName)) == 0)
                    fullName[0] = 0;
                if (StrICmp(tgtName, altName) == 0 && // match only for the DOS name
                    StrICmp(tgtName, fullName) != 0)  // (the full name differs)
                {
                    // rename ("tidy up") the file/directory whose DOS name conflicts to a temporary 8.3 name (no extra DOS name needed)
                    char tmpName[MAX_PATH + 20];
                    if (strlen(name) >= _countof(tmpName))
                    {
                        TRACE_E("SalCreateDirectoryEx(): path too long for DOS-name collision workaround: " << name);
                    }
                    else
                    {
                        lstrcpyn(tmpName, name, _countof(tmpName));
                        CutDirectory(tmpName);
                        SalPathAddBackslash(tmpName, _countof(tmpName));
                        char* tmpNamePart = tmpName + strlen(tmpName);
                        char origFullName[MAX_PATH + 20];
                        if (SalPathAppend(tmpName, fullName, _countof(tmpName)))
                        {
                            strcpy(origFullName, tmpName);
                            DWORD num = (GetTickCount() / 10) % 0xFFF;
                            DWORD origFullNameAttr = SalGetFileAttributes(origFullName);
                            while (1)
                            {
                                sprintf(tmpNamePart, "sal%03X", num++);
                                if (SalMoveFile(origFullName, tmpName))
                                    break;
                                DWORD e = GetLastError();
                                if (e != ERROR_FILE_EXISTS && e != ERROR_ALREADY_EXISTS)
                                {
                                    tmpName[0] = 0;
                                    break;
                                }
                            }
                            if (tmpName[0] != 0) // if we managed to "tidy up" the conflicting file, retry the move
                            {                    // and then restore the original name of the "tidied" file
                                BOOL createDirDone = nameW != NULL ? CreateDirectoryW(nameW, NULL) : FALSE;
                                if (!SalMoveFile(tmpName, origFullName))
                                { // this can apparently happen: inexplicably Windows creates a file named origFullName instead of name (the DOS name)
                                    TRACE_I("Unexpected situation: unable to rename file from tmp-name to original long file name! " << origFullName);
                                    if (createDirDone)
                                    {
                                        if (nameW != NULL && RemoveDirectoryW(nameW))
                                            createDirDone = FALSE;
                                        if (!SalMoveFile(tmpName, origFullName))
                                            TRACE_E("Fatal unexpected situation: unable to rename file from tmp-name to original long file name! " << origFullName);
                                    }
                                }
                                else
                                {
                                    if ((origFullNameAttr & FILE_ATTRIBUTE_ARCHIVE) == 0)
                                    {
                                        CStrP origFullNameW(ConvertAllocUtf8ToWide(origFullName, -1));
                                        if (origFullNameW != NULL)
                                            SetFileAttributesW(origFullNameW, origFullNameAttr); // leave it without extra handling or retries; not important (normally toggles unpredictably)
                                    }
                                }

                                if (createDirDone)
                                    return TRUE;
                            }
                        }
                        else
                            TRACE_E("Original full file name is too long, unable to bypass only-dos-name-overwrite problem!");
                    }
                }
            }
        }
        if (err != NULL)
            *err = errLoc;
    }
    return FALSE;
}

BOOL GetDirTime(const char* dirName, FILETIME* ftModified)
{
    HANDLE dir;
    CStrP dirNameW(ConvertAllocUtf8ToWide(dirName, -1));
    dir = dirNameW != NULL
              ? HANDLES_Q(CreateFileW(dirNameW, GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      NULL, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS,
                                      NULL))
              : INVALID_HANDLE_VALUE;
    if (dir != INVALID_HANDLE_VALUE)
    {
        BOOL ret = GetFileTime(dir, NULL /*ftCreated*/, NULL /*ftAccessed*/, ftModified);
        HANDLES(CloseHandle(dir));
        return ret;
    }
    return FALSE;
}

BOOL DoCopyDirTime(HWND hProgressDlg, const char* targetName, FILETIME* modified, CProgressDlgData& dlgData, BOOL quiet)
{
    // if the path ends with a space/dot, we must append '\\', otherwise CreateFile
    // trims the spaces/dots and works with a different path
    const char* targetNameCrFile = targetName;
    char targetNameCrFileCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(targetNameCrFile, targetNameCrFileCopy);
    CStrP targetNameW(ConvertAllocUtf8ToWide(targetNameCrFile, -1));

    BOOL showError = !quiet;
    DWORD error = NO_ERROR;
    DWORD attr = targetNameW != NULL ? GetFileAttributesW(targetNameW) : INVALID_FILE_ATTRIBUTES;
    BOOL setAttr = FALSE;
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
    {
        if (targetNameW != NULL)
            SetFileAttributesW(targetNameW, attr & ~FILE_ATTRIBUTE_READONLY);
        setAttr = TRUE;
    }
    HANDLE file;
    file = targetNameW != NULL
               ? HANDLES_Q(CreateFileW(targetNameW, GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       NULL, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS,
                                       NULL))
               : INVALID_HANDLE_VALUE;
    if (file != INVALID_HANDLE_VALUE)
    {
        if (SetFileTime(file, NULL /*&ftCreated*/, NULL /*&ftAccessed*/, modified))
            showError = FALSE; // success!
        else
            error = GetLastError();
        HANDLES(CloseHandle(file));
    }
    else
        error = GetLastError();
    if (setAttr)
    {
        if (targetNameW != NULL)
            SetFileAttributesW(targetNameW, attr);
    }

    if (showError)
    {
        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
        if (*dlgData.CancelWorker)
            return FALSE;

        int ret;
        ret = IDCANCEL;
        if (dlgData.IgnoreAllCopyDirTimeErr)
            ret = IDB_IGNORE;
        else
        {
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = (char*)targetNameCrFile;
            data[2] = (char*)(DWORD_PTR)error;
            SendMessage(hProgressDlg, WM_USER_DIALOG, 11, (LPARAM)data);
        }
        switch (ret)
        {
        case IDB_IGNOREALL:
            dlgData.IgnoreAllCopyDirTimeErr = TRUE; // break intentionally omitted here
        case IDB_IGNORE:
            break;

        case IDCANCEL:
            return FALSE;
        }
    }
    return TRUE;
}

BOOL DoCreateDir(HWND hProgressDlg, char* name, DWORD attr,
                 DWORD clearReadonlyMask, CProgressDlgData& dlgData,
                 CQuadWord& totalDone, CQuadWord& operTotal,
                 const char* sourceDir, BOOL adsCopy, COperations* script,
                 void* buffer, BOOL& skip, BOOL& alreadyExisted,
                 BOOL createAsEncrypted, BOOL ignInvalidName)
{
    if (script->CopyAttrs && createAsEncrypted)
        TRACE_E("DoCreateDir(): unexpected parameter value: createAsEncrypted is TRUE when script->CopyAttrs is TRUE!");

    skip = FALSE;
    alreadyExisted = FALSE;
    CQuadWord lastTransferredFileSize;
    script->GetTFS(&lastTransferredFileSize);

    BOOL invalidName = FileNameIsInvalid(name, TRUE, ignInvalidName);

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameCrDir = name;
    char nameCrDirCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameCrDir, nameCrDirCopy);
    const char* sourceDirCrDir = sourceDir;
    char sourceDirCrDirCopy[3 * MAX_PATH];
    if (sourceDirCrDir != NULL)
        MakeCopyWithBackslashIfNeeded(sourceDirCrDir, sourceDirCrDirCopy);

    while (1)
    {
        DWORD err;
        if (!invalidName && SalCreateDirectoryEx(name, &err))
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)CREATE_DIR_SIZE.Value, TRUE, 0, NULL, MAX_OP_FILESIZE); // directory already created

            DWORD newAttr = attr & clearReadonlyMask;
            if (sourceDir != NULL && adsCopy) // copy ADS when required
            {
                CQuadWord operDone = CREATE_DIR_SIZE; // directory already created
                BOOL adsSkip = FALSE;
                if (!DoCopyADS(hProgressDlg, sourceDir, TRUE, name, totalDone,
                               operDone, operTotal, dlgData, script, &adsSkip, buffer) ||
                    adsSkip) // user cancelled or skipped at least one ADS
                {
                    if (RemoveDirectoryUtf8(nameCrDir) == 0)
                    {
                        DWORD err2 = GetLastError();
                        TRACE_E("Unable to remove newly created directory: " << name << ", error: " << GetErrorText(err2));
                    }
                    if (!adsSkip)
                        return FALSE; // cancel the entire operation (Skip must return TRUE)
                    skip = TRUE;
                    newAttr = -1; // the directory should no longer exist, so do not apply attributes
                }
            }
            if (newAttr != -1)
            {
                if (script->CopyAttrs || createAsEncrypted) // set Compressed & Encrypted attributes based on the source directory
                {
                    if (createAsEncrypted)
                    {
                        newAttr &= ~FILE_ATTRIBUTE_COMPRESSED;
                        newAttr |= FILE_ATTRIBUTE_ENCRYPTED;
                    }
                    DWORD changeAttrErr = NO_ERROR;
                    DWORD currentAttrs = SalGetFileAttributes(name);
                    if (currentAttrs != INVALID_FILE_ATTRIBUTES)
                    {
                        if ((newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) == 0)
                        {
                            changeAttrErr = UncompressFile(name, currentAttrs);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_ENCRYPTED) != (currentAttrs & FILE_ATTRIBUTE_ENCRYPTED))
                        {
                            BOOL dummyCancelOper = FALSE;
                            if (newAttr & FILE_ATTRIBUTE_ENCRYPTED)
                            {
                                changeAttrErr = MyEncryptFile(hProgressDlg, name, currentAttrs, 0 /* allow encrypting directories with the SYSTEM attribute */,
                                                              dlgData, dummyCancelOper, FALSE);

                                if ( //(WindowsVistaAndLater || script->TargetPathSupEFS) &&  // complain regardless of OS version and EFS support; originally directories on FAT could not be encrypted before Vista, we behave the same (to match Explorer, the Encrypted attribute is not that important)
                                    !dlgData.DirCrLossEncrAll && changeAttrErr != ERROR_SUCCESS)
                                {                                                              // failed to set the Encrypted attribute on the directory, ask the user what to do
                                    WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                                    if (*dlgData.CancelWorker)
                                        goto CANCEL_CRDIR;

                                    int ret;
                                    if (dlgData.SkipAllDirCrLossEncr)
                                        ret = IDB_SKIP;
                                    else
                                    {
                                        ret = IDCANCEL;
                                        char* data[4];
                                        data[0] = (char*)&ret;
                                        data[1] = (char*)FALSE;
                                        data[2] = name;
                                        data[3] = (char*)(!script->IsCopyOperation);
                                        SendMessage(hProgressDlg, WM_USER_DIALOG, 12, (LPARAM)data);
                                    }
                                    switch (ret)
                                    {
                                    case IDB_ALL:
                                        dlgData.DirCrLossEncrAll = TRUE; // break intentionally omitted here
                                    case IDYES:
                                        break;

                                    case IDB_SKIPALL:
                                        dlgData.SkipAllDirCrLossEncr = TRUE;
                                    case IDB_SKIP:
                                    {
                                        ClearReadOnlyAttr(nameCrDir); // remove read-only attribute so the file can be deleted
                                        RemoveDirectoryUtf8(nameCrDir);
                                        script->SetTFS(lastTransferredFileSize); // add TFS only after the directory is fully outside; ProgressSize will be synced outside (no point in adjusting it here)
                                        skip = TRUE;
                                        return TRUE;
                                    }

                                    case IDCANCEL:
                                        goto CANCEL_CRDIR;
                                    }
                                }
                            }
                            else
                                changeAttrErr = MyDecryptFile(name, currentAttrs, FALSE);
                        }
                        if (changeAttrErr == NO_ERROR &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != (currentAttrs & FILE_ATTRIBUTE_COMPRESSED) &&
                            (newAttr & FILE_ATTRIBUTE_COMPRESSED) != 0)
                        {
                            changeAttrErr = CompressFile(name, currentAttrs);
                        }
                    }
                    else
                        changeAttrErr = GetLastError();
                    if (changeAttrErr != NO_ERROR)
                    {
                        TRACE_I("DoCreateDir(): Unable to set Encrypted or Compressed attributes for " << name << "! error=" << GetErrorText(changeAttrErr));
                    }
                }
                SetFileAttributesUtf8(nameCrDir, newAttr);

                if (script->CopyAttrs) // verify whether the source file attributes were preserved
                {
                    DWORD curAttrs;
                    curAttrs = SalGetFileAttributes(name);
                    if (curAttrs == INVALID_FILE_ATTRIBUTES || (curAttrs & DISPLAYED_ATTRIBUTES) != (newAttr & DISPLAYED_ATTRIBUTES))
                    {                                                              // attributes probably did not transfer; warn the user
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllSetAttrsErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = name;
                            data[2] = (char*)(DWORD_PTR)(newAttr & DISPLAYED_ATTRIBUTES);
                            data[3] = (char*)(DWORD_PTR)(curAttrs == INVALID_FILE_ATTRIBUTES ? 0 : (curAttrs & DISPLAYED_ATTRIBUTES));
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 9, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllSetAttrsErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                        {
                        CANCEL_CRDIR:

                            ClearReadOnlyAttr(nameCrDir); // remove read-only so the file can be deleted
                            RemoveDirectoryUtf8(nameCrDir);
                            return FALSE;
                        }
                        }
                    }
                }

                if (sourceDir != NULL && script->CopySecurity) // should NTFS security permissions be copied?
                {
                    DWORD err2;
                    if (!DoCopySecurity(sourceDir, name, &err2, NULL))
                    {
                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            goto CANCEL_CRDIR;

                        int ret;
                        ret = IDCANCEL;
                        if (dlgData.IgnoreAllCopyPermErr)
                            ret = IDB_IGNORE;
                        else
                        {
                            char* data[4];
                            data[0] = (char*)&ret;
                            data[1] = (char*)sourceDir;
                            data[2] = name;
                            data[3] = (char*)(DWORD_PTR)err2;
                            SendMessage(hProgressDlg, WM_USER_DIALOG, 10, (LPARAM)data);
                        }
                        switch (ret)
                        {
                        case IDB_IGNOREALL:
                            dlgData.IgnoreAllCopyPermErr = TRUE; // break intentionally omitted here
                        case IDB_IGNORE:
                            break;

                        case IDCANCEL:
                            goto CANCEL_CRDIR;
                        }
                    }
                }
            }
            return TRUE;
        }
        else
        {
            if (invalidName)
                err = ERROR_INVALID_NAME;
            if (err == ERROR_ALREADY_EXISTS ||
                err == ERROR_FILE_EXISTS)
            {
                DWORD attr2 = SalGetFileAttributes(name);
                if (attr2 & FILE_ATTRIBUTE_DIRECTORY) // "directory overwrite"
                {
                    if (dlgData.CnfrmDirOver && !dlgData.DirOverwriteAll) // should we ask the user about overwriting the directory?
                    {
                        char sAttr[101], tAttr[101];
                        GetDirInfo(sAttr, _countof(sAttr), sourceDir);
                        GetDirInfo(tAttr, _countof(tAttr), name);

                        WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                        if (*dlgData.CancelWorker)
                            return FALSE;

                        if (dlgData.SkipAllDirOver)
                            goto SKIP_CREATE_ERROR;

                        int ret = IDCANCEL;
                        char* data[5];
                        data[0] = (char*)&ret;
                        data[1] = name;
                        data[2] = tAttr;
                        data[3] = (char*)sourceDir;
                        data[4] = sAttr;
                        SendMessage(hProgressDlg, WM_USER_DIALOG, 7, (LPARAM)data);
                        switch (ret)
                        {
                        case IDB_ALL:
                            dlgData.DirOverwriteAll = TRUE;
                        case IDYES:
                            break;

                        case IDB_SKIPALL:
                            dlgData.SkipAllDirOver = TRUE;
                        case IDB_SKIP:
                            goto SKIP_CREATE_ERROR;

                        case IDCANCEL:
                            return FALSE;
                        }
                    }
                    alreadyExisted = TRUE;
                    return TRUE; // o.k.
                }

                WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
                if (*dlgData.CancelWorker)
                    return FALSE;

                if (dlgData.SkipAllDirCreate)
                    goto SKIP_CREATE_ERROR;

                int ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORCREATINGDIR);
                data[2] = name;
                data[3] = LoadStr(IDS_NAMEALREADYUSED);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDirCreate = TRUE;
                case IDB_SKIP:
                    goto SKIP_CREATE_ERROR;

                case IDCANCEL:
                    return FALSE;
                }
                continue;
            }

            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDirCreateErr)
                goto SKIP_CREATE_ERROR;

            int ret;
            ret = IDCANCEL;
            char* data[4];
            data[0] = (char*)&ret;
            data[1] = LoadStr(IDS_ERRORCREATINGDIR);
            data[2] = name;
            data[3] = GetErrorText(err);
            SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
            switch (ret)
            {
            case IDRETRY:
                break;

            case IDB_SKIPALL:
                dlgData.SkipAllDirCreateErr = TRUE;
            case IDB_SKIP:
            {
            SKIP_CREATE_ERROR:

                skip = TRUE; // this is a skip (all operations within the directory must be skipped)
                return TRUE;
            }
            case IDCANCEL:
                return FALSE;
            }
        }
    }
}

BOOL DoDeleteDir(HWND hProgressDlg, char* name, const CQuadWord& size, COperations* script,
                 CQuadWord& totalDone, DWORD attr, BOOL dontUseRecycleBin, CProgressDlgData& dlgData)
{
    DWORD err;
    int AutoRetryCounter = 0;
    DWORD startTime = GetTickCount();

    // if the path ends with a space/dot, we must append '\\'; otherwise SetFileAttributes
    // and RemoveDirectory trim the spaces/dots and operate on a different path
    const char* nameRmDir = name;
    char nameRmDirCopy[3 * MAX_PATH];
    MakeCopyWithBackslashIfNeeded(nameRmDir, nameRmDirCopy);

    while (1)
    {
        ClearReadOnlyAttr(nameRmDir, attr); // ensure it can be deleted

        err = ERROR_SUCCESS;
        if (script->CanUseRecycleBin && !dontUseRecycleBin &&
            (script->InvertRecycleBin && dlgData.UseRecycleBin == 0 ||
             !script->InvertRecycleBin && dlgData.UseRecycleBin == 1) &&
            IsDirectoryEmpty(name)) // subdirectory must not contain any files!!!
        {
            if (!PathContainsValidComponents((char*)name, FALSE))
            {
                err = ERROR_INVALID_NAME;
            }
            else
            {
                CStrP nameW(ConvertAllocUtf8ToWide(name, -1));
                if (nameW == NULL)
                {
                    err = ERROR_NO_UNICODE_TRANSLATION;
                }
                else
                {
                    int nameLen = lstrlenW(nameW);
                    WCHAR* nameListW = (WCHAR*)malloc((nameLen + 2) * sizeof(WCHAR));
                    if (nameListW == NULL)
                    {
                        err = ERROR_NOT_ENOUGH_MEMORY;
                    }
                    else
                    {
                        memcpy(nameListW, nameW, (nameLen + 1) * sizeof(WCHAR));
                        nameListW[nameLen + 1] = 0; // double null

                        CShellExecuteWnd shellExecuteWnd;
                        SHFILEOPSTRUCTW opCode;
                        memset(&opCode, 0, sizeof(opCode));

                        opCode.hwnd = shellExecuteWnd.Create(hProgressDlg, "SEW: DoDeleteDir");

                        opCode.wFunc = FO_DELETE;
                        opCode.pFrom = nameListW;
                        opCode.fFlags = FOF_ALLOWUNDO | FOF_SILENT | FOF_NOCONFIRMATION;
                        opCode.lpszProgressTitle = L"";
                        err = SHFileOperationW(&opCode);
                        free(nameListW);
                    }
                }
            }
        }
        else
        {
            if (RemoveDirectoryUtf8(nameRmDir) == 0)
                err = GetLastError();
        }

        if (err == ERROR_SUCCESS)
        {
            script->AddBytesToSpeedMetersAndTFSandPS((DWORD)size.Value, TRUE, 0, NULL, MAX_OP_FILESIZE);

            totalDone += size;
            SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
            return TRUE;
        }
        else
        {
            WaitForSingleObject(dlgData.WorkerNotSuspended, INFINITE); // if we should be in suspend mode, wait ...
            if (*dlgData.CancelWorker)
                return FALSE;

            if (dlgData.SkipAllDeleteErr)
                goto SKIP_DELETE;

            if (AutoRetryCounter < 4 && GetTickCount() - startTime + (AutoRetryCounter + 1) * 100 <= 2000 &&
                (err == ERROR_DIR_NOT_EMPTY || err == ERROR_SHARING_VIOLATION))
            { // add auto-retry to handle this case: I have directories 1\2\3, deleting 1 including subdirectories while 3 is shown in a panel (watching for changes) -> removing 2 reports "directory not empty" because 3 stays in a transitional state due to change notifications (it is deleted, so it cannot be listed, but it still exists on disk briefly; quite a mess)
                //        TRACE_I("DoDeleteDir(): err: " << GetErrorText(err));
                AutoRetryCounter++;
                Sleep(AutoRetryCounter * 100);
                //        TRACE_I("DoDeleteDir(): " << AutoRetryCounter << ". retry, delay is " << AutoRetryCounter * 100 << "ms");
            }
            else
            {
                int ret;
                ret = IDCANCEL;
                char* data[4];
                data[0] = (char*)&ret;
                data[1] = LoadStr(IDS_ERRORDELETINGDIR);
                data[2] = (char*)nameRmDir;
                data[3] = GetErrorText(err);
                SendMessage(hProgressDlg, WM_USER_DIALOG, 0, (LPARAM)data);
                switch (ret)
                {
                case IDRETRY:
                    break;

                case IDB_SKIPALL:
                    dlgData.SkipAllDeleteErr = TRUE;
                case IDB_SKIP:
                {
                SKIP_DELETE:

                    totalDone += size;
                    script->SetProgressSize(totalDone);
                    SetProgress(hProgressDlg, 0, CaclProg(totalDone, script->TotalSize), dlgData);
                    return TRUE;
                }

                case IDCANCEL:
                    return FALSE;
                }
            }
        }

        DWORD attr2 = SalGetFileAttributes(nameRmDir); // get the current attribute state
        if (attr2 != INVALID_FILE_ATTRIBUTES)
            attr = attr2;
    }
}

#define FSCTL_GET_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS)        // REPARSE_DATA_BUFFER
#define FSCTL_DELETE_REPARSE_POINT CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 43, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) // REPARSE_DATA_BUFFER,

#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)

/*  This code copies a junction point into an empty directory (the directory must be created in
    advance � to keep it simple we always use "D:\\ZUMPA\\link" here).

  People sometimes want to copy the contents of the junction, sometimes they want to copy only the junction as a link,
  and sometimes they want to skip it (unclear whether that should create an empty junction directory)...
  if we ever implement it properly, the script builder will need a comprehensive dialog asking what to do.

#define FSCTL_SET_REPARSE_POINT     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 41, METHOD_BUFFERED, FILE_SPECIAL_ACCESS) // REPARSE_DATA_BUFFER,
#define FSCTL_GET_REPARSE_POINT     CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 42, METHOD_BUFFERED, FILE_ANY_ACCESS) // REPARSE_DATA_BUFFER

// Structure for FSCTL_SET_REPARSE_POINT, FSCTL_GET_REPARSE_POINT, and
// FSCTL_DELETE_REPARSE_POINT.
// This version of the reparse data buffer is only for Microsoft tags.
*/

