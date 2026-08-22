# Comprehensive Assessment of RAII and Smart Pointer Opportunities

## Executive Summary

An audit of the Open Salamander (`FileManager`) codebase reveals a substantial legacy footprint of manual resource management patterns written prior to modern C++ idioms. The codebase relies heavily on raw handles (`HANDLE`, `HBITMAP`, `HDC`, `HKEY`, `HINSTANCE`), manual memory management (`new`/`delete`, `malloc`/`free`, `HeapAlloc`/`HeapFree`, `CoTaskMemFree`), manual COM interface count management (`Release()`), and custom container templates (`TIndirectArray<T>`) that encode ownership outside the C++ type system.

While basic scoped kernel handle infrastructure (`CScopedKernelHandle`) has been recently introduced in selected paths, widespread manual cleanup remains across core application modules. This introduces risks of:
1. **Resource Leaks on Error Paths**: Early returns and thrown exceptions bypass downstream cleanup functions (`DeleteObject`, `ReleaseDC`, `CloseHandle`, `FindClose`, `FreeLibrary`, `CoTaskMemFree`).
2. **Double-Free and Use-After-Free Flaws**: Transfer of ownership across sub-routines using raw pointers without strict move-only semantics.
3. **Verbose Boilerplate**: Repetitive cleanup logic duplicated across multiple conditional exit branches.

The presence of the **Windows Implementation Library (WIL)** in `src/common/dep/wil/` alongside modern C++17/20 support provides an immediate, zero-overhead foundation for modernizing these patterns using `wil::unique_any`, `wil::com_ptr`, `std::unique_ptr`, `std::shared_ptr`, `std::vector`, and scope guards (`gsl::finally` / `wil::scope_exit`).

---

## Module-by-Module Detailed Findings and Modern C++ Implementations

---

### 1. Core Application & Win32 GUI Subsystem

#### Location
- `src/app_entry.cpp` (Lines 1535–1573, 1728–1740, 2200–2230)
- `src/bitmap.cpp` (Lines 36–48, 113–130)
- `src/gui_controls.cpp` (Lines 120–150)

#### Current Implementation Analysis
`src/app_entry.cpp` manages long-lived and temporary Win32 GDI objects (`HFONT`, `HBITMAP`, `HBRUSH`, `HDC`) using raw handles and manual destruction functions (`DeleteObject`, `ReleaseDC`).

In `src/app_entry.cpp` (Lines 2200–2230):
```cpp
// LEGACY: Temporary bitmap allocations with manual cleanup
if (!CreateToolbarBitmaps(HInstance, IDB_MENU, ...,
                          hTmpMaskBitmap, hTmpGrayBitmap, hTmpColorBitmap, ...))
    return FALSE;

HMenuMarkImageList = ImageList_Create(...);
ImageList_Add(HMenuMarkImageList, hTmpColorBitmap, hTmpMaskBitmap);
HANDLES(DeleteObject(hTmpMaskBitmap));
HANDLES(DeleteObject(hTmpGrayBitmap));
HANDLES(DeleteObject(hTmpColorBitmap));

if (!CreateToolbarBitmaps(..., hTmpMaskBitmap, hTmpGrayBitmap, hTmpColorBitmap, ...))
    return FALSE; // Leak if previous step allocated resources or failed here!

HHotMenuImageList = ImageList_Create(...);
HGrayMenuImageList = ImageList_Create(...);
// Manual DeleteObject repeated...
if (HHotMenuImageList == NULL || HGrayMenuImageList == NULL)
    return FALSE; // Temporary GDI bitmaps leak if image list creation returns NULL!
```

In `src/bitmap.cpp` (Lines 113–130):
```cpp
// LEGACY: Manual boolean tracking for ReleaseDC
BOOL releaseDC = FALSE;
if (hDC == NULL)
{
    hDC = HANDLES(GetDC(NULL));
    if (hDC == NULL)
        return FALSE;
    releaseDC = TRUE;
}
HBmp = HANDLES(CreateCompatibleBitmap(hDC, width, height));
if (HBmp == NULL)
{
    if (releaseDC)
        HANDLES(ReleaseDC(NULL, hDC)); // Repeated manual cleanup on error path
    return FALSE;
}
if (releaseDC)
    HANDLES(ReleaseDC(NULL, hDC));
```

