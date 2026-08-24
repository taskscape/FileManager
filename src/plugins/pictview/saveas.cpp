// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA)

#include "..\\..\\common\\monotonic_time.h"
#include <zmouse.h>
#include <shlobj.h>

#include "lib\\pvw32dll.h"
#include "renderer.h"
#include "dialogs.h"
#include "pictview.h"
#include "pictview.rh"
#include "pictview.rh2"
#include "lang\lang.rh"

#define SAVEAS_GRAY_FLAG 0x40000000
#define SAVEAS_GRAY_MASK 0x3FFFFFFF

#define SAVEAS_COMMENT_FLAG 0x4000
#define SAVEAS_COMMENT_MASK 0x3FFF

typedef struct _gen_ftype
{
    LPCTSTR ext;
    int type;
} PVFS_FTYPE;

// Output formats the WIC engine can encode. Everything the old PictView
// engine could additionally write has no free replacement, so it is gone
// from here and from the IDS_SAVEASFILTER* lists.
static PVFS_FTYPE fs_types[] = {
    {_T("jpg"), PVF_JPG | SAVEAS_COMMENT_FLAG},
    {_T("bmp"), PVF_BMP},
    {_T("tif"), PVF_TIFF | SAVEAS_COMMENT_FLAG},
    {_T("gif"), PVF_GIF | SAVEAS_COMMENT_FLAG},
    {_T("png"), PVF_PNG | SAVEAS_COMMENT_FLAG},
    {NULL, 0}};

static PVFS_FTYPE fs_comptypes[] = {
    {_T("CCITT G3"), PVCS_CCITT_3},
    {_T("CCITT G4"), PVCS_CCITT_4},
    {_T("Deflating (LZ77)"), PVCS_DEFLATE},
    {_T("JPEG"), PVCS_JPEG_HUFFMAN},
    {_T("Lempel-Ziv-Welch (LZW)"), PVCS_LZW},
    {_T("PackBits"), PVCS_PACKBITS},
    {_T("Run-Length Encoding"), PVCS_RLE},
    {_T("Uncompressed"), PVCS_NO_COMPRESSION},
    {NULL, 0}};

void GetMyDocumentsPath(LPTSTR initDir)
{
    initDir[0] = 0;
    PWSTR pathW = NULL;
    // Resolve Documents directly through Known Folders instead of allocating a legacy special-folder PIDL.
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, NULL, &pathW)) && pathW != NULL)
    {
#ifdef UNICODE
        wcsncpy_s(initDir, MAX_PATH, pathW, _TRUNCATE);
#else
        WideToUtf8Buffer(pathW, initDir, MAX_PATH);
#endif
        CoTaskMemFree(pathW);
    }
}

typedef struct tagProgBarInfo
{
    CViewerWindow* pViewer;
    CMonotonicTimePoint lastUpdateTicks;
    CMonotonicTimePoint lastCheckTicks;
} sProgBarInfo, *psProgBarInfo;

BOOL WINAPI SaveProgressProcedure(int done, void* data)
{
    // The conversion callback can run for an extended save, so its UI throttles use a non-wrapping clock.
    const CMonotonicTimePoint ticks = CMonotonicClock::Now();

    if (CMonotonicClock::HasElapsed(((psProgBarInfo)data)->lastUpdateTicks, 100, ticks) || done > 95)
    {
        // for performance reasons, do it just 3 times a second
        ((psProgBarInfo)data)->pViewer->SetProgress(done);
        ((psProgBarInfo)data)->lastUpdateTicks = ticks;
    }
    if (CMonotonicClock::HasElapsed(((psProgBarInfo)data)->lastCheckTicks, 500, ticks))
    {
        // for performance reasons, do it just twice a second
        MSG msg;
        HWND hWnd = ((psProgBarInfo)data)->pViewer->HWindow;

        while (PeekMessage(&msg, hWnd, WM_KEYUP, WM_KEYUP, PM_NOREMOVE))
        {
            int nVirtKey;    // virtual-key code
            LPARAM lKeyData; // key data

            GetMessage(&msg, hWnd, WM_KEYUP, WM_KEYUP);
            nVirtKey = (int)msg.wParam;
            lKeyData = msg.lParam;
            if (nVirtKey == 27)
            { // virtual-key code for ESC
                while (PeekMessage(&msg, hWnd, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE))
                {
                }
                return TRUE;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        ((psProgBarInfo)data)->lastCheckTicks = ticks;
    }

    return FALSE;
}

void EnableDisableControls(HWND hDlg, int firstID, int lastID, BOOL bEnable)
{
    for (; firstID <= lastID; firstID++)
    {
        EnableWindow(GetDlgItem(hDlg, firstID), bEnable);
    }
} /* EnableDisableControls */

void PositionControl(HWND hDlg, int baseID, int moveID)
{
    RECT r1, r2;
    HWND hParent;

    hParent = GetParent(hDlg);
    // Our templated dialog is a subwindow of the main common dlg window
    // GetWindowRect returns screen coordinates
    GetWindowRect(GetDlgItem(hParent, baseID), &r1);
    GetWindowRect(GetDlgItem(hDlg, moveID), &r2);
    ScreenToClient(hParent, (LPPOINT)&r1);
    ScreenToClient(hParent, &((LPPOINT)&r1)[1]);
    ScreenToClient(hDlg, (LPPOINT)&r2);
    ScreenToClient(hDlg, &((LPPOINT)&r2)[1]);
    SetWindowPos(GetDlgItem(hDlg, moveID), 0, r1.left, r2.top,
                 r1.right - r1.left, r2.bottom - r2.top, SWP_NOZORDER | SWP_SHOWWINDOW);
} /* PositionControl */


// ****************************************************************************
// The Save As dialog runs on the modern Shell file dialog: the former hook
// template panel is re-created through IFileDialogCustomize, and format-
// dependent controls refresh through IFileDialogEvents::OnTypeChange. Captions
// reuse the former template texts so the localization state stays unchanged.

#define PVSAVE_COMPRESSION       2001
#define PVSAVE_BITDEPTH          2002
#define PVSAVE_ROTATION          2003
#define PVSAVE_FLIP              2004
#define PVSAVE_INVERT            2005
#define PVSAVE_COMMENT           2006
#define PVSAVE_ADVANCED          2007
#define PVSAVE_GIF_INTERLACED    2008
#define PVSAVE_GIF_89A           2009
#define PVSAVE_TIFF_MAKE_STRIPS  2010
#define PVSAVE_JPEG_QUALITY      2011
#define PVSAVE_JPEG_SUBSAMPLING  2012
#define PVSAVE_TIFF_STRIP_SIZE   2013
#define PVSAVE_JPEG_QUALITY_TEXT 2014
#define PVSAVE_SUBSAMPLING_TEXT  2015
#define PVSAVE_STRIP_SIZE_TEXT   2016

typedef struct _PvSaveAsUiState
{
    LPPVImageInfo pvii;
    const TCHAR* filterDoubled; // double-null-terminated filter list
} PvSaveAsUiState;

static const TwoWords PvColorDepths[] = {{2, IDS_CLRS_MONO},
                                         {16, IDS_CLRS_16},
                                         {256, IDS_CLRS_256},
                                         {256, IDS_CLRS_256GR},
                                         {PV_COLOR_HC15, IDS_CLRS_HC15},
                                         {PV_COLOR_HC16, IDS_CLRS_HC16},
                                         {PV_COLOR_TC24, IDS_CLRS_TC24},
                                         {PV_COLOR_TC32, IDS_CLRS_TC32}};

static const TwoDWords PvRotations[] = {
    {IDS_ROT_NONE, 0},
    {IDS_ROT_90, PVSF_ROTATE90},
    {IDS_ROT_180, PVSF_FLIP_VERT | PVSF_FLIP_HOR},
    {IDS_ROT_270, PVSF_ROTATE90 | PVSF_FLIP_VERT | PVSF_FLIP_HOR},
    {0, 0}};
static const TwoDWords PvFlips[] = {
    {IDS_FLIP_NONE, 0},
    {IDS_FLIP_VERT, PVSF_FLIP_VERT},
    {IDS_FLIP_HOR, PVSF_FLIP_HOR},
    {0, 0}};

// given the selected file type index, extract PVF_xxx and the pattern segment;
// returns TRUE when the format supports a comment (we support saving one)
static BOOL PvGetFormatInfo(const TCHAR* filterDoubled, UINT typeIndex, DWORD* pFormat,
                            const TCHAR** pPattern)
{
    *pFormat = 0;
    UINT seg = 0;
    const TCHAR* s = filterDoubled;
    while (*s)
    {
        seg++;
        if (seg == 2 * typeIndex)
        {
            if (pPattern != NULL)
                *pPattern = s;
            while ((s = _tcschr(s, _T('.'))) != NULL)
            {
                s++;
                for (PVFS_FTYPE* ftype = fs_types; ftype->ext != NULL; ftype++)
                {
                    if (!_tcsnicmp(ftype->ext, s, _tcslen(ftype->ext)))
                    {
                        *pFormat = ftype->type & SAVEAS_COMMENT_MASK;
                        return (ftype->type & SAVEAS_COMMENT_FLAG) ? TRUE : FALSE;
                    }
                }
            }
            return FALSE;
        }
        while (*s)
            s++;
        s++;
    }
    return FALSE;
}

static HRESULT PvAddComboItem(IFileDialogCustomize* cust, DWORD ctrl, DWORD itemID, LPCTSTR label)
{
#ifdef UNICODE
    return cust->AddControlItem(ctrl, itemID, label);
#else
    int len = MultiByteToWideChar(CP_ACP, 0, label, -1, NULL, 0);
    if (len == 0)
        return E_FAIL;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide == NULL)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, label, -1, wide, len);
    HRESULT hr = cust->AddControlItem(ctrl, itemID, wide);
    free(wide);
    return hr;
#endif
}

