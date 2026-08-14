using FileManager.UiTests.Infrastructure;
using Microsoft.Win32;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class ConfigurationRecoveryUiTests : FileManagerUiTestBase
{
    // Keep this aligned with CONFIGURATION_WRITE_FAULT_EXIT_CODE in src/regwork.h.
    private const int ConfigurationWriteFaultExitCode = 121;
    private const string ConfigurationRoot = @"Software\Open Salamander\6.0";
    private const string ConfigurationBackupRoot = ConfigurationRoot + ".backup.63A7CD13";

    [Test]
    [Category("FaultInjection")]
    public void Every_interrupted_configuration_write_restores_a_complete_profile()
    {
        UiTestSettings.RequireConfigurationFaultInjection();

        var reportPath = Path.Combine(Path.GetTempPath(), $"filemanager-config-writes-{Guid.NewGuid():N}.txt");
        RegistryRootsSnapshot? baselineSnapshot = null;
        try
        {
            var baseline = ReadFirstConfigurationCheckBox();
            CommitFirstConfigurationCheckBox(baseline);
            RestartFileManager();
            Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline), "The baseline configuration did not survive restart.");
            // Freeze the complete host store only after a normal save and restart have validated the baseline.
            baselineSnapshot = RegistryRootsSnapshot.Capture(ConfigurationRoot, ConfigurationBackupRoot);

            var candidate = !baseline;
            var operationCount = UiTestSettings.LimitConfigurationFaultBoundaries(
                MeasureConfigurationWriteBoundaries(baseline, candidate, reportPath));

            for (var writeBoundary = 1; writeBoundary <= operationCount; writeBoundary++)
                AssertRecoveryAtWriteBoundary(writeBoundary, baseline, candidate, baselineSnapshot);
        }
        finally
        {
            if (baselineSnapshot is not null)
            {
                // Leave the disposable account at its verified pre-matrix state even when a boundary assertion fails.
                StopCurrentApplication(baselineSnapshot.Restore);
            }
            if (File.Exists(reportPath))
                File.Delete(reportPath);
        }
    }

    private int MeasureConfigurationWriteBoundaries(bool baseline, bool candidate, string reportPath)
    {
        RestartFileManager(new Dictionary<string, string>
        {
            ["FILEMANAGER_CONFIG_FAULT_REPORT"] = reportPath,
        });
        Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline));
        CommitFirstConfigurationCheckBox(candidate);

        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            if (File.Exists(reportPath) && int.TryParse(File.ReadAllText(reportPath), out var operationCount) && operationCount > 0)
                return operationCount;

            Thread.Sleep(50);
        }

        Assert.Fail("The configuration writer did not report its registry write-boundary count.");
        return 0;
    }

    private void AssertRecoveryAtWriteBoundary(int writeBoundary, bool baseline, bool candidate,
                                               RegistryRootsSnapshot baselineSnapshot)
    {
        // Every boundary starts from byte-for-byte equivalent registry state, independent of the preceding crash result.
        RestartFileManager(afterPreviousApplicationStopped: baselineSnapshot.Restore);
        Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline),
                    $"Boundary {writeBoundary} did not begin from the complete baseline profile.");

        // Arm only after the baseline dialog closes so its deferred command save cannot consume this trial.
        NativeCommands.ArmNextConfigurationWriteFault(MainWindowHandle, writeBoundary);
        var dialog = OpenConfigurationDialog();
        ToggleFirstConfigurationCheckBox(dialog);
        CommitConfigurationDialogWithoutWaiting(dialog);
        Assert.That(WaitForFileManagerExit(TimeSpan.FromSeconds(15)), Is.EqualTo(ConfigurationWriteFaultExitCode),
                    $"Configuration fault injection did not stop at write boundary {writeBoundary}.");

        RestartFileManager();
        var restored = ReadFirstConfigurationCheckBox();
        Assert.That(restored == baseline || restored == candidate, Is.True,
                    $"Restart after write boundary {writeBoundary} exposed a mixed configuration profile.");
    }

    private bool ReadFirstConfigurationCheckBox()
    {
        var dialog = OpenConfigurationDialog();
        var value = IsFirstConfigurationCheckBoxChecked(dialog);
        CloseConfigurationDialog(dialog, commit: false);
        return value;
    }

    private void CommitFirstConfigurationCheckBox(bool expectedValue)
    {
        var dialog = OpenConfigurationDialog();
        if (IsFirstConfigurationCheckBoxChecked(dialog) != expectedValue)
            ToggleFirstConfigurationCheckBox(dialog);
        CloseConfigurationDialog(dialog, commit: true);
    }

    private sealed class RegistryRootsSnapshot
    {
        private readonly Dictionary<string, RegistryNode?> roots;

        private RegistryRootsSnapshot(Dictionary<string, RegistryNode?> roots)
        {
            this.roots = roots;
        }

        internal static RegistryRootsSnapshot Capture(params string[] rootPaths)
        {
            using var currentUser = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64);
            var roots = new Dictionary<string, RegistryNode?>(StringComparer.OrdinalIgnoreCase);
            foreach (var rootPath in rootPaths)
            {
                using var key = currentUser.OpenSubKey(rootPath, writable: false);
                // Capture absence as well as content so stale recovery backups cannot leak into another boundary.
                roots[rootPath] = key is null ? null : RegistryNode.Capture(key);
            }
            return new RegistryRootsSnapshot(roots);
        }

        internal void Restore()
        {
            using var currentUser = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView.Registry64);
            foreach (var (rootPath, snapshot) in roots)
            {
                currentUser.DeleteSubKeyTree(rootPath, throwOnMissingSubKey: false);
                if (snapshot is null)
                    continue;

                using var key = currentUser.CreateSubKey(rootPath, writable: true)
                    ?? throw new InvalidOperationException($"Could not recreate registry baseline '{rootPath}'.");
                snapshot.Restore(key);
            }
        }
    }

    private sealed class RegistryNode
    {
        private readonly Dictionary<string, RegistryValue> values = new(StringComparer.OrdinalIgnoreCase);
        private readonly Dictionary<string, RegistryNode> children = new(StringComparer.OrdinalIgnoreCase);

        internal static RegistryNode Capture(RegistryKey key)
        {
            var node = new RegistryNode();
            foreach (var valueName in key.GetValueNames())
            {
                var value = key.GetValue(valueName, null, RegistryValueOptions.DoNotExpandEnvironmentNames)
                    ?? throw new InvalidOperationException($"Registry value '{key.Name}\\{valueName}' could not be captured.");
                // Registry APIs return mutable arrays; clone them so the baseline remains immutable in memory.
                node.values[valueName] = new RegistryValue(CloneRegistryValue(value), key.GetValueKind(valueName));
            }

            foreach (var childName in key.GetSubKeyNames())
            {
                using var child = key.OpenSubKey(childName, writable: false)
                    ?? throw new InvalidOperationException($"Registry key '{key.Name}\\{childName}' could not be captured.");
                node.children[childName] = Capture(child);
            }
            return node;
        }

        internal void Restore(RegistryKey key)
        {
            foreach (var (valueName, value) in values)
                key.SetValue(valueName, CloneRegistryValue(value.Data), value.Kind);

            foreach (var (childName, childSnapshot) in children)
            {
                using var child = key.CreateSubKey(childName, writable: true)
                    ?? throw new InvalidOperationException($"Could not recreate registry baseline '{key.Name}\\{childName}'.");
                childSnapshot.Restore(child);
            }
        }

        private static object CloneRegistryValue(object value) => value switch
        {
            byte[] bytes => bytes.ToArray(),
            string[] strings => strings.ToArray(),
            _ => value,
        };

        private sealed record RegistryValue(object Data, RegistryValueKind Kind);
    }
}
