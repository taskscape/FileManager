using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using NUnit.Framework;

namespace FileManager.UiTests;

/// <summary>
/// Delivery Handoff end-to-end scenarios (handoff-spec.md C.12.3). The working
/// material is the quick-start tree of A.5.1 in the left panel; the right panel
/// is an empty staging folder. Every file lives below the fixture's GUID folder.
/// </summary>
[TestFixture]
[Category("UI")]
public sealed class HandoffUiTests : FileOperationUiTestBase
{
    // Plug-in menu commands (src/plugins/handoff/handoff.rh2).
    private const int CmdBuild = 1;          // CMD_BUILD
    private const int CmdVerify = 2;         // CMD_VERIFY
    private const int CmdValidateSpec = 3;   // CMD_VALIDATE_SPEC
    private const int CmdNewSpec = 4;        // CMD_NEW_SPEC

    // Standard dialog buttons.
    private const int IdOk = 1;
    private const int IdCancel = 2;
    private const int IdRetry = 4;
    private const int IdYes = 6;

    // Frozen control IDs (src/plugins/handoff/lang/lang.rh).
    private const int IdcWorkingPath = 1000;      // IDC_HO_WORKING_PATH (session dialog marker)
    private const int IdcVariables = 1010;        // IDC_HO_VARIABLES
    private const int IdcVariableValue = 1011;    // IDC_HO_VARIABLE_VALUE
    private const int IdcPackageName = 1013;      // IDC_HO_PACKAGE_NAME
    private const int IdcRules = 1021;            // IDC_HO_RULES (review window marker)
    private const int IdcFindings = 1024;         // IDC_HO_FINDINGS
    private const int IdcApproveAll = 1026;       // IDC_HO_APPROVE_ALL
    private const int IdcRescan = 1029;           // IDC_HO_RESCAN
    private const int IdcAckWarnings = 1031;      // IDC_HO_ACK_WARNINGS
    private const int IdcResultIcon = 1040;       // IDC_HO_RESULT_ICON (result dialog marker)
    private const int IdcVerifyPath = 1050;       // IDC_HO_VERIFY_PATH (verify window marker)
    private const int IdcVerifyAgain = 1053;      // IDC_HO_VERIFY_AGAIN
    private const int IdcTemplates = 1060;        // IDC_HO_TEMPLATES (new specification marker)
    private const int IdcNewSpecPath = 1062;      // IDC_HO_NEWSPEC_PATH
    private const int IdcValidatePath = 1070;     // IDC_HO_VALIDATE_PATH (validate dialog marker)

    private const string Client = "ACME";
    private const string Project = "Rebrand";

    private string WorkingRoot => Path.Combine(Workspace.RootDirectory, "handoff-work");
    private string StagingRoot => Path.Combine(Workspace.RootDirectory, "handoff-staging");
    private static string PackageName => $"{Client}-{Project}_Delivery_{DateTime.Now:yyyy-MM-dd}_R01";
    private string PackageRoot => Path.Combine(StagingRoot, PackageName);

