// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// ****************************************************************************
//
// CBackbufferedDC -- DC with back buffer, for smooth drawing of more complex
// grafickych celku
//

class CBackbufferedDC
{
public:
    CBackbufferedDC();
    CBackbufferedDC(HWND window);
    ~CBackbufferedDC();
    void Destroy();

    // nastavi okno ke kteremu se DC vaze
    void SetWindow(HWND window);

    // aktualizuje vnitrni data v zavislosti na zmene velikosti okna/rozliseni
    // obrazovky apod; nevolat mezi BeginPaint a EndPaint
    void Update();

    // starts drawing to window, _must_ pair with EndPaint, cannot be called
    // opakovane
    void BeginPaint();

    // ukonci kresleni a zkopiruje obsah back-bufferu na obrazovku
    void EndPaint();

    // DC for drawing to window, valid only between BeginPaint and EndPaint
    operator HDC();

    // vrati rectangle o rozmerech bufferu
    const RECT& GetRect() { return ClientRect; }

private:
    HDC DC;
    HWND HWindow;
    HBITMAP HBitmap;
    HBITMAP OldHBitmap;
    PAINTSTRUCT PS;
    RECT ClientRect;
};

inline BOOL FastFillRect(HDC hdc, const RECT& r)
{
    return ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &r, NULL, 0, 0);
}

inline BOOL FastFillRect(HDC hdc, int x1, int y1, int x2, int y2)
{
    RECT r;
    r.left = x1;
    r.top = y1;
    r.right = x2;
    r.bottom = y2;
    return FastFillRect(hdc, r);
}
