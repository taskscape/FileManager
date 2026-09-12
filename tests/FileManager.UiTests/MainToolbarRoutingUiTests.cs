using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class MainToolbarRoutingUiTests : FileOperationUiTestBase
{
    [Test]
    public void Default_middle_toolbar_buttons_route_to_their_user_observable_commands()
    {
        EnsureMiddleToolbarIsVisible();

        // Copy opens a modal operation through the button route and preserves the owned source file after committing to the other panel.
        SelectSourceItem("copy-file.txt");
        ClickToolbarCommand(NativeCommands.CopyFiles);
        var copyDialog = WaitForOperationDialog();
        SetDialogPath(copyDialog, Workspace.TargetDirectory);
        CloseDialog(copyDialog, commit: true);
        WaitForOperationOutputToBeReleased(Workspace.TargetPath("copy-file.txt"),
            "Toolbar Copy did not release the destination file.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("copy-file.txt")), Is.EqualTo("copy-file-content"));

        // Swapping through the hit-tested button must change the path used by later file operations.
        ClickToolbarCommand(NativeCommands.SwapPanels);
        ActivateSourcePanel();
        WaitForMainWindowTitleContaining("target", "Toolbar Swap Panels did not move the target path to the left panel.");

        // Create Directory exercises the modal path with an owned relative name rather than a translated caption.
        ClickToolbarCommand(NativeCommands.CreateDirectory);
        var createDialog = WaitForOperationDialog(NativeCommands.CreateDirectory);
        SetDialogPath(createDialog, "toolbar-created");
        CloseDialog(createDialog, commit: true);
        WaitForFileSystem(() => Directory.Exists(Workspace.TargetPath("toolbar-created")),
            "Toolbar Create Directory did not create the owned target directory.");

        // The View button needs an explicit target-panel selection after Swap Panels exchanged the two workspace paths.
        SelectTargetItem("view-file.txt");
        ClickToolbarCommand(NativeCommands.ViewFile);
        var viewer = WaitForWindow(window =>
            string.Equals(window.Properties.ClassName.ValueOrDefault, "Salamander's Viewer Window", StringComparison.Ordinal));
        Assert.That(viewer.Name, Does.Contain("view-file.txt").IgnoreCase,
                    "Toolbar View did not open the selected owned file.");
        viewer.Close();
        WaitForWindowToClose(viewer);

        // Find is modeless, so its stable results control proves that the button reached the separate Find window route.
        ClickToolbarCommand(NativeCommands.FindFiles);
        var findDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != NativeMainWindowHandle &&
            window.FindFirstDescendant(cf => cf.ByAutomationId(NativeCommands.FindResults.ToString())) is not null);
        CloseModelessDialog(findDialog);
    }

    private void EnsureMiddleToolbarIsVisible()
    {
        if (NativeCommands.HasDefaultMiddleToolbar(NativeMainWindowHandle))
            return;

        // Fresh profiles hide the documented middle toolbar, so reveal its default command set through the product before testing its buttons.
        NativeCommands.ExecuteSynchronously(NativeMainWindowHandle, NativeCommands.ToggleMiddleToolbar);
        WaitForFileSystem(() => NativeCommands.HasDefaultMiddleToolbar(NativeMainWindowHandle),
            "Options > Visible Toolbars did not reveal the default middle toolbar needed for routing coverage.");
    }

    private void ClickToolbarCommand(int command)
    {
        var invoked = NativeCommands.TryInvokeToolbarCommand(NativeMainWindowHandle, command, out var failure);
        Assert.That(invoked, Is.True, failure);
    }
}
