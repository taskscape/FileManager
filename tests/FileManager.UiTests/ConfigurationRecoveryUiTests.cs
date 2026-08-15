using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class ConfigurationRecoveryUiTests : FileManagerUiTestBase
{
    // Keep this aligned with CONFIGURATION_WRITE_FAULT_EXIT_CODE in src/regwork.h.
    private const int ConfigurationWriteFaultExitCode = 121;

    [Test]
    [Category("FaultInjection")]
    public void Every_interrupted_configuration_write_restores_a_complete_profile()
    {
        UiTestSettings.RequireConfigurationFaultInjection();

        // The fault-injection report is disposable test data, not a file in the user's normal temp directory.
        var reportPath = Path.Combine(UiTestSettings.TestDataRoot, $"filemanager-config-writes-{Guid.NewGuid():N}.txt");
        try
        {
            var baseline = ReadFirstConfigurationCheckBox();
            CommitFirstConfigurationCheckBox(baseline);
            RestartFileManager();
            Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline), "The baseline configuration did not survive restart.");

            var candidate = !baseline;
            var operationCount = MeasureConfigurationWriteBoundaries(baseline, candidate, reportPath);
            RestoreBaseline(baseline);

            for (var writeBoundary = 1; writeBoundary <= operationCount; writeBoundary++)
                AssertRecoveryAtWriteBoundary(writeBoundary, baseline, candidate);
        }
        finally
        {
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

    private void AssertRecoveryAtWriteBoundary(int writeBoundary, bool baseline, bool candidate)
    {
        RestartFileManager(new Dictionary<string, string>
        {
            ["FILEMANAGER_CONFIG_FAULT_AFTER_WRITE"] = writeBoundary.ToString(),
        });
        Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline),
                    $"Boundary {writeBoundary} did not begin from the complete baseline profile.");

        var dialog = OpenConfigurationDialog();
        ToggleFirstConfigurationCheckBox(dialog);
        CommitConfigurationDialogWithoutWaiting(dialog);
        Assert.That(WaitForFileManagerExit(TimeSpan.FromSeconds(15)), Is.EqualTo(ConfigurationWriteFaultExitCode),
                    $"Configuration fault injection did not stop at write boundary {writeBoundary}.");

        RestartFileManager();
        var restored = ReadFirstConfigurationCheckBox();
        Assert.That(restored == baseline || restored == candidate, Is.True,
                    $"Restart after write boundary {writeBoundary} exposed a mixed configuration profile.");
        RestoreBaseline(baseline);
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

    private void RestoreBaseline(bool baseline)
    {
        CommitFirstConfigurationCheckBox(baseline);
        RestartFileManager();
        Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline), "The next fault trial did not start from a complete baseline profile.");
    }
}
