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
            Assert.That(MainWindowHandle, Is.Not.EqualTo(nint.Zero));
            Assert.That(MainWindow.Title, Is.Not.Empty);
            Assert.That(MainWindow.IsEnabled, Is.True);
            Assert.That(MainWindow.BoundingRectangle.Width, Is.GreaterThan(0));
            Assert.That(MainWindow.BoundingRectangle.Height, Is.GreaterThan(0));
        });

        MainWindow.Focus();
    }

    private void AssertAccessibilityTree()
    {
        // Native controls may be owner-drawn, but the main window must still expose a non-empty UIA tree.
        var descendants = MainWindow.FindAllDescendants();
        Assert.That(descendants, Is.Not.Empty);
        Assert.That(descendants.Any(element => element.ControlType == ControlType.MenuBar ||
                                               element.ControlType == ControlType.MenuItem),
                    Is.True,
                    "The main window did not expose a menu through UI Automation.");
    }

    private void AssertConfigurationCommitPersistsAfterRestart()
    {
        // Restore the original value before returning so repeated cases remain independent in the disposable profile.
        var configurationDialog = OpenConfigurationDialog();
        var originalState = ToggleFirstConfigurationCheckBox(configurationDialog);
        CloseConfigurationDialog(configurationDialog, commit: true);

        RestartFileManager();
        var reloadedDialog = OpenConfigurationDialog();
        try
        {
            Assert.That(IsFirstConfigurationCheckBoxChecked(reloadedDialog), Is.EqualTo(!originalState),
                        "The configuration value was not retained after a committed dialog and restart.");
        }
        finally
        {
            // Restore the incoming profile even when the persistence assertion fails, preventing repetition cascades.
            if (IsFirstConfigurationCheckBoxChecked(reloadedDialog) != originalState)
                ToggleFirstConfigurationCheckBox(reloadedDialog);
            CloseConfigurationDialog(reloadedDialog, commit: true);
        }
    }

    private void AssertFtpBookmarkCreationPersistsAfterRestart(int scenarioNumber)
    {
        // A unique name prevents a prior failed run in the same disposable profile from satisfying this test accidentally.
        var createdBookmarkName = $"UIA3-persist-{scenarioNumber:000}-{Guid.NewGuid():N}";
        var editedBookmarkName = $"{createdBookmarkName}-edited";
        var bookmarksDialog = OpenFtpBookmarksDialog();
        CreateFtpBookmark(bookmarksDialog, createdBookmarkName);
        RenameFocusedFtpBookmark(bookmarksDialog, editedBookmarkName);
        CloseFtpBookmarksDialog(bookmarksDialog);

        RestartFileManager();
        var reloadedDialog = OpenFtpBookmarksDialog();
        Assert.That(reloadedDialog.FindAllDescendants().Any(element => element.Name == editedBookmarkName), Is.True,
                    "The created and edited FTP bookmark was not present after restart.");
        CloseFtpBookmarksDialog(reloadedDialog);
    }
}