    protected override string ApplicationArguments =>
        $"{UiTestSettings.Arguments} -l \"{WorkingRoot}\" -r \"{StagingRoot}\" -p 1";

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Panels list the quick-start tree from the start; scenario-specific
        // changes made later are read by the plug-in's own scan of the disk.
        Directory.CreateDirectory(StagingRoot);
        HandoffFixtures.BuildQuickStartTree(WorkingRoot);
    }

    [Test]
    public void Build_creates_verified_package_with_manifest_and_contents()
    {
        var review = ScanWithVariables();
        ApproveRule(review, "Approved PDFs");
        var resultText = BuildAndReadResult(review);

        Assert.That(resultText, Does.StartWith("Verified"), "The result dialog did not report a verified package.");
        var expected = new[]
        {
            "01_Approved_PDFs/ACME-Rebrand_Brand-Guidelines_v4.pdf",
            "01_Approved_PDFs/ACME-Rebrand_Stationery_v2.pdf",
            "02_Source_Artwork/Campaign/Hero.psd",
            "02_Source_Artwork/Logo/ACME-logo-mono.svg",
            "02_Source_Artwork/Logo/ACME-logo-primary.ai",
            "03_Licensed_Assets/Fonts/Inter-Regular.otf",
            "CONTENTS.txt",
            "manifest.csv",
            "manifest.json",
        };
        Assert.That(ListTree(PackageRoot), Is.EqualTo(expected), "The package tree differs from the plan.");

        using var manifest = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(PackageRoot, "manifest.json")));
        var files = manifest.RootElement.GetProperty("files").EnumerateArray().ToList();
        Assert.That(files, Has.Count.EqualTo(6), "manifest.json does not list every delivered file.");
        foreach (var entry in files)
        {
            var path = Path.Combine(PackageRoot, entry.GetProperty("path").GetString()!.Replace('/', '\\'));
            Assert.That(Sha256(path), Is.EqualTo(entry.GetProperty("sha256").GetString()), $"Hash mismatch for {path}.");
        }
        Assert.That(File.Exists(Path.Combine(StagingRoot, PackageName + ".handoff-build.json")), Is.True,
                    "The build record was not written beside the package.");
        AssertNoPartialFolders();
        // The working material is copied, never moved.
        Assert.That(File.Exists(Path.Combine(WorkingRoot, "Approved", "Brand-Guidelines_v4.pdf")), Is.True);
    }

    [Test]
    public void Missing_required_item_blocks_build_and_lists_omission()
    {
        Directory.Delete(Path.Combine(WorkingRoot, "Artwork", "Final"), recursive: true);
        var review = ScanWithVariables();

        Assert.That(FindingRows(review), Has.Some.Contains("HO-REQ-001"), "The empty required rule was not reported.");
        Assert.That(NativeCommands.IsDialogControlEnabled(review, IdOk), Is.False, "Build stayed enabled with an omission.");
        Assert.That(Directory.EnumerateFileSystemEntries(StagingRoot), Is.Empty, "Reviewing created entries in the staging location.");
        CloseModelessDialog(review);
    }

    [Test]
    public void Image_below_minimum_is_reported()
    {
        File.WriteAllBytes(Path.Combine(WorkingRoot, "Artwork", "Final", "Campaign", "Hero.psd"),
                           HandoffFixtures.MakePsd(1800, 1200, withResolution: true, dpi: 300));
        var review = ScanWithVariables();

        Assert.That(HasRow(FindingRows(review), "HO-IMG-013", "Hero.psd"), Is.True, "The 1 800 px long edge was not reported.");
        CloseModelessDialog(review);
    }

    [Test]
    public void Superseded_revision_is_excluded()
    {
        var review = ScanWithVariables();

        Assert.That(HasRow(FindingRows(review), "HO-SEL-001", "Brand-Guidelines_v3.pdf"), Is.True,
                    "The older revision was not marked as superseded.");
        ApproveRule(review, "Approved PDFs");
        BuildAndReadResult(review);
        var staged = ListTree(PackageRoot);
        Assert.That(staged, Has.Some.EndsWith("Brand-Guidelines_v4.pdf"));
        Assert.That(staged, Has.None.Contains("_v3"), "A superseded revision was staged.");
    }

    [Test]
    public void Existing_package_name_is_refused()
    {
        Directory.CreateDirectory(PackageRoot);
        var session = OpenSessionWithVariables();

        WaitFor(() => NativeCommands.GetDialogControlText(session, IdcPackageName) == PackageName,
                "The package name preview did not show the expected name.");
        Thread.Sleep(500);
        Assert.That(NativeCommands.IsDialogControlEnabled(session, IdOk), Is.False, "Scan stayed enabled for an existing package.");
        Assert.That(Directory.EnumerateFileSystemEntries(PackageRoot), Is.Empty, "The existing package folder was modified.");
        CloseModelessDialog(session);
    }

    [Test]
    public void Cancel_during_build_leaves_no_package()
    {
        var review = ScanWithVariables();
        ApproveRule(review, "Approved PDFs");
        var before = HashTree(WorkingRoot);

        // Holding a source open without sharing makes the copy fail deterministically,
        // so the build is still running when the reviewer cancels it from the prompt.
        using (new FileStream(Path.Combine(WorkingRoot, "Approved", "Stationery_v2.pdf"), FileMode.Open, FileAccess.Read, FileShare.None))
        {
            EnsureWarningsAcknowledged(review);
            NativeCommands.PostDialogButtonClick(review, IdOk);
            var prompt = WaitForTopLevel(handle => NativeCommands.HasDialogButton(handle, IdRetry) &&
                                                   NativeCommands.HasDialogButton(handle, IdCancel), 60_000,
                                         "The build did not ask whether to retry the locked file.");
            NativeCommands.PostDialogButtonClick(prompt, IdCancel);
            var result = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcResultIcon), 60_000,
                                         "The cancelled build did not show its result.");
            NativeCommands.PostDialogButtonClick(result, IdOk);
        }

        Assert.That(Directory.Exists(PackageRoot), Is.False, "A cancelled build left a package under its final name.");
        AssertNoPartialFolders();
        Assert.That(HashTree(WorkingRoot), Is.EqualTo(before), "The working material changed during a cancelled build.");
        CloseModelessDialog(review);
    }

    [Test]
    public void Verify_detects_modified_and_unlisted_files()
    {
        var review = ScanWithVariables();
        ApproveRule(review, "Approved PDFs");
        BuildAndReadResult(review);
        WaitUntilNativeWindowClosed(review);

        File.AppendAllText(Path.Combine(PackageRoot, "02_Source_Artwork", "Logo", "ACME-logo-mono.svg"), "<!-- edited -->");
        File.WriteAllText(Path.Combine(PackageRoot, "extra.txt"), "not part of the delivery");
        FocusTargetItem(PackageName);
        InvokeHandoff(CmdVerify, "Verify Delivery Package");
        var verify = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcVerifyPath), 30_000,
                                     "The verify window did not open.");
        WaitFor(() => NativeCommands.IsDialogControlEnabled(verify, IdcVerifyAgain), "Verification did not finish.", 120_000);

        var rows = FindingRows(verify);
        Assert.That(HasRow(rows, "HO-VER-003", "ACME-logo-mono.svg"), Is.True, "The modified file was not reported: " + string.Join(" | ", rows));
        Assert.That(HasRow(rows, "HO-VER-004", "extra.txt"), Is.True, "The unlisted file was not reported: " + string.Join(" | ", rows));
        CloseModelessDialog(verify);
    }

    [Test]
    public void Invalid_specification_reports_line_and_column()
    {
        const string name = "broken.handoff.json";
        File.WriteAllText(Path.Combine(WorkingRoot, name), "{\n  \"handoffSpec\": 1,\n  \"id\": \"broken\",,\n}\n");
        RefreshSourcePanel();
        FocusSourceItem(name);

        InvokeHandoff(CmdValidateSpec, "Validate Specification");
        var dialog = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcValidatePath), 30_000,
                                     "The validate dialog did not open.");
        WaitFor(() => FindingRows(dialog).Count > 0, "The validate dialog listed no findings.");

        // Columns: Line, Column, Code, Message.
        var row = FindingRows(dialog).FirstOrDefault(text => text.Contains("HO-SPEC-001"));
        Assert.That(row, Is.Not.Null, "HO-SPEC-001 was not listed.");
        var cells = row!.Split('\t');
        Assert.That(cells[0], Is.EqualTo("3"), "The syntax error was not located on line 3.");
        Assert.That(int.TryParse(cells[1], out var column) && column > 0, Is.True, "The syntax error had no column.");
        NativeCommands.PostDialogButtonClick(dialog, IdOk);
    }

    [Test]
    public void New_specification_from_template_never_overwrites()
    {
        var target = Path.Combine(WorkingRoot, ".handoff", "client-delivery-example.handoff.json");
        var template = File.ReadAllBytes(Path.Combine(HandoffFixtures.RepositoryRoot(), "src", "plugins", "handoff", "templates",
                                                      "client-delivery-example.handoff.json"));

        var first = OpenNewSpecDialog();
        Assert.That(NativeCommands.GetDialogControlText(first, IdcNewSpecPath), Is.EqualTo(target).IgnoreCase,
                    "The default location is not the working folder's .handoff folder.");
        NativeCommands.PostDialogButtonClick(first, IdOk);
        WaitUntilNativeWindowClosed(first);
        Assert.That(File.ReadAllBytes(target), Is.EqualTo(template), "The written specification differs from the template.");

        File.AppendAllText(target, " ");
        var edited = File.ReadAllBytes(target);
        var second = OpenNewSpecDialog();
        // Creating the first file focused it inside .handoff, so the second default would nest; aim at the same file explicitly.
        NativeCommands.SetDialogControlText(second, IdcNewSpecPath, target);
        NativeCommands.PostDialogButtonClick(second, IdOk);
        var refusal = WaitForTopLevel(handle => handle != second && !NativeCommands.HasDialogControl(handle, IdcTemplates) &&
                                                (NativeCommands.HasDialogButton(handle, IdOk) || NativeCommands.HasDialogButton(handle, IdCancel)),
                                      30_000, "Creating over an existing file did not report a refusal.");
        NativeCommands.PostDialogButtonClick(refusal, NativeCommands.HasDialogButton(refusal, IdOk) ? IdOk : IdCancel);
        WaitUntilNativeWindowClosed(refusal);
        NativeCommands.PostDialogButtonClick(second, IdCancel);
        WaitUntilNativeWindowClosed(second);
        Assert.That(File.ReadAllBytes(target), Is.EqualTo(edited), "An existing specification was overwritten.");
    }

    [Test]
    public void Unicode_and_long_paths_are_staged()
    {
        // A Polish name exercises transliteration; a CJK name below a working path longer
        // than MAX_PATH exercises long paths (the rule's flat target keeps the package path short).
        File.WriteAllBytes(Path.Combine(WorkingRoot, "Approved", "Przegląd_Łódź_v1.pdf"), HandoffFixtures.MakePdf(1, 5));
        var deep = Path.Combine(WorkingRoot, "Approved");
        for (var i = 0; Path.Combine(deep, "日本語_v1.pdf").Length <= 300; i++)
            deep = Path.Combine(deep, $"segment-{i:00}-long-folder-name-for-path-limits");
        Directory.CreateDirectory(deep);
        File.WriteAllBytes(Path.Combine(deep, "日本語_v1.pdf"), HandoffFixtures.MakePdf(1, 6));

        var review = ScanWithVariables();
        ApproveRule(review, "Approved PDFs");
        var resultText = BuildAndReadResult(review);

        Assert.That(resultText, Does.StartWith("Verified"));
        Assert.That(File.Exists(Path.Combine(PackageRoot, "01_Approved_PDFs", "ACME-Rebrand_Przeglad_Lodz_v1.pdf")), Is.True,
                    "The Polish name was not transliterated into a portable target name.");
        // Brand guidelines v4, stationery, the Polish file, and the CJK file below the long path.
        var stagedPdfs = Directory.GetFiles(Path.Combine(PackageRoot, "01_Approved_PDFs"), "*.pdf");
        Assert.That(stagedPdfs, Has.Length.EqualTo(4), "The file below the long working path was not staged.");
        Assert.That(stagedPdfs.All(path => Path.GetFileName(path).All(ch => ch < 128)), Is.True, "A staged name was not made portable.");
    }

    // ---------------------------------------------------------------- flows

    private void InvokeHandoff(int pluginCommand, string commandName)
    {
        RequireHandoffPluginRuntime();
        NativeCommands.Execute(NativeMainWindowHandle, WaitForPluginCommand("handoff.spl", pluginCommand, commandName));
    }

    private nint OpenSessionWithVariables()
    {
        InvokeHandoff(CmdBuild, "Build Delivery Package");
        var session = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcWorkingPath), 30_000,
                                      "The build session dialog did not open.");
        // The specification is discovered after the dialog appears; its variables then fill the list.
        WaitFor(() => ListRows(session, IdcVariables).Count >= 4, "The specification's variables were not listed.");
        SetVariable(session, "Client code", Client);
        SetVariable(session, "Project code", Project);
        return session;
    }

    private nint ScanWithVariables()
    {
        var session = OpenSessionWithVariables();
        WaitFor(() => NativeCommands.IsDialogControlEnabled(session, IdOk), "Scan did not become available.");
        NativeCommands.PostDialogButtonClick(session, IdOk);
        var review = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcRules), 30_000,
                                     "The review window did not open.");
        WaitFor(() => NativeCommands.IsDialogControlEnabled(review, IdcRescan) && ListRows(review, IdcRules).Count > 1,
                "Scanning and inspection did not finish.", 120_000);
        return review;
    }

    private void SetVariable(nint session, string label, string value)
    {
        SelectListRow(session, IdcVariables, row => row.StartsWith(label, StringComparison.Ordinal));
        NativeCommands.SetDialogControlText(session, IdcVariableValue, value);
        WaitFor(() => ListRows(session, IdcVariables).Any(row => row.StartsWith(label, StringComparison.Ordinal) && row.Contains(value)),
                $"The value of {label} was not applied.");
    }

    private void ApproveRule(nint review, string ruleTitle)
    {
        SelectListRow(review, IdcRules, row => row.StartsWith(ruleTitle, StringComparison.Ordinal));
        WaitFor(() => NativeCommands.IsDialogControlEnabled(review, IdcApproveAll), "Approve all in rule did not become available.");
        NativeCommands.ClickDialogButton(review, IdcApproveAll);
    }

    private void EnsureWarningsAcknowledged(nint review)
    {
        if (!NativeCommands.IsDialogControlEnabled(review, IdOk) && NativeCommands.IsDialogControlEnabled(review, IdcAckWarnings) &&
            !NativeCommands.IsDialogCheckBoxChecked(review, IdcAckWarnings))
            NativeCommands.ClickDialogControl(review, IdcAckWarnings);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(review, IdOk),
                "Build did not become available: " + string.Join(" | ", FindingRows(review)));
    }

    private string BuildAndReadResult(nint review)
    {
        EnsureWarningsAcknowledged(review);
        // Build opens a modal result dialog, so the click is posted rather than sent.
        NativeCommands.PostDialogButtonClick(review, IdOk);
        var result = WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcResultIcon), 120_000,
                                     "The build did not finish.");
        var text = NativeCommands.GetDialogControlText(result, 1041); // IDC_HO_RESULT_TEXT
        NativeCommands.PostDialogButtonClick(result, IdOk);
        WaitUntilNativeWindowClosed(result);
        return text;
    }

    private nint OpenNewSpecDialog()
    {
        InvokeHandoff(CmdNewSpec, "New Specification from Template");
        return WaitForTopLevel(handle => NativeCommands.HasDialogControl(handle, IdcTemplates), 30_000,
                               "The new specification dialog did not open.");
    }

    private void FocusTargetItem(string name)
    {
        var list = FindPanelList(left: false);
        var handle = list.Properties.NativeWindowHandle.Value;
        NativeCommands.ActivateFilePanel(handle);
        NativeCommands.RefreshActiveFilePanel(NativeMainWindowHandle);
        Thread.Sleep(750);
        NativeCommands.ClearActiveSelection(NativeMainWindowHandle);
        NativeCommands.QuickSearch(handle, name);
        Thread.Sleep(750);
    }

    // ---------------------------------------------------------------- native and UIA helpers

    private nint WaitForTopLevel(Func<nint, bool> predicate, int timeoutMilliseconds, string failure)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMilliseconds);
        while (DateTime.UtcNow < deadline)
        {
            foreach (var handle in NativeCommands.GetTopLevelWindows(Application.ProcessId))
                if (handle != NativeMainWindowHandle && predicate(handle))
                    return handle;
            Thread.Sleep(100);
        }
        Assert.Fail(failure + " Open windows: " + string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId)));
        return 0;
    }

    private static void WaitFor(Func<bool> condition, string failure, int timeoutMilliseconds = 30_000)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromMilliseconds(timeoutMilliseconds);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                if (condition())
                    return;
            }
            catch (Exception ex) when (ex is System.Runtime.InteropServices.COMException || ex is FlaUI.Core.Exceptions.ElementNotAvailableException)
            {
                // A list can be rebuilt between UIA reads; poll again.
            }
            Thread.Sleep(200);
        }
        Assert.Fail(failure);
    }

    private AutomationElement ListElement(nint dialog, int controlId)
    {
        var element = Automation.FromHandle(dialog).FindFirstDescendant(cf => cf.ByAutomationId(controlId.ToString()));
        Assert.That(element, Is.Not.Null, $"Control {controlId} was not exposed to UI Automation.");
        return element!;
    }

    private IEnumerable<AutomationElement> RowElements(nint dialog, int controlId) =>
        ListElement(dialog, controlId).FindAllChildren(cf => cf.ByControlType(ControlType.ListItem)
                                                                .Or(cf.ByControlType(ControlType.DataItem)));

    // Each row as tab-separated cell texts in column order.
    private List<string> ListRows(nint dialog, int controlId) =>
        RowElements(dialog, controlId).Select(row => string.Join("\t", RowCells(row))).ToList();

    private static List<string> RowCells(AutomationElement row)
    {
        // The list-view provider exposes sub-items as children; some versions also repeat the first column.
        var cells = row.FindAllChildren().Select(cell => cell.Name ?? string.Empty).ToList();
        if (cells.Count == 0 || cells[0] != (row.Name ?? string.Empty))
            cells.Insert(0, row.Name ?? string.Empty);
        return cells;
    }

    private static bool HasRow(IEnumerable<string> rows, string first, string second) =>
        rows.Any(row => row.Contains(first, StringComparison.Ordinal) && row.Contains(second, StringComparison.Ordinal));

    private List<string> FindingRows(nint dialog) => ListRows(dialog, IdcFindings);

    private void SelectListRow(nint dialog, int controlId, Func<string, bool> predicate)
    {
        AutomationElement? match = null;
        WaitFor(() => (match = RowElements(dialog, controlId).FirstOrDefault(row => predicate(row.Name))) is not null,
                $"No row matched in list {controlId}.");
        match!.Patterns.SelectionItem.Pattern.Select();
    }

    private void AssertNoPartialFolders()
    {
        var partials = Directory.EnumerateDirectories(StagingRoot)
            .Where(path => Path.GetFileName(path).StartsWith('.') && path.EndsWith(".partial", StringComparison.OrdinalIgnoreCase))
            .ToList();
        Assert.That(partials, Is.Empty, "An unfinished package folder remained in the staging location.");
    }

    private static List<string> ListTree(string root) =>
        Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .Select(path => Path.GetRelativePath(root, path).Replace('\\', '/'))
            .OrderBy(path => path, StringComparer.OrdinalIgnoreCase)
            .ToList();

    private static Dictionary<string, string> HashTree(string root) =>
        Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories)
            .ToDictionary(path => Path.GetRelativePath(root, path), Sha256, StringComparer.OrdinalIgnoreCase);

    private static string Sha256(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }
}

