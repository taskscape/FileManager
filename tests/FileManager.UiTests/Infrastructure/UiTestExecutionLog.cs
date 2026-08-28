using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace FileManager.UiTests.Infrastructure;

/// <summary>
/// Retains one chronological, human-readable transcript for every test that launches salamand.exe.
/// </summary>
internal sealed class UiTestExecutionLog : IDisposable
{
    private static readonly Encoding NativeAnsiEncoding = CreateNativeAnsiEncoding();
    private static readonly HashSet<string> TextCrashReportExtensions =
        new(StringComparer.OrdinalIgnoreCase) { ".TXT", ".INF", ".OPS", ".BUG" };
    private static readonly HashSet<string> BinaryCrashReportExtensions =
        new(StringComparer.OrdinalIgnoreCase) { ".DMP", ".7Z" };

    private readonly object writeGate = new();
    private readonly object observationGate = new();
    private readonly StreamWriter writer;
    private readonly Stopwatch elapsed = Stopwatch.StartNew();
    private readonly ConcurrentDictionary<int, string> observedProcesses = new();
    private readonly Dictionary<(int ProcessId, nint WindowHandle), string> windowSnapshots = [];
    private readonly HashSet<int> reportedProcessExits = [];
    private readonly CancellationTokenSource observerCancellation = new();
    private readonly Task observerTask;
    private string? productDialogTranscriptPath;
    private int productDialogLinesRead;
    private long sequence;
    private bool disposed;

    private UiTestExecutionLog(string outputRoot, string testName, string fullTestName, string testId)
    {
        Directory.CreateDirectory(outputRoot);
        var safeName = MakeSafeFileName(testName);
        Path = System.IO.Path.Combine(
            outputRoot,
            $"{DateTime.UtcNow:yyyyMMddTHHmmssfffZ}-{safeName}-{testId}-{Guid.NewGuid():N}.log");
        writer = new StreamWriter(new FileStream(Path, FileMode.CreateNew, FileAccess.Write, FileShare.Read),
                                  new UTF8Encoding(encoderShouldEmitUTF8Identifier: false))
        {
            AutoFlush = true,
        };

        // A self-describing header lets a retained file be understood without its TRX container.
        writer.WriteLine("Open Salamander UI test execution transcript");
        writer.WriteLine("format-version: 1");
        writer.WriteLine($"test: {fullTestName}");
        writer.WriteLine($"test-id: {testId}");
        writer.WriteLine($"started-utc: {DateTime.UtcNow:O}");
        writer.WriteLine($"machine: {Environment.MachineName}");
        writer.WriteLine($"process-architecture: {RuntimeInformation.ProcessArchitecture}");
        writer.WriteLine();
        writer.WriteLine("Columns: sequence | elapsed | UTC | category | event/details");
        writer.WriteLine();

        // Polling native HWNDs complements the product-side dialog transcript by also seeing shell and crash-reporter UI.
        observerTask = Task.Run(ObserveWindowsAsync);
    }

    internal string Path { get; }

    internal static UiTestExecutionLog Start(string outputRoot, string testName, string fullTestName, string testId) =>
        new(outputRoot, testName, fullTestName, testId);

    internal void Record(string category, string message)
    {
        lock (writeGate)
        {
            if (disposed)
                return;

            var eventSequence = ++sequence;
            var timestamp = DateTime.UtcNow;
            var normalized = NormalizeLineEndings(message);
            var lines = normalized.Split('\n');
            writer.WriteLine(FormattableString.Invariant(
                $"{eventSequence:D6} | +{elapsed.Elapsed.TotalSeconds,10:0.000}s | {timestamp:O} | {category} | {lines[0]}"));
            for (var index = 1; index < lines.Length; index++)
                writer.WriteLine($"       |             |                              |          | {lines[index]}");
        }
    }

    internal void RecordException(string category, Exception exception) =>
        Record(category, $"{exception.GetType().FullName}: {exception.Message}\n{exception.StackTrace ?? "<no stack trace>"}");

    internal void ObserveProcess(int processId, string role)
    {
        if (observedProcesses.TryAdd(processId, role))
            Record("PROCESS", $"observe role={role} pid={processId}");
    }

    internal void WatchProductDialogTranscript(string path)
    {
        lock (observationGate)
        {
            productDialogTranscriptPath = path;
            productDialogLinesRead = 0;
        }
        Record("PRODUCT-DIALOGS", $"watch path={Quote(path)}");
    }

    internal void CaptureCurrentWindows(string reason)
    {
        Record("OBSERVER", $"snapshot reason={reason}");
        CaptureWindowChanges(forceAll: true);
    }

