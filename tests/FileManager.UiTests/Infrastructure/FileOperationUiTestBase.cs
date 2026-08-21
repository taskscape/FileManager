using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

[NonParallelizable]
// Every derived fixture needs the interactive disposable-profile lane selected by the documented UI filter.
[Category("UI")]
public abstract class FileOperationUiTestBase : FileManagerUiTestBase
{
    private FileOperationWorkspace? workspace;

    protected FileOperationWorkspace Workspace => workspace ??= new FileOperationWorkspace(TargetVolumeRoot);

    // A characterization fixture can opt into a dedicated second volume.  The
    // default keeps source and target under one disposable temporary root.
    protected virtual string? TargetVolumeRoot => null;

    protected override string ApplicationArguments =>
        $"{UiTestSettings.Arguments} -l \"{Workspace.SourceDirectory}\" -r \"{Workspace.TargetDirectory}\" -p 1";

    protected override void OnAfterFileManagerStopped()
    {
        workspace?.Dispose();
    }

    protected void SelectSourceItem(string name)
    {
        var list = FindSourceList();
        NativeCommands.QuickSearch(list!.Properties.NativeWindowHandle.Value, name);
    }

    protected void SelectSourceItems(params string[] names)
    {
        // Explicit Insert selection exercises commands over mixed and multiple items instead of only the focused fallback.
        var list = FindSourceList();
        foreach (var name in names)
        {
            NativeCommands.QuickSearch(list.Properties.NativeWindowHandle.Value, name);
            NativeCommands.ToggleFocusedSelection(list.Properties.NativeWindowHandle.Value);
        }
    }

    protected Window ExecuteWithPath(int command, string sourceName, string path, bool commit)
    {
        if (sourceName.Length != 0)
            SelectSourceItem(sourceName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        var dialog = WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
        SetDialogPath(dialog, path);
        CloseDialog(dialog, commit);
        return dialog;
    }

    protected Window ExecuteAndWaitForDialog(int command, string sourceName)
    {
        SelectSourceItem(sourceName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    protected Window WaitForOperationPrompt(int buttonId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.FindFirstDescendant(cf => cf.ByAutomationId(buttonId.ToString()))?.AsButton() is not null);
    }

    protected static void ChooseOperationPrompt(Window dialog, int buttonId)
    {
        var button = dialog.FindFirstDescendant(cf => cf.ByAutomationId(buttonId.ToString()))?.AsButton();
        Assert.That(button, Is.Not.Null, $"The operation prompt did not expose button {buttonId}.");
        button!.Invoke();
        WaitForWindowToClose(dialog);
    }

    protected void SubmitInvalidPathAndCancel(int command, string sourceName, string path)
    {
        var operationDialog = ExecuteWithPathWithoutWaitingForClose(command, sourceName, path);
        var failureDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.Properties.NativeWindowHandle.Value != operationDialog.Properties.NativeWindowHandle.Value);
        CloseDialog(failureDialog, commit: true);
        // Quick Rename destroys and recreates its input dialog after an execution error; copy/move validation keeps it alive.
        var dialogToCancel = operationDialog.IsAvailable
            ? operationDialog
            : WaitForWindow(window =>
                window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
                window.FindFirstDescendant(cf => cf.ByAutomationId("2"))?.AsButton() is not null);
        CloseDialog(dialogToCancel, commit: false);
    }

    protected void ConfirmDeleteIfPrompted()
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTime.UtcNow < timeout)
        {
            var dialog = Application.GetAllTopLevelWindows(Automation)
                .FirstOrDefault(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
            var yes = dialog?.FindFirstDescendant(cf => cf.ByAutomationId("6"))?.AsButton();
            if (yes is not null)
            {
                yes.Invoke();
                return;
            }
            Thread.Sleep(100);
        }
    }

    protected static void CloseDialog(Window dialog, bool commit)
    {
        var button = dialog.FindFirstDescendant(cf => cf.ByAutomationId(commit ? "1" : "2"))?.AsButton();
        Assert.That(button, Is.Not.Null, $"The operation dialog did not expose its {(commit ? "OK" : "Cancel")} button.");
        button!.Invoke();
        WaitForWindowToClose(dialog);
    }

    protected static void WaitForFileSystem(Func<bool> predicate, string failureMessage)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        while (DateTime.UtcNow < timeout)
        {
            if (predicate())
                return;
            Thread.Sleep(100);
        }

