using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;
using System.Diagnostics;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
[Category("LiveFtp")]
[Explicit("Contacts the external MojeRzeczy FTPS server; run through scripts/run-ftp-test.ps1 only.")]
public sealed class MojeRzeczyFtpsUiTests : FileManagerUiTestBase
{
    private const string Host = "ftp.mojerzeczy.com";
    private const string RemoteSkanPath = "/skan.txt";
    private const string ReferenceSkanPath = @"C:\Projects\FtpMojerzeczy\skan.txt";
    private const int FtpDownloadTargetPathControl = 781;
    private const int FtpAddToQueueControl = 782;
    private const string CertificateDialogTitle = "Server Identity Problem";
    private const string WelcomeMessageDialogTitle = "Welcome Message";
    private const string DebugErrorLogDirectoryVariable = "FILEMANAGER_UI_FTP_DEBUG_ERROR_LOG_DIRECTORY";
    private const string MissingCredentialsMessage = "FTP UI tests have not been performed due to missing credentials (MOJERZEC_USERNAME and/or MOJERZEC_PASSWORD).";

    [Test]
    public void Quick_connect_downloads_skan_txt_with_explicit_ftps_passive_binary_transfer_and_an_invalid_certificate()
    {
        var (username, password) = GetCredentialsOrPass();
        var expectedSize = RequireReferenceSkanSize();
        var downloadDirectory = CreateDownloadDirectory();
        // Always connect in the left panel so the post-connect remote listing is the known source for the download command.
        ActivateLeftFilePanel();
        var connectDialog = OpenFtpConnectDialog();

        ConfigureExplicitFtpsConnection(connectDialog, username, password);
        NativeCommands.PostDialogButtonClick(connectDialog.Properties.NativeWindowHandle.Value, 1);

        var capturedErrors = WaitForConnectionToComplete(connectDialog);
        Assert.That(capturedErrors, Is.Empty,
                    "The FTPS error dialog was logged and dismissed before failing the test: " + string.Join("; ", capturedErrors));

        DismissWelcomeMessage();
        DownloadRemoteSkan(downloadDirectory);
        capturedErrors = WaitForDownloadedFile(downloadDirectory, expectedSize, capturedErrors);
        Assert.That(capturedErrors, Is.Empty,
                    "The FTPS error dialog was logged and dismissed before failing the test: " + string.Join("; ", capturedErrors));

        var downloadedFile = Path.Combine(downloadDirectory, Path.GetFileName(RemoteSkanPath));
        Assert.That(new FileInfo(downloadedFile).Length, Is.EqualTo(expectedSize),
                    $"Downloaded {RemoteSkanPath} did not match the reference size in {ReferenceSkanPath}.");
    }

    private static long RequireReferenceSkanSize()
    {
        Assert.That(File.Exists(ReferenceSkanPath), Is.True,
                    $"The live FTP test requires the reference file created by C:\\Projects\\FtpMojerzeczy: {ReferenceSkanPath}.");
        var size = new FileInfo(ReferenceSkanPath).Length;
        Assert.That(size, Is.GreaterThan(0), $"The reference {ReferenceSkanPath} must not be empty.");
        return size;
    }

    private static string CreateDownloadDirectory()
    {
        var directory = Path.Combine(UiTestSettings.TestDataRoot, "live-ftp-downloads", Guid.NewGuid().ToString("N"));
        // Keep the external server artifact below the sandbox root so fixture cleanup cannot touch a user directory.
        Directory.CreateDirectory(directory);
        return directory;
    }

