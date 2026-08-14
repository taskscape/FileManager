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
    private readonly HashSet<string> journalsBeforeStart = new(StringComparer.OrdinalIgnoreCase);

    protected FileOperationWorkspace Workspace => workspace ??= new FileOperationWorkspace(TargetVolumeRoot);

    // A characterization fixture can opt into a dedicated second volume.  The
    // default keeps source and target under one disposable temporary root.
    protected virtual string? TargetVolumeRoot => null;

    protected override string ApplicationArguments =>
        $"{UiTestSettings.Arguments} -l \"{Workspace.SourceDirectory}\" -r \"{Workspace.TargetDirectory}\" -p 1";

    protected override void BeforeFileManagerStarted()
    {
        journalsBeforeStart.Clear();
        var journalDirectory = GetOperationJournalDirectory();
        if (Directory.Exists(journalDirectory))
            journalsBeforeStart.UnionWith(Directory.EnumerateFiles(journalDirectory, "*.opj"));
    }

    protected override void OnAfterFileManagerStopped()
    {
        try
        {
            var journalDirectory = GetOperationJournalDirectory();
            if (Directory.Exists(journalDirectory))
            {
                // Each nonparallel case removes only journals it created, so a failed worker cannot block the next startup.
                foreach (var journal in Directory.EnumerateFiles(journalDirectory, "*.opj")
                                                 .Where(path => !journalsBeforeStart.Contains(path)))
                    File.Delete(journal);
            }
        }
        finally
        {
            var completedWorkspace = workspace;
            // Clear first so a cleanup exception cannot leak this case's partial topology into the next NUnit case.
            workspace = null;
            completedWorkspace?.Dispose();
        }
    }

    protected void SelectSourceItem(string name)
    {
        // ADS and topology scenarios create sources after startup, so refresh before selecting from the owner-drawn list.
        NativeCommands.RefreshLeftPanel(MainWindowHandle);
        var list = MainWindow.FindAllDescendants()
            .Where(element => string.Equals(element.Properties.ClassName.ValueOrDefault, "SalamanderItemsBox", StringComparison.Ordinal))
            .OrderBy(element => element.BoundingRectangle.Left)
            .FirstOrDefault();
        Assert.That(list, Is.Not.Null, "The left file panel did not expose its SalamanderItemsBox control.");
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
        NativeCommands.Execute(MainWindowHandle, command);
        var dialog = WaitForOperationEntryDialog();
        SetDialogPath(dialog, path);
        CloseDialog(dialog, commit);
        return dialog;
    }

    protected Window ExecuteAndWaitForDialog(int command, string sourceName)
    {
        SelectSourceItem(sourceName);
        NativeCommands.Execute(MainWindowHandle, command);
        return WaitForOperationEntryDialog();
    }

    protected Window WaitForOperationPrompt(int buttonId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, buttonId));
    }

    protected static void ChooseOperationPrompt(Window dialog, int buttonId)
    {
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, buttonId);
        WaitForWindowToClose(dialog);
    }

    protected void SubmitInvalidPathAndCancel(int command, string sourceName, string path)
    {
        var operationDialog = ExecuteWithPathWithoutWaitingForClose(command, sourceName, path);
        var operationDialogHandle = operationDialog.Properties.NativeWindowHandle.Value;
        var failureDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
            window.Properties.NativeWindowHandle.Value != operationDialogHandle &&
            (NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 1) ||
             NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 2)));
        // Error boxes use OK, while overwrite/collision confirmations are cancelled to preserve both filesystem items.
        CloseDialog(failureDialog, commit: NativeCommands.HasDialogControl(failureDialog.Properties.NativeWindowHandle.Value, 1));
        // Some validation failures close the entry dialog themselves; cancel it only when its HWND remains alive.
        if (NativeCommands.IsWindowAvailable(operationDialogHandle))
            CloseDialog(operationDialog, commit: false);
    }

    protected void ConfirmDeleteIfPrompted()
    {
        DismissOptionalPrompt(6, TimeSpan.FromSeconds(3), 6); // IDYES
    }

    protected void ConfirmCreateDirectoryParentsIfPrompted()
    {
        DismissOptionalPrompt(1, TimeSpan.FromSeconds(3), 1, 2); // IDOK with IDCANCEL distinguishes the missing-parent question.
    }

    protected bool DismissOptionalPrompt(int buttonToClick, TimeSpan wait, params int[] requiredControls)
    {
        var timeout = DateTime.UtcNow + wait;
        while (DateTime.UtcNow < timeout)
        {
            var dialog = GetFileManagerTopLevelWindows()
                .FirstOrDefault(window => window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
                                          requiredControls.All(controlId =>
                                              NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, controlId)));
            if (dialog is not null)
            {
                // Stable button IDs allow optional confirmations to vary by profile without translated-text coupling.
                NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, buttonToClick);
                return true;
            }
            Thread.Sleep(100);
        }
        return false;
    }

    protected static void CloseDialog(Window dialog, bool commit)
    {
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, commit ? 1 : 2);
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

    protected static void WaitForOperationJournalTerminal(string source, string failureMessage)
    {
        WaitForFileSystem(() =>
        {
            var journal = FindOperationJournalFor(source);
            if (journal is null || !TryReadOperationJournal(journal, out var content))
                return false;
            // These are the four terminal states recognized by the native startup reconciler.
            return content.Contains("OPERATION|completed", StringComparison.Ordinal) ||
                   content.Contains("OPERATION|cancelled", StringComparison.Ordinal) ||
                   content.Contains("OPERATION|failed", StringComparison.Ordinal) ||
                   content.Contains("OPERATION|reconciled", StringComparison.Ordinal);
        }, failureMessage);
    }

    protected static string? FindOperationJournalFor(string source)
    {
        var directory = GetOperationJournalDirectory();
        if (!Directory.Exists(directory))
            return null;

        // Newest-first lookup binds completion checks to the disposable source path unique to this case.
        return Directory.EnumerateFiles(directory, "*.opj")
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault(path => TryReadOperationJournal(path, out var content) &&
                                    content.Contains(source, StringComparison.Ordinal));
    }

    protected static bool TryReadOperationJournal(string path, out string content)
    {
        try
        {
            // The application appends journal records while tests observe them, so readers must share writes and deletion.
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                                              FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            content = reader.ReadToEnd();
            return true;
        }
        catch (IOException)
        {
            content = string.Empty;
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            content = string.Empty;
            return false;
        }
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
        NativeCommands.Execute(MainWindowHandle, command);
        var dialog = WaitForOperationEntryDialog();
        SetDialogPath(dialog, path);
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, 1);
        return dialog;
    }

    private Window WaitForOperationEntryDialog()
    {
        // Destination and name dialogs expose editable input plus standard OK/Cancel identities across languages.
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
            window.FindAllDescendants().Any(element => element.ControlType is ControlType.Edit or ControlType.ComboBox) &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 1) &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 2));
    }

    private static string GetOperationJournalDirectory() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Open Salamander", "operation-journals");
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
        // Rename is panel-local, so its overwrite fixture must collide in the source panel rather than the opposite panel.
        File.WriteAllText(SourcePath("rename-overwrite-target.txt"), "rename-overwrite-target-content");
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
            DeleteDirectoryTreeWithoutTraversingReparsePoints(RootDirectory);
        if (!string.Equals(TargetWorkspaceDirectory, RootDirectory, StringComparison.OrdinalIgnoreCase) &&
            Directory.Exists(TargetWorkspaceDirectory))
            DeleteDirectoryTreeWithoutTraversingReparsePoints(TargetWorkspaceDirectory);
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

    private static void DeleteDirectoryTreeWithoutTraversingReparsePoints(string directory)
    {
        foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
        {
            var attributes = File.GetAttributes(entry);
            if ((attributes & FileAttributes.Directory) != 0)
            {
                // A junction or directory symlink is a fixture entry, never a cleanup traversal boundary.
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                    Directory.Delete(entry);
                else
                    DeleteDirectoryTreeWithoutTraversingReparsePoints(entry);
            }
            else
            {
                if ((attributes & FileAttributes.ReadOnly) != 0)
                    File.SetAttributes(entry, attributes & ~FileAttributes.ReadOnly);
                File.Delete(entry);
            }
        }

        Directory.Delete(directory);
    }
}
