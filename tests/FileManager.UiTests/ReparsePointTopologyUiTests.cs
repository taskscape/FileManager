using System.Diagnostics;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

// The topology is prepared before FileManager starts so panel enumeration sees
// the reparse entries without relying on a refresh race.  Every target is a
// child of the fixture's disposable workspace; the target is nevertheless
// outside the selected operation root.
[TestFixture]
[Category("ReparsePoints")]
public sealed class ReparsePointTopologyUiTests : FileOperationUiTestBase
{
    private string operationRoot = null!;
    private string firstOutsideTarget = null!;
    private string changedOutsideTarget = null!;
    private string deleteTarget = null!;
    private bool directorySymlinkAvailable;

    protected override void BeforeFileManagerStarted()
    {
        // Preserve base journal isolation before creating the topology visible at application startup.
        base.BeforeFileManagerStarted();
        operationRoot = Workspace.SourcePath("reparse-operation-root");
        firstOutsideTarget = Path.Combine(Workspace.RootDirectory, "outside-first-target");
        changedOutsideTarget = Path.Combine(Workspace.RootDirectory, "outside-changed-target");
        deleteTarget = Path.Combine(Workspace.RootDirectory, "delete-target");
        Directory.CreateDirectory(operationRoot);
        Directory.CreateDirectory(firstOutsideTarget);
        Directory.CreateDirectory(changedOutsideTarget);
        Directory.CreateDirectory(deleteTarget);
        File.WriteAllText(Path.Combine(operationRoot, "inside.txt"), "operation-root-content");
        File.WriteAllText(Path.Combine(firstOutsideTarget, "sentinel.txt"), "first-target-content");
        File.WriteAllText(Path.Combine(changedOutsideTarget, "sentinel.txt"), "changed-target-content");
        File.WriteAllText(Path.Combine(deleteTarget, "sentinel.txt"), "delete-target-content");

        var changedLink = Path.Combine(operationRoot, "changed-junction");
        CreateJunction(changedLink, firstOutsideTarget);
        Directory.Delete(changedLink);
        CreateJunction(changedLink, changedOutsideTarget);

        // This deliberately points back into the selected tree.  A recursive
        // planner that follows it never reaches a finite plan.
        CreateJunction(Path.Combine(operationRoot, "cycle-junction"), operationRoot);
        CreateJunction(Workspace.SourcePath("delete-junction"), deleteTarget);

        try
        {
            Directory.CreateSymbolicLink(Path.Combine(operationRoot, "outside-symlink"), changedOutsideTarget);
            directorySymlinkAvailable = true;
        }
        catch (Exception exception) when (IsSymbolicLinkPrivilegeUnavailable(exception))
        {
            // Junction coverage remains available when Windows reports ERROR_PRIVILEGE_NOT_HELD as IOException or access denied.
        }
    }

    [Test]
    public void Copy_does_not_traverse_changed_or_cyclic_junction_targets_outside_the_operation_root()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "reparse-operation-root", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.TargetPath("reparse-operation-root\\inside.txt")),
                          "Copy did not preserve the non-reparse member of the selected operation root.");

        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Path.Combine(firstOutsideTarget, "sentinel.txt")), Is.EqualTo("first-target-content"));
            Assert.That(File.ReadAllText(Path.Combine(changedOutsideTarget, "sentinel.txt")), Is.EqualTo("changed-target-content"));
            Assert.That(Directory.Exists(Workspace.TargetPath("reparse-operation-root\\changed-junction")), Is.False,
                        "Copy must not materialize or traverse a reparse directory target.");
            Assert.That(Directory.Exists(Workspace.TargetPath("reparse-operation-root\\cycle-junction")), Is.False,
                        "Copy must not enter a cyclic reparse directory.");
        });
    }

    [Test]
    public void Delete_junction_removes_only_the_link_and_never_its_target()
    {
        SelectSourceItem("delete-junction");
        NativeCommands.Execute(MainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !Directory.Exists(Workspace.SourcePath("delete-junction")),
                          "Delete did not remove the selected junction.");
        Assert.That(File.ReadAllText(Path.Combine(deleteTarget, "sentinel.txt")), Is.EqualTo("delete-target-content"));
    }

    [Test]
    public void Copy_does_not_traverse_a_directory_symbolic_link_outside_the_operation_root()
    {
        if (!directorySymlinkAvailable)
            Assert.Ignore("The current test host does not permit disposable directory symbolic links.");

        ExecuteWithPath(NativeCommands.CopyFiles, "reparse-operation-root", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.TargetPath("reparse-operation-root\\inside.txt")),
                          "Copy did not preserve the non-reparse member of the selected operation root.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Path.Combine(changedOutsideTarget, "sentinel.txt")), Is.EqualTo("changed-target-content"));
            Assert.That(Directory.Exists(Workspace.TargetPath("reparse-operation-root\\outside-symlink")), Is.False,
                        "Copy must not materialize or traverse a directory symbolic-link target.");
        });
    }

    private static void CreateJunction(string linkPath, string targetPath)
    {
        var commandInterpreter = Environment.GetEnvironmentVariable("ComSpec") ?? "cmd.exe";
        using var process = Process.Start(new ProcessStartInfo(commandInterpreter,
            $"/d /s /c mklink /J \"{linkPath}\" \"{targetPath}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
        });
        Assert.That(process, Is.Not.Null, "The test host could not start cmd.exe to create a disposable junction.");
        process!.WaitForExit();
        Assert.That(process.ExitCode, Is.Zero, "Creating the disposable junction failed.");
        Assert.That(Directory.Exists(linkPath), Is.True, "The disposable junction was not created.");
    }

    private static bool IsSymbolicLinkPrivilegeUnavailable(Exception exception)
    {
        // .NET maps CreateSymbolicLink privilege failures differently across Windows runtime versions.
        return exception is UnauthorizedAccessException ||
               exception is IOException && (exception.HResult & 0xFFFF) == 1314; // ERROR_PRIVILEGE_NOT_HELD
    }
}
