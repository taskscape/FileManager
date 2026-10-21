using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[Category("UI")]
public sealed class InfostackerUiTests : FileOperationUiTestBase
{
    // Stable native controls test the production dialog, worker, HTTP request and clipboard together.
    private const int FileControl = 1000, StatusControl = 1001, UrlControl = 1003, CopyControl = 1004;
    private LoopbackInfostackerServer server = null!;
    private string? previousUrl;
    private const string FileName = "publikacja-zażółć-100%.bin";
    private static readonly byte[] Payload = Enumerable.Range(0, 256).Select(value => (byte)value).ToArray();

    protected override void SeedWorkspaceBeforeFileManagerStart(FileOperationWorkspace workspace)
    {
        File.WriteAllBytes(workspace.SourcePath(FileName), Payload);
        File.WriteAllText(workspace.SourcePath("note.md"), "# A note\n\n![[image.png|200]]\n![[image.png]]", new UTF8Encoding(false));
        File.WriteAllBytes(workspace.SourcePath("image.png"), Payload);
        File.WriteAllText(workspace.SourcePath("missing.md"), "![[absent.pdf]]");
        File.WriteAllText(workspace.SourcePath("escape.md"), "![[../outside.pdf]]");
        File.WriteAllBytes(Path.Combine(workspace.RootDirectory, "outside.pdf"), Payload);
        File.WriteAllText(workspace.SourcePath("simple.md"), "# Plain UTF-8\n\nCześć!");
    }

    protected override void BeforeFileManagerStarted()
    {
        base.BeforeFileManagerStarted();
        previousUrl = Environment.GetEnvironmentVariable("FILEMANAGER_UI_INFOSTACKER_URL");
        server = new LoopbackInfostackerServer();
        Environment.SetEnvironmentVariable("FILEMANAGER_UI_INFOSTACKER_URL", server.Url);
    }

    protected override void OnAfterFileManagerStopped()
    {
        // Restore only our fixture override, including after failed assertions or startup.
        Environment.SetEnvironmentVariable("FILEMANAGER_UI_INFOSTACKER_URL", previousUrl);
        try { server?.Dispose(); }
        finally { base.OnAfterFileManagerStopped(); }
    }

    [Test]
    public void Selected_binary_is_uploaded_intact_and_public_URL_is_copied()
    {
        var dialog = OpenPublish(FileName);
        Assert.That(NativeCommands.GetDialogControlText(dialog, FileControl), Is.EqualTo(Workspace.SourcePath(FileName)));
        Assert.That(server.Requests, Is.Empty, "Opening the review window must not publish.");
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Publishing did not succeed.");
        var upload = server.Requests.Single();
        Assert.That(upload.Headers, Does.StartWith("POST /sharing/uploadmarkdownwithfiles HTTP/1.1"));
        Assert.That(upload.Parts, Has.Count.EqualTo(2));
        Assert.That(upload.Parts[0].Headers, Does.Contain("name=\"markdown\""));
        Assert.That(Encoding.UTF8.GetString(upload.Parts[0].Bytes), Does.Contain($"![[{FileName.Replace("%", "%25")}]]"));
        Assert.That(upload.Parts[1].Headers, Does.Contain($"name=\"files\"; filename=\"{FileName}\""));
        Assert.That(upload.Parts[1].Bytes, Is.EqualTo(Payload));
        AssertCopied(dialog, server.Url + "sharing/file-test-123");
        CaptureDialog(dialog, "infostacker-published.png");
        Assert.That(File.ReadAllBytes(Workspace.SourcePath(FileName)), Is.EqualTo(Payload));
    }

    [Test]
    public void Markdown_includes_title_and_deduplicated_embedded_attachment()
    {
        var dialog = OpenPublish("note.md");
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Markdown publication did not succeed.");
        var parts = server.Requests.Single().Parts;
        Assert.That(parts, Has.Count.EqualTo(2));
        Assert.That(Encoding.UTF8.GetString(parts[0].Bytes), Is.EqualTo("note\n\n# A note\n\n![[image.png|200]]\n![[image.png]]"));
        Assert.That(parts[1].Bytes, Is.EqualTo(Payload));
    }

