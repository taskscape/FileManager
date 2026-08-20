using FileManager.UiTests.Infrastructure;
using FlaUI.Core.Definitions;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class BasicUiTests : FileManagerUiTestBase
{
    [TestCaseSource(typeof(BasicUiScenarios), nameof(BasicUiScenarios.All))]
    public void Basic_ui_scenario(UiScenario scenario)
    {
        switch (scenario.Kind)
        {
            case UiScenarioKind.MainWindow:
                AssertMainWindow();
                break;

            case UiScenarioKind.AccessibilityTree:
                AssertAccessibilityTree();
                break;

            case UiScenarioKind.ConfigurationCancel:
                CloseConfigurationDialog(OpenConfigurationDialog(), commit: false);
                AssertMainWindow();
                break;

            case UiScenarioKind.ConfigurationCommit:
                CloseConfigurationDialog(OpenConfigurationDialog(), commit: true);
                AssertMainWindow();
                break;

            case UiScenarioKind.ConfigurationPersistence:
                AssertConfigurationCommitPersistsAfterRestart();
                break;

            case UiScenarioKind.RestartAfterCommit:
                CloseConfigurationDialog(OpenConfigurationDialog(), commit: true);
                RestartFileManager();
                AssertMainWindow();
                break;

            case UiScenarioKind.FtpBookmarkCreationPersists:
                AssertFtpBookmarkCreationPersistsAfterRestart(scenario.Number);
                break;

            default:
                Assert.Fail($"Unsupported UI scenario: {scenario.Kind}.");
                break;
        }
    }

    private void AssertMainWindow()
    {
        // Basic visibility and focus checks verify that UIA3 attached to the native top-level window.
        Assert.Multiple(() =>
        {
            Assert.That(MainWindow.Properties.NativeWindowHandle.Value, Is.Not.EqualTo(nint.Zero));
            Assert.That(MainWindow.Title, Is.Not.Empty);
            Assert.That(MainWindow.IsEnabled, Is.True);
            Assert.That(MainWindow.BoundingRectangle.Width, Is.GreaterThan(0));
            Assert.That(MainWindow.BoundingRectangle.Height, Is.GreaterThan(0));
        });

        MainWindow.Focus();
    }

    private void AssertAccessibilityTree()
    {
        // The legacy native menu is owner-drawn and is not required to surface as a UIA MenuBar.
        var descendants = MainWindow.FindAllDescendants();
        Assert.That(descendants, Is.Not.Empty);
    }

    private void AssertConfigurationCommitPersistsAfterRestart()
    {
        // Restore the original value before returning so repeated cases remain independent in the disposable profile.
        var configurationDialog = OpenConfigurationDialog();
        var originalState = ToggleFirstConfigurationCheckBox(configurationDialog);
        CloseConfigurationDialog(configurationDialog, commit: true);
        // The native dialog saves after its property-sheet window closes, so wait before terminating this process for restart coverage.
        WaitForConfigurationClearReadOnlyPersistence(!originalState);

        RestartFileManager();
        var reloadedDialog = OpenConfigurationDialog();
        Assert.That(IsFirstConfigurationCheckBoxChecked(reloadedDialog), Is.EqualTo(!originalState),
                    "The configuration value was not retained after a committed dialog and restart.");
        ToggleFirstConfigurationCheckBox(reloadedDialog);
        CloseConfigurationDialog(reloadedDialog, commit: true);
        WaitForConfigurationClearReadOnlyPersistence(originalState);
    }

    private void AssertFtpBookmarkCreationPersistsAfterRestart(int scenarioNumber)
    {
        // A unique name prevents a prior failed run in the same disposable profile from satisfying this test accidentally.
        var createdBookmarkName = $"UIA3-persist-{scenarioNumber:000}-{Guid.NewGuid():N}";
        var editedBookmarkName = $"{createdBookmarkName}-edited";
        var bookmarksDialog = OpenFtpBookmarksDialog();
        CreateFtpBookmark(bookmarksDialog, createdBookmarkName);
        RenameFocusedFtpBookmark(bookmarksDialog, editedBookmarkName);
        // Prove the organizer accepted the rename before its Close action persists the collection.
        WaitForFtpBookmark(bookmarksDialog, editedBookmarkName,
                           "The FTP organizer did not retain the created and edited bookmark before restart.");
        CloseFtpBookmarksDialog(bookmarksDialog);

        RestartFileManager();
        var reloadedDialog = OpenFtpBookmarksDialog();
        // The post-restart native read proves that the accepted organizer state was reloaded from the isolated profile.
        WaitForFtpBookmark(reloadedDialog, editedBookmarkName,
                           "The created and edited FTP bookmark was not present after restart.");
        CloseFtpBookmarksDialog(reloadedDialog);
    }

    private static void WaitForFtpBookmark(FlaUI.Core.AutomationElements.Window bookmarksDialog, string bookmarkName, string failureMessage)
    {
        // The owner-drawn list populates during dialog initialization, so wait for its native ANSI item table before judging persistence.
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < timeout)
        {
            if (NativeCommands.FtpBookmarksContains(bookmarksDialog.Properties.NativeWindowHandle.Value, bookmarkName))
                return;

            Thread.Sleep(100);
        }

        Assert.Fail(failureMessage);
    }
}
