// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#include "lstrfix.h"
#include "..\shexreg.h"
#include "shellext.h"

//
// Global variables
//
UINT g_cRefThisDll = 0;         // Reference count of this DLL.
UINT g_cLocksCount = 0;         // number of server locks
HINSTANCE g_hmodThisDll = NULL; // Handle to this DLL itself.

#ifdef SHEXT_LOG_ENABLED

HANDLE LogFile = NULL;
HANDLE LogFileMutex = NULL;
char ModuleName[MAX_PATH];

#ifdef _WIN64
const char* ShExtName = "salextx64.dll";
#else  // _WIN64
const char* ShExtName = "salextx86.dll";
#endif // _WIN64

static BOOL Utf8ToWideSimple(const char* src, WCHAR* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return FALSE;
    buf[0] = 0;
    if (src == NULL)
        return FALSE;
    int res = MultiByteToWideChar(CP_UTF8, 0, src, -1, buf, bufSize);
    if (res == 0)
        res = MultiByteToWideChar(CP_ACP, 0, src, -1, buf, bufSize);
    return res != 0;
}

static BOOL WideToUtf8Simple(const WCHAR* src, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return FALSE;
    buf[0] = 0;
    if (src == NULL)
        return FALSE;
    int res = WideCharToMultiByte(CP_UTF8, 0, src, -1, buf, bufSize, NULL, NULL);
    if (res == 0)
        res = WideCharToMultiByte(CP_ACP, 0, src, -1, buf, bufSize, NULL, NULL);
    return res != 0;
}

void WriteToLog(const char* str)
{
    if (LogFile != NULL)
    {
        DWORD wr;
        SYSTEMTIME st;
        char buf[100];
        WaitForSingleObject(LogFileMutex, INFINITE);
        // Keep the append seek on the Boolean API so a valid file position never shares the old error sentinel.
        LARGE_INTEGER endOffset = {0};
        SetFilePointerEx(LogFile, endOffset, NULL, FILE_END);
        GetLocalTime(&st);
        // The CRT-free logger formats the fixed-width timestamp directly to avoid an unbounded formatter.
        buf[0] = (char)('0' + st.wHour / 10);
        buf[1] = (char)('0' + st.wHour % 10);
        buf[2] = ':';
        buf[3] = (char)('0' + st.wMinute / 10);
        buf[4] = (char)('0' + st.wMinute % 10);
        buf[5] = ':';
        buf[6] = (char)('0' + st.wSecond / 10);
        buf[7] = (char)('0' + st.wSecond % 10);
        buf[8] = ':';
        buf[9] = (char)('0' + st.wMilliseconds / 100);
        buf[10] = (char)('0' + (st.wMilliseconds / 10) % 10);
        buf[11] = (char)('0' + st.wMilliseconds % 10);
        buf[12] = 0;
        WriteFile(LogFile, buf, lstrlen(buf), &wr, NULL);
        WriteFile(LogFile, ": ", 2, &wr, NULL);
        WriteFile(LogFile, ModuleName, lstrlen(ModuleName), &wr, NULL);
        WriteFile(LogFile, ": ", 2, &wr, NULL);
        WriteFile(LogFile, str, lstrlen(str), &wr, NULL);
        WriteFile(LogFile, "\r\n", 2, &wr, NULL);
        FlushFileBuffers(LogFile);
        ReleaseMutex(LogFileMutex);
    }
}

