// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <strsafe.h>

#include "operation_plan.h"
#include "worker.h"

namespace
{
char* DuplicatePlanPath(const char* path)
{
    if (path == NULL)
        return NULL;
    return DupStr(path);
}

BOOL SetPlanOperand(EOperationPlanOperandKind& kind, char*& path, DWORD& value,
                    const char* operationValue, BOOL isPath)
{
    kind = isPath ? opokPath : opokDWORD;
    if (isPath)
    {
        path = DuplicatePlanPath(operationValue);
        return path != NULL;
    }
    else
        value = (DWORD)(DWORD_PTR)operationValue;
    return TRUE;
}

BOOL IsPathSource(COperationCode opcode)
{
    return opcode != ocLabelForSkipOfCreateDir && opcode != ocCopyDirTime;
}

BOOL IsPathTarget(COperationCode opcode)
{
    return opcode != ocChangeAttrs && opcode != ocLabelForSkipOfCreateDir && opcode != ocCopyDirTime;
}
}

COperationPlanItem::COperationPlanItem() : Opcode(ocCopyFile), Size(0, 0), FileSize(0, 0),
                                             Attr(0), OpFlags(0), SourceKind(opokNone), TargetKind(opokNone),
                                             SourcePath(NULL), TargetPath(NULL), SourceValue(0), TargetValue(0)
{
}

COperationPlanItem::COperationPlanItem(const COperationPlanItem& item) : SourcePath(NULL), TargetPath(NULL)
{
    *this = item;
}

COperationPlanItem::~COperationPlanItem()
{
    if (SourcePath != NULL) free(SourcePath);
    if (TargetPath != NULL) free(TargetPath);
}

COperationPlanItem& COperationPlanItem::operator=(const COperationPlanItem& item)
{
    if (this == &item)
        return *this;
    if (SourcePath != NULL) free(SourcePath);
    if (TargetPath != NULL) free(TargetPath);
    Opcode = item.Opcode;
    Size = item.Size;
    FileSize = item.FileSize;
    Attr = item.Attr;
    OpFlags = item.OpFlags;
    SourceKind = item.SourceKind;
    TargetKind = item.TargetKind;
    SourcePath = item.SourceKind == opokPath ? DuplicatePlanPath(item.SourcePath) : NULL;
    TargetPath = item.TargetKind == opokPath ? DuplicatePlanPath(item.TargetPath) : NULL;
    SourceValue = item.SourceValue;
    TargetValue = item.TargetValue;
    return *this;
}

BOOL COperationPlanItem::IsGood() const
{
    return (SourceKind != opokPath || SourcePath != NULL) &&
           (TargetKind != opokPath || TargetPath != NULL);
}

COperationPlan::COperationPlan() : Items(16, 16)
{
    OperationId[0] = 0;
}

BOOL COperationPlan::Capture(COperations& operations)
{
    if (Items.Count != 0)
        return FALSE; // snapshots are immutable once exposed to the execution boundary

    // A truncated correlation ID could join this immutable plan to a different operation, so reject it.
    if (FAILED(StringCchCopyA(OperationId, _countof(OperationId), operations.GetCorrelationId())))
        return FALSE;

    for (int index = 0; index < operations.Count; ++index)
    {
        const COperation& operation = operations.At(index);
        COperationPlanItem item;
        item.Opcode = operation.Opcode;
        item.Size = operation.Size;
        if (operation.Opcode == ocCopyFile || operation.Opcode == ocMoveFile)
            item.FileSize = operation.FileSize;
        item.Attr = operation.Attr;
        item.OpFlags = operation.OpFlags;

        if (operation.SourceName != NULL &&
            !SetPlanOperand(item.SourceKind, item.SourcePath, item.SourceValue,
                            operation.SourceName, IsPathSource(operation.Opcode)))
            return FALSE;
        if (operation.TargetName != NULL &&
            !SetPlanOperand(item.TargetKind, item.TargetPath, item.TargetValue,
                            operation.TargetName, IsPathTarget(operation.Opcode)))
            return FALSE;

        int itemIndex = Items.Add(item);
        if (itemIndex == ULONG_MAX || !Items.IsGood() || !Items.At(itemIndex).IsGood())
        {
            Items.ResetState();
            return FALSE;
        }
    }
    return TRUE;
}

const char* COperationPlan::GetOpcodeName(COperationCode opcode)
{
    switch (opcode)
    {
    case ocCopyFile: return "copy-file";
    case ocMoveFile: return "move-file";
    case ocDeleteFile: return "delete-file";
    case ocCreateDir: return "create-dir";
    case ocMoveDir: return "move-dir";
    case ocDeleteDir: return "delete-dir";
    case ocDeleteDirLink: return "delete-dir-link";
    case ocChangeAttrs: return "change-attrs";
    case ocCountSize: return "count-size";
    case ocConvert: return "convert";
    case ocLabelForSkipOfCreateDir: return "skip-dir-label";
    case ocCopyDirTime: return "copy-dir-time";
    default: return "unknown";
    }
}
