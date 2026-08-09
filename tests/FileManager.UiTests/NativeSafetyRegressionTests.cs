using NUnit.Framework;
using System.Text.RegularExpressions;

namespace FileManager.UiTests;

// These checks intentionally run without the executable.  A deterministic
// name-swap race cannot be coordinated through the legacy UI, so this keeps
// the native handle-binding contract from being silently removed while the
// executable-level file-operation suite covers normal delete and overwrite
// behavior.
public sealed class NativeSafetyRegressionTests
{
    [Test]
    public void Destructive_operations_keep_the_handle_identity_guard()
    {
        var root = FindRepositoryRoot();
        var helper = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(helper, Does.Contain("FILE_FLAG_OPEN_REPARSE_POINT"));
            Assert.That(helper, Does.Contain("GetFileInformationByHandle"));
            Assert.That(helper, Does.Contain("GetFinalPathNameByHandleW"));
            Assert.That(helper, Does.Contain("SetFileInformationByHandle(handle, FileDispositionInfo"));
            Assert.That(operations, Does.Contain("CaptureOperationFileIdentities(op, &identityError)"));
            Assert.That(copy, Does.Contain("VerifyFileIdentity(targetName, expectedTargetIdentity, error)"));
            Assert.That(copy, Does.Contain("DeleteFileWithVerifiedIdentity(name, operation->SourceIdentity, &err)"));
        });
    }

    [Test]
    public void Copy_engine_uses_unambiguous_64_bit_file_size_and_seek_wrappers()
    {
        var root = FindRepositoryRoot();
        var declarations = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var wrappers = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(declarations, Does.Contain("struct CFileOffsetResult"));
            Assert.That(declarations, Does.Contain("CFileOffsetResult SalGetFileSizeEx"));
            Assert.That(declarations, Does.Contain("CFileOffsetResult SalSetFilePointerEx"));
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

    [Test]
    public void Crash_report_uploads_use_certificate_validated_https_with_explicit_consent()
    {
        var root = FindRepositoryRoot();
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var project = File.ReadAllText(Path.Combine(root, "src", "vcxproj", "salmon", "salmon_base.props"));
        var dialog = File.ReadAllText(Path.Combine(root, "src", "lang", "lang.rc"));
        var reporting = File.ReadAllText(Path.Combine(root, "reporting.md"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 12. Replace the custom crash uploader with HTTPS WinHTTP — Implemented"));
        });
    }

    [Test]
    public void Dynamic_library_loads_use_restricted_search_paths()
    {
        var root = FindRepositoryRoot();
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var releaseHandles = File.ReadAllText(Path.Combine(root, "src", "common", "handles.h"));
        var debugHandles = File.ReadAllText(Path.Combine(root, "src", "common", "handles.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        Assert.Multiple(() =>
        {
            Assert.That(startup, Does.Contain("InitializeDllSearchPaths()"));
            Assert.That(startup, Does.Contain("SetDefaultDllDirectories"));
            Assert.That(startup, Does.Contain("AddDllDirectory"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_APPLICATION_DIR"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_SYSTEM32"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_USER_DIRS"));
            Assert.That(releaseHandles, Does.Contain("GetFullPathNameW"));
            Assert.That(releaseHandles, Does.Contain("::LoadLibraryExW(fullPath, NULL, loadFlags)"));
            Assert.That(debugHandles, Does.Contain("::LoadLibraryExW(fullPath, NULL, loadFlags)"));
            Assert.That(releaseHandles, Does.Contain("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"));
            Assert.That(debugHandles, Does.Contain("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR"));
            Assert.That(Regex.Matches(releaseHandles, @"(?m)^(?!\s*//).*?\bLoadLibraryW\s*\(").Count, Is.Zero,
                        "Release builds must not restore unrestricted LoadLibraryW calls.");
            Assert.That(Regex.Matches(debugHandles, @"(?m)^(?!\s*//).*?\bLoadLibraryW\s*\(").Count, Is.Zero,
                        "Debug builds must not restore unrestricted LoadLibraryW calls.");
            Assert.That(refactoring, Does.Contain("### 19. Constrain DLL search paths — Implemented"));
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
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(client, Does.Contain("EnterCriticalSection(&Lock)"));
            Assert.That(client, Does.Contain("for (int attempt = 0; attempt != 2; ++attempt)"));
            Assert.That(client, Does.Contain("responseHeader.PayloadLength > responseCapacity"));
            Assert.That(broker, Does.Contain("SHCreateItemFromParsingName"));
            Assert.That(broker, Does.Contain("requestHeader.PayloadLength > PARSER_BROKER_MAX_PAYLOAD"));
            Assert.That(thumbnails, Does.Contain("ParserBroker.LoadThumbnail"));
            Assert.That(thumbnails, Does.Not.Contain("(*loader)->LoadThumbnail"));
            Assert.That(archives, Does.Contain("ParserBroker.QueryArchiveMetadata"));
            Assert.That(installer, Does.Contain("salbroker.exe"));
            Assert.That(refactoring, Does.Contain("### 21. Move risky parsers and previewers out of process — Implemented"));
        });
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
