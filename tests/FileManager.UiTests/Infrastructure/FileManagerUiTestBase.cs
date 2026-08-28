using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Exceptions;
using FlaUI.UIA3;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

[NonParallelizable]
public abstract class FileManagerUiTestBase
{
    private readonly List<Application> launchedApplications = [];
    private readonly HashSet<int> launchedCrashReporterIds = [];
    private UiTestExecutionLog? executionLog;
    private bool productDiagnosticsCaptured;

    protected UIA3Automation Automation { get; private set; } = null!;
    protected Application Application { get; private set; } = null!;
    protected Window MainWindow { get; private set; } = null!;
    // Cache the native owner while UIA is responsive so restart-heavy fixtures do not query a stale provider later.
    protected nint NativeMainWindowHandle { get; private set; }

    protected virtual string ApplicationArguments => UiTestSettings.Arguments;

    // Recovery fixtures deliberately start behind a modal prompt; ordinary fixtures still require an interactive main window.
    protected virtual bool AllowDisabledMainWindowDuringStartup => false;

    [SetUp]
    public void StartFileManager()
    {
        UiTestSettings.RequireTestSandbox();
        productDiagnosticsCaptured = false;
        var test = TestContext.CurrentContext.Test;
        // Start outside the disposable sandbox so cleanup cannot erase the diagnostic needed to explain a setup failure.
        executionLog = UiTestExecutionLog.Start(UiTestSettings.ExecutionTranscriptRoot, test.Name, test.FullName, test.ID);
        UiTestTrace.Attach(executionLog);
        try
        {
            UiTestSandbox.EnsureInitialized();
            // ApplicationArguments may construct a workspace, so clear stale test data before resolving its source and target paths.
            executionLog.Record("HARNESS", $"test.start executable=\"{UiTestSettings.ExecutablePath}\" arguments=\"{ApplicationArguments}\"");
            executionLog.Record("SANDBOX", $"initialized data-root=\"{UiTestSettings.TestDataRoot}\" registry-root=\"{UiTestSettings.ConfigurationRegistryRoot}\"");
            executionLog.WatchProductDialogTranscript(Path.Combine(UiTestSettings.TestDataRoot, "ui-test-dialogs.log"));
            Automation = new UIA3Automation();
            BeforeFileManagerStarted();
            StartApplication();
        }
        catch (Exception exception)
        {
            executionLog.RecordException("SETUP-FAILURE", exception);
            FinishExecutionLog("setup.failed");
            throw;
        }
    }

    [TearDown]
    public void StopFileManager()
    {
        executionLog?.Record("NUNIT", BuildNUnitOutcomeDescription());
        try
        {
            StopFileManagerCore();
        }
        catch (Exception exception)
        {
            executionLog?.RecordException("TEARDOWN-FAILURE", exception);
            throw;
        }
        finally
        {
            FinishExecutionLog("teardown.complete");
        }
    }

    private void StopFileManagerCore()
    {
        executionLog?.CaptureCurrentWindows("teardown.before-process-stop");
        // Killing only processes launched by this fixture prevents a failed UI test from leaking instances.
        foreach (var application in launchedApplications)
        {
            try
            {
                if (!application.HasExited)
                {
                    UiTestTrace.Record("PROCESS", $"terminate-request role=salamand.exe pid={application.ProcessId}");
                    application.Kill();
                }
                else
                {
                    // Preserve the real exit code before disposing FlaUI's process wrapper, especially for crashes and fault injection.
                    UiTestTrace.Record("PROCESS", $"already-exited role=salamand.exe pid={application.ProcessId} code={application.ExitCode}");
                }
                // The native single-instance gate can otherwise hand the next test's launch to a process still shutting down.
                WaitForApplicationExit(application);
                var exitCode = application.HasExited ? application.ExitCode.ToString() : "<still-running>";
                UiTestTrace.Record("PROCESS", $"stopped role=salamand.exe pid={application.ProcessId} code={exitCode}");
            }
            catch (InvalidOperationException)
            {
                // A failed launch can detach FlaUI's Process object; it is already unavailable to this fixture.
            }
            application.Dispose();
        }

        foreach (var crashReporterId in launchedCrashReporterIds)
        {
            try
            {
                using var crashReporter = Process.GetProcessById(crashReporterId);
                if (!crashReporter.HasExited)
                {
                    UiTestTrace.Record("PROCESS", $"terminate-request role=salmon.exe pid={crashReporterId}");
                    crashReporter.Kill();
                }
            }
            catch (ArgumentException)
            {
                // The reporter already stopped with its FileManager parent.
            }
        }
        launchedCrashReporterIds.Clear();

        try
        {
            OnAfterFileManagerStopped();
        }
        finally
        {
            CaptureProductDiagnostics();
            // Preserve restart state within one test but always remove its mutations before NUnit starts the next case.
            UiTestSandbox.Cleanup();
        }

        // Setup can intentionally skip before UIA3 is created when the test sandbox is not configured.
        if (Automation is not null)
            Automation.Dispose();
    }

