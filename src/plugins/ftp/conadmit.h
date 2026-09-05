// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
// FTP connection admission control (ftp-improvements.md section 2)
//
// A configuration field is not a connection limit. Before this plug-in may grow
// a worker pool automatically it needs one place in the process that actually
// knows how many authenticated control connections exist per server, and that
// every path - automatic worker growth, manual worker addition from the
// operation dialog, worker reconnect and panel browsing - must pass through.
//
// A lease represents one logical connection as the existing configuration text
// defines it: a control connection together with the data connections it opens
// counts as one. Actual socket counts are a separate measurement (see
// CFTPTransferMetrics), not a second limit.
//
// Leases are keyed by host and port, because "max number of concurrent
// connections to the server" is a statement about the server. The finer
// endpoint identity (user, proxy, security mode) is recorded alongside so a
// diagnostic dump can tell two accounts on one host apart without weakening the
// enforced bound. Limits imposed by the server itself, or across other client
// applications, remain server-enforced - this only governs what we originate.
// ****************************************************************************

#define FTPADMISSION_KEY_MAX 280  // "host:port" bounded by HOST_MAX_SIZE plus the port text
#define FTPADMISSION_ID_MAX 512   // full endpoint identity, for diagnostics only
#define FTPADMISSION_UNLIMITED -1 // no configured maximum

// One lease, held for as long as its owner may keep a control connection open.
// The owner (a worker or a panel control connection) embeds this by value, so
// there is no separate allocation to leak on an error path.
class CFTPConnectionLease
{
protected:
    friend class CFTPConnectionAdmission;

    BOOL Held;
    char Key[FTPADMISSION_KEY_MAX]; // the key the lease was counted against; a lease outlives table compaction

public:
    CFTPConnectionLease();
    ~CFTPConnectionLease();

    BOOL IsHeld() const { return Held; }
};

class CFTPConnectionAdmission
{
protected:
    // Entered from worker sockets, the panel connection and the operation
    // dialog. It is a leaf section: nothing else may be entered from inside it.
    CRITICAL_SECTION AdmissionCritSect;

    struct CEndpointUsage
    {
        char Key[FTPADMISSION_KEY_MAX];   // host:port - what the limit is enforced on
        char Identity[FTPADMISSION_ID_MAX]; // last observed full identity, for diagnostics
        int HeldCount;                    // leases currently held
        int PeakCount;                    // largest simultaneous count observed
        int DenialCount;                  // acquisitions refused by this bound
    };

    TDirectArray<CEndpointUsage> Endpoints;

    // The controller is a global object, so a socket destroyed during static
    // teardown could reach it after its critical section is gone. This flag
    // makes such a late call a no-op instead of touching freed state; by then
    // the counts no longer matter, because the process is going away.
    BOOL Usable;

public:
    CFTPConnectionAdmission();
    ~CFTPConnectionAdmission();

    // Takes one lease for 'host':'port' when 'maxConnections' allows it.
    // 'maxConnections' is FTPADMISSION_UNLIMITED or a positive bound; a
    // non-positive configured value is treated as unlimited rather than as a
    // bound of zero, which would deadlock every connection path.
    // Returns FALSE without modifying 'lease' when the bound is already reached.
    BOOL Acquire(CFTPConnectionLease* lease, const char* host, int port, const char* user,
                 int proxyUID, BOOL encryptedControl, BOOL encryptedData, int maxConnections);

    // Releases a held lease. Safe to call on a lease that is not held, so every
    // failure and shutdown path can call it unconditionally.
    void Release(CFTPConnectionLease* lease);

    // Moves a held lease from 'from' to 'to' without touching the counter. Used
    // when the panel hands its already authenticated control connection to
    // worker 0: the number of connections to the server does not change, so
    // releasing and re-acquiring could spuriously fail against a full bound.
    void Transfer(CFTPConnectionLease* from, CFTPConnectionLease* to);

    // Number of further leases available for 'host':'port' under
    // 'maxConnections'. Returns a large value when unlimited. Used by the growth
    // logic to decide whether adding a worker can succeed before it allocates
    // one; it is advisory, since another thread may take the slot first -
    // Acquire() remains the only authority.
    int GetAvailable(const char* host, int port, int maxConnections);

    // Current lease count for 'host':'port' (diagnostics and UI text).
    int GetHeldCount(const char* host, int port);
};

// One controller per process, shared by every operation and panel connection.
extern CFTPConnectionAdmission FTPConnectionAdmission;

// Resolves the effective connection maximum for a server: the per-server
// setting when it is enabled, otherwise the global default, otherwise
// unlimited. Kept in one function so every admission call site applies the
// same interpretation of the two-level configuration.
// 'useMaxCon' uses the stored tri-state (0 = no limit, 1 = use 'maxCon',
// 2 = follow the global default).
int GetEffectiveMaxConnections(int useMaxCon, int maxCon);