/// <summary>Quick-start working material (handoff-spec.md A.5.1) generated without external tools.</summary>
internal static class HandoffFixtures
{
    private const double A4W = 595.276, A4H = 841.89, A3W = 841.89, A3H = 1190.551;

    internal static void BuildQuickStartTree(string root)
    {
        Write(root, ".handoff/client-delivery.handoff.json",
              File.ReadAllBytes(Path.Combine(RepositoryRoot(), "src", "plugins", "handoff", "templates", "client-delivery-example.handoff.json")));
        Write(root, "Approved/Brand-Guidelines_v3.pdf", MakePdf(2, 1));
        Write(root, "Approved/Brand-Guidelines_v4.pdf", MakePdf(3, 2));
        Write(root, "Approved/Stationery_v2.pdf", MakePdf(1, 3));
        Write(root, "Artwork/Final/Logo/ACME-logo-primary.ai", MakePdf(1, 4, A3W, A3H));
        Write(root, "Artwork/Final/Logo/ACME-logo-mono.svg",
              Encoding.UTF8.GetBytes("<?xml version=\"1.0\"?>\n<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>\n"));
        Write(root, "Artwork/Final/Campaign/Hero.psd", MakePsd(2400, 1600, withResolution: true, dpi: 300));
        Write(root, "Artwork/WIP/Hero-explorations.psd", MakePsd(800, 600, withResolution: false, dpi: 0));
        Write(root, "Assets/Fonts/Inter-Regular.otf", Encoding.ASCII.GetBytes("OTTO").Concat(Enumerable.Repeat((byte)1, 252)).ToArray());
        Write(root, "Assets/licences.csv",
              Encoding.UTF8.GetBytes("File,Licence,Licensor,Expires,Scope\r\nInter-Regular.otf,OFL-1.1,The Inter Project Authors,,Unlimited\r\n"));
        Write(root, "Thumbs.db", Enumerable.Repeat((byte)7, 64).ToArray());
    }

