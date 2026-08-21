using System.IO.Compression;
using System.Diagnostics;
using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[Category("UI")]
[Category("Characterization")]
[Category("KnownDefect")]
public sealed class ReportedDefectCharacterizationUiTests : FileOperationUiTestBase
{
    private const string ZipName = "characterization-open.zip";
    private const string ZipPayloadName = "zip-characterization-payload.txt";
    private const string MoveOverwriteName = "move-overwrite-characterization.txt";
    private const string MoveCancelName = "move-cancel-characterization.txt";
    private readonly string editFileName = $"files-menu-edit-{Guid.NewGuid():N}.txt";
    private readonly HashSet<string> journalsBeforeStart = new(StringComparer.OrdinalIgnoreCase);

    protected override void BeforeFileManagerStarted()
    {
        journalsBeforeStart.Clear();
        var journalDirectory = GetOperationJournalDirectory();
        if (Directory.Exists(journalDirectory))
            journalsBeforeStart.UnionWith(Directory.EnumerateFiles(journalDirectory));
        // Seed every reported scenario before launch so the first native panel listing contains deterministic fixtures.
        CreateZipFixture();
        File.WriteAllText(Workspace.SourcePath(MoveOverwriteName), "move-overwrite-source");
        File.WriteAllText(Workspace.TargetPath(MoveOverwriteName), "move-overwrite-existing-target");
        File.WriteAllText(Workspace.SourcePath(MoveCancelName), "move-cancel-source");
        File.WriteAllText(Workspace.TargetPath(MoveCancelName), "move-cancel-existing-target");
        File.WriteAllText(Workspace.SourcePath(editFileName), "edit-characterization-content");
    }

    protected override void OnAfterFileManagerStopped()
    {
        try
        {
            var journalDirectory = GetOperationJournalDirectory();
            if (!Directory.Exists(journalDirectory))
                return;

            // A deliberately failing operation must not leave recovery input that changes the next independent test.
            foreach (var journal in Directory.EnumerateFiles(journalDirectory).Where(path => !journalsBeforeStart.Contains(path)))
                File.Delete(journal);
        }
        finally
        {
            base.OnAfterFileManagerStopped();
        }
    }

    [Test]
    public void Zip_open_after_information_dialog_navigates_into_archive()
    {
        UiTestSettings.RequireZipPlugin();

        SelectSourceItem(ZipName);
        NativeCommands.Execute(MainWindowHandle, NativeCommands.Open);
        // The reported defect shows an informational OK dialog before returning to an unchanged panel.
        DismissOptionalPrompt(1, TimeSpan.FromSeconds(3), 1);

        WaitForCondition(
            () => NativeCommands.GetWindowTitle(MainWindowHandle).Contains(ZipName, StringComparison.OrdinalIgnoreCase),
            "After accepting the ZIP-open message, FileManager did not navigate into the selected archive.");

        // Quick search proves that navigation exposes archive contents rather than only changing decorative window text.
        SelectSourceItem(ZipPayloadName);
    }

    [Test]
    public void Help_search_returns_the_configured_existing_help_result()
    {
        var (searchTerm, expectedResult) = UiTestSettings.RequireHelpSearchFixture();
        RequireDeployedMainHelp();
        var windowsBefore = NativeCommands.GetVisibleTopLevelWindowHandles().ToHashSet();
        nint helpWindowHandle = 0;
        try
        {
            NativeCommands.Execute(MainWindowHandle, NativeCommands.HelpSearch);
            helpWindowHandle = WaitForNewTopLevelWindow(windowsBefore,
                handle => string.Equals(NativeCommands.GetWindowClassName(handle), "HH Parent", StringComparison.Ordinal));

            var helpWindow = Automation.FromHandle(helpWindowHandle).AsWindow();
            var searchInput = helpWindow.FindAllDescendants()
                .FirstOrDefault(element => element.ControlType == ControlType.Edit && element.IsEnabled)
                ?.AsTextBox();
            Assert.That(searchInput, Is.Not.Null, "The Help Search panel did not expose an enabled search input.");

            searchInput!.Text = searchTerm;
            NativeCommands.PressEnter(searchInput.Properties.NativeWindowHandle.Value);

            WaitForCondition(() => HelpWindowContains(helpWindowHandle, expectedResult),
                $"Help Search did not return the configured existing result '{expectedResult}' for '{searchTerm}'.");
        }
        finally
        {
            // HTML Help owns a top-level window outside normal FileManager dialog cleanup.
            NativeCommands.RequestWindowClose(helpWindowHandle);
        }
    }

