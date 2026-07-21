using System.Diagnostics;
using FlaUI.Core;
using FlaUI.Core.AutomationElements;
using FlaUI.UIA3;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

[NonParallelizable]
public abstract class FileManagerUiTestBase
{
    private readonly List<Application> launchedApplications = [];

    protected UIA3Automation Automation { get; private set; } = null!;
    protected Application Application { get; private set; } = null!;
    protected Window MainWindow { get; private set; } = null!;

    [SetUp]
    public void StartFileManager()
    {
        UiTestSettings.RequireIsolatedProfile();
        Automation = new UIA3Automation();
        StartApplication();
    }

    [TearDown]
    public void StopFileManager()
    {
        // Killing only processes launched by this fixture prevents a failed UI test from leaking instances.
        foreach (var application in launchedApplications)
        {
            if (!application.HasExited)
                application.Kill();
            application.Dispose();
        }

        // Setup can intentionally skip before UIA3 is created when the isolated profile opt-in is absent.
        if (Automation is not null)
            Automation.Dispose();
    }

    protected void RestartFileManager()
    {
        // Restart coverage verifies that the application remains launchable after a committed configuration dialog.
        if (!Application.HasExited)
            Application.Kill();

        StartApplication();
    }

    protected Window OpenConfigurationDialog()
    {
        NativeCommands.OpenConfiguration(MainWindow.Properties.NativeWindowHandle.Value);
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    protected void CloseConfigurationDialog(Window dialog, bool commit)
    {
        // Standard Win32 property sheets expose IDOK as automation ID 1 and IDCANCEL as automation ID 2.
        var buttonId = commit ? "1" : "2";
        var button = dialog.FindFirstDescendant(cf => cf.ByAutomationId(buttonId))?.AsButton();
        Assert.That(button, Is.Not.Null, $"Configuration dialog did not expose button {buttonId}.");
        button!.Invoke();
        WaitForWindowToClose(dialog);
    }

    protected bool ToggleFirstConfigurationCheckBox(Window dialog)
    {
        // Toggling a visible option gives the persistence test a real user commit without relying on translated labels.
        var checkBox = dialog.FindAllDescendants()
            .FirstOrDefault(element => element.ControlType == FlaUI.Core.Definitions.ControlType.CheckBox)
            ?.AsCheckBox();
        Assert.That(checkBox, Is.Not.Null, "Configuration dialog did not expose a check box through UI Automation.");

        var originalState = checkBox!.IsChecked;
        // A three-state check box would not have a deterministic inverse for this basic persistence assertion.
        Assert.That(originalState, Is.Not.Null, "Configuration persistence test requires a two-state check box.");
        checkBox.Toggle();
        return originalState!.Value;
    }

    protected bool IsFirstConfigurationCheckBoxChecked(Window dialog)
    {
        // The same visible control is inspected after relaunch to prove the committed value was read from persisted settings.
        var checkBox = dialog.FindAllDescendants()
            .FirstOrDefault(element => element.ControlType == FlaUI.Core.Definitions.ControlType.CheckBox)
            ?.AsCheckBox();
        Assert.That(checkBox, Is.Not.Null, "Configuration dialog did not expose a check box through UI Automation.");
        var currentState = checkBox!.IsChecked;
        // Match the toggle helper so both sides of the restart assertion use a two-state value.
        Assert.That(currentState, Is.Not.Null, "Configuration persistence test requires a two-state check box.");
        return currentState!.Value;
    }

    protected Window OpenFtpBookmarksDialog()
    {
        // The host allocates FTP menu IDs dynamically, so the test profile provides the runtime command used to open this dialog.
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, UiTestSettings.RequireFtpOrganizeCommand());
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    protected void CreateFtpBookmark(Window bookmarksDialog, string bookmarkName)
    {
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
        // Close commits the edited bookmark collection in the FTP organizer instead of discarding it with Cancel.
        var closeButton = bookmarksDialog.FindFirstDescendant(cf => cf.ByAutomationId("575"))?.AsButton();
        Assert.That(closeButton, Is.Not.Null, "FTP bookmarks dialog did not expose its Close button.");
        closeButton!.Invoke();
        WaitForWindowToClose(bookmarksDialog);
    }

    private void StartApplication()
    {
        var startInfo = new ProcessStartInfo(UiTestSettings.ExecutablePath, UiTestSettings.Arguments)
        {
            UseShellExecute = false,
        };

        Application = FlaUI.Core.Application.Launch(startInfo);
        launchedApplications.Add(Application);
        Application.WaitWhileMainHandleIsMissing(TimeSpan.FromSeconds(20));
        MainWindow = Application.GetMainWindow(Automation) ??
                     throw new AssertionException("FileManager did not expose a UI Automation main window.");
    }

    private Window WaitForWindow(Func<Window, bool> predicate)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            var dialog = Application.GetAllTopLevelWindows(Automation).FirstOrDefault(predicate);
            if (dialog is not null)
                return dialog;

            Thread.Sleep(100);
        }

        Assert.Fail("Timed out waiting for the requested FileManager window.");
        return null!;
    }

    private static void WaitForWindowToClose(Window dialog)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (!dialog.IsAvailable)
                return;

            Thread.Sleep(100);
        }

        Assert.Fail("Timed out waiting for the configuration dialog to close.");
    }
}
