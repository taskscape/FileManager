using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using FlaUI.Core.Definitions;
using Microsoft.Win32;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class LanguageSwitchingUiTests : FileManagerUiTestBase
{
    private const int ConfigurationTree = 1;
    private const int LanguageButton = 387;
    private const int LanguageDisplay = 388;
    private const int LanguageList = 1031;

    [Test]
    public void Selecting_Polish_loads_the_Polish_resources_after_restart()
    {
        var configuration = OpenConfigurationDialogInAnyLanguage();
        Assert.That(ConfigurationDialogPages.SelectLanguagePage(configuration.Properties.NativeWindowHandle.Value), Is.True,
                    "The Configuration dialog did not expose its Regional language page.");

        NativeCommands.PostDialogButtonClick(configuration.Properties.NativeWindowHandle.Value, LanguageButton);
        var selector = WaitForWindow(window => string.Equals(window.Title, "Select Language", StringComparison.Ordinal));
        // Polish is the only bundled UI language whose type-ahead prefix is P, so this drives the same list selection a user makes.
        NativeCommands.SelectDialogListViewItemByPrefix(selector.Properties.NativeWindowHandle.Value, LanguageList, 'P');
        NativeCommands.ClickDialogButton(selector.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(selector);

        Assert.That(NativeCommands.GetDialogControlText(configuration.Properties.NativeWindowHandle.Value, LanguageDisplay),
                    Does.Contain("pol").IgnoreCase,
                    "The language selector did not update the pending Configuration value to Polish.");

        NativeCommands.PostDialogButtonClick(configuration.Properties.NativeWindowHandle.Value, 5);
        var restartNotice = WaitForWindow(window => NativeCommands.GetDialogText(window.Properties.NativeWindowHandle.Value)
            .Contains("Language changes do not take effect", StringComparison.Ordinal));
        NativeCommands.ClickDialogButton(restartNotice.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(restartNotice);
        WaitForWindowToClose(configuration);
        WaitForPersistedLanguage("polish.slg");

        RestartFileManager();

        var reloadedConfiguration = OpenConfigurationDialogInAnyLanguage();
        Assert.That(ConfigurationDialogPages.SelectLanguagePage(reloadedConfiguration.Properties.NativeWindowHandle.Value), Is.True,
                    "The restarted Configuration dialog did not expose its Regional language page.");
        // The localized button proves the startup resource module, not merely the stored filename, was reloaded as Polish.
        Assert.That(NativeCommands.GetDialogControlText(reloadedConfiguration.Properties.NativeWindowHandle.Value, LanguageButton),
                    Does.Contain("Język"),
                    "Restart loaded English resources instead of the persisted Polish language module.");
        CloseConfigurationDialog(reloadedConfiguration, commit: false);

        // Query the live FTP string table after restart; correct .rc2 bytes alone do not prove the dialog renders them.
        var connectDialog = OpenFtpConnectDialog();
        Assert.That(NativeCommands.FtpBookmarksContains(connectDialog.Properties.NativeWindowHandle.Value, "Szybkie połączenie"), Is.True,
                    "The FTP bookmark list did not render Szybkie połączenie correctly.");
        NativeCommands.ClickDialogButton(connectDialog.Properties.NativeWindowHandle.Value, 2);
        WaitForWindowToClose(connectDialog);

        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.FindFiles);
        var findDialog = WaitForWindow(window =>
            window.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString())) is not null);
        var results = findDialog.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString()))!;
        // Check the rendered header so an ANSI list-view call cannot silently corrupt UTF-8 resource text.
        var columnNames = results.FindAllDescendants(cf => cf.ByControlType(ControlType.HeaderItem))
            .Select(column => column.Name).ToArray();
        Assert.That(columnNames, Does.Contain("Ścieżka"),
                    "The Polish Find results header did not render Ścieżka correctly.");

    }

    private Window OpenConfigurationDialogInAnyLanguage()
    {
        NativeCommands.OpenConfiguration(NativeMainWindowHandle);
        // The caption changes with the UI language; the property-sheet tree is the stable native identity.
        return WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, ConfigurationTree));
    }

    private static void WaitForPersistedLanguage(string expectedLanguage)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTime.UtcNow < deadline)
        {
            using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
            if (root?.GetValue("Active Generation") is int generation)
            {
                using var configuration = root.OpenSubKey($"Configuration Generations\\Generation {generation}\\Configuration");
                if (string.Equals(configuration?.GetValue("Language") as string, expectedLanguage, StringComparison.OrdinalIgnoreCase))
                    return;
            }

            Thread.Sleep(50);
        }

        Assert.Fail("The real Configuration dialog did not commit polish.slg before the restart.");
    }
}