    protected void RestartFileManager(IReadOnlyDictionary<string, string>? environment = null)
    {
        UiTestTrace.Record("HARNESS", $"application.restart environment-overrides={DescribeEnvironmentNames(environment)}");
        // Restart coverage verifies that the application remains launchable after a committed configuration dialog.
        if (!Application.HasExited)
        {
            Application.Kill();
            // Do not let the next launch race the process-wide single-instance mutex.
            WaitForApplicationExit(Application);
        }

        StartApplication(environment);
    }

    protected Window OpenConfigurationDialog()
    {
        UiTestTrace.Record("HARNESS", "open-dialog name=Configuration");
        NativeCommands.OpenConfiguration(NativeMainWindowHandle);
        // The localized configuration page keeps the stable property-sheet caption in the English test language.
        return WaitForWindow(window => string.Equals(window.Title, "Configuration", StringComparison.Ordinal));
    }

    protected void CloseConfigurationDialog(Window dialog, bool commit)
    {
        // Standard Win32 property sheets retain IDOK/IDCANCEL even when their buttons are absent from the legacy UIA tree.
        NativeCommands.CloseStandardDialog(dialog.Properties.NativeWindowHandle.Value, commit);
        WaitForWindowToClose(dialog);
    }

    protected void CommitConfigurationDialogWithoutWaiting(Window dialog)
    {
        // Fault injection terminates the process during the deferred native save, so waiting for
        // the dialog's normal close is not meaningful.  The caller waits for the process instead.
        NativeCommands.CloseStandardDialog(dialog.Properties.NativeWindowHandle.Value, commit: true);
    }

    protected int WaitForFileManagerExit(TimeSpan timeout)
    {
        UiTestTrace.Record("WAIT", $"application.exit pid={Application.ProcessId} timeout={timeout.TotalSeconds:0.###}s");
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (Application.HasExited)
            {
                UiTestTrace.Record("WAIT", $"application.exit satisfied pid={Application.ProcessId} code={Application.ExitCode}");
                return Application.ExitCode;
            }

            Thread.Sleep(50);
        }