#### Suggested Modern C++ Implementation
Replace raw GDI handles with RAII wrappers `wil::unique_hbitmap`, `wil::unique_hdc_window`, `wil::unique_hfont`, and `wil::unique_select_object`:

```cpp
// MODERN C++ / RAII:
#include <wil/resource.h>

// 1. Temporary Bitmaps using wil::unique_hbitmap
wil::unique_hbitmap hMaskBmp, hGrayBmp, hColorBmp;
if (!CreateToolbarBitmaps(HInstance, IDB_MENU, ...,
                          hMaskBmp.put(), hGrayBmp.put(), hColorBmp.put(), ...))
    return FALSE;

HMenuMarkImageList = ImageList_Create(menuIconSize, menuIconSize, ILC_MASK | ILC_COLORDDB, 2, 1);
ImageList_Add(HMenuMarkImageList, hColorBmp.get(), hMaskBmp.get());
// Automatic DeleteObject on hMaskBmp, hGrayBmp, hColorBmp destruction even on early returns!

// 2. Device Context Management using wil::unique_hdc_window or scoped DC guard
auto dc = (hDC != NULL) ? wil::unique_hdc_window() : wil::GetDC(NULL);
HBITMAP hBmp = HANDLES(CreateCompatibleBitmap(dc.get(), width, height));
if (!hBmp)
    return FALSE; // ReleaseDC called automatically by dc destructor
```

---

### 2. File Operations, Async Copy & File System Engine

#### Location
- `src/async_copy.cpp` (Lines 125–153, 379–410, 550–600)
- `src/safefile.cpp` (Lines 103, 171, 799–803, 819–825)
- `src/fileswindow_delete.cpp` (Lines 25–50)

#### Current Implementation Analysis
`src/async_copy.cpp` allocates copy buffers with `malloc` and manages event synchronization handles manually. In `IsDirectoryEmpty` and security check functions, raw search handles (`HANDLE`) and token structures are managed with manual `FindClose`, `CloseHandle`, and `HeapFree` calls scattered across multiple early `return` paths.

In `src/async_copy.cpp` (Lines 125–153 & 379–410):
```cpp
// LEGACY: IsDirectoryEmpty manual FindClose cleanup on early returns
HANDLE search;
CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
search = dirW != NULL ? HANDLES_Q(FindFirstFileW(dirW, &fileData)) : INVALID_HANDLE_VALUE;
if (search != INVALID_HANDLE_VALUE)
{
    do {
        if (...) {
            HANDLES(FindClose(search)); // Manual FindClose before return
            return FALSE;
        }
    } while (FindNextFileW(search, &fileData));
    HANDLES(FindClose(search)); // Duplicated FindClose
}
```

In `src/async_copy.cpp` (Lines 550–600 - `IsUserAdmin`):
```cpp
// LEGACY: Security Token cleanup duplicated 4 times
if (!GetTokenInformation(...))
{
    HeapFree(GetProcessHeap(), 0, TokenGroupList);
    FreeSid(AdminsDomainSid);
    CloseHandle(hUserToken);
    return FALSE;
}
```

In `src/fileswindow_delete.cpp` (Lines 25–50):
```cpp
// LEGACY: Raw malloc allocation and raw COM Release
WCHAR* pathW = (WCHAR*)malloc(pathLength * sizeof(WCHAR));
if (pathW == NULL)
    return E_OUTOFMEMORY;
...
IShellItem* item = NULL;
HRESULT result = SHCreateItemFromParsingName(pathW, NULL, IID_PPV_ARGS(&item));
free(pathW);
if (FAILED(result))
    return result; // item leaks if initialized prior or on throw!
result = operation->DeleteItem(item, NULL);
item->Release();
```

