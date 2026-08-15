using Microsoft.Win32;
using NUnit.Framework;
using FileManager.UiTests.Infrastructure;

namespace FileManager.UiTests;

[SetUpFixture]
public sealed class UiTestSandboxLifetime
{
    [OneTimeTearDown]
    public void RemoveSandboxAfterTheTestRun()
    {
        // NUnit invokes this after both successful and failed cases, preserving the current user's normal profile.
        UiTestSandbox.Cleanup();
    }
}

internal static class UiTestSandbox
{
    private const string OwnershipMarkerName = ".filemanager-testdata-owner";
    private const string OwnershipMarkerContents = "Open Salamander UI test sandbox";
    private static readonly HashSet<string> OwnedRoots = new(StringComparer.OrdinalIgnoreCase);
    private static bool initialized;

    internal static void EnsureInitialized()
    {
        if (initialized)
            return;

        // A fixed, guarded name keeps deletion limited to a directory the harness explicitly owns.
        RegisterRoot(UiTestSettings.TestDataRoot);
        Registry.CurrentUser.DeleteSubKeyTree(UiTestSettings.ConfigurationRegistryRoot, throwOnMissingSubKey: false);
        using var configurationKey = Registry.CurrentUser.CreateSubKey(UiTestSettings.ConfigurationRegistryRoot, writable: true);
        Assert.That(configurationKey, Is.Not.Null, "The UI test configuration key could not be created.");
        initialized = true;
    }

    internal static void RegisterAdditionalRoot(string root)
    {
        EnsureInitialized();
        RegisterRoot(root);
    }

    internal static void Cleanup()
    {
        if (!initialized)
            return;

        // The suffix is a fixed contract; deleting only this key cannot touch the user's live configuration root.
        Registry.CurrentUser.DeleteSubKeyTree(UiTestSettings.ConfigurationRegistryRoot, throwOnMissingSubKey: false);
        foreach (var root in OwnedRoots.OrderByDescending(path => path.Length))
        {
            var marker = Path.Combine(root, OwnershipMarkerName);
            if (Directory.Exists(root) && File.Exists(marker) &&
                string.Equals(File.ReadAllText(marker), OwnershipMarkerContents, StringComparison.Ordinal))
                // A failed reparse-point test must remove its owned links without traversing their targets.
                FileOperationWorkspace.DeleteDirectoryTree(root);
        }
        OwnedRoots.Clear();
        initialized = false;
    }

    private static void RegisterRoot(string root)
    {
        var marker = Path.Combine(root, OwnershipMarkerName);
        if (Directory.Exists(root))
        {
            if (!File.Exists(marker) || !string.Equals(File.ReadAllText(marker), OwnershipMarkerContents, StringComparison.Ordinal))
                Assert.Fail($"Refusing to reuse or delete unowned UI test data directory '{root}'.");
            // Reuse has the same reparse-safe cleanup requirement as final teardown after an interrupted test run.
            FileOperationWorkspace.DeleteDirectoryTree(root);
        }

        Directory.CreateDirectory(root);
        // The marker prevents cleanup from deleting a similarly named directory that was not created by this run.
        File.WriteAllText(marker, OwnershipMarkerContents);
        Directory.CreateDirectory(Path.Combine(root, "temp"));
        // The native roaming-data override creates Open Salamander below this parent on demand.
        Directory.CreateDirectory(Path.Combine(root, "appdata"));
        OwnedRoots.Add(root);
    }
}
