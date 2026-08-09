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
