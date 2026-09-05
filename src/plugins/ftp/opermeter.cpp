// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA / StringCchPrintfA)

#include "opermeter.h"

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

// Populated from the configuration; empty selects %TEMP%. Written once during
// configuration load, read while formatting a finished operation's document.
char FTPMetricsOutputDir[MAX_PATH] = "";

// Names are stable identifiers in the exported JSON: a benchmark comparison
// script matches on them, so they must not be localized.
static const char* MeteredCommandNames[fmcCount] =
    {
        "login", "cwd_pwd", "type", "listing", "mlst", "feat", "size", "mdtm",
        "pasv_port", "retr_stor", "mkd_rmd", "dele", "rename", "other"};

static const char* MeteredHandshakeNames[fmhCount] =
    {"full", "resumed", "unknown"};

const char* GetFTPMeteredCommandName(CFTPMeteredCommand cmd)
{
    return (cmd >= 0 && cmd < fmcCount) ? MeteredCommandNames[cmd] : "other";
}

const char* GetFTPMeteredHandshakeName(CFTPMeteredHandshake result)
{
    return (result >= 0 && result < fmhCount) ? MeteredHandshakeNames[result] : "unknown";
}

// Copies 'src' into a JSON string body, escaping the characters RFC 8259
// requires. Server greetings and system strings are server-controlled text, so
// they can never be pasted into the document unescaped.
static void AppendJSONEscaped(char* dst, int dstSize, const char* src)
{
    int used = (int)strlen(dst);
    for (const char* s = src; s != NULL && *s != 0 && used < dstSize - 8; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\')
        {
            dst[used++] = '\\';
            dst[used++] = (char)c;
        }
        else if (c < 0x20 || c == 0x7F)
        {
            // Control characters (including the CRLF that ends every FTP reply)
            // become \u escapes rather than being dropped, so the exported text
            // stays a faithful record of what the server sent.
            _snprintf_s(dst + used, dstSize - used, _TRUNCATE, "\\u%04X", (unsigned)c);
            used += 6;
        }
        else
            dst[used++] = (char)c;
    }
    dst[used] = 0;
}

// Formats a non-negative rate with 'decimals' fractional digits without going
// through the C library's floating-point conversion. "%f" honours LC_NUMERIC,
// so under a locale such as pl-PL it would emit "833,333" - which is not a
// number in JSON and would make the whole document unparseable. Splitting the
// value into an integer part and a scaled fraction keeps the output identical
// on every machine, which is also what makes two runs comparable.
static void FormatJSONRate(char* buf, int bufSize, double value, int decimals)
{
    if (bufSize <= 0)
        return;
    if (!(value > 0.0)) // also catches NaN
    {
        StringCchCopyNA(buf, bufSize, "0.0", bufSize);
        return;
    }
    unsigned __int64 scale = 1;
    for (int i = 0; i < decimals; i++)
        scale *= 10;
    // Saturate rather than wrap: an implausible rate must not print as a small
    // number and quietly flatter the result.
    double scaled = value * (double)scale;
    unsigned __int64 total = scaled >= 1.8e19 ? 0xFFFFFFFFFFFFFFFFui64 : (unsigned __int64)(scaled + 0.5);
    _snprintf_s(buf, bufSize, _TRUNCATE, "%I64u.%0*I64u", total / scale, decimals, total % scale);
}

CFTPTransferMetrics::CFTPTransferMetrics()
{
    HANDLES(InitializeCriticalSection(&MetricsCritSect));
    // Zeroing the whole counter block keeps "not reached yet" a single
    // representation (0) for both timestamps and counters.
    Enabled = FALSE;
    Reported = FALSE;
    OperationStart = 0;
    DiscoveryStart = 0;
    DiscoveryEnd = 0;
    FirstPayloadByte = 0;
    FirstCompletedFile = 0;
    OperationEnd = 0;
    PayloadBytes = 0;
    FilesCompleted = 0;
    FilesFailed = 0;
    FilesSkipped = 0;
    DirectoriesDiscovered = 0;
    ListingsCompleted = 0;
    ListingsFailed = 0;
    memset(LatencyHistogram, 0, sizeof(LatencyHistogram));
    LatencySamples = 0;
    LatencyTotalMs = 0;
    LatencyMaxMs = 0;
    memset(Commands, 0, sizeof(Commands));
    ConnectionAttempts = 0;
    ConnectionReuses = 0;
    ConnectionRefusals = 0;
    AdmissionDenials = 0;
    LoginCount = 0;
    RetryCount = 0;
    WorkerTarget = 0;
    WorkerPeakCount = 0;
    UIDLookupCalls = 0;
    UIDLookupScannedItems = 0;
    DiscoveryAssignments = 0;
    TransferAssignments = 0;
    BackpressurePauses = 0;
    DirCacheHits = 0;
    DirCacheMisses = 0;
    DuplicateListingFetches = 0;
    memset(Handshakes, 0, sizeof(Handshakes));
    memset(HandshakeTotalMs, 0, sizeof(HandshakeTotalMs));
    DiskQueueWaitMs = 0;
    DiskServiceMs = 0;
    BufferAllocations = 0;
    BufferReuses = 0;
    UIRefreshCount = 0;
    UIRefreshTotalMs = 0;
    ServerHost[0] = 0;
    ServerPort = 0;
    ServerSystem[0] = 0;
    SecurityMode[0] = 0;
    OperationName[0] = 0;
}

