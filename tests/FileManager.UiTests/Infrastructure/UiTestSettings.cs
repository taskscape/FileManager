using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

internal static class UiTestSettings
{
    internal static string ExecutablePath =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_EXE") ?? string.Empty;

    internal static string Arguments =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_ARGUMENTS") ?? string.Empty;

    internal static void RequireIsolatedProfile()
    {
        // UI tests save FileManager configuration, so prevent accidental execution against a developer profile.
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_ISOLATED"), "1", StringComparison.Ordinal))
            Assert.Ignore("Set FILEMANAGER_UI_ISOLATED=1 and use a dedicated Windows test profile to run UI tests.");

        if (string.IsNullOrWhiteSpace(ExecutablePath) || !File.Exists(ExecutablePath))
            Assert.Ignore("Set FILEMANAGER_UI_EXE to an existing FileManager executable before running UI tests.");
    }

    internal static void RequireConfigurationFaultInjection()
    {
        RequireIsolatedProfile();
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_CONFIG_FAULT_INJECTION"), "1", StringComparison.Ordinal))
            Assert.Ignore("Set FILEMANAGER_UI_CONFIG_FAULT_INJECTION=1 to run the exhaustive configuration write-boundary recovery test.");
    }

    internal static int LimitConfigurationFaultBoundaries(int measuredCount)
    {
        // Optional smoke runs may cap the matrix; the unset default remains exhaustive for the dedicated recovery lane.
        return int.TryParse(Environment.GetEnvironmentVariable("FILEMANAGER_UI_CONFIG_FAULT_BOUNDARY_LIMIT"), out var limit) && limit > 0
            ? Math.Min(measuredCount, limit)
            : measuredCount;
    }

    internal static void RequireZipPlugin()
    {
        RequireIsolatedProfile();
        // The opt-in confirms both deployment and enablement because a disabled plug-in is indistinguishable through the public UI.
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_ZIP_PLUGIN"), "1", StringComparison.Ordinal))
            Assert.Ignore("Install and enable the Zip plug-in, then set FILEMANAGER_UI_ZIP_PLUGIN=1 to run ZIP navigation characterization.");
    }

    internal static (string SearchTerm, string ExpectedResult) RequireHelpSearchFixture()
    {
        RequireIsolatedProfile();
        var term = Environment.GetEnvironmentVariable("FILEMANAGER_UI_HELP_SEARCH_TERM");
        var expected = Environment.GetEnvironmentVariable("FILEMANAGER_UI_HELP_EXPECTED_RESULT");
        if (string.IsNullOrWhiteSpace(term) || string.IsNullOrWhiteSpace(expected))
            Assert.Ignore("Deploy salamand.chm and set FILEMANAGER_UI_HELP_SEARCH_TERM plus FILEMANAGER_UI_HELP_EXPECTED_RESULT for its language.");

        return (term!, expected!);
    }

    internal static string RequireCrossVolumeRoot()
    {
        var root = Environment.GetEnvironmentVariable("FILEMANAGER_UI_CROSS_VOLUME_ROOT");
        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
            Assert.Ignore("Set FILEMANAGER_UI_CROSS_VOLUME_ROOT to an existing dedicated directory on a second volume to run cross-volume move characterization tests.");

        var sourceVolume = Path.GetPathRoot(Path.GetTempPath());
        var targetVolume = Path.GetPathRoot(Path.GetFullPath(root));
        if (string.Equals(sourceVolume, targetVolume, StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_CROSS_VOLUME_ROOT must be on a different volume from the temporary directory.");

        return Path.GetFullPath(root);
    }

    internal static string RequireUnsupportedAdsTargetRoot()
    {
        var root = Environment.GetEnvironmentVariable("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT");
        if (string.IsNullOrWhiteSpace(root) || !Directory.Exists(root))
            Assert.Ignore("Set FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT to an existing dedicated directory on an ADS-unsupported volume to run this test.");

        var sourceVolume = Path.GetPathRoot(Path.GetTempPath());
        var targetVolume = Path.GetPathRoot(Path.GetFullPath(root));
        if (string.Equals(sourceVolume, targetVolume, StringComparison.OrdinalIgnoreCase))
            Assert.Ignore("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT must be on a different volume from the temporary directory.");

        return Path.GetFullPath(root);
    }

    internal static void RequireRecycleBinTest()
    {
        if (!string.Equals(Environment.GetEnvironmentVariable("FILEMANAGER_UI_RECYCLE_BIN"), "1", StringComparison.Ordinal))
            Assert.Ignore("Set FILEMANAGER_UI_RECYCLE_BIN=1 in an isolated profile with the default recycle-bin setting enabled to run this test.");
    }
}
