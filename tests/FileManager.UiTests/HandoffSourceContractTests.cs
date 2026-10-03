using NUnit.Framework;
using System.Text.RegularExpressions;

namespace FileManager.UiTests;

// Source-level contracts of the Delivery Handoff plug-in (handoff-spec.md T8.4).
// They run without the executable and pin the safety properties that a UI test
// cannot observe: publication without replacement, a single owned-delete path,
// embedded templates, and complete finding strings in both language modules.
public sealed class HandoffSourceContractTests
{
    [Test]
    public void Publication_renames_by_handle_without_replacement()
    {
        var fileSystem = File.ReadAllText(Path.Combine(PluginRoot(), "engine", "file_system.cpp"));
        Assert.That(fileSystem, Does.Contain("RenameRelativePublicationFile"),
                    "The package must be published by handle through the shared no-replace rename.");
    }

    [Test]
    public void Plugin_sources_never_replace_or_shell_copy_files()
    {
        foreach (var path in SourceFiles())
        {
            var text = StripComments(File.ReadAllText(path));
            Assert.That(text, Does.Not.Contain("MOVEFILE_REPLACE_EXISTING"), path);
            Assert.That(Regex.IsMatch(text, @"\bCopyFile(?:A|W|2|Ex|ExA|ExW)?\s*\("), Is.False, $"{path} copies through CopyFile*.");
            Assert.That(Regex.IsMatch(text, @"\bMoveFile(?:A|W|Ex|ExA|ExW)?\s*\("), Is.False, $"{path} moves files.");
        }
    }

    [Test]
    public void Raw_deletions_exist_only_in_the_owned_delete_boundary()
    {
        var allowed = Path.Combine(PluginRoot(), "engine", "file_system.cpp");
        foreach (var path in SourceFiles())
        {
            var text = StripComments(File.ReadAllText(path));
            var deletes = Regex.IsMatch(text, @"\b(?:DeleteFileW?|RemoveDirectoryW?|SHFileOperationW?)\s*\(");
            if (string.Equals(path, allowed, StringComparison.OrdinalIgnoreCase))
            {
                Assert.That(text, Does.Contain("DeleteOwnedFile"), "The owned-delete boundary was renamed or removed.");
                continue;
            }
            Assert.That(deletes, Is.False, $"{path} deletes outside engine/file_system.cpp.");
        }
    }

    [Test]
    public void Every_template_is_embedded_in_the_plugin()
    {
        var resources = File.ReadAllText(Path.Combine(PluginRoot(), "handoff.rc")) +
                        File.ReadAllText(Path.Combine(PluginRoot(), "handoff.rc2"));
        var templates = Directory.GetFiles(Path.Combine(PluginRoot(), "templates"), "*.handoff.json");
        Assert.That(templates, Has.Length.EqualTo(4), "The template set changed; update C.4.14 and the New Specification dialog.");
        foreach (var template in templates)
            Assert.That(resources, Does.Contain("templates\\\\" + Path.GetFileName(template)),
                        $"{Path.GetFileName(template)} is not embedded as RCDATA.");
    }

    [Test]
    public void Every_finding_and_label_has_english_and_polish_text()
    {
        var root = PluginRoot();
        var codes = Regex.Matches(File.ReadAllText(Path.Combine(root, "engine", "catalog.inc")), @"^HO_CODE\((\w+),", RegexOptions.Multiline)
            .Select(match => "IDS_HO_" + match.Groups[1].Value).ToList();
        var labels = Regex.Matches(File.ReadAllText(Path.Combine(root, "engine", "labels.inc")), @"^HO_LABEL\((\w+),", RegexOptions.Multiline)
            .Select(match => "IDS_HL_" + match.Groups[1].Value).ToList();
        Assert.That(codes, Has.Count.GreaterThanOrEqualTo(100), "The finding catalogue was not parsed.");
        var english = StringTableIds(Path.Combine(root, "lang", "lang.rc2"));
        var polish = StringTableIds(Path.Combine(root, "lang", "pl", "lang_pl.rc2"));
        Assert.Multiple(() =>
        {
            foreach (var id in codes.Concat(labels))
            {
                Assert.That(english, Does.Contain(id), $"English text is missing for {id}.");
                Assert.That(polish, Does.Contain(id), $"Polish text is missing for {id}.");
            }
        });
    }

    private static HashSet<string> StringTableIds(string path) =>
        Regex.Matches(File.ReadAllText(path), @"^\s*(IDS_\w+)\s+""", RegexOptions.Multiline).Select(match => match.Groups[1].Value).ToHashSet();

    private static IEnumerable<string> SourceFiles() =>
        Directory.EnumerateFiles(PluginRoot(), "*.*", SearchOption.AllDirectories)
            .Where(path => path.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase) || path.EndsWith(".h", StringComparison.OrdinalIgnoreCase))
            .Where(path => !path.Contains(Path.DirectorySeparatorChar + "vcxproj" + Path.DirectorySeparatorChar));

    // Comments may name forbidden APIs to explain why they are avoided.
    private static string StripComments(string source) =>
        Regex.Replace(source, @"//[^\r\n]*|/\*.*?\*/", string.Empty, RegexOptions.Singleline);

    private static string PluginRoot() => Path.Combine(RepositoryRoot(), "src", "plugins", "handoff");

    private static string RepositoryRoot()
    {
        var current = new DirectoryInfo(TestContext.CurrentContext.TestDirectory);
        while (current is not null && !Directory.Exists(Path.Combine(current.FullName, "src", "plugins", "handoff")))
            current = current.Parent;
        Assert.That(current, Is.Not.Null, "The repository root was not found above the test directory.");
        return current!.FullName;
    }
}
