using System.Diagnostics;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using FlaUI.UIA3;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

[NonParallelizable]
public abstract class FileManagerUiTestBase
{
    private const int ConfigurationCheckBoxId = 304; // IDC_CLEARREADONLY is persisted on the General page.
    private readonly List<Application> launchedApplications = [];
    private readonly HashSet<int> preexistingHelperProcessIds = [];
    private long configurationGenerationBeforeOpen;

    protected UIA3Automation Automation { get; private set; } = null!;
    protected Application Application { get; private set; } = null!;
    protected Window MainWindow { get; private set; } = null!;
    // Keep the authoritative HWND outside UIA so forced-exit recovery loops never query a disconnected COM element.
    protected nint MainWindowHandle { get; private set; }

    protected virtual string ApplicationArguments => UiTestSettings.Arguments;

    [SetUp]
    public void StartFileManager()
    {
        UiTestSettings.RequireIsolatedProfile();
        // Snapshot helpers before launch so cleanup never terminates a process owned by another test or user session.
        preexistingHelperProcessIds.Clear();
        foreach (var helper in Process.GetProcessesByName("salmon"))
        {
            using (helper)
                preexistingHelperProcessIds.Add(helper.Id);
        }
        Automation = new UIA3Automation();
        try
        {
            BeforeFileManagerStarted();
            StartApplication();
        }
        catch
        {
            // NUnit does not guarantee this fixture's TearDown after a failed SetUp, so release launched processes here.
            CleanupFixture();
            throw;
        }
    }

    [TearDown]
    public void StopFileManager()
    {
        CleanupFixture();
    }

    private void CleanupFixture()
    {
        // Run every cleanup layer even if process shutdown or fixture-specific disposal reports a failure.
        try
        {
            StopLaunchedProcesses();
        }
        finally
        {
            try
            {
                OnAfterFileManagerStopped();
            }
            finally
            {
                // Setup can intentionally skip before UIA3 is created when the isolated profile opt-in is absent.
                Automation?.Dispose();
            }
        }
    }

    private void StopLaunchedProcesses()
    {
        // Killing only processes launched by this fixture prevents a failed UI test from leaking application instances.
        foreach (var application in launchedApplications)
            StopAndDisposeApplication(application);
        launchedApplications.Clear();

        var expectedHelperPath = Path.Combine(Path.GetDirectoryName(UiTestSettings.ExecutablePath)!, "salmon.exe");
        foreach (var helper in Process.GetProcessesByName("salmon"))
        {
            using (helper)
            {
                if (preexistingHelperProcessIds.Contains(helper.Id) || helper.HasExited)
                    continue;

                // Match the executable directory as well as the PID snapshot before terminating a test-owned helper.
                if (!string.Equals(helper.MainModule?.FileName, expectedHelperPath, StringComparison.OrdinalIgnoreCase))
                    continue;

                helper.Kill(entireProcessTree: true);
                helper.WaitForExit(5000);
            }
        }
    }

    protected void RestartFileManager(IReadOnlyDictionary<string, string>? environment = null,
                                      Action? afterPreviousApplicationStopped = null)
    {
        StopCurrentApplication(afterPreviousApplicationStopped);
        // Fault recovery can restart thousands of crashed processes; recycle COM/UIA state so dead event subscribers cannot accumulate.
        Automation.Dispose();
        Automation = new UIA3Automation();
        MainWindowHandle = 0;
        MainWindow = null!;

        StartApplication(environment);
    }

    protected void StopCurrentApplication(Action? afterApplicationStopped = null)
    {
        var application = Application;
        StopAndDisposeApplication(application);
        launchedApplications.Remove(application);
        // External state is replaced only after the process that owns it has exited.
        afterApplicationStopped?.Invoke();
    }

    private static void StopAndDisposeApplication(Application application)
    {
        // One bounded shutdown path keeps normal teardown and crash-recovery restarts consistent.
        if (!application.HasExited)
            application.Kill();

        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (!application.HasExited && DateTime.UtcNow < deadline)
            Thread.Sleep(25);
        if (!application.HasExited)
            throw new InvalidOperationException("FileManager did not exit during test cleanup.");
        application.Dispose();
    }

