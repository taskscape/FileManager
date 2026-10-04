// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Shared plan model. Kept free of the plug-in SDK so NativeSafetyTests can
// compile this core without INSIDE_SALAMANDER.

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace reorg
{

enum EPlanKind
{
    PlanReorganize = 0,
    PlanRevert = 1
};

enum EEditOp
{
    EditMove = 0,
    EditRename,
    EditCreateFolder,
    EditUnstage,
    EditExclude,
    EditRemoveEmptyFolder
};

enum EConflictDefault
{
    ConflictAsk = 0,
    ConflictKeepBoth,
    ConflictSkip
};

enum EOnError
{
    OnErrorStop = 0,
    OnErrorSkipDependents
};

enum EItemType
{
    ItemAny = 0,
    ItemFile,
    ItemDir
};

enum EResolutionChoice
{
    ResKeepBoth = 0,
    ResSkip,
    ResReplace,
    ResMerge,
    ResRename
};

enum EReversibility
{
    RevExact = 0,
    RevWithLoss,
    RevUntilFinalized,
    RevNotReversible
};

enum EIssueSeverity
{
    SevError = 0,
    SevWarning,
    SevInfo
};

enum EChangeKind
{
    ChangeUnchanged = 0,
    ChangeMovedHere,
    ChangeRenamed,
    ChangeMovedAndRenamed,
    ChangeMovedWithFolder,
    ChangeNewFolder,
    ChangeKeptBoth,
    ChangeReplaces,
    ChangeMerged,
    ChangeContains,
    ChangeApplied
};

enum EStepKind
{
    StepCreateDir = 1,
    StepMove = 2,
    StepRemoveEmptyDir = 3,
    StepCopyDirTime = 4
};

enum EStepRole
{
    RoleEdit = 0,
    RoleTempRename,
    RoleDisplace,
    RoleCleanup,
    RoleMerge,
    RoleStore
};

struct CFileId
{
    bool Valid;
    DWORD VolumeSerial;
    BYTE FileId[16];
    unsigned __int64 Size;
    unsigned __int64 LastWrite; // FILETIME
    DWORD Attributes;
    bool IsDir;

    CFileId() : Valid(false), VolumeSerial(0), Size(0), LastWrite(0), Attributes(0), IsDir(false)
    {
        memset(FileId, 0, sizeof(FileId));
    }
};

struct CRoot
{
    std::wstring Id;
    std::wstring Label;
    std::wstring Path;
    std::wstring VolumeSerial; // "0x........"
    std::wstring FileSystem;
};

struct CRuleMatch
{
    std::wstring ScopeRoot;
    std::wstring RelativePathGlob;
    std::wstring NameMask;
    bool HasExtensions;
    std::vector<std::wstring> Extensions;
    bool HasSizeMin;
    unsigned __int64 SizeMin;
    bool HasSizeMax;
    unsigned __int64 SizeMax;
    std::wstring ModifiedFrom;
    std::wstring ModifiedTo;
    std::wstring CreatedFrom;
    std::wstring CreatedTo;
    DWORD AttributesSet;
    DWORD AttributesClear;
    EItemType ItemType;

    CRuleMatch()
        : HasExtensions(false), HasSizeMin(false), SizeMin(0), HasSizeMax(false), SizeMax(0),
          AttributesSet(0), AttributesClear(0), ItemType(ItemAny)
    {
    }
};

struct CRule
{
    std::wstring Id;
    std::wstring Name;
    bool Enabled;
    int Order;
    CRuleMatch Match;
    std::wstring Destination;
    std::wstring TemplateError; // unknown token; rule cannot be enabled

    CRule() : Enabled(true), Order(0) {}
};

struct CEdit
{
    int Seq;
    EEditOp Op;
    std::wstring Source;
    std::wstring DestinationDir;
    std::wstring NewName;
    std::wstring Path;
    std::wstring Origin;
    std::wstring CreatedUtc;
    std::wstring Note;

    CEdit() : Seq(0), Op(EditMove) {}
};

struct CResolution
{
    std::wstring IssueKey;
    EResolutionChoice Choice;
    std::wstring ResultName;
    std::wstring DecidedUtc;
};

struct CAcknowledgement
{
    std::wstring IssueKey;
    std::wstring AckUtc;
};

struct CReviewMark
{
    bool Present;
    std::wstring CompiledSha256;
    std::wstring ReviewedUtc;
    int StepCount;

    CReviewMark() : Present(false), StepCount(0) {}
};

struct CBaselineItem
{
    std::wstring Path;
    bool Dir;
    unsigned __int64 Size;
    std::wstring LastWriteUtc;
    DWORD Attributes;
    DWORD ReparseTag;
    std::wstring VolumeSerial;
    std::wstring FileId;

    CBaselineItem() : Dir(false), Size(0), Attributes(0), ReparseTag(0) {}
};

struct CApplyRecord
{
    std::wstring ApplyId;
    std::wstring StartedUtc;
    std::wstring FinishedUtc;
    std::wstring Status;
    std::wstring Journal;
};

struct CRecoveryStoreOption
{
    bool Explicit;
    std::map<std::wstring, std::wstring> Paths; // volume serial -> folder

    CRecoveryStoreOption() : Explicit(false) {}
};

struct CTextScanOption
{
    bool Enabled;
    std::vector<std::wstring> Extensions;
    DWORD MaxFileBytes;

    CTextScanOption() : Enabled(false), MaxFileBytes(4194304) {}
};

struct CPlanOptions
{
    EConflictDefault ConflictDefault;
    std::wstring KeepBothPattern;
    bool CleanupEmptiedFolders;
    CRecoveryStoreOption RecoveryStore;
    EOnError OnError;
    bool VerifyAfterApply;
    bool DeepLockCheck;
    bool ScanShortcuts;
    bool ScanUrls;
    bool ScanApplicationPaths;
    CTextScanOption Text;
    std::vector<std::wstring> ExtraRoots;
    bool RecordAuthor;

    CPlanOptions()
        : ConflictDefault(ConflictAsk), KeepBothPattern(L"{name} ({n}){ext}"),
          CleanupEmptiedFolders(true), OnError(OnErrorStop), VerifyAfterApply(true),
          DeepLockCheck(false), ScanShortcuts(true), ScanUrls(true), ScanApplicationPaths(true),
          RecordAuthor(true)
    {
        const wchar_t* ext[] = {L"htm", L"html", L"css", L"md", L"xml", L"json", L"csproj", L"vcxproj", L"props", L"sln"};
        for (int i = 0; i < 10; ++i)
            Text.Extensions.push_back(ext[i]);
    }
};

// Opaque JSON kept so unknown fields survive a round trip (plan format P5).
struct CJsonValue;

struct CPlanDocument
{
    int FormatVersion;
    std::wstring PlanId;
    std::wstring Name;
    EPlanKind Kind;
    std::wstring RevertOfPlanId;
    std::wstring RevertOfApplyId;
    std::wstring CreatedUtc;
    std::wstring ModifiedUtc;
    std::wstring CreatedBy;
    std::wstring CreatedOn;
    std::wstring ApplicationVersion;
    std::wstring PluginVersion;
    std::vector<CRoot> ScopeRoots;
    std::vector<CRoot> DestinationRoots;
    CPlanOptions Options;
    std::vector<CRule> Rules;
    std::vector<CEdit> Edits;
    std::vector<CResolution> Resolutions;
    std::vector<CAcknowledgement> Acknowledgements;
    CReviewMark Review;
    std::wstring BaselineCapturedUtc;
    std::vector<CBaselineItem> Baseline;
    std::vector<CApplyRecord> Applies;
    std::string ExtensionsJson; // verbatim extensions object, "{}" when empty
    std::string UnknownJson;    // other unknown top-level keys, as a JSON object body without braces
    bool Dirty;
    std::wstring FilePath;

    CPlanDocument() : FormatVersion(1), Kind(PlanReorganize), Dirty(false) {}
};

struct CSnapshotItem
{
    std::wstring Path;
    std::wstring ParentPath;
    std::wstring Name;
    bool IsDir;
    unsigned __int64 Size;
    unsigned __int64 LastWrite;
    unsigned __int64 Created;
    DWORD Attributes;
    DWORD ReparseTag;
    DWORD VolumeSerial;
    bool HasFileId;
    BYTE FileId[16];
    bool CaseSensitive;
    bool CaseSensitiveKnown;
    std::wstring FileSystem;
    bool PersistentFileIds;
    bool ReadOnlyVolume;
    DWORD MaxComponent;
    unsigned __int64 FreeBytes;
    bool CloudPlaceholder;
    bool CloudSynced;
    int HardLinkCount;

    CSnapshotItem()
        : IsDir(false), Size(0), LastWrite(0), Created(0), Attributes(0), ReparseTag(0),
          VolumeSerial(0), HasFileId(false), CaseSensitive(false), CaseSensitiveKnown(false),
          PersistentFileIds(true), ReadOnlyVolume(false), MaxComponent(255), FreeBytes(0),
          CloudPlaceholder(false), CloudSynced(false), HardLinkCount(1)
    {
        memset(FileId, 0, sizeof(FileId));
    }
};

struct CIssue
{
    std::wstring Code;
    EIssueSeverity Severity;
    std::wstring Key;
    std::vector<std::wstring> Nodes;
    std::vector<std::wstring> Params;
    std::vector<EResolutionChoice> Resolutions;
    std::wstring Text;
};

struct CCompiledStep
{
    EStepKind Kind;
    DWORD Flags; // SALOPSTEPF bits, duplicated here so the core stays SDK-free
    std::wstring Source;
    std::wstring Target;
    CFileId ExpectedIdentity;
    bool VerifyIdentity;
    DWORD ExpectedMetadataLosses;
    EReversibility Class;
    EStepRole Role;
    std::wstring Node;
    std::wstring Reason;
    bool Dir;
    bool CrossVolume;
    std::vector<int> Deps;
    std::wstring StorePlaceholder; // non-empty when the path uses {store:serial}

    // Apply marshals every field, so steps built field-by-field (createDir, copyDirTime,
    // store folders) must not carry indeterminate VerifyIdentity or loss bits to the host.
    CCompiledStep()
        : Kind(StepMove), Flags(0), VerifyIdentity(false), ExpectedMetadataLosses(0), Class(RevExact),
          Role(RoleEdit), Dir(false), CrossVolume(false)
    {
    }
};

struct CCompiledPlan
{
    std::vector<CCompiledStep> Steps;
    std::wstring Hash;
    std::map<std::wstring, std::wstring> StoreRoots; // volume serial text -> concrete or placeholder root
    bool Ok;
    std::wstring Error;

    CCompiledPlan() : Ok(false) {}
};

struct CCancellation
{
    volatile LONG Cancelled;
    CCancellation() : Cancelled(0) {}
    bool IsCancelled() const { return Cancelled != 0; }
    void Cancel() { InterlockedExchange(&Cancelled, 1); }
};

const DWORD kStepFSourceIsDir = 0x0001;
const DWORD kStepFTargetMustNotExist = 0x0002;
const DWORD kStepFAllowCrossVolume = 0x0004;
const DWORD kStepFVerifySourceIdentity = 0x0008;
const DWORD kStepFMetadataLossAccepted = 0x0010;
const DWORD kStepFCreateDirAcceptExisting = 0x0020;

const DWORD kLossLastWrite = 0x0001;
const DWORD kLossAttributes = 0x0002;
const DWORD kLossSecurity = 0x0004;
const DWORD kLossAds = 0x0008;
const DWORD kLossCompressionEfs = 0x0010;
const DWORD kLossCreationLastAccess = 0x0020;

} // namespace reorg
