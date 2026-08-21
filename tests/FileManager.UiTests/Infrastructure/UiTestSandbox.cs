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
        // The final fixture boundary may remove an intact root; per-test cleanup keeps its marker for retry safety.
        UiTestSandbox.Cleanup(removeRoots: true);
    }
}

internal static class UiTestSandbox
{
    private const string OwnershipMarkerName = ".filemanager-testdata-owner";
    private const string OwnershipMarkerContents = "Open Salamander UI test sandbox";
    private static readonly HashSet<string> OwnedRoots = new(StringComparer.OrdinalIgnoreCase);
    // Retain known roots across per-test resets so the assembly teardown can remove clean marked directories.
    private static readonly HashSet<string> KnownRoots = new(StringComparer.OrdinalIgnoreCase);
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

    internal static void Cleanup(bool removeRoots = false)
    {
        if (!initialized && (!removeRoots || KnownRoots.Count == 0))
            return;

        var cleanupCompleted = false;
        try
        {
            // The suffix is a fixed contract; deleting only this key cannot touch the user's live configuration root.
            Registry.CurrentUser.DeleteSubKeyTree(UiTestSettings.ConfigurationRegistryRoot, throwOnMissingSubKey: false);
            var roots = removeRoots ? KnownRoots : OwnedRoots;
            foreach (var root in roots.OrderByDescending(path => path.Length))
            {
                EnsureOwnedRoot(root);
                // Preserve the ownership marker until every other entry is gone so a locked file cannot orphan this root.
                ClearOwnedRoot(root, removeRoots);
            }
            cleanupCompleted = true;
        }
        finally
        {
            // Resetting state after a cleanup error lets the next fixture revalidate and retry the marked root.
            OwnedRoots.Clear();
            initialized = false;
            if (removeRoots && cleanupCompleted)
                KnownRoots.Clear();
        }
    }

    private static void RegisterRoot(string root)
    {
        if (Directory.Exists(root))
        {
            EnsureOwnedRoot(root);
            // Reusing an interrupted root must keep its marker until all stale handles have released.
            ClearOwnedRoot(root, removeRoot: false);
        }

        Directory.CreateDirectory(root);
        var marker = Path.Combine(root, OwnershipMarkerName);
        // The marker prevents cleanup from deleting a similarly named directory that was not created by this run.
        File.WriteAllText(marker, OwnershipMarkerContents);
        Directory.CreateDirectory(Path.Combine(root, "temp"));
        // The native roaming-data override creates Open Salamander below this parent on demand.
        Directory.CreateDirectory(Path.Combine(root, "appdata"));
        OwnedRoots.Add(root);
        KnownRoots.Add(root);
    }

    private static void EnsureOwnedRoot(string root)
    {
        var marker = Path.Combine(root, OwnershipMarkerName);
        if (!Directory.Exists(root) || !File.Exists(marker) ||
            !string.Equals(File.ReadAllText(marker), OwnershipMarkerContents, StringComparison.Ordinal))
            Assert.Fail($"Refusing to reuse or delete unowned UI test data directory '{root}'.");
    }

    private static void ClearOwnedRoot(string root, bool removeRoot)
    {
        var marker = Path.Combine(root, OwnershipMarkerName);
        foreach (var entry in Directory.EnumerateFileSystemEntries(root))
        {
            if (string.Equals(entry, marker, StringComparison.OrdinalIgnoreCase))
                continue;

            // Delete reparse points themselves so cleanup never walks beyond this harness-owned root.
            var attributes = File.GetAttributes(entry);
            if ((attributes & FileAttributes.ReparsePoint) != 0)
            {
                if ((attributes & FileAttributes.Directory) != 0)
                    Directory.Delete(entry);
                else
                    File.Delete(entry);
            }
            else if ((attributes & FileAttributes.Directory) != 0)
            {
                FileOperationWorkspace.DeleteDirectoryTree(entry);
            }
            else
            {
                File.Delete(entry);
            }
        }

        if (removeRoot)
        {
            // Delete the marker last: an exception above must leave the root provably owned for a later retry.
            File.Delete(marker);
            Directory.Delete(root);
        }
    }
}
