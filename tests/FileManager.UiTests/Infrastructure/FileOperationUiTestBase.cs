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
    // Secondary-volume panels can report a completed refresh before their owner-drawn quick-search and selection states are usable.
    private const int PanelSettleMilliseconds = 750;
    private FileOperationWorkspace? workspace;

    protected FileOperationWorkspace Workspace => workspace ??= new FileOperationWorkspace(TargetVolumeRoot);

    // A characterization fixture can opt into a dedicated second volume. The
    // default keeps source and target under one guarded test-data root.
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
        // Create scenario-specific inputs before FileManager enumerates its panels;
        // post-start files can be absent from the legacy owner-drawn list on CI.
        SeedWorkspaceBeforeFileManagerStart(Workspace);
    }

    protected virtual void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
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

    protected void SelectSourceItem(string name) => SelectPanelItem(left: true, name);

    protected void SelectTargetItem(string name) => SelectPanelItem(left: false, name);

    protected void SelectPanelItem(bool left, string name)
    {
        var list = left ? PrepareSourcePanelForSelection() : PreparePanelForSelection(left);
        // Starting from an empty native selected set keeps an incomplete panel refresh from carrying a prior fixture into this operation.
        NativeCommands.ClearActiveSelection(NativeMainWindowHandle);
        if (left)
            QuickSearchSourceItem(list, name);
        else
            QuickSearchPanelItem(list, name);
        // Mark the focused match explicitly so Copy/Move/Delete observe the same selected-item state as an interactive user.
        NativeCommands.ToggleFocusedSelection(list.Properties.NativeWindowHandle.Value);
        // Insert publishes selection-dependent command state on the panel's following idle turn, which can be delayed on a secondary volume.
        Thread.Sleep(PanelSettleMilliseconds);
        // Insert advances the caret after selecting; restore the match because Quick Rename acts on the caret rather than the selection.
        if (left)
            QuickSearchSourceItem(list, name);
        else
            QuickSearchPanelItem(list, name);
    }

    /// <summary>
    /// Focuses a listed name without Insert so Copy uses the empty-selection focused-item rule.
    /// </summary>
    protected void FocusSourceItem(string name)
    {
        var list = PreparePanelForSelection(left: true);
        NativeCommands.ClearActiveSelection(NativeMainWindowHandle);
        QuickSearchPanelItem(list, name);
    }

    protected void ActivateTargetPanel()
    {
        var list = FindPanelList(left: false);
        NativeCommands.ActivateFilePanel(list.Properties.NativeWindowHandle.Value);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void OpenFocusedItem()
    {
        // CM_OPEN is the same Enter path used for directories and archives.
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.OpenFile);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void GoToParentDirectory()
    {
        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.ParentDirectory);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void SwapPanels()
    {
        // Pointer swap finishes inside the handler; sending keeps the next ActivateSourcePanel from reading the pre-swap layout.
        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.SwapPanels);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void ActivateSourcePanel()
    {
        var list = FindPanelList(left: true);
        NativeCommands.ActivateFilePanel(list.Properties.NativeWindowHandle.Value);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void WaitForMainWindowTitleContaining(string text, string failureMessage)
    {
        WaitForFileSystem(
            () => NativeCommands.GetWindowTitle(NativeMainWindowHandle)
                .Contains(text, StringComparison.OrdinalIgnoreCase),
            failureMessage);
    }

    /// <summary>
    /// Posts WM_CLOSE and waits on the native lifetime. UIA Close and post-close handle reads
    /// time out once Find's UI thread has started tearing down the modeless dialog.
    /// </summary>
    protected static void CloseModelessDialog(Window dialog)
    {
        nint handle;
        try
        {
            handle = dialog.Properties.NativeWindowHandle.Value;
        }
        catch (COMException)
        {
            return;
        }

        CloseModelessDialog(handle);
    }

    protected static void CloseModelessDialog(nint handle)
    {
        if (handle == 0 || !NativeCommands.WindowExists(handle))
            return;
        NativeCommands.PostCloseWindow(handle);
        WaitUntilNativeWindowClosed(handle);
    }

    protected static void WaitUntilNativeWindowClosed(nint handle)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (!NativeCommands.WindowExists(handle))
                return;
            Thread.Sleep(100);
        }

        Assert.Fail("Timed out waiting for the native window to close.");
    }

    protected void WaitUntilNoWindowTitled(string title, string failureMessage)
    {
        WaitForFileSystem(
            () => NativeCommands.FindDialogByTitle(Application.ProcessId, title) == 0,
            failureMessage);
    }

    protected void DismissOptionalOkDialog()
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTime.UtcNow < deadline)
        {
            var dialog = NativeCommands.GetTopLevelWindows(Application.ProcessId)
                .FirstOrDefault(handle => handle != NativeMainWindowHandle &&
                                          NativeCommands.HasDialogButton(handle, 1));
            if (dialog != 0)
            {
                NativeCommands.ClickDialogButton(dialog, 1); // IDOK
                return;
            }
            Thread.Sleep(100);
        }
    }

    protected void RefreshSourcePanel() => RefreshPanel(left: true);

    protected void RefreshTargetPanel() => RefreshPanel(left: false);

    protected void RefreshPanel(bool left)
    {
        var list = FindPanelList(left);
        NativeCommands.ActivateFilePanel(list.Properties.NativeWindowHandle.Value);
        // Refresh the panel after a test has changed its fixture on disk or navigated into a new listing.
        NativeCommands.RefreshActiveFilePanel(NativeMainWindowHandle);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void SelectSourceItems(params string[] names)
    {
        // Explicit Insert selection exercises commands over mixed and multiple items instead of only the focused fallback.
        var list = PreparePanelForSelection(left: true);
        // Multiple selection must begin empty so every subsequent Insert represents exactly one requested fixture.
        NativeCommands.ClearActiveSelection(NativeMainWindowHandle);
        // Keep multi-item gestures on the same active source panel as a user selection sequence.
        foreach (var name in names)
        {
            QuickSearchPanelItem(list, name);
            NativeCommands.ToggleFocusedSelection(list.Properties.NativeWindowHandle.Value);
            // Preserve each Insert selection until the owner-drawn panel has published it before searching for the next item.
            Thread.Sleep(PanelSettleMilliseconds);
        }
    }

    protected Window ExecuteWithPath(int command, string sourceName, string path, bool commit)
    {
        if (sourceName.Length != 0)
            SelectSourceItem(sourceName);
        WaitForCommandEnabled(command);
        NativeCommands.Execute(NativeMainWindowHandle, command);
        var dialog = WaitForOperationDialog(command);
        SetDialogPath(dialog, path);
        CloseDialog(dialog, commit);
        return dialog;
    }

    protected Window ExecuteAndWaitForDialog(int command, string sourceName)
    {
        SelectSourceItem(sourceName);
        WaitForCommandEnabled(command);
        NativeCommands.Execute(NativeMainWindowHandle, command);
        return WaitForOperationDialog(command);
    }

    protected Window WaitForOperationPrompt(int buttonId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
            NativeCommands.HasDialogButton(window.Properties.NativeWindowHandle.Value, buttonId));
    }

    protected Window WaitForOperationPrompt(string title, int buttonId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
            string.Equals(NativeCommands.GetWindowTitle(window.Properties.NativeWindowHandle.Value), title, StringComparison.Ordinal) &&
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
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
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
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
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
                .FirstOrDefault(handle => handle != NativeMainWindowHandle &&
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
            try
            {
                if (predicate())
                    return;
            }
            catch (IOException)
            {
                // A predicate that reads an operation output can race its final native handle close; retry instead of converting that transient state into a test failure.
            }
            catch (UnauthorizedAccessException)
            {
                // A destination can briefly inherit restrictive ACL or sharing state while the native operation commits it; the same completion barrier applies.
            }
            Thread.Sleep(100);
        }

        var openWindowTitles = string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId));
        // Include native modal titles to differentiate a delayed operation from an error that needs a separate user response.
        Assert.Fail($"{failureMessage} Open FileManager windows: {openWindowTitles}.");
    }

    /// <summary>
    /// Waits until an operation output exists and no process still owns a
    /// conflicting file handle. File existence alone is not a completion
    /// signal: the native copy/move worker publishes the directory entry before
    /// closing its destination handle.
    /// </summary>
    protected void WaitForOperationOutputToBeReleased(string path, string failureMessage)
    {
        WaitForFileSystem(() =>
        {
            try
            {
                // FileShare.None turns this probe into a real handle-release barrier rather than merely proving that the directory entry is visible.
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.None);
                return true;
            }
            catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException or IOException or UnauthorizedAccessException)
            {
                return false;
            }
        }, failureMessage);
    }

    /// <summary>
    /// Waits for a filesystem condition while confirming IDYES prompts (overwrite,
    /// delete, NTFS compress). Compare-then-copy hits overwrite on the differing
    /// same-name file; answering once up front leaves later prompts blocking the worker.
    /// </summary>
    protected void WaitForFileSystemAnsweringYes(Func<bool> predicate, string failureMessage)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(30);
        while (DateTime.UtcNow < timeout)
        {
            try
            {
                if (predicate())
                    return;
            }
            catch (IOException)
            {
                // Destination handles can still be closing while an overwrite prompt is showing.
            }
            catch (UnauthorizedAccessException)
            {
                // A read-only destination can appear briefly while attributes are applied.
            }

            var confirmation = NativeCommands.GetTopLevelWindows(Application.ProcessId)
                .FirstOrDefault(handle => handle != NativeMainWindowHandle &&
                                          NativeCommands.HasDialogButton(handle, 6));
            if (confirmation != 0)
                NativeCommands.PostDialogButtonClick(confirmation, 6); // IDYES
            Thread.Sleep(100);
        }

        var openWindowTitles = string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId));
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

    protected FlaUI.Core.AutomationElements.AutomationElement FindPanelList(bool left)
    {
        // Panel ordering is stable even when translated labels are unavailable to UI Automation.
        var lists = MainWindow.FindAllDescendants()
            .Where(element => string.Equals(element.Properties.ClassName.ValueOrDefault, "SalamanderItemsBox", StringComparison.Ordinal))
            .OrderBy(element => element.BoundingRectangle.Left)
            .ToList();
        Assert.That(lists.Count, Is.GreaterThanOrEqualTo(left ? 1 : 2),
                    left
                        ? "The left file panel did not expose its SalamanderItemsBox control."
                        : "The right file panel did not expose its SalamanderItemsBox control.");
        return lists[left ? 0 : 1];
    }

    private FlaUI.Core.AutomationElements.AutomationElement PrepareSourcePanelForSelection() =>
        PreparePanelForSelection(left: true);

    private FlaUI.Core.AutomationElements.AutomationElement PreparePanelForSelection(bool left)
    {
        var list = FindPanelList(left);
        NativeCommands.ActivateFilePanel(list.Properties.NativeWindowHandle.Value);
        // A visible main window can precede secondary-volume directory enumeration, so
        // refresh every selection path before quick-searching a seeded fixture.
        NativeCommands.RefreshActiveFilePanel(NativeMainWindowHandle);
        Thread.Sleep(PanelSettleMilliseconds);
        return list;
    }

    private static void QuickSearchSourceItem(FlaUI.Core.AutomationElements.AutomationElement list, string name) =>
        QuickSearchPanelItem(list, name);

    private static void QuickSearchPanelItem(FlaUI.Core.AutomationElements.AutomationElement list, string name)
    {
        NativeCommands.QuickSearch(list.Properties.NativeWindowHandle.Value, name);
        // Clipboard ownership is not stable in the headless runner, so wait for the panel's native quick-search update instead of probing global clipboard state.
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected static void SetDialogPath(Window dialog, string path)
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
        NativeCommands.Execute(NativeMainWindowHandle, command);
        var dialog = WaitForOperationDialog(command);
        SetDialogPath(dialog, path);
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, 1);
        return dialog;
    }

    // Filter native top-level windows by IDE_PATH so ComboLBox helpers and progress prompts cannot be mistaken for the input dialog.
    // The main window must be excluded explicitly: the control lookup falls back
    // to a recursive child scan, so a descendant of the panel window can carry the
    // same control id and be mistaken for the operation dialog. The text then goes
    // to the wrong control while the real dialog is submitted with its default.
    private bool IsOperationDialog(Window window) =>
        window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
        NativeCommands.HasOperationPathControl(window.Properties.NativeWindowHandle.Value);

    protected Window WaitForOperationDialog()
    {
        return WaitForWindow(IsOperationDialog);
    }

    // A posted WM_COMMAND can transiently arrive while the panel is between idle
    // refreshes: the toolbar still reports the command enabled, yet the handler
    // reads a stale enabler or an intermediate listing and returns without opening
    // any dialog or prompt. A user simply triggers the command again, so after the
    // caller's own post stayed silent for one wait window this helper re-posts
    // 'postedCommand' on a bounded budget instead of failing the case on one lost
    // message. The first pass must not post: callers already did, and a duplicate
    // would open a second dialog that later operation waits would then match. The
    // re-post is skipped while the main window is disabled because that means a
    // modal dialog is already up (possibly mid-initialization) and the wait must
    // observe it rather than queue a duplicate command behind it. A genuinely
    // broken dialog still ends in the standard WaitForWindow timeout diagnostics.
    protected Window WaitForOperationDialog(int postedCommand)
    {
        const int repostWaitMilliseconds = 4000;
        var retryDeadline = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        bool firstPass = true;
        while (true)
        {
            if (!firstPass && postedCommand != 0 && NativeCommands.IsWindowEnabledWindow(NativeMainWindowHandle))
                NativeCommands.Execute(NativeMainWindowHandle, postedCommand);
            firstPass = false;
            var dialog = WaitForWindowOrNull(IsOperationDialog, repostWaitMilliseconds);
            if (dialog is not null)
                return dialog;
            if (DateTime.UtcNow >= retryDeadline)
            {
                FailWaitForWindow();
                return null!; // unreachable; Assert.Fail throws
            }
        }
    }

    protected Window WaitForDialogWithControl(int controlId)
    {
        return WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, controlId));
    }

    protected void ApplyPanelFilter(string mask)
    {
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeFilter);
        var dialog = WaitForDialogWithControl(NativeCommands.FilterEdit);
        var handle = dialog.Properties.NativeWindowHandle.Value;
        NativeCommands.ClickDialogControl(handle, NativeCommands.FilterUse);
        RetainDialogControlText(handle, NativeCommands.FilterEdit, mask);
        CloseDialog(dialog, commit: true);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void ClearPanelFilter()
    {
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ChangeFilter);
        var dialog = WaitForDialogWithControl(NativeCommands.FilterEdit);
        NativeCommands.ClickDialogControl(dialog.Properties.NativeWindowHandle.Value, NativeCommands.FilterDontUse);
        CloseDialog(dialog, commit: true);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    protected void SelectByMask(string mask)
    {
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.SelectByMask);
        var dialog = WaitForDialogWithControl(NativeCommands.FileMaskControl);
        RetainDialogControlText(dialog.Properties.NativeWindowHandle.Value, NativeCommands.FileMaskControl, mask);
        CloseDialog(dialog, commit: true);
        Thread.Sleep(PanelSettleMilliseconds);
    }

    private static void RetainDialogControlText(nint dialogHandle, int controlId, string text)
    {
        // Combo history transfer can overwrite text written the instant the dialog appears,
        // the same race SetDialogPath already retries for IDE_PATH.
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (true)
        {
            NativeCommands.SetDialogControlText(dialogHandle, controlId, text);
            Thread.Sleep(200);
            if (string.Equals(NativeCommands.GetDialogControlText(dialogHandle, controlId), text, StringComparison.Ordinal))
            {
                Thread.Sleep(200);
                if (string.Equals(NativeCommands.GetDialogControlText(dialogHandle, controlId), text, StringComparison.Ordinal))
                    return;
            }

            if (DateTime.UtcNow >= deadline)
                break;
        }

        Assert.Fail($"The native dialog did not retain control {controlId} text '{text}'.");
    }

    protected void SetCopyNamedMask(Window copyDialog, string mask)
    {
        var handle = copyDialog.Properties.NativeWindowHandle.Value;
        // Criteria controls exist even while Options is collapsed; Transfer still reads them on OK.
        NativeCommands.SetDialogCheckBoxState(handle, NativeCommands.CopyNamedCheck, isChecked: true);
        NativeCommands.SetDialogControlText(handle, NativeCommands.CopyNamedMask, mask);
    }

    protected void WaitForCommandEnabled(int command)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        var observedToolbarCommand = false;
        while (DateTime.UtcNow < deadline)
        {
            var toolbarState = NativeCommands.TryGetToolbarCommandEnabled(NativeMainWindowHandle, command);
            if (toolbarState.HasValue)
            {
                observedToolbarCommand = true;
                if (toolbarState.Value)
                    return;
            }

            // Some legacy toolbar layouts omit the command entirely; give their panel selection a bounded idle turn before dispatching.
            if (!observedToolbarCommand && DateTime.UtcNow >= deadline - TimeSpan.FromSeconds(4))
                return;

            // Selection changes are published from the panel's next idle cycle, which can be slower when a secondary volume is first enumerated.
            Thread.Sleep(100);
        }

        Assert.Fail($"Visible toolbar command {command} remained disabled after selecting the sandbox item.");
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

            // The unsupported-volume move case must be visible in the panel before
            // it opens, otherwise its quick-search can select a seeded collision file.
            var unsupportedTargetSource = SourcePath("ads-unsupported-target.txt");
            File.WriteAllText(unsupportedTargetSource, "ads-unsupported-default-content");
            AlternateDataStreams.Write(unsupportedTargetSource, "must-not-silently-disappear", "source-stream-content"u8.ToArray());
        }
        catch (IOException)
        {
            // The volume does not support streams; the affected cases skip themselves.
        }
    }

    /// <summary>Creates the ANSI-round-trippable file identities before panel enumeration.</summary>
    public void SeedAnsiRoundTrippableUnicodeAndLongPathFixtures()
    {
        const string treeName = "unicode-long-tree";
        const string longSegment = "long-unicode-\u00e9-segment-123456789012345678901234567890";
        const string payloadName = "payload-\u00e9.txt";
        const int productPathMaximumLength = 247;
        var longPathBase = Path.Combine(TargetDirectory, treeName);
        var remainingPathLength = productPathMaximumLength - longPathBase.Length - payloadName.Length - 1;
        var longSegmentCount = Math.Max(1, remainingPathLength / (longSegment.Length + 1));
        var longRelativePath = string.Join(Path.DirectorySeparatorChar.ToString(),
            Enumerable.Repeat(longSegment, longSegmentCount));
        var longSource = SourcePath(Path.Combine(treeName, longRelativePath, payloadName));

        // Seed all names before launch so the legacy panel cannot race the filesystem watcher.
        File.WriteAllText(SourcePath("first-unicode-\u00e9.txt"), "first-unicode-content");
        File.WriteAllText(SourcePath("second-unicode-\u00f6.txt"), "second-unicode-content");
        Directory.CreateDirectory(Path.GetDirectoryName(longSource)!);
        File.WriteAllText(longSource, "long-unicode-content");
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
                DeleteFileAllowingReadOnly(child);
        }

        Directory.Delete(path);
    }

    internal static void DeleteFileAllowingReadOnly(string path)
    {
        // Change Attributes tests leave Read-only files that File.Delete otherwise refuses.
        var attributes = File.GetAttributes(path);
        if ((attributes & FileAttributes.ReadOnly) != 0)
            File.SetAttributes(path, attributes & ~FileAttributes.ReadOnly);
        File.Delete(path);
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