    // Minimal well-formed PDF with exact cross-reference offsets; 'variant' keeps contents unique.
    internal static byte[] MakePdf(int pages, int variant, double width = A4W, double height = A4H)
    {
        var objects = new List<string> { "<< /Type /Catalog /Pages 2 0 R >>" };
        var kids = string.Join(" ", Enumerable.Range(0, pages).Select(i => $"{3 + i} 0 R"));
        objects.Add($"<< /Type /Pages /Kids [{kids}] /Count {pages} >>");
        for (var i = 0; i < pages; i++)
            objects.Add(FormattableString.Invariant($"<< /Type /Page /Parent 2 0 R /Resources << >> /MediaBox [0 0 {width:0.000} {height:0.000}] /Variant {variant} >>"));
        var pdf = new StringBuilder("%PDF-1.7\n%âãÏÓ\n");
        var offsets = new List<int>();
        var latin1 = Encoding.Latin1;
        foreach (var (body, index) in objects.Select((body, index) => (body, index)))
        {
            offsets.Add(latin1.GetByteCount(pdf.ToString()));
            pdf.Append($"{index + 1} 0 obj\n{body}\nendobj\n");
        }
        var xref = latin1.GetByteCount(pdf.ToString());
        pdf.Append($"xref\n0 {objects.Count + 1}\n0000000000 65535 f \n");
        foreach (var offset in offsets)
            pdf.Append($"{offset:D10} 00000 n \n");
        pdf.Append($"trailer\n<< /Size {objects.Count + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n");
        return latin1.GetBytes(pdf.ToString());
    }

