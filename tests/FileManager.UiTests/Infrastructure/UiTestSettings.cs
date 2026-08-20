using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

internal static class UiTestSettings
{
    internal static string ExecutablePath =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_EXE") ?? string.Empty;

    internal static string Arguments =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_ARGUMENTS") ?? string.Empty;

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

    internal static int RequireFtpOrganizeCommand()
    {
        // Plug-in menu command IDs are allocated by the host at runtime, so the sandboxed test runner supplies this ID.
        if (!int.TryParse(Environment.GetEnvironmentVariable("FILEMANAGER_UI_FTP_ORGANIZE_COMMAND"), out var command) || command <= 0)
            Assert.Ignore("Set FILEMANAGER_UI_FTP_ORGANIZE_COMMAND to the FTP plug-in's runtime Organize Bookmarks command ID to run FTP persistence tests.");

        return command;
    }

    internal static int RequireFtpConnectCommand()
    {
        // Plug-in menu command IDs are allocated by the host at runtime, so use the runner-discovered quick-connect command.
        if (!int.TryParse(Environment.GetEnvironmentVariable("FILEMANAGER_UI_FTP_CONNECT_COMMAND"), out var command) || command <= 0)
            Assert.Ignore("Set FILEMANAGER_UI_FTP_CONNECT_COMMAND to the FTP plug-in's runtime Connect to FTP Server command ID to run protocol fixture tests.");

        return command;
    }

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

    internal static string RequireCrossVolumeRoot()
    {
        var root = Environment.GetEnvironmentVariable("FILEMANAGER_UI_CROSS_VOLUME_ROOT");
        if (string.IsNullOrWhiteSpace(root))
            Assert.Ignore("Set FILEMANAGER_UI_CROSS_VOLUME_ROOT to a filemanager-testdata directory on a second volume to run cross-volume move characterization tests.");

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
            Assert.Ignore("Set FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT to a filemanager-testdata directory on an ADS-unsupported volume to run this test.");

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
