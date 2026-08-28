using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class FileAccessUiTests : FileOperationUiTestBase
{
    private const string ContentToken = "fm-content-token-9f3c1a";

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Content-search and duplicate-delete cases need extra names that are not prefixes of the shared workspace fixtures.
        File.WriteAllText(workspace.SourcePath("content-unique.txt"), $"prefix {ContentToken} suffix");
        Directory.CreateDirectory(workspace.SourcePath(Path.Combine("dup-tree", "left")));
        Directory.CreateDirectory(workspace.SourcePath(Path.Combine("dup-tree", "right")));
        Directory.CreateDirectory(workspace.SourcePath(Path.Combine("dup-tree", "other")));
        File.WriteAllText(workspace.SourcePath(Path.Combine("dup-tree", "left", "dup-payload.txt")), "dup-same-bytes");
        File.WriteAllText(workspace.SourcePath(Path.Combine("dup-tree", "right", "dup-payload.txt")), "dup-same-bytes");
        File.WriteAllText(workspace.SourcePath(Path.Combine("dup-tree", "other", "dup-other.txt")), "dup-other-bytes");
    }

    [Test]
    public void Find_files_searches_subdirectories_from_the_active_panel()
    {
        // Stable control IDs make the search deterministic without coupling the test to translated labels.
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.FindFiles);
        var findDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.FindFirstDescendant(cf => cf.ByAutomationId("2510")) is not null);

        SetComboValue(findDialog, "2505", "find-target.txt");
        SetComboValue(findDialog, "2501", Workspace.SourceDirectory);
        SetCheckBox(findDialog, "2503", expected: true);
        SetCheckBox(findDialog, "2508", expected: false);

        var findNow = findDialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(findNow, Is.Not.Null, "Find dialog did not expose its Find Now button.");
        findNow!.Invoke();

        var results = findDialog.FindFirstDescendant(cf => cf.ByAutomationId("2510"));
        Assert.That(results, Is.Not.Null, "Find dialog did not expose its results list.");
        // The exact mask makes a single virtual-list row sufficient to prove the nested target was found.
        WaitForFileSystem(() => NativeCommands.GetListViewItemCount(results!.Properties.NativeWindowHandle.Value) == 1,
                          "Find did not return the unique nested file.");
        findDialog.Close();
        WaitForWindowToClose(findDialog);
    }

    [Test]
    public void View_file_opens_the_selected_file_in_the_internal_viewer()
    {
        // The viewer window class is a native invariant independent of its localized caption suffix.
        SelectSourceItem("view-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ViewFile);

        var viewer = WaitForWindow(window =>
            string.Equals(window.Properties.ClassName.ValueOrDefault, "Salamander's Viewer Window", StringComparison.Ordinal));
        Assert.That(viewer.Name, Does.Contain("view-file.txt").IgnoreCase,
                    "Internal viewer caption did not identify the selected file.");
        viewer.Close();
        WaitForWindowToClose(viewer);
    }

    [Test]
    public void Edit_file_opens_the_selected_file_in_the_configured_editor()
    {
        // Point the disposable profile at the harness-owned editor before the
        // dispatch. The seeded product default is notepad.exe, and Windows 11's
        // packaged Notepad opens files as tabs in one shared window, so the
        // original assertion could inspect - and then close - a window holding
        // the current user's unsaved documents. The change goes through the real
        // Configuration page because a committed profile is checksum-protected.
        var (command, arguments, initialDirectory) = SandboxEditor.ProfileEntry();
        var configuration = OpenConfigurationDialog();
        Assert.That(ConfigurationDialogPages.RewriteSelectedEditor(
                        configuration.Properties.NativeWindowHandle.Value,
                        SandboxEditor.SeededCommand, command, arguments, initialDirectory),
                    Is.True, "The Configuration dialog did not accept the sandbox editor entry.");
        CloseConfigurationDialog(configuration, commit: true);
        SandboxEditor.WaitForProfileEntry(command);
        RestartFileManager();

        // The editor is out of process, so the opened file name in its window proves the edit dispatch reached it.
        SelectSourceItem("edit-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.EditFile);

        var expectedTitle = SandboxEditor.WindowTitlePrefix + "edit-file.txt";
        var editor = WaitForDesktopWindow(window =>
                window.Properties.ProcessId.ValueOrDefault != Application.ProcessId &&
                string.Equals(window.Properties.Name.ValueOrDefault, expectedTitle, StringComparison.Ordinal),
            $"Configured editor did not open the selected file in a window titled '{expectedTitle}'.");
        // Only a window this lane owns is ever closed: the title is exact and the
        // stub creates one process and one window per Edit dispatch.
        editor.Close();
        WaitForWindowToClose(editor);
    }

    [Test]
    public void Copy_then_view_the_destination_in_the_other_panel()
    {
        // View already covers a source fixture; this chain proves the inactive panel can open the copy.
        ExecuteWithPath(NativeCommands.CopyFiles, "view-file.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("view-file.txt"),
            "Copy did not release view-file.txt on the target panel.");

        ActivateTargetPanel();
        SelectTargetItem("view-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ViewFile);

        var viewer = WaitForWindow(window =>
            string.Equals(window.Properties.ClassName.ValueOrDefault, "Salamander's Viewer Window", StringComparison.Ordinal));
        Assert.That(viewer.Name, Does.Contain("view-file.txt").IgnoreCase,
                    "Internal viewer caption did not identify the copied file.");
        viewer.Close();
        WaitForWindowToClose(viewer);
        Assert.Multiple(() =>
        {
            Assert.That(MainWindow.IsEnabled, Is.True, "Closing the viewer did not return a usable main window.");
            Assert.That(File.Exists(Workspace.SourcePath("view-file.txt")), Is.True);
        });
    }

    [Test]
    public void Copy_then_edit_the_destination_with_the_sandbox_editor()
    {
        // Same Configuration rewrite as Edit_file: Windows 11 Notepad is unsafe as an assertion target.
        var (command, arguments, initialDirectory) = SandboxEditor.ProfileEntry();
        var configuration = OpenConfigurationDialog();
        Assert.That(ConfigurationDialogPages.RewriteSelectedEditor(
                        configuration.Properties.NativeWindowHandle.Value,
                        SandboxEditor.SeededCommand, command, arguments, initialDirectory),
                    Is.True, "The Configuration dialog did not accept the sandbox editor entry.");
        CloseConfigurationDialog(configuration, commit: true);
        SandboxEditor.WaitForProfileEntry(command);
        RestartFileManager();

        ExecuteWithPath(NativeCommands.CopyFiles, "edit-file.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("edit-file.txt"),
            "Copy did not release edit-file.txt on the target panel.");

        ActivateTargetPanel();
        SelectTargetItem("edit-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.EditFile);

        var expectedTitle = SandboxEditor.WindowTitlePrefix + "edit-file.txt";
        var editor = WaitForDesktopWindow(window =>
                window.Properties.ProcessId.ValueOrDefault != Application.ProcessId &&
                string.Equals(window.Properties.Name.ValueOrDefault, expectedTitle, StringComparison.Ordinal),
            $"Configured editor did not open the copied file in a window titled '{expectedTitle}'.");
        editor.Close();
        WaitForWindowToClose(editor);
    }

    [Test]
    public void Find_result_focus_then_copy_places_the_file_on_the_target()
    {
        // Find already asserts one results row; this continues with Focus so Copy uses the panel path, not the list.
        var findDialog = OpenFindDialog();
        var findHandle = findDialog.Properties.NativeWindowHandle.Value;
        SetComboValue(findDialog, NativeCommands.FindNamed.ToString(), "find-target.txt");
        SetComboValue(findDialog, NativeCommands.FindLookIn.ToString(), Workspace.SourceDirectory);
        SetCheckBox(findDialog, NativeCommands.FindIncludeSubdirs.ToString(), expected: true);
        SetCheckBox(findDialog, NativeCommands.FindGrep.ToString(), expected: false);
        InvokeFindNow(findDialog);
        var results = findDialog.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString()));
        Assert.That(results, Is.Not.Null, "Find dialog did not expose its results list.");
        WaitForFileSystem(() => NativeCommands.GetListViewItemCount(results!.Properties.NativeWindowHandle.Value) == 1,
                          "Find did not return the unique nested file.");

        NativeCommands.SelectFocusedListViewItem(results!.Properties.NativeWindowHandle.Value);
        NativeCommands.Execute(findHandle, NativeCommands.FindFocus);
        WaitForMainWindowTitleContaining("find-tree",
            "Find Focus did not change the active panel to the directory that contains find-target.txt.");

        // Leave Find open: WM_CLOSE after Focus corrupts the debug heap and raises a CRT assertion instead of the Copy dialog.
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("find-target.txt"),
            "Copy after Find Focus did not release the destination.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("find-target.txt")), Is.EqualTo("find-target-content"));
    }

    [Test]
    public void Find_content_search_returns_the_unique_token_and_zero_for_a_miss()
    {
        var findDialog = OpenFindDialog();
        SetComboValue(findDialog, NativeCommands.FindNamed.ToString(), "*.txt");
        SetComboValue(findDialog, NativeCommands.FindLookIn.ToString(), Workspace.SourceDirectory);
        SetCheckBox(findDialog, NativeCommands.FindIncludeSubdirs.ToString(), expected: true);
        SetCheckBox(findDialog, NativeCommands.FindGrep.ToString(), expected: true);
        SetCheckBox(findDialog, NativeCommands.FindHex.ToString(), expected: false);
        SetCheckBox(findDialog, NativeCommands.FindCaseSensitive.ToString(), expected: false);
        SetCheckBox(findDialog, NativeCommands.FindWholeWords.ToString(), expected: false);
        SetCheckBox(findDialog, NativeCommands.FindRegular.ToString(), expected: false);
        SetComboValue(findDialog, NativeCommands.FindContaining.ToString(), ContentToken);
        InvokeFindNow(findDialog);
        var results = findDialog.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString()));
        Assert.That(results, Is.Not.Null, "Find dialog did not expose its results list.");
        WaitForFileSystem(() => NativeCommands.GetListViewItemCount(results!.Properties.NativeWindowHandle.Value) == 1,
                          "Content search did not return the unique token.");

        SetComboValue(findDialog, NativeCommands.FindContaining.ToString(), "fm-content-absent-zzzz");
        InvokeFindNow(findDialog);
        WaitForFileSystem(() => NativeCommands.GetListViewItemCount(results!.Properties.NativeWindowHandle.Value) == 0,
                          "Content search for a missing token did not return zero results.");
        Thread.Sleep(1000);
        Assert.That(NativeCommands.GetListViewItemCount(results!.Properties.NativeWindowHandle.Value), Is.EqualTo(0),
                    "Content search later reported a hit for the missing token.");
        CloseModelessDialog(findDialog);
    }

    [Test]
    public void Find_duplicates_then_delete_one_copy_leaves_the_other()
    {
        var findDialog = OpenFindDialog();
        SetComboValue(findDialog, NativeCommands.FindNamed.ToString(), "dup-payload.txt");
        SetComboValue(findDialog, NativeCommands.FindLookIn.ToString(), Workspace.SourcePath("dup-tree"));
        SetCheckBox(findDialog, NativeCommands.FindIncludeSubdirs.ToString(), expected: true);
        SetCheckBox(findDialog, NativeCommands.FindGrep.ToString(), expected: false);

        var findHandle = findDialog.Properties.NativeWindowHandle.Value;
        var results = findDialog.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString()));
        Assert.That(results, Is.Not.Null, "Find dialog did not expose its results list.");
        var resultsHandle = results!.Properties.NativeWindowHandle.Value;

        NativeCommands.Execute(findHandle, NativeCommands.FindDuplicates);
        var dupDialog = WaitForDialogWithControl(NativeCommands.DuplicateSameName);
        var dupHandle = dupDialog.Properties.NativeWindowHandle.Value;
        NativeCommands.SetDialogCheckBoxState(dupHandle, NativeCommands.DuplicateSameName, isChecked: true);
        NativeCommands.SetDialogCheckBoxState(dupHandle, NativeCommands.DuplicateSameSize, isChecked: true);
        NativeCommands.SetDialogCheckBoxState(dupHandle, NativeCommands.DuplicateSameContent, isChecked: true);
        // Post IDOK: SendMessage would nest inside StartSearch and can deadlock the Find UI thread.
        NativeCommands.PostDialogButtonClick(dupHandle, 1);
        WaitUntilNativeWindowClosed(dupHandle);

        WaitForFileSystem(() => NativeCommands.GetListViewItemCount(resultsHandle) >= 2,
                          "Find Duplicates did not list both identical copies.");

        NativeCommands.SelectFocusedListViewItem(resultsHandle);
        NativeCommands.Execute(findHandle, NativeCommands.FindFocus);
        WaitForFileSystem(
            () =>
            {
                var title = NativeCommands.GetWindowTitle(NativeMainWindowHandle);
                return title.Contains("left", StringComparison.OrdinalIgnoreCase) ||
                       title.Contains("right", StringComparison.OrdinalIgnoreCase);
            },
            "Find Focus did not open a directory that contains a duplicate.");

        // Same debug-heap constraint as Copy-after-Focus: delete from the panel while Find is still open.
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        WaitForFileSystem(() => Directory.GetFiles(Workspace.SourcePath("dup-tree"), "dup-payload.txt",
                                                   SearchOption.AllDirectories).Length == 1,
                          "Deleting one duplicate did not leave exactly one copy on disk.");
        Assert.That(File.Exists(Workspace.SourcePath(Path.Combine("dup-tree", "other", "dup-other.txt"))), Is.True,
                    "Find Duplicates delete removed a unique file.");
    }

    private Window OpenFindDialog()
    {
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.FindFiles);
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString())) is not null);
    }

    private static void InvokeFindNow(Window findDialog)
    {
        var findNow = findDialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(findNow, Is.Not.Null, "Find dialog did not expose its Find Now button.");
        findNow!.Invoke();
    }

    private static void SetComboValue(Window dialog, string automationId, string value)
    {
        // Find fields are native combo boxes even when their histories are empty.
        var combo = dialog.FindFirstDescendant(cf => cf.ByAutomationId(automationId))?.AsComboBox();
        Assert.That(combo, Is.Not.Null, $"Find dialog did not expose combo box {automationId}.");
        combo!.Value = value;
    }

    private static void SetCheckBox(Window dialog, string automationId, bool expected)
    {
        // Persisted Find options are normalized so prior cases cannot change search semantics.
        var checkBox = dialog.FindFirstDescendant(cf => cf.ByAutomationId(automationId))?.AsCheckBox();
        Assert.That(checkBox, Is.Not.Null, $"Find dialog did not expose check box {automationId}.");
        if (checkBox!.IsChecked != expected)
            checkBox.Toggle();
    }
}
