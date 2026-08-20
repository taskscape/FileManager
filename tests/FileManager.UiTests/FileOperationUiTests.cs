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
        // The intermediate directory does not exist yet, so the host asks before
        // creating the whole branch. That prompt is MB_OKCANCEL, so its affirmative
        // button is IDOK rather than IDYES.
        AnswerQuestionIfPrompted(1); // IDOK

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

        // Seeded with the rest of the workspace so the panel already lists the item.
        var target = Workspace.TargetPath("ads-copy.txt");
        var large = FileOperationWorkspace.LargeStreamContent;

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

        // Seeded with the rest of the workspace so the panel already lists the item.
        var target = Workspace.TargetPath("ads-overwrite.txt");

        ExecuteWithPath(NativeCommands.CopyFiles, "ads-overwrite.txt", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

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

        // Seeded with the rest of the workspace so the panel already lists the item.
        var source = Workspace.SourcePath("ads-retry.txt");
        var target = Workspace.TargetPath("ads-retry.txt");

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
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("overwrite-file.txt")) == "overwrite-source-content",
                          "Confirmed overwrite did not replace the target content.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("overwrite-file.txt")), Is.EqualTo("overwrite-source-content"));
    }

    [Test]
    public void Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "overwrite-all-tree", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(185), 185); // IDB_ALL

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("overwrite-all-tree\\nested\\first.txt")) == "overwrite-all-first-source" &&
                                File.ReadAllText(Workspace.TargetPath("overwrite-all-tree\\nested\\second.txt")) == "overwrite-all-second-source",
                          "Overwrite All did not reconcile every conflicting descendant.");
    }

    [Test]
    public void Copy_skip_keeps_the_existing_target_and_the_source()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "skip-file.txt", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(173), 173); // IDB_SKIP

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("skip-file.txt")) == "skip-target-content",
                          "Skip unexpectedly modified the conflicting target.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("skip-file.txt")), Is.EqualTo("skip-source-content"));
    }

    [Test]
    public void Copy_skip_all_keeps_the_existing_conflicting_tree()
    {
        ExecuteWithPath(NativeCommands.CopyFiles, "skip-all-tree", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(174), 174); // IDB_SKIPALL

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

        // The journal is appended as the operation runs, and a journal naming the
        // source exists from the planning record onwards. Wait for the completion
        // record so the assertions below see the finished document, not a prefix.
        WaitForFileSystem(() => ReadJournalFor(source).Contains("OPERATION|completed", StringComparison.Ordinal),
                          "Copy did not persist a completed durable operation journal.");
        var content = ReadJournalFor(source);
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
    [Category("Unicode")]
    public void Ansi_round_trippable_unicode_and_long_path_operations_preserve_distinct_entries()
    {
        // Native operation scripts still use the process ANSI code page. Exercise non-ASCII names that round-trip
        // on both Western and Central-European runners; surrogate and decomposed-name support remains a product gap.
        // Distinct ASCII prefixes prevent the legacy quick-search selector from matching the sibling before it reaches the accented suffix.
        const string firstUnicodeName = "first-unicode-\u00e9.txt";
        const string secondUnicodeName = "second-unicode-\u00f6.txt";
        const string renamedName = "renamed-accent-\u00e9.txt";
        const string treeName = "unicode-long-tree";
        const string longSegment = "long-unicode-\u00e9-segment-123456789012345678901234567890";
        const string payloadName = "payload-\u00e9.txt";

        // PATH_MAX_PATH (src/plugins/shared/spl_gen.h) leaves 247 usable characters.
        // Budget the filename as well as its directory so runners with longer sandbox
        // roots exercise a supported deep path instead of provoking the native error dialog.
        const int productPathMaximumLength = 247;
        var longPathBase = Path.Combine(Workspace.TargetDirectory, treeName);
        var remainingPathLength = productPathMaximumLength - longPathBase.Length - payloadName.Length - 1;
        var longSegmentCount = Math.Max(1, remainingPathLength / (longSegment.Length + 1));
        var longRelativePath = string.Join(Path.DirectorySeparatorChar.ToString(),
            Enumerable.Repeat(longSegment, longSegmentCount));

        // File identities keep the two non-ASCII entries distinct independently of display-text comparisons.
        File.WriteAllText(Workspace.SourcePath(firstUnicodeName), "first-unicode-content");
        File.WriteAllText(Workspace.SourcePath(secondUnicodeName), "second-unicode-content");
        var longSource = Workspace.SourcePath(Path.Combine(treeName, longRelativePath, payloadName));
        // Keep the exact source name within the ANSI product boundary before opening the native copy dialog.
        Assert.That(longSource.Length, Is.LessThanOrEqualTo(productPathMaximumLength));
        Directory.CreateDirectory(Path.GetDirectoryName(longSource)!);
        File.WriteAllText(longSource, "long-unicode-content");
        // File IDs prove that native operations do not collapse the distinct non-ASCII entries.
        var firstSourceIdentity = FileIdentity.Capture(Workspace.SourcePath(firstUnicodeName));
        var secondSourceIdentity = FileIdentity.Capture(Workspace.SourcePath(secondUnicodeName));
        var longSourceIdentity = FileIdentity.Capture(longSource);
        Assert.That(firstSourceIdentity, Is.Not.EqualTo(secondSourceIdentity));

        // Select through unique ASCII prefixes because the legacy panel's WM_CHAR quick-search path is itself ANSI.
        SelectSourceItem("first-unicode-");
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);
        // File operations complete asynchronously after their dialog closes; serialize selections across the plug-in-rich release layout.
        WaitForFileSystem(() => File.Exists(Workspace.TargetPath(firstUnicodeName)),
                          "Copy did not preserve the first Unicode entry.");
        SelectSourceItem("second-unicode-");
        ExecuteWithPath(NativeCommands.CopyFiles, string.Empty, Workspace.TargetDirectory, commit: true);
        WaitForFileSystem(() => File.Exists(Workspace.TargetPath(secondUnicodeName)),
                          "Copy did not preserve the second Unicode entry.");
        ExecuteWithPath(NativeCommands.CopyFiles, treeName, Workspace.TargetDirectory, commit: true);
        WaitForFileSystem(() => File.Exists(Workspace.TargetPath(Path.Combine(treeName, longRelativePath, payloadName))),
                          "Copy did not preserve the long-path entry.");
        Assert.Multiple(() =>
        {
            Assert.That(File.ReadAllText(Workspace.TargetPath(firstUnicodeName)), Is.EqualTo("first-unicode-content"));
            Assert.That(File.ReadAllText(Workspace.TargetPath(secondUnicodeName)), Is.EqualTo("second-unicode-content"));
            Assert.That(File.ReadAllText(Workspace.TargetPath(Path.Combine(treeName, longRelativePath, payloadName))),
                        Is.EqualTo("long-unicode-content"));
            Assert.That(FileIdentity.Capture(Workspace.TargetPath(firstUnicodeName)), Is.Not.EqualTo(firstSourceIdentity));
            Assert.That(FileIdentity.Capture(Workspace.TargetPath(secondUnicodeName)), Is.Not.EqualTo(secondSourceIdentity));
            Assert.That(FileIdentity.Capture(Workspace.TargetPath(Path.Combine(treeName, longRelativePath, payloadName))),
                        Is.Not.EqualTo(longSourceIdentity));
        });

        SelectSourceItem("first-unicode-");
        ExecuteWithPath(NativeCommands.RenameFile, string.Empty, renamedName, commit: true);
        WaitForFileSystem(() => File.Exists(Workspace.SourcePath(renamedName)),
                          "Rename did not retain the first Unicode filename.");
        SelectSourceItem("second-unicode-");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath(secondUnicodeName)),
                          "Delete did not remove only the selected Unicode filename.");
        Assert.That(File.Exists(Workspace.SourcePath(renamedName)), Is.True,
                    "Deleting the second name must not remove its renamed counterpart.");
        // The legacy rename operation may commit through copy/replace, so content—not file ID—is its public invariant.
        Assert.That(File.ReadAllText(Workspace.SourcePath(renamedName)), Is.EqualTo("first-unicode-content"));
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
        var expectedTimestamp = new DateTime(2023, 11, 03, 08, 15, 00, DateTimeKind.Utc);
        File.SetLastWriteTimeUtc(source, expectedTimestamp);

        ExecuteWithPath(NativeCommands.RenameFile, "rename-overwrite.txt", "rename-overwrite-target.txt", commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

        var target = Workspace.SourcePath("rename-overwrite-target.txt");
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
        ChooseOperationPrompt(WaitForOperationPrompt(6), 6); // IDYES

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
        ChooseOperationPrompt(WaitForOperationPrompt(173), 173); // IDB_SKIP

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("move-skip.txt")) == "move-skip-target-content",
                          "Skipped move unexpectedly modified the conflicting target.");
        Assert.That(File.ReadAllText(Workspace.SourcePath("move-skip.txt")), Is.EqualTo("move-skip-source-content"));
    }

    [Test]
    public void Move_overwrite_all_replaces_every_conflict_before_removing_the_source_tree()
    {
        // Overwrite All must commit every destination before the move removes the complete source tree.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-overwrite-all-tree", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(185), 185); // IDB_ALL
        // The metadata-preservation gate is answered below, once per affected item.

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("move-overwrite-all-tree\\nested\\first.txt")) ==
                                    "move-overwrite-all-first-source" &&
                                File.ReadAllText(Workspace.TargetPath("move-overwrite-all-tree\\nested\\second.txt")) ==
                                    "move-overwrite-all-second-source",
                          "Move Overwrite All did not replace every conflicting descendant.");
        // Removing the source is gated behind the metadata-preservation prompt, which
        // reports the timestamps this move cannot carry over and is raised once per
        // affected item. Accepting the loss is what completes the move.
        WaitForFileSystemAnsweringQuestions(() => !Directory.Exists(Workspace.SourcePath("move-overwrite-all-tree")),
                                            6, // IDYES
                                            "Move Overwrite All retained the fully committed source tree.");
    }

    [Test]
    public void Move_skip_all_retains_conflicting_sources_but_moves_nonconflicting_siblings()
    {
        // Skip All suppresses later conflict prompts without retaining independently committed siblings.
        ExecuteWithPath(NativeCommands.MoveFiles, "move-skip-all-tree", Workspace.TargetDirectory, commit: true);
        ChooseOperationPrompt(WaitForOperationPrompt(174), 174); // IDB_SKIPALL

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
    }

    [Test]
    public void Delete_file_removes_the_selected_file()
    {
        SelectSourceItem("delete-file.txt");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-file.txt")), "Delete did not remove the selected file.");
    }

    [Test]
    public void Delete_directory_removes_all_descendants()
    {
        SelectSourceItem("delete-tree");
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
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
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();

        WaitForFileSystem(() => !File.Exists(source), "Recycle-bin delete did not remove the source file.");
        WaitForFileSystem(() => ShellRecycleBin.GetItemCount(volumeRoot) > itemCountBefore,
                          "Delete did not create a recycle-bin item. Ensure the current user uses the default recycle-bin setting.");
    }

    [Test]
    public void Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation()
    {
        var source = Workspace.SourcePath("cancel-conflict.txt");

        ExecuteWithPath(NativeCommands.CopyFiles, "cancel-conflict.txt", Workspace.TargetDirectory, commit: true);
        // The conflict prompt proves the copy is genuinely in progress and waiting.
        var conflict = WaitForOperationPrompt(6);
        // Cancel through the progress window rather than the conflict prompt: only
        // RequestCancellation records OPERATION|cancelled, while declining the
        // conflict is journalled as an ordinary failure.
        CancelThroughProgressWindow();
        // The worker is still parked on the conflict prompt and cannot unwind until it
        // is answered; dismissing it now lets the operation finish as the cancellation
        // it was already asked for.
        NativeCommands.PostDialogButtonClick(conflict.Properties.NativeWindowHandle.Value, 2); // IDCANCEL
        ConfirmCancellationIfPrompted();

        WaitForFileSystem(() => File.ReadAllText(Workspace.TargetPath("cancel-conflict.txt")) == "cancel-conflict-target-content",
                          "Cancellation unexpectedly changed the target.");
        Assert.That(File.ReadAllText(source), Is.EqualTo("cancel-conflict-source-content"));
        // FindJournalFor returns the journal path; the cancellation record lives in its contents.
        WaitForFileSystem(() => ReadJournalFor(source).Contains("OPERATION|cancelled", StringComparison.Ordinal),
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
        ChooseOperationPrompt(WaitForOperationPrompt(7), 7); // IDNO
        var reopenedRenameDialog = WaitForWindow(window =>
            window.Properties.NativeWindowHandle.Value != MainWindow.Properties.NativeWindowHandle.Value &&
            window.FindFirstDescendant(cf => cf.ByAutomationId("2"))?.AsButton() is not null);
        CloseDialog(reopenedRenameDialog, commit: false);

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
        // Retry/Skip/Skip All belongs to the product's own delete engine, and the
        // panel only runs that engine for permanent deletion: with the Recycle
        // Bin enabled it hands the whole selection to the shell, which raises its
        // own "File In Use" window instead. Switch the disposable profile to
        // immediate deletion, which is what Shift+Delete selects interactively.
        var configuration = OpenConfigurationDialog();
        Assert.That(ConfigurationDialogPages.SelectImmediateDeletion(configuration.Properties.NativeWindowHandle.Value),
                    Is.True, "The Configuration dialog did not accept immediate deletion.");
        CloseConfigurationDialog(configuration, commit: true);

        // Handling the worker prompt prevents this case from passing merely because deletion has not completed yet.
        using var handle = Workspace.HoldSourceFileOpen("delete-locked.txt");
        SelectSourceItems("delete-locked.txt", "delete-z-after-skip.txt");
        WaitForCommandEnabled(NativeCommands.DeleteFiles);
        NativeCommands.Execute(MainWindow.Properties.NativeWindowHandle.Value, NativeCommands.DeleteFiles);
        ConfirmDeleteIfPrompted();
        ChooseOperationPrompt(WaitForOperationPrompt(173), 173); // IDB_SKIP

        WaitForFileSystem(() => !File.Exists(Workspace.SourcePath("delete-z-after-skip.txt")),
                          "Delete did not continue with later items after skipping the locked file.");
        Assert.That(File.Exists(Workspace.SourcePath("delete-locked.txt")), Is.True,
                    "Skipping a delete error removed the locked source file.");
    }

    private static string? FindJournalFor(string source)
    {
        // Durable-operation evidence is redirected to the guarded test-data root with the application itself.
        var directory = UiTestSettings.JournalDirectory;
        if (!Directory.Exists(directory))
            return null;

        return Directory.EnumerateFiles(directory, "*.opj")
            .OrderByDescending(File.GetLastWriteTimeUtc)
            .FirstOrDefault(path => ReadJournal(path).Contains(source, StringComparison.Ordinal));
    }

    /// <summary>Contents of the journal naming <paramref name="source"/>, empty when there is none yet.</summary>
    private static string ReadJournalFor(string source)
    {
        var path = FindJournalFor(source);
        return path is null ? string.Empty : ReadJournal(path);
    }

    /// <summary>
    /// Reads a journal the application may still hold open. File.ReadAllText
    /// requests no write sharing, so it raises a sharing violation against a
    /// journal the running operation has not closed yet.
    /// </summary>
    private static string ReadJournal(string path)
    {
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                                              FileShare.ReadWrite | FileShare.Delete);
            using var reader = new StreamReader(stream);
            return reader.ReadToEnd();
        }
        catch (IOException)
        {
            // A journal being rewritten right now simply has nothing to match yet.
            return string.Empty;
        }
    }

}