SECURITY_ATTRIBUTES* CreateAccessableSecurityAttributes(SECURITY_ATTRIBUTES* sa, SECURITY_DESCRIPTOR* sd,
                                                        DWORD allowedAccessMask, PSID* psidEveryone, PACL* paclNewDacl)
{
    SID_IDENTIFIER_AUTHORITY siaWorld = SECURITY_WORLD_SID_AUTHORITY;
    int nAclSize;

    *psidEveryone = NULL;
    *paclNewDacl = NULL;

    // Create the everyone sid
    if (!AllocateAndInitializeSid(&siaWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, psidEveryone))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): AllocateAndInitializeSid() failed!");
        goto ErrorExit;
    }

    nAclSize = GetLengthSid(psidEveryone) * 2 + sizeof(ACCESS_ALLOWED_ACE) + sizeof(ACCESS_DENIED_ACE) + sizeof(ACL);
    // This ACL is constructed by the extension and only borrowed by CreateMutex during creation.
    *paclNewDacl = (PACL)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, nAclSize);
    if (*paclNewDacl == NULL)
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): LocalAlloc() failed!");
        goto ErrorExit;
    }
    if (!InitializeAcl(*paclNewDacl, nAclSize, ACL_REVISION))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): InitializeAcl() failed!");
        goto ErrorExit;
    }
    if (!AddAccessDeniedAce(*paclNewDacl, ACL_REVISION, WRITE_DAC | WRITE_OWNER, *psidEveryone))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): AddAccessDeniedAce() failed!");
        goto ErrorExit;
    }
    if (!AddAccessAllowedAce(*paclNewDacl, ACL_REVISION, allowedAccessMask, *psidEveryone))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): AddAccessAllowedAce() failed!");
        goto ErrorExit;
    }
    if (!InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): InitializeSecurityDescriptor() failed!");
        goto ErrorExit;
    }
    if (!SetSecurityDescriptorDacl(sd, TRUE, *paclNewDacl, FALSE))
    {
        //    TRACE_E("CreateAccessableSecurityAttributes(): SetSecurityDescriptorDacl() failed!");
        goto ErrorExit;
    }
    sa->nLength = sizeof(SECURITY_ATTRIBUTES);
    sa->bInheritHandle = FALSE;
    sa->lpSecurityDescriptor = sd;
    return sa;

ErrorExit:
    if (*paclNewDacl != NULL)
    {
        HeapFree(GetProcessHeap(), 0, *paclNewDacl);
        *paclNewDacl = NULL;
    }
    if (*psidEveryone != NULL)
    {
        FreeSid(*psidEveryone);
        *psidEveryone = NULL;
    }
    return NULL;
}

#endif // SHEXT_LOG_ENABLED

// ****************************************************************************
// SalIsWindowsVersionOrGreater
//
// Based on SDK 8.1 VersionHelpers.h
// Indicates if the current OS version matches, or is greater than, the provided
// version information. This function is useful in confirming a version of Windows
// Server that doesn't share a version number with a client release.
// http://msdn.microsoft.com/en-us/library/windows/desktop/dn424964%28v=vs.85%29.aspx
//

BOOL SalIsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    OSVERSIONINFOEXW osvi;
    DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0,
                                                                                                   VER_MAJORVERSION, VER_GREATER_EQUAL),
                                                                               VER_MINORVERSION, VER_GREATER_EQUAL),
                                                           VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

    SecureZeroMemory(&osvi, sizeof(osvi)); // replacement for memset (does not require the RTL)
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    osvi.dwMajorVersion = wMajorVersion;
    osvi.dwMinorVersion = wMinorVersion;
    osvi.wServicePackMajor = wServicePackMajor;
    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

int APIENTRY
DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        // Extension DLL one-time initialization
        g_hmodThisDll = hInstance;

#ifdef SHEXT_LOG_ENABLED

        {
            PSID psidEveryone;
            PACL paclNewDacl;
            SECURITY_ATTRIBUTES sa;
            SECURITY_DESCRIPTOR sd;
            SECURITY_ATTRIBUTES* saPtr = CreateAccessableSecurityAttributes(&sa, &sd, SYNCHRONIZE /*| MUTEX_MODIFY_STATE*/, &psidEveryone, &paclNewDacl);

            {
                WCHAR moduleNameW[MAX_PATH];
                if (GetModuleFileNameW(g_hmodThisDll, moduleNameW, MAX_PATH) != 0)
                    WideToUtf8Simple(moduleNameW, ModuleName, MAX_PATH);
                else
                    ModuleName[0] = 0;
            }
            {
                WCHAR logFileNameW[MAX_PATH];
                if (Utf8ToWideSimple(SHEXT_LOG_FILENAME, logFileNameW, MAX_PATH))
                {
                    LogFile = CreateFileW(logFileNameW, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          NULL, OPEN_ALWAYS, 0, NULL);
                }
                else
                {
                    LogFile = INVALID_HANDLE_VALUE;
                }
            }

            LogFileMutex = CreateMutex(saPtr, FALSE, "shext-log-mutex");
            // CreateMutex has consumed the security descriptor; release the extension-owned backing ACL and SID.
            if (paclNewDacl != NULL)
                HeapFree(GetProcessHeap(), 0, paclNewDacl);
            if (psidEveryone != NULL)
                FreeSid(psidEveryone);
            if (LogFileMutex == NULL)
                LogFileMutex = OpenMutex(SYNCHRONIZE, FALSE, "shext-log-mutex");
            if (LogFile == INVALID_HANDLE_VALUE || LogFileMutex == NULL)
            {
                if (LogFile == INVALID_HANDLE_VALUE)
                    MessageBox(NULL, "Unable to open log file!", ShExtName, MB_OK);
                if (LogFileMutex == NULL)
                    MessageBox(NULL, "Unable to open shext-log-mutex!", ShExtName, MB_OK);
                if (LogFile != INVALID_HANDLE_VALUE)
                    CloseHandle(LogFile);
                LogFile = NULL;
            }
            WriteToLog("Loading DLL");
        }

