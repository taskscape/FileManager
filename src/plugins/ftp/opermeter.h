// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "..\\..\\common\\wide_path.h" // metrics output paths must survive Unicode and long-path Win32 boundaries
#include "..\\..\\common\\monotonic_time.h" // 64-bit monotonic time: phase durations must survive a GetTickCount wrap

// ****************************************************************************
// Transfer measurement (ftp-improvements.md section 1)
//
// Optimization claims about large-folder copies need per-phase evidence, so the
// plug-in aggregates counters in memory and exports one machine-readable JSON
// document per operation instead of writing a record per packet. Everything here
// is deliberately allocation-free on the hot paths: counters are plain 64-bit
// fields updated under one short critical section, and the only formatting work
// happens once, when an operation finishes.
//
// Collection is off unless the user opts in (see CFTPTransferMetrics::IsEnabled),
// because instrumentation must not change the behaviour it is supposed to
// measure. When disabled every Add/Note call still runs, but it only touches
// this object's memory - no I/O, no formatting and no extra synchronization
// beyond the counter section that already exists per operation.
// ****************************************************************************

// Commands whose count and cumulative duration are tracked separately. The list
// intentionally mirrors the FTP verbs that dominate many-small-files copies; a
// verb that is not listed lands in fmcOther so totals stay complete.
enum CFTPMeteredCommand
{
    fmcLogin, // greeting + AUTH/USER/PASS/ACCT + login-script commands
    fmcCwdPwd,
    fmcType,
    fmcListing, // LIST / NLST / MLSD
    fmcMlst,    // single-entry MLST (targeted metadata check)
    fmcFeat,
    fmcSize,
    fmcMdtm,
    fmcPasvPort, // passive/active data-connection setup
    fmcRetrStor, // RETR / STOR / APPE
    fmcMkdRmd,
    fmcDele,
    fmcRename,
    fmcOther,

    fmcCount // must stay last
};

// Outcome of one TLS handshake as reported by SChannel. "Unknown" is a real,
// distinct result: some providers do not expose SECPKG_ATTR_SESSION_INFO, and
// reporting a guess would defeat the purpose of measuring resumption at all.
enum CFTPMeteredHandshake
{
    fmhFull,
    fmhResumed,
    fmhUnknown,

    fmhCount // must stay last
};

// Names used both in the JSON export and in the log; kept next to the enums so
// a new verb cannot be added without a name.
const char* GetFTPMeteredCommandName(CFTPMeteredCommand cmd);
const char* GetFTPMeteredHandshakeName(CFTPMeteredHandshake result);

struct CFTPMeteredCommandStats
{
    unsigned __int64 Count;
    unsigned __int64 TotalMs; // sum of measured durations, milliseconds
    unsigned __int64 MaxMs;
};

// Per-operation counters. One instance lives inside CFTPOperation, so the
// object's lifetime matches the operation's and no global registry of live
// operations is needed.
class CFTPTransferMetrics
{
protected:
    // Counters are touched from worker sockets, the disk thread and the
    // operation dialog. They get their own short section rather than reusing
    // OperCritSect, so measurement can never widen an existing lock's scope.
    CRITICAL_SECTION MetricsCritSect;

    BOOL Enabled;  // FALSE = collection is off; calls still return immediately
    BOOL Reported; // TRUE = the JSON document has already been written (exactly once)

    // --- phase timestamps (0 = the phase has not been reached yet) ---
    CMonotonicTimePoint OperationStart;
    CMonotonicTimePoint DiscoveryStart;
    CMonotonicTimePoint DiscoveryEnd;
    CMonotonicTimePoint FirstPayloadByte;
    CMonotonicTimePoint FirstCompletedFile;
    CMonotonicTimePoint OperationEnd;

    // --- payload and item progress ---
    unsigned __int64 PayloadBytes;
    unsigned __int64 FilesCompleted;
    unsigned __int64 FilesFailed;
    unsigned __int64 FilesSkipped;
    unsigned __int64 DirectoriesDiscovered;
    unsigned __int64 ListingsCompleted;
    unsigned __int64 ListingsFailed;

    // --- per-file completion latency ---
    // A full sample vector would grow without bound on a 100,000-file copy, so
    // latency is summarized with a fixed logarithmic histogram. Bucket i covers
    // [2^i, 2^(i+1)) ms, which is precise enough to compare p50/p95 between runs
    // while costing one array of 32 counters.
    unsigned __int64 LatencyHistogram[32];
    unsigned __int64 LatencySamples;
    unsigned __int64 LatencyTotalMs;
    unsigned __int64 LatencyMaxMs;

    // --- command counters ---
    CFTPMeteredCommandStats Commands[fmcCount];

