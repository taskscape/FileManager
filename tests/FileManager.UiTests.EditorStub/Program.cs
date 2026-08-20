using System.Windows.Forms;

namespace FileManager.UiTests.EditorStub;

/// <summary>
/// The external editor the UI lane configures inside its disposable sandbox.
///
/// The lane used to leave the seeded default (notepad.exe) in place, which made
/// the Edit case reach a shared application instance: Windows 11 Notepad opens
/// additional files as tabs in an existing window, so closing "the editor
/// window" could close a window holding unsaved documents that belong to the
/// person running the tests. This stub is copied into the test-data root and
/// launched from there, so every Edit dispatch gets a process and a window that
/// the harness exclusively owns.
/// </summary>
internal static class Program
{
    /// <summary>Prefix the UI lane matches on, ahead of the opened file name.</summary>
    internal const string WindowTitlePrefix = "SandboxEditor - ";

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            // Being launched without exactly one path means the dispatch under
            // test malformed its argument list; say so instead of idling.
            MessageBox.Show("SandboxEditor expects exactly one file path.", "SandboxEditor",
                            MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 2;
        }

        // The host passes the bare name and sets the working directory to the
        // file's folder, so resolve to a full path and then step out of that
        // folder: a live working directory would pin the workspace tree and
        // break the harness's cleanup.
        var path = Path.GetFullPath(args[0]);
        try
        {
            Directory.SetCurrentDirectory(Path.GetTempPath());
        }
        catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
        {
            // Staying put only risks a cleanup retry, not a wrong test result.
        }

        string content;
        try
        {
            content = File.ReadAllText(path);
        }
        catch (Exception ex)
        {
            // Reaching the editor with a path it cannot open is still a dispatch
            // failure, so surface it in the window the test inspects.
            content = $"<unreadable: {ex.GetType().Name}: {ex.Message}>";
        }

        using var form = new Form
        {
            // The file name in the caption is what proves the dispatch carried
            // the selected item through to the configured editor.
            Text = WindowTitlePrefix + Path.GetFileName(path),
            Width = 640,
            Height = 400,
            StartPosition = FormStartPosition.CenterScreen,
        };
        form.Controls.Add(new TextBox
        {
            Multiline = true,
            ReadOnly = true,
            Dock = DockStyle.Fill,
            ScrollBars = ScrollBars.Both,
            // The full path lets a failing run show which file actually arrived.
            Text = path + Environment.NewLine + Environment.NewLine + content,
        });

        Application.Run(form);
        return 0;
    }
}
