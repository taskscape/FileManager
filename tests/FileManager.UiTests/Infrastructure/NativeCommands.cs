using System.Runtime.InteropServices;

namespace FileManager.UiTests.Infrastructure;

internal static class NativeCommands
{
    // CM_CONFIGURATION is the stable native command used by the Options > Configuration menu item.
    internal const int Configuration = 686;
    // CM_CUSTOMIZETOP opens the shared Customize Toolbar dialog without depending on translated menu text.
    internal const int CustomizeTopToolbar = 804;
    internal const int CopyFiles = 727;
    internal const int MoveFiles = 728;
    internal const int DeleteFiles = 729;
    internal const int CreateDirectory = 730;
    // These stable host commands cover the three distinct file-discovery and file-opening paths.
    internal const int FindFiles = 741;
    internal const int ViewFile = 742;
    internal const int EditFile = 743;
    internal const int RenameFile = 754;
    private const uint WmCommand = 0x0111;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const uint LvmGetItemCount = 0x1004;
    private const uint BmGetCheck = 0x00F0;
    private const uint BmClick = 0x00F5;
    private const int ConfigurationClearReadOnlyCheckBox = 304;
    internal const int OperationPathControl = 210;
    private const int VkEscape = 0x1B;

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern nint SetFocus(nint hWnd);

