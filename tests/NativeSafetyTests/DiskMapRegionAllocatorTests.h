// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Included inside NativeSafetyTests.cpp's anonymous namespace; <new> and <vector> come from that file's top.
#include "../../src/plugins/diskmap/DiskMap/Utils.CRegionAllocator.h"

// Exposes the block bookkeeping so the test can prove growth past the former fixed limit.
class CInspectableRegionAllocator : public CRegionAllocator
{
public:
    explicit CInspectableRegionAllocator(size_t blockSize) : CRegionAllocator(blockSize) {}
    int BlockCount() const { return this->_blockCount; }
};

int TestDiskMapRegionAllocator()
{
    // A whole-volume DiskMap layout needs far more blocks than the former 256-entry array held,
    // and the overflowing block pointers corrupted Salamander's shared heap. Small blocks reach
    // that count quickly; a guard region after the allocator object catches any write past it.
    const size_t blockSize = 64;
    const int allocations = 4 * INITIAL_BLOCK_CAPACITY + 3;
    const size_t guardBytes = 64 * 1024;
    const unsigned char guardValue = 0xA5;

    std::vector<unsigned char> storage(sizeof(CInspectableRegionAllocator) + guardBytes, guardValue);
    CInspectableRegionAllocator* alloc = new (storage.data()) CInspectableRegionAllocator(blockSize);
    int result = 0;
    std::vector<BYTE*> blocks;
    for (int i = 0; i < allocations && result == 0; i++)
    {
        BYTE* p = (BYTE*)alloc->Allocate(blockSize);
        if (p == NULL)
            result = Fail("DiskMap region allocator refused a block past its initial capacity");
        else
        {
            memset(p, (unsigned char)i, blockSize);
            blocks.push_back(p);
        }
    }
    for (size_t offset = sizeof(CInspectableRegionAllocator); result == 0 && offset < storage.size(); offset++)
    {
        if (storage[offset] != guardValue)
            result = Fail("DiskMap region allocator wrote past its own object");
    }
    if (result == 0 && alloc->BlockCount() != allocations)
        result = Fail("DiskMap region allocator did not record every block");
    for (int i = 0; result == 0 && i < allocations; i++)
    {
        for (size_t b = 0; b < blockSize; b++)
        {
            if (blocks[i][b] != (unsigned char)i)
            {
                result = Fail("DiskMap region allocator handed out overlapping memory");
                break;
            }
        }
    }
    if (result == 0)
    {
        // Rebuilding a layout (window resize) must reuse retained blocks without growing the list.
        alloc->FreeAll(FALSE);
        for (int i = 0; result == 0 && i < allocations; i++)
        {
            if (alloc->Allocate(blockSize) != blocks[i])
                result = Fail("DiskMap region allocator did not reuse its retained blocks");
        }
        if (result == 0 && alloc->BlockCount() != allocations)
            result = Fail("DiskMap region allocator grew while reusing retained blocks");
    }
    // Oversized requests must fail cleanly in every build, not only when _DEBUG is defined.
    if (result == 0 && alloc->Allocate(blockSize + 1) != NULL)
        result = Fail("DiskMap region allocator accepted a request larger than its block");
    if (result == 0)
    {
        alloc->FreeAll(TRUE);
        if (alloc->BlockCount() != 0 || alloc->Allocate(blockSize / 2) == NULL)
            result = Fail("DiskMap region allocator could not restart after releasing its blocks");
    }
    alloc->~CInspectableRegionAllocator();
    return result;
}
