using NUnit.Framework;

namespace FileManager.UiTests.Infrastructure;

internal static class AlternateDataStreams
{
    internal static void RequireSupportAt(string directory)
    {
        var file = Path.Combine(directory, $"ads-probe-{Guid.NewGuid():N}.tmp");
        var stream = StreamPath(file, "probe");
        try
        {
            File.WriteAllBytes(file, [0]);
            File.WriteAllBytes(stream, [0x5a]);

            if (!File.ReadAllBytes(stream).SequenceEqual(new byte[] { 0x5a }))
                Assert.Ignore($"The filesystem at '{directory}' did not preserve alternate data stream content.");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            Assert.Ignore($"The filesystem at '{directory}' does not support alternate data streams: {exception.Message}");
        }
        finally
        {
            TryDelete(stream);
            TryDelete(file);
        }
    }

    internal static void RequireUnsupportedAt(string directory)
    {
        var file = Path.Combine(directory, $"ads-probe-{Guid.NewGuid():N}.tmp");
        var stream = StreamPath(file, "probe");
        try
        {
            File.WriteAllBytes(file, [0]);
            File.WriteAllBytes(stream, [0x5a]);
            Assert.Ignore($"The configured unsupported ADS target '{directory}' preserves alternate data streams.");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or NotSupportedException)
        {
            // The configured target rejected the ADS probe as required by this test lane.
        }
        finally
        {
            TryDelete(stream);
            TryDelete(file);
        }
    }

    internal static void Write(string file, string name, byte[] content) =>
        File.WriteAllBytes(StreamPath(file, name), content);

    internal static void AssertContent(string file, string name, byte[] expected) =>
        Assert.That(File.ReadAllBytes(StreamPath(file, name)), Is.EqualTo(expected),
                    $"Alternate data stream '{name}' was not preserved for '{file}'.");

    internal static void AssertAbsent(string file, string name) =>
        Assert.That(File.Exists(StreamPath(file, name)), Is.False,
                    $"Alternate data stream '{name}' unexpectedly exists for '{file}'.");

    internal static FileStream LockForRead(string file, string name) =>
        new(StreamPath(file, name), FileMode.Open, FileAccess.Read, FileShare.None);

    private static string StreamPath(string file, string name) => $"{file}:{name}";

    private static void TryDelete(string path)
    {
        try
        {
            File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
