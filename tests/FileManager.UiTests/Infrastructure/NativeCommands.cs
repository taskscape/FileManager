using System.Runtime.InteropServices;
using System.Text;

namespace FileManager.UiTests.Infrastructure;

internal static class NativeCommands
{
    private delegate bool EnumWindowsCallback(nint windowHandle, nint parameter);

    // CM_CONFIGURATION is the stable native command used by the Options > Configuration menu item.
    internal const int Configuration = 686;
    private const int LeftPanelRefresh = 724;
    // CM_CUSTOMIZETOP opens the shared Customize Toolbar dialog without depending on translated menu text.
    internal const int CustomizeTopToolbar = 804;
    internal const int CopyFiles = 727;
    internal const int MoveFiles = 728;
    internal const int DeleteFiles = 729;
    internal const int CreateDirectory = 730;
    internal const int Open = 732;
    internal const int Edit = 743;
    internal const int RenameFile = 754;
    internal const int HelpSearch = 2212;
    private const uint WmNull = 0x0000;
    private const uint WmClose = 0x0010;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const uint WmKeyUp = 0x0101;
    private const uint WmCommand = 0x0111;
    private const uint BmGetCheck = 0x00F0;
    private const uint BmClick = 0x00F5;
    private const int BstUnchecked = 0;
    private const int BstChecked = 1;
    private const uint CbGetCurSel = 0x0147;
    private const uint CbSetCurSel = 0x014E;
    private const int CbnSelChange = 1;
    private const uint WmApp = 0x8000;
    private const uint WmUiTestReady = WmApp + 417;
    private const uint WmUiTestCommand = WmApp + 418;
    private const uint WmUiTestConfigGeneration = WmApp + 419;
    private const uint WmUiTestConfigFault = WmApp + 420;
    private const uint WmUiTestFtpOrganizeCommand = WmApp + 421;
    private const uint TvmGetNextItem = 0x110A;
    private const uint TvmSelectItem = 0x110B;
    private const int TvgnRoot = 0;
    private const int TvgnCaret = 9;
    private const uint SmtoAbortIfHung = 0x0002;
    private const int VkEscape = 0x1B;
    private const int VkReturn = 0x0D;

    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SendMessageTimeout(nint hWnd, uint msg, nint wParam, nint lParam,
                                                  uint flags, uint timeout, out nint result);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll")]
    private static extern nint SetFocus(nint hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, nint parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(nint parentHandle, EnumWindowsCallback callback, nint parameter);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint windowHandle, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint windowHandle);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindow(nint windowHandle);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(nint windowHandle, StringBuilder className, int maximumCount);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(nint windowHandle, StringBuilder text, int maximumCount);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowEnabled(nint windowHandle);

    [DllImport("user32.dll")]
    private static extern nint GetDlgItem(nint dialogHandle, int controlId);

    [DllImport("user32.dll")]
    private static extern int GetDlgCtrlID(nint controlHandle);

    internal static void Execute(nint windowHandle, int command)
    {
        // The native test protocol acknowledges queueing without blocking this thread inside the resulting modal dialog.
        if (SendMessageTimeout(windowHandle, WmUiTestCommand, command, 0, SmtoAbortIfHung, 2000, out var accepted) == 0 ||
            accepted == 0)
            throw new InvalidOperationException($"FileManager did not accept command {command} after reporting startup readiness.");
    }

    internal static void RefreshLeftPanel(nint windowHandle)
    {
        // Mode 1 is restricted natively to CM_LEFTREFRESH and returns only after the new panel listing is available.
        if (SendMessageTimeout(windowHandle, WmUiTestCommand, LeftPanelRefresh, 1, SmtoAbortIfHung, 5000, out var refreshed) == 0 ||
            refreshed == 0)
            throw new InvalidOperationException("FileManager did not synchronously refresh the source panel.");
    }

    internal static bool IsStartupReady(nint windowHandle)
    {
        // A bounded synchronous query distinguishes a created main window from a fully initialized application.
        return SendMessageTimeout(windowHandle, WmUiTestReady, 0, 0, SmtoAbortIfHung, 250, out var ready) != 0 &&
               ready != 0;
    }

    internal static long GetConfigurationGeneration(nint windowHandle)
    {
        // The generation advances after the Configuration handler completes persistence and post-dialog refresh work.
        return SendMessageTimeout(windowHandle, WmUiTestConfigGeneration, 0, 0, SmtoAbortIfHung, 2000, out var generation) != 0
            ? generation.ToInt64()
            : -1;
    }

