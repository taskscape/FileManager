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
        var number = 1;
        // The distribution keeps the suite at exactly 100 cases while giving persistence paths dedicated coverage.
        foreach (var (kind, runs) in new[]
                 {
                     (UiScenarioKind.MainWindow, 15),
                     (UiScenarioKind.AccessibilityTree, 15),
                     (UiScenarioKind.ConfigurationCancel, 15),
                     (UiScenarioKind.ConfigurationCommit, 15),
                     (UiScenarioKind.ConfigurationPersistence, 15),
                     (UiScenarioKind.RestartAfterCommit, 15),
                     (UiScenarioKind.FtpBookmarkCreationPersists, 10),
                 })
        {
            for (var repetition = 1; repetition <= runs; repetition++)
                yield return new UiScenario(number++, kind, $"Run_{repetition:00}");
        }
    }
}