#endif // SHEXT_LOG_ENABLED
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        // release the data from the registry

#ifdef ENABLE_SH_MENU_EXT

        SECDeleteAllItems();

#endif // ENABLE_SH_MENU_EXT

#ifdef SHEXT_LOG_ENABLED

        WriteToLog("Unloading DLL");
        if (LogFile != NULL)
            CloseHandle(LogFile);
        if (LogFileMutex != NULL)
            CloseHandle(LogFileMutex);

#endif // SHEXT_LOG_ENABLED
    }

    return 1; // ok
}

BOOL WINAPI
_DllMainCRTStartup(HANDLE hDllHandle,
                   DWORD dwReason,
                   LPVOID lpReserved)
{
    DWORD exitCode;

    exitCode = DllMain(hDllHandle, dwReason, lpReserved);

    return exitCode;
}

STDAPI
DllCanUnloadNow()
{
    return (g_cRefThisDll == 0 && g_cLocksCount == 0 ? S_OK : S_FALSE);
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppvOut)
{
    WriteToLog("DllGetClassObject");

    *ppvOut = NULL;
    if (MyIsEqualIID(rclsid, &CLSID_ShellExtension))
        return CreateShellExtClassFactory(riid, ppvOut);
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCheckVersion(REFCLSID rclsid)
{
    return MyIsEqualIID(rclsid, &CLSID_ShellExtension) ? S_OK : S_FALSE;
}

//****************************************************************************
//
// IShellExtClassFactory
//

#undef INTERFACE
#define INTERFACE IShellExtClassFactory

HRESULT CreateShellExtClassFactory(REFIID riid, LPVOID* ppvOut)
{
    ShellExtClassFactory* secf;
    HRESULT hr;

    if (ppvOut == NULL)
        return E_INVALIDARG;

    // create object
    secf = SECF_Constructor();

    if (secf == NULL)
        return E_OUTOFMEMORY;

    // Get interface, whichs calls AddRef
    hr = secf->lpVtbl->QueryInterface((IShellExtClassFactory*)secf, riid, ppvOut);
    if (secf->m_cRef == 0)
        SECF_Destructor(secf); // if QueryInterface failed, release the object
    return hr;
}

static IShellExtClassFactoryVtbl vtShellExtClassFactory;
static BOOL g_vtShellExtClassFactoryInitialized = FALSE;

ShellExtClassFactory* SECF_Constructor()
{
    ShellExtClassFactory* secf;

    // COM object storage remains wholly owned by this extension, so use the process heap.
    secf = (ShellExtClassFactory*)HeapAlloc(GetProcessHeap(), 0, sizeof(ShellExtClassFactory));

    if (secf == NULL)
        return NULL;

    if (!g_vtShellExtClassFactoryInitialized)
    {
        vtShellExtClassFactory.QueryInterface = SECF_QueryInterface;
        vtShellExtClassFactory.AddRef = SECF_AddRef;
        vtShellExtClassFactory.Release = SECF_Release;
        vtShellExtClassFactory.CreateInstance = SECF_CreateInstance;
        vtShellExtClassFactory.LockServer = SECF_LockServer;

        g_vtShellExtClassFactoryInitialized = TRUE;
    }

    // initialize the vtbl
    secf->lpVtbl = &vtShellExtClassFactory;

    // initialize additional data
    secf->m_cRef = 0;

    g_cRefThisDll++;
    return secf;
}

void SECF_Destructor(ShellExtClassFactory* secf)
{
    if (secf == NULL)
        return;

    g_cRefThisDll--;

    HeapFree(GetProcessHeap(), 0, secf);
    return;
}

STDMETHODIMP SECF_QueryInterface(THIS_ REFIID riid, LPVOID* ppvObj)
{
    *ppvObj = NULL;

    // Any interface on this object is the object pointer
    if (MyIsEqualIID(riid, &IID_IUnknown) || MyIsEqualIID(riid, &IID_IClassFactory))
    {
        *ppvObj = (LPCLASSFACTORY)This;

        This->lpVtbl->AddRef(This);

        return NOERROR;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
SECF_AddRef(THIS)
{
    ShellExtClassFactory* secf = (ShellExtClassFactory*)This;
    return ++secf->m_cRef;
}

STDMETHODIMP_(ULONG)
SECF_Release(THIS)
{
    ShellExtClassFactory* secf = (ShellExtClassFactory*)This;

    if (--secf->m_cRef)
        return secf->m_cRef;

    SECF_Destructor(secf);

    return 0;
}

STDMETHODIMP SECF_CreateInstance(THIS_
                                     LPUNKNOWN pUnkOuter,
                                 REFIID riid,
                                 LPVOID* ppvObj)
{
    *ppvObj = NULL;

    WriteToLog("SECF_CreateInstance");

    // Shell extensions typically don't support aggregation (inheritance)
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    // Create the main shell extension object.  The shell will then call
    // QueryInterface with IID_IShellExtInit--this is how shell extensions are
    // initialized.
    return CreateShellExt(riid, ppvObj);
}

STDMETHODIMP SECF_LockServer(THIS_
                                 BOOL fLock)
{
    if (fLock)
        g_cLocksCount++;
    else
        g_cLocksCount--;
    return S_OK;
}

//****************************************************************************
//
// IShellExt
//

#undef INTERFACE
#define INTERFACE IShellExt

HRESULT CreateShellExt(REFIID riid, LPVOID* ppvOut)
{
    ShellExt* se;
    HRESULT hr;

    if (ppvOut == NULL)
        return E_INVALIDARG;

    // create object
    se = SE_Constructor();

    if (se == NULL)
        return E_OUTOFMEMORY;

    // Get interface, whichs calls AddRef
    hr = se->lpVtbl->QueryInterface((IShellExt*)se, riid, ppvOut);
    if (se->m_cRef == 0)
        SE_Destructor(se); // if QueryInterface failed, release the object
    return hr;
}

static ImpIShellExtInitVtbl vtShellExtInit;
static ImpICopyHookVtbl vtCopyHook;
static ImpICopyHookWVtbl vtCopyHookW;
static IShellExtVtbl vtShellExt;
static BOOL g_vtShellExtInitialized = FALSE;

ShellExt* SE_Constructor()
{
    ShellExt* se;

    // Shell-extension interface objects never cross a GlobalAlloc ownership boundary.
    se = (ShellExt*)HeapAlloc(GetProcessHeap(), 0, sizeof(ShellExt));
    if (se == NULL)
        return NULL;

    se->m_pSEI = (ShellExtInit*)HeapAlloc(GetProcessHeap(), 0, sizeof(ShellExtInit));
    se->m_pCH = (CopyHook*)HeapAlloc(GetProcessHeap(), 0, sizeof(CopyHook));
    se->m_pCHW = (CopyHookW*)HeapAlloc(GetProcessHeap(), 0, sizeof(CopyHookW));
    if (se->m_pSEI == NULL || se->m_pCH == NULL || se->m_pCHW == NULL)
    {
        if (se->m_pSEI != NULL)
            HeapFree(GetProcessHeap(), 0, se->m_pSEI);
        if (se->m_pCH != NULL)
            HeapFree(GetProcessHeap(), 0, se->m_pCH);
        if (se->m_pCHW != NULL)
            HeapFree(GetProcessHeap(), 0, se->m_pCHW);
        HeapFree(GetProcessHeap(), 0, se);
        return NULL;
    }

    if (!g_vtShellExtInitialized)
    {
        vtShellExtInit.QueryInterface = SEI_QueryInterface;
        vtShellExtInit.AddRef = SEI_AddRef;
        vtShellExtInit.Release = SEI_Release;
        vtShellExtInit.Initialize = SEI_Initialize;

        vtCopyHook.QueryInterface = CH_QueryInterface;
        vtCopyHook.AddRef = CH_AddRef;
        vtCopyHook.Release = CH_Release;
        vtCopyHook.CopyCallback = CH_CopyCallback;

        vtCopyHookW.QueryInterface = CHW_QueryInterface;
        vtCopyHookW.AddRef = CHW_AddRef;
        vtCopyHookW.Release = CHW_Release;
        vtCopyHookW.CopyCallback = CHW_CopyCallback;

        vtShellExt.QueryInterface = SE_QueryInterface;
        vtShellExt.AddRef = SE_AddRef;
        vtShellExt.Release = SE_Release;

#ifdef ENABLE_SH_MENU_EXT

        vtShellExt.QueryContextMenu = SE_QueryContextMenu;
        vtShellExt.InvokeCommand = SE_InvokeCommand;
        vtShellExt.GetCommandString = SE_GetCommandString;

#endif // ENABLE_SH_MENU_EXT

        g_vtShellExtInitialized = TRUE;
    }

    // initialize the vtbl
    se->lpVtbl = &vtShellExt;
    se->m_pSEI->lpVtbl = &vtShellExtInit;
    se->m_pSEI->m_pObj = se;
    se->m_pCH->lpVtbl = &vtCopyHook;
    se->m_pCH->m_pObj = se;
    se->m_pCHW->lpVtbl = &vtCopyHookW;
    se->m_pCHW->m_pObj = se;

    // initialize additional data
    se->m_cRef = 0;
    //  se->m_pIDFolder = NULL;
    se->m_pDataObj = NULL;
    //  se->m_hRegKey = NULL;

#ifdef ENABLE_SH_MENU_EXT

    // load the registry entries
    SECLoadRegistry();

#endif // ENABLE_SH_MENU_EXT

    g_cRefThisDll++;

    return se;
}

void SE_Destructor(ShellExt* se)
{
    if (se == NULL)
        return;

    if (se->m_pDataObj)
        se->m_pDataObj->lpVtbl->Release(se->m_pDataObj);

    g_cRefThisDll--;

    HeapFree(GetProcessHeap(), 0, se->m_pCHW);
    HeapFree(GetProcessHeap(), 0, se->m_pCH);
    HeapFree(GetProcessHeap(), 0, se->m_pSEI);
    HeapFree(GetProcessHeap(), 0, se);
    return;
}

STDMETHODIMP SE_QueryInterface(THIS_ REFIID riid, LPVOID* ppvObj)
{
    ShellExt* se = (ShellExt*)This;

#ifdef SHEXT_LOG_ENABLED
    // Diagnostic logging must not add a variable-size formatter to the CRT-free extension.
    WriteToLog("SE_QueryInterface");
#endif // SHEXT_LOG_ENABLED

    *ppvObj = NULL;

    if (MyIsEqualIID(riid, &IID_IUnknown)

#ifdef ENABLE_SH_MENU_EXT

        || MyIsEqualIID(riid, &IID_IContextMenu)

#endif // ENABLE_SH_MENU_EXT

    )
    {

#ifdef ENABLE_SH_MENU_EXT

        WriteToLog("SE_QueryInterface: IUnknown or IContextMenu");

#else // ENABLE_SH_MENU_EXT

        WriteToLog("SE_QueryInterface: IUnknown");

#endif // ENABLE_SH_MENU_EXT

        *ppvObj = This;
    }
    else if (MyIsEqualIID(riid, &IID_IShellExtInit))
    {
        WriteToLog("SE_QueryInterface: IShellExtInit");
        *ppvObj = se->m_pSEI;
    }
    else if (MyIsEqualIID(riid, &IID_IShellCopyHookA))
    {
        WriteToLog("SE_QueryInterface: IShellCopyHookA");
        *ppvObj = se->m_pCH;
    }
    else if (MyIsEqualIID(riid, &IID_IShellCopyHookW))
    {
        WriteToLog("SE_QueryInterface: IShellCopyHookW (mame, ale nedavame)");
        // *ppvObj = se->m_pCHW;
    }
#ifdef SHEXT_LOG_ENABLED
    else
    {
        WriteToLog("SE_QueryInterface: unsupported IID");
    }
#endif // SHEXT_LOG_ENABLED

    if (*ppvObj)
    {
        This->lpVtbl->AddRef(This);

        return NOERROR;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
SE_AddRef(THIS)
{
    ShellExt* se = (ShellExt*)This;
    return ++se->m_cRef;
}

STDMETHODIMP_(ULONG)
SE_Release(THIS)
{
    ShellExt* se = (ShellExt*)This;

    if (--se->m_cRef)
        return se->m_cRef;

    SE_Destructor(se);

    return 0;
}