    private delegate bool EnumWindowsCallback(nint windowHandle, nint parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, nint parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint windowHandle, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint windowHandle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(nint windowHandle);

    [DllImport("user32.dll")]
    private static extern nint GetDlgItem(nint dialogHandle, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(nint parentHandle, EnumWindowsCallback callback, nint parameter);

    [DllImport("user32.dll")]
    private static extern int GetDlgCtrlID(nint controlHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowText(nint windowHandle, string text);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLength(nint windowHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint windowHandle, char[] buffer, int maximumCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(nint windowHandle, char[] buffer, int maximumCount);

    internal static void OpenConfiguration(nint windowHandle)
    {
        // Send directly because the custom menu loop consumes externally posted WM_COMMAND messages before native dispatch.
        SendMessage(windowHandle, WmCommand, Configuration, 0);
    }

    internal static void AcceptStartupLanguage(nint dialogHandle)
    {
        // The empty sandbox selects the bundled language with the standard IDOK command before the main window exists.
        SendMessage(dialogHandle, WmCommand, 1, 0);
    }

    internal static void CloseStandardDialog(nint dialogHandle, bool commit)
    {
        var controlId = commit ? 5 : 2;
        var buttonHandle = GetDlgItem(dialogHandle, controlId);
        // Click the actual property-sheet child because a parent WM_COMMAND without its button handle is ignored by this legacy dialog.
        if (buttonHandle == 0)
            throw new InvalidOperationException($"Configuration dialog did not expose native button {controlId}.");
        SendMessage(buttonHandle, BmClick, 0, 0);
    }

    internal static IReadOnlyList<nint> GetTopLevelWindows(int processId)
    {
        var windows = new List<nint>();
        // Native enumeration retains legacy property sheets that the UIA application view can omit from its Window control type.
        EnumWindows((windowHandle, _) =>
        {
            GetWindowThreadProcessId(windowHandle, out var ownerProcessId);
            var classBuffer = new char[64];
            GetClassName(windowHandle, classBuffer, classBuffer.Length);
            // ComboLBox is a popup helper, not a dialog; retain borderless legacy dialogs needed by native operation tests.
            if (ownerProcessId == (uint)processId && IsWindowVisible(windowHandle) &&
                !string.Equals(new string(classBuffer).TrimEnd('\0'), "ComboLBox", StringComparison.Ordinal))
                windows.Add(windowHandle);
            return true;
        }, 0);
        return windows;
    }

    internal static bool WindowExists(nint windowHandle)
    {
        // UIA can retain a stale provider after a legacy dialog closes, so use the native lifetime check for close waits.
        return IsWindow(windowHandle);
    }

    internal static bool HasOperationPathControl(nint dialogHandle)
    {
        // The legacy copy/move templates use IDE_PATH even when UIA omits the inner edit of a combo box.
        return FindDialogControl(dialogHandle, OperationPathControl) != 0;
    }

    internal static void SetOperationPath(nint dialogHandle, string path)
    {
        var pathControl = FindDialogControl(dialogHandle, OperationPathControl);
        if (pathControl == 0 || !SetWindowText(pathControl, path))
            throw new InvalidOperationException("The operation dialog did not expose its native destination/name input.");
    }

    internal static bool HasDialogButton(nint dialogHandle, int controlId)
    {
        // Standard buttons remain native controls even where the legacy provider does not publish a Button pattern.
        return FindDialogControl(dialogHandle, controlId) != 0;
    }

    internal static void ClickDialogButton(nint dialogHandle, int controlId)
    {
        var buttonHandle = FindDialogControl(dialogHandle, controlId);
        if (buttonHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native button {controlId}.");
        SendMessage(buttonHandle, BmClick, 0, 0);
    }

    internal static void DismissKnownStartupErrorDialogs(int processId)
    {
        foreach (var windowHandle in GetTopLevelWindows(processId))
        {
            var length = GetWindowTextLength(windowHandle);
            var buffer = new char[length + 1];
            GetWindowText(windowHandle, buffer, buffer.Length);
            var title = new string(buffer).TrimEnd('\0');
            if (string.Equals(title, "Error", StringComparison.Ordinal) ||
                string.Equals(title, "UnRAR", StringComparison.Ordinal))
            {
                // The clean open-source build lacks PictView's converter and UnRAR's binary, so their expected load notices cannot block UI startup.
                ClickDialogButton(windowHandle, 1);
            }
        }
    }

    private static nint FindDialogControl(nint dialogHandle, int controlId)
    {
        var controlHandle = GetDlgItem(dialogHandle, controlId);
        if (controlHandle != 0)
            return controlHandle;

        // Some custom legacy templates nest their controls below an intermediate child window.
        EnumChildWindows(dialogHandle, (childHandle, _) =>
        {
            if (GetDlgCtrlID(childHandle) == controlId)
            {
                controlHandle = childHandle;
                return false;
            }
            return true;
        }, 0);
        return controlHandle;
    }

    internal static bool ToggleConfigurationClearReadOnlyCheckBox(nint dialogHandle)
    {
        nint checkBoxHandle = 0;
        // The General page is nested below the property sheet, so locate its stable control ID recursively.
        EnumChildWindows(dialogHandle, (childHandle, _) =>
        {
            if (GetDlgCtrlID(childHandle) == ConfigurationClearReadOnlyCheckBox)
            {
                checkBoxHandle = childHandle;
                return false;
            }
            return true;
        }, 0);
        if (checkBoxHandle == 0)
            throw new InvalidOperationException("Configuration dialog did not expose its native Clear Readonly checkbox.");

        var originalState = SendMessage(checkBoxHandle, BmGetCheck, 0, 0);
        if (originalState != 0 && originalState != 1)
            throw new InvalidOperationException("Configuration persistence test requires a two-state checkbox.");
        SendMessage(checkBoxHandle, BmClick, 0, 0);
        return originalState == 1;
    }

    internal static bool IsConfigurationClearReadOnlyCheckBoxChecked(nint dialogHandle)
    {
        nint checkBoxHandle = 0;
        // Match the toggle path so persistence compares the exact same General-page setting after restart.
        EnumChildWindows(dialogHandle, (childHandle, _) =>
        {
            if (GetDlgCtrlID(childHandle) == ConfigurationClearReadOnlyCheckBox)
            {
                checkBoxHandle = childHandle;
                return false;
            }
            return true;
        }, 0);
        if (checkBoxHandle == 0)
            throw new InvalidOperationException("Configuration dialog did not expose its native Clear Readonly checkbox.");
        var state = SendMessage(checkBoxHandle, BmGetCheck, 0, 0);
        if (state != 0 && state != 1)
            throw new InvalidOperationException("Configuration persistence test requires a two-state checkbox.");
        return state == 1;
    }

    internal static void Execute(nint windowHandle, int command)
    {
        // The custom native menu loop consumes posted WM_COMMAND notifications, so tests must synchronously dispatch stable command IDs.
        SendMessage(windowHandle, WmCommand, command, 0);
    }

    internal static void QuickSearch(nint listHandle, string name)
    {
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, VkEscape, 0);
        foreach (var character in name)
            SendMessage(listHandle, WmChar, character, 1);
    }

    internal static void ToggleFocusedSelection(nint listHandle)
    {
        // Insert is the native panel gesture that selects the focused item while preserving prior selections.
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, 0x2D, 0); // VK_INSERT
    }

    internal static int GetListViewItemCount(nint listHandle)
    {
        // LVM_GETITEMCOUNT crosses process boundaries without caller-owned buffers and works for the virtual Find list.
        return checked((int)SendMessage(listHandle, LvmGetItemCount, 0, 0));
    }
}
