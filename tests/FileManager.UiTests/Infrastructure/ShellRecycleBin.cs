using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal static class ShellRecycleBin
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ShQueryRbInfo
    {
        public int cbSize;
        public long i64Size;
        public long i64NumItems;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern int SHQueryRecycleBin(string? rootPath, ref ShQueryRbInfo queryInfo);

    internal static long GetItemCount(string volumeRoot)
    {
        var info = new ShQueryRbInfo { cbSize = Marshal.SizeOf<ShQueryRbInfo>() };
        var result = SHQueryRecycleBin(volumeRoot, ref info);
        if (result != 0)
            throw new InvalidOperationException($"SHQueryRecycleBin failed for {volumeRoot} (0x{result:X8}).");
        return info.i64NumItems;
    }
}
