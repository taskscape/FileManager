using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class FileAccessUiTests : FileOperationUiTestBase
{
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
        using var editorProcesses = new TestOwnedProcessScope("notepad");
        // The process scope also cleans a headless default editor when the product defect prevents HWND discovery.
        // The default editor is out of process, so the opened file name in its window proves the edit dispatch reached it.
        SelectSourceItem("edit-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.EditFile);

        var editor = WaitForDesktopWindow(window =>
                window.Properties.ProcessId.ValueOrDefault != Application.ProcessId &&
                window.Name.Contains("edit-file.txt", StringComparison.OrdinalIgnoreCase),
            "Configured editor did not open the selected file.");
        editor.Close();
        WaitForWindowToClose(editor);
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
