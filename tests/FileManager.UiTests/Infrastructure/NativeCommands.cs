using System.Runtime.InteropServices;
using System.Text;

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
    // CM_ACTIVEREFRESH synchronously refreshes the active file panel before a test quick-searches newly created files.
    internal const int RefreshActivePanel = 740;
    // CM_ACTIVEUNSELECTALL prevents a stale selection from being combined with the item a test is about to mark.
    private const int UnselectAll = 844;
    private const uint WmCommand = 0x0111;
    private const uint CbSetCurSel = 0x014E;
    private const int CbnSelChange = 1;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const uint WmLButtonDown = 0x0201;
    private const uint WmLButtonUp = 0x0202;
    private const uint LvmGetItemCount = 0x1004;
    private const uint LbFindStringExact = 0x01A2;
    private const uint BmGetCheck = 0x00F0;
    private const uint BmClick = 0x00F5;
    private const uint TbIsButtonEnabled = 0x0409;
    private const uint TbCommandToIndex = 0x0419;
    private const int ConfigurationClearReadOnlyCheckBox = 304;
    internal const int OperationPathControl = 210;
    private const int VkEscape = 0x1B;
    private const uint WmSetText = 0x000C;
    private const uint WmGetText = 0x000D;
    private const uint WmGetTextLength = 0x000E;

    // Caption of the owner-less startup notice raised when a configuration
    // generation could not be validated (src/app_entry.cpp).
    private const string ConfigurationNoticeCaption = "Open Salamander Configuration";

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern nint SendMessageText(nint hWnd, uint msg, nint wParam, string lParam);

    // FTP's legacy list box expects ANSI text, unlike the Unicode operation-path controls above.
    [DllImport("user32.dll", CharSet = CharSet.Ansi, EntryPoint = "SendMessageA")]
    private static extern nint SendMessageAnsiText(nint hWnd, uint msg, nint wParam, string lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern nint SendMessageBuffer(nint hWnd, uint msg, nint wParam, StringBuilder lParam);

    [DllImport("user32.dll")]
    private static extern nint SetFocus(nint hWnd);

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect
    {
        internal int Left;
        internal int Top;
        internal int Right;
        internal int Bottom;
    }

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetClientRect(nint hWnd, out Rect rectangle);

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

    internal static void SelectComboBoxItem(nint dialogHandle, nint comboHandle, int itemIndex)
    {
        // UIA's SelectionItem pattern updates the native combo without its parent notification, unlike a user selection.
        if (SendMessage(comboHandle, CbSetCurSel, itemIndex, 0) == -1)
            throw new InvalidOperationException($"The native combo box did not contain item {itemIndex}.");

        var controlId = GetDlgCtrlID(comboHandle);
        var notification = (nint)((CbnSelChange << 16) | (controlId & 0xffff));
        SendMessage(dialogHandle, WmCommand, notification, comboHandle);
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

    internal static IReadOnlyList<string> GetTopLevelWindowTitles(int processId)
    {
        var titles = new List<string>();
        foreach (var windowHandle in GetTopLevelWindows(processId))
        {
            var buffer = new char[GetWindowTextLength(windowHandle) + 1];
            GetWindowText(windowHandle, buffer, buffer.Length);
            var title = new string(buffer).TrimEnd('\0');
            if (string.Equals(title, "Microsoft Visual C++ Runtime Library", StringComparison.Ordinal))
            {
                // Debug-runtime dialogs put the assertion details in child controls; include them before teardown kills the process.
                var details = new List<string>();
                EnumChildWindows(windowHandle, (childHandle, _) =>
                {
                    var childBuffer = new char[GetWindowTextLength(childHandle) + 1];
                    GetWindowText(childHandle, childBuffer, childBuffer.Length);
                    var childText = new string(childBuffer).TrimEnd('\0').Trim();
                    if (childText.Length != 0)
                        details.Add(childText.Replace("\r", " ").Replace("\n", " "));
                    return true;
                }, 0);
                if (details.Count != 0)
                    title += ": " + string.Join(" | ", details);
            }
            titles.Add(title);
        }
        // Preserve native modal captions in failures because legacy error templates often omit UI Automation text.
        return titles;
    }

    internal static IReadOnlyList<string> GetTopLevelWindowDescriptions(int processId)
    {
        var descriptions = new List<string>();
        foreach (var windowHandle in GetTopLevelWindows(processId))
        {
            var classBuffer = new char[64];
            GetClassName(windowHandle, classBuffer, classBuffer.Length);
            var titleBuffer = new char[GetWindowTextLength(windowHandle) + 1];
            GetWindowText(windowHandle, titleBuffer, titleBuffer.Length);
            // Include the native class with the caption so verifier startup timeouts distinguish a hidden main window from a modal error.
            descriptions.Add($"0x{windowHandle:X} class='{new string(classBuffer).TrimEnd('\0')}' title='{new string(titleBuffer).TrimEnd('\0')}'");
        }
        return descriptions;
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
        var pathControl = RequireOperationPathControl(dialogHandle);
        // WM_SETTEXT through SendMessage, not SetWindowText: across a process
        // boundary SetWindowText updates the cached window title that
        // GetWindowText then reads back, so a write and its verification agreed
        // with each other while the control itself never received the text and the
        // application read an empty value. Copy and Move hid this because their
        // default already names the destination the test wanted; Create Directory
        // and Rename need a value and were submitted with the default instead.
        SendMessageText(pathControl, WmSetText, 0, path);
    }

    internal static string GetOperationPath(nint dialogHandle)
    {
        var pathControl = RequireOperationPathControl(dialogHandle);
        // Read through the window procedure as well, so the verification observes
        // the same text the application will transfer out of the dialog.
        var length = (int)SendMessage(pathControl, WmGetTextLength, 0, 0);
        if (length <= 0)
            return string.Empty;
        var buffer = new StringBuilder(length + 1);
        SendMessageBuffer(pathControl, WmGetText, buffer.Capacity, buffer);
        return buffer.ToString();
    }

    private static nint RequireOperationPathControl(nint dialogHandle)
    {
        var pathControl = FindDialogControl(dialogHandle, OperationPathControl);
        if (pathControl == 0)
            throw new InvalidOperationException("The operation dialog did not expose its native destination/name input.");
        return pathControl;
    }
    /// <summary>
    /// Clicks a dialog button without waiting for the click to be handled. Use
    /// this when the button synchronously opens another modal dialog: the
    /// SendMessage form does not return until that dialog is dismissed, so the
    /// caller would deadlock before it could answer it.
    /// </summary>
    internal static void PostDialogButtonClick(nint dialogHandle, int controlId)
    {
        var buttonHandle = FindDialogControl(dialogHandle, controlId);
        if (buttonHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native button {controlId}.");
        PostMessage(buttonHandle, BmClick, 0, 0);
    }

    /// <summary>Handle of a top-level dialog with the given caption, 0 when absent.</summary>
    internal static nint FindDialogByTitle(int processId, string title)
    {
        foreach (var windowHandle in GetTopLevelWindows(processId))
        {
            if (string.Equals(GetWindowTitle(windowHandle), title, StringComparison.Ordinal))
                return windowHandle;
        }
        return 0;
    }

    internal static string GetWindowTitle(nint windowHandle)
    {
        // Native captions distinguish dialogs that deliberately share standard button IDs such as IDYES.
        var buffer = new char[GetWindowTextLength(windowHandle) + 1];
        GetWindowText(windowHandle, buffer, buffer.Length);
        return new string(buffer).TrimEnd(char.MinValue);
    }

    internal static bool HasDialogButton(nint dialogHandle, int controlId)
    {
        // Standard buttons remain native controls even where the legacy provider does not publish a Button pattern.
        return FindDialogControl(dialogHandle, controlId) != 0;
    }

    internal static bool FtpBookmarksContains(nint dialogHandle, string bookmarkName)
    {
        // The FTP list box is ANSI and owner-drawn, so query its native ANSI string table instead of UIA descendants.
        const int ftpBookmarksList = 561; // IDL_BOOKMARKS in src/plugins/ftp/lang/lang.rh.
        var listHandle = FindDialogControl(dialogHandle, ftpBookmarksList);
        if (listHandle == 0)
            throw new InvalidOperationException("FTP bookmarks dialog did not expose its native bookmark list.");
        return SendMessageAnsiText(listHandle, LbFindStringExact, -1, bookmarkName) != -1;
    }

    internal static bool? TryGetToolbarCommandEnabled(nint windowHandle, int command)
    {
        bool? enabled = null;
        // The toolbar receives state changes during the native idle cycle, even when the legacy menu retains its startup state.
        EnumChildWindows(windowHandle, (childHandle, _) =>
        {
            var classBuffer = new char[64];
            GetClassName(childHandle, classBuffer, classBuffer.Length);
            if (!string.Equals(new string(classBuffer).TrimEnd('\0'), "ToolbarWindow32", StringComparison.Ordinal))
                return true;

            if (SendMessage(childHandle, TbCommandToIndex, command, 0) != -1)
            {
                enabled = SendMessage(childHandle, TbIsButtonEnabled, command, 0) != 0;
                return false;
            }
            return true;
        }, 0);
        return enabled;
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
                string.Equals(title, "UnRAR", StringComparison.Ordinal) ||
                string.Equals(title, ConfigurationNoticeCaption, StringComparison.Ordinal))
            {
                // Plug-in notices: both plug-ins that used to fail here now load
                // cleanly (PictView decodes through WIC, UnRAR resolves its optional
                // RARLAB library only when a RAR is opened), but this stays as a
                // safety net so an unrelated plug-in cannot wedge UI startup.
                //
                // Configuration notice: the harness kills the application between
                // cases, which can interrupt a profile write. The product then
                // reports that it fell back to the last verified profile through an
                // owner-less MessageBox shown before the main window exists, so the
                // notice must be acknowledged or every later case times out waiting
                // for a main window that is not coming.
                ClickDialogButton(windowHandle, 1);
            }
            else if (string.Equals(title, "Check for New Versions", StringComparison.Ordinal))
            {
                // A restored plug-in profile may reopen its optional update prompt; decline it so startup can expose the host window.
                ClickDialogButton(windowHandle, 2);
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
        // Posting lets the test observe a modal operation dialog after the user-equivalent panel activation instead of blocking in its handler.
        if (!PostMessage(windowHandle, WmCommand, command, 0))
            throw new InvalidOperationException($"Could not post native command {command}.");
    }

    internal static void RefreshActiveFilePanel(nint windowHandle)
    {
        // Refresh is non-modal, so send it synchronously to guarantee the panel has enumerated externally-created test files.
        SendMessage(windowHandle, WmCommand, RefreshActivePanel, 0);
    }

    internal static void ClearActiveSelection(nint windowHandle)
    {
        // Clearing through the host command synchronizes the selected set with the active panel instead of relying on a prior test's caret state.
        SendMessage(windowHandle, WmCommand, UnselectAll, 0);
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

    internal static void ActivateFilePanel(nint listHandle)
    {
        if (!GetClientRect(listHandle, out var rectangle) || rectangle.Right <= rectangle.Left || rectangle.Bottom <= rectangle.Top)
            throw new InvalidOperationException("The source file panel did not expose a usable client area.");

        // A click in the empty lower-right list area changes the host's active panel without selecting or opening any sandbox item.
        var x = rectangle.Right - 1;
        var y = rectangle.Bottom - 1;
        var point = unchecked((nint)((y << 16) | (x & 0xffff)));
        SendMessage(listHandle, WmLButtonDown, 0, point);
        SendMessage(listHandle, WmLButtonUp, 0, point);
    }

    internal static int GetListViewItemCount(nint listHandle)
    {
        // LVM_GETITEMCOUNT crosses process boundaries without caller-owned buffers and works for the virtual Find list.
        return checked((int)SendMessage(listHandle, LvmGetItemCount, 0, 0));
    }
}
