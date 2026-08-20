using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class ConfigurationRecoveryUiTests : FileManagerUiTestBase
{
    // Keep this aligned with CONFIGURATION_WRITE_FAULT_EXIT_CODE in src/regwork.h.
    private const int ConfigurationWriteFaultExitCode = 121;
    // These names are emitted at the five atomic-tail mutations in CommitConfigurationTransaction.
    private static readonly string[] TransactionFaultPhases =
        ["checksum", "complete", "generation-flush", "selector", "store-flush"];

    [Test]
    [Category("FaultInjection")]
    public void Interrupted_configuration_writes_at_transaction_boundaries_restore_a_complete_profile()
    {
        UiTestSettings.RequireConfigurationFaultInjection();

        // The fault-injection report is disposable test data, not a file in the user's normal temp directory.
        var reportPath = Path.Combine(UiTestSettings.TestDataRoot, $"filemanager-config-writes-{Guid.NewGuid():N}.txt");
        var armPath = Path.Combine(UiTestSettings.TestDataRoot, $"filemanager-config-arm-{Guid.NewGuid():N}.txt");
        try
        {
            var baseline = ReadFirstConfigurationCheckBox();
            CommitFirstConfigurationCheckBox(baseline);
            RestartFileManager();
            Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline), "The baseline configuration did not survive restart.");

            var candidate = !baseline;
            var operationCount = MeasureConfigurationWriteBoundaries(baseline, candidate, reportPath, armPath);
            RestoreBaseline(baseline);

            // The inactive generation is unreachable until the final selector write. Sample its payload uniformly,
            // then exhaust the five named structural mutations without assuming the payload has a stable write count.
            foreach (var writeBoundary in SelectTransactionWriteBoundaries(operationCount))
                AssertRecoveryAtWriteBoundary(writeBoundary, baseline, candidate, armPath);
            foreach (var phase in TransactionFaultPhases)
                AssertRecoveryAtTransactionPhase(phase, baseline, candidate, armPath);
        }
        finally
        {
            if (File.Exists(reportPath))
                File.Delete(reportPath);
            if (File.Exists(armPath))
                File.Delete(armPath);
        }
    }

    private static IEnumerable<int> SelectTransactionWriteBoundaries(int operationCount)
    {
        // A bounded structural matrix keeps the release gate practical even when plug-ins add thousands of snapshot values.
        var boundaries = new SortedSet<int>
        {
            1,
            2,
            operationCount / 4,
            operationCount / 2,
            operationCount * 3 / 4,
        };
        return boundaries.Where(boundary => boundary >= 1 && boundary <= operationCount);
    }

    private int MeasureConfigurationWriteBoundaries(bool baseline, bool candidate, string reportPath, string armPath)
    {
        File.Delete(armPath);
        RestartFileManager(new Dictionary<string, string>
        {
            ["FILEMANAGER_CONFIG_FAULT_REPORT"] = reportPath,
            ["FILEMANAGER_CONFIG_FAULT_ARM_FILE"] = armPath,
        });
        // Measure from the persisted generation without opening and immediately reopening the modeless legacy property sheet.
        Assert.That(ReadPersistedConfigurationClearReadOnly(), Is.EqualTo(baseline));
        CommitFirstConfigurationCheckBox(candidate, armPath);

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

    private void AssertRecoveryAtWriteBoundary(int writeBoundary, bool baseline, bool candidate, string armPath)
    {
        AssertRecoveryAtFaultPoint($"write boundary {writeBoundary}",
            new Dictionary<string, string> { ["FILEMANAGER_CONFIG_FAULT_AFTER_WRITE"] = writeBoundary.ToString() },
            baseline, candidate, armPath);
    }

    private void AssertRecoveryAtTransactionPhase(string phase, bool baseline, bool candidate, string armPath)
    {
        // Structural phase names remain deterministic when an earlier test changes the plug-in configuration payload.
        AssertRecoveryAtFaultPoint($"transaction phase {phase}",
            new Dictionary<string, string> { ["FILEMANAGER_CONFIG_FAULT_PHASE"] = phase },
            baseline, candidate, armPath);
    }

    private void AssertRecoveryAtFaultPoint(string faultPoint, IReadOnlyDictionary<string, string> faultEnvironment,
                                            bool baseline, bool candidate, string armPath)
    {
        // Emit the selected fault point so any startup failure identifies the exact transaction phase in CI logs.
        TestContext.Progress.WriteLine($"Testing configuration {faultPoint}.");
        File.Delete(armPath);
        var environment = new Dictionary<string, string>(faultEnvironment)
        {
            ["FILEMANAGER_CONFIG_FAULT_ARM_FILE"] = armPath,
        };
        RestartFileManager(environment);
        // The faulted process must start from a durable baseline before its only Configuration-dialog mutation.
        Assert.That(ReadPersistedConfigurationClearReadOnly(), Is.EqualTo(baseline),
                    $"Configuration {faultPoint} did not begin from the complete baseline profile.");

        var dialog = OpenConfigurationDialog();
        ToggleFirstConfigurationCheckBox(dialog);
        // Arm only after startup is complete and immediately before accepting the mutation under test.
        File.WriteAllText(armPath, "armed");
        CommitConfigurationDialogWithoutWaiting(dialog);
        Assert.That(WaitForFileManagerExit(TimeSpan.FromSeconds(15)), Is.EqualTo(ConfigurationWriteFaultExitCode),
                    $"Configuration fault injection did not stop at {faultPoint}.");

        RestartFileManager();
        var restored = ReadFirstConfigurationCheckBox();
        Assert.That(restored == baseline || restored == candidate, Is.True,
                    $"Restart after {faultPoint} exposed a mixed configuration profile.");
        RestoreBaseline(baseline);
    }

    private bool ReadFirstConfigurationCheckBox()
    {
        var dialog = OpenConfigurationDialog();
        var value = IsFirstConfigurationCheckBoxChecked(dialog);
        CloseConfigurationDialog(dialog, commit: false);
        return value;
    }

    private void CommitFirstConfigurationCheckBox(bool expectedValue, string? armPath = null)
    {
        var dialog = OpenConfigurationDialog();
        if (IsFirstConfigurationCheckBoxChecked(dialog) != expectedValue)
            ToggleFirstConfigurationCheckBox(dialog);
        if (armPath is not null)
        {
            // Measurement uses the same post-startup arming point as crash trials so their write counts are comparable.
            File.WriteAllText(armPath, "armed");
        }
        CloseConfigurationDialog(dialog, commit: true);
        // Fault trials must not kill the process until the asynchronous host commit made the restored baseline durable.
        WaitForConfigurationClearReadOnlyPersistence(expectedValue);
    }

    private void RestoreBaseline(bool baseline)
    {
        CommitFirstConfigurationCheckBox(baseline);
        RestartFileManager();
        Assert.That(ReadFirstConfigurationCheckBox(), Is.EqualTo(baseline), "The next fault trial did not start from a complete baseline profile.");
    }
}
