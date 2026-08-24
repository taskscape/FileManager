// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED
#include "precomp.h"
#include "operation_journal.h"
#include <time.h>
// Use StrSafe for the bounded process command line built below.
#include <strsafe.h>
//#ifdef MSVC_RUNTIME_CHECKS
#include <rtcapi.h>
//#endif // MSVC_RUNTIME_CHECKS

#include "allochan.h"
#include "menu.h"
#include "cfgdlg.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "shellib.h"
#include "worker.h"
#include "iconpool.h"
#include "snooper.h"
#include "viewer.h"
#include "editwnd.h"
#include "find.h"
#include "zip.h"
#include "pack.h"
#include "cache.h"
#include "dialogs.h"
#include "gui.h"
#include "tasklist.h"
#include <uxtheme.h>
#include "olespy.h"
#include "geticon.h"
#include "logo.h"
#include "color.h"
#include "toolbar.h"
#include "common/scoped_gdi.h"

#include "svg.h"

extern "C"
{
#include "shexreg.h"
}
#include "salshlib.h"
#include "shiconov.h"
#include "salmoncl.h"
#include "jumplist.h"
#include "usermenu.h"
#include "execute.h"
#include "drivelst.h"

// Application graphics extracted from app_entry.cpp as a mechanical move:
// highlight-color heuristics, default-color updates, constant graphics and
// image lists, shortcut overlay, the DPI scaling family, InitializeGraphics/
// ReleaseGraphics, and the ColorsChanged broadcast. Startup and cleanup
// counterparts therefore live together, apart from the entry sequence.
// ****************************************************************************

// Walks through all colors from configuration and if they have the default value set,
// nastavi jim prislusne barevne hodnoty

COLORREF GetHilightColor(COLORREF clr1, COLORREF clr2)
{
    WORD h1, l1, s1;
    ColorRGBToHLS(clr1, &h1, &l1, &s1);
    BYTE gray1 = GetGrayscaleFromRGB(GetRValue(clr1), GetGValue(clr1), GetBValue(clr1));
    BYTE gray2 = GetGrayscaleFromRGB(GetRValue(clr2), GetGValue(clr2), GetBValue(clr2));
    COLORREF res;
    if (gray2 < 170 && gray1 <= 220)
    {
        unsigned wantedGray = (unsigned)gray1 + 20 + (220 - (unsigned)gray1) / 2;
        if (wantedGray < (unsigned)gray2 + 100)
            wantedGray = (unsigned)gray2 + 100;
        if (wantedGray > 255)
            wantedGray = 255;
        BOOL first = TRUE;
        while (first || l1 != 240)
        {
            first = FALSE;
            l1 += 5;
            if (l1 > 240)
                l1 = 240;
            res = ColorHLSToRGB(h1, l1, s1);
            if ((unsigned)GetGrayscaleFromRGB(GetRValue(res), GetGValue(res), GetBValue(res)) >= wantedGray)
                break;
        }
    }
    else
    {
        if ((gray1 >= gray2 ? gray1 - gray2 : gray2 - gray1) > 85 ||
            gray2 < 85 ||
            gray1 < 75)
        {
            if (gray1 > gray2)
            {
                res = RGB((4 * (unsigned)GetRValue(clr1) + 3 * (unsigned)GetRValue(clr2)) / 7,
                          (4 * (unsigned)GetGValue(clr1) + 3 * (unsigned)GetGValue(clr2)) / 7,
                          (4 * (unsigned)GetBValue(clr1) + 3 * (unsigned)GetBValue(clr2)) / 7);
            }
            else
            {
                res = RGB(((unsigned)GetRValue(clr1) + (unsigned)GetRValue(clr2)) / 2,
                          ((unsigned)GetGValue(clr1) + (unsigned)GetGValue(clr2)) / 2,
                          ((unsigned)GetBValue(clr1) + (unsigned)GetBValue(clr2)) / 2);
            }
        }
        else
        {
            res = RGB(0, 0, 0);
        }
    }
    return res;
}

COLORREF GetFullRowHighlight(COLORREF bkHighlightColor) // returns a "heuristic" highlight for full row mode
{
    // trochu heuristiky: zsvetle pozadi budeme "trochu" ztmavovat a tmave pozadi "trochu" zesvetlovat
    WORD h, l, s;
    ColorRGBToHLS(bkHighlightColor, &h, &l, &s);

    if (l < 121) // [TMAVA]  0-120 -> zesvetlime Luminance progresivne 0..120 -> +40..+20
        l += 20 + 20 * (120 - l) / 120;
    else // [SVETLA] 121-240 -> ztmavime Luminance o konstatnich 20
        l -= 20;

    return ColorHLSToRGB(h, l, s);
}

