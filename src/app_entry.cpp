// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "update_check.h"
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
#include "app_shutdown.h" // shutdown sequence moved to app_shutdown.cpp

#pragma comment(linker, "/ENTRY:MyEntryPoint") // we want our own entry point to the application

#pragma comment(lib, "UxTheme.lib")

typedef BOOL(WINAPI* FSetDefaultDllDirectories)(DWORD directoryFlags);
typedef PVOID(WINAPI* FAddDllDirectory)(PCWSTR newDirectory);

// Restrict process-wide DLL resolution before any optional module is loaded.
// Windows 7 requires KB2533623 for these APIs; continuing without it would
// restore the unsafe current-directory and PATH search behavior.
static BOOL InitializeDllSearchPaths()
{
    HMODULE kernel32 = GetModuleHandle(TEXT("kernel32.dll"));
    FSetDefaultDllDirectories setDefaultDllDirectories =
        kernel32 == NULL ? NULL : (FSetDefaultDllDirectories)GetProcAddress(kernel32, "SetDefaultDllDirectories");
    FAddDllDirectory addDllDirectory =
        kernel32 == NULL ? NULL : (FAddDllDirectory)GetProcAddress(kernel32, "AddDllDirectory");
    if (setDefaultDllDirectories == NULL || addDllDirectory == NULL)
    {
        SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
        return FALSE;
    }

    const DWORD defaultDirectoryFlags = LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                                        LOAD_LIBRARY_SEARCH_SYSTEM32 |
                                        LOAD_LIBRARY_SEARCH_USER_DIRS;
    if (!setDefaultDllDirectories(defaultDirectoryFlags))
        return FALSE;

    WCHAR applicationPath[MAX_PATH];
    DWORD applicationPathLength = GetModuleFileNameW(NULL, applicationPath, _countof(applicationPath));
    if (applicationPathLength == 0 || applicationPathLength >= _countof(applicationPath))
    {
        if (applicationPathLength >= _countof(applicationPath))
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    WCHAR* fileName = wcsrchr(applicationPath, L'\\');
    if (fileName == NULL)
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }
    *fileName = L'\0';

    // This is the sole process-wide user directory. Plug-ins use their own
    // canonical DLL path and LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR instead.
    return addDllDirectory(applicationPath) != NULL;
}

// expose the original entry point of the application
extern "C" int WinMainCRTStartup();

#ifdef X64_STRESS_TEST

#define X64_STRESS_TEST_ALLOC_COUNT 1000

LPVOID X64StressTestPointers[X64_STRESS_TEST_ALLOC_COUNT];

void X64StressTestAlloc()
{
    // at this point, loader has already loaded EXE and RTL, and during RTL initialization a heap was created and allocated,
    // which resides at addresses below 4GB; to push further allocations above 4GB, we must occupy the lower part
    // of virtual memory and then force RTL through allocations to expand its heap
    //
    // occupy space in virtual memory
    UINT64 vaAllocated = 0;
    _int64 allocSize[] = {10000000, 1000000, 100000, 10000, 1000, 100, 10, 1, 0};
    for (int i = 0; allocSize[i] != 0; i++)
        while (VirtualAlloc(0, allocSize[i], MEM_RESERVE, PAGE_NOACCESS) <= (LPVOID)(UINT_PTR)0x00000000ffffffff) // on access we want exception and we don't want MEM_COMMIT, so we don't waste space unnecessarily
            vaAllocated += allocSize[i];

    // now inflate RTL heap
    UINT64 rtlAllocated = 0;
    _int64 rtlAllocSize[] = {10000000, 1000000, 100000, 10000, 1000, 100, 10, 1, 0};
    for (int i = 0; rtlAllocSize[i] != 0; i++)
        while (_malloc_dbg(rtlAllocSize[i], _CRT_BLOCK, __FILE__, __LINE__) <= (LPVOID)(UINT_PTR)0x00000000ffffffff)
            rtlAllocated += rtlAllocSize[i];

    // check success
    void* testNew = new char; // new goes through alloc, but we should verify it too
    if (testNew <= (LPVOID)(UINT_PTR)0x00000000ffffffff)
        MessageBox(NULL, "new address <= 0x00000000ffffffff!\nPlease contact support@taskscape.com with this information.", "X64_STRESS_TEST", MB_OK | MB_ICONEXCLAMATION);
    delete testNew;
}

#endif //X64_STRESS_TEST
// our own entry point, which we requested from linker using pragma
int MyEntryPoint()
{
#ifdef X64_STRESS_TEST
    // through allocations we eat up lower 4GB of memory, so that further allocations have pointers larger than DWORD
    X64StressTestAlloc();
#endif //X64_STRESS_TEST

    int ret = 1; // error

    // start Salmon, we want it to catch maximum of our crashes
    if (SalmonInit())
    {
        // call original entry point of application and start the program
        ret = WinMainCRTStartup();
    }
    else
        MessageBox(NULL, "Open Salamander Bug Reporter (salmon.exe) initialization has failed. Please reinstall Open Salamander.",
                   SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONSTOP);

    // debugger doesn't come here anymore, it gets killed in RTL (tested under VC 2008 with our RTL)

    // finishing
    return ret;
}

static DWORD Crc32Tab[256];
static BOOL Crc32TabInitialized = FALSE;

// Defined in app_globals.cpp
extern char OpenReadmeInNotepad[MAX_PATH];

void MakeCrc32Table(DWORD* crcTab)
{
    DWORD c;
    DWORD poly = 0xedb88320L; //polynomial exclusive-or pattern

    /*
  // generate crc polonomial, using precomputed poly should be faster
  // terms of polynomial defining this crc (except x^32):
  static const Byte p[] = {0,1,2,4,5,7,8,10,11,12,16,22,23,26};

  // make exclusive-or pattern from polynomial (0xedb88320L)
  poly = 0L;
  for (n = 0; n < sizeof(p)/sizeof(Byte); n++)
    poly |= 1L << (31 - p[n]);
*/
    int n;
    for (n = 0; n < 256; n++)
    {
        c = (UINT32)n;

        int k;
        for (k = 0; k < 8; k++)
            c = c & 1 ? poly ^ (c >> 1) : c >> 1;

        crcTab[n] = c;
    }
}

DWORD UpdateCrc32(const void* buffer, DWORD count, DWORD crcVal)
{
    CALL_STACK_MESSAGE_NONE

    if (buffer == NULL)
        return 0;

    if (!Crc32TabInitialized)
    {
        MakeCrc32Table(Crc32Tab);
        Crc32TabInitialized = TRUE;
    }

    BYTE* p = (BYTE*)buffer;
    DWORD c = crcVal ^ 0xFFFFFFFF;

    if (count)
        do
        {
            c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
        } while (--count);

    // Honza: meril jsem nasledujici optimalizace a nemaji zadny vyznam;
    // jedina sance by bylo prepsani do ASM a cteni pameti po DWORDech,
    // from which individual bytes could then be picked out;
    // pri soucasnem nastaveni release verze neni prekladac schopen tuto
    // optimalizaci provest za nas.
    /*
  int remain = count % 8;
  count -= remain;
  while (remain)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    remain--;
  }
  while (count)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    count -= 8;
  }
*/
    /*
  int remain = count % 4;
  count -= remain;
  while (remain > 0)
  {
    c = Crc32Tab[((int)c ^ (*p++)) & 0xff] ^ (c >> 8);
    remain--;
  }


  DWORD *pdw = (DWORD*)p;
  DWORD dw;
  while (count > 0)
  {
    dw = *pdw++;
    c = Crc32Tab[((int)c ^ ((BYTE)(dw))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 8))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 16))) & 0xFF] ^ (c >> 8);
    c = Crc32Tab[((int)c ^ ((BYTE)(dw >> 24))) & 0xFF] ^ (c >> 8);
    count -= 4;
  }
*/
    return c ^ 0xFFFFFFFF; /* (instead of ~c for 64-bit machines) */
}

BOOL IsRemoteSession(void)
{
    return GetSystemMetrics(SM_REMOTESESSION);
}

// ****************************************************************************

BOOL SalamanderIsNotBusy(DWORD* lastIdleTime)
{
    // k SalamanderBusy a k LastSalamanderIdleTime se chodi bez kritickych sekci, nevadi,
    // protoze jsou to DWORDy a tudiz nemuzou byt "rozpracovane" pri switchnuti kontextu
    // (always contains old or new value, nothing else can happen)
    if (lastIdleTime != NULL)
        *lastIdleTime = LastSalamanderIdleTime;
    if (!SalamanderBusy)
        return TRUE;
    DWORD oldLastIdleTime = LastSalamanderIdleTime;
    if (GetTickCount() - oldLastIdleTime <= 100)                                   // if SalamanderBusy hasn't been set for too long (e.g., modal dialog is open)
        Sleep(100);                                                                // wait to see if SalamanderBusy changes
    return !SalamanderBusy || (int)(LastSalamanderIdleTime - oldLastIdleTime) > 0; // not "busy" or at least oscillating
}

BOOL InitPreloadedStrings()
{
    DirColumnStr = DupStr(LoadStr(IDS_DIRCOLUMN));
    // Preloaded resource strings are CRT-terminated allocations.
    DirColumnStrLen = (int)strlen(DirColumnStr);

    ColExtStr = DupStr(LoadStr(IDS_COLUMN_NAME_EXT));
    ColExtStrLen = (int)strlen(ColExtStr);

    UpDirTypeName = DupStr(LoadStr(IDS_UPDIRTYPENAME));
    UpDirTypeNameLen = (int)strlen(UpDirTypeName);

    CommonFileTypeName = DupStr(LoadStr(IDS_COMMONFILETYPE));
    CommonFileTypeNameLen = (int)strlen(CommonFileTypeName);
    CommonFileTypeName2 = DupStr(LoadStr(IDS_COMMONFILETYPE2));

    ProgDlgHoursStr = DupStr(LoadStr(IDS_PROGDLGHOURS));
    ProgDlgMinutesStr = DupStr(LoadStr(IDS_PROGDLGMINUTES));
    ProgDlgSecsStr = DupStr(LoadStr(IDS_PROGDLGSECS));

    return TRUE;
}

// ****************************************************************************

void InitLocales()
{
    int i;
    for (i = 0; i < 256; i++)
    {
        IsNotAlphaNorNum[i] = !IsCharAlphaNumeric((char)i);
        IsAlpha[i] = IsCharAlpha((char)i);
    }

    WCHAR decimalSeparatorW[5];
    int decimalSeparatorUtf8Len = 0;
    int decimalSeparatorWLen = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, decimalSeparatorW, _countof(decimalSeparatorW));
    if (decimalSeparatorWLen > 0 && decimalSeparatorWLen <= (int)_countof(decimalSeparatorW))
        decimalSeparatorUtf8Len = ConvertWideToUtf8(decimalSeparatorW, -1, DecimalSeparator, _countof(DecimalSeparator));
    if (decimalSeparatorUtf8Len == 0 || decimalSeparatorUtf8Len > (int)_countof(DecimalSeparator))
    {
        strcpy(DecimalSeparator, ".");
        DecimalSeparatorLen = 1;
    }
    else
    {
        DecimalSeparatorLen = decimalSeparatorUtf8Len - 1;
        DecimalSeparator[DecimalSeparatorLen] = 0; // keep the terminator in place
    }

    WCHAR thousandsSeparatorW[5];
    int thousandsSeparatorUtf8Len = 0;
    int thousandsSeparatorWLen = GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_STHOUSAND, thousandsSeparatorW, _countof(thousandsSeparatorW));
    if (thousandsSeparatorWLen > 0 && thousandsSeparatorWLen <= (int)_countof(thousandsSeparatorW))
        thousandsSeparatorUtf8Len = ConvertWideToUtf8(thousandsSeparatorW, -1, ThousandsSeparator, _countof(ThousandsSeparator));
    if (thousandsSeparatorUtf8Len == 0 || thousandsSeparatorUtf8Len > (int)_countof(ThousandsSeparator))
    {
        strcpy(ThousandsSeparator, " ");
        ThousandsSeparatorLen = 1;
    }
    else
    {
        ThousandsSeparatorLen = thousandsSeparatorUtf8Len - 1;
        ThousandsSeparator[ThousandsSeparatorLen] = 0; // keep the terminator in place
    }
}

// ****************************************************************************

HICON GetFileOrPathIconAux(const char* path, BOOL large, BOOL isDir)
{
    __try
    {
        SHFILEINFO shi;
        if (!GetFileIcon(path, FALSE, &shi.hIcon, large ? ICONSIZE_32 : ICONSIZE_16, TRUE, isDir))
            shi.hIcon = NULL;
        //Presli jsme na vlastni implementaci (mensi pametova narocnost, fungujici XOR ikonky)
        //shi.hIcon = NULL;
        //SHGetFileInfo(path, 0, &shi, sizeof(shi),
        //              SHGFI_ICON | SHGFI_SHELLICONSIZE | (large ? 0 : SHGFI_SMALLICON));
        // pridame handle na 'shi.hIcon' do HANDLES
        if (shi.hIcon != NULL)
            HANDLES_ADD(__htIcon, __hoLoadImage, shi.hIcon);
        return shi.hIcon;
    }
    __except (CCallStack::HandleException(GetExceptionInformation(), 13))
    {
        FGIExceptionHasOccured++;
    }
    return NULL;
}

HICON GetDriveIcon(const char* root, UINT type, BOOL accessible, BOOL large)
{
    CALL_STACK_MESSAGE5("GetDriveIcon(%s, %u, %d, %d)", root, type, accessible, large);
    int id;
    switch (type)
    {
    case DRIVE_REMOVABLE: // ikonky 3.5, 5.25
    {
        HICON i = GetFileOrPathIconAux(root, large, TRUE);
        if (i != NULL)
            return i;
        id = 28; // 3 1/2 " mechanika
        break;
    }

    case DRIVE_REMOTE:
        id = (accessible ? 33 : 31);
        break;
    case DRIVE_CDROM:
        id = 30;
        break;
    case DRIVE_RAMDISK:
        id = 34;
        break;

    default:
    {
        id = 32;
        if (type == DRIVE_FIXED && root[1] == ':')
        {
            char win[MAX_PATH];
            if (GetWindowsDirectory(win, MAX_PATH) && win[1] == ':' && win[0] == root[0])
                id = 36;
        }
        break;
    }
    }
    int iconSize = IconSizes[large ? ICONSIZE_32 : ICONSIZE_16];
    return SalLoadIcon(ImageResDLL, id, iconSize);

    // JRYFIXME - investigate whether IconLRFlags can be removed? (W7+)

    /* JRYFIXME - grepnout plosne zdrojaky na LoadImage / IMAGE_ICON
  return (HICON)HANDLES(LoadImage(ImageResDLL, MAKEINTRESOURCE(id), IMAGE_ICON,
                                  large ? ICON32_CX : ICON16_CX,
                                  large ? ICON32_CX : ICON16_CX,
                                  IconLRFlags));
  */
}

HICON SalLoadIcon(HINSTANCE hDLL, int id, int iconSize)
{
    //return (HICON)HANDLES(LoadImage(hDLL, MAKEINTRESOURCE(id), IMAGE_ICON, iconSize, iconSize, IconLRFlags));
    HICON hIcon;
    LoadIconWithScaleDown(hDLL, MAKEINTRESOURCEW(id), iconSize, iconSize, &hIcon);
    HANDLES_ADD(__htIcon, __hoLoadImage, hIcon);
    return hIcon;
}

// ****************************************************************************