#### Suggested Modern C++ Implementation
Utilize `wil::unique_hfind`, `wil::unique_handle`, `wil::com_ptr`, and `std::vector`/`std::unique_ptr<BYTE[]>`:

```cpp
// MODERN C++ / RAII:
#include <wil/resource.h>
#include <wil/com.h>
#include <vector>

// 1. Directory enumeration with wil::unique_hfind
CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
WIN32_FIND_DATAW fileData;
wil::unique_hfind search(dirW != NULL ? FindFirstFileW(dirW, &fileData) : INVALID_HANDLE_VALUE);
if (search)
{
    do {
        if (IsUserFile(fileData))
            return FALSE; // FindClose called automatically by search destructor!
    } while (FindNextFileW(search.get(), &fileData));
}
return TRUE;

// 2. Safe memory buffer and COM item management in Delete Items
std::vector<WCHAR> pathW(pathLength);
// or std::wstring pathW(pathLength, L'\0');
wil::com_ptr<IShellItem> item;
HRESULT hr = SHCreateItemFromParsingName(pathW.data(), NULL, IID_PPV_ARGS(&item));
if (FAILED(hr))
    return hr;

HRESULT result = operation->DeleteItem(item.get(), NULL);
// item released automatically by wil::com_ptr destructor
```

---

### 3. Plug-in Host & Extension Loading Subsystem

#### Location
- `src/plugins_loading.cpp` (Lines 1086–1099, 1676–1759, 2144–2226, 2523–3465)
- `src/plugins/7zip/7zip.cpp` (Lines 649–670, 891–966)

#### Current Implementation Analysis
`CSalamanderPluginEntry` and `CPluginData` manage plugin module handles (`HINSTANCE DLL`), language module handles (`HLanguage`), icon list instances (`CIconList*`), and viewer mask items (`CViewerMasksItem*`) via raw pointers and manual lifetime state flags.

In `src/plugins_loading.cpp` (Lines 1676–1702):
```cpp
// LEGACY: Manual object destruction and raw allocation tracking
delete p->PluginIcons;
p->PluginIcons = NULL;
delete p->PluginIconsGray;
p->PluginIconsGray = NULL;

p->PluginIcons = new CIconList();
if (p->PluginIcons != NULL)
{
    p->PluginIconsGray = new CIconList();
    if (p->PluginIconsGray != NULL && !p->PluginIconsGray->CreateAsCopy(p->PluginIcons, TRUE))
    {
        delete p->PluginIconsGray;
        p->PluginIconsGray = NULL; // Rollback boilerplate
    }
}
```

In `src/plugins/7zip/7zip.cpp` (Lines 891–966 - `UnpackWholeArchive`):
```cpp
// LEGACY: Multiple dynamically allocated raw objects with complex manual unwind
CSalamanderDirectoryAbstract* dir = SalamanderGeneral->AllocSalamanderDirectory(FALSE);
C7zClient* client = new C7zClient();
CPluginDataInterface* pluginData = new CPluginDataInterface(client);

if (!client->ListArchive(...))
{
    delete (CPluginDataInterface*)pluginData;
    delete client;
    SalamanderGeneral->FreeSalamanderDirectory(dir);
    return FALSE; // Complex manual cleanup path required for every exit point
}
```

#### Suggested Modern C++ Implementation
Modernize module lifetime management using `wil::unique_hmodule` and use `std::unique_ptr` for plugin interface objects:

