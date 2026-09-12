using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class PanelHistoryUiTests : FileOperationUiTestBase
{
    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Each unique payload makes the navigation assertion exercise the folder the panel actually opened.
        Directory.CreateDirectory(workspace.SourcePath("history-a"));
        Directory.CreateDirectory(workspace.SourcePath("history-b"));
        Directory.CreateDirectory(workspace.TargetPath("other-panel"));
        File.WriteAllText(workspace.SourcePath("history-a\\a.txt"), "history-a-content");
        File.WriteAllText(workspace.SourcePath("history-b\\b.txt"), "history-b-content");
        File.WriteAllText(workspace.TargetPath("other-panel\\right-only.txt"), "right-panel-content");
    }

    [Test]
    public void PanelHistory_and_other_panel_path_commands_navigate_to_the_expected_owned_folder()
    {
        // Change Directory creates the adjacent history transition needed to exercise Back and Forward.
        SelectSourceItem("history-a");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("history-a", "Opening history-a did not navigate the active panel.");
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeDirectory);
        var changeToHistoryB = WaitForOperationDialog(NativeCommands.ChangeDirectory);
        SetDialogPath(changeToHistoryB, Workspace.SourcePath("history-b"));
        CloseDialog(changeToHistoryB, commit: true);
        WaitForMainWindowTitleContaining("history-b", "Opening history-b did not navigate the active panel.");

        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.ActiveBack);
        WaitForMainWindowTitleContaining("history-a", "Back did not return the active panel to history-a.");
        ExecuteWithPath(NativeCommands.CopyFiles, "a.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("a.txt"),
            "Copy after Back did not use the history-a folder.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("a.txt")), Is.EqualTo("history-a-content"));

        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.ActiveForward);
        WaitForMainWindowTitleContaining("history-b", "Forward did not return the active panel to history-b.");
        ExecuteWithPath(NativeCommands.CopyFiles, "b.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("b.txt"),
            "Copy after Forward did not use the history-b folder.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("b.txt")), Is.EqualTo("history-b-content"));

        // Put the right panel on a distinct owned path before synchronizing the active left panel from it.
        ActivateTargetPanel();
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeDirectory);
        var changeDirectory = WaitForOperationDialog(NativeCommands.ChangeDirectory);
        SetDialogPath(changeDirectory, Workspace.TargetPath("other-panel"));
        CloseDialog(changeDirectory, commit: true);
        WaitForMainWindowTitleContaining("other-panel", "Change Directory did not move the right panel to its owned path.");

        ActivateSourcePanel();
        WaitForMainWindowTitleContaining("history-b", "Activating the left panel unexpectedly changed its history-b path.");
        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.ActiveAsOtherPanel);
        WaitForMainWindowTitleContaining("other-panel", "Go to Path from Other Panel did not synchronize the active left panel.");

        // Revisiting the right panel proves synchronization changed only the requested left panel.
        ActivateTargetPanel();
        WaitForMainWindowTitleContaining("other-panel", "Go to Path from Other Panel changed the right panel's owned path.");
        ActivateSourcePanel();
        ExecuteWithPath(NativeCommands.CopyFiles, "right-only.txt", Workspace.SourceDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.SourcePath("right-only.txt"),
            "Copy after path synchronization did not use the other panel's owned folder.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("right-only.txt")), Is.EqualTo("right-panel-content"));
    }
}