char* BuildName(char* path, char* name, char* dosName, BOOL* skip, BOOL* skipAll, const char* sourcePath)
{
    if (skip != NULL)
        *skip = FALSE;
    int l1 = (int)strlen(path); // je vzdy na stacku ...
    int l2, len = l1;
    if (name != NULL)
    {
        l2 = (int)strlen(name);
        len += l2;
        if (path[l1 - 1] != '\\')
            len++;
        if (len >= MAX_PATH && dosName != NULL)
        {
            int l3 = (int)strlen(dosName);
            if (len - l2 + l3 < MAX_PATH)
            {
                len = len - l2 + l3;
                name = dosName;
                l2 = l3;
            }
        }
    }
    if (len >= MAX_PATH)
    {
        char text[2 * MAX_PATH + 100];
        _snprintf_s(text, _TRUNCATE, LoadStr(IDS_NAMEISTOOLONG), name, path);

        if (skip != NULL)
        {
            if (skipAll == NULL || !*skipAll)
            {
                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = MainWindow->HWindow;
                params.Flags = MSGBOXEX_YESNOOKCANCEL | MB_ICONEXCLAMATION | MSGBOXEX_DEFBUTTON3 | MSGBOXEX_SILENT;
                params.Caption = LoadStr(IDS_ERRORTITLE);
                params.Text = text;
                char aliasBtnNames[200];
                /* slouzi pro skript export_mnu.py, ktery generuje salmenu.mnu pro Translator
   nechame pro tlacitka msgboxu resit kolize hotkeys tim, ze simulujeme, ze jde o menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_MSGBOXBTN_SKIP
  {MNTT_IT, IDS_MSGBOXBTN_SKIPALL
  {MNTT_IT, IDS_MSGBOXBTN_FOCUS
  {MNTT_PE, 0
};
*/
                sprintf(aliasBtnNames, "%d\t%s\t%d\t%s\t%d\t%s",
                        DIALOG_YES, LoadStr(IDS_MSGBOXBTN_SKIP),
                        DIALOG_NO, LoadStr(IDS_MSGBOXBTN_SKIPALL),
                        DIALOG_OK, LoadStr(IDS_MSGBOXBTN_FOCUS));
                params.AliasBtnNames = aliasBtnNames;
                int msgRes = SalMessageBoxEx(&params);
                if (msgRes == DIALOG_YES /* Skip */ || msgRes == DIALOG_NO /* Skip All */)
                    *skip = TRUE;
                if (msgRes == DIALOG_NO /* Skip All */ && skipAll != NULL)
                    *skipAll = TRUE;
                if (msgRes == DIALOG_OK /* Focus */)
                    MainWindow->PostFocusNameInPanel(PANEL_SOURCE, sourcePath, name);
            }
            else
                *skip = TRUE;
        }
        else
        {
            SalMessageBox(MainWindow->HWindow, text, LoadStr(IDS_ERRORTITLE),
                          MB_OK | MB_ICONEXCLAMATION);
        }
        return NULL;
    }
    char* txt = (char*)malloc(len + 1);
    if (txt == NULL)
    {
        TRACE_E(LOW_MEMORY);
        return txt;
    }
    if (name != NULL)
    {
        memmove(txt, path, l1);
        if (path[l1 - 1] != '\\')
            txt[l1++] = '\\';
        memmove(txt + l1, name, l2 + 1);
    }
    else
        memmove(txt, path, l1 + 1);
    return txt;
}

// ****************************************************************************

BOOL HasTheSameRootPath(const char* path1, const char* path2)
{
    if (LowerCase[path1[0]] == LowerCase[path2[0]] && path1[1] == path2[1])
    {
        if (path1[1] == ':')
            return TRUE; // stejny root normal ("c:\path") cesty
        else
        {
            if (path1[0] == '\\' && path1[1] == '\\') // oboji UNC
            {
                const char* s1 = path1 + 2;
                const char* s2 = path2 + 2;
                while (*s1 != 0 && *s1 != '\\')
                {
                    if (LowerCase[*s1] == LowerCase[*s2])
                    {
                        s1++;
                        s2++;
                    }
                    else
                        break; // ruzne masiny
                }
                if (*s1 != 0 && *s1++ == *s2++) // preskok '\\'
                {
                    while (*s1 != 0 && *s1 != '\\')
                    {
                        if (LowerCase[*s1] == LowerCase[*s2])
                        {
                            s1++;
                            s2++;
                        }
                        else
                            break; // ruzne disky
                    }
                    return (*s1 == 0 && (*s2 == 0 || *s2 == '\\')) || *s1 == *s2 ||
                           (*s2 == 0 && (*s1 == 0 || *s1 == '\\'));
                }
            }
        }
    }
    return FALSE;
}

// ****************************************************************************

BOOL HasTheSameRootPathAndVolume(const char* p1, const char* p2)
{
    CALL_STACK_MESSAGE3("HasTheSameRootPathAndVolume(%s, %s)", p1, p2);

    BOOL ret = FALSE;
    if (HasTheSameRootPath(p1, p2))
    {
        ret = TRUE;
        char root[MAX_PATH];
        char ourPath[MAX_PATH];
        char p1Volume[100] = "1";
        char p2Volume[100] = "2";
        char resPath[MAX_PATH];
        // Volume comparisons require the complete input path.
        if (FAILED(StringCchCopyA(resPath, _countof(resPath), p1)))
            return FALSE;
        ResolveSubsts(resPath);
        GetRootPath(root, resPath);
        if (!IsUNCPath(root) && GetDriveType(root) == DRIVE_FIXED) // reparse points only make sense on fixed disks
        {
            // if it's not a root path, try to traverse through reparse points
            BOOL cutPathIsPossible = TRUE;
            char p1NetPath[MAX_PATH];
            p1NetPath[0] = 0;
            ResolveLocalPathWithReparsePoints(ourPath, p1, &cutPathIsPossible, NULL, NULL, NULL, NULL, p1NetPath);

            if (p1NetPath[0] == 0) // cannot get volume from network path, won't even try
            {
                while (!GetVolumeNameForVolumeMountPoint(ourPath, p1Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectory(ourPath))
                    {
                        strcpy(p1Volume, "fail"); // even root didn't return success, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure of both paths we return MATCH as it's more probable)
                        break;
                    }
                    SalPathAddBackslash(ourPath, MAX_PATH);
                }
            }

            // if under W2K and it's not a root path, try to traverse through reparse points
            cutPathIsPossible = TRUE;
            char p2NetPath[MAX_PATH];
            p2NetPath[0] = 0;
            ResolveLocalPathWithReparsePoints(ourPath, p2, &cutPathIsPossible, NULL, NULL, NULL, NULL, p2NetPath);

            if ((p1NetPath[0] == 0) != (p2NetPath[0] == 0) || // if only one of the paths is a network path or
                p1NetPath[0] != 0 && !HasTheSameRootPath(p1NetPath, p2NetPath))
                ret = FALSE; // don't have same root, report different volumes (can't verify volumes on network paths)

            if (p2NetPath[0] == 0 && ret) // cannot get volume from network path, won't even try + if already decided, won't try either
            {
                while (!GetVolumeNameForVolumeMountPoint(ourPath, p2Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectory(ourPath))
                    {
                        strcpy(p2Volume, "fail"); // even root didn't return success, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure of both paths we return MATCH as it's more probable)
                        break;
                    }
                    SalPathAddBackslash(ourPath, MAX_PATH);
                }
                if (strcmp(p1Volume, p2Volume) != 0)
                    ret = FALSE;
            }
        }
    }
    return ret;
}

// ****************************************************************************

BOOL PathsAreOnTheSameVolume(const char* path1, const char* path2, BOOL* resIsOnlyEstimation)
{
    char root1[MAX_PATH];
    char root2[MAX_PATH];
    char ourPath[MAX_PATH];
    char path1NetPath[MAX_PATH];
    char path2NetPath[MAX_PATH];
    // Volume comparison must not resolve a truncated path identity.
    if (FAILED(StringCchCopyA(ourPath, _countof(ourPath), path1)))
        return FALSE;
    ResolveSubsts(ourPath);
    GetRootPath(root1, ourPath);
    if (FAILED(StringCchCopyA(ourPath, _countof(ourPath), path2)))
        return FALSE;
    ResolveSubsts(ourPath);
    GetRootPath(root2, ourPath);
    BOOL ret = TRUE;
    BOOL trySimpleTest = TRUE;
    if (resIsOnlyEstimation != NULL)
        *resIsOnlyEstimation = TRUE;
    if (!IsUNCPath(path1) && !IsUNCPath(path2)) // no point checking volumes on UNC paths
    {
        char p1Volume[100] = "1";
        char p2Volume[100] = "2";
        UINT drvType1 = GetDriveType(root1);
        UINT drvType2 = GetDriveType(root2);
        if (drvType1 != DRIVE_REMOTE && drvType2 != DRIVE_REMOTE) // krome site je sance zjistit "volume name"
        {
            BOOL cutPathIsPossible = TRUE;
            path1NetPath[0] = 0;         // network path pointed to by current (last) local symlink in the path
            if (drvType1 == DRIVE_FIXED) // reparse points only make sense on fixed disks
            {
                // if under W2K and it's not a root path, try to traverse through reparse points
                ResolveLocalPathWithReparsePoints(ourPath, path1, &cutPathIsPossible, NULL, NULL, NULL, NULL, path1NetPath);
            }
            else if (FAILED(StringCchCopyA(ourPath, _countof(ourPath), root1)))
                return FALSE;
            int numOfGetVolNamesFailed = 0;
            if (path1NetPath[0] == 0) // cannot get "volume name" from network path, won't even try
            {
                while (!GetVolumeNameForVolumeMountPoint(ourPath, p1Volume, 100))
                {
                    if (!cutPathIsPossible || !CutDirectory(ourPath))
                    { // even root didn't return success, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure of both paths with same roots we return MATCH as it's more probable)
                        numOfGetVolNamesFailed++;
                        break;
                    }
                    SalPathAddBackslash(ourPath, MAX_PATH);
                }
            }

            cutPathIsPossible = TRUE;
            path2NetPath[0] = 0;         // network path pointed to by current (last) local symlink in the path
            if (drvType2 == DRIVE_FIXED) // reparse points only make sense on fixed disks
            {
                // if under W2K and it's not a root path, try to traverse through reparse points
                ResolveLocalPathWithReparsePoints(ourPath, path2, &cutPathIsPossible, NULL, NULL, NULL, NULL, path2NetPath);
            }
            else if (FAILED(StringCchCopyA(ourPath, _countof(ourPath), root2)))
                return FALSE;
            if (path2NetPath[0] == 0) // cannot get "volume name" from network path, won't even try
            {
                if (path1NetPath[0] == 0)
                {
                    while (!GetVolumeNameForVolumeMountPoint(ourPath, p2Volume, 100))
                    {
                        if (!cutPathIsPossible || !CutDirectory(ourPath))
                        { // even root didn't return success, unexpected (unfortunately happens on substed drives under W2K - debugged at Bachaalany - on failure of both paths with same roots we return MATCH as it's more probable)
                            numOfGetVolNamesFailed++;
                            break;
                        }
                        SalPathAddBackslash(ourPath, MAX_PATH);
                    }
                    if (numOfGetVolNamesFailed != 2)
                    {
                        if (numOfGetVolNamesFailed == 0 && resIsOnlyEstimation != NULL)
                            *resIsOnlyEstimation = FALSE; // jediny pripad, kdy jsme si jisty vysledkem je, kdyz se podarilo ziskat "volume name" z obou cest (zaroven tak nemohly byt sitove)
                        if (numOfGetVolNamesFailed == 1 || strcmp(p1Volume, p2Volume) != 0)
                            ret = FALSE; // only one "volume name" was obtained, so these are not the same volumes (and if they are, we cannot detect it - perhaps if this failed because of SUBST, resolving the target path from SUBST could handle it)
                        trySimpleTest = FALSE;
                    }
                }
                else // only one path is network, these are not the same volumes (and if they are, we cannot detect it)
                {
                    ret = FALSE;
                    trySimpleTest = FALSE;
                }
            }
            else
            {
                if (path1NetPath[0] != 0) // srovname rooty sitovych cest
                {
                    GetRootPath(root1, path1NetPath);
                    GetRootPath(root2, path2NetPath);
                }
                else // only one path is network, these are not the same volumes (and if they are, we cannot detect it)
                {
                    ret = FALSE;
                    trySimpleTest = FALSE;
                }
            }
        }
    }

    if (trySimpleTest) // only try whether root paths match (network paths + everything on NT)
    {
        ret = _stricmp(root1, root2) == 0;

        if (resIsOnlyEstimation != NULL)
        {
            // Estimation is valid only when both original paths fit completely.
            if (SUCCEEDED(StringCchCopyA(path1NetPath, _countof(path1NetPath), path1)) &&
                SUCCEEDED(StringCchCopyA(path2NetPath, _countof(path2NetPath), path2)) &&
                ResolveSubsts(path1NetPath) && ResolveSubsts(path2NetPath))
            {
                if (IsTheSamePath(path1NetPath, path2NetPath))
                    *resIsOnlyEstimation = FALSE; // stejne cesty = urcite i stejne svazky
            }
        }
    }
    return ret;
}

// ****************************************************************************

BOOL IsTheSamePath(const char* path1, const char* path2)
{
    if (*path1 == '\\')
        path1++;
    if (*path2 == '\\')
        path2++;
    while (*path1 != 0 && LowerCase[*path1] == LowerCase[*path2])
    {
        path1++;
        path2++;
    }
    if (*path1 == '\\')
        path1++;
    if (*path2 == '\\')
        path2++;
    return *path1 == 0 && *path2 == 0;
}

BOOL IsTheSamePathW(const WCHAR* path1, const WCHAR* path2)
{
    if (path1 == NULL || path2 == NULL)
        return path1 == path2;
    if (*path1 == L'\\' || *path1 == L'/')
        path1++;
    if (*path2 == L'\\' || *path2 == L'/')
        path2++;
    while (*path1 != 0 && towlower(*path1) == towlower(*path2))
    {
        path1++;
        path2++;
    }
    if (*path1 == L'\\' || *path1 == L'/')
        path1++;
    if (*path2 == L'\\' || *path2 == L'/')
        path2++;
    return *path1 == 0 && *path2 == 0;
}

// ****************************************************************************

int CommonPrefixLength(const char* path1, const char* path2)
{
    const char* lastBackslash = path1;
    int backslashCount = 0;
    int sameCount = 0;
    const char* s1 = path1;
    const char* s2 = path2;
    while (*s1 != 0 && *s2 != 0 && LowerCase[*s1] == LowerCase[*s2])
    {
        if (*s1 == '\\')
        {
            lastBackslash = s1;
            backslashCount++;
        }
        s1++;
        s2++;
    }

    if (s1 - path1 < 3)
        return 0;

    if (*s1 == 0 && *s2 == '\\' || *s1 == '\\' && *s2 == 0 ||
        *s1 == 0 && *s2 == 0 && *(s1 - 1) != '\\')
    {
        lastBackslash = s1; // tento terminator nebude v lastBackslash
        backslashCount++;
    }

    if (path1[1] == ':')
    {
        // classic path
        if (path1[2] != '\\')
            return 0;

        // osetrim specialni pripad: u root cesty musime vratit delku i s posledni zpetnym lomitkem
        if (lastBackslash - path1 < 3)
            return 3;

        return (int)(lastBackslash - path1);
    }
    else
    {
        // UNC path
        if (path1[0] != '\\' || path1[1] != '\\')
            return 0;
        if (backslashCount < 4) // path must have the form "\\machine\share"
            return 0;

        return (int)(lastBackslash - path1);
    }
}

// ****************************************************************************

