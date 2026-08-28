using System.IO.Compression;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class ArchiveOperationUiTests : FileOperationUiTestBase
{
    private const string ZipName = "zip-member-copy.zip";
    private const string ZipPayloadName = "zip-member-payload.txt";
    private const string ZipPayloadContent = "zip-characterization-content";

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        File.WriteAllText(workspace.SourcePath("pack-one.txt"), "pack-one-content");
        File.WriteAllText(workspace.SourcePath("pack-two.txt"), "pack-two-content");
        using var archive = ZipFile.Open(workspace.SourcePath(ZipName), ZipArchiveMode.Create);
        var entry = archive.CreateEntry(ZipPayloadName);
        using var writer = new StreamWriter(entry.Open());
        writer.Write(ZipPayloadContent);
    }

    [Test]
    public void Pack_then_unpack_restores_the_selected_members()
    {
        // ZIP Open already navigates a pre-seeded archive; Pack/Unpack are the Files menu commands that create one.
        UiTestSettings.RequireZipPlugin();

        SelectSourceItems("pack-one.txt", "pack-two.txt");
        WaitForCommandEnabled(NativeCommands.Pack);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.Pack);
        var packDialog = WaitForOperationDialog();
        SetDialogPath(packDialog, Path.Combine(Workspace.TargetDirectory, "session.zip"));
        NativeCommands.SetDialogCheckBoxState(packDialog.Properties.NativeWindowHandle.Value,
                                              NativeCommands.PackMoveFiles, isChecked: false);
        try
        {
            NativeCommands.SelectComboBoxItemContaining(packDialog.Properties.NativeWindowHandle.Value,
                                                        NativeCommands.PackerCombo, "ZIP (Plugin)");
        }
        catch (InvalidOperationException)
        {
            Assert.Ignore("The Pack dialog did not list the Zip plug-in packer.");
        }

        CloseDialog(packDialog, commit: true);
        DismissOptionalOkDialog();
        var archivePath = Workspace.TargetPath("session.zip");
        WaitForOperationOutputToBeReleased(archivePath, "Pack did not release session.zip.");

        ActivateTargetPanel();
        SelectTargetItem("session.zip");
        WaitForCommandEnabled(NativeCommands.Unpack);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.Unpack);
        var unpackDialog = WaitForOperationDialog();
        var unpackRoot = Workspace.SourcePath("unpacked-session");
        SetDialogPath(unpackDialog, unpackRoot);
        NativeCommands.SetDialogCheckBoxState(unpackDialog.Properties.NativeWindowHandle.Value,
                                              NativeCommands.UnpackDeleteArchive, isChecked: false);
        CloseDialog(unpackDialog, commit: true);
        DismissOptionalOkDialog();

        WaitForOperationOutputToBeReleased(Path.Combine(unpackRoot, "pack-one.txt"),
            "Unpack did not release pack-one.txt.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Path.Combine(unpackRoot, "pack-one.txt")), Is.EqualTo("pack-one-content"));
            Assert.That(File.ReadAllText(Path.Combine(unpackRoot, "pack-two.txt")), Is.EqualTo("pack-two-content"));
            Assert.That(File.Exists(archivePath), Is.True, "Unpack unexpectedly deleted the archive.");
        });
    }

    [Test]
    public void Zip_copy_member_to_disk_leaves_the_archive_intact()
    {
        UiTestSettings.RequireZipPlugin();

        SelectSourceItem(ZipName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.OpenFile);
        DismissOptionalOkDialog();
        WaitForMainWindowTitleContaining(ZipName,
            "Open did not navigate into the seeded archive.");

        ExecuteWithPath(NativeCommands.CopyFiles, ZipPayloadName, Workspace.TargetDirectory, commit: true);
        var extracted = Workspace.TargetPath(ZipPayloadName);
        WaitForOperationOutputToBeReleased(extracted, "Copy from the archive did not release the destination file.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(extracted), Is.EqualTo(ZipPayloadContent));
            Assert.That(File.Exists(Workspace.SourcePath(ZipName)), Is.True,
                        "Copying a ZIP member out removed the archive from disk.");
        });
    }

    [Test]
    public void Unpack_then_delete_the_archive_keeps_the_extracted_files()
    {
        UiTestSettings.RequireZipPlugin();

        SelectSourceItem(ZipName);
        WaitForCommandEnabled(NativeCommands.Unpack);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.Unpack);
        var unpackDialog = WaitForOperationDialog();
        var unpackRoot = Workspace.TargetPath("unpacked-zip");
        SetDialogPath(unpackDialog, unpackRoot);
        NativeCommands.SetDialogCheckBoxState(unpackDialog.Properties.NativeWindowHandle.Value,
                                              NativeCommands.UnpackDeleteArchive, isChecked: false);
        CloseDialog(unpackDialog, commit: true);
        DismissOptionalOkDialog();
        WaitForOperationOutputToBeReleased(Path.Combine(unpackRoot, ZipPayloadName),
            "Unpack did not release the extracted payload.");

        SelectSourceItem(ZipName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath(ZipName)),
                          "Delete did not remove the archive after unpacking.");
        Assert.That(File.ReadAllText(Path.Combine(unpackRoot, ZipPayloadName)), Is.EqualTo(ZipPayloadContent));
    }
}