static HRESULT PvSetEditTextTchar(IFileDialogCustomize* cust, DWORD ctrl, LPCTSTR text)
{
#ifdef UNICODE
    return cust->SetEditBoxText(ctrl, text);
#else
    int len = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (len == 0)
        return E_FAIL;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide == NULL)
        return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide, len);
    HRESULT hr = cust->SetEditBoxText(ctrl, wide);
    free(wide);
    return hr;
#endif
}

static HRESULT PvGetEditTextTchar(IFileDialogCustomize* cust, DWORD ctrl, LPTSTR buffer, size_t bufferCount)
{
    PWSTR textW = NULL;
    HRESULT hr = cust->GetEditBoxText(ctrl, &textW);
    if (FAILED(hr) || textW == NULL)
    {
        if (textW != NULL)
            CoTaskMemFree(textW);
        buffer[0] = 0;
        return FAILED(hr) ? hr : E_FAIL;
    }
#ifdef UNICODE
    size_t len = wcslen(textW) + 1;
    hr = len <= bufferCount ? S_OK : E_NOT_SUFFICIENT_BUFFER;
    if (len > bufferCount)
        len = bufferCount;
    memcpy(buffer, textW, (len - 1) * sizeof(TCHAR));
    buffer[len - 1] = 0;
#else
    int needed = WideCharToMultiByte(CP_ACP, 0, textW, -1, NULL, 0, NULL, NULL);
    if (needed <= 0)
    {
        CoTaskMemFree(textW);
        buffer[0] = 0;
        return E_FAIL;
    }
    char* wide = (char*)malloc(needed);
    if (wide == NULL)
    {
        CoTaskMemFree(textW);
        buffer[0] = 0;
        return E_OUTOFMEMORY;
    }
    WideCharToMultiByte(CP_ACP, 0, textW, -1, wide, needed, NULL, NULL);
    // clip deliberately like the former EM_LIMITTEXT + GetDlgItemText pair
    size_t copyLen = strlen(wide);
    if (copyLen >= bufferCount)
    {
        memcpy(buffer, wide, bufferCount - 1);
        buffer[bufferCount - 1] = 0;
        hr = E_NOT_SUFFICIENT_BUFFER;
    }
    else
    {
        memcpy(buffer, wide, copyLen + 1);
        hr = S_OK;
    }
    free(wide);
#endif
    CoTaskMemFree(textW);
    return hr;
}

// The Shell file dialog is Unicode-only, so TCHAR plug-in text converts at this boundary.
static WCHAR* DupPictViewWide(LPCTSTR text)
{
#ifdef UNICODE
    size_t len = _tcslen(text) + 1;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide != NULL)
        memcpy(wide, text, len * sizeof(WCHAR));
    return wide;
#else
    int len = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (len == 0)
        return NULL;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide != NULL && MultiByteToWideChar(CP_ACP, 0, text, -1, wide, len) == 0)
    {
        free(wide);
        return NULL;
    }
    return wide;
#endif
}

static HRESULT CreatePvSaveAsShellItem(LPCTSTR path, IShellItem** item)
{
    WCHAR* pathW = DupPictViewWide(path);
    if (pathW == NULL)
        return E_OUTOFMEMORY;
    HRESULT result = SHCreateItemFromParsingName(pathW, NULL, IID_PPV_ARGS(item));
    free(pathW);
    return result;
}

static BOOL DupPictViewTcharFromWide(const WCHAR* wide, LPTSTR buffer, DWORD bufferCount)
{
#ifdef UNICODE
    size_t len = wcslen(wide) + 1;
    if ((DWORD)len > bufferCount)
        return FALSE;
    memcpy(buffer, wide, len * sizeof(TCHAR));
    return TRUE;
#else
    int converted = WideCharToMultiByte(CP_ACP, 0, wide, -1, buffer, (int)bufferCount, NULL, NULL);
    // WideCharToMultiByte fails (instead of writing) when the buffer is too small
    return converted > 0 && strlen(buffer) < bufferCount; // refuse instead of truncating
#endif
}

