// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Included inside NativeSafetyTests.cpp's anonymous namespace; udf_bounds.h needs no std headers.
#include "../../src/plugins/uniso/udf_bounds.h"

// The UnISO UDF reader trusted image-supplied offsets and lengths and read/wrote past sector and
// extent buffers on crafted discs. Every call site now gates on UDFRangeInBounds; this locks in the
// overflow-safe contract, especially the wraparound case a naive "offset + length <= size" misses.
int TestUdfBoundsCheck()
{
    const unsigned __int64 sector = 2048;

    // A zero-length range exactly at the end of the buffer is valid (an empty allocation area).
    if (!UDFRangeInBounds(sector, 0, sector))
        return Fail("UDFRangeInBounds rejected an empty range at the buffer end");
    // A descriptor that fits exactly must be accepted.
    if (!UDFRangeInBounds(2028, 20, sector))
        return Fail("UDFRangeInBounds rejected a descriptor that fits exactly");
    if (!UDFRangeInBounds(0, sector, sector))
        return Fail("UDFRangeInBounds rejected a full-buffer range");

    // One byte past the end, in offset or in length, must be refused.
    if (UDFRangeInBounds(sector + 1, 0, sector))
        return Fail("UDFRangeInBounds accepted an offset past the buffer");
    if (UDFRangeInBounds(2028, 21, sector))
        return Fail("UDFRangeInBounds accepted a descriptor one byte past the buffer");

    // The File Entry L_EA case: a 0xF000-byte extended-attribute area in a 2 KB sector must be refused.
    if (UDFRangeInBounds(216, 0xF000, sector))
        return Fail("UDFRangeInBounds accepted an oversized extended-attribute area");

    // The overflow case that defeats "offset + length <= size": offset + length wraps to a small value.
    const unsigned __int64 maxU64 = ~0ull;
    if (UDFRangeInBounds(1000, maxU64, sector))
        return Fail("UDFRangeInBounds accepted a length that overflows offset + length");
    if (UDFRangeInBounds(16, maxU64 - 8, sector))
        return Fail("UDFRangeInBounds accepted a near-max length that wraps");

    // A VAT buffer check: writing 4096 bytes at offset 0 of a 16-byte buffer must be refused.
    if (UDFRangeInBounds(0, 4096, 16))
        return Fail("UDFRangeInBounds accepted a write larger than the VAT buffer");

    // A 50 GB image needs about 93 MiB of entries; the former 64 MiB cutoff rejected it.
    const unsigned __int64 largeTrack = 50000000000ull;
    const unsigned int largePartition = (unsigned int)(largeTrack / sector);
    const unsigned __int64 largeVat = (unsigned __int64)largePartition * 4 + 152;
    if (!UDFVatLengthInBounds(largeVat, largePartition, largeTrack, (unsigned int)sector))
        return Fail("VAT length validation rejected a valid large virtual partition");
    if (!UDFVatLengthInBounds((unsigned __int64)largePartition * 4 + 0xFFFF,
                              largePartition, largeTrack, (unsigned int)sector))
        return Fail("VAT length validation rejected the largest representable header");
    if (UDFVatLengthInBounds((unsigned __int64)largePartition * 4 + 0x10000,
                             largePartition, largeTrack, (unsigned int)sector))
        return Fail("VAT length validation accepted a table beyond partition geometry");
    if (UDFVatLengthInBounds(largeVat, largePartition, 1024 * sector, (unsigned int)sector))
        return Fail("VAT length validation trusted a partition larger than the backing track");
    if (UDFVatLengthInBounds(0x100000010ull, 0xFFFFFFFFu, maxU64, (unsigned int)sector) ||
        UDFVatLengthInBounds(0, largePartition, largeTrack, (unsigned int)sector) ||
        UDFVatLengthInBounds(16, 0, largeTrack, (unsigned int)sector) ||
        UDFVatLengthInBounds(16, largePartition, largeTrack, 0))
        return Fail("VAT length validation accepted a truncated length or invalid geometry");

    return 0;
}
