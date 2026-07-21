using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal static class NativeCommands
{
    // CM_CONFIGURATION is the stable native command used by the Options > Configuration menu item.
    internal const int Configuration = 686;
    private const uint WmCommand = 0x0111;

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

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
}
