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
