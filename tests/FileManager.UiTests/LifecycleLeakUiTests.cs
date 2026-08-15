using System.Diagnostics;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[Category("Leak")]
public sealed class LifecycleLeakUiTests : FileManagerUiTestBase
{
    [Test]
    public void Repeated_clean_startup_and_shutdown_does_not_accumulate_process_resources()
    {
        UiTestSettings.RequireTestSandbox();
        var samples = new List<ProcessResourceSnapshot>();
        var cycles = UiTestSettings.RequireLeakLifecycleCycles();

        for (var cycle = 0; cycle < cycles; cycle++)
        {
            // Sampling each fresh process catches profile- or shutdown-induced growth that a single launch hides.
            using var process = Process.GetProcessById(Application.ProcessId);
            samples.Add(ProcessResourceSnapshot.Capture(process));
            Assert.That(MainWindow.IsAvailable, Is.True, $"The main window was unavailable in lifecycle cycle {cycle + 1}.");

            if (cycle + 1 < cycles)
                RestartFileManager();
        }

        AssertBounded(samples.Select(sample => (long)sample.HandleCount), 32, "kernel handles");
        AssertBounded(samples.Select(sample => (long)sample.GdiObjectCount), 12, "GDI objects");
        AssertBounded(samples.Select(sample => (long)sample.UserObjectCount), 12, "USER objects");
        AssertBounded(samples.Select(sample => sample.PrivateBytes), 64L * 1024 * 1024, "private bytes");
    }

    private static void AssertBounded(IEnumerable<long> values, long maximumSpread, string resourceName)
    {
        var sample = values.ToArray();
        Assert.That(sample, Is.Not.Empty);
        // Compare the whole run so a monotonic leak cannot be hidden by selecting only the first and final process.
        Assert.That(sample.Max() - sample.Min(), Is.LessThanOrEqualTo(maximumSpread),
                    $"{resourceName} varied by more than the approved lifecycle budget.");
    }
}
