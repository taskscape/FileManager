using System.Diagnostics;
using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal sealed record ProcessResourceSnapshot(int HandleCount, uint GdiObjectCount, uint UserObjectCount, long PrivateBytes)
{
    private const uint GrGdiObjects = 0;
    private const uint GrUserObjects = 1;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetGuiResources(nint processHandle, uint uiFlags);

    internal static ProcessResourceSnapshot Capture(Process process)
    {
        // Refresh before sampling because Process caches handle and private-byte counters between reads.
        process.Refresh();
        return new ProcessResourceSnapshot(
            process.HandleCount,
            GetGuiResources(process.Handle, GrGdiObjects),
            GetGuiResources(process.Handle, GrUserObjects),
            process.PrivateMemorySize64);
    }
}