    protected Window OpenConfigurationDialog()
    {
        configurationGenerationBeforeOpen = NativeCommands.GetConfigurationGeneration(MainWindowHandle);
        NativeCommands.Execute(MainWindowHandle, NativeCommands.Configuration);
        // CTreePropDialog uses stable custom OK ID 5, tree ID 1, and standard Cancel ID 2.
        var dialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
            window.FindFirstDescendant(cf => cf.ByControlType(ControlType.Tree)) is not null &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 5) &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 2));
        // Always activate General so configuration tests do not depend on LastFocusedPage from an earlier run.
        var dialogHandle = dialog.Properties.NativeWindowHandle.Value;
        NativeCommands.SelectFirstTreePage(dialogHandle, 1);
        WaitForConfigurationCheckBox(dialogHandle);
        return dialog;
    }

    protected void CloseConfigurationDialog(Window dialog, bool commit)
    {
        // The tree-property dialog maps its custom OK control to ID 5 and retains IDCANCEL as ID 2.
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, commit ? 5 : 2);
        WaitForWindowToClose(dialog);
        WaitForMainWindowEnabled();
        WaitForConfigurationCompletion();
    }

    protected void CommitConfigurationDialogWithoutWaiting(Window dialog)
    {
        // Fault injection terminates the process during the deferred native save, so waiting for
        // the dialog's normal close is not meaningful.  The caller waits for the process instead.
        NativeCommands.ClickDialogButton(dialog.Properties.NativeWindowHandle.Value, 5);
    }

    protected int WaitForFileManagerExit(TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (Application.HasExited)
                return Application.ExitCode;

            Thread.Sleep(50);
        }

        Assert.Fail("FileManager did not terminate at the requested configuration write boundary.");
        return 0;
    }

    protected bool ToggleFirstConfigurationCheckBox(Window dialog)
    {
        var dialogHandle = dialog.Properties.NativeWindowHandle.Value;
        // Read and change the real HWND state so a stale UIA ToggleState cannot create a false persistence failure.
        var originalState = NativeCommands.GetDialogCheckBoxState(dialogHandle, ConfigurationCheckBoxId);
        NativeCommands.ToggleDialogCheckBox(dialogHandle, ConfigurationCheckBoxId);
        return originalState;
    }

    protected bool IsFirstConfigurationCheckBoxChecked(Window dialog)
    {
        // Inspect the same visible native control after relaunch to prove the persisted value was loaded.
        return NativeCommands.GetDialogCheckBoxState(dialog.Properties.NativeWindowHandle.Value,
                                                      ConfigurationCheckBoxId);
    }

    protected Window OpenFtpBookmarksDialog()
    {
        // Resolve the host-owned SUID in the tested process so FTP cases never depend on a manually copied runtime value.
        var command = NativeCommands.GetFtpOrganizeBookmarksCommand(MainWindowHandle);
        if (command <= 0)
            Assert.Ignore("The tested FileManager build did not load the FTP plug-in required by this persistence test.");
        NativeCommands.Execute(MainWindowHandle, command);
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindowHandle);
    }

    protected void CreateFtpBookmark(Window bookmarksDialog, string bookmarkName)
    {
        EditFtpBookmarkName(bookmarksDialog, 572, bookmarkName, "New");
    }

    protected void RenameFocusedFtpBookmark(Window bookmarksDialog, string bookmarkName)
    {
        EditFtpBookmarkName(bookmarksDialog, 573, bookmarkName, "Rename");
    }

    private void EditFtpBookmarkName(Window bookmarksDialog, int commandButtonId, string bookmarkName, string operation)
    {
        // Stable plug-in resource IDs keep create and rename on one locale-independent dialog path.
        NativeCommands.ClickDialogButton(bookmarksDialog.Properties.NativeWindowHandle.Value, commandButtonId);
        var nameDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindowHandle &&
            window.Properties.NativeWindowHandle.Value != bookmarksDialog.Properties.NativeWindowHandle.Value);
        var nameBox = nameDialog.FindFirstDescendant(cf => cf.ByAutomationId("602"))?.AsTextBox();
        Assert.That(nameBox, Is.Not.Null, $"{operation} FTP bookmark dialog did not expose its name field.");
        nameBox!.Text = bookmarkName;
        NativeCommands.ClickDialogButton(nameDialog.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(nameDialog);
    }

    protected void CloseFtpBookmarksDialog(Window bookmarksDialog)
    {
        // Close commits the edited bookmark collection in the FTP organizer instead of discarding it with Cancel.
        NativeCommands.ClickDialogButton(bookmarksDialog.Properties.NativeWindowHandle.Value, 575);
        WaitForWindowToClose(bookmarksDialog);
    }

    protected virtual void OnAfterFileManagerStopped()
    {
    }

    // Some native integration scenarios must arrange durable state before the
    // executable reaches its startup reconciliation point.
    protected virtual void BeforeFileManagerStarted()
    {
    }

    protected virtual bool IsExpectedStartupModal(nint windowHandle)
    {
        return false;
    }

    protected Window[] GetFileManagerTopLevelWindows()
    {
        // Enumerate HWNDs first because UIA can omit native modal dialogs even while their owner is visibly disabled.
        return NativeCommands.GetTopLevelWindowHandles(Application.ProcessId)
            .Where(windowHandle => NativeCommands.GetWindowClassName(windowHandle) is
                "SalamanderMainWindowVer25" or "#32770")
            .Select(windowHandle => Automation.FromHandle(windowHandle).AsWindow())
            .ToArray();
    }

    private void StartApplication(IReadOnlyDictionary<string, string>? environment = null)
    {
        var startInfo = new ProcessStartInfo(UiTestSettings.ExecutablePath, ApplicationArguments)
        {
            UseShellExecute = false,
        };
        if (environment is not null)
        {
            foreach (var (name, value) in environment)
                startInfo.Environment[name] = value;
        }

        Application = FlaUI.Core.Application.Launch(startInfo);
        launchedApplications.Add(Application);

        // A clean profile can show the language selector first, so attach only to the native main-window class.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        var confirmedLanguageDialogs = new HashSet<nint>();
        var dismissedPathErrors = new HashSet<nint>();
        while (DateTime.UtcNow < timeout)
        {
            // Classify startup HWNDs natively so a broken dialog UIA provider cannot block the entire fixture setup.
            var windowHandles = NativeCommands.GetTopLevelWindowHandles(Application.ProcessId);
            var languageDialogHandle = windowHandles.FirstOrDefault(windowHandle =>
                NativeCommands.GetWindowClassName(windowHandle) == "#32770" &&
                NativeCommands.HasDialogControl(windowHandle, 1031));
            if (languageDialogHandle != 0)
            {
                if (confirmedLanguageDialogs.Add(languageDialogHandle))
                {
                    // IDOK accepts the application's preselected fallback without coupling startup to translated labels.
                    Assert.That(NativeCommands.HasDialogControl(languageDialogHandle, 1), Is.True,
                                "Language selector did not expose its OK button.");
                    NativeCommands.ClickDialogButton(languageDialogHandle, 1);
                }
            }

            var pathErrorDialogHandle = windowHandles.FirstOrDefault(windowHandle =>
                string.Equals(NativeCommands.GetWindowTitle(windowHandle), "Error Changing Directory",
                              StringComparison.Ordinal));
            if (pathErrorDialogHandle != 0)
            {
                if (dismissedPathErrors.Add(pathErrorDialogHandle))
                {
                    // File-operation cases persist disposable panel paths; acknowledge their next-launch invalid-path notice.
                    NativeCommands.ClickDialogButton(pathErrorDialogHandle, 1);
                }
            }

            var mainWindowHandle = windowHandles.FirstOrDefault(windowHandle =>
                string.Equals(NativeCommands.GetWindowClassName(windowHandle),
                              "SalamanderMainWindowVer25", StringComparison.Ordinal));
            var expectedStartupModal = windowHandles.Any(IsExpectedStartupModal);
            if (mainWindowHandle != 0 && languageDialogHandle == 0 &&
                (expectedStartupModal ||
                 NativeCommands.IsWindowEnabledForInput(mainWindowHandle) && NativeCommands.IsStartupReady(mainWindowHandle)))
            {
                // Native enumeration already requires visibility and avoids unsupported IsOffscreen providers.
                // The narrowly identified recovery modal appears before the normal readiness handshake and owns its disabled main window.
                MainWindowHandle = mainWindowHandle;
                MainWindow = Automation.FromHandle(mainWindowHandle).AsWindow();
                return;
            }

            if (Application.HasExited)
                throw new AssertionException($"FileManager exited before exposing its main window (exit code {Application.ExitCode}).");

            Thread.Sleep(100);
        }

        // Preserve timeout diagnostics without re-entering the UIA provider that may have caused startup to stall.
        var startupWindows = string.Join(Environment.NewLine,
            NativeCommands.GetTopLevelWindowHandles(Application.ProcessId).Select(windowHandle =>
                $"handle={windowHandle}, class='{NativeCommands.GetWindowClassName(windowHandle)}', " +
                $"title='{NativeCommands.GetWindowTitle(windowHandle)}', message='{NativeCommands.GetMessageBoxText(windowHandle)}', " +
                $"enabled={NativeCommands.IsWindowEnabledForInput(windowHandle)}, children=[{NativeCommands.DescribeChildControls(windowHandle)}]"));
        // Startup inventory separates a missing window from a visible application that never published readiness.
        throw new AssertionException($"FileManager did not expose a ready native main window within 20 seconds.{Environment.NewLine}{startupWindows}");
    }

    protected Window WaitForWindow(Func<Window, bool> predicate)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            var windows = GetFileManagerTopLevelWindows();
            var nativeAssertion = windows.FirstOrDefault(window =>
                NativeCommands.DescribeChildControls(window.Properties.NativeWindowHandle.Value)
                    .Contains("Debug Assertion Failed!", StringComparison.Ordinal));
            if (nativeAssertion is not null)
            {
                // A native assertion is an application failure, so report it immediately instead of masking it as a synchronization timeout.
                Assert.Fail($"FileManager triggered a native debug assertion.{Environment.NewLine}" +
                            NativeCommands.DescribeChildControls(nativeAssertion.Properties.NativeWindowHandle.Value));
            }

            var dialog = windows.FirstOrDefault(predicate);
            if (dialog is not null)
                return dialog;

            Thread.Sleep(100);
        }

        var visibleWindows = string.Join(Environment.NewLine, GetFileManagerTopLevelWindows().Select(window =>
            $"handle={window.Properties.NativeWindowHandle.Value}, class='{window.Properties.ClassName.ValueOrDefault}', " +
            $"title='{window.Title}', enabled={window.IsEnabled}, offscreen={window.Properties.IsOffscreen.ValueOrDefault}, " +
            $"message='{NativeCommands.GetMessageBoxText(window.Properties.NativeWindowHandle.Value)}', " +
            $"children=[{NativeCommands.DescribeChildControls(window.Properties.NativeWindowHandle.Value)}]"));
        // Native child inventory exposes assertion and error dialogs whose UIA provider omits their message text.
        Assert.Fail($"Timed out waiting for the requested FileManager window.{Environment.NewLine}{visibleWindows}");
        return null!;
    }

    protected static void WaitForWindowToClose(Window dialog)
    {
        var windowHandle = dialog.Properties.NativeWindowHandle.Value;
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            // UIA can retain the element after native destruction, so observe the actual HWND lifetime.
            if (!NativeCommands.IsWindowAvailable(windowHandle))
                return;

            Thread.Sleep(100);
        }

        Assert.Fail("Timed out waiting for the requested dialog to close.");
    }

    private void WaitForMainWindowEnabled()
    {
        var mainWindowHandle = MainWindowHandle;
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (NativeCommands.IsWindowEnabledForInput(mainWindowHandle))
                return;
            Thread.Sleep(100);
        }

        var remainingWindows = string.Join(Environment.NewLine, GetFileManagerTopLevelWindows().Select(window =>
            $"handle={window.Properties.NativeWindowHandle.Value}, class='{NativeCommands.GetWindowClassName(window.Properties.NativeWindowHandle.Value)}', " +
            $"title='{NativeCommands.GetWindowTitle(window.Properties.NativeWindowHandle.Value)}', enabled={window.IsEnabled}"));
        // Report the modal owner that kept the main window disabled after Configuration closed.
        Assert.Fail($"FileManager main window remained disabled after the modal dialog closed.{Environment.NewLine}{remainingWindows}");
    }

    private void WaitForConfigurationCompletion()
    {
        var mainWindowHandle = MainWindowHandle;
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (NativeCommands.GetConfigurationGeneration(mainWindowHandle) > configurationGenerationBeforeOpen)
                return;
            Thread.Sleep(100);
        }

        Assert.Fail("Configuration handler did not finish persistence after the dialog closed.");
    }

    private static void WaitForConfigurationCheckBox(nint dialogHandle)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTime.UtcNow < timeout)
        {
            if (NativeCommands.IsDialogControlVisible(dialogHandle, ConfigurationCheckBoxId))
                return;
            Thread.Sleep(50);
        }

        // A visible stable control proves the General page selection completed before state is read or changed.
        Assert.Fail("Configuration dialog did not activate its General page check box.");
    }
}
