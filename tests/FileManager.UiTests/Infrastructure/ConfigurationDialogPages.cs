using System.Runtime.InteropServices;
using System.Text;

namespace FileManager.UiTests.Infrastructure;

/// <summary>
/// Drives individual pages of the Configuration dialog so a sandboxed profile
/// can be reconfigured the way a user would.
///
/// The profile cannot be edited in the registry directly: a committed
/// configuration generation carries a checksum, and the product deliberately
/// rejects a hand-modified generation and falls back to the previous one
/// (<c>SelectCommittedConfigurationGeneration</c> in <c>src/mainwnd_config.cpp</c>).
/// Going through the real page keeps that integrity contract intact.
/// </summary>
internal static class ConfigurationDialogPages
{
    // The tree that selects pages is control 1 of the tree property-sheet holder
    // (_TPD_IDC_TREE in src/common/sheets.cpp).
    private const int PageTreeControl = 1;

    // Editors page controls (IDE_COMMAND / IDE_ARGUMENTS / IDE_INITDIR in src/lang/lang.rh).
    private const int CommandEdit = 352;
    private const int InitDirEdit = 357;
    private const int ArgumentsEdit = 359;

    // System page: "Remove files and directories immediately when deleted"
    // (IDR_RECYCLE1 in src/lang/lang.rh).
    private const int ImmediateDeletionRadio = 538;

    private const uint BmClick = 0x00F5;
    private const uint BmGetCheck = 0x00F0;

    private const uint WmSetText = 0x000C;
    private const uint WmGetText = 0x000D;
    private const uint WmGetTextLength = 0x000E;
    private const uint WmCommand = 0x0111;
    private const uint EnChange = 0x0300;