```cpp
// MODERN C++ / RAII:
#include <wil/resource.h>
#include <memory>

// 1. Automatic DLL and Language Module lifetime
class CPluginData {
private:
    wil::unique_hmodule m_dll;
    wil::unique_hmodule m_langModule;
    std::unique_ptr<CIconList> m_pluginIcons;
    std::unique_ptr<CIconList> m_pluginIconsGray;
public:
    BOOL InitDLL(const char* path) {
        m_dll.reset(LoadLibraryUtf8(path));
        return m_dll.is_valid();
    } // Automatic FreeLibrary on destruction or re-assignment
};

// 2. Scoped archive client and directory management
auto dirDeleter = [](CSalamanderDirectoryAbstract* d) {
    if (d) SalamanderGeneral->FreeSalamanderDirectory(d);
};
std::unique_ptr<CSalamanderDirectoryAbstract, decltype(dirDeleter)> dir(
    SalamanderGeneral->AllocSalamanderDirectory(FALSE), dirDeleter);

auto client = std::make_unique<C7zClient>();
auto pluginData = std::make_unique<CPluginDataInterface>(client.get());

// All resources (dir, client, pluginData) cleanly unwound upon return or exception!
```

---

### 4. Registry, Configuration & State Persistence Subsystem

#### Location
- `src/regwork.cpp` (Lines 146–159, 182–225, 591–737)
- `src/mainwnd_config.cpp` (Lines 450–520)

#### Current Implementation Analysis
`src/regwork.cpp` defines procedural wrappers around Windows Registry operations (`ClearKeyAux`, `CreateKeyAux`, `OpenKeyAux`). Registry keys (`HKEY`) are handled as raw primitive handles with manual `RegCloseKey` calls.

In `src/regwork.cpp` (Lines 146–159):
```cpp
// LEGACY: Raw HKEY handle management with manual RegCloseKey
BOOL ClearKeyAux(HKEY key)
{
    char name[MAX_PATH];
    HKEY subKey;
    while (RegEnumKey(key, 0, name, MAX_PATH) == ERROR_SUCCESS)
    {
        if (RegOpenKeyEx(key, name, 0, KEY_READ | KEY_WRITE, &subKey) == ERROR_SUCCESS)
        {
            BOOL ret = ClearKeyAux(subKey);
            HANDLES(RegCloseKey(subKey)); // Manual handle close
            if (!ret || RegDeleteKey(key, name) != ERROR_SUCCESS)
                return FALSE;
        }
    }
    return TRUE;
}
```

#### Suggested Modern C++ Implementation
Adopt `wil::unique_hkey` for registry key ownership:

```cpp
// MODERN C++ / RAII:
#include <wil/registry.h>

BOOL ClearKeyAux(HKEY key)
{
    char name[MAX_PATH];
    while (RegEnumKey(key, 0, name, MAX_PATH) == ERROR_SUCCESS)
    {
        wil::unique_hkey subKey;
        if (RegOpenKeyEx(key, name, 0, KEY_READ | KEY_WRITE, subKey.put()) == ERROR_SUCCESS)
        {
            if (!ClearKeyAux(subKey.get()) || RegDeleteKey(key, name) != ERROR_SUCCESS)
                return FALSE; // subKey automatically closed on exit
        }
    }
    return TRUE;
}
```

---

### 5. Shell Integration, OLE/COM & Windows APIs

#### Location
- `src/execute.cpp` (Lines 2285–2301)
- `src/file_enumeration.cpp` (Lines 2009–2075)
- `src/drivelst.cpp` (Lines 1490–1510)
- `src/common/strutils.cpp` (Lines 126–130)

#### Current Implementation Analysis
Calls to Shell APIs such as `SHGetKnownFolderPath`, `SHCreateItemFromParsingName`, and `IFileOpenDialog` allocate CoTask memory buffers (`PWSTR`) and COM interface pointers (`IShellItem*`, `IFileOpenDialog*`). Cleanup relies on explicit `CoTaskMemFree` and `->Release()` calls duplicated across error check branches.

In `src/execute.cpp` (Lines 2285–2301 - `BrowseCommand`):
```cpp
// LEGACY: Manual COM interface release and CoTaskMemFree
PWSTR resultPathW = NULL;
if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &resultPathW)))
{
    ...
    free(resultPath);
    CoTaskMemFree(resultPathW); // Manual CoTaskMemFree
    item->Release();            // Manual COM release
    dialog->Release();          // Manual COM release
}
```

