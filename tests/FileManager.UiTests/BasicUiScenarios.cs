using NUnit.Framework;

namespace FileManager.UiTests;

public enum UiScenarioKind
{
    MainWindow,
    AccessibilityTree,
    ConfigurationCancel,
    ConfigurationCommit,
    ConfigurationPersistence,
    RestartAfterCommit,
    FtpBookmarkCreationPersists,
}

public sealed record UiScenario(int Number, UiScenarioKind Kind, string Description);

internal static class BasicUiScenarios
{
    internal static IEnumerable<TestCaseData> All
    {
        get
        {
            // The nightly lock stress lane reuses these lifecycle repeats to expose leaked windows, stale handles, and lock defects.
            foreach (var scenario in CreateScenarios())
                yield return new TestCaseData(scenario)
                    .SetName($"UI_{scenario.Number:000}_{scenario.Kind}_{scenario.Description}")
                    .SetCategory("UI")
                    .SetCategory("LockStress");
        }
    }

    private static IEnumerable<UiScenario> CreateScenarios()
    {
        // The main gate samples distinct lifecycle risks once; prolonged repetition belongs to a separately scheduled soak.
        return new[]
        {
            new UiScenario(1, UiScenarioKind.MainWindow, "Cold_start"),
            new UiScenario(2, UiScenarioKind.AccessibilityTree, "Owner_drawn_accessibility"),
            new UiScenario(3, UiScenarioKind.ConfigurationCancel, "Discarded_settings"),
            new UiScenario(4, UiScenarioKind.ConfigurationCommit, "Committed_settings"),
            new UiScenario(5, UiScenarioKind.ConfigurationPersistence, "Persisted_settings_restart"),
            new UiScenario(6, UiScenarioKind.RestartAfterCommit, "Restart_after_settings_commit"),
            new UiScenario(7, UiScenarioKind.FtpBookmarkCreationPersists, "Plugin_profile_persistence"),
        };
    }
}
