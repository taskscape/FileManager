// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

// Case-insensitive comparison of a fact name against a literal. Fact names are
// case independent in RFC 3659, so "Size", "SIZE" and "size" must all match.
static BOOL FactNameIs(const char* name, int nameLen, const char* literal)
{
    int literalLen = (int)strlen(literal);
    if (nameLen != literalLen)
        return FALSE;
    for (int i = 0; i < nameLen; i++)
    {
        char a = name[i];
        char b = literal[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = (char)(b - 'A' + 'a');
        if (a != b)
            return FALSE;
    }
    return TRUE;
}

static BOOL ValueIs(const char* value, int valueLen, const char* literal)
{
    return FactNameIs(value, valueLen, literal);
}

// Converts a decimal size with an explicit overflow check. A size that does not
// fit in 64 bits, or contains a non-digit, leaves the size unknown instead of
// silently truncating - a wrong size would corrupt resume decisions.
static BOOL ParseCheckedSize(const char* value, int valueLen, CQuadWord* size)
{
    if (valueLen <= 0 || valueLen > 20)
        return FALSE;
    unsigned __int64 result = 0;
    for (int i = 0; i < valueLen; i++)
    {
        if (value[i] < '0' || value[i] > '9')
            return FALSE;
        unsigned __int64 digit = (unsigned __int64)(value[i] - '0');
        if (result > (0xFFFFFFFFFFFFFFFFui64 - digit) / 10)
            return FALSE; // would overflow
        result = result * 10 + digit;
    }
    size->SetUI64(result);
    return TRUE;
}

static BOOL ReadFixedDigits(const char* s, int count, int* value)
{
    int result = 0;
    for (int i = 0; i < count; i++)
    {
        if (s[i] < '0' || s[i] > '9')
            return FALSE;
        result = result * 10 + (s[i] - '0');
    }
    *value = result;
    return TRUE;
}

// Parses an RFC 3659 time-val: YYYYMMDDHHMMSS with an optional ".sss" fraction.
// The value is UTC by definition; no local-time conversion happens here so the
// caller keeps that decision explicit.
static BOOL ParseMLSxTimestamp(const char* value, int valueLen, SYSTEMTIME* time)
{
    if (valueLen < 14)
        return FALSE;
    int year, month, day, hour, minute, second;
    if (!ReadFixedDigits(value, 4, &year) ||
        !ReadFixedDigits(value + 4, 2, &month) ||
        !ReadFixedDigits(value + 6, 2, &day) ||
        !ReadFixedDigits(value + 8, 2, &hour) ||
        !ReadFixedDigits(value + 10, 2, &minute) ||
        !ReadFixedDigits(value + 12, 2, &second))
    {
        return FALSE;
    }
    // Range validation matters: a malformed date must not reach the panel or the
    // file-time conversion, which would report a Windows error instead.
    if (year < 1601 || year > 9999 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour > 23 || minute > 59 || second > 60) // 60 = leap second, clamped below
    {
        return FALSE;
    }

    int milliseconds = 0;
    if (valueLen > 14 && value[14] == '.')
    {
        // Fractional seconds are optional and may have any number of digits;
        // only the first three are meaningful for SYSTEMTIME.
        int digits = 0;
        for (int i = 15; i < valueLen && value[i] >= '0' && value[i] <= '9'; i++)
        {
            if (digits < 3)
                milliseconds = milliseconds * 10 + (value[i] - '0');
            digits++;
        }
        while (digits < 3 && digits > 0)
        {
            milliseconds *= 10;
            digits++;
        }
    }

    memset(time, 0, sizeof(SYSTEMTIME));
    time->wYear = (WORD)year;
    time->wMonth = (WORD)month;
    time->wDay = (WORD)day;
    time->wHour = (WORD)hour;
    time->wMinute = (WORD)minute;
    time->wSecond = (WORD)(second > 59 ? 59 : second); // SYSTEMTIME has no leap second
    time->wMilliseconds = (WORD)milliseconds;

    // A syntactically valid but non-existent date (31 February) is rejected here
    // rather than being silently accepted by the caller.
    FILETIME probe;
    if (!SystemTimeToFileTime(time, &probe))
        return FALSE;
    return TRUE;
}

static BOOL ParseOctalMode(const char* value, int valueLen, WORD* mode)
{
    if (valueLen <= 0 || valueLen > 7)
        return FALSE;
    unsigned result = 0;
    for (int i = 0; i < valueLen; i++)
    {
        if (value[i] < '0' || value[i] > '7')
            return FALSE;
        result = result * 8 + (unsigned)(value[i] - '0');
    }
    if (result > 0xFFFF)
        return FALSE;
    *mode = (WORD)result;
    return TRUE;
}

BOOL ParseMLSxLine(const char* line, int lineLen, CMLSxEntry* entry)
{
    // Initialize everything up front so a caller inspecting a rejected line
    // cannot read values left over from the previous entry.
    memset(entry, 0, sizeof(*entry));
    entry->Kind = mlsxEntryUnknown;
    entry->Size.Set(0, 0);

    if (line == NULL || lineLen <= 0)
        return FALSE;

    // Facts end at the first space; the rest of the line - spaces included - is
    // the pathname. Splitting on later spaces would break every file name that
    // contains one, which is exactly what MLSD is meant to fix.
    int factsEnd = 0;
    while (factsEnd < lineLen && line[factsEnd] != ' ')
        factsEnd++;
    if (factsEnd >= lineLen)
        return FALSE; // no separator, therefore no pathname: not a valid entry

    const char* name = line + factsEnd + 1;
    int nameLen = lineLen - factsEnd - 1;
    if (nameLen <= 0 || nameLen >= FTP_MAX_PATH)
        return FALSE; // an empty or over-long name is not usable as a path component
    memcpy(entry->Name, name, nameLen);
    entry->Name[nameLen] = 0;
    entry->NameLen = nameLen;

    int pos = 0;
    while (pos < factsEnd)
    {
        int factStart = pos;
        while (pos < factsEnd && line[pos] != ';')
            pos++;
        int factLen = pos - factStart;
        if (pos < factsEnd)
            pos++; // step over the ';'
        if (factLen == 0)
            continue; // tolerate an empty fact, e.g. the trailing ';'

        const char* fact = line + factStart;
        int eq = 0;
        while (eq < factLen && fact[eq] != '=')
            eq++;
        if (eq >= factLen)
            continue; // a fact without a value carries no information; ignore it

        const char* factName = fact;
        int factNameLen = eq;
        const char* factValue = fact + eq + 1;
        int factValueLen = factLen - eq - 1;

        if (FactNameIs(factName, factNameLen, "type"))
        {
            if (ValueIs(factValue, factValueLen, "file"))
                entry->Kind = mlsxEntryFile;
            else if (ValueIs(factValue, factValueLen, "dir"))
                entry->Kind = mlsxEntryDir;
            else if (ValueIs(factValue, factValueLen, "cdir"))
                entry->Kind = mlsxEntryCurrentDir;
            else if (ValueIs(factValue, factValueLen, "pdir"))
                entry->Kind = mlsxEntryParentDir;
            else if (factValueLen > 8 && FactNameIs(factValue, 8, "OS.unix="))
            {
                // "OS.unix=slink:<target>" and "OS.unix=symlink" both mark a
                // link. The target after the colon is not read: RFC 3659 does
                // not define how a target containing a semicolon is escaped, so
                // inferring one here could produce a wrong path.
                const char* osValue = factValue + 8;
                int osValueLen = factValueLen - 8;
                int colon = 0;
                while (colon < osValueLen && osValue[colon] != ':')
                    colon++;
                if (FactNameIs(osValue, colon, "slink") || FactNameIs(osValue, colon, "symlink"))
                {
                    entry->IsLink = TRUE;
                    entry->Kind = mlsxEntryOther; // the link's own type is unknown until it is resolved
                }
                else
                    entry->Kind = mlsxEntryOther;
            }
            else
                entry->Kind = mlsxEntryOther;
        }
        else if (FactNameIs(factName, factNameLen, "size") ||
                 FactNameIs(factName, factNameLen, "sizd"))
        {
            // A rejected size leaves SizeKnown FALSE, which downstream code
            // treats as "unknown size", not as zero.
            entry->SizeKnown = ParseCheckedSize(factValue, factValueLen, &entry->Size);
        }
        else if (FactNameIs(factName, factNameLen, "modify"))
        {
            entry->ModifyKnown = ParseMLSxTimestamp(factValue, factValueLen, &entry->ModifyUTC);
        }
        else if (FactNameIs(factName, factNameLen, "unix.mode") ||
                 FactNameIs(factName, factNameLen, "UNIX.mode"))
        {
            entry->UnixModeKnown = ParseOctalMode(factValue, factValueLen, &entry->UnixMode);
        }
        else if (FactNameIs(factName, factNameLen, "perm"))
        {
            int copyLen = factValueLen < SizeOf(entry->Perm) - 1 ? factValueLen : SizeOf(entry->Perm) - 1;
            for (int i = 0; i < copyLen; i++)
            {
                char c = factValue[i];
                entry->Perm[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
            }
            entry->Perm[copyLen] = 0;
            entry->PermKnown = TRUE;
        }
        // Every other fact is ignored on purpose: RFC 3659 lets servers add
        // facts freely, and an unknown fact must not make an entry unusable.
    }

    return TRUE;
}

BOOL FEATReplyAdvertisesMLSx(const char* reply, int replyLen)
{
    if (reply == NULL || replyLen <= 0)
        return FALSE;
    const char* s = reply;
    const char* end = reply + replyLen;
    while (s < end)
    {
        // Feature lines are indented by one space; the first and last lines carry
        // the 211 reply code and are not features.
        const char* lineStart = s;
        while (s < end && *s != '\r' && *s != '\n')
            s++;
        const char* lineEnd = s;
        while (s < end && (*s == '\r' || *s == '\n'))
            s++;

        while (lineStart < lineEnd && (*lineStart == ' ' || *lineStart == '\t'))
            lineStart++;
        int lineLen = (int)(lineEnd - lineStart);
        // The MLST line lists the supported facts after the keyword, so match a
        // prefix rather than the whole line. MLSD availability follows from MLST
        // per RFC 3659 section 7.8; a server advertising only "MLSD" is
        // non-conforming, but accepting it costs nothing.
        if (lineLen >= 4 && FactNameIs(lineStart, 4, "MLST"))
            return TRUE;
        if (lineLen >= 4 && FactNameIs(lineStart, 4, "MLSD"))
            return TRUE;
    }
    return FALSE;
}

BOOL GetNextMLSxLine(const char** listing, const char* listingEnd,
                     const char** lineStart, int* lineLen)
{
    const char* s = *listing;
    // Skip blank lines: some servers terminate the data stream with an extra
    // CRLF, which is not an entry.
    while (s < listingEnd && (*s == '\r' || *s == '\n'))
        s++;
    if (s >= listingEnd)
    {
        *listing = s;
        return FALSE;
    }
    const char* start = s;
    while (s < listingEnd && *s != '\r' && *s != '\n')
        s++;
    *lineStart = start;
    *lineLen = (int)(s - start);
    *listing = s;
    return TRUE;
}