// rebuild the bit-depth combo for the format/compression pair; returns the working
// color count (the former fallback loop adjusted it the same way)
static DWORD PvFillBitDepthItems(IFileDialogCustomize* cust, PvSaveAsUiState* ui, DWORD format,
                                 DWORD compr)
{
    DWORD cm = ui->pvii->ColorModel == PVCM_CMYK ? PVCM_RGB : ui->pvii->ColorModel;
    DWORD clrs = ui->pvii->Colors;
    if (clrs == 2)
        cm = PVCM_RGB; // save bilevel images as bilevel by default: some format may claim PVCM_GRAYS
    if ((clrs > 2) && (clrs < 16))
        clrs = 16;
    if ((clrs > 16) && (clrs < 256))
        clrs = 256;
    // Patera 2005.03.20: Save originally 32bit images as 24bit: The alpha was lost anyway
    if (clrs == PV_COLOR_TC32)
        clrs = PV_COLOR_TC24;

    cust->RemoveAllControlItems(PVSAVE_BITDEPTH);
    DWORD selected = (DWORD)-1;
    DWORD lastNonGray = (DWORD)-1;
    DWORD lastAdded = (DWORD)-1;
    DWORD workingClrs = clrs;
    for (int i = 0; i < sizeof(PvColorDepths) / sizeof(PvColorDepths[0]); i++)
    {
        if ((PvColorDepths[i][0] >= clrs) || (i > 0))
        {
            BOOL grays = PvColorDepths[i][1] == IDS_CLRS_256GR;
            if (PVW32DLL.PVIsOutCombSupported(format, compr, PvColorDepths[i][0],
                                              grays ? PVCM_GRAYS : PVCM_RGB) != -1)
            {
                DWORD itemID = PvColorDepths[i][0] | (grays ? SAVEAS_GRAY_FLAG : 0);
                TCHAR fmt[100];
                CQuadWord depthCount(PvColorDepths[i][0], 0);
                SalamanderGeneral->ExpandPluralString(fmt, sizeof(fmt), LoadStr(PvColorDepths[i][1]),
                                                      1, &depthCount);
                PvAddComboItem(cust, PVSAVE_BITDEPTH, itemID, fmt);
                lastAdded = itemID;
                if (!(itemID & SAVEAS_GRAY_FLAG))
                    lastNonGray = itemID;
                // exact match against the source model selects immediately
                if ((ui->pvii->Colors == PvColorDepths[i][0]) &&
                    ((grays ? FALSE : TRUE) ^ (cm == PVCM_GRAYS)))
                    selected = itemID;
                // the former fallback adjusted the working depth to the closest supported one
                if (((itemID & SAVEAS_GRAY_MASK) >= clrs) && (!(itemID & SAVEAS_GRAY_FLAG) ^ (cm == PVCM_GRAYS)) &&
                    workingClrs == clrs)
                    workingClrs = itemID & SAVEAS_GRAY_MASK;
                if ((ui->pvii->Colors == PV_COLOR_TC32) && (itemID == PV_COLOR_TC24))
                    workingClrs = PV_COLOR_TC24; // saving 32bit image to a format supporting 24 bits at max
            }
        }
    }
    if (selected == (DWORD)-1)
        selected = lastNonGray != (DWORD)-1 ? lastNonGray : lastAdded;
    if (selected != (DWORD)-1)
        cust->SetSelectedControlItem(PVSAVE_BITDEPTH, selected);
    return workingClrs;
}

// rebuild the compression combo for the format/depth pair
static void PvFillCompressionItems(IFileDialogCustomize* cust, PvSaveAsUiState* ui, DWORD format,
                                   DWORD clrs, BOOL grays, DWORD oldCompr)
{
    DWORD cm = ui->pvii->ColorModel == PVCM_CMYK ? PVCM_RGB : ui->pvii->ColorModel;
    if (grays)
        cm = PVCM_GRAYS;

    // looks like changing file format from JPEG to TIFF -> don't make JPEG-compr or raw the default for TIFF
    if (format == PVF_TIFF && ((oldCompr == PVCS_JPEG_HUFFMAN) || (oldCompr == PVCS_NO_COMPRESSION)))
        oldCompr = (DWORD)-1;

    cust->RemoveAllControlItems(PVSAVE_COMPRESSION);
    int cnt = 0;
    DWORD firstSupported = (DWORD)-1;
    for (PVFS_FTYPE* ctype = fs_comptypes; ctype->ext != NULL; ctype++)
    {
        if (PVW32DLL.PVIsOutCombSupported(format, ctype->type, clrs & SAVEAS_GRAY_MASK, cm) != -1)
        {
            PvAddComboItem(cust, PVSAVE_COMPRESSION, ctype->type, ctype->ext);
            if (firstSupported == (DWORD)-1)
                firstSupported = ctype->type;
            if (ctype->type == oldCompr)
                cust->SetSelectedControlItem(PVSAVE_COMPRESSION, ctype->type); // restore the original selection
            cnt++;
        }
    }
    if (cnt != 1)
    {
        // Note: cnt is zero for formats not directly supporting this bit depth (i.e. saving TC img as GIF)
        PvAddComboItem(cust, PVSAVE_COMPRESSION, PVCS_DEFAULT, LoadStr(IDS_SAVE_DEFAULT));
        cust->SetSelectedControlItem(PVSAVE_COMPRESSION, PVCS_DEFAULT); // select the first (default or only) compression
    }
    else if (oldCompr != (DWORD)-1)
        cust->SetSelectedControlItem(PVSAVE_COMPRESSION, oldCompr);
    else if (firstSupported != (DWORD)-1)
        cust->SetSelectedControlItem(PVSAVE_COMPRESSION, firstSupported);
}

// refresh everything that depends on the selected file type; mirrors the former
// CDN_TYPECHANGE handling where both lists rebuilt from the default compression
static void PvRefreshFormatControls(IFileDialogCustomize* cust, PvSaveAsUiState* ui, UINT typeIndex)
{
    DWORD format = 0;
    BOOL commentSupported = PvGetFormatInfo(ui->filterDoubled, typeIndex, &format, NULL);

    CDCONTROLSTATEF state = CDCS_ENABLEDVISIBLE;
    // comment availability follows the format
    cust->GetControlState(PVSAVE_COMMENT, &state);
    cust->SetControlState(PVSAVE_COMMENT, commentSupported ? (state | CDCS_ENABLED) : (state & ~CDCS_ENABLED));

    DWORD workingClrs = PvFillBitDepthItems(cust, ui, format, PVCS_DEFAULT);

    DWORD depthID = 0;
    BOOL grays = SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_BITDEPTH, &depthID)) &&
                 (depthID & SAVEAS_GRAY_FLAG) != 0;
    PvFillCompressionItems(cust, ui, format, workingClrs, grays, (DWORD)-1);

    // format-specific sections enable exactly like the former EnableDisableControls calls
    cust->GetControlState(PVSAVE_GIF_INTERLACED, &state);
    CDCONTROLSTATEF gifState = (format == PVF_GIF) ? (state | CDCS_ENABLED) : (state & ~CDCS_ENABLED);
    cust->SetControlState(PVSAVE_GIF_INTERLACED, gifState);
    cust->SetControlState(PVSAVE_GIF_89A, gifState);

    BOOL jpegActive = (format == PVF_JPG);
    DWORD comprID = 0;
    if (!jpegActive && SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_COMPRESSION, &comprID)) &&
        comprID == PVCS_JPEG_HUFFMAN)
        jpegActive = TRUE;
    cust->GetControlState(PVSAVE_JPEG_QUALITY, &state);
    CDCONTROLSTATEF jpegState = jpegActive ? (state | CDCS_ENABLED) : (state & ~CDCS_ENABLED);
    cust->SetControlState(PVSAVE_JPEG_QUALITY, jpegState);
    cust->SetControlState(PVSAVE_JPEG_SUBSAMPLING, jpegState);

    cust->GetControlState(PVSAVE_TIFF_MAKE_STRIPS, &state);
    CDCONTROLSTATEF tiffState = (format == PVF_TIFF) ? (state | CDCS_ENABLED) : (state & ~CDCS_ENABLED);
    cust->SetControlState(PVSAVE_TIFF_MAKE_STRIPS, tiffState);
    cust->SetControlState(PVSAVE_TIFF_STRIP_SIZE, tiffState);
}

