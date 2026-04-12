// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "bitmap.h"

// Internal helper: memory-DC bitmap used for flicker-free drawing of disabled buttons.
class CGuiBitmap : public CBitmap
{
public:
    HBITMAP CreateCopyBitmap()
    {
        CALL_STACK_MESSAGE1("CSharedBitmapAndDC::CreateCopyBitmap()");
        HDC hdc = HANDLES(GetDC(NULL));

        HBITMAP HCopyBitmap = HANDLES(CreateCompatibleBitmap(hdc, Width, Height));

        HDC hMemDC = HANDLES(CreateCompatibleDC(hdc));
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, HCopyBitmap);

        HBITMAP hOld = (HBITMAP)SelectObject(HMemDC, HOldBmp);

        HIMAGELIST hImageList = ImageList_Create(Width, Height, ILC_MASK | GetImageListColorFlags(), 1, 0);
        ImageList_AddMasked(hImageList, HBmp, GetSysColor(COLOR_BTNFACE)); // j.r. the color was hardcoded to 192,192,192 here, which caused issues with the XP look
        RECT r;
        r.left = 0;
        r.top = 0;
        r.right = Width;
        r.bottom = Height;
        FillRect(hMemDC, &r, (HBRUSH)HANDLES(GetStockObject(WHITE_BRUSH)));
        ImageList_Draw(hImageList, 0, hMemDC, 0, 0, ILD_TRANSPARENT);
        ImageList_Destroy(hImageList);

        SelectObject(HMemDC, hOld);

        SelectObject(hMemDC, hOldBitmap);
        HANDLES(DeleteDC(hMemDC));

        HANDLES(ReleaseDC(NULL, hdc));
        return HCopyBitmap;
    }
};
