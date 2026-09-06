using FileManager.UiTests.Infrastructure;
using Microsoft.Win32;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class ConfigurationPayloadFailureUiTests : FileManagerUiTestBase
{
    [TestCase("value:Title bar prefix text", false)]
    [TestCase("key:Panel Items Hilighting", false)]
    [TestCase("value:Title bar prefix text", true)]
    [TestCase("key:Panel Items Hilighting", true)]
    [Category("FaultInjection")]
    public void A_returned_payload_error_retains_the_previous_snapshot_and_allows_a_later_retry(string fault, bool retry)
    {
        UiTestSettings.RequireConfigurationFaultInjection();
        var baseline = ReadCheckBox();
        CommitCheckBox(baseline);
        var arm = Path.Combine(UiTestSettings.TestDataRoot, "config-returned-error.arm");
        File.Delete(arm);
        RestartFileManager(new Dictionary<string, string>
        {
            ["FILEMANAGER_CONFIG_RETURN_ERROR"] = fault,
            ["FILEMANAGER_CONFIG_FAULT_ARM_FILE"] = arm,
            ["FILEMANAGER_UI_CONFIG_STATUS"] = "1",
        });
        // Drain startup and coalesced saves before capturing the transaction baseline.
        WaitForExplicitSave();
        var dialog = OpenConfigurationDialog();
        if (IsFirstConfigurationCheckBoxChecked(dialog) == baseline) ToggleFirstConfigurationCheckBox(dialog);
        var originalGeneration = ActiveGeneration();
        File.WriteAllText(arm, "armed");
        CommitConfigurationDialogWithoutWaiting(dialog);
        // A native error can appear while its UIA Name provider is still attaching.
        var failure = WaitForWindow(window => NativeCommands.GetWindowTitle(window.Properties.NativeWindowHandle.ValueOrDefault) == "Error Saving Configuration");
        Assert.That(NativeCommands.GetDialogText(failure.Properties.NativeWindowHandle.Value),
                    Does.Contain("could not be saved completely"));
        // A later successful registry call must not clear the earlier payload error.
        Assert.That(ActiveGeneration(), Is.EqualTo(originalGeneration));
        Assert.That(ReadPersistedConfigurationClearReadOnly(), Is.EqualTo(baseline));
        using (var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot))
        using (var incomplete = root!.OpenSubKey($"Configuration Generations\\Generation {1 - originalGeneration}"))
            Assert.That(incomplete?.GetValue("Transaction Complete"), Is.Null);
        NativeCommands.ClickDialogButton(failure.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(failure);
        File.Delete(arm);
        if (retry)
        {
            // Retry the retained in-memory settings after the one-shot fault is disarmed.
            CommitCheckBox(!baseline);
            RestartFileManager();
            Assert.That(ReadCheckBox(), Is.EqualTo(!baseline));
        }
        else
        {
            // Do not let an ordinary shutdown save hide a failed transaction.
            Application.Kill();
            WaitForFileManagerExit(TimeSpan.FromSeconds(10));
            RestartFileManager();
            Assert.That(ReadCheckBox(), Is.EqualTo(baseline));
        }
    }

    private static int ActiveGeneration()
    {
        using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
        Assert.That(root?.GetValue("Active Generation"), Is.TypeOf<int>());
        var generation = (int)root!.GetValue("Active Generation")!;
        Assert.That(generation, Is.InRange(0, 1));
        return generation;
    }

    private void WaitForExplicitSave()
    {
        var status = Path.Combine(UiTestSettings.TestDataRoot, ".config-save-status");
        File.Delete(status);
        NativeCommands.Execute(NativeMainWindowHandle, 687); // CM_SAVECONFIG, the normal explicit save command
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            if (File.Exists(status) && File.ReadAllText(status) == "complete") return;
            Thread.Sleep(50);
        }
        Assert.Fail("The explicit baseline save did not complete.");
    }

    private bool ReadCheckBox()
    {
        var dialog = OpenConfigurationDialog();
        var value = IsFirstConfigurationCheckBoxChecked(dialog);
        CloseConfigurationDialog(dialog, commit: false);
        return value;
    }

    private void CommitCheckBox(bool value)
    {
        var dialog = OpenConfigurationDialog();
        if (IsFirstConfigurationCheckBoxChecked(dialog) != value) ToggleFirstConfigurationCheckBox(dialog);
        CloseConfigurationDialog(dialog, commit: true);
        WaitForConfigurationClearReadOnlyPersistence(value);
    }
}