class CPvSaveAsEvents : public IFileDialogEvents
{
public:
    PvSaveAsUiState* Ui;

    CPvSaveAsEvents(PvSaveAsUiState* ui) : Ui(ui), RefCount(1) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        // manual QI keeps this file free of the SHLWAPI QISearch dependency
        if (ppv == NULL)
            return E_POINTER;
        if (riid == IID_IUnknown)
            *ppv = static_cast<IUnknown*>(static_cast<IFileDialogEvents*>(this));
        else if (riid == IID_IFileDialogEvents)
            *ppv = static_cast<IFileDialogEvents*>(this);
        else
        {
            *ppv = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&RefCount); }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG ret = InterlockedDecrement(&RefCount);
        if (ret == 0)
            delete this;
        return ret;
    }

    STDMETHODIMP OnFileOk(IFileDialog*) override { return S_OK; }
    STDMETHODIMP OnFolderChanging(IFileDialog*, IShellItem*) override { return S_OK; }
    STDMETHODIMP OnFolderChange(IFileDialog*) override { return S_OK; }
    STDMETHODIMP OnSelectionChange(IFileDialog*) override { return S_OK; }
    STDMETHODIMP OnShareViolation(IFileDialog*, IShellItem*, FDE_SHAREVIOLATION_RESPONSE* response) override
    {
        *response = FDESVR_DEFAULT;
        return S_OK;
    }
    STDMETHODIMP OnOverwrite(IFileDialog*, IShellItem*, FDE_OVERWRITE_RESPONSE* response) override
    {
        *response = FDEOR_DEFAULT;
        return S_OK;
    }

    STDMETHODIMP OnTypeChange(IFileDialog* pfd) override
    {
        IFileDialogCustomize* cust = NULL;
        UINT typeIndex = 0;
        if (SUCCEEDED(pfd->QueryInterface(IID_PPV_ARGS(&cust))) && cust != NULL)
        {
            if (SUCCEEDED(pfd->GetFileTypeIndex(&typeIndex)) && typeIndex >= 1)
                PvRefreshFormatControls(cust, Ui, typeIndex);
            cust->Release();
        }
        return S_OK;
    }

private:
    LONG RefCount;
};

// create the whole customized panel once, before Show; captions reuse the
// former template texts
static HRESULT PvCreateSaveAsPanel(IFileDialogCustomize* cust, PvSaveAsUiState* ui)
{
    UNREFERENCED_PARAMETER(ui);
    HRESULT hr;
    hr = cust->StartVisualGroup(PVSAVE_COMPRESSION, L"Compression:");
    if (SUCCEEDED(hr))
        hr = cust->AddComboBox(PVSAVE_COMPRESSION);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->AddCheckButton(PVSAVE_ADVANCED, L"Options", FALSE);
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_BITDEPTH, L"Color depth:");
    if (SUCCEEDED(hr))
        hr = cust->AddComboBox(PVSAVE_BITDEPTH);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_ROTATION, L"Rotation:");
    if (SUCCEEDED(hr))
        hr = cust->AddComboBox(PVSAVE_ROTATION);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_FLIP, L"Flip/Mirror:");
    if (SUCCEEDED(hr))
        hr = cust->AddComboBox(PVSAVE_FLIP);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->AddCheckButton(PVSAVE_INVERT, L"Invert colors (make negative)", FALSE);
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_COMMENT, L"Comment:");
    if (SUCCEEDED(hr))
        hr = cust->AddEditBox(PVSAVE_COMMENT, L"");
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_GIF_INTERLACED, L"GIF");
    if (SUCCEEDED(hr))
        hr = cust->AddCheckButton(PVSAVE_GIF_INTERLACED, L"Interlaced", FALSE);
    if (SUCCEEDED(hr))
        hr = cust->AddCheckButton(PVSAVE_GIF_89A, L"Create GIF89a", FALSE);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_ADVANCED + 100, L"JPEG, TIFF-JPEG");
    if (SUCCEEDED(hr))
        hr = cust->AddText(PVSAVE_JPEG_QUALITY_TEXT, L"Quality (%):");
    if (SUCCEEDED(hr))
        hr = cust->AddEditBox(PVSAVE_JPEG_QUALITY, L"");
    if (SUCCEEDED(hr))
        hr = cust->AddText(PVSAVE_SUBSAMPLING_TEXT, L"Subsampling:");
    if (SUCCEEDED(hr))
        hr = cust->AddComboBox(PVSAVE_JPEG_SUBSAMPLING);
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    if (FAILED(hr))
        return hr;

    hr = cust->StartVisualGroup(PVSAVE_TIFF_MAKE_STRIPS, L"TIFF");
    if (SUCCEEDED(hr))
        hr = cust->AddCheckButton(PVSAVE_TIFF_MAKE_STRIPS, L"Make strips", FALSE);
    if (SUCCEEDED(hr))
        hr = cust->AddText(PVSAVE_STRIP_SIZE_TEXT, L"Strip size (KB):");
    if (SUCCEEDED(hr))
        hr = cust->AddEditBox(PVSAVE_TIFF_STRIP_SIZE, L"");
    if (SUCCEEDED(hr))
        hr = cust->EndVisualGroup();
    return hr;
}

// seed the persistent combos/checks from the stored state (former WM_INITDIALOG tail)
static void PvFillRotationFlipAndChecks(IFileDialogCustomize* cust, SAVEAS_INFO_PTR psai)
{
    for (const TwoDWords* r = PvRotations; r[0][0] != 0; r++)
        PvAddComboItem(cust, PVSAVE_ROTATION, r[0][1], LoadStr((int)r[0][0]));
    cust->SetSelectedControlItem(PVSAVE_ROTATION, psai->Rotation);
    for (const TwoDWords* f = PvFlips; f[0][0] != 0; f++)
        PvAddComboItem(cust, PVSAVE_FLIP, f[0][1], LoadStr((int)f[0][0]));
    cust->SetSelectedControlItem(PVSAVE_FLIP, psai->Flip);

    cust->SetCheckButtonState(PVSAVE_INVERT, (psai->Flags & PVSF_INVERT) != 0);
    cust->SetCheckButtonState(PVSAVE_GIF_INTERLACED, (G.Save.Flags & PVSF_INTERLACE) != 0);
    cust->SetCheckButtonState(PVSAVE_GIF_89A, (G.Save.Flags & PVSF_GIF89) != 0);
    cust->SetCheckButtonState(PVSAVE_TIFF_MAKE_STRIPS, !(G.Save.Flags & PVSF_DO_NOT_STRIP));

    TCHAR numBuf[16];
    _stprintf(numBuf, _T("%u"), (unsigned)G.Save.JPEGQuality);
    PvSetEditTextTchar(cust, PVSAVE_JPEG_QUALITY, numBuf);
    PvAddComboItem(cust, PVSAVE_JPEG_SUBSAMPLING, 0, _T("1:1:1"));
    PvAddComboItem(cust, PVSAVE_JPEG_SUBSAMPLING, 1, _T("2:1:1"));
    cust->SetSelectedControlItem(PVSAVE_JPEG_SUBSAMPLING, G.Save.JPEGSubsampling);
    _stprintf(numBuf, _T("%u"), (unsigned)G.Save.TIFFStripSize);
    PvSetEditTextTchar(cust, PVSAVE_TIFF_STRIP_SIZE, numBuf);
}