        Assert.Fail("FileManager did not terminate at the requested configuration write boundary.");
        return 0;
    }

    protected bool ToggleFirstConfigurationCheckBox(Window dialog)
    {
        // This legacy property sheet omits checkbox UIA patterns, so use its stable General-page control ID for the persistence mutation.
        return NativeCommands.ToggleConfigurationClearReadOnlyCheckBox(dialog.Properties.NativeWindowHandle.Value);
    }

    protected bool IsFirstConfigurationCheckBoxChecked(Window dialog)
    {
        // Read the same native control used for the mutation to prove the committed value was reloaded after restart.
        return NativeCommands.IsConfigurationClearReadOnlyCheckBoxChecked(dialog.Properties.NativeWindowHandle.Value);
    }

    protected void WaitForConfigurationClearReadOnlyPersistence(bool expectedValue)
    {
        UiTestTrace.Record("WAIT", $"configuration.persistence setting=ClearReadOnly expected={expectedValue} timeout=10s");
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (TryReadPersistedConfigurationClearReadOnly(out var value) && value == expectedValue)
            {
                UiTestTrace.Record("WAIT", "configuration.persistence satisfied setting=ClearReadOnly");
                return;
            }

            Thread.Sleep(50);
        }

        Assert.Fail("The accepted Configuration dialog did not commit its isolated registry generation before restart.");
    }

    protected void WaitForFtpBookmarkPersistence(string bookmarkName)
    {
        UiTestTrace.Record("WAIT", $"ftp-bookmark.persistence name=\"{bookmarkName}\" timeout=10s");
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (TryFindPersistedFtpBookmark(bookmarkName))
            {
                UiTestTrace.Record("WAIT", "ftp-bookmark.persistence satisfied");
                return;
            }

            Thread.Sleep(50);
        }

        Assert.Fail("The accepted FTP organizer did not commit its bookmark to the isolated registry generation before restart.");
    }

    protected bool ReadPersistedConfigurationClearReadOnly()
    {
        // Fault-injection trials inspect the active generation directly so opening a read-only dialog cannot trigger unrelated UI lifecycle races.
        Assert.That(TryReadPersistedConfigurationClearReadOnly(out var value), Is.True,
                    "The isolated profile does not contain a complete active Configuration generation.");
        return value;
    }

    private static bool TryReadPersistedConfigurationClearReadOnly(out bool value)
    {
        // A value is usable only when both the generation pointer and its target key are present.
        using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
        if (root?.GetValue("Active Generation") is int generation && generation is >= 0 and <= 1)
        {
            using var configuration = root.OpenSubKey($"Configuration Generations\\Generation {generation}\\Configuration");
            if (configuration?.GetValue("Clear Readonly Attribute") is int persistedValue)
            {
                value = persistedValue != 0;
                return true;
            }
        }

        value = default;
        return false;
    }

    private static bool TryFindPersistedFtpBookmark(string bookmarkName)
    {
        // Inspect the committed generation, not the live plug-in list, so a forced
        // restart cannot hide a queued host transaction behind an open dialog state.
        using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
        if (root?.GetValue("Active Generation") is not int generation || generation is < 0 or > 1)
            return false;

        using var bookmarks = root.OpenSubKey($"Configuration Generations\\Generation {generation}\\Plugins Configuration\\FTP\\Bookmarks");
        if (bookmarks is null)
            return false;

        foreach (var bookmarkKeyName in bookmarks.GetSubKeyNames())
        {
            using var bookmark = bookmarks.OpenSubKey(bookmarkKeyName);
            if (string.Equals(bookmark?.GetValue("Name") as string, bookmarkName, StringComparison.Ordinal))
                return true;
        }

        return false;
    }

    protected Window OpenFtpBookmarksDialog()
    {
        UiTestTrace.Record("HARNESS", "open-dialog name=FTP-Organize-Bookmarks");
        RequireFtpPluginRuntime();
        // FTP IDs are process-local, so wait for the instance under test rather than reusing a predecessor's SUID.
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value,
                               WaitForFtpPluginCommand(pluginCommand: 7, "Organize Bookmarks"));
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    protected Window OpenFtpConnectDialog()
    {
        UiTestTrace.Record("HARNESS", "open-dialog name=FTP-Connect");
        RequireFtpPluginRuntime();
        // The quick-connect SUID must come from this launch for the protocol fixture to drive the real plug-in command.
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value,
                               WaitForFtpPluginCommand(pluginCommand: 1, "Connect to FTP Server"));
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    protected void ConnectFtpServer(Window connectDialog, string hostAddress)
    {
        UiTestTrace.Record("HARNESS", $"ftp-connect set-host=\"{hostAddress}\" invoke-connect=true");
        // IDE_HOSTADDRESS is a stable plug-in resource ID; host:port preserves the production parser path.
        var hostBox = connectDialog.FindFirstDescendant(cf => cf.ByAutomationId("563"))?.AsComboBox();
        Assert.That(hostBox, Is.Not.Null, "FTP connect dialog did not expose its host-address field.");
        hostBox!.Value = hostAddress;

        var connectButton = connectDialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(connectButton, Is.Not.Null, "FTP connect dialog did not expose its Connect button.");
        connectButton!.Invoke();
    }

    protected void CreateFtpBookmark(Window bookmarksDialog, string bookmarkName)
    {
        UiTestTrace.Record("HARNESS", $"ftp-bookmark create name=\"{bookmarkName}\"");
        // These resource IDs are stable plug-in control identities, allowing UIA3 to create a bookmark without display text coupling.
        var newButton = bookmarksDialog.FindFirstDescendant(cf => cf.ByAutomationId("572"))?.AsButton();
        Assert.That(newButton, Is.Not.Null, "FTP bookmarks dialog did not expose its New button.");
        newButton!.Invoke();

        var nameDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.Properties.NativeWindowHandle.Value != bookmarksDialog.Properties.NativeWindowHandle.Value);
        var nameBox = nameDialog.FindFirstDescendant(cf => cf.ByAutomationId("602"))?.AsTextBox();
        Assert.That(nameBox, Is.Not.Null, "New FTP bookmark dialog did not expose its name field.");
        nameBox!.Text = bookmarkName;

        var okButton = nameDialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(okButton, Is.Not.Null, "New FTP bookmark dialog did not expose its OK button.");
        okButton!.Invoke();
        WaitForWindowToClose(nameDialog);
    }

    protected void RenameFocusedFtpBookmark(Window bookmarksDialog, string bookmarkName)
    {
        UiTestTrace.Record("HARNESS", $"ftp-bookmark rename-focused name=\"{bookmarkName}\"");
        // Creating a bookmark focuses it, so Rename exercises the profile-edit commit without relying on list item text.
        var renameButton = bookmarksDialog.FindFirstDescendant(cf => cf.ByAutomationId("573"))?.AsButton();
        Assert.That(renameButton, Is.Not.Null, "FTP bookmarks dialog did not expose its Rename button.");
        renameButton!.Invoke();

        var nameDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.Properties.NativeWindowHandle.Value != bookmarksDialog.Properties.NativeWindowHandle.Value);
        var nameBox = nameDialog.FindFirstDescendant(cf => cf.ByAutomationId("602"))?.AsTextBox();
        Assert.That(nameBox, Is.Not.Null, "Rename FTP bookmark dialog did not expose its name field.");
        nameBox!.Text = bookmarkName;

        var okButton = nameDialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(okButton, Is.Not.Null, "Rename FTP bookmark dialog did not expose its OK button.");
        okButton!.Invoke();
        WaitForWindowToClose(nameDialog);
    }

    protected void CloseFtpBookmarksDialog(Window bookmarksDialog)
    {
        UiTestTrace.Record("HARNESS", "ftp-bookmark close-and-commit");
        // Close commits the edited bookmark collection in the FTP organizer instead of discarding it with Cancel.
        var closeButton = bookmarksDialog.FindFirstDescendant(cf => cf.ByAutomationId("575"))?.AsButton();
        Assert.That(closeButton, Is.Not.Null, "FTP bookmarks dialog did not expose its Close button.");
        closeButton!.Invoke();
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

    private void StartApplication(IReadOnlyDictionary<string, string>? environment = null)
    {
        var reportersBeforeLaunch = GetTestCrashReporterIds();
        ResetPluginCommandMap();
        var executableDirectory = Path.GetDirectoryName(UiTestSettings.ExecutablePath);
        if (string.IsNullOrWhiteSpace(executableDirectory))
            throw new InvalidOperationException("The FileManager executable must have a containing directory.");
        EnsureCrashReporterIsStaged(executableDirectory);
        var startInfo = new ProcessStartInfo(UiTestSettings.ExecutablePath, ApplicationArguments)
        {
            UseShellExecute = false,
            // Plug-ins sit beside the staged executable; the test-host directory does not contain their relative layout.
            WorkingDirectory = executableDirectory,
        };
        // Every child process receives the same data and configuration boundaries as the test host.
        startInfo.Environment["FILEMANAGER_UI_TESTDATA_ROOT"] = UiTestSettings.TestDataRoot;
        startInfo.Environment["FILEMANAGER_UI_CONFIG_ROOT"] = UiTestSettings.ConfigurationRegistryRoot;
        startInfo.Environment["TEMP"] = Path.Combine(UiTestSettings.TestDataRoot, "temp");
        startInfo.Environment["TMP"] = Path.Combine(UiTestSettings.TestDataRoot, "temp");
        if (environment is not null)
        {
            foreach (var (name, value) in environment)
                startInfo.Environment[name] = value;
        }

        UiTestTrace.Record("PROCESS",
                           $"launch role=salamand.exe path=\"{startInfo.FileName}\" arguments=\"{startInfo.Arguments}\" " +
                           $"working-directory=\"{startInfo.WorkingDirectory}\" environment-overrides={DescribeEnvironmentNames(environment)}");
        Application = FlaUI.Core.Application.Launch(startInfo);
        launchedApplications.Add(Application);
        executionLog?.ObserveProcess(Application.ProcessId, "salamand.exe");
        UiTestTrace.Record("PROCESS", $"launched role=salamand.exe pid={Application.ProcessId}");
        MainWindow = WaitForNativeMainWindow(reportersBeforeLaunch);
        // A Debug FileManager starts salmon.exe beside itself; track only new sibling reporters for fixture teardown.
        TrackCrashReporters(reportersBeforeLaunch);
        executionLog?.CaptureCurrentWindows("main-window-ready");
    }

    private void TrackCrashReporters(IReadOnlySet<int> reportersBeforeLaunch)
    {
        // Register reporters as soon as they appear so startup crashes and their modal UI are not lost behind the main-window wait.
        foreach (var reporterId in GetTestCrashReporterIds().Except(reportersBeforeLaunch))
        {
            if (launchedCrashReporterIds.Add(reporterId))
                executionLog?.ObserveProcess(reporterId, "salmon.exe");
        }
    }

    private static void ResetPluginCommandMap()
    {
        // Deleting this harness-owned map before launch prevents a stopped instance from satisfying the next instance's readiness check.
        File.Delete(UiTestSettings.PluginCommandMapPath);
    }

    private static void EnsureCrashReporterIsStaged(string executableDirectory)
    {
        var crashReporterPath = Path.Combine(executableDirectory, "salmon.exe");
        // Fail before launch when the sibling reporter is absent, because FileManager otherwise blocks the test session with a native modal error.
        Assert.That(File.Exists(crashReporterPath), Is.True,
            $"FileManager UI tests require salmon.exe beside salamand.exe. Run scripts\\runtests.ps1 to stage the complete Debug x64 test artifact. Missing: {crashReporterPath}");
    }

    private static void RequireFtpPluginRuntime()
    {
        var runtimeRoot = Path.GetDirectoryName(UiTestSettings.ExecutablePath) ?? string.Empty;
        var requiredFiles = new[]
        {
            Path.Combine(runtimeRoot, "plugins", "ftp", "ftp.spl"),
            Path.Combine(runtimeRoot, "plugins", "ftp", "lang", "english.slg"),
        };
        var missingFiles = requiredFiles.Where(path => !File.Exists(path)).ToArray();
        if (missingFiles.Length != 0)
        {
            // An absent plug-in means this scenario was not exercised; do not misreport its missing dynamic command as an application defect.
            Assert.Ignore($"FTP UI tests require a complete deployed FTP runtime. Missing: {string.Join(", ", missingFiles)}");
        }
    }

    private int WaitForFtpPluginCommand(int pluginCommand, string commandName)
    {
        UiTestTrace.Record("WAIT", $"ftp-command-map plugin-command={pluginCommand} name=\"{commandName}\" timeout=20s");
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        var lastObservedRecords = Array.Empty<string>();
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                lastObservedRecords = File.ReadLines(UiTestSettings.PluginCommandMapPath).TakeLast(12).ToArray();
                foreach (var line in lastObservedRecords)
                {
                    var fields = line.Split('|');
                    if (fields.Length == 4 &&
                        int.TryParse(fields[0], out var processId) && processId == Application.ProcessId &&
                        string.Equals(Path.GetFileName(fields[1]), "ftp.spl", StringComparison.OrdinalIgnoreCase) &&
                        int.TryParse(fields[2], out var loggedPluginCommand) && loggedPluginCommand == pluginCommand &&
                        int.TryParse(fields[3], out var salamanderCommand) && salamanderCommand > 0)
                    {
                        UiTestTrace.Record("WAIT", $"ftp-command-map satisfied salamander-command={salamanderCommand}");
                        return salamanderCommand;
                    }
                }
            }
            catch (IOException)
            {
                // The native process appends the map while it loads plug-ins; retry its shared file on the next short poll.
            }

            Thread.Sleep(100);
        }

        // Include the bounded transcript so a future plug-in protocol mismatch is actionable rather than a generic readiness timeout.
        var records = lastObservedRecords.Length == 0 ? "<none>" : string.Join(", ", lastObservedRecords);
        Assert.Fail($"FileManager process {Application.ProcessId} did not register the FTP {commandName} command before it was invoked. Observed records: {records}.");
        return 0;
    }

    private static HashSet<int> GetTestCrashReporterIds()
    {
        var directory = Path.GetDirectoryName(UiTestSettings.ExecutablePath);
        if (string.IsNullOrWhiteSpace(directory))
            return [];
        var expectedPath = Path.Combine(directory, "salmon.exe");
        var reporterIds = new HashSet<int>();
        foreach (var process in Process.GetProcessesByName("salmon"))
        {
            try
            {
                if (string.Equals(process.MainModule?.FileName, expectedPath, StringComparison.OrdinalIgnoreCase))
                    reporterIds.Add(process.Id);
            }
            catch (Exception ex) when (ex is InvalidOperationException || ex is System.ComponentModel.Win32Exception)
            {
                // A reporter that exited or changed state during enumeration cannot be owned by the active fixture anymore.
            }
            finally
            {
                process.Dispose();
            }
        }
        return reporterIds;
    }

    private Window WaitForNativeMainWindow(IReadOnlySet<int> reportersBeforeLaunch)
    {
        // The first start against a fresh profile auto-installs every bundled
        // plug-in before the main window becomes usable, which can take far longer
        // than a warm start. Twenty seconds turned that into an intermittent
        // "did not expose its main window" failure on the first case of a run.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(60);
        while (DateTime.UtcNow < timeout)
        {
            TrackCrashReporters(reportersBeforeLaunch);
            // UIA's application view can omit a modal Win32 error window, so acknowledge the known open-source plug-in notice natively.
            NativeCommands.DismissKnownStartupErrorDialogs(Application.ProcessId);
            var windows = Application.GetAllTopLevelWindows(Automation);
            Window? languageDialog = null;
            foreach (var window in windows)
            {
                try
                {
                    if (string.Equals(window.Properties.ClassName.ValueOrDefault, "SalamanderMainWindowVer25", StringComparison.Ordinal))
                    {
                        // Ordinary fixtures wait for an interactive owner; recovery fixtures explicitly opt into its disabled state.
                        if (window.IsEnabled || AllowDisabledMainWindowDuringStartup)
                        {
                            NativeMainWindowHandle = window.Properties.NativeWindowHandle.Value;
                            return window;
                        }
                        continue;
                    }

                    if (string.Equals(window.Properties.ClassName.ValueOrDefault, "#32770", StringComparison.Ordinal) &&
                        string.Equals(window.Title, "Open Salamander", StringComparison.Ordinal))
                        languageDialog = window;

                }
                catch (COMException)
                {
                    // Restart/fault-injection tests can enumerate a window after its UIA provider has gone stale.
                }
            }
            if (languageDialog is not null)
            {
                // A fresh test registry deliberately prompts for its bundled default language before creating the versioned main window.
                NativeCommands.AcceptStartupLanguage(languageDialog.Properties.NativeWindowHandle.Value);
            }

            Thread.Sleep(100);
        }

        throw new AssertionException(BuildStartupFailureMessage());
    }

    private string BuildStartupFailureMessage()
    {
        var processState = "unavailable";
        try
        {
            using var process = Process.GetProcessById(Application.ProcessId);
            processState = process.HasExited
                ? $"exited with code {process.ExitCode}"
                : $"running (responding={process.Responding})";
        }
        catch (Exception exception) when (exception is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            // A verifier-terminated process can disappear while its exit state is being queried.
            processState = "exited before its exit code could be collected";
        }

        var windows = NativeCommands.GetTopLevelWindowDescriptions(Application.ProcessId);
        // Verifier startup failures otherwise collapse into an opaque UIA timeout; preserve native state for the diagnostic artifact.
        return "FileManager did not expose its SalamanderMainWindowVer25 UI Automation main window. " +
               $"Process {Application.ProcessId} was {processState}. Visible native windows: " +
               (windows.Count == 0 ? "<none>." : string.Join("; ", windows));
    }

    protected Window WaitForWindow(Func<Window, bool> predicate)
    {
        UiTestTrace.Record("WAIT", "window.open timeout=10s");
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            // Convert handles one at a time because one stale provider must not abort discovery of another live dialog.
            Window? dialog = null;
            foreach (var windowHandle in NativeCommands.GetTopLevelWindows(Application.ProcessId))
            {
                // Modal CMessageBox leaves the owner's UIA provider timing out; callers already exclude the main window by handle.
                if (NativeMainWindowHandle != 0 && windowHandle == NativeMainWindowHandle)
                    continue;
                try
                {
                    var candidate = Automation.FromHandle(windowHandle).AsWindow();
                    if (predicate(candidate))
                    {
                        dialog = candidate;
                        break;
                    }
                }
                catch (Exception ex) when (ex is ElementNotAvailableException || ex is COMException || ex is TimeoutException)
                {
                    // A native window can close or its UIA provider can detach between enumeration and conversion; retry the remaining live handles.
                }
            }
            if (dialog is not null)
            {
                UiTestTrace.Record("WAIT",
                                   $"window.open satisfied hwnd=0x{dialog.Properties.NativeWindowHandle.Value:X} title=\"{dialog.Title}\"");
                return dialog;
            }

            Thread.Sleep(100);
        }

        // Naming what was on screen instead separates "the prompt never appeared"
        // from "the prompt appeared but did not match", which the bare timeout
        // message left indistinguishable.
        var titles = string.Join(", ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId));
        Assert.Fail($"Timed out waiting for the requested FileManager window. Open FileManager windows: {(titles.Length == 0 ? "none" : titles)}.");
        return null!;
    }

    protected static void WaitForWindowToClose(Window dialog)
    {
        // Capture the native handle before closing because legacy UIA providers can time out while reading stale properties.
        nint dialogHandle;
        try
        {
            dialogHandle = dialog.Properties.NativeWindowHandle.Value;
        }
        catch (COMException)
        {
            // A viewer can destroy its UIA provider before Close returns; its unavailable handle is already closed.
            return;
        }
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        UiTestTrace.Record("WAIT", $"window.close hwnd=0x{dialogHandle:X} timeout=10s");
        while (DateTime.UtcNow < timeout)
        {
            if (!NativeCommands.WindowExists(dialogHandle))
            {
                UiTestTrace.Record("WAIT", $"window.close satisfied hwnd=0x{dialogHandle:X}");
                return;
            }

            Thread.Sleep(100);
        }

        Assert.Fail("Timed out waiting for the configuration dialog to close.");
    }

    private static void WaitForApplicationExit(Application application)
    {
        try
        {
            using var process = Process.GetProcessById(application.ProcessId);
            process.WaitForExit(10_000);
        }
        catch (ArgumentException)
        {
            // A process that has already exited has released the single-instance gate needed by the next test.
        }
    }

    private void CaptureProductDiagnostics()
    {
        if (executionLog is null || productDiagnosticsCaptured)
            return;

        // Capture before sandbox cleanup because native dialog and crash files deliberately live under that disposable root.
        try
        {
            executionLog.CaptureProductDiagnostics(UiTestSettings.TestDataRoot);
        }
        catch (Exception exception)
        {
            // Diagnostics must explain a product failure without becoming a second teardown failure themselves.
            executionLog.RecordException("DIAGNOSTIC-CAPTURE-FAILURE", exception);
        }
        finally
        {
            productDiagnosticsCaptured = true;
        }
    }

    private void FinishExecutionLog(string lifecycleEvent)
    {
        var log = executionLog;
        if (log is null)
            return;

        try
        {
            CaptureProductDiagnostics();
        }
        catch (Exception exception)
        {
            log.RecordException("DIAGNOSTIC-CAPTURE-FAILURE", exception);
        }

        log.Record("HARNESS", lifecycleEvent);
        UiTestTrace.Detach(log);
        var transcriptPath = log.Path;
        try
        {
            log.Dispose();
            // NUnit carries this absolute path into TRX-aware runners while the file also remains directly readable.
            TestContext.AddTestAttachment(transcriptPath, "Open Salamander UI execution transcript");
        }
        catch (Exception exception)
        {
            TestContext.Progress.WriteLine($"Could not finalize UI execution transcript '{transcriptPath}': {exception.Message}");
        }
        finally
        {
            executionLog = null;
        }
    }

    private static string BuildNUnitOutcomeDescription()
    {
        var result = TestContext.CurrentContext.Result;
        var message = string.IsNullOrWhiteSpace(result.Message)
            ? "<none>"
            : result.Message.Replace("\r", " ", StringComparison.Ordinal).Replace("\n", " ", StringComparison.Ordinal);
        var label = string.IsNullOrWhiteSpace(result.Outcome.Label) ? "<none>" : result.Outcome.Label;
        // Keep the assertion location beside the final UI state instead of requiring a separate console or TRX lookup.
        var stackTrace = string.IsNullOrWhiteSpace(result.StackTrace) ? "<none>" : result.StackTrace;
        return $"test-body outcome={result.Outcome.Status} label={label} message=\"{message}\"\nstack-trace:\n{stackTrace}";
    }

    private static string DescribeEnvironmentNames(IReadOnlyDictionary<string, string>? environment)
    {
        // Values may contain credentials or fault payloads; names are sufficient to explain launch shape safely.
        return environment is null || environment.Count == 0
            ? "<none>"
            : string.Join(",", environment.Keys.Order(StringComparer.Ordinal));
    }
}
