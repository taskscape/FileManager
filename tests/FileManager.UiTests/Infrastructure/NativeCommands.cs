using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal static class NativeCommands
{
    // CM_CONFIGURATION is the stable native command used by the Options > Configuration menu item.
    internal const int Configuration = 686;
    internal const int CopyFiles = 727;
    internal const int MoveFiles = 728;
    internal const int DeleteFiles = 729;
    internal const int CreateDirectory = 730;
    internal const int RenameFile = 754;
    private const uint WmCommand = 0x0111;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const int VkEscape = 0x1B;

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern nint SetFocus(nint hWnd);

    internal static void OpenConfiguration(nint windowHandle)
    {
        // Native dispatch avoids hard-coded English menu labels while FlaUI validates the resulting dialog.
        if (!PostMessage(windowHandle, WmCommand, Configuration, 0))
            throw new InvalidOperationException("Unable to post the Configuration command to FileManager.");
    }

    internal static void Execute(nint windowHandle, int command)
    {
        // UIA drives all assertions; this Win32 bridge only reaches commands whose host-assigned IDs have no stable UIA locator.
        if (!PostMessage(windowHandle, WmCommand, command, 0))
            throw new InvalidOperationException($"Unable to post command {command} to FileManager.");
    }

    internal static void QuickSearch(nint listHandle, string name)
    {
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, VkEscape, 0);
        foreach (var character in name)
            SendMessage(listHandle, WmChar, character, 1);
    }
}