// read every option back into the local state (former WM_DESTROY body)
static void PvHarvestSaveAsState(IFileDialogCustomize* cust, SAVEAS_INFO_PTR psai)
{
    DWORD id = 0;
    psai->Compression = PVCS_DEFAULT;
    if (SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_COMPRESSION, &id)))
        psai->Compression = id;
    if (SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_BITDEPTH, &id)))
        psai->Colors = id;
    psai->Flags = 0;
    if (SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_FLIP, &id)))
        psai->Flip = id;
    if (SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_ROTATION, &id)))
        psai->Rotation = id;
    BOOL checked = FALSE;
    if (SUCCEEDED(cust->GetCheckButtonState(PVSAVE_INVERT, &checked)) && checked)
        psai->Flags |= PVSF_INVERT;
    TCHAR comment[SAVEAS_MAX_COMMENT_SIZE];
    if (FAILED(PvGetEditTextTchar(cust, PVSAVE_COMMENT, comment, SAVEAS_MAX_COMMENT_SIZE)))
        comment[SAVEAS_MAX_COMMENT_SIZE - 1] = 0;
    memcpy(psai->Comment, comment, SAVEAS_MAX_COMMENT_SIZE);

    G.Save.Flags = 0;
    if (SUCCEEDED(cust->GetCheckButtonState(PVSAVE_GIF_INTERLACED, &checked)) && checked)
        G.Save.Flags |= PVSF_INTERLACE;
    if (SUCCEEDED(cust->GetCheckButtonState(PVSAVE_GIF_89A, &checked)) && checked)
        G.Save.Flags |= PVSF_GIF89;
    if (SUCCEEDED(cust->GetCheckButtonState(PVSAVE_TIFF_MAKE_STRIPS, &checked)) && !checked)
        G.Save.Flags |= PVSF_DO_NOT_STRIP;

    TCHAR quality[16];
    if (SUCCEEDED(PvGetEditTextTchar(cust, PVSAVE_JPEG_QUALITY, quality, _countof(quality))))
    {
        int q = _ttoi(quality);
        G.Save.JPEGQuality = (DWORD)min(100, max(q, 1));
    }
    if (SUCCEEDED(cust->GetSelectedControlItem(PVSAVE_JPEG_SUBSAMPLING, &id)))
        G.Save.JPEGSubsampling = id;
    TCHAR stripSize[16];
    if (SUCCEEDED(PvGetEditTextTchar(cust, PVSAVE_TIFF_STRIP_SIZE, stripSize, _countof(stripSize))))
        G.Save.TIFFStripSize = (DWORD)_ttoi(stripSize);
}

// releases every dialog resource on any exit path of OnFileSaveAs
struct PvSaveAsDialogScope
{
    IFileSaveDialog* Dialog;
    IFileDialogCustomize* Cust;
    CPvSaveAsEvents* Events;
    COMDLG_FILTERSPEC* Specs;
    WCHAR** Texts;
    UINT TextCount;
    BOOL ComOwned;
    ~PvSaveAsDialogScope()
    {
        if (Texts != NULL)
        {
            for (UINT i = 0; i < TextCount * 2; i++)
                free(Texts[i]);
            delete[] Texts;
        }
        delete[] Specs;
        if (Cust != NULL)
            Cust->Release();
        if (Dialog != NULL)
            Dialog->Release();
        if (Events != NULL)
            Events->Release(); // deletes the object once its advise reference is gone
        if (ComOwned)
            CoUninitialize();
    }
};

const char* StrIStr(const char* txt, const char* pattern)
{
    if (txt == NULL || pattern == NULL)
        return NULL;

    const char* s = txt;
    int len = (int)strlen(pattern);
    int txtLen = (int)strlen(txt);
    while (txtLen >= len)
    {
        if (SalamanderGeneral->StrNICmp(s, pattern, len) == 0)
            return s;
        s++;
        txtLen--;
    }
    return NULL;
}

