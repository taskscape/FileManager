// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "planio.h"

namespace reorg
{
namespace
{

const char* kKnown[] = {
    "format", "formatVersion", "planId", "name", "kind", "revertOf", "createdUtc", "modifiedUtc",
    "createdBy", "createdOn", "application", "scopeRoots", "destinationRoots", "options", "rules",
    "edits", "resolutions", "acknowledgements", "review", "baseline", "applies", "extensions"};

bool KnownKey(const std::string& key)
{
    for (size_t i = 0; i < sizeof(kKnown) / sizeof(kKnown[0]); ++i)
    {
        if (key == kKnown[i])
            return true;
    }
    return false;
}

std::wstring NeedString(const CJsonValue& obj, const char* key, bool required, CPlanIoResult& result)
{
    const CJsonValue* value = obj.Find(key);
    if (value == NULL || value->Kind == JsonNull)
    {
        if (required)
        {
            result.Ok = false;
            result.Error = L"missing " + Utf8ToWide(key);
        }
        return std::wstring();
    }
    if (value->Kind != JsonString)
    {
        result.Ok = false;
        result.Error = L"expected string " + Utf8ToWide(key);
        return std::wstring();
    }
    return Utf8ToWide(value->String);
}

CJsonValue StringOrNull(const std::wstring& text)
{
    if (text.empty())
        return CJsonValue::MakeNull();
    return CJsonValue::MakeStringW(text);
}

const char* KindName(EPlanKind kind) { return kind == PlanRevert ? "revert" : "reorganize"; }
const char* ConflictName(EConflictDefault value)
{
    if (value == ConflictKeepBoth)
        return "keepBoth";
    if (value == ConflictSkip)
        return "skip";
    return "ask";
}
const char* OpName(EEditOp op)
{
    switch (op)
    {
    case EditRename: return "rename";
    case EditCreateFolder: return "createFolder";
    case EditUnstage: return "unstage";
    case EditExclude: return "exclude";
    case EditRemoveEmptyFolder: return "removeEmptyFolder";
    default: return "move";
    }
}
const char* ChoiceName(EResolutionChoice choice)
{
    switch (choice)
    {
    case ResSkip: return "skip";
    case ResReplace: return "replace";
    case ResMerge: return "merge";
    case ResRename: return "rename";
    default: return "keepBoth";
    }
}

EEditOp ParseOp(const std::wstring& text)
{
    if (text == L"rename") return EditRename;
    if (text == L"createFolder") return EditCreateFolder;
    if (text == L"unstage") return EditUnstage;
    if (text == L"exclude") return EditExclude;
    if (text == L"removeEmptyFolder") return EditRemoveEmptyFolder;
    return EditMove;
}

EResolutionChoice ParseChoice(const std::wstring& text)
{
    if (text == L"skip") return ResSkip;
    if (text == L"replace") return ResReplace;
    if (text == L"merge") return ResMerge;
    if (text == L"rename") return ResRename;
    return ResKeepBoth;
}

CJsonValue RootToJson(const CRoot& root, bool destination)
{
    CJsonValue obj;
    obj.Kind = JsonObject;
    obj.Object["id"] = CJsonValue::MakeStringW(root.Id);
    if (destination)
        obj.Object["label"] = CJsonValue::MakeStringW(root.Label);
    obj.Object["path"] = CJsonValue::MakeStringW(root.Path);
    obj.Object["volumeSerial"] = StringOrNull(root.VolumeSerial);
    obj.Object["fileSystem"] = StringOrNull(root.FileSystem);
    return obj;
}

CRoot RootFromJson(const CJsonValue& obj, CPlanIoResult& result)
{
    CRoot root;
    root.Id = NeedString(obj, "id", true, result);
    root.Label = NeedString(obj, "label", false, result);
    root.Path = NeedString(obj, "path", true, result);
    root.VolumeSerial = NeedString(obj, "volumeSerial", false, result);
    root.FileSystem = NeedString(obj, "fileSystem", false, result);
    return root;
}

} // namespace

CPlanIoResult LoadPlanJson(const std::string& utf8, CPlanDocument& plan)
{
    CPlanIoResult result;
    CJsonParseResult parsed = JsonParse(utf8);
    if (!parsed.Ok)
    {
        result.Error = Utf8ToWide(parsed.Error);
        result.Line = parsed.Line;
        result.Column = parsed.Column;
        return result;
    }
    if (parsed.Value.Kind != JsonObject)
    {
        result.Error = L"plan file must be a JSON object";
        return result;
    }
    const CJsonValue* format = parsed.Value.Find("format");
    if (format == NULL || format->Kind != JsonString || format->String != "open-salamander.reorganization-plan")
    {
        result.Error = L"not a reorganization plan";
        return result;
    }
    const CJsonValue* version = parsed.Value.Find("formatVersion");
    if (version == NULL || version->Kind != JsonInt)
    {
        result.Error = L"missing formatVersion";
        return result;
    }
    if (version->Int > 1)
    {
        result.Error = L"This plan uses a newer file format and cannot be opened by this version.";
        return result;
    }
    plan = CPlanDocument();
    result.Ok = true;
    plan.FormatVersion = (int)version->Int;
    plan.PlanId = NeedString(parsed.Value, "planId", true, result);
    plan.Name = NeedString(parsed.Value, "name", true, result);
    std::wstring kind = NeedString(parsed.Value, "kind", true, result);
    plan.Kind = kind == L"revert" ? PlanRevert : PlanReorganize;
    plan.CreatedUtc = NeedString(parsed.Value, "createdUtc", true, result);
    plan.ModifiedUtc = NeedString(parsed.Value, "modifiedUtc", true, result);
    if (!result.Ok)
        return result;
    plan.CreatedBy = NeedString(parsed.Value, "createdBy", false, result);
    plan.CreatedOn = NeedString(parsed.Value, "createdOn", false, result);
    const CJsonValue* revert = parsed.Value.Find("revertOf");
    if (revert && revert->Kind == JsonObject)
    {
        plan.RevertOfPlanId = NeedString(*revert, "planId", false, result);
        plan.RevertOfApplyId = NeedString(*revert, "applyId", false, result);
    }
    const CJsonValue* app = parsed.Value.Find("application");
    if (app && app->Kind == JsonObject)
    {
        plan.ApplicationVersion = NeedString(*app, "version", false, result);
        plan.PluginVersion = NeedString(*app, "pluginVersion", false, result);
    }
    const CJsonValue* scopes = parsed.Value.Find("scopeRoots");
    if (scopes && scopes->Kind == JsonArray)
    {
        for (size_t i = 0; i < scopes->Array.size(); ++i)
            plan.ScopeRoots.push_back(RootFromJson(scopes->Array[i], result));
    }
    const CJsonValue* dests = parsed.Value.Find("destinationRoots");
    if (dests && dests->Kind == JsonArray)
    {
        for (size_t i = 0; i < dests->Array.size(); ++i)
            plan.DestinationRoots.push_back(RootFromJson(dests->Array[i], result));
    }
    const CJsonValue* options = parsed.Value.Find("options");
    if (options && options->Kind == JsonObject)
    {
        std::wstring conflict = NeedString(*options, "conflictDefault", false, result);
        if (conflict == L"keepBoth")
            plan.Options.ConflictDefault = ConflictKeepBoth;
        else if (conflict == L"skip")
            plan.Options.ConflictDefault = ConflictSkip;
        std::wstring pattern = NeedString(*options, "keepBothPattern", false, result);
        if (!pattern.empty())
            plan.Options.KeepBothPattern = pattern;
        const CJsonValue* cleanup = options->Find("cleanupEmptiedFolders");
        if (cleanup && cleanup->Kind == JsonBool)
            plan.Options.CleanupEmptiedFolders = cleanup->Bool;
        const CJsonValue* verify = options->Find("verifyAfterApply");
        if (verify && verify->Kind == JsonBool)
            plan.Options.VerifyAfterApply = verify->Bool;
        const CJsonValue* deep = options->Find("deepLockCheck");
        if (deep && deep->Kind == JsonBool)
            plan.Options.DeepLockCheck = deep->Bool;
        std::wstring onError = NeedString(*options, "onError", false, result);
        if (onError == L"skipDependents")
            plan.Options.OnError = OnErrorSkipDependents;
        const CJsonValue* store = options->Find("recoveryStore");
        if (store && store->Kind == JsonObject)
        {
            std::wstring mode = NeedString(*store, "mode", false, result);
            plan.Options.RecoveryStore.Explicit = mode == L"explicit";
            const CJsonValue* paths = store->Find("paths");
            if (paths && paths->Kind == JsonObject)
            {
                for (std::map<std::string, CJsonValue>::const_iterator it = paths->Object.begin(); it != paths->Object.end(); ++it)
                {
                    if (it->second.Kind == JsonString)
                        plan.Options.RecoveryStore.Paths[Utf8ToWide(it->first)] = Utf8ToWide(it->second.String);
                }
            }
        }
        const CJsonValue* scanners = options->Find("scanners");
        if (scanners && scanners->Kind == JsonObject)
        {
            const CJsonValue* shortcuts = scanners->Find("shortcuts");
            if (shortcuts && shortcuts->Kind == JsonBool)
                plan.Options.ScanShortcuts = shortcuts->Bool;
            const CJsonValue* urls = scanners->Find("urls");
            if (urls && urls->Kind == JsonBool)
                plan.Options.ScanUrls = urls->Bool;
            const CJsonValue* apps = scanners->Find("applicationPaths");
            if (apps && apps->Kind == JsonBool)
                plan.Options.ScanApplicationPaths = apps->Bool;
            const CJsonValue* text = scanners->Find("text");
            if (text && text->Kind == JsonObject)
            {
                const CJsonValue* enabled = text->Find("enabled");
                if (enabled && enabled->Kind == JsonBool)
                    plan.Options.Text.Enabled = enabled->Bool;
                const CJsonValue* maxBytes = text->Find("maxFileBytes");
                if (maxBytes && maxBytes->Kind == JsonInt)
                    plan.Options.Text.MaxFileBytes = (DWORD)maxBytes->Int;
                const CJsonValue* extensions = text->Find("extensions");
                if (extensions && extensions->Kind == JsonArray)
                {
                    plan.Options.Text.Extensions.clear();
                    for (size_t i = 0; i < extensions->Array.size(); ++i)
                    {
                        if (extensions->Array[i].Kind == JsonString)
                            plan.Options.Text.Extensions.push_back(Utf8ToWide(extensions->Array[i].String));
                    }
                }
            }
        }
    }
    const CJsonValue* rules = parsed.Value.Find("rules");
    if (rules && rules->Kind == JsonArray)
    {
        for (size_t i = 0; i < rules->Array.size(); ++i)
        {
            const CJsonValue& obj = rules->Array[i];
            CRule rule;
            rule.Id = NeedString(obj, "id", false, result);
            rule.Name = NeedString(obj, "name", false, result);
            const CJsonValue* enabled = obj.Find("enabled");
            rule.Enabled = enabled == NULL || enabled->Kind != JsonBool || enabled->Bool;
            const CJsonValue* order = obj.Find("order");
            rule.Order = order && order->Kind == JsonInt ? (int)order->Int : (int)i + 1;
            rule.Destination = NeedString(obj, "destination", false, result);
            const CJsonValue* match = obj.Find("match");
            if (match && match->Kind == JsonObject)
            {
                rule.Match.ScopeRoot = NeedString(*match, "scopeRoot", false, result);
                rule.Match.RelativePathGlob = NeedString(*match, "relativePathGlob", false, result);
                rule.Match.NameMask = NeedString(*match, "nameMask", false, result);
                std::wstring itemType = NeedString(*match, "itemType", false, result);
                if (itemType == L"file")
                    rule.Match.ItemType = ItemFile;
                else if (itemType == L"dir")
                    rule.Match.ItemType = ItemDir;
            }
            plan.Rules.push_back(rule);
        }
    }
    const CJsonValue* edits = parsed.Value.Find("edits");
    if (edits && edits->Kind == JsonArray)
    {
        int previous = 0;
        for (size_t i = 0; i < edits->Array.size(); ++i)
        {
            const CJsonValue& obj = edits->Array[i];
            CEdit edit;
            const CJsonValue* seq = obj.Find("seq");
            if (seq == NULL || seq->Kind != JsonInt)
            {
                result.Ok = false;
                result.Error = L"edit is missing seq";
                return result;
            }
            edit.Seq = (int)seq->Int;
            if (edit.Seq <= previous)
            {
                result.Ok = false;
                result.Error = L"edit sequence is not strictly increasing";
                return result;
            }
            previous = edit.Seq;
            edit.Op = ParseOp(NeedString(obj, "op", true, result));
            edit.Source = NeedString(obj, "source", false, result);
            edit.DestinationDir = NeedString(obj, "destinationDir", false, result);
            edit.NewName = NeedString(obj, "newName", false, result);
            edit.Path = NeedString(obj, "path", false, result);
            edit.Origin = NeedString(obj, "origin", false, result);
            edit.CreatedUtc = NeedString(obj, "createdUtc", false, result);
            edit.Note = NeedString(obj, "note", false, result);
            if (edit.Op == EditRemoveEmptyFolder && plan.Kind != PlanRevert)
            {
                result.Ok = false;
                result.Error = L"removeEmptyFolder is only valid in a revert plan";
                return result;
            }
            plan.Edits.push_back(edit);
        }
    }
    const CJsonValue* resolutions = parsed.Value.Find("resolutions");
    if (resolutions && resolutions->Kind == JsonArray)
    {
        for (size_t i = 0; i < resolutions->Array.size(); ++i)
        {
            CResolution resolution;
            resolution.IssueKey = NeedString(resolutions->Array[i], "issueKey", false, result);
            resolution.Choice = ParseChoice(NeedString(resolutions->Array[i], "choice", false, result));
            resolution.ResultName = NeedString(resolutions->Array[i], "resultName", false, result);
            resolution.DecidedUtc = NeedString(resolutions->Array[i], "decidedUtc", false, result);
            plan.Resolutions.push_back(resolution);
        }
    }
    const CJsonValue* acks = parsed.Value.Find("acknowledgements");
    if (acks && acks->Kind == JsonArray)
    {
        for (size_t i = 0; i < acks->Array.size(); ++i)
        {
            CAcknowledgement ack;
            ack.IssueKey = NeedString(acks->Array[i], "issueKey", false, result);
            ack.AckUtc = NeedString(acks->Array[i], "ackUtc", false, result);
            plan.Acknowledgements.push_back(ack);
        }
    }
    const CJsonValue* review = parsed.Value.Find("review");
    if (review && review->Kind == JsonObject)
    {
        plan.Review.Present = true;
        plan.Review.CompiledSha256 = NeedString(*review, "compiledSha256", false, result);
        plan.Review.ReviewedUtc = NeedString(*review, "reviewedUtc", false, result);
        const CJsonValue* steps = review->Find("stepCount");
        plan.Review.StepCount = steps && steps->Kind == JsonInt ? (int)steps->Int : 0;
    }
    const CJsonValue* baseline = parsed.Value.Find("baseline");
    if (baseline && baseline->Kind == JsonObject)
    {
        plan.BaselineCapturedUtc = NeedString(*baseline, "capturedUtc", false, result);
        const CJsonValue* items = baseline->Find("items");
        if (items && items->Kind == JsonArray)
        {
            for (size_t i = 0; i < items->Array.size(); ++i)
            {
                CBaselineItem item;
                item.Path = NeedString(items->Array[i], "path", false, result);
                const CJsonValue* dir = items->Array[i].Find("dir");
                item.Dir = dir && dir->Kind == JsonBool && dir->Bool;
                const CJsonValue* size = items->Array[i].Find("size");
                item.Size = size && size->Kind == JsonInt ? (unsigned __int64)size->Int : 0;
                item.LastWriteUtc = NeedString(items->Array[i], "lastWriteUtc", false, result);
                const CJsonValue* attrs = items->Array[i].Find("attributes");
                item.Attributes = attrs && attrs->Kind == JsonInt ? (DWORD)attrs->Int : 0;
                const CJsonValue* tag = items->Array[i].Find("reparseTag");
                item.ReparseTag = tag && tag->Kind == JsonInt ? (DWORD)tag->Int : 0;
                item.VolumeSerial = NeedString(items->Array[i], "volumeSerial", false, result);
                item.FileId = NeedString(items->Array[i], "fileId", false, result);
                plan.Baseline.push_back(item);
            }
        }
    }
    const CJsonValue* applies = parsed.Value.Find("applies");
    if (applies && applies->Kind == JsonArray)
    {
        for (size_t i = 0; i < applies->Array.size(); ++i)
        {
            CApplyRecord record;
            record.ApplyId = NeedString(applies->Array[i], "applyId", false, result);
            record.StartedUtc = NeedString(applies->Array[i], "startedUtc", false, result);
            record.FinishedUtc = NeedString(applies->Array[i], "finishedUtc", false, result);
            record.Status = NeedString(applies->Array[i], "status", false, result);
            record.Journal = NeedString(applies->Array[i], "journal", false, result);
            plan.Applies.push_back(record);
        }
    }
    const CJsonValue* extensions = parsed.Value.Find("extensions");
    if (extensions)
        plan.ExtensionsJson = JsonWrite(*extensions);
    CJsonValue unknown;
    unknown.Kind = JsonObject;
    for (std::map<std::string, CJsonValue>::const_iterator it = parsed.Value.Object.begin(); it != parsed.Value.Object.end(); ++it)
    {
        if (!KnownKey(it->first))
            unknown.Object[it->first] = it->second;
    }
    if (!unknown.Object.empty())
        plan.UnknownJson = JsonWrite(unknown);
    plan.Dirty = false;
    result.Ok = result.Error.empty();
    return result;
}

std::string SavePlanJson(const CPlanDocument& plan)
{
    CJsonValue root;
    root.Kind = JsonObject;
    root.Object["format"] = CJsonValue::MakeString("open-salamander.reorganization-plan");
    root.Object["formatVersion"] = CJsonValue::MakeInt(plan.FormatVersion == 0 ? 1 : plan.FormatVersion);
    root.Object["planId"] = CJsonValue::MakeStringW(plan.PlanId);
    root.Object["name"] = CJsonValue::MakeStringW(plan.Name);
    root.Object["kind"] = CJsonValue::MakeString(KindName(plan.Kind));
    if (plan.Kind == PlanRevert)
    {
        CJsonValue revert;
        revert.Kind = JsonObject;
        revert.Object["planId"] = CJsonValue::MakeStringW(plan.RevertOfPlanId);
        revert.Object["applyId"] = CJsonValue::MakeStringW(plan.RevertOfApplyId);
        root.Object["revertOf"] = revert;
    }
    root.Object["createdUtc"] = CJsonValue::MakeStringW(plan.CreatedUtc);
    root.Object["modifiedUtc"] = CJsonValue::MakeStringW(plan.ModifiedUtc);
    if (!plan.CreatedBy.empty())
        root.Object["createdBy"] = CJsonValue::MakeStringW(plan.CreatedBy);
    if (!plan.CreatedOn.empty())
        root.Object["createdOn"] = CJsonValue::MakeStringW(plan.CreatedOn);
    CJsonValue app;
    app.Kind = JsonObject;
    app.Object["name"] = CJsonValue::MakeString("Open Salamander");
    app.Object["version"] = CJsonValue::MakeStringW(plan.ApplicationVersion.empty() ? L"6.0" : plan.ApplicationVersion);
    app.Object["pluginVersion"] = CJsonValue::MakeStringW(plan.PluginVersion.empty() ? L"1.0" : plan.PluginVersion);
    root.Object["application"] = app;
    CJsonValue scopes;
    scopes.Kind = JsonArray;
    for (size_t i = 0; i < plan.ScopeRoots.size(); ++i)
        scopes.Array.push_back(RootToJson(plan.ScopeRoots[i], false));
    root.Object["scopeRoots"] = scopes;
    CJsonValue dests;
    dests.Kind = JsonArray;
    for (size_t i = 0; i < plan.DestinationRoots.size(); ++i)
        dests.Array.push_back(RootToJson(plan.DestinationRoots[i], true));
    root.Object["destinationRoots"] = dests;
    CJsonValue options;
    options.Kind = JsonObject;
    options.Object["conflictDefault"] = CJsonValue::MakeString(ConflictName(plan.Options.ConflictDefault));
    options.Object["keepBothPattern"] = CJsonValue::MakeStringW(plan.Options.KeepBothPattern);
    options.Object["cleanupEmptiedFolders"] = CJsonValue::MakeBool(plan.Options.CleanupEmptiedFolders);
    options.Object["onError"] = CJsonValue::MakeString(plan.Options.OnError == OnErrorSkipDependents ? "skipDependents" : "stop");
    options.Object["verifyAfterApply"] = CJsonValue::MakeBool(plan.Options.VerifyAfterApply);
    options.Object["deepLockCheck"] = CJsonValue::MakeBool(plan.Options.DeepLockCheck);
    CJsonValue store;
    store.Kind = JsonObject;
    store.Object["mode"] = CJsonValue::MakeString(plan.Options.RecoveryStore.Explicit ? "explicit" : "auto");
    if (!plan.Options.RecoveryStore.Paths.empty())
    {
        CJsonValue paths;
        paths.Kind = JsonObject;
        for (std::map<std::wstring, std::wstring>::const_iterator it = plan.Options.RecoveryStore.Paths.begin(); it != plan.Options.RecoveryStore.Paths.end(); ++it)
            paths.Object[WideToUtf8(it->first)] = CJsonValue::MakeStringW(it->second);
        store.Object["paths"] = paths;
    }
    options.Object["recoveryStore"] = store;
    CJsonValue scanners;
    scanners.Kind = JsonObject;
    scanners.Object["shortcuts"] = CJsonValue::MakeBool(plan.Options.ScanShortcuts);
    scanners.Object["urls"] = CJsonValue::MakeBool(plan.Options.ScanUrls);
    scanners.Object["applicationPaths"] = CJsonValue::MakeBool(plan.Options.ScanApplicationPaths);
    CJsonValue text;
    text.Kind = JsonObject;
    text.Object["enabled"] = CJsonValue::MakeBool(plan.Options.Text.Enabled);
    text.Object["maxFileBytes"] = CJsonValue::MakeInt(plan.Options.Text.MaxFileBytes);
    CJsonValue extensions;
    extensions.Kind = JsonArray;
    for (size_t i = 0; i < plan.Options.Text.Extensions.size(); ++i)
        extensions.Array.push_back(CJsonValue::MakeStringW(plan.Options.Text.Extensions[i]));
    text.Object["extensions"] = extensions;
    scanners.Object["text"] = text;
    CJsonValue extra;
    extra.Kind = JsonArray;
    for (size_t i = 0; i < plan.Options.ExtraRoots.size(); ++i)
        extra.Array.push_back(CJsonValue::MakeStringW(plan.Options.ExtraRoots[i]));
    scanners.Object["extraRoots"] = extra;
    options.Object["scanners"] = scanners;
    root.Object["options"] = options;
    CJsonValue rules;
    rules.Kind = JsonArray;
    for (size_t i = 0; i < plan.Rules.size(); ++i)
    {
        CJsonValue rule;
        rule.Kind = JsonObject;
        rule.Object["id"] = CJsonValue::MakeStringW(plan.Rules[i].Id);
        rule.Object["name"] = CJsonValue::MakeStringW(plan.Rules[i].Name);
        rule.Object["enabled"] = CJsonValue::MakeBool(plan.Rules[i].Enabled);
        rule.Object["order"] = CJsonValue::MakeInt(plan.Rules[i].Order);
        rule.Object["destination"] = CJsonValue::MakeStringW(plan.Rules[i].Destination);
        CJsonValue match;
        match.Kind = JsonObject;
        match.Object["scopeRoot"] = StringOrNull(plan.Rules[i].Match.ScopeRoot);
        match.Object["relativePathGlob"] = StringOrNull(plan.Rules[i].Match.RelativePathGlob);
        match.Object["nameMask"] = StringOrNull(plan.Rules[i].Match.NameMask);
        match.Object["itemType"] = CJsonValue::MakeString(plan.Rules[i].Match.ItemType == ItemFile ? "file" : (plan.Rules[i].Match.ItemType == ItemDir ? "dir" : "any"));
        rule.Object["match"] = match;
        rules.Array.push_back(rule);
    }
    root.Object["rules"] = rules;
    CJsonValue edits;
    edits.Kind = JsonArray;
    for (size_t i = 0; i < plan.Edits.size(); ++i)
    {
        const CEdit& edit = plan.Edits[i];
        CJsonValue obj;
        obj.Kind = JsonObject;
        obj.Object["seq"] = CJsonValue::MakeInt(edit.Seq);
        obj.Object["op"] = CJsonValue::MakeString(OpName(edit.Op));
        if (!edit.Source.empty())
            obj.Object["source"] = CJsonValue::MakeStringW(edit.Source);
        if (!edit.DestinationDir.empty())
            obj.Object["destinationDir"] = CJsonValue::MakeStringW(edit.DestinationDir);
        obj.Object["newName"] = StringOrNull(edit.NewName);
        if (!edit.Path.empty())
            obj.Object["path"] = CJsonValue::MakeStringW(edit.Path);
        obj.Object["origin"] = CJsonValue::MakeStringW(edit.Origin);
        obj.Object["createdUtc"] = CJsonValue::MakeStringW(edit.CreatedUtc);
        if (!edit.Note.empty())
            obj.Object["note"] = CJsonValue::MakeStringW(edit.Note);
        edits.Array.push_back(obj);
    }
    root.Object["edits"] = edits;
    CJsonValue resolutions;
    resolutions.Kind = JsonArray;
    for (size_t i = 0; i < plan.Resolutions.size(); ++i)
    {
        CJsonValue obj;
        obj.Kind = JsonObject;
        obj.Object["issueKey"] = CJsonValue::MakeStringW(plan.Resolutions[i].IssueKey);
        obj.Object["choice"] = CJsonValue::MakeString(ChoiceName(plan.Resolutions[i].Choice));
        if (!plan.Resolutions[i].ResultName.empty())
            obj.Object["resultName"] = CJsonValue::MakeStringW(plan.Resolutions[i].ResultName);
        obj.Object["decidedUtc"] = CJsonValue::MakeStringW(plan.Resolutions[i].DecidedUtc);
        resolutions.Array.push_back(obj);
    }
    root.Object["resolutions"] = resolutions;
    CJsonValue acks;
    acks.Kind = JsonArray;
    for (size_t i = 0; i < plan.Acknowledgements.size(); ++i)
    {
        CJsonValue obj;
        obj.Kind = JsonObject;
        obj.Object["issueKey"] = CJsonValue::MakeStringW(plan.Acknowledgements[i].IssueKey);
        obj.Object["ackUtc"] = CJsonValue::MakeStringW(plan.Acknowledgements[i].AckUtc);
        acks.Array.push_back(obj);
    }
    root.Object["acknowledgements"] = acks;
    if (plan.Review.Present)
    {
        CJsonValue review;
        review.Kind = JsonObject;
        review.Object["compiledSha256"] = CJsonValue::MakeStringW(plan.Review.CompiledSha256);
        review.Object["reviewedUtc"] = CJsonValue::MakeStringW(plan.Review.ReviewedUtc);
        review.Object["stepCount"] = CJsonValue::MakeInt(plan.Review.StepCount);
        root.Object["review"] = review;
    }
    CJsonValue baseline;
    baseline.Kind = JsonObject;
    baseline.Object["capturedUtc"] = CJsonValue::MakeStringW(plan.BaselineCapturedUtc);
    CJsonValue items;
    items.Kind = JsonArray;
    for (size_t i = 0; i < plan.Baseline.size(); ++i)
    {
        CJsonValue obj;
        obj.Kind = JsonObject;
        obj.Object["path"] = CJsonValue::MakeStringW(plan.Baseline[i].Path);
        obj.Object["dir"] = CJsonValue::MakeBool(plan.Baseline[i].Dir);
        obj.Object["size"] = CJsonValue::MakeInt((__int64)plan.Baseline[i].Size);
        obj.Object["lastWriteUtc"] = CJsonValue::MakeStringW(plan.Baseline[i].LastWriteUtc);
        obj.Object["attributes"] = CJsonValue::MakeInt(plan.Baseline[i].Attributes);
        obj.Object["reparseTag"] = CJsonValue::MakeInt(plan.Baseline[i].ReparseTag);
        obj.Object["volumeSerial"] = CJsonValue::MakeStringW(plan.Baseline[i].VolumeSerial);
        obj.Object["fileId"] = CJsonValue::MakeStringW(plan.Baseline[i].FileId);
        items.Array.push_back(obj);
    }
    baseline.Object["items"] = items;
    root.Object["baseline"] = baseline;
    CJsonValue applies;
    applies.Kind = JsonArray;
    for (size_t i = 0; i < plan.Applies.size(); ++i)
    {
        CJsonValue obj;
        obj.Kind = JsonObject;
        obj.Object["applyId"] = CJsonValue::MakeStringW(plan.Applies[i].ApplyId);
        obj.Object["startedUtc"] = CJsonValue::MakeStringW(plan.Applies[i].StartedUtc);
        obj.Object["finishedUtc"] = CJsonValue::MakeStringW(plan.Applies[i].FinishedUtc);
        obj.Object["status"] = CJsonValue::MakeStringW(plan.Applies[i].Status);
        obj.Object["journal"] = CJsonValue::MakeStringW(plan.Applies[i].Journal);
        applies.Array.push_back(obj);
    }
    root.Object["applies"] = applies;
    if (!plan.ExtensionsJson.empty())
    {
        CJsonParseResult ext = JsonParse(plan.ExtensionsJson);
        if (ext.Ok)
            root.Object["extensions"] = ext.Value;
    }
    if (root.Object.find("extensions") == root.Object.end())
    {
        CJsonValue empty;
        empty.Kind = JsonObject;
        root.Object["extensions"] = empty;
    }
    if (!plan.UnknownJson.empty())
    {
        CJsonParseResult unknown = JsonParse(plan.UnknownJson);
        if (unknown.Ok && unknown.Value.Kind == JsonObject)
        {
            for (std::map<std::string, CJsonValue>::const_iterator it = unknown.Value.Object.begin(); it != unknown.Value.Object.end(); ++it)
                root.Object[it->first] = it->second;
        }
    }
    return JsonWrite(root);
}

CPlanIoResult SavePlanFile(IFileSystemProbe& probe, const std::wstring& path, CPlanDocument& plan)
{
    CPlanIoResult result;
    plan.ModifiedUtc = UtcNowIso();
    std::string json = SavePlanJson(plan);
    DWORD error = 0;
    if (!probe.WriteAtomic(path, json, error))
    {
        result.Error = L"Could not save the plan.";
        return result;
    }
    plan.FilePath = path;
    plan.Dirty = false;
    result.Ok = true;
    return result;
}

} // namespace reorg
