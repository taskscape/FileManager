using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
public sealed class ApplicationVerifierStartupUiTests : FileManagerUiTestBase
{
    [Test]
    [Category("VerifierStartup")]
    public void Startup_exposes_the_native_main_window_under_the_selected_verifier_layer()
    {
        // Fixture setup owns the launch; this narrow probe keeps verifier failures focused on process startup rather than seven unrelated UI scenarios.
        Assert.That(MainWindow.Properties.ClassName.ValueOrDefault, Is.EqualTo("SalamanderMainWindowVer25"));
    }
}