BOOL CRendererWindow::OnFileSaveAs(LPCTSTR pInitDir)
{
    static int cntClipboard = 1;
    static int cntCapture = 1;
    static int cntScan = 1;
    static SAVEAS_INFO sai = {PVCS_DEFAULT, 0, 0, 0, 0, 0, ""};
    SAVEAS_INFO lsai;
    TCHAR errBuff[1000];
    TCHAR fileName[MAX_PATH];
    LPTSTR s;
    int* pCnt = NULL;
    int ret;
    DWORD format;
    TCHAR initDir[MAX_PATH] = _T("");

    if (((pvii.Format == PVF_ICO) || (pvii.Format == PVF_PNG) || (pvii.Format == PVF_TGA) || (pvii.Format == PVF_TIFF) || (pvii.Format == PVF_ANI) || (pvii.Format == PVF_PSD)) && (pvii.Colors == PV_COLOR_TC32))
    {
        /*     MSGBOXEX_PARAMS  mboxParams;

     memset(&mboxParams, 0, sizeof(mboxParams));
     mboxParams.HParent = HWindow;
     mboxParams.Text = LoadStr(IDS_SAVE_LOST_ALPHA); mboxParams.Caption = LoadStr(IDS_PLUGINNAME);
     mboxParams.Flags = MSGBOXEX_YESNO | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT | MSGBOXEX_ESCAPEENABLED;
     mboxParams.CheckBoxText = LoadStr(IDS_DONT_SHOW_AGAIN);
*/
        if (!(G.DontShowAnymore & DSA_ALPHA_LOST))
        {
            BOOL bChecked = FALSE;
            int ret2 = ShowOneTimeMessage(HWindow, IDS_SAVE_LOST_ALPHA, &bChecked,
                                          MSGBOXEX_YESNO | MSGBOXEX_ICONQUESTION | MSGBOXEX_SILENT,
                                          IDS_DONT_SHOW_AGAIN_SLA);

            if (bChecked)
            {
                G.DontShowAnymore |= DSA_ALPHA_LOST;
            }
            if (IDYES != ret2)
            {
                //            SalamanderGeneral->SalMessageBoxEx(&mboxParams)
                return FALSE;
            }
        }
    }
    // use local copy to make several simultaneously open SaveAs dialogs work
    lsai = sai;
    if (pInitDir)
    {
        StringCchCopyNA(initDir, SizeOf(initDir), pInitDir, SizeOf(initDir)); // counted bounded copy instead of lstrcpyn
    }
    else
    {
        if (FileName[0] == '<')
        {
            StringCchCopyNA(initDir, SizeOf(initDir), G.Save.InitDir, SizeOf(initDir)); // counted bounded copy instead of lstrcpyn
        }
        else
        {
            StringCchCopyNA(initDir, SizeOf(initDir), FileName, SizeOf(initDir)); // counted bounded copy instead of lstrcpyn
            if (!SalamanderGeneral->CutDirectory(initDir))
                initDir[0] = 0;
        }
    }

    if (initDir[0] == 0)
        GetMyDocumentsPath(initDir);

    // store the filename without path and suffix
    s = (LPTSTR)_tcsrchr(FileName, '\\');
    _tcscpy(fileName, s ? (s + 1) : _T(""));
    s = (LPTSTR)_tcsrchr(fileName, '.');
    if (s)
        *s = 0; // ".cvspass" is extension in Windows
    if (FileName[0] == '<')
    {
        int nameID;

        if (!_tcscmp(FileName, LoadStr(IDS_CLIPBOARD_TITLE)))
        {
            nameID = IDS_CLIPBOARD_FNAME;
            pCnt = &cntClipboard;
        }
        else if (!_tcscmp(FileName, LoadStr(IDS_CAPTURE_TITLE)))
        {
            nameID = IDS_CAPTURE_FNAME;
            pCnt = &cntCapture;
        }
        else if (!_tcscmp(FileName, LoadStr(IDS_SCAN_TITLE)))
        {
            nameID = IDS_SCAN_FNAME;
            pCnt = &cntScan;
        }
        else
        { // can only be deleted image -> no name provided
            fileName[0] = 0;
            nameID = -1;
        }
        if (nameID != -1)
        {
            _stprintf(fileName, _T("%s%d"), LoadStr(nameID), *pCnt);
        }
    }

    // The Save As dialog runs on the modern Shell interface; the customized panel
    // replaces the former hook template while keeping every option and its defaults.
    UINT filterIndex;
    TCHAR filterStr[1000];
    if (pvii.Colors == 2)
    {
        filterIndex = G.LastSaveAsFilterIndexMono;
        StringCchCopyNA(filterStr, 1000, LoadStr(IDS_SAVEASFILTERMONO), 1000); // counted bounded copy instead of lstrcpyn
    }
    else
    {
        filterIndex = G.LastSaveAsFilterIndexColor;
        StringCchCopyNA(filterStr, 1000, LoadStr(IDS_SAVEASFILTERCOLOR), 1000); // counted bounded copy instead of lstrcpyn
    }
    s = filterStr;
    DWORD filtersDoubledCount = 0;
    while (*s != 0)
    { // create a double-null-terminated list
        if (*s == '|')
        {
            *s = 0;
            filtersDoubledCount++;
        }
        s++;
    }
    UINT pairCount = filtersDoubledCount / 2;
    if (filterIndex > pairCount)
        filterIndex = pairCount;

    lsai.pvii = &pvii;
    // Start with no rotation & no flip
    lsai.Rotation = lsai.Flip = 0;
    CALL_STACK_MESSAGE2(_T("OnFileSaveAs: GSFN(%s)"), FileName);

    HRESULT comInit = CoInitialize(NULL);
    if (FAILED(comInit) && comInit != RPC_E_CHANGED_MODE)
        return FALSE;
    BOOL comOwned = SUCCEEDED(comInit);

    PvSaveAsUiState uiState = {&pvii, filterStr};
    IFileSaveDialog* fileDialog = NULL;
    IFileDialogCustomize* cust = NULL;
    CPvSaveAsEvents* events = new CPvSaveAsEvents(&uiState);
    DWORD cookie = 0;
    BOOL specsOK = events != NULL &&
                   SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&fileDialog))) &&
                   fileDialog != NULL &&
                   SUCCEEDED(fileDialog->QueryInterface(IID_PPV_ARGS(&cust))) && cust != NULL;
    COMDLG_FILTERSPEC* specs = NULL;
    WCHAR** texts = NULL;
    UINT filled = 0;
    if (specsOK)
    {
        // build COMDLG_FILTERSPEC from the doubled list
        specs = new COMDLG_FILTERSPEC[pairCount];
        texts = new WCHAR*[pairCount * 2]();
        specsOK = specs != NULL && texts != NULL;
        const TCHAR* fs = filterStr;
        while (specsOK && *fs != '\0' && filled < pairCount * 2)
        {
            WCHAR* wide = DupPictViewWide(fs); // dup stops at the segment terminator
            if (wide == NULL)
            {
                specsOK = FALSE;
                break;
            }
            texts[filled] = wide;
            if ((filled % 2) == 0)
                specs[filled / 2].pszName = wide;
            else
                specs[filled / 2].pszSpec = wide;
            filled++;
            while (*fs)
                fs++;
            fs++;
        }
        specsOK = specsOK && filled == pairCount * 2;
    }

    // releases every dialog resource on any exit path of OnFileSaveAs
    struct PvSaveAsDialogScope
    {
        IFileSaveDialog* Dialog;
        IFileDialogCustomize* Cust;
        CPvSaveAsEvents* Events;
        COMDLG_FILTERSPEC* Specs;
        WCHAR** Texts;
        UINT PairCount;
        BOOL ComOwned;
        ~PvSaveAsDialogScope()
        {
            if (Texts != NULL)
            {
                for (UINT i = 0; i < PairCount * 2; i++)
                    free(Texts[i]);
                delete[] Texts;
            }
            delete[] Specs;
            if (Cust != NULL)
                Cust->Release();
            if (Dialog != NULL)
                Dialog->Release();
            if (Events != NULL)
                Events->Release(); // deletes the object once its advise reference is gone
            if (ComOwned)
                CoUninitialize();
        }
    } pvScope = {fileDialog, cust, events, specs, texts, pairCount, comOwned};

    if (specsOK)
    {
        fileDialog->SetFileTypes(pairCount, specs);
        fileDialog->SetFileTypeIndex(filterIndex);
        {
            WCHAR* nameW = DupPictViewWide(fileName);
            if (nameW != NULL)
            {
                fileDialog->SetFileName(nameW);
                free(nameW);
            }
        }
        {
            IShellItem* folder = NULL;
            if (SUCCEEDED(CreatePvSaveAsShellItem(initDir, &folder)))
            {
                fileDialog->SetFolder(folder);
                folder->Release();
            }
        }
        DWORD options = FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        fileDialog->GetOptions(&options);
        // the manual overwrite/readonly prompts below stay in charge
        options |= FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
        options &= ~FOS_OVERWRITEPROMPT;
        fileDialog->SetOptions(options);

        // the customized panel is created once and keeps its state across re-shows
        PvCreateSaveAsPanel(cust, &uiState);
        PvFillRotationFlipAndChecks(cust, &lsai);
        PvRefreshFormatControls(cust, &uiState, filterIndex);
        fileDialog->Advise(events, &cookie);
    }

    for (;;)
    {
        LPTSTR s2;
        HANDLE hFile;

        BOOL dialogAccepted = FALSE;
        if (specsOK && SUCCEEDED(fileDialog->Show(HWindow)))
        {
            dialogAccepted = TRUE;
            // harvest every option from the customized panel into the local state
            PvHarvestSaveAsState(cust, &lsai);
            IShellItem* item = NULL;
            PWSTR pathW = NULL;
            if (SUCCEEDED(fileDialog->GetResult(&item)) && item != NULL &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)) && pathW != NULL)
            {
                // a result that cannot fit the fixed buffer is refused instead of truncated
                dialogAccepted = DupPictViewTcharFromWide(pathW, fileName, MAX_PATH);
                CoTaskMemFree(pathW);
            }
            else
                dialogAccepted = FALSE;
            if (item != NULL)
                item->Release();
        }

        if (!dialogAccepted)
        {
            // don't save options
            return FALSE;
        }

        format = 0;
        if (pvii.Colors == 2)
        {
            // remember the filter index for the next time
            G.LastSaveAsFilterIndexMono = filterIndex;
        }
        else
        {
            G.LastSaveAsFilterIndexColor = filterIndex;
        }
        {
            // derive the initial directory from the chosen path (the former
            // nFileOffset projection is replaced by a leaf-name cut)
            LPTSTR leaf = (LPTSTR)_tcsrchr(fileName, '\\');
            if (leaf != NULL)
            {
                size_t dirLen = leaf - fileName;
                if (dirLen >= SizeOf(initDir))
                    dirLen = SizeOf(initDir) - 1;
                memcpy(initDir, fileName, dirLen * sizeof(TCHAR));
                initDir[dirLen] = 0;
            }
        }
        UINT selectedType = filterIndex;
        fileDialog->GetFileTypeIndex(&selectedType);
        PvGetFormatInfo(filterStr, selectedType, &format, (const TCHAR**)&s);
        if (_tcschr(s, '*') && (format == PVF_IRF))
        {
            // suffix depends on compression
            if ((lsai.Compression == PVCS_CCITT_4) || (lsai.Compression == PVCS_DEFAULT))
            {
                static char buffCIT[] = ".cit";
                s = _T(buffCIT);
            }
            else
            {
                static char buffDAT[] = ".dat";
                s = _T(buffDAT);
            }
        }

        if ((FileName[0] == '<') && G.Save.RememberPath)
            StringCchCopyNA(G.Save.InitDir, SizeOf(G.Save.InitDir), initDir, SizeOf(G.Save.InitDir)); // counted bounded copy instead of lstrcpyn

        ret = (int)_tcslen(fileName);
        if (fileName[ret - 1] == '.')
        {
            // user doesn't want us to append any suffix
            fileName[ret - 1] = 0;
        }
        else
        {
            LPTSTR ext;

            // the leaf-name start replaces the former nFileOffset projection
            LPTSTR nameStart = (LPTSTR)_tcsrchr(fileName, '\\');
            nameStart = nameStart != NULL ? nameStart + 1 : fileName;

            ext = (LPTSTR)_tcsrchr(nameStart, '.');
            if (ext < nameStart)
            { // ".cvspass" is extension in Windows
                ext = fileName + ret;
            }
            if (StrIStr(s, ext))
                s = (LPTSTR)StrIStr(s, ext);
            // strip off additional suffixes, if present
            s2 = (LPTSTR)_tcschr(s, ';');
            if (s2)
                *s2 = 0;

            if (_tcsicmp(ext, s))
            {
                // not a default one
                s2 = (LPTSTR)_tcsrchr(nameStart, '\\');
                // find file name beginning
                if (!s2)
                {
                    s2 = nameStart;
                }
                else
                {
                    s2++;
                }
                // check whether it consists just of upper-case letters & digits
                while (((*s2 >= 'A') && (*s2 <= 'Z')) || ((*s2 >= '0') && (*s2 <= '9')) || (*s2 == '_'))
                    s2++;
                if (!*s2)
                {
                    // yes, it does -> make it lower-case like extension
                    _tcslwr(nameStart);
                }
                if ((ext - fileName) + strlen(s) < _countof(fileName))
                    _tcscat(ext, s); // append the default suffix
                else
                {
                    SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_TOOLONGNAME),
                                                     LoadStr(IDS_ERRORTITLE),
                                                     MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }
            }
        }
        hFile = CreateFileUtf8Local(fileName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING, 0, 0);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(hFile);
            _stprintf(errBuff, LoadStr(IDS_SAVE_ERR_EXISTS_OVERWRITE), fileName);
            ret = SalamanderGeneral->SalMessageBox(HWindow, errBuff,
                                                   LoadStr(IDS_ERRORTITLE), MB_ICONEXCLAMATION | MB_YESNOCANCEL);
            if (ret == IDCANCEL)
            {
                // store options
                sai = lsai;
                return TRUE;
            }
            if (ret != IDYES)
            {
                // ask for a new name
                continue;
            }
        }
        else
        {
            ret = GetLastError();
            if (ret == ERROR_FILE_NOT_FOUND)
            {
                break;
            }
            if (ret == ERROR_ACCESS_DENIED)
            {
                // No rights or R/O attribute - check what is the case
                ret = SalamanderGeneral->SalGetFileAttributes(fileName); // 0xFFFFFFFF on error
                if ((ret != 0xFFFFFFFF) && (ret & FILE_ATTRIBUTE_READONLY))
                {
                    // R/O attrib
                    _stprintf(errBuff, LoadStr(IDS_READ_ONLY_REWRITE), fileName);
                    ret = SalamanderGeneral->SalMessageBox(HWindow, errBuff,
                                                           LoadStr(IDS_ERRORTITLE), MB_ICONEXCLAMATION | MB_YESNOCANCEL);
                    if (ret == IDCANCEL)
                    {
                        sai = lsai; // store options
                        return TRUE;
                    }
                    if (ret != IDYES)
                    {
                        // ask for a new name
                        continue;
                    }
                    SalamanderGeneral->ClearReadOnlyAttr(fileName);
                }
                // else: no rights: DeleteFile should also fail with ERROR_ACCESS_DENIED
            }
        }
        if (!DeleteFileUtf8Local(fileName))
        {
            ret = GetLastError();
            SalamanderGeneral->GetErrorText(ret, errBuff, SizeOf(errBuff));
            if (IDCANCEL == SalamanderGeneral->SalMessageBox(HWindow, errBuff,
                                                             LoadStr(IDS_ERRORTITLE), MB_ICONEXCLAMATION | MB_OKCANCEL))
            {
                sai = lsai; // store options
                return TRUE;
            }
        }
        else
        {
            break; // file deleted successfully
        }
    }
    ret = SaveImage(fileName, format, &lsai);

    // report the change on the path (our file has appeared)
    TCHAR changedPath[MAX_PATH];
    StringCchCopyNA(changedPath, MAX_PATH, fileName, MAX_PATH); // counted bounded copy instead of lstrcpyn
    SalamanderGeneral->CutDirectory(changedPath);
    SalamanderGeneral->PostChangeOnPathNotification(changedPath, FALSE);

    if (ret != PVC_OK)
    {
        if (ret != PVC_CANCELED)
        {
            _stprintf(errBuff, LoadStr(IDS_SAVEERROR), PVW32DLL.PVGetErrorText(ret)); //"Canceled (error example)");
            SalamanderGeneral->SalMessageBox(HWindow, errBuff, LoadStr(IDS_ERRORTITLE),
                                             MB_ICONEXCLAMATION | MB_OK);
        }
        else
        {
            SalamanderGeneral->SalMessageBox(HWindow, LoadStr(IDS_CANCELED_BY_USER),
                                             LoadStr(IDS_PLUGINNAME), MB_OK | MB_ICONINFORMATION | MSGBOXEX_SILENT);
        }
    }
    else
    {
        if (pCnt)
        {
            (*pCnt)++; // increase counter only on successful save
        }
        if (!(G.DontShowAnymore & DSA_SAVE_SUCCESS))
        {
            BOOL checked = FALSE;

            ShowOneTimeMessage(HWindow, IDS_SAVE_AS_SUCCESS, &checked, MSGBOXEX_OK);
            if (checked)
            {
                G.DontShowAnymore |= DSA_SAVE_SUCCESS;
            }
        }
        else
        {
            Viewer->SetStatusBarTexts(IDS_SAVE_AS_SUCCESS);
            bEatSBTextOnce = TRUE;
        }
        sai.PrevInputColors = pvii.Colors;
    }
    sai = lsai;
    return TRUE;
}

