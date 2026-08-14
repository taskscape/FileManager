using System.Diagnostics;

namespace FileManager.UiTests.Infrastructure;

internal sealed class TestOwnedProcessScope : IDisposable
{
    private readonly string processName;
    private readonly HashSet<int> processIdsBefore;

    internal TestOwnedProcessScope(string processName)
    {
        this.processName = processName;
        // PID identity ensures cleanup never terminates an editor that existed before the test command.
        processIdsBefore = Process.GetProcessesByName(processName)
            .Select(process =>
            {
                using (process)
                    return process.Id;
            })
            .ToHashSet();
    }

    public void Dispose()
    {
        foreach (var process in Process.GetProcessesByName(processName))
        {
            using (process)
            {
                // A failed editor launch can remain headless, so post-command PID is the cleanup fallback when no HWND exists.
                if (processIdsBefore.Contains(process.Id) || process.HasExited)
                    continue;
                process.Kill(entireProcessTree: true);
                process.WaitForExit(5000);
            }
        }
    }
}
