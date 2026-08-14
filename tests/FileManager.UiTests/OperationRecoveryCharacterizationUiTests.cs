using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class OperationRecoveryCharacterizationUiTests : FileOperationUiTestBase
{
    private string journalPath = null!;
    private string temporaryPath = null!;
    private string targetPath = null!;
    private HashSet<string> reportsBeforeStartup = null!;

    protected override void BeforeFileManagerStarted()
    {
        targetPath = Workspace.TargetPath("restart-reconciled.txt");
        temporaryPath = Workspace.TargetPath("SALCPrestart-reconciled.tmp");
        File.WriteAllText(temporaryPath, "recovered-after-restart");

        var journalDirectory = GetJournalDirectory();
        Directory.CreateDirectory(journalDirectory);
        reportsBeforeStartup = Directory.EnumerateFiles(journalDirectory, "reconciliation-*.txt").ToHashSet(StringComparer.OrdinalIgnoreCase);
        journalPath = Path.Combine(journalDirectory, $"operation-characterization-{Guid.NewGuid():N}.opj");
        File.WriteAllText(journalPath,
            $"FORMAT|1{Environment.NewLine}" +
            $"OPERATION|planned|items=1{Environment.NewLine}" +
            $"ITEM|0|copy-file|source|{targetPath}|identity|{Environment.NewLine}" +
            $"TEMP|0|{temporaryPath}{Environment.NewLine}" +
            $"STATE|0|temporary-ready{Environment.NewLine}");
    }

    protected override bool IsExpectedStartupModal(nint windowHandle)
    {
        // Stable dialog/control IDs identify the recovery choice without depending on its localized title or text.
        return NativeCommands.GetWindowClassName(windowHandle) == "#32770" &&
               NativeCommands.HasDialogControl(windowHandle, 2477) &&
               NativeCommands.HasDialogControl(windowHandle, 6) &&
               NativeCommands.HasDialogControl(windowHandle, 7) &&
               NativeCommands.HasDialogControl(windowHandle, 2);
    }

    [Test]
    [Category("Recovery")]
    public void Restart_reconciliation_commits_a_fully_written_transactional_target()
    {
        // The fixture accepts the expected startup modal before its disabled owner would satisfy normal readiness.
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES: resume a ready target
        var completion = WaitForOperationPrompt(1); // IDOK: recovery summary
        ChooseOperationPrompt(completion, 1);

        WaitForFileSystem(() => File.Exists(targetPath), "Restart reconciliation did not commit the ready temporary target.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(targetPath), Is.EqualTo("recovered-after-restart"));
            Assert.That(File.Exists(temporaryPath), Is.False);
            Assert.That(File.ReadAllText(journalPath), Does.Contain("OPERATION|reconciled"));
        });
    }

    protected override void OnAfterFileManagerStopped()
    {
        base.OnAfterFileManagerStopped();
        if (!string.IsNullOrWhiteSpace(journalPath) && File.Exists(journalPath))
            File.Delete(journalPath);

        if (reportsBeforeStartup is not null)
        {
            foreach (var report in Directory.EnumerateFiles(GetJournalDirectory(), "reconciliation-*.txt"))
            {
                if (!reportsBeforeStartup.Contains(report))
                    File.Delete(report);
            }
        }
    }

    private static string GetJournalDirectory() => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Open Salamander", "operation-journals");
}