In `src/drivelst.cpp` (Lines 1490–1510 - `InitOneDrivePath`):
```cpp
// LEGACY: Raw pointer from SHGetKnownFolderPath
PWSTR path = NULL;
if (DynSHGetKnownFolderPath(my_FOLDERID_SkyDrive, 0, NULL, &path) == S_OK && path != NULL)
{
    ...
    CoTaskMemFree(path); // Manual free required
}
```

#### Suggested Modern C++ Implementation
Use `wil::unique_cotaskmem_string` and `wil::com_ptr<T>`:

```cpp
// MODERN C++ / RAII:
#include <wil/com.h>
#include <wil/resource.h>

// 1. Known folder path using wil::unique_cotaskmem_string
wil::unique_cotaskmem_string path;
if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SkyDrive, 0, NULL, path.put())))
{
    ConvertU2A(path.get(), -1, OneDrivePath, _countof(OneDrivePath));
} // CoTaskMemFree called automatically by path destructor

// 2. COM Dialog handling using wil::com_ptr
wil::com_ptr<IFileOpenDialog> dialog;
THROW_IF_FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)));

wil::com_ptr<IShellItem> item;
if (SUCCEEDED(dialog->GetResult(&item)))
{
    wil::unique_cotaskmem_string resultPathW;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, resultPathW.put())))
    {
        // Use resultPathW.get()
    }
} // item and dialog released automatically, resultPathW freed automatically!
```

---

### 6. Archive & Compression Processing Engine

#### Location
- `src/zip.cpp` (Lines 110–180)
- `src/zip_general_api.cpp` (Lines 210–290)
- `src/salbzip2.cpp` (Lines 80–140)

#### Current Implementation Analysis
Archive routines allocate dynamic memory buffers for compression state, stream headers, and temporary decompression blocks using raw `new BYTE[]` or `malloc`. Deallocation is handled procedurally at the end of functions. If stream corruption or CRC verification throws an exception or returns early, memory buffers leak.

In `src/salbzip2.cpp` (Lines 80–140):
```cpp
// LEGACY: Raw buffer allocation for decompression
char* outBuffer = (char*)malloc(decompressedSize);
if (outBuffer == NULL)
    return FALSE;

if (!DecompressBlock(inBuffer, outBuffer))
{
    free(outBuffer); // Manual free on error path
    return FALSE;
}
// Process outBuffer...
free(outBuffer); // Duplicated free on success path
```

#### Suggested Modern C++ Implementation
Replace raw buffers with `std::vector<BYTE>` or `std::unique_ptr<BYTE[]>`:

```cpp
// MODERN C++ / RAII:
#include <vector>
#include <memory>

// std::vector handles dynamic allocation, capacity, and guaranteed deallocation
std::vector<char> outBuffer(decompressedSize);
if (!DecompressBlock(inBuffer, outBuffer.data()))
    return FALSE; // Buffer automatically freed on error return

// Process outBuffer.data()...
```

---

### 7. Custom Containers, Data Structures & Infrastructure

#### Location
- `src/common/array.h` (Lines 210–290)
- `src/common/commonarray.cpp` (Lines 45–120)
- `src/cache.cpp` (Lines 1512–1540)

#### Current Implementation Analysis
The codebase relies heavily on legacy container templates `TIndirectArray<T>` and `TDirectArray<T>`. `TIndirectArray<T>` stores raw `void*` elements and encodes element ownership dynamically at runtime via a `CDeleteType` enum flag (`dtDelete` vs `dtNoDelete`).

In `src/common/array.h` (Lines 218–260):
```cpp
// LEGACY: Dynamic ownership flag checked at runtime
class TIndirectArray : public CArray
{
    virtual ~TIndirectArray() { Destroy(); }
    void Destroy() {
        if (DeleteType == dtDelete) {
            for (int i = 0; i < Count; i++)
                delete (DATA_TYPE*)At(i); // Manual loop deletion
        }
    }
};
```