CFTPTransferMetrics::~CFTPTransferMetrics()
{
    HANDLES(DeleteCriticalSection(&MetricsCritSect));
}

BOOL CFTPTransferMetrics::IsEnabled()
{
    // Reading one plain int without the configuration section is intentional:
    // the flag only ever changes in the configuration dialog, and a torn read is
    // impossible for an aligned int on the platforms this plug-in targets.
    return Config.EnableTransferMetrics;
}

void CFTPTransferMetrics::Start(const char* operationName)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    Enabled = IsEnabled();
    if (Enabled)
    {
        OperationStart = CMonotonicClock::Now();
        StringCchCopyNA(OperationName, SizeOf(OperationName),
                        operationName != NULL ? operationName : "", SizeOf(OperationName));
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::SetOperationName(const char* operationName)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        StringCchCopyNA(OperationName, SizeOf(OperationName),
                        operationName != NULL ? operationName : "", SizeOf(OperationName));
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::SetTarget(const char* host, int port, const char* serverSystem,
                                    const char* securityMode)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        // Only endpoint identity is retained. The user name, password and
        // account are deliberately absent from this object so no redaction step
        // can be forgotten later.
        StringCchCopyNA(ServerHost, SizeOf(ServerHost), host != NULL ? host : "", SizeOf(ServerHost));
        ServerPort = port;
        StringCchCopyNA(ServerSystem, SizeOf(ServerSystem),
                        serverSystem != NULL ? serverSystem : "", SizeOf(ServerSystem));
        StringCchCopyNA(SecurityMode, SizeOf(SecurityMode),
                        securityMode != NULL ? securityMode : "", SizeOf(SecurityMode));
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDiscoveryStart()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled && DiscoveryStart == 0)
        DiscoveryStart = CMonotonicClock::Now();
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDiscoveryEnd()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    // Discovery can be declared finished more than once (a retried listing
    // reopens it), so the last mark wins - the phase ends when the tree is
    // actually fully enumerated.
    if (Enabled)
        DiscoveryEnd = CMonotonicClock::Now();
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteFirstPayloadByte()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled && FirstPayloadByte == 0)
        FirstPayloadByte = CMonotonicClock::Now();
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteOperationEnd()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        OperationEnd = CMonotonicClock::Now();
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::AddPayloadBytes(unsigned __int64 bytes)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        PayloadBytes += bytes;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteFileCompleted(CMonotonicDuration latencyMs)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        FilesCompleted++;
        if (FirstCompletedFile == 0)
            FirstCompletedFile = CMonotonicClock::Now();

        LatencySamples++;
        LatencyTotalMs += latencyMs;
        if (latencyMs > LatencyMaxMs)
            LatencyMaxMs = latencyMs;
        // Bucket i holds [2^i, 2^(i+1)) ms; bucket 0 also absorbs sub-millisecond
        // completions so no sample is lost.
        int bucket = 0;
        unsigned __int64 value = latencyMs;
        while (value > 1 && bucket < SizeOf(LatencyHistogram) - 1)
        {
            value >>= 1;
            bucket++;
        }
        LatencyHistogram[bucket]++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteFileFailed()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        FilesFailed++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteFileSkipped()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        FilesSkipped++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDirectoriesDiscovered(unsigned __int64 count)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        DirectoriesDiscovered += count;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteListing(BOOL succeeded)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        if (succeeded)
            ListingsCompleted++;
        else
            ListingsFailed++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteCommand(CFTPMeteredCommand cmd, CMonotonicDuration durationMs)
{
    if (cmd < 0 || cmd >= fmcCount)
        cmd = fmcOther;
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        Commands[cmd].Count++;
        Commands[cmd].TotalMs += durationMs;
        if (durationMs > Commands[cmd].MaxMs)
            Commands[cmd].MaxMs = durationMs;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteConnectionAttempt(BOOL reused)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        ConnectionAttempts++;
        if (reused)
            ConnectionReuses++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteConnectionRefusal()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        ConnectionRefusals++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteAdmissionDenial()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        AdmissionDenials++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteLogin()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        LoginCount++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteRetry()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        RetryCount++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::SetWorkerTarget(int target)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        WorkerTarget = target;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteWorkerCount(int count)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled && count > WorkerPeakCount)
        WorkerPeakCount = count;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteUIDLookup(unsigned __int64 scannedItems)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        // Scanned-item work is the number the queue-indexing change in section 3
        // is meant to drive to zero, so it is measured separately from the call
        // count rather than inferred from wall-clock time.
        UIDLookupCalls++;
        UIDLookupScannedItems += scannedItems;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteAssignment(BOOL discovery)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        if (discovery)
            DiscoveryAssignments++;
        else
            TransferAssignments++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteBackpressurePause()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        BackpressurePauses++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDirCache(BOOL hit)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        if (hit)
            DirCacheHits++;
        else
            DirCacheMisses++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDuplicateListingFetch()
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
        DuplicateListingFetches++;
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteHandshake(CFTPMeteredHandshake result, CMonotonicDuration durationMs)
{
    if (result < 0 || result >= fmhCount)
        result = fmhUnknown;
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        Handshakes[result]++;
        HandshakeTotalMs[result] += durationMs;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteDiskWork(CMonotonicDuration waitMs, CMonotonicDuration serviceMs)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        DiskQueueWaitMs += waitMs;
        DiskServiceMs += serviceMs;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteBuffer(BOOL reused)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        if (reused)
            BufferReuses++;
        else
            BufferAllocations++;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

void CFTPTransferMetrics::NoteUIRefresh(CMonotonicDuration durationMs)
{
    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled)
    {
        UIRefreshCount++;
        UIRefreshTotalMs += durationMs;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));
}

