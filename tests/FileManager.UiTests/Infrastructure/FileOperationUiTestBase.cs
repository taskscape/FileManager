using System.Runtime.InteropServices;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

[NonParallelizable]
// Every derived fixture uses the guarded current-user test-data and configuration boundaries.
[Category("UI")]
public abstract class FileOperationUiTestBase : FileManagerUiTestBase
{
    private FileOperationWorkspace? workspace;

    protected FileOperationWorkspace Workspace => workspace ??= new FileOperationWorkspace(TargetVolumeRoot);

    // A characterization fixture can opt into a dedicated second volume.  The
    // The default keeps source and target under one guarded test-data root.
    protected virtual string? TargetVolumeRoot => null;

    protected override string ApplicationArguments =>
        $"{UiTestSettings.Arguments} -l \"{Workspace.SourceDirectory}\" -r \"{Workspace.TargetDirectory}\" -p 1";

    protected override void BeforeFileManagerStarted()
    {
        // Teardown kills the application, which can interrupt an operation and
        // leave an incomplete journal in the shared appdata folder. The next
        // start would then raise the modal "Recover file operations" prompt,
        // which owns the main window: WaitForNativeMainWindow deliberately skips
        // a disabled main window, so every later case in the run would time out
        // waiting for a window it can never reach.
        //
        // Fixtures that deliberately seed a journal override this without
        // calling base, so their own recovery scenario still runs.
        PurgeStaleOperationJournals();
    }

    /// <summary>
    /// Removes operation journals left by a previous case. A killed process can
    /// still hold its journal open for a moment, so this retries briefly: one
    /// surviving journal is enough to raise the recovery prompt and strand every
    /// remaining case in the run.
    /// </summary>
    protected static void PurgeStaleOperationJournals()
    {
        var directory = UiTestSettings.JournalDirectory;
        if (!Directory.Exists(directory))
            return;

        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (true)
        {
            var remaining = 0;
            foreach (var journal in Directory.EnumerateFiles(directory))
            {
                try
                {
                    File.Delete(journal);
                }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
                {
                    remaining++;
                }
            }

            if (remaining == 0 || DateTime.UtcNow >= deadline)
                return;
            Thread.Sleep(100);
        }
    }

    protected override void OnAfterFileManagerStopped()
    {
        // The sandbox editor inherits the workspace as its working directory, so a
        // stub still open after a failed assertion would pin the tree being deleted.
        SandboxEditor.StopStrayInstances();
        // Purge here as well as before the next start: the application has just
        // exited, so this is the point at which its journal handle is released.
        PurgeStaleOperationJournals();
        try
        {
            workspace?.Dispose();
        }
        finally
        {
            // NUnit reuses fixture instances, so each test must receive a newly seeded disposable workspace.
            workspace = null;
        }
    }

    protected void SelectSourceItem(string name)
    {
        var list = FindSourceList();
        NativeCommands.ActivateFilePanel(list!.Properties.NativeWindowHandle.Value);
        // The command gates are computed from the host's active panel, which changes only through the normal panel click route.
        NativeCommands.QuickSearch(list!.Properties.NativeWindowHandle.Value, name);
        // The owner-drawn panel applies quick-search selection asynchronously; wait before dispatching a selection-sensitive command.
        Thread.Sleep(250);
        // Mark the focused match explicitly so Copy/Move/Delete observe the same selected-item state as an interactive user.
        NativeCommands.ToggleFocusedSelection(list.Properties.NativeWindowHandle.Value);
        // Insert advances the caret after selecting; restore the match because Quick Rename acts on the caret rather than the selection.
        NativeCommands.QuickSearch(list.Properties.NativeWindowHandle.Value, name);
    }

    protected void SelectSourceItems(params string[] names)
    {
        // Explicit Insert selection exercises commands over mixed and multiple items instead of only the focused fallback.
        var list = FindSourceList();
        NativeCommands.ActivateFilePanel(list.Properties.NativeWindowHandle.Value);
        // Keep multi-item gestures on the same active source panel as a user selection sequence.
        foreach (var name in names)
        {
            NativeCommands.QuickSearch(list.Properties.NativeWindowHandle.Value, name);
            // Match the single-item path: quick-search focus is asynchronous and Insert must target the settled match.
            Thread.Sleep(250);
            NativeCommands.ToggleFocusedSelection(list.Properties.NativeWindowHandle.Value);
            // Let the caret advance before the next quick-search so all prior selections remain intact.
            Thread.Sleep(100);
        }
    }

