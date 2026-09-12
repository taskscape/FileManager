using NUnit.Framework;

namespace FileManager.UiTests;

public sealed class MainToolbarRoutingContractTests
{
    [Test]
    public void Default_middle_toolbar_keeps_the_routed_smoke_commands()
    {
        var root = FindRepositoryRoot();
        var toolbarDefinitions = File.ReadAllText(Path.Combine(root, "src", "toolbar_button_defs.cpp"));
        var configurationDefaults = File.ReadAllText(Path.Combine(root, "src", "dialogs_config_general.cpp"));

        // The UI routing smoke needs this documented default composition; losing a command is a product regression, not a locator change.
        Assert.Multiple(() =>
        {
            Assert.That(configurationDefaults, Does.Contain("DefMiddleToolBar = \"2,3,17,21,22,23,72,26,24,25,27,55,28,29,30,31,32,33\""));
            Assert.That(toolbarDefinitions, Does.Contain("/*TBBE_CREATE_DIR*/"));
            Assert.That(toolbarDefinitions, Does.Contain("/*TBBE_FIND_FILE*/"));
            Assert.That(toolbarDefinitions, Does.Contain("/*TBBE_SWAP_PANELS*/"));
            Assert.That(toolbarDefinitions, Does.Contain("/*TBBE_COPY*/"));
            Assert.That(toolbarDefinitions, Does.Contain("/*TBBE_VIEW*/"));
        });
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(TestContext.CurrentContext.TestDirectory);
        while (current is not null && !Directory.Exists(Path.Combine(current.FullName, "src")))
            current = current.Parent;

        return current?.FullName
            ?? throw new DirectoryNotFoundException("Could not locate the FileManager repository root.");
    }
}