    // --- connection / scheduling ---
    unsigned __int64 ConnectionAttempts;
    unsigned __int64 ConnectionReuses;
    unsigned __int64 ConnectionRefusals; // classified server connection-limit refusals
    unsigned __int64 AdmissionDenials;   // leases refused by our own admission controller
    unsigned __int64 LoginCount;
    unsigned __int64 RetryCount;
    int WorkerTarget;    // currently requested number of workers
    int WorkerPeakCount; // largest number of workers the operation ever held

    // --- queue / scheduler ---
    unsigned __int64 UIDLookupCalls;
    unsigned __int64 UIDLookupScannedItems; // items visited by the linear fallback
    unsigned __int64 DiscoveryAssignments;
    unsigned __int64 TransferAssignments;
    unsigned __int64 BackpressurePauses;

    // --- listing cache ---
    unsigned __int64 DirCacheHits;
    unsigned __int64 DirCacheMisses;
    unsigned __int64 DuplicateListingFetches;

    // --- TLS ---
    unsigned __int64 Handshakes[fmhCount];
    unsigned __int64 HandshakeTotalMs[fmhCount];

    // --- local work ---
    unsigned __int64 DiskQueueWaitMs;
    unsigned __int64 DiskServiceMs;
    unsigned __int64 BufferAllocations;
    unsigned __int64 BufferReuses;
    unsigned __int64 UIRefreshCount;
    unsigned __int64 UIRefreshTotalMs;

    // --- run identity (redacted: no user, password or account is stored) ---
    char ServerHost[256];
    int ServerPort;
    char ServerSystem[128];
    char SecurityMode[32]; // "plain", "ftps-control", "ftps-data"
    char OperationName[64];

public:
    CFTPTransferMetrics();
    ~CFTPTransferMetrics();

    // Enables collection and stamps the operation start. Called once, when the
    // operation object is configured.
    void Start(const char* operationName);

    // TRUE when the user opted in through the plug-in configuration. Checked
    // once per operation rather than per counter update.
    static BOOL IsEnabled();

    // Records the identity of the run so two JSON documents can be compared
    // meaningfully. Credentials are deliberately not accepted by this API.
    void SetTarget(const char* host, int port, const char* serverSystem, const char* securityMode);

    // Names the operation type. Separate from Start() because the type is only
    // decided after the connection data is known.
    void SetOperationName(const char* operationName);

    // --- phase marks (each records only the first occurrence) ---
    void NoteDiscoveryStart();
    void NoteDiscoveryEnd();
    void NoteFirstPayloadByte();
    void NoteOperationEnd();

    // --- progress ---
    void AddPayloadBytes(unsigned __int64 bytes);
    void NoteFileCompleted(CMonotonicDuration latencyMs);
    void NoteFileFailed();
    void NoteFileSkipped();
    void NoteDirectoriesDiscovered(unsigned __int64 count);
    void NoteListing(BOOL succeeded);

    // --- commands ---
    void NoteCommand(CFTPMeteredCommand cmd, CMonotonicDuration durationMs);

    // --- connections ---
    void NoteConnectionAttempt(BOOL reused);
    void NoteConnectionRefusal();
    void NoteAdmissionDenial();
    void NoteLogin();
    void NoteRetry();
    void SetWorkerTarget(int target);
    void NoteWorkerCount(int count);

    // --- scheduler ---
    void NoteUIDLookup(unsigned __int64 scannedItems);
    void NoteAssignment(BOOL discovery);
    void NoteBackpressurePause();

    // --- listing cache ---
    void NoteDirCache(BOOL hit);
    void NoteDuplicateListingFetch();

    // --- TLS ---
    void NoteHandshake(CFTPMeteredHandshake result, CMonotonicDuration durationMs);

    // --- local work ---
    void NoteDiskWork(CMonotonicDuration waitMs, CMonotonicDuration serviceMs);
    void NoteBuffer(BOOL reused);
    void NoteUIRefresh(CMonotonicDuration durationMs);

    // Writes the JSON result document. Called once from the operation's
    // teardown; a second call is ignored so a cancelled-then-finished operation
    // cannot produce two conflicting records. 'queueCount'/'queueRejected'/
    // 'queueHighWaterMark' come from CFTPQueue so queue bounds appear in the
    // same document as the transfer phases they explain.
    void Report(int queueCount, int queueRejected, int queueHighWaterMark);

protected:
    // Serializes the counters into 'buf'. Split out so the file write happens
    // outside MetricsCritSect - formatting holds the section, I/O does not.
    int FormatJSON(char* buf, int bufSize, int queueCount, int queueRejected,
                   int queueHighWaterMark);

    // Returns the millisecond value at the given percentile of the latency
    // histogram, or 0 when no sample exists. The value is the upper bound of the
    // bucket the percentile falls into, so it is an over-estimate by design -
    // never a number that flatters the measurement.
    unsigned __int64 LatencyPercentile(int percentile);
};

// Directory that receives the JSON documents. Empty means "use %TEMP%". The
// value is configuration, not a command line, so it is never executed.
extern CPathW FTPMetricsOutputDir;
