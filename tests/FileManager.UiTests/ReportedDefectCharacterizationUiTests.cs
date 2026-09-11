using System.IO.Compression;
using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class ReportedDefectCharacterizationUiTests : FileOperationUiTestBase
{
    private const string ZipName = "zip-open-characterization.zip";
    private const string ZipPayloadName = "zip-open-payload.txt";
    private const string NestedZipName = "zip-open-browse-characterization.zip";
    private const string NestedZipFolderName = "zip-open-browse-folder";
    private const string NestedZipInnerFileName = "zip-open-browse-payload.txt";

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Seed the archive before launch so the native panel's initial enumeration contains the reported fixture.
        using var archive = ZipFile.Open(workspace.SourcePath(ZipName), ZipArchiveMode.Create);
        var entry = archive.CreateEntry(ZipPayloadName);
        using var writer = new StreamWriter(entry.Open());
        writer.Write("zip-characterization-content");

        // The browse fixture mirrors the reported scenario with files and folders: the
        // archive must open and its directory structure must stay navigable afterwards.
        using var nested = ZipFile.Open(workspace.SourcePath(NestedZipName), ZipArchiveMode.Create);
        var rootEntry = nested.CreateEntry("zip-open-browse-root.txt");
        using (var rootWriter = new StreamWriter(rootEntry.Open()))
            rootWriter.Write("zip-open-browse-root-content");
        var innerEntry = nested.CreateEntry(NestedZipFolderName + "/" + NestedZipInnerFileName);
        using (var innerWriter = new StreamWriter(innerEntry.Open()))
            innerWriter.Write("zip-open-browse-inner-content");
    }

    [Test]
    public void Zip_open_after_information_dialog_navigates_into_archive()
    {
        UiTestSettings.RequireZipPlugin();

        SelectSourceItem(ZipName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.OpenFile);
        DismissOptionalInformationDialog();

        WaitForMainWindowTitleContaining(ZipName,
            "After accepting the ZIP-open message, FileManager did not navigate into the selected archive.");

        // Quick search exercises the archive listing after navigation instead of accepting a decorative title change alone.
        SelectSourceItem(ZipPayloadName);
    }

    [Test]
    public void Zip_open_browses_folder_contents_after_the_information_dialog()
    {
        UiTestSettings.RequireZipPlugin();

        SelectSourceItem(NestedZipName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.OpenFile);
        DismissOptionalInformationDialog();

        WaitForMainWindowTitleContaining(NestedZipName,
            "After accepting the ZIP-open message, FileManager did not navigate into the selected archive.");

        // The archive root must list the seeded folder, proving the reported listing is browsable.
        SelectSourceItem(NestedZipFolderName);

        // Entering the listed folder must navigate inside the archive instead of failing silently.
        OpenFocusedItem();
        WaitForMainWindowTitleContaining(NestedZipFolderName,
            "Entering the archived folder did not navigate into its contents.");

        // The nested member must be listed inside the folder, so its contents are accessible.
        SelectSourceItem(NestedZipInnerFileName);
    }

    [Test]
    public void Help_search_returns_the_configured_existing_help_result()
    {
        var (searchTerm, expectedResult) = UiTestSettings.RequireHelpSearchFixture();
        RequireDeployedMainHelp();
        Window? helpWindow = null;
        try
        {
            NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.HelpSearch);
            helpWindow = WaitForDesktopWindow(window =>
                    string.Equals(window.Properties.ClassName.ValueOrDefault, "HH Parent", StringComparison.Ordinal),
                "The HTML Help Search window did not open.");

            var searchInput = helpWindow.FindAllDescendants()
                .FirstOrDefault(element => element.ControlType == FlaUI.Core.Definitions.ControlType.Edit && element.IsEnabled)
                ?.AsTextBox();
            Assert.That(searchInput, Is.Not.Null, "The Help Search panel did not expose an enabled search input.");

            searchInput!.Text = searchTerm;
            NativeCommands.PressEnter(searchInput.Properties.NativeWindowHandle.Value);

            WaitForFileSystem(
                () => helpWindow.FindAllDescendants()
                    .Any(element => element.Name.Contains(expectedResult, StringComparison.OrdinalIgnoreCase)),
                $"Help Search did not return the configured existing result '{expectedResult}' for '{searchTerm}'.");
        }
        finally
        {
            // HTML Help runs outside the FileManager process, so close only the window captured for this test.
            if (helpWindow is not null && NativeCommands.WindowExists(helpWindow.Properties.NativeWindowHandle.Value))
            {
                helpWindow.Close();
                WaitForWindowToClose(helpWindow);
            }
        }
    }

    private void DismissOptionalInformationDialog()
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTime.UtcNow < deadline)
        {
            var dialog = NativeCommands.GetTopLevelWindows(Application.ProcessId)
                .FirstOrDefault(handle => handle != MainWindow.Properties.NativeWindowHandle.Value &&
                                          NativeCommands.HasDialogButton(handle, 1));
            if (dialog != 0)
            {
                NativeCommands.ClickDialogButton(dialog, 1); // IDOK
                return;
            }
            Thread.Sleep(100);
        }
    }

    private static void RequireDeployedMainHelp()
    {
        var executableDirectory = Path.GetDirectoryName(UiTestSettings.ExecutablePath)!;
        var helpDirectory = Path.Combine(executableDirectory, "help");
        // Searching cannot be characterized when the selected artifact has no compiled main help archive.
        if (!Directory.Exists(helpDirectory) ||
            !Directory.EnumerateFiles(helpDirectory, "salamand.chm", SearchOption.AllDirectories).Any())
            Assert.Ignore("Deploy help/<language>/salamand.chm beside the tested executable to run Help Search characterization.");
    }
}
