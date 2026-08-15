using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal readonly record struct FileIdentity(uint VolumeSerialNumber, uint FileIndexHigh, uint FileIndexLow)
{
    internal static FileIdentity Capture(string path)
    {
        // Opening the entry supplies the NTFS identity rather than trusting a normalized display path.
        using SafeFileHandle handle = File.OpenHandle(path, FileMode.Open, FileAccess.Read,
                                                      FileShare.ReadWrite | FileShare.Delete, FileOptions.None);
        if (!GetFileInformationByHandle(handle, out var information))
            throw new Win32Exception(Marshal.GetLastWin32Error(), $"Could not identify '{path}'.");

        return new FileIdentity(information.VolumeSerialNumber, information.FileIndexHigh, information.FileIndexLow);
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetFileInformationByHandle(SafeFileHandle file, out ByHandleFileInformation information);

    [StructLayout(LayoutKind.Sequential)]
    private struct ByHandleFileInformation
    {
        internal uint FileAttributes;
        internal System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
        internal System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
        internal uint VolumeSerialNumber;
        internal uint FileSizeHigh;
        internal uint FileSizeLow;
        internal uint NumberOfLinks;
        internal uint FileIndexHigh;
        internal uint FileIndexLow;
    }
}
