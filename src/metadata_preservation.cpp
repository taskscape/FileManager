// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#include "worker.h"

// Metadata-preservation contract extracted from async_copy.cpp as a mechanical
// move; all public declarations already live in worker.h.
EMetadataTargetFileSystem GetMetadataTargetFileSystem(const char* targetPath)
{
    if (targetPath == NULL || targetPath[0] == 0)
        return mtfsUnknown;

    CStrP targetPathW(ConvertAllocUtf8ToWide(targetPath, -1));
    WCHAR volumePath[MAX_PATH];
    WCHAR fileSystemName[64];
    if (targetPathW != NULL &&
        GetVolumePathNameW(targetPathW, volumePath, _countof(volumePath)) &&
        GetVolumeInformationW(volumePath, NULL, 0, NULL, NULL, NULL,
                              fileSystemName, _countof(fileSystemName)))
    {
        if (GetDriveTypeW(volumePath) == DRIVE_REMOTE)
            return mtfsSmb;
        if (_wcsicmp(fileSystemName, L"NTFS") == 0)
            return mtfsNtfs;
        if (_wcsicmp(fileSystemName, L"ReFS") == 0)
            return mtfsRefs;
        if (_wcsicmp(fileSystemName, L"FAT") == 0 ||
            _wcsicmp(fileSystemName, L"FAT32") == 0 ||
            _wcsicmp(fileSystemName, L"exFAT") == 0)
        {
            return mtfsFat;
        }
    }

    return StrNICmp(targetPath, "\\\\", 2) == 0 ? mtfsSmb : mtfsUnknown;
}

CMetadataPreservationContract GetMetadataPreservationContract(EMetadataOperation operation,
                                                               EMetadataTargetFileSystem targetFileSystem)
{
    CMetadataPreservationContract contract;
    contract.Content = mpRequired;
    contract.LastWriteTime = mpBestEffort;
    contract.CreationAndAccessTimes = mpUnsupported;
    contract.Attributes = mpBestEffort;
    contract.Security = mpBestEffort;
    contract.AlternateDataStreams = mpBestEffort;
    contract.CompressionAndEncryption = mpBestEffort;

    if (operation == moSameVolumeMove)
    {
        // A native rename keeps the same filesystem object; no copy-time
        // translation is involved.  SMB shares can still impose server-side
        // rename semantics, so represent their non-content metadata as best
        // effort rather than over-promising the local-filesystem guarantee.
        if (targetFileSystem == mtfsSmb)
        {
            contract.CreationAndAccessTimes = mpBestEffort;
            return contract;
        }
        contract.LastWriteTime = mpRequired;
        contract.CreationAndAccessTimes = mpRequired;
        contract.Attributes = mpRequired;
        contract.Security = mpRequired;
        contract.AlternateDataStreams = mpRequired;
        contract.CompressionAndEncryption = mpRequired;
        return contract;
    }

    if (targetFileSystem == mtfsFat)
    {
        contract.Security = mpUnsupported;
        contract.AlternateDataStreams = mpUnsupported;
        contract.CompressionAndEncryption = mpUnsupported;
    }
    else if (targetFileSystem == mtfsRefs)
    {
        // ReFS is ACL-capable but does not provide NTFS compression or EFS.
        contract.CompressionAndEncryption = mpUnsupported;
    }
    return contract;
}

static const char* GetMetadataTargetFileSystemName(EMetadataTargetFileSystem fileSystem)
{
    switch (fileSystem)
    {
    case mtfsNtfs:
        return "NTFS";
    case mtfsRefs:
        return "ReFS";
    case mtfsFat:
        return "FAT/exFAT";
    case mtfsSmb:
        return "SMB";
    default:
        return "the target filesystem";
    }
}

void RecordMetadataLoss(CProgressDlgData& dlgData, DWORD lossMask,
                        const char* sourceName, const char* targetName)
{
    if (lossMask == mmlNone)
        return;

    dlgData.MetadataLosses.LossMask |= lossMask;
    if (dlgData.MetadataLosses.TargetFileSystem == mtfsUnknown && targetName != NULL)
        dlgData.MetadataLosses.TargetFileSystem = GetMetadataTargetFileSystem(targetName);
    if (dlgData.MetadataLosses.FirstSourceName.IsEmpty() && sourceName != NULL)
    {
        // Metadata diagnostics must retain a complete first source identity.
        dlgData.MetadataLosses.FirstSourceName.Set(sourceName);
    }
    if (dlgData.MetadataLosses.FirstTargetName.IsEmpty() && targetName != NULL)
        dlgData.MetadataLosses.FirstTargetName.Set(targetName);
}

