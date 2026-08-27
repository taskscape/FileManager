using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class LargeFlatDirectoryDeleteUiTests : FileOperationUiTestBase
{
    private const string DirectoryName = "delete-large-flat-directory";
    private const int FileCount = 2_048;

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        var directory = workspace.SourcePath(DirectoryName);
        Directory.CreateDirectory(directory);
        for (var index = 0; index < FileCount; index++)
        {
            // A four-figure flat directory guards the per-entry delete path
            // without making every ordinary file-operation fixture pay its cost.
            File.WriteAllText(Path.Combine(directory, $"entry-{index:D5}.txt"), "flat-directory-delete-content");
        }
    }

    [Test]
    public void Delete_large_flat_directory_removes_all_descendants()
    {
        var directory = Workspace.SourcePath(DirectoryName);

        SelectSourceItem(DirectoryName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        // Completion must remain observable for a realistic high-entry delete.
        WaitForFileSystem(() => !Directory.Exists(directory),
                          "Delete did not remove the large flat directory.");
    }
}
