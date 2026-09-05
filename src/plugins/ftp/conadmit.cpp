// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA)

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

CFTPConnectionAdmission FTPConnectionAdmission;

// Endpoints with no held lease are kept so peak/denial statistics survive a
// disconnect-reconnect cycle, but the table is compacted once it grows past
// this bound so a long session that visits many servers cannot grow without
// limit.
#define FTPADMISSION_MAX_TRACKED_ENDPOINTS 64

// Builds the key the limit is enforced on. The host is compared case
// insensitively because DNS names are, and because the same server reached as
// "Server" and "server" must share one bound.
static void BuildAdmissionKey(char* key, int keySize, const char* host, int port)
{
    _snprintf_s(key, keySize, _TRUNCATE, "%s:%d", host != NULL ? host : "", port);
    for (char* s = key; *s != 0; s++)
    {
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s - 'A' + 'a');
    }
}

CFTPConnectionLease::CFTPConnectionLease()
{
    Held = FALSE;
    Key[0] = 0;
}

CFTPConnectionLease::~CFTPConnectionLease()
{
    // A lease that reaches its destructor still held means an owner forgot to
    // release it on some path; the counter would then never drop and the server
    // would look permanently full. Report it rather than silently leaking the
    // slot, and free it so the process stays usable.
    if (Held)
    {
        TRACE_E("CFTPConnectionLease: destroyed while still held; releasing it to keep admission counts correct");
        FTPConnectionAdmission.Release(this);
    }
}

CFTPConnectionAdmission::CFTPConnectionAdmission() : Endpoints(4, 8)
{
    HANDLES(InitializeCriticalSection(&AdmissionCritSect));
    Usable = TRUE;
}

CFTPConnectionAdmission::~CFTPConnectionAdmission()
{
    // Marked before the section is destroyed, so a lease released during static
    // teardown returns without entering it.
    Usable = FALSE;
    HANDLES(DeleteCriticalSection(&AdmissionCritSect));
}

BOOL CFTPConnectionAdmission::Acquire(CFTPConnectionLease* lease, const char* host, int port,
                                      const char* user, int proxyUID, BOOL encryptedControl,
                                      BOOL encryptedData, int maxConnections)
{
    if (!Usable)
        return FALSE; // static teardown: nothing can meaningfully be admitted any more
    if (lease == NULL)
        return FALSE;
    if (lease->Held)
    {
        // Re-acquiring an already held lease would double-count one connection.
        TRACE_E("CFTPConnectionAdmission::Acquire(): the lease is already held");
        return TRUE;
    }

    char key[FTPADMISSION_KEY_MAX];
    BuildAdmissionKey(key, SizeOf(key), host, port);

    // A stored maximum of zero or a negative value is not a usable bound: treat
    // it as "no limit" instead of refusing every connection.
    if (maxConnections <= 0)
        maxConnections = FTPADMISSION_UNLIMITED;

    BOOL granted = FALSE;
    HANDLES(EnterCriticalSection(&AdmissionCritSect));

    int found = -1;
    for (int i = 0; i < Endpoints.Count; i++)
    {
        if (strcmp(Endpoints[i].Key, key) == 0)
        {
            found = i;
            break;
        }
    }

    if (found == -1)
    {
        // Compact idle entries before growing, so a session that browses many
        // servers keeps a bounded table.
        if (Endpoints.Count >= FTPADMISSION_MAX_TRACKED_ENDPOINTS)
        {
            for (int i = Endpoints.Count - 1; i >= 0; i--)
            {
                if (Endpoints[i].HeldCount == 0)
                {
                    Endpoints.Delete(i);
                    if (!Endpoints.IsGood())
                        Endpoints.ResetState();
                }
            }
        }
        CEndpointUsage usage;
        memset(&usage, 0, sizeof(usage));
        StringCchCopyNA(usage.Key, SizeOf(usage.Key), key, SizeOf(usage.Key));
        found = Endpoints.Add(usage);
        if (!Endpoints.IsGood())
        {
            Endpoints.ResetState();
            found = -1;
        }
    }

    if (found != -1)
    {
        CEndpointUsage& usage = Endpoints[found];
        // The identity is refreshed on every acquisition: it is diagnostic
        // context for the most recent connection, not part of the bound.
        _snprintf_s(usage.Identity, SizeOf(usage.Identity), _TRUNCATE, "%s@%s proxy=%d ctrl=%s data=%s",
                    user != NULL ? user : "", key, proxyUID,
                    encryptedControl ? "tls" : "plain", encryptedData ? "tls" : "plain");
        if (maxConnections == FTPADMISSION_UNLIMITED || usage.HeldCount < maxConnections)
        {
            usage.HeldCount++;
            if (usage.HeldCount > usage.PeakCount)
                usage.PeakCount = usage.HeldCount;
            StringCchCopyNA(lease->Key, SizeOf(lease->Key), key, SizeOf(lease->Key));
            lease->Held = TRUE;
            granted = TRUE;
        }
        else
            usage.DenialCount++;
    }
    else
    {
        // Without a table entry the bound cannot be tracked. Refuse rather than
        // grant an uncounted connection: an unlimited configuration would then
        // still work through the entry that already exists, and a bounded one
        // stays bounded.
        TRACE_E(LOW_MEMORY);
    }

    HANDLES(LeaveCriticalSection(&AdmissionCritSect));
    return granted;
}

