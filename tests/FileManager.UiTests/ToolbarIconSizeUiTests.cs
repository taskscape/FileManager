using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[Category("UI")]
public sealed class ToolbarIconSizeUiTests : FileManagerUiTestBase
{
    [Test]
    public void Customize_toolbar_cycles_all_icon_sizes_and_persists_the_choice_after_restart()
    {
        var dialog = OpenCustomizeToolbarDialog();
        var combo = GetIconSizeCombo(dialog);
        var originalIndex = GetSelectedIndex(combo);

        Assert.That(combo.Items.Select(item => item.Name), Is.EqualTo(new[]
        {
            "Small (16x16 pixels)",
            "Medium (24x24 pixels)",
            "Large (32x32 pixels)",
        }));

        // Closing between choices exercises the native image-list rebuild and layout path for every supported size.
        for (var index = 0; index < 3; index++)
        {
            // Drive the native selection notification because UIA Select() alone bypasses the legacy dialog's CBN_SELCHANGE handler.
            NativeCommands.SelectComboBoxItem(dialog.Properties.NativeWindowHandle.Value,
                                               combo.Properties.NativeWindowHandle.Value, index);
            CloseCustomizeToolbarDialog(dialog);
            dialog = OpenCustomizeToolbarDialog();
            combo = GetIconSizeCombo(dialog);
            Assert.That(GetSelectedIndex(combo), Is.EqualTo(index));
        }

        CloseCustomizeToolbarDialog(dialog);
        Thread.Sleep(500); // The native configuration writer intentionally debounces user changes for 250 ms.
        RestartFileManager();

        dialog = OpenCustomizeToolbarDialog();
        combo = GetIconSizeCombo(dialog);
        Assert.That(GetSelectedIndex(combo), Is.EqualTo(2),
            "The large toolbar icon preference was not restored after restarting the executable.");

        // Restore the disposable profile to its incoming setting so repeated UI runs remain independent.
        NativeCommands.SelectComboBoxItem(dialog.Properties.NativeWindowHandle.Value,
                                           combo.Properties.NativeWindowHandle.Value, originalIndex);
        CloseCustomizeToolbarDialog(dialog);
        Thread.Sleep(500);
    }

    private Window OpenCustomizeToolbarDialog()
    {
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.CustomizeTopToolbar);
        return WaitForWindow(window => window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value);
    }

    private static ComboBox GetIconSizeCombo(Window dialog)
    {
        // The resource ID is the stable automation identity for the new native combo box.
        var combo = dialog.FindFirstDescendant(cf => cf.ByAutomationId("2748"))?.AsComboBox();
        Assert.That(combo, Is.Not.Null, "Customize Toolbar did not expose the icon-size selector.");
        return combo!;
    }

    private static int GetSelectedIndex(ComboBox combo)
    {
        var items = combo.Items;
        var selected = Array.FindIndex(items, item => item.IsSelected);
        Assert.That(selected, Is.GreaterThanOrEqualTo(0), "The icon-size selector had no selected item.");
        return selected;
    }

    private static void CloseCustomizeToolbarDialog(Window dialog)
    {
        var closeButton = dialog.FindFirstDescendant(cf => cf.ByAutomationId("1"))?.AsButton();
        Assert.That(closeButton, Is.Not.Null, "Customize Toolbar did not expose its Close button.");
        closeButton!.Invoke();
        WaitForWindowToClose(dialog);
    }
}