int CommonPrefixLengthW(const WCHAR* path1, const WCHAR* path2)
{
    if (path1 == NULL || path2 == NULL)
        return 0;

    const WCHAR* lastBackslash = path1;
    int backslashCount = 0;
    const WCHAR* s1 = path1;
    const WCHAR* s2 = path2;
    while (*s1 != 0 && *s2 != 0 && towlower(*s1) == towlower(*s2))
    {
        if (*s1 == L'\\')
        {
            lastBackslash = s1;
            backslashCount++;
        }
        s1++;
        s2++;
    }

    if (s1 - path1 < 3)
        return 0;

    if ((*s1 == 0 && *s2 == L'\\') || (*s1 == L'\\' && *s2 == 0) ||
        (*s1 == 0 && *s2 == 0 && *(s1 - 1) != L'\\'))
    {
        lastBackslash = s1;
        backslashCount++;
    }

    if (path1[1] == L':')
    {
        if (path1[2] != L'\\')
            return 0;

        if (lastBackslash - path1 < 3)
            return 3;

        return (int)(lastBackslash - path1);
    }
    else
    {
        if (path1[0] != L'\\' || path1[1] != L'\\')
            return 0;
        if (backslashCount < 4)
            return 0;

        return (int)(lastBackslash - path1);
    }
}

// ****************************************************************************

BOOL SalPathIsPrefix(const char* prefix, const char* path)
{
    int commonLen = CommonPrefixLength(prefix, path);
    if (commonLen == 0)
        return FALSE;

    int prefixLen = (int)strlen(prefix);
    if (prefixLen < 3)
        return FALSE;

    // CommonPrefixLength returned the length without the last backslash (unless it was a root path).
    // If our prefix has a trailing backslash, drop it.
    if (prefixLen > 3 && prefix[prefixLen - 1] == '\\')
        prefixLen--;

    return (commonLen == prefixLen);
}

BOOL SalPathIsPrefixW(const WCHAR* prefix, const WCHAR* path)
{
    if (prefix == NULL || path == NULL)
        return FALSE;

    int commonLen = CommonPrefixLengthW(prefix, path);
    if (commonLen == 0)
        return FALSE;

    int prefixLen = (int)wcslen(prefix);
    if (prefixLen < 3)
        return FALSE;

    if (prefixLen > 3 && prefix[prefixLen - 1] == L'\\')
        prefixLen--;

    return (commonLen == prefixLen);
}

// ****************************************************************************

BOOL IsDirError(DWORD err)
{
    return err == ERROR_NETWORK_ACCESS_DENIED ||
           err == ERROR_ACCESS_DENIED ||
           err == ERROR_SECTOR_NOT_FOUND ||
           err == ERROR_SHARING_VIOLATION ||
           err == ERROR_BAD_PATHNAME ||
           err == ERROR_FILE_NOT_FOUND ||
           err == ERROR_PATH_NOT_FOUND ||
           err == ERROR_INVALID_NAME ||   // if there is a diacritic in the path on English Windows, this error is reported instead of ERROR_PATH_NOT_FOUND
           err == ERROR_INVALID_FUNCTION; // hlasilo jednomu chlapikovi na WinXP na sitovem disku Y: v okamziku, kdy Salam pristupoval na cestu, ktera jiz neexistovala (nedoslo tak ke zkraceni a chlapik byl dobre v riti ;-) Shift+F7 na Y:\ to vyresila)
}

// ****************************************************************************

BOOL CutDirectory(char* path, char** cutDir)
{
    CALL_STACK_MESSAGE2("CutDirectory(%s,)", path);
    int l = (int)strlen(path);
    char* lastBackslash = path + l - 1;
    while (--lastBackslash >= path && *lastBackslash != '\\')
        ;
    char* nextBackslash = lastBackslash;
    while (--nextBackslash >= path && *nextBackslash != '\\')
        ;
    if (lastBackslash < path)
    {
        if (cutDir != NULL)
            *cutDir = path + l;
        return FALSE; // "somedir" or "c:\"
    }
    if (nextBackslash < path) // "c:\somedir" or "c:\somedir\"
    {
        if (cutDir != NULL)
        {
            if (*(path + l - 1) == '\\')
                *(path + --l) = 0; // remove trailing '\\'
            memmove(lastBackslash + 2, lastBackslash + 1, l - (lastBackslash - path));
            *cutDir = lastBackslash + 2; // "somedir" or "seconddir"
        }
        *(lastBackslash + 1) = 0; // "c:\"
    }
    else // "c:\firstdir\seconddir" or "c:\firstdir\seconddir\"
    {    // UNC: "\\server\share\path"
        if (path[0] == '\\' && path[1] == '\\' && nextBackslash <= path + 2)
        { // "\\server\share" - neda se zkratit
            if (cutDir != NULL)
                *cutDir = path + l;
            return FALSE;
        }
        *lastBackslash = 0;
        if (cutDir != NULL) // trim trailing '\'
        {
            if (*(path + l - 1) == '\\')
                *(path + l - 1) = 0;
            *cutDir = lastBackslash + 1;
        }
    }
    return TRUE;
}

BOOL CutDirectoryW(WCHAR* path, WCHAR** cutDir)
{
    if (path == NULL)
        return FALSE;
    int l = (int)wcslen(path);
    WCHAR* lastBackslash = path + l - 1;
    while (--lastBackslash >= path && *lastBackslash != L'\\')
        ;
    WCHAR* nextBackslash = lastBackslash;
    while (--nextBackslash >= path && *nextBackslash != L'\\')
        ;
    if (lastBackslash < path)
    {
        if (cutDir != NULL)
            *cutDir = path + l;
        return FALSE;
    }
    if (nextBackslash < path)
    {
        if (cutDir != NULL)
        {
            if (*(path + l - 1) == L'\\')
                *(path + --l) = 0;
            memmove(lastBackslash + 2, lastBackslash + 1, (l - (lastBackslash - path)) * sizeof(WCHAR));
            *cutDir = lastBackslash + 2;
        }
        *(lastBackslash + 1) = 0;
    }
    else
    {
        if (path[0] == L'\\' && path[1] == L'\\' && nextBackslash <= path + 2)
        {
            if (cutDir != NULL)
                *cutDir = path + l;
            return FALSE;
        }
        *lastBackslash = 0;
        if (cutDir != NULL)
        {
            if (*(path + l - 1) == L'\\')
                *(path + l - 1) = 0;
            *cutDir = lastBackslash + 1;
        }
    }
    return TRUE;
}

// ****************************************************************************

int GetRootPath(char* root, int rootBufSize, const char* path)
{                                           // CAUTION: atypical use from GetShellFolder(): for "\\\\" returns "\\\\\\", for "\\\\server" returns "\\\\server\\"
    if (root == NULL || path == NULL || rootBufSize <= 0)
        return 0;

    root[0] = 0;

    if (path[0] == '\\' && path[1] == '\\') // UNC
    {
        const char* s = path + 2;
        while (*s != 0 && *s != '\\')
            s++;
        if (*s != 0)
            s++; // '\\'
        while (*s != 0 && *s != '\\')
            s++;
        int len = (int)(s - path);
        if (len > rootBufSize - 2)
            len = rootBufSize - 2;
        if (len < 0)
            return 0;
        memcpy(root, path, len);
        root[len] = '\\';
        root[len + 1] = 0;
        return len + 1;
    }
    else
    {
        if (rootBufSize < 4)
            return 0;
        root[0] = path[0];
        root[1] = ':';
        root[2] = '\\';
        root[3] = 0;
        return 3;
    }
}

int GetRootPath(char* root, const char* path)
{
    return GetRootPath(root, MAX_PATH, path);
}

int GetRootPathW(WCHAR* root, int rootBufSizeInChars, const WCHAR* path)
{
    if (root == NULL || path == NULL || rootBufSizeInChars <= 0)
        return 0;

    root[0] = 0;

    if (path[0] == L'\\' && path[1] == L'\\') // UNC
    {
        const WCHAR* s = path + 2;
        while (*s != 0 && *s != L'\\' && *s != L'/')
            s++;
        if (*s != 0)
            s++;
        while (*s != 0 && *s != L'\\' && *s != L'/')
            s++;
        int len = (int)(s - path);
        if (len > rootBufSizeInChars - 2)
            len = rootBufSizeInChars - 2;
        if (len < 0)
            return 0;
        memcpy(root, path, len * sizeof(WCHAR));
        root[len] = L'\\';
        root[len + 1] = 0;
        return len + 1;
    }
    else
    {
        if (rootBufSizeInChars < 4)
            return 0;
        root[0] = path[0];
        root[1] = L':';
        root[2] = L'\\';
        root[3] = 0;
        return 3;
    }
}

int GetRootPathW(WCHAR* root, const WCHAR* path)
{
    return GetRootPathW(root, MAX_PATH, path);
}


// ****************************************************************************

char* NumberToStr(char* buffer, const CQuadWord& number)
{
    _ui64toa(number.Value, buffer, 10);
    int l = (int)strlen(buffer);
    char* s = buffer + l;
    int c = 0;
    while (--s > buffer)
    {
        if ((++c % 3) == 0)
        {
            memmove(s + ThousandsSeparatorLen, s, (c / 3) * 3 + (c / 3 - 1) * ThousandsSeparatorLen + 1);
            memcpy(s, ThousandsSeparator, ThousandsSeparatorLen);
        }
    }
    return buffer;
}

int NumberToStr2(char* buffer, const CQuadWord& number)
{
    _ui64toa(number.Value, buffer, 10);
    int l = (int)strlen(buffer);
    char* s = buffer + l;
    int c = 0;
    while (--s > buffer)
    {
        if ((++c % 3) == 0)
        {
            memmove(s + ThousandsSeparatorLen, s, (c / 3) * 3 + (c / 3 - 1) * ThousandsSeparatorLen + 1);
            memcpy(s, ThousandsSeparator, ThousandsSeparatorLen);
            l += ThousandsSeparatorLen;
        }
    }
    return l;
}

// ****************************************************************************

BOOL PointToLocalDecimalSeparator(char* buffer, int bufferSize)
{
    char* s = strrchr(buffer, '.');
    if (s != NULL)
    {
        int len = (int)strlen(buffer);
        if (len - 1 + DecimalSeparatorLen > bufferSize - 1)
        {
            TRACE_E("PointToLocalDecimalSeparator() small buffer!");
            return FALSE;
        }
        memmove(s + DecimalSeparatorLen, s + 1, len - (s - buffer));
        memcpy(s, DecimalSeparator, DecimalSeparatorLen);
    }
    return TRUE;
}

// ****************************************************************************
//
// GetCmdLine - ziskani parametru z prikazove radky
//
// buf + size - buffer pro parametry
// argv - pole ukazatelu, ktere se naplni parametry
// argCount - on input it is the number of elements in argv, on output it contains the number of parameters
// cmdLine - parametry prikazove radky (bez jmena .exe souboru - z WinMain)

BOOL GetCmdLine(char* buf, int size, char* argv[], int& argCount, char* cmdLine)
{
    int space = argCount;
    argCount = 0;
    char* c = buf;
    char* end = buf + size;

    char* s = cmdLine;
    char term;
    while (*s != 0)
    {
        if (*s == '"') // pocatecni '"'
        {
            if (*++s == 0)
                break;
            term = '"';
        }
        else
            term = ' ';

        if (argCount < space && c < end)
            argv[argCount++] = c;
        else
            return c < end; // error only if buffer is small

        while (1)
        {
            if (*s == term || *s == 0)
            {
                if (*s == 0 || term != '"' || *++s != '"') // neni-li to nahrada "" -> "
                {
                    if (*s != 0)
                        s++;
                    while (*s != 0 && *s == ' ')
                        s++;
                    if (c < end)
                    {
                        *c++ = 0;
                        break;
                    }
                    else
                        return FALSE;
                }
            }
            if (c < end)
                *c++ = *s++;
            else
                return FALSE;
        }
    }
    return TRUE;
}

// ****************************************************************************
//
// GetComCtlVersion
//

typedef struct _DllVersionInfo
{
    DWORD cbSize;
    DWORD dwMajorVersion; // Major version
    DWORD dwMinorVersion; // Minor version
    DWORD dwBuildNumber;  // Build number
    DWORD dwPlatformID;   // DLLVER_PLATFORM_*
} DLLVERSIONINFO;

typedef HRESULT(CALLBACK* DLLGETVERSIONPROC)(DLLVERSIONINFO*);

HRESULT GetComCtlVersion(LPDWORD pdwMajor, LPDWORD pdwMinor)
{
    HINSTANCE hComCtl;
    //load the DLL
    hComCtl = HANDLES(LoadLibrary(TEXT("comctl32.dll")));
    if (hComCtl)
    {
        HRESULT hr = S_OK;
        DLLGETVERSIONPROC pDllGetVersion;
        /*
     You must get this function explicitly because earlier versions of the DLL
     don't implement this function. That makes the lack of implementation of the
     function a version marker in itself.
    */
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hComCtl, TEXT("DllGetVersion")); // nema header
        if (pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);
            hr = (*pDllGetVersion)(&dvi);
            if (SUCCEEDED(hr))
            {
                *pdwMajor = dvi.dwMajorVersion;
                *pdwMinor = dvi.dwMinorVersion;
            }
            else
            {
                hr = E_FAIL;
            }
        }
        else
        {
            /*
      If GetProcAddress failed, then the DLL is a version previous to the one
      shipped with IE 3.x.
      */
            *pdwMajor = 4;
            *pdwMinor = 0;
        }
        HANDLES(FreeLibrary(hComCtl));
        return hr;
    }
    TRACE_E("LoadLibrary on comctl32.dll failed");
    return E_FAIL;
}

// ****************************************************************************

void InitDefaultDir()
{
    char dir[4] = " :\\";
    char d;
    for (d = 'A'; d <= 'Z'; d++)
    {
        dir[0] = d;
        strcpy(DefaultDir[d - 'A'], dir);
    }
}

// ****************************************************************************