void CFTPConnectionAdmission::Release(CFTPConnectionLease* lease)
{
    if (lease == NULL || !lease->Held)
        return; // unconditional release from every failure path must be harmless
    if (!Usable)
    {
        // Static teardown: the section is already gone and the counts no longer
        // matter. Clear the lease so its destructor does not report it as leaked.
        lease->Held = FALSE;
        lease->Key[0] = 0;
        return;
    }

    HANDLES(EnterCriticalSection(&AdmissionCritSect));
    for (int i = 0; i < Endpoints.Count; i++)
    {
        if (strcmp(Endpoints[i].Key, lease->Key) == 0)
        {
            if (Endpoints[i].HeldCount > 0)
                Endpoints[i].HeldCount--;
            else
                TRACE_E("CFTPConnectionAdmission::Release(): unbalanced release for " << lease->Key);
            break;
        }
    }
    lease->Held = FALSE;
    lease->Key[0] = 0;
    HANDLES(LeaveCriticalSection(&AdmissionCritSect));
}

void CFTPConnectionAdmission::Transfer(CFTPConnectionLease* from, CFTPConnectionLease* to)
{
    if (from == NULL || to == NULL || !from->Held || !Usable)
        return;
    HANDLES(EnterCriticalSection(&AdmissionCritSect));
    if (to->Held)
    {
        // The destination already counts a connection of its own; releasing the
        // source keeps the total right instead of losing one slot forever.
        HANDLES(LeaveCriticalSection(&AdmissionCritSect));
        TRACE_E("CFTPConnectionAdmission::Transfer(): the destination lease is already held");
        Release(from);
        return;
    }
    StringCchCopyNA(to->Key, SizeOf(to->Key), from->Key, SizeOf(to->Key));
    to->Held = TRUE;
    from->Held = FALSE;
    from->Key[0] = 0;
    HANDLES(LeaveCriticalSection(&AdmissionCritSect));
}

int CFTPConnectionAdmission::GetAvailable(const char* host, int port, int maxConnections)
{
    if (maxConnections <= 0 || !Usable)
        return TRANSFERWORKERS_MAX; // unlimited: never the binding constraint on a worker target

    char key[FTPADMISSION_KEY_MAX];
    BuildAdmissionKey(key, SizeOf(key), host, port);

    int available = maxConnections;
    HANDLES(EnterCriticalSection(&AdmissionCritSect));
    for (int i = 0; i < Endpoints.Count; i++)
    {
        if (strcmp(Endpoints[i].Key, key) == 0)
        {
            available = maxConnections - Endpoints[i].HeldCount;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&AdmissionCritSect));
    return available > 0 ? available : 0;
}

int CFTPConnectionAdmission::GetHeldCount(const char* host, int port)
{
    if (!Usable)
        return 0;

    char key[FTPADMISSION_KEY_MAX];
    BuildAdmissionKey(key, SizeOf(key), host, port);

    int held = 0;
    HANDLES(EnterCriticalSection(&AdmissionCritSect));
    for (int i = 0; i < Endpoints.Count; i++)
    {
        if (strcmp(Endpoints[i].Key, key) == 0)
        {
            held = Endpoints[i].HeldCount;
            break;
        }
    }
    HANDLES(LeaveCriticalSection(&AdmissionCritSect));
    return held;
}

int GetEffectiveMaxConnections(int useMaxCon, int maxCon)
{
    // The per-server setting is a tri-state whose value 2 means "use whatever
    // the global default says"; only the 1 case carries its own number.
    if (useMaxCon == 2)
        return Config.UseMaxConcurrentConnections ? Config.MaxConcurrentConnections : FTPADMISSION_UNLIMITED;
    if (useMaxCon == 1)
        return maxCon > 0 ? maxCon : FTPADMISSION_UNLIMITED;
    return FTPADMISSION_UNLIMITED; // 0 = explicitly no limit
}