unsigned __int64 CFTPTransferMetrics::LatencyPercentile(int percentile)
{
    if (LatencySamples == 0)
        return 0;
    // Round the target rank up so p95 of 20 samples is the 19th, not the 18th.
    unsigned __int64 target = (LatencySamples * percentile + 99) / 100;
    if (target == 0)
        target = 1;
    unsigned __int64 seen = 0;
    for (int i = 0; i < SizeOf(LatencyHistogram); i++)
    {
        seen += LatencyHistogram[i];
        if (seen >= target)
            return (unsigned __int64)1 << (i + 1); // bucket upper bound: an over-estimate by construction
    }
    return LatencyMaxMs;
}

int CFTPTransferMetrics::FormatJSON(char* buf, int bufSize, int queueCount, int queueRejected,
                                    int queueHighWaterMark)
{
    // Local copies of the escaped, server-controlled strings; built before the
    // main body so the fixed-size buffer arithmetic below stays simple.
    char hostJSON[2 * SizeOf(ServerHost)];
    char systemJSON[2 * SizeOf(ServerSystem)];
    hostJSON[0] = 0;
    systemJSON[0] = 0;
    AppendJSONEscaped(hostJSON, SizeOf(hostJSON), ServerHost);
    AppendJSONEscaped(systemJSON, SizeOf(systemJSON), ServerSystem);

    CMonotonicDuration totalMs = OperationEnd > OperationStart ? OperationEnd - OperationStart : 0;
    CMonotonicDuration discoveryMs = (DiscoveryEnd > DiscoveryStart) ? DiscoveryEnd - DiscoveryStart : 0;
    CMonotonicDuration toFirstByteMs = (FirstPayloadByte > OperationStart) ? FirstPayloadByte - OperationStart : 0;
    CMonotonicDuration toFirstFileMs = (FirstCompletedFile > OperationStart) ? FirstCompletedFile - OperationStart : 0;

    // Rates are computed here rather than in the consumer so a run's own numbers
    // are self-consistent even when documents are compared across builds.
    double seconds = totalMs > 0 ? (double)totalMs / 1000.0 : 0.0;
    double filesPerSec = seconds > 0.0 ? (double)FilesCompleted / seconds : 0.0;
    double bytesPerSec = seconds > 0.0 ? (double)PayloadBytes / seconds : 0.0;
    char filesPerSecText[64];
    char bytesPerSecText[64];
    FormatJSONRate(filesPerSecText, SizeOf(filesPerSecText), filesPerSec, 3);
    FormatJSONRate(bytesPerSecText, SizeOf(bytesPerSecText), bytesPerSec, 1);

    int used = _snprintf_s(buf, bufSize, _TRUNCATE,
                           "{\n"
                           "  \"schema\": \"ftp-transfer-metrics/1\",\n"
                           "  \"build\": {\n"
                           "    \"plugin\": \"ftp.spl\",\n"
                           "    \"version\": \"%d.%d\",\n"
                           "    \"architecture\": \"%s\",\n"
                           "    \"configuration\": \"%s\"\n"
                           "  },\n"
                           "  \"target\": {\n"
                           "    \"host\": \"%s\",\n"
                           "    \"port\": %d,\n"
                           "    \"system\": \"%s\",\n"
                           "    \"security\": \"%s\"\n"
                           "  },\n"
                           "  \"operation\": \"%s\",\n"
                           "  \"phases_ms\": {\n"
                           "    \"total\": %I64u,\n"
                           "    \"discovery\": %I64u,\n"
                           "    \"to_first_payload_byte\": %I64u,\n"
                           "    \"to_first_completed_file\": %I64u\n"
                           "  },\n"
                           "  \"throughput\": {\n"
                           "    \"files_completed\": %I64u,\n"
                           "    \"files_failed\": %I64u,\n"
                           "    \"files_skipped\": %I64u,\n"
                           "    \"payload_bytes\": %I64u,\n"
                           "    \"files_per_second\": %s,\n"
                           "    \"payload_bytes_per_second\": %s\n"
                           "  },\n"
                           "  \"file_latency_ms\": {\n"
                           "    \"samples\": %I64u,\n"
                           "    \"mean\": %I64u,\n"
                           "    \"p50_upper_bound\": %I64u,\n"
                           "    \"p95_upper_bound\": %I64u,\n"
                           "    \"max\": %I64u\n"
                           "  },\n",
                           VERSINFO_MAJOR, VERSINFO_MINORA,
#ifdef _WIN64
                           "x64",
#else
                           "Win32",
#endif
#ifdef _DEBUG
                           "Debug",
#else
                           "Release",
#endif
                           hostJSON, ServerPort, systemJSON, SecurityMode, OperationName,
                           totalMs, discoveryMs, toFirstByteMs, toFirstFileMs,
                           FilesCompleted, FilesFailed, FilesSkipped, PayloadBytes,
                           filesPerSecText, bytesPerSecText,
                           LatencySamples,
                           LatencySamples > 0 ? LatencyTotalMs / LatencySamples : 0,
                           LatencyPercentile(50), LatencyPercentile(95), LatencyMaxMs);
    if (used < 0)
        return -1;

    used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE, "  \"commands\": {\n");
    for (int i = 0; i < fmcCount; i++)
    {
        used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE,
                            "    \"%s\": {\"count\": %I64u, \"total_ms\": %I64u, \"max_ms\": %I64u}%s\n",
                            MeteredCommandNames[i], Commands[i].Count, Commands[i].TotalMs,
                            Commands[i].MaxMs, i + 1 < fmcCount ? "," : "");
    }
    used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE, "  },\n");

    used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE,
                        "  \"tls_handshakes\": {\n");
    for (int i = 0; i < fmhCount; i++)
    {
        used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE,
                            "    \"%s\": {\"count\": %I64u, \"total_ms\": %I64u}%s\n",
                            MeteredHandshakeNames[i], Handshakes[i], HandshakeTotalMs[i],
                            i + 1 < fmhCount ? "," : "");
    }
    used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE, "  },\n");

    used += _snprintf_s(buf + used, bufSize - used, _TRUNCATE,
                        "  \"connections\": {\n"
                        "    \"attempts\": %I64u,\n"
                        "    \"reuses\": %I64u,\n"
                        "    \"server_refusals\": %I64u,\n"
                        "    \"admission_denials\": %I64u,\n"
                        "    \"logins\": %I64u,\n"
                        "    \"retries\": %I64u,\n"
                        "    \"worker_target\": %d,\n"
                        "    \"worker_peak\": %d\n"
                        "  },\n"
                        "  \"scheduler\": {\n"
                        "    \"discovery_assignments\": %I64u,\n"
                        "    \"transfer_assignments\": %I64u,\n"
                        "    \"backpressure_pauses\": %I64u,\n"
                        "    \"uid_lookups\": %I64u,\n"
                        "    \"uid_lookup_scanned_items\": %I64u,\n"
                        "    \"queue_items\": %d,\n"
                        "    \"queue_high_water_mark\": %d,\n"
                        "    \"queue_rejected_items\": %d\n"
                        "  },\n"
                        "  \"listings\": {\n"
                        "    \"completed\": %I64u,\n"
                        "    \"failed\": %I64u,\n"
                        "    \"directories_discovered\": %I64u,\n"
                        "    \"cache_hits\": %I64u,\n"
                        "    \"cache_misses\": %I64u,\n"
                        "    \"duplicate_fetches\": %I64u\n"
                        "  },\n"
                        "  \"local\": {\n"
                        "    \"disk_queue_wait_ms\": %I64u,\n"
                        "    \"disk_service_ms\": %I64u,\n"
                        "    \"buffer_allocations\": %I64u,\n"
                        "    \"buffer_reuses\": %I64u,\n"
                        "    \"ui_refreshes\": %I64u,\n"
                        "    \"ui_refresh_total_ms\": %I64u\n"
                        "  }\n"
                        "}\n",
                        ConnectionAttempts, ConnectionReuses, ConnectionRefusals, AdmissionDenials,
                        LoginCount, RetryCount, WorkerTarget, WorkerPeakCount,
                        DiscoveryAssignments, TransferAssignments, BackpressurePauses,
                        UIDLookupCalls, UIDLookupScannedItems,
                        queueCount, queueHighWaterMark, queueRejected,
                        ListingsCompleted, ListingsFailed, DirectoriesDiscovered,
                        DirCacheHits, DirCacheMisses, DuplicateListingFetches,
                        DiskQueueWaitMs, DiskServiceMs, BufferAllocations, BufferReuses,
                        UIRefreshCount, UIRefreshTotalMs);
    return used;
}

