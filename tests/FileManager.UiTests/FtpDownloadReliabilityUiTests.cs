using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;
using System.Text;
using Microsoft.Win32;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class FtpDownloadReliabilityUiTests : FileOperationUiTestBase
{
    private static readonly byte[] Original = Encoding.ASCII.GetBytes("original complete local destination");
    private string Target => Workspace.TargetPath(LoopbackFtpDownloadServer.FileName);
    private static string Marker(string name) => Path.Combine(UiTestSettings.TestDataRoot, ".ftp-reliability." + name);

    [TestCase(false, false)]
    [TestCase(true, false)]
    [TestCase(true, true)]
    public async Task Compressed_listing_populates_the_panel_and_preserves_download_size(bool compression, bool machineReadable)
    {
        // A real queued copy proves the panel produced usable file metadata instead of numbered raw-listing rows.
        await using var server = new LoopbackFtpDownloadServer { MachineReadableList = machineReadable, RequestCompression = compression };
        server.ReleaseTransfer.TrySetResult();
        await BeginTransfer(server, move: false);
        await WaitFor(() => File.Exists(Target) && ReadShared(Target).SequenceEqual(server.Payload),
                      "Compressed listing did not yield a complete downloadable panel item.");
        Assert.That(server.Commands, Has.Some.EqualTo("LIST"));
        Assert.That(server.Commands.Any(command => command == "MODE Z"), Is.EqualTo(compression));
        Assert.That(server.Commands.Any(command => command.StartsWith("RETR ", StringComparison.Ordinal) && command.EndsWith(LoopbackFtpDownloadServer.FileName, StringComparison.Ordinal)), Is.True);
    }

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        // A complete pre-existing destination makes premature overwrite observable.
        File.WriteAllBytes(workspace.TargetPath(LoopbackFtpDownloadServer.FileName), Original);
        foreach (var name in new[] { "arm", "entered", "release", "completed" }) File.Delete(Marker(name));
    }

    [Test]
    public async Task Polish_progress_lists_preserve_localized_cell_text()
    {
        // Select and persist the real language module, rather than injecting untranslated fixture strings.
        NativeCommands.OpenConfiguration(NativeMainWindowHandle);
        var configuration = WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 1));
        Assert.That(ConfigurationDialogPages.SelectLanguagePage(configuration.Properties.NativeWindowHandle.Value), Is.True);
        NativeCommands.PostDialogButtonClick(configuration.Properties.NativeWindowHandle.Value, 387);
        var selector = WaitForWindow(window => window.Title == "Select Language");
        NativeCommands.SelectDialogListViewItemByPrefix(selector.Properties.NativeWindowHandle.Value, 1031, 'P');
        NativeCommands.ClickDialogButton(selector.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(selector);
        NativeCommands.PostDialogButtonClick(configuration.Properties.NativeWindowHandle.Value, 5);
        var notice = WaitForWindow(window => NativeCommands.GetDialogText(window.Properties.NativeWindowHandle.Value)
            .Contains("Language changes do not take effect", StringComparison.Ordinal));
        NativeCommands.ClickDialogButton(notice.Properties.NativeWindowHandle.Value, 1);
        WaitForWindowToClose(notice);
        WaitForWindowToClose(configuration);
        // The dialog closes before its configuration transaction has necessarily committed.
        await WaitFor(() =>
        {
            using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
            if (root?.GetValue("Active Generation") is not int generation) return false;
            using var saved = root.OpenSubKey($"Configuration Generations\\Generation {generation}\\Configuration");
            return saved?.GetValue("Language") as string == "polish.slg";
        }, "Polish language selection did not commit before restart.");
        RestartFileManager();
        await using var server = new LoopbackFtpDownloadServer();
        await BeginTransfer(server, move: false);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "Polish FTP transfer did not reach its payload barrier.");
        var progress = WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 761));
        var handle = progress.Properties.NativeWindowHandle.Value;
        // Keep the real operation dialog available after completion, then expose both owner-data lists.
        if (NativeCommands.GetDialogCheckBoxState(handle, 778) != 0)
            NativeCommands.ClickDialogButton(handle, 778);
        if (NativeCommands.GetDialogControlText(handle, 758).Contains(">>", StringComparison.Ordinal))
            NativeCommands.ClickDialogButton(handle, 758);
        await WaitFor(() => progress.FindFirstDescendant(cf => cf.ByAutomationId("763")) is not null,
                      "The FTP Connections list did not become visible.");
        Assert.That(NativeCommands.RequeryListViewUnicodeFormat(handle, 763), Is.True,
                    "Connections reverted to ANSI notifications after a format requery.");
        Assert.That(NativeCommands.RequeryListViewUnicodeFormat(handle, 769), Is.True,
                    "Operations reverted to ANSI notifications after a format requery.");
        var connections = progress.FindFirstDescendant(cf => cf.ByAutomationId("763"))!;
        Assert.That(connections.FindAllDescendants().Select(cell => cell.Name),
                    Has.Some.Contains("Kopiowanie"), "Connections did not expose the Polish action text.");
        server.ReleaseTransfer.TrySetResult();
        var operations = progress.FindFirstDescendant(cf => cf.ByAutomationId("769"))!;
        await WaitFor(() => operations.FindAllDescendants().Any(cell => cell.Name == "Zakończono"),
                      "Operations corrupted the Polish completed status.");
        var names = operations.FindAllDescendants().Select(cell => cell.Name).ToArray();
        Assert.That(names, Has.Some.Contains("Kopiuj"));
        Assert.That(names, Has.None.Contains("ZakoÅ"));
        // Retain the rendered dialog alongside the transcript, not just the resource-source assertion.
        using var capture = progress.Capture();
        capture.Save(Path.Combine(UiTestSettings.ExecutionTranscriptRoot, "ftp-polish-progress.png"), System.Drawing.Imaging.ImageFormat.Png);
    }

    [TestCase(false)]
    [TestCase(true)]
    public async Task Queued_download_preserves_the_destination_until_local_finalization_and_then_completes(bool move)
    {
        RestartFileManager(new Dictionary<string, string> { ["FILEMANAGER_UI_FTP_FAULT"] = "pause" });
        await using var server = new LoopbackFtpDownloadServer();
        await BeginTransfer(server, move);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "FTP payload did not reach its midpoint.");
        AssertOriginal();
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
        File.WriteAllText(Marker("arm"), "armed");
        server.ReleaseTransfer.TrySetResult();
        await WaitFor(() => File.Exists(Marker("entered")), "FTP worker did not reach durable finalization.");
        Assert.That(server.NetworkCompleted.Task.IsCompleted, Is.True);
        AssertOriginal();
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False, "Remote deletion preceded local completion.");
        File.WriteAllText(Marker("release"), "continue");
        await WaitFor(() => ReadMarker("completed") == "0", "FTP finalization did not finish after release.");
        WaitForOperationOutputToBeReleased(Target, "FTP publication did not release the local target.");
        Assert.That(File.ReadAllBytes(Target), Is.EqualTo(server.Payload));
        if (move) await WaitFor(() => server.DeleteReceived.Task.IsCompleted, "Durable FTP move did not delete the remote fixture.");
        else Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
    }

    [TestCase("flush")]
    [TestCase("metadata")]
    [TestCase("commit")]
    [TestCase("close")]
    [TestCase("admission")]
    public async Task Queued_move_retains_remote_source_after_failed_local_finalization(string phase)
    {
        RestartFileManager(new Dictionary<string, string> { ["FILEMANAGER_UI_FTP_FAULT"] = phase });
        await using var server = new LoopbackFtpDownloadServer();
        await BeginTransfer(server, move: true);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "FTP payload did not reach the failure fixture.");
        AssertOriginal();
        File.WriteAllText(Marker("arm"), "armed");
        server.ReleaseTransfer.TrySetResult();
        string? completion = null;
        await WaitFor(() => (completion = ReadMarker("completed")) is { Length: > 0 }, "FTP did not exercise its selected local failure.");
        Assert.That(completion, Is.Not.EqualTo("0"));
        // The failed result has returned to the worker; give its event queue a
        // turn before checking that it cannot advance to the remote DELE state.
        await Task.Delay(300);
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False, "An unsuccessful local result enabled DELE.");
        if (phase != "close") AssertOriginal();
        else Assert.That(ReadShared(Target), Is.EqualTo(server.Payload), "A close failure lost the already published bytes.");
    }

    [TestCase(true)]
    [TestCase(false)]
    public async Task Terminating_a_queued_overwrite_preserves_the_original_at_zero_and_partial_bytes(bool beforeAnyBytes)
    {
        await using var server = new LoopbackFtpDownloadServer(beforeAnyBytes);
        await BeginTransfer(server, move: true);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "FTP transfer did not reach the process interruption barrier.");
        AssertOriginal();
        Application.Kill();
        WaitForFileManagerExit(TimeSpan.FromSeconds(10));
        Assert.That(File.ReadAllBytes(Target), Is.EqualTo(Original));
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
    }

    [Test]
    public async Task Interrupted_queued_overwrite_preserves_the_existing_local_file()
    {
        await using var server = new LoopbackFtpDownloadServer { DisconnectAtPause = true };
        await BeginTransfer(server, move: true);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "FTP interruption did not reach its payload barrier.");
        server.ReleaseTransfer.TrySetResult();
        await WaitFor(() => server.NetworkCompleted.Task.IsCompleted, "FTP disconnect was not delivered.");
        await Task.Delay(300);
        AssertOriginal();
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
    }

    [Test]
    public async Task Cancelling_a_queued_move_preserves_the_original_and_remote_source()
    {
        await using var server = new LoopbackFtpDownloadServer();
        await BeginTransfer(server, move: true);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "FTP did not reach the cancellation barrier.");
        var progress = WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 761));
        var progressHandle = progress.Properties.NativeWindowHandle.Value;
        NativeCommands.PostDialogButtonClick(progressHandle, 2);
        var confirmation = WaitForWindow(window => NativeCommands.HasDialogButton(window.Properties.NativeWindowHandle.Value, 6));
        // Answer the FTP operation's own cancellation confirmation.
        NativeCommands.PostDialogButtonClick(confirmation.Properties.NativeWindowHandle.Value, 6);
        // Let the single-session fixture finish its data reply so it can read
        // the worker's ABOR/QUIT while the application is stopping.
        server.DisconnectAtPause = true;
        server.ReleaseTransfer.TrySetResult();
        await WaitFor(() => !NativeCommands.WindowExists(progressHandle), "Cancelled FTP operation did not close.");
        AssertOriginal();
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
    }

    [TestCase(false)]
    [TestCase(true)]
    public async Task Direct_view_requires_successful_disk_completion_before_publishing(bool rejectCloseAdmission)
    {
        if (rejectCloseAdmission)
            RestartFileManager(new Dictionary<string, string> { ["FILEMANAGER_UI_FTP_FAULT"] = "close-admission" });
        await using var server = new LoopbackFtpDownloadServer();
        await ConnectAndSelect(server);
        var temporaryRoot = Path.Combine(UiTestSettings.TestDataRoot, "temp");
        var previous = Directory.GetFiles(temporaryRoot, ".salftp-*.part.meta", SearchOption.AllDirectories).ToHashSet(StringComparer.OrdinalIgnoreCase);
        NativeCommands.Execute(NativeMainWindowHandle, NativeCommands.ViewFile);
        await WaitFor(() => server.TransferPaused.Task.IsCompleted, "Direct FTP view did not reach its payload barrier.");
        var metadata = Directory.GetFiles(temporaryRoot, ".salftp-*.part.meta", SearchOption.AllDirectories).Single(path => !previous.Contains(path));
        var target = Path.Combine(Path.GetDirectoryName(metadata)!, LoopbackFtpDownloadServer.FileName);
        // The viewer's cache name must not expose a partial download.
        Assert.That(File.Exists(target), Is.False);
        if (rejectCloseAdmission) File.WriteAllText(Marker("arm"), "armed");
        server.ReleaseTransfer.TrySetResult();
        if (rejectCloseAdmission)
        {
            string? completion = null;
            await WaitFor(() => (completion = ReadMarker("completed")) is { Length: > 0 }, "Direct FTP view did not exercise close admission failure.");
            Assert.That(completion, Is.Not.EqualTo("0"));
            await Task.Delay(300);
            Assert.That(File.Exists(target), Is.False, "Failed disk completion published the viewer cache.");
            Assert.That(File.Exists(metadata[..^5]), Is.True, "Failed disk completion lost its private staged bytes.");
        }
        else
        {
            var viewer = WaitForWindow(window => window.Properties.ClassName.ValueOrDefault == "Salamander's Viewer Window");
            Assert.That(viewer.Title, Does.Contain(LoopbackFtpDownloadServer.FileName).IgnoreCase);
            Assert.That(ReadShared(target), Is.EqualTo(server.Payload));
            Assert.That(File.Exists(metadata), Is.False);
            viewer.Close();
            WaitForWindowToClose(viewer);
        }
        Assert.That(server.DeleteReceived.Task.IsCompleted, Is.False);
    }

    private async Task ConnectAndSelect(LoopbackFtpDownloadServer server)
    {
        ActivateSourcePanel();
        var connect = OpenFtpConnectDialog();
        var handle = connect.Properties.NativeWindowHandle.Value;
        NativeCommands.SetDialogCheckBoxState(handle, 566, isChecked: true);
        var host = connect.FindFirstDescendant(cf => cf.ByAutomationId("563"))!.AsComboBox();
        host.Value = "127.0.0.1";
        NativeCommands.PostDialogButtonClick(handle, 570);
        // Stable control identity also works with localized advanced-option captions.
        var advanced = WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 585));
        var advancedHandle = advanced.Properties.NativeWindowHandle.Value;
        NativeCommands.SetDialogControlText(advancedHandle, 585, server.Port.ToString(System.Globalization.CultureInfo.InvariantCulture));
        var mode = advanced.FindFirstDescendant(cf => cf.ByAutomationId("584"))!.AsComboBox();
        NativeCommands.SelectComboBoxItem(advancedHandle, mode.Properties.NativeWindowHandle.Value, 1);
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 587, isChecked: true);
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 596, isChecked: false);
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 597, isChecked: false);
        // Select compression explicitly so persisted defaults cannot change the protocol exercised by a case.
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 598, isChecked: server.RequestCompression);
        NativeCommands.ClickDialogButton(advancedHandle, 1);
        WaitForWindowToClose(advanced);
        NativeCommands.PostDialogButtonClick(handle, 1);
        await WaitFor(() => server.ListingSent.Task.IsCompleted && !NativeCommands.WindowExists(handle), "FTP fixture did not populate the panel.");
        var welcome = NativeCommands.GetTopLevelWindows(Application.ProcessId)
            .FirstOrDefault(window => NativeCommands.HasDialogControl(window, 641));
        if (welcome != 0) NativeCommands.ClickDialogButton(welcome, 1);
        SelectSourceItem(LoopbackFtpDownloadServer.FileName);
    }

    private async Task BeginTransfer(LoopbackFtpDownloadServer server, bool move)
    {
        await ConnectAndSelect(server);
        NativeCommands.Execute(NativeMainWindowHandle, move ? NativeCommands.MoveFiles : NativeCommands.CopyFiles);
        var transfer = WaitForWindow(window => NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, 781));
        var transferHandle = transfer.Properties.NativeWindowHandle.Value;
        NativeCommands.SetDialogCheckBoxState(transferHandle, 782, isChecked: false);
        NativeCommands.SetDialogControlText(transferHandle, 781, Path.Combine(Workspace.TargetDirectory, "*.*"));
        Thread.Sleep(250);
        NativeCommands.PostDialogButtonClick(transferHandle, 1);
    }

    private async Task WaitFor(Func<bool> predicate, string message)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(20);
        while (DateTime.UtcNow < deadline)
        {
            if (predicate()) return;
            foreach (var window in NativeCommands.GetTopLevelWindows(Application.ProcessId))
            {
                // Overwrite approval is a real worker prompt for this exact test file.
                if (NativeCommands.HasDialogButton(window, 1308)) NativeCommands.PostDialogButtonClick(window, 1308);
                if (NativeCommands.HasDialogControl(window, 641)) NativeCommands.PostDialogButtonClick(window, 1);
            }
            await Task.Delay(50);
        }
        Assert.Fail(message + " Windows: " + string.Join("; ", NativeCommands.GetTopLevelWindows(Application.ProcessId)
            .Select(window => NativeCommands.GetWindowTitle(window) + ": " + NativeCommands.GetDialogText(window))));
    }

    private void AssertOriginal() => Assert.That(ReadShared(Target), Is.EqualTo(Original));
    private static string? ReadMarker(string name)
    {
        var path = Marker(name);
        if (!File.Exists(path)) return null;

        try
        {
            // The worker creates the marker before it closes its write handle, so
            // share reads and retry through WaitFor instead of racing File.ReadAllText.
            return Encoding.UTF8.GetString(ReadShared(path));
        }
        catch (IOException)
        {
            return null;
        }
    }

    private static byte[] ReadShared(string path)
    {
        using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var copy = new MemoryStream();
        file.CopyTo(copy);
        return copy.ToArray();
    }
}
