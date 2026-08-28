using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
internal sealed class UiTestExecutionLogTests
{
    [Test]
    public void Execution_log_retains_ordered_human_readable_records()
    {
        var outputRoot = Path.Combine(TestContext.CurrentContext.WorkDirectory,
                                      "execution-log-contract-" + Guid.NewGuid().ToString("N"));
        try
        {
            string transcriptPath;
            var testDataRoot = Path.Combine(outputRoot, "filemanager-testdata");
            var dialogTranscript = Path.Combine(testDataRoot, "ui-test-dialogs.log");
            var crashDirectory = Path.Combine(testDataRoot, "appdata", "Open Salamander");
            Directory.CreateDirectory(crashDirectory);
            // Exercise the real writer without launching native UI so its format remains a fast source-independent contract.
            using (var log = UiTestExecutionLog.Start(outputRoot, "format/test", "Fixture.format/test", "contract"))
            {
                transcriptPath = log.Path;
                log.Record("ACTION", "first");
                log.Record("WINDOW", "second\ncontrol text");
                log.WatchProductDialogTranscript(dialogTranscript);
                File.WriteAllText(dialogTranscript, "12:00:00.000|SHOW|caption=Example|text=Question\r\n");
                // Representative text and binary artifacts verify useful crash evidence is retained before sandbox cleanup.
                File.WriteAllText(Path.Combine(crashDirectory, "sample.TXT"), "example crash report");
                File.WriteAllBytes(Path.Combine(crashDirectory, "sample.DMP"), [1, 2, 3]);
                log.CaptureProductDiagnostics(testDataRoot);
            }

            var transcript = File.ReadAllText(transcriptPath);
            Assert.Multiple(() =>
            {
                Assert.That(transcript, Does.Contain("Open Salamander UI test execution transcript"));
                Assert.That(transcript, Does.Contain("000001").And.Contain("ACTION | first"));
                Assert.That(transcript, Does.Contain("000002").And.Contain("WINDOW | second"));
                Assert.That(transcript.IndexOf("ACTION | first", StringComparison.Ordinal),
                            Is.LessThan(transcript.IndexOf("WINDOW | second", StringComparison.Ordinal)));
                Assert.That(transcript, Does.Contain("PRODUCT-DIALOGS | 12:00:00.000|SHOW|caption=Example|text=Question"));
                Assert.That(transcript, Does.Contain("CRASH-REPORT-TEXT").And.Contain("example crash report"));
                Assert.That(transcript, Does.Contain("transcript.complete"));
                Assert.That(File.Exists(Path.Combine(Path.ChangeExtension(transcriptPath, null) + ".artifacts", "sample.DMP")), Is.True);
            });
        }
        finally
        {
            // This GUID directory is owned exclusively by the contract test and must not accumulate across local runs.
            if (Directory.Exists(outputRoot))
                Directory.Delete(outputRoot, recursive: true);
        }
    }
}