void UpdateDefaultColors(SALCOLOR* colors, CHighlightMasks* highlightMasks, BOOL processColors, BOOL processMasks)
{
    if (processColors)
    {
        int bitsPerPixel = GetCurrentBPP();

        // barvy pera pro ramecek kolem polozky prebereme ze systemove barvy textu okna
        if (GetFValue(colors[FOCUS_ACTIVE_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_ACTIVE_NORMAL], GetSysColor(COLOR_WINDOWTEXT));
        if (GetFValue(colors[FOCUS_ACTIVE_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_ACTIVE_SELECTED], GetSysColor(COLOR_WINDOWTEXT));
        if (GetFValue(colors[FOCUS_BK_INACTIVE_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_NORMAL], GetSysColor(COLOR_WINDOW));
        if (GetFValue(colors[FOCUS_BK_INACTIVE_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[FOCUS_BK_INACTIVE_SELECTED], GetSysColor(COLOR_WINDOW));

        // texty polozek v panelu prebereme ze systemove barvy textu okna
        if (GetFValue(colors[ITEM_FG_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_FG_NORMAL], GetSysColor(COLOR_WINDOWTEXT));
        if (GetFValue(colors[ITEM_FG_FOCUSED]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_FG_FOCUSED], GetSysColor(COLOR_WINDOWTEXT));
        if (GetFValue(colors[ITEM_FG_HIGHLIGHT]) & SCF_DEFAULT) // FULL ROW HIGHLIGHT vychazi z _NORMAL
            SetRGBPart(&colors[ITEM_FG_HIGHLIGHT], GetCOLORREF(colors[ITEM_FG_NORMAL]));

        // pozadi polozek v panelu prebereme ze systemove barvy pozadi okna
        if (GetFValue(colors[ITEM_BK_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_BK_NORMAL], GetSysColor(COLOR_WINDOW));
        if (GetFValue(colors[ITEM_BK_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[ITEM_BK_SELECTED], GetSysColor(COLOR_WINDOW));
        if (GetFValue(colors[ITEM_BK_HIGHLIGHT]) & SCF_DEFAULT) // HIGHLIGHT kopirujeme z NORMAL (aby fungovaly i custom/norton mody)
            SetRGBPart(&colors[ITEM_BK_HIGHLIGHT], GetFullRowHighlight(GetCOLORREF(colors[ITEM_BK_NORMAL])));

        // barvy progress bary
        if (GetFValue(colors[PROGRESS_FG_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_FG_NORMAL], GetSysColor(COLOR_WINDOWTEXT));
        if (GetFValue(colors[PROGRESS_FG_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_FG_SELECTED], GetSysColor(COLOR_HIGHLIGHTTEXT));
        if (GetFValue(colors[PROGRESS_BK_NORMAL]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_BK_NORMAL], GetSysColor(COLOR_WINDOW));
        if (GetFValue(colors[PROGRESS_BK_SELECTED]) & SCF_DEFAULT)
            SetRGBPart(&colors[PROGRESS_BK_SELECTED], GetSysColor(COLOR_HIGHLIGHT));

        // barva selected odstinu ikonky
        if (GetFValue(colors[ICON_BLEND_SELECTED]) & SCF_DEFAULT)
        {
            // normalne kopirujeme do selected barvu z focused+selected
            SetRGBPart(&colors[ICON_BLEND_SELECTED], GetCOLORREF(colors[ICON_BLEND_FOCSEL]));
            // if this is red (Salamander profile and thanks to color depth we can
            // dovolit) pouzijeme pro selected svetlejsi odstin
            if (bitsPerPixel > 8 && GetCOLORREF(colors[ICON_BLEND_FOCSEL]) == RGB(255, 0, 0))
                SetRGBPart(&colors[ICON_BLEND_SELECTED], RGB(255, 128, 128));
        }

#define COLOR_HOTLIGHT 26 // winuser.h

        // titulky panelu (aktivni/neaktivni)

        // aktivni titulek panelu: POZADI
        if (GetFValue(colors[ACTIVE_CAPTION_BK]) & SCF_DEFAULT)
            SetRGBPart(&colors[ACTIVE_CAPTION_BK], GetSysColor(COLOR_ACTIVECAPTION));
        // aktivni titulek panelu: TEXT
        if (GetFValue(colors[ACTIVE_CAPTION_FG]) & SCF_DEFAULT)
            SetRGBPart(&colors[ACTIVE_CAPTION_FG], GetSysColor(COLOR_CAPTIONTEXT));
        // neaktivni titulek panelu: POZADI
        if (GetFValue(colors[INACTIVE_CAPTION_BK]) & SCF_DEFAULT)
            SetRGBPart(&colors[INACTIVE_CAPTION_BK], GetSysColor(COLOR_INACTIVECAPTION));
        // neaktivni titulek panelu: TEXT
        if (GetFValue(colors[INACTIVE_CAPTION_FG]) & SCF_DEFAULT)
        {
            // preferujeme stejnou barvu textu jako pro aktivni titulek, ale nekdy je tato barva priliz
            // blizka barve pozadi, potom zkusime barvu pro textu pro neaktivni titulek
            COLORREF clrBk = GetCOLORREF(colors[INACTIVE_CAPTION_BK]);
            COLORREF clrFgAc = GetSysColor(COLOR_CAPTIONTEXT);
            COLORREF clrFgIn = GetSysColor(COLOR_INACTIVECAPTIONTEXT);
            BYTE grayBk = GetGrayscaleFromRGB(GetRValue(clrBk), GetGValue(clrBk), GetBValue(clrBk));
            BYTE grayFgAc = GetGrayscaleFromRGB(GetRValue(clrFgAc), GetGValue(clrFgAc), GetBValue(clrFgAc));
            BYTE grayFgIn = GetGrayscaleFromRGB(GetRValue(clrFgIn), GetGValue(clrFgIn), GetBValue(clrFgIn));
            SetRGBPart(&colors[INACTIVE_CAPTION_FG], (abs(grayFgAc - grayBk) >= abs(grayFgIn - grayBk)) ? clrFgAc : clrFgIn);
        }

        // barvy hot polozek
        COLORREF hotColor = GetSysColor(COLOR_HOTLIGHT);
        if (GetFValue(colors[HOT_PANEL]) & SCF_DEFAULT)
            SetRGBPart(&colors[HOT_PANEL], hotColor);

        // hilight pro active panel caption
        if (GetFValue(colors[HOT_ACTIVE]) & SCF_DEFAULT)
        {
            COLORREF clr = GetCOLORREF(colors[ACTIVE_CAPTION_FG]);
            if (bitsPerPixel > 4)
                clr = GetHilightColor(clr, GetCOLORREF(colors[ACTIVE_CAPTION_BK]));
            SetRGBPart(&colors[HOT_ACTIVE], clr);
        }
        // hilight pro inactive panel caption
        if (GetFValue(colors[HOT_INACTIVE]) & SCF_DEFAULT)
        {
            COLORREF clr = GetCOLORREF(colors[INACTIVE_CAPTION_FG]);
            if (bitsPerPixel > 4)
                clr = GetHilightColor(clr, GetCOLORREF(colors[INACTIVE_CAPTION_BK]));
            SetRGBPart(&colors[HOT_INACTIVE], clr);
        }
    }

    if (processMasks)
    {
        // barvy zavisle na jmenu+atributech souboru
        int i;
        for (i = 0; i < highlightMasks->Count; i++)
        {
            CHighlightMasksItem* item = highlightMasks->At(i);
            if (GetFValue(item->NormalFg) & SCF_DEFAULT)
                SetRGBPart(&item->NormalFg, GetCOLORREF(colors[ITEM_FG_NORMAL]));
            if (GetFValue(item->NormalBk) & SCF_DEFAULT)
                SetRGBPart(&item->NormalBk, GetCOLORREF(colors[ITEM_BK_NORMAL]));
            if (GetFValue(item->FocusedFg) & SCF_DEFAULT)
                SetRGBPart(&item->FocusedFg, GetCOLORREF(colors[ITEM_FG_FOCUSED]));
            if (GetFValue(item->FocusedBk) & SCF_DEFAULT)
                SetRGBPart(&item->FocusedBk, GetCOLORREF(colors[ITEM_BK_FOCUSED]));
            if (GetFValue(item->SelectedFg) & SCF_DEFAULT)
                SetRGBPart(&item->SelectedFg, GetCOLORREF(colors[ITEM_FG_SELECTED]));
            if (GetFValue(item->SelectedBk) & SCF_DEFAULT)
                SetRGBPart(&item->SelectedBk, GetCOLORREF(colors[ITEM_BK_SELECTED]));
            if (GetFValue(item->FocSelFg) & SCF_DEFAULT)
                SetRGBPart(&item->FocSelFg, GetCOLORREF(colors[ITEM_FG_FOCSEL]));
            if (GetFValue(item->FocSelBk) & SCF_DEFAULT)
                SetRGBPart(&item->FocSelBk, GetCOLORREF(colors[ITEM_BK_FOCSEL]));
            if (GetFValue(item->HighlightFg) & SCF_DEFAULT)
                SetRGBPart(&item->HighlightFg, GetCOLORREF(item->NormalFg));
            if (GetFValue(item->HighlightBk) & SCF_DEFAULT) // FULL ROW HIGHLIGHT vychazi z _NORMAL
                SetRGBPart(&item->HighlightBk, GetFullRowHighlight(GetCOLORREF(item->NormalBk)));
        }
    }
}

//****************************************************************************
//
// Based on display color depth, determines whether to use 256-color
// or 16-color bitmaps.
//

BOOL Use256ColorsBitmap()
{
    int bitsPerPixel = GetCurrentBPP();
    return (bitsPerPixel > 8); // vice nez 256 barev
}

DWORD GetImageListColorFlags()
{
    // if the image list has 16-bit color depth, the alpha channel of new icons misbehaves under WinXP with 32-bit color display (32-bit depth works)
    // if the image list has 32-bit color depth, blending when drawing selected items misbehaves under Win2K with 32-bit color display (16-bit depth works)
    return ILC_COLOR32;
}

// ****************************************************************************

int LoadColorTable(int id, RGBQUAD* rgb, int rgbCount)
{
    int count = 0;
    HRSRC hRsrc = FindResource(HInstance, MAKEINTRESOURCE(id), RT_RCDATA);
    if (hRsrc)
    {
        void* data = LoadResource(HInstance, hRsrc);
        if (data)
        {
            DWORD size = SizeofResource(HInstance, hRsrc);
            if (size > 0)
            {
                int max = min(rgbCount, (WORD)size / 3);
                BYTE* ptr = (BYTE*)data;
                int i;
                for (i = 0; i < max; i++)
                {
                    rgb[i].rgbBlue = *ptr++;
                    rgb[i].rgbGreen = *ptr++;
                    rgb[i].rgbRed = *ptr++;
                    rgb[i].rgbReserved = 0;
                    count++;
                }
            }
        }
    }
    return count;
}

BOOL InitializeConstGraphics()
{
    // zajistime si hladky graficky vystup
    // 20 GDI API calls should be more than enough
    // je to implicitni hodnota z NT 4.0 WS
    if (GdiGetBatchLimit() < 20)
    {
        TRACE_I("Increasing GdiBatchLimit");
        GdiSetBatchLimit(20);
    }

    if (LoadColorTable(IDC_COLORTABLE, ColorTable, 256) != 256)
    {
        TRACE_E("Loading ColorTable failed");
        return FALSE;
    }

    if (SystemParametersInfo(SPI_GETDRAGFULLWINDOWS, 0, &DragFullWindows, FALSE) == 0)
        DragFullWindows = TRUE;

    // inicializace LogFont struktury
    NONCLIENTMETRICS ncm;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0);
    LogFont = ncm.lfStatusFont;
    /*
  LogFont.lfHeight = -10;
  LogFont.lfWidth = 0;
  LogFont.lfEscapement = 0;
  LogFont.lfOrientation = 0;
  LogFont.lfWeight = FW_NORMAL;
  LogFont.lfItalic = 0;
  LogFont.lfUnderline = 0;
  LogFont.lfStrikeOut = 0;
  LogFont.lfCharSet = UserCharset;
  LogFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
  LogFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  LogFont.lfQuality = DEFAULT_QUALITY;
  LogFont.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
  strcpy(LogFont.lfFaceName, "MS Shell Dlg 2");
  */

    // tyto brushe jsou alokovane systemem a automaticky se meni pri zmene barev
    HDialogBrush = GetSysColorBrush(COLOR_BTNFACE);
    HButtonTextBrush = GetSysColorBrush(COLOR_BTNTEXT);
    HMenuSelectedBkBrush = GetSysColorBrush(COLOR_HIGHLIGHT);
    HMenuSelectedTextBrush = GetSysColorBrush(COLOR_HIGHLIGHTTEXT);
    HMenuHilightBrush = GetSysColorBrush(COLOR_3DHILIGHT);
    HMenuGrayTextBrush = GetSysColorBrush(COLOR_3DSHADOW);
    if (HDialogBrush == NULL || HButtonTextBrush == NULL ||
        HMenuSelectedTextBrush == NULL || HMenuHilightBrush == NULL ||
        HMenuGrayTextBrush == NULL)
    {
        TRACE_E("Unable to create brush.");
        return FALSE;
    }
    ItemBitmap.CreateBmp(NULL, 1, 1); // zajisteni existence bitmapy

    // load the bitmap only once (do not refresh it when resolution changes)
    // and if the user switched colors from 256 upward, during LoadBitmap (i.e. bitmap
    // kompatibilni s display DC) by bitmapa zustala v degradovanych barvach;
    // proto ji nacteme jako DIB
    // HWorkerBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_WORKER)));
    //HWorkerBitmap = (HBITMAP)HANDLES(LoadImage(HInstance, MAKEINTRESOURCE(IDB_WORKER), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
    //if (HWorkerBitmap == NULL)
    //  return FALSE;

    // pri zmene fontu se volaji explicitne CreatePanelFont a CreateEnvFont, prvni inicializaci provedeme zde
    CreatePanelFont();
    CreateEnvFonts();

    if (Font == NULL || FontUL == NULL || EnvFont == NULL || EnvFontUL == NULL || TooltipFont == NULL)
    {
        TRACE_E("Unable to create fonts.");
        return FALSE;
    }

    return TRUE;
}

void ReleaseConstGraphics()
{
    ItemBitmap.Destroy();
    //if (HWorkerBitmap != NULL)
    //{
    //  HANDLES(DeleteObject(HWorkerBitmap));
    //  HWorkerBitmap = NULL;
    //}

    if (Font != NULL)
    {
        HANDLES(DeleteObject(Font));
        Font = NULL;
    }

    if (FontUL != NULL)
    {
        HANDLES(DeleteObject(FontUL));
        FontUL = NULL;
    }

    if (TooltipFont != NULL)
    {
        HANDLES(DeleteObject(TooltipFont));
        TooltipFont = NULL;
    }

    if (EnvFont != NULL)
    {
        HANDLES(DeleteObject(EnvFont));
        EnvFont = NULL;
    }

    if (EnvFontUL != NULL)
    {
        HANDLES(DeleteObject(EnvFontUL));
        EnvFontUL = NULL;
    }
}

BOOL AuxAllocateImageLists()
{
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        SimpleIconLists[i] = new CIconList();
        if (SimpleIconLists[i] == NULL)
        {
            TRACE_E(LOW_MEMORY);
            return FALSE;
        }
    }

    ThrobberFrames = new CIconList();
    if (ThrobberFrames == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    LockFrames = new CIconList();
    if (LockFrames == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return FALSE;
    }

    return TRUE;
}

// pomoci TweakUI si mohou uzivatele menit ikonku shortcuty (default, custom, zadna)
// pokusime se ji ctit
BOOL GetShortcutOverlay()
{
    int i;
    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        if (HShortcutOverlays[i] != NULL)
        {
            HANDLES(DestroyIcon(HShortcutOverlays[i]));
            HShortcutOverlays[i] = NULL;
        }
    }

    /*
  //#include <CommonControls.h>

  // cteni ikon overlayu ze systemoveho image-listu, zbytecne pomale, nacteme je primo z imageres.dll
  // tenhle kod tu nechavam jen pro pripad, ze bysme zase potrebovali zjistit kde ty ikony jsou
  typedef DECLSPEC_IMPORT HRESULT (WINAPI *F__SHGetImageList)(int iImageList, REFIID riid, void **ppvObj);

  F__SHGetImageList MySHGetImageList = (F__SHGetImageList)GetProcAddress(Shell32DLL, "SHGetImageList"); // Min: XP
  if (MySHGetImageList != NULL)
  {
    int shareIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SHARE);
    int linkIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_LINK);
    int offlineIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SLOWFILE);
    shareIndex = SHGetIconOverlayIndex(NULL, IDO_SHGIOI_SHARE);

    IImageList *imageList;
    if (MySHGetImageList(1 /* SHIL_SMALL * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_16] = icon;
      imageList->Release();
    }
    if (MySHGetImageList(0 /* SHIL_LARGE * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_32] = icon;
      imageList->Release();
    }
    if (MySHGetImageList(2 /* SHIL_EXTRALARGE * /, IID_IImageList, (void **)&imageList) == S_OK &&
        imageList != NULL)
    {
      int i;
      imageList->GetOverlayImage(linkIndex, &i);
      HICON icon;
      if (imageList->GetIcon(i, 0, &icon) != S_OK)
        icon = NULL;
      HShortcutOverlays[ICONSIZE_48] = icon;
      imageList->Release();
    }
  }
*/

    HKEY hKey;
    if (NOHANDLES(RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                               "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Icons",
                               0, KEY_QUERY_VALUE, &hKey)) == ERROR_SUCCESS)
    {
        char buff[MAX_PATH + 10];
        DWORD buffLen = sizeof(buff);
        buff[0] = 0;
        SalRegQueryValueEx(hKey, "29", NULL, NULL, (LPBYTE)buff, &buffLen);
        if (buff[0] != 0)
        {
            char* num = strrchr(buff, ','); // cislo ikony je za posledni carkou
            if (num != NULL)
            {
                int index = atoi(num + 1);
                *num = 0;

                HICON hIcons[2] = {0, 0};

                ExtractIcons(buff, index,
                             MAKELONG(32, 16),
                             MAKELONG(32, 16),
                             hIcons, NULL, 2, IconLRFlags);

                HShortcutOverlays[ICONSIZE_32] = hIcons[0];
                HShortcutOverlays[ICONSIZE_16] = hIcons[1];

                ExtractIcons(buff, index,
                             48,
                             48,
                             hIcons, NULL, 1, IconLRFlags);
                HShortcutOverlays[ICONSIZE_48] = hIcons[0];

                for (i = 0; i < ICONSIZE_COUNT; i++)
                    if (HShortcutOverlays[i] != NULL)
                        HANDLES_ADD(__htIcon, __hoLoadImage, HShortcutOverlays[i]);
            }
        }
        NOHANDLES(RegCloseKey(hKey));
    }

    for (i = 0; i < ICONSIZE_COUNT; i++)
    {
        if (HShortcutOverlays[i] == NULL)
        {
            HShortcutOverlays[i] = (HICON)HANDLES(LoadImage(ImageResDLL, MAKEINTRESOURCE(163),
                                                            IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
        }
    }
    return (HShortcutOverlays[ICONSIZE_16] != NULL &&
            HShortcutOverlays[ICONSIZE_32] != NULL &&
            HShortcutOverlays[ICONSIZE_48] != NULL);
}

int GetCurrentBPP(HDC hDC)
{
    HDC hdc;
    if (hDC == NULL)
        hdc = GetDC(NULL);
    else
        hdc = hDC;
    int bpp = GetDeviceCaps(hdc, PLANES) * GetDeviceCaps(hdc, BITSPIXEL);
    if (hDC == NULL)
        ReleaseDC(NULL, hdc);

    return bpp;
}

int GetSystemDPI()
{
    if (SystemDPI == 0)
    {
        TRACE_E("GetSystemDPI() SystemDPI == 0!");
        return 96;
    }
    else
    {
        return SystemDPI;
    }
}

int GetScaleForSystemDPI()
{
    int dpi = GetSystemDPI();
    int scale;
    if (dpi <= 96)
        scale = 100;
    else if (dpi <= 120)
        scale = 125;
    else if (dpi <= 144)
        scale = 150;
    else if (dpi <= 192)
        scale = 200;
    else if (dpi <= 240)
        scale = 250;
    else if (dpi <= 288)
        scale = 300;
    else if (dpi <= 384)
        scale = 400;
    else
        scale = 500;

    return scale;
}

int GetIconSizeForSystemDPI(CIconSizeEnum iconSize)
{
    if (SystemDPI == 0)
    {
        TRACE_E("GetIconSizeForSystemDPI() SystemDPI == 0!");
        return 16;
    }

    if (iconSize < ICONSIZE_16 || iconSize >= ICONSIZE_COUNT)
    {
        TRACE_E("GetIconSizeForSystemDPI() unknown iconSize!");
        return 16;
    }

    // DPI Name      DPI   Scale factor
    // --------------------------------
    // Smaller        96   1.00 (100%)
    // Medium        120   1.25 (125%)
    // Larger        144   1.50 (150%)
    // Extra Large   192   2.00 (200%)
    // Custom        240   2.50 (250%)
    // Custom        288   3.00 (300%)
    // Custom        384   4.00 (400%)
    // Custom        480   5.00 (500%)

    int scale = GetScaleForSystemDPI();

    int baseIconSize[ICONSIZE_COUNT] = {16, 32, 48}; // must match CIconSizeEnum

    return (baseIconSize[iconSize] * scale) / 100;
}

BOOL IsValidToolbarIconSize(int iconSize)
{
    // Reject corrupted or future registry values instead of creating an unexpectedly large image list.
    return iconSize == TOOLBAR_ICON_SIZE_SMALL ||
           iconSize == TOOLBAR_ICON_SIZE_MEDIUM ||
           iconSize == TOOLBAR_ICON_SIZE_LARGE;
}

int GetToolbarIconSizeForSystemDPI()
{
    // Scale the persisted logical size with the same system-DPI policy used by existing icons.
    int logicalSize = IsValidToolbarIconSize(Configuration.ToolbarIconSize) ?
                          Configuration.ToolbarIconSize :
                          TOOLBAR_ICON_SIZE_SMALL;
    return (logicalSize * GetScaleForSystemDPI()) / 100;
}

void GetSystemDPI(HDC hDC)
{
    // For system-wide DPI (legacy compatibility), use GetDeviceCaps
    // For per-monitor DPI (modern), use GetDpiForWindow() on a window handle
    HDC hTmpDC;
    if (hDC == NULL)
        hTmpDC = GetDC(NULL);
    else
        hTmpDC = hDC;
    SystemDPI = GetDeviceCaps(hTmpDC, LOGPIXELSX);
#ifdef _DEBUG
    if (SystemDPI != GetDeviceCaps(hTmpDC, LOGPIXELSY))
        TRACE_E("Unexpected situation: LOGPIXELSX != LOGPIXELSY.");
#endif
    if (hDC == NULL)
        ReleaseDC(NULL, hTmpDC);
}

// Per-monitor DPI support functions

void InitializeDpiAwareness()
{
    // Try SetProcessDpiAwarenessContext (Windows 10 v1607+ / Build 14393+)
    typedef BOOL(WINAPI *SetProcessDpiAwarenessContextFunc)(HANDLE);
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)-4)
#endif

    HMODULE user32 = GetModuleHandle("user32.dll");
    if (user32)
    {
        SetProcessDpiAwarenessContextFunc fnSetDpiContext =
            (SetProcessDpiAwarenessContextFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (fnSetDpiContext && fnSetDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        {
            return;
        }
    }

    // Try SetProcessDpiAwareness (Windows 8.1 / Windows Server 2012 R2+)
    typedef HRESULT(WINAPI *SetProcessDpiAwarenessFunc)(int);
    HMODULE shcore = LoadLibrary("shcore.dll");
    if (shcore)
    {
        SetProcessDpiAwarenessFunc fnSetDpiAwareness =
            (SetProcessDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (fnSetDpiAwareness && SUCCEEDED(fnSetDpiAwareness(2))) // PROCESS_PER_MONITOR_DPI_AWARE
        {
            FreeLibrary(shcore);
            return;
        }
        FreeLibrary(shcore);
    }

    // Fallback: SetProcessDPIAware (Windows Vista+)
    if (user32)
    {
        typedef BOOL(WINAPI *SetProcessDPIAwareFunc)();
        SetProcessDPIAwareFunc fnSetDPIAware =
            (SetProcessDPIAwareFunc)GetProcAddress(user32, "SetProcessDPIAware");
        if (fnSetDPIAware)
        {
            fnSetDPIAware();
        }
    }
}

int GetDpiForWindow(HWND hwnd)
{
    // Windows 8.1+ supports per-monitor DPI
    // For older Windows or if GetDpiForWindow fails, fall back to SystemDPI
    typedef UINT(WINAPI *GetDpiForWindowFunc)(HWND);
    static GetDpiForWindowFunc fnGetDpiForWindow = NULL;
    static BOOL initialized = FALSE;

    if (!initialized) {
        // This non-UNICODE target binds the generic API to ANSI, so use the
        // matching literal while resolving the optional Windows 10 export.
        HMODULE user32 = GetModuleHandle("user32.dll");
        if (user32) {
            fnGetDpiForWindow = (GetDpiForWindowFunc)GetProcAddress(user32, "GetDpiForWindow");
        }
        initialized = TRUE;
    }

    if (fnGetDpiForWindow && hwnd) {
        UINT dpi = fnGetDpiForWindow(hwnd);
        if (dpi > 0)
            return (int)dpi;
    }

    // If hwnd is NULL or GetDpiForWindow failed, try GetDpiForSystem (Windows 10+)
    typedef UINT(WINAPI *GetDpiForSystemFunc)();
    static GetDpiForSystemFunc fnGetDpiForSystem = NULL;
    static BOOL initializedSystem = FALSE;
    if (!initializedSystem) {
        HMODULE user32 = GetModuleHandle("user32.dll");
        if (user32) {
            fnGetDpiForSystem = (GetDpiForSystemFunc)GetProcAddress(user32, "GetDpiForSystem");
        }
        initializedSystem = TRUE;
    }
    if (fnGetDpiForSystem) {
        UINT dpi = fnGetDpiForSystem();
        if (dpi > 0)
            return (int)dpi;
    }

    return SystemDPI != 0 ? SystemDPI : 96;
}

int GetDpiForMonitor(HMONITOR hMonitor)
{
    typedef HRESULT(WINAPI *GetDpiForMonitorFunc)(HMONITOR, int, UINT*, UINT*);
    static GetDpiForMonitorFunc fnGetDpiForMonitor = NULL;
    static BOOL initialized = FALSE;

    if (!initialized) {
        HMODULE shcore = GetModuleHandle("shcore.dll");
        if (!shcore)
            shcore = LoadLibrary("shcore.dll");
        if (shcore) {
            fnGetDpiForMonitor = (GetDpiForMonitorFunc)GetProcAddress(shcore, "GetDpiForMonitor");
        }
        initialized = TRUE;
    }

    if (fnGetDpiForMonitor && hMonitor) {
        UINT dpiX = 0, dpiY = 0;
        if (SUCCEEDED(fnGetDpiForMonitor(hMonitor, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiX > 0) {
            return (int)dpiX;
        }
    }

    return GetSystemDPI();
}

int GetScaleForWindow(HWND hwnd)
{
    int dpi = GetDpiForWindow(hwnd);
    return GetScaleForDpi(dpi);
}

int GetScaleForDpi(int dpi)
{
    int scale;
    if (dpi <= 96)
        scale = 100;
    else if (dpi <= 120)
        scale = 125;
    else if (dpi <= 144)
        scale = 150;
    else if (dpi <= 192)
        scale = 200;
    else if (dpi <= 240)
        scale = 250;
    else if (dpi <= 288)
        scale = 300;
    else if (dpi <= 384)
        scale = 400;
    else if (dpi <= 480)
        scale = 500;
    else
        scale = dpi * 100 / 96;  // Fallback for unusual DPI values
    return scale;
}

int GetSystemMetricsForDpi(int nIndex, int dpi)
{
    // Get system metrics scaled for specific DPI
    typedef int(WINAPI *GetSystemMetricsForDpiFunc)(int, UINT);
    static GetSystemMetricsForDpiFunc fnGetSystemMetricsForDpi = NULL;
    static BOOL initialized = FALSE;

    if (!initialized) {
        HMODULE user32 = GetModuleHandle("user32.dll");
        if (user32) {
            fnGetSystemMetricsForDpi = (GetSystemMetricsForDpiFunc)GetProcAddress(user32, "GetSystemMetricsForDpi");
        }
        initialized = TRUE;
    }

    if (fnGetSystemMetricsForDpi) {
        return fnGetSystemMetricsForDpi(nIndex, (UINT)dpi);
    }

    int baseValue = GetSystemMetrics(nIndex);
    return MulDiv(baseValue, dpi, 96);
}

int GetIconSizeForDpi(int dpi, CIconSizeEnum iconSize)
{
    if (iconSize < ICONSIZE_16 || iconSize >= ICONSIZE_COUNT)
    {
        TRACE_E("GetIconSizeForDpi() unknown iconSize!");
        return 16;
    }

    int scale = GetScaleForDpi(dpi);
    int baseIconSize[ICONSIZE_COUNT] = {16, 32, 48};
    return (baseIconSize[iconSize] * scale) / 100;
}

int GetIconSizeForWindow(HWND hwnd, CIconSizeEnum iconSize)
{
    int dpi = GetDpiForWindow(hwnd);
    return GetIconSizeForDpi(dpi, iconSize);
}

int GetToolbarIconSizeForDpi(int dpi)
{
    int logicalSize = IsValidToolbarIconSize(Configuration.ToolbarIconSize) ?
                          Configuration.ToolbarIconSize :
                          TOOLBAR_ICON_SIZE_SMALL;
    return (logicalSize * GetScaleForDpi(dpi)) / 100;
}

int GetToolbarIconSizeForWindow(HWND hwnd)
{
    return GetToolbarIconSizeForDpi(GetDpiForWindow(hwnd));
}

// Menu and toolbar image lists are built from temporary GDI bitmaps; keeping
// this work in its own SEH-free scope lets the scoped bitmaps unwind while
// InitializeGraphics keeps its structured exception handler.
static BOOL CreateMenuAndToolbarImageLists()
{
    int menuIconSize = IconSizes[ICONSIZE_16];
    int toolbarIconSize = GetToolbarIconSizeForSystemDPI();
    int iconSize = menuIconSize;
    // Temporary toolbar bitmaps live only until the ImageList_Add calls; the
    // guard deletes them through the tracked handle API even on early returns.
    CScopedGDIBitmap hTmpMaskBitmap;
    CScopedGDIBitmap hTmpGrayBitmap;
    CScopedGDIBitmap hTmpColorBitmap;
    // Keep menu state glyphs in the same Fluent color language as command icons.
    CSVGIcon menuMarkIcons[] = {{0, "MenuCheck"}, {1, "MenuRadio"}};
    if (!CreateToolbarBitmaps(HInstance,
                              IDB_MENU,
                              RGB(255, 0, 255), GetSysColor(COLOR_BTNFACE),
                              *hTmpMaskBitmap.Put(), *hTmpGrayBitmap.Put(), *hTmpColorBitmap.Put(),
                              FALSE, menuMarkIcons, _countof(menuMarkIcons), menuIconSize))
        return FALSE;
    HMenuMarkImageList = ImageList_Create(menuIconSize, menuIconSize, ILC_MASK | ILC_COLORDDB, 2, 1);
    ImageList_Add(HMenuMarkImageList, hTmpColorBitmap.Get(), hTmpMaskBitmap.Get());
    hTmpMaskBitmap.Reset();
    hTmpGrayBitmap.Reset();
    hTmpColorBitmap.Reset();

    CSVGIcon* svgIcons;
    int svgIconsCount;
    GetSVGIconsMainToolbar(&svgIcons, &svgIconsCount);
    // Render a compact command set for menus before creating the independently sized toolbar set.
    if (!CreateToolbarBitmaps(HInstance,
                              Use256ColorsBitmap() ? IDB_TOOLBAR_256 : IDB_TOOLBAR_16,
                              RGB(255, 0, 255), GetSysColor(COLOR_BTNFACE),
                              *hTmpMaskBitmap.Put(), *hTmpGrayBitmap.Put(), *hTmpColorBitmap.Put(),
                              TRUE, svgIcons, svgIconsCount, menuIconSize))
        return FALSE;
    HHotMenuImageList = ImageList_Create(menuIconSize, menuIconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
    HGrayMenuImageList = ImageList_Create(menuIconSize, menuIconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
    ImageList_Add(HHotMenuImageList, hTmpColorBitmap.Get(), hTmpMaskBitmap.Get());
    ImageList_Add(HGrayMenuImageList, hTmpGrayBitmap.Get(), hTmpMaskBitmap.Get());
    hTmpMaskBitmap.Reset();
    hTmpGrayBitmap.Reset();
    hTmpColorBitmap.Reset();

    if (HHotMenuImageList == NULL || HGrayMenuImageList == NULL)
    {
        TRACE_E("Unable to create image list.");
        return FALSE;
    }

    if (!CreateToolbarBitmaps(HInstance,
                              Use256ColorsBitmap() ? IDB_TOOLBAR_256 : IDB_TOOLBAR_16,
                              RGB(255, 0, 255), GetSysColor(COLOR_BTNFACE),
                              *hTmpMaskBitmap.Put(), *hTmpGrayBitmap.Put(), *hTmpColorBitmap.Put(),
                              TRUE, svgIcons, svgIconsCount, toolbarIconSize))
        return FALSE;
    HHotToolBarImageList = ImageList_Create(toolbarIconSize, toolbarIconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
    HGrayToolBarImageList = ImageList_Create(toolbarIconSize, toolbarIconSize, ILC_MASK | ILC_COLORDDB, IDX_TB_COUNT, 1);
    ImageList_Add(HHotToolBarImageList, hTmpColorBitmap.Get(), hTmpMaskBitmap.Get());
    ImageList_Add(HGrayToolBarImageList, hTmpGrayBitmap.Get(), hTmpMaskBitmap.Get());
    hTmpMaskBitmap.Reset();
    hTmpGrayBitmap.Reset();
    hTmpColorBitmap.Reset();

    if (HHotToolBarImageList == NULL || HGrayToolBarImageList == NULL)
    {
        TRACE_E("Unable to create image list.");
        return FALSE;
    }
    return TRUE;
}

// (Re)builds all shared graphics resources: reads the system icon color
// depth and DPI, scales the 16/32/48 icon-size table, loads the color scheme,
// creates image lists, fonts, brushes and constant bitmaps. With 'colorsOnly'
// only color-dependent resources are recreated (used by ColorsChanged);
// otherwise everything is torn down and rebuilt. Returns FALSE on failure.
BOOL InitializeGraphics(BOOL colorsOnly)
{
    // 48x48 az od XP
    // ve skutecnosti jsou velke ikonky podporeny uz davno, lze je nahodit
    // Desktop/Properties/???/Large Icons; caution, the system image list will then not exist
    // pro ikonky 32x32; navic bychom meli ze systemu vytahnout realne velikosti ikonek
    // zatim na to kasleme a 48x48 povolime az od XP, kde jsou bezne dostupne

    //
    // Vytahneme z Registry pozadovanou barevnou hloubku ikonek
    //
    int iconColorsCount = 0;
    HDC hDesktopDC = GetDC(NULL);
    int bpp = GetCurrentBPP(hDesktopDC);
    GetSystemDPI(hDesktopDC);
    ReleaseDC(NULL, hDesktopDC);

    IconSizes[ICONSIZE_16] = GetIconSizeForSystemDPI(ICONSIZE_16);
    IconSizes[ICONSIZE_32] = GetIconSizeForSystemDPI(ICONSIZE_32);
    IconSizes[ICONSIZE_48] = GetIconSizeForSystemDPI(ICONSIZE_48);

    HKEY hKey;
    if (OpenKeyAux(NULL, HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", hKey))
    {
        // dalsi zajimave hodnoty: "Shell Icon Size", "Shell Small Icon Size"
        char buff[100];
        if (GetValueAux(NULL, hKey, "Shell Icon Bpp", REG_SZ, buff, 100))
        {
            iconColorsCount = atoi(buff);
        }
        else
        {
            if (WindowsVistaAndLater)
            {
                // ve viste tento klic proste neni a zatim netusim, cim je nahrazen,
                // so pretend that icons run in full colors (otherwise we displayed ugly 16-color icons)
                iconColorsCount = 32;
            }
        }

        if (iconColorsCount > bpp)
            iconColorsCount = bpp;
        if (bpp <= 8)
            iconColorsCount = 0;

        HANDLES(RegCloseKey(hKey));
    }

    TRACE_I("InitializeGraphics() bpp=" << bpp << " iconColorsCount=" << iconColorsCount);
    if (bpp >= 4 && iconColorsCount <= 4)
        IconLRFlags = LR_VGACOLOR;
    else
        IconLRFlags = 0;

    HDC dc = HANDLES(GetDC(NULL));
    CHighlightMasks* masks = MainWindow != NULL ? MainWindow->HighlightMasks : NULL;
    UpdateDefaultColors(CurrentColors, masks, TRUE, masks != NULL);
    if (!colorsOnly)
    {
        ImageResDLL = HANDLES(LoadLibraryEx("imageres.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
        if (ImageResDLL == NULL)
        {
            TRACE_E("Unable to load library imageres.dll.");
            return FALSE;
        }

        Shell32DLL = HANDLES(LoadLibraryEx("shell32.dll", NULL, LOAD_LIBRARY_AS_DATAFILE));
        if (Shell32DLL == NULL) // to se snad vubec nemuze stat (zaklad win 4.0)
        {
            TRACE_E("Unable to load library shell32.dll.");
            return FALSE;
        }

        HINSTANCE iconDLL = ImageResDLL;
        int iconIndex = 164;
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            HSharedOverlays[i] = (HICON)HANDLES(LoadImage(iconDLL, MAKEINTRESOURCE(iconIndex),
                                                          IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
        }
        GetShortcutOverlay(); // HShortcutOverlayXX

        iconIndex = 97;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            HSlowFileOverlays[i] = (HICON)HANDLES(LoadImage(iconDLL, MAKEINTRESOURCE(iconIndex),
                                                            IMAGE_ICON, IconSizes[i], IconSizes[i], IconLRFlags));
        }

        HGroupIcon = SalLoadImage(4, 20, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags);
        HFavoritIcon = (HICON)HANDLES(LoadImage(Shell32DLL, MAKEINTRESOURCE(319), IMAGE_ICON, IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], IconLRFlags));
        if (HSharedOverlays[ICONSIZE_16] == NULL ||
            HSharedOverlays[ICONSIZE_32] == NULL ||
            HSharedOverlays[ICONSIZE_48] == NULL ||
            HShortcutOverlays[ICONSIZE_16] == NULL ||
            HShortcutOverlays[ICONSIZE_32] == NULL ||
            HShortcutOverlays[ICONSIZE_48] == NULL ||
            HSlowFileOverlays[ICONSIZE_16] == NULL ||
            HSlowFileOverlays[ICONSIZE_32] == NULL ||
            HSlowFileOverlays[ICONSIZE_48] == NULL ||
            HGroupIcon == NULL || HFavoritIcon == NULL)
        {
            TRACE_E("Unable to read icon overlays for shared directories, shortcuts or slow files, or icon for groups or favorites.");
            return FALSE;
        }

        // prekladac hlasil chybu: error C2712: Cannot use __try in functions that require object unwinding
        // obchazim to vlozenim alokace do funkce
        //    SymbolsIconList = new CIconList();
        //    LargeSymbolsIconList = new CIconList();
        if (!AuxAllocateImageLists())
            return FALSE;

        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (!SimpleIconLists[i]->Create(IconSizes[i], IconSizes[i], symbolsCount))
            {
                TRACE_E("Unable to create image lists.");
                return FALSE;
            }
            SimpleIconLists[i]->SetBkColor(GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
        }

        if (!ThrobberFrames->CreateFromPNG(HInstance, MAKEINTRESOURCE(IDB_THROBBER), THROBBER_WIDTH))
        {
            TRACE_E("Unable to create throbber.");
            return FALSE;
        }

        if (!LockFrames->CreateFromPNG(HInstance, MAKEINTRESOURCE(IDB_LOCK), LOCK_WIDTH))
        {
            TRACE_E("Unable to create lock.");
            return FALSE;
        }

        HFindSymbolsImageList = ImageList_Create(IconSizes[ICONSIZE_16], IconSizes[ICONSIZE_16], ILC_MASK | GetImageListColorFlags(), 2, 0);
        if (HFindSymbolsImageList == NULL)
        {
            TRACE_E("Unable to create image list.");
            return FALSE;
        }
        ImageList_SetImageCount(HFindSymbolsImageList, 2); // inicializace
                                                           //    ImageList_SetBkColor(HFindSymbolsImageList, GetSysColor(COLOR_WINDOW)); // aby pod XP chodily pruhledne ikonky

        if (!CreateMenuAndToolbarImageLists())
            return FALSE;
        int iconSize = IconSizes[ICONSIZE_16]; // menu glyph size also drives the small SVG arrows below

        HBottomTBImageList = ImageList_Create(BOTTOMBAR_CX, BOTTOMBAR_CY, ILC_MASK | ILC_COLORDDB, 12, 0);
        HHotBottomTBImageList = ImageList_Create(BOTTOMBAR_CX, BOTTOMBAR_CY, ILC_MASK | ILC_COLORDDB, 12, 0);
        if (HBottomTBImageList == NULL || HHotBottomTBImageList == NULL)
        {
            TRACE_E("Unable to create image list.");
            return FALSE;
        }

        // vytahnu z shell 32 ikony:
        int indexes[] = {symbolsExecutable, symbolsDirectory, symbolsNonAssociated, symbolsAssociated, -1};
        int resID[] = {3, 4, 1, 2, -1};
        int vistaResID[] = {15, 4, 2, 90, -1};
        HICON hIcon;
        for (i = 0; indexes[i] != -1; i++)
        {
            int sizeIndex;
            for (sizeIndex = 0; sizeIndex < ICONSIZE_COUNT; sizeIndex++)
            {
                hIcon = SalLoadImage(vistaResID[i], resID[i], IconSizes[sizeIndex], IconSizes[sizeIndex], IconLRFlags);
                if (hIcon != NULL)
                {
                    SimpleIconLists[sizeIndex]->ReplaceIcon(indexes[i], hIcon);
                    if (sizeIndex == ICONSIZE_16)
                    {
                        if (indexes[i] == symbolsDirectory)
                            ImageList_ReplaceIcon(HFindSymbolsImageList, 0, hIcon);
                        if (indexes[i] == symbolsNonAssociated)
                            ImageList_ReplaceIcon(HFindSymbolsImageList, 1, hIcon);
                    }
                    HANDLES(DestroyIcon(hIcon));
                }
                else
                    TRACE_E("Cannot retrieve icon from IMAGERES.DLL or SHELL32.DLL resID=" << resID[i]);
            }
        }
        char systemDir[MAX_PATH];
        GetSystemDirectory(systemDir, MAX_PATH);
        // 16x16, 32x32, 48x48
        int sizeIndex;
        for (sizeIndex = ICONSIZE_16; sizeIndex < ICONSIZE_COUNT; sizeIndex++)
        {
            // ikonka adresare
            hIcon = NULL;
            __try
            {
                if (!GetFileIcon(systemDir, FALSE, &hIcon, (CIconSizeEnum)sizeIndex, FALSE, FALSE))
                    hIcon = NULL;
            }
            __except (CCallStack::HandleException(GetExceptionInformation(), 15))
            {
                FGIExceptionHasOccured++;
                hIcon = NULL;
            }
            if (hIcon != NULL) // if we do not get the icon, there is still the 4 from shell32.dll
            {
                SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsDirectory, hIcon);
                NOHANDLES(DestroyIcon(hIcon));
            }

            // ikonka ".."
            hIcon = (HICON)HANDLES(LoadImage(HInstance, MAKEINTRESOURCE(IDI_UPPERDIR),
                                             IMAGE_ICON, IconSizes[sizeIndex], IconSizes[sizeIndex],
                                             IconLRFlags));
            SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsUpDir, hIcon);
            HANDLES(DestroyIcon(hIcon));

            // ikonka archiv
            hIcon = LoadArchiveIcon(IconSizes[sizeIndex], IconSizes[sizeIndex], IconLRFlags);
            SimpleIconLists[sizeIndex]->ReplaceIcon(symbolsArchive, hIcon);
            HANDLES(DestroyIcon(hIcon));
        }

        WORD bits[8] = {0x0055, 0x00aa, 0x0055, 0x00aa,
                        0x0055, 0x00aa, 0x0055, 0x00aa};
        HBITMAP hBrushBitmap = HANDLES(CreateBitmap(8, 8, 1, 1, &bits));
        HDitherBrush = HANDLES(CreatePatternBrush(hBrushBitmap));
        HANDLES(DeleteObject(hBrushBitmap));
        if (HDitherBrush == NULL)
            return FALSE;

        HUpDownBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_UPDOWN)));
        HZoomBitmap = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_ZOOM)));
        HFilter = HANDLES(LoadBitmap(HInstance, MAKEINTRESOURCE(IDB_FILTER)));

        if (HUpDownBitmap == NULL ||
            HZoomBitmap == NULL || HFilter == NULL)
        {
            TRACE_E("HUpDownBitmap == NULL || HZoomBitmap == NULL || HFilter == NULL");
            return FALSE;
        }

        SVGArrowRight.Load(IDV_ARROW_RIGHT, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowRightSmall.Load(IDV_ARROW_RIGHT, -1, (int)((double)iconSize / 2.5), SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowMore.Load(IDV_ARROW_MORE, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowLess.Load(IDV_ARROW_LESS, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
        SVGArrowDropDown.Load(IDV_ARROW_DOWN, -1, -1, SVGSTATE_ENABLED | SVGSTATE_DISABLED);
    }

    ImageList_SetBkColor(HHotToolBarImageList, GetSysColor(COLOR_BTNFACE));
    ImageList_SetBkColor(HGrayToolBarImageList, GetSysColor(COLOR_BTNFACE));
    // Menu image lists use the same background while retaining their compact dimensions.
    ImageList_SetBkColor(HHotMenuImageList, GetSysColor(COLOR_BTNFACE));
    ImageList_SetBkColor(HGrayMenuImageList, GetSysColor(COLOR_BTNFACE));

    if (SystemParametersInfo(SPI_GETMOUSEHOVERTIME, 0, &MouseHoverTime, FALSE) == 0)
    {
        if (SystemParametersInfo(SPI_GETMENUSHOWDELAY, 0, &MouseHoverTime, FALSE) == 0)
            MouseHoverTime = 400;
    }
    //  TRACE_I("MouseHoverTime="<<MouseHoverTime);

    COLORREF normalBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_NORMAL]));
    COLORREF selectedBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_SELECTED]));
    COLORREF focusedBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_FOCUSED]));
    COLORREF focselBkgnd = GetNearestColor(dc, GetCOLORREF(CurrentColors[ITEM_BK_FOCSEL]));
    COLORREF activeCaption = GetNearestColor(dc, GetCOLORREF(CurrentColors[ACTIVE_CAPTION_BK]));
    COLORREF inactiveCaption = GetNearestColor(dc, GetCOLORREF(CurrentColors[INACTIVE_CAPTION_BK]));
    HANDLES(ReleaseDC(NULL, dc));

    HNormalBkBrush = HANDLES(CreateSolidBrush(normalBkgnd));
    HFocusedBkBrush = HANDLES(CreateSolidBrush(focusedBkgnd));
    HSelectedBkBrush = HANDLES(CreateSolidBrush(selectedBkgnd));
    HFocSelBkBrush = HANDLES(CreateSolidBrush(focselBkgnd));
    HActiveCaptionBrush = HANDLES(CreateSolidBrush(activeCaption));
    HInactiveCaptionBrush = HANDLES(CreateSolidBrush(inactiveCaption));

    if (HNormalBkBrush == NULL || HFocusedBkBrush == NULL ||
        HSelectedBkBrush == NULL || HFocSelBkBrush == NULL ||
        HActiveCaptionBrush == NULL || HInactiveCaptionBrush == NULL ||
        HMenuSelectedBkBrush == NULL)
    {
        TRACE_E("Unable to create brush.");
        return FALSE;
    }

    HActiveNormalPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[FOCUS_ACTIVE_NORMAL])));
    HActiveSelectedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[FOCUS_ACTIVE_SELECTED])));
    HInactiveNormalPen = HANDLES(CreatePen(PS_DOT, 0, GetCOLORREF(CurrentColors[FOCUS_FG_INACTIVE_NORMAL])));
    HInactiveSelectedPen = HANDLES(CreatePen(PS_DOT, 0, GetCOLORREF(CurrentColors[FOCUS_FG_INACTIVE_SELECTED])));

    HThumbnailNormalPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_NORMAL])));
    HThumbnailFucsedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_FOCUSED])));
    HThumbnailSelectedPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_SELECTED])));
    HThumbnailFocSelPen = HANDLES(CreatePen(PS_SOLID, 0, GetCOLORREF(CurrentColors[THUMBNAIL_FRAME_FOCSEL])));

    BtnShadowPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNSHADOW)));
    BtnHilightPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNHILIGHT)));
    Btn3DLightPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_3DLIGHT)));
    BtnFacePen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_BTNFACE)));
    WndFramePen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_WINDOWFRAME)));
    WndPen = HANDLES(CreatePen(PS_SOLID, 0, GetSysColor(COLOR_WINDOW)));
    if (HActiveNormalPen == NULL || HActiveSelectedPen == NULL ||
        HInactiveNormalPen == NULL || HInactiveSelectedPen == NULL ||
        HThumbnailNormalPen == NULL || HThumbnailFucsedPen == NULL ||
        HThumbnailSelectedPen == NULL || HThumbnailFocSelPen == NULL ||
        BtnShadowPen == NULL || BtnHilightPen == NULL || BtnFacePen == NULL ||
        Btn3DLightPen == NULL || WndFramePen == NULL || WndPen == NULL)
    {
        TRACE_E("Unable to create a pen.");
        return FALSE;
    }

    COLORMAP clrMap[3];
    clrMap[0].from = RGB(255, 0, 255);
    clrMap[0].to = GetSysColor(COLOR_BTNFACE);
    clrMap[1].from = RGB(255, 255, 255);
    clrMap[1].to = GetSysColor(COLOR_BTNHILIGHT);
    clrMap[2].from = RGB(128, 128, 128);
    clrMap[2].to = GetSysColor(COLOR_BTNSHADOW);
    HHeaderSort = HANDLES(CreateMappedBitmap(HInstance, IDB_HEADER, 0, clrMap, 3));
    if (HHeaderSort == NULL)
    {
        TRACE_E("Unable to load bitmap HHeaderSort.");
        return FALSE;
    }

    clrMap[0].from = RGB(128, 128, 128); // seda -> COLOR_BTNSHADOW
    clrMap[0].to = GetSysColor(COLOR_BTNSHADOW);
    clrMap[1].from = RGB(0, 0, 0); // cerna -> COLOR_BTNTEXT
    clrMap[1].to = GetSysColor(COLOR_BTNTEXT);
    clrMap[2].from = RGB(255, 255, 255); // bila -> pruhledna
    clrMap[2].to = RGB(255, 0, 255);
    HBITMAP hBottomTB = HANDLES(CreateMappedBitmap(HInstance, IDB_BOTTOMTOOLBAR, 0, clrMap, 3));
    BOOL remapWhite = FALSE;
    if (GetCurrentBPP() > 8)
    {
        clrMap[2].from = RGB(255, 255, 255); // bila -> svetle sedivou (at to tak nerve)
        clrMap[2].to = RGB(235, 235, 235);
        remapWhite = TRUE;
    }
    HBITMAP hHotBottomTB = HANDLES(CreateMappedBitmap(HInstance, IDB_BOTTOMTOOLBAR, 0, clrMap, remapWhite ? 3 : 2));
    ImageList_RemoveAll(HBottomTBImageList);
    ImageList_AddMasked(HBottomTBImageList, hBottomTB, RGB(255, 0, 255));
    ImageList_RemoveAll(HHotBottomTBImageList);
    ImageList_AddMasked(HHotBottomTBImageList, hHotBottomTB, RGB(255, 0, 255));
    HANDLES(DeleteObject(hBottomTB));
    HANDLES(DeleteObject(hHotBottomTB));
    ImageList_SetBkColor(HBottomTBImageList, GetSysColor(COLOR_BTNFACE));
    ImageList_SetBkColor(HHotBottomTBImageList, GetSysColor(COLOR_BTNFACE));
    return TRUE;
}