    internal static void ArmNextConfigurationWriteFault(nint windowHandle, int writeBoundary)
    {
        // One-shot native arming prevents startup or cancel-dialog saves from consuming the requested boundary.
        if (writeBoundary <= 0 ||
            SendMessageTimeout(windowHandle, WmUiTestConfigFault, writeBoundary, 0, SmtoAbortIfHung, 2000, out var armed) == 0 ||
            armed == 0)
            throw new InvalidOperationException($"FileManager did not arm configuration write boundary {writeBoundary}.");
    }

    internal static int GetFtpOrganizeBookmarksCommand(nint windowHandle)
    {
        // Ask the isolated process after start-up because plug-in SUIDs belong to its native menu allocation.
        return SendMessageTimeout(windowHandle, WmUiTestFtpOrganizeCommand, 0, 0, SmtoAbortIfHung, 5000, out var command) != 0
            ? checked((int)command.ToInt64())
            : 0;
    }

    internal static nint[] GetTopLevelWindowHandles(int processId)
    {
        return GetVisibleTopLevelWindowHandles()
            .Where(windowHandle =>
            {
                GetWindowThreadProcessId(windowHandle, out var ownerProcessId);
                return ownerProcessId == (uint)processId;
            })
            .ToArray();
    }

    internal static nint[] GetVisibleTopLevelWindowHandles()
    {
        var handles = new List<nint>();
        // Native enumeration retains Win32 dialogs that some UIA providers omit from desktop and application searches.
        EnumWindows((windowHandle, _) =>
        {
            if (IsWindowVisible(windowHandle))
                handles.Add(windowHandle);
            return true;
        }, 0);
        return handles.ToArray();
    }

    internal static string GetWindowClassName(nint windowHandle)
    {
        var className = new StringBuilder(256);
        // Native class lookup remains reliable for owner-drawn windows whose UIA ClassName property is unsupported.
        return GetClassName(windowHandle, className, className.Capacity) > 0 ? className.ToString() : string.Empty;
    }

    internal static string GetWindowTitle(nint windowHandle)
    {
        var title = new StringBuilder(512);
        // Native title lookup avoids UIA property failures while classifying startup-only error dialogs.
        return GetWindowText(windowHandle, title, title.Capacity) > 0 ? title.ToString() : string.Empty;
    }

    internal static string GetMessageBoxText(nint dialogHandle)
    {
        var staticTextHandle = GetDlgItem(dialogHandle, -1);
        // Standard message boxes expose their body through the IDC_STATIC control even when UIA is unresponsive.
        return staticTextHandle != 0 ? GetWindowTitle(staticTextHandle) : string.Empty;
    }

    internal static string DescribeChildControls(nint dialogHandle)
    {
        var controls = new List<string>();
        // Native child inventory keeps startup failures diagnosable without invoking an unresponsive UIA provider.
        EnumChildWindows(dialogHandle, (controlHandle, _) =>
        {
            controls.Add($"id={GetDlgCtrlID(controlHandle)}, class='{GetWindowClassName(controlHandle)}', text='{GetWindowTitle(controlHandle)}'");
            return true;
        }, 0);
        return string.Join("; ", controls);
    }

    internal static bool IsWindowAvailable(nint windowHandle)
    {
        // HWND lifetime is authoritative when UIA retains a stale element after a modal dialog closes.
        return IsWindow(windowHandle);
    }

    internal static bool IsWindowEnabledForInput(nint windowHandle)
    {
        // Win32 enabled state provides a stable readiness check after a modal dialog releases its owner.
        return IsWindowEnabled(windowHandle);
    }

    internal static bool IsWindowResponsive(nint windowHandle)
    {
        // WM_NULL has no side effects and proves the owning UI thread can still process synchronous messages.
        return SendMessageTimeout(windowHandle, WmNull, 0, 0, SmtoAbortIfHung, 250, out _) != 0;
    }

    internal static void RequestWindowClose(nint windowHandle)
    {
        // Tests close only windows they opened, avoiding leaked Help or editor processes after an assertion.
        if (windowHandle != 0 && IsWindow(windowHandle))
            PostMessage(windowHandle, WmClose, 0, 0);
    }

    internal static void ClickDialogButton(nint dialogHandle, int buttonId)
    {
        var buttonHandle = GetDlgItem(dialogHandle, buttonId);
        // BM_CLICK follows the control's normal notification path when UIA's Invoke pattern is unavailable.
        if (buttonHandle == 0 || !PostMessage(buttonHandle, BmClick, 0, 0))
            throw new InvalidOperationException($"Dialog did not accept button command {buttonId}.");
    }

