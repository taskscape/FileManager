using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

// This fixture only runs when the caller explicitly supplies a dedicated
// second volume.  It never deletes the supplied root, only its GUID child.
[TestFixture]
public sealed class CrossVolumeMoveCharacterizationUiTests : FileOperationUiTestBase
{
    protected override string? TargetVolumeRoot => UiTestSettings.RequireCrossVolumeRoot();

    [Test]
    [Category("CrossVolume")]
    public void Move_across_volumes_copies_the_complete_tree_before_removing_the_source()
    {
        Assert.That(Path.GetPathRoot(Workspace.SourceDirectory), Is.Not.EqualTo(Path.GetPathRoot(Workspace.TargetDirectory)),
                    "The cross-volume fixture must use different source and target volumes.");

        ExecuteWithPath(NativeCommands.MoveFiles, "move-tree", Workspace.TargetDirectory, commit: true);

        var targetPayload = Workspace.TargetPath("move-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(targetPayload), "Cross-volume move did not create the complete target tree.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(targetPayload), Is.EqualTo("move-tree-content"));
            Assert.That(Directory.Exists(Workspace.SourcePath("move-tree")), Is.False,
                        "Cross-volume move retained the source after the target was committed.");
        });
    }
}
