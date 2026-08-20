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
            var sample = ProcessResourceSnapshot.Capture(process);
            samples.Add(sample);
            // Preserve resource evidence in CI so a future budget failure identifies bootstrap or steady-state variation.
            TestContext.Progress.WriteLine($"Lifecycle cycle {cycle + 1}: handles={sample.HandleCount}, GDI={sample.GdiObjectCount}, USER={sample.UserObjectCount}, private={sample.PrivateBytes}.");
            Assert.That(MainWindow.IsAvailable, Is.True, $"The main window was unavailable in lifecycle cycle {cycle + 1}.");

            if (cycle + 1 < cycles)
                RestartFileManager();
        }

        // A fresh isolated profile installs bundled plug-ins only in cycle one; leak budgets compare the subsequent equivalent warm starts.
        var steadyStateSamples = samples.Skip(1).ToArray();
        AssertBounded(steadyStateSamples.Select(sample => (long)sample.HandleCount), 32, "kernel handles");
        // Independent warm starts load optional plug-in artwork at different points; bound that variation without masking growth.
        AssertBounded(steadyStateSamples.Select(sample => (long)sample.GdiObjectCount), 64, "GDI objects");
        AssertBounded(steadyStateSamples.Select(sample => (long)sample.UserObjectCount), 12, "USER objects");
        AssertBounded(steadyStateSamples.Select(sample => sample.PrivateBytes), 64L * 1024 * 1024, "private bytes");

        // A true cross-restart leak raises the later population even when individual asynchronous samples are noisy.
        AssertNoUpwardShift(steadyStateSamples.Select(sample => (long)sample.HandleCount), 16, "kernel handles");
        AssertNoUpwardShift(steadyStateSamples.Select(sample => (long)sample.GdiObjectCount), 16, "GDI objects");
        AssertNoUpwardShift(steadyStateSamples.Select(sample => (long)sample.UserObjectCount), 6, "USER objects");
        AssertNoUpwardShift(steadyStateSamples.Select(sample => sample.PrivateBytes), 16L * 1024 * 1024, "private bytes");
    }

    private static void AssertBounded(IEnumerable<long> values, long maximumSpread, string resourceName)
    {
        var sample = values.ToArray();
        Assert.That(sample, Is.Not.Empty);
        // Compare the whole run so a monotonic leak cannot be hidden by selecting only the first and final process.
        Assert.That(sample.Max() - sample.Min(), Is.LessThanOrEqualTo(maximumSpread),
                    $"{resourceName} varied by more than the approved lifecycle budget.");
    }

    private static void AssertNoUpwardShift(IEnumerable<long> values, long maximumShift, string resourceName)
    {
        var sample = values.ToArray();
        var midpoint = sample.Length / 2;
        // Population means retain monotonic leak sensitivity without letting one asynchronous plug-in sample dominate the verdict.
        var earlyMean = sample.Take(midpoint).Average();
        var lateMean = sample.Skip(midpoint).Average();
        Assert.That(lateMean - earlyMean, Is.LessThanOrEqualTo(maximumShift),
                    $"{resourceName} accumulated across repeated clean restarts.");
    }
}