void RecordPlannedMetadataLosses(CProgressDlgData& dlgData, const COperations* script,
                                 const char* sourceName, const char* targetName)
{
    DWORD lossMask = script->PlannedMetadataLosses;
    CMetadataPreservationContract contract = GetMetadataPreservationContract(moCrossVolumeMove,
                                                                               script->TargetMetadataFileSystem);
    if (contract.CreationAndAccessTimes == mpUnsupported)
        lossMask |= mmlCreationAndAccessTimes;
    if (script->CopySecurity && contract.Security == mpUnsupported)
        lossMask |= mmlSecurity;
    if (script->CopyAttrs && contract.CompressionAndEncryption == mpUnsupported)
        lossMask |= mmlCompressionAndEncryption;

    if (dlgData.MetadataLosses.TargetFileSystem == mtfsUnknown)
        dlgData.MetadataLosses.TargetFileSystem = script->TargetMetadataFileSystem;
    RecordMetadataLoss(dlgData, lossMask, sourceName, targetName);
}

static void AppendMetadataLossLine(char* text, int textSize, const char* lossName)
{
    int length = (int)strlen(text);
    if (length >= textSize - 1)
        return;
    _snprintf_s(text + length, textSize - length, _TRUNCATE, "%s- %s",
                length == 0 ? "" : "\r\n", lossName);
}

BOOL ConfirmMetadataLossesBeforeSourceDeletion(HWND hProgressDlg, CProgressDlgData& dlgData,
                                               const char* sourceName, const char* targetName)
{
    if (dlgData.KeepSourceAfterMetadataLoss)
        return FALSE;

    DWORD unacknowledgedLosses = dlgData.MetadataLosses.LossMask &
                                 ~dlgData.MetadataLosses.AcknowledgedLossMask;
    if (unacknowledgedLosses == mmlNone)
        return TRUE;

    if (dlgData.MetadataLosses.FirstSourceName.IsEmpty() && sourceName != NULL)
        dlgData.MetadataLosses.FirstSourceName.Set(sourceName);
    if (dlgData.MetadataLosses.FirstTargetName.IsEmpty() && targetName != NULL)
        dlgData.MetadataLosses.FirstTargetName.Set(targetName);

    char lossDetails[768];
    lossDetails[0] = 0;
    if (unacknowledgedLosses & mmlLastWriteTime)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_LASTWRITETIME));
    if (unacknowledgedLosses & mmlCreationAndAccessTimes)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_CREATIONANDACCESSTIMES));
    if (unacknowledgedLosses & mmlAttributes)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_ATTRIBUTES));
    if (unacknowledgedLosses & mmlSecurity)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_SECURITY));
    if (unacknowledgedLosses & mmlAlternateDataStreams)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_ADS));
    if (unacknowledgedLosses & mmlCompressionAndEncryption)
        AppendMetadataLossLine(lossDetails, _countof(lossDetails), LoadStr(IDS_METADATALOSS_COMPRESSIONANDENCRYPTION));

    char firstSourceUtf8[MAX_PATH];
    dlgData.MetadataLosses.FirstSourceName.ToUtf8(firstSourceUtf8, sizeof(firstSourceUtf8));
    char text[3 * MAX_PATH + 1024];
    _snprintf_s(text, _countof(text), _TRUNCATE, LoadStr(IDS_METADATALOSS_BEFORESOURCEDELETE),
                GetMetadataTargetFileSystemName(dlgData.MetadataLosses.TargetFileSystem),
                !dlgData.MetadataLosses.FirstSourceName.IsEmpty() ? firstSourceUtf8 : sourceName,
                lossDetails);

    int result = IDCANCEL;
    char* data[2];
    data[0] = (char*)&result;
    data[1] = text;
    SendMessage(hProgressDlg, WM_USER_DIALOG, 13, (LPARAM)data);
    if (result == IDYES)
    {
        dlgData.MetadataLosses.AcknowledgedLossMask |= unacknowledgedLosses;
        return TRUE;
    }

    // A decline turns the remaining move into a copy.  This avoids later
    // directory-delete errors after the user deliberately retained a child.
    dlgData.KeepSourceAfterMetadataLoss = TRUE;
    return FALSE;
}
