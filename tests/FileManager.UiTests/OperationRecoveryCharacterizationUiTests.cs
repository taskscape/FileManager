using FileManager.UiTests.Infrastructure;
using Microsoft.Win32.SafeHandles;
using NUnit.Framework;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

namespace FileManager.UiTests;

[TestFixture]
public sealed class OperationRecoveryCharacterizationUiTests : FileOperationUiTestBase
{
    private string journalPath = null!;
    private string temporaryPath = null!;
    private string targetPath = null!;
    private HashSet<string> reportsBeforeStartup = null!;

    // This fixture must attach to the disabled owner window so it can answer the intentionally seeded recovery prompt.
    protected override bool AllowDisabledMainWindowDuringStartup => true;

    protected override void BeforeFileManagerStarted()
    {
        PurgeStaleOperationJournals();
        var scenario = (string)TestContext.CurrentContext.Test.Arguments[0]!;
        targetPath = Workspace.TargetPath("restart-reconciled.txt");
        temporaryPath = Workspace.TargetPath("SALCPrestart-reconciled.tmp");
        File.WriteAllText(temporaryPath, "recovered-after-restart");
        if (scenario != "absent")
            File.WriteAllText(targetPath, "previous-target");

        var journalDirectory = GetJournalDirectory();
        Directory.CreateDirectory(journalDirectory);
        reportsBeforeStartup = Directory.EnumerateFiles(journalDirectory, "reconciliation-*.txt").ToHashSet(StringComparer.OrdinalIgnoreCase);
        journalPath = Path.Combine(journalDirectory, $"operation-characterization-{Guid.NewGuid():N}.opj");
        // The fixture records the real file IDs and digest, so startup exercises
        // production evidence validation rather than trusting a SALCP filename.
        var legacy = scenario.StartsWith("legacy", StringComparison.Ordinal);
        var ready = legacy ? string.Empty :
            $"READY2|0|attempt=1|auto=1|{targetPath}|{temporaryPath}|" +
            $"{(File.Exists(targetPath) ? Evidence(targetPath, false) : "absent")}|{Evidence(temporaryPath, false)}|" +
            $"{Evidence(Workspace.TargetDirectory, true)}|security=1|end{Environment.NewLine}";
        File.WriteAllText(journalPath,
            $"FORMAT|{(legacy ? 1 : 2)}{Environment.NewLine}" +
            $"OPERATION|planned|items=1{Environment.NewLine}" +
            $"ITEM|0|copy-file|source|{targetPath}|identity|{Environment.NewLine}" +
            $"TEMP|0|{temporaryPath}{Environment.NewLine}" +
            ready + $"STATE|0|temporary-ready|attempt=1{Environment.NewLine}");
        if (scenario == "changed-target") File.WriteAllText(targetPath, "newer-target");
        if (scenario == "changed-stage") File.WriteAllText(temporaryPath, "unrelated-stage");
    }

    [TestCase("verified", 6)]
    [TestCase("absent", 6)]
    [TestCase("discard", 7)]
    [TestCase("cancel", 2)]
    [TestCase("legacy-resume", 6)]
    [TestCase("legacy-discard", 7)]
    [TestCase("changed-target", 6)]
    [TestCase("changed-stage", 6)]
    [Category("Recovery")]
    public void Restart_reconciliation_commits_a_fully_written_transactional_target(string scenario, int action)
    {
        // Startup reaches the main window before presenting the modal recovery choice.
        ChooseOperationPrompt(WaitForOperationPrompt(6), action);
        var completion = WaitForOperationPrompt(1); // IDOK: recovery summary
        ChooseOperationPrompt(completion, 1);

        var committed = scenario is "verified" or "absent";
        var resolved = committed || scenario == "discard";
        var expectedTarget = committed ? "recovered-after-restart" : scenario == "changed-target" ? "newer-target" : "previous-target";
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(targetPath), Is.EqualTo(expectedTarget));
            Assert.That(File.Exists(temporaryPath), Is.EqualTo(!resolved));
            Assert.That(File.ReadAllText(journalPath).Contains("OPERATION|reconciled", StringComparison.Ordinal), Is.EqualTo(resolved));
            if (!resolved)
                Assert.That(File.ReadAllText(temporaryPath), Is.EqualTo(scenario == "changed-stage" ? "unrelated-stage" : "recovered-after-restart"));
        });
    }

    private static string Evidence(string path, bool directory)
    {
        using var handle = CreateFileW(path, 0x80000000, 1, IntPtr.Zero, 3, directory ? 0x02000000u : 0u, IntPtr.Zero);
        Assert.That(handle.IsInvalid, Is.False, $"Cannot open evidence fixture: {Marshal.GetLastWin32Error()}");
        Assert.That(GetFileInformationByHandle(handle, out var info), Is.True);
        static ulong Time(System.Runtime.InteropServices.ComTypes.FILETIME time) => ((ulong)(uint)time.dwHighDateTime << 32) | (uint)time.dwLowDateTime;
        var digest = directory ? "unverified" : Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(path))).ToLowerInvariant();
        return $"{info.Volume:x16},{info.IndexHigh:x16},{info.IndexLow:x16},{Time(info.Creation):x16},{Time(info.Write):x16}," +
               $"{(((ulong)info.SizeHigh << 32) | info.SizeLow):x16},{digest}";
    }

    // Match the Win32 identity layout; managed path timestamps alone cannot distinguish a replacement file.
    [StructLayout(LayoutKind.Sequential)]
    private struct FileInformation
    {
        public uint Attributes;
        public System.Runtime.InteropServices.ComTypes.FILETIME Creation, Access, Write;
        public uint Volume, SizeHigh, SizeLow, Links, IndexHigh, IndexLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint sharing, IntPtr security, uint disposition, uint flags, IntPtr template);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInformation information);

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

    // The application redirects its roaming data into the same root that owns this recovery fixture.
    private static string GetJournalDirectory() => UiTestSettings.JournalDirectory;
}
