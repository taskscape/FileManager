// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "../../src/plugins/reorganize/core/planio.h"
#include "../../src/plugins/reorganize/core/mapping.h"
#include "../../src/plugins/reorganize/core/compiler.h"
#include "../../src/plugins/reorganize/core/journal.h"
#include "../../src/plugins/reorganize/core/reconcile.h"
#include "../../src/plugins/reorganize/core/revert.h"
#include "../../src/plugins/reorganize/core/report.h"
#include "../../src/plugins/reorganize/core/rules.h"

namespace
{
using namespace reorg;

int FailReorg(const char* message)
{
    fprintf(stderr, "NativeSafetyTests: %s\n", message);
    return 1;
}

CSnapshotItem FileItem(const wchar_t* path, DWORD volume, unsigned __int64 size)
{
    CSnapshotItem item;
    std::wstring normalized, error;
    NormalizePath(path, normalized, error);
    item.Path = normalized;
    item.ParentPath = ParentPath(normalized);
    item.Name = LeafName(normalized);
    item.IsDir = false;
    item.Size = size;
    item.VolumeSerial = volume;
    item.HasFileId = true;
    item.FileId[15] = (BYTE)(volume & 0xFF);
    item.Attributes = FILE_ATTRIBUTE_NORMAL;
    item.FileSystem = L"NTFS";
    item.PersistentFileIds = true;
    return item;
}

CSnapshotItem DirItem(const wchar_t* path, DWORD volume)
{
    CSnapshotItem item = FileItem(path, volume, 0);
    item.IsDir = true;
    item.Attributes = FILE_ATTRIBUTE_DIRECTORY;
    item.HasFileId = true;
    return item;
}

int TestReorganizeCore()
{
    CJsonParseResult bad = JsonParse("{");
    if (bad.Ok)
        return FailReorg("JSON accepted a truncated object");
    CJsonParseResult dup = JsonParse("{\"a\":1,\"a\":2}");
    if (dup.Ok)
        return FailReorg("JSON accepted a duplicate key");
    CJsonParseResult surrogate = JsonParse("\"\\uD800\"");
    if (surrogate.Ok)
        return FailReorg("JSON accepted a lone surrogate");
    CJsonParseResult deep = JsonParse("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]");
    if (deep.Ok)
        return FailReorg("JSON accepted nesting past the depth limit");
    CJsonParseResult number = JsonParse("{\"n\":9223372036854775807}");
    if (!number.Ok || number.Value.Find("n") == NULL || number.Value.Find("n")->Kind != JsonInt)
        return FailReorg("JSON rejected the maximum int64");
    std::string round = JsonWrite(number.Value);
    CJsonParseResult again = JsonParse(round);
    if (!again.Ok || again.Value.Find("n")->Int != INT64_MAX)
        return FailReorg("JSON did not round-trip an int64");

    CPlanDocument plan;
    plan.PlanId = L"6f1c2a8e-3b7d-4c1e-9a52-0d4b8f2e7c11";
    plan.Name = L"Acme migration";
    plan.CreatedUtc = L"2026-10-03T09:12:44Z";
    plan.ModifiedUtc = plan.CreatedUtc;
    plan.FormatVersion = 1;
    CRoot scope;
    scope.Id = L"s1";
    scope.Path = L"D:\\Inherited";
    scope.VolumeSerial = L"0x00000011";
    scope.FileSystem = L"NTFS";
    plan.ScopeRoots.push_back(scope);
    CRoot dest;
    dest.Id = L"d1";
    dest.Label = L"Standard";
    dest.Path = L"D:\\Standard";
    dest.VolumeSerial = L"0x00000011";
    dest.FileSystem = L"NTFS";
    plan.DestinationRoots.push_back(dest);
    plan.UnknownJson = "{\"futureFlag\":true}\n";
    std::string saved = SavePlanJson(plan);
    CPlanDocument loaded;
    CPlanIoResult io = LoadPlanJson(saved, loaded);
    if (!io.Ok || loaded.Name != plan.Name || loaded.UnknownJson.find("futureFlag") == std::string::npos)
        return FailReorg("plan round-trip dropped an unknown field");
    CPlanIoResult newer = LoadPlanJson("{\"format\":\"open-salamander.reorganization-plan\",\"formatVersion\":2,\"planId\":\"x\",\"name\":\"n\",\"kind\":\"reorganize\",\"createdUtc\":\"t\",\"modifiedUtc\":\"t\"}", loaded);
    if (newer.Ok)
        return FailReorg("a newer plan format was accepted");

    CMemoryFileSystem memory;
    memory.AddDir(L"D:\\Inherited", 0x11, L"NTFS");
    memory.AddFile(L"D:\\Inherited\\a.txt", 4, 10, FILE_ATTRIBUTE_NORMAL, 0x11, FileItem(L"D:\\Inherited\\a.txt", 0x11, 4).FileId);
    std::string original = SavePlanJson(plan);
    memory.Files[L"D:\\plan.reorgplan"] = original;
    memory.Nodes[L"D:\\plan.reorgplan"].Content = original;
    memory.FailReplace = true;
    CPlanIoResult failed = SavePlanFile(memory, L"D:\\plan.reorgplan", plan);
    if (failed.Ok || memory.Files[L"D:\\plan.reorgplan"] != original)
        return FailReorg("an injected replace failure changed the saved plan");

    CSnapshot snapshot;
    snapshot.Add(DirItem(L"D:\\Inherited", 0x11));
    snapshot.Add(DirItem(L"D:\\Standard", 0x11));
    snapshot.Add(FileItem(L"D:\\Inherited\\a.txt", 0x11, 4));
    snapshot.Add(DirItem(L"D:\\Inherited\\Docs", 0x11));
    snapshot.Add(FileItem(L"D:\\Inherited\\Docs\\b.txt", 0x11, 8));
    CPlanHistory history;
    CStageResult moved = StageMove(loaded, history, snapshot, L"D:\\Inherited\\a.txt", L"D:\\Standard", L"", L"user");
    if (!moved.Ok)
        return FailReorg("staging a move was rejected");
    COverlay overlay = BuildOverlay(snapshot, loaded);
    const COverlayNode* node = overlay.FindKey(L"D:\\Inherited\\a.txt");
    if (node == NULL || !PathsEqual(node->ProposedPath, L"D:\\Standard\\a.txt", false))
        return FailReorg("the proposed tree did not show the staged move");
    if (!history.Undo(loaded))
        return FailReorg("undo did not restore the plan");
    overlay = BuildOverlay(snapshot, loaded);
    node = overlay.FindKey(L"D:\\Inherited\\a.txt");
    if (node == NULL || node->Change != ChangeUnchanged)
        return FailReorg("undo left a staged move in the proposed tree");
    if (!history.Redo(loaded))
        return FailReorg("redo did not restore the staged move");

    CStageResult cycle = StageMove(loaded, history, snapshot, L"D:\\Inherited", L"D:\\Inherited\\Docs", L"", L"user");
    if (cycle.Ok)
        return FailReorg("a move into a descendant was stored");

    CRule rule;
    rule.Id = L"r1";
    rule.Name = L"Text";
    rule.Order = 1;
    rule.Match.NameMask = L"*.txt";
    rule.Match.ItemType = ItemFile;
    rule.Destination = L"{dest:Standard}\\Inbox\\{name}{ext}";
    std::wstring destDir, newName, error;
    if (!ExpandTemplate(rule.Destination, *snapshot.Find(L"D:\\Inherited\\a.txt"), loaded, snapshot, destDir, newName, error))
        return FailReorg("template expansion failed");
    if (!PathsEqual(destDir, L"D:\\Standard\\Inbox", false) || newName != L"a.txt")
        return FailReorg("template tokens did not expand");
    if (AgreeMaskWide(L"a.txt", L"*.txt", true) == false || AgreeMaskWide(L"a.txt", L"*.doc", true))
        return FailReorg("mask matching diverged from AgreeMask");

    CMappingResult mapping = ParseMapping("source,destination,note\r\n\"D:\\Inherited\\a.txt\",\"D:\\Standard\\renamed.txt\",\"keep\"\r\n", L"D:\\Inherited", L"D:\\Standard", snapshot);
    if (mapping.Rows.size() != 1 || !mapping.Rows[0].Accepted || mapping.Rows[0].NewName != L"renamed.txt")
        return FailReorg("CSV mapping did not accept a quoted row");

    CIssueSink issues;
    overlay = BuildOverlay(snapshot, loaded);
    CAnalysisContext context;
    context.Plan = &loaded;
    context.Snapshot = &snapshot;
    context.Overlay = &overlay;
    ValidatePlan(context, issues);
    CCompiledPlan compiled = CompilePlan(overlay, loaded, issues);
    if (!compiled.Ok || compiled.Steps.empty() || compiled.Hash.size() != 64)
        return FailReorg("the compiler did not produce a hashed plan");
    CCompiledPlan againPlan = CompilePlan(overlay, loaded, issues);
    if (againPlan.Hash != compiled.Hash)
        return FailReorg("the compiled hash changed between identical runs");

    std::vector<std::string> fields;
    fields.push_back("1");
    fields.push_back("plan=" + WideToUtf8(loaded.PlanId));
    std::string record = FormatJournalRecord("REORGJOURNAL", fields);
    CJournal journal;
    if (!ParseJournal(record, journal) || journal.Records.size() != 1)
        return FailReorg("journal round-trip failed");
    std::string torn = record + "DONE|1|crc=00000000";
    if (!ParseJournal(torn, journal) || journal.Records.size() != 1 || journal.ManualOnly)
        return FailReorg("a torn journal suffix was not discarded");
    std::string corrupt = record;
    corrupt.insert(record.size() - 2, "|extra");
    std::string interior = record + FormatJournalRecord("DONE", std::vector<std::string>(1, "0"));
    interior.replace(interior.find("crc="), 12, "crc=DEADBEEF");
    // The replaced checksum belongs to the first record only when it is not last. Append a valid tail.
    interior += FormatJournalRecord("END-APPLY", std::vector<std::string>(1, "status=failed"));
    ParseJournal(interior, journal);

    CReportRow row;
    row.Kind = L"Move";
    row.Original = L"=cmd|'/c calc'!A1";
    row.Proposed = L"D:\\Standard\\a.txt";
    std::vector<CReportRow> rows;
    rows.push_back(row);
    std::string csv = WriteReportCsv(rows);
    if (csv.find("\"'=cmd") == std::string::npos)
        return FailReorg("CSV report did not neutralize a formula");
    std::string html = WriteReportHtml(L"Acme <migration>", loaded.PlanId, compiled.Hash, rows);
    if (html.find("<migration>") != std::string::npos)
        return FailReorg("HTML report did not escape a value");

    std::wstring storeError;
    CSnapshotItem displaced = *snapshot.Find(L"D:\\Inherited\\a.txt");
    std::wstring store = ChooseRecoveryStore(loaded, displaced, L"D:\\Standard\\a.txt", storeError);
    if (store.empty())
        return FailReorg("recovery store selection failed on the same volume");

    return 0;
}
}
