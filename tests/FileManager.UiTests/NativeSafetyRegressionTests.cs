using NUnit.Framework;
using System.Text.RegularExpressions;

namespace FileManager.UiTests;

// These checks intentionally run without the executable.  A deterministic
// name-swap race cannot be coordinated through the legacy UI, so this keeps
// the native handle-binding contract from being silently removed while the
// executable-level file-operation suite covers normal delete and overwrite
// behavior.
// Refactoring notes remain documentation, not a mutable input to product
// contracts; each assertion below instead names the concrete source boundary.
public sealed class NativeSafetyRegressionTests
{
    [Test]
    public void Root_test_runner_collects_every_documented_automated_test_layer()
    {
        var root = FindRepositoryRoot();
        var runner = File.ReadAllText(Path.Combine(root, "scripts", "runtests.ps1"));
        var releaseWorkflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var releaseInstaller = File.ReadAllText(Path.Combine(root, "tools", "build-release-installer.ps1"));
        var localInstaller = File.ReadAllText(Path.Combine(root, "scripts", "build-installer.ps1"));
        var installerStager = File.ReadAllText(Path.Combine(root, "tools", "prepare_installer.ps1"));
        var quarantineVerifier = File.ReadAllText(Path.Combine(root, "tools", "verify-ui-test-quarantine.ps1"));
        var quarantineWorkflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "quarantined-ui-tests.yml"));
        var liveFtpRunner = File.ReadAllText(Path.Combine(root, "scripts", "run-ftp-test.ps1"));
        var liveFtpTest = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "MojeRzeczyFtpsUiTests.cs"));
        var sandbox = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "UiTestSandbox.cs"));
        var uiTestBase = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "FileManagerUiTestBase.cs"));
        var nightlyWorkflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "nightly-lock-stress.yml"));
        var verifierRunner = File.ReadAllText(Path.Combine(root, "tools", "run-lock-verifier-stress.ps1"));

        // The root command is the advertised entry point, so pin every
        // independently executable probe and the complete NUnit/UI collector.
        Assert.Multiple(() =>
        {
            Assert.That(runner, Does.Contain("verify-operation-completion-protocol.ps1"));
            Assert.That(runner, Does.Contain("verify-durable-copy-commit.ps1"));
            Assert.That(runner, Does.Contain("test-zlib-compatibility.ps1"));
            Assert.That(runner, Does.Contain("test-bzip2-compatibility.ps1"));
            Assert.That(runner, Does.Contain("foreach ($architecture in @('x64', 'x86'))"));
            Assert.That(runner, Does.Contain("test-cmark-gfm-hardening.ps1"));
            Assert.That(runner, Does.Contain("test-sqlite-recovery.ps1"));
            Assert.That(runner, Does.Contain("verify-no-new-terminatethread.ps1"));
            Assert.That(runner, Does.Contain("verify-no-new-raw-thread-creation.ps1"));
            Assert.That(runner, Does.Contain("verify-no-new-gettickcount.ps1"));
            Assert.That(runner, Does.Contain("verify-no-new-max-path-buffers.ps1"));
            Assert.That(runner, Does.Contain("verify-no-new-unsafe-string-calls.ps1"));
            Assert.That(runner, Does.Contain("verify-fluent-icon-coverage.ps1"));
            Assert.That(runner, Does.Contain("Deterministic FTP/FTPS/HTTP fixture tests"));
            Assert.That(runner, Does.Contain("DeterministicNetworkFixtureTests"));
            // Dynamic plug-in SUIDs must never be transferred from a probe process to the instance a UI fixture controls.
            Assert.That(uiTestBase, Does.Contain("WaitForFtpPluginCommand"));
            Assert.That(uiTestBase, Does.Contain("Application.ProcessId"));
            Assert.That(uiTestBase, Does.Contain("PluginCommandMapPath"));
            // A partial test artifact starts FileManager behind a native reporter modal, so keep both the runner staging and fixture preflight contract.
            Assert.That(runner, Does.Contain("Stage-UiTestCrashReporter"));
            Assert.That(uiTestBase, Does.Contain("EnsureCrashReporterIsStaged"));
            Assert.That(runner, Does.Contain("FileManager.UiTests (complete NUnit project)"));
            Assert.That(runner, Does.Contain("run-lock-verifier-stress.ps1"));
            // The opt-in local release mode must cover the packaging job as well as the Debug release gate.
            Assert.That(runner, Does.Contain("[switch]$ReleasePipeline"));
            // Keep local parity from silently collapsing the workflow's staged Debug artifact hand-off into one build.
            Assert.That(runner, Does.Contain("[switch]$PrerequisiteOnly"));
            // A plain local run must keep diagnostic and credentialed external-server tests out of the release-equivalent verdict.
            Assert.That(runner, Does.Contain("[string]$NUnitFilter = 'TestCategory!=Quarantined&TestCategory!=LiveFtp'"));
            Assert.That(runner, Does.Contain("Get-ReleasePipelinePrerequisiteFailures"));
            Assert.That(runner, Does.Contain("Resolve-ReleasePipelineBaseCommit"));
            Assert.That(runner, Does.Contain("Build-ReleaseGateDebugArtifacts"));
            Assert.That(runner, Does.Contain("Resolve-SqliteDll -RequestedPath $SqliteDll"));
            // Local parity must invoke the same complete Build Installer implementation as the separate CI job.
            Assert.That(runner, Does.Contain("build-release-installer.ps1"));
            Assert.That(runner, Does.Contain("Build Installer (x64 Release)"));
            Assert.That(releaseWorkflow, Does.Contain(".\\tools\\build-release-installer.ps1"));
            Assert.That(releaseInstaller, Does.Contain("Configuration=Release"));
            Assert.That(releaseInstaller, Does.Contain("Platform=x64"));
            Assert.That(releaseInstaller, Does.Contain("PlatformToolset=$PlatformToolset"));
            Assert.That(releaseInstaller, Does.Contain("PreferredToolArchitecture=x64"));
            Assert.That(releaseInstaller, Does.Contain("OPENSAL_BUILD_DIR"));
            // Local installer builds must stage this invocation's output rather than an old build_stage tree.
            Assert.That(localInstaller, Does.Contain("$env:OPENSAL_BUILD_DIR"));
            Assert.That(localInstaller, Does.Contain("[IO.Path]::GetFullPath($BuildRoot).TrimEnd('\\') + '\\'"));
            Assert.That(releaseInstaller, Does.Contain("audit-pe-hardening.ps1"));
            Assert.That(releaseInstaller, Does.Contain("new-symbol-index.ps1"));
            Assert.That(releaseInstaller, Does.Contain("prepare_installer.ps1"));
            Assert.That(releaseInstaller, Does.Contain("setup.iss"));
            // Packaging must contain the current Release payload, not a stale matching binary recovered from the source tree.
            Assert.That(installerStager, Does.Contain("salamander\\Release_x64"));
            Assert.That(installerStager, Does.Not.Contain("\"src\""));
            Assert.That(installerStager, Does.Contain("7zwrapper.dll"));
            // Same-major installers need a fresh manifest to register the bundled plug-ins in an existing profile.
            Assert.That(installerStager, Does.Contain("plugins.ver"));
            Assert.That(installerStager, Does.Contain("ToUnixTimeSeconds"));
            Assert.That(runner, Does.Contain("Installer_Staging-runtests-"));
            Assert.That(releaseWorkflow, Does.Contain("Installer_Staging-${{ github.run_id }}"));
            Assert.That(releaseInstaller, Does.Contain("prepare-installer.log"));
            Assert.That(runner, Does.Contain("Retaining failed Release installer build directory"));
            Assert.That(runner, Does.Contain("FailOnSkipped"));
            Assert.That(runner, Does.Contain("@outcome='NotExecuted'"));
            // A host without symlink privilege must skip the complete UI lane rather than falsely report partial coverage as a release failure.
            Assert.That(runner, Does.Contain("$optionalUiIgnoreMessagePrefixes"));
            Assert.That(runner, Does.Contain("SeCreateSymbolicLinkPrivilege"));
            Assert.That(runner, Does.Contain("The complete UI test suite has not been completed"));
            Assert.That(runner, Does.Contain("$uiTestEnvironmentSkipReason = $_.Exception.Message"));
            Assert.That(runner, Does.Contain("NonBlockingSkip:$uiTestEnvironmentSkipIsNonBlocking"));
            Assert.That(runner, Does.Contain("$nonBlockingEnvironmentSkipped"));
            Assert.That(runner, Does.Contain("capability-gated NUnit tests were skipped despite -FailOnSkipped"));
            // Only manifest-backed categories may leave the blocking inventory; unrecognized NUnit Ignore remains a hard failure.
            Assert.That(runner, Does.Contain("verify-ui-test-quarantine.ps1"));
            Assert.That(runner, Does.Contain("NUnitFilter"));
            Assert.That(runner, Does.Contain("unexpectedly ignored"));
            Assert.That(quarantineVerifier, Does.Contain("Quarantine is filterable coverage"));
            Assert.That(quarantineVerifier, Does.Contain("expiresOn"));
            Assert.That(quarantineWorkflow, Does.Contain("TestCategory=Quarantined"));
            Assert.That(quarantineWorkflow, Does.Contain("continue-on-error: true"));
            // The optional external FTPS endpoint stays isolated while missing secrets produce a truthful passed non-execution.
            Assert.That(liveFtpRunner, Does.Contain("MOJERZEC_USERNAME"));
            Assert.That(liveFtpRunner, Does.Contain("MOJERZEC_PASSWORD"));
            Assert.That(liveFtpRunner, Does.Contain("FTP UI tests have not been performed due to missing credentials"));
            Assert.That(liveFtpRunner, Does.Contain("FILEMANAGER_UI_ISOLATED"));
            Assert.That(liveFtpRunner, Does.Contain("FILEMANAGER_UI_FTP_DEBUG_ERROR_LOG_DIRECTORY"));
            Assert.That(liveFtpRunner, Does.Contain("TestCategory=LiveFtp"));
            // The credentialed lane is only useful when it proves the server's documented payload was fully downloaded.
            Assert.That(liveFtpTest, Does.Contain("GetCredentialsOrPass"));
            Assert.That(liveFtpTest, Does.Contain("Assert.Pass(MissingCredentialsMessage)"));
            Assert.That(liveFtpTest, Does.Contain("FTP UI tests have not been performed due to missing credentials"));
            Assert.That(liveFtpTest, Does.Contain("RemoteSkanPath = \"/skan.txt\""));
            Assert.That(liveFtpTest, Does.Contain("FtpDownloadTargetPathControl = 781"));
            Assert.That(liveFtpTest, Does.Contain("FtpAddToQueueControl = 782"));
            Assert.That(liveFtpTest, Does.Contain("DismissWelcomeMessage()"));
            Assert.That(liveFtpTest, Does.Contain("new FileStream(downloadedFile, FileMode.Open, FileAccess.Read, FileShare.None)"));
            Assert.That(liveFtpTest, Does.Contain("stream.Length == expectedSize"));
            // The runner must select explicit data and registry boundaries before driving the current user's desktop.
            Assert.That(runner, Does.Contain("FILEMANAGER_UI_TESTDATA_ROOT"));
            // A disappearing prompt provider is transport noise, not evidence that the native operation omitted its prompt.
            Assert.That(uiTestBase, Does.Contain("ElementNotAvailableException || ex is COMException || ex is TimeoutException"));
            // The release workflow must consume the root inventory rather than
            // maintaining a second list that can silently lose coverage.
            Assert.That(releaseWorkflow, Does.Contain("name: Complete automated release gate"));
            // Validate the workflow command semantically so adding an explicit mode switch cannot invalidate an otherwise compatible release gate.
            Assert.That(releaseWorkflow, Does.Contain(".\\scripts\\runtests.ps1"));
            Assert.That(releaseWorkflow, Does.Contain("-NoReleasePipeline"));
            Assert.That(releaseWorkflow, Does.Contain("-BaseCommit $env:RELEASE_BASE_COMMIT"));
            Assert.That(releaseWorkflow, Does.Contain("-SqliteDll $env:SQLITE_TEST_DLL"));
            Assert.That(releaseWorkflow, Does.Contain("-FailOnSkipped"));
            Assert.That(releaseWorkflow, Does.Contain("-SkipLockVerifier"));
            Assert.That(releaseWorkflow, Does.Contain("-NUnitFilter 'TestCategory!=Quarantined&TestCategory!=LiveFtp'"));
            Assert.That(releaseWorkflow, Does.Contain("needs: release-tests"));
            Assert.That(releaseWorkflow, Does.Contain("fetch-depth: 0"));
            Assert.That(releaseWorkflow, Does.Contain("FILEMANAGER_UI_CONFIG_FAULT_INJECTION: '1'"));
            // CI hosts are provisioned out of band, so the root runner must reject an absent .NET 10 SDK without invoking a privileged installer.
            Assert.That(runner, Does.Contain("Get-Dotnet10SdkCommand"));
            Assert.That(releaseWorkflow, Does.Not.Contain("actions/setup-dotnet"));
            Assert.That(nightlyWorkflow, Does.Not.Contain("actions/setup-dotnet"));
            Assert.That(quarantineWorkflow, Does.Not.Contain("actions/setup-dotnet"));
            Assert.That(releaseWorkflow, Does.Contain("if-no-files-found: warn"));
            // The runner owns capability detection; workflows must not require privileged volume provisioning.
            Assert.That(runner, Does.Contain("Resolve-WritableFixedDDrive"));
            Assert.That(runner, Does.Contain("FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON"));
            Assert.That(runner, Does.Contain("all tests that depend on a second volume have been skipped"));
            Assert.That(releaseWorkflow, Does.Not.Contain("manage-ui-test-volumes.ps1"));
            Assert.That(quarantineWorkflow, Does.Not.Contain("manage-ui-test-volumes.ps1"));
            Assert.That(nightlyWorkflow, Does.Not.Contain("manage-ui-test-volumes.ps1"));
            // Verifier fault injection is diagnostic: it must not block release on an opaque startup timeout.
            Assert.That(releaseWorkflow, Does.Not.Contain("Run Application Verifier lock stress on fresh volumes"));
            Assert.That(nightlyWorkflow, Does.Contain("Run baseline startup probe without Application Verifier"));
            Assert.That(nightlyWorkflow, Does.Contain("Identify the first verifier layer that blocks startup"));
            Assert.That(nightlyWorkflow, Does.Contain("setup-vs2026-buildtools.ps1"));
            Assert.That(verifierRunner, Does.Contain("VerifierLayers"));
            Assert.That(verifierRunner, Does.Contain("verifier-tests.trx"));
            Assert.That(runner, Does.Contain("[switch]$SkipLockVerifier"));
            Assert.That(sandbox, Does.Contain("ClearOwnedRoot"));
            Assert.That(sandbox, Does.Contain("Delete the marker last"));
            Assert.That(releaseWorkflow, Does.Not.Contain("vars.FILEMANAGER_UI"));
        });
    }

    [Test]
    public void Focused_ui_runner_stages_ftp_payloads_and_reparse_setup_treats_only_missing_privilege_as_optional()
    {
        var root = FindRepositoryRoot();
        var focusedRunner = File.ReadAllText(Path.Combine(root, "run-ui-tests.ps1"));
        var uiTestBase = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "FileManagerUiTestBase.cs"));
        var reparseTests = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "ReparsePointTopologyUiTests.cs"));

        // Keep focused local runs from converting missing runtime payloads or host privileges into false application failures.
        Assert.Multiple(() =>
        {
            Assert.That(focusedRunner, Does.Contain("New-FtpUiTestRuntime"));
            Assert.That(focusedRunner, Does.Contain("plugins\\ftp\\ftp.spl"));
            Assert.That(focusedRunner, Does.Contain("plugins\\ftp\\lang\\english.slg"));
            Assert.That(focusedRunner, Does.Contain("$ftpRuntimeRequired"));
            Assert.That(focusedRunner, Does.Contain("Raw Visual Studio output scatters plug-ins by project"));
            Assert.That(focusedRunner, Does.Contain("Remove-Item -LiteralPath $runtimeStagingRoot -Recurse -Force"));
            Assert.That(uiTestBase, Does.Contain("RequireFtpPluginRuntime"));
            Assert.That(uiTestBase, Does.Contain("FTP UI tests require a complete deployed FTP runtime"));
            Assert.That(reparseTests, Does.Contain("ErrorPrivilegeNotHeld = 1314"));
            Assert.That(reparseTests, Does.Contain("exception is IOException && (exception.HResult & 0xFFFF) == ErrorPrivilegeNotHeld"));
            Assert.That(reparseTests, Does.Contain("Assert.Ignore(\"The current test host does not permit disposable directory symbolic links.\")"));
        });
    }

    [Test]
    public void Unchecked_string_calls_are_ratchet_gated_and_external_boundaries_report_capacity_and_encoding_failures()
    {
        var root = FindRepositoryRoot();
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-unsafe-string-calls.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "common", "strutils.cpp"));
        var declarations = File.ReadAllText(Path.Combine(root, "src", "common", "strutils.h"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(ratchet, Does.Contain("git diff --no-ext-diff --unified=0 $BaseCommit HEAD"));
            Assert.That(ratchet, Does.Contain("'*.c' '*.cc' '*.cpp' '*.h' '*.hpp'"));
            Assert.That(ratchet, Does.Contain("(?:strcpy|strcat|sprintf|lstrcpy"));
            Assert.That(ratchet, Does.Contain("exit 1"));
            Assert.That(workflow, Does.Contain("verify-no-new-unsafe-string-calls.ps1 -BaseCommit origin/"));
            Assert.That(declarations, Does.Contain("enum EBoundedStringResult"));
            Assert.That(declarations, Does.Contain("bsrTruncated"));
            Assert.That(declarations, Does.Contain("bsrEncodingError"));
            Assert.That(declarations, Does.Contain("FormatStringChecked"));
            Assert.That(strings, Does.Contain("if (sourceLength >= destinationCapacity)"),
                        "A string exactly filling the payload capacity must leave room for its terminator.");
            Assert.That(strings, Does.Contain("if ((size_t)required >= destinationCapacity)"),
                        "Formatting must reject a one-character overflow before writing a partial value.");
            Assert.That(strings, Does.Contain("WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, -1, NULL, 0"),
                        "UTF-8 capacity is measured after encoding expansion, not from UTF-16 character count.");
            Assert.That(strings, Does.Contain("if ((size_t)required > destinationCapacity)"));
            Assert.That(strings, Does.Contain("ConvertWideToUtf8Checked(src.cFileName"));
            Assert.That(startup, Does.Contain("FormatStringChecked(languageFileName + 1"));
            Assert.That(startup, Does.Not.Contain("sprintf(strrchr(path, '\\\\') + 1"));
        });
    }

    [Test]
    public void New_fixed_max_path_buffers_are_rejected_by_the_changed_lines_ci_ratchet()
    {
        var root = FindRepositoryRoot();
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-max-path-buffers.ps1"));
        var exemptions = File.ReadAllText(Path.Combine(root, "tools", "max-path-buffer-exemptions.md"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var pluginLoading = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(ratchet, Does.Contain("git diff --no-ext-diff --unified=0 $BaseCommit HEAD"));
            Assert.That(ratchet, Does.Contain("'*.c' '*.cc' '*.cpp' '*.h' '*.hpp'"));
            Assert.That(ratchet, Does.Contain("(?:char|WCHAR)"));
            Assert.That(ratchet, Does.Contain("MAX_PATH-RATCHET-EXEMPT"));
            Assert.That(ratchet, Does.Contain("Test-ApprovedExemption"));
            Assert.That(ratchet, Does.Contain("- Reason:"));
            Assert.That(ratchet, Does.Contain("- Removal:"));
            Assert.That(ratchet, Does.Contain("exit 1"));
            Assert.That(exemptions, Does.Contain("No exemptions are currently approved."));
            Assert.That(exemptions, Does.Contain("MAX_PATH-RATCHET-EXEMPT: ID"));
            Assert.That(workflow, Does.Contain("verify-no-new-max-path-buffers.ps1 -BaseCommit origin/"));
        });
    }

    [Test]
    public void Bundled_zlib_is_current_has_retained_compatibility_vectors_and_runs_in_ci()
    {
        var root = FindRepositoryRoot();
        var vendorRecord = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "zlib", "VENDOR.md"));
        var header = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "zlib", "zlib.h"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salamand.vcxproj"));
        var probe = File.ReadAllText(Path.Combine(root, "tools", "zlib_compatibility_probe.c"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));

        // Keep the vendor provenance, hostile streams, and build path coupled to the production C sources.
        Assert.Multiple(() =>
        {
            Assert.That(header, Does.Contain("#define ZLIB_VERSION \"1.3.2\""));
            Assert.That(vendorRecord, Does.Contain("e8bf55f3017aa181690990cb58a994e77885da140609fc8f94abe9b65d2cae28"));
            Assert.That(vendorRecord, Does.Contain("every calendar quarter"));
            Assert.That(vendorRecord, Does.Contain("security advisory or release"));
            Assert.That(project, Does.Contain("..\\common\\dep\\zlib\\infback.c"));
            Assert.That(project, Does.Contain("..\\common\\dep\\zlib\\uncompr.c"));
            Assert.That(probe, Does.Contain("zlib 1.2.11 output"));
            Assert.That(probe, Does.Contain("VerifyRejectedStream"));
            Assert.That(File.Exists(Path.Combine(root, "tests", "zlib-vectors", "legacy-zlib-1.2.11.hex")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "zlib-vectors", "truncated-zlib-stream.hex")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "zlib-vectors", "bad-adler32-zlib-stream.hex")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "zlib-vectors", "invalid-deflate-zlib-stream.hex")), Is.True);
            Assert.That(workflow, Does.Contain("test-zlib-compatibility.ps1"));
        });
    }

    [Test]
    public void Bundled_bzip2_uses_the_verified_release_and_replays_archive_parser_regressions()
    {
        var root = FindRepositoryRoot();
        var vendorRecord = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "bzip2", "VENDOR.md"));
        var header = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "bzip2", "bzlib.h"));
        var decoder = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "bzip2", "decompress.c"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salamand.vcxproj"));
        var adapter = File.ReadAllText(Path.Combine(root, "src", "salbzip2.cpp"));
        var probe = File.ReadAllText(Path.Combine(root, "tools", "bzip2_compatibility_probe.c"));
        var harness = File.ReadAllText(Path.Combine(root, "tools", "test-bzip2-compatibility.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var soakWorkflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "nightly-parser-fuzz.yml"));

        // Pin the vendor identity, streaming adapter, retained corpus, and CI gate as one parser boundary.
        Assert.Multiple(() =>
        {
            Assert.That(header, Does.Contain("bzip2/libbzip2 version 1.0.8 of 13 July 2019"));
            Assert.That(decoder, Does.Contain("if (groupNo >= nSelectors)"));
            Assert.That(vendorRecord, Does.Contain("083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3"));
            Assert.That(vendorRecord, Does.Contain("every calendar quarter"));
            Assert.That(project, Does.Contain("<DisableSpecificWarnings>4244;%(DisableSpecificWarnings)</DisableSpecificWarnings>"));
            Assert.That(adapter, Does.Contain("BZ2_bzDecompress(strm)"));
            Assert.That(adapter, Does.Contain("buffer ownership and result contract"));
            Assert.That(probe, Does.Contain("1.0.8, 13-Jul-2019"));
            Assert.That(probe, Does.Contain("BZ_STREAM_END"));
            Assert.That(File.Exists(Path.Combine(root, "tests", "bzip2-vectors", "golden-stream.hex")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "bzip2-vectors", "legacy-stream.hex")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "bzip2-vectors", "truncated-stream.hex")), Is.True);
            Assert.That(Directory.GetFiles(Path.Combine(root, "tests", "bzip2-vectors", "fuzz"), "*.hex").Length, Is.GreaterThanOrEqualTo(5));
            Assert.That(harness, Does.Contain("/DBZ_NO_STDIO"));
            Assert.That(harness, Does.Contain("Get-ChildItem -LiteralPath (Join-Path $fixtureDirectory 'fuzz')"));
            Assert.That(harness, Does.Contain("[int]$Iterations = 1"));
            Assert.That(workflow, Does.Contain("test-bzip2-compatibility.ps1"));
            Assert.That(soakWorkflow, Does.Contain("test-bzip2-compatibility.ps1 -Architecture x64 -Iterations 250"));
        });
    }

    [Test]
    public void Trust_boundary_text_uses_bounded_owned_storage_and_explicit_capacity_failures()
    {
        var root = FindRepositoryRoot();
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var controlHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon.h"));
        var controlConnection = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon2.cpp"));
        var registry = File.ReadAllText(Path.Combine(root, "src", "regwork.cpp"));
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));
        var salmonHeader = File.ReadAllText(Path.Combine(root, "src", "salmon", "salmon.h"));

        Assert.Multiple(() =>
        {
            Assert.That(upload, Does.Contain("const size_t kMaximumResponseSize = 64 * 1024"));
            // Checked addition rejects both integer wraparound and a response
            // beyond the explicit cap before the owned buffer is extended.
            Assert.That(upload, Does.Contain("!CheckedAddSize(response->size(), availableSize, &responseSize)"));
            Assert.That(upload, Does.Contain("responseSize > kMaximumResponseSize"));
            Assert.That(controlHeader, Does.Contain("CRTLCON_MAXIMUM_REPLY_SIZE (64 * 1024)"));
            Assert.That(controlConnection, Does.Contain("if (newSize > CRTLCON_MAXIMUM_REPLY_SIZE)"));
            Assert.That(controlConnection, Does.Contain("err = WSAEMSGSIZE"));
            Assert.That(registry, Does.Contain("std::string ConfigurationWriteFaultReport"));
            Assert.That(registry, Does.Contain("ConfigurationFaultEnvironmentMaximum = 32767"));
            Assert.That(registry, Does.Contain("GetEnvironmentVariableA(name, NULL, 0)"));
            Assert.That(registry, Does.Contain("return cferTooLarge"));
            // Fault injection must not terminate a plug-in-triggered save before the UI harness reaches its intended commit.
            Assert.That(registry, Does.Contain("FILEMANAGER_CONFIG_FAULT_ARM_FILE"));
            Assert.That(registry, Does.Contain("IsConfigurationWriteFaultArmed()"));
            Assert.That(registry, Does.Contain("FILEMANAGER_CONFIG_FAULT_PHASE"));
            Assert.That(registry, Does.Contain("CreateFileA(ConfigurationWriteFaultReport.c_str()"));
            Assert.That(minidump, Does.Contain("CopyBoundedExternalText"));
            Assert.That(minidump, Does.Contain("memchr(source, 0, sourceCapacity)"));
            Assert.That(minidump, Does.Contain("kMaximumCrashReportPathLength = 32767"));
            // The reporter no longer builds an app-local DbgHelp path; the Windows component is resolved from System32.
            Assert.That(minidump, Does.Contain("LOAD_LIBRARY_SEARCH_SYSTEM32"));
            Assert.That(minidump, Does.Contain("std::string dumpFileName"));
            Assert.That(minidump, Does.Contain("ERROR_INVALID_DATA"));
            Assert.That(minidump, Does.Not.Contain("char szFileName[MAX_PATH]"));
            Assert.That(minidump, Does.Not.Contain("static char findPath[MAX_PATH]"));
            Assert.That(salmonHeader, Does.Contain("int targetPathSize"));
            Assert.That(salmonHeader, Does.Contain("int shortNameSize"));
        });
    }

    [Test]
    public void External_size_fields_use_checked_arithmetic_before_allocation_or_io()
    {
        var root = FindRepositoryRoot();
        var arithmetic = File.ReadAllText(Path.Combine(root, "src", "common", "checked_arithmetic.h"));
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));
        var client = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker", "salbroker.cpp"));
        var pictViewThumbnails = File.ReadAllText(Path.Combine(root, "src", "plugins", "pictview", "thumbs.cpp"));
        var pictViewWicEngine = File.ReadAllText(Path.Combine(root, "src", "plugins", "pictview", "PVWicEngine.cpp"));
        var pictViewDirectory = Path.Combine(root, "src", "plugins", "pictview");
        var zipCommon = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "common.cpp"));
        var zipExtraction = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "extract.cpp"));
        var zipListing = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "list.cpp"));
        var zipAdd = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "add.cpp"));
        var zipAddDelete = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "add_del.cpp"));
        var zipIcons = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "chicon.cpp"));
        var zipSettings = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "dialogs2.cpp"));
        var zipMain = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "main.cpp"));
        var splitCombine = File.ReadAllText(Path.Combine(root, "src", "plugins", "splitcbn", "combine.cpp"));
        var csvParser = File.ReadAllText(Path.Combine(root, "src", "plugins", "dbviewer", "csvlib", "csvlib.cpp"));
        var pluginLoading = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));

        // These assertions pin the overflow boundaries that adversarial file,
        // HTTP, and IPC lengths must traverse before touching a buffer.
        Assert.Multiple(() =>
        {
            Assert.That(arithmetic, Does.Contain("CheckedAddUInt64"));
            Assert.That(arithmetic, Does.Contain("CheckedMultiplyUInt64"));
            Assert.That(arithmetic, Does.Contain("CheckedAddSize"));
            Assert.That(arithmetic, Does.Contain("CheckedMultiplySize"));
            Assert.That(arithmetic, Does.Contain("CheckedCastUInt64ToDword"));
            Assert.That(arithmetic, Does.Contain("CheckedCastSizeToDword"));
            Assert.That(arithmetic, Does.Contain("CheckedCastSizeToInt"));
            Assert.That(upload, Does.Contain("CheckedCastDwordToSize(available, &availableSize)"));
            Assert.That(upload, Does.Contain("CheckedAddSize(response->size(), availableSize, &responseSize)"));
            Assert.That(upload, Does.Contain("CheckedCastSizeToDword(strlen(multipartPrefix), &multipartPrefixLength)"));
            Assert.That(upload, Does.Contain("CheckedCastUInt64ToDword(remaining, &bytesToRead)"));
            Assert.That(upload, Does.Contain("CheckedCastSizeToInt(response.size(), &responseLength)"));
            Assert.That(client, Does.Contain("CheckedMultiplyUInt64((uint64_t)(chars - 1), (uint64_t)sizeof(WCHAR), &pathBytes64)"));
            Assert.That(client, Does.Contain("CheckedAddDword(prefixLength, pathBytes, &totalPayloadLength)"));
            Assert.That(client, Does.Contain("CheckedMultiplyUInt64(thumbnail->Width, thumbnail->Height, &expectedPixelBytes)"));
            Assert.That(client, Does.Contain("CheckedAddDword((DWORD)sizeof(*thumbnail), thumbnail->PixelBytes, &expectedResponseLength)"));
            Assert.That(broker, Does.Contain("CheckedMultiplyDword((DWORD)bitmapInfo.bmWidth, (DWORD)bitmapInfo.bmHeight, &pixels)"));
            Assert.That(broker, Does.Contain("CheckedAddDword((DWORD)sizeof(CParserBrokerThumbnailResponse), pixelBytes, &packedResponseLength)"));
            Assert.That(pictViewThumbnails, Does.Contain("#define WIN_THUMBNAIL_MAX_PAYLOAD (32 * 1024 * 1024)"));
            Assert.That(pictViewThumbnails, Does.Contain("newPos.QuadPart > WIN_THUMBNAIL_MAX_PAYLOAD"));
            // The shared-memory envelope that used to host the 32-bit PictView
            // engine is gone; codec-controlled surface sizes are now bounded
            // inside the in-process WIC engine instead.
            Assert.That(pictViewWicEngine, Does.Contain("CheckedCastUInt64ToSize(total, &allocation)"));
            Assert.That(pictViewWicEngine, Does.Contain("CheckedCastUInt64ToSize(alphaTotal, &alphaAllocation)"));
            Assert.That(pictViewWicEngine, Does.Contain("CheckedCastUInt64ToSize(total, &bufferSize)"));
            Assert.That(File.Exists(Path.Combine(pictViewDirectory, "PVMessageWrapper.cpp")), Is.False);
            Assert.That(File.Exists(Path.Combine(pictViewDirectory, "PVMessageEnvelope.cpp")), Is.False);
            Assert.That(File.Exists(Path.Combine(pictViewDirectory, "Thumbnailer.cpp")), Is.False);
            Assert.That(pictViewThumbnails, Does.Contain("CheckedCastUInt64ToDword(newPos.QuadPart, &thumbnailLength)"));
            Assert.That(pictViewThumbnails, Does.Contain("CheckedAddUInt64((uint64_t)sizeof(HuffmanTbl) + sizeof(Quant75Tbl), newPos.QuadPart, &totalLength64)"));
            Assert.That(pictViewThumbnails, Does.Contain("CheckedCastSizeToInt(allocationLength, &returnedLength)"));
            Assert.That(pictViewThumbnails, Does.Contain("GetFileSizeEx(hFile, &streamSize)"));
            Assert.That(pictViewThumbnails, Does.Not.Contain("SetFilePointer(hFile, 0, NULL, FILE_END)"));
            Assert.That(zipCommon, Does.Contain("BOOL CZipCommon::ProcessLocalHeader"));
            Assert.That(zipCommon, Does.Contain("CheckedAddUInt64(dataOffset, sizeof(CLocalFileHeader), &dataOffset)"));
            Assert.That(zipCommon, Does.Contain("CheckedAddDword(nextOffset, 4, &offset)"));
            Assert.That(zipCommon, Does.Contain("CheckedAddUInt64(CentrDirOffs, CentrDirSize, &endOfCentrDir)"));
            Assert.That(zipCommon, Does.Contain("CheckedAddUInt64(CentrDirOffs, ExtraBytes, &readOffset)"));
            Assert.That(zipCommon, Does.Contain("CheckedAddUInt64(ZipFile->FilePointer, sizeof(CFileHeader), &centralHeaderEnd)"));
            Assert.That(zipExtraction, Does.Contain("if (!ProcessLocalHeader(localHeader, fileInfo, &aesExtraField))"));
            Assert.That(zipExtraction, Does.Contain("Refuse a wrapped central-directory cursor before archive names drive extraction selection."));
            Assert.That(zipExtraction, Does.Contain("CheckedAddUInt64(fileInfo->DataOffset, aesHeaderSize, &aesPayloadOffset)"));
            Assert.That(zipExtraction, Does.Contain("CheckedAddUInt64(fileInfo->DataOffset, ENCRYPT_HEADER_SIZE, &encryptedPayloadOffset)"));
            Assert.That(zipExtraction, Does.Contain("fileInfo->DataOffset = aesPayloadOffset"));
            Assert.That(zipExtraction, Does.Contain("fileInfo->DataOffset = encryptedPayloadOffset"));
            Assert.That(zipExtraction, Does.Contain("int CZipUnpack::GetCompressedPayloadSize"));
            Assert.That(zipExtraction, Does.Contain("if (fileInfo->CompSize >= encryptionOverhead)"));
            Assert.That(zipExtraction, Does.Contain("fileInfo->CompSize - encryptionOverhead"));
            Assert.That(zipExtraction, Does.Contain("GetCompressedPayloadSize(fileInfo, &BytesLeft, errorID)"));
            Assert.That(zipExtraction, Does.Contain("GetCompressedPayloadSize(fileInfo, &bytesLeft, errorID)"));
            Assert.That(zipExtraction, Does.Contain("CheckedAddSize(strlen(fileName), 1, &fileNameBytes)"));
            Assert.That(zipExtraction, Does.Contain("CheckedAddSize((size_t)tempNameLen, 1, &nameAllocationSize)"));
            Assert.That(zipExtraction, Does.Not.Contain("malloc(len + 1)"));
            Assert.That(zipListing, Does.Contain("Do not let a ZIP64 offset wrap into a different central-directory listing position."));
            Assert.That(zipAdd, Does.Contain("CheckedCastUInt64ToDword(archiveEnd, &sfxArchiveSize)"));
            Assert.That(zipAddDelete, Does.Contain("CheckedCastUInt64ToDword(ZipFile->Size, &sfxArchiveSize)"));
            Assert.That(zipIcons, Does.Contain("iconsCount <= 0 || iconsCount > USHRT_MAX"));
            Assert.That(zipIcons, Does.Contain("CheckedMultiplySize((size_t)iconsCount - 1, sizeof(MEMICONDIRENTRY), &entryBytes)"));
            Assert.That(zipIcons, Does.Contain("CheckedCastSizeToDword(size, &resourceSize)"));
            Assert.That(zipIcons, Does.Contain("#define ICO_IMAGE_MAX_BYTES (32 * 1024 * 1024)"));
            Assert.That(zipIcons, Does.Contain("if (w == 0 ||"));
            Assert.That(zipIcons, Does.Contain("CheckedMultiplyDword((DWORD)w, (DWORD)sizeof(ICONDIRENTRY), &directoryReadSize)"));
            Assert.That(zipIcons, Does.Contain("!CheckedAddUInt64(offset, size, &imageEnd)"));
            Assert.That(zipIcons, Does.Contain("sourceDirectorySize <= resourceSize"));
            Assert.That(zipIcons, Does.Contain("CheckedMultiplySize(directory->idCount, sizeof(CIcon), &iconsSize)"));
            Assert.That(zipIcons, Does.Contain("if (s == 0 || s > ICO_IMAGE_MAX_BYTES)"));
            Assert.That(zipCommon, Does.Contain("static BOOL ReadSfxString"));
            Assert.That(zipCommon, Does.Contain("stringSize >= destinationSize"));
            Assert.That(zipCommon, Does.Contain("CheckedAddSize((size_t)stringSize, 1, &allocationSize)"));
            Assert.That(zipCommon, Does.Contain("static BOOL AppendSfxBytes"));
            Assert.That(zipCommon, Does.Contain("CheckedAddDword(stored, byteCount, &required)"));
            Assert.That(zipMain, Does.Contain("maxSerializedSfxSettingsSize = 1024 * 1024"));
            Assert.That(zipMain, Does.Contain("siz <= maxSerializedSfxSettingsSize"));
            Assert.That(zipSettings, Does.Contain("#define SFX_SETTINGS_IMPORT_MAX_BYTES (1024 * 1024)"));
            Assert.That(zipSettings, Does.Contain("CheckedCastUInt64ToSize(file->Size, &importSize)"));
            Assert.That(zipSettings, Does.Contain("CheckedAddSize(importSize, 1, &allocationSize)"));
            Assert.That(zipSettings, Does.Contain("CheckedCastSizeToDword(importSize, &readSize)"));
            Assert.That(splitCombine, Does.Contain("CheckedAddUInt64(totalSize.Value, size.Value, &combinedSize)"));
            Assert.That(splitCombine, Does.Contain("totalSize.Value = combinedSize"));
            Assert.That(csvParser, Does.Contain("CheckedAddSize(textLen, 1, &nameChars)"));
            Assert.That(csvParser, Does.Contain("CheckedMultiplySize(nameChars, sizeof(CChar), &nameBytes)"));
            Assert.That(csvParser, Does.Contain("CheckedAddSize((size_t)BufferSize, 1, &bufferChars)"));
            Assert.That(csvParser, Does.Contain("CheckedCastSizeToInt(*textLen, &sourceTextLen)"));
            Assert.That(csvParser, Does.Contain("CheckedMultiplySize((size_t)len, sizeof(wchar_t), &bufferBytes)"));
        });
    }

    [Test]
    public void Transactional_copy_results_preserve_phase_error_paths_retryability_and_partial_effects_for_legacy_dialogs()
    {
        var root = FindRepositoryRoot();
        var result = File.ReadAllText(Path.Combine(root, "src", "operation_result.h"));
        var copy = ReadOperationImplementationSources(root);
        var combine = File.ReadAllText(Path.Combine(root, "src", "plugins", "splitcbn", "combine.cpp"));

        // Core copy and Split/Combine retain complete outcomes until their adapters feed
        // an unchanged BOOL/error pair to the pre-existing progress-dialog contracts.
        Assert.Multiple(() =>
        {
            Assert.That(result, Does.Contain("enum EOperationResultPhase"));
            Assert.That(result, Does.Contain("orpVerifyDurableCopy"));
            Assert.That(result, Does.Contain("orpVerifyDestinationIdentity"));
            Assert.That(result, Does.Contain("orpCommitTransactionalTarget"));
            Assert.That(result, Does.Contain("DWORD Win32Error"));
            Assert.That(result, Does.Contain("HRESULT HResult"));
            Assert.That(result, Does.Contain("const char* Source"));
            Assert.That(result, Does.Contain("const char* Destination"));
            Assert.That(result, Does.Contain("BOOL Retryable"));
            Assert.That(result, Does.Contain("DWORD PartialEffects"));
            Assert.That(result, Does.Contain("opeTemporaryTargetReady"));
            Assert.That(result, Does.Contain("opeDestinationCommitted"));
            Assert.That(result, Does.Contain("HRESULT_FROM_WIN32(error)"));
            Assert.That(result, Does.Contain("enum EOperationCleanupPhase"));
            Assert.That(result, Does.Contain("COperationCleanupError CleanupErrors[2]"));
            Assert.That(result, Does.Contain("void AppendCleanupError"));
            Assert.That(result, Does.Contain("void BuildDiagnosticSummary"));
            Assert.That(result, Does.Contain("Cleanup is secondary evidence"));
            Assert.That(result, Does.Contain("BOOL ToLegacyBool(DWORD* error) const"));
            // These helpers now cross the split copy implementation boundary, so linkage may change without weakening the result contract.
            Assert.That(copy, Does.Contain("COperationResult CommitTransactionalTargetFile"));
            Assert.That(copy, Does.Contain("COperationResult VerifyDurableCopyCommit"));
            Assert.That(copy, Does.Contain("COperationResult verificationResult = VerifyDurableCopyCommit"));
            Assert.That(copy, Does.Contain("while (!verificationResult.ToLegacyBool(&verificationError))"));
            Assert.That(copy, Does.Contain("COperationResult commitResult = CommitTransactionalTargetFile"));
            Assert.That(copy, Does.Contain("while (!commitResult.ToLegacyBool(&err))"));
            Assert.That(combine, Does.Contain("class CCombineTemporaryOutput"));
            Assert.That(combine, Does.Contain("COperationResult VerifyCombinedOutput"));
            Assert.That(combine, Does.Contain("COperationResult reserveResult = temporaryOutput.Reserve(targetName)"));
            Assert.That(combine, Does.Contain("PromoteFileUtf8Local(temporaryOutput.GetName(), targetName)"));
            Assert.That(combine, Does.Contain("result->AppendCleanupError(orcpDeleteUnverifiedTarget"));
        });
    }

    [Test]
    public void File_operation_failures_capture_the_primary_error_before_cleanup_and_offer_copyable_context()
    {
        var root = FindRepositoryRoot();
        var result = File.ReadAllText(Path.Combine(root, "src", "operation_result.h"));
        var copy = ReadOperationImplementationSources(root);
        var combine = File.ReadAllText(Path.Combine(root, "src", "plugins", "splitcbn", "combine.cpp"));

        // Keep primary failure, cleanup evidence, and copyable diagnostics coupled across core and plug-in output paths.
        Assert.Multiple(() =>
        {
            Assert.That(result, Does.Contain("orcpCloseVerificationHandle"));
            Assert.That(result, Does.Contain("orcpDeleteUnverifiedTarget"));
            Assert.That(result, Does.Contain("CleanupErrorCount < _countof(CleanupErrors)"));
            Assert.That(result, Does.Contain("BuildDiagnosticSummary"));
            Assert.That(copy, Does.Contain("Capture the verification result before CloseHandle can replace GetLastError."));
            Assert.That(copy, Does.Contain("result.AppendCleanupError(orcpCloseVerificationHandle, cleanupError, targetName)"));
            Assert.That(copy, Does.Contain("verificationResult.AppendCleanupError(orcpDeleteUnverifiedTarget, err, op->TargetName)"));
            Assert.That(copy, Does.Contain("Diagnostic (copy with Ctrl+C):"));
            Assert.That(combine, Does.Contain("result->AppendCleanupError(orcpDeleteUnverifiedTarget, GetLastError(), Name)"));
            Assert.That(combine, Does.Contain("The primary combine failure remains actionable"));
        });
    }

    [Test]
    public void Kernel_handle_ownership_is_scoped_and_preserves_legacy_close_failures()
    {
        var root = FindRepositoryRoot();
        var scopedHandle = File.ReadAllText(Path.Combine(root, "src", "common", "scoped_kernel_handle.h"));
        var identity = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));

        // These source checks pin the RAII seam that protects verified delete
        // handles across identity mismatch, mutation failure, and future returns.
        Assert.Multiple(() =>
        {
            Assert.That(scopedHandle, Does.Contain("class CScopedKernelHandle"));
            Assert.That(scopedHandle, Does.Contain("CScopedKernelHandle(const CScopedKernelHandle&)"));
            Assert.That(scopedHandle, Does.Contain("HANDLE Release()"));
            Assert.That(scopedHandle, Does.Contain("void Reset(HANDLE handle = INVALID_HANDLE_VALUE)"));
            Assert.That(scopedHandle, Does.Contain("BOOL Close(DWORD* error)"));
            Assert.That(scopedHandle, Does.Contain("HANDLES(CloseHandle(handle))"));
            Assert.That(scopedHandle, Does.Contain("const DWORD error = GetLastError()"));
            Assert.That(scopedHandle, Does.Contain("SetLastError(error)"));
            Assert.That(identity, Does.Contain("#include \"common/scoped_kernel_handle.h\""));
            Assert.That(identity, Does.Contain("CScopedKernelHandle handle(HANDLES_Q(CreateFileUtf8"));
            Assert.That(identity, Does.Contain("CScopedKernelHandle* handle, DWORD* error"));
            Assert.That(identity, Does.Contain("handle->Reset(HANDLES_Q(CreateFileUtf8"));
            Assert.That(identity, Does.Contain("handle->Get()"));
            Assert.That(identity, Does.Contain("handle.Close(&closeError)"));
            Assert.That(identity, Does.Not.Contain("CloseHandle(handle)"));
        });
    }

    [Test]
    public void Scoped_native_resources_protect_file_operations_and_plugin_boundaries()
    {
        var root = FindRepositoryRoot();
        var resources = File.ReadAllText(Path.Combine(root, "src", "common", "scoped_native_resources.h"));
        var copy = ReadOperationImplementationSources(root);
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var scripts = File.ReadAllText(Path.Combine(root, "src", "plugins", "automation", "scriptlist.cpp"));

        // These source checks pin cleanup at operation and plug-in seams where
        // future returns or callbacks would otherwise bypass a manual pair.
        Assert.Multiple(() =>
        {
            Assert.That(resources, Does.Contain("class CScopedHeapBuffer"));
            Assert.That(resources, Does.Contain("class CScopedMappingView"));
            Assert.That(resources, Does.Contain("class CScopedCriticalSection"));
            Assert.That(resources, Does.Contain("CScopedHeapBuffer(const CScopedHeapBuffer&)"));
            Assert.That(resources, Does.Contain("CScopedMappingView(const CScopedMappingView&)"));
            Assert.That(resources, Does.Contain("CScopedCriticalSection(const CScopedCriticalSection&)"));
            Assert.That(resources, Does.Contain("free(Buffer)"));
            Assert.That(resources, Does.Contain("UnmapViewOfFile(View)"));
            Assert.That(resources, Does.Contain("EnterCriticalSection(CriticalSection)"));
            Assert.That(resources, Does.Contain("LeaveCriticalSection(CriticalSection)"));
            Assert.That(resources, Does.Contain("const DWORD error = GetLastError()"));
            Assert.That(copy, Does.Contain("CScopedHeapBuffer inputBuffer(malloc(ASYNC_COPY_BUF_SIZE))"));
            Assert.That(copy, Does.Contain("CScopedHeapBuffer outputBuffer(malloc(ASYNC_COPY_BUF_SIZE))"));
            Assert.That(copy, Does.Not.Contain("free(bufIn);"));
            Assert.That(copy, Does.Not.Contain("free(bufOut);"));
            Assert.That(broker, Does.Contain("CScopedCriticalSection lock(&Lock, lkrExternalBroker, \"ParserBroker.Lock\")"));
            Assert.That(broker, Does.Not.Contain("LeaveCriticalSection(&Lock);"));
            Assert.That(scripts, Does.Contain("CScopedMappingView codeView(MapViewOfFile"));
            Assert.That(scripts, Does.Not.Contain("UnmapViewOfFile(pszCodeA)"));
        });
    }

    [Test]
    public void Lock_ordering_has_rank_assertions_timeout_diagnostics_and_a_nightly_verifier_lane()
    {
        var root = FindRepositoryRoot();
        var ordering = File.ReadAllText(Path.Combine(root, "src", "common", "lock_ordering.cpp"));
        var orderingHeader = File.ReadAllText(Path.Combine(root, "src", "common", "lock_ordering.h"));
        var scopedResources = File.ReadAllText(Path.Combine(root, "src", "common", "scoped_native_resources.h"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var cache = File.ReadAllText(Path.Combine(root, "src", "cache.cpp"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salamand.vcxproj"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "nightly-lock-stress.yml"));
        var stressRunner = File.ReadAllText(Path.Combine(root, "tools", "run-lock-verifier-stress.ps1"));

        // Pin the wrapper, implementation, build registration, runtime lane, and ledger so the rank scheme cannot become documentation-only.
        Assert.Multiple(() =>
        {
            Assert.That(orderingHeader, Does.Contain("enum CLockRank"));
            Assert.That(orderingHeader, Does.Contain("lkrExternalBroker = 70"));
            Assert.That(orderingHeader, Does.Contain("void LockOrderEnter"));
            Assert.That(scopedResources, Does.Contain("LockOrderEnter(CriticalSection, Rank, LockName)"));
            Assert.That(scopedResources, Does.Contain("LockOrderLeave(CriticalSection, Rank, LockName)"));
            Assert.That(ordering, Does.Contain("_ASSERTE(rank > LockOrderStack[LockOrderStackCount - 1].Rank)"));
            Assert.That(ordering, Does.Contain("IsRecursiveAcquisition(criticalSection, rank)"));
            Assert.That(ordering, Does.Contain("TryEnterCriticalSection(criticalSection)"));
            Assert.That(ordering, Does.Contain("GetTickCount64() - waitStarted >= timeoutMilliseconds"));
            Assert.That(ordering, Does.Contain("LockOrderStack[LockOrderStackCount - 1]"));
            Assert.That(ordering, Does.Contain("EnterCriticalSection(criticalSection)"));
            Assert.That(ordering, Does.Contain("waiter=%lu"));
            Assert.That(ordering, Does.Contain("owner=%lu"));
            Assert.That(ordering, Does.Contain("OutputDebugStringA(message)"));
            Assert.That(broker, Does.Contain("lkrExternalBroker, \"ParserBroker.Lock\""));
            Assert.That(cache, Does.Contain("CScopedCriticalSection lock(&CS, lkrWorkerQueue, \"DeleteManager.Queue\")"));
            Assert.That(cache, Does.Contain("Detach under the rank, then call into the plug-in without holding shared queue state."));
            Assert.That(broker, Does.Contain("CMonotonicClock::DeadlineAfter(timeout)"));
            Assert.That(broker, Does.Contain("RemainingWin32TimerDelay(deadline, CMonotonicClock::Now())"));
            Assert.That(File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon1.cpp")),
                        Does.Contain("const CMonotonicTimePoint resolveDeadline = CMonotonicClock::DeadlineAfter(resolveTimeoutMilliseconds)"));
            Assert.That(broker, Does.Contain("void CParserBrokerClient::StopLocked()"));
            Assert.That(broker, Does.Contain("StopLocked(); // timeout, malformed response, or crash"));
            Assert.That(project, Does.Contain("..\\common\\lock_ordering.cpp"));
            Assert.That(architecture, Does.Contain("### 7.1 Lock ordering contract"));
            Assert.That(workflow, Does.Contain("tools\\run-lock-verifier-stress.ps1"));
            Assert.That(workflow, Does.Contain("self-hosted"));
            // The verifier lane must retain the full diagnostic layers rather than regress to lock-only coverage.
            Assert.That(stressRunner, Does.Contain("@('-enable') + $layers + @('-for', $targetName)"));
            Assert.That(stressRunner, Does.Contain("@('Heaps', 'Handles', 'Locks', 'Exceptions')"));
            // appverif.exe is GUI-subsystem, so the wrapper must explicitly wait for its configuration result.
            Assert.That(stressRunner, Does.Contain("Start-Process -FilePath $AppVerifierPath -ArgumentList $Arguments -Wait -PassThru"));
            Assert.That(stressRunner, Does.Contain("/p /enable $targetName /full"));
            Assert.That(stressRunner, Does.Contain("@('-delete', 'settings', '-for', $targetName)"));
            Assert.That(stressRunner, Does.Contain("TestCategory=LockStress"));
            Assert.That(stressRunner, Does.Contain("finally"));
            Assert.That(stressRunner, Does.Contain("LogOutputDirectory"));
        });
    }

    [Test]
    public void Thread_owners_keep_worker_lifetime_stop_completion_naming_com_and_exception_policy_together()
    {
        var root = FindRepositoryRoot();
        var owner = File.ReadAllText(Path.Combine(root, "src", "common", "thread_owner.h"));
        var checkPath = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var callStack = File.ReadAllText(Path.Combine(root, "src", "callstk.cpp"));
        var automation = File.ReadAllText(Path.Combine(root, "src", "plugins", "automation", "scriptlist.cpp"));
        var renamer = File.ReadAllText(Path.Combine(root, "src", "plugins", "renamer", "regexp.cpp"));
        var oleSpy = File.ReadAllText(Path.Combine(root, "src", "olespy.cpp"));
        var messages = File.ReadAllText(Path.Combine(root, "src", "common", "messages.cpp"));
        var trace = File.ReadAllText(Path.Combine(root, "src", "common", "trace.cpp"));
        var find = File.ReadAllText(Path.Combine(root, "src", "find.cpp"));
        var mapi = File.ReadAllText(Path.Combine(root, "src", "mapi.cpp"));
        var snooper = File.ReadAllText(Path.Combine(root, "src", "snooper.cpp"));
        var auxThreads = File.ReadAllText(Path.Combine(root, "src", "path_utils.cpp"));
        var driveList = File.ReadAllText(Path.Combine(root, "src", "drivelst.cpp"));
        var viewer = File.ReadAllText(Path.Combine(root, "src", "viewer2.cpp"));
        var safeWait = File.ReadAllText(Path.Combine(root, "src", "string_resources.cpp"));
        var packAc = File.ReadAllText(Path.Combine(root, "src", "packac.cpp"));
        var registryWorker = File.ReadAllText(Path.Combine(root, "src", "regwork.cpp"));
        var iconPool = File.ReadAllText(Path.Combine(root, "src", "iconpool.cpp"));
        var cache = File.ReadAllText(Path.Combine(root, "src", "cache.cpp"));
        var findDialog = File.ReadAllText(Path.Combine(root, "src", "find_dialog_ui.cpp"));
        var filesWindow = File.ReadAllText(Path.Combine(root, "src", "fileswindow_init.cpp"));
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var operationDialog = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var dialogsHeader = File.ReadAllText(Path.Combine(root, "src", "dialogs.h"));
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-raw-thread-creation.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var pluginLoading = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));

        // These source contracts preserve the worker boundary where shutdown and
        // completion ordering cannot be deterministically driven through the UI.
        Assert.Multiple(() =>
        {
            Assert.That(owner, Does.Contain("class CThreadOwner"));
            Assert.That(owner, Does.Contain("CThreadOwnerEntry"));
            Assert.That(owner, Does.Contain("_beginthreadex"));
            Assert.That(owner, Does.Contain("StopEvent"));
            Assert.That(owner, Does.Contain("CompletionEvent"));
            Assert.That(owner, Does.Contain("SetThreadOwnerDebuggerName"));
            Assert.That(owner, Does.Contain("CoInitializeEx"));
            Assert.That(owner, Does.Contain("CoUninitialize"));
            Assert.That(owner, Does.Contain("catch (...)"));
            Assert.That(owner, Does.Contain("SetEvent(launch->CompletionEvent)"));
            Assert.That(owner, Does.Contain("StopAndJoin(INFINITE)"));
            Assert.That(owner, Does.Contain("caller keeps parameter alive"));
            Assert.That(checkPath, Does.Contain("CThreadOwner ThreadCheckPath"));
            Assert.That(checkPath, Does.Contain("ThreadCheckPathOwnedF"));
            Assert.That(checkPath, Does.Contain("StopAndJoin(CThreadShutdownDeadline(\"check-path worker\"))"));
            Assert.That(checkPath, Does.Not.Contain("CreateThread("));
            Assert.That(checkPath, Does.Not.Contain("SetThreadNameInVCAndTrace(\"CheckPath\")"));
            // The crash reporter keeps its legacy event protocol, but the owner
            // must retain the handle until that worker has completed its safe join.
            Assert.That(callStack, Does.Contain("CThreadOwner BugReportThreadOwner"));
            Assert.That(callStack, Does.Contain("ThreadBugReportOwnedF"));
            Assert.That(callStack, Does.Contain("StopAndJoin(CThreadShutdownDeadline(\"call-stack bug report\"))"));
            Assert.That(callStack, Does.Not.Contain("CreateThread(ThreadBugReportF"));
            // The automation caller passes stack data, so completion must be joined before this method returns.
            Assert.That(automation, Does.Contain("CPluginThreadOwner executionThread"));
            Assert.That(automation, Does.Contain("NameAutomationExecutionThread"));
            Assert.That(automation, Does.Contain("executionThread.StopAndJoin(INFINITE)"));
            Assert.That(automation, Does.Not.Contain("CreateThread(NULL, 0, ExecuteEntryProc"));
            Assert.That(renamer, Does.Contain("CThreadOwner executionThread"));
            Assert.That(renamer, Does.Contain("StopAndJoin(CThreadShutdownDeadline(\"renamer regular expression\"))"));
            Assert.That(renamer, Does.Not.Contain("CreateThread(NULL, 0, RegExecThread"));
            Assert.That(oleSpy, Does.Contain("CThreadOwner workers[5]"));
            Assert.That(oleSpy, Does.Contain("StopAndJoin(CThreadShutdownDeadline(\"OLE allocator stress\"))"));
            Assert.That(oleSpy, Does.Not.Contain("CreateThread(NULL, 0, OleSpyStressTestF"));
            Assert.That(messages, Does.Contain("MessagesMessageBoxOwnedThreadF"));
            Assert.That(messages, Does.Contain("MessagesWMessageBoxOwnedThreadF"));
            Assert.That(messages, Does.Not.Contain("HANDLE thread = CreateThread"));
            Assert.That(messages, Does.Contain("messageBoxOwner.StopAndJoin(INFINITE)"));
            Assert.That(trace, Does.Contain("TraceMessageBoxOwnedThreadF"));
            Assert.That(trace, Does.Contain("TraceWMessageBoxOwnedThreadF"));
            Assert.That(trace, Does.Not.Contain("HANDLE msgBoxThread = CreateThread"));
            Assert.That(trace, Does.Contain("msgBoxOwner.StopAndJoin(INFINITE)"));
            Assert.That(find, Does.Contain("ThreadFindDialogMessageLoopOwnedF"));
            Assert.That(find, Does.Contain("AddOwnedAuxThread(loop, \"Find dialog message loop\")"));
            Assert.That(find, Does.Not.Contain("CreateThread(NULL, 0, ThreadFindDialogMessageLoop"));
            Assert.That(find, Does.Not.Contain("_endthreadex(ok ? 0 : 1)"));
            Assert.That(mapi, Does.Contain("AddOwnedAuxThread(thread, \"Simple MAPI sender\")"));
            Assert.That(mapi, Does.Not.Contain("CreateThread(NULL, 0, SimpleMAPISendMailThread"));
            Assert.That(snooper, Does.Contain("CThreadOwner* SnooperThreadOwner"));
            Assert.That(snooper, Does.Contain("ThreadSnooperOwned"));
            Assert.That(snooper, Does.Contain("SnooperThreadOwner->StopAndJoin(CThreadShutdownDeadline(\"directory snooper\"))"));
            Assert.That(snooper, Does.Contain("CThreadOwner* SafeFindCloseThreadOwner"));
            Assert.That(snooper, Does.Contain("SafeFindCloseThreadOwner->StopAndJoin(CThreadShutdownDeadline(\"safe notification-handle closer\"))"));
            Assert.That(snooper, Does.Not.Contain("CreateThread(NULL, 0, ThreadSnooper"));
            Assert.That(auxThreads, Does.Contain("void AddOwnedAuxThread(CThreadOwner* owner"));
            Assert.That(auxThreads, Does.Contain("delete auxiliary.Owner"));
            Assert.That(driveList, Does.Contain("thread->Start(ReadCDVolNameThreadF"));
            Assert.That(driveList, Does.Contain("AddOwnedAuxThread(thread, \"removable-drive volume probe\")"));
            Assert.That(driveList, Does.Not.Contain("CreateThread(NULL, 0, ReadCDVolNameThreadF"));
            Assert.That(viewer, Does.Contain("ThreadViewerMessageLoopOwned"));
            Assert.That(viewer, Does.Contain("loop->Start(ThreadViewerMessageLoopOwned, &data, \"viewer message loop\")"));
            Assert.That(viewer, Does.Contain("AddOwnedAuxThread(loop, \"viewer message loop\")"));
            Assert.That(viewer, Does.Not.Contain("CreateThread(NULL, 0, ThreadViewerMessageLoop"));
            Assert.That(safeWait, Does.Contain("ThreadSafeWaitWindowFOwned"));
            Assert.That(safeWait, Does.Contain("thread->Start(ThreadSafeWaitWindowFOwned"));
            Assert.That(safeWait, Does.Contain("AddOwnedAuxThread(thread, \"safe-wait message loop\")"));
            Assert.That(safeWait, Does.Not.Contain("CreateThread(NULL, 0, ThreadSafeWaitWindowF"));
            Assert.That(packAc, Does.Contain("PackACDiskSearchThreadOwned"));
            Assert.That(packAc, Does.Contain("SearchThreadOwner->Start(PackACDiskSearchThreadOwned"));
            Assert.That(packAc, Does.Contain("SearchThreadOwner->StopAndJoin(0)"));
            Assert.That(registryWorker, Does.Contain("ThreadBodyOwned(void* param, HANDLE stopEvent)"));
            Assert.That(registryWorker, Does.Contain("ThreadOwner->Start(CRegistryWorkerThread::ThreadBodyOwned"));
            Assert.That(registryWorker, Does.Contain("ThreadOwner->StopAndJoin(0)"));
            Assert.That(registryWorker, Does.Not.Contain("CreateThread(NULL, 0, CRegistryWorkerThread::ThreadBody"));
            Assert.That(iconPool, Does.Contain("worker->Start(WorkerThreadProc, this, \"icon pool worker\")"));
            Assert.That(iconPool, Does.Contain("Workers[i]->StopAndJoin(CThreadShutdownDeadline(\"icon work pool\"))"));
            Assert.That(iconPool, Does.Not.Contain("CreateThread(NULL, 0, WorkerThreadProc"));
            Assert.That(cache, Does.Contain("ThreadCacheHandlesOwned(void* param, HANDLE stopEvent)"));
            Assert.That(cache, Does.Contain("ThreadOwner->Start(ThreadCacheHandlesOwned, this, \"cache-handles worker\")"));
            Assert.That(cache, Does.Contain("ThreadOwner->StopAndJoin(CThreadShutdownDeadline(\"cache-handles worker\"))"));
            Assert.That(cache, Does.Not.Contain("CreateThread(NULL, 0, ThreadCacheHandles"));
            Assert.That(findDialog, Does.Contain("GrepThreadOwner->Start(GrepThreadF, &GrepData, \"Find grep worker\")"));
            Assert.That(findDialog, Does.Contain("GrepThreadOwner->StopAndJoin(0)"));
            Assert.That(findDialog, Does.Not.Contain("CreateThread(NULL, 0, GrepThreadF"));
            Assert.That(filesWindow, Does.Contain("IconCacheThreadOwner->Start(IconThreadThreadF, this, \"panel icon reader\")"));
            Assert.That(filesWindow, Does.Contain("IconCacheThreadOwner->StopAndJoin(CThreadShutdownDeadline(\"panel icon reader\"))"));
            Assert.That(filesWindow, Does.Not.Contain("CreateThread(NULL, 0, IconThreadThreadF"));
            Assert.That(upload, Does.Contain("UploadThreadOwner->Start(UploadThreadF, params, \"crash-report upload\")"));
            Assert.That(upload, Does.Contain("UploadThreadOwner->StopAndJoin(0)"));
            Assert.That(upload, Does.Not.Contain("CreateThread(NULL, 0, UploadThreadF"));
            Assert.That(minidump, Does.Contain("MinidumpThreadOwner->Start(MinidumpThreadF, params, \"crash-report minidump\")"));
            Assert.That(minidump, Does.Contain("MinidumpThreadOwner->StopAndJoin(0)"));
            Assert.That(minidump, Does.Not.Contain("CreateThread(NULL, 0, MinidumpThreadF"));
            Assert.That(operations, Does.Contain("CThreadOwner* StartWorker"));
            Assert.That(operations, Does.Contain("worker->Start(ThreadWorkerOwned, &data, \"operation worker\")"));
            Assert.That(operations, Does.Not.Contain("CreateThread(NULL, 0, ThreadWorker"));
            Assert.That(dialogsHeader, Does.Contain("CThreadOwner* WorkerOwner"));
            Assert.That(operationDialog, Does.Contain("WorkerOwner->StopAndJoin(0)"));
            Assert.That(ratchet, Does.Contain("git diff --no-ext-diff --unified=0 $BaseCommit HEAD"));
            Assert.That(ratchet, Does.Contain("CThreadOwner"));
            Assert.That(ratchet, Does.Contain("CreateThread|_beginthreadex"));
            Assert.That(workflow, Does.Contain("verify-no-new-raw-thread-creation.ps1 -BaseCommit origin/"));
            Assert.That(pluginLoading, Does.Contain("const int maximumFSNamesPerPlugin = 256"));
            Assert.That(pluginLoading, Does.Contain("StorePluginOutputIndex(newFSNameIndex, i)"));
        });
    }

    [Test]
    public void Crash_report_compression_uses_the_restricted_loader_owned_worker_and_shutdown_contract()
    {
        var root = FindRepositoryRoot();
        var compression = File.ReadAllText(Path.Combine(root, "src", "salmon", "compress.cpp"));
        var compressionHeader = File.ReadAllText(Path.Combine(root, "src", "salmon", "compress.h"));
        var dialog = File.ReadAllText(Path.Combine(root, "src", "salmon", "dialogs.cpp"));

        // This crash-path contract prevents diagnostics from depending on CWD,
        // raw thread handles, or an unbounded worker during dialog teardown.
        Assert.Multiple(() =>
        {
            Assert.That(compression, Does.Contain("CWidePath wideWrapperPath"));
            Assert.That(compression, Does.Contain("LoadLibraryExW(fullPath, NULL, loadFlags)"));
            Assert.That(compression, Does.Contain("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"));
            Assert.That(compression, Does.Contain("LOAD_LIBRARY_SEARCH_SYSTEM32"));
            Assert.That(compression, Does.Contain("LOAD_LIBRARY_SEARCH_USER_DIRS"));
            Assert.That(compression, Does.Contain("CThreadOwner Thread"));
            Assert.That(compression, Does.Contain("CThreadShutdownDeadline(\"crash-report compression\")"));
            Assert.That(compression, Does.Contain("CorrelationId"));
            Assert.That(compression, Does.Contain("GetModuleFileNameOwned"));
            Assert.That(compression, Does.Contain("std::string archive"));
            Assert.That(compression, Does.Not.Contain("HCompressThread"));
            Assert.That(Regex.Matches(compression, @"(?m)^(?!\s*//).*?\b(?:CreateThread|SetCurrentDirectory|strcpy|strcat)\s*\(").Count, Is.Zero);
            Assert.That(compressionHeader, Does.Contain("void StopCompressThread()"));
            Assert.That(dialog, Does.Contain("StopCompressThread();"));
        });
    }

    [Test]
    public void Shared_plugin_thread_queue_uses_owned_cooperative_shutdown_without_forced_termination()
    {
        var root = FindRepositoryRoot();
        var queue = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "auxtools.cpp"));
        var queueHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "auxtools.h"));
        var owner = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "plugin_thread_owner.h"));
        var ftpConsumer = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "fs1.cpp"));
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-raw-thread-creation.ps1"));

        // The shared queue is compiled into many plug-ins, so preserve its API
        // while pinning the cooperative ownership boundary and safe-join order.
        Assert.Multiple(() =>
        {
            Assert.That(queueHeader, Does.Contain("CPluginThreadOwner* Owner"));
            Assert.That(queueHeader, Does.Contain("CThreadQueueStopBody"));
            Assert.That(queueHeader, Does.Contain("CScopedQueueLock"));
            Assert.That(owner, Does.Contain("class CPluginThreadOwner"));
            Assert.That(owner, Does.Contain("StopEvent"));
            Assert.That(owner, Does.Contain("CompletionEvent"));
            Assert.That(owner, Does.Contain("catch (...)"));
            Assert.That(owner, Does.Contain("StopAndJoin(INFINITE)"));
            Assert.That(queue, Does.Contain("CPluginThreadShutdownDeadline"));
            Assert.That(queue, Does.Contain("WaitForSafeJoin(thread)"));
            Assert.That(queue, Does.Contain("GetTickCount64()"));
            Assert.That(queue, Does.Contain("SetEvent(data->Accepted)"));
            Assert.That(queue, Does.Not.Contain("TerminateThread("));
            Assert.That(queue, Does.Not.Contain("Sleep("));
            Assert.That(queue, Does.Not.Contain("CreateThread("));
            Assert.That(ftpConsumer, Does.Contain("CThreadQueue AuxThreadQueue(\"FTP Aux\")"));
            Assert.That(ftpConsumer, Does.Contain("AuxThreadQueue.KillAll(TRUE, 0, 0)"));
            Assert.That(ratchet, Does.Contain("src/plugins/shared/plugin_thread_owner.h"));
        });
    }

    [Test]
    public void Seven_zip_task_dispatch_owns_worker_completion_cancellation_and_progress_subclass_lifetime()
    {
        var root = FindRepositoryRoot();
        var threads = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "7zthreads.cpp"));
        var pluginOwner = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "plugin_thread_owner.h"));

        // Archive callbacks synchronously consult the dialog, so keep their
        // message pump while pinning the owned asynchronous completion boundary.
        Assert.Multiple(() =>
        {
            Assert.That(threads, Does.Contain("CPluginThreadOwner Worker"));
            Assert.That(threads, Does.Contain("WM_7ZIP_TASKCOMPLETE"));
            Assert.That(threads, Does.Contain("PostMessage(operation->ProgressWindow, WM_7ZIP, WM_7ZIP_TASKCOMPLETE"));
            Assert.That(threads, Does.Contain("CProgressDialogSubclassScope"));
            Assert.That(threads, Does.Contain("SetWindowLongPtr(Window, GWLP_WNDPROC, (LONG_PTR)PreviousProcedure)"));
            Assert.That(threads, Does.Contain("RequestCancellation"));
            Assert.That(threads, Does.Contain("SEVEN_ZIP_TASK_CANCEL_DEADLINE"));
            Assert.That(threads, Does.Contain("continuing to pump until its owned completion arrives"));
            Assert.That(threads, Does.Not.Contain("::CreateThread("));
            Assert.That(threads, Does.Not.Contain("MsgWaitForMultipleObjects(1, &hThread, FALSE, INFINITE"));
            Assert.That(pluginOwner, Does.Contain("class CPluginThreadOwner"));
        });
    }

    [Test]
    public void Shutdown_deadlines_report_named_phases_and_preserve_shared_state_until_safe_join()
    {
        var root = FindRepositoryRoot();
        var owner = File.ReadAllText(Path.Combine(root, "src", "common", "thread_owner.h"));
        var checkPath = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var cache = File.ReadAllText(Path.Combine(root, "src", "cache.cpp"));
        var icons = File.ReadAllText(Path.Combine(root, "src", "fileswindow_init.cpp"));
        var snooper = File.ReadAllText(Path.Combine(root, "src", "snooper.cpp"));
        var callStack = File.ReadAllText(Path.Combine(root, "src", "callstk.cpp"));
        var auxiliary = File.ReadAllText(Path.Combine(root, "src", "path_utils.cpp"));
        var appEntry = ReadApplicationLifecycleSources(root);

        // These contracts make stalled shutdown observable without letting a
        // caller release shared state while a legacy worker can still use it.
        Assert.Multiple(() =>
        {
            Assert.That(owner, Does.Contain("class CThreadShutdownDeadline"));
            Assert.That(owner, Does.Contain("DWORD cancellationDeadline = 5000"));
            Assert.That(owner, Does.Contain("DWORD recoveryDeadline = 30000"));
            Assert.That(owner, Does.Contain("TraceDeadlineBreach(\"cancellation\""));
            Assert.That(owner, Does.Contain("TraceDeadlineBreach(\"operation recovery\""));
            Assert.That(owner, Does.Contain("GetExitCodeThread(worker, &exitCode)"));
            Assert.That(owner, Does.Contain("Keeping the process alive for a safe join."));
            Assert.That(owner, Does.Contain("WaitForSingleObject(worker, INFINITE)"));
            Assert.That(owner, Does.Contain("StopAndJoin(const CThreadShutdownDeadline& deadline)"));
            Assert.That(checkPath, Does.Contain("CThreadShutdownDeadline(\"check-path worker\")"));
            Assert.That(cache, Does.Contain("CThreadShutdownDeadline(\"cache-handles worker\")"));
            Assert.That(icons, Does.Contain("CThreadShutdownDeadline(\"panel icon reader\")"));
            Assert.That(snooper, Does.Contain("CThreadShutdownDeadline(\"directory snooper\")"));
            Assert.That(snooper, Does.Contain("CThreadShutdownDeadline(\"safe notification-handle closer\")"));
            Assert.That(callStack, Does.Contain("CThreadShutdownDeadline(\"call-stack bug report\")"));
            Assert.That(auxiliary, Does.Contain("struct CAuxThread"));
            Assert.That(auxiliary, Does.Contain("CThreadShutdownDeadline(auxiliary.Description).WaitForSafeJoin(t)"));
            Assert.That(auxiliary, Does.Not.Contain("TerminateThread(t, 666)"));
            Assert.That(appEntry, Does.Contain("ShutdownAuxThreads()"));
            Assert.That(appEntry, Does.Not.Contain("TerminateAuxThreads()"));
        });
    }

    [Test]
    public void Check_path_workers_use_signaled_work_cancellation_and_deadline_waits()
    {
        var root = FindRepositoryRoot();
        var owner = File.ReadAllText(Path.Combine(root, "src", "common", "thread_owner.h"));
        var checkPath = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var safeWait = File.ReadAllText(Path.Combine(root, "src", "string_resources.cpp"));
        var waitWindow = File.ReadAllText(Path.Combine(root, "src", "dialogs_attributes.cpp"));

        // These source contracts keep cancellation, work completion, and retry
        // deadlines as independently signaled wake reasons rather than timing guesses.
        Assert.Multiple(() =>
        {
            Assert.That(owner, Does.Contain("HANDLE GetCompletionEvent() const"));
            Assert.That(checkPath, Does.Contain("HANDLE waits[] = {CPFirstStart, stopEvent}"));
            Assert.That(checkPath, Does.Contain("WaitForMultipleObjects(2, waits, FALSE, INFINITE)"));
            Assert.That(checkPath, Does.Contain("ThreadCheckPath[0].RequestStop()"));
            Assert.That(checkPath, Does.Contain("WaitForAvailableCheckPathWorker"));
            Assert.That(checkPath, Does.Contain("WaitForMultipleObjects(completionCount, completions, FALSE, INFINITE)"));
            Assert.That(checkPath, Does.Contain("CreateWaitableTimer(NULL, TRUE, NULL)"));
            Assert.That(checkPath, Does.Contain("SetWaitableTimer(deadlineTimer.Get()"));
            Assert.That(checkPath, Does.Contain("cpwrWorkCompleted"));
            Assert.That(checkPath, Does.Contain("cpwrUserCancelled"));
            Assert.That(checkPath, Does.Contain("cpwrDeadlineElapsed"));
            Assert.That(checkPath, Does.Contain("GetSafeWaitWindowCancelEvent()"));
            Assert.That(checkPath, Does.Not.Contain("Sleep("));
            Assert.That(checkPath, Does.Not.Contain("CPFirstTerminate"));
            Assert.That(safeWait, Does.Contain("HANDLE GetSafeWaitWindowCancelEvent()"));
            Assert.That(safeWait, Does.Contain("DuplicateHandle(GetCurrentProcess(), SafeWaitWindowCancelEvent"));
            Assert.That(safeWait, Does.Contain("void SignalSafeWaitWindowCancellation()"));
            Assert.That(waitWindow, Does.Contain("SignalSafeWaitWindowCancellation();"));
        });
    }

    [Test]
    public void Monotonic_64_bit_timers_cross_the_32_bit_boundary_and_reject_backward_samples()
    {
        var root = FindRepositoryRoot();
        var clock = File.ReadAllText(Path.Combine(root, "src", "common", "monotonic_time.h"));
        var pluginTimers = File.ReadAllText(Path.Combine(root, "src", "plugins_filesystem.cpp"));
        var pluginHeader = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var ftpControl = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon1.cpp"));
        var ftpShutdown = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon2.cpp"));
        var ftpCommands = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon3.cpp"));
        var ftpActive = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon5.cpp"));
        var ftpHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon.h"));
        var fileOperationDialogs = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var asyncCopy = ReadOperationImplementationSources(root);
        var find = File.ReadAllText(Path.Combine(root, "src", "find.cpp"));
        var findUi = File.ReadAllText(Path.Combine(root, "src", "find_dialog_ui.cpp"));
        var findHeader = File.ReadAllText(Path.Combine(root, "src", "find.h"));
        var navigation = File.ReadAllText(Path.Combine(root, "src", "fileswindow_navigation.cpp"));
        var configPanels = File.ReadAllText(Path.Combine(root, "src", "dialogs_config_panels.cpp"));
        var dialogsHeader = File.ReadAllText(Path.Combine(root, "src", "dialogs.h"));
        var zipProgress = File.ReadAllText(Path.Combine(root, "src", "zip_progress.cpp"));
        var zipHeader = File.ReadAllText(Path.Combine(root, "src", "zip.h"));
        var appEntry = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var fileCompareCache = File.ReadAllText(Path.Combine(root, "src", "plugins", "filecomp", "filecache.cpp"));
        var snooper = File.ReadAllText(Path.Combine(root, "src", "snooper.cpp"));
        var archiving = File.ReadAllText(Path.Combine(root, "src", "fileswindow_archiving.cpp"));
        var mainWindowPanels = File.ReadAllText(Path.Combine(root, "src", "mainwnd_panels.cpp"));
        var iconReader = File.ReadAllText(Path.Combine(root, "src", "fileswindow_wndproc.cpp"));
        var iconReaderHeader = File.ReadAllText(Path.Combine(root, "src", "fileswnd.h"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var controls = File.ReadAllText(Path.Combine(root, "src", "gui_controls.cpp"));
        var controlsHeader = File.ReadAllText(Path.Combine(root, "src", "gui.h"));
        var progressBar = File.ReadAllText(Path.Combine(root, "src", "gui_progressbar.cpp"));
        var mouseHook = File.ReadAllText(Path.Combine(root, "src", "path_utils.cpp"));
        var fileListRendering = File.ReadAllText(Path.Combine(root, "src", "filesbox_rendering.cpp"));
        var editWindow = File.ReadAllText(Path.Combine(root, "src", "editwnd.cpp"));
        var constants = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var panelHeader = File.ReadAllText(Path.Combine(root, "src", "fileswnd.h"));
        var panelInit = File.ReadAllText(Path.Combine(root, "src", "fileswindow_init.cpp"));
        var toolbarHeader = File.ReadAllText(Path.Combine(root, "src", "toolbar.h"));
        var toolbar = File.ReadAllText(Path.Combine(root, "src", "toolbar_core.cpp"));
        var statusWindow = File.ReadAllText(Path.Combine(root, "src", "stswnd.cpp"));
        var statusWindowHeader = File.ReadAllText(Path.Combine(root, "src", "stswnd.h"));
        var menuPopup = File.ReadAllText(Path.Combine(root, "src", "menu_popup.cpp"));
        var menuHeader = File.ReadAllText(Path.Combine(root, "src", "menu.h"));
        var fileColumns = File.ReadAllText(Path.Combine(root, "src", "fileswindow_columns.cpp"));
        var pack = File.ReadAllText(Path.Combine(root, "src", "pack3.cpp"));
        var timingRatchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-gettickcount.ps1"));

        // These boundary values model the old 49.7-day wrap while the source
        // checks keep the native timer queue on the tested 64-bit seam.
        static ulong Elapsed(ulong start, ulong now) => now >= start ? now - start : 0;

        Assert.Multiple(() =>
        {
            Assert.That(Elapsed(0xFFFF_FFF0UL, 0x1_0000_0020UL), Is.EqualTo(0x30UL),
                        "A deadline crossing the old 32-bit wrap must retain its elapsed duration.");
            Assert.That(Elapsed(500UL, 400UL), Is.Zero,
                        "A synthetic backward sample must not fabricate elapsed time.");
            Assert.That(0x1_0000_0020UL >= 0x1_0000_0010UL, Is.True,
                        "64-bit deadlines remain directly orderable after the former wrap boundary.");
            Assert.That(clock, Does.Contain("typedef ULONGLONG CMonotonicTimePoint"));
            Assert.That(clock, Does.Contain("typedef ULONGLONG CMonotonicDuration"));
            Assert.That(clock, Does.Contain("return GetTickCount64();"));
            Assert.That(clock, Does.Contain("return now >= start ? now - start : 0;"));
            Assert.That(clock, Does.Contain("AtLeastDurationAgo"));
            Assert.That(clock, Does.Contain("RemainingWin32TimerDelay"));
            Assert.That(constants, Does.Contain("extern CMonotonicTimePoint MouseWheelMSGTime"));
            Assert.That(mouseHook, Does.Contain("CMonotonicClock::HasElapsed(MouseWheelMSGTime, MOUSEWHEELMSG_VALID, mouseWheelNow)"));
            Assert.That(fileListRendering, Does.Contain("CMonotonicClock::HasElapsed(MouseWheelMSGTime, MOUSEWHEELMSG_VALID, mouseWheelNow)"));
            Assert.That(editWindow, Does.Contain("CMonotonicClock::HasElapsed(MouseWheelMSGTime, MOUSEWHEELMSG_VALID, mouseWheelNow)"));
            Assert.That(panelHeader, Does.Contain("CMonotonicTimePoint LastIconOvrRefreshTime"));
            Assert.That(panelHeader, Does.Contain("CMonotonicTimePoint NextIconOvrRefreshTime"));
            Assert.That(panelHeader, Does.Contain("CMonotonicTimePoint LastInactiveRefreshStart"));
            Assert.That(panelHeader, Does.Contain("CMonotonicTimePoint LastInactiveRefreshEnd"));
            Assert.That(panelInit, Does.Contain("LastIconOvrRefreshTime = CMonotonicClock::AtLeastDurationAgo(ICONOVR_REFRESH_PERIOD)"));
            Assert.That(archiving, Does.Contain("CMonotonicClock::HasReached(NextIconOvrRefreshTime, overlayNow)"));
            Assert.That(archiving, Does.Contain("CMonotonicClock::Elapsed(LastIconOvrRefreshTime, overlayNow)"));
            Assert.That(iconReader, Does.Contain("CMonotonicClock::Elapsed(LastInactiveRefreshStart, LastInactiveRefreshEnd)"));
            Assert.That(iconReader, Does.Contain("elapsedSinceRefresh < delay - 100"));
            Assert.That(toolbarHeader, Does.Contain("CMonotonicTimePoint DropDownUpTime"));
            Assert.That(toolbar, Does.Contain("CMonotonicClock::HasElapsed(DropDownUpTime, 26"));
            Assert.That(statusWindowHeader, Does.Contain("CMonotonicTimePoint DelayedThrobberShowTime"));
            Assert.That(statusWindow, Does.Contain("CMonotonicClock::DeadlineAfter((CMonotonicDuration)delay)"));
            Assert.That(statusWindow, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(DelayedThrobberShowTime"));
            Assert.That(menuHeader, Does.Contain("CMonotonicTimePoint ChangeTickCount"));
            Assert.That(menuHeader, Does.Contain("MENU_CHANGE_TICK_NONE"));
            Assert.That(menuPopup, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(changeDeadline"));
            Assert.That(menuPopup, Does.Contain("CMonotonicClock::HasReached(changeDeadline"));
            Assert.That(menuPopup, Does.Not.Contain("GetTickCount() - SharedRes->ChangeTickCount"));
            Assert.That(panelHeader, Does.Contain("CMonotonicTimePoint LButtonDownTime"));
            Assert.That(fileColumns, Does.Contain("CMonotonicClock::Elapsed(LButtonDownTime, CMonotonicClock::Now()) > minimumDelay"));
            Assert.That(fileColumns, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(dragDeadline"));
            Assert.That(fileColumns, Does.Not.Contain("GetTickCount() - LButtonDownTime"));
            Assert.That(pack, Does.Contain("CMonotonicClock::DeadlineAfter((CMonotonicDuration)PackWinTimeout)"));
            Assert.That(pack, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(packDeadline"));
            Assert.That(pack, Does.Not.Contain("GetTickCount() - start"));
            Assert.That(dialogsHeader, Does.Contain("CMonotonicTimePoint NextTimeLeftUpdateTime"));
            Assert.That(fileOperationDialogs, Does.Contain("CMonotonicClock::HasReached(NextTimeLeftUpdateTime, ti)"));
            Assert.That(fileOperationDialogs, Does.Contain("nextTimeLeftUpdateDelay"));
            Assert.That(pluginHeader, Does.Contain("CMonotonicTimePoint AbsTimeout"));
            Assert.That(pluginTimers, Does.Contain("CMonotonicClock::DeadlineAfter(relTimeout)"));
            Assert.That(pluginTimers, Does.Contain("CMonotonicClock::HasReached(timer->AbsTimeout, timeNow)"));
            Assert.That(pluginTimers, Does.Not.Contain("GetTickCount("),
                        "The plug-in timer queue must not reintroduce a wrap-prone clock read.");
            Assert.That(pluginTimers, Does.Not.Contain("(int)(timer->AbsTimeout - timeNow)"));
            Assert.That(ftpHeader, Does.Contain("CMonotonicTimePoint StartTime"));
            Assert.That(ftpHeader, Does.Contain("CMonotonicClock::Now()"));
            Assert.That(ftpControl, Does.Contain("CMonotonicClock::Elapsed(StartTime"));
            Assert.That(ftpControl, Does.Contain("const CMonotonicTimePoint waitDeadline"));
            Assert.That(ftpControl, Does.Not.Contain("GetTickCount()"),
                        "The FTP control wait path must not reintroduce a wrap-prone timer read.");
            Assert.That(ftpShutdown, Does.Contain("const CMonotonicTimePoint closeDeadline"));
            Assert.That(ftpShutdown, Does.Not.Contain("GetTickCount()"),
                        "The FTP disconnect wait must preserve one monotonic deadline across replies.");
            Assert.That(ftpCommands, Does.Contain("CMonotonicTimePoint commandDeadline"));
            Assert.That(ftpCommands, Does.Contain("GetWaitWindowElapsed(operationStart)"));
            Assert.That(ftpCommands, Does.Contain("A live data transfer retains the legacy extension policy"));
            Assert.That(ftpCommands, Does.Contain("CMonotonicTimePoint finishDeadline = CMonotonicClock::DeadlineAfter(serverTimeout2)"));
            Assert.That(ftpCommands, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(finishDeadline"));
            Assert.That(ftpCommands, Does.Contain("if (start2 != previousStart)"));
            Assert.That(ftpActive, Does.Contain("const CMonotonicTimePoint listenDeadline = CMonotonicClock::DeadlineAfter(serverTimeout)"));
            Assert.That(ftpActive, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(listenDeadline, CMonotonicClock::Now())"));
            Assert.That(ftpActive, Does.Not.Contain("DWORD startTime = GetTickCount()"));
            Assert.That(fileOperationDialogs, Does.Contain("const CMonotonicTimePoint deadline = CMonotonicClock::DeadlineAfter(PROGRESS_DIALOG_STARTUP_TIMEOUT)"));
            Assert.That(fileOperationDialogs, Does.Contain("RemainingWin32TimerDelay(deadline, CMonotonicClock::Now())"));
            Assert.That(fileOperationDialogs, Does.Not.Contain("DWORD startedAt = GetTickCount()"));
            Assert.That(asyncCopy, Does.Contain("CMonotonicTimePoint startTime = CMonotonicClock::Now()"));
            Assert.That(asyncCopy, Does.Contain("CMonotonicClock::Elapsed(startTime, ti)"));
            Assert.That(findHeader, Does.Contain("volatile LONGLONG FoundVisibleTick"));
            Assert.That(find, Does.Contain("InterlockedCompareExchange64(&data->FoundVisibleTick, 0, 0)"));
            Assert.That(find, Does.Contain("CMonotonicClock::HasElapsed(lastVisible, 500"));
            Assert.That(findUi, Does.Contain("InterlockedExchange64(&GrepData.FoundVisibleTick"));
            Assert.That(findUi, Does.Not.Contain("GrepData.FoundVisibleTick = GetTickCount()"));
            Assert.That(navigation, Does.Contain("CMonotonicTimePoint lastEscCheckTime"));
            Assert.That(navigation, Does.Contain("CMonotonicClock::HasElapsed(lastEscCheckTime, 200"));
            Assert.That(navigation, Does.Not.Contain("GetTickCount() - lastEscCheckTime"));
            Assert.That(dialogsHeader, Does.Contain("ULONGLONG LastTickCount"));
            Assert.That(configPanels, Does.Contain("CMonotonicClock::HasElapsed(LastTickCount, 100, ticks)"));
            Assert.That(configPanels, Does.Not.Contain("ticks - LastTickCount > 100"));
            Assert.That(zipHeader, Does.Contain("ULONGLONG LastTickCount"));
            Assert.That(zipProgress, Does.Contain("CMonotonicClock::HasElapsed(LastTickCount, 100, ticks)"));
            Assert.That(zipProgress, Does.Not.Contain("ticks - LastTickCount > 100"));
            Assert.That(appEntry, Does.Contain("ULONGLONG LastCrtCheckMemoryTime"));
            Assert.That(appEntry, Does.Contain("CMonotonicClock::HasElapsed(LastCrtCheckMemoryTime, 3000"));
            Assert.That(appEntry, Does.Not.Contain("GetTickCount() - LastCrtCheckMemoryTime"));
            Assert.That(fileCompareCache, Does.Contain("CMonotonicClock::HasElapsed(readStartedAt, 200, readFinishedAt)"));
            Assert.That(fileCompareCache, Does.Contain("CMonotonicClock::HasElapsed(initialTicks, 2000, readFinishedAt)"));
            Assert.That(fileCompareCache, Does.Not.Contain("GetTickCount() - initialTicks"));
            Assert.That(snooper, Does.Contain("CMonotonicTimePoint ignoreRefreshesDeadline"));
            Assert.That(snooper, Does.Contain("CMonotonicClock::RemainingWin32TimerDelay(ignoreRefreshesDeadline"));
            Assert.That(snooper, Does.Contain("CMonotonicClock::DeadlineAfter(REFRESH_PAUSE)"));
            Assert.That(snooper, Does.Not.Contain("ignoreRefreshesAbsTimeout - GetTickCount()"));
            Assert.That(archiving, Does.Contain("static CMonotonicTimePoint lastBreakCheck"));
            Assert.That(archiving, Does.Contain("CMonotonicClock::HasElapsed(lastBreakCheck, 200"));
            Assert.That(archiving, Does.Not.Contain("GetTickCount() - lastBreakCheck"));
            Assert.That(mainWindowPanels, Does.Contain("CMonotonicTimePoint readBegTime"));
            Assert.That(mainWindowPanels, Does.Contain("CMonotonicClock::HasElapsed(readBegTime, 200"));
            Assert.That(mainWindowPanels, Does.Contain("CMonotonicClock::Elapsed(readBegTime, CMonotonicClock::Now())"));
            Assert.That(mainWindowPanels, Does.Not.Contain("GetTickCount() - readBegTime"));
            Assert.That(iconReaderHeader, Does.Contain("CMonotonicTimePoint EndOfIconReadingTime"));
            Assert.That(iconReader, Does.Contain("CMonotonicClock::HasElapsed(EndOfIconReadingTime, 1000"));
            Assert.That(iconReader, Does.Not.Contain("GetTickCount() - EndOfIconReadingTime"));
            Assert.That(operations, Does.Contain("CMonotonicTimePoint LastBuildInterruptionCheck"));
            Assert.That(operations, Does.Contain("CMonotonicClock::HasElapsed(LastBuildInterruptionCheck, BS_TIMEOUT"));
            Assert.That(operations, Does.Not.Contain("GetTickCount() - LastTickCount"));
            Assert.That(controlsHeader, Does.Contain("CMonotonicTimePoint DropDownUpTime"));
            Assert.That(controls, Does.Contain("CMonotonicClock::HasElapsed(DropDownUpTime, 26"));
            Assert.That(controls, Does.Not.Contain("GetTickCount() - DropDownUpTime"));
            Assert.That(controlsHeader, Does.Contain("CMonotonicTimePoint SelfMoveTicks"));
            Assert.That(progressBar, Does.Contain("CMonotonicClock::HasElapsed(SelfMoveTicks"));
            Assert.That(progressBar, Does.Not.Contain("GetTickCount() - SelfMoveTicks"));
            Assert.That(timingRatchet, Does.Contain("GetTickCount\\s*\\("));
            Assert.That(timingRatchet, Does.Contain("CMonotonicClock"));
        });
    }

    [Test]
    public void Win32_path_boundaries_use_owned_wide_paths_and_extended_length_syntax()
    {
        var root = FindRepositoryRoot();
        var widePath = File.ReadAllText(Path.Combine(root, "src", "common", "wide_path.h"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "common", "strutils.cpp"));
        var handles = File.ReadAllText(Path.Combine(root, "src", "common", "handles.cpp"));
        var navigation = File.ReadAllText(Path.Combine(root, "src", "fileswindow_navigation.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(widePath, Does.Contain("class CWidePath"));
            Assert.That(widePath, Does.Contain("GetDisplayPath"));
            Assert.That(widePath, Does.Contain("GetPathForWin32Api"));
            Assert.That(widePath, Does.Contain("GetFullPathForWin32Api"));
            Assert.That(widePath, Does.Contain("GetFullPathNameW(DisplayPath, 0, NULL, NULL)"));
            Assert.That(widePath, Does.Contain("extendedUncPrefix"));
            Assert.That(widePath, Does.Contain("PrefixExtendedLengthPathIfNeeded"));
            Assert.That(widePath, Does.Contain("MB_ERR_INVALID_CHARS"));
            Assert.That(widePath, Does.Contain("display spelling separate from the API spelling"));
            Assert.That(strings, Does.Contain("CWidePath fileNameW(fileName)"));
            Assert.That(strings, Does.Contain("GetPathForWin32Api"));
            Assert.That(strings, Does.Not.Contain("CStrStackOrHeap"));
            Assert.That(handles, Does.Contain("CWidePath fileNameW(lpFileName)"));
            Assert.That(handles, Does.Contain("GetFullPathForWin32Api"));
            Assert.That(handles, Does.Not.Contain("Utf8AllocWideHandles"));
            Assert.That(navigation, Does.Contain("CWidePath pathW(path)"));
            Assert.That(navigation, Does.Not.Contain("CStrStackOrHeap"));
        });
    }

    [Test]
    public void Utf16_internal_paths_support_dynamic_wide_path_and_procedural_functions()
    {
        var root = FindRepositoryRoot();
        var widePath = File.ReadAllText(Path.Combine(root, "src", "common", "wide_path.h"));
        var constants = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var pathUtils = ReadPathHistorySources(root);
        var appEntry = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var pathChecking = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));

        var filesWndHeader = File.ReadAllText(Path.Combine(root, "src", "fileswnd.h"));
        var filesWndInit = File.ReadAllText(Path.Combine(root, "src", "fileswindow_init.cpp"));
        var splGen = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "spl_gen.h"));
        var pluginsHeader = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var zipGenApi = File.ReadAllText(Path.Combine(root, "src", "zip_general_api.cpp"));
        var salamandHeader = File.ReadAllText(Path.Combine(root, "src", "salamand.h"));
        var workerHeader = File.ReadAllText(Path.Combine(root, "src", "worker.h"));
        var findHeader = File.ReadAllText(Path.Combine(root, "src", "find.h"));
        var findDlg2 = File.ReadAllText(Path.Combine(root, "src", "finddlg2.cpp"));
        var executeSource = File.ReadAllText(Path.Combine(root, "src", "execute.cpp"));

        Assert.Multiple(() =>
        {
            // CPathW class checks
            Assert.That(widePath, Does.Contain("class CPathW"));
            Assert.That(widePath, Does.Contain("EnsureCapacity"));
            Assert.That(widePath, Does.Contain("BOOL Append(const WCHAR* subPath)"));
            Assert.That(widePath, Does.Contain("BOOL AddBackslash()"));
            Assert.That(widePath, Does.Contain("void RemoveBackslash()"));
            Assert.That(widePath, Does.Contain("void StripPath()"));
            Assert.That(widePath, Does.Contain("void RemoveExtension()"));
            Assert.That(widePath, Does.Contain("BOOL AddExtension(const WCHAR* extension)"));
            Assert.That(widePath, Does.Contain("BOOL RenameExtension(const WCHAR* extension)"));
            Assert.That(widePath, Does.Contain("const WCHAR* FindFileName() const"));
            Assert.That(widePath, Does.Contain("BOOL RemovePoints()"));
            Assert.That(widePath, Does.Contain("char* ToUtf8Alloc() const"));
            Assert.That(widePath, Does.Contain("BOOL ToUtf8("));
            Assert.That(widePath, Does.Contain("BOOL Equals("));

            // Consts header declaration checks
            Assert.That(constants, Does.Contain("BOOL CutDirectoryW(WCHAR* path, WCHAR** cutDir = NULL);"));
            Assert.That(constants, Does.Contain("BOOL SalPathAppendW(WCHAR* path, const WCHAR* name, int pathSizeInChars);"));
            Assert.That(constants, Does.Contain("BOOL SalPathAddBackslashW(WCHAR* path, int pathSizeInChars);"));
            Assert.That(constants, Does.Contain("void SalPathRemoveBackslashW(WCHAR* path);"));
            Assert.That(constants, Does.Contain("void SlashesToBackslashesAndRemoveDupsW(WCHAR* path);"));
            Assert.That(constants, Does.Contain("void SalPathStripPathW(WCHAR* path);"));
            Assert.That(constants, Does.Contain("void SalPathRemoveExtensionW(WCHAR* path);"));
            Assert.That(constants, Does.Contain("BOOL SalPathAddExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars);"));
            Assert.That(constants, Does.Contain("BOOL SalPathRenameExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars);"));
            Assert.That(constants, Does.Contain("const WCHAR* SalPathFindFileNameW(const WCHAR* path);"));
            Assert.That(constants, Does.Contain("int CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2);"));
            Assert.That(constants, Does.Contain("BOOL SalPathIsPrefixW(const WCHAR* prefix, const WCHAR* path);"));
            Assert.That(constants, Does.Contain("BOOL IsTheSamePathW(const WCHAR* path1, const WCHAR* path2);"));
            Assert.That(constants, Does.Contain("int GetRootPathW(WCHAR* root, int rootBufSizeInChars, const WCHAR* path);"));

            // Source implementation checks
            Assert.That(pathUtils, Does.Contain("BOOL SalPathAppendW(WCHAR* path, const WCHAR* name, int pathSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("BOOL SalPathAddBackslashW(WCHAR* path, int pathSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("void SalPathRemoveBackslashW(WCHAR* path)"));
            Assert.That(pathUtils, Does.Contain("void SalPathStripPathW(WCHAR* path)"));
            Assert.That(pathUtils, Does.Contain("void SalPathRemoveExtensionW(WCHAR* path)"));
            Assert.That(pathUtils, Does.Contain("BOOL SalPathAddExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("BOOL SalPathRenameExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("const WCHAR* SalPathFindFileNameW(const WCHAR* path)"));

            Assert.That(appEntry, Does.Contain("int CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2)"));
            Assert.That(appEntry, Does.Contain("BOOL SalPathIsPrefixW(const WCHAR* prefix, const WCHAR* path)"));
            Assert.That(appEntry, Does.Contain("BOOL IsTheSamePathW(const WCHAR* path1, const WCHAR* path2)"));
            Assert.That(appEntry, Does.Contain("int GetRootPathW(WCHAR* root, int rootBufSizeInChars, const WCHAR* path)"));
            Assert.That(appEntry, Does.Contain("BOOL CutDirectoryW(WCHAR* path, WCHAR** cutDir)"));

            Assert.That(pathChecking, Does.Contain("void SlashesToBackslashesAndRemoveDupsW(WCHAR* path)"));
            Assert.That(pathChecking, Does.Contain("BOOL ClearReadOnlyAttrW(const WCHAR* name, DWORD attr)"));

            Assert.That(constants, Does.Contain("BOOL ClearReadOnlyAttrW(const WCHAR* name, DWORD attr = -1);"));
            Assert.That(constants, Does.Contain("void RemoveTemporaryDirW(const WCHAR* dir);"));
            Assert.That(constants, Does.Contain("void RemoveEmptyDirsW(const WCHAR* dir);"));

            Assert.That(pathUtils, Does.Contain("void _RemoveTemporaryDirW(const WCHAR* dir)"));
            Assert.That(pathUtils, Does.Contain("void RemoveTemporaryDirW(const WCHAR* dir)"));
            Assert.That(pathUtils, Does.Contain("void _RemoveEmptyDirsW(const WCHAR* dir)"));
            Assert.That(pathUtils, Does.Contain("void RemoveEmptyDirsW(const WCHAR* dir)"));

            // Panel path and edit control wide helper checks
            Assert.That(filesWndHeader, Does.Contain("CPathW PathW;"));
            Assert.That(filesWndHeader, Does.Contain("const WCHAR* GetPathW()"));
            Assert.That(filesWndHeader, Does.Contain("void SetPathW(const WCHAR* path);"));
            Assert.That(filesWndInit, Does.Contain("void CFilesWindowAncestor::SetPathW(const WCHAR* path)"));

            Assert.That(constants, Does.Contain("BOOL SetEditOrComboTextW(HWND hWnd, const WCHAR* text);"));
            Assert.That(constants, Does.Contain("BOOL GetEditOrComboTextW(HWND hWnd, WCHAR* buf, int bufSizeInChars);"));
            Assert.That(constants, Does.Contain("BOOL GetEditOrComboTextW(HWND hWnd, CPathW& path);"));
            Assert.That(pathUtils, Does.Contain("BOOL SetEditOrComboTextW(HWND hWnd, const WCHAR* text)"));
            Assert.That(pathUtils, Does.Contain("BOOL GetEditOrComboTextW(HWND hWnd, WCHAR* buf, int bufSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("BOOL GetEditOrComboTextW(HWND hWnd, CPathW& path)"));

            // Plugin bridge wide path checks
            Assert.That(splGen, Does.Contain("virtual int WINAPI CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2) = 0;"));
            Assert.That(splGen, Does.Contain("virtual BOOL WINAPI PathIsPrefixW(const WCHAR* prefix, const WCHAR* path) = 0;"));
            Assert.That(splGen, Does.Contain("virtual BOOL WINAPI IsTheSamePathW(const WCHAR* path1, const WCHAR* path2) = 0;"));
            Assert.That(splGen, Does.Contain("virtual BOOL WINAPI CutDirectoryW(WCHAR* path, WCHAR** cutDir = NULL) = 0;"));
            Assert.That(splGen, Does.Contain("virtual BOOL WINAPI SalPathAppendW(WCHAR* path, const WCHAR* name, int pathSizeInChars) = 0;"));

            Assert.That(pluginsHeader, Does.Contain("virtual int WINAPI CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2);"));
            Assert.That(pluginsHeader, Does.Contain("virtual BOOL WINAPI PathIsPrefixW(const WCHAR* prefix, const WCHAR* path);"));
            Assert.That(pluginsHeader, Does.Contain("virtual BOOL WINAPI IsTheSamePathW(const WCHAR* path1, const WCHAR* path2);"));
            Assert.That(pluginsHeader, Does.Contain("virtual BOOL WINAPI CutDirectoryW(WCHAR* path, WCHAR** cutDir = NULL);"));

            Assert.That(zipGenApi, Does.Contain("int CSalamanderGeneral::CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2)"));
            Assert.That(zipGenApi, Does.Contain("BOOL CSalamanderGeneral::PathIsPrefixW(const WCHAR* prefix, const WCHAR* path)"));
            Assert.That(zipGenApi, Does.Contain("BOOL CSalamanderGeneral::IsTheSamePathW(const WCHAR* path1, const WCHAR* path2)"));

            // Path history wide methods check
            Assert.That(salamandHeader, Does.Contain("void GetPathW(WCHAR* buffer, int bufferSizeInChars);"));
            Assert.That(salamandHeader, Does.Contain("void GetPathW(CPathW& path);"));
            Assert.That(salamandHeader, Does.Contain("void AddPathW("));
            Assert.That(salamandHeader, Does.Contain("void AddPathUniqueW("));
            Assert.That(pathUtils, Does.Contain("void CPathHistoryItem::GetPathW(WCHAR* buffer, int bufferSizeInChars)"));
            Assert.That(pathUtils, Does.Contain("void CPathHistory::AddPathW("));
            Assert.That(pathUtils, Does.Contain("void CPathHistory::AddPathUniqueW("));

            // Operations queue wide workpath methods check
            Assert.That(workerHeader, Does.Contain("CPathW WorkPath1W;"));
            Assert.That(workerHeader, Does.Contain("CPathW WorkPath2W;"));
            Assert.That(workerHeader, Does.Contain("void SetWorkPath1W(const WCHAR* pathW, BOOL inclSubDirs)"));
            Assert.That(workerHeader, Does.Contain("void SetWorkPath2W(const WCHAR* pathW, BOOL inclSubDirs)"));
            Assert.That(workerHeader, Does.Contain("const WCHAR* GetSourceNameW(CPathW& pathBuf) const"));
            Assert.That(workerHeader, Does.Contain("const WCHAR* GetTargetNameW(CPathW& pathBuf) const"));

            // Find dialog wide common prefix path checks
            Assert.That(findHeader, Does.Contain("BOOL GetCommonPrefixPathW(WCHAR* buffer, int bufferMaxInChars, int& commonPrefixChars);"));
            Assert.That(findHeader, Does.Contain("BOOL GetCommonPrefixPathW(CPathW& path, int& commonPrefixChars);"));
            Assert.That(findDlg2, Does.Contain("BOOL CFindDialog::GetCommonPrefixPathW(WCHAR* buffer, int bufferMaxInChars, int& commonPrefixChars)"));
            Assert.That(findDlg2, Does.Contain("BOOL CFindDialog::GetCommonPrefixPathW(CPathW& path, int& commonPrefixChars)"));

            // Command expansion wide Win32 API checks
            Assert.That(executeSource, Does.Contain("GetWindowsDirectoryW(pathW, MAX_PATH)"));
            Assert.That(executeSource, Does.Contain("GetSystemDirectoryW(pathW, MAX_PATH)"));
            Assert.That(executeSource, Does.Contain("GetModuleFileNameW(HInstance, pathW, MAX_PATH)"));
            Assert.That(executeSource, Does.Contain("GetShortPathNameW(pathW, shortPathW, MAX_PATH)"));
        });
    }

    [Test]
    public void Destructive_operations_keep_the_handle_identity_guard()
    {
        var root = FindRepositoryRoot();
        var helper = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var copy = ReadOperationImplementationSources(root);

        // The mutation API borrows the RAII owner's handle; it must not receive
        // or retain ownership of the verified delete handle itself.
        Assert.Multiple(() =>
        {
            Assert.That(helper, Does.Contain("FILE_FLAG_OPEN_REPARSE_POINT"));
            Assert.That(helper, Does.Contain("GetFileInformationByHandle"));
            Assert.That(helper, Does.Contain("GetFinalPathNameByHandleW"));
            // Identity verification must not normalize every entry in a large
            // FAT/exFAT directory before the destructive handle is checked.
            Assert.That(helper, Does.Contain("FILE_NAME_OPENED | VOLUME_NAME_NT"));
            Assert.That(helper, Does.Not.Contain("FILE_NAME_NORMALIZED"));
            Assert.That(helper, Does.Contain("OperationExecutionFileSystem().SetFileInformationByHandle(handle.Get(), FileDispositionInfo"));
            Assert.That(operations, Does.Contain("CaptureOperationFileIdentities(op, &identityError)"));
            // Verification reports its failure through the local address before
            // the typed result preserves that code for the legacy dialog path.
            Assert.That(copy, Does.Contain("VerifyFileIdentity(targetName, expectedTargetIdentity, &error)"));
            Assert.That(copy, Does.Contain("DeleteFileWithVerifiedIdentity(name, operation->SourceIdentity, &err)"));
        });
    }

    [Test]
    public void Reparse_point_policy_never_traverses_or_hydrates_unselected_targets()
    {
        var root = FindRepositoryRoot();
        var planner = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var deletion = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var navigation = File.ReadAllText(Path.Combine(root, "src", "fileswindow_navigation.cpp"));
        var topologyTests = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "ReparsePointTopologyUiTests.cs"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));

        Assert.Multiple(() =>
        {
            Assert.That(planner, Does.Contain("REPARSE_POINT_POLICY: Directory reparse points are operation"));
            Assert.That(planner, Does.Contain("skipping directory reparse point without traversal"));
            Assert.That(planner, Does.Contain("containing source directory cannot be removed as part of"));
            Assert.That(planner, Does.Contain("REPARSE_POINT_POLICY: File reparse points are not opened by planning"));
            Assert.That(planner, Does.Contain("skipping file reparse point without hydration"));
            Assert.That(planner, Does.Not.Contain("CConfirmLinkTgtCopyDlg(HWindow, sourcePath"),
                        "The legacy link-content path would follow a target outside the operation root.");
            Assert.That(deletion, Does.Contain("juncData->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT"));
            Assert.That(deletion, Does.Contain("juncData->ReparseTag != IO_REPARSE_TAG_SYMLINK"));
            Assert.That(deletion, Does.Contain("ERROR_REPARSE_TAG_MISMATCH"));
            Assert.That(navigation, Does.Contain("IO_REPARSE_TAG_FILE_PLACEHOLDER"));
            Assert.That(topologyTests, Does.Contain("changed-junction"));
            Assert.That(topologyTests, Does.Contain("cycle-junction"));
            Assert.That(topologyTests, Does.Contain("outside-symlink"));
            Assert.That(topologyTests, Does.Contain("Delete_junction_removes_only_the_link_and_never_its_target"));
            Assert.That(architecture, Does.Contain("Reparse-point operation policy"));
        });
    }

    [Test]
    public void Copy_engine_uses_unambiguous_64_bit_file_size_and_seek_wrappers()
    {
        var root = FindRepositoryRoot();
        var declarations = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "spl_com.h"));
        var api = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var wrappers = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(declarations, Does.Contain("struct CFileOffsetResult"));
            Assert.That(api, Does.Contain("CFileOffsetResult SalGetFileSizeEx"));
            Assert.That(api, Does.Contain("CFileOffsetResult SalSetFilePointerEx"));
            Assert.That(wrappers, Does.Contain("GetFileSizeEx(file, &size)"));
            Assert.That(wrappers, Does.Contain("SetFilePointerEx(file, input, &output, moveMethod)"));
            Assert.That(wrappers, Does.Contain("CFileOffsetResult(GetLastError())"));
            Assert.That(copy, Does.Contain("SalGetFileSizeEx("));
            Assert.That(copy, Does.Contain("SalSetFilePointerEx("));
            Assert.That(Regex.Matches(copy, @"(?m)^(?!\s*//).*?\bGetFileSize\s*\(").Count, Is.Zero,
                        "The copy engine must not reintroduce raw GetFileSize calls.");
            Assert.That(Regex.Matches(copy, @"(?m)^(?!\s*//).*?\bSetFilePointer\s*\(").Count, Is.Zero,
                        "The copy engine must not reintroduce raw SetFilePointer calls.");
        });
    }

    [TestCase(0xFFFFFFFFUL, true, 0xFFFFFFFFUL, true)]
    [TestCase(0xFFFFFFFFUL, true, 100000UL, false)]
    [TestCase(0x100000000UL, true, 0xFFFFFFFFUL, false)]
    [TestCase(0UL, false, 0xFFFFFFFFUL, false)]
    public void Plugin_readers_preserve_full_file_sizes_and_reject_only_their_explicit_caps(
        ulong size, bool succeeded, ulong cap, bool expectedAccepted)
    {
        // Exercise the maximum DWORD, successful sentinel value, over-cap, and failed-query contracts independently.
        Assert.That(succeeded && size <= cap, Is.EqualTo(expectedAccepted));

        var root = FindRepositoryRoot();
        var sdk = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "spl_gen.h"));
        var checkver = File.ReadAllText(Path.Combine(root, "src", "plugins", "checkver", "data.cpp"));
        var peviewer = File.ReadAllText(Path.Combine(root, "src", "plugins", "peviewer", "peviewer.cpp"));
        var tar = File.ReadAllText(Path.Combine(root, "src", "plugins", "tar", "fileio.cpp"));
        var nethood = File.ReadAllText(Path.Combine(root, "src", "plugins", "nethood", "cache.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(sdk, Does.Contain("CFileOffsetResult SalGetPluginFileSizeEx"));
            Assert.That(checkver, Does.Contain("!sizeResult.Succeeded || sizeResult.Value.Value == 0 || sizeResult.Value.Value > LOADED_SCRIPT_MAX"));
            Assert.That(peviewer, Does.Contain("CQuadWord& fileSize"));
            Assert.That(peviewer, Does.Contain("fileSize.HiDWord != 0"));
            Assert.That(peviewer, Does.Contain("fileSize.HiDWord, fileSize.LoDWord"));
            Assert.That(tar, Does.Contain("SalGetPluginFileSizeEx(SalamanderGeneral, File)"));
            Assert.That(tar, Does.Contain("StreamPos.Value > InputSize.Value"));
            Assert.That(tar, Does.Contain("read > InputSize.Value - StreamPos.Value"));
            Assert.That(nethood, Does.Contain("sizeResult.Succeeded && sizeResult.Value.Value <= 1000"));
            Assert.That(Regex.Matches(checkver + peviewer + tar + nethood, @"(?m)^(?!\s*//).*?\bGetFileSize\s*\(").Count, Is.Zero,
                        "Bounded plug-in readers must not reintroduce sentinel-ambiguous GetFileSize calls.");
        });
    }

    [Test]
    public void Split_combine_stages_and_verifies_output_before_publishing_it()
    {
        // Keep the plug-in from reintroducing a direct, partial write to the requested destination.
        var root = FindRepositoryRoot();
        var combine = File.ReadAllText(Path.Combine(root, "src", "plugins", "splitcbn", "combine.cpp"));
        var combineFiles = combine[..combine.IndexOf("//  CalculateFileCRC", StringComparison.Ordinal)];

        Assert.Multiple(() =>
        {
            Assert.That(combine, Does.Contain("SalGetPluginFileSizeEx(SalamanderGeneral"));
            Assert.That(Regex.Matches(combineFiles, @"(?m)^(?!\s*//).*?\bGetFileSize\s*\(").Count, Is.Zero,
                        "Combine totals and progress must not revive sentinel-ambiguous GetFileSize calls.");
            Assert.That(combine, Does.Contain("std::unique_ptr<char, decltype(&free)>"));
            Assert.That(combine, Does.Contain("SalGetTempFileName(targetDirectory, \"SCB\""));
            Assert.That(combine, Does.Contain("FILE_FLAG_WRITE_THROUGH"));
            Assert.That(combine, Does.Contain("FlushFileBuffers(outfile.Get()->HFile)"));
            Assert.That(combine, Does.Contain("VerifyCombinedOutput(temporaryOutput.GetName(), totalSize)"));
            Assert.That(combine, Does.Contain("GetFileInformationByHandle(output, &information)"));
            Assert.That(combine, Does.Contain("ReplaceFileW(targetNameW, stagedNameW, NULL, REPLACEFILE_WRITE_THROUGH"));
            Assert.That(combine, Does.Contain("MoveFileExW(stagedNameW, targetNameW, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)"));
            Assert.That(combine, Does.Contain("COperationResult"));
            Assert.That(combine, Does.Contain("result.ToLegacyBool(&error)"));
        });
    }

    [Test]
    public void File_operation_planning_uses_an_immutable_plan_and_narrow_filesystem_adapter()
    {
        var root = FindRepositoryRoot();
        var planHeader = File.ReadAllText(Path.Combine(root, "src", "operation_plan.h"));
        var plan = File.ReadAllText(Path.Combine(root, "src", "operation_plan.cpp"));
        var fileSystem = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.h"));
        var nativeFileSystem = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.cpp"));
        var planner = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(planHeader, Does.Contain("class COperationPlan"));
            Assert.That(planHeader, Does.Contain("BOOL Capture(COperations& operations)"));
            Assert.That(plan, Does.Contain("snapshots are immutable once exposed to the execution boundary"));
            Assert.That(plan, Does.Contain("DuplicatePlanPath"));
            Assert.That(plan, Does.Not.Contain("CreateFile("));
            Assert.That(plan, Does.Not.Contain("DeleteFile("));
            Assert.That(fileSystem, Does.Contain("class CFileOperationFileSystem"));
            Assert.That(fileSystem, Does.Contain("GetAttributes"));
            Assert.That(fileSystem, Does.Contain("GetDiskFreeSpace"));
            Assert.That(fileSystem, Does.Contain("SetFileOperationFileSystemForTests"));
            Assert.That(nativeFileSystem, Does.Contain("CWin32FileOperationFileSystem"));
            Assert.That(planner, Does.Contain("FileOperationFileSystem().GetAttributes"));
            Assert.That(planner, Does.Contain("FileOperationFileSystem().GetDiskFreeSpace"));
            Assert.That(journal, Does.Contain("AppendGoldenMasterPlan"));
            Assert.That(journal, Does.Contain("PLANITEM|%d|%s"));
        });
    }

    [Test]
    public void File_operation_journal_does_not_block_the_progress_dialog_on_large_plans()
    {
        var root = FindRepositoryRoot();
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));

        var workerBodyIndex = operations.IndexOf("unsigned ThreadWorkerBody", StringComparison.Ordinal);
        var startWorkerIndex = operations.IndexOf("CThreadOwner* StartWorker", StringComparison.Ordinal);
        var beginJournalIndex = operations.IndexOf("if (!script->BeginJournal())", StringComparison.Ordinal);

        // Large copies froze at 0% because the dialog thread opened every source and
        // flushed the journal after every path fragment before the worker started.
        Assert.Multiple(() =>
        {
            Assert.That(journal, Does.Contain("JournalWriteBufferCapacity"));
            Assert.That(journal, Does.Contain("FlushDurable"));
            Assert.That(journal, Does.Not.Contain("GetPathIdentity"));
            Assert.That(journal, Does.Contain("|unavailable"));
            Assert.That(workerBodyIndex, Is.GreaterThanOrEqualTo(0));
            Assert.That(startWorkerIndex, Is.GreaterThan(workerBodyIndex));
            Assert.That(beginJournalIndex, Is.GreaterThan(workerBodyIndex));
            Assert.That(beginJournalIndex, Is.LessThan(startWorkerIndex));
            Assert.That(operations.IndexOf("BeginJournal", startWorkerIndex < 0 ? 0 : startWorkerIndex, StringComparison.Ordinal), Is.EqualTo(-1));
        });
    }

    [Test]
    public void File_operation_correlation_ids_cross_plan_worker_ui_journal_and_log_boundaries()
    {
        var root = FindRepositoryRoot();
        var workerHeader = File.ReadAllText(Path.Combine(root, "src", "worker.h"));
        var worker = File.ReadAllText(Path.Combine(root, "src", "worker.cpp"));
        var workerBody = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var plan = File.ReadAllText(Path.Combine(root, "src", "operation_plan.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var dialogs = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var log = File.ReadAllText(Path.Combine(root, "src", "execlog.cpp"));

        // This source-level characterization pins the handoffs that cannot be deterministically faulted from UIA.
        Assert.Multiple(() =>
        {
            Assert.That(workerHeader, Does.Contain("OPERATION_CORRELATION_ID_LENGTH"));
            Assert.That(worker, Does.Contain("CreateOperationCorrelationId"));
            Assert.That(plan, Does.Contain("operations.GetCorrelationId()"));
            Assert.That(workerBody, Does.Contain("Worker-%s"));
            Assert.That(workerBody, Does.Contain("BeginItemAttempt(i)"));
            Assert.That(dialogs, Does.Contain("%s [#%s]"));
            Assert.That(dialogs, Does.Contain("Script->RecordItemRetry()"));
            Assert.That(journal, Does.Contain("CORRELATION|operation=%s"));
            Assert.That(journal, Does.Contain("RETRY|%d|attempt=%d"));
            Assert.That(log, Does.Contain("operation=%s, item=%d, attempt=%d"));
        });
    }

    [Test]
    public void Central_retry_policy_bounds_transient_read_retries_and_blocks_destructive_commits()
    {
        var root = FindRepositoryRoot();
        var policy = File.ReadAllText(Path.Combine(root, "src", "retry_policy.h"));
        var result = File.ReadAllText(Path.Combine(root, "src", "operation_result.h"));
        var copy = ReadOperationImplementationSources(root);

        // Source characterization keeps retry pacing and destructive-operation safety independently reviewable.
        Assert.Multiple(() =>
        {
            Assert.That(policy, Does.Contain("enum ERetryOperationKind"));
            Assert.That(policy, Does.Contain("rokReadOnly"));
            Assert.That(policy, Does.Contain("rokDestructiveCommit"));
            Assert.That(policy, Does.Contain("ERROR_NETNAME_DELETED"));
            Assert.That(policy, Does.Contain("kAutomaticRetryLimit = 3"));
            Assert.That(policy, Does.Contain("GetAutomaticRetryDelay"));
            // Retry jitter must use the non-wrapping clock that replaced the legacy tick sample.
            Assert.That(policy, Does.Contain("CMonotonicClock::Now()"));
            Assert.That(policy, Does.Contain("WaitForSingleObject(cancellationEvent, delay)"));
            Assert.That(policy, Does.Contain("kind == rokDestructiveCommit"));
            Assert.That(result, Does.Contain("return IsTransientOperationError(error);"));
            Assert.That(copy, Does.Contain("PrepareAutomaticRetry(err, &autoRetryAttemptsSNAP, rokReadOnly"));
            Assert.That(copy, Does.Contain("PrepareAutomaticRetry(err, &AutoRetryAttemptsSNAP, rokReadOnly"));
            Assert.That(copy, Does.Contain("PrepareAutomaticRetry(err, &autoRetryAttempts, rokDestructiveCommit"));
            Assert.That(copy, Does.Contain("PrepareAutomaticRetry(err, &AutoRetryCounter, rokDestructiveCommit"));
            Assert.That(copy, Does.Not.Contain("Sleep(100);"));
            Assert.That(copy, Does.Not.Contain("Sleep(AutoRetryCounter * 100);"));
        });
    }

    [Test]
    public void Transactional_copy_and_move_expose_each_durable_phase_to_a_deterministic_fault_adapter()
    {
        var root = FindRepositoryRoot();
        var executionHeader = File.ReadAllText(Path.Combine(root, "src", "operation_execution_filesystem.h"));
        var executionAdapter = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.cpp"));
        var copy = ReadOperationImplementationSources(root);
        var identities = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var nativeTests = File.ReadAllText(Path.Combine(root, "tests", "NativeSafetyTests", "NativeSafetyTests.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(executionHeader, Does.Contain("class COperationExecutionFileSystem"));
            Assert.That(executionHeader, Does.Contain("virtual HANDLE CreateFile"));
            Assert.That(executionHeader, Does.Contain("virtual BOOL WriteFile"));
            Assert.That(executionHeader, Does.Contain("virtual BOOL SetFileTime"));
            Assert.That(executionHeader, Does.Contain("virtual BOOL FlushFileBuffers"));
            Assert.That(executionHeader, Does.Contain("virtual BOOL ReplaceFile"));
            Assert.That(executionHeader, Does.Contain("virtual BOOL SetFileInformationByHandle"));
            Assert.That(executionHeader, Does.Contain("SetOperationExecutionFileSystemForTests"));
            Assert.That(executionAdapter, Does.Contain("CWin32OperationExecutionFileSystem"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().CreateFile"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().WriteFile"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().SetFileTime"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().FlushFileBuffers"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().ReplaceFile"));
            Assert.That(copy, Does.Contain("OperationExecutionFileSystem().MoveFile"));
            Assert.That(identities, Does.Contain("OperationExecutionFileSystem().SetFileInformationByHandle"));
            Assert.That(journal, Does.Contain("STATE|%d|prepared"));
            Assert.That(journal, Does.Contain("STATE|%d|temporary-ready"));
            Assert.That(nativeTests, Does.Contain("RunTransactionalFaultSequence(OperationExecutionFileSystem()"));
            Assert.That(nativeTests, Does.Contain("fake.GetCalls() != expectedCalls[phaseIndex]"));
        });
    }

    [Test]
    public void Native_destructive_operation_characterization_suite_retains_the_required_scenarios()
    {
        var root = FindRepositoryRoot();
        var operations = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "FileOperationUiTests.cs"));
        var largeDelete = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "LargeFlatDirectoryDeleteUiTests.cs"));
        // File access commands live separately from destructive operations but remain part of the required UI characterization set.
        var access = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "FileAccessUiTests.cs"));
        var crossVolume = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "CrossVolumeMoveCharacterizationUiTests.cs"));
        var recovery = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "OperationRecoveryCharacterizationUiTests.cs"));
        var workspace = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "FileOperationUiTestBase.cs"));
        var settings = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "UiTestSettings.cs"));
        var ads = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "AlternateDataStreams.cs"));
        var unsupportedAds = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "AlternateDataStreamsUnsupportedTargetUiTests.cs"));
        var nativeCommands = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "NativeCommands.cs"));

        Assert.Multiple(() =>
        {
            Assert.That(operations, Does.Contain("Copy_overwrite_replaces_the_existing_target_only_after_the_user_confirms"));
            Assert.That(operations, Does.Contain("Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree"));
            Assert.That(operations, Does.Contain("Copy_skip_keeps_the_existing_target_and_the_source"));
            Assert.That(operations, Does.Contain("Copy_skip_all_keeps_the_existing_conflicting_tree"));
            Assert.That(operations, Does.Contain("Rename_overwrite_replaces_the_collision_without_losing_source_metadata"));
            Assert.That(operations, Does.Contain("Rename_overwrite_decline_keeps_the_original_file_and_existing_target"));
            Assert.That(operations, Does.Contain("Rename_case_only_change_preserves_the_file_and_updates_its_displayed_name"));
            Assert.That(operations, Does.Contain("The default move fixture characterizes same-volume behavior."));
            Assert.That(operations, Does.Contain("Move_overwrite_replaces_the_existing_target_and_removes_the_source"));
            Assert.That(operations, Does.Contain("Move_skip_keeps_the_existing_target_and_the_unmoved_source"));
            Assert.That(operations, Does.Contain("Move_overwrite_all_replaces_every_conflict_before_removing_the_source_tree"));
            Assert.That(operations, Does.Contain("Move_skip_all_retains_conflicting_sources_but_moves_nonconflicting_siblings"));
            Assert.That(operations, Does.Contain("Delete_mixed_selection_removes_the_selected_file_and_directory_tree"));
            Assert.That(operations, Does.Contain("Delete_skip_for_locked_file_keeps_it_and_continues_with_later_items"));
            // A dedicated fixture keeps the expensive large-directory setup out
            // of ordinary operation cases while retaining this regression guard.
            Assert.That(largeDelete, Does.Contain("Delete_large_flat_directory_removes_all_descendants"));
            Assert.That(largeDelete, Does.Contain("FileCount = 2_048"));
            Assert.That(access, Does.Contain("Find_files_searches_subdirectories_from_the_active_panel"));
            Assert.That(access, Does.Contain("View_file_opens_the_selected_file_in_the_internal_viewer"));
            Assert.That(access, Does.Contain("Edit_file_opens_the_selected_file_in_the_configured_editor"));
            Assert.That(operations, Does.Contain("Delete_to_recycle_bin_removes_the_source_and_creates_a_recoverable_shell_item"));
            Assert.That(operations, Does.Contain("Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation"));
            Assert.That(crossVolume, Does.Contain("Move_across_volumes_copies_the_complete_tree_before_removing_the_source"));
            Assert.That(crossVolume, Does.Contain("RequireCrossVolumeRoot"));
            Assert.That(operations, Does.Contain("Copy_preserves_multiple_empty_large_and_edge_named_alternate_data_streams"));
            Assert.That(operations, Does.Contain("Copy_overwrite_replaces_target_streams_and_removes_stale_streams"));
            Assert.That(operations, Does.Contain("Copy_retries_a_temporarily_denied_alternate_data_stream_without_losing_it"));
            Assert.That(crossVolume, Does.Contain("Move_across_ADS_capable_volumes_preserves_multiple_streams_before_removing_the_source"));
            Assert.That(unsupportedAds, Does.Contain("Cross_volume_move_to_an_ADS_unsupported_target_keeps_the_source_when_metadata_loss_is_declined"));
            // Secondary-volume panel enumeration and shared IDYES dialogs require an explicit panel-settling and caption invariant.
            Assert.That(workspace, Does.Contain("PrepareSourcePanelForSelection"));
            Assert.That(workspace, Does.Contain("QuickSearchSourceItem"));
            Assert.That(workspace, Does.Contain("PanelSettleMilliseconds"));
            // Copy and move assertions must wait for native handle release, not only for directory-entry visibility.
            Assert.That(workspace, Does.Contain("WaitForOperationOutputToBeReleased"));
            Assert.That(operations, Does.Contain("WaitForOperationOutputToBeReleased"));
            Assert.That(nativeCommands, Does.Contain("RefreshActiveFilePanel"));
            Assert.That(nativeCommands, Does.Contain("ClearActiveSelection"));
            // The remote runner has no stable interactive clipboard owner, so panel readiness must never depend on clipboard observation.
            Assert.That(nativeCommands, Does.Not.Contain("GetClipboardSequenceNumber"));
            Assert.That(unsupportedAds, Does.Contain("WaitForOperationPrompt(\"Confirm Alternate Data Streams Loss\", 6)"));
            Assert.That(ads, Does.Contain("RequireSupportAt"));
            Assert.That(ads, Does.Contain("RequireUnsupportedAt"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_CROSS_VOLUME_ROOT"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_SECOND_VOLUME_SKIP_REASON"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_SKIP_REASON"));
            Assert.That(recovery, Does.Contain("Restart_reconciliation_commits_a_fully_written_transactional_target"));
            Assert.That(recovery, Does.Contain("STATE|0|temporary-ready"));
            Assert.That(workspace, Does.Contain("TargetVolumeRoot"));
            Assert.That(workspace, Does.Contain("TargetWorkspaceDirectory"));
        });
    }

    [Test]
    public void Crash_report_uploads_use_certificate_validated_https_with_explicit_consent()
    {
        var root = FindRepositoryRoot();
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salmon", "salmon_base.props"));
        var dialog = File.ReadAllText(Path.Combine(root, "src", "lang", "lang.rc"));
        var reporting = File.ReadAllText(Path.Combine(root, "reporting.md"));

        Assert.Multiple(() =>
        {
            Assert.That(upload, Does.Contain("WinHttpOpen("));
            Assert.That(upload, Does.Contain("WinHttpConnect(session, kServerName, INTERNET_DEFAULT_HTTPS_PORT"));
            Assert.That(upload, Does.Contain("WinHttpOpenRequest(connection, L\"POST\", kUploadPath"));
            Assert.That(upload, Does.Contain("WINHTTP_FLAG_SECURE"));
            Assert.That(upload, Does.Contain("WINHTTP_DISABLE_REDIRECTS"));
            Assert.That(upload, Does.Contain("WinHttpGetIEProxyConfigForCurrentUser"));
            Assert.That(upload, Does.Contain("/api/v1/crash-reports"));
            Assert.That(upload, Does.Contain("Transfer-Encoding: chunked"));
            Assert.That(upload, Does.Contain("WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH"));
            Assert.That(upload, Does.Contain("FinishChunkedRequest"));
            Assert.That(upload, Does.Contain("CancelUploadThread"));
            Assert.That(upload, Does.Not.Contain("DWORD totalLength"));
            Assert.That(upload, Does.Not.Contain("WINHTTP_OPTION_SECURITY_FLAGS"));
            Assert.That(upload, Does.Not.Contain("winsock2.h"));
            Assert.That(upload, Does.Not.Contain("http://"));
            Assert.That(Regex.Matches(upload, @"(?m)^(?!\s*//).*?\b(gethostbyname|connect|send|recv)\s*\(").Count, Is.Zero,
                        "The crash uploader must not reintroduce raw Winsock transport calls.");
            Assert.That(project, Does.Contain("winhttp.lib"));
            Assert.That(project, Does.Not.Contain("Ws2_32.lib"));
            Assert.That(dialog, Does.Contain("Consent: Send Report uploads this crash archive"));
            Assert.That(dialog, Does.Contain("over HTTPS"));
            Assert.That(reporting, Does.Contain("https://reports.taskscape.com/api/v1/crash-reports"));
            Assert.That(reporting, Does.Contain("http://reports.taskscape.com/upload.php"));
            Assert.That(reporting, Does.Contain("Transfer-Encoding: chunked"));
        });
    }

    [Test]
    public void Crash_minidumps_default_to_metadata_without_private_memory_or_data_segments()
    {
        var root = FindRepositoryRoot();
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));
        var reporting = File.ReadAllText(Path.Combine(root, "reporting.md"));

        // Keep the privacy default visible in both the executable policy and user-facing report inventory.
        Assert.Multiple(() =>
        {
            Assert.That(minidump, Does.Contain("Keep crash reports diagnostically useful without serializing arbitrary private writable memory."));
            Assert.That(minidump, Does.Not.Contain("MiniDumpWithPrivateReadWriteMemory"));
            Assert.That(minidump, Does.Not.Contain("MiniDumpWithDataSegs"));
            Assert.That(minidump, Does.Not.Contain("MiniDumpWithHandleData"));
            Assert.That(minidump, Does.Contain("Privacy is the default: never retry a failed minimal dump with a memory-rich variant."));
            Assert.That(reporting, Does.Contain("intentionally excludes private writable memory, module data segments, and handle data"));
        });
    }

    [Test]
    public void Release_builds_apply_and_audit_the_common_PE_mitigation_baseline()
    {
        var root = FindRepositoryRoot();
        var targets = File.ReadAllText(Path.Combine(root, "src", "Directory.Build.targets"));
        var audit = File.ReadAllText(Path.Combine(root, "tools", "audit-pe-hardening.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var releaseInstaller = File.ReadAllText(Path.Combine(root, "tools", "build-release-installer.ps1"));

        // Project properties alone are insufficient: require both post-import enforcement and PE-header inspection.
        Assert.Multiple(() =>
        {
            Assert.That(targets, Does.Contain("'$(Configuration)' == 'Release'"));
            Assert.That(targets, Does.Contain("'$(Configuration)' == 'Debug'"));
            Assert.That(targets, Does.Contain("/FS %(AdditionalOptions)"));
            Assert.That(targets, Does.Contain("<SDLCheck>true</SDLCheck>"));
            Assert.That(targets, Does.Contain("<ControlFlowGuard>Guard</ControlFlowGuard>"));
            Assert.That(targets, Does.Contain("/CETCOMPAT /HIGHENTROPYVA"));
            Assert.That(audit, Does.Contain("dumpbin.exe"));
            Assert.That(audit, Does.Contain("Control Flow Guard"));
            Assert.That(audit, Does.Contain("CET compatible"));
            // The audit must reject a mixed Debug/Release staging directory instead of applying Release requirements to Debug helpers.
            Assert.That(audit, Does.Contain("exact Release_x64 artifact directory"));
            // CRT-free and x86 release helpers retain their compatible mitigations without being required to carry unsupported CFG/CET metadata.
            Assert.That(audit, Does.Contain("crtFreeArtifactNames"));
            Assert.That(audit, Does.Contain("machine \\(x64\\)"));
            // The shared Build Installer pass owns the Release artifact root and PE audit for both CI and local parity.
            Assert.That(releaseInstaller, Does.Contain("audit-pe-hardening.ps1"));
            Assert.That(releaseInstaller, Does.Contain("salamander\\Release_x64"));
        });
    }

    [Test]
    public void Release_pipeline_uses_the_single_supported_VS_2026_toolset()
    {
        var root = FindRepositoryRoot();
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var runner = File.ReadAllText(Path.Combine(root, "scripts", "runtests.ps1"));

        // A single compiler contract prevents unavailable legacy toolchains from masking release-test failures.
        Assert.Multiple(() =>
        {
            Assert.That(workflow, Does.Contain("PLATFORM_TOOLSET: v145"));
            Assert.That(workflow, Does.Contain("setup-vs2026-buildtools.ps1"));
            Assert.That(runner, Does.Contain("[ValidateSet('v145')]"));
            Assert.That(Regex.Matches(workflow, "PLATFORM_TOOLSET: (?!v145)").Count, Is.Zero);
            Assert.That(Regex.Matches(workflow, "group: FileManager UI").Count, Is.EqualTo(3));
        });
    }

    [Test]
    public void Legacy_zip_extraction_rejects_escaping_and_device_entry_paths_before_target_concatenation()
    {
        var root = FindRepositoryRoot();
        var extraction = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "extract.cpp"));
        var resources = File.ReadAllText(Path.Combine(root, "src", "plugins", "zip", "lang", "lang.rc2"));

        // Preserve both pre-concatenation validation and the pinned-root check before bytes reach a legacy-created handle.
        Assert.Multiple(() =>
        {
            Assert.That(extraction, Does.Contain("static BOOL IsUnsafeArchiveEntryPath"));
            Assert.That(extraction, Does.Contain("static BOOL IsReparsePointExtractionRoot"));
            Assert.That(extraction, Does.Contain("static BOOL HasReparsePointInExtractionPath"));
            Assert.That(extraction, Does.Contain("class CExtractionRootHandle"));
            Assert.That(extraction, Does.Contain("FILE_FLAG_OPEN_REPARSE_POINT"));
            Assert.That(extraction, Does.Contain("GetFinalPathNameByHandleA"));
            Assert.That(extraction, Does.Contain("extractionRoot.Contains(OutputFile->File)"));
            Assert.That(extraction, Does.Contain("Do not write archive bytes when the opened object resolved outside the pinned root."));
            Assert.That(extraction, Does.Contain("FILE_ATTRIBUTE_REPARSE_POINT"));
            Assert.That(extraction, Does.Contain("Recheck every existing prefix"));
            Assert.That(extraction, Does.Contain("HasReparsePointInExtractionPath(targetDir, extractionRootLength)"));
            Assert.That(extraction, Does.Contain("Drive-qualified paths and alternate streams"));
            Assert.That(extraction, Does.Contain("Win32 device aliases bypass normal directory traversal"));
            Assert.That(extraction, Does.Contain("return IDS_UNSAFEEXTRACTPATH"));
            Assert.That(extraction.IndexOf("IsUnsafeArchiveEntryPath(entryPath)", StringComparison.Ordinal), Is.LessThan(extraction.IndexOf("*(targetDir + targetDirLen++)", StringComparison.Ordinal)));
            Assert.That(extraction.IndexOf("IsReparsePointExtractionRoot(targetDir)", StringComparison.Ordinal), Is.LessThan(extraction.IndexOf("ErrorID = ExtractFiles(targetDir);", StringComparison.Ordinal)));
            Assert.That(resources, Does.Contain("Cannot extract archive entry because its path escapes the selected destination."));
        });
    }

    [Test]
    public void Lifecycle_leak_lane_measures_handle_gui_and_private_memory_growth()
    {
        var root = FindRepositoryRoot();
        var snapshot = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "ProcessResourceSnapshot.cs"));
        var test = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "LifecycleLeakUiTests.cs"));
        var settings = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "UiTestSettings.cs"));
        var nightly = File.ReadAllText(Path.Combine(root, ".github", "workflows", "nightly-lock-stress.yml"));

        // The scheduled soak must keep every measured resource in its failure condition, not merely repeat launches.
        Assert.Multiple(() =>
        {
            Assert.That(snapshot, Does.Contain("GetGuiResources"));
            Assert.That(test, Does.Contain("kernel handles"));
            Assert.That(test, Does.Contain("GDI objects"));
            Assert.That(test, Does.Contain("USER objects"));
            Assert.That(test, Does.Contain("private bytes"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_LEAK_CYCLES"));
            // Job-level configuration keeps the 100-cycle soak available to both provider probes and the full nightly suite.
            Assert.That(nightly, Does.Contain("FILEMANAGER_UI_LEAK_CYCLES: '100'"));
        });
    }

    [Test]
    public void Native_safety_target_executes_shared_checked_arithmetic_boundaries()
    {
        var root = FindRepositoryRoot();
        var project = File.ReadAllText(Path.Combine(root, "tests", "NativeSafetyTests", "NativeSafetyTests.vcxproj"));
        var source = File.ReadAllText(Path.Combine(root, "tests", "NativeSafetyTests", "NativeSafetyTests.cpp"));
        var runner = File.ReadAllText(Path.Combine(root, "scripts", "runtests.ps1"));

        // The aggregate runner must execute the C++ target, not merely leave a project available for manual use.
        Assert.Multiple(() =>
        {
            Assert.That(project, Does.Contain("<PlatformToolset>v145</PlatformToolset>"));
            Assert.That(source, Does.Contain("CheckedAddUInt64"));
            Assert.That(source, Does.Contain("CheckedMultiplyUInt64"));
            Assert.That(source, Does.Contain("CheckedCastUInt64ToDword"));
            Assert.That(source, Does.Contain("TestNativeFileOperationCharacterization"));
            Assert.That(source, Does.Contain("CopyFileW(source.c_str(), copied.c_str(), FALSE)"));
            Assert.That(source, Does.Contain("MoveFileW(copied.c_str(), renamed.c_str())"));
            Assert.That(source, Does.Contain("DeleteFileW(renamed.c_str())"));
            Assert.That(source, Does.Contain("TestExecutionAdapterFaultInjection"));
            Assert.That(source, Does.Contain("SetOperationExecutionFileSystemForTests(&fake)"));
            Assert.That(source, Does.Contain("ERROR_DISK_FULL"));
            Assert.That(source, Does.Contain("ERROR_DISK_QUOTA_EXCEEDED"));
            Assert.That(source, Does.Contain("ERROR_SHARING_VIOLATION"));
            Assert.That(runner, Does.Contain("Invoke-NativeSafetyTests"));
            Assert.That(runner, Does.Contain("NativeSafetyTests (Debug x64)"));
        });
    }

    [Test]
    public void Release_symbols_are_indexed_by_codeview_identity_and_verified_before_private_retention()
    {
        var root = FindRepositoryRoot();
        var indexer = File.ReadAllText(Path.Combine(root, "tools", "new-symbol-index.ps1"));
        var verifier = File.ReadAllText(Path.Combine(root, "tools", "test-symbol-index.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var releaseInstaller = File.ReadAllText(Path.Combine(root, "tools", "build-release-installer.ps1"));

        // Exact symbol lookup requires both the PE CodeView key and immutable hashes before the private artifact is retained.
        Assert.Multiple(() =>
        {
            Assert.That(indexer, Does.Contain("RSDS"));
            Assert.That(indexer, Does.Contain("symbolKey"));
            Assert.That(indexer, Does.Contain("moduleSha256"));
            Assert.That(indexer, Does.Contain("pdbSha256"));
            // Identical staged helper copies may share a CodeView key; the verifier must still reject a key with divergent content.
            Assert.That(verifier, Does.Contain("Symbol key resolves to inconsistent release content"));
            // Symbol indexing remains part of the shared Build Installer gate rather than a CI-only step list.
            Assert.That(releaseInstaller, Does.Contain("new-symbol-index.ps1"));
            Assert.That(releaseInstaller, Does.Contain("test-symbol-index.ps1"));
            Assert.That(workflow, Does.Contain("release-symbol-index.json"));
            // The workflow must request the repository maximum instead of a
            // higher value that Actions silently clamps and warns about.
            Assert.That(workflow, Does.Contain("retention-days: 90"));
        });
    }

    [Test]
    public void Release_pipeline_runs_complete_UI_and_native_safety_coverage_with_VS_2026()
    {
        var root = FindRepositoryRoot();
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "build-installer.yml"));
        var releaseInstaller = File.ReadAllText(Path.Combine(root, "tools", "build-release-installer.ps1"));

        // Complete UI and focused native checks retain broad coverage without a second compiler lane.
        Assert.Multiple(() =>
        {
            Assert.That(releaseInstaller, Does.Contain("v145 native regression subset"));
            Assert.That(workflow, Does.Contain("Run Build Installer pass (Release | x64)"));
            Assert.That(workflow, Does.Contain("Run release tests without unexpected skips"));
            Assert.That(workflow, Does.Contain("Upload v145 complete UI results"));
            // The release gate must remain runnable after a forced main-branch update.
            Assert.That(workflow, Does.Contain("Force-pushes can leave the event's previous revision outside the checkout"));
            Assert.That(workflow, Does.Not.Contain("compare-vstest-trx.ps1"));
        });
    }

    [Test]
    public void Crash_artifacts_enable_EFS_before_dump_or_archive_creation()
    {
        var root = FindRepositoryRoot();
        var salmon = File.ReadAllText(Path.Combine(root, "src", "salmon", "salmon.cpp"));
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));
        var compression = File.ReadAllText(Path.Combine(root, "src", "salmon", "compress.cpp"));

        // A failed EFS setup must stop report creation rather than leave an unprotected fallback artifact.
        Assert.Multiple(() =>
        {
            Assert.That(salmon, Does.Contain("EnsureCrashReportDirectoryEncrypted"));
            Assert.That(salmon, Does.Contain("EncryptFileW(apiPath)"));
            Assert.That(salmon, Does.Contain("CrashReportRetentionTicks"));
            Assert.That(salmon, Does.Contain("IsRetainedCrashReportArtifact"));
            Assert.That(salmon, Does.Contain("DeleteNamedCrashReportArtifact"));
            Assert.That(salmon, Does.Contain("const char* extensions[] = {\".DMP\", \".TXT\", \".INF\", \".OPS\", \".7Z\"}"));
            Assert.That(minidump, Does.Contain("FilterMiniDumpCallback"));
            Assert.That(minidump, Does.Contain("case IncludeVmRegionCallback"));
            Assert.That(minidump, Does.Contain("ModuleWriteDataSeg | ModuleWriteTlsData"));
            Assert.That(minidump, Does.Contain("Unable to encrypt the crash-report directory"));
            Assert.That(compression, Does.Contain("!EnsureCrashReportDirectoryEncrypted(archive.c_str())"));
            Assert.That(compression, Does.Contain("reportName + \".DMP|\""));
            Assert.That(compression, Does.Not.Contain("std::string mask = reportName + \".*\""));
        });
    }

    [Test]
    public void Unsafe_API_baseline_rejects_new_repository_call_fingerprints()
    {
        var root = FindRepositoryRoot();
        var generator = File.ReadAllText(Path.Combine(root, "tools", "new-unsafe-api-baseline.ps1"));
        var verifier = File.ReadAllText(Path.Combine(root, "tools", "test-unsafe-api-baseline.ps1"));
        var runner = File.ReadAllText(Path.Combine(root, "scripts", "runtests.ps1"));
        var baseline = File.ReadAllText(Path.Combine(root, "tools", "unsafe-api-baseline.json"));

        // Content fingerprints distinguish pre-existing debt from a new API call without treating line movement as an addition.
        Assert.Multiple(() =>
        {
            Assert.That(generator, Does.Contain("Get-UnsafeApiEntries"));
            Assert.That(verifier, Does.Contain("New unsafe API debt"));
            Assert.That(verifier, Does.Contain("fingerprint"));
            // The baseline must scan from its own repository root when callers run the script from scripts/.
            Assert.That(verifier, Does.Contain("git -C $root grep"));
            Assert.That(runner, Does.Contain("test-unsafe-api-baseline.ps1"));
            Assert.That(baseline, Does.Contain("\"schemaVersion\":  1"));
        });
    }

    [Test]
    public void Unicode_matrix_exercises_ansi_round_trippable_names_and_a_long_native_copy_path()
    {
        var root = FindRepositoryRoot();
        var operations = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "FileOperationUiTests.cs"));
        var identities = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "FileIdentity.cs"));

        // Keep identity checks on opened handles while the matrix stays within the product's documented ANSI path boundary.
        Assert.Multiple(() =>
        {
            Assert.That(operations, Does.Contain("Ansi_round_trippable_unicode_and_long_path_operations_preserve_distinct_entries"));
            // Keep the two quick-search targets distinguishable before their non-ASCII suffixes.
            Assert.That(operations, Does.Contain("first-unicode-\\u00e9.txt"));
            Assert.That(operations, Does.Contain("Enumerable.Repeat"));
            // Include the payload in the dynamic budget so a longer CI sandbox root cannot exceed PATH_MAX_PATH.
            Assert.That(operations, Does.Contain("productPathMaximumLength"));
            Assert.That(operations, Does.Contain("payloadName.Length"));
            Assert.That(operations, Does.Contain("NativeCommands.RenameFile"));
            Assert.That(operations, Does.Contain("NativeCommands.DeleteFiles"));
            Assert.That(operations, Does.Contain("FileIdentity.Capture"));
            Assert.That(identities, Does.Contain("GetFileInformationByHandle"));
            Assert.That(identities, Does.Contain("FileShare.ReadWrite | FileShare.Delete"));
        });
    }

    [Test]
    public void Ftp_bookmark_verification_uses_the_legacy_ansi_list_box()
    {
        var root = FindRepositoryRoot();
        var basicUiTests = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "BasicUiTests.cs"));
        var nativeCommands = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "NativeCommands.cs"));

        // An ANSI list box misreads a SendMessageW string at its first UTF-16 terminator, and a forced restart must wait for the committed generation.
        Assert.Multiple(() =>
        {
            Assert.That(nativeCommands, Does.Contain("EntryPoint = \"SendMessageA\""));
            Assert.That(nativeCommands, Does.Contain("SendMessageAnsiText(listHandle, LbFindStringExact"));
            Assert.That(basicUiTests, Does.Contain("WaitForFtpBookmark(bookmarksDialog"));
            Assert.That(basicUiTests, Does.Contain("WaitForFtpBookmarkPersistence(editedBookmarkName)"));
            Assert.That(basicUiTests, Does.Contain("WaitForFtpBookmark(reloadedDialog"));
        });
    }

    [Test]
    public void Dynamic_library_loads_use_restricted_search_paths()
    {
        var root = FindRepositoryRoot();
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var releaseHandles = File.ReadAllText(Path.Combine(root, "src", "common", "handles.h"));
        var debugHandles = File.ReadAllText(Path.Combine(root, "src", "common", "handles.cpp"));
        var widePath = File.ReadAllText(Path.Combine(root, "src", "common", "wide_path.h"));

        Assert.Multiple(() =>
        {
            Assert.That(startup, Does.Contain("InitializeDllSearchPaths()"));
            Assert.That(startup, Does.Contain("SetDefaultDllDirectories"));
            Assert.That(startup, Does.Contain("AddDllDirectory"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_APPLICATION_DIR"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_SYSTEM32"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_USER_DIRS"));
            // A normal fresh profile must take the same plug-in seeding path as an isolated UI-test profile.
            Assert.That(startup, Does.Contain("selectedPreviousConfigurationRoot || !hasCommittedConfiguration"));
            Assert.That(widePath, Does.Contain("GetFullPathNameW"));
            Assert.That(releaseHandles, Does.Contain("GetFullPathForWin32Api"));
            Assert.That(debugHandles, Does.Contain("GetFullPathForWin32Api"));
            Assert.That(releaseHandles, Does.Contain("::LoadLibraryExW(fullPath, NULL, loadFlags)"));
            Assert.That(debugHandles, Does.Contain("::LoadLibraryExW(fullPath, NULL, loadFlags)"));
            Assert.That(releaseHandles, Does.Contain("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"));
            Assert.That(debugHandles, Does.Contain("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"));
            Assert.That(Regex.Matches(releaseHandles, @"(?m)^(?!\s*//).*?\bLoadLibraryW\s*\(").Count, Is.Zero,
                        "Release builds must not restore unrestricted LoadLibraryW calls.");
            Assert.That(Regex.Matches(debugHandles, @"(?m)^(?!\s*//).*?\bLoadLibraryW\s*\(").Count, Is.Zero,
                        "Debug builds must not restore unrestricted LoadLibraryW calls.");
        });
    }

    [Test]
    public void Crash_reporter_loads_dbghelp_from_system32_not_the_application_directory()
    {
        var root = FindRepositoryRoot();
        var minidump = File.ReadAllText(Path.Combine(root, "src", "salmon", "minidump.cpp"));

        // Crash reporting must still work when the installer correctly omits a copied Windows system DLL.
        Assert.Multiple(() =>
        {
            Assert.That(minidump, Does.Contain("LoadLibraryExA(\"dbghelp.dll\", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)"));
            Assert.That(minidump, Does.Not.Contain("moduleFileName.substr(0, slash + 1) + \"dbghelp.dll\""));
        });
    }

    [Test]
    public void Thumbnail_and_archive_metadata_use_the_restartable_parser_broker()
    {
        var root = FindRepositoryRoot();
        var protocol = File.ReadAllText(Path.Combine(root, "src", "parserbroker_protocol.h"));
        var client = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker", "salbroker.cpp"));
        var thumbnails = File.ReadAllText(Path.Combine(root, "src", "fileswindow_init.cpp"));
        var archives = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var installer = File.ReadAllText(Path.Combine(root, "Installer", "setup.iss"));

        Assert.Multiple(() =>
        {
            Assert.That(protocol, Does.Contain("PARSER_BROKER_VERSION = 1"));
            Assert.That(protocol, Does.Contain("PARSER_BROKER_MAX_PAYLOAD"));
            Assert.That(protocol, Does.Contain("CParserBrokerMessageHeader"));
            Assert.That(protocol, Does.Contain("pbmtThumbnailRequest"));
            Assert.That(protocol, Does.Contain("pbmtArchiveMetadataRequest"));
            Assert.That(client, Does.Contain("CreateRestrictedToken"));
            Assert.That(client, Does.Contain("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE"));
            Assert.That(client, Does.Contain("JOB_OBJECT_LIMIT_PROCESS_MEMORY"));
            Assert.That(client, Does.Contain("CancelIoEx"));
            // Request serialization is scope-owned and ranked so callback unwinding
            // cannot strand the broker lock or obscure its acquisition order.
            Assert.That(client, Does.Contain("CScopedCriticalSection lock(&Lock, lkrExternalBroker, \"ParserBroker.Lock\")"));
            Assert.That(client, Does.Contain("for (int attempt = 0; attempt != 2; ++attempt)"));
            Assert.That(client, Does.Contain("responseHeader.PayloadLength > responseCapacity"));
            Assert.That(broker, Does.Contain("SHCreateItemFromParsingName"));
            Assert.That(broker, Does.Contain("requestHeader.PayloadLength > PARSER_BROKER_MAX_PAYLOAD"));
            Assert.That(thumbnails, Does.Contain("ParserBroker.LoadThumbnail"));
            Assert.That(thumbnails, Does.Not.Contain("(*loader)->LoadThumbnail"));
            Assert.That(archives, Does.Contain("ParserBroker.QueryArchiveMetadata"));
            Assert.That(installer, Does.Contain("salbroker.exe"));
        });
    }

    [Test]
    public void Plugin_entry_scope_and_callback_state_restore_host_state_after_an_unwinding_entry_point()
    {
        var root = FindRepositoryRoot();
        var loader = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var header = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var messages = File.ReadAllText(Path.Combine(root, "src", "mainwnd_messages.cpp"));
        var shutdown = File.ReadAllText(Path.Combine(root, "src", "mainwnd_shutdown.cpp"));
        var pathUtilities = File.ReadAllText(Path.Combine(root, "src", "path_utils.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(loader, Does.Contain("class CPluginEntryScope"));
            Assert.That(loader, Does.Contain("~CPluginEntryScope()"));
            Assert.That(loader, Does.Contain("CPluginDataLock dataLock"));
            Assert.That(loader, Does.Contain("PluginIface.Init(NULL, 0)"));
            // This ordering contract spans lines, so accept both Git checkout
            // conventions while still requiring the two calls to remain adjacent.
            Assert.That(loader, Does.Match(@"CallbackState\.Leave\(\);\r?\n        SalamanderGeneral\.Init\(PluginIface\.GetInterface\(\)\);"));
            Assert.That(loader, Does.Contain("CPluginEntryScope pluginEntry(Plugins.GetCallbackState(), PluginIface,"));
            Assert.That(loader, Does.Contain("pluginEntry.SetReturnedInterface(resIface)"));
            Assert.That(loader, Does.Not.Contain("EnterPlugin(); // for the plugin entry point"));
            // Callback state must stay owned by CPlugins while the loading scope
            // receives only the narrow reference it needs for balanced entry/exit.
            Assert.That(header, Does.Contain("class CPluginCallbackState"));
            Assert.That(header, Does.Contain("std::atomic<int> CallbackDepth"));
            Assert.That(header, Does.Contain("SRWLOCK TransitionLock"));
            Assert.That(header, Does.Contain("CPluginCallbackState CallbackState"));
            Assert.That(header, Does.Contain("CPluginCallbackState& GetCallbackState()"));
            Assert.That(loader, Does.Contain("void CPluginCallbackState::Enter()"));
            Assert.That(loader, Does.Contain("void CPluginCallbackState::Leave()"));
            Assert.That(loader, Does.Contain("Plugins.GetCallbackState().Enter()"));
            Assert.That(loader, Does.Not.Contain("AlreadyInPlugin"));
            Assert.That(loader, Does.Not.Contain("PluginNestingStateLock"));
            Assert.That(header, Does.Contain("BOOL IsInPlugin();"));
            Assert.That(messages, Does.Contain("IsInPlugin() || StopRefresh > 0"));
            Assert.That(shutdown, Does.Contain("!endAfterCleanup && IsInPlugin()"));
            Assert.That(pathUtilities, Does.Contain("!IsInPlugin()"));
        });
    }

    [Test]
    public void Delete_manager_callback_registration_invalidates_before_window_teardown_and_rejects_stale_generations()
    {
        var root = FindRepositoryRoot();
        var cacheHeader = File.ReadAllText(Path.Combine(root, "src", "cache.h"));
        var cache = File.ReadAllText(Path.Combine(root, "src", "cache.cpp"));
        var enumerationData = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var enumeration = File.ReadAllText(Path.Combine(root, "src", "file_enumeration.cpp"));
        var messages = File.ReadAllText(Path.Combine(root, "src", "mainwnd_messages.cpp"));

        Assert.Multiple(() =>
        {
            // The delete-manager lock makes target invalidation atomic with worker notification posting.
            Assert.That(cacheHeader, Does.Contain("HWND CallbackWindow"));
            Assert.That(cacheHeader, Does.Contain("DWORD CallbackGeneration"));
            Assert.That(cacheHeader, Does.Contain("RegisterCallbackWindow(HWND hWindow)"));
            Assert.That(cacheHeader, Does.Contain("InvalidateCallbackWindow(HWND hWindow)"));
            Assert.That(cache, Does.Contain("PostMessage(CallbackWindow, WM_USER_PROCESSDELETEMAN, (WPARAM)CallbackGeneration, 0)"));
            Assert.That(cache, Does.Contain("CallbackWindow = NULL;"));
            Assert.That(cache, Does.Contain("CallbackGeneration++"));
            Assert.That(cache, Does.Contain("CallbackWindow == hWindow && CallbackGeneration == generation"));
            Assert.That(cache, Does.Not.Contain("PostMessage(MainWindow->HWindow, WM_USER_PROCESSDELETEMAN, 0, 0)"));
            // Dispatch must reject stale messages, and destruction must invalidate before the window tears down children.
            Assert.That(messages, Does.Contain("DeleteManager.RegisterCallbackWindow(HWindow);"));
            Assert.That(messages, Does.Contain("DeleteManager.IsCurrentCallbackWindow(HWindow, (DWORD)wParam)"));
            Assert.That(messages, Does.Contain("DeleteManager.InvalidateCallbackWindow(HWindow);"));
            Assert.That(messages.IndexOf("DeleteManager.InvalidateCallbackWindow(HWindow);", StringComparison.Ordinal),
                        Is.LessThan(messages.IndexOf("SHChangeNotifyRelease();", StringComparison.Ordinal)));
            // Viewer requests also stop waiting when their source panel is invalidated or its HWND post fails.
            Assert.That(enumerationData, Does.Contain("BOOL WaitingForResult"));
            Assert.That(enumeration, Does.Contain("CancelFileNamesEnumRequestLocked"));
            Assert.That(enumeration, Does.Contain("FileNamesEnumData.WaitingForResult && FileNamesEnumData.SrcUID == sourceUID"));
            Assert.That(enumeration, Does.Contain("CancelFileNamesEnumRequestLocked((int)(UINT_PTR)FileNamesEnumSources[i - 1]);"));
            Assert.That(enumeration, Does.Contain("if (!PostMessage(hWnd, WM_USER_ENUMFILENAMES, reqUID, 0))"));
        });
    }

    [Test]
    public void Icon_work_pool_bounds_memory_and_prioritizes_visible_current_generation_work()
    {
        var root = FindRepositoryRoot();
        var header = File.ReadAllText(Path.Combine(root, "src", "iconpool.h"));
        var implementation = File.ReadAllText(Path.Combine(root, "src", "iconpool.cpp"));

        // The queue contract prevents directory-sized background warming from
        // retaining unbounded work or delaying the currently visible panel.
        Assert.Multiple(() =>
        {
            Assert.That(header, Does.Contain("#define ICON_POOL_QUEUE_SIZE 64"));
            Assert.That(header, Does.Contain("enum EIconWorkPriority"));
            Assert.That(header, Does.Contain("iwpVisible"));
            Assert.That(header, Does.Contain("DWORD Generation"));
            Assert.That(header, Does.Contain("volatile LONG InProgress"));
            Assert.That(header, Does.Contain("struct CIconQueueMetrics"));
            Assert.That(header, Does.Contain("BackpressureRejected"));
            Assert.That(header, Does.Contain("VisiblePreemptions"));
            Assert.That(header, Does.Contain("DWORD BeginGeneration()"));
            Assert.That(header, Does.Contain("CancelObsoleteGenerations"));
            Assert.That(header, Does.Contain("CIconQueueMetrics GetMetrics()"));
            Assert.That(implementation, Does.Contain("IsSameWorkItem(queued, *item)"));
            Assert.That(implementation, Does.Contain("item->Priority == iwpVisible"));
            Assert.That(implementation, Does.Contain("Visible work may reclaim only dormant background slots"));
            Assert.That(implementation, Does.Contain("CancelObsoleteGenerations(DWORD currentGeneration)"));
            Assert.That(implementation, Does.Contain("candidate.Priority > WorkQueue[selected].Priority"));
            Assert.That(implementation, Does.Contain("AllWorkCompletedEvent"));
            Assert.That(implementation, Does.Contain("WaitForSingleObject(AllWorkCompletedEvent, timeoutMs)"));
            Assert.That(implementation, Does.Not.Contain("QueueHead"));
            Assert.That(implementation, Does.Not.Contain("QueueTail"));
            Assert.That(implementation, Does.Not.Contain("Sleep(1)"));
        });
    }

    [Test]
    public void Directory_listing_uses_bounded_checkpoints_and_a_retained_metadata_budget()
    {
        var root = FindRepositoryRoot();
        var navigation = File.ReadAllText(Path.Combine(root, "src", "fileswindow_navigation.cpp"));
        var listBox = File.ReadAllText(Path.Combine(root, "src", "filesbox_rendering.cpp"));
        var resourceIds = File.ReadAllText(Path.Combine(root, "src", "texts.rh2"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "lang", "texts.rc2"));

        // Large synthetic directories must encounter predictable cancellation
        // checkpoints while the native panel retains no more than its budget.
        const int batchSize = 256;
        const int retainedBudget = 100000;
        var checkpoints = Enumerable.Range(1, 1_000_000).Count(item => item % batchSize == 0);
        var retainedItems = 0;
        var metadataBudgetReached = false;
        foreach (var _ in Enumerable.Range(1, 1_000_000))
        {
            if (retainedItems >= retainedBudget - 1)
            {
                metadataBudgetReached = true;
                break;
            }

            retainedItems++;
        }

        Assert.Multiple(() =>
        {
            Assert.That(checkpoints, Is.EqualTo(1_000_000 / batchSize));
            Assert.That(retainedBudget, Is.GreaterThan(batchSize));
            Assert.That(metadataBudgetReached, Is.True);
            Assert.That(retainedItems, Is.EqualTo(retainedBudget - 1));
            Assert.That(navigation, Does.Contain("#define DIRECTORY_ENUMERATION_BATCH_SIZE 256"));
            Assert.That(navigation, Does.Contain("#define DIRECTORY_ENUMERATION_MAX_CACHED_ITEMS 100000"));
            Assert.That(navigation, Does.Contain("IsDirectoryEnumerationBatchBoundary(NumberOfItemsInCurDir)"));
            Assert.That(navigation, Does.Contain("Sleep(0);"));
            // Directory cancellation must retain the batch checkpoint while avoiding the 32-bit tick wrap.
            Assert.That(navigation, Does.Contain("atBatchBoundary || CMonotonicClock::HasElapsed(lastEscCheckTime, 200"));
            Assert.That(navigation, Does.Contain("UserWantsToCancelSafeWaitWindow()"));
            Assert.That(navigation, Does.Contain("Files->Count + Dirs->Count >= DIRECTORY_ENUMERATION_MAX_CACHED_ITEMS - 1"));
            Assert.That(navigation, Does.Contain("directoryEnumerationLimitReached = TRUE"));
            Assert.That(navigation, Does.Contain("StatusLine->SetText(LoadStr(IDS_DIRECTORYENUMERATIONLIMIT))"));
            Assert.That(listBox, Does.Contain("SetItemsCount2(count)"));
            Assert.That(listBox, Does.Contain("Parent->VisibleItemsArray.InvalidateArr()"));
            Assert.That(resourceIds, Does.Contain("IDS_DIRECTORYENUMERATIONLIMIT"));
            Assert.That(strings, Does.Contain("Directory listing was limited to 100,000 items"));
        });
    }

    [Test]
    public void Allocation_emergency_is_noninteractive_and_defers_recovery_to_the_ui_thread()
    {
        var root = FindRepositoryRoot();
        var allocatorHeader = File.ReadAllText(Path.Combine(root, "src", "common", "allochan.h"));
        var allocator = File.ReadAllText(Path.Combine(root, "src", "common", "allochan.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var mainWindowMessages = File.ReadAllText(Path.Combine(root, "src", "mainwnd_messages.cpp"));
        var constants = File.ReadAllText(Path.Combine(root, "src", "consts.h"));

        // OOM cannot be safely synthesized in the executable; these source-level
        // checks pin the non-interactive handler and its pre-registered UI handoff.
        Assert.Multiple(() =>
        {
            Assert.That(allocatorHeader, Does.Contain("SetAllocEmergencyNotificationWindow"));
            Assert.That(allocatorHeader, Does.Contain("IsAllocationEmergencyActive"));
            Assert.That(allocator, Does.Contain("const SIZE_T AllocEmergencyReserveSize = 64 * 1024"));
            Assert.That(allocator, Does.Contain("HeapAlloc(GetProcessHeap(), 0, AllocEmergencyReserveSize)"));
            Assert.That(allocator, Does.Contain("InterlockedCompareExchange(&AllocEmergencyActive, TRUE, FALSE)"));
            Assert.That(allocator, Does.Contain("InterlockedExchangePointer((PVOID volatile*)&AllocEmergencyReserve, NULL)"));
            Assert.That(allocator, Does.Contain("HeapFree(GetProcessHeap(), 0, reserve)"));
            Assert.That(allocator, Does.Contain("NotifyAllocationEmergency()"));
            Assert.That(allocator, Does.Contain("ActivateAllocationEmergency();"));
            Assert.That(allocator, Does.Contain("PostMessage(window, message, 0, 0)"));
            Assert.That(allocator, Does.Contain("return 0;"));
            Assert.That(allocator, Does.Not.Contain("MessageBox("));
            Assert.That(allocator, Does.Not.Contain("Sleep("));
            Assert.That(allocator, Does.Not.Contain("while ("));
            Assert.That(allocator, Does.Not.Contain("EnterCriticalSection("));
            Assert.That(allocator, Does.Not.Contain("TAllocEmergencyRecoveryCallback"));
            Assert.That(allocator, Does.Not.Contain("TerminateProcess("));
            Assert.That(journal, Does.Contain("void COperationJournal::PersistEmergencyShutdownState()"));
            Assert.That(journal, Does.Contain("OPERATION|memory-pressure"));
            Assert.That(journal, Does.Contain("FILE_FLAG_WRITE_THROUGH"));
            Assert.That(startup, Does.Contain("SetAllocEmergencyNotificationWindow(MainWindow->HWindow, WM_USER_ALLOCATION_EMERGENCY)"));
            Assert.That(constants, Does.Contain("#define WM_USER_ALLOCATION_EMERGENCY WM_APP + 416"));
            Assert.That(mainWindowMessages, Does.Contain("case WM_USER_ALLOCATION_EMERGENCY:"));
            Assert.That(mainWindowMessages, Does.Contain("COperationJournal::PersistEmergencyShutdownState();"));
            Assert.That(mainWindowMessages, Does.Contain("PostMessage(HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);"));
            Assert.That(operations, Does.Contain("if (IsAllocationEmergencyActive())"));
            Assert.That(operations, Does.Contain("COperationsQueue::AddOperation"));
        });
    }

    [Test]
    public void Background_producers_bound_non_durable_work_and_report_backpressure()
    {
        var root = FindRepositoryRoot();
        var findHeader = File.ReadAllText(Path.Combine(root, "src", "find.h"));
        var find = File.ReadAllText(Path.Combine(root, "src", "find.cpp"));
        var findUi = File.ReadAllText(Path.Combine(root, "src", "find_dialog_ui.cpp"));
        var brokerHeader = File.ReadAllText(Path.Combine(root, "src", "parserbroker.h"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var ftpHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "operats.h"));
        var ftpQueue = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "operats1.cpp"));
        var ftpDisk = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "operats5.cpp"));
        var snooperHeader = File.ReadAllText(Path.Combine(root, "src", "snooper.h"));
        var snooper = File.ReadAllText(Path.Combine(root, "src", "snooper.cpp"));
        var workerHeader = File.ReadAllText(Path.Combine(root, "src", "worker.h"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var operationDialog = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var cacheHeader = File.ReadAllText(Path.Combine(root, "src", "cache.h"));
        var cache = File.ReadAllText(Path.Combine(root, "src", "cache.cpp"));

        // Each producer uses a policy appropriate to its semantics: discovery work
        // may stop or fall back, while durable FTP intent is rejected atomically.
        Assert.Multiple(() =>
        {
            Assert.That(findHeader, Does.Contain("ResultLimitReached"));
            Assert.That(findHeader, Does.Contain("DirectoryStackHighWaterMark"));
            Assert.That(find, Does.Contain("#define FIND_MAX_RESULTS 100000"));
            Assert.That(find, Does.Contain("#define FIND_MAX_DEFERRED_DIRECTORIES 4096"));
            Assert.That(find, Does.Contain("Find result budget reached"));
            Assert.That(find, Does.Contain("dirStackCount < FIND_MAX_DEFERRED_DIRECTORIES"));
            Assert.That(find, Does.Contain("DirectoryStackFallbacks++"));
            Assert.That(findUi, Does.Contain("GrepData.ResultLimitReached = FALSE"));
            Assert.That(brokerHeader, Does.Contain("struct CParserBrokerQueueMetrics"));
            Assert.That(brokerHeader, Does.Contain("GetQueueMetrics()"));
            Assert.That(broker, Does.Contain("#define PARSER_BROKER_MAX_PENDING_REQUESTS 8"));
            Assert.That(broker, Does.Contain("pending > PARSER_BROKER_MAX_PENDING_REQUESTS"));
            Assert.That(broker, Does.Contain("InterlockedDecrement(&PendingRequests)"));
            Assert.That(ftpHeader, Does.Contain("#define FTP_OPERATION_QUEUE_LIMIT 100000"));
            Assert.That(ftpHeader, Does.Contain("#define FTP_DISK_WORK_QUEUE_LIMIT 512"));
            Assert.That(ftpHeader, Does.Contain("GetQueueMetrics"));
            Assert.That(ftpHeader, Does.Contain("GetWorkQueueMetrics"));
            Assert.That(ftpQueue, Does.Contain("Items.Count >= FTP_OPERATION_QUEUE_LIMIT"));
            Assert.That(ftpQueue, Does.Contain("bounded operation queue rejected expansion"));
            Assert.That(ftpDisk, Does.Contain("Work.Count >= FTP_DISK_WORK_QUEUE_LIMIT"));
            Assert.That(ftpDisk, Does.Contain("bounded disk queue rejected work"));
            Assert.That(snooperHeader, Does.Contain("struct CDirectoryRefreshMetrics"));
            Assert.That(snooper, Does.Contain("GetDirectoryRefreshMetrics()"));
            Assert.That(snooper, Does.Contain("SnooperRefreshWaits"));
            Assert.That(snooper, Does.Contain("at most one live snooper refresh outstanding"));
            Assert.That(workerHeader, Does.Contain("#define DISK_OPERATION_QUEUE_LIMIT 64"));
            Assert.That(workerHeader, Does.Contain("struct COperationsQueueMetrics"));
            Assert.That(workerHeader, Does.Contain("GetQueueMetrics()"));
            Assert.That(operations, Does.Contain("OperDlgs.Count >= DISK_OPERATION_QUEUE_LIMIT"));
            Assert.That(operations, Does.Contain("bounded operation queue rejected admission"));
            Assert.That(operationDialog, Does.Contain("!OperationsQueue.AddOperation"));
            Assert.That(operationDialog, Does.Contain("IDS_OPERATIONQUEUEFULL"));
            Assert.That(cacheHeader, Does.Contain("#define DELETE_MANAGER_QUEUE_LIMIT 4096"));
            Assert.That(cacheHeader, Does.Contain("struct CDeleteManagerQueueMetrics"));
            Assert.That(cache, Does.Contain("Data.Count >= DELETE_MANAGER_QUEUE_LIMIT"));
            Assert.That(cache, Does.Contain("Delete-manager cleanup queue is full"));
            Assert.That(cache, Does.Contain("CDeleteManager::GetQueueMetrics"));
        });
    }

    [Test]
    public void Plugin_callbacks_are_contained_and_the_failing_plugin_is_deferred_for_unload()
    {
        var root = FindRepositoryRoot();
        var header = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var loader = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var registry = File.ReadAllText(Path.Combine(root, "src", "plugins_interface.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(header, Does.Contain("#define PLUGIN_CALLBACK"));
            Assert.That(header, Does.Contain("HandlePluginCallbackException"));
            Assert.That(header, Does.Contain("PLUGIN_CALLBACK(Interface, \"Connect\""));
            Assert.That(header, Does.Contain("PLUGIN_CALLBACK(Interface, \"GetInterfaceForFS\""));
            Assert.That(loader, Does.Contain("PLUGIN_CALLBACK(Iface, \"ListCurrentPath\""));
            Assert.That(loader, Does.Contain("PLUGIN_CALLBACK(Interface, \"CloseFS\""));
            Assert.That(loader, Does.Contain("PLUGIN_CALLBACK(Plugin, \"ReleasePluginData\""));
            Assert.That(loader, Does.Contain("plugin->LoadOnStart = FALSE"));
            Assert.That(loader, Does.Contain("plugin->ShouldUnload = TRUE"));
            Assert.That(loader, Does.Contain("WM_USER_POSTCMDORUNLOADPLUGIN"));
            Assert.That(loader, Does.Not.Contain("TerminateProcess(GetCurrentProcess(), 1)"));
            Assert.That(registry, Does.Contain("CPlugins::GetPluginData(const void* pluginInterface)"));
        });
    }

    [Test]
    public void Plugin_interface_results_are_validated_before_host_encapsulation()
    {
        var root = FindRepositoryRoot();
        var loader = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var gui = File.ReadAllText(Path.Combine(root, "src", "plugins_archiver.cpp"));
        var pluginHeader = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var iconList = File.ReadAllText(Path.Combine(root, "src", "iconlist.cpp"));
        var iconListHeader = File.ReadAllText(Path.Combine(root, "src", "iconlist.h"));
        var display = File.ReadAllText(Path.Combine(root, "src", "fileswindow_display.cpp"));

        // An invalid callback result must be rejected before the host dispatches, copies, or deletes plug-in memory.
        Assert.Multiple(() =>
        {
            Assert.That(loader, Does.Contain("static BOOL ValidatePluginInterfaceResult"));
            Assert.That(loader, Does.Contain("static BOOL ValidatePluginContractString"));
            Assert.That(loader, Does.Contain("Do not inspect even the first byte until its committed page has been verified."));
            Assert.That(loader, Does.Contain("static BOOL ValidatePluginContractIcon"));
            Assert.That(loader, Does.Contain("GetIconInfo(icon, &iconInfo)"));
            Assert.That(loader, Does.Contain("destroyIcon != FALSE && destroyIcon != TRUE"));
            Assert.That(loader, Does.Contain("VirtualQuery(address, &memory, sizeof(memory))"));
            Assert.That(loader, Does.Contain("displayStringLimit = 4096"));
            Assert.That(loader, Does.Contain("configurationStringLimit = MAX_PATH - 1"));
            Assert.That(loader, Does.Contain("Invalid or overlong plug-in contract string"));
            Assert.That(loader, Does.Contain("__except (EXCEPTION_EXECUTE_HANDLER)"));
            Assert.That(loader, Does.Contain("Plug-in entry point returned an invalid interface pointer."));
            Assert.That(loader, Does.Contain("ValidatePluginInterfaceResult(archiver, \"archiver\")"));
            Assert.That(loader, Does.Contain("ValidatePluginInterfaceResult(thumbnailLoader, \"thumbnail loader\")"));
            Assert.That(loader, Does.Contain("pluginInterfacesValid &&"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::AddPanelArchiver(): invalid or overlong plug-in extension list!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::AddMenuItem(): invalid or overlong plug-in menu label!"));
            Assert.That(loader, Does.Contain("CSalamanderBuildMenu::AddSubmenuStart(): invalid or overlong plug-in submenu label!"));
            Assert.That(loader, Does.Contain("CPluginData::AddMenuItem(): invalid or overlong plug-in menu label!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::ForceRemoveViewer(): invalid or overlong plug-in mask!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::ForceRemovePanelArchiver(): invalid or overlong plug-in extension!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::SetChangeDriveMenuItem(): invalid or overlong plug-in title!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::SetThumbnailLoader(): invalid or overlong plug-in mask."));
            Assert.That(loader, Does.Contain("GetChangeDriveOrDisconnectItem(): invalid plug-in title or icon result!"));
            Assert.That(loader, Does.Contain("!ValidatePluginContractString(title, 4096, TRUE) || !ValidatePluginContractIcon(icon, destroyIcon)"));
            Assert.That(loader, Does.Contain("GetFSIcon(): invalid plug-in icon result!"));
            Assert.That(loader, Does.Contain("CSalamanderConnect::SetBitmapWithIcons(): invalid plug-in icon bitmap!"));
            Assert.That(loader, Does.Contain("bmp.bmHeight != 16"));
            Assert.That(pluginHeader, Does.Contain("hotTextsCount < 0 || hotTextsCount > 100"));
            // The cached terminator pointer validates the fixed SDK output buffer without rescanning it.
            Assert.That(pluginHeader, Does.Contain("textEnd == NULL"));
            Assert.That(pluginHeader, Does.Contain("hotTextLength > textLength - hotTextStart"));
            Assert.That(pluginHeader, Does.Contain("invalid plug-in output count, span, or unterminated text"));
            Assert.That(pluginHeader, Does.Contain("offset < 0 || offset > pathLen"));
            Assert.That(pluginHeader, Does.Contain("invalid plug-in text offset"));
            Assert.That(pluginHeader, Does.Contain("GetFullName(): unterminated plug-in output buffer"));
            Assert.That(pluginHeader, Does.Contain("GetFullFSPath(): unterminated plug-in output buffer"));
            Assert.That(pluginHeader, Does.Contain("GetPathForMainWindowTitle(): unterminated plug-in output buffer"));
            Assert.That(pluginHeader, Does.Contain("static BOOL ValidatePluginOutputString"));
            Assert.That(pluginHeader, Does.Contain("CompleteDirectoryLineHotPath(): unterminated plug-in output buffer"));
            Assert.That(pluginHeader, Does.Contain("GetNoItemsInPanelText(): unterminated plug-in output buffer"));
            Assert.That(pluginHeader, Does.Contain("maxSimplePluginIcons = 4096"));
            Assert.That(pluginHeader, Does.Contain("invalid plug-in image-list count"));
            Assert.That(iconList, Does.Contain("imageWidth > INT_MAX / imageColumns || imageHeight > INT_MAX / imageRows"));
            Assert.That(iconListHeader, Does.Contain("int GetImageCount() const"));
            Assert.That(display, Does.Contain("iconListIndex < 0 || iconListIndex >= iconList->GetImageCount()"));
            Assert.That(display, Does.Contain("Invalid plug-in simple-icon index"));
            Assert.That(pluginHeader, Does.Contain("TDirectArray<CIconList*> CreatedIconLists"));
            Assert.That(gui, Does.Contain("BOOL CSalamanderGUI::TakeCreatedIconList"));
            Assert.That(gui, Does.Contain("Compare addresses only; never dereference"));
            Assert.That(loader, Does.Contain("TakeCreatedIconList(iconList, &createdIconList)"));
            Assert.That(loader, Does.Contain("icon list was not created by this plug-in's host GUI facade"));
        });
    }

    [Test]
    public void Configuration_saves_stage_validate_and_atomically_select_a_generation()
    {
        var root = FindRepositoryRoot();
        var configuration = ReadConfigurationSources(root);
        var registryWork = File.ReadAllText(Path.Combine(root, "src", "regwork.cpp"));
        var plugins = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var pluginPersistence = File.ReadAllText(Path.Combine(root, "src", "plugins_interface.cpp"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));

        Assert.Multiple(() =>
        {
            Assert.That(configuration, Does.Contain("Configuration Generations"));
            Assert.That(configuration, Does.Contain("Active Generation"));
            Assert.That(configuration, Does.Contain("Transaction Complete"));
            Assert.That(configuration, Does.Contain("Transaction Checksum"));
            Assert.That(configuration, Does.Contain("CalculateConfigurationChecksum"));
            Assert.That(configuration, Does.Contain("IsCommittedConfigurationGeneration"));
            Assert.That(configuration, Does.Contain("BeginConfigurationTransaction"));
            Assert.That(configuration, Does.Contain("CommitConfigurationTransaction"));
            // The wrapper retains RegFlushKey durability while counting writes
            // for transactional-save fault injection.
            Assert.That(configuration, Does.Contain("FlushConfigurationRegistryKey(generationKey) == ERROR_SUCCESS"));
            Assert.That(registryWork, Does.Contain("LONG result = RegFlushKey(key)"));
            // A recursive registry deletion must close its child handle before
            // deleting the child name, or Windows can reject the deletion.
            Assert.That(registryWork, Does.Contain("subKeyScope.Close();"));
            Assert.That(registryWork.IndexOf("subKeyScope.Close();", StringComparison.Ordinal),
                        Is.GreaterThanOrEqualTo(0).And.LessThan(registryWork.IndexOf("RegDeleteKey(key, name)", StringComparison.Ordinal)));
            Assert.That(configuration, Does.Contain("SetValue(storeKey, CONFIGURATION_ACTIVE_GENERATION_REG"));
            Assert.That(configuration, Does.Contain("RetirePreviousConfigurationGenerationAfterSuccessfulStartup"));
            Assert.That(configuration, Does.Contain("OpenCommittedConfigurationGeneration(storeKey, fallbackGeneration"));
            Assert.That(startup, Does.Contain("SetConfigurationStoreRoot(SALAMANDER_ROOT_REG)"));
            Assert.That(startup, Does.Contain("SelectCommittedConfigurationGeneration()"));
            Assert.That(plugins, Does.Contain("MainWindow->SaveConfig();"));
            Assert.That(plugins, Does.Contain("MainWindow->SaveConfig(parent);"));
            Assert.That(plugins, Does.Not.Contain("CreateKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG"),
                        "Plug-in commits must not mutate the checksum-protected active generation.");
            // Lazy plug-ins, including FTP, must retain their private settings while a legacy profile becomes a generation.
            Assert.That(pluginPersistence, Does.Contain("CopyUnloadedPluginConfiguration"));
            Assert.That(pluginPersistence, Does.Contain("SHCopyKey(sourcePluginKey, NULL, destinationPluginKey, 0)"));
            Assert.That(pluginPersistence, Does.Contain("GetConfigurationStoreRoot()"));
            Assert.That(plugins, Does.Contain("Recover settings omitted by an earlier lazy-plug-in migration"));
            // The raw profile remains the recovery source for snapshots created before this preservation fix.
            Assert.That(configuration, Does.Contain("const char* GetConfigurationStoreRoot()"));
            Assert.That(architecture, Does.Contain("the root's `Active Generation` DWORD"));
        });
    }

    [Test]
    public void Configuration_profiles_are_schema_versioned_migrated_and_validated_before_loading()
    {
        var root = FindRepositoryRoot();
        var configuration = ReadConfigurationSources(root);
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));

        Assert.Multiple(() =>
        {
            Assert.That(configuration, Does.Contain("Configuration Schema Version"));
            Assert.That(configuration, Does.Contain("CONFIGURATION_SCHEMA_VERSION = 1"));
            Assert.That(configuration, Does.Contain("MigrateConfigurationSchema"));
            Assert.That(configuration, Does.Contain("ValidateCompleteConfigurationSchema"));
            Assert.That(configuration, Does.Contain("ValidateWindowConfigurationSchema"));
            Assert.That(configuration, Does.Contain("schemaVersion > CONFIGURATION_SCHEMA_VERSION"));
            Assert.That(configuration, Does.Contain("configVersion == THIS_CONFIG_VERSION"));
            Assert.That(configuration, Does.Contain("left < right && top < bottom"));
            Assert.That(configuration, Does.Contain("(separatedDrives & ~visibleDrives) == 0"));
            Assert.That(configuration, Does.Contain("ValidateRequiredDwordRange(viewerKey, \"Tabelator Size\", 1, 30)"));
            Assert.That(configuration, Does.Contain("GetConfigurationSchemaDiagnostic"));
            Assert.That(configuration, Does.Contain("The default profile will be used."));
            Assert.That(startup, Does.Contain("GetConfigurationSchemaDiagnostic()"));
            Assert.That(startup, Does.Contain("Open Salamander Configuration"));
            Assert.That(architecture, Does.Contain("Each transactional generation has a schema version."));
        });
    }

    [Test]
    public void Metadata_preservation_contract_records_losses_and_gates_move_source_deletion()
    {
        var root = FindRepositoryRoot();
        var workerHeader = File.ReadAllText(Path.Combine(root, "src", "worker.h"));
        var worker = File.ReadAllText(Path.Combine(root, "src", "worker.cpp"));
        var planner = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var copy = ReadOperationImplementationSources(root);
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var dialogs = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "lang", "texts.rc2"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));

        Assert.Multiple(() =>
        {
            Assert.That(workerHeader, Does.Contain("enum EMetadataPreservation"));
            Assert.That(workerHeader, Does.Contain("mpRequired"));
            Assert.That(workerHeader, Does.Contain("mpBestEffort"));
            Assert.That(workerHeader, Does.Contain("mpUnsupported"));
            Assert.That(workerHeader, Does.Contain("enum EMetadataTargetFileSystem"));
            Assert.That(workerHeader, Does.Contain("mtfsNtfs"));
            Assert.That(workerHeader, Does.Contain("mtfsRefs"));
            Assert.That(workerHeader, Does.Contain("mtfsFat"));
            Assert.That(workerHeader, Does.Contain("mtfsSmb"));
            Assert.That(workerHeader, Does.Contain("struct CMetadataLossRecord"));
            Assert.That(workerHeader, Does.Contain("CMetadataLossRecord MetadataLosses"));
            Assert.That(worker, Does.Contain("PlannedMetadataLosses = mmlNone"));
            Assert.That(planner, Does.Contain("GetMetadataTargetFileSystem(targetPath)"));
            Assert.That(planner, Does.Contain("script->PlannedMetadataLosses |= mmlAlternateDataStreams"));
            Assert.That(planner, Does.Contain("script->PlannedMetadataLosses |= mmlSecurity"));
            // The contract must consider the target filesystem as well as the
            // operation type before it decides which losses are acceptable.
            Assert.That(copy, Does.Contain("GetMetadataPreservationContract(EMetadataOperation operation,"));
            Assert.That(copy, Does.Contain("RecordMetadataLoss(dlgData, mmlAlternateDataStreams"));
            Assert.That(copy, Does.Contain("RecordMetadataLoss(dlgData, mmlLastWriteTime"));
            Assert.That(copy, Does.Contain("RecordMetadataLoss(dlgData, mmlSecurity"));
            Assert.That(copy, Does.Contain("RecordPlannedMetadataLosses(dlgData, script, op->SourceName, op->TargetName)"));
            Assert.That(copy, Does.Contain("ConfirmMetadataLossesBeforeSourceDeletion(hProgressDlg, dlgData, op->SourceName, op->TargetName)"));
            Assert.That(copy.IndexOf("ConfirmMetadataLossesBeforeSourceDeletion(hProgressDlg, dlgData, op->SourceName, op->TargetName)", StringComparison.Ordinal),
                        Is.LessThan(copy.IndexOf("DeleteFileWithVerifiedIdentity(op->SourceName", StringComparison.Ordinal)),
                        "A cross-volume move must ask about recorded metadata loss before deleting its source.");
            Assert.That(operations, Does.Contain("RecordPlannedMetadataLosses(dlgData, script, op->SourceName, NULL)"));
            Assert.That(operations, Does.Contain("ConfirmMetadataLossesBeforeSourceDeletion(hProgressDlg, dlgData, op->SourceName, NULL)"));
            Assert.That(dialogs, Does.Contain("case 13:"));
            Assert.That(dialogs, Does.Contain("MB_YESNO | MB_DEFBUTTON2"));
            Assert.That(strings, Does.Contain("IDS_METADATALOSS_BEFORESOURCEDELETE"));
            // Reparse policy precedes this contract, so the documented section
            // number advances while the preservation matrix remains required.
            Assert.That(architecture, Does.Contain("#### 5.2.2 Metadata preservation contract"));
            Assert.That(architecture, Does.Contain("Copy to NTFS"));
            Assert.That(architecture, Does.Contain("Copy to ReFS"));
            Assert.That(architecture, Does.Contain("Copy to FAT/FAT32/exFAT"));
            Assert.That(architecture, Does.Contain("Copy to SMB"));
            Assert.That(architecture, Does.Contain("only an explicit **Yes** allows source deletion"));
        });
    }

    [Test]
    public void Security_descriptor_copy_uses_the_privilege_aware_preservation_matrix()
    {
        var root = FindRepositoryRoot();
        var copy = ReadOperationImplementationSources(root);
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));

        Assert.Multiple(() =>
        {
            Assert.That(copy, Does.Contain("GetNamedSecurityInfoW(targetNameSecW, SE_FILE_OBJECT,"));
            Assert.That(copy, Does.Contain("PSID previousOwner = NULL"));
            Assert.That(copy, Does.Contain("GainWriteOwnerAccess()"));
            Assert.That(copy, Does.Contain("BOOL changingOwnerOrGroup"));
            Assert.That(copy, Does.Contain("else if (!changingOwnerOrGroup)"));
            Assert.That(copy, Does.Contain("ERROR_PRIVILEGE_NOT_HELD"));
            Assert.That(copy, Does.Contain("BOOL attemptedWrite = FALSE"));
            Assert.That(copy, Does.Contain("if (!attemptedWrite)"));
            Assert.That(copy, Does.Contain("SetDaclWithInheritance(targetNameSecW, srcDACL, inheritedDacl)"));
            Assert.That(copy, Does.Contain("IsSecurityDescriptorPreserved("));
            Assert.That(copy, Does.Contain("AreEqualExplicitAces("));
            Assert.That(copy, Does.Contain("INHERITED_ACE"));
            Assert.That(copy, Does.Contain("a NULL DACL grants full access and must never be approximated"));
            Assert.That(copy, Does.Contain("inaccessible source descriptor: report a best-effort metadata loss without touching the target"));
            Assert.That(copy, Does.Contain("inaccessible target descriptor: do not attempt a blind, partial repair"));
            Assert.That(copy, Does.Contain("unable to restore the target descriptor after a partial update"));
            Assert.That(copy, Does.Not.Contain("AddAccessAllowedAce(allowChPermDACL"),
                        "The former temporary permissive DACL fallback must not return.");
            Assert.That(copy.IndexOf("PSID previousOwner = NULL", StringComparison.Ordinal),
                        Is.LessThan(copy.IndexOf("    GainWriteOwnerAccess();", StringComparison.Ordinal)),
                        "The target descriptor must be snapshotted before privilege-dependent mutation.");
            Assert.That(architecture, Does.Contain("##### Security descriptor privilege matrix"));
            Assert.That(architecture, Does.Contain("`SeRestorePrivilege` enabled"));
            Assert.That(architecture, Does.Contain("explicit deny ACEs"));
            Assert.That(architecture, Does.Contain("Source or target descriptor inaccessible"));
            Assert.That(architecture, Does.Contain("FAT/FAT32/exFAT target"));
            Assert.That(architecture, Does.Contain("restore the target snapshot"));
        });
    }

    [Test]
    public void Release_diagnostic_ring_is_bounded_sanitized_and_reported_only_through_the_existing_consent_flow()
    {
        var root = FindRepositoryRoot();
        var diagnostics = File.ReadAllText(Path.Combine(root, "src", "release_diagnostics.cpp"));
        var diagnosticsHeader = File.ReadAllText(Path.Combine(root, "src", "release_diagnostics.h"));
        var worker = File.ReadAllText(Path.Combine(root, "src", "worker.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var copy = ReadOperationImplementationSources(root);
        var plugins = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var callStack = File.ReadAllText(Path.Combine(root, "src", "callstk.cpp"));
        var bugReport = File.ReadAllText(Path.Combine(root, "src", "bugreprt.cpp"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salamand.vcxproj"));
        var reporting = File.ReadAllText(Path.Combine(root, "reporting.md"));

        // Pin the bounded storage, safe publication, producer coverage, and
        // consent-gated local sidecar so release diagnostics cannot regress to debug-only traces.
        Assert.Multiple(() =>
        {
            Assert.That(diagnostics, Does.Contain("kReleaseDiagnosticCapacity = 128"));
            Assert.That(diagnostics, Does.Contain("CReleaseDiagnosticEntry ReleaseDiagnosticEntries[kReleaseDiagnosticCapacity]"));
            Assert.That(diagnostics, Does.Contain("InterlockedCompareExchange(&entry.Sequence, -sequence, published)"));
            Assert.That(diagnostics, Does.Contain("MemoryBarrier()"));
            Assert.That(diagnostics, Does.Contain("InterlockedCompareExchange(&entry.Sequence, sequence, -sequence)"));
            Assert.That(diagnostics, Does.Contain("CopySanitizedLabel"));
            Assert.That(diagnostics, Does.Contain("BOOL WriteDiagnosticText"));
            Assert.That(diagnostics, Does.Contain("*scan == '\\\\' || *scan == '/'"));
            Assert.That(diagnostics, Does.Contain("RecordReleaseDiagnosticOperationTransition"));
            Assert.That(diagnostics, Does.Contain("RecordReleaseDiagnosticWait"));
            Assert.That(diagnostics, Does.Contain("RecordReleaseDiagnosticRetry"));
            Assert.That(diagnostics, Does.Contain("RecordReleaseDiagnosticPluginIdentity"));
            Assert.That(diagnosticsHeader, Does.Contain("ExportReleaseDiagnosticRingBuffer"));
            Assert.That(worker, Does.Contain("RecordReleaseDiagnosticOperationTransition"));
            Assert.That(operations, Does.Contain("RecordReleaseDiagnosticWait(\"worker_startup\""));
            Assert.That(copy, Does.Contain("RecordReleaseDiagnosticRetry(\"copy_network_read\")"));
            Assert.That(plugins, Does.Contain("RecordReleaseDiagnosticPluginIdentity(DLLName)"));
            Assert.That(bugReport, Does.Contain("PrintReleaseDiagnosticRingBuffer(PrintLine, param)"));
            // The bounded StringCch copy prevents the diagnostic sidecar suffix from overrunning its path.
            Assert.That(callStack, Does.Contain("StringCchCopyA(extension"));
            Assert.That(callStack, Does.Contain("ExportReleaseDiagnosticRingBuffer(diagnosticPath)"));
            Assert.That(project, Does.Contain("..\\release_diagnostics.cpp"));
            Assert.That(project, Does.Contain("..\\release_diagnostics.h"));
            Assert.That(reporting, Does.Contain("Release diagnostic ring (`.OPS`)"));
            Assert.That(reporting, Does.Contain("View Report** provides the local export without sending it"));
        });
    }

    [Test]
    public void Network_operations_have_phase_deadlines_cancellation_and_failure_classification()
    {
        var root = FindRepositoryRoot();
        var checkver = File.ReadAllText(Path.Combine(root, "src", "plugins", "checkver", "internet.cpp"));
        var checkverHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "checkver", "checkver.h"));
        var checkverPlugin = File.ReadAllText(Path.Combine(root, "src", "plugins", "checkver", "checkver.cpp"));
        var checkverDialog = File.ReadAllText(Path.Combine(root, "src", "plugins", "checkver", "dialogs.cpp"));
        var ftp = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon1.cpp"));
        var ftpTls = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ssl.cpp"));
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(checkver, Does.Contain("kResolveAndConnectTimeoutMs = 15000"));
            Assert.That(checkver, Does.Contain("kSendTimeoutMs = 15000"));
            Assert.That(checkver, Does.Contain("kReceiveTimeoutMs = 30000"));
            Assert.That(checkver, Does.Contain("INTERNET_OPTION_CONNECT_TIMEOUT"));
            Assert.That(checkver, Does.Contain("INTERNET_OPTION_SEND_TIMEOUT"));
            Assert.That(checkver, Does.Contain("INTERNET_OPTION_DATA_RECEIVE_TIMEOUT"));
            Assert.That(checkverHeader, Does.Contain("void CancelDownloadThread()"));
            Assert.That(checkver, Does.Contain("InterlockedExchange(&DownloadCancelled, TRUE)"));
            Assert.That(checkver, Does.Contain("HINTERNET ActiveDownloadSession = NULL"));
            Assert.That(checkver, Does.Contain("HINTERNET ActiveDownloadRequest = NULL"));
            Assert.That(checkver, Does.Contain("InternetCloseHandle(request)"));
            Assert.That(checkver, Does.Contain("InternetCloseHandle(session)"));
            Assert.That(checkverPlugin, Does.Contain("CancelDownloadThread(); // release WinINet waits"));
            Assert.That(checkverDialog, Does.Contain("CancelDownloadThread(); // close the active network handle"));
            Assert.That(checkver, Does.Contain("ERROR_INTERNET_TIMEOUT"));
            Assert.That(checkver, Does.Contain("ERROR_INTERNET_LOGIN_FAILURE"));
            Assert.That(checkver, Does.Contain("Protocol or TLS failure"));
            Assert.That(ftp, Does.Contain("int resolveTimeout"));
            Assert.That(ftp, Does.Contain("int connectTimeout"));
            Assert.That(ftp, Does.Contain("int protocolTimeout"));
            Assert.That(ftp, Does.Contain("IDS_GETIPTIMEOUT"));
            Assert.That(ftp, Does.Contain("IDS_OPENCONTIMEOUT"));
            Assert.That(ftp, Does.Contain("CloseSocket(NULL)"));
            Assert.That(ftpTls, Does.Contain("DWORD tlsHandshakeTimeout = Config.GetServerRepliesTimeout() * 1000"));
            Assert.That(ftpTls, Does.Contain("SO_RCVTIMEO"));
            Assert.That(ftpTls, Does.Contain("SO_SNDTIMEO"));
            Assert.That(ftpTls, Does.Contain("TLS handshake deadline expired."));
            Assert.That(upload, Does.Contain("WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000)"));
            Assert.That(upload, Does.Contain("HINTERNET ActiveUploadSession = NULL"));
            Assert.That(upload, Does.Contain("RegisterActiveUploadSession(session, uploadParams)"));
            Assert.That(upload, Does.Contain("InterlockedExchange(&params->Cancelled, TRUE)"));
            Assert.That(upload, Does.Contain("WinHttpCloseHandle(request)"));
            Assert.That(upload, Does.Contain("WinHttpCloseHandle(session)"));
            Assert.That(upload, Does.Contain("Network timeout"));
            Assert.That(upload, Does.Contain("Authentication failure"));
            Assert.That(upload, Does.Contain("Protocol or TLS failure"));
        });
    }

    [Test]
    public void Ftp_downloads_stage_identity_validate_resume_and_publish_only_after_a_durable_commit()
    {
        var root = FindRepositoryRoot();
        var control = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon5.cpp"));
        var dataConnection = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "datacon1.cpp"));
        var disk = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "operats5.cpp"));

        // Network loss must leave an explicitly incomplete side file; only a fully verified transfer can replace the cache entry.
        Assert.Multiple(() =>
        {
            Assert.That(control, Does.Contain("FtpIncompleteSuffix[] = \".ftp-incomplete\""));
            Assert.That(control, Does.Contain("CFTPTransactionalDownloadMetadata"));
            Assert.That(control, Does.Contain("RemoteSize"));
            Assert.That(control, Does.Contain("RemoteDateAndTimeValid"));
            Assert.That(control, Does.Contain("WriteTransactionalDownloadMetadata"));
            Assert.That(control, Does.Contain("FILE_FLAG_WRITE_THROUGH"));
            Assert.That(control, Does.Contain("memcmp(&actual, &expected, sizeof(actual)) == 0"));
            Assert.That(control, Does.Contain("size.QuadPart < 0 || (unsigned __int64)size.QuadPart > expected.RemoteSize"));
            Assert.That(control, Does.Contain("!asciiMode && fileSizeInBytes != CQuadWord(-1, -1)"));
            Assert.That(control, Does.Contain("ftpcmdRestartTransfer"));
            Assert.That(control, Does.Contain("FTP_D1_PARTIALSUCCESS"));
            Assert.That(control, Does.Contain("CommitTransactionalDownload(stagedTargetName, tgtFileName, metadataName"));
            Assert.That(control, Does.Contain("FlushFileBuffers(stagedFile)"));
            Assert.That(control, Does.Contain("MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH"));
            Assert.That(dataConnection, Does.Contain("TgtDiskFileResumeOffset = resumeOffset"));
            Assert.That(dataConnection, Does.Contain("DiskWork.AppendToFile = TgtDiskFile == NULL && TgtDiskFileAppend"));
            Assert.That(disk, Does.Contain("localWork.AppendToFile ? OPEN_ALWAYS : CREATE_ALWAYS"));
            Assert.That(disk, Does.Contain("SetFilePointerEx(f, offset, NULL, FILE_BEGIN)"));
        });
    }

    [Test]
    public void Ftp_certificate_exceptions_are_endpoint_bound_expiring_and_pinned()
    {
        var root = FindRepositoryRoot();
        var tlsHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ssl.h"));
        var tls = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ssl.cpp"));
        var sockets = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "sockets.cpp"));
        // Normalize checkout line endings to LF so multiline source contracts verify code structure rather than Git autocrlf state.
        var dataConnection = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "datacon1.cpp"))
            .Replace("\r\n", "\n", StringComparison.Ordinal)
            .Replace("\r", "\n", StringComparison.Ordinal);
        var control = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ctrlcon1.cpp"));
        var operationDialog = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "dialogs5.cpp"));
        var configuration = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ftp.cpp"));
        var dialogResource = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "lang", "lang.rc"));

        // Pin every trust dimension so chain failures can be explicitly accepted without making the decision reusable.
        Assert.Multiple(() =>
        {
            Assert.That(tlsHeader, Does.Contain("enum CCertificateExceptionScope"));
            Assert.That(tlsHeader, Does.Contain("cesSession"));
            Assert.That(tlsHeader, Does.Contain("cesPersistent"));
            Assert.That(tlsHeader, Does.Contain("MatchesEndpoint(LPCSTR host, unsigned short port) const"));
            Assert.That(tlsHeader, Does.Contain("RememberException(CCertificateExceptionScope scope) const"));
            Assert.That(tlsHeader, Does.Contain("IsCurrentException() const"));
            Assert.That(tls, Does.Contain("#define FTP_CERTIFICATE_EXCEPTION_LIMIT 64"));
            Assert.That(tls, Does.Contain("#define FTP_CERTIFICATE_SESSION_EXCEPTION_HOURS 8"));
            Assert.That(tls, Does.Contain("#define FTP_CERTIFICATE_PERSISTENT_EXCEPTION_DAYS 30"));
            Assert.That(tls, Does.Contain("FTP_CERTIFICATE_EXCEPTION_SPKI"));
            Assert.That(tls, Does.Contain("FTP_CERTIFICATE_EXCEPTION_CERTIFICATE"));
            Assert.That(tls, Does.Contain("CryptHashPublicKeyInfo"));
            Assert.That(tls, Does.Contain("CryptHashCertificate(0, CALG_SHA_256"));
            Assert.That(tls, Does.Contain("_stricmp(Records[i].Host, host) == 0 && Records[i].Port == port"));
            Assert.That(tls, Does.Contain("memcmp(Records[i].SPKIFingerprint, spkiFingerprint"));
            Assert.That(tls, Does.Contain("memcmp(Records[i].CertificateFingerprint, certificateFingerprint"));
            Assert.That(tls, Does.Contain("IsExpired(Records[i].ExpiresAt)"));
            // Compact-from-end must keep i inside the live prefix; Records[-1] overlays Config.ConParamsCS.
            Assert.That(tls, Does.Contain("for (int i = Count - 1; i >= 0 && i < Count;)"));
            Assert.That(tls, Does.Contain("void ClampCount()"));
            Assert.That(tls, Does.Contain("void RemoveAt(int i)"));
            Assert.That(tls, Does.Contain("if (i < 0 || i >= Count)"));
            Assert.That(tls, Does.Contain("Records[-1] occupies the bytes immediately before this store, including Config.ConParamsCS."));
            Assert.That(tls, Does.Contain("record.Scope == cesPersistent"));
            Assert.That(tls, Does.Contain("pCertificate->IsVerified() || pCertificate->IsCurrentException()"));
            Assert.That(tls, Does.Contain("IsDataConnection || pCertificate->MatchesEndpoint(HostAddress, HostPort)"));
            Assert.That(tls, Does.Contain("CertificateExceptions.IsAccepted(HostAddress, HostPort, der, derLength, &endpointKnown)"));
            Assert.That(tls, Does.Contain("else if (endpointKnown)"));
            // Passive data sockets must retain the control DNS identity; the passive port is not a certificate endpoint.
            Assert.That(sockets, Does.Contain("BOOL CSocket::CopyTlsTargetFrom(CSocket* source)"));
            Assert.That(dataConnection, Does.Contain("CopyTlsTargetFrom(SSLConForReuse)"));
            // A server may return 150 before Windows posts FD_CONNECT, so the data-channel TLS handshake must wait for that connect event.
            Assert.That(dataConnection, Does.Contain("UsePassiveMode && ReceivedConnected && EncryptConnection && SSLConn == NULL"));
            Assert.That(dataConnection, Does.Contain("if (EncryptConnection && SSLConn == NULL)\n                EncryptPassiveDataCon();"));
            Assert.That(control, Does.Contain("unverifiedCert->RememberException(certificateDialog.RememberException() ? cesPersistent : cesSession)"));
            Assert.That(operationDialog, Does.Contain("unverifiedCertificate->RememberException(certificateDialog.RememberException() ? cesPersistent : cesSession)"));
            Assert.That(configuration, Does.Contain("LoadCertificateExceptions(regKey, registry)"));
            Assert.That(configuration, Does.Contain("SaveCertificateExceptions(regKey, registry)"));
            Assert.That(dialogResource, Does.Contain("Remember this exception for 30 days"));
        });
    }

    [Test]
    public void Ftp_data_channel_tls_handshake_is_readiness_driven()
    {
        var root = FindRepositoryRoot();
        var tls = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "ssl.cpp"));
        var sockets = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "sockets.h"));
        var download = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "datacon1.cpp"));
        var upload = File.ReadAllText(Path.Combine(root, "src", "plugins", "ftp", "datacon2.cpp"));

        // Pin the nonblocking ownership and readiness gates so later FTPS fixes cannot reintroduce per-file socket-thread waits.
        Assert.Multiple(() =>
        {
            Assert.That(sockets, Does.Contain("SSL* SSLHandshakeConn"));
            Assert.That(sockets, Does.Contain("BeginAsyncEncryptSocket"));
            Assert.That(sockets, Does.Contain("ContinueAsyncEncryptSocket"));
            Assert.That(tls, Does.Contain("static CTlsHandshakeResult AdvanceHandshake"));
            Assert.That(tls, Does.Contain("if (error == WSAEWOULDBLOCK)"));
            Assert.That(download, Does.Contain("ContinueAsyncEncryptSocket(LogUID"));
            Assert.That(upload, Does.Contain("ContinueAsyncEncryptSocket(LogUID"));
            Assert.That(download, Does.Not.Contain("!EncryptSocket(LogUID"));
            Assert.That(upload, Does.Not.Contain("!EncryptSocket(LogUID"));
            Assert.That(upload, Does.Contain("!IsAsyncEncryptingSocket() &&"));
        });
    }

    [Test]
    public void Bundled_7zip_uses_26_02_and_preserves_upgrade_compatibility_contract()
    {
        var root = FindRepositoryRoot();
        var version = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "7za", "c", "7zVersion.h"));
        var project = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "vcxproj", "7ZA", "7za.dll.vcxproj"));
        var wrapper = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "7za", "cpp", "7zip", "UI", "7zwrapper", "7zwrapper.cpp"));
        var threading = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "7za", "c", "LzFindMt.c"));
        var extract = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "7za", "cpp", "7zip", "Archive", "7z", "7zExtract.cpp"));
        var openCallback = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "open.h"));
        var extractCallback = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "extract.h"));
        var updateCallback = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "update.h"));
        var retryableStreams = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "FStreams.h"));
        var pluginProject = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "vcxproj", "7zip.vcxproj"));
        var record = File.ReadAllText(Path.Combine(root, "src", "plugins", "7zip", "doc", "upgrade-26.02.md"));
        var corpus = File.ReadAllText(Path.Combine(root, "tools", "test-7zip-compatibility.ps1"));
        var runner = File.ReadAllText(Path.Combine(root, "scripts", "runtests.ps1"));

        // Pin the upgraded parser and the compatibility gate so later vendor refreshes cannot silently drop it.
        Assert.Multiple(() =>
        {
            Assert.That(version, Does.Contain("#define MY_VERSION_NUMBERS \"26.02\""));
            Assert.That(version, Does.Contain("#define MY_DATE \"2026-06-25\""));
            Assert.That(project, Does.Contain("Lzma2DecMt.c"));
            Assert.That(project, Does.Contain("ZstdDec.c"));
            Assert.That(project, Does.Contain("LzfseDecoder.cpp"));
            Assert.That(wrapper, Does.Contain("Z7_IFACES_IMP_UNK_2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)"));
            Assert.That(wrapper, Does.Contain("Create_ALWAYS(archiveName)"));
            Assert.That(threading, Does.Contain("RunThreadWithCallStackObject"));
            Assert.That(extract, Does.Contain("FlushCorrupted"));
            Assert.That(openCallback, Does.Contain("Z7_IFACES_IMP_UNK_3(IArchiveOpenCallback"));
            Assert.That(extractCallback, Does.Contain("Z7_IFACES_IMP_UNK_2(IArchiveExtractCallback"));
            Assert.That(updateCallback, Does.Contain("Z7_IFACE_COM7_IMP(IArchiveUpdateCallback)"));
            Assert.That(retryableStreams, Does.Contain("class CRetryableOutFileStream : public IOutStream"));
            Assert.That(retryableStreams, Does.Contain("CMyComPtr<IOutStream> Stream"));
            Assert.That(pluginProject, Does.Contain("filename.cpp"));
            Assert.That(pluginProject, Does.Contain("timeutils.cpp"));
            Assert.That(record, Does.Contain("C7502DD4557481F52CCF1B3E680329F1FDD207E79A25544AFEB3106325474944"));
            Assert.That(record, Does.Contain("fuzz regressions"));
            Assert.That(record, Does.Contain("extraction snapshots"));
            Assert.That(corpus, Does.Contain("CompressFilesDelegate"));
            Assert.That(corpus, Does.Contain("Get-ExtractionManifest"));
            Assert.That(corpus, Does.Contain("header-bitflip"));
            Assert.That(corpus, Does.Contain("payload-bitflip"));
            Assert.That(corpus, Does.Contain("footer-bitflip"));
            Assert.That(runner, Does.Contain("7-Zip wrapper/oracle compatibility corpus"));
        });
    }

    [Test]
    public void Bundled_sqlite_uses_a_verified_current_amalgamation_and_exercises_the_owned_database_recovery_contract()
    {
        var root = FindRepositoryRoot();
        var amalgamation = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "sqlite", "sqlite3.c"));
        var header = File.ReadAllText(Path.Combine(root, "src", "plugins", "shared", "sqlite", "sqlite3.h"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "sqlite", "sqlite_base.props"));
        var policy = File.ReadAllText(Path.Combine(root, "src", "common", "dep", "sqlite", "readme.txt"));
        var externalReader = File.ReadAllText(Path.Combine(root, "src", "shiconov.cpp"));
        var recoveryTest = File.ReadAllText(Path.Combine(root, "tools", "test-sqlite-recovery.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));

        // Keep the vendored binary, ownership boundary, and executable recovery probe aligned after future upgrades.
        Assert.Multiple(() =>
        {
            Assert.That(amalgamation, Does.Contain("#define SQLITE_VERSION        \"3.53.4\""));
            Assert.That(amalgamation, Does.Contain("bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc"));
            Assert.That(header, Does.Contain("#define SQLITE_VERSION        \"3.53.4\""));
            Assert.That(header, Does.Contain("bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc"));
            Assert.That(project, Does.Contain("SQLITE_DQS=0"));
            Assert.That(project, Does.Contain("SQLITE_ENABLE_API_ARMOR"));
            Assert.That(project, Does.Contain("SQLITE_DEFAULT_SYNCHRONOUS=2"));
            Assert.That(project, Does.Contain("SQLITE_DEFAULT_WAL_SYNCHRONOUS=2"));
            Assert.That(project, Does.Contain("SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=1000"));
            Assert.That(policy, Does.Contain("628a44cfe82c66aed1ccbbe85a562d2e33ebe64b3288981ed76285612227934e"));
            Assert.That(policy, Does.Contain("PRAGMA integrity_check"));
            Assert.That(policy, Does.Contain("BEGIN IMMEDIATE"));
            // The policy sentence spans lines; tolerate CRLF without weakening
            // the exact prohibition guarded by this contract test.
            Assert.That(policy, Does.Match("FileManager must not run\\r?\\nintegrity checks, checkpoints, migrations, or recovery writes against it\\."));
            Assert.That(externalReader, Does.Contain("SQLITE_OPEN_READONLY"));
            Assert.That(externalReader, Does.Contain("never a FileManager recovery or write target"));
            Assert.That(recoveryTest, Does.Contain("PRAGMA journal_mode=WAL"));
            Assert.That(recoveryTest, Does.Contain("BEGIN IMMEDIATE"));
            Assert.That(recoveryTest, Does.Contain("PRAGMA integrity_check"));
            Assert.That(recoveryTest, Does.Contain("sqlite3_compileoption_used"));
            Assert.That(recoveryTest, Does.Contain("CorruptPage"));
            Assert.That(workflow, Does.Contain("test-sqlite-recovery.ps1"));
        });
    }

    [Test]
    public void Bundled_cmark_gfm_uses_the_verified_release_and_the_viewer_rejects_unsafe_or_unbounded_rendering()
    {
        var root = FindRepositoryRoot();
        var version = File.ReadAllText(Path.Combine(root, "src", "plugins", "ieviewer", "cmark-gfm", "build", "src", "cmark-gfm_version.h"));
        var vendorRecord = File.ReadAllText(Path.Combine(root, "src", "plugins", "ieviewer", "cmark-gfm", "VENDOR.md"));
        var renderer = File.ReadAllText(Path.Combine(root, "src", "plugins", "ieviewer", "markdown_rendering.cpp"));
        var rendererHeader = File.ReadAllText(Path.Combine(root, "src", "plugins", "ieviewer", "markdown_rendering.h"));
        var viewer = File.ReadAllText(Path.Combine(root, "src", "plugins", "ieviewer", "markdown.cpp"));
        var probe = File.ReadAllText(Path.Combine(root, "tools", "cmark_gfm_hardening_probe.cpp"));
        var harness = File.ReadAllText(Path.Combine(root, "tools", "test-cmark-gfm-hardening.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var soakWorkflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "nightly-parser-fuzz.yml"));

        // Pin the vendor identity, safe default, bounded tree/output seam, and CI fuzz gate together.
        Assert.Multiple(() =>
        {
            Assert.That(version, Does.Contain("CMARK_GFM_VERSION_STRING \"0.29.0.gfm.13\""));
            Assert.That(vendorRecord, Does.Contain("5abc61798ebd9de5660bc076443c07abad2b8d15dbc11094a3a79644b8ad243a"));
            Assert.That(vendorRecord, Does.Contain("CMARK_OPT_UNSAFE"));
            Assert.That(rendererHeader, Does.Contain("MarkdownMaximumInputBytes = 1024 * 1024"));
            Assert.That(rendererHeader, Does.Contain("MarkdownMaximumNodeCount = 100000"));
            Assert.That(rendererHeader, Does.Contain("MarkdownMaximumTreeDepth = 128"));
            Assert.That(rendererHeader, Does.Contain("MarkdownMaximumOutputBytes = 4 * 1024 * 1024"));
            Assert.That(renderer, Does.Contain("CMARK_OPT_SAFE | CMARK_OPT_VALIDATE_UTF8"));
            Assert.That(renderer, Does.Not.Match("const int options[^;]*CMARK_OPT_UNSAFE"));
            Assert.That(renderer, Does.Contain("cmark_parser_new(options)"));
            Assert.That(renderer, Does.Contain("cmark_render_html(document, options, cmark_parser_get_syntax_extensions(parser))"));
            Assert.That(renderer, Does.Contain("CheckTreeBudget"));
            Assert.That(viewer, Does.Contain("mmeAllViewerExtensions"));
            Assert.That(viewer, Does.Contain("bytes > MarkdownMaximumInputBytes - markdown.size()"));
            Assert.That(viewer, Does.Contain("RenderMarkdownToSafeHtml"));
            Assert.That(probe, Does.Contain("RunExtensionCombinationFuzz"));
            Assert.That(probe, Does.Contain("mrrOutputTooLarge"));
            Assert.That(File.Exists(Path.Combine(root, "tests", "cmark-gfm", "snapshots", "basic.html")), Is.True);
            Assert.That(File.Exists(Path.Combine(root, "tests", "cmark-gfm", "snapshots", "strikethrough.html")), Is.True);
            Assert.That(harness, Does.Contain("markdown_rendering.cpp"));
            Assert.That(harness, Does.Contain("cmark_gfm_hardening_probe.cpp"));
            Assert.That(harness, Does.Contain("[int]$Iterations = 1"));
            Assert.That(workflow, Does.Contain("test-cmark-gfm-hardening.ps1"));
            Assert.That(soakWorkflow, Does.Contain("test-cmark-gfm-hardening.ps1 -Iterations 250"));
        });
    }

    // Refactors may move an operation seam, but every source below must remain compiled into the product.
    private static string ReadOperationImplementationSources(string root) =>
        ReadCompiledNativeSources(root,
            "async_copy.cpp", "copy_commit.cpp", "copy_loop.cpp", "metadata_preservation.cpp",
            "security_helpers.cpp", "file_attributes.cpp", "ads_operations.cpp", "recycle_bin_delete.cpp");

    // Application shutdown is intentionally split from startup so lifecycle contracts span both compiled units.
    private static string ReadApplicationLifecycleSources(string root) =>
        ReadCompiledNativeSources(root, "app_entry.cpp", "app_shutdown.cpp");

    // Configuration persistence and import are separate implementation seams with one external contract.
    private static string ReadConfigurationSources(string root) =>
        ReadCompiledNativeSources(root, "mainwnd_config.cpp", "config_store.cpp", "config_import.cpp");

    // Path history was separated from general path handling without changing the UTF-16 API surface.
    private static string ReadPathHistorySources(string root) =>
        ReadCompiledNativeSources(root, "path_utils.cpp", "path_history.cpp");

    private static string ReadCompiledNativeSources(string root, params string[] relativePaths)
    {
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salamand.vcxproj"));
        return string.Join(Environment.NewLine, relativePaths.Select(relativePath =>
        {
            var projectPath = relativePath.Replace('/', '\\');
            Assert.That(project, Does.Contain($"<ClCompile Include=\"..\\{projectPath}\">"),
                $"{relativePath} must remain compiled by salamand.vcxproj.");
            return File.ReadAllText(Path.Combine(root, "src", relativePath));
        }));
    }

    private static string FindRepositoryRoot()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "architecture.md")) &&
                Directory.Exists(Path.Combine(directory.FullName, "src")))
                return directory.FullName;
        }

        throw new DirectoryNotFoundException("Could not find the FileManager repository root.");
    }
}
