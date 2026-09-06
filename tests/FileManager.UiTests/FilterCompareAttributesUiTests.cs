using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class FilterCompareAttributesUiTests : FileOperationUiTestBase
{
    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        Write(workspace, source: true, Path.Combine("filter-tree", "filter-a.txt"), "filter-a-content");
        Write(workspace, source: true, Path.Combine("filter-tree", "filter-b.txt"), "filter-b-content");
        Write(workspace, source: true, Path.Combine("filter-tree", "filter-c.dat"), "filter-c-content");
        Write(workspace, source: true, Path.Combine("mask-tree", "nested", "mask-keep.txt"), "mask-keep-content");
        Write(workspace, source: true, Path.Combine("mask-tree", "nested", "mask-skip.dat"), "mask-skip-content");
        // Asymmetric names let Compare Directories select left-only and content-differing files without relying on time.
        Write(workspace, source: true, Path.Combine("cmp-left", "cmp-left-only.txt"), "cmp-left-only-content");
        Write(workspace, source: true, Path.Combine("cmp-left", "cmp-differ.txt"), "cmp-differ-left-content");
        Write(workspace, source: false, Path.Combine("cmp-right", "cmp-right-only.txt"), "cmp-right-only-content");
        Write(workspace, source: false, Path.Combine("cmp-right", "cmp-differ.txt"), "cmp-differ-right-content");
        Write(workspace, source: true, "attr-file.txt", "attr-file-content");
        Write(workspace, source: true, "ntfs-press.txt", "ntfs-press-content");
        Write(workspace, source: true, "MixedCase.txt", "mixed-case-content");
        Directory.CreateDirectory(Path.GetDirectoryName(workspace.SourcePath("convert-lf.txt"))!);
        File.WriteAllText(workspace.SourcePath("convert-lf.txt"), "line1\nline2");
    }

    [Test]
    public void Filter_then_select_all_copies_only_the_visible_names()
    {
        // Filter hides names from the listing; Select All must not copy the omitted .dat file.
        SelectSourceItem("filter-tree");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("filter-tree",
            "Enter did not open the filter-tree fixture folder.");

        ApplyPanelFilter("*.txt");
        NativeCommands.SelectAllActivePanel(MainWindow.Properties.NativeWindowHandle.Value);
        Thread.Sleep(750);
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);

        WaitForOperationOutputToBeReleased(Workspace.TargetPath("filter-a.txt"),
            "Filtered copy did not release filter-a.txt.");
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("filter-b.txt"),
            "Filtered copy did not release filter-b.txt.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("filter-a.txt")), Is.EqualTo("filter-a-content"));
            Assert.That(File.ReadAllText(Workspace.TargetPath("filter-b.txt")), Is.EqualTo("filter-b-content"));
            Assert.That(File.Exists(Workspace.TargetPath("filter-c.dat")), Is.False,
                        "Filtered copy transferred the hidden .dat file.");
        });

        ClearPanelFilter();
        Assert.That(File.Exists(Workspace.SourcePath(Path.Combine("filter-tree", "filter-c.dat"))), Is.True);
    }

    [Test]
    public void Compare_directories_then_copy_the_left_only_and_differing_files()
    {
        SelectSourceItem("cmp-left");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("cmp-left",
            "Enter did not open the left compare fixture folder.");
        ActivateTargetPanel();
        SelectTargetItem("cmp-right");
        OpenFocusedItem();
        WaitForMainWindowTitleContaining("cmp-right",
            "Enter did not open the right compare fixture folder.");
        NativeCommands.ActivateFilePanel(FindPanelList(left: true).Properties.NativeWindowHandle.Value);
        Thread.Sleep(750);

        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.CompareDirectories);
        var dialog = WaitForDialogWithControl(NativeCommands.CompareByContent);
        var handle = dialog.Properties.NativeWindowHandle.Value;
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CompareByTime, isChecked: false);
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CompareBySize, isChecked: false);
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CompareByAttr, isChecked: false);
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CompareSubdirs, isChecked: false);
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CompareByContent, isChecked: true);
        NativeCommands.PostDialogButtonClick(handle, 1);
        WaitForWindowToClose(dialog);
        WaitUntilNoWindowTitled("Comparing Directories",
            "Compare Directories did not finish before Copy was dispatched.");

        var compareTarget = Workspace.TargetPath("cmp-right");
        WaitForCommandEnabled(NativeCommands.CopyFiles);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.CopyFiles);
        var copyDialog = WaitForOperationDialog(NativeCommands.CopyFiles);
        SetDialogPath(copyDialog, compareTarget);
        NativeCommands.PostDialogButtonClick(copyDialog.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(copyDialog);

        var leftOnly = Path.Combine(compareTarget, "cmp-left-only.txt");
        var differ = Path.Combine(compareTarget, "cmp-differ.txt");
        // cmp-differ.txt already exists on the target; wait for overwrite content, not merely a released handle.
        WaitForFileSystemAnsweringYes(
            () =>
            {
                try
                {
                    using var leftStream = new FileStream(leftOnly, FileMode.Open, FileAccess.Read, FileShare.None);
                    using var differStream = new FileStream(differ, FileMode.Open, FileAccess.Read, FileShare.None);
                    using var differReader = new StreamReader(differStream);
                    return differReader.ReadToEnd() == "cmp-differ-left-content";
                }
                catch (Exception exception) when (exception is FileNotFoundException or IOException or UnauthorizedAccessException)
                {
                    return false;
                }
            },
            "Copy of Compare Directories selection did not copy left-only and differing files.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(leftOnly), Is.EqualTo("cmp-left-only-content"));
            Assert.That(File.ReadAllText(differ), Is.EqualTo("cmp-differ-left-content"));
            Assert.That(File.Exists(Path.Combine(compareTarget, "cmp-right-only.txt")), Is.True);
        });
    }

    [Test]
    public void Change_attributes_then_copy_preserves_read_only()
    {
        // BM_CLICK is required so the 3-state Read-only box records its dirty flag before OK.
        SelectSourceItem("attr-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ChangeAttributes);
        var dialog = WaitForDialogWithControl(NativeCommands.ReadOnlyAttribute);
        NativeCommands.ClickDialogControl(dialog.Properties.NativeWindowHandle.Value, NativeCommands.ReadOnlyAttribute);
        CloseDialog(dialog, commit: true);

        WaitForFileSystem(() => (File.GetAttributes(Workspace.SourcePath("attr-file.txt")) & FileAttributes.ReadOnly) != 0,
                          "Change Attributes did not set the Read-only flag on the source.");

        ExecuteWithPath(NativeCommands.CopyFiles, "attr-file.txt", Workspace.TargetDirectory, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("attr-file.txt"),
            "Copy after Change Attributes did not release the destination.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("attr-file.txt")), Is.EqualTo("attr-file-content"));
            Assert.That(File.GetAttributes(Workspace.SourcePath("attr-file.txt")) & FileAttributes.ReadOnly,
                        Is.EqualTo(FileAttributes.ReadOnly));
            Assert.That(File.GetAttributes(Workspace.TargetPath("attr-file.txt")) & FileAttributes.ReadOnly,
                        Is.EqualTo(FileAttributes.ReadOnly),
                        "Copy did not preserve the Read-only attribute set through Change Attributes.");
        });
    }

    [Test]
    public void Change_case_uppercases_the_selected_name_without_rewriting_content()
    {
        SelectSourceItem("MixedCase.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ChangeCase);
        var dialog = WaitForDialogWithControl(NativeCommands.UpperCaseRadio);
        NativeCommands.ClickDialogControl(dialog.Properties.NativeWindowHandle.Value, NativeCommands.UpperCaseRadio);
        CloseDialog(dialog, commit: true);

        WaitForFileSystem(() => Directory.EnumerateFiles(Workspace.SourceDirectory)
                                      .Any(path => Path.GetFileName(path) == "MIXEDCASE.TXT"),
                          "Change Case did not persist the upper-case directory entry.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("MIXEDCASE.TXT")), Is.EqualTo("mixed-case-content"));
    }

    [Test]
    public void Convert_changes_lf_endings_to_crlf()
    {
        SelectSourceItem("convert-lf.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.ConvertFiles);
        var dialog = WaitForDialogWithControl(NativeCommands.EofCrlfRadio);
        NativeCommands.ClickDialogControl(dialog.Properties.NativeWindowHandle.Value, NativeCommands.EofCrlfRadio);
        CloseDialog(dialog, commit: true);

        var path = Workspace.SourcePath("convert-lf.txt");
        WaitForOperationOutputToBeReleased(path, "Convert did not release convert-lf.txt.");
        Assert.That(File.ReadAllText(path).Replace("\r\n", "\n"), Is.EqualTo("line1\nline2"));
        Assert.That(File.ReadAllBytes(path), Does.Contain((byte)'\r'));
    }

    [Test]
    public void Copy_with_named_mask_copies_text_files_only()
    {
        // Criteria controls exist while Options is collapsed; Transfer still reads IDC_CM_NAMED on OK.
        SelectSourceItem("mask-tree");
        WaitForCommandEnabled(NativeCommands.CopyFiles);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.CopyFiles);
        var dialog = WaitForOperationDialog(NativeCommands.CopyFiles);
        SetDialogPath(dialog, Workspace.TargetDirectory);
        SetCopyNamedMask(dialog, "*.txt");
        CloseDialog(dialog, commit: true);

        var kept = Workspace.TargetPath(Path.Combine("mask-tree", "nested", "mask-keep.txt"));
        WaitForOperationOutputToBeReleased(kept, "Masked copy did not release mask-keep.txt.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(kept), Is.EqualTo("mask-keep-content"));
            Assert.That(File.Exists(Workspace.TargetPath(Path.Combine("mask-tree", "nested", "mask-skip.dat"))),
                        Is.False, "Copy with *.txt created the excluded .dat member.");
        });
    }

    [Test]
    public void Ntfs_compress_then_uncompress_toggles_the_compressed_attribute()
    {
        var path = Workspace.SourcePath("ntfs-press.txt");
        var root = Path.GetPathRoot(path);
        if (root is null || !string.Equals(new DriveInfo(root).DriveFormat, "NTFS", StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("NTFS Compress requires an NTFS volume under the sandbox test-data root.");

        SelectSourceItem("ntfs-press.txt");
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeAttributes);
        var compressDialog = WaitForDialogWithControl(NativeCommands.CompressedAttribute);
        if (!NativeCommands.IsDialogControlEnabled(compressDialog.Properties.NativeWindowHandle.Value,
                                                   NativeCommands.CompressedAttribute))
        {
            CloseDialog(compressDialog, commit: false);
            Assert.Ignore("The Change Attributes Compressed box is disabled; the volume does not support file-based compression.");
        }

        // CM_COMPRESS shows a modal CMessageBox then disables the main window for tree analysis; that sequence
        // leaves no answerable top-level prompt in this harness. The Compressed box uses the same worker.
        NativeCommands.ClickAttributeCheckBox(compressDialog.Properties.NativeWindowHandle.Value,
                                             NativeCommands.CompressedAttribute, wantedState: 1);
        CloseDialog(compressDialog, commit: true);
        WaitForFileSystem(() => (File.GetAttributes(path) & FileAttributes.Compressed) != 0,
                          "NTFS Compress did not set the Compressed attribute.");

        SelectSourceItem("ntfs-press.txt");
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeAttributes);
        var uncompressDialog = WaitForDialogWithControl(NativeCommands.CompressedAttribute);
        NativeCommands.ClickAttributeCheckBox(uncompressDialog.Properties.NativeWindowHandle.Value,
                                              NativeCommands.CompressedAttribute, wantedState: 0);
        CloseDialog(uncompressDialog, commit: true);
        WaitForFileSystem(() => (File.GetAttributes(path) & FileAttributes.Compressed) == 0,
                          "NTFS Uncompress did not clear the Compressed attribute.");
    }

    private static void Write(FileOperationWorkspace workspace, bool source, string relativePath, string content)
    {
        var path = source ? workspace.SourcePath(relativePath) : workspace.TargetPath(relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }
}