BOOL PackErrorHandler(HWND parent, const WORD err, ...)
{
    va_list argList;
    char buff[1000];
    BOOL ret = FALSE;

    parent = parent == NULL ? (MainWindow != NULL ? MainWindow->HWindow : NULL) : parent;

    va_start(argList, err);
    FormatMessage(FORMAT_MESSAGE_FROM_STRING, LoadStr(err), 0, 0, buff, 1000, &argList);
    if (err < IDS_PACKQRY_PREFIX)
        SalMessageBox(parent, buff, LoadStr(IDS_PACKERR_TITLE), MB_OK | MB_ICONEXCLAMATION);
    else
        ret = SalMessageBox(parent, buff, LoadStr(IDS_PACKERR_TITLE), MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
    va_end(argList);
    return ret;
}


#ifdef USE_BETA_EXPIRATION_DATE

int ShowBetaExpDlg()
{
    CBetaExpiredDialog dlg(NULL);
    return (int)dlg.Execute();
}

#endif // USE_BETA_EXPIRATION_DATE

//
// ****************************************************************************

struct VS_VERSIONINFO_HEADER
{
    WORD wLength;
    WORD wValueLength;
    WORD wType;
};

BOOL GetModuleVersion(HINSTANCE hModule, WORD* major, WORD* minor)
{
    HRSRC hRes = FindResource(hModule, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    if (hRes == NULL)
        return FALSE;

    HGLOBAL hVer = LoadResource(hModule, hRes);
    if (hVer == NULL)
        return FALSE;

    DWORD resSize = SizeofResource(hModule, hRes);
    const BYTE* first = (BYTE*)LockResource(hVer);
    if (resSize == 0 || first == 0)
        return FALSE;

    const BYTE* iterator = first + sizeof(VS_VERSIONINFO_HEADER);

    DWORD signature = 0xFEEF04BD;

    while (memcmp(iterator, &signature, 4) != 0)
    {
        iterator++;
        if (iterator + 4 >= first + resSize)
            return FALSE;
    }

    VS_FIXEDFILEINFO* ffi = (VS_FIXEDFILEINFO*)iterator;

    *major = HIWORD(ffi->dwFileVersionMS);
    *minor = LOWORD(ffi->dwFileVersionMS);

    return TRUE;
}

//****************************************************************************
//
// CMessagesKeeper
//

CMessagesKeeper::CMessagesKeeper()
{
    Index = 0;
    Count = 0;
}

void CMessagesKeeper::Add(const MSG* msg)
{
    Messages[Index] = *msg;
    Index = (Index + 1) % MESSAGES_KEEPER_COUNT;
    if (Count < MESSAGES_KEEPER_COUNT)
        Count++;
}

void CMessagesKeeper::Print(char* buffer, int buffMax, int index)
{
    if (buffMax <= 0)
        return;
    if (index >= Count)
    {
        _snprintf_s(buffer, buffMax, _TRUNCATE, "(error)");
    }
    else
    {
        int i;
        if (Count == MESSAGES_KEEPER_COUNT)
            i = (Index + index) % MESSAGES_KEEPER_COUNT;
        else
            i = index;
        const MSG* msg = &Messages[i];
        // MSG::time remains the documented wrapping DWORD tick, so compare it with the 32-bit projection of the 64-bit exception time.
        _snprintf_s(buffer, buffMax, _TRUNCATE, "w=0x%p m=0x%X w=0x%IX l=0x%IX t=%u p=%d,%d",
                    msg->hwnd, msg->message, msg->wParam, msg->lParam,
                    msg->time - (DWORD)SalamanderExceptionTime, msg->pt.x, msg->pt.y);
    }
}

CMessagesKeeper MessagesKeeper;

typedef VOID(WINAPI* FDisableProcessWindowsGhosting)(VOID);

void TurnOFFWindowGhosting() // kdyz se "ghosting" nevypne, schovavaji se safe-wait-okenka po peti sekundach "not responding" stavu aplikace (kdyz aplikace nezpracovava zpravy)
{
    if (User32DLL != NULL)
    {
        FDisableProcessWindowsGhosting disableProcessWindowsGhosting = (FDisableProcessWindowsGhosting)GetProcAddress(User32DLL, "DisableProcessWindowsGhosting"); // Min: XP
        if (disableProcessWindowsGhosting != NULL)
            disableProcessWindowsGhosting();
    }
}

//
// ****************************************************************************

void UIDToString(GUID* uid, char* buff, int buffSize)
{
    wchar_t buffw[64] = {0};
    StringFromGUID2(*uid, buffw, 64);
    WideCharToMultiByte(CP_ACP, 0, buffw, -1, buff, buffSize, NULL, NULL);
    buff[buffSize - 1] = 0;
}

void StringToUID(char* buff, GUID* uid)
{
    wchar_t buffw[64] = {0};
    MultiByteToWideChar(CP_ACP, 0, buff, -1, buffw, 64);
    buffw[63] = 0;
    CLSIDFromString(buffw, uid);
}

void CleanUID(char* uid)
{
    char* s = uid;
    char* d = uid;
    while (*s != 0)
    {
        while (*s == '{' || *s == '}' || *s == '-')
            s++;
        *d++ = *s++;
    }
}

//
// ****************************************************************************

//#ifdef MSVC_RUNTIME_CHECKS
char RTCErrorDescription[RTC_ERROR_DESCRIPTION_SIZE] = {0};
// custom reporting funkce opsana z MSDN - http://msdn.microsoft.com/en-us/library/cb00sk7k(v=VS.90).aspx
#pragma runtime_checks("", off)
int MyRTCErrorFunc(int errType, const wchar_t* file, int line,
                   const wchar_t* module, const wchar_t* format, ...)
{
    // Prevent re-entrance.
    static long running = 0;
    while (InterlockedExchange(&running, 1))
        Sleep(0);
    // Now, disable all RTC failures.
    int numErrors = _RTC_NumErrors();
    int* errors = (int*)_alloca(numErrors);
    for (int i = 0; i < numErrors; i++)
        errors[i] = _RTC_SetErrorType((_RTC_ErrorNumber)i, _RTC_ERRTYPE_IGNORE);

    // First, get the rtc error number from the var-arg list.
    va_list vl;
    va_start(vl, format);
    _RTC_ErrorNumber rtc_errnum = va_arg(vl, _RTC_ErrorNumber);
    va_end(vl);

    static wchar_t buf[RTC_ERROR_DESCRIPTION_SIZE];
    static char bufA[RTC_ERROR_DESCRIPTION_SIZE];
    const char* err = _RTC_GetErrDesc(rtc_errnum);
    _snwprintf_s(buf, _TRUNCATE, L"  Error Number: %d\r\n  Description: %S\r\n  Line: #%d\r\n  File: %s\r\n  Module: %s\r\n",
                 rtc_errnum,
                 err,
                 line,
                 file ? file : L"Unknown",
                 module ? module : L"Unknown");

    WideCharToMultiByte(CP_ACP, 0, buf, -1, bufA, RTC_ERROR_DESCRIPTION_SIZE, NULL, NULL);
    bufA[RTC_ERROR_DESCRIPTION_SIZE - 1] = 0;
    // Runtime-check text is a bounded diagnostic field, not an execution identity.
    StringCchCopyNA(RTCErrorDescription, RTC_ERROR_DESCRIPTION_SIZE, bufA, RTC_ERROR_DESCRIPTION_SIZE - 1);

    // prefer breaking here with an exception, the callstack should be clearer - if not, we can remove this exception
    // viz popis chovani _CrtDbgReportW - http://msdn.microsoft.com/en-us/library/8hyw4sy7(v=VS.90).aspx
    RaiseException(OPENSAL_EXCEPTION_RTC, 0, 0, NULL); // nase vlastni "rtc" exception

    // sem uz se nedostaneme, proces byl ukoncen; pokracuji jen z formalnich duvodu, kdybychom neco menili

    // Now, restore the RTC errortypes.
    for (int i = 0; i < numErrors; i++)
        _RTC_SetErrorType((_RTC_ErrorNumber)i, errors[i]);
    running = 0;

    return -1;
}
#pragma runtime_checks("", restore)
//#endif // MSVC_RUNTIME_CHECKS

//
// ****************************************************************************

#ifdef _DEBUG

// Debug heap checks can run after 49 days; preserve their three-second cadence across tick wrap.
ULONGLONG LastCrtCheckMemoryTime;

#endif //_DEBUG

STDAPI _StrRetToBuf(STRRET* psr, LPCITEMIDLIST pidl, LPSTR pszBuf, UINT cchBuf);

BOOL FindPluginsWithoutImportedCfg(BOOL* doNotDeleteImportedCfg)
{
    char names[1000];
    int skipped;
    Plugins.RemoveNoLongerExistingPlugins(FALSE, TRUE, names, 1000, 10, &skipped, MainWindow->HWindow);
    if (names[0] != 0)
    {
        *doNotDeleteImportedCfg = TRUE;
        MSGBOXEX_PARAMS params;
        memset(&params, 0, sizeof(params));
        params.HParent = MainWindow->HWindow;
        params.Flags = MB_OKCANCEL | MB_ICONQUESTION;
        params.Caption = SALAMANDER_TEXT_VERSION;
        char skippedNames[200];
        skippedNames[0] = 0;
        if (skipped > 0)
            sprintf(skippedNames, LoadStr(IDS_NUMOFSKIPPEDPLUGINNAMES), skipped);
        char msg[2000];
        sprintf(msg, LoadStr(IDS_NOTALLPLUGINSCFGIMPORTED), names, skippedNames);
        params.Text = msg;
        char aliasBtnNames[200];
        /* slouzi pro skript export_mnu.py, ktery generuje salmenu.mnu pro Translator
   nechame pro tlacitka msgboxu resit kolize hotkeys tim, ze simulujeme, ze jde o menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_STARTWITHOUTMISSINGPLUGINS
  {MNTT_IT, IDS_SELLANGEXITBUTTON
  {MNTT_PE, 0
};
*/
        sprintf(aliasBtnNames, "%d\t%s\t%d\t%s",
                DIALOG_OK, LoadStr(IDS_STARTWITHOUTMISSINGPLUGINS),
                DIALOG_CANCEL, LoadStr(IDS_SELLANGEXITBUTTON));
        params.AliasBtnNames = aliasBtnNames;
        return SalMessageBoxEx(&params) == IDCANCEL;
    }
    return FALSE;
}

void StartNotepad(const char* file)
{
    STARTUPINFO si = {0};
    PROCESS_INFORMATION pi;
    char buf[MAX_PATH];
    char buf2[MAX_PATH + 50];

    if (strlen(file) >= MAX_PATH)
        return;

    GetSystemDirectory(buf, MAX_PATH); // give it the system directory so it does not block deletion of the current working directory
    // A path accepted by the legacy check still needs bounded command-line formatting.
    if (FAILED(StringCchPrintfA(buf2, ARRAYSIZE(buf2), "notepad.exe \"%s\"", file)))
        return;
    si.cb = sizeof(STARTUPINFO);
    if (HANDLES(CreateProcess(NULL, buf2, NULL, NULL, TRUE, CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
                              NULL, buf, &si, &pi)))
    {
        HANDLES(CloseHandle(pi.hProcess));
        HANDLES(CloseHandle(pi.hThread));
    }
}

BOOL RunningInCompatibilityMode()
{
    // Version virtualization is not a capability contract, so do not block startup on an obsolete version-lie detector.
    TRACE_I("Compatibility-mode version detection is disabled; feature probes govern supported behavior.");
    return FALSE;
}

void GetCommandLineParamExpandEnvVars(const char* argv, char* target, DWORD targetSize, BOOL hotpathForJumplist)
{
    char curDir[MAX_PATH];
    if (hotpathForJumplist)
    {
        BOOL ret = ExpandHotPath(NULL, argv, target, targetSize, FALSE); // if path syntax is not OK, TRACE_E appears, which does not bother us
        if (!ret)
        {
            TRACE_E("ExpandHotPath failed.");
            // Fallback command paths must fit completely; do not continue with a prefix.
            if (FAILED(StringCchCopyA(target, targetSize, argv)))
            {
                if (targetSize > 0)
                    target[0] = 0;
                return;
            }
        }
    }
    else
    {
        DWORD auxRes = ExpandEnvironmentStrings(argv, target, targetSize); // uzivatele si prali moznost predavat jako parametr env promenne
        if (auxRes == 0 || auxRes > targetSize)
        {
            TRACE_E("ExpandEnvironmentStrings failed.");
            // Fallback command paths must fit completely; do not continue with a prefix.
            if (FAILED(StringCchCopyA(target, targetSize, argv)))
            {
                if (targetSize > 0)
                    target[0] = 0;
                return;
            }
        }
    }
    if (!IsPluginFSPath(target) && GetCurrentDirectory(MAX_PATH, curDir))
    {
        SalGetFullName(target, NULL, curDir, NULL, NULL, targetSize);
    }
}

// If parameters are OK, returns TRUE, otherwise returns FALSE.
BOOL ParseCommandLineParameters(LPSTR cmdLine, CCommandLineParams* cmdLineParams)
{
    // nechceme menit cesty, menit ikonu, menit prefix -- vse je potreba vynulovat
    ZeroMemory(cmdLineParams, sizeof(CCommandLineParams));

    char buf[4096];
    char* argv[20];
    int p = 20; // pocet prvku pole argv

    char curDir[MAX_PATH];
    GetModuleFileName(HInstance, ConfigurationName, MAX_PATH);
    *(strrchr(ConfigurationName, '\\') + 1) = 0;
    const char* configReg = "config.reg";
    strcat(ConfigurationName, configReg);
    if (!FileExists(ConfigurationName) && GetOurPathInRoamingAPPDATA(curDir, _countof(curDir)) &&
        SalPathAppend(curDir, configReg, MAX_PATH) && FileExists(curDir))
    { // if file config.reg does not exist next to .exe, also look for it in APPDATA
        // Configuration-file selection requires the complete roaming-path identity.
        if (SUCCEEDED(StringCchCopyA(ConfigurationName, _countof(ConfigurationName), curDir)))
            ConfigurationNameIgnoreIfNotExists = FALSE;
    }
    OpenReadmeInNotepad[0] = 0;
    if (GetCmdLine(buf, _countof(buf), argv, p, cmdLine))
    {
        int i;
        for (i = 0; i < p; i++)
        {
            if (StrICmp(argv[i], "-l") == 0) // left panel path
            {
                if (i + 1 < p)
                {
                    GetCommandLineParamExpandEnvVars(argv[i + 1], cmdLineParams->LeftPath, 2 * MAX_PATH, FALSE);
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-r") == 0) // right panel path
            {
                if (i + 1 < p)
                {
                    GetCommandLineParamExpandEnvVars(argv[i + 1], cmdLineParams->RightPath, 2 * MAX_PATH, FALSE);
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-a") == 0) // active panel path
            {
                if (i + 1 < p)
                {
                    GetCommandLineParamExpandEnvVars(argv[i + 1], cmdLineParams->ActivePath, 2 * MAX_PATH, FALSE);
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-aj") == 0) // active panel path (hot paths syntax for jumplist) - interni, nedokumentovane
            {
                if (i + 1 < p)
                {
                    GetCommandLineParamExpandEnvVars(argv[i + 1], cmdLineParams->ActivePath, 2 * MAX_PATH, TRUE);
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-c") == 0) // default config file
            {
                if (i + 1 < p)
                {
                    char* s = argv[i + 1];
                    if (*s == '\\' && *(s + 1) == '\\' || // UNC full path
                        *s != 0 && *(s + 1) == ':')       // "c:\" full path
                    {                                     // full name
                        // An explicit configuration file must fit before it becomes active.
                        if (FAILED(StringCchCopyA(ConfigurationName, _countof(ConfigurationName), argv[i + 1])))
                            return FALSE;
                    }
                    else // relative name
                    {
                        GetModuleFileName(HInstance, ConfigurationName, MAX_PATH);
                        *(strrchr(ConfigurationName, '\\') + 1) = 0;
                        SalPathAppend(ConfigurationName, s, MAX_PATH);
                        if (!FileExists(ConfigurationName) && GetOurPathInRoamingAPPDATA(curDir, _countof(curDir)) &&
                            SalPathAppend(curDir, s, MAX_PATH) && FileExists(curDir))
                        { // if the relative file specified after -C does not exist next to .exe, also look for it in APPDATA
                            // The roaming configuration path must remain a complete identity.
                            if (FAILED(StringCchCopyA(ConfigurationName, _countof(ConfigurationName), curDir)))
                                return FALSE;
                        }
                    }
                    ConfigurationNameIgnoreIfNotExists = FALSE;
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-i") == 0) // icon index
            {
                if (i + 1 < p)
                {
                    char* s = argv[i + 1];
                    if ((*s == '0' || *s == '1' || *s == '2' || *s == '3') && *(s + 1) == 0) // 0, 1, 2, 3
                    {
                        Configuration.MainWindowIconIndexForced = (*s - '0');

                        cmdLineParams->SetMainWindowIconIndex = TRUE;
                        cmdLineParams->MainWindowIconIndex = Configuration.MainWindowIconIndexForced;
                    }
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-t") == 0) // title prefix
            {
                if (i + 1 < p)
                {
                    Configuration.UseTitleBarPrefixForced = TRUE;
                    char* s = argv[i + 1];
                    if (*s != 0)
                    {
                        // Title prefixes are fixed presentation fields.
                        StringCchCopyNA(Configuration.TitleBarPrefixForced, _countof(Configuration.TitleBarPrefixForced), s, TITLE_PREFIX_MAX - 1);

                        cmdLineParams->SetTitlePrefix = TRUE;
                        StringCchCopyNA(cmdLineParams->TitlePrefix, _countof(cmdLineParams->TitlePrefix), s, _countof(cmdLineParams->TitlePrefix) - 1);
                    }
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-o") == 0) // tvarime se, jako by bylo nahozene OnlyOneIstance
            {
                Configuration.ForceOnlyOneInstance = TRUE;
                continue;
            }

            if (StrICmp(argv[i], "-p") == 0) // activate panel
            {
                if (i + 1 < p)
                {
                    char* s = argv[i + 1];
                    if ((*s == '0' || *s == '1' || *s == '2') && *(s + 1) == 0) // 0, 1, 2
                    {
                        cmdLineParams->ActivatePanel = (*s - '0');
                    }
                    i++;
                    continue;
                }
            }

            if (StrICmp(argv[i], "-run_notepad") == 0 && i + 1 < p)
            { // Vista+: after installation: installer (SFX7ZIP) executes Salamander and asks for execution of notepad with readme file
                // The deferred Notepad target must fit as a complete filesystem identity.
                if (FAILED(StringCchCopyA(OpenReadmeInNotepad, _countof(OpenReadmeInNotepad), argv[i + 1])))
                    return FALSE;
                i++;
                continue;
            }

            return FALSE; // wrong parameters
        }
    }
    return TRUE;
}

int WinMainBody(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR cmdLine, int cmdShow)
{
    int myExitCode = 1;

    InitializeDpiAwareness();

    //--- nechci zadne kriticke chyby jako "no disk in drive A:"
    SetErrorMode(SetErrorMode(0) | SEM_FAILCRITICALERRORS);

    if (!InitializeDllSearchPaths())
    {
        char errorText[300];
        _snprintf_s(errorText, _TRUNCATE,
                    "Open Salamander cannot enable secure DLL loading (error %lu).\n"
                    "Install the Windows security update required for SetDefaultDllDirectories and try again.",
                    GetLastError());
        MessageBox(NULL, errorText, SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
        return myExitCode;
    }

    // seed generatoru nahodnych cisel
    srand((unsigned)time(NULL) ^ (unsigned)_getpid());

#ifdef _DEBUG
    // #define _CRTDBG_ALLOC_MEM_DF        0x01  /* Turn on debug allocation */ (DEFAULT ON)
    // #define _CRTDBG_DELAY_FREE_MEM_DF   0x02  /* Don't actually free memory */
    // #define _CRTDBG_CHECK_ALWAYS_DF     0x04  /* Check heap every alloc/dealloc */
    // #define _CRTDBG_RESERVED_DF         0x08  /* Reserved - do not use */
    // #define _CRTDBG_CHECK_CRT_DF        0x10  /* Leak check/diff CRT blocks */
    // #define _CRTDBG_LEAK_CHECK_DF       0x20  /* Leak check at program exit */

    // pri podezreni na prepis alokovane pameti lze odkomentovat nasledujici dva radky
    // dojde ke zpomaleni Salamandera a pri kazdem free alloc se provede test konzistence heapu
    // int crtDbg = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);   // Get the current bits
    // _CrtSetDbgFlag(crtDbg | _CRTDBG_CHECK_ALWAYS_DF);
    // _CrtSetDbgFlag(crtDbg | _CRTDBG_LEAK_CHECK_DF);

    // another interesting debugging function: if a memory leak occurs, displayed in parentheses
    // dekadicke cislo, ktere udava poradi alokovaneho bloku, napriklad _CRT_WARN: {104200};
    // funkci _CrtSetBreakAlloc umoznuje breaknou na tomto bloku
    // _CrtSetBreakAlloc(33521);

    LastCrtCheckMemoryTime = CMonotonicClock::Now();

    // na tomto pripade prepisu konce pameti zabere ochrana -- v IDLE se zobrazi messagebox
    // a do TraceServeru se nalejou debug hlasky
    //
//  char *p1 = (char*)malloc( 4 );
//  strcpy( p1 , "Oops" );
#endif //_DEBUG

    /*
   // test "Heap Block Corruptions: Full-page heap", viz http://support.microsoft.com/kb/286470
   // allocates all blocks (must have at least 16 bytes) so that an inaccessible page follows them, so
   // jakykoliv prepis konce bloku vede k exceptione
   // instalovat Debugging Tools for Windows, v gflags.exe pro "salamand.exe" vybrat "Enable page heap",
   // fungovalo mi to pod W2K i pod XP (pod Vistou by melo taky)
   // NEBO: pouzit pripravene sal-pageheap-register.reg a sal-pageheap-unregister.reg (to pak neni
   // potreba instalovat Debugging Tools for Windows)
   //
   // prosinec/2011: testoval jsem pod VS2008 + page heap + Win7x64 a nasledujici prepis nevyvolava exception
   // nasel jsem popis alokace v tomto rezimu: http://msdn.microsoft.com/en-us/library/ms220938(v=VS.90).aspx

  char *test = (char *)malloc(16);
//  char *test = (char *)HeapAlloc(GetProcessHeap(), 0, 16);
  char bufff[100];
  sprintf(bufff, "test=%p", test);
  MessageBox(NULL, bufff, "a", MB_OK);
  test[16] = 0;
*/

    char testCharValue = 129;
    int testChar = testCharValue;
    if (testChar != 129) // if testChar is negative, we have a problem: LowerCase[testCharValue] reaches outside the array...
    {
        MessageBox(NULL, "Default type 'char' is not 'unsigned char', but 'signed char'. See '/J' compiler switch in MSVC.",
                   "Compilation Error", MB_OK | MB_ICONSTOP);
    }

    MainThreadID = GetCurrentThreadId();
    HInstance = hInstance;
    CALL_STACK_MESSAGE4("WinMainBody(0x%p, , %s, %d)", hInstance, cmdLine, cmdShow);

    // Tak za tohle ja nemuzu ... co delat, kdyz to dela konkurence, musime
    // taky - inspirovano v Exploreru.
    // A ja se divil, ze jim tak pekne chodi paint.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    SetTraceProcessName("Salamander");
    SetThreadNameInVCAndTrace("Main");
    SetMessagesTitle(MAINWINDOW_NAME);
    TRACE_I("Begin");

    // inicializace OLE
    if (FAILED(OleInitialize(NULL)))
    {
        TRACE_E("Error in CoInitialize.");
        return 1;
    }

    //  HOldWPHookProc = SetWindowsHookEx(WH_CALLWNDPROC,     // HANDLES neumi!
    //                                    WPMessageHookProc,
    //                                    NULL, GetCurrentThreadId());

    User32DLL = NOHANDLES(LoadLibrary("user32.dll"));
    if (User32DLL == NULL)
        TRACE_E("Unable to load library user32.dll."); // not a fatal error

    TurnOFFWindowGhosting();

    NtDLL = HANDLES(LoadLibrary("NTDLL.DLL"));
    if (NtDLL == NULL)
        TRACE_E("Unable to load library ntdll.dll."); // not a fatal error

    // detekce defaultniho userova charsetu pro fonty
    CHARSETINFO ci;
    memset(&ci, 0, sizeof(ci));
    WCHAR localeName[LOCALE_NAME_MAX_LENGTH];
    WCHAR codePageText[10];
    if (GetUserDefaultLocaleName(localeName, _countof(localeName)) &&
        GetLocaleInfoEx(localeName, LOCALE_IDEFAULTANSICODEPAGE, codePageText, _countof(codePageText)))
    {
        // Resolve the user's named locale explicitly instead of depending on the legacy default-LCID mapping.
        if (TranslateCharsetInfo((DWORD*)(DWORD_PTR)MAKELONG(_wtoi(codePageText), 0), &ci, TCI_SRCCODEPAGE))
        {
            UserCharset = ci.ciCharset;
        }
    }

    // because memory-mapped files are used, allocation granularity must be obtained
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    AllocationGranularity = si.dwAllocationGranularity;

    // Windows Versions supported by Open Salamander
    //
    // Name               wMajorVersion  wMinorVersion
    //------------------------------------------------
    // Windows XP         5              1
    // Windows XP x64     5              2
    // Windows Vista      6              0
    // Windows 7          6              1
    // Windows 8          6              2
    // Windows 8.1        6              3
    // Windows 10         10             0             (poznamka: preview verze W10 z 2014 vracely verzi 6.4)

    if (!SalIsWindowsVersionOrGreater(6, 1, 0))
    {
        // this will probably not be reached, older systems will be missing statically linked exports
        // knihoven a uzivatele serve nejaka nepochopitelna hlaska na urovni PE loaderu ve Windows
        // nevolat SalMessageBox
        MessageBox(NULL, "You need at least Windows 7 to run this program.",
                   SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONEXCLAMATION);
    EXIT_1:
        if (User32DLL != NULL)
        {
            NOHANDLES(FreeLibrary(User32DLL));
            User32DLL = NULL;
        }
        if (NtDLL != NULL)
        {
            HANDLES(FreeLibrary(NtDLL));
            NtDLL = NULL;
        }
        return myExitCode;
    }

    WindowsVistaAndLater = SalIsWindowsVersionOrGreater(6, 0, 0);
    WindowsXP64AndLater = SalIsWindowsVersionOrGreater(5, 2, 0);
    Windows7AndLater = SalIsWindowsVersionOrGreater(6, 1, 0);
    Windows8AndLater = SalIsWindowsVersionOrGreater(6, 2, 0);
    Windows8_1AndLater = SalIsWindowsVersionOrGreater(6, 3, 0);
    Windows10AndLater = SalIsWindowsVersionOrGreater(10, 0, 0);

    DWORD integrityLevel;
    if (GetProcessIntegrityLevel(&integrityLevel) && integrityLevel >= SECURITY_MANDATORY_HIGH_RID)
        RunningAsAdmin = TRUE;

    // if possible, use GetNativeSystemInfo, otherwise keep the GetSystemInfo result
    typedef void(WINAPI * PGNSI)(LPSYSTEM_INFO);
    PGNSI pGNSI = (PGNSI)GetProcAddress(GetModuleHandle("kernel32.dll"), "GetNativeSystemInfo"); // Min: XP
    if (pGNSI != NULL)
        pGNSI(&si);
    Windows64Bit = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;

    if (!GetWindowsDirectory(WindowsDirectory, MAX_PATH))
        WindowsDirectory[0] = 0;

    // zajima nas iface ITaskbarList3, ktery MS zavedli od Windows 7 - napriklad progress v taskbar buttons
    if (Windows7AndLater)
    {
        TaskbarBtnCreatedMsg = RegisterWindowMessage("TaskbarButtonCreated");
        if (TaskbarBtnCreatedMsg == 0)
        {
            DWORD err = GetLastError();
            TRACE_E("RegisterWindowMessage() failed for 'TaskbarButtonCreated'. Error:" << err);
        }
    }

    // mame nastavene globalni promenne, muzeme inicializovat tento mutex
    if (!TaskList.Init())
        TRACE_E("TaskList.Init() failed!");

    if (!InitializeWinLib())
        goto EXIT_1; // musime inicializovat WinLib pred prvnim zobrazenim
                     // wait dialog (window classes must be registered)
                     // ImportConfiguration can already open this dialog

    LoadSaveToRegistryMutex.Init();

    if (!ConfigureFileManagerUiTestConfigurationStore())
    {
        // Invalid test routing is fatal because continuing would risk the current user's live configuration.
        goto EXIT_1;
    }

    // zkusime z aktualni konfigurace vytahnout hodnotu "AutoImportConfig" -> existuje v pripade, ze provadime UPGRADE
    BOOL autoImportConfig = FALSE;
    char autoImportConfigFromKey[200];
    autoImportConfigFromKey[0] = 0;
    if (!GetUpgradeInfo(&autoImportConfig, autoImportConfigFromKey, 200)) // user si preje exit softu
    {
        myExitCode = 0;
    EXIT_1a:
        ReleaseWinLib();
        goto EXIT_1;
    }
    const char* configKey = autoImportConfig ? autoImportConfigFromKey : SalamanderConfigurationRoots[0];

    // zkusime z aktualni konfigurace vytahnout klic urcujici jazyk
    LoadSaveToRegistryMutex.Enter();
    HKEY hSalamander;
    DWORD langChanged = FALSE; // TRUE = starting Salamander for the first time with another language (load all plugins to verify we have this language version for them too, or let user choose fallback versions)
    if (OpenKey(HKEY_CURRENT_USER, configKey, hSalamander))
    {
        HKEY actKey;
        DWORD configVersion = 1; // toto je konfig od 1.52 a starsi
        if (OpenKey(hSalamander, SALAMANDER_VERSION_REG, actKey))
        {
            configVersion = 2; // toto je konfig od 1.6b1
            GetValue(actKey, SALAMANDER_VERSIONREG_REG, REG_DWORD,
                     &configVersion, sizeof(DWORD));
            CloseKey(actKey);
        }
        if (configVersion >= 59 /* 2.53 beta 2 */ && // before 2.53 beta 2 there was only English, so reading makes no sense; offer user's default system language or manual language selection
            OpenKey(hSalamander, SALAMANDER_CONFIG_REG, actKey))
        {
            GetValue(actKey, CONFIG_LANGUAGE_REG, REG_SZ,
                     Configuration.SLGName, MAX_PATH);
            GetValue(actKey, CONFIG_USEALTLANGFORPLUGINS_REG, REG_DWORD,
                     &Configuration.UseAsAltSLGInOtherPlugins, sizeof(DWORD));
            GetValue(actKey, CONFIG_ALTLANGFORPLUGINS_REG, REG_SZ,
                     Configuration.AltPluginSLGName, MAX_PATH);
            GetValue(actKey, CONFIG_LANGUAGECHANGED_REG, REG_DWORD, &langChanged, sizeof(DWORD));
            CloseKey(actKey);
        }
        CloseKey(hSalamander);
    }
    LoadSaveToRegistryMutex.Leave();

FIND_NEW_SLG_FILE:

    // if the key does not exist, show the selection dialog
    BOOL newSLGFile = FALSE; // TRUE if .SLG was selected during this Salamander run
    if (Configuration.SLGName[0] == 0)
    {
        CLanguageSelectorDialog slgDialog(NULL, Configuration.SLGName, NULL);
        slgDialog.Initialize();
        if (slgDialog.GetLanguagesCount() == 0)
        {
            MessageBox(NULL, "Unable to find any language file (.SLG) in subdirectory LANG.\n"
                             "Please reinstall Open Salamander.",
                       SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
            goto EXIT_1a;
        }
        Configuration.UseAsAltSLGInOtherPlugins = FALSE;
        Configuration.AltPluginSLGName[0] = 0;

        char prevVerSLGName[MAX_PATH];
        if (!autoImportConfig &&                            // pri UPGRADE toto nema smysl (jazyk se cte o par radek vyse, tahle rutina by ho jen precetla znovu)
            FindLanguageFromPrevVerOfSal(prevVerSLGName) && // importneme jazyk z predchozi verze, je dost pravdepodobne, ze ho user opet chce pouzit (jde o import stare konfigurace Salama)
            slgDialog.SLGNameExists(prevVerSLGName) &&
            // A malformed previous setting must fall back to the normal picker.
            SUCCEEDED(StringCchCopyA(Configuration.SLGName, _countof(Configuration.SLGName), prevVerSLGName)))
        {
        }
        else
        {
            int langIndex = slgDialog.GetPreferredLanguageIndex(NULL, TRUE);
            if (langIndex == -1) // tato instalace neobsahuje jazyk souhlasici s aktualnim user-locale ve Windows
            {

// kdyz se tohle zakomentuje, nebudeme posilat lidi tahat jazykove verze z webu (napr. kdyz tam zadne nejsou)
// JRY: pro AS 2.53, kery jde s cestinou, nemcinou a anglictinou je pro ostatni jazyky posleme na forum do sekce
//      "Translations" /viewforum.php?f=23 - treba to nekoho namotivuje a pujde svuj preklad vytvorit
#define OFFER_OTHERLANGUAGE_VERSIONS

#ifndef OFFER_OTHERLANGUAGE_VERSIONS
                if (slgDialog.GetLanguagesCount() == 1)
                    slgDialog.GetSLGName(Configuration.SLGName); // if only one language exists, use it
                else
                {
#endif // OFFER_OTHERLANGUAGE_VERSIONS

                    // otevreme dialog vyberu jazyku, aby mohl user downloadnout a nainstalovat dalsi jazyky
                    if (slgDialog.Execute() == IDCANCEL)
                        goto EXIT_1a;

#ifndef OFFER_OTHERLANGUAGE_VERSIONS
                }
#endif // OFFER_OTHERLANGUAGE_VERSIONS
            }
            else
            {
                slgDialog.GetSLGName(Configuration.SLGName, langIndex); // if a language matching the current Windows user-locale exists, use it
            }
        }
        newSLGFile = TRUE;
        langChanged = TRUE;
    }

    char path[MAX_PATH];
    char errorText[MAX_PATH + 200];
    GetModuleFileName(NULL, path, MAX_PATH);
    char* languageFileName = strrchr(path, '\\');
    if (languageFileName == NULL ||
        FormatStringChecked(languageFileName + 1, _countof(path) - (languageFileName + 1 - path),
                            "lang\\%s", Configuration.SLGName) != bsrSuccess)
    {
        MessageBox(NULL, "The selected language-file path is too long or invalid.",
                   SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
        goto EXIT_1a;
    }
    HLanguage = HANDLES(LoadLibraryUtf8(path));
    LanguageID = 0;
    if (HLanguage == NULL || !IsSLGFileValid(HInstance, HLanguage, LanguageID, IsSLGIncomplete))
    {
        if (HLanguage != NULL)
            HANDLES(FreeLibrary(HLanguage));
        if (!newSLGFile) // remembered .SLG file probably stopped existing, try to find another one
        {
            if (FormatStringChecked(errorText, _countof(errorText),
                                    "File %s was not found or is not valid language file.\nOpen Salamander "
                                    "will try to search for some other language file (.SLG).",
                                    path) != bsrSuccess)
                CopyStringChecked(errorText, _countof(errorText), "The selected language file is invalid.");
            MessageBox(NULL, errorText, SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);
            Configuration.SLGName[0] = 0;
            goto FIND_NEW_SLG_FILE;
        }
        else // should never happen - .SLG file was already tested
        {
            if (FormatStringChecked(errorText, _countof(errorText),
                                    "File %s was not found or is not valid language file.\n"
                                    "Please run Open Salamander again and try to choose some other language file.",
                                    path) != bsrSuccess)
                CopyStringChecked(errorText, _countof(errorText), "The selected language file is invalid.");
            MessageBox(NULL, errorText, "Open Salamander", MB_OK | MB_ICONERROR);
            goto EXIT_1a;
        }
    }

    strcpy(Configuration.LoadedSLGName, Configuration.SLGName);

    // nechame jiz bezici salmon nacist zvolene SLG (zatim pouzival nejake provizorni)
    SalmonSetSLG(Configuration.SLGName);

    CCommandLineParams cmdLineParams;
    if (!ParseCommandLineParameters(cmdLine, &cmdLineParams))
    {
        SalMessageBox(NULL, LoadStr(IDS_INVALIDCMDLINE), SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONERROR);

    EXIT_2:
        if (HLanguage != NULL)
            HANDLES(FreeLibrary(HLanguage));
        goto EXIT_1a;
    }

    if (RunningInCompatibilityMode())
    {
        CCommonDialog dlg(HLanguage, IDD_COMPATIBILITY_MODE, NULL);
        if (dlg.Execute() == IDCANCEL)
            goto EXIT_2;
    }

#ifdef USE_BETA_EXPIRATION_DATE
    // beta verze je casove limitovana, viz BETA_EXPIRATION_DATE
    // if today is the day determined by this variable or any later one, show a window and exit
    SYSTEMTIME st;
    GetLocalTime(&st);
    SYSTEMTIME* expire = &BETA_EXPIRATION_DATE;
    if (st.wYear > expire->wYear ||
        (st.wYear == expire->wYear && st.wMonth > expire->wMonth) ||
        (st.wYear == expire->wYear && st.wMonth == expire->wMonth && st.wDay >= expire->wDay))
    {
        if (ShowBetaExpDlg() == IDCANCEL)
            goto EXIT_2;
    }
#endif // USE_BETA_EXPIRATION_DATE

    // otevreme splash screen

    GetSystemDPI(NULL);

    // if configuration does not exist or is later changed during import from file, the user
    // is out of luck and the splash screen will follow the default or old value
    LoadSaveToRegistryMutex.Enter();
    if (OpenKey(HKEY_CURRENT_USER, configKey, hSalamander))
    {
        HKEY actKey;
        if (OpenKey(hSalamander, SALAMANDER_CONFIG_REG, actKey))
        {
            GetValue(actKey, CONFIG_SHOWSPLASHSCREEN_REG, REG_DWORD,
                     &Configuration.ShowSplashScreen, sizeof(DWORD));
            CloseKey(actKey);
        }
        CloseKey(hSalamander);
    }
    LoadSaveToRegistryMutex.Leave();

    if (Configuration.ShowSplashScreen)
        SplashScreenOpen();

    // configuration import window contains a listview with checkboxes, must initialize COMMON CONTROLS
    INITCOMMONCONTROLSEX initCtrls;
    initCtrls.dwSize = sizeof(INITCOMMONCONTROLSEX);
    initCtrls.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES |
                      ICC_TAB_CLASSES | ICC_COOL_CLASSES |
                      ICC_DATE_CLASSES | ICC_USEREX_CLASSES;
    if (!InitCommonControlsEx(&initCtrls))
    {
        TRACE_E("InitCommonControlsEx failed");
        SplashScreenCloseIfExist();
        goto EXIT_2;
    }

    SetWinLibStrings(LoadStr(IDS_INVALIDNUMBER), MAINWINDOW_NAME); // j.r. - posunout na spravne misto

    // inicializace pakovacu; drive provadeno v konstruktorech; ted presunuto sem,
    // kdy uz je rozhodnuto o jazykovem DLL
    PackerFormatConfig.InitializeDefaultValues();
    ArchiverConfig.InitializeDefaultValues();
    PackerConfig.InitializeDefaultValues();
    UnpackerConfig.InitializeDefaultValues();

    // if the file exists, it will be imported into the registry
    BOOL importCfgFromFileWasSkipped = FALSE;
    ImportConfiguration(NULL, ConfigurationName, ConfigurationNameIgnoreIfNotExists, autoImportConfig,
                        &importCfgFromFileWasSkipped);

    // obslouzime prechod ze stareho configu na novy

    // Zavolame funkci, ktera se pokusi najit konfiguraci odpovidajici nasi verzi programu.
    // If it manages to find it, variable 'loadConfiguration' will be set and the function returns
    // TRUE. Pokud konfigurace jeste nebude existovat, funkce postupne prohleda stare
    // konfigurace z pole 'SalamanderConfigurationRoots' (od nejmladsich k nejstrasim).
    // Pokud nalezne nekterou z konfiguraci, zobrazi dialog a nabidne jeji konverzi do
    // konfigurace soucasne a smazani z registry. Po zobrazeni posledniho dialogu vrati
    // TRUE a nastavi promenne 'deleteConfigurations' a 'loadConfiguration' dle voleb
    // uzivatele. Pokud uzivatel zvoli ukonceni aplikace, vrati funkce FALSE.

    // pole urcujici indexy konfiguraci v poli 'SalamanderConfigurationRoots',
    // ktere maji byt smazany (0 -> zadna)
    BOOL deleteConfigurations[SALCFG_ROOTS_COUNT];
    ZeroMemory(deleteConfigurations, sizeof(deleteConfigurations));

    CALL_STACK_MESSAGE1("WinMainBody::FindLatestConfiguration");

    // ukazatel do pole 'SalamanderConfigurationRoots' na konfiguraci, ktera ma byt
    // nactena (NULL -> zadna; pouziji se default hodnoty)
    if (autoImportConfig)
        SALAMANDER_ROOT_REG = autoImportConfigFromKey; // pri UPGRADE nema hledani konfigurace smysl
    else if (IsFileManagerUiTestConfigurationStore())
    {
        // The dedicated root deliberately bypasses migration discovery across normal-version configuration keys.
        SALAMANDER_ROOT_REG = SalamanderConfigurationRoots[0];
    }
    else
    {
        if (!FindLatestConfiguration(deleteConfigurations, SALAMANDER_ROOT_REG))
        {
            SplashScreenCloseIfExist();
            goto EXIT_2;
        }
    }

    InitializeShellib(); // OLE je treba inicializovat pred otevrenim HTML helpu - CSalamanderEvaluation

    // Retain the migration result before selecting a transactional generation, which replaces the public root path.
    BOOL selectedPreviousConfigurationRoot = SALAMANDER_ROOT_REG != SalamanderConfigurationRoots[0];

    // Preserve the version root separately, then resolve it to its last committed generation.
    // All existing configuration readers continue to use SALAMANDER_ROOT_REG.
    SetConfigurationStoreRoot(SALAMANDER_ROOT_REG);
    BOOL hasCommittedConfiguration = SelectCommittedConfigurationGeneration();
    // A sandbox is new only until its first complete generation exists; later starts must reload its own saved settings.
    BOOL currentCfgDoesNotExist = autoImportConfig || selectedPreviousConfigurationRoot ||
                                   (IsFileManagerUiTestConfigurationStore() && !hasCommittedConfiguration);
    BOOL saveNewConfig = currentCfgDoesNotExist;
    const char* configurationDiagnostic = GetConfigurationSchemaDiagnostic();
    if (configurationDiagnostic != NULL)
    {
        // Raised before the main window exists and outside SalMessageBox, so it
        // needs its own transcript record to be visible to a UI-test run.
        LogUiTestDialog("SHOW", "Open Salamander Configuration", configurationDiagnostic, MB_OK | MB_ICONWARNING, 0);
        int diagnosticResult = MessageBox(NULL, configurationDiagnostic, "Open Salamander Configuration",
                                          MB_OK | MB_ICONWARNING);
        LogUiTestDialog("RESULT", "Open Salamander Configuration", configurationDiagnostic,
                        MB_OK | MB_ICONWARNING, diagnosticResult);
    }

    // if the user does not want more instances, only activate the previous one
    if (!currentCfgDoesNotExist &&
        CheckOnlyOneInstance(&cmdLineParams))
    {
        SplashScreenCloseIfExist();
        myExitCode = 0;
    EXIT_3:
        ReleaseShellib();
        goto EXIT_2;
    }

    // overim verzi CommonControlu
    if (GetComCtlVersion(&CCVerMajor, &CCVerMinor) != S_OK) // JRYFIXME - testy kolem common controls posunout na W7+
    {
        CCVerMajor = 0; // tohle asi nikdy nenastane - nemaji comctl32.dll
        CCVerMinor = 0;
    }

    CALL_STACK_MESSAGE1("WinMainBody::StartupDialog");

    //  StartupDialog.Open(HLanguage);

    int i;
    for (i = 0; i < NUMBER_OF_COLORS; i++)
        UserColors[i] = SalamanderColors[i];

    //--- inicializacni cast
    CALL_STACK_MESSAGE1("WinMainBody::inicialization");
    IfExistSetSplashScreenText(LoadStr(IDS_STARTUP_DATA));

    InitDefaultDir();
    PackSetErrorHandler(PackErrorHandler);
    InitLocales();

    if (!InitPreloadedStrings())
    {
        SplashScreenCloseIfExist();
    EXIT_4:
        ReleasePreloadedStrings();
        goto EXIT_3;
    }
    if (!InitializeCheckThread() || !InitializeFind())
    {
        SplashScreenCloseIfExist();
    EXIT_5:
        ReleaseCheckThreads();
        goto EXIT_4;
    }
    InitializeMenuWheelHook();
    SetupWinLibHelp(&SalamanderHelp);
    if (!InitializeDiskCache())
    {
        SplashScreenCloseIfExist();
    EXIT_6:
        ReleaseFind();
        goto EXIT_5;
    }
    if (!InitializeConstGraphics())
    {
        SplashScreenCloseIfExist();
    EXIT_7:
        ReleaseConstGraphics();
        goto EXIT_6;
    }
    if (!InitializeGraphics(FALSE))
    {
        SplashScreenCloseIfExist();
    EXIT_8:
        ReleaseGraphics(FALSE);
        goto EXIT_7;
    }
    if (!InitializeMenu() || !BuildSalamanderMenus())
    {
        SplashScreenCloseIfExist();
        goto EXIT_8;
    }
    if (!InitializeThread())
    {
        SplashScreenCloseIfExist();
    EXIT_9:
        TerminateThread();
        goto EXIT_8;
    }
    if (!InitializeViewer())
    {
        SplashScreenCloseIfExist();
        ReleaseViewer();
        goto EXIT_9;
    }

    // pripojeni OLE SPYe
    // posunuto pod InitializeGraphics, ktere pod WinXP vyhazovala leaky (asi zase neajake cache)
    // OleSpyRegister();    // disabled because after the Windows 2000 update from 02/2005, starting+closing Salamander from MSVC began hitting debug-breakpoint: Invalid Address specified to RtlFreeHeap( 130000, 14bc74 ) - perhaps MS started calling RtlFreeHeap directly somewhere instead of OLE free, and the spy information block at the start of the allocated block broke it (malloc returns pointer shifted after the spy information block)
    //OleSpySetBreak(2754); // brakne na [n-te] alokaci z dumpu

    // inicializace workera (diskove operace)
    InitWorker();

    // initialize icon thread pool for parallel icon extraction
    // uses number of CPU cores - 1, minimum 2, maximum ICON_POOL_MAX_WORKERS
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int iconPoolWorkers = (int)sysInfo.dwNumberOfProcessors - 1;
    if (iconPoolWorkers < 2)
        iconPoolWorkers = 2;
    if (iconPoolWorkers > ICON_POOL_MAX_WORKERS)
        iconPoolWorkers = ICON_POOL_MAX_WORKERS;
    IconPool.Initialize(iconPoolWorkers);

    // inicializace knihovny pro komunikaci s SalShExt/SalamExt/SalExtX86/SalExtX64.DLL (shell copy hook + shell context menu)
    InitSalShLib();

    // inicializace knihovny pro praci s shell icon overlays (Tortoise SVN + CVS)
    LoadIconOvrlsInfo(SALAMANDER_ROOT_REG);
    InitShellIconOverlays();

    // initialize functions for walking through next/previous file in panel/Find from viewer
    InitFileNamesEnumForViewers();

    // nacteme seznam sharovanych adresaru
    IfExistSetSplashScreenText(LoadStr(IDS_STARTUP_SHARES));
    Shares.Refresh();

    CMainWindow::RegisterUniversalClass(CS_DBLCLKS | CS_SAVEBITS,
                                        0,
                                        0,
                                        NULL,
                                        LoadCursor(NULL, IDC_ARROW),
                                        (HBRUSH)(COLOR_3DFACE + 1),
                                        NULL,
                                        SAVEBITS_CLASSNAME,
                                        NULL);
    CMainWindow::RegisterUniversalClass(CS_DBLCLKS,
                                        0,
                                        0,
                                        NULL,
                                        LoadCursor(NULL, IDC_ARROW),
                                        (HBRUSH)(COLOR_3DFACE + 1),
                                        NULL,
                                        SHELLEXECUTE_CLASSNAME,
                                        NULL);

    Associations.ReadAssociations(FALSE); // nacteni asociaci z Registry

    // registrace shell extensions
    // if we find a library in the "utils" subdirectory, verify its registration and register it if needed
    char shellExtPath[MAX_PATH];
    GetModuleFileName(HInstance, shellExtPath, MAX_PATH);
    char* shellExtPathSlash = strrchr(shellExtPath, '\\');
    if (shellExtPathSlash != NULL)
    {
        strcpy(shellExtPathSlash + 1, "utils\\salextx86.dll");
#ifdef _WIN64
        if (FileExists(shellExtPath))
            SalShExtRegistered = SECRegisterToRegistry(shellExtPath, TRUE, KEY_WOW64_32KEY);
        strcpy(shellExtPathSlash + 1, "utils\\salextx64.dll");
        if (FileExists(shellExtPath))
            SalShExtRegistered &= SECRegisterToRegistry(shellExtPath, FALSE, 0);
        else
            SalShExtRegistered = FALSE;
#else  // _WIN64
        if (FileExists(shellExtPath))
            SalShExtRegistered = SECRegisterToRegistry(shellExtPath, FALSE, 0);
        if (Windows64Bit)
        {
            strcpy(shellExtPathSlash + 1, "utils\\salextx64.dll");
            if (FileExists(shellExtPath))
                SalShExtRegistered &= SECRegisterToRegistry(shellExtPath, TRUE, KEY_WOW64_64KEY);
            else
                SalShExtRegistered = FALSE;
        }
#endif // _WIN64
    }

    //--- vytvoreni hlavniho okna
    if (CMainWindow::RegisterUniversalClass(CS_DBLCLKS | CS_OWNDC,
                                            0,
                                            0,
                                            NULL, // HIcon
                                            LoadCursor(NULL, IDC_ARROW),
                                            NULL /*(HBRUSH)(COLOR_WINDOW + 1)*/, // HBrush
                                            NULL,
                                            CFILESBOX_CLASSNAME,
                                            NULL) &&
        CMainWindow::RegisterUniversalClass(CS_DBLCLKS,
                                            0,
                                            0,
                                            HANDLES(LoadIcon(HInstance,
                                                             MAKEINTRESOURCE(IDI_SALAMANDER))),
                                            LoadCursor(NULL, IDC_ARROW),
                                            (HBRUSH)(COLOR_WINDOW + 1),
                                            NULL,
                                            CMAINWINDOW_CLASSNAME,
                                            NULL))
    {
        MainWindow = new CMainWindow;
        if (MainWindow != NULL)
        {
            MainWindow->CmdShow = cmdShow;
            if (MainWindow->Create(CMAINWINDOW_CLASSNAME,
                                   "",
                                   WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                   CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                                   NULL,
                                   NULL,
                                   HInstance,
                                   MainWindow))
            {
                SetMessagesParent(MainWindow->HWindow);
                PluginMsgBoxParent = MainWindow->HWindow;
                // Defer OOM recovery until the UI owns execution, so the failed
                // allocation never runs journal or shutdown code under worker locks.
                SetAllocEmergencyNotificationWindow(MainWindow->HWindow, WM_USER_ALLOCATION_EMERGENCY);

                // vytahneme z registry Group Policy
                IfExistSetSplashScreenText(LoadStr(IDS_STARTUP_POLICY));
                SystemPolicies.LoadFromRegistry();

                CALL_STACK_MESSAGE1("WinMainBody::load_config");
                BOOL setActivePanelAndPanelPaths = FALSE; // aktivni panel + cesty v panelech se nastavuji v MainWindow->LoadConfig()
                if (!MainWindow->LoadConfig(currentCfgDoesNotExist, !importCfgFromFileWasSkipped ? &cmdLineParams : NULL))
                {
                    setActivePanelAndPanelPaths = TRUE;
                    UpdateDefaultColors(CurrentColors, MainWindow->HighlightMasks, FALSE, TRUE);
                    Plugins.CheckData();
                    MainWindow->InsertMenuBand();
                    if (Configuration.TopToolBarVisible)
                        MainWindow->ToggleTopToolBar();
                    if (Configuration.DriveBarVisible)
                        MainWindow->ToggleDriveBar(Configuration.DriveBar2Visible, FALSE);
                    if (Configuration.PluginsBarVisible)
                        MainWindow->TogglePluginsBar();
                    if (Configuration.MiddleToolBarVisible)
                        MainWindow->ToggleMiddleToolBar();
                    if (Configuration.BottomToolBarVisible)
                        MainWindow->ToggleBottomToolBar();
                    MainWindow->CreateAndInsertWorkerBand(); // na zaver vlozime workera
                    MainWindow->LeftPanel->UpdateDriveIcon(TRUE);
                    MainWindow->RightPanel->UpdateDriveIcon(TRUE);
                    MainWindow->LeftPanel->UpdateFilterSymbol();
                    MainWindow->RightPanel->UpdateFilterSymbol();
                    if (!SystemPolicies.GetNoRun())
                        SendMessage(MainWindow->HWindow, WM_COMMAND, CM_TOGGLEEDITLINE, TRUE);
                    MainWindow->SetWindowIcon();
                    MainWindow->SetWindowTitle();
                    // one-shot background check for a newer GitHub release, started
                    // once the main window exists so the result can update title/menu
                    StartUpdateCheck(MainWindow->HWindow);
                    SplashScreenCloseIfExist();
                    ShowWindow(MainWindow->HWindow, cmdShow);
                    UpdateWindow(MainWindow->HWindow);
                    MainWindow->RefreshDirs();
                    MainWindow->FocusLeftPanel();
                }

                if (Configuration.ReloadEnvVariables)
                    InitEnvironmentVariablesDifferences();

                if (newSLGFile)
                {
                    Plugins.ClearLastSLGNames(); // aby pripadne doslo k nove volbe nahradniho jazyka u vsech pluginu
                    Configuration.ShowSLGIncomplete = TRUE;
                }

                MainMenu.SetSkillLevel(CfgSkillLevelToMenu(Configuration.SkillLevel));

                if (!MainWindow->IsGood())
                {
                    SetMessagesParent(NULL);
                    DestroyWindow(MainWindow->HWindow);
                    TRACE_E(LOW_MEMORY);
                }
                else
                {
                    if (!importCfgFromFileWasSkipped) // only if the application does not exit immediately (then it makes no sense)
                        MainWindow->ApplyCommandLineParams(&cmdLineParams, setActivePanelAndPanelPaths);

                    if (Windows7AndLater)
                        CreateJumpList();

                    IdleRefreshStates = TRUE;  // pri pristim Idle vynutime kontrolu stavovych promennych
                    IdleCheckClipboard = TRUE; // nechame kontrolovat take clipboard

                    AccelTable1 = HANDLES(LoadAccelerators(HInstance, MAKEINTRESOURCE(IDA_MAINACCELS1)));
                    AccelTable2 = HANDLES(LoadAccelerators(HInstance, MAKEINTRESOURCE(IDA_MAINACCELS2)));

                    MainWindow->CanClose = TRUE; // ted teprve povolime zavreni hl. okna
                    // aby soubory nevyskakovali postupne (jak se nacitaji jejich ikony)
                    UpdateWindow(MainWindow->HWindow);
                    COperationJournal::OfferRecovery(MainWindow->HWindow);

                    BOOL doNotDeleteImportedCfg = FALSE;
                    if (autoImportConfig && // find out whether the new version has fewer plugins than the old one and part of old configuration would not transfer because of that
                        FindPluginsWithoutImportedCfg(&doNotDeleteImportedCfg))
                    {                               // je potreba exit softu bez ulozeni konfigurace
                        SALAMANDER_ROOT_REG = NULL; // tohle by melo spolehlive zamezit zapisu do konfigurace v registry
                        PostMessage(MainWindow->HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);
                    }
                    else
                    {
                        if (Configuration.ConfigVersion < THIS_CONFIG_VERSION
#ifndef _WIN64 // FIXME_X64_WINSCP
                            || Configuration.AddX86OnlyPlugins
#endif // _WIN64
                        )
                        {                                            // auto-install plug-inu ze standardniho plug-in-podadresare "plugins"
#ifndef _WIN64                                                       // FIXME_X64_WINSCP
                            Configuration.AddX86OnlyPlugins = FALSE; // jednou staci
#endif                                                               // _WIN64
                            Plugins.AutoInstallStdPluginsDir(MainWindow->HWindow);
                            Configuration.LastPluginVer = 0;   // when switching to new version, file plugins.ver will be canceled
                            Configuration.LastPluginVerOP = 0; // when switching to new version, file plugins.ver will also be canceled for the other platform
                            saveNewConfig = TRUE;              // new configuration must be saved (so this is not repeated next run)
                        }
                        // loading plugins.ver file ((re)installation of plug-ins), needed even the first time (in case
                        // instalace plug-inu pred prvnim spustenim Salamandera)
                        if (Plugins.ReadPluginsVer(MainWindow->HWindow, Configuration.ConfigVersion < THIS_CONFIG_VERSION))
                            saveNewConfig = TRUE; // new configuration must be saved (so this is not repeated next run)
                        // load plug-inu, ktere maji nastaveny flag load-on-start
                        Plugins.HandleLoadOnStartFlag(MainWindow->HWindow);
                        // if starting for the first time with changed language, load all plugins to show
                        // whether they have this language version + optionally let the user choose fallback languages
                        if (langChanged)
                            Plugins.LoadAll(MainWindow->HWindow);

                        // pluginy FTP a WinSCP nove volaji SalamanderGeneral->SetPluginUsesPasswordManager() aby se prihlasily k odberu eventu z password managera
                        // zavedeno s verzi kofigurace 45 -- dame vsem pluginum moznost se prihlasit
                        if (Configuration.ConfigVersion < 45) // zavedeni password manageru
                            Plugins.LoadAll(MainWindow->HWindow);

                        if (IsFileManagerUiTestSandboxRequested())
                        {
                            // The UI suite invokes FTP commands directly; eagerly assign
                            // their otherwise lazy owner-drawn-menu IDs only in its sandbox.
                            CMenuPopup* pluginsMenu =
                                (CMenuPopup*)MainMenu.GetSubMenu(CML_PLUGINS, FALSE);
                            Plugins.InitUiTestPluginMenuItems(MainWindow->HWindow, pluginsMenu, "ftp.spl");
                        }

                        // Save into a new generation below the newest version root.
                        SetConfigurationStoreRoot(SalamanderConfigurationRoots[0]);
                        SelectCommittedConfigurationGeneration();
                        // save configuration immediately while it is a clean conversion of the old version -- user may
                        // have "Save Cfg on Exit" disabled and if something changes during Salamander run, they do not want to save it at the end
                        if (saveNewConfig)
                        {
                            MainWindow->SaveConfig();
                        }
                        // searches the array and if any root is marked for deletion, deletes it + deletes old configuration
                        // po UPGRADE a tez smazne hodnotu "AutoImportConfig" v klici konfigurace teto verze Salama
                        MainWindow->DeleteOldConfigurations(deleteConfigurations, autoImportConfig, autoImportConfigFromKey,
                                                            doNotDeleteImportedCfg);

                        // only first Salamander instance: check whether cleanup is needed
                        // TEMP of unnecessary disk-cache files (after crash or locking by another application
                        // muzou soubory v TEMPu zustat)
                        // must test on global (across all sessions) variable so two can see each other
                        // instance Salamanderu spustene pod FastUserSwitching
                        // Problem nahlasen na foru: /viewtopic.php?t=2643
                        if (FirstInstance_3_or_later)
                        {
                            DiskCache.ClearTEMPIfNeeded(MainWindow->HWindow, MainWindow->GetActivePanelHWND());
                        }

                        if (importCfgFromFileWasSkipped) // if we skipped import of config.reg or another .reg file (parameter -C)
                        {                                // informujeme usera o nutnosti noveho startu Salama a nechame ho exitnout soft
                            MSGBOXEX_PARAMS params;
                            memset(&params, 0, sizeof(params));
                            params.HParent = MainWindow->HWindow;
                            params.Flags = MB_OK | MB_ICONINFORMATION;
                            params.Caption = SALAMANDER_TEXT_VERSION;
                            params.Text = LoadStr(IDS_IMPORTCFGFROMFILESKIPPED);
                            char aliasBtnNames[200];
                            /* slouzi pro skript export_mnu.py, ktery generuje salmenu.mnu pro Translator
   nechame pro tlacitka msgboxu resit kolize hotkeys tim, ze simulujeme, ze jde o menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] =
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_SELLANGEXITBUTTON
  {MNTT_PE, 0
};
*/
                            sprintf(aliasBtnNames, "%d\t%s", DIALOG_OK, LoadStr(IDS_SELLANGEXITBUTTON));
                            params.AliasBtnNames = aliasBtnNames;
                            SalMessageBoxEx(&params);
                            PostMessage(MainWindow->HWindow, WM_USER_FORCECLOSE_MAINWND, 0, 0);
                        }
                        /*
            // je-li treba, vyvolame zobrazeni dialogu Tip of the Day
            // 0xffffffff = open quiet - if it fails, do not bother the user
            if (Configuration.ShowTipOfTheDay)
              PostMessage(MainWindow->HWindow, WM_COMMAND, CM_HELP_TIP, 0xffffffff);
  */
                    }

                    // from now on, closing paths will be remembered
                    MainWindow->CanAddToDirHistory = TRUE;

                    // uzivatele chteji mit start-up cestu v historii i v pripade, ze ji neuspinili
                    MainWindow->LeftPanel->UserWorkedOnThisPath = TRUE;
                    MainWindow->RightPanel->UserWorkedOnThisPath = TRUE;

                    // let the process list know we are running and have the main window (we can be activated for OnlyOneInstance)
                    TaskList.SetProcessState(PROCESS_STATE_RUNNING, MainWindow->HWindow);

                    // pozadame Salmon o kontrolu, zda na disku nejsou stare bug reporty, ktere by bylo potreba odeslat
                    SalmonCheckBugs();

                    if (IsSLGIncomplete[0] != 0 && Configuration.ShowSLGIncomplete)
                        PostMessage(MainWindow->HWindow, WM_USER_SLGINCOMPLETE, 0, 0);

                    //--- aplikacni smycka
                    CALL_STACK_MESSAGE1("WinMainBody::message_loop");
                    DWORD activateParamsRequestUID = 0;
                    BOOL skipMenuBar;
                    MSG msg;
                    BOOL haveMSG = FALSE; // FALSE if GetMessage() should be called in loop condition
                    while (haveMSG || GetMessage(&msg, NULL, 0, 0))
                    {
                        haveMSG = FALSE;
                        if (msg.message != WM_USER_SHOWWINDOW && msg.message != WM_USER_WAKEUP_FROM_IDLE && /*msg.message != WM_USER_SETPATHS &&*/
                            msg.message != WM_QUERYENDSESSION && msg.message != WM_USER_SALSHEXT_PASTE &&
                            msg.message != WM_USER_CLOSE_MAINWND && msg.message != WM_USER_FORCECLOSE_MAINWND)
                        { // except "connect", "shutdown", "do-paste" and "close-main-wnd" messages, all are the start of BUSY mode
                            SalamanderBusy = TRUE;
                            // This public plug-in ABI timestamp is a DWORD, not a monotonic-time value.
                            LastSalamanderIdleTime = GetTickCount();
                        }

                        if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYDOWN) &&
                            msg.wParam != VK_MENU && msg.wParam != VK_CONTROL && msg.wParam != VK_SHIFT)
                        {
                            SetCurrentToolTip(NULL, 0); // zhasneme tooltip
                        }

                        skipMenuBar = FALSE;
                        if (Configuration.QuickSearchEnterAlt && msg.message == WM_SYSCHAR)
                            skipMenuBar = TRUE;

                        // zajistime zaslani zprav do naseho menu (obchazime tim potrebu hooku pro klavesnici)
                        if (MainWindow == NULL || MainWindow->MenuBar == NULL || !MainWindow->CaptionIsActive ||
                            MainWindow->QuickRenameWindowActive() ||
                            skipMenuBar || GetCapture() != NULL || // je-li captured mouse - mohli bychom zpusobit vizualni problemy
                            !MainWindow->MenuBar->IsMenuBarMessage(&msg))
                        {
                            CWindowsObject* wnd = WindowsManager.GetWindowPtr(GetActiveWindow());

                            // Bottom Toolbar - text change according to VK_CTRL, VK_MENU and VK_SHIFT
                            if ((msg.message == WM_SYSKEYDOWN || msg.message == WM_KEYDOWN ||
                                 msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) &&
                                MainWindow != NULL)
                                MainWindow->UpdateBottomToolBar();

                            if ((wnd == NULL || !wnd->Is(otDialog) ||
                                 !IsDialogMessage(wnd->HWindow, &msg)) &&
                                (MainWindow == NULL || !MainWindow->CaptionIsActive || // pridano "!MainWindow->CaptionIsActive", aby se v nemodalnich oknech pluginu neprekladaly akceleratory (F7 v "FTP Logs" neni nic moc)
                                 MainWindow->QuickRenameWindowActive() ||
                                 !TranslateAccelerator(MainWindow->HWindow, AccelTable1, &msg) &&
                                     (MainWindow->EditMode || !TranslateAccelerator(MainWindow->HWindow, AccelTable2, &msg))))
                            {
                                TranslateMessage(&msg);
                                DispatchMessage(&msg);
                            }
                        }

                        if (MainWindow != NULL && MainWindow->CanClose)
                        { // je-li Salamander nastartovany, muzeme ho prohlasit za NE BUSY
                            SalamanderBusy = FALSE;
                        }

                    TEST_IDLE:
                        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                        {
                            if (msg.message == WM_QUIT)
                                break;      // equivalent of the situation when GetMessage() returns FALSE
                            haveMSG = TRUE; // we have a message, process it (without calling GetMessage())
                        }
                        else // if no message is in the queue, perform Idle processing
                        {
#ifdef _DEBUG
                            // jednou za tri vteriny osetrime konzistenci heapu
                            if (_CrtSetDbgFlag(_CRTDBG_REPORT_FLAG) & _CRTDBG_ALLOC_MEM_DF)
                            {
                                // Keep the debug heap-check interval monotonic during long-lived sessions.
                                if (CMonotonicClock::HasElapsed(LastCrtCheckMemoryTime, 3000, CMonotonicClock::Now())) // kazde tri vteriny
                                {
                                    if (!_CrtCheckMemory())
                                    {
                                        HWND hParent = NULL;
                                        if (MainWindow != NULL)
                                            hParent = MainWindow->HWindow;
                                        MessageBox(hParent, "_CrtCheckMemory failed. Look to the Trace Server for details.", "Open Salamander", MB_OK | MB_ICONERROR);
                                    }
                                    LastCrtCheckMemoryTime = CMonotonicClock::Now();
                                }
                            }
#endif //_DEBUG

                            if (MainWindow != NULL)
                            {
                                CannotCloseSalMainWnd = TRUE; // musime zamezit zavreni hlavniho okna Salamandera behem provadeni nasledujicich rutin
                                MainWindow->OnEnterIdle();

                                // wait for ESC release only if listing refresh in panel directly
                                // navazuje (coz pres IDLE nehrozi)
                                if (WaitForESCReleaseBeforeTestingESC)
                                    WaitForESCReleaseBeforeTestingESC = FALSE;

                                // zjistime, zda nas nezada cizi "OnlyOneInstance" Salamander o aktivaci a nastaveni cest v panelech?
                                // FControlThread by v takovem pripade nastavil parametry do globalni CommandLineParams a zvysil RequestUID
                                // if the main thread was in IDLE, it woke up thanks to posted WM_USER_WAKEUP_FROM_IDLE
                                if (!SalamanderBusy && CommandLineParams.RequestUID > activateParamsRequestUID)
                                {
                                    CCommandLineParams paramsCopy;
                                    BOOL applyParams = FALSE;

                                    NOHANDLES(EnterCriticalSection(&CommandLineParamsCS));
                                    // tesne pred vstupem do kriticke sekce mohlo dojit k timeoutu v control threadu, overime ze jeste stoji o vysledek
                                    // also verify that the request has not expired (calling thread waits only until TASKLIST_TODO_TIMEOUT and then waiting
                                    // vzda a spusti novou instanci Salamander; nechceme v takovem pripade pozadavek vyplnit)
                                    // RequestTimestamp is a fixed-width shared-memory field used by
                                    // older instances, so retain its 32-bit wrap-aware comparison.
                                    DWORD tickCount = GetTickCount();
                                    if (CommandLineParams.RequestUID != 0 &&
                                        tickCount - CommandLineParams.RequestTimestamp < TASKLIST_TODO_TIMEOUT)
                                    {
                                        memcpy(&paramsCopy, &CommandLineParams, sizeof(CCommandLineParams));
                                        applyParams = TRUE;

                                        // ulozime UID, ktere jsme jiz odbavili, abychom necyklili
                                        activateParamsRequestUID = CommandLineParams.RequestUID;
                                        // dame control threadu zpravu, ze jsme cesty prijali
                                        SetEvent(CommandLineParamsProcessed);
                                    }
                                    NOHANDLES(LeaveCriticalSection(&CommandLineParamsCS));

                                    // uvolnili jsme sdilene prostredky, muzeme se jit parat s cestama
                                    if (applyParams && MainWindow != NULL)
                                    {
                                        SendMessage(MainWindow->HWindow, WM_USER_SHOWWINDOW, 0, 0);
                                        MainWindow->ApplyCommandLineParams(&paramsCopy);
                                    }
                                }

                                // zajistime unik z odstranenych drivu na fixed drive (po vysunuti device - USB flash disk, atd.)
                                if (!SalamanderBusy && ChangeLeftPanelToFixedWhenIdle)
                                {
                                    ChangeLeftPanelToFixedWhenIdle = FALSE;
                                    ChangeLeftPanelToFixedWhenIdleInProgress = TRUE;
                                    if (MainWindow != NULL && MainWindow->LeftPanel != NULL)
                                        MainWindow->LeftPanel->ChangeToRescuePathOrFixedDrive(MainWindow->LeftPanel->HWindow);
                                    ChangeLeftPanelToFixedWhenIdleInProgress = FALSE;
                                }
                                if (!SalamanderBusy && ChangeRightPanelToFixedWhenIdle)
                                {
                                    ChangeRightPanelToFixedWhenIdle = FALSE;
                                    ChangeRightPanelToFixedWhenIdleInProgress = TRUE;
                                    if (MainWindow != NULL && MainWindow->RightPanel != NULL)
                                        MainWindow->RightPanel->ChangeToRescuePathOrFixedDrive(MainWindow->RightPanel->HWindow);
                                    ChangeRightPanelToFixedWhenIdleInProgress = FALSE;
                                }
                                if (!SalamanderBusy && OpenCfgToChangeIfPathIsInaccessibleGoTo)
                                {
                                    OpenCfgToChangeIfPathIsInaccessibleGoTo = FALSE;
                                    if (MainWindow != NULL)
                                        PostMessage(MainWindow->HWindow, WM_USER_CONFIGURATION, 6, 0);
                                }

                                // if any plug-in wanted unload or menu rebuild, perform it... (only if not "busy")
                                if (!SalamanderBusy && ExecCmdsOrUnloadMarkedPlugins)
                                {
                                    int cmd;
                                    CPluginData* data;
                                    Plugins.GetCmdAndUnloadMarkedPlugins(MainWindow->HWindow, &cmd, &data);
                                    ExecCmdsOrUnloadMarkedPlugins = (cmd != -1);
                                    if (cmd >= 0 && cmd < 500) // spusteni prikazu Salamandera na zadost plug-inu
                                    {
                                        int wmCmd = GetWMCommandFromSalCmd(cmd);
                                        if (wmCmd != -1)
                                        {
                                            // vygenerujeme WM_COMMAND a nechame ho hned zpracovat
                                            msg.hwnd = MainWindow->HWindow;
                                            msg.message = WM_COMMAND;
                                            msg.wParam = (DWORD)LOWORD(wmCmd); // radsi orizneme horni WORD (0 - cmd z menu)
                                            msg.lParam = 0;
                                            // msg.time is intentionally not updated - see related comment in menu_popup.cpp
                                            GetCursorPos(&msg.pt);

                                            haveMSG = TRUE; // we have a message, process it (without calling GetMessage())
                                        }
                                    }
                                    else
                                    {
                                        if (cmd >= 500 && cmd < 1000500) // spusteni prikazu menuExt na zadost plug-inu
                                        {
                                            int id = cmd - 500;
                                            SalamanderBusy = TRUE; // jdeme provest prikaz menu - uz jsme zase "busy"
                                            LastSalamanderIdleTime = GetTickCount();
                                            if (data != NULL && data->GetLoaded())
                                            {
                                                if (data->GetPluginInterfaceForMenuExt()->NotEmpty())
                                                {
                                                    CALL_STACK_MESSAGE4("CPluginInterfaceForMenuExt::ExecuteMenuItem(, , %d,) (%s v. %s)",
                                                                        id, data->DLLName, data->Version);

                                                    // snizime prioritu threadu na "normal" (aby operace prilis nezatezovaly stroj)
                                                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

                                                    CSalamanderForOperations sm(MainWindow->GetActivePanel());
                                                    data->GetPluginInterfaceForMenuExt()->ExecuteMenuItem(&sm, MainWindow->HWindow, id, 0);

                                                    // opet zvysime prioritu threadu, operace dobehla
                                                    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                                                }
                                                else
                                                {
                                                    TRACE_E("Plugin must have PluginInterfaceForMenuExt when "
                                                            "calling CSalamanderGeneral::PostMenuExtCommand()!");
                                                }
                                            }
                                            else
                                            {
                                                // nemusi byt naloaden, staci aby se PostMenuExtCommand zavolal z Release pluginu,
                                                // ktery je vyvolany postnutym unloadem
                                                // TRACE_E("Unexpected situation during call of menu command in \"sal-idle\".");
                                            }
                                            if (MainWindow != NULL && MainWindow->CanClose) // konec provadeni prikazu menu
                                            {                                               // je-li Salamander nastartovany, muzeme ho prohlasit za NE BUSY
                                                SalamanderBusy = FALSE;
                                            }
                                            CannotCloseSalMainWnd = FALSE;
                                            goto TEST_IDLE; // zkusime znovu "idle" (napr. aby se mohl zpracovat dalsi postnuty prikaz/unload)
                                        }
                                    }
                                }
                                if (!SalamanderBusy && OpenPackOrUnpackDlgForMarkedPlugins)
                                {
                                    CPluginData* data;
                                    int pluginIndex;
                                    Plugins.OpenPackOrUnpackDlgForMarkedPlugins(&data, &pluginIndex);
                                    OpenPackOrUnpackDlgForMarkedPlugins = (data != NULL);
                                    if (data != NULL) // otevreni Pack/Unpack dialogu na zadost plug-inu
                                    {
                                        SalamanderBusy = TRUE; // jdeme provest prikaz menu - uz jsme zase "busy"
                                        LastSalamanderIdleTime = GetTickCount();
                                        if (data->OpenPackDlg)
                                        {
                                            CFilesWindow* activePanel = MainWindow->GetActivePanel();
                                            if (activePanel != NULL && activePanel->Is(ptDisk))
                                            { // otevreni Pack dialogu
                                                MainWindow->CancelPanelsUI();
                                                activePanel->UserWorkedOnThisPath = TRUE;
                                                activePanel->StoreSelection(); // ulozime selection pro prikaz Restore Selection
                                                activePanel->Pack(MainWindow->GetNonActivePanel(), pluginIndex,
                                                                  data->Name, data->PackDlgDelFilesAfterPacking);
                                            }
                                            else
                                                TRACE_E("Unexpected situation: type of active panel is not Disk!");
                                            data->OpenPackDlg = FALSE;
                                            data->PackDlgDelFilesAfterPacking = 0;
                                        }
                                        else
                                        {
                                            if (data->OpenUnpackDlg)
                                            {
                                                CFilesWindow* activePanel = MainWindow->GetActivePanel();
                                                if (activePanel != NULL && activePanel->Is(ptDisk))
                                                { // otevreni Unpack dialogu
                                                    MainWindow->CancelPanelsUI();
                                                    activePanel->UserWorkedOnThisPath = TRUE;
                                                    activePanel->StoreSelection(); // ulozime selection pro prikaz Restore Selection
                                                    activePanel->Unpack(MainWindow->GetNonActivePanel(), pluginIndex,
                                                                        data->Name, data->UnpackDlgUnpackMask);
                                                }
                                                else
                                                    TRACE_E("Unexpected situation: type of active panel is not Disk!");
                                                data->OpenUnpackDlg = FALSE;
                                                if (data->UnpackDlgUnpackMask != NULL)
                                                    free(data->UnpackDlgUnpackMask);
                                                data->UnpackDlgUnpackMask = NULL;
                                            }
                                        }
                                        if (MainWindow != NULL && MainWindow->CanClose) // konec otevirani Pack/Unpack dialogu
                                        {                                               // je-li Salamander nastartovany, muzeme ho prohlasit za NE BUSY
                                            SalamanderBusy = FALSE;
                                        }
                                        CannotCloseSalMainWnd = FALSE;
                                        goto TEST_IDLE; // zkusime znovu "idle" (napr. aby se mohl zpracovat dalsi postnuty prikaz/unload/Pack/Unpack)
                                    }
                                }
                                if (!SalamanderBusy && OpenReadmeInNotepad[0] != 0)
                                { // spustime notepad se souborem 'OpenReadmeInNotepad' pro instalak pod Vista+
                                    StartNotepad(OpenReadmeInNotepad);
                                    OpenReadmeInNotepad[0] = 0;
                                }
                                CannotCloseSalMainWnd = FALSE;
                            }
                        }
                    }
                }
                PluginMsgBoxParent = NULL;
            }
            else
            {
                TRACE_E(LOW_MEMORY);
            }
        }
    }
    else
        TRACE_E("Unable to register main window class.");

    // pro pripad chyby zkusim zavrit dialog
    SplashScreenCloseIfExist();

    // vratime prioritu do puvodniho stavu
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);

    // Shutdown sequence lives in app_shutdown.cpp; it runs once after this point.
    ShutdownSalamander();
    return 0;
}

int WINAPI
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR cmdLine, int cmdShow)
{
#ifndef CALLSTK_DISABLE
    __try
    {
#endif // CALLSTK_DISABLE

        //#ifdef MSVC_RUNTIME_CHECKS
        _RTC_SetErrorFuncW(&MyRTCErrorFunc);
        //#endif // MSVC_RUNTIME_CHECKS

        int result = WinMainBody(hInstance, hPrevInstance, cmdLine, cmdShow);

        return result;
#ifndef CALLSTK_DISABLE
    }
    __except (CCallStack::HandleException(GetExceptionInformation()))
    {
        TRACE_I("Thread Main: calling ExitProcess(1).");
        //    ExitProcess(1);
        TerminateProcess(GetCurrentProcess(), 1); // tvrdsi exit (tenhle jeste neco vola)
        return 1;
    }
#endif // CALLSTK_DISABLE
}
