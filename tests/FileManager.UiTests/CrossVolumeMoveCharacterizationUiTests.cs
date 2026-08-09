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

    [Test]
    [Category("CrossVolume")]
    [Category("AlternateDataStreams")]
    public void Move_across_ADS_capable_volumes_preserves_multiple_streams_before_removing_the_source()
    {
        Assert.That(Path.GetPathRoot(Workspace.SourceDirectory), Is.Not.EqualTo(Path.GetPathRoot(Workspace.TargetDirectory)),
                    "The cross-volume fixture must use different source and target volumes.");
        AlternateDataStreams.RequireSupportAt(Workspace.SourceDirectory);
        AlternateDataStreams.RequireSupportAt(Workspace.TargetDirectory);

        var source = Workspace.SourcePath("ads-cross-volume.txt");
        var target = Workspace.TargetPath("ads-cross-volume.txt");
        File.WriteAllText(source, "ads-cross-volume-default-content");
        AlternateDataStreams.Write(source, "first", "first-cross-volume-stream"u8.ToArray());
        AlternateDataStreams.Write(source, "second", "second-cross-volume-stream"u8.ToArray());

        ExecuteWithPath(NativeCommands.MoveFiles, "ads-cross-volume.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(target), "Cross-volume move did not create the ADS test target.");
        Assert.Multiple(() =>
        {
            Assert.That(File.Exists(source), Is.False, "Cross-volume move retained the source after copying ADS.");
            AlternateDataStreams.AssertContent(target, "first", "first-cross-volume-stream"u8.ToArray());
            AlternateDataStreams.AssertContent(target, "second", "second-cross-volume-stream"u8.ToArray());
        });
    }
}
