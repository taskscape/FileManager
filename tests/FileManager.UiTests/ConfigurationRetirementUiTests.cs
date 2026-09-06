using FileManager.UiTests.Infrastructure;
using Microsoft.Win32;
using NUnit.Framework;
using System.Security.Principal;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class ConfigurationRetirementUiTests : FileManagerUiTestBase
{
    private static string Marker(string name) => Path.Combine(UiTestSettings.TestDataRoot, ".config-retirement." + name);
    private static string SaveStatus => Path.Combine(UiTestSettings.TestDataRoot, ".config-save-status");

    [TestCase(false)]
    [TestCase(true)]
    public async Task Startup_does_not_retire_a_fallback_after_another_instance_replaces_its_loaded_generation(bool reuseSlot)
    {
        await PrepareBaseline();
        var original = Snapshot();
        File.WriteAllText(Marker("arm"), "armed");
        var secondary = StartAdditionalFileManager(new Dictionary<string, string>
        { ["FILEMANAGER_CONFIG_RETIRE_BARRIER"] = "before-lock" });
        await WaitFor(() => File.Exists(Marker("entered")), "Second startup did not reach retirement.");
        await SaveChangedCheckBox();
        if (reuseSlot)
        {
            // Startup and coalesced saves may already have switched slots. Use
            // completed explicit saves to ensure the original slot is actually reused.
            for (var attempt = 0; attempt < 3 && Snapshot().Generation != original.Generation; ++attempt)
                await SaveExplicitly();
        }
        var current = Snapshot();
        Assert.That(current.Token, Is.Not.EqualTo(original.Token));
        if (reuseSlot) Assert.That(current.Generation, Is.EqualTo(original.Generation));
        Assert.That(HasGeneration(1 - current.Generation), Is.True);
        File.WriteAllText(Marker("release"), "continue");
        await WaitFor(() => File.Exists(Marker("completed")), "Second startup did not finish retirement validation.");
        // The second startup waits after releasing the mutex, excluding later
        // automatic saves from this observation of the retirement decision.
        Assert.That(Snapshot(), Is.EqualTo(current));
        Assert.That(HasGeneration(1 - current.Generation), Is.True, "An obsolete startup removed the current fallback.");
        secondary.Kill();
        AssertSettingsSurviveRestart();
    }

    [Test]
    public async Task Retirement_holds_the_save_mutex_through_selector_validation_and_deletion()
    {
        await PrepareBaseline();
        File.WriteAllText(Marker("arm"), "armed");
        var secondary = StartAdditionalFileManager(new Dictionary<string, string>
        { ["FILEMANAGER_CONFIG_RETIRE_BARRIER"] = "after-selector" });
        await WaitFor(() => File.Exists(Marker("entered")), "Second startup did not hold the retirement barrier.");
        // Startup can save before retirement; the checkpoint identifies the
        // selector protected by the mutex, independent of those earlier writes.
        var original = Snapshot();
        using (var identity = WindowsIdentity.GetCurrent())
        using (var mutex = Mutex.OpenExisting($"Global\\TaskscapeLtdSalamanderLoadSaveRegistry_{identity.User!.Value}"))
        {
            var acquired = false;
            try { acquired = mutex.WaitOne(0); }
            catch (AbandonedMutexException) { acquired = true; }
            if (acquired) mutex.ReleaseMutex();
            Assert.That(acquired, Is.False, "Retirement read its selector without owning the save mutex.");
        }
        var dialog = OpenConfigurationDialog();
        ToggleFirstConfigurationCheckBox(dialog);
        File.Delete(SaveStatus);
        CommitConfigurationDialogWithoutWaiting(dialog);
        Assert.That(Snapshot(), Is.EqualTo(original));
        File.WriteAllText(Marker("release"), "continue");
        await WaitFor(() => File.Exists(Marker("completed")), "Retirement did not release the save mutex.");
        await WaitFor(() => File.Exists(SaveStatus) && File.ReadAllText(SaveStatus) == "complete", "The other process could not finish its queued save.");
        var current = Snapshot();
        Assert.That(current.Token, Is.Not.EqualTo(original.Token));
        Assert.That(HasGeneration(current.Generation), Is.True);
        secondary.Kill();
        AssertSettingsSurviveRestart();
    }

    private async Task PrepareBaseline()
    {
        foreach (var name in new[] { "arm", "entered", "release", "completed", "finish-release" }) File.Delete(Marker(name));
        RestartFileManager(new Dictionary<string, string> { ["FILEMANAGER_UI_CONFIG_STATUS"] = "1" });
        await SaveExplicitly();
    }

    private async Task SaveExplicitly()
    {
        File.Delete(SaveStatus);
        NativeCommands.Execute(NativeMainWindowHandle, 687); // explicit Save Configuration
        await WaitFor(() => File.Exists(SaveStatus) && File.ReadAllText(SaveStatus) == "complete", "Baseline save did not finish.");
    }

    private void AssertSettingsSurviveRestart()
    {
        // A normal startup may write an equivalent new generation. Verify the
        // restored setting instead of requiring its transaction identity to persist.
        var expected = ReadPersistedConfigurationClearReadOnly();
        RestartFileManager();
        var dialog = OpenConfigurationDialog();
        Assert.That(IsFirstConfigurationCheckBoxChecked(dialog), Is.EqualTo(expected));
        CloseConfigurationDialog(dialog, commit: false);
    }

    private async Task SaveChangedCheckBox()
    {
        var dialog = OpenConfigurationDialog();
        ToggleFirstConfigurationCheckBox(dialog);
        File.Delete(SaveStatus);
        CommitConfigurationDialogWithoutWaiting(dialog);
        await WaitFor(() => File.Exists(SaveStatus) && File.ReadAllText(SaveStatus) == "complete", "Concurrent save did not finish.");
    }

    private static (int Generation, string Token) Snapshot()
    {
        using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
        var generation = (int)root!.GetValue("Active Generation")!;
        using var key = root.OpenSubKey($"Configuration Generations\\Generation {generation}");
        Assert.That(key?.GetValue("Transaction Identity"), Is.TypeOf<byte[]>());
        return (generation, Convert.ToHexString((byte[])key!.GetValue("Transaction Identity")!));
    }

    private static bool HasGeneration(int generation)
    {
        using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
        using var key = root?.OpenSubKey($"Configuration Generations\\Generation {generation}");
        return key is not null;
    }

    private static async Task WaitFor(Func<bool> predicate, string message)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return;
            await Task.Delay(50);
        }
        Assert.Fail(message);
    }
}
