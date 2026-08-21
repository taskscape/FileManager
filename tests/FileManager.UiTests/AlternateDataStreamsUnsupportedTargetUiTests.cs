using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

// This fixture needs a caller-supplied FAT/FAT32/exFAT-like volume. It creates
// and removes only a GUID child below that dedicated root.
[TestFixture]
public sealed class AlternateDataStreamsUnsupportedTargetUiTests : FileOperationUiTestBase
{
    protected override string? TargetVolumeRoot => UiTestSettings.RequireUnsupportedAdsTargetRoot();

    [Test]
    [Category("CrossVolume")]
    [Category("AlternateDataStreams")]
    public void Cross_volume_move_to_an_ADS_unsupported_target_keeps_the_source_when_metadata_loss_is_declined()
    {
        Assert.That(Path.GetPathRoot(Workspace.SourceDirectory), Is.Not.EqualTo(Path.GetPathRoot(Workspace.TargetDirectory)),
                    "The unsupported-target fixture must use different source and target volumes.");
        AlternateDataStreams.RequireSupportAt(Workspace.SourceDirectory);
        AlternateDataStreams.RequireUnsupportedAt(Workspace.TargetDirectory);

        // This ADS-bearing input is seeded before startup so quick-search cannot
        // select an earlier collision fixture while the source panel is stale.
        var source = Workspace.SourcePath("ads-unsupported-target.txt");
        var target = Workspace.TargetPath("ads-unsupported-target.txt");
        // The dedicated target starts empty; an overwrite prompt here would prove selection raced the panel listing.
        Assert.That(File.Exists(target), Is.False, "The ADS-loss test target must not collide before the move starts.");

        ExecuteWithPath(NativeCommands.MoveFiles, "ads-unsupported-target.txt", Workspace.TargetDirectory, commit: true);
        // The custom ADS dialog and an overwrite dialog both expose IDYES, so keep the caption invariant with the response.
        ChooseOperationPrompt(WaitForOperationPrompt("Confirm Alternate Data Streams Loss", 6), 6); // IDYES: copy the default stream while discarding unsupported ADS.
        // The metadata-loss decision is a standard Question message box whose No preserves the ADS-bearing source.
        ChooseOperationPrompt(WaitForOperationPrompt("Question", 7), 7); // IDNO: retain the source after the metadata-loss warning.

        WaitForFileSystem(() => File.Exists(target), "The unsupported target did not receive the default data stream.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(target), Is.EqualTo("ads-unsupported-default-content"));
            Assert.That(File.Exists(source), Is.True, "Declining metadata loss deleted the ADS-bearing source.");
            AlternateDataStreams.AssertContent(source, "must-not-silently-disappear", "source-stream-content"u8.ToArray());
            AlternateDataStreams.AssertAbsent(target, "must-not-silently-disappear");
        });
    }
}
