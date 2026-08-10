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
    public void Unchecked_string_calls_are_ratchet_gated_and_external_boundaries_report_capacity_and_encoding_failures()
    {
        var root = FindRepositoryRoot();
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-unsafe-string-calls.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "common", "strutils.cpp"));
        var declarations = File.ReadAllText(Path.Combine(root, "src", "common", "strutils.h"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 37. Ratchet unchecked string-copy and formatting calls — Implemented"));
        });
    }

    [Test]
    public void New_fixed_max_path_buffers_are_rejected_by_the_changed_lines_ci_ratchet()
    {
        var root = FindRepositoryRoot();
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-max-path-buffers.ps1"));
        var exemptions = File.ReadAllText(Path.Combine(root, "tools", "max-path-buffer-exemptions.md"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 36. Ban new fixed `MAX_PATH` buffers — Implemented"));
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
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(registry, Does.Contain("CreateFileA(ConfigurationWriteFaultReport.c_str()"));
            Assert.That(minidump, Does.Contain("CopyBoundedExternalText"));
            Assert.That(minidump, Does.Contain("memchr(source, 0, sourceCapacity)"));
            Assert.That(minidump, Does.Contain("kMaximumCrashReportPathLength = 32767"));
            Assert.That(minidump, Does.Contain("std::vector<char> buffer(capacity)"));
            Assert.That(minidump, Does.Contain("std::string dumpFileName"));
            Assert.That(minidump, Does.Contain("ERROR_INVALID_DATA"));
            Assert.That(minidump, Does.Not.Contain("char szFileName[MAX_PATH]"));
            Assert.That(minidump, Does.Not.Contain("static char findPath[MAX_PATH]"));
            Assert.That(salmonHeader, Does.Contain("int targetPathSize"));
            Assert.That(salmonHeader, Does.Contain("int shortNameSize"));
            Assert.That(refactoring, Does.Contain("### 38. Replace fixed buffers at trust boundaries first — Implemented"));
        });
    }

    [Test]
    public void External_size_fields_use_checked_arithmetic_before_allocation_or_io()
    {
        var root = FindRepositoryRoot();
        var arithmetic = File.ReadAllText(Path.Combine(root, "src", "common", "checked_arithmetic.h"));
        var upload = File.ReadAllText(Path.Combine(root, "src", "salmon", "upload.cpp"));
        var client = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker", "salbroker.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 39. Use checked arithmetic for sizes, offsets, and allocations — Implemented"));
        });
    }

    [Test]
    public void Transactional_copy_results_preserve_phase_error_paths_retryability_and_partial_effects_for_legacy_dialogs()
    {
        var root = FindRepositoryRoot();
        var result = File.ReadAllText(Path.Combine(root, "src", "operation_result.h"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        // The native worker keeps the complete outcome until this adapter feeds an
        // unchanged BOOL/error pair to the pre-existing progress dialog.
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
            Assert.That(result, Does.Contain("BOOL ToLegacyBool(DWORD* error) const"));
            Assert.That(copy, Does.Contain("static COperationResult CommitTransactionalTargetFile"));
            Assert.That(copy, Does.Contain("static COperationResult VerifyDurableCopyCommit"));
            Assert.That(copy, Does.Contain("COperationResult verificationResult = VerifyDurableCopyCommit"));
            Assert.That(copy, Does.Contain("while (!verificationResult.ToLegacyBool(&verificationError))"));
            Assert.That(copy, Does.Contain("COperationResult commitResult = CommitTransactionalTargetFile"));
            Assert.That(copy, Does.Contain("while (!commitResult.ToLegacyBool(&err))"));
            Assert.That(refactoring, Does.Contain("### 40. Make operation result types explicit — Implemented"));
        });
    }

    [Test]
    public void Kernel_handle_ownership_is_scoped_and_preserves_legacy_close_failures()
    {
        var root = FindRepositoryRoot();
        var scopedHandle = File.ReadAllText(Path.Combine(root, "src", "common", "scoped_kernel_handle.h"));
        var identity = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 41. Adopt RAII for kernel handles in touched code — Implemented"));
        });
    }

    [Test]
    public void Scoped_native_resources_protect_file_operations_and_plugin_boundaries()
    {
        var root = FindRepositoryRoot();
        var resources = File.ReadAllText(Path.Combine(root, "src", "common", "scoped_native_resources.h"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));
        var broker = File.ReadAllText(Path.Combine(root, "src", "parserbroker.cpp"));
        var scripts = File.ReadAllText(Path.Combine(root, "src", "plugins", "automation", "scriptlist.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(broker, Does.Contain("CScopedCriticalSection lock(&Lock)"));
            Assert.That(broker, Does.Not.Contain("LeaveCriticalSection(&Lock);"));
            Assert.That(scripts, Does.Contain("CScopedMappingView codeView(MapViewOfFile"));
            Assert.That(scripts, Does.Not.Contain("UnmapViewOfFile(pszCodeA)"));
            Assert.That(refactoring, Does.Contain("### 42. Adopt RAII for memory, mappings, and critical sections — Implemented"));
        });
    }

    [Test]
    public void Thread_owners_keep_worker_lifetime_stop_completion_naming_com_and_exception_policy_together()
    {
        var root = FindRepositoryRoot();
        var owner = File.ReadAllText(Path.Combine(root, "src", "common", "thread_owner.h"));
        var checkPath = File.ReadAllText(Path.Combine(root, "src", "path_checking.cpp"));
        var ratchet = File.ReadAllText(Path.Combine(root, "tools", "verify-no-new-raw-thread-creation.ps1"));
        var workflow = File.ReadAllText(Path.Combine(root, ".github", "workflows", "pr-msbuild.yml"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        // These source contracts preserve the worker boundary where shutdown and
        // completion ordering cannot be deterministically driven through the UI.
        Assert.Multiple(() =>
        {
            Assert.That(owner, Does.Contain("class CThreadOwner"));
            Assert.That(owner, Does.Contain("CThreadOwnerEntry"));
            Assert.That(owner, Does.Contain("_beginthreadex"));
            Assert.That(owner, Does.Contain("StopEvent"));
            Assert.That(owner, Does.Contain("CompletionEvent"));
            Assert.That(owner, Does.Contain("SetThreadNameInVCAndTrace"));
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
            Assert.That(ratchet, Does.Contain("git diff --no-ext-diff --unified=0 $BaseCommit HEAD"));
            Assert.That(ratchet, Does.Contain("CThreadOwner"));
            Assert.That(ratchet, Does.Contain("CreateThread|_beginthreadex"));
            Assert.That(workflow, Does.Contain("verify-no-new-raw-thread-creation.ps1 -BaseCommit origin/"));
            Assert.That(refactoring, Does.Contain("### 43. Standardize thread creation and ownership — Implemented"));
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
        var appEntry = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 44. Define bounded shutdown deadlines without unsafe escalation — Implemented"));
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
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 35. Introduce a dynamic wide-path abstraction — Implemented"));
        });
    }

    [Test]
    public void Destructive_operations_keep_the_handle_identity_guard()
    {
        var root = FindRepositoryRoot();
        var helper = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));

        // The mutation API borrows the RAII owner's handle; it must not receive
        // or retain ownership of the verified delete handle itself.
        Assert.Multiple(() =>
        {
            Assert.That(helper, Does.Contain("FILE_FLAG_OPEN_REPARSE_POINT"));
            Assert.That(helper, Does.Contain("GetFileInformationByHandle"));
            Assert.That(helper, Does.Contain("GetFinalPathNameByHandleW"));
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
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 34. Exercise junction, symlink, mount-point, and cloud-placeholder cases — Implemented"));
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
    public void File_operation_planning_uses_an_immutable_plan_and_narrow_filesystem_adapter()
    {
        var root = FindRepositoryRoot();
        var planHeader = File.ReadAllText(Path.Combine(root, "src", "operation_plan.h"));
        var plan = File.ReadAllText(Path.Combine(root, "src", "operation_plan.cpp"));
        var fileSystem = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.h"));
        var nativeFileSystem = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.cpp"));
        var planner = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 27. Extract a testable file-operation planning seam — Implemented"));
        });
    }

    [Test]
    public void Transactional_copy_and_move_expose_each_durable_phase_to_a_deterministic_fault_adapter()
    {
        var root = FindRepositoryRoot();
        var executionHeader = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.h"));
        var executionAdapter = File.ReadAllText(Path.Combine(root, "src", "file_operation_filesystem.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));
        var identities = File.ReadAllText(Path.Combine(root, "src", "file_identity.cpp"));
        var journal = File.ReadAllText(Path.Combine(root, "src", "operation_journal.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 29. Add crash-consistency fault injection at every operation phase — Implemented"));
        });
    }

    [Test]
    public void Native_destructive_operation_characterization_suite_retains_the_required_scenarios()
    {
        var root = FindRepositoryRoot();
        var operations = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "FileOperationUiTests.cs"));
        var crossVolume = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "CrossVolumeMoveCharacterizationUiTests.cs"));
        var recovery = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "OperationRecoveryCharacterizationUiTests.cs"));
        var workspace = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "FileOperationUiTestBase.cs"));
        var settings = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "UiTestSettings.cs"));
        var ads = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "Infrastructure", "AlternateDataStreams.cs"));
        var unsupportedAds = File.ReadAllText(Path.Combine(root, "tests", "FileManager.UiTests", "AlternateDataStreamsUnsupportedTargetUiTests.cs"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        Assert.Multiple(() =>
        {
            Assert.That(operations, Does.Contain("Copy_overwrite_replaces_the_existing_target_only_after_the_user_confirms"));
            Assert.That(operations, Does.Contain("Copy_overwrite_all_applies_the_choice_to_the_complete_conflicting_tree"));
            Assert.That(operations, Does.Contain("Copy_skip_keeps_the_existing_target_and_the_source"));
            Assert.That(operations, Does.Contain("Copy_skip_all_keeps_the_existing_conflicting_tree"));
            Assert.That(operations, Does.Contain("Rename_overwrite_replaces_the_collision_without_losing_source_metadata"));
            Assert.That(operations, Does.Contain("The default move fixture characterizes same-volume behavior."));
            Assert.That(operations, Does.Contain("Delete_to_recycle_bin_removes_the_source_and_creates_a_recoverable_shell_item"));
            Assert.That(operations, Does.Contain("Cancelling_an_in_progress_conflicting_copy_keeps_both_versions_and_records_cancellation"));
            Assert.That(crossVolume, Does.Contain("Move_across_volumes_copies_the_complete_tree_before_removing_the_source"));
            Assert.That(crossVolume, Does.Contain("RequireCrossVolumeRoot"));
            Assert.That(operations, Does.Contain("Copy_preserves_multiple_empty_large_and_edge_named_alternate_data_streams"));
            Assert.That(operations, Does.Contain("Copy_overwrite_replaces_target_streams_and_removes_stale_streams"));
            Assert.That(operations, Does.Contain("Copy_retries_a_temporarily_denied_alternate_data_stream_without_losing_it"));
            Assert.That(crossVolume, Does.Contain("Move_across_ADS_capable_volumes_preserves_multiple_streams_before_removing_the_source"));
            Assert.That(unsupportedAds, Does.Contain("Cross_volume_move_to_an_ADS_unsupported_target_keeps_the_source_when_metadata_loss_is_declined"));
            Assert.That(ads, Does.Contain("RequireSupportAt"));
            Assert.That(ads, Does.Contain("RequireUnsupportedAt"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_CROSS_VOLUME_ROOT"));
            Assert.That(settings, Does.Contain("FILEMANAGER_UI_ADS_UNSUPPORTED_TARGET_ROOT"));
            Assert.That(recovery, Does.Contain("Restart_reconciliation_commits_a_fully_written_transactional_target"));
            Assert.That(recovery, Does.Contain("STATE|0|temporary-ready"));
            Assert.That(workspace, Does.Contain("TargetVolumeRoot"));
            Assert.That(workspace, Does.Contain("TargetWorkspaceDirectory"));
            Assert.That(refactoring, Does.Contain("### 28. Build native characterization tests for copy, move, delete, and rename — Implemented"));
            Assert.That(refactoring, Does.Contain("### 32. Test alternate data streams end to end — Implemented"));
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
        var widePath = File.ReadAllText(Path.Combine(root, "src", "common", "wide_path.h"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        Assert.Multiple(() =>
        {
            Assert.That(startup, Does.Contain("InitializeDllSearchPaths()"));
            Assert.That(startup, Does.Contain("SetDefaultDllDirectories"));
            Assert.That(startup, Does.Contain("AddDllDirectory"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_APPLICATION_DIR"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_SYSTEM32"));
            Assert.That(startup, Does.Contain("LOAD_LIBRARY_SEARCH_USER_DIRS"));
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
            // Request serialization is now scope-owned so callback unwinding
            // cannot strand the broker lock.
            Assert.That(client, Does.Contain("CScopedCriticalSection lock(&Lock)"));
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

    [Test]
    public void Plugin_entry_scope_restores_host_state_after_an_unwinding_entry_point()
    {
        var root = FindRepositoryRoot();
        var loader = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var header = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var messages = File.ReadAllText(Path.Combine(root, "src", "mainwnd_messages.cpp"));
        var shutdown = File.ReadAllText(Path.Combine(root, "src", "mainwnd_shutdown.cpp"));
        var pathUtilities = File.ReadAllText(Path.Combine(root, "src", "path_utils.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

        Assert.Multiple(() =>
        {
            Assert.That(loader, Does.Contain("class CPluginEntryScope"));
            Assert.That(loader, Does.Contain("~CPluginEntryScope()"));
            Assert.That(loader, Does.Contain("CPluginDataLock dataLock"));
            Assert.That(loader, Does.Contain("PluginIface.Init(NULL, 0)"));
            Assert.That(loader, Does.Contain("LeavePlugin();\n        SalamanderGeneral.Init(PluginIface.GetInterface());"));
            Assert.That(loader, Does.Contain("CPluginEntryScope pluginEntry(PluginIface, SalamanderGeneral, BuiltForVersion)"));
            Assert.That(loader, Does.Contain("pluginEntry.SetReturnedInterface(resIface)"));
            Assert.That(loader, Does.Not.Contain("EnterPlugin(); // for the plugin entry point"));
            Assert.That(loader, Does.Contain("static SRWLOCK PluginNestingStateLock = SRWLOCK_INIT"));
            Assert.That(loader, Does.Contain("static std::atomic<int> AlreadyInPlugin(0)"));
            Assert.That(header, Does.Contain("BOOL IsInPlugin();"));
            Assert.That(messages, Does.Contain("IsInPlugin() || StopRefresh > 0"));
            Assert.That(shutdown, Does.Contain("!endAfterCleanup && IsInPlugin()"));
            Assert.That(pathUtilities, Does.Contain("!IsInPlugin()"));
            Assert.That(refactoring, Does.Contain("### 22. Implemented: make plug-in entry bookkeeping exception-safe"));
        });
    }

    [Test]
    public void Plugin_callbacks_are_contained_and_the_failing_plugin_is_deferred_for_unload()
    {
        var root = FindRepositoryRoot();
        var header = File.ReadAllText(Path.Combine(root, "src", "plugins.h"));
        var loader = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var registry = File.ReadAllText(Path.Combine(root, "src", "plugins_interface.cpp"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 23. Add failure barriers around every plug-in callback — Implemented"));
        });
    }

    [Test]
    public void Configuration_saves_stage_validate_and_atomically_select_a_generation()
    {
        var root = FindRepositoryRoot();
        var configuration = File.ReadAllText(Path.Combine(root, "src", "mainwnd_config.cpp"));
        var registryWork = File.ReadAllText(Path.Combine(root, "src", "regwork.cpp"));
        var plugins = File.ReadAllText(Path.Combine(root, "src", "plugins_loading.cpp"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(configuration, Does.Contain("SetValue(storeKey, CONFIGURATION_ACTIVE_GENERATION_REG"));
            Assert.That(configuration, Does.Contain("RetirePreviousConfigurationGenerationAfterSuccessfulStartup"));
            Assert.That(configuration, Does.Contain("OpenCommittedConfigurationGeneration(storeKey, fallbackGeneration"));
            Assert.That(startup, Does.Contain("SetConfigurationStoreRoot(SALAMANDER_ROOT_REG)"));
            Assert.That(startup, Does.Contain("SelectCommittedConfigurationGeneration()"));
            Assert.That(plugins, Does.Contain("MainWindow->SaveConfig();"));
            Assert.That(plugins, Does.Contain("MainWindow->SaveConfig(parent);"));
            Assert.That(plugins, Does.Not.Contain("CreateKey(HKEY_CURRENT_USER, SALAMANDER_ROOT_REG"),
                        "Plug-in commits must not mutate the checksum-protected active generation.");
            Assert.That(refactoring, Does.Contain("### 24. Make configuration saves transactional — Implemented"));
            Assert.That(architecture, Does.Contain("the root's `Active Generation` DWORD"));
        });
    }

    [Test]
    public void Configuration_profiles_are_schema_versioned_migrated_and_validated_before_loading()
    {
        var root = FindRepositoryRoot();
        var configuration = File.ReadAllText(Path.Combine(root, "src", "mainwnd_config.cpp"));
        var startup = File.ReadAllText(Path.Combine(root, "src", "app_entry.cpp"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 25. Version and validate the complete configuration schema — Implemented"));
        });
    }

    [Test]
    public void Metadata_preservation_contract_records_losses_and_gates_move_source_deletion()
    {
        var root = FindRepositoryRoot();
        var workerHeader = File.ReadAllText(Path.Combine(root, "src", "worker.h"));
        var worker = File.ReadAllText(Path.Combine(root, "src", "worker.cpp"));
        var planner = File.ReadAllText(Path.Combine(root, "src", "fileswindow_operations.cpp"));
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));
        var operations = File.ReadAllText(Path.Combine(root, "src", "operations_core.cpp"));
        var dialogs = File.ReadAllText(Path.Combine(root, "src", "dialogs_file_ops.cpp"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "lang", "texts.rc2"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 31. Publish an explicit metadata preservation contract — Implemented"));
        });
    }

    [Test]
    public void Security_descriptor_copy_uses_the_privilege_aware_preservation_matrix()
    {
        var root = FindRepositoryRoot();
        var copy = File.ReadAllText(Path.Combine(root, "src", "async_copy.cpp"));
        var architecture = File.ReadAllText(Path.Combine(root, "architecture.md"));
        var refactoring = File.ReadAllText(Path.Combine(root, "refactoring.md"));

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
            Assert.That(refactoring, Does.Contain("### 33. Verify ACL and ownership preservation under privilege variation — Implemented"));
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
