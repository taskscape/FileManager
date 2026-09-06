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
    // CM_OPEN drives the same archive-opening path as the Files > Open action without localized menu lookup.
    internal const int OpenFile = 732;
    // These stable host commands cover the three distinct file-discovery and file-opening paths.
    internal const int FindFiles = 741;
    internal const int ViewFile = 742;
    internal const int EditFile = 743;
    // CM_HELP_SEARCH opens the HTML Help search tab while keeping the test independent of translated menu labels.
    internal const int HelpSearch = 2212;
    internal const int RenameFile = 754;
    // CM_ACTIVEREFRESH synchronously refreshes the active file panel before a test quick-searches newly created files.
    internal const int RefreshActivePanel = 740;
    // CM_SWAPPANELS exchanges the two panel paths so Copy can be driven in the opposite direction.
    internal const int SwapPanels = 783;
    // CM_ACTIVEPARENTDIR / CM_ACTIVE_CHANGEDIR / CM_CHANGEFILTER keep navigation on the active panel.
    internal const int ParentDirectory = 822;
    internal const int ChangeDirectory = 862;
    internal const int ChangeFilter = 779;
    internal const int SelectByMask = 841;
    internal const int SelectAll = 842;
    // CM_ACTIVEUNSELECTALL prevents a stale selection from being combined with the item a test is about to mark.
    internal const int UnselectAll = 844;
    internal const int Pack = 850;
    internal const int Unpack = 851;
    internal const int CompareDirectories = 737;
    internal const int ChangeCase = 747;
    internal const int ChangeAttributes = 748;
    internal const int ConvertFiles = 814;
    // Find-dialog commands are posted to the modeless Find window, not the main window.
    internal const int FindFocus = 2225;
    internal const int FindDelete = 2282;
    internal const int FindDuplicates = 2292;
    // Stable dialog control IDs from src/lang/lang.rh; used instead of translated captions.
    internal const int FileMaskControl = 101;
    internal const int CopyNamedCheck = 217;
    internal const int CopyNamedMask = 216;
    internal const int FilterDontUse = 406;
    internal const int FilterUse = 407;
    internal const int FilterEdit = 409;
    internal const int PackerCombo = 511;
    internal const int PackMoveFiles = 512;
    internal const int UnpackDeleteArchive = 6210;
    internal const int UpperCaseRadio = 544;
    internal const int ReadOnlyAttribute = 244;
    internal const int CompressedAttribute = 246;
    internal const int EofCrlfRadio = 2494;
    internal const int CompareByTime = 192;
    internal const int CompareByContent = 193;
    internal const int CompareByAttr = 194;
    internal const int CompareSubdirs = 195;
    internal const int CompareBySize = 197;
    internal const int FindLookIn = 2501;
    internal const int FindIncludeSubdirs = 2503;
    internal const int FindContaining = 2504;
    internal const int FindNamed = 2505;
    internal const int FindGrep = 2508;
    internal const int FindResults = 2510;
    internal const int FindRegular = 2513;
    internal const int FindWholeWords = 2514;
    internal const int FindCaseSensitive = 2515;
    internal const int FindHex = 2516;
    internal const int DuplicateSameName = 2751;
    internal const int DuplicateSameSize = 2752;
    internal const int DuplicateSameContent = 2753;
    private const uint WmCommand = 0x0111;
    private const uint CbSetCurSel = 0x014E;
    private const uint CbGetCount = 0x0146;
    private const uint CbGetLbTextLen = 0x0149;
    private const uint CbGetLbText = 0x0148;
    private const int CbnSelChange = 1;
    private const uint WmChar = 0x0102;
    private const uint WmKeyDown = 0x0100;
    private const uint WmLButtonDown = 0x0201;
    private const uint WmLButtonUp = 0x0202;
    private const uint LvmGetItemCount = 0x1004;
    private const uint LbFindStringExact = 0x01A2;
    private const uint BmGetCheck = 0x00F0;
    private const uint BmSetCheck = 0x00F1;
    private const uint BmClick = 0x00F5;
    private const uint TbIsButtonEnabled = 0x0409;
    private const uint TbCommandToIndex = 0x0419;
    private const int ConfigurationClearReadOnlyCheckBox = 304;
    internal const int OperationPathControl = 210;
    private const int VkEscape = 0x1B;
    // HTML Help recognizes VK_RETURN from its search edit even when UIA exposes no reliable submit action.
    private const int VkReturn = 0x0D;
    private const uint WmSetText = 0x000C;
    private const uint WmGetText = 0x000D;
    private const uint WmGetTextLength = 0x000E;
    private const int GwlStyle = -16;
    private const long EsPassword = 0x0020;

    // Caption of the owner-less startup notice raised when a configuration
    // generation could not be validated (src/app_entry.cpp).
    private const string ConfigurationNoticeCaption = "Open Salamander Configuration";

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SendMessageTimeout(nint hWnd, uint msg, nint wParam, nint lParam, uint flags, uint timeoutMilliseconds, out nint result);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern nint SendMessageText(nint hWnd, uint msg, nint wParam, string lParam);

    // FTP's legacy list box expects ANSI text, unlike the Unicode operation-path controls above.
    [DllImport("user32.dll", CharSet = CharSet.Ansi, EntryPoint = "SendMessageA")]
    private static extern nint SendMessageAnsiText(nint hWnd, uint msg, nint wParam, string lParam);

    [DllImport("user32.dll", CharSet = CharSet.Ansi, EntryPoint = "SendMessageA")]
    private static extern nint SendMessageAnsiBuffer(nint hWnd, uint msg, nint wParam, StringBuilder lParam);

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

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowEnabled(nint windowHandle);

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

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern nint GetWindowLongPtr(nint windowHandle, int index);

    internal static void OpenConfiguration(nint windowHandle)
    {
        TraceAction("open-configuration", windowHandle);
        // Send directly because the custom menu loop consumes externally posted WM_COMMAND messages before native dispatch.
        SendMessage(windowHandle, WmCommand, Configuration, 0);
    }

    internal static void AcceptStartupLanguage(nint dialogHandle)
    {
        TraceAction("accept-startup-language", dialogHandle, "button=IDOK(1)");
        // The empty sandbox selects the bundled language with the standard IDOK command before the main window exists.
        SendMessage(dialogHandle, WmCommand, 1, 0);
    }

    internal static void CloseStandardDialog(nint dialogHandle, bool commit)
    {
        var controlId = commit ? 5 : 2;
        TraceAction("close-standard-dialog", dialogHandle, $"commit={commit} control={controlId}");
        var buttonHandle = GetDlgItem(dialogHandle, controlId);
        // Click the actual property-sheet child because a parent WM_COMMAND without its button handle is ignored by this legacy dialog.
        if (buttonHandle == 0)
            throw new InvalidOperationException($"Configuration dialog did not expose native button {controlId}.");
        SendMessage(buttonHandle, BmClick, 0, 0);
    }

    internal static void SelectComboBoxItem(nint dialogHandle, nint comboHandle, int itemIndex)
    {
        TraceAction("select-combo-item", dialogHandle, $"control={GetDlgCtrlID(comboHandle)} index={itemIndex}");
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

    internal static bool IsWindowEnabledWindow(nint windowHandle)
    {
        // Callers distinguish "no modal dialog is up" from "a modal dialog owns input"
        // through the owner's enabled state before re-posting a swallowed command.
        return IsWindow(windowHandle) && IsWindowEnabled(windowHandle);
    }

    internal static bool HasOperationPathControl(nint dialogHandle)
    {
        // The legacy copy/move templates use IDE_PATH even when UIA omits the inner edit of a combo box.
        return FindDialogControl(dialogHandle, OperationPathControl) != 0;
    }

    internal static bool HasDialogControl(nint dialogHandle, int controlId)
    {
        // Plug-ins own distinct dialog templates, so callers need to identify their stable control without relying on translated UIA names.
        return FindDialogControl(dialogHandle, controlId) != 0;
    }

    internal static void SetDialogControlText(nint dialogHandle, int controlId, string text)
    {
        var controlHandle = RequireDialogControl(dialogHandle, controlId);
        // Never serialize password-edit contents into a retained CI artifact.
        var loggedText = IsPasswordControl(controlHandle) ? "<redacted-password>" : QuoteForTrace(text);
        TraceAction("set-dialog-text", dialogHandle, $"control={controlId} text={loggedText}");
        // Send through the target window procedure so cross-process tests update the text that the native dialog will actually transfer.
        SendMessageText(controlHandle, WmSetText, 0, text);
    }

    internal static string GetDialogControlText(nint dialogHandle, int controlId)
    {
        var controlHandle = RequireDialogControl(dialogHandle, controlId);
        // Read through the same window procedure as the setter so retention checks do not observe UIA's cached title instead of the native value.
        var length = (int)SendMessage(controlHandle, WmGetTextLength, 0, 0);
        if (length <= 0)
            return string.Empty;
        var buffer = new StringBuilder(length + 1);
        SendMessageBuffer(controlHandle, WmGetText, buffer.Capacity, buffer);
        return buffer.ToString();
    }

    internal static void SetOperationPath(nint dialogHandle, string path)
    {
        var pathControl = RequireOperationPathControl(dialogHandle);
        TraceAction("set-operation-path", dialogHandle, $"path={QuoteForTrace(path)}");
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

    private static nint RequireDialogControl(nint dialogHandle, int controlId)
    {
        var controlHandle = FindDialogControl(dialogHandle, controlId);
        if (controlHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native control {controlId}.");
        return controlHandle;
    }
    /// <summary>
    /// Clicks a dialog button without waiting for the click to be handled. Use
    /// this when the button synchronously opens another modal dialog: the
    /// SendMessage form does not return until that dialog is dismissed, so the
    /// caller would deadlock before it could answer it.
    /// </summary>
    internal static void PostDialogButtonClick(nint dialogHandle, int controlId)
    {
        TraceAction("post-dialog-button", dialogHandle, $"control={controlId}");
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

    internal static string GetDialogText(nint dialogHandle)
    {
        var lines = new List<string>();
        var title = GetWindowTitle(dialogHandle);
        if (!string.IsNullOrWhiteSpace(title))
            lines.Add(title);

        // Message-box text lives in native child controls and is not always exposed by the legacy UIA provider.
        EnumChildWindows(dialogHandle, (childHandle, _) =>
        {
            var text = GetWindowTitle(childHandle).Trim();
            if (!string.IsNullOrWhiteSpace(text))
                lines.Add(text);
            return true;
        }, 0);
        return string.Join(Environment.NewLine, lines.Distinct(StringComparer.Ordinal));
    }

    internal static string DescribeWindowForTranscript(nint windowHandle, string processRole)
    {
        var className = GetWindowClass(windowHandle);
        var title = GetWindowTitle(windowHandle);
        var lines = new List<string>
        {
            $"window hwnd=0x{windowHandle:X} class={QuoteForTrace(className)} title={QuoteForTrace(title)} " +
            $"visible={IsWindowVisible(windowHandle)} enabled={IsWindowEnabled(windowHandle)}",
        };

        // The main panel tree is large and volatile; dialogs and salmon.exe carry the diagnostic text worth retaining.
        var includeControls = !string.Equals(className, "SalamanderMainWindowVer25", StringComparison.Ordinal) ||
                              string.Equals(processRole, "salmon.exe", StringComparison.OrdinalIgnoreCase);
        if (!includeControls)
            return string.Join(Environment.NewLine, lines);

        var controlCount = 0;
        EnumChildWindows(windowHandle, (childHandle, _) =>
        {
            if (controlCount >= 200)
                return false;
            controlCount++;
            var childClass = GetWindowClass(childHandle);
            var childText = IsPasswordControl(childHandle) ? "<redacted-password>" : GetWindowTitle(childHandle);
            if (childText.Length > 1_000)
                childText = childText[..1_000] + "<truncated>";
            lines.Add($"control[{controlCount}] hwnd=0x{childHandle:X} id={GetDlgCtrlID(childHandle)} " +
                      $"class={QuoteForTrace(childClass)} text={QuoteForTrace(childText)} " +
                      $"visible={IsWindowVisible(childHandle)} enabled={IsWindowEnabled(childHandle)}");
            return true;
        }, 0);
        if (controlCount >= 200)
            lines.Add("controls-truncated=true limit=200");
        return string.Join(Environment.NewLine, lines);
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
        TraceAction("click-dialog-button", dialogHandle, $"control={controlId}");
        var buttonHandle = FindDialogControl(dialogHandle, controlId);
        if (buttonHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native button {controlId}.");
        SendMessage(buttonHandle, BmClick, 0, 0);
    }

    internal static void SetDialogCheckBoxState(nint dialogHandle, int controlId, bool isChecked)
    {
        TraceAction("set-dialog-checkbox", dialogHandle, $"control={controlId} checked={isChecked}");
        var checkBoxHandle = FindDialogControl(dialogHandle, controlId);
        if (checkBoxHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native check box {controlId}.");

        // FTP's three-state defaults can be inherited from the host profile, so
        // set the exact per-connection state before exercising a live server.
        SendMessage(checkBoxHandle, BmSetCheck, isChecked ? 1 : 0, 0);
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
                TraceAction("dismiss-startup-dialog", windowHandle, $"title={QuoteForTrace(title)} button=IDOK(1)");
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
                TraceAction("dismiss-startup-dialog", windowHandle, $"title={QuoteForTrace(title)} button=IDCANCEL(2)");
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
        TraceAction("toggle-configuration-checkbox", dialogHandle,
                    $"control={ConfigurationClearReadOnlyCheckBox} from={originalState} to={(originalState == 0 ? 1 : 0)}");
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

    internal static void PostCloseWindow(nint windowHandle)
    {
        TraceAction("post-close-window", windowHandle);
        // UIA Window.Close can block while the Find UI thread is still in a nested search; WM_CLOSE is posted instead.
        if (!PostMessage(windowHandle, 0x0010, 0, 0)) // WM_CLOSE
            throw new InvalidOperationException("Could not post WM_CLOSE.");
    }

    internal static void Execute(nint windowHandle, int command)
    {
        TraceAction("post-command", windowHandle, $"command={DescribeCommand(command)}({command})");
        // Posting lets the test observe a modal operation dialog after the user-equivalent panel activation instead of blocking in its handler.
        if (!PostMessage(windowHandle, WmCommand, command, 0))
            throw new InvalidOperationException($"Could not post native command {command}.");
    }

    internal static void ExecuteSynchronously(nint windowHandle, int command)
    {
        TraceAction("send-command", windowHandle, $"command={DescribeCommand(command)}({command})");
        // Swap/parent must finish layout before the test reads the active path; posting would race the title and listing.
        SendMessage(windowHandle, WmCommand, command, 0);
    }

    internal static void RefreshActiveFilePanel(nint windowHandle)
    {
        TraceAction("refresh-active-panel", windowHandle, $"command={RefreshActivePanel}");
        // Refresh is non-modal, so send it synchronously to guarantee the panel has enumerated externally-created test files.
        SendMessage(windowHandle, WmCommand, RefreshActivePanel, 0);
    }

    internal static void ClearActiveSelection(nint windowHandle)
    {
        TraceAction("clear-active-selection", windowHandle, $"command={UnselectAll}");
        // Clearing through the host command synchronizes the selected set with the active panel instead of relying on a prior test's caret state.
        SendMessage(windowHandle, WmCommand, UnselectAll, 0);
    }

    internal static void QuickSearch(nint listHandle, string name)
    {
        TraceAction("quick-search", listHandle, $"text={QuoteForTrace(name)}");
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, VkEscape, 0);
        foreach (var character in name)
            SendMessage(listHandle, WmChar, character, 1);
    }

    internal static void PressEnter(nint controlHandle)
    {
        TraceAction("press-enter", controlHandle);
        // HTML Help commits its search field through the native Enter key path rather than a reliable UIA action.
        SetFocus(controlHandle);
        SendMessage(controlHandle, WmKeyDown, VkReturn, 0);
    }

    internal static void ToggleFocusedSelection(nint listHandle)
    {
        TraceAction("toggle-focused-selection", listHandle, "key=VK_INSERT");
        // Insert is the native panel gesture that selects the focused item while preserving prior selections.
        SetFocus(listHandle);
        SendMessage(listHandle, WmKeyDown, 0x2D, 0); // VK_INSERT
    }

    internal static void SelectFocusedListViewItem(nint listHandle)
    {
        TraceAction("select-focused-list-item", listHandle);
        // A client click selects the focused Find-results row without the Space toggle
        // that would deselect an item the search already marked.
        if (!GetClientRect(listHandle, out var rectangle) || rectangle.Right <= rectangle.Left || rectangle.Bottom <= rectangle.Top)
            throw new InvalidOperationException("The Find results list did not expose a usable client area.");
        SetFocus(listHandle);
        var x = Math.Min(8, rectangle.Right - 1);
        var y = Math.Min(8, rectangle.Bottom - 1);
        var point = unchecked((nint)((y << 16) | (x & 0xffff)));
        PostMessage(listHandle, WmLButtonDown, 0, point);
        PostMessage(listHandle, WmLButtonUp, 0, point);
    }

    internal static void ActivateFilePanel(nint listHandle)
    {
        TraceAction("activate-file-panel", listHandle);
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
        // LVM_GETITEMCOUNT must not use blocking SendMessage: Find's UI thread stays inside StartSearch
        // (especially Find Duplicates) and a synchronous send deadlocks testhost for the rest of the run.
        const uint smtoAbortIfHung = 0x0002;
        if (SendMessageTimeout(listHandle, LvmGetItemCount, 0, 0, smtoAbortIfHung, 300, out var count) == 0)
            return -1;
        return checked((int)count);
    }

    internal static void SelectAllActivePanel(nint windowHandle)
    {
        TraceAction("select-all-active-panel", windowHandle, $"command={SelectAll}");
        // Select All is non-modal; send it so Copy observes the same selected set a user would see.
        SendMessage(windowHandle, WmCommand, SelectAll, 0);
    }

    internal static int GetDialogCheckBoxState(nint dialogHandle, int controlId)
    {
        var checkBoxHandle = FindDialogControl(dialogHandle, controlId);
        if (checkBoxHandle == 0)
            throw new InvalidOperationException($"The dialog did not expose native check box {controlId}.");
        return (int)SendMessage(checkBoxHandle, BmGetCheck, 0, 0);
    }

    internal static bool IsDialogCheckBoxChecked(nint dialogHandle, int controlId)
    {
        return GetDialogCheckBoxState(dialogHandle, controlId) != 0;
    }

    internal static void ClickAttributeCheckBox(nint dialogHandle, int controlId, int wantedState)
    {
        TraceAction("set-attribute-checkbox", dialogHandle, $"control={controlId} wanted-state={wantedState}");
        // The Compressed box is 3-state; one BM_CLICK from Checked lands on Indeterminate (leave unchanged).
        for (var attempt = 0; attempt < 4; attempt++)
        {
            if (GetDialogCheckBoxState(dialogHandle, controlId) == wantedState)
                return;
            ClickDialogControl(dialogHandle, controlId);
        }

        throw new InvalidOperationException(
            $"Attribute check box {controlId} stayed at {GetDialogCheckBoxState(dialogHandle, controlId)} instead of {wantedState}.");
    }

    internal static void ClickDialogControl(nint dialogHandle, int controlId)
    {
        TraceAction("click-dialog-control", dialogHandle, $"control={controlId}");
        // BM_CLICK raises BN_CLICKED so 3-state attribute boxes record their dirty flag.
        ClickDialogButton(dialogHandle, controlId);
    }

    internal static bool IsDialogControlEnabled(nint dialogHandle, int controlId)
    {
        var controlHandle = FindDialogControl(dialogHandle, controlId);
        return controlHandle != 0 && IsWindowEnabled(controlHandle);
    }

    internal static void SelectComboBoxItemContaining(nint dialogHandle, int comboControlId, string text)
    {
        TraceAction("select-combo-item-containing", dialogHandle,
                    $"control={comboControlId} match={QuoteForTrace(text)}");
        var comboHandle = RequireDialogControl(dialogHandle, comboControlId);
        var count = (int)SendMessage(comboHandle, CbGetCount, 0, 0);
        for (var index = 0; index < count; index++)
        {
            var item = GetComboBoxItemText(comboHandle, index);
            if (item.Contains(text, StringComparison.OrdinalIgnoreCase))
            {
                SelectComboBoxItem(dialogHandle, comboHandle, index);
                return;
            }
        }

        throw new InvalidOperationException($"The combo box {comboControlId} did not contain an item matching '{text}'.");
    }

    private static string GetComboBoxItemText(nint comboHandle, int itemIndex)
    {
        // Packer titles are stored as ANSI strings in the host combo, matching the ANSI pack dialog.
        var length = (int)SendMessage(comboHandle, CbGetLbTextLen, itemIndex, 0);
        if (length <= 0)
            return string.Empty;
        var buffer = new StringBuilder(length + 1);
        SendMessageAnsiBuffer(comboHandle, CbGetLbText, itemIndex, buffer);
        return buffer.ToString();
    }

    private static void TraceAction(string action, nint windowHandle, string? details = null)
    {
        // Central native-action records preserve user-equivalent operations in the same order as window observations.
        var suffix = string.IsNullOrWhiteSpace(details) ? string.Empty : " " + details;
        UiTestTrace.Record("ACTION",
                           $"{action} hwnd=0x{windowHandle:X} class={QuoteForTrace(GetWindowClass(windowHandle))} " +
                           $"title={QuoteForTrace(GetWindowTitle(windowHandle))}{suffix}");
    }

    private static bool IsPasswordControl(nint controlHandle)
    {
        return string.Equals(GetWindowClass(controlHandle), "Edit", StringComparison.OrdinalIgnoreCase) &&
               ((long)GetWindowLongPtr(controlHandle, GwlStyle) & EsPassword) != 0;
    }

    private static string GetWindowClass(nint windowHandle)
    {
        var classBuffer = new char[256];
        GetClassName(windowHandle, classBuffer, classBuffer.Length);
        return new string(classBuffer).TrimEnd('\0');
    }

    private static string QuoteForTrace(string value)
    {
        var escaped = value.Replace("\\", "\\\\", StringComparison.Ordinal)
                           .Replace("\r", "\\r", StringComparison.Ordinal)
                           .Replace("\n", "\\n", StringComparison.Ordinal)
                           .Replace("\"", "\\\"", StringComparison.Ordinal);
        return $"\"{escaped}\"";
    }

    private static string DescribeCommand(int command) => command switch
    {
        Configuration => nameof(Configuration),
        CustomizeTopToolbar => nameof(CustomizeTopToolbar),
        CopyFiles => nameof(CopyFiles),
        MoveFiles => nameof(MoveFiles),
        DeleteFiles => nameof(DeleteFiles),
        CreateDirectory => nameof(CreateDirectory),
        OpenFile => nameof(OpenFile),
        FindFiles => nameof(FindFiles),
        ViewFile => nameof(ViewFile),
        EditFile => nameof(EditFile),
        HelpSearch => nameof(HelpSearch),
        RenameFile => nameof(RenameFile),
        RefreshActivePanel => nameof(RefreshActivePanel),
        SwapPanels => nameof(SwapPanels),
        ParentDirectory => nameof(ParentDirectory),
        ChangeDirectory => nameof(ChangeDirectory),
        ChangeFilter => nameof(ChangeFilter),
        SelectByMask => nameof(SelectByMask),
        SelectAll => nameof(SelectAll),
        UnselectAll => nameof(UnselectAll),
        Pack => nameof(Pack),
        Unpack => nameof(Unpack),
        CompareDirectories => nameof(CompareDirectories),
        ChangeCase => nameof(ChangeCase),
        ChangeAttributes => nameof(ChangeAttributes),
        ConvertFiles => nameof(ConvertFiles),
        FindFocus => nameof(FindFocus),
        FindDelete => nameof(FindDelete),
        FindDuplicates => nameof(FindDuplicates),
        _ => "dynamic-or-unknown",
    };
}
