using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class PanelNavigationUiTests : FileOperationUiTestBase
{
    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Target-only name proves Copy after swap and after Change Directory uses the new current path.
        File.WriteAllText(workspace.TargetPath("swap-return.txt"), "swap-return-content");
    }

    [Test]
    public void Enter_subdirectory_copy_then_parent_keeps_the_listing_usable()
    {
        // Panels start on workspace roots; payload.txt lives under copy-tree\nested, not in copy-tree itself.
        SelectSourceItem("copy-tree");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("copy-tree",
            "Enter on copy-tree did not navigate the active panel into that subdirectory.");
        SelectSourceItem("nested");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("nested",
            "Enter on nested did not open the directory that contains payload.txt.");

        ExecuteWithPath(NativeCommands.CopyFiles, "payload.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("payload.txt"),
            "Copy from inside copy-tree did not release the destination file.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("payload.txt")), Is.EqualTo("copy-tree-content"));

        GoToParentDirectory();
        WaitForMainWindowTitleContaining("copy-tree",
            "Parent directory did not return from nested to copy-tree.");
        GoToParentDirectory();
        WaitForMainWindowTitleContaining("source",
            "Parent directory did not return the active panel to the workspace source path.");
        SelectSourceItem("copy-tree");
        Assert.That(File.Exists(Workspace.SourcePath(Path.Combine("copy-tree", "nested", "payload.txt"))), Is.True);
    }

    [Test]
    public void Swap_panels_then_copy_writes_in_the_opposite_direction()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("copy-file.txt"),
            "The first copy did not release the destination file.");

        SwapPanels();
        // Swap exchanges the panel windows without changing which CFilesWindow is active; the former
        // target is now the visual left listing and must be activated before Copy runs the other way.
        ActivateSourcePanel();
        WaitForMainWindowTitleContaining("target",
            "Swap Panels did not move the former target path onto the active left panel.");

        ExecuteWithPath(NativeCommands.CopyFiles, "swap-return.txt", Workspace.SourceDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.SourcePath("swap-return.txt"),
            "Copy after swap did not release the destination file.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("copy-file.txt")), Is.EqualTo("copy-file-content"));
            Assert.That(File.ReadAllText(Workspace.SourcePath("swap-return.txt")), Is.EqualTo("swap-return-content"));
            Assert.That(File.Exists(Workspace.TargetPath("swap-return.txt")), Is.True,
                        "Copy after swap unexpectedly removed the original target file.");
        });
    }

    [Test]
    public void Refresh_lists_a_file_created_after_startup_so_it_can_be_copied()
    {
        var appeared = Workspace.SourcePath("refresh-appeared.txt");
        File.WriteAllText(appeared, "refresh-appeared-content");

        RefreshSourcePanel();
        ExecuteWithPath(NativeCommands.CopyFiles, "refresh-appeared.txt", Workspace.TargetDirectory, commit: true);

        WaitForOperationOutputToBeReleased(Workspace.TargetPath("refresh-appeared.txt"),
            "Copy after Refresh did not release the destination file.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("refresh-appeared.txt")), Is.EqualTo("refresh-appeared-content"));
        Assert.That(File.Exists(appeared), Is.True, "Copy after Refresh unexpectedly removed the source file.");
    }

    [Test]
    public void Change_directory_to_the_sandbox_target_then_copy_from_that_path()
    {
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ChangeDirectory);
        var dialog = WaitForOperationDialog(NativeCommands.ChangeDirectory);
        SetDialogPath(dialog, Workspace.TargetDirectory);
        CloseDialog(dialog, commit: true);
        WaitForMainWindowTitleContaining("target",
            "Change Directory did not move the active panel onto the sandbox target folder.");

        ExecuteWithPath(NativeCommands.CopyFiles, "swap-return.txt", Workspace.SourceDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.SourcePath("swap-return.txt"),
            "Copy after Change Directory did not release the destination file.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("swap-return.txt")), Is.EqualTo("swap-return-content"));
    }
}