// ****************************************************************************

void ReleaseGraphics(BOOL colorsOnly)
{
    if (!colorsOnly)
    {
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (HSharedOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HSharedOverlays[i]));
                HSharedOverlays[i] = NULL;
            }
            if (HShortcutOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HShortcutOverlays[i]));
                HShortcutOverlays[i] = NULL;
            }
            if (HSlowFileOverlays[i] != NULL)
            {
                HANDLES(DestroyIcon(HSlowFileOverlays[i]));
                HSlowFileOverlays[i] = NULL;
            }
        }

        if (HGroupIcon != NULL)
        {
            HANDLES(DestroyIcon(HGroupIcon));
            HGroupIcon = NULL;
        }

        if (HFavoritIcon != NULL)
        {
            HANDLES(DestroyIcon(HFavoritIcon));
            HFavoritIcon = NULL;
        }

        if (HZoomBitmap != NULL)
        {
            HANDLES(DeleteObject(HZoomBitmap));
            HZoomBitmap = NULL;
        }

        if (HFilter != NULL)
        {
            HANDLES(DeleteObject(HFilter));
            HFilter = NULL;
        }

        if (HUpDownBitmap != NULL)
        {
            HANDLES(DeleteObject(HUpDownBitmap));
            HUpDownBitmap = NULL;
        }
    }

    if (HNormalBkBrush != NULL)
    {
        HANDLES(DeleteObject(HNormalBkBrush));
        HNormalBkBrush = NULL;
    }
    if (HFocusedBkBrush != NULL)
    {
        HANDLES(DeleteObject(HFocusedBkBrush));
        HFocusedBkBrush = NULL;
    }
    if (HSelectedBkBrush != NULL)
    {
        HANDLES(DeleteObject(HSelectedBkBrush));
        HSelectedBkBrush = NULL;
    }
    if (HFocSelBkBrush != NULL)
    {
        HANDLES(DeleteObject(HFocSelBkBrush));
        HFocSelBkBrush = NULL;
    }
    if (HActiveCaptionBrush != NULL)
    {
        HANDLES(DeleteObject(HActiveCaptionBrush));
        HActiveCaptionBrush = NULL;
    }
    if (HInactiveCaptionBrush != NULL)
    {
        HANDLES(DeleteObject(HInactiveCaptionBrush));
        HInactiveCaptionBrush = NULL;
    }
    if (HActiveNormalPen != NULL)
    {
        HANDLES(DeleteObject(HActiveNormalPen));
        HActiveNormalPen = NULL;
    }
    if (HActiveSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HActiveSelectedPen));
        HActiveSelectedPen = NULL;
    }
    if (HInactiveNormalPen != NULL)
    {
        HANDLES(DeleteObject(HInactiveNormalPen));
        HInactiveNormalPen = NULL;
    }
    if (HInactiveSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HInactiveSelectedPen));
        HInactiveSelectedPen = NULL;
    }
    if (HThumbnailNormalPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailNormalPen));
        HThumbnailNormalPen = NULL;
    }
    if (HThumbnailFucsedPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailFucsedPen));
        HThumbnailFucsedPen = NULL;
    }
    if (HThumbnailSelectedPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailSelectedPen));
        HThumbnailSelectedPen = NULL;
    }
    if (HThumbnailFocSelPen != NULL)
    {
        HANDLES(DeleteObject(HThumbnailFocSelPen));
        HThumbnailFocSelPen = NULL;
    }
    if (BtnShadowPen != NULL)
    {
        HANDLES(DeleteObject(BtnShadowPen));
        BtnShadowPen = NULL;
    }
    if (BtnHilightPen != NULL)
    {
        HANDLES(DeleteObject(BtnHilightPen));
        BtnHilightPen = NULL;
    }
    if (Btn3DLightPen != NULL)
    {
        HANDLES(DeleteObject(Btn3DLightPen));
        Btn3DLightPen = NULL;
    }
    if (BtnFacePen != NULL)
    {
        HANDLES(DeleteObject(BtnFacePen));
        BtnFacePen = NULL;
    }
    if (WndFramePen != NULL)
    {
        HANDLES(DeleteObject(WndFramePen));
        WndFramePen = NULL;
    }
    if (WndPen != NULL)
    {
        HANDLES(DeleteObject(WndPen));
        WndPen = NULL;
    }
    if (HHeaderSort != NULL)
    {
        HANDLES(DeleteObject(HHeaderSort));
        HHeaderSort = NULL;
    }

    if (!colorsOnly)
    {
        if (HDitherBrush != NULL)
        {
            HANDLES(DeleteObject(HDitherBrush));
            HDitherBrush = NULL;
        }
        if (HHotToolBarImageList != NULL)
        {
            ImageList_Destroy(HHotToolBarImageList);
            HHotToolBarImageList = NULL;
        }
        if (HGrayToolBarImageList != NULL)
        {
            ImageList_Destroy(HGrayToolBarImageList);
            HGrayToolBarImageList = NULL;
        }
        // Menu lists are owned by the graphics lifecycle alongside toolbar lists.
        if (HHotMenuImageList != NULL)
        {
            ImageList_Destroy(HHotMenuImageList);
            HHotMenuImageList = NULL;
        }
        if (HGrayMenuImageList != NULL)
        {
            ImageList_Destroy(HGrayMenuImageList);
            HGrayMenuImageList = NULL;
        }
        if (HBottomTBImageList != NULL)
        {
            ImageList_Destroy(HBottomTBImageList);
            HBottomTBImageList = NULL;
        }
        if (HHotBottomTBImageList != NULL)
        {
            ImageList_Destroy(HHotBottomTBImageList);
            HHotBottomTBImageList = NULL;
        }
        if (HMenuMarkImageList != NULL)
        {
            ImageList_Destroy(HMenuMarkImageList);
            HMenuMarkImageList = NULL;
        }
        int i;
        for (i = 0; i < ICONSIZE_COUNT; i++)
        {
            if (SimpleIconLists[i] != NULL)
            {
                delete SimpleIconLists[i];
                SimpleIconLists[i] = NULL;
            }
        }
        if (ThrobberFrames != NULL)
        {
            delete ThrobberFrames;
            ThrobberFrames = NULL;
        }
        if (LockFrames != NULL)
        {
            delete LockFrames;
            LockFrames = NULL;
        }
        if (HFindSymbolsImageList != NULL)
        {
            ImageList_Destroy(HFindSymbolsImageList);
            HFindSymbolsImageList = NULL;
        }
        if (Shell32DLL != NULL)
        {
            HANDLES(FreeLibrary(Shell32DLL));
            Shell32DLL = NULL;
        }
        if (ImageResDLL != NULL)
        {
            HANDLES(FreeLibrary(ImageResDLL));
            ImageResDLL = NULL;
        }
    }
}