        Assert.Fail(failureMessage);
    }

    protected Window WaitForDesktopWindow(Func<Window, bool> predicate, string failureMessage)
    {
        // Editors run out of process, so desktop-level UIA is required to observe the configured editor window.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < timeout)
        {
            var window = Automation.GetDesktop().FindAllChildren()
                .Where(element => element.ControlType == ControlType.Window)
                .Select(element => element.AsWindow())
                .FirstOrDefault(predicate);
            if (window is not null)
                return window;

            Thread.Sleep(100);
        }

        Assert.Fail(failureMessage);
        return null!;
    }

    private FlaUI.Core.AutomationElements.AutomationElement FindSourceList()
    {
        // Panel ordering is stable even when translated labels are unavailable to UI Automation.
        var list = MainWindow.FindAllDescendants()
            .Where(element => string.Equals(element.Properties.ClassName.ValueOrDefault, "SalamanderItemsBox", StringComparison.Ordinal))
            .OrderBy(element => element.BoundingRectangle.Left)
            .FirstOrDefault();
        Assert.That(list, Is.Not.Null, "The left file panel did not expose its SalamanderItemsBox control.");
        return list!;
    }

    private static void SetDialogPath(Window dialog, string path)
    {
        var input = dialog.FindAllDescendants()
            .FirstOrDefault(element => element.ControlType is ControlType.Edit or ControlType.ComboBox);
        Assert.That(input, Is.Not.Null, "The operation dialog did not expose its destination/name input.");
        if (input!.ControlType == ControlType.ComboBox)
            input.AsComboBox().Value = path;
        else
            input.AsTextBox().Text = path;
    }

    private Window ExecuteWithPathWithoutWaitingForClose(int command, string sourceName, string path)
    {
        if (sourceName.Length != 0)
            SelectSourceItem(sourceName);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        var dialog = WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
        SetDialogPath(dialog, path);
        var ok = dialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(ok, Is.Not.Null, "The operation dialog did not expose its OK button.");
        ok!.Invoke();
        return dialog;
    }
}

