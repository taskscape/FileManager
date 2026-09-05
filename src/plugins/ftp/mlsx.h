// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
// RFC 3659 machine-readable listings (ftp-improvements.md section 4)
//
// MLSD returns one entry per line as a set of "fact=value;" pairs followed by a
// single space and the pathname. Unlike LIST it has a defined grammar, so the
// size and modification time it reports are trustworthy enough to avoid a
// per-file SIZE or MDTM round trip.
//
// This parser is deliberately small and bounded: it never allocates, never
// scans past the line it was given, and reports every fact it could not
// establish as unknown rather than substituting a zero. A missing size is not a
// zero-byte file, and a missing timestamp is not the epoch - downstream code
// depends on being able to tell those apart when deciding about resume offsets
// and overwrite prompts.
// ****************************************************************************

// Per-authenticated-session capability state. "Unknown" means FEAT has not been
// answered yet for this session; it is never persisted as an assumption about a
// server contacted earlier, because the same address can be a different server
// tomorrow.
enum CFTPMLSxState
{
    mlsxUnknown,
    mlsxSupported,
    mlsxUnsupported
};

// Per-session MLSD/MLST capability learned from FEAT (never persisted).
class CFTPMLSxSupport
{
protected:
    // A single aligned LONG updated with interlocked operations: the state is
    // read on every listing and written once per login, so it does not justify
    // a critical section of its own (and adding one would need a new entry in
    // servers\critsect.txt for no benefit).
    volatile LONG State;

public:
    CFTPMLSxSupport() { State = mlsxUnknown; }

    CFTPMLSxState Get() const { return (CFTPMLSxState)State; }
    void Set(CFTPMLSxState state) { InterlockedExchange((volatile LONG*)&State, (LONG)state); }

    // Called when a connection is dropped or handed over: capabilities belong to
    // an authenticated session, so they must be renegotiated afterwards.
    void Reset() { Set(mlsxUnknown); }
};

// What an MLSx "type" fact says the entry is. cdir/pdir are the listed
// directory itself and its parent; they must never be followed recursively.
enum CMLSxEntryKind
{
    mlsxEntryUnknown,
    mlsxEntryFile,
    mlsxEntryDir,
    mlsxEntryCurrentDir, // type=cdir
    mlsxEntryParentDir,  // type=pdir
    mlsxEntryOther       // a type this client does not act on (device, socket, ...)
};

struct CMLSxEntry
{
    char Name[FTP_MAX_PATH]; // pathname exactly as sent, including any internal spaces
    int NameLen;

    CMLSxEntryKind Kind;
    BOOL IsLink; // type=OS.unix=slink / OS.unix=symlink: a link whose target is deliberately not inferred

    BOOL SizeKnown;
    CQuadWord Size;

    BOOL ModifyKnown;
    SYSTEMTIME ModifyUTC; // RFC 3659 timestamps are UTC; conversion to local time is the caller's decision

    BOOL UnixModeKnown;
    WORD UnixMode; // from the UNIX.mode fact (octal in the wire format)

    BOOL PermKnown;
    char Perm[32]; // RFC 3659 "perm" fact, lower-cased
};

// Parses one MLSx line (without its CRLF). Returns FALSE when the line has no
// pathname or is otherwise not a valid entry; 'entry' is fully initialized in
// every case, so a caller may inspect it after a failure without reading stale
// data. Facts this client does not use are ignored, as RFC 3659 requires.
BOOL ParseMLSxLine(const char* line, int lineLen, CMLSxEntry* entry);

// Scans a multi-line FEAT reply for the MLST feature. RFC 3659 advertises MLSD
// support through the MLST line (which also carries the supported facts), so
// requiring a literal "MLSD" line would reject conforming servers.
// Returns TRUE when the server advertises MLST.
BOOL FEATReplyAdvertisesMLSx(const char* reply, int replyLen);

// Splits a listing buffer into lines for ParseMLSxLine. '*listing' is advanced
// past the returned line. Returns FALSE at the end of the buffer.
BOOL GetNextMLSxLine(const char** listing, const char* listingEnd,
                     const char** lineStart, int* lineLen);
