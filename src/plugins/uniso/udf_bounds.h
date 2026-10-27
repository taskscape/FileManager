// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Lengths and offsets in the UDF on-disc structures come straight from the image. A crafted image
// can set them to reach past the sector or extent buffer being parsed, so every image-supplied range
// is validated with this helper before use. The subtraction form is deliberate: the obvious
// "offset + length <= bufferSize" wraps around when the image overflows the sum and would then pass.
static inline bool UDFRangeInBounds(unsigned __int64 offset, unsigned __int64 length, unsigned __int64 bufferSize)
{
    return offset <= bufferSize && length <= bufferSize - offset;
}

// A VAT has four bytes per logical block plus a UDF 2.x header whose length is a Uint16.
// Bound entries by both the partition and the backing track before multiplying, and reject
// lengths that the reader's DWORD offsets cannot represent without first narrowing them.
static inline bool UDFVatLengthInBounds(unsigned __int64 length, unsigned int partitionBlocks,
                                       unsigned __int64 trackBytes, unsigned int rawSectorSize)
{
    if (length == 0 || length > 0xFFFFFFFFull || partitionBlocks == 0 || rawSectorSize == 0)
        return false;
    unsigned __int64 blocks = trackBytes / rawSectorSize;
    if (blocks > partitionBlocks)
        blocks = partitionBlocks;
    return blocks != 0 && length <= blocks * 4 + 0xFFFFull && length <= trackBytes;
}
