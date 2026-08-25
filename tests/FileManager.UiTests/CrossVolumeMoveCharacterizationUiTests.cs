using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

// The runner supplies fixed writable D: when available; direct callers may provide
// the same dedicated root explicitly. The fixture removes only its GUID child.
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
        // Cross-volume copies cannot preserve every timestamp, so acknowledge the explicit gate before expecting source deletion.
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

        var targetPayload = Workspace.TargetPath("move-tree\\nested\\payload.txt");
        // A cross-volume move stages a copy first, so visibility of the target does not prove the worker released it.
        WaitForOperationOutputToBeReleased(targetPayload, "Cross-volume move did not release the complete target tree.");
        // Destination handles can close before the source-removal transaction commits, so wait for that independent completion boundary.
        WaitForFileSystem(() => !Directory.Exists(Workspace.SourcePath("move-tree")),
                          "Cross-volume move committed the target but did not remove the source tree.");
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
        // ADS survives on the NTFS target, but the cross-volume metadata gate still protects source deletion.
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

        // ADS verification must wait for the staged cross-volume copy to close its destination handle.
        WaitForOperationOutputToBeReleased(target, "Cross-volume move did not release the ADS test target.");
        // ADS copy completion is distinct from deleting the source stream set on the original volume.
        WaitForFileSystem(() => !File.Exists(source),
                          "Cross-volume move committed the ADS target but did not remove the source file.");
        Assert.Multiple(() =>
        {
            Assert.That(File.Exists(source), Is.False, "Cross-volume move retained the source after copying ADS.");
            AlternateDataStreams.AssertContent(target, "first", "first-cross-volume-stream"u8.ToArray());
            AlternateDataStreams.AssertContent(target, "second", "second-cross-volume-stream"u8.ToArray());
        });
    }
}
