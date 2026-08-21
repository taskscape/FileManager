// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include <Aclapi.h>
#include <Ntsecapi.h>

#include "security_helpers.h"

// Privilege setup, admin check, and best-effort security-descriptor copy extracted
// from async_copy.cpp as a mechanical move; behavior is unchanged.
BOOL HaveWriteOwnerRight = FALSE; // does the process have the WRITE_OWNER right?
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

    // This token-query buffer never crosses an API ownership boundary, so keep it on the process heap.
    TokenGroupList = (PTOKEN_GROUPS)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, InfoLength);

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
      HeapFree(GetProcessHeap(), 0, TokenGroupList);
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
    HeapFree(GetProcessHeap(), 0, TokenGroupList);
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

// Security descriptors cannot be treated as a single best-effort SetNamedSecurityInfo
// call.  Windows may accept one component and reject another (in particular when
// SeRestorePrivilege is absent), which can leave a copied file with a more open or
// less accessible DACL than either the source or the pre-copy target.  Keep the
// comparison deliberately at the components covered by the metadata contract:
// owner, group, DACL protection/inheritance, and every explicit ACE (including
// explicit ACCESS_DENIED ACEs).
static BOOL AreEqualSids(PSID first, PSID second)
{
    return first == second || (first != NULL && second != NULL && EqualSid(first, second));
}

static BOOL GetDaclProtection(PSECURITY_DESCRIPTOR securityDescriptor, BOOL* protectedDacl)
{
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;
    if (!GetSecurityDescriptorControl(securityDescriptor, &control, &revision))
        return FALSE;
    *protectedDacl = (control & SE_DACL_PROTECTED) != 0;
    return TRUE;
}

static BOOL AreEqualExplicitAces(PACL first, PACL second)
{
    if (first == NULL || second == NULL)
        return first == second; // a NULL DACL grants full access and must never be approximated

    ACL_SIZE_INFORMATION firstInfo;
    ACL_SIZE_INFORMATION secondInfo;
    if (!GetAclInformation(first, &firstInfo, sizeof(firstInfo), AclSizeInformation) ||
        !GetAclInformation(second, &secondInfo, sizeof(secondInfo), AclSizeInformation))
    {
        return FALSE;
    }

    // Compare each explicit ACE as a multiset.  Inherited entries can legitimately
    // differ after a copy into a different directory, but an explicit allow or deny
    // entry is part of the source descriptor and must survive byte-for-byte.
    for (DWORD firstIndex = 0; firstIndex < firstInfo.AceCount; firstIndex++)
    {
        PVOID firstAce = NULL;
        if (!GetAce(first, firstIndex, &firstAce))
            return FALSE;
        ACE_HEADER* firstHeader = (ACE_HEADER*)firstAce;
        if ((firstHeader->AceFlags & INHERITED_ACE) != 0)
            continue;

        DWORD firstCount = 0;
        DWORD secondCount = 0;
        for (DWORD index = 0; index < firstInfo.AceCount; index++)
        {
            PVOID ace = NULL;
            if (!GetAce(first, index, &ace))
                return FALSE;
            ACE_HEADER* header = (ACE_HEADER*)ace;
            if ((header->AceFlags & INHERITED_ACE) == 0 && header->AceSize == firstHeader->AceSize &&
                memcmp(header, firstHeader, firstHeader->AceSize) == 0)
            {
                firstCount++;
            }
        }
        for (DWORD index = 0; index < secondInfo.AceCount; index++)
        {
            PVOID ace = NULL;
            if (!GetAce(second, index, &ace))
                return FALSE;
            ACE_HEADER* header = (ACE_HEADER*)ace;
            if ((header->AceFlags & INHERITED_ACE) == 0 && header->AceSize == firstHeader->AceSize &&
                memcmp(header, firstHeader, firstHeader->AceSize) == 0)
            {
                secondCount++;
            }
        }
        if (firstCount != secondCount)
            return FALSE;
    }

    // The loop above detects missing source ACEs.  Repeat in the other direction
    // so a target's pre-existing explicit allow cannot make the output permissive.
    for (DWORD secondIndex = 0; secondIndex < secondInfo.AceCount; secondIndex++)
    {
        PVOID secondAce = NULL;
        if (!GetAce(second, secondIndex, &secondAce))
            return FALSE;
        ACE_HEADER* secondHeader = (ACE_HEADER*)secondAce;
        if ((secondHeader->AceFlags & INHERITED_ACE) != 0)
            continue;

        DWORD firstCount = 0;
        DWORD secondCount = 0;
        for (DWORD index = 0; index < firstInfo.AceCount; index++)
        {
            PVOID ace = NULL;
            if (!GetAce(first, index, &ace))
                return FALSE;
            ACE_HEADER* header = (ACE_HEADER*)ace;
            if ((header->AceFlags & INHERITED_ACE) == 0 && header->AceSize == secondHeader->AceSize &&
                memcmp(header, secondHeader, secondHeader->AceSize) == 0)
            {
                firstCount++;
            }
        }
        for (DWORD index = 0; index < secondInfo.AceCount; index++)
        {
            PVOID ace = NULL;
            if (!GetAce(second, index, &ace))
                return FALSE;
            ACE_HEADER* header = (ACE_HEADER*)ace;
            if ((header->AceFlags & INHERITED_ACE) == 0 && header->AceSize == secondHeader->AceSize &&
                memcmp(header, secondHeader, secondHeader->AceSize) == 0)
            {
                secondCount++;
            }
        }
        if (firstCount != secondCount)
            return FALSE;
    }
    return TRUE;
}

