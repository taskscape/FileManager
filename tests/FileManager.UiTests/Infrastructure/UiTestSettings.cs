using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

internal static class UiTestSettings
{
    internal static string ExecutablePath =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_EXE") ?? string.Empty;

    internal static string Arguments =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_ARGUMENTS") ?? string.Empty;

    internal static string ExecutionTranscriptRoot
    {
        get
        {
            var configuredRoot = Environment.GetEnvironmentVariable("FILEMANAGER_UI_TRANSCRIPT_ROOT");
            // Direct IDE runs still retain diagnostics even when the aggregate runner did not provide an artifact directory.
            return Path.GetFullPath(string.IsNullOrWhiteSpace(configuredRoot)
                ? Path.Combine(TestContext.CurrentContext.WorkDirectory, "TestResults", "ui-test-transcripts")
                : configuredRoot);
        }
    }

    internal const string ConfigurationRegistryRoot = "Software\\Open Salamander\\6.0-filemanager-testdata";

    internal static string TestDataRoot
    {
        get
        {
            var configuredRoot = Environment.GetEnvironmentVariable("FILEMANAGER_UI_TESTDATA_ROOT");
            if (string.IsNullOrWhiteSpace(configuredRoot))
                Assert.Ignore("Set FILEMANAGER_UI_TESTDATA_ROOT to an absolute filemanager-testdata directory before running UI tests.");

            var fullRoot = Path.GetFullPath(configuredRoot);
            if (!string.Equals(Path.GetFileName(fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)),
                               "filemanager-testdata", StringComparison.OrdinalIgnoreCase))
                Assert.Ignore("FILEMANAGER_UI_TESTDATA_ROOT must name the disposable filemanager-testdata directory.");
            return fullRoot;
        }
    }

    // Each launched process records its dynamic plug-in menu IDs below the harness-owned test-data boundary.
    internal static string PluginCommandMapPath => Path.Combine(TestDataRoot, "ui-test-plugin-commands.log");

    internal static void RequireTestSandbox()
    {
        // A current-user run is safe only when both the file and registry boundaries are explicitly selected.
        _ = TestDataRoot;
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_CONFIG_ROOT"), ConfigurationRegistryRoot,
                           StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("Set FILEMANAGER_UI_CONFIG_ROOT to the documented -filemanager-testdata registry key before running UI tests.");

        if (string.IsNullOrWhiteSpace(ExecutablePath) || !File.Exists(ExecutablePath))
            Assert.Ignore("Set FILEMANAGER_UI_EXE to an existing FileManager executable before running UI tests.");
    }

    internal static void RequireConfigurationFaultInjection()
    {
        RequireTestSandbox();
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_CONFIG_FAULT_INJECTION"), "1", StringComparison.Ordinal))
            Assert.Ignore("Set FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1 to run the structural configuration write-boundary recovery test.");
    }

    internal static void RequireZipPlugin()
    {
        RequireTestSandbox();
        // The opt-in confirms deployment and enablement because the public UI cannot distinguish a disabled plug-in from an absent one.
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_ZIP_PLUGIN"), "1", StringComparison.Ordinal))
            Assert.Ignore("Install and enable the Zip plug-in, then set FILEMANAGER_UI_ZIP_PLUGIN=1 to run ZIP navigation characterization.");
    }

    internal static (string SearchTerm, string ExpectedResult) RequireHelpSearchFixture()
    {
        RequireTestSandbox();
        var term = Environment.GetEnvironmentVariable("FILEMANAGER_UI_HELP_SEARCH_TERM");
        var expected = Environment.GetEnvironmentVariable("FILEMANAGER_UI_HELP_EXPECTED_RESULT");
        // Language-specific input keeps the contract meaningful for every deployed CHM rather than assuming English result text.
        if (string.IsNullOrWhiteSpace(term) || string.IsNullOrWhiteSpace(expected))
            Assert.Ignore("Deploy salamand.chm and set FILEMANAGER_UI_HELP_SEARCH_TERM plus FILEMANAGER_UI_HELP_EXPECTED_RESULT for its language.");

        return (term!, expected!);
    }

    internal static string RequireCrossVolumeRoot()
    {
        var root = Environment.GetEnvironmentVariable("FILEMANAGER_UI_CROSS_VOLUME_ROOT");
        if (string.IsNullOrWhiteSpace(root))
            // The runner turns an unavailable writable D: capability into an allowed skip for the complete secondary-volume lane.
            Assert.Ignore(Environment.GetEnvironmentVariable("FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON") ??
                          "Second-volume UI tests skipped: fixed writable D:\\ is unavailable; all tests that depend on a second volume have been skipped.");

        var sourceVolume = Path.GetPathRoot(TestDataRoot);
        var fullRoot = Path.GetFullPath(root);
        var targetVolume = Path.GetPathRoot(fullRoot);
        if (string.Equals(sourceVolume, targetVolume, StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_CROSS_VOLUME_ROOT must be on a different volume from the temporary directory.");

        if (!string.Equals(Path.GetFileName(fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)),
                           "filemanager-testdata", StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_CROSS_VOLUME_ROOT must name a disposable filemanager-testdata directory.");
        UiTestSandbox.RegisterAdditionalRoot(fullRoot);
        return fullRoot;
    }

    internal static string RequireUnsupportedAdsTargetRoot()
    {
        var root = Environment.GetEnvironmentVariable("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT");
        if (string.IsNullOrWhiteSpace(root))
            // NTFS D: can run cross-volume cases but cannot provide the ADS-loss boundary, so this lane remains an allowed capability skip.
            Assert.Ignore(Environment.GetEnvironmentVariable("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_SKIP_REASON") ??
                          Environment.GetEnvironmentVariable("FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON") ??
                          "Second-volume UI tests skipped: fixed writable D:\\ is unavailable; all tests that depend on a second volume have been skipped.");

        var sourceVolume = Path.GetPathRoot(TestDataRoot);
        var fullRoot = Path.GetFullPath(root);
        var targetVolume = Path.GetPathRoot(fullRoot);
        if (string.Equals(sourceVolume, targetVolume, StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT must be on a different volume from the temporary directory.");

        if (!string.Equals(Path.GetFileName(fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)),
                           "filemanager-testdata", StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT must name a disposable filemanager-testdata directory.");
        UiTestSandbox.RegisterAdditionalRoot(fullRoot);
        return fullRoot;
    }

    internal static void RequireRecycleBinTest()
    {
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_RECYCLE_BIN"), "1", StringComparison.Ordinal))
            Assert.Ignore("Set FILEMANAGER_UI_RECYCLE_BIN=1 with the default recycle-bin setting enabled to run this test.");
    }

    internal static int RequireLeakLifecycleCycles()
    {
        RequireTestSandbox();
        var rawValue = Environment.GetEnvironmentVariable("FILEMANAGER_UI_LEAK_CYCLES");
        // A bounded override keeps the release gate practical while the nightly lane can run a longer soak.
        if (string.IsNullOrWhiteSpace(rawValue))
            return 20;
        if (!int.TryParse(rawValue, out var cycles) || cycles < 5 || cycles > 200)
            Assert.Ignore("FILEMANAGER_UI_LEAK_CYCLES must be an integer from 5 through 200.");
        return cycles;
    }

    internal static string JournalDirectory => Path.Combine(TestDataRoot, "appdata", "Open Salamander", "operation-journals");
}