    internal void CaptureProductDiagnostics(string testDataRoot)
    {
        // Product-side records catch prompts which open and close between observer polls.
        productDialogTranscriptPath ??= System.IO.Path.Combine(testDataRoot, "ui-test-dialogs.log");
        CaptureProductDialogEvents();
        Record("PRODUCT-DIALOGS",
               File.Exists(productDialogTranscriptPath)
                   ? $"complete lines={productDialogLinesRead} path={Quote(productDialogTranscriptPath)}"
                   : $"absent path={Quote(productDialogTranscriptPath)}");

        var reportRoot = System.IO.Path.Combine(testDataRoot, "appdata", "Open Salamander");
        if (!Directory.Exists(reportRoot))
        {
            Record("CRASH-REPORT", $"artifact-directory absent path={Quote(reportRoot)}");
            return;
        }

        var reportFiles = Directory.EnumerateFiles(reportRoot, "*", SearchOption.TopDirectoryOnly)
            .Where(file => TextCrashReportExtensions.Contains(System.IO.Path.GetExtension(file)) ||
                           BinaryCrashReportExtensions.Contains(System.IO.Path.GetExtension(file)))
            .OrderBy(file => file, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        if (reportFiles.Length == 0)
        {
            Record("CRASH-REPORT", $"no artifacts path={Quote(reportRoot)}");
            return;
        }

        var artifactDirectory = System.IO.Path.ChangeExtension(Path, null) + ".artifacts";
        Directory.CreateDirectory(artifactDirectory);
        foreach (var reportFile in reportFiles)
        {
            var info = new FileInfo(reportFile);
            var retainedPath = System.IO.Path.Combine(artifactDirectory, info.Name);
            File.Copy(reportFile, retainedPath, overwrite: true);
            Record("CRASH-REPORT",
                   $"retained name={Quote(info.Name)} bytes={info.Length} modified-utc={info.LastWriteTimeUtc:O} path={Quote(retainedPath)}");
            if (TextCrashReportExtensions.Contains(info.Extension))
                AppendTextFile("CRASH-REPORT-TEXT", reportFile, 262_144);
        }
    }

    public void Dispose()
    {
        lock (writeGate)
        {
            if (disposed)
                return;
        }

        observerCancellation.Cancel();
        try
        {
            observerTask.Wait(TimeSpan.FromSeconds(2));
        }
        catch (AggregateException exception) when (exception.InnerExceptions.All(inner => inner is TaskCanceledException))
        {
            // Cancellation is the normal observer shutdown path at the end of each test.
        }

        CaptureWindowChanges(forceAll: false);
        Record("HARNESS", "transcript.complete");
        lock (writeGate)
        {
            disposed = true;
            writer.Dispose();
        }
        observerCancellation.Dispose();
    }

    private async Task ObserveWindowsAsync()
    {
        try
        {
            while (!observerCancellation.IsCancellationRequested)
            {
                CaptureProductDialogEvents();
                CaptureWindowChanges(forceAll: false);
                await Task.Delay(100, observerCancellation.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (observerCancellation.IsCancellationRequested)
        {
            // The owning test always cancels this loop before closing its transcript.
        }
        catch (Exception exception)
        {
            RecordException("OBSERVER-ERROR", exception);
        }
    }

    private void CaptureProductDialogEvents()
    {
        lock (observationGate)
        {
            var sourcePath = productDialogTranscriptPath;
            if (string.IsNullOrWhiteSpace(sourcePath) || !File.Exists(sourcePath))
                return;

            try
            {
                // The native writer appends one complete CRLF record per handle-open, so only completed lines advance the cursor.
                string contents;
                using (var reader = new StreamReader(new FileStream(sourcePath, FileMode.Open, FileAccess.Read,
                                                                    FileShare.ReadWrite | FileShare.Delete),
                                                     NativeAnsiEncoding, detectEncodingFromByteOrderMarks: true))
                    contents = reader.ReadToEnd();
                var normalized = NormalizeLineEndings(contents);
                var lines = normalized.Split('\n');
                var completedLineCount = lines.Length - 1;
                for (var index = productDialogLinesRead; index < completedLineCount; index++)
                {
                    if (lines[index].Length != 0)
                        Record("PRODUCT-DIALOGS", lines[index]);
                }
                productDialogLinesRead = Math.Max(productDialogLinesRead, completedLineCount);
            }
            catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
            {
                Record("OBSERVER-ERROR", $"dialog-transcript path={Quote(sourcePath)} error={Quote(exception.Message)}");
            }
        }
    }

    private void CaptureWindowChanges(bool forceAll)
    {
        lock (observationGate)
        {
            var liveWindows = new HashSet<(int ProcessId, nint WindowHandle)>();
            foreach (var (processId, role) in observedProcesses.OrderBy(pair => pair.Key))
            {
                IReadOnlyList<nint> handles;
                try
                {
                    handles = NativeCommands.GetTopLevelWindows(processId);
                }
                catch (Exception exception)
                {
                    Record("OBSERVER-ERROR", $"enumerate pid={processId} role={role}: {exception.Message}");
                    continue;
                }

                if (handles.Count == 0)
                    RecordProcessExitIfNeeded(processId, role);

                foreach (var handle in handles)
                {
                    var key = (processId, handle);
                    liveWindows.Add(key);
                    string snapshot;
                    try
                    {
                        snapshot = NativeCommands.DescribeWindowForTranscript(handle, role);
                    }
                    catch (Exception exception)
                    {
                        snapshot = $"hwnd=0x{handle:X} snapshot-error={Quote(exception.Message)}";
                    }

                    if (!windowSnapshots.TryGetValue(key, out var previous))
                    {
                        windowSnapshots[key] = snapshot;
                        Record("WINDOW", $"opened pid={processId} role={role}\n{snapshot}");
                    }
                    else if (forceAll || !string.Equals(previous, snapshot, StringComparison.Ordinal))
                    {
                        windowSnapshots[key] = snapshot;
                        Record("WINDOW", $"{(forceAll ? "snapshot" : "changed")} pid={processId} role={role}\n{snapshot}");
                    }
                }
            }

            foreach (var key in windowSnapshots.Keys.Where(key => !liveWindows.Contains(key)).ToArray())
            {
                Record("WINDOW", $"closed pid={key.ProcessId} hwnd=0x{key.WindowHandle:X}");
                windowSnapshots.Remove(key);
            }
        }
    }

    private void RecordProcessExitIfNeeded(int processId, string role)
    {
        if (reportedProcessExits.Contains(processId))
            return;

        try
        {
            using var process = Process.GetProcessById(processId);
            if (!process.HasExited)
                return;
            Record("PROCESS", $"exit role={role} pid={processId} code={process.ExitCode}");
        }
        catch (ArgumentException)
        {
            Record("PROCESS", $"exit role={role} pid={processId} code=<unavailable>");
        }
        catch (InvalidOperationException)
        {
            Record("PROCESS", $"exit role={role} pid={processId} code=<unavailable>");
        }
        reportedProcessExits.Add(processId);
    }

    private void AppendTextFile(string category, string sourcePath, int maximumCharacters)
    {
        if (!File.Exists(sourcePath))
        {
            Record(category, $"absent path={Quote(sourcePath)}");
            return;
        }

        try
        {
            using var reader = new StreamReader(new FileStream(sourcePath, FileMode.Open, FileAccess.Read,
                                                               FileShare.ReadWrite | FileShare.Delete),
                                                NativeAnsiEncoding, detectEncodingFromByteOrderMarks: true);
            var buffer = new char[maximumCharacters + 1];
            var count = reader.ReadBlock(buffer, 0, buffer.Length);
            var truncated = count > maximumCharacters;
            var text = new string(buffer, 0, Math.Min(count, maximumCharacters));
            Record(category,
                   $"begin path={Quote(sourcePath)} truncated={truncated.ToString(CultureInfo.InvariantCulture).ToLowerInvariant()}\n" +
                   text + "\nend");
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            Record(category, $"unreadable path={Quote(sourcePath)} error={Quote(exception.Message)}");
        }
    }

    private static string MakeSafeFileName(string value)
    {
        var invalid = System.IO.Path.GetInvalidFileNameChars().ToHashSet();
        var safe = new string(value.Select(character => invalid.Contains(character) ? '_' : character).ToArray()).Trim();
        if (safe.Length == 0)
            safe = "unnamed-test";
        return safe.Length <= 80 ? safe : safe[..80];
    }

    private static string NormalizeLineEndings(string value) =>
        value.Replace("\r\n", "\n", StringComparison.Ordinal).Replace('\r', '\n');

    private static Encoding CreateNativeAnsiEncoding()
    {
        // Native dialog and crash writers serialize the process ANSI strings rather than UTF-8.
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        return Encoding.GetEncoding(CultureInfo.CurrentCulture.TextInfo.ANSICodePage);
    }

    private static string Quote(string value) => $"\"{value.Replace("\"", "\\\"", StringComparison.Ordinal)}\"";
}

/// <summary>Central action sink used by native test drivers without coupling them to NUnit lifecycle code.</summary>
internal static class UiTestTrace
{
    private static readonly object Gate = new();
    private static UiTestExecutionLog? current;

    internal static void Attach(UiTestExecutionLog log)
    {
        lock (Gate)
            current = log;
    }

    internal static void Detach(UiTestExecutionLog log)
    {
        lock (Gate)
        {
            if (ReferenceEquals(current, log))
                current = null;
        }
    }

    internal static void Record(string category, string message)
    {
        UiTestExecutionLog? target;
        lock (Gate)
            target = current;
        target?.Record(category, message);
    }
}
