using System.Diagnostics;
using Microsoft.Win32;
using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

/// <summary>
/// Installs a harness-owned external editor inside the disposable test-data
/// root so the Edit case never drives an application instance it does not own.
///
/// The product seeds <c>notepad.exe</c> for <c>*.*</c>
/// (<c>src/mainwnd_init.cpp</c>). On Windows 11 that resolves to the packaged,
/// tabbed Notepad, which opens additional files as tabs in an existing window:
/// the lane would then inspect - and close - a window that can hold unsaved
/// documents belonging to whoever is running the tests. This stub is copied
/// into the test-data root and produces exactly one process and one window per
/// Edit dispatch.
/// </summary>
internal static class SandboxEditor
{
    internal const string WindowTitlePrefix = "SandboxEditor - ";

    /// <summary>The editor command the product seeds into a fresh profile.</summary>
    internal const string SeededCommand = "notepad.exe";

    private const string StubFileName = "SandboxEditor.exe";

    /// <summary>Editor directory below the disposable test-data root.</summary>
    internal static string InstallDirectory => Path.Combine(UiTestSettings.TestDataRoot, "editor");

    internal static string ExecutablePath => Path.Combine(InstallDirectory, StubFileName);

    /// <summary>
    /// Copies the stub into the sandbox and returns its path. Skips the case
    /// rather than falling back to the machine's editor when the stub is absent.
    /// </summary>
    internal static string Install()
    {
        var source = LocateBuiltStub();
        if (source is null)
            Assert.Ignore($"The sandbox editor stub was not built; {StubFileName} was not found beside the test assembly.");

        Directory.CreateDirectory(InstallDirectory);
        // The stub is framework-dependent, so its runtimeconfig/deps files must
        // travel with it for the shared runtime to host it.
        foreach (var file in Directory.EnumerateFiles(Path.GetDirectoryName(source!)!))
        {
            var name = Path.GetFileName(file);
            if (name.StartsWith("SandboxEditor.", StringComparison.OrdinalIgnoreCase))
                File.Copy(file, Path.Combine(InstallDirectory, name), overwrite: true);
        }

        Assert.That(File.Exists(ExecutablePath), Is.True, "The sandbox editor stub was not installed into the test-data root.");
        return ExecutablePath;
    }

    /// <summary>
    /// The command, arguments and working directory the sandboxed profile must
    /// use. The argument shape mirrors the product default: the name as the
    /// argument and the containing directory as the working directory.
    /// </summary>
    internal static (string Command, string Arguments, string InitialDirectory) ProfileEntry()
        => (Install(), "\"$(Name)\"", "$(FullPath)");

    /// <summary>
    /// Waits until the committed profile carries the sandbox editor. The
    /// product writes a configuration generation after its property sheet
    /// closes, so restarting before that lands would silently discard the
    /// change and fall back to the seeded editor.
    /// </summary>
    internal static void WaitForProfileEntry(string expectedCommand)
    {
        var timeout = DateTime.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTime.UtcNow < timeout)
        {
            // Read-only: a committed generation is checksum-protected and must
            // only ever be written by the product itself.
            using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
            if (root?.GetValue("Active Generation") is int generation)
            {
                using var entry = root.OpenSubKey(
                    $"Configuration Generations\\Generation {generation}\\Editors\\1");
                if (entry?.GetValue("Command") is string command &&
                    string.Equals(command, expectedCommand, StringComparison.OrdinalIgnoreCase))
                    return;
            }
            Thread.Sleep(100);
        }

        Assert.Fail("The committed configuration profile did not record the sandbox editor.");
    }

    /// <summary>
    /// Stops any stub still running from this sandbox. Only processes whose
    /// image is the copy inside the test-data root are touched, so an editor
    /// belonging to the person running the tests can never be affected.
    /// </summary>
    internal static void StopStrayInstances()
    {
        foreach (var process in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(StubFileName)))
        {
            try
            {
                if (string.Equals(process.MainModule?.FileName, ExecutablePath, StringComparison.OrdinalIgnoreCase))
                    process.Kill();
            }
            catch (Exception ex) when (ex is InvalidOperationException || ex is System.ComponentModel.Win32Exception)
            {
                // The stub exited while it was being inspected.
            }
            finally
            {
                process.Dispose();
            }
        }
    }

    private static string? LocateBuiltStub()
    {
        // The stub builds beside the test assembly through its project reference;
        // fall back to its own output folder for a non-default build layout.
        var testDirectory = Path.GetDirectoryName(typeof(SandboxEditor).Assembly.Location);
        if (string.IsNullOrWhiteSpace(testDirectory))
            return null;

        var candidates = new List<string> { Path.Combine(testDirectory, StubFileName) };
        var repositoryTests = Path.GetFullPath(Path.Combine(testDirectory, "..", "..", "..", ".."));
        var stubOutput = Path.Combine(repositoryTests, "FileManager.UiTests.EditorStub", "bin");
        if (Directory.Exists(stubOutput))
            candidates.AddRange(Directory.EnumerateFiles(stubOutput, StubFileName, SearchOption.AllDirectories));

        return candidates.FirstOrDefault(File.Exists);
    }
}