void CFTPTransferMetrics::Report(int queueCount, int queueRejected, int queueHighWaterMark)
{
    // The buffer is sized for the fixed document above with room for the two
    // escaped server strings; formatting is bounded, so it cannot be exceeded.
    const int bufSize = 16384;
    char* buf = (char*)malloc(bufSize);
    if (buf == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return;
    }
    buf[0] = 0;

    char fileName[MAX_PATH];
    fileName[0] = 0;
    BOOL write = FALSE;

    HANDLES(EnterCriticalSection(&MetricsCritSect));
    if (Enabled && !Reported)
    {
        Reported = TRUE; // exactly one document per operation, even if teardown runs twice
        if (OperationEnd == 0)
            OperationEnd = CMonotonicClock::Now();
        write = FormatJSON(buf, bufSize, queueCount, queueRejected, queueHighWaterMark) > 0;

        // A local timestamp only names the file; every measured duration in the
        // document itself comes from the monotonic clock.
        SYSTEMTIME st;
        GetLocalTime(&st);
        char dir[MAX_PATH];
        if (FTPMetricsOutputDir[0] != 0)
            StringCchCopyNA(dir, SizeOf(dir), FTPMetricsOutputDir, SizeOf(dir));
        else if (GetTempPathA(SizeOf(dir), dir) == 0)
            dir[0] = 0;
        if (dir[0] != 0)
        {
            // GetTempPath already ends with a backslash; a configured directory
            // may not, so normalize before composing the file name.
            int dirLen = (int)strlen(dir);
            if (dirLen > 0 && dir[dirLen - 1] != '\\' && dirLen + 1 < SizeOf(dir))
            {
                dir[dirLen] = '\\';
                dir[dirLen + 1] = 0;
            }
            _snprintf_s(fileName, SizeOf(fileName), _TRUNCATE,
                        "%sftp-metrics-%04d%02d%02d-%02d%02d%02d-%lu.json",
                        dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                        GetCurrentProcessId());
        }
        else
            write = FALSE;
    }
    HANDLES(LeaveCriticalSection(&MetricsCritSect));

    // File I/O happens outside the counter section: a slow or full disk must not
    // block a worker that is still updating counters during teardown.
    if (write && fileName[0] != 0)
    {
        // The plug-in's UTF-8 wrapper, so a metrics directory outside the ANSI
        // code page still works.
        HANDLE file = HANDLES_Q(CreateFileUtf8Local(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                                    FILE_ATTRIBUTE_NORMAL, NULL));
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            DWORD length = (DWORD)strlen(buf);
            if (!WriteFile(file, buf, length, &written, NULL) || written != length)
                TRACE_E("CFTPTransferMetrics::Report(): unable to write the metrics document");
            HANDLES(CloseHandle(file));
        }
        else
            TRACE_E("CFTPTransferMetrics::Report(): unable to create " << fileName);
    }
    free(buf);
}