    [Test]
    public void Move_overwrite_replaces_existing_target_instead_of_deleting_it()
    {
        var source = Workspace.SourcePath(MoveOverwriteName);
        var target = Workspace.TargetPath(MoveOverwriteName);

        ExecuteWithPath(NativeCommands.MoveFiles, MoveOverwriteName, Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES: overwrite

        WaitForFileSystem(() => !File.Exists(source), "Confirmed move overwrite did not finish removing the source.");
        Assert.Multiple(() =>
        {
            Assert.That(File.Exists(target), Is.True,
                        "Move overwrite deleted the existing target instead of replacing it.");
            Assert.That(File.ReadAllText(target), Is.EqualTo("move-overwrite-source"),
                        "Move overwrite did not preserve the moved source content at the target.");
        });
    }

    [Test]
    public void Move_collision_cancel_keeps_both_files_unchanged()
    {
        var source = Workspace.SourcePath(MoveCancelName);
        var target = Workspace.TargetPath(MoveCancelName);

        ExecuteWithPath(NativeCommands.MoveFiles, MoveCancelName, Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(2), 2); // IDCANCEL

        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(source), Is.EqualTo("move-cancel-source"),
                        "Cancelling a move collision changed or removed the source.");
            Assert.That(File.ReadAllText(target), Is.EqualTo("move-cancel-existing-target"),
                        "Cancelling a move collision changed or removed the existing target.");
        });
    }

    [Test]
    public void Files_menu_edit_opens_editor_without_freezing_main_window()
    {
        var windowsBefore = NativeCommands.GetVisibleTopLevelWindowHandles().ToHashSet();
        var notepadProcessesBefore = Process.GetProcessesByName("notepad")
            .Select(process =>
            {
                using (process)
                    return process.Id;
            })
            .ToHashSet();
        nint editorWindowHandle = 0;
        try
        {
            SelectSourceItem(editFileName);
            NativeCommands.Execute(MainWindowHandle, NativeCommands.Edit);

            var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
            var lastResponsive = DateTime.UtcNow;
            while (DateTime.UtcNow < deadline)
            {
                if (NativeCommands.IsWindowResponsive(MainWindowHandle))
                    lastResponsive = DateTime.UtcNow;
                else if (DateTime.UtcNow - lastResponsive > TimeSpan.FromSeconds(2))
                    Assert.Fail("Files > Edit left the FileManager main window unresponsive for more than two seconds.");

                editorWindowHandle = NativeCommands.GetVisibleTopLevelWindowHandles()
                    .FirstOrDefault(handle => !windowsBefore.Contains(handle) &&
                                              NativeCommands.GetWindowTitle(handle)
                                                  .Contains(editFileName, StringComparison.OrdinalIgnoreCase));
                if (editorWindowHandle != 0)
                    return;
                Thread.Sleep(100);
            }

            Assert.Fail("Files > Edit did not open an editor window for the selected file within ten seconds.");
        }
        finally
        {
            // The unique file name restricts cleanup to the editor window created by this case.
            if (editorWindowHandle == 0)
            {
                editorWindowHandle = NativeCommands.GetVisibleTopLevelWindowHandles()
                    .FirstOrDefault(handle => !windowsBefore.Contains(handle) &&
                                              NativeCommands.GetWindowTitle(handle)
                                                  .Contains(editFileName, StringComparison.OrdinalIgnoreCase));
            }
            NativeCommands.RequestWindowClose(editorWindowHandle);
            CloseTestOwnedDefaultEditorProcesses(notepadProcessesBefore);
        }
    }

    private void CreateZipFixture()
    {
        using var archive = ZipFile.Open(Workspace.SourcePath(ZipName), ZipArchiveMode.Create);
        var entry = archive.CreateEntry(ZipPayloadName);
        using var writer = new StreamWriter(entry.Open());
        writer.Write("zip-characterization-content");
    }

    private static string GetOperationJournalDirectory() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Open Salamander", "operation-journals");

    private static void CloseTestOwnedDefaultEditorProcesses(IReadOnlySet<int> processesBefore)
    {
        foreach (var process in Process.GetProcessesByName("notepad"))
        {
            using (process)
            {
                // A frozen default editor may never create an HWND, so its post-command PID is the only safe cleanup identity.
                if (processesBefore.Contains(process.Id) || process.HasExited)
                    continue;
                process.Kill(entireProcessTree: true);
                process.WaitForExit(5000);
            }
        }
    }

    private static void RequireDeployedMainHelp()
    {
        var executableDirectory = Path.GetDirectoryName(UiTestSettings.ExecutablePath)!;
        var helpDirectory = Path.Combine(executableDirectory, "help");
        // The search UI cannot be characterized when the selected build has no compiled main help archive.
        if (!Directory.Exists(helpDirectory) ||
            !Directory.EnumerateFiles(helpDirectory, "salamand.chm", SearchOption.AllDirectories).Any())
            Assert.Ignore("Deploy help/<language>/salamand.chm beside the tested executable to run Help Search characterization.");
    }

    private nint WaitForNewTopLevelWindow(IReadOnlySet<nint> windowsBefore, Func<nint, bool> predicate)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            var handle = NativeCommands.GetVisibleTopLevelWindowHandles()
                .FirstOrDefault(candidate => !windowsBefore.Contains(candidate) && predicate(candidate));
            if (handle != 0)
                return handle;
            if (!NativeCommands.IsWindowResponsive(MainWindowHandle))
                Assert.Fail("FileManager became unresponsive while opening the requested top-level window.");
            Thread.Sleep(100);
        }

        Assert.Fail("The requested top-level window did not open within ten seconds.");
        return 0;
    }

    private bool HelpWindowContains(nint helpWindowHandle, string expectedResult)
    {
        if (!NativeCommands.IsWindowAvailable(helpWindowHandle))
            return false;
        var helpWindow = Automation.FromHandle(helpWindowHandle);
        // Result discovery remains language-configurable while still asserting the real rendered Help UI.
        return helpWindow.FindAllDescendants()
            .Any(element => element.Name.Contains(expectedResult, StringComparison.OrdinalIgnoreCase));
    }

    private static void WaitForCondition(Func<bool> predicate, string failureMessage)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
                return;
            Thread.Sleep(100);
        }
        Assert.Fail(failureMessage);
    }
}