#### Suggested Modern C++ Implementation
Transition container usage from `TIndirectArray<T>` to standard standard library containers:
- **Owning collection**: `std::vector<std::unique_ptr<T>>` or `std::vector<T>`
- **Non-owning collection (view/borrowed)**: `std::vector<T*>` or `std::span<T*>` / `std::vector<std::reference_wrapper<T>>`

```cpp
// MODERN C++ / RAII:
#include <vector>
#include <memory>

// Replacing TIndirectArray<CArchiveItemInfo> (dtDelete) with std::vector<std::unique_ptr<CArchiveItemInfo>>:
std::vector<std::unique_ptr<CArchiveItemInfo>> itemList;
itemList.reserve(itemCount);

auto aii = std::make_unique<CArchiveItemInfo>(name, fileData, isDir);
itemList.push_back(std::move(aii));

// Iteration using modern C++ range-for:
for (const auto& item : itemList)
{
    ProcessItem(item.get());
}
// Clearing or destroying itemList automatically destructs all owned CArchiveItemInfo instances safely!
```

---

## Refactoring Priority & Impact Matrix

| Priority | Module / Subsystem | Primary Files | Target Resource Types | Modern C++ RAII Replacement | Impact / Risk Reduction |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **P1** | **Shell Integration & COM** | `execute.cpp`, `file_enumeration.cpp`, `drivelst.cpp` | `IShellItem*`, `IFileOpenDialog*`, `PWSTR` (CoTaskMem) | `wil::com_ptr<T>`, `wil::unique_cotaskmem_string` | Eliminates COM reference count leaks and memory leaks in file dialogs. High safety gain. |
| **P1** | **File Ops & Async Engine** | `async_copy.cpp`, `safefile.cpp`, `fileswindow_delete.cpp` | `HANDLE` (files, events, search), security tokens | `wil::unique_hfind`, `wil::unique_handle`, `CScopedKernelHandle` | Prevents file handle leaks during file operations, background copy, and delete errors. High safety gain. |
| **P2** | **Win32 GUI & GDI** | `app_entry.cpp`, `bitmap.cpp`, `gui_controls.cpp` | `HBITMAP`, `HDC`, `HFONT`, `HBRUSH` | `wil::unique_hbitmap`, `wil::unique_hdc_window`, `wil::unique_select_object` | Prevents GDI handle resource exhaustion on long-running GUI sessions. Medium safety gain. |
| **P2** | **Plug-in Host & Loading** | `plugins_loading.cpp`, `7zip.cpp` | `HINSTANCE` (DLLs, SLGs), raw plugin objects | `wil::unique_hmodule`, `std::unique_ptr<T>` | Eliminates DLL handle leaks and simplifies complex plugin initialization unwind paths. Medium safety gain. |
| **P3** | **Registry & Config** | `regwork.cpp`, `mainwnd_config.cpp` | `HKEY` | `wil::unique_hkey` | Simplifies recursive registry operations and guarantees key closure on error. Low/Medium safety gain. |
| **P3** | **Containers & Core Data** | `array.h`, `commonarray.cpp`, `cache.cpp` | `TIndirectArray<T>`, raw buffer allocations | `std::vector<std::unique_ptr<T>>`, `std::vector<BYTE>` | Replaces dynamic runtime ownership flags with compile-time type safety. Medium effort, high long-term code clarity. |

---

## Conclusion & Next Steps

The Open Salamander codebase possesses an excellent native helper library with **WIL** (`src/common/dep/wil/`) already bundled. By systematically introducing WIL smart handle wrappers (`wil::unique_hbitmap`, `wil::unique_hdc_window`, `wil::unique_hfind`, `wil::unique_hkey`, `wil::unique_hmodule`, `wil::com_ptr`) and C++ standard library smart pointers (`std::unique_ptr`, `std::vector`), the codebase can achieve exception-safe, self-cleaning resource management while eliminating thousands of lines of fragile error-cleanup boilerplate.
