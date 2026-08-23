using NUnit.Framework;
using System.Xml.Linq;

namespace FileManager.UiTests;

// This source-level contract keeps the product-major labels synchronized while preserving
// the existing date- and CI-driven build-number construction across native and installer builds.
public sealed class ApplicationVersionContractTests
{
    [Test]
    public void Product_major_is_6_and_build_components_remain_automatic()
    {
        var root = FindRepositoryRoot();
        var versionHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "spl_vers.h"));
        var buildProps = File.ReadAllText(Path.Combine(root, "src", "Directory.Build.props"));
        var installer = File.ReadAllText(Path.Combine(root, "Installer", "setup.iss"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var configuration = File.ReadAllText(Path.Combine(root, "src", "mainwnd_config.cpp"));
        var constants = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var manifestPath = Path.Combine(root, "src", "manifest.xml");
        var manifest = XDocument.Load(manifestPath);
        XNamespace assemblyNamespace = "urn:schemas-microsoft-com:asm.v1";
        var assemblyVersion = manifest.Root?.Element(assemblyNamespace + "assemblyIdentity")?.Attribute("version")?.Value;
        var shellExtension = File.ReadAllText(Path.Combine(root, "src", "shellext", "shellext.rc"));
        var shellRegistration = File.ReadAllText(Path.Combine(root, "src", "shexreg.h"));
        var readme = File.ReadAllText(Path.Combine(root, "README.md"));

        Assert.Multiple(() =>
        {
            Assert.That(versionHeader, Does.Contain("#define VERSINFO_SALAMANDER_MAJOR 6"));
            Assert.That(versionHeader, Does.Contain("VERSINFO_xstr(VERSINFO_SALAMANDER_MAJOR) \".\" VERSINFO_SALAMANDER_BUILDDATE"));
            Assert.That(buildProps, Does.Contain("$([System.DateTime]::Now.ToString('yyyyMMdd'))"));
            Assert.That(buildProps, Does.Contain("VERSINFO_SALAMANDER_BUILDDATE_DYNAMIC=$(SalamanderBuildDate)"));

            Assert.That(installer, Does.Contain("#define MyAppVersion \"6.0\""));
            Assert.That(installer, Does.Contain("AppVersion={#MyAppVersion}.{#BuildNumber}"));
            Assert.That(installer, Does.Contain("OutputBaseFilename=OpenSalamander_{#MyAppVersion}.{#BuildNumber}"));
            Assert.That(workflow, Does.Contain("/DBuildNumber=${{ github.run_number }}"));
            // The Node 24 GitHub Script release action receives the generated tag through its environment.
            Assert.That(workflow, Does.Contain("RELEASE_TAG: 6.0.${{ github.run_number }}"));
            Assert.That(workflow, Does.Contain("OpenSalamander_6.0.${{ github.run_number }}.exe"));

            Assert.That(configuration, Does.Contain("const DWORD THIS_CONFIG_VERSION = 105;"));
            Assert.That(configuration, Does.Contain("\"Software\\\\Open Salamander\\\\6.0\","));
            Assert.That(configuration, Does.Contain("\"Software\\\\Open Salamander\\\\5.0\","));
            Assert.That(constants, Does.Contain("#define SALCFG_ROOTS_COUNT 84"));
            Assert.That(assemblyVersion, Is.EqualTo("6.0.0.0"));
            Assert.That(shellExtension, Does.Contain("#define FILE_SALVER \"6.0\""));
            Assert.That(shellRegistration, Does.Contain("#define SALSHEXT_SHAREDNAMESAPPENDIX \"600\""));
            Assert.That(readme, Does.Contain("OpenSalamander_6.0.{build_number}.exe"));
        });
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(TestContext.CurrentContext.TestDirectory);
        while (current is not null)
        {
            if (Directory.Exists(Path.Combine(current.FullName, ".git")))
            {
                return current.FullName;
            }

            current = current.Parent;
        }

        throw new DirectoryNotFoundException("Could not locate the FileManager repository root.");
    }
}