    [Test]
    public void Plain_markdown_requires_no_file_part_and_escapes_returned_ID()
    {
        server.Response = "{\"other\":true,\"id\":\"a/b ?#é\"}";
        var dialog = OpenPublish("simple.md");
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Plain Markdown publication did not succeed.");
        Assert.That(server.Requests.Single().Parts, Has.Count.EqualTo(1));
        AssertCopied(dialog, server.Url + "sharing/a%2Fb%20%3F%23%C3%A9");
    }

    [TestCase(413, "{}", "100 MB")]
    [TestCase(500, "{}", "HTTP 500")]
    [TestCase(200, "not JSON", "invalid publishing response")]
    [TestCase(200, "{}", "invalid publishing response")]
    [TestCase(200, "{\"id\":42}", "invalid publishing response")]
    [TestCase(200, "{\"id\":\" \"}", "invalid publishing response")]
    [TestCase(200, "{\"id\":\"one\",\"id\":\"two\"}", "invalid publishing response")]
    [TestCase(302, "{}", "HTTP 302")]
    public void Rejected_or_invalid_response_preserves_clipboard_and_allows_retry(int status, string response, string expected)
    {
        server.Status = status; server.Response = response;
        var dialog = OpenPublish(FileName);
        SetClipboard("clipboard-before-error");
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, 1), "Failed upload did not return to a retryable state.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain(expected));
        Assert.That(NativeCommands.GetDialogControlText(dialog, UrlControl), Is.Empty);
        Assert.That(ReadClipboard(), Is.EqualTo("clipboard-before-error"));
        // A manual retry is a new request and can recover from a transient service error.
        server.Status = 200; server.Response = "{\"id\":\"retried\"}";
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Retry did not succeed.");
        Assert.That(server.Requests, Has.Count.EqualTo(2));
        AssertCopied(dialog, server.Url + "sharing/retried");
    }

    [TestCase("missing.md")]
    [TestCase("escape.md")]
    public void Unavailable_or_outside_embed_is_rejected_before_network_request(string file)
    {
        var dialog = OpenPublish(file);
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, 1), "Invalid embed did not finish validation.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.StartWith("Publishing failed:"));
        Assert.That(server.Requests, Is.Empty);
    }

    [Test]
    public void Oversized_file_is_rejected_before_upload()
    {
        using (var file = new FileStream(Workspace.SourcePath(FileName), FileMode.Open, FileAccess.Write)) file.SetLength(100L * 1024 * 1024 + 1);
        var dialog = OpenPublish(FileName);
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, 1), "Size validation did not finish.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("100 MB"));
        Assert.That(server.Requests, Is.Empty);
    }

    [Test]
    public void File_open_for_writing_is_rejected_before_upload()
    {
        var dialog = OpenPublish(FileName);
        using var writer = new FileStream(Workspace.SourcePath(FileName), FileMode.Open, FileAccess.Write, FileShare.ReadWrite);
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, 1), "Locked file did not finish validation.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("cannot be read"));
        Assert.That(server.Requests, Is.Empty);
    }

    [Test]
    public void Waiting_for_service_keeps_progress_visible_and_can_be_cancelled()
    {
        server.HoldResponse = true;
        var dialog = OpenPublish(FileName);
        SetClipboard("clipboard-before-cancel");
        StartPublish(dialog);
        WaitFor(() => server.Received.Task.IsCompleted, "The loopback service received no upload.");
        WaitFor(() => NativeCommands.GetDialogControlText(dialog, StatusControl).Contains("Waiting for Infostacker"), "Waiting state was not displayed.");
        Assert.That(NativeCommands.IsDialogControlEnabled(dialog, 1), Is.False);
        Assert.That(NativeCommands.HasDialogControl(dialog, 1002), Is.True);
        NativeCommands.PostDialogButtonClick(dialog, 2);
        WaitFor(() => NativeCommands.GetDialogControlText(dialog, StatusControl).StartsWith("Cancelling"), "Cancellation was not acknowledged.");
        server.Release.TrySetResult();
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, 1), "Cancellation did not finish.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("cancelled"));
        Assert.That(ReadClipboard(), Is.EqualTo("clipboard-before-cancel"));
    }

    [Test]
    public void Clipboard_contention_retains_URL_and_Copy_link_recovers_without_republishing()
    {
        var dialog = OpenPublish(FileName);
        WaitFor(() => OpenClipboard(0), "Cannot lock the clipboard for contention coverage.");
        try
        {
            StartPublish(dialog);
            WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Publishing did not return a URL.");
            Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("clipboard is unavailable"));
        }
        finally { CloseClipboard(); }
        NativeCommands.PostDialogButtonClick(dialog, CopyControl);
        WaitFor(() => NativeCommands.GetDialogControlText(dialog, StatusControl).Contains("copied to the clipboard"), "Copy link did not recover.");
        AssertCopied(dialog, server.Url + "sharing/file-test-123");
        Assert.That(server.Requests, Has.Count.EqualTo(1));
    }

    [Test]
    public void Closing_review_without_publishing_sends_no_request()
    {
        var dialog = OpenPublish(FileName);
        NativeCommands.PostDialogButtonClick(dialog, 2);
        WaitFor(() => !NativeCommands.GetTopLevelWindows(Application.ProcessId).Contains(dialog), "Review did not close.");
        Assert.That(server.Requests, Is.Empty);
    }

    [Test]
    public void Focused_file_is_used_when_no_items_are_marked()
    {
        // Settle the caret through the existing selection sequence before clearing the
        // marks; a single quick-search can be overtaken by the startup panel refresh.
        SelectSourceItem(FileName);
        NativeCommands.ClearActiveSelection(NativeMainWindowHandle);
        var dialog = InvokePublish();
        Assert.That(NativeCommands.GetDialogControlText(dialog, FileControl), Is.EqualTo(Workspace.SourcePath(FileName)));
        Assert.That(server.Requests, Is.Empty);
    }

    [Test]
    public void Polish_language_is_rendered_after_configuration_restart()
    {
        // Persist the application language through its real dialog so the plugin must load polish.slg.
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
        WaitFor(() =>
        {
            using var root = Registry.CurrentUser.OpenSubKey(UiTestSettings.ConfigurationRegistryRoot);
            if (root?.GetValue("Active Generation") is not int generation) return false;
            using var saved = root.OpenSubKey($"Configuration Generations\\Generation {generation}\\Configuration");
            return saved?.GetValue("Language") as string == "polish.slg";
        }, "Polish language selection did not commit.");
        RestartFileManager();
        var dialog = OpenPublish(FileName);
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("Publikacja tworzy publiczny link"));
        Assert.That(NativeCommands.GetDialogControlText(dialog, 1), Is.EqualTo("Publikuj"));
        StartPublish(dialog);
        WaitFor(() => NativeCommands.IsDialogControlEnabled(dialog, CopyControl), "Polish publication did not succeed.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, StatusControl), Does.Contain("został skopiowany do schowka"));
        Assert.That(ReadClipboard(), Is.EqualTo(server.Url + "sharing/file-test-123"));
        CaptureDialog(dialog, "infostacker-polish-published.png");
    }

    private void CaptureDialog(nint dialog, string name)
    {
        // Print the actual window in physical pixels; desktop crops and DPI-virtualized
        // bounds can capture another application or clip a high-DPI dialog.
        var previousDpi = SetThreadDpiAwarenessContext(-4);
        try
        {
            Assert.That(GetWindowRect(dialog, out var bounds), Is.True);
            using var capture = new System.Drawing.Bitmap(bounds.Right - bounds.Left, bounds.Bottom - bounds.Top);
            using var graphics = System.Drawing.Graphics.FromImage(capture);
            var dc = graphics.GetHdc();
            try { Assert.That(PrintWindow(dialog, dc, 2), Is.True, "Cannot capture the publishing window."); }
            finally { graphics.ReleaseHdc(dc); }
            capture.Save(Path.Combine(UiTestSettings.ExecutionTranscriptRoot, name), System.Drawing.Imaging.ImageFormat.Png);
        }
        finally { if (previousDpi != 0) SetThreadDpiAwarenessContext(previousDpi); }
    }

    private nint OpenPublish(string name)
    {
        SelectSourceItem(name);
        return InvokePublish();
    }

    private nint InvokePublish()
    {
        NativeCommands.Execute(NativeMainWindowHandle, WaitForPluginCommand("infostacker.spl", 1, "Publish to Infostacker"));
        nint dialog = 0;
        WaitFor(() => (dialog = NativeCommands.GetTopLevelWindows(Application.ProcessId)
            .FirstOrDefault(window => NativeCommands.HasDialogControl(window, FileControl))) != 0, "Publishing dialog did not open.");
        return dialog;
    }

    // The worker starts asynchronously, so a synchronous click can safely establish the busy
    // state before validation-error cases begin waiting for Publish to become enabled again.
    private static void StartPublish(nint dialog) => NativeCommands.ClickDialogButton(dialog, 1);

    private void AssertCopied(nint dialog, string expected)
    {
        WaitFor(() => NativeCommands.GetDialogControlText(dialog, StatusControl).Contains("copied to the clipboard"), "Success did not report a copied URL.");
        Assert.That(NativeCommands.GetDialogControlText(dialog, UrlControl), Is.EqualTo(expected));
        Assert.That(ReadClipboard(), Is.EqualTo(expected));
        // A completed worker must leave the native progress control at its final value.
        Assert.That((int)SendMessageW(GetDlgItem(dialog, 1002), 0x0408, 0, 0), Is.EqualTo(100));
    }

    private static void WaitFor(Func<bool> condition, string failure)
    {
        var deadline = DateTime.UtcNow.AddSeconds(30);
        while (DateTime.UtcNow < deadline)
        {
            if (condition()) return;
            Thread.Sleep(100);
        }
        Assert.Fail(failure);
    }

    private static string ReadClipboard()
    {
        WaitFor(() => OpenClipboard(0), "Cannot read the clipboard.");
        try
        {
            var memory = GetClipboardData(13);
            if (memory == 0) return "";
            var text = GlobalLock(memory);
            try { return Marshal.PtrToStringUni(text) ?? ""; }
            finally { if (text != 0) GlobalUnlock(memory); }
        }
        finally { CloseClipboard(); }
    }

    private void SetClipboard(string text)
    {
        WaitFor(() => OpenClipboard(NativeMainWindowHandle), "Cannot seed the clipboard.");
        try
        {
            var memory = GlobalAlloc(2, (nuint)((text.Length + 1) * 2));
            Assert.That(memory, Is.Not.EqualTo(0));
            var pointer = GlobalLock(memory);
            Marshal.Copy((text + '\0').ToCharArray(), 0, pointer, text.Length + 1);
            GlobalUnlock(memory);
            Assert.That(EmptyClipboard(), Is.True);
            Assert.That(SetClipboardData(13, memory), Is.Not.EqualTo(0));
        }
        finally { CloseClipboard(); }
    }

    [DllImport("user32.dll")] private static extern bool OpenClipboard(nint owner);
    [DllImport("user32.dll")] private static extern bool CloseClipboard();
    [DllImport("user32.dll")] private static extern bool EmptyClipboard();
    [DllImport("user32.dll")] private static extern nint GetClipboardData(uint format);
    [DllImport("user32.dll")] private static extern nint SetClipboardData(uint format, nint memory);
    [DllImport("kernel32.dll")] private static extern nint GlobalAlloc(uint flags, nuint bytes);
    [DllImport("kernel32.dll")] private static extern nint GlobalLock(nint memory);
    [DllImport("kernel32.dll")] private static extern bool GlobalUnlock(nint memory);
    [StructLayout(LayoutKind.Sequential)] private struct WindowRect { internal int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] private static extern bool GetWindowRect(nint window, out WindowRect bounds);
    [DllImport("user32.dll")] private static extern bool PrintWindow(nint window, nint dc, uint flags);
    [DllImport("user32.dll")] private static extern nint SetThreadDpiAwarenessContext(nint context);
    [DllImport("user32.dll")] private static extern nint GetDlgItem(nint dialog, int controlId);
    [DllImport("user32.dll")] private static extern nint SendMessageW(nint window, uint message, nint wParam, nint lParam);
}
