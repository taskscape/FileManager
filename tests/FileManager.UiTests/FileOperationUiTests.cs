using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class FileOperationUiTests : FileOperationUiTestBase
{
    [Test]
    public void Create_directory_creates_requested_nested_directory()
    {
        ExecuteWithPath(NativeCommands.CreateDirectory, string.Empty, "created\\nested", commit: true);

        WaitForFileSystem(() => Directory.Exists(Workspace.SourcePath("created\\nested")),
                          "Create Directory did not create the requested nested directory.");
    }

    [Test]
    public void Copy_file_copies_content_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.TargetPath("copy-file.txt")), "Copy did not create the destination file.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("copy-file.txt")), Is.EqualTo("copy-file-content"));
        Assert.That(File.Exists(Workspace.SourcePath("copy-file.txt")), Is.True, "Copy unexpectedly removed the source file.");
    }

    [Test]
    public void Copy_file_persists_a_completed_recovery_journal_with_item_intent()
    {
        var source = Workspace.SourcePath("copy-file.txt");
        var target = Workspace.TargetPath("copy-file.txt");

        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => FindJournalFor(source) is not null,
                          "Copy did not persist a durable operation journal.");
        var journal = FindJournalFor(source)!;
        var content = File.ReadAllText(journal);

        Assert.That(content, Does.Contain($"ITEM|").And.Contain("|copy-file|").And.Contain(source).And.Contain(target));
        Assert.That(content, Does.Contain("STATE|").And.Contain("|prepared"));
        Assert.That(content, Does.Contain("|committed"));
        Assert.That(content, Does.Contain("OPERATION|completed"));
    }

    [Test]
    public void Copy_directory_copies_all_descendants_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "copy-tree", Workspace.TargetDirectory, commit: true);

        var copiedPayload = Workspace.TargetPath("copy-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(copiedPayload), "Copy did not create the directory descendant.");
        Assert.That(File.ReadAllText(copiedPayload), Is.EqualTo("copy-tree-content"));
        Assert.That(Directory.Exists(Workspace.SourcePath("copy-tree\\nested")), Is.True);
    }

    [Test]
    public void Rename_file_renames_without_changing_content()
    {
        ExecuteWithPath(NativeCommands.RenameFile, "rename-file.txt", "renamed-file.txt", commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.SourcePath("renamed-file.txt")), "Rename did not create the requested file name.");
        Assert.That(File.Exists(Workspace.SourcePath("rename-file.txt")), Is.False);
        Assert.That(File.ReadAllText(Workspace.SourcePath("renamed-file.txt")), Is.EqualTo("rename-file-content"));
    }

    [Test]
    public void Rename_directory_preserves_all_descendants()
    {
        ExecuteWithPath(NativeCommands.RenameFile, "rename-tree", "renamed-tree", commit: true);

        var payload = Workspace.SourcePath("renamed-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(payload), "Rename did not preserve the directory descendant.");
        Assert.That(Directory.Exists(Workspace.SourcePath("rename-tree")), Is.False);
        Assert.That(File.ReadAllText(payload), Is.EqualTo("rename-tree-content"));
    }

    [Test]
    public void Move_file_moves_content_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.MoveFiles, "move-file.txt", Workspace.TargetDirectory, commit: true);

        var target = Workspace.TargetPath("move-file.txt");
        WaitForFileSystem(() => File.Exists(target), "Move did not create the destination file.");
        Assert.That(File.Exists(Workspace.SourcePath("move-file.txt")), Is.False, "Move retained the source file.");
        Assert.That(File.ReadAllText(target), Is.EqualTo("move-file-content"));
    }

    [Test]
    public void Move_directory_moves_all_descendants_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.MoveFiles, "move-tree", Workspace.TargetDirectory, commit: true);

        var payload = Workspace.TargetPath("move-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(payload), "Move did not create the directory descendant.");
        Assert.That(Directory.Exists(Workspace.SourcePath("move-tree")), Is.False, "Move retained the source directory.");
        Assert.That(File.ReadAllText(payload), Is.EqualTo("move-tree-content"));
    }

    [Test]
    public void Delete_file_removes_the_selected_file()
    {
        SelectSourceItem("delete-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-file.txt")), "Delete did not remove the selected file.");
    }

    [Test]
    public void Delete_directory_removes_all_descendants()
    {
        SelectSourceItem("delete-tree");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !Directory.Exists(Workspace.SourcePath("delete-tree")), "Delete did not remove the selected directory tree.");
    }

    [TestCase(NativeCommands.CreateDirectory, "", "cancelled-directory")]
    [TestCase(NativeCommands.CopyFiles, "cancel-copy.txt", "")]
    [TestCase(NativeCommands.MoveFiles, "cancel-move.txt", "")]
    [TestCase(NativeCommands.RenameFile, "cancel-rename.txt", "cancelled-rename.txt")]
    public void Cancelling_operation_dialog_leaves_source_and_target_unchanged(int command, string sourceName, string enteredPath)
    {
        var path = command is NativeCommands.CopyFiles or NativeCommands.MoveFiles ? Workspace.TargetDirectory : enteredPath;
        ExecuteWithPath(command, sourceName, path, commit: false);

        if (sourceName.Length != 0)
            Assert.That(File.Exists(Workspace.SourcePath(sourceName)), Is.True, "Cancellation changed the source item.");
        if (command == NativeCommands.CreateDirectory)
            Assert.That(Directory.Exists(Workspace.SourcePath(enteredPath)), Is.False);
        if (command is NativeCommands.CopyFiles or NativeCommands.MoveFiles)
            Assert.That(File.Exists(Workspace.TargetPath(sourceName)), Is.False, "Cancellation created a target item.");
        if (command == NativeCommands.RenameFile)
            Assert.That(File.Exists(Workspace.SourcePath(enteredPath)), Is.False, "Cancellation renamed the source item.");
    }

    [Test]
    public void Create_directory_failure_keeps_existing_file_intact()
    {
        SubmitInvalidPathAndCancel(NativeCommands.CreateDirectory, string.Empty, "create-collision");

        Assert.That(File.ReadAllText(Workspace.SourcePath("create-collision")), Is.EqualTo("create-collision-content"));
    }

    [TestCase(NativeCommands.CopyFiles, "copy-file.txt")]
    [TestCase(NativeCommands.MoveFiles, "move-file.txt")]
    public void Copy_or_move_failure_does_not_modify_source(int command, string sourceName)
    {
        SubmitInvalidPathAndCancel(command, sourceName, Workspace.TargetPath("blocked-target\\child.txt"));

        Assert.That(File.Exists(Workspace.SourcePath(sourceName)), Is.True);
        Assert.That(File.Exists(Workspace.TargetPath("blocked-target")), Is.True);
    }

    [Test]
    public void Rename_collision_keeps_the_original_file_and_existing_target()
    {
        SubmitInvalidPathAndCancel(NativeCommands.RenameFile, "rename-file.txt", "rename-collision.txt");

        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-file.txt")), Is.EqualTo("rename-file-content"));
        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-collision.txt")), Is.EqualTo("collision-content"));
    }

    [Test]
    public void Delete_failure_for_locked_file_keeps_the_file()
    {
        using var handle = Workspace.HoldSourceFileOpen("delete-locked.txt");
        SelectSourceItem("delete-locked.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => File.Exists(Workspace.SourcePath("delete-locked.txt")), "A failed delete removed the locked source file.");
    }

    private static string? FindJournalFor(string source)
    {
        var directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                                     "Open Salamander", "operation-journals");
        if (!Directory.Exists(directory))
            return null;

        return Directory.EnumerateFiles(directory, "*.opj")
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault(path => File.ReadAllText(path).Contains(source, StringComparison.Ordinal));
    }
}
