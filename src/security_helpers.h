// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#pragma once

// Security/privilege helpers extracted from async_copy.cpp. CSrcSecurity lives
// here so DoMoveFile (async_copy.cpp) and DoCopySecurity share one declaration
// instead of drifted externs. Include after precomp.h (project convention).
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

void GainWriteOwnerAccess();
BOOL DoCopySecurity(const char* sourceName, const char* targetName, DWORD* err, CSrcSecurity* srcSecurity);