    // PSD header with an optional ResolutionInfo resource (enough for header inspection).
    internal static byte[] MakePsd(uint width, uint height, bool withResolution, double dpi)
    {
        var psd = new List<byte>(Encoding.ASCII.GetBytes("8BPS"));
        Put16(psd, 1);
        psd.AddRange(new byte[6]);
        Put16(psd, 3);
        Put32(psd, height);
        Put32(psd, width);
        Put16(psd, 8);
        Put16(psd, 3);
        Put32(psd, 0);
        var resources = new List<byte>();
        if (withResolution)
        {
            resources.AddRange(Encoding.ASCII.GetBytes("8BIM"));
            Put16(resources, 0x03ED);
            Put16(resources, 0);
            Put32(resources, 16);
            var value = (uint)(dpi * 65536.0);
            Put32(resources, value);
            Put16(resources, 1);
            Put16(resources, 1);
            Put32(resources, value);
            Put16(resources, 1);
            Put16(resources, 1);
        }
        Put32(psd, (uint)resources.Count);
        psd.AddRange(resources);
        Put32(psd, 0);
        Put16(psd, 0);
        return psd.ToArray();
    }

    internal static string RepositoryRoot()
    {
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null && !Directory.Exists(Path.Combine(directory.FullName, "src", "plugins", "handoff", "templates")))
            directory = directory.Parent;
        Assert.That(directory, Is.Not.Null, "The repository root with src\\plugins\\handoff\\templates was not found.");
        return directory!.FullName;
    }

    private static void Write(string root, string relative, byte[] bytes)
    {
        var path = Path.Combine(root, relative.Replace('/', '\\'));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    private static void Put16(List<byte> data, int value)
    {
        data.Add((byte)(value >> 8));
        data.Add((byte)value);
    }

    private static void Put32(List<byte> data, uint value)
    {
        data.Add((byte)(value >> 24));
        data.Add((byte)(value >> 16));
        data.Add((byte)(value >> 8));
        data.Add((byte)value);
    }
}