    protected Window ExecuteWithPath(int command, string sourceName, string path, bool commit)
    {
        if (sourceName.Length != 0)
            SelectSourceItem(sourceName);
        WaitForCommandEnabled(command);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        var dialog = WaitForOperationDialog();
        SetDialogPath(dialog, path);
        CloseDialog(dialog, commit);
        return dialog;
    }

    protected Window ExecuteAndWaitForDialog(int command, string sourceName)
    {
        SelectSourceItem(sourceName);
        WaitForCommandEnabled(command);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        return WaitForOperationDialog();
    }

    protected Window WaitForOperationPrompt(int buttonId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            NativeCommands.HasDialogButton(window.Properties.NativeWindowHandle.Value, buttonId));
    }

    protected static void ChooseOperationPrompt(Window dialog, int buttonId)
    {
        // Native button IDs are stable while this application's legacy UIA provider can omit action patterns.
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, buttonId);
        WaitForWindowToClose(dialog);
    }

    protected void SubmitInvalidPathAndCancel(int command, string sourceName, string path)
    {
        var operationDialog = ExecuteWithPathWithoutWaitingForClose(command, sourceName, path);
        var failureDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.Properties.NativeWindowHandle.Value != operationDialog.Properties.NativeWindowHandle.Value);
        CloseDialog(failureDialog, commit: true);
        // The host destroys and recreates its input dialog after an execution error,
        // and the recreated window can carry a different handle while the original
        // still reports itself as available. Re-find the input dialog by its path
        // control instead of trusting the handle captured before the error.
        var dialogToCancel = WaitForOperationDialog();
        CloseDialog(dialogToCancel, commit: false);
    }

    /// <summary>
    /// Requests cancellation the way a user does during a running operation. The
    /// progress window offers Cancel but no Yes, which distinguishes it from a
    /// conflict prompt that also carries a Cancel button.
    /// </summary>
    protected void CancelThroughProgressWindow()
    {
        var progress = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            NativeCommands.HasDialogButton(window.Properties.NativeWindowHandle.Value, 2) &&
            !NativeCommands.HasDialogButton(window.Properties.NativeWindowHandle.Value, 6));
        // Post rather than send: this click opens the confirmation synchronously,
        // so a blocking send would not return until that confirmation is answered.
        NativeCommands.PostDialogButtonClick(progress.Properties.NativeWindowHandle.Value, 2); // IDCANCEL
        // The confirmation appears on top of the progress window, so answer it
        // before waiting for anything else to close.
        ConfirmCancellationIfPrompted();
    }
    /// <summary>
    /// Answers the "Do you want to cancel this operation?" confirmation that the
    /// progress dialog raises when cancellation is requested (IDS_CANCELOPERATION,
    /// MB_YESNO). Leaving it open blocks the worker and the operation never ends.
    /// Matching on the caption avoids the Yes button of a conflict prompt.
    /// </summary>
    protected void ConfirmCancellationIfPrompted() => AnswerQuestionIfPrompted(6); // IDYES

    /// <summary>
    /// Answers the host's generic Question prompt when one appears. Several
    /// operations raise one as a normal part of the flow - cancellation, and
    /// creating a directory whose parent does not exist yet - and leaving it open
    /// blocks the worker. Matching on the caption avoids the Yes button that a
    /// conflict prompt also carries.
    /// </summary>
    protected bool AnswerQuestionIfPrompted(int buttonId)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTime.UtcNow < timeout)
        {
            var question = NativeCommands.FindDialogByTitle(Application.ProcessId, "Question");
            if (question != 0)
            {
                NativeCommands.ClickDialogButton(question, buttonId);
                return true;
            }
            Thread.Sleep(100);
        }
        return false;
    }

    protected void ConfirmDeleteIfPrompted()
    {
        // Native button lookup, not UI Automation: the message box renumbers its
        // buttons after they are created (CMessageBox assigns GWLP_ID in
        // WM_INITDIALOG), so the automation id still reports the template
        // placeholder and a search for IDYES found nothing. The confirmation then
        // stayed open and the delete never ran - visible only as a SHOW with no
        // matching RESULT in the dialog transcript.
        //
        // The click is posted because answering it runs the whole operation
        // inside the application's message handling, which can raise a further
        // modal prompt this test still has to answer.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTime.UtcNow < timeout)
        {
            var confirmation = NativeCommands.GetTopLevelWindows(Application.ProcessId)
                .FirstOrDefault(handle => handle != MainWindow.Properties.NativeWindowHandle.Value &&
                                          NativeCommands.HasDialogButton(handle, 6));
            if (confirmation != 0)
            {
                NativeCommands.PostDialogButtonClick(confirmation, 6); // IDYES
                return;
            }
            Thread.Sleep(100);
        }
    }

    protected static void CloseDialog(Window dialog, bool commit)
    {
        // Native standard IDs keep operation submission independent of incomplete Button UIA patterns.
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, commit ? 1 : 2);
        WaitForWindowToClose(dialog);
    }

    protected void WaitForFileSystem(Func<bool> predicate, string failureMessage)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        while (DateTime.UtcNow < timeout)
        {
            if (predicate())
                return;
            Thread.Sleep(100);
        }

        var openWindowTitles = string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId));
        // Include native modal titles to differentiate a delayed operation from an error that needs a separate user response.
        Assert.Fail($"{failureMessage} Open FileManager windows: {openWindowTitles}.");
    }

    /// <summary>
    /// Waits for a filesystem condition while answering the host's Question prompts.
    /// Gated operations raise one prompt per affected item, so a single answer up
    /// front leaves the rest of the operation waiting.
    /// </summary>
    protected void WaitForFileSystemAnsweringQuestions(Func<bool> predicate, int buttonId, string failureMessage)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(30);
        while (DateTime.UtcNow < timeout)
        {
            if (predicate())
                return;

            var question = NativeCommands.FindDialogByTitle(Application.ProcessId, "Question");
            if (question != 0)
                NativeCommands.ClickDialogButton(question, buttonId);
            Thread.Sleep(100);
        }

        var openWindowTitles = string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId));
        Assert.Fail($"{failureMessage} Open FileManager windows: {openWindowTitles}.");
    }

    protected Window WaitForDesktopWindow(Func<Window, bool> predicate, string failureMessage)
    {
        // Editors run out of process, so desktop-level UIA is required to observe the configured editor window.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < timeout)
        {
            // A desktop-wide scan races any window that closes while it runs, so a
            // stale provider must not fail the case that is still waiting.
            Window? window = null;
            foreach (var element in Automation.GetDesktop().FindAllChildren())
            {
                try
                {
                    if (element.ControlType != ControlType.Window)
                        continue;
                    var candidate = element.AsWindow();
                    if (predicate(candidate))
                    {
                        window = candidate;
                        break;
                    }
                }
                catch (COMException)
                {
                    // The window went away between enumeration and inspection.
                }
            }
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
        // IDE_PATH is shared by the native create/copy/move/rename templates and bypasses their incomplete UIA children.
        //
        // The dialog performs its own data-to-window transfer shortly after that
        // control exists, so text written the moment the dialog is found can be
        // overwritten again. Copy and Move hid this because their default value is
        // already the destination the test wanted; Create Directory starts empty and
        // was therefore submitted blank, which the host rejects as an empty name.
        // Re-apply until the value survives a settling delay.
        var handle = dialog.Properties.NativeWindowHandle.Value;
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (true)
        {
            NativeCommands.SetOperationPath(handle, path);
            Thread.Sleep(200);
            if (string.Equals(NativeCommands.GetOperationPath(handle), path, StringComparison.Ordinal))
            {
                Thread.Sleep(200);
                if (string.Equals(NativeCommands.GetOperationPath(handle), path, StringComparison.Ordinal))
                    return;
            }

            if (DateTime.UtcNow >= deadline)
                break;
        }

        Assert.Fail($"The native operation dialog did not retain the submitted destination or name. Wanted '{path}', read '{NativeCommands.GetOperationPath(handle)}'.");
    }

    private Window ExecuteWithPathWithoutWaitingForClose(int command, string sourceName, string path)
    {
        if (sourceName.Length != 0)
            SelectSourceItem(sourceName);
        WaitForCommandEnabled(command);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, command);
        var dialog = WaitForOperationDialog();
        SetDialogPath(dialog, path);
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, 1);
        return dialog;
    }

    private Window WaitForOperationDialog()
    {
        // Filter native top-level windows by IDE_PATH so ComboLBox helpers and progress prompts cannot be mistaken for the input dialog.
        // The main window must be excluded explicitly: the control lookup falls back
        // to a recursive child scan, so a descendant of the panel window can carry the
        // same control id and be mistaken for the operation dialog. The text then goes
        // to the wrong control while the real dialog is submitted with its default.
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            NativeCommands.HasOperationPathControl(window.Properties.NativeWindowHandle.Value));
    }

    protected void WaitForCommandEnabled(int command)
    {
        var toolbarState = NativeCommands.TryGetToolbarCommandEnabled(MainWindow.Properties.NativeWindowHandle.Value, command);
        if (toolbarState == false)
        {
            Assert.Fail($"Visible toolbar command {command} remained disabled after selecting the sandbox item.");
        }

        // This host refreshes command enablers into toolbars but leaves the native menu at its startup state; absent buttons therefore require the command handler itself to prove enablement.
        Thread.Sleep(250);
    }
}