public sealed class FileOperationWorkspace : IDisposable
{
    public FileOperationWorkspace(string? targetVolumeRoot = null)
    {
        RootDirectory = Path.Combine(Path.GetTempPath(), "FileManager.UiTests", Guid.NewGuid().ToString("N"));
        SourceDirectory = Path.Combine(RootDirectory, "source");
        TargetWorkspaceDirectory = targetVolumeRoot is null
            ? RootDirectory
            : Path.Combine(targetVolumeRoot, "FileManager.UiTests", Guid.NewGuid().ToString("N"));
        TargetDirectory = Path.Combine(TargetWorkspaceDirectory, "target");
        Directory.CreateDirectory(SourceDirectory);
        Directory.CreateDirectory(TargetDirectory);

        WriteSourceFile("copy-file.txt", "copy-file-content");
        WriteSourceFile("rename-file.txt", "rename-file-content");
        WriteSourceFile("rename-collision.txt", "collision-content");
        // Conflict, case-only, and multi-item seeds distinguish each native rename/move/delete branch.
        WriteSourceFile("rename-case.txt", "rename-case-content");
        WriteSourceFile("move-file.txt", "move-file-content");
        WriteSourceFile("move-overwrite.txt", "move-overwrite-source-content");
        WriteSourceFile("move-skip.txt", "move-skip-source-content");
        WriteSourceFile("delete-file.txt", "delete-file-content");
        WriteSourceFile("delete-mixed-file.txt", "delete-mixed-file-content");
        WriteSourceFile("delete-z-after-skip.txt", "delete-after-skip-content");
        WriteSourceFile("cancel-copy.txt", "cancel-copy-content");
        WriteSourceFile("cancel-move.txt", "cancel-move-content");
        WriteSourceFile("cancel-rename.txt", "cancel-rename-content");
        WriteSourceFile("delete-locked.txt", "delete-locked-content");
        WriteSourceFile("overwrite-file.txt", "overwrite-source-content");
        WriteSourceFile("skip-file.txt", "skip-source-content");
        WriteSourceFile("cancel-conflict.txt", "cancel-conflict-source-content");
        WriteSourceFile("rename-overwrite.txt", "rename-overwrite-source-content");
        WriteSourceFile("recycle-file.txt", "recycle-file-content");
        WriteSourceFile("view-file.txt", "view-file-content");
        WriteSourceFile("edit-file.txt", "edit-file-content");
        WriteSourceFile(Path.Combine("find-tree", "find-target.txt"), "find-target-content");
        WriteSourceFile("create-collision", "create-collision-content");
        WriteSourceFile(Path.Combine("copy-tree", "nested", "payload.txt"), "copy-tree-content");
        WriteSourceFile(Path.Combine("rename-tree", "nested", "payload.txt"), "rename-tree-content");
        WriteSourceFile(Path.Combine("move-tree", "nested", "payload.txt"), "move-tree-content");
        WriteSourceFile(Path.Combine("move-overwrite-all-tree", "nested", "first.txt"), "move-overwrite-all-first-source");
        WriteSourceFile(Path.Combine("move-overwrite-all-tree", "nested", "second.txt"), "move-overwrite-all-second-source");
        WriteSourceFile(Path.Combine("move-skip-all-tree", "nested", "first.txt"), "move-skip-all-first-source");
        WriteSourceFile(Path.Combine("move-skip-all-tree", "nested", "second.txt"), "move-skip-all-second-source");
        WriteSourceFile(Path.Combine("move-skip-all-tree", "nested", "unique.txt"), "move-skip-all-unique-source");
        WriteSourceFile(Path.Combine("delete-tree", "nested", "payload.txt"), "delete-tree-content");
        WriteSourceFile(Path.Combine("delete-mixed-tree", "nested", "payload.txt"), "delete-mixed-tree-content");
        WriteSourceFile(Path.Combine("rename-collision-source", "payload.txt"), "rename-collision-source-content");
        WriteSourceFile(Path.Combine("rename-collision-target", "payload.txt"), "rename-collision-target-content");
        WriteSourceFile(Path.Combine("overwrite-all-tree", "nested", "first.txt"), "overwrite-all-first-source");
        WriteSourceFile(Path.Combine("overwrite-all-tree", "nested", "second.txt"), "overwrite-all-second-source");
        WriteSourceFile(Path.Combine("skip-all-tree", "nested", "first.txt"), "skip-all-first-source");
        WriteSourceFile(Path.Combine("skip-all-tree", "nested", "second.txt"), "skip-all-second-source");
        File.WriteAllText(TargetPath("overwrite-file.txt"), "overwrite-target-content");
        File.WriteAllText(TargetPath("skip-file.txt"), "skip-target-content");
        File.WriteAllText(TargetPath("cancel-conflict.txt"), "cancel-conflict-target-content");
        File.WriteAllText(TargetPath("move-overwrite.txt"), "move-overwrite-target-content");
        File.WriteAllText(TargetPath("move-skip.txt"), "move-skip-target-content");
        File.WriteAllText(TargetPath("rename-overwrite-target.txt"), "rename-overwrite-target-content");
        WriteTargetFile(Path.Combine("overwrite-all-tree", "nested", "first.txt"), "overwrite-all-first-target");
        WriteTargetFile(Path.Combine("overwrite-all-tree", "nested", "second.txt"), "overwrite-all-second-target");
        WriteTargetFile(Path.Combine("skip-all-tree", "nested", "first.txt"), "skip-all-first-target");
        WriteTargetFile(Path.Combine("skip-all-tree", "nested", "second.txt"), "skip-all-second-target");
        // Move-All targets expose whether copied sources are deleted only for committed items.
        WriteTargetFile(Path.Combine("move-overwrite-all-tree", "nested", "first.txt"), "move-overwrite-all-first-target");
        WriteTargetFile(Path.Combine("move-overwrite-all-tree", "nested", "second.txt"), "move-overwrite-all-second-target");
        WriteTargetFile(Path.Combine("move-skip-all-tree", "nested", "first.txt"), "move-skip-all-first-target");
        WriteTargetFile(Path.Combine("move-skip-all-tree", "nested", "second.txt"), "move-skip-all-second-target");
        File.WriteAllText(Path.Combine(TargetDirectory, "blocked-target"), "not-a-directory");
    }

    public string RootDirectory { get; }
    public string TargetWorkspaceDirectory { get; }
    public string SourceDirectory { get; }
    public string TargetDirectory { get; }

    public string SourcePath(string relativePath) => Path.Combine(SourceDirectory, relativePath);
    public string TargetPath(string relativePath) => Path.Combine(TargetDirectory, relativePath);

    public FileStream HoldSourceFileOpen(string relativePath) => new(SourcePath(relativePath), FileMode.Open, FileAccess.Read, FileShare.None);

    public void Dispose()
    {
        if (Directory.Exists(RootDirectory))
            Directory.Delete(RootDirectory, recursive: true);
        if (!string.Equals(TargetWorkspaceDirectory, RootDirectory, StringComparison.OrdinalIgnoreCase) &&
            Directory.Exists(TargetWorkspaceDirectory))
            Directory.Delete(TargetWorkspaceDirectory, recursive: true);
    }

    private void WriteSourceFile(string relativePath, string content)
    {
        var path = SourcePath(relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }

    private void WriteTargetFile(string relativePath, string content)
    {
        var path = TargetPath(relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, content);
    }
}