// saves the image into the file 'fileName' using format 'format' (PVF_xxx)
// returns the PVSaveImage function's return value
int CRendererWindow::SaveImage(LPCTSTR fileName, DWORD format, SAVEAS_INFO_PTR psai)
{
    PVSaveImageInfo sii;
    sProgBarInfo pbi;

    memset(&sii, 0, sizeof(PVSaveImageInfo));
    sii.cbSize = sizeof(PVSaveImageInfo);
    sii.Format = format;
    sii.ColorModel = pvii.ColorModel == PVCM_CMYK ? PVCM_RGB : pvii.ColorModel;
    // psai is NULL when saving wallpaper
    sii.Colors = psai ? (psai->Colors & SAVEAS_GRAY_MASK) : pvii.Colors;
    if (psai)
    {
        if (psai->Colors & SAVEAS_GRAY_FLAG)
        {
            sii.ColorModel = PVCM_GRAYS;
        }
        else if (sii.Colors > 256)
        {
            // must be reset to RGB when saving originally Grayscale image as Hi/TrueColor
            sii.ColorModel = PVCM_RGB;
        }
    }
    sii.Compression = psai ? psai->Compression : PVCS_DEFAULT;
    sii.Flags = psai ? (psai->Flags & (PVSF_INVERT | PVSF_ROTATE90 | PVSF_FLIP_HOR | PVSF_FLIP_VERT)) : 0;
    // Flip DPI if rotating
    sii.HorDPI = (sii.Flags & PVSF_ROTATE90) ? pvii.VerDPI : pvii.HorDPI;
    sii.VerDPI = (sii.Flags & PVSF_ROTATE90) ? pvii.HorDPI : pvii.VerDPI;
#if 0
  if (PVW32DLL.PVIsOutCombSupported(sii.Format, PVCS_DEFAULT, sii.Colors, sii.ColorModel) == -1) {
     // the target format does not support the source bit depth with any compression scheme
     // we must perform some bit-depth conversion
     int i;
     for (i = 0; i < 6; i++) {
        switch (sii.Colors) { // we try to upgrade bit depth
           case 2: sii.Colors = 16; break;
           case PV_COLOR_HC15: sii.Colors = PV_COLOR_HC16; break;
           case PV_COLOR_HC16: sii.Colors = PV_COLOR_TC24; break;
           case PV_COLOR_TC24: sii.Colors = PV_COLOR_TC32; break;
           case PV_COLOR_TC32: sii.Colors = 256; break; // some formats are 8bit only
           default: if ((sii.Colors >= 3) && (sii.Colors <= 16)) sii.Colors = 256;
             else if ((sii.Colors > 16) && (sii.Colors <= 256)) sii.Colors = PV_COLOR_HC15;
        }
        if (PVW32DLL.PVIsOutCombSupported(sii.Format, PVCS_DEFAULT, sii.Colors, sii.ColorModel) != -1) {
           break; // found one!!
        }
     }
  }
#endif
    if (fMirrorHor)
    {
        // Patch: PVW32Cnv.dll will mirror the image in memory
        sii.Flags ^= PVSF_FLIP_HOR;
        fMirrorHor = FALSE;
        PVW32DLL.PVSetStretchParameters(PVHandle, XStretchedRange,
                                        YStretchedRange * (1 - 2 * fMirrorVert), COLORONCOLOR);
    }
    if (fMirrorVert)
    {
        sii.Flags ^= PVSF_FLIP_VERT;
    }
    if (sii.Format == PVF_GIF)
    {
        sii.Flags |= G.Save.Flags & (PVSF_GIF89 | PVSF_INTERLACE);
    }
    if (sii.Format == PVF_JPG)
    {
        sii.Misc.JPEG.Quality = G.Save.JPEGQuality;
        // 0 means 2x1:1:1        = here 0 means 1:1:1
        sii.Misc.JPEG.SubSampling = !G.Save.JPEGSubsampling;
    }
    if (sii.Format == PVF_TIFF)
    {
        sii.Flags |= G.Save.Flags & PVSF_DO_NOT_STRIP;
        sii.Misc.TIFF.StripSize = G.Save.TIFFStripSize;
        sii.Misc.TIFF.JPEGQuality = G.Save.JPEGQuality;
        // 0 means 2x1:1:1        = here 0 means 1:1:1
        sii.Misc.TIFF.JPEGSubSampling = !G.Save.JPEGSubsampling;
    }
    sii.Transp.Flags = PVTF_ORIGINAL; // Preserve transparency
    if (psai && psai->Comment[0])
    {
        sii.Comment = psai->Comment;
        // we save terminating zero only in TIFFs
        sii.CommentSize = (int)strlen(sii.Comment) + (sii.Format == PVF_TIFF ? 1 : 0);
    }

    // refresh the window so it does not look messy during longer saves after the SaveAs dialog
    UpdateWindow(Viewer->HWindow);
    HCURSOR hOldCur = SetCursor(LoadCursor(NULL, IDC_WAIT));
    CALL_STACK_MESSAGE6("Save pars: %ux%ux%u, %u, %u", pvii.Width, pvii.Height, sii.Colors, sii.Format, sii.Flags);

    Viewer->InitProgressBar();
    pbi.pViewer = Viewer;
    // Initialize both callback throttles from the same 64-bit sample.
    pbi.lastCheckTicks = pbi.lastUpdateTicks = CMonotonicClock::Now();
#ifdef _UNICODE
    // UTF-8 at the engine boundary: the WIC engine decodes these back to
    // UTF-16, so a path outside the ANSI code page reaches the codecs intact.
    char fileNameA[_MAX_PATH * 3];

    WideToUtf8Buffer(fileName, fileNameA, (int)sizeof(fileNameA));
    int saveRet = PVW32DLL.PVSaveImage(PVHandle, fileNameA, &sii, SaveProgressProcedure, &pbi, pvii.CurrentImage);
#else
    int saveRet = PVW32DLL.PVSaveImage(PVHandle, fileName, &sii, SaveProgressProcedure, &pbi, pvii.CurrentImage);
#endif
    Viewer->KillProgressBar();
    SetCursor(hOldCur);

    return saveRet;
}