public sealed class FileOperationWorkspace : IDisposable
{
    public FileOperationWorkspace(string? targetVolumeRoot = null)
    {
        // Every filesystem operation stays below the test-run-owned filemanager-testdata directory.
        RootDirectory = Path.Combine(UiTestSettings.TestDataRoot, Guid.NewGuid().ToString("N"));
        SourceDirectory = Path.Combine(RootDirectory, "source");
        TargetWorkspaceDirectory = targetVolumeRoot is null
            ? RootDirectory
            : Path.Combine(targetVolumeRoot, Guid.NewGuid().ToString("N"));
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
        // Rename resolves inside the panel holding the item, so its collision target
        // has to exist in the source directory; seeding it in the target directory
        // meant the overwrite prompt under test never appeared.
        WriteSourceFile("rename-overwrite-target.txt", "rename-overwrite-target-content");
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
        SeedAlternateDataStreamFixtures();
    }

    /// <summary>
    /// Seeds the alternate-data-stream fixtures with every other fixture file,
    /// before the application lists the directory. Creating them from inside a test
    /// raced the panel refresh: the item was not listed yet, so quick-search
    /// selected nothing and the operation under test never ran.
    ///
    /// Failures are ignored so a volume without stream support still yields a usable
    /// workspace; those cases skip themselves through
    /// <see cref="AlternateDataStreams.RequireSupportAt"/>.
    /// </summary>
    private void SeedAlternateDataStreamFixtures()
    {
        try
        {
            var copySource = SourcePath("ads-copy.txt");
            File.WriteAllText(copySource, "ads-copy-default-content");
            AlternateDataStreams.Write(copySource, "notes", "named-stream-content"u8.ToArray());
            AlternateDataStreams.Write(copySource, "empty", []);
            AlternateDataStreams.Write(copySource, "large", LargeStreamContent);
            AlternateDataStreams.Write(copySource, "edge name.with.dots", "edge-stream-content"u8.ToArray());

            var overwriteSource = SourcePath("ads-overwrite.txt");
            var overwriteTarget = TargetPath("ads-overwrite.txt");
            File.WriteAllText(overwriteSource, "ads-overwrite-source");
            File.WriteAllText(overwriteTarget, "ads-overwrite-target");
            AlternateDataStreams.Write(overwriteSource, "replacement", "replacement-stream-content"u8.ToArray());
            AlternateDataStreams.Write(overwriteTarget, "replacement", "stale-replacement-content"u8.ToArray());
            AlternateDataStreams.Write(overwriteTarget, "stale", "stale-stream-content"u8.ToArray());

            var retrySource = SourcePath("ads-retry.txt");
            File.WriteAllText(retrySource, "ads-retry-default-content");
            AlternateDataStreams.Write(retrySource, "temporarily-denied", "retry-stream-content"u8.ToArray());
        }
        catch (IOException)
        {
            // The volume does not support streams; the affected cases skip themselves.
        }
    }

