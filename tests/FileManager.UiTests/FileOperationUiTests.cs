using FileManager.UiTests.Infrastructure;
using FlaUI.Core.AutomationElements;
using NUnit.Framework;
using System.Text.RegularExpressions;

namespace FileManager.UiTests;

[TestFixture]
public sealed class FileOperationUiTests : FileOperationUiTestBase
{
    [Test]
    public void Create_directory_creates_requested_nested_directory()
    {
        ExecuteWithPath(NativeCommands.CreateDirectory, string.Empty, "created\\nested", commit: true);
        // A disposable profile may ask before creating the missing parent; the test owns that normal interaction.
        ConfirmCreateDirectoryParentsIfPrompted();

        WaitForFileSystem(() => Directory.Exists(Workspace.SourcePath("created\\nested")),
                          "Create Directory did not create the requested nested directory.");
    }

    [Test]
    public void Copy_file_copies_content_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.TargetPath("copy-file.txt")), "Copy did not create the destination file.");
        Assert.That(File.ReadAllText(Workspace.TargetPath("copy-file.txt")), Is.EqualTo("copy-file-content"));
        Assert.That(File.Exists(Workspace.SourcePath("copy-file.txt")), Is.True, "Copy unexpectedly removed the source file.");
    }

    [Test]
    public void Copy_preserves_last_write_time_metadata()
    {
        var source = Workspace.SourcePath("copy-file.txt");
        var expectedTimestamp = new DateTime(2024, 03, 21, 12, 34, 56, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(source, expectedTimestamp);

        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);

        var target = Workspace.TargetPath("copy-file.txt");
        WaitForFileSystem(() => File.Exists(target), "Copy did not create the destination file.");
        Assert.That(File.GetLastWriteTimeUtc(target), Is.EqualTo(File.GetLastWriteTimeUtc(source)),
                    "Copy did not preserve the source last-write metadata.");
    }

    [Test]
    [Category("AlternateDataStreams")]
    public void Copy_preserves_multiple_empty_large_and_edge_named_alternate_data_streams()
    {
        AlternateDataStreams.RequireSupportAt(Workspace.SourceDirectory);
        AlternateDataStreams.RequireSupportAt(Workspace.TargetDirectory);

        var source = Workspace.SourcePath("ads-copy.txt");
        var target = Workspace.TargetPath("ads-copy.txt");
        var large = CreateLargeStreamContent();
        File.WriteAllText(source, "ads-copy-default-content");
        AlternateDataStreams.Write(source, "notes", "named-stream-content"u8.ToArray());
        AlternateDataStreams.Write(source, "empty", []);
        AlternateDataStreams.Write(source, "large", large);
        AlternateDataStreams.Write(source, "edge name.with.dots", "edge-stream-content"u8.ToArray());

        ExecuteWithPath(NativeCommands.CopyFiles, "ads-copy.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => File.Exists(target), "Copy did not create the ADS test target.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(target), Is.EqualTo("ads-copy-default-content"));
            AlternateDataStreams.AssertContent(target, "notes", "named-stream-content"u8.ToArray());
            AlternateDataStreams.AssertContent(target, "empty", []);
            AlternateDataStreams.AssertContent(target, "large", large);
            AlternateDataStreams.AssertContent(target, "edge name.with.dots", "edge-stream-content"u8.ToArray());
        });
    }

    [Test]
    [Category("AlternateDataStreams")]
    public void Copy_overwrite_replaces_target_streams_and_removes_stale_streams()
    {
        AlternateDataStreams.RequireSupportAt(Workspace.SourceDirectory);
        AlternateDataStreams.RequireSupportAt(Workspace.TargetDirectory);

        var source = Workspace.SourcePath("ads-overwrite.txt");
        var target = Workspace.TargetPath("ads-overwrite.txt");
        File.WriteAllText(source, "ads-overwrite-source");
        File.WriteAllText(target, "ads-overwrite-target");
        AlternateDataStreams.Write(source, "replacement", "replacement-stream-content"u8.ToArray());
        AlternateDataStreams.Write(target, "replacement", "stale-replacement-content"u8.ToArray());
        AlternateDataStreams.Write(target, "stale", "stale-stream-content"u8.ToArray());

        ExecuteWithPath(NativeCommands.CopyFiles, "ads-overwrite.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(6, TimeSpan.FromSeconds(3), 6); // IDYES when this profile confirms overwrites.

        WaitForFileSystem(() => File.ReadAllText(target) == "ads-overwrite-source",
                          "Confirmed overwrite did not replace the ADS test target.");
        Assert.Multiple(() =>
        {
            AlternateDataStreams.AssertContent(target, "replacement", "replacement-stream-content"u8.ToArray());
            AlternateDataStreams.AssertAbsent(target, "stale");
        });
    }

    [Test]
    [Category("AlternateDataStreams")]
    public void Copy_retries_a_temporarily_denied_alternate_data_stream_without_losing_it()
    {
        AlternateDataStreams.RequireSupportAt(Workspace.SourceDirectory);
        AlternateDataStreams.RequireSupportAt(Workspace.TargetDirectory);

        var source = Workspace.SourcePath("ads-retry.txt");
        var target = Workspace.TargetPath("ads-retry.txt");
        File.WriteAllText(source, "ads-retry-default-content");
        AlternateDataStreams.Write(source, "temporarily-denied", "retry-stream-content"u8.ToArray());

        var deniedStream = AlternateDataStreams.LockForRead(source, "temporarily-denied");
        try
        {
            ExecuteWithPath(NativeCommands.CopyFiles, "ads-retry.txt", Workspace.TargetDirectory, commit: true);
            var retryPrompt = WaitForOperationPrompt(4); // IDRETRY
            deniedStream.Dispose();
            ChooseOperationPrompt(retryPrompt, 4);
        }
        finally
        {
            deniedStream.Dispose();
        }

        WaitForFileSystem(() => File.Exists(target), "Retry did not complete the ADS copy.");
        AlternateDataStreams.AssertContent(target, "temporarily-denied", "retry-stream-content"u8.ToArray());
    }

    [Test]
    public void Copy_overwrite_replaces_the_existing_target_only_after_the_user_confirms()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "overwrite-file.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(6, TimeSpan.FromSeconds(3), 6); // IDYES when this profile confirms overwrites.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("overwrite-file.txt")) == "overwrite-source-content",
                          "Confirmed overwrite did not replace the target content.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("overwrite-file.txt")), Is.EqualTo("overwrite-source-content"));
    }

    [Test]
    public void Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "overwrite-all-tree", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(185, TimeSpan.FromSeconds(3), 185); // IDB_ALL when conflict choices are enabled.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("overwrite-all-tree\\nested\\first.txt")) == "overwrite-all-first-source" &&
                                File.ReadAllText(Workspace.TargetPath("overwrite-all-tree\\nested\\second.txt")) == "overwrite-all-second-source",
                          "Overwrite All did not reconcile every conflicting descendant.");
    }

    [Test]
    public void Copy_skip_keeps_the_existing_target_and_the_source()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "skip-file.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(173, TimeSpan.FromSeconds(3), 173); // Semantic checks expose an unavailable Skip choice.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("skip-file.txt")) == "skip-target-content",
                          "Skip unexpectedly modified the conflicting target.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("skip-file.txt")), Is.EqualTo("skip-source-content"));
    }

    [Test]
    public void Copy_skip_all_keeps_the_existing_conflicting_tree()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "skip-all-tree", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(174, TimeSpan.FromSeconds(3), 174); // Semantic checks expose an unavailable Skip All choice.

        WaitForFileSystem(() => Directory.Exists(Workspace.TargetPath("skip-all-tree")), "Skip All removed the existing target directory.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("skip-all-tree\\nested\\first.txt")), Is.EqualTo("skip-all-first-target"));
            Assert.That(File.ReadAllText(Workspace.TargetPath("skip-all-tree\\nested\\second.txt")), Is.EqualTo("skip-all-second-target"));
            Assert.That(File.Exists(Workspace.SourcePath("skip-all-tree\\nested\\first.txt")), Is.True);
        });
    }

    [Test]
    public void Copy_file_persists_a_completed_recovery_journal_with_item_intent()
    {
        var source = Workspace.SourcePath("copy-file.txt");
        var target = Workspace.TargetPath("copy-file.txt");

        ExecuteWithPath(NativeCommands.CopyFiles, "copy-file.txt", Workspace.TargetDirectory, commit: true);

        WaitForFileSystem(() => FindOperationJournalFor(source) is not null,
                          "Copy did not persist a durable operation journal.");
        var journal = FindOperationJournalFor(source)!;
        var content = File.ReadAllText(journal);
        var planItem = content.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries)
            .Single(line => line.StartsWith("PLANITEM|0|copy-file|", StringComparison.Ordinal));
        var correlation = Regex.Match(content, @"CORRELATION\|operation=(?<id>[0-9A-F]{8}-[0-9A-F]{8}-[0-9A-F]{8})",
                                      RegexOptions.CultureInvariant);

        Assert.That(content, Does.Contain($"ITEM|").And.Contain("|copy-file|").And.Contain(source).And.Contain(target));
        // A completed copy must retain one dispatch ID in the plan, item, and attempt records for recovery triage.
        Assert.That(correlation.Success, Is.True, "The journal must persist a command-dispatch correlation ID.");
        Assert.That(content, Does.Contain($"PLAN|1|operation={correlation.Groups["id"].Value}|"));
        Assert.That(content, Does.Contain($"|operation={correlation.Groups["id"].Value}|sequence=0|attempt=1"));
        Assert.That(content, Does.Contain("PLAN|1|operation="));
        Assert.That(planItem, Does.Contain($"source={source}|target={target}"),
                    "The immutable plan snapshot must preserve the generated copy intent before execution.");
        Assert.That(content, Does.Contain("STATE|").And.Contain("|prepared"));
        Assert.That(content, Does.Contain("|committed"));
        Assert.That(content, Does.Contain("OPERATION|completed"));
    }

    [Test]
    public void Copy_directory_copies_all_descendants_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "copy-tree", Workspace.TargetDirectory, commit: true);

        var copiedPayload = Workspace.TargetPath("copy-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(copiedPayload), "Copy did not create the directory descendant.");
        Assert.That(File.ReadAllText(copiedPayload), Is.EqualTo("copy-tree-content"));
        Assert.That(Directory.Exists(Workspace.SourcePath("copy-tree\\nested")), Is.True);
    }

    [Test]
    public void Rename_file_renames_without_changing_content()
    {
        ExecuteWithPath(NativeCommands.RenameFile, "rename-file.txt", "renamed-file.txt", commit: true);

        WaitForFileSystem(() => File.Exists(Workspace.SourcePath("renamed-file.txt")), "Rename did not create the requested file name.");
        Assert.That(File.Exists(Workspace.SourcePath("rename-file.txt")), Is.False);
        Assert.That(File.ReadAllText(Workspace.SourcePath("renamed-file.txt")), Is.EqualTo("rename-file-content"));
    }

    [Test]
    public void Rename_directory_preserves_all_descendants()
    {
        ExecuteWithPath(NativeCommands.RenameFile, "rename-tree", "renamed-tree", commit: true);

        var payload = Workspace.SourcePath("renamed-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(payload), "Rename did not preserve the directory descendant.");
        Assert.That(Directory.Exists(Workspace.SourcePath("rename-tree")), Is.False);
        Assert.That(File.ReadAllText(payload), Is.EqualTo("rename-tree-content"));
    }

    [Test]
    public void Rename_case_only_change_preserves_the_file_and_updates_its_displayed_name()
    {
        // The native rename path treats a case-only target as the same identity rather than an overwrite collision.
        ExecuteWithPath(NativeCommands.RenameFile, "rename-case.txt", "RENAME-CASE.txt", commit: true);

        WaitForFileSystem(() => Directory.EnumerateFiles(Workspace.SourceDirectory)
                                      .Any(path => Path.GetFileName(path) == "RENAME-CASE.txt"),
                          "Case-only rename did not persist the requested directory-entry casing.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("RENAME-CASE.txt")), Is.EqualTo("rename-case-content"));
    }

    [Test]
    public void Rename_overwrite_replaces_the_collision_without_losing_source_metadata()
    {
        var source = Workspace.SourcePath("rename-overwrite.txt");
        var target = Workspace.SourcePath("rename-overwrite-target.txt");
        var expectedTimestamp = new DateTime(2023, 11, 03, 08, 15, 00, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(source, expectedTimestamp);

        // Fail at fixture setup instead of timing out on a prompt when the panel-local collision is missing.
        Assert.Multiple(() =>
        {
            Assert.That(File.Exists(source), Is.True, "The rename source fixture is missing.");
            Assert.That(File.Exists(target), Is.True, "The rename collision fixture is missing from the source panel.");
        });

        ExecuteWithPath(NativeCommands.RenameFile, "rename-overwrite.txt", "rename-overwrite-target.txt", commit: true);
        DismissOptionalPrompt(6, TimeSpan.FromSeconds(3), 6); // IDYES when this profile confirms overwrites.

        WaitForFileSystem(() => File.ReadAllText(target) == "rename-overwrite-source-content",
                          "Confirmed rename overwrite did not replace the collision target.");
        Assert.Multiple(() =>
        {
            Assert.That(File.Exists(source), Is.False);
            Assert.That(File.GetLastWriteTimeUtc(target), Is.EqualTo(expectedTimestamp));
        });
    }

    [Test]
    public void Move_file_moves_content_to_other_panel()
    {
        Assert.That(Path.GetPathRoot(Workspace.SourceDirectory), Is.EqualTo(Path.GetPathRoot(Workspace.TargetDirectory)),
                    "The default move fixture characterizes same-volume behavior.");
        ExecuteWithPath(NativeCommands.MoveFiles, "move-file.txt", Workspace.TargetDirectory, commit: true);

        var target = Workspace.TargetPath("move-file.txt");
        WaitForFileSystem(() => File.Exists(target), "Move did not create the destination file.");
        Assert.That(File.Exists(Workspace.SourcePath("move-file.txt")), Is.False, "Move retained the source file.");
        Assert.That(File.ReadAllText(target), Is.EqualTo("move-file-content"));
    }

    [Test]
    public void Move_directory_moves_all_descendants_to_other_panel()
    {
        ExecuteWithPath(NativeCommands.MoveFiles, "move-tree", Workspace.TargetDirectory, commit: true);

        var payload = Workspace.TargetPath("move-tree\\nested\\payload.txt");
        WaitForFileSystem(() => File.Exists(payload), "Move did not create the directory descendant.");
        Assert.That(Directory.Exists(Workspace.SourcePath("move-tree")), Is.False, "Move retained the source directory.");
        Assert.That(File.ReadAllText(payload), Is.EqualTo("move-tree-content"));
    }

    [Test]
    public void Move_overwrite_replaces_the_existing_target_and_removes_the_source()
    {
        // Move must apply the confirmed overwrite before deleting the source identity.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-overwrite.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(6, TimeSpan.FromSeconds(3), 6); // IDYES when this profile confirms overwrites.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("move-overwrite.txt")) == "move-overwrite-source-content",
                          "Confirmed move overwrite did not replace the target content.");
        Assert.That(File.Exists(Workspace.SourcePath("move-overwrite.txt")), Is.False,
                    "Confirmed move overwrite retained the source file.");
    }

    [Test]
    public void Move_skip_keeps_the_existing_target_and_the_unmoved_source()
    {
        // A skipped move collision must retain both versions because no target commit occurred.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-skip.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(173, TimeSpan.FromSeconds(3), 173); // Semantic checks expose an unavailable Skip choice.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("move-skip.txt")) == "move-skip-target-content",
                          "Skipped move unexpectedly modified the conflicting target.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("move-skip.txt")), Is.EqualTo("move-skip-source-content"));
    }

    [Test]
    public void Move_overwrite_all_replaces_every_conflict_before_removing_the_source_tree()
    {
        // Overwrite All must commit every destination before the move removes the complete source tree.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-overwrite-all-tree", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(185, TimeSpan.FromSeconds(3), 185); // IDB_ALL when conflict choices are enabled.

        var firstTarget = Workspace.TargetPath("move-overwrite-all-tree\\nested\\first.txt");
        var secondTarget = Workspace.TargetPath("move-overwrite-all-tree\\nested\\second.txt");
        WaitForFileSystem(() => File.Exists(firstTarget) && File.Exists(secondTarget) &&
                                File.ReadAllText(firstTarget) == "move-overwrite-all-first-source" &&
                                File.ReadAllText(secondTarget) == "move-overwrite-all-second-source",
                          "Move Overwrite All did not replace every conflicting descendant.");
        WaitForOperationJournalTerminal(Workspace.SourcePath("move-overwrite-all-tree"),
                                        "Move Overwrite All left its durable operation journal incomplete.");
        Assert.That(Directory.Exists(Workspace.SourcePath("move-overwrite-all-tree")), Is.False,
                    "Move Overwrite All retained the fully committed source tree.");
    }

    [Test]
    public void Move_skip_all_retains_conflicting_sources_but_moves_nonconflicting_siblings()
    {
        // Skip All suppresses later conflict prompts without retaining independently committed siblings.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-skip-all-tree", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(174, TimeSpan.FromSeconds(3), 174); // Semantic checks expose an unavailable Skip All choice.

        WaitForFileSystem(() => File.Exists(Workspace.TargetPath("move-skip-all-tree\\nested\\unique.txt")),
                          "Move Skip All did not continue with a nonconflicting sibling.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath("move-skip-all-tree\\nested\\first.txt")),
                        Is.EqualTo("move-skip-all-first-target"));
            Assert.That(File.ReadAllText(Workspace.TargetPath("move-skip-all-tree\\nested\\second.txt")),
                        Is.EqualTo("move-skip-all-second-target"));
            Assert.That(File.Exists(Workspace.SourcePath("move-skip-all-tree\\nested\\first.txt")), Is.True);
            Assert.That(File.Exists(Workspace.SourcePath("move-skip-all-tree\\nested\\second.txt")), Is.True);
            Assert.That(File.Exists(Workspace.SourcePath("move-skip-all-tree\\nested\\unique.txt")), Is.False);
        });
        // The visible result can precede the worker's final directory cleanup and durable terminal transition.
        WaitForOperationJournalTerminal(Workspace.SourcePath("move-skip-all-tree"),
                                        "Move Skip All left its durable operation journal incomplete.");
    }

    [Test]
    public void Delete_file_removes_the_selected_file()
    {
        SelectSourceItem("delete-file.txt");
        NativeCommands.Execute(MainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-file.txt")), "Delete did not remove the selected file.");
    }

    [Test]
    public void Delete_directory_removes_all_descendants()
    {
        SelectSourceItem("delete-tree");
        NativeCommands.Execute(MainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !Directory.Exists(Workspace.SourcePath("delete-tree")), "Delete did not remove the selected directory tree.");
    }

    [Test]
    public void Delete_mixed_selection_removes_the_selected_file_and_directory_tree()
    {
        // Mixed selection verifies that the delete plan retains both file and recursive-directory intents.
        SelectSourceItems("delete-mixed-file.txt", "delete-mixed-tree");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-mixed-file.txt")) &&
                                !Directory.Exists(Workspace.SourcePath("delete-mixed-tree")),
                          "Delete did not remove every item in the mixed selection.");
    }

    [Test]
    [Category("RecycleBin")]
    public void Delete_to_recycle_bin_removes_the_source_and_creates_a_recoverable_shell_item()
    {
        UiTestSettings.RequireRecycleBinTest();
        var source = Workspace.SourcePath("recycle-file.txt");
        var volumeRoot = Path.GetPathRoot(source)!;
        var itemCountBefore = ShellRecycleBin.GetItemCount(volumeRoot);

        SelectSourceItem("recycle-file.txt");
        NativeCommands.Execute(MainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(source), "Recycle-bin delete did not remove the source file.");
        WaitForFileSystem(() => ShellRecycleBin.GetItemCount(volumeRoot) > itemCountBefore,
                          "Delete did not create a recycle-bin item. Ensure the isolated profile uses the default recycle-bin setting.");
    }

    [Test]
    public void Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation()
    {
        var source = Workspace.SourcePath("cancel-conflict.txt");

        ExecuteWithPath(NativeCommands.CopyFiles, "cancel-conflict.txt", Workspace.TargetDirectory, commit: true);
        DismissOptionalPrompt(2, TimeSpan.FromSeconds(3), 2); // Semantic checks expose an unavailable Cancel choice.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("cancel-conflict.txt")) == "cancel-conflict-target-content",
                          "Cancellation unexpectedly changed the target.");
        Assert.That(File.ReadAllText(source), Is.EqualTo("cancel-conflict-source-content"));
        WaitForFileSystem(() =>
        {
            var journal = FindOperationJournalFor(source);
            return journal is not null && TryReadOperationJournal(journal, out var content) &&
                   content.Contains("OPERATION|cancelled", StringComparison.Ordinal);
        },
                          "Cancellation was not recorded in the durable operation journal.");
    }

    [TestCase(NativeCommands.CreateDirectory, "", "cancelled-directory")]
    [TestCase(NativeCommands.CopyFiles, "cancel-copy.txt", "")]
    [TestCase(NativeCommands.MoveFiles, "cancel-move.txt", "")]
    [TestCase(NativeCommands.RenameFile, "cancel-rename.txt", "cancelled-rename.txt")]
    public void Cancelling_operation_dialog_leaves_source_and_target_unchanged(int command, string sourceName, string enteredPath)
    {
        var path = command is NativeCommands.CopyFiles or NativeCommands.MoveFiles ? Workspace.TargetDirectory : enteredPath;
        ExecuteWithPath(command, sourceName, path, commit: false);

        if (sourceName.Length != 0)
            Assert.That(File.Exists(Workspace.SourcePath(sourceName)), Is.True, "Cancellation changed the source item.");
        if (command == NativeCommands.CreateDirectory)
            Assert.That(Directory.Exists(Workspace.SourcePath(enteredPath)), Is.False);
        if (command is NativeCommands.CopyFiles or NativeCommands.MoveFiles)
            Assert.That(File.Exists(Workspace.TargetPath(sourceName)), Is.False, "Cancellation created a target item.");
        if (command == NativeCommands.RenameFile)
            Assert.That(File.Exists(Workspace.SourcePath(enteredPath)), Is.False, "Cancellation renamed the source item.");
    }

    [Test]
    public void Create_directory_failure_keeps_existing_file_intact()
    {
        SubmitInvalidPathAndCancel(NativeCommands.CreateDirectory, string.Empty, "create-collision");

        Assert.That(File.ReadAllText(Workspace.SourcePath("create-collision")), Is.EqualTo("create-collision-content"));
    }

    [TestCase(NativeCommands.CopyFiles, "copy-file.txt")]
    [TestCase(NativeCommands.MoveFiles, "move-file.txt")]
    public void Copy_or_move_failure_does_not_modify_source(int command, string sourceName)
    {
        SubmitInvalidPathAndCancel(command, sourceName, Workspace.TargetPath("blocked-target\\child.txt"));

        Assert.That(File.Exists(Workspace.SourcePath(sourceName)), Is.True);
        Assert.That(File.Exists(Workspace.TargetPath("blocked-target")), Is.True);
    }

    [Test]
    public void Rename_overwrite_decline_keeps_the_original_file_and_existing_target()
    {
        // IDNO is the rename-specific skip path and must return to the rename dialog without changing either file.
        ExecuteWithPath(NativeCommands.RenameFile, "rename-file.txt", "rename-collision.txt", commit: true);
        if (DismissOptionalPrompt(7, TimeSpan.FromSeconds(3), 7)) // IDNO
        {
            var reopenedRenameDialog = WaitForWindow(window =>
                window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
                window.FindFirstDescendant(cf => cf.ByAutomationId("2"))?.AsButton() is not null);
            CloseDialog(reopenedRenameDialog, commit: false);
        }

        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-file.txt")), Is.EqualTo("rename-file-content"));
        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-collision.txt")), Is.EqualTo("collision-content"));
    }

    [Test]
    public void Rename_directory_collision_keeps_both_directory_trees()
    {
        // Directory collisions are rejected rather than offered the file-only overwrite path.
        SubmitInvalidPathAndCancel(NativeCommands.RenameFile, "rename-collision-source", "rename-collision-target");

        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-collision-source\\payload.txt")),
                    Is.EqualTo("rename-collision-source-content"));
        Assert.That(File.ReadAllText(Workspace.SourcePath("rename-collision-target\\payload.txt")),
                    Is.EqualTo("rename-collision-target-content"));
    }

    [Test]
    public void Delete_skip_for_locked_file_keeps_it_and_continues_with_later_items()
    {
        // Handling the worker prompt prevents this case from passing merely because deletion has not completed yet.
        using var handle = Workspace.HoldSourceFileOpen("delete-locked.txt");
        SelectSourceItem("delete-locked.txt");
        NativeCommands.Execute(MainWindowHandle, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        DismissOptionalPrompt(173, TimeSpan.FromSeconds(3), 173); // Semantic checks expose a missing locked-file Skip path.

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-z-after-skip.txt")),
                          "Delete did not continue with later items after skipping the locked file.");
        Assert.That(File.Exists(Workspace.SourcePath("delete-locked.txt")), Is.True,
                    "Skipping a delete error removed the locked source file.");
    }

    private static byte[] CreateLargeStreamContent()
    {
        var content = new byte[(3 * 1024 * 1024) + 17];
        for (var index = 0; index < content.Length; index++)
            content[index] = (byte)(index % 251);

        return content;
    }
}
