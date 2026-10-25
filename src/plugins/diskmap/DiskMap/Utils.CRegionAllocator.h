// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <limits.h> // INT_MAX bounds the block-list growth below

#define INITIAL_BLOCK_CAPACITY 256
#define DEFAULT_BLOCK_SIZE 1024 * 1024 * 1 //1MB

// Bump allocator that hands out memory from large blocks.
class CRegionAllocator
{
protected:
    size_t _blockSize;
    int _blockCount;
    int _blockCapacity;

    int _currentBlock;
    size_t _lastBlockRemSize;
    BYTE* _lastBlockPosition;

    // Whole-volume treemaps need far more than 256 blocks (C:\ with ~15M files needs ~1 GB of cushions);
    // the block list grows on demand, because a fixed array was overrun into the shared Salamander heap.
    BYTE** _blocks;

    // Grows the block list so another block can be recorded; on failure the existing list stays intact.
    BOOL EnsureBlockSlot()
    {
        if (this->_blockCount < this->_blockCapacity)
            return TRUE;
        if (this->_blockCapacity > INT_MAX / 2)
            return FALSE;
        int newCapacity = (this->_blockCapacity > 0) ? this->_blockCapacity * 2 : INITIAL_BLOCK_CAPACITY;
        BYTE** newBlocks = (BYTE**)realloc(this->_blocks, (size_t)newCapacity * sizeof(BYTE*));
        if (newBlocks == NULL)
            return FALSE;
        this->_blocks = newBlocks;
        this->_blockCapacity = newCapacity;
        return TRUE;
    }

public:
    CRegionAllocator(size_t blockSize = DEFAULT_BLOCK_SIZE)
    {
        this->_blockSize = blockSize;
        this->_currentBlock = -1;
        this->_blockCount = 0;
        this->_blockCapacity = 0;
        this->_blocks = NULL;
        this->_lastBlockRemSize = 0;
        this->_lastBlockPosition = NULL;
    }
    ~CRegionAllocator()
    {
        for (int i = 0; i < this->_blockCount; i++)
        {
            free(this->_blocks[i]);
        }
        free(this->_blocks);
        this->_blocks = NULL;
        this->_blockCount = 0;
        this->_blockCapacity = 0;
    }
    // The allocator owns its block list; a copy would free the same blocks twice.
    CRegionAllocator(const CRegionAllocator&) = delete;
    CRegionAllocator& operator=(const CRegionAllocator&) = delete;

    // Returns NULL when the request cannot be satisfied; callers allocate through non-throwing
    // placement new, so a NULL result skips construction instead of writing through NULL.
    void* Allocate(size_t size)
    {
        // An oversized request would overrun its block, so it is refused in every build, not only _DEBUG.
        if (size > this->_blockSize)
        {
            return NULL;
        }

        if (size > this->_lastBlockRemSize)
        {
            if (this->_currentBlock + 1 == this->_blockCount)
            {
                if (!this->EnsureBlockSlot())
                    return NULL;
                BYTE* block = (BYTE*)malloc(this->_blockSize);
                if (block == NULL)
                    return NULL;
                this->_blocks[this->_blockCount] = block;
                this->_blockCount++;
            }
            this->_currentBlock++;
            this->_lastBlockRemSize = this->_blockSize;
            this->_lastBlockPosition = this->_blocks[this->_currentBlock];
        }

        void* p = this->_lastBlockPosition;
        this->_lastBlockPosition += size;
        this->_lastBlockRemSize -= size;
        return p;
    }
    void FreeAll(BOOL deallocate = TRUE)
    {
        this->_currentBlock = -1;
        this->_lastBlockRemSize = 0;
        this->_lastBlockPosition = NULL;
        if (deallocate)
        {
            for (int i = 0; i < this->_blockCount; i++)
            {
                free(this->_blocks[i]);
            }
            this->_blockCount = 0;
        }
    }
};