//
// ****************************************************************************
// ColorsChanged
//

void ColorsChanged(BOOL refresh, BOOL colorsOnly, BOOL reloadUMIcons)
{
    CALL_STACK_MESSAGE2("ColorsChanged(%d)", refresh);
    // CAUTION! fonts must be FALSE so the font handle does not change, which
    // se museji dozvedet toolbary, ktere jej pouzivaji
    ReleaseGraphics(colorsOnly);
    InitializeGraphics(colorsOnly);
    ItemBitmap.ReCreateForScreenDC();
    UpdateViewerColors(ViewerColors);
    if (!colorsOnly)
        ShellIconOverlays.ColorsChanged();

    if (MainWindow != NULL && MainWindow->EditWindow != NULL)
        MainWindow->EditWindow->SetFont();

    Associations.ColorsChanged();

    if (MainWindow != NULL)
    {
        MainWindow->OnColorsChanged(reloadUMIcons);
    }

    // dame vedet findum o zmene barev
    FindDialogQueue.BroadcastMessage(WM_USER_COLORCHANGEFIND, 0, 0);

    // rozesleme tuto novinku i mezi plug-iny
    Plugins.Event(PLUGINEVENT_COLORSCHANGED, 0);

    if (MainWindow != NULL && MainWindow->HTopRebar != NULL)
        SendMessage(MainWindow->HTopRebar, RB_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_BTNFACE));

    if (refresh && MainWindow != NULL)
    {
        InvalidateRect(MainWindow->HWindow, NULL, TRUE);
    }
    // Internal Viewer a Find:  obnova vsech oken
    BroadcastConfigChanged();
}