    private void DownloadRemoteSkan(string downloadDirectory)
    {
        var remotePanel = ActivateLeftFilePanel();
        NativeCommands.ClearActiveSelection(MainWindow.Properties.NativeWindowHandle.Value);
        NativeCommands.QuickSearch(remotePanel.Properties.NativeWindowHandle.Value, Path.GetFileName(RemoteSkanPath));
        // Insert makes the live-server item explicit, rather than relying on Copy's focused-item fallback.
        NativeCommands.ToggleFocusedSelection(remotePanel.Properties.NativeWindowHandle.Value);
        Thread.Sleep(750);

        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.CopyFiles);
        var downloadDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            NativeCommands.HasDialogControl(window.Properties.NativeWindowHandle.Value, FtpDownloadTargetPathControl));
        var downloadHandle = downloadDialog.Properties.NativeWindowHandle.Value;
        // A saved profile may queue FTP downloads instead of starting them, which would make a completed-file assertion meaningless.
        NativeCommands.SetDialogCheckBoxState(downloadHandle, FtpAddToQueueControl, isChecked: false);
        SetFtpDownloadTargetPath(downloadHandle, Path.Combine(downloadDirectory, "*.*"));
        NativeCommands.PostDialogButtonClick(downloadHandle, 1);
    }

    private void DismissWelcomeMessage()
    {
        var welcomeMessage = NativeCommands.FindDialogByTitle(Application.ProcessId, WelcomeMessageDialogTitle);
        if (welcomeMessage == 0)
            return;

        // The plug-in displays this modeless server banner after a successful login; close it so it cannot reclaim the foreground from the copy operation.
        NativeCommands.ClickDialogButton(welcomeMessage, 1);
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (NativeCommands.WindowExists(welcomeMessage) && DateTime.UtcNow < deadline)
            Thread.Sleep(100);
        Assert.That(NativeCommands.WindowExists(welcomeMessage), Is.False, "The FTP welcome message did not close before the download started.");
    }

    private IReadOnlyList<string> WaitForDownloadedFile(string downloadDirectory, long expectedSize, IReadOnlyList<string> capturedErrors)
    {
        var errors = capturedErrors.ToList();
        var downloadedFile = Path.Combine(downloadDirectory, Path.GetFileName(RemoteSkanPath));
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(120);
        while (DateTime.UtcNow < deadline)
        {
            CaptureAndDismissDebugErrorDialogs(errors);
            if (errors.Count != 0)
                return errors;

            try
            {
                // The directory entry can appear before the FTP worker closes it; FileShare.None makes size validation a real completion barrier.
                using var stream = new FileStream(downloadedFile, FileMode.Open, FileAccess.Read, FileShare.None);
                if (stream.Length == expectedSize)
                    return errors;
            }
            catch (Exception exception) when (exception is FileNotFoundException or DirectoryNotFoundException or IOException or UnauthorizedAccessException)
            {
                // The asynchronous transfer has not published a readable, completed destination yet.
            }

            Thread.Sleep(100);
        }

        var actualSize = File.Exists(downloadedFile) ? new FileInfo(downloadedFile).Length.ToString() : "missing";
        Assert.Fail($"Timed out downloading {RemoteSkanPath}. Expected {expectedSize} bytes, observed {actualSize}. " +
                    "Open FileManager windows: " + string.Join("; ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId)));
        return errors;
    }

    private void SetFtpDownloadTargetPath(nint dialogHandle, string downloadTarget)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            NativeCommands.SetDialogControlText(dialogHandle, FtpDownloadTargetPathControl, downloadTarget);
            Thread.Sleep(200);
            if (string.Equals(NativeCommands.GetDialogControlText(dialogHandle, FtpDownloadTargetPathControl), downloadTarget, StringComparison.Ordinal))
            {
                Thread.Sleep(200);
                if (string.Equals(NativeCommands.GetDialogControlText(dialogHandle, FtpDownloadTargetPathControl), downloadTarget, StringComparison.Ordinal))
                    return;
            }
        }

        Assert.Fail($"The FTP download dialog did not retain destination mask '{downloadTarget}'.");
    }

    private AutomationElement ActivateLeftFilePanel()
    {
        var panel = MainWindow.FindAllDescendants()
            .Where(element => string.Equals(element.Properties.ClassName.ValueOrDefault, "SalamanderItemsBox", StringComparison.Ordinal))
            .OrderBy(element => element.BoundingRectangle.Left)
            .FirstOrDefault();
        Assert.That(panel, Is.Not.Null, "The left FileManager panel did not expose its SalamanderItemsBox control.");
        NativeCommands.ActivateFilePanel(panel!.Properties.NativeWindowHandle.Value);
        return panel;
    }

    private static (string Username, string Password) GetCredentialsOrPass()
    {
        var username = Environment.GetEnvironmentVariable("MOJERZEC_USERNAME");
        var password = Environment.GetEnvironmentVariable("MOJERZEC_PASSWORD");
        if (string.IsNullOrWhiteSpace(username) || string.IsNullOrWhiteSpace(password))
        {
            // Missing external secrets are a passed non-execution so ordinary developer machines report the omitted FTPS coverage honestly.
            TestContext.Progress.WriteLine(MissingCredentialsMessage);
            Assert.Pass(MissingCredentialsMessage);
        }

        return (username!, password!);
    }

    private void ConfigureExplicitFtpsConnection(Window connectDialog, string username, string password)
    {
        // Use the plug-in's resource IDs so this external-only test exercises its actual connection dialog without translated captions.
        var anonymousLogin = RequireCheckBox(connectDialog, "566", "anonymous-login option");
        if (anonymousLogin.IsChecked == true)
            anonymousLogin.Toggle();
        Assert.That(anonymousLogin.IsChecked, Is.False, "The FTP client must use the supplied credentials instead of anonymous login.");

        // The native dialog commits these values on focus loss, so move through all three fields before opening Advanced Options.
        var hostAddress = RequireComboBox(connectDialog, "563", "host-address field");
        hostAddress.Focus();
        hostAddress.Value = Host;
        var userName = RequireTextBox(connectDialog, "567", "user-name field");
        userName.Focus();
        userName.Text = username;
        var userPassword = RequireTextBox(connectDialog, "568", "password field");
        userPassword.Focus();
        userPassword.Text = password;

        NativeCommands.PostDialogButtonClick(connectDialog.Properties.NativeWindowHandle.Value, 570);
        var advancedDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.Properties.NativeWindowHandle.Value != connectDialog.Properties.NativeWindowHandle.Value &&
            string.Equals(window.Title, "Advanced Options", StringComparison.Ordinal));
        var advancedHandle = advancedDialog.Properties.NativeWindowHandle.Value;

        RequireTextBox(advancedDialog, "585", "FTP port field").Text = "21";
        var transferMode = RequireComboBox(advancedDialog, "584", "transfer-mode selector");
        // Index 1 is Binary; index 0 deliberately inherits the global default and is unsuitable for this server contract.
        NativeCommands.SelectComboBoxItem(advancedHandle, transferMode.Properties.NativeWindowHandle.Value, 1);
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 587, isChecked: true);
        // Explicit FTPS upgrades the control channel with AUTH TLS and encrypts the passive data channel as well.
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 596, isChecked: true);
        NativeCommands.SetDialogCheckBoxState(advancedHandle, 597, isChecked: true);

        NativeCommands.ClickDialogButton(advancedHandle, 1);
        WaitForWindowToClose(advancedDialog);
    }

    private IReadOnlyList<string> WaitForConnectionToComplete(Window connectDialog)
    {
        var connectHandle = connectDialog.Properties.NativeWindowHandle.Value;
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(45);
        DateTime? connectionClosedAt = null;
        var acceptedCertificate = false;
        var capturedErrors = new List<string>();

        while (DateTime.UtcNow < deadline)
        {
            var certificateDialog = NativeCommands.FindDialogByTitle(Application.ProcessId, CertificateDialogTitle);
            if (certificateDialog != 0)
            {
                // The server is allowed to have a hostname-invalid certificate, but never persist that exception beyond this disposable test profile.
                NativeCommands.SetDialogCheckBoxState(certificateDialog, 943, isChecked: false);
                NativeCommands.ClickDialogButton(certificateDialog, 1);
                acceptedCertificate = true;
                connectionClosedAt = null;
            }

            var openTitles = NativeCommands.GetTopLevelWindowTitles(Application.ProcessId);
            CaptureAndDismissDebugErrorDialogs(capturedErrors);
            if (openTitles.Any(title => string.Equals(title, "Error Connecting to FTP Server", StringComparison.Ordinal)))
            {
                Assert.Fail("The MojeRzeczy FTPS connection failed. Open FileManager windows: " + string.Join("; ", openTitles));
            }

            if (!NativeCommands.WindowExists(connectHandle) && certificateDialog == 0 && MainWindow.IsEnabled)
            {
                connectionClosedAt ??= DateTime.UtcNow;
                // A failed quick connect reopens this dialog; require a short stable interval after the modal connection flow ends.
                if (DateTime.UtcNow - connectionClosedAt >= TimeSpan.FromSeconds(2))
                    return capturedErrors;
            }
            else
            {
                connectionClosedAt = null;
            }

            Thread.Sleep(100);
        }

        var certificateNote = acceptedCertificate ? " The certificate exception was accepted for this session." : string.Empty;
        Assert.Fail("Timed out waiting for the MojeRzeczy FTPS connection to complete." + certificateNote +
                    " Open FileManager windows: " + string.Join("; ", NativeCommands.GetTopLevelWindowTitles(Application.ProcessId)));
        return capturedErrors;
    }

    private void CaptureAndDismissDebugErrorDialogs(ICollection<string> capturedErrors)
    {
        if (!IsDebugApplication())
            return;

        foreach (var dialogHandle in NativeCommands.GetTopLevelWindows(Application.ProcessId))
        {
            var title = NativeCommands.GetWindowTitle(dialogHandle);
            if (!title.Contains("error", StringComparison.OrdinalIgnoreCase) &&
                !title.Contains("assert", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(title, "Microsoft Visual C++ Runtime Library", StringComparison.Ordinal))
                continue;

            var logPath = WriteDebugErrorDialogLog(title, NativeCommands.GetDialogText(dialogHandle));
            // Dismiss first so a modal native message cannot keep the external-server lane blocked after the failure is recorded.
            if (NativeCommands.HasDialogButton(dialogHandle, 1))
                NativeCommands.ClickDialogButton(dialogHandle, 1);
            else if (NativeCommands.HasDialogButton(dialogHandle, 2))
                NativeCommands.ClickDialogButton(dialogHandle, 2);
            capturedErrors.Add(logPath);
        }
    }

    private static bool IsDebugApplication()
    {
        var executablePath = UiTestSettings.ExecutablePath;
        // The dedicated runner's Debug_x64 staging path remains reliable when FileVersionInfo cannot read a transient build artifact.
        if (executablePath.Contains("Debug_", StringComparison.OrdinalIgnoreCase))
            return true;

        try
        {
            // Retain the file metadata check for manually supplied debug executables outside the dedicated runner's staging layout.
            return FileVersionInfo.GetVersionInfo(executablePath).IsDebug;
        }
        catch
        {
            return false;
        }
    }

    private static string WriteDebugErrorDialogLog(string title, string text)
    {
        var directory = Environment.GetEnvironmentVariable(DebugErrorLogDirectoryVariable);
        if (string.IsNullOrWhiteSpace(directory))
            directory = Path.Combine(AppContext.BaseDirectory, "ftp-debug-error-dialogs");
        Directory.CreateDirectory(directory);

        var fileName = $"{DateTime.UtcNow:yyyyMMdd-HHmmssfff}-ftp-error.txt";
        var path = Path.Combine(directory, fileName);
        // Preserve the complete native dialog text outside the disposable profile for the failure that follows.
        File.WriteAllText(path, $"UTC: {DateTime.UtcNow:O}{Environment.NewLine}Title: {title}{Environment.NewLine}{Environment.NewLine}{text}");
        return path;
    }

    private static CheckBox RequireCheckBox(Window dialog, string automationId, string description)
    {
        var checkBox = dialog.FindFirstDescendant(cf => cf.ByAutomationId(automationId))?.AsCheckBox();
        Assert.That(checkBox, Is.Not.Null, $"FTP dialog did not expose its {description}.");
        return checkBox!;
    }

    private static ComboBox RequireComboBox(Window dialog, string automationId, string description)
    {
        var comboBox = dialog.FindFirstDescendant(cf => cf.ByAutomationId(automationId))?.AsComboBox();
        Assert.That(comboBox, Is.Not.Null, $"FTP dialog did not expose its {description}.");
        return comboBox!;
    }

    private static TextBox RequireTextBox(Window dialog, string automationId, string description)
    {
        var textBox = dialog.FindFirstDescendant(cf => cf.ByAutomationId(automationId))?.AsTextBox();
        Assert.That(textBox, Is.Not.Null, $"FTP dialog did not expose its {description}.");
        return textBox!;
    }
}