static BOOL IsSecurityDescriptorPreserved(PSID sourceOwner, PSID sourceGroup, PACL sourceDacl,
                                          PSECURITY_DESCRIPTOR sourceDescriptor,
                                          PSID targetOwner, PSID targetGroup, PACL targetDacl,
                                          PSECURITY_DESCRIPTOR targetDescriptor)
{
    BOOL sourceProtected;
    BOOL targetProtected;
    return AreEqualSids(sourceOwner, targetOwner) &&
           AreEqualSids(sourceGroup, targetGroup) &&
           GetDaclProtection(sourceDescriptor, &sourceProtected) &&
           GetDaclProtection(targetDescriptor, &targetProtected) &&
           sourceProtected == targetProtected &&
           AreEqualExplicitAces(sourceDacl, targetDacl);
}

static DWORD SetDaclWithInheritance(const WCHAR* targetName, PACL dacl, BOOL inheritedDacl)
{
    return SetNamedSecurityInfoW((LPWSTR)targetName, SE_FILE_OBJECT,
                                 DACL_SECURITY_INFORMATION |
                                     (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                                 NULL, NULL, dacl, NULL);
}

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
    if (*err != ERROR_SUCCESS)
    {
        if (srcSD != NULL)
            LocalFree(srcSD);
        return FALSE; // inaccessible source descriptor: report a best-effort metadata loss without touching the target
    }

    CStrP targetNameSecW(ConvertAllocUtf8ToWide(targetNameSec, -1));
    if (targetNameSecW == NULL)
    {
        *err = ERROR_NO_UNICODE_TRANSLATION;
        LocalFree(srcSD);
        return FALSE;
    }

    PSID previousOwner = NULL;
    PSID previousGroup = NULL;
    PACL previousDacl = NULL;
    PSECURITY_DESCRIPTOR previousDescriptor = NULL;
    *err = GetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                 DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                 &previousOwner, &previousGroup, &previousDacl, NULL, &previousDescriptor);
    if (*err != ERROR_SUCCESS)
    {
        LocalFree(srcSD);
        return FALSE; // inaccessible target descriptor: do not attempt a blind, partial repair
    }

    BOOL sourceProtectedDacl;
    if (!GetDaclProtection(srcSD, &sourceProtectedDacl))
    {
        *err = GetLastError();
        LocalFree(previousDescriptor);
        LocalFree(srcSD);
        return FALSE;
    }
    BOOL inheritedDacl = !sourceProtectedDacl;
    if (IsSecurityDescriptorPreserved(srcOwner, srcGroup, srcDACL, srcSD,
                                      previousOwner, previousGroup, previousDacl, previousDescriptor))
    {
        LocalFree(previousDescriptor);
        LocalFree(srcSD);
        return TRUE;
    }

    GainWriteOwnerAccess();
    BOOL changingOwnerOrGroup = !AreEqualSids(srcOwner, previousOwner) || !AreEqualSids(srcGroup, previousGroup);
    DWORD setError;
    BOOL attemptedWrite = FALSE;
    if (HaveWriteOwnerRight)
    {
        // SeRestorePrivilege authorizes a complete descriptor update.  It still
        // needs post-write verification because SetNamedSecurityInfo may report a
        // component-specific failure after accepting another component.
        attemptedWrite = TRUE;
        setError = SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                         DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
                                             (inheritedDacl ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
                                         srcOwner, srcGroup, srcDACL, NULL);
    }
    else if (!changingOwnerOrGroup)
    {
        // Without restore privilege it is safe to repair only the DACL when the
        // target already has the requested owner and group.  Never take ownership
        // temporarily: that was the source of partially copied descriptors.
        attemptedWrite = TRUE;
        setError = SetDaclWithInheritance(targetNameSecW, srcDACL, inheritedDacl);
    }
    else
    {
        setError = ERROR_PRIVILEGE_NOT_HELD;
    }

    PSID appliedOwner = NULL;
    PSID appliedGroup = NULL;
    PACL appliedDacl = NULL;
    PSECURITY_DESCRIPTOR appliedDescriptor = NULL;
    BOOL appliedRead = GetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                             DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION,
                                             &appliedOwner, &appliedGroup, &appliedDacl, NULL, &appliedDescriptor) == ERROR_SUCCESS;
    BOOL preserved = setError == ERROR_SUCCESS && appliedRead &&
                     IsSecurityDescriptorPreserved(srcOwner, srcGroup, srcDACL, srcSD,
                                                   appliedOwner, appliedGroup, appliedDacl, appliedDescriptor);
    if (appliedDescriptor != NULL)
        LocalFree(appliedDescriptor);
    if (preserved)
    {
        LocalFree(previousDescriptor);
        LocalFree(srcSD);
        *err = ERROR_SUCCESS;
        return TRUE;
    }

    if (!attemptedWrite)
    {
        // Owner/group differ and no restore privilege was available.  No target
        // mutation was attempted, so surface the best-effort warning directly.
        LocalFree(previousDescriptor);
        LocalFree(srcSD);
        *err = setError;
        return FALSE;
    }

    // A failed verification must not leave the output with a partial descriptor.
    // Restore privilege lets us restore the complete snapshot; otherwise only the
    // DACL was changed, so restoring that component cannot alter owner or group.
    DWORD rollbackError;
    if (HaveWriteOwnerRight)
    {
        BOOL previousProtectedDacl;
        rollbackError = GetDaclProtection(previousDescriptor, &previousProtectedDacl) ?
                            SetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION |
                                                      (previousProtectedDacl ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION),
                                                  previousOwner, previousGroup, previousDacl, NULL) :
                            GetLastError();
    }
    else
    {
        BOOL previousProtectedDacl;
        rollbackError = GetDaclProtection(previousDescriptor, &previousProtectedDacl) ?
                            SetDaclWithInheritance(targetNameSecW, previousDacl, !previousProtectedDacl) :
                            GetLastError();
    }
    if (rollbackError != ERROR_SUCCESS)
    {
        TRACE_E("DoCopySecurity(): unable to restore the target descriptor after a partial update: " << GetErrorText(rollbackError));
        *err = rollbackError;
    }
    else
    {
        *err = setError != ERROR_SUCCESS ? setError : ERROR_INVALID_SECURITY_DESCR;
    }
    LocalFree(previousDescriptor);
    LocalFree(srcSD);
    return FALSE;
}
