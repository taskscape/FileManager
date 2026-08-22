// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>

#include "handles.h"

// Owns a temporary GDI bitmap so early returns between creation and use cannot
// leak the handle; deletion goes through the tracked handle API.
class CScopedGDIBitmap
{
public:
    CScopedGDIBitmap() : Bitmap(NULL)
    {
    }

    explicit CScopedGDIBitmap(HBITMAP bitmap) : Bitmap(bitmap)
    {
    }

    ~CScopedGDIBitmap()
    {
        // Destruction must not replace the failure code a caller is returning.
        const DWORD error = GetLastError();
        Reset();
        SetLastError(error);
    }

private:
    CScopedGDIBitmap(const CScopedGDIBitmap&);
    CScopedGDIBitmap& operator=(const CScopedGDIBitmap&);

public:
    BOOL IsValid() const
    {
        return Bitmap != NULL;
    }

    HBITMAP Get() const
    {
        return Bitmap;
    }

    // Out-parameter form for legacy APIs that return an HBITMAP through HBITMAP&.
    HBITMAP* Put()
    {
        Reset();
        return &Bitmap;
    }

    void Reset(HBITMAP bitmap = NULL)
    {
        if (Bitmap != NULL)
            HANDLES(DeleteObject(Bitmap));
        Bitmap = bitmap;
    }

private:
    HBITMAP Bitmap;
};
