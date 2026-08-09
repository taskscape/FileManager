using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

internal static class UiTestSettings
{
    internal static string ExecutablePath =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_EXE") ?? string.Empty;

    internal static string Arguments =>
        Environment.GetEnvironmentVariable("FILEMANAGER_UI_ARGUMENTS") ?? string.Empty;

    internal static int RequireFtpOrganizeCommand()
    {
        // Plug-in menu command IDs are allocated by the host at runtime, so the isolated test runner supplies this ID.
        if (!int.TryParse(Environment.GetEnvironmentVariable("FILEMANAGER_UI_FTP_ORGANIZE_COMMAND"), out var command) || command <= 0)
            Assert.Ignore("Set FILEMANAGER_UI_FTP_ORGANIZE_COMMAND to the FTP plug-in's runtime Organize Bookmarks command ID to run FTP persistence tests.");

        return command;
    }

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
}