    /// <summary>Deterministic multi-megabyte stream payload shared with the tests.</summary>
    public static byte[] LargeStreamContent { get; } = CreateLargeStreamContent();

    private static byte[] CreateLargeStreamContent()
    {
        var content = new byte[(3 * 1024 * 1024) + 17];
        for (var index = 0; index < content.Length; index++)
            content[index] = (byte)(index % 251);
        return content;
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
        DeleteDirectoryTree(RootDirectory);
        if (!string.Equals(TargetWorkspaceDirectory, RootDirectory, StringComparison.OrdinalIgnoreCase) &&
            Directory.Exists(TargetWorkspaceDirectory))
            DeleteDirectoryTree(TargetWorkspaceDirectory);
    }

    internal static void DeleteDirectoryTree(string path)
    {
        if (!Directory.Exists(path))
            return;

        foreach (var child in Directory.EnumerateFileSystemEntries(path))
        {
            var attributes = File.GetAttributes(child);
            if ((attributes & FileAttributes.ReparsePoint) != 0)
            {
                // Delete the link itself without recursively following a junction or symbolic-link target.
                if ((attributes & FileAttributes.Directory) != 0)
                    Directory.Delete(child);
                else
                    File.Delete(child);
            }
            else if ((attributes & FileAttributes.Directory) != 0)
                DeleteDirectoryTree(child);
            else
                File.Delete(child);
        }

        Directory.Delete(path);
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