    internal static bool HasDialogControl(nint dialogHandle, int controlId)
    {
        // Resource IDs identify native controls without instantiating a potentially unresponsive UIA provider.
        return GetDlgItem(dialogHandle, controlId) != 0;
    }

    internal static bool IsDialogControlVisible(nint dialogHandle, int controlId)
    {
        // Visibility confirms that the requested property page, rather than a hidden child page, owns the control.
        return FindDescendantControl(dialogHandle, controlId, requireVisible: true) != 0;
    }

    internal static void SelectFirstTreePage(nint dialogHandle, int treeId)
    {
        var treeHandle = GetDlgItem(dialogHandle, treeId);
        var rootItem = treeHandle == 0 ? 0 : SendMessage(treeHandle, TvmGetNextItem, TvgnRoot, 0);
        // Selecting the root makes persisted-option tests independent of the user's remembered configuration page.
        if (rootItem == 0 || SendMessage(treeHandle, TvmSelectItem, TvgnCaret, rootItem) == 0)
            throw new InvalidOperationException("Configuration dialog did not expose a selectable General page.");
    }

    internal static bool GetDialogCheckBoxState(nint dialogHandle, int checkBoxId)
    {
        var checkBoxHandle = FindDescendantControl(dialogHandle, checkBoxId, requireVisible: true);
        if (checkBoxHandle == 0)
            throw new InvalidOperationException($"Dialog did not expose visible check box {checkBoxId}.");

        // Native button state is authoritative when UIA returns a cached ToggleState after process restart.
        return SendMessage(checkBoxHandle, BmGetCheck, 0, 0).ToInt64() switch
        {
            BstUnchecked => false,
            BstChecked => true,
            _ => throw new InvalidOperationException($"Dialog check box {checkBoxId} was indeterminate."),
        };
    }

    internal static void ToggleDialogCheckBox(nint dialogHandle, int checkBoxId)
    {
        var checkBoxHandle = FindDescendantControl(dialogHandle, checkBoxId, requireVisible: true);
        var originalState = GetDialogCheckBoxState(dialogHandle, checkBoxId);
        // Synchronous BM_CLICK makes the subsequent state assertion independent of UIA event delivery.
        SendMessage(checkBoxHandle, BmClick, 0, 0);
        if (GetDialogCheckBoxState(dialogHandle, checkBoxId) == originalState)
            throw new InvalidOperationException($"Dialog check box {checkBoxId} did not change after its native click.");
    }

    private static nint FindDescendantControl(nint parentHandle, int controlId, bool requireVisible)
    {
        nint result = 0;
        // Property-page controls are grandchildren of the outer dialog, so search the full native child hierarchy.
        EnumChildWindows(parentHandle, (controlHandle, _) =>
        {
            if (GetDlgCtrlID(controlHandle) != controlId || requireVisible && !IsWindowVisible(controlHandle))
                return true;
            result = controlHandle;
            return false;
        }, 0);
        return result;
    }

    internal static void SelectComboBoxItem(nint dialogHandle, nint comboHandle, int index)
    {
        // Win32 CB_SETCURSEL does not notify the owner, so mirror a user choice with the required CBN_SELCHANGE command.
        var selected = SendMessage(comboHandle, CbSetCurSel, index, 0).ToInt64();
        if (selected != index)
            throw new InvalidOperationException($"Combo box did not select item {index}.");

        var command = (CbnSelChange << 16) | (GetDlgCtrlID(comboHandle) & 0xFFFF);
        SendMessage(dialogHandle, WmCommand, command, comboHandle);
    }

    internal static int GetComboBoxSelection(nint comboHandle)
    {
        // Query native state because UIA can retain a stale SelectionItem view after a Win32 notification.
        return checked((int)SendMessage(comboHandle, CbGetCurSel, 0, 0).ToInt64());
    }

    internal static void QuickSearch(nint listHandle, string name)
    {
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, VkEscape, 0);
        foreach (var character in name)
            SendMessage(listHandle, WmChar, character, 1);
    }

    internal static void PressEnter(nint controlHandle)
    {
        // Submit native Help search input without depending on the localized search-button caption.
        SetFocus(controlHandle);
        SendMessage(controlHandle, WmKeyDown, VkReturn, 0);
        SendMessage(controlHandle, WmKeyUp, VkReturn, 0);
    }
}
