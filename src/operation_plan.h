// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

class COperations;

// The operation plan is deliberately separate from the live worker state.  It
// contains only the deterministic instructions produced by the planner, so it
// can be captured, compared, or persisted before a worker starts prompting or
// touching the filesystem.
enum COperationCode
{
    ocCopyFile,
    ocMoveFile,
    ocDeleteFile,
    ocCreateDir,
    ocMoveDir,
    ocDeleteDir,
    ocDeleteDirLink,
    ocChangeAttrs,             // requested attributes are stored in TargetName as a DWORD
    ocCountSize,
    ocConvert,
    ocLabelForSkipOfCreateDir, // SourceName/TargetName hold DWORD payloads; Attr holds a script index
    ocCopyDirTime,             // SourceName and Attr hold the last-write FILETIME payload
};

enum EOperationPlanOperandKind
{
    opokNone,
    opokPath,
    opokDWORD
};

struct COperationPlanItem
{
    COperationCode Opcode;
    CQuadWord Size;
    CQuadWord FileSize;
    DWORD Attr;
    DWORD OpFlags;
    EOperationPlanOperandKind SourceKind;
    EOperationPlanOperandKind TargetKind;
    char* SourcePath;
    char* TargetPath;
    DWORD SourceValue;
    DWORD TargetValue;

    COperationPlanItem();
    COperationPlanItem(const COperationPlanItem& item);
    ~COperationPlanItem();
    COperationPlanItem& operator=(const COperationPlanItem& item);
    BOOL IsGood() const;
};

class COperationPlan
{
private:
    TDirectArray<COperationPlanItem> Items;

public:
    COperationPlan();

    // Captures the current script without retaining its pointer-owned names or
    // referencing worker, dialog, or filesystem state.
    BOOL Capture(COperations& operations);

    int GetCount() const { return Items.Count; }
    const COperationPlanItem& At(int index) { return Items.At(index); }

    static const char* GetOpcodeName(COperationCode opcode);
};
