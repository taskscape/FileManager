using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class SessionAndSelectionUiTests : FileOperationUiTestBase
{
    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // Distinct ASCII prefixes keep incremental search from treating one mask fixture as a prefix of another.
        File.WriteAllText(workspace.SourcePath("mask-a.txt"), "mask-a-content");
        File.WriteAllText(workspace.SourcePath("mask-b.txt"), "mask-b-content");
        File.WriteAllText(workspace.SourcePath("mask-c.dat"), "mask-c-content");
    }

    [Test]
    public void Select_by_mask_copies_only_the_matching_names()
    {
        // Product Select dialog, not harness Insert multi-select, is what Files > Select actually drives.
        SelectByMask("mask-*.txt");
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);

        WaitForOperationOutputToBeReleased(Workspace.TargetPath("mask-a.txt"),
            "Select-by-mask copy did not release mask-a.txt.");
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("mask-b.txt"),
            "Select-by-mask copy did not release mask-b.txt.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("mask-a.txt")), Is.EqualTo("mask-a-content"));
            Assert.That(File.ReadAllText(Workspace.TargetPath("mask-b.txt")), Is.EqualTo("mask-b-content"));
            Assert.That(File.Exists(Workspace.TargetPath("mask-c.dat")), Is.False,
                        "Select *.txt unexpectedly copied the non-matching .dat file.");
        });
    }

    [Test]
    public void Unselect_all_leaves_copy_acting_on_the_focused_item_only()
    {
        SelectByMask("mask-*.txt");
        // Empty selection is the product rule that Copy then uses the focused item rather than the previous mask set.
        FocusSourceItem("mask-a.txt");
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);

        WaitForOperationOutputToBeReleased(Workspace.TargetPath("mask-a.txt"),
            "Focused-item copy after Unselect All did not release the destination.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("mask-a.txt")), Is.EqualTo("mask-a-content"));
            Assert.That(File.Exists(Workspace.TargetPath("mask-b.txt")), Is.False,
                        "Copy after Unselect All transferred more than the focused item.");
        });
    }

    [Test]
    public void Create_copy_rename_move_and_delete_complete_in_one_session()
    {
        // One process and workspace: leftover selection, panel path, and idle queue after a real session chain.
        ExecuteWithPath(NativeCommands.CreateDirectory, string.Empty, "created\\session", commit: true);
        AnswerQuestionIfPrompted(1); // IDOK for the intermediate-folder prompt
        WaitForFileSystem(() => Directory.Exists(Workspace.SourcePath("created\\session")),
                          "Create Directory did not create the session folder.");

        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.SourcePath("created\\session"), commit: true);
        WaitForOperationOutputToBeReleased(Workspace.SourcePath("created\\session\\copy-file.txt"),
            "Copy into the created session folder did not release the destination.");

        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ChangeDirectory);
        var changeDir = WaitForOperationDialog(NativeCommands.ChangeDirectory);
        SetDialogPath(changeDir, Workspace.SourcePath("created\\session"));
        CloseDialog(changeDir, commit: true);
        WaitForMainWindowTitleContaining("session",
            "Change Directory did not enter the created session folder.");

        ExecuteWithPath(NativeCommands.RenameFile, "copy-file.txt", "session-renamed.txt", commit: true);
        WaitForFileSystem(() => File.Exists(Workspace.SourcePath("created\\session\\session-renamed.txt")),
                          "Rename did not produce session-renamed.txt inside the session folder.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("created\\session\\session-renamed.txt")),
                    Is.EqualTo("copy-file-content"));

        GoToParentDirectory();
        WaitForMainWindowTitleContaining("created",
            "Parent directory did not return to the created folder.");
        ExecuteWithPath(NativeCommands.MoveFiles, "session", Workspace.TargetDirectory, commit: true);
        WaitForFileSystem(() => Directory.Exists(Workspace.TargetPath("session")) &&
                                !Directory.Exists(Workspace.SourcePath("created\\session")),
                          "Move did not relocate the session folder to the other panel.");
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("session\\session-renamed.txt"),
            "Move did not release the relocated session file.");

        ActivateTargetPanel();
        SelectTargetItem("session");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        WaitForFileSystem(() => !Directory.Exists(Workspace.TargetPath("session")),
                          "Delete did not remove the moved session folder.");

        Assert.That(MainWindow.IsEnabled, Is.True, "The main window was not usable after the session command chain.");
        Assert.That(File.Exists(Workspace.SourcePath("copy-file.txt")), Is.True,
                    "The session copy unexpectedly removed the original source file.");
    }
}