    private const uint TvmGetNextItem = 0x1100 + 10;
    private const uint TvmSelectItem = 0x1100 + 11;
    private const nint TvgnRoot = 0;
    private const nint TvgnNext = 1;
    private const nint TvgnChild = 4;
    private const nint TvgnCaret = 9;

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessage(nint hWnd, uint msg, nint wParam, nint lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern nint SendMessageString(nint hWnd, uint msg, nint wParam, string lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
    private static extern nint SendMessageBuffer(nint hWnd, uint msg, nint wParam, StringBuilder lParam);

    [DllImport("user32.dll")]
    private static extern nint GetDlgItem(nint hDlg, int controlId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint hWnd);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(nint hWnd, StringBuilder className, int maxCount);

    private delegate bool EnumChildCallback(nint windowHandle, nint parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumChildWindows(nint parent, EnumChildCallback callback, nint parameter);

    /// <summary>
    /// Selects the Editors page and rewrites its currently selected entry.
    /// Pages are identified by content rather than by a translated tree label:
    /// only the Editors page carries a command, and the seeded default is
    /// <c>notepad.exe</c> (src/mainwnd_init.cpp), while every seeded viewer entry
    /// has an empty command.
    /// </summary>
    /// <returns><c>true</c> when the entry was rewritten.</returns>
    internal static bool RewriteSelectedEditor(nint configurationDialog, string seededCommand,
                                               string command, string arguments, string initialDirectory)
    {
        var tree = GetDlgItem(configurationDialog, PageTreeControl);
        if (tree == 0)
            return false;

        foreach (var item in EnumerateTreeItems(tree))
        {
            SendMessage(tree, TvmSelectItem, TvgnCaret, item);
            // The holder creates the page dialog while handling TVN_SELCHANGED.
            Thread.Sleep(150);

            var page = FindVisiblePageWithCommand(configurationDialog, seededCommand);
            if (page == 0)
                continue;

            SetEditText(page, CommandEdit, command);
            SetEditText(page, ArgumentsEdit, arguments);
            SetEditText(page, InitDirEdit, initialDirectory);
            // The page stores all three edits from a single EN_CHANGE
            // (CCfgPageEditors::DialogProc -> StoreControls).
            SendMessage(page, WmCommand, (nint)((EnChange << 16) | (uint)CommandEdit), GetDlgItem(page, CommandEdit));
            return string.Equals(GetEditText(page, CommandEdit), command, StringComparison.Ordinal);
        }

        return false;
    }

    /// <summary>
    /// Selects the System page and switches deletion away from the Recycle Bin.
    ///
    /// With the Recycle Bin enabled the panel hands the whole delete to the
    /// shell (<c>CFilesWindow::DeleteThroughRecycleBin</c>), which reports its own
    /// errors in its own windows. The product's Retry/Skip/Skip All handling
    /// belongs to its delete engine, and that engine only runs for permanent
    /// deletion - the same path a user reaches with Shift+Delete.
    /// </summary>
    /// <returns><c>true</c> when the page accepted the choice.</returns>
    internal static bool SelectImmediateDeletion(nint configurationDialog)
    {
        var page = SelectPageWithControl(configurationDialog, ImmediateDeletionRadio);
        if (page == 0)
            return false;

        var radio = GetDlgItem(page, ImmediateDeletionRadio);
        if (radio == 0)
            return false;

        // BM_CLICK rather than BM_SETCHECK so the auto-radio group clears its
        // siblings; the page reads the group back through CTransferInfo on OK.
        SendMessage(radio, BmClick, 0, 0);
        return SendMessage(radio, BmGetCheck, 0, 0) == 1;
    }

    /// <summary>
    /// Walks the page tree until the created page carries <paramref name="controlId"/>.
    /// Pages are matched by content because their tree labels are translated.
    /// </summary>
    private static nint SelectPageWithControl(nint configurationDialog, int controlId)
    {
        var tree = GetDlgItem(configurationDialog, PageTreeControl);
        if (tree == 0)
            return 0;

        foreach (var item in EnumerateTreeItems(tree))
        {
            SendMessage(tree, TvmSelectItem, TvgnCaret, item);
            // The holder creates the page dialog while handling TVN_SELCHANGED.
            Thread.Sleep(150);

            var page = FindVisiblePage(configurationDialog, candidate => GetDlgItem(candidate, controlId) != 0);
            if (page != 0)
                return page;
        }

        return 0;
    }

    private static IEnumerable<nint> EnumerateTreeItems(nint tree)
    {
        // Depth-first walk; pages can be nested below a category node.
        var pending = new Stack<nint>();
        var root = SendMessage(tree, TvmGetNextItem, TvgnRoot, 0);
        while (root != 0)
        {
            pending.Push(root);
            root = SendMessage(tree, TvmGetNextItem, TvgnNext, root);
        }

        while (pending.Count > 0)
        {
            var item = pending.Pop();
            yield return item;
            var child = SendMessage(tree, TvmGetNextItem, TvgnChild, item);
            while (child != 0)
            {
                pending.Push(child);
                child = SendMessage(tree, TvmGetNextItem, TvgnNext, child);
            }
        }
    }

    private static nint FindVisiblePageWithCommand(nint configurationDialog, string seededCommand) =>
        FindVisiblePage(configurationDialog, page =>
            GetDlgItem(page, CommandEdit) != 0 &&
            string.Equals(GetEditText(page, CommandEdit), seededCommand, StringComparison.OrdinalIgnoreCase));

    private static nint FindVisiblePage(nint configurationDialog, Func<nint, bool> predicate)
    {
        nint match = 0;
        EnumChildWindows(configurationDialog, (child, _) =>
        {
            if (!IsWindowVisible(child))
                return true;
            var className = new StringBuilder(64);
            GetClassName(child, className, className.Capacity);
            if (!string.Equals(className.ToString(), "#32770", StringComparison.Ordinal))
                return true;
            if (!predicate(child))
                return true;

            match = child;
            return false;
        }, 0);
        return match;
    }

    private static void SetEditText(nint page, int controlId, string text)
    {
        var edit = GetDlgItem(page, controlId);
        if (edit != 0)
            SendMessageString(edit, WmSetText, 0, text);
    }

    private static string GetEditText(nint page, int controlId)
    {
        var edit = GetDlgItem(page, controlId);
        if (edit == 0)
            return string.Empty;
        var length = (int)SendMessage(edit, WmGetTextLength, 0, 0);
        var buffer = new StringBuilder(length + 1);
        SendMessageBuffer(edit, WmGetText, buffer.Capacity, buffer);
        return buffer.ToString();
    }
}
