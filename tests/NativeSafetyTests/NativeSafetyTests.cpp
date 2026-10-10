// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include <stdint.h>
#include <stdio.h>
#include <string>
#include "../../src/common/resource_strings_utf8.h" // exercise the production resource loader with real multilingual resources
#include "../../src/common/network_resources_utf8.h" // verify the exact provider/cache conversion used by net: enumeration
#include "../../src/common/unicode_shell_link.h" // exercise production Unicode layout and COM/system text boundaries
#include "../../src/common/utf8_control_text.h" // verify actual control text independently of the host ANSI code page
#include "../../src/common/utf8_menu_text.h" // Exercise menu storage and rasterized captions through the production UTF-8 boundary.
#pragma comment(lib, "comctl32.lib") // Exercise the real list-view display-notification boundary.

#include "../../src/common/checked_arithmetic.h"
#include "../../src/operation_execution_filesystem.h"
#include "../../src/common/scoped_readonly_file.h"
#include "../../src/common/stable_move_source.h" // exercise the exact source-ownership contract used by moves
#include "../../src/common/conditional_file_publication.h" // execute the production race boundary on real files
#include "../../src/common/operation_recovery.h" // exercise claimed journals and verified recovery on real files
#include "../../src/common/configuration_payload.h" // reject incomplete payloads even when later writes succeed
#include "../../src/common/ftp_transactional_download.h" // share the actual FTP staging and durable completion boundary
#include "../../src/common/file_close_completion.h"
#include <future>
#include <winioctl.h>
#include <sddl.h>

namespace
{
int Fail(const char* message)
{
    fprintf(stderr, "NativeSafetyTests: %s\n", message);
    return 1;
}

int TestCheckedArithmeticBoundaries()
{
    uint64_t value = 0;
    DWORD dword = 0;
    size_t size = 0;

    // These native boundary cases prevent a future caller from accepting an
    // allocation or Win32 byte count that has silently wrapped.
    if (!CheckedAddUInt64(UINT64_MAX - 1, 1, &value) || value != UINT64_MAX)
        return Fail("CheckedAddUInt64 rejected a valid boundary sum");
    if (CheckedAddUInt64(UINT64_MAX, 1, &value))
        return Fail("CheckedAddUInt64 accepted an overflowing sum");
    if (!CheckedMultiplyUInt64(UINT64_MAX, 1, &value) || value != UINT64_MAX)
        return Fail("CheckedMultiplyUInt64 rejected a valid boundary product");
    if (CheckedMultiplyUInt64(UINT64_MAX, 2, &value))
        return Fail("CheckedMultiplyUInt64 accepted an overflowing product");
    if (!CheckedCastUInt64ToDword(MAXDWORD, &dword) || dword != MAXDWORD)
        return Fail("CheckedCastUInt64ToDword rejected MAXDWORD");
    if (CheckedCastUInt64ToDword((uint64_t)MAXDWORD + 1, &dword))
        return Fail("CheckedCastUInt64ToDword accepted a truncated value");
    if (!CheckedCastUInt64ToSize(1, &size) || size != 1)
        return Fail("CheckedCastUInt64ToSize rejected a valid value");
    return 0;
}

int TestResourceStringsUtf8()
{
    const HINSTANCE instance = GetModuleHandleW(NULL);
    // Resource boundaries cover toolbar hints and pending-network labels as well as menu captions.
    const char* expected[] = {u8"&Utw\u00f3rz katalog...\tF7", u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144", "&Create Directory...\tF7",
                              u8"Przeszukiwanie sieci\u2026", u8"Odzyskiwanie usuni\u0119tych plik\u00f3w"};
    for (int index = 0; index < static_cast<int>(_countof(expected)); ++index)
    {
        const int bytes = static_cast<int>(strlen(expected[index]));
        if (LoadStringUtf8(instance, 4001 + index, NULL, 0) != bytes)
            return Fail("resource loader measured characters instead of UTF-8 bytes");
        std::string buffer(bytes + 2, '#');
        // A buffer without terminator space must fail without publishing a partial character or overwriting its sentinel.
        if (LoadStringUtf8(instance, 4001 + index, &buffer[0], bytes) != 0 || buffer[0] != 0 || buffer[bytes] != '#')
            return Fail("resource loader accepted or overran an undersized UTF-8 destination");
        if (LoadStringUtf8(instance, 4001 + index, &buffer[0], bytes + 1) != bytes ||
            strcmp(buffer.c_str(), expected[index]) != 0 || buffer[bytes + 1] != '#')
            return Fail("resource loader corrupted a localized label or its terminator boundary");
    }
    char missing[] = "sentinel";
    if (LoadStringUtf8(instance, 4999, missing, sizeof(missing)) != 0 || missing[0] != 0)
        return Fail("missing resources left stale display text in the destination");
    return 0;
}

struct CUtf8ListTestState
{
    const char* Text;
    bool Copied;
    bool Notified;
};

LRESULT CALLBACK Utf8ListTestWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NOTIFY && ((NMHDR*)lParam)->code == LVN_GETDISPINFOW)
    {
        // Exercise both provider styles: text copied into the request and a returned persistent pointer.
        CUtf8ListTestState* state = (CUtf8ListTestState*)GetWindowLongPtrW(window, GWLP_USERDATA);
        CUtf8ListViewDispInfo display(lParam);
        if (state->Copied)
            strcpy_s(display.Get()->item.pszText, display.Get()->item.cchTextMax, state->Text);
        else
            display.Get()->item.pszText = (char*)state->Text;
        state->Notified = true;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int TestNativeControlTextUtf8()
{
    // Exercise formatted captions, checkbox labels, lists and saved captions through real Unicode controls.
    HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 200, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (window == NULL)
        return Fail("could not create UTF-8 control test window");
    HWND checkbox = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 200, 20, window, (HMENU)1, NULL, NULL);
    HWND list = CreateWindowExW(0, L"LISTBOX", L"", WS_CHILD | LBS_HASSTRINGS, 0, 20, 200, 40, window, (HMENU)2, NULL, NULL);
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 0, 60, 200, 40, window, (HMENU)3, NULL, NULL);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    HWND cells = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | LVS_REPORT, 0, 0, 200, 100, window, (HMENU)4, NULL, NULL);
    CUtf8ListTestState state = {"", false, false};
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
    SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)Utf8ListTestWindowProc);
    SendMessageW(cells, LVM_SETUNICODEFORMAT, TRUE, 0);
    LVITEMW callbackItem = {};
    callbackItem.mask = LVIF_TEXT;
    callbackItem.pszText = LPSTR_TEXTCALLBACKW;
    bool valid = checkbox != NULL && list != NULL && combo != NULL && cells != NULL &&
                 SendMessageW(cells, LVM_INSERTITEMW, 0, (LPARAM)&callbackItem) == 0;
    const char* labels[] = {u8"&Kopiuj dane z zak\u0142adki \"Szybkie po\u0142\u0105czenie\"",
                            u8"Za\u017c\u00f3\u0142\u0107 g\u0119\u015bl\u0105 ja\u017a\u0144", u8"\u6771\u4eac \U0001f4c1"};
    for (int index = 0; valid && index < (int)_countof(labels); ++index)
    {
        WCHAR expected[200], actual[200];
        char captured[600];
        valid = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, labels[index], -1, expected, _countof(expected)) != 0;
        valid = valid && SendUtf8ControlString(window, WM_SETTEXT, 0, labels[index]) != 0 &&
                SendUtf8DialogControlString(window, 1, WM_SETTEXT, 0, labels[index]) != 0;
        GetWindowTextW(checkbox, actual, _countof(actual));
        valid = valid && wcscmp(actual, expected) == 0;
        valid = valid && LegacyControlTextToUtf8(labels[index]) == labels[index];
        for (int copied = 0; valid && copied < 2; ++copied)
        {
            state.Text = labels[index];
            state.Copied = copied != 0;
            state.Notified = false;
            LVITEMW cell = {};
            cell.pszText = actual;
            cell.cchTextMax = _countof(actual);
            SendMessageW(cells, LVM_GETITEMTEXTW, 0, (LPARAM)&cell);
            valid = state.Notified && wcscmp(actual, expected) == 0;
        }
        GetWindowTextW(window, actual, _countof(actual));
        valid = valid && wcscmp(actual, expected) == 0 &&
                ReadUtf8ControlText(window, captured, _countof(captured)) == (int)strlen(labels[index]) &&
                strcmp(captured, labels[index]) == 0;
        // Restoring a captured caption must not lose accents or supplementary characters.
        SendUtf8ControlString(checkbox, WM_SETTEXT, 0, captured);
        GetWindowTextW(checkbox, actual, _countof(actual));
        valid = valid && wcscmp(actual, expected) == 0;
        valid = valid && SendUtf8ControlString(list, LB_ADDSTRING, 0, labels[index]) == index &&
                SendUtf8ControlString(combo, CB_ADDSTRING, 0, labels[index]) == index;
        SendMessageW(list, LB_GETTEXT, index, (LPARAM)actual);
        valid = valid && wcscmp(actual, expected) == 0;
        SendMessageW(combo, CB_GETLBTEXT, index, (LPARAM)actual);
        valid = valid && wcscmp(actual, expected) == 0;
        char shortCaption[] = {'#', '#', '#'};
        valid = valid && ReadUtf8ControlText(window, shortCaption, 2) == 0 && shortCaption[0] == 0 && shortCaption[2] == '#';
        valid = valid && SendUtf8ControlString(checkbox, WM_SETTEXT, 0, "\xc5") == -1;
        GetWindowTextW(checkbox, actual, _countof(actual));
        valid = valid && wcscmp(actual, expected) == 0;
    }
    // Short display requests must clip at Unicode boundaries and leave the caller's guard untouched.
    WCHAR clipped[] = {L'#', L'#', L'#'};
    NMLVDISPINFOW shortRequest = {};
    shortRequest.hdr.code = LVN_GETDISPINFOW;
    shortRequest.item.mask = LVIF_TEXT;
    shortRequest.item.pszText = clipped;
    shortRequest.item.cchTextMax = 2;
    {
        CUtf8ListViewDispInfo display((LPARAM)&shortRequest);
        display.Get()->item.pszText = (char*)u8"\U0001f4c1x";
        display.Get()->item.iImage = 7;
    }
    valid = valid && clipped[0] == 0 && clipped[2] == L'#' && shortRequest.item.iImage == 7;
    // Old bookmark bytes are normalized before formatting, while their stored representation remains untouched.
    const char legacyName[] = "B\xe9";
    WCHAR legacyExpected[10], legacyActual[10];
    std::string normalized = LegacyControlTextToUtf8(legacyName);
    valid = valid && MultiByteToWideChar(CP_ACP, 0, legacyName, -1, legacyExpected, _countof(legacyExpected)) != 0 &&
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, normalized.c_str(), -1, legacyActual, _countof(legacyActual)) != 0 &&
            wcscmp(legacyActual, legacyExpected) == 0;
    DestroyWindow(window);
    return valid ? 0 : Fail("UTF-8 caption, checkbox, list or capture/restore text was corrupted");
}

int TestNativeMenusUtf8()
{
    // The FTP context menu inserts these commands into HMENU before the custom popup reads them back.
    const char* labels[] = {u8"&Podgl\u0105d\tF3", u8"Podgl\u0105d za pomoc\u0105...\tCtrl+Shift+F3",
                            u8"Przenie\u015b/Zmie\u0144 nazw\u0119...\tF6", u8"Usu\u0144\tF8",
                            u8"Zmie\u0144 atrybuty...\tCtrl+F2", u8"&Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1"};
    const wchar_t* expected[] = {L"&Podgl\u0105d\tF3", L"Podgl\u0105d za pomoc\u0105...\tCtrl+Shift+F3",
                                L"Przenie\u015b/Zmie\u0144 nazw\u0119...\tF6", L"Usu\u0144\tF8",
                                L"Zmie\u0144 atrybuty...\tCtrl+F2", L"&Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1"};
    HMENU menu = CreatePopupMenu();
    HDC actualDC = CreateCompatibleDC(NULL), expectedDC = CreateCompatibleDC(NULL);
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = 768;
    bitmapInfo.bmiHeader.biHeight = -64;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    void *actualPixels = NULL, *expectedPixels = NULL;
    HBITMAP actualBitmap = CreateDIBSection(actualDC, &bitmapInfo, DIB_RGB_COLORS, &actualPixels, NULL, 0);
    HBITMAP expectedBitmap = CreateDIBSection(expectedDC, &bitmapInfo, DIB_RGB_COLORS, &expectedPixels, NULL, 0);
    bool valid = menu != NULL && actualDC != NULL && expectedDC != NULL && actualBitmap != NULL && expectedBitmap != NULL;
    HGDIOBJ oldActual = SelectObject(actualDC, actualBitmap), oldExpected = SelectObject(expectedDC, expectedBitmap);
    SelectObject(actualDC, GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(expectedDC, GetStockObject(DEFAULT_GUI_FONT));
    RECT canvas = {0, 0, 768, 64};
    for (int index = 0; valid && index < _countof(labels); ++index)
    {
        MENUITEMINFOA item = {};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_STATE | MIIM_DATA |
                     (index % 2 == 0 ? MIIM_STRING | MIIM_FTYPE | MIIM_BITMAP : MIIM_TYPE);
        item.fType = MFT_RADIOCHECK;
        item.fState = MFS_CHECKED;
        item.wID = 100 + index;
        item.dwItemData = 0x12345678;
        item.hbmpItem = index % 2 == 0 ? HBMMENU_CALLBACK : NULL;
        item.dwTypeData = const_cast<char*>(labels[index]);
        // Deliberately wrong byte count: insertion consumes a terminated caption, not an ACP character count.
        item.cch = 1;
        valid = InsertMenuItemUtf8(menu, index, TRUE, &item) != FALSE;
        wchar_t nativeText[256] = {};
        MENUITEMINFOW native = {};
        native.cbSize = sizeof(native);
        native.fMask = item.fMask;
        native.dwTypeData = nativeText;
        native.cch = _countof(nativeText);
        std::string restored;
        valid = valid && GetMenuItemInfoW(menu, index, TRUE, &native) && wcscmp(nativeText, expected[index]) == 0 &&
                native.wID == item.wID && native.fState == item.fState && native.fType == item.fType &&
                native.dwItemData == item.dwItemData && native.hbmpItem == item.hbmpItem &&
                GetMenuItemTextUtf8(menu, index, TRUE, restored) && restored == labels[index];

        for (int capacity = 1; capacity <= static_cast<int>(strlen(labels[index])) + 2; ++capacity)
        {
            char clipped[256];
            memset(clipped, '#', sizeof(clipped));
            CopyMenuTextUtf8(clipped, capacity, labels[index]);
            std::wstring decoded;
            valid = valid && clipped[capacity] == '#' && Utf8TextToWide(clipped, -1, decoded) &&
                    std::wstring(expected[index]).compare(0, decoded.size(), decoded) == 0;
        }

        // Compare actual pixels and measured bounds with Unicode GDI, including counted columns and hidden mnemonics.
        const char* tab = strchr(labels[index], '\t');
        int length = tab == NULL ? static_cast<int>(strlen(labels[index])) : static_cast<int>(tab - labels[index]);
        const wchar_t* wideTab = wcschr(expected[index], L'\t');
        int wideLength = wideTab == NULL ? static_cast<int>(wcslen(expected[index])) : static_cast<int>(wideTab - expected[index]);
        for (UINT flags : {UINT(DT_SINGLELINE | DT_HIDEPREFIX), UINT(DT_SINGLELINE | DT_NOPREFIX)})
        {
            RECT measuredActual = {}, measuredExpected = {};
            int actualHeight = DrawMenuTextUtf8(actualDC, labels[index], length, &measuredActual, flags | DT_CALCRECT);
            int expectedHeight = DrawTextW(expectedDC, expected[index], wideLength, &measuredExpected, flags | DT_CALCRECT);
            valid = valid && actualHeight == expectedHeight && EqualRect(&measuredActual, &measuredExpected);
            FillRect(actualDC, &canvas, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            FillRect(expectedDC, &canvas, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            RECT actualRect = canvas, expectedRect = canvas;
            DrawMenuTextUtf8(actualDC, labels[index], length, &actualRect, flags);
            DrawTextW(expectedDC, expected[index], wideLength, &expectedRect, flags);
            GdiFlush();
            valid = valid && memcmp(actualPixels, expectedPixels, 768 * 64 * 4) == 0;
        }
    }
    std::string longCaption;
    for (int index = 0; index < 2500; ++index)
        longCaption += u8"\u0142\u6771";
    valid = valid && AppendMenuUtf8(menu, MF_STRING, 500, longCaption.c_str());
    std::string restored;
    valid = valid && GetMenuItemTextUtf8(menu, 500, FALSE, restored) && restored == longCaption &&
            ModifyMenuUtf8(menu, 500, MF_BYCOMMAND | MF_STRING | MF_GRAYED, 500, labels[3]) &&
            GetMenuItemTextUtf8(menu, 500, FALSE, restored) && restored == labels[3];
    MENUITEMINFOA replacement = {};
    replacement.cbSize = sizeof(replacement);
    replacement.fMask = MIIM_STRING;
    replacement.dwTypeData = const_cast<char*>(labels[4]);
    valid = valid && SetMenuItemInfoUtf8(menu, 500, FALSE, &replacement) &&
            GetMenuItemTextUtf8(menu, 500, FALSE, restored) && restored == labels[4];
    int count = GetMenuItemCount(menu);
    replacement.dwTypeData = const_cast<char*>("\xc5");
    valid = valid && !SetMenuItemInfoUtf8(menu, 500, FALSE, &replacement) &&
            !AppendMenuUtf8(menu, MF_STRING, 501, "\xc5") && GetMenuItemCount(menu) == count &&
            GetMenuItemTextUtf8(menu, 500, FALSE, restored) && restored == labels[4];
    HMENU submenu = CreatePopupMenu();
    valid = valid && InsertMenuUtf8(menu, 0, MF_BYPOSITION | MF_POPUP | MF_CHECKED, reinterpret_cast<UINT_PTR>(submenu), labels[0]) &&
            GetSubMenu(menu, 0) == submenu && AppendMenuUtf8(menu, MF_SEPARATOR, 0, NULL) &&
            AppendMenuUtf8(menu, MF_OWNERDRAW, 502, reinterpret_cast<const char*>(0x76543210));
    MENUITEMINFOW owner = {};
    owner.cbSize = sizeof(owner);
    owner.fMask = MIIM_DATA | MIIM_FTYPE;
    valid = valid && GetMenuItemInfoW(menu, 502, FALSE, &owner) && owner.dwItemData == 0x76543210 && (owner.fType & MFT_OWNERDRAW) != 0;
    SelectObject(actualDC, oldActual);
    SelectObject(expectedDC, oldExpected);
    DeleteObject(actualBitmap);
    DeleteObject(expectedBitmap);
    DeleteDC(actualDC);
    DeleteDC(expectedDC);
    DestroyMenu(menu);
    return valid ? 0 : Fail("native menus corrupted UTF-8 captions, metadata, measured widths or rendered pixels");
}

int TestNetworkResourcesUtf8()
{
    // Names returned by WNetEnumResourceW must remain navigable after caching and converting back for WNetOpenEnumW.
    WCHAR remote[] = L"Ca\u0142a sie\u0107";
    WCHAR comment[] = L"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1";
    WCHAR provider[] = L"Sie\u0107 Microsoft Windows";
    WCHAR local[] = L"";
    NETRESOURCEW source = {RESOURCE_GLOBALNET, RESOURCETYPE_DISK, RESOURCEDISPLAYTYPE_NETWORK,
                          RESOURCEUSAGE_CONTAINER, local, remote, comment, provider};
    CNetworkResourceUtf8 utf8(source);
    if (utf8.Error != NO_ERROR || strcmp(utf8.Resource.lpRemoteName, u8"Ca\u0142a sie\u0107") != 0 ||
        strcmp(utf8.Resource.lpComment, u8"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1") != 0 ||
        strcmp(utf8.Resource.lpProvider, u8"Sie\u0107 Microsoft Windows") != 0 ||
        utf8.Resource.lpLocalName == NULL || utf8.Resource.lpLocalName[0] != 0)
        return Fail("network provider text was converted through ANSI or lost empty fields");
    remote[0] = L'X';
    CNetworkResourceWide wide(utf8.Resource);
    if (wide.Error != NO_ERROR || wcscmp(wide.Resource.lpRemoteName, L"Ca\u0142a sie\u0107") != 0 ||
        wcscmp(wide.Resource.lpComment, comment) != 0 || wcscmp(wide.Resource.lpProvider, provider) != 0 ||
        wide.Resource.dwScope != source.dwScope || wide.Resource.dwType != source.dwType ||
        wide.Resource.dwDisplayType != source.dwDisplayType || wide.Resource.dwUsage != source.dwUsage)
        return Fail("cached network text lost ownership, flags or its Unicode navigation identity");
    NETRESOURCEW empty = {};
    CNetworkResourceUtf8 nullFields(empty);
    if (nullFields.Error != NO_ERROR || nullFields.Resource.lpRemoteName != NULL ||
        nullFields.Resource.lpLocalName != NULL || nullFields.Resource.lpComment != NULL || nullFields.Resource.lpProvider != NULL)
        return Fail("network conversion changed absent fields into empty strings");
    char invalidUtf8[] = "\xc5";
    NETRESOURCEA invalid = {};
    invalid.lpRemoteName = invalidUtf8;
    CNetworkResourceWide rejected(invalid);
    WCHAR invalidUtf16[] = {0xd800, 0};
    empty.lpRemoteName = invalidUtf16;
    CNetworkResourceUtf8 rejectedWide(empty);
    if (rejected.Error != ERROR_NO_UNICODE_TRANSLATION || rejectedWide.Error != ERROR_NO_UNICODE_TRANSLATION)
        return Fail("network conversion accepted malformed text and changed its identity");
    return 0;
}

int TestUnicodeTextBoundaries()
{
    // Sweep real GDI widths: Polish, CJK and supplementary characters must stay valid through both ellipsis modes.
    HDC dc = CreateCompatibleDC(NULL);
    if (dc == NULL)
        return Fail("could not create the Unicode layout DC");
    const std::wstring path = L"C:\\Za\u017c\u00f3\u0142\u0107\\\u6771\u4eac\\\U0001f4c1plik.txt";
    SIZE originalSize, dots;
    GetTextExtentPoint32W(dc, path.c_str(), (int)path.size(), &originalSize);
    GetTextExtentPoint32W(dc, L"...", 3, &dots);
    bool valid = true;
    for (int width = 0; width <= originalSize.cx + 10; ++width)
    {
        for (int mode = 0; mode != 2; ++mode)
        {
            std::wstring clipped, decoded;
            std::string utf8;
            SIZE measured;
            if (!EllipsizeUnicodeText(dc, path, width, mode != 0, L'\\', clipped, measured) ||
                !WideTextToUtf8(clipped.c_str(), (int)clipped.size(), utf8) || !Utf8TextToWide(utf8.c_str(), -1, decoded) ||
                decoded != clipped || (width >= dots.cx && measured.cx > width) ||
                (width >= originalSize.cx && clipped != path))
                valid = false;
        }
    }
    // The full filename survives path shortening when it and the ellipsis fit.
    const std::wstring leaf = path.substr(path.rfind(L'\\'));
    SIZE leafSize, measured;
    GetTextExtentPoint32W(dc, leaf.c_str(), (int)leaf.size(), &leafSize);
    std::wstring clipped;
    if (!EllipsizeUnicodeText(dc, path, leafSize.cx + dots.cx, true, L'\\', clipped, measured) ||
        clipped.size() < leaf.size() || clipped.substr(clipped.size() - leaf.size()) != leaf)
        valid = false;
    const std::string prefix = u8"Sie\u0107: ";
    const std::string body = u8"Za\u017c\u00f3\u0142\u0107 \u6771\u4eac \U0001f4c1 plik";
    const std::string suffix = u8" - b\u0142\u0105d";
    const std::string source = prefix + body + suffix;
    for (int width = 0; width != 220; ++width)
    {
        for (int mode = 0; mode != 2; ++mode)
        {
            std::string truncated;
            std::wstring wide;
            if (!TruncateUtf8Substring(dc, source.c_str(), (int)prefix.size(), (int)body.size(), width, mode != 0, truncated) ||
                !Utf8TextToWide(truncated.c_str(), -1, wide) || truncated.compare(0, prefix.size(), prefix) != 0 ||
                truncated.size() < prefix.size() + suffix.size() ||
                truncated.substr(truncated.size() - suffix.size()) != suffix)
                valid = false;
        }
    }
    std::string zeroStart, emptyBody, longResult;
    std::string longText(12000, 'x');
    std::wstring rejected;
    if (!TruncateUtf8Substring(dc, body.c_str(), 0, (int)body.size(), 0, false, zeroStart) || zeroStart != "..." ||
        !TruncateUtf8Substring(dc, body.c_str(), 0, 0, 0, false, emptyBody) || emptyBody != body ||
        !TruncateUtf8Substring(dc, longText.c_str(), 0, (int)longText.size(), 120, false, longResult) || longText.size() != 12000 ||
        TruncateUtf8Substring(dc, body.c_str(), 3, (int)body.size() - 3, 120, false, longResult) ||
        Utf8TextToWide("\xc5", -1, rejected))
        valid = false;
    DeleteDC(dc);
    if (!valid)
        return Fail("Unicode ellipsis split a character, changed fixed text, or exceeded its measured width");

    std::string formatted;
    if (!FormatUtf8Pair(u8"Sie\u0107 %2!s! / %1!s!", u8"\u017b:\\", u8"\u6771\u4eac\U0001f4c1", formatted) ||
        formatted != u8"Sie\u0107 \u6771\u4eac\U0001f4c1 / \u017b:\\")
        return Fail("RDP indexed formatting lost Unicode or locale-specific insertion order");
    wchar_t* system = NULL;
    DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, ERROR_ACCESS_DENIED, 0, (wchar_t*)&system, 0, NULL);
    std::string error;
    std::wstring errorWide;
    bool converted = Win32ErrorTextUtf8(ERROR_ACCESS_DENIED, 0, error) && Utf8TextToWide(error.c_str(), -1, errorWide);
    std::wstring expected = count != 0 ? std::wstring(system, count) : L"";
    LocalFree(system);
    while (!expected.empty() && (expected.back() == L'\r' || expected.back() == L'\n'))
        expected.pop_back();
    if (count == 0 || !converted || errorWide != expected || Win32ErrorTextUtf8(0xdeadbeef, 0, error) || !error.empty())
        return Fail("system-error text used ACP, retained CRLF, or published stale text for an unknown code");
    return 0;
}

int TestUnicodeShellLinkTarget()
{
    // Use a real COM shortcut target; no network connection or target-file creation is needed for this boundary check.
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized))
        return Fail("could not initialize the shortcut COM test");
    IShellLinkW* link = NULL;
    HRESULT created = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&link);
    const wchar_t* path = L"C:\\Za\u017c\u00f3\u0142\u0107\\\u6771\u4eac\\\U0001f4c1.txt";
    char buffer[1024] = {};
    char shortBuffer[] = "sentinel";
    bool valid = SUCCEEDED(created) && SUCCEEDED(link->SetPath(path)) && ShellLinkTargetUtf8(link, buffer, sizeof(buffer));
    std::wstring decoded;
    valid = valid && Utf8TextToWide(buffer, -1, decoded) && decoded == path &&
            !ShellLinkTargetUtf8(link, shortBuffer, sizeof(shortBuffer)) && strcmp(shortBuffer, "sentinel") == 0;
    if (link != NULL)
        link->Release();
    CoUninitialize();
    return valid ? 0 : Fail("shortcut target lost Unicode or partially overwrote an undersized destination");
}

int TestNativeFileOperationCharacterization()
{
    wchar_t temporaryPath[MAX_PATH];
    wchar_t temporaryDirectory[MAX_PATH];
    const char payload[] = "native-characterization";
    const char* failure = NULL;

    // Keep the C++ characterization self-owned so destructive Win32 calls cannot escape the test directory.
    if (GetTempPathW(_countof(temporaryPath), temporaryPath) == 0 ||
        GetTempFileNameW(temporaryPath, L"nst", 0, temporaryDirectory) == 0 ||
        !DeleteFileW(temporaryDirectory) || !CreateDirectoryW(temporaryDirectory, NULL))
        return Fail("could not create the native characterization directory");

    const std::wstring source = std::wstring(temporaryDirectory) + L"\\source.txt";
    const std::wstring copied = std::wstring(temporaryDirectory) + L"\\copied.txt";
    const std::wstring renamed = std::wstring(temporaryDirectory) + L"\\renamed.txt";
    const std::wstring moved = std::wstring(temporaryDirectory) + L"\\moved.txt";
    HANDLE sourceHandle = CreateFileW(source.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD bytesWritten = 0;
    if (sourceHandle == INVALID_HANDLE_VALUE ||
        !WriteFile(sourceHandle, payload, sizeof(payload) - 1, &bytesWritten, NULL) ||
        bytesWritten != sizeof(payload) - 1)
    {
        failure = "could not create the native source file";
    }
    if (sourceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(sourceHandle);

    if (failure == NULL && !CopyFileW(source.c_str(), copied.c_str(), FALSE))
        failure = "native copy did not create the destination";
    if (failure == NULL && !MoveFileW(copied.c_str(), renamed.c_str()))
        failure = "native rename did not preserve the copied entry";
    if (failure == NULL && !MoveFileW(source.c_str(), moved.c_str()))
        failure = "native move did not relocate the source entry";
    if (failure == NULL && (!DeleteFileW(renamed.c_str()) || !DeleteFileW(moved.c_str())))
        failure = "native delete did not remove the moved and renamed entries";
    if (failure == NULL && (GetFileAttributesW(renamed.c_str()) != INVALID_FILE_ATTRIBUTES ||
                            GetFileAttributesW(moved.c_str()) != INVALID_FILE_ATTRIBUTES))
        failure = "native delete left a visible file entry";

    DeleteFileW(source.c_str());
    DeleteFileW(copied.c_str());
    DeleteFileW(renamed.c_str());
    DeleteFileW(moved.c_str());
    RemoveDirectoryW(temporaryDirectory);
    return failure != NULL ? Fail(failure) : 0;
}

// Test double that fails a chosen file-system phase with a given Win32 error.
class CPhaseFailingFileSystem : public COperationExecutionFileSystem
{
public:
    enum EFailingPhase
    {
        fpCreate,
        fpWrite,
        fpMetadata,
        fpFlush,
        fpReplace,
        fpMove,
        fpDelete,
        fpSuccess
    } FailingPhase;

    CPhaseFailingFileSystem(EFailingPhase failingPhase, DWORD failingError)
        : FailingPhase(failingPhase), FailingError(failingError), Calls(0) {}

    HANDLE CreateFile(const char*, DWORD, DWORD, DWORD, DWORD) override { return Fail(fpCreate) ? INVALID_HANDLE_VALUE : (HANDLE)1; }
    BOOL WriteFile(HANDLE, const void*, DWORD bytesToWrite, DWORD* bytesWritten, LPOVERLAPPED) override
    {
        if (Fail(fpWrite))
            return FALSE;
        if (bytesWritten != NULL)
            *bytesWritten = bytesToWrite;
        return TRUE;
    }
    BOOL SetFileTime(HANDLE, const FILETIME*, const FILETIME*, const FILETIME*) override { return !Fail(fpMetadata); }
    BOOL FlushFileBuffers(HANDLE) override { return !Fail(fpFlush); }
    BOOL ReplaceFile(const char*, const char*) override { return !Fail(fpReplace); }
    BOOL MoveFile(const char*, const char*) override { return !Fail(fpMove); }
    BOOL SetFileInformationByHandle(HANDLE, FILE_INFO_BY_HANDLE_CLASS, void*, DWORD) override { return !Fail(fpDelete); }

    int GetCalls() const { return Calls; }

private:
    BOOL Fail(EFailingPhase phase) const
    {
        ++Calls;
        if (FailingPhase != phase)
            return FALSE;
        SetLastError(FailingError);
        return TRUE;
    }

    DWORD FailingError;
    mutable int Calls;
};

CPhaseFailingFileSystem::EFailingPhase RunTransactionalFaultSequence(COperationExecutionFileSystem& fileSystem,
                                                                       BOOL useMoveCommit)
{
    DWORD bytesWritten = 0;
    FILE_DISPOSITION_INFO disposition = {TRUE};
    HANDLE target = fileSystem.CreateFile("temporary", GENERIC_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL);
    if (target == INVALID_HANDLE_VALUE)
        return CPhaseFailingFileSystem::fpCreate;
    if (!fileSystem.WriteFile(target, "x", 1, &bytesWritten, NULL) || bytesWritten != 1)
        return CPhaseFailingFileSystem::fpWrite;
    if (!fileSystem.SetFileTime(target, NULL, NULL, NULL))
        return CPhaseFailingFileSystem::fpMetadata;
    if (!fileSystem.FlushFileBuffers(target))
        return CPhaseFailingFileSystem::fpFlush;
    if (useMoveCommit)
    {
        if (!fileSystem.MoveFile("temporary", "target"))
            return CPhaseFailingFileSystem::fpMove;
    }
    else if (!fileSystem.ReplaceFile("target", "temporary"))
        return CPhaseFailingFileSystem::fpReplace;
    if (!fileSystem.SetFileInformationByHandle((HANDLE)1, FileDispositionInfo, &disposition, sizeof(disposition)))
        return CPhaseFailingFileSystem::fpDelete;
    return CPhaseFailingFileSystem::fpSuccess;
}

// Exercise actual Windows replacement, including Git-style read-only files and
// a sharing failure, so rollback and handle-based restoration are verified on disk.
int TestReadOnlyReplacement()
{
    wchar_t temporaryPath[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t stage[MAX_PATH];
    if (!GetTempPathW(_countof(temporaryPath), temporaryPath) ||
        !GetTempFileNameW(temporaryPath, L"rot", 0, target))
        return Fail("could not reserve read-only replacement target");
    if (!GetTempFileNameW(temporaryPath, L"ros", 0, stage))
    {
        DeleteFileW(target);
        return Fail("could not reserve read-only replacement stage");
    }

    const char* failure = NULL;
    for (int scenario = 0; scenario < 8 && failure == NULL; ++scenario)
    {
        const DWORD targetAttrs = (scenario & 1) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
        const DWORD stageAttrs = (scenario & 2) ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;
        const BOOL blockReplacement = (scenario & 4) != 0;
        HANDLE files[2] = {
            CreateFileW(target, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL),
            CreateFileW(stage, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)};
        for (int i = 0; i < 2; ++i)
        {
            DWORD written;
            if (files[i] == INVALID_HANDLE_VALUE ||
                !WriteFile(files[i], i == 0 ? "old" : "new", 3, &written, NULL) || written != 3)
                failure = "could not write read-only replacement fixture";
            if (files[i] != INVALID_HANDLE_VALUE)
                CloseHandle(files[i]);
        }
        if (!SetFileAttributesW(target, targetAttrs) || !SetFileAttributesW(stage, stageAttrs))
            failure = "could not set replacement fixture attributes";

        HANDLE blocker = blockReplacement ? CreateFileW(target, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL) : INVALID_HANDLE_VALUE;
        if (blockReplacement && blocker == INVALID_HANDLE_VALUE)
            failure = "could not block replacement for rollback test";
        {
            CScopedReadOnlyFile original;
            CScopedReadOnlyFile replacement;
            if (failure == NULL && (!original.MakeWritable(target) || !replacement.MakeWritable(stage)))
                failure = "could not prepare read-only replacement";
            if (failure == NULL)
            {
                const BOOL committed = ReplaceFileW(target, stage, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL);
                if (committed)
                    original.Dismiss();
                if (committed == blockReplacement)
                    failure = "replacement did not match the expected sharing outcome";
                if (!replacement.Restore() || (!committed && !original.Restore()))
                    failure = "read-only restoration failed after replacement";
            }
        }
        if (blocker != INVALID_HANDLE_VALUE)
            CloseHandle(blocker);
        const DWORD expected = blockReplacement ? targetAttrs : stageAttrs;
        if ((GetFileAttributesW(target) & FILE_ATTRIBUTE_READONLY) != (expected & FILE_ATTRIBUTE_READONLY))
            failure = "replacement did not preserve destination read-only state";
        if (blockReplacement &&
            (GetFileAttributesW(stage) & FILE_ATTRIBUTE_READONLY) != (stageAttrs & FILE_ATTRIBUTE_READONLY))
            failure = "failed replacement changed stage read-only state";
        HANDLE check = CreateFileW(target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        char actual[3];
        DWORD read = 0;
        if (check == INVALID_HANDLE_VALUE || !ReadFile(check, actual, sizeof(actual), &read, NULL) ||
            read != 3 || memcmp(actual, blockReplacement ? "old" : "new", 3) != 0)
            failure = "replacement did not preserve the expected file content";
        if (check != INVALID_HANDLE_VALUE)
            CloseHandle(check);
        SetFileAttributesW(target, FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(stage, FILE_ATTRIBUTE_NORMAL);
    }
    DeleteFileW(target);
    DeleteFileW(stage);
    return failure != NULL ? Fail(failure) : 0;
}

// Only disposition is injected; successful calls use real handles in owned fixtures.
class CMoveSourceFileSystem : public CPhaseFailingFileSystem
{
public:
    CMoveSourceFileSystem() : CPhaseFailingFileSystem(fpSuccess, ERROR_SUCCESS), FailDeletion(FALSE) {}
    BOOL FailDeletion;
    BOOL SetFileInformationByHandle(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, void* data, DWORD size) override
    {
        if (FailDeletion && kind == FileDispositionInfo)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return FALSE;
        }
        return ::SetFileInformationByHandle(file, kind, data, size);
    }
};

int TestStableMoveSource()
{
    // User temp roots can exceed MAX_PATH; fixtures use the full Unicode API capacity.
    std::vector<WCHAR> temporaryPath(32768), sourceStorage(32768);
    WCHAR* source = sourceStorage.data();
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"sms", 0, source))
        return Fail("could not reserve stable move fixture");
    const std::wstring renamed = std::wstring(source) + L".renamed";
    const char* failure = NULL;
    CMoveSourceFileSystem fileSystem;
    for (int scenario = 0; scenario < 3 && failure == NULL; ++scenario)
    {
        HANDLE writer = CreateFileW(source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD written;
        if (writer == INVALID_HANDLE_VALUE || !WriteFile(writer, "original", 8, &written, NULL) || written != 8)
            failure = "could not populate stable move fixture";
        {
            CStableMoveSource blocked;
            if (failure == NULL && blocked.Open(source))
                failure = "a move accepted an already active writer";
        }
        if (writer != INVALID_HANDLE_VALUE)
            CloseHandle(writer);
        if (scenario != 0)
            SetFileAttributesW(source, FILE_ATTRIBUTE_READONLY);
        {
            CStableMoveSource lease;
            if (failure == NULL && !lease.Open(source))
                failure = "could not acquire stable move source";
            for (int phase = 0; phase < 2 && failure == NULL; ++phase)
            {
                // Simulate reader retry and the post-copy/pre-delete gap. Closing
                // readers must never release the move's independent source lease.
                HANDLE reader = lease.OpenReader(FILE_FLAG_SEQUENTIAL_SCAN);
                char contents[8];
                DWORD read = 0;
                if (reader == INVALID_HANDLE_VALUE || !ReadFile(reader, contents, 8, &read, NULL) ||
                    read != 8 || memcmp(contents, "original", 8) != 0)
                    failure = "reopened move reader did not retain original contents";
                if (reader != INVALID_HANDLE_VALUE)
                    CloseHandle(reader);
                writer = CreateFileW(source, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      NULL, OPEN_EXISTING, 0, NULL);
                if (writer != INVALID_HANDLE_VALUE)
                {
                    failure = "writer entered the gap after a move reader closed";
                    CloseHandle(writer);
                }
                if (MoveFileW(source, renamed.c_str()))
                    failure = "source was renamed while the move still owned it";
            }
            if (failure == NULL && scenario == 2)
            {
                fileSystem.FailDeletion = TRUE;
                if (lease.Delete(fileSystem) || !(GetFileAttributesW(source) & FILE_ATTRIBUTE_READONLY))
                    failure = "failed deletion did not retain the read-only source";
                fileSystem.FailDeletion = FALSE;
            }
            if (failure == NULL && !lease.Delete(fileSystem))
                failure = "stable move source could not be deleted through its held handle";
        }
        if (failure == NULL && GetFileAttributesW(source) != INVALID_FILE_ATTRIBUTES)
            failure = "successful move disposition left the source visible";
        SetFileAttributesW(source, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(source);
    }
    SetFileAttributesW(source, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(renamed.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(source);
    DeleteFileW(renamed.c_str());
    return failure != NULL ? Fail(failure) : 0;
}

bool WritePublicationFixture(const std::wstring& path, const char* contents)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    const DWORD size = (DWORD)strlen(contents);
    const bool ok = WriteFile(file, contents, size, &written, NULL) && written == size;
    return CloseHandle(file) && ok;
}

std::string ReadPublicationFixture(const std::wstring& path)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return "";
    char contents[32] = {};
    DWORD read = 0;
    const bool ok = ReadFile(file, contents, sizeof(contents), &read, NULL) != FALSE;
    CloseHandle(file);
    return ok ? std::string(contents, read) : "";
}

// Inject at the actual rename/flush boundary, leaving all successful operations
// to Windows so the test observes preserved bytes rather than an emulated plan.
class CPublicationFileSystem : public CMoveSourceFileSystem
{
public:
    std::wstring TargetPath;
    int Renames = 0;
    int InsertOccupantAt = 0;
    int FailRenameAt = 0;
    BOOL FailFlush = FALSE;
    BOOL RenameFileByHandle(HANDLE file, HANDLE directory, const WCHAR* name) override
    {
        ++Renames;
        if (Renames == InsertOccupantAt && !WritePublicationFixture(TargetPath, "newer")) return FALSE;
        if (Renames == FailRenameAt) { SetLastError(ERROR_DISK_FULL); return FALSE; }
        return COperationExecutionFileSystem::RenameFileByHandle(file, directory, name);
    }
    BOOL FlushFileBuffers(HANDLE file) override
    {
        if (FailFlush) { SetLastError(ERROR_WRITE_FAULT); return FALSE; }
        return ::FlushFileBuffers(file);
    }
};

struct CPublicationRecordFixture
{
    const char* FailState = NULL;
    std::string States;
    static BOOL Append(void* context, const char* state, const WCHAR*)
    {
        CPublicationRecordFixture& fixture = *(CPublicationRecordFixture*)context;
        fixture.States += std::string(state) + "\n";
        return fixture.FailState == NULL || strcmp(fixture.FailState, state) != 0;
    }
};

BOOL SetPublicationFixtureDacl(const std::wstring& path, const WCHAR* sddl)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, NULL)) return FALSE;
    PACL dacl = NULL;
    BOOL present, defaulted;
    BOOL ok = GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted);
    if (ok) ok = SetNamedSecurityInfoW(const_cast<WCHAR*>(path.c_str()), SE_FILE_OBJECT,
                                      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                      NULL, NULL, dacl, NULL) == ERROR_SUCCESS;
    LocalFree(descriptor);
    return ok;
}

BOOL PublicationFixtureDaclMatches(const std::wstring& path, const WCHAR* sddl)
{
    PSECURITY_DESCRIPTOR expected = NULL, actual = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &expected, NULL)) return FALSE;
    PACL wanted = NULL, found = NULL;
    BOOL present, defaulted;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    const BOOL ok = GetSecurityDescriptorDacl(expected, &present, &wanted, &defaulted) &&
                    GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          NULL, NULL, &found, NULL, &actual) == ERROR_SUCCESS &&
                    GetSecurityDescriptorControl(actual, &control, &revision) && (control & SE_DACL_PROTECTED) &&
                    wanted != NULL && found != NULL && wanted->AclSize == found->AclSize &&
                    memcmp(wanted, found, wanted->AclSize) == 0;
    LocalFree(expected);
    if (actual != NULL) LocalFree(actual);
    return ok;
}

int TestConditionalPublication()
{
    std::vector<WCHAR> temporaryPath(32768), directory(32768);
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"pub", 0, directory.data()) ||
        !DeleteFileW(directory.data()) || !CreateDirectoryW(directory.data(), NULL))
        return Fail("could not reserve conditional publication directory");
    const std::wstring target = std::wstring(directory.data()) + L"\\target";
    const std::wstring stage = std::wstring(directory.data()) + L"\\SALCP-stage";
    const std::wstring backup = stage + L".previous";
    const std::wstring swapped = target + L".swapped";
    const char* failure = NULL;
    for (int scenario = 0; scenario < 14 && failure == NULL; ++scenario)
    {
        const bool existing = scenario != 2 && scenario != 11;
        const bool readOnly = scenario == 4 || scenario == 7;
        if (!WritePublicationFixture(stage, "staged") || (existing && !WritePublicationFixture(target, "old")))
            failure = "could not populate conditional publication fixtures";
        if (readOnly && (!SetFileAttributesW(stage.c_str(), FILE_ATTRIBUTE_READONLY) ||
                          !SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY)))
            failure = "could not set publication read-only attributes";
        if (scenario == 10 && !WritePublicationFixture(backup, "unrelated"))
            failure = "could not create occupied backup fixture";
        const WCHAR* targetDacl = L"D:P(A;;FA;;;OW)";
        const WCHAR* sourceDacl = L"D:P(A;;FA;;;OW)(A;;FR;;;WD)";
        if (scenario >= 12 && (!SetPublicationFixtureDacl(target, targetDacl) || !SetPublicationFixtureDacl(stage, sourceDacl)))
            failure = "could not create protected publication ACL fixtures";
        CPublicationFileSystem fileSystem;
        fileSystem.TargetPath = target;
        fileSystem.InsertOccupantAt = scenario == 1 ? 2 : scenario == 2 ? 1 : 0;
        fileSystem.FailRenameAt = scenario == 5 ? 2 : 0;
        fileSystem.FailFlush = scenario == 6;
        fileSystem.FailDeletion = scenario == 7;
        CPublicationRecordFixture records;
        records.FailState = scenario == 3 ? "publication-planned" :
                            scenario == 8 ? "destination-backed-up" :
                            scenario == 9 ? "destination-published" : NULL;
        CPublicationOutcome outcome;
        {
            CConditionalFilePublication publication;
            if (failure == NULL && !publication.Open(target.c_str(), stage.c_str(), existing, scenario != 13))
                failure = "could not acquire publication files";
            if (failure == NULL && existing)
            {
                // This is the former validation/replacement gap: neither a
                // same-length edit nor a pathname swap may change the approved file.
                HANDLE writer = CreateFileW(target.c_str(), GENERIC_WRITE,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                              NULL, OPEN_EXISTING, 0, NULL);
                if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); failure = "destination write entered publication gap"; }
                if (MoveFileW(target.c_str(), swapped.c_str())) failure = "destination rename entered publication gap";
            }
            if (failure == NULL) outcome = publication.Commit(fileSystem, CPublicationRecordFixture::Append, &records);
        }
        const std::string actual = ReadPublicationFixture(target);
        const std::string staged = ReadPublicationFixture(stage);
        const std::string previous = ReadPublicationFixture(backup);
        if (failure == NULL)
        {
            if (scenario == 0 || scenario == 4 || scenario >= 11)
            {
                if (!outcome.Committed || outcome.Error != ERROR_SUCCESS || outcome.BackupRetained ||
                    actual != "staged" || !staged.empty() || !previous.empty())
                    failure = "successful publication did not preserve staged data";
            }
            else if (scenario == 1 || scenario == 2)
            {
                if (outcome.Committed || outcome.Error == ERROR_SUCCESS || actual != "newer" || staged != "staged" ||
                    previous != (existing ? "old" : ""))
                    failure = "conflicting publication destroyed an unexpected occupant or its backup";
            }
            else if (scenario == 3 || scenario == 5 || scenario == 8 || scenario == 10)
            {
                if (outcome.Committed || outcome.Error == ERROR_SUCCESS || actual != "old" || staged != "staged" ||
                    previous != (scenario == 10 ? "unrelated" : ""))
                    failure = "failed publication did not retain or restore the original destination";
            }
            else if (!outcome.Committed || !outcome.BackupRetained || actual != "staged" || previous != "old" ||
                     (scenario == 7 ? outcome.CleanupError == ERROR_SUCCESS : outcome.Error == ERROR_SUCCESS))
                failure = "post-publication failure lost its recoverable previous version";
            if (readOnly && (!(GetFileAttributesW(target.c_str()) & FILE_ATTRIBUTE_READONLY) ||
                              (scenario == 7 && !(GetFileAttributesW(backup.c_str()) & FILE_ATTRIBUTE_READONLY))))
                failure = "publication or failed backup deletion changed read-only attributes";
            // Preserve a restricted old destination unless source-security copy
            // was explicitly selected; either choice must retain ACL protection.
            if (scenario >= 12 && !PublicationFixtureDaclMatches(target, scenario == 12 ? targetDacl : sourceDacl))
                failure = "publication changed the selected protected ACL policy";
        }
        if (failure != NULL) fprintf(stderr, "Publication scenario %d: error=%lu cleanup=%lu\n", scenario, outcome.Error, outcome.CleanupError);
        const std::wstring paths[] = {target, stage, backup, swapped};
        for (const std::wstring& path : paths) { SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(path.c_str()); }
    }
    RemoveDirectoryW(directory.data());
    return failure == NULL ? 0 : Fail(failure);
}

BOOL CreatePublicationJunction(const std::wstring& link, const std::wstring& target)
{
    // Junctions need no symbolic-link privilege and keep this regression runnable
    // under the same non-administrator account as the self-hosted pipeline.
    struct CMountPointData
    {
        DWORD Tag;
        WORD DataLength, Reserved, SubstituteOffset, SubstituteLength, PrintOffset, PrintLength;
        WCHAR Names[1];
    };
    const std::wstring substitute = L"\\??\\" + target;
    const DWORD bytes = (DWORD)(offsetof(CMountPointData, Names) +
                                (substitute.size() + 1 + target.size() + 1) * sizeof(WCHAR));
    std::vector<BYTE> buffer(bytes, 0);
    CMountPointData* data = (CMountPointData*)buffer.data();
    data->Tag = IO_REPARSE_TAG_MOUNT_POINT;
    data->DataLength = (WORD)(bytes - 8);
    data->SubstituteLength = (WORD)(substitute.size() * sizeof(WCHAR));
    data->PrintOffset = data->SubstituteLength + sizeof(WCHAR);
    data->PrintLength = (WORD)(target.size() * sizeof(WCHAR));
    memcpy(data->Names, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
    memcpy((BYTE*)data->Names + data->PrintOffset, target.c_str(), (target.size() + 1) * sizeof(WCHAR));
    if (!CreateDirectoryW(link.c_str(), NULL)) return FALSE;
    HANDLE directory = CreateFileW(link.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    if (directory == INVALID_HANDLE_VALUE) { RemoveDirectoryW(link.c_str()); return FALSE; }
    DWORD returned;
    const BOOL ok = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, data, bytes, NULL, 0, &returned, NULL);
    CloseHandle(directory);
    if (!ok) RemoveDirectoryW(link.c_str());
    return ok;
}

int TestPublicationDirectoryRedirection()
{
    std::vector<WCHAR> temporaryPath(32768), rootBuffer(32768);
    if (!GetTempPathW((DWORD)temporaryPath.size(), temporaryPath.data()) ||
        !GetTempFileNameW(temporaryPath.data(), L"pdr", 0, rootBuffer.data()) ||
        !DeleteFileW(rootBuffer.data()) || !CreateDirectoryW(rootBuffer.data(), NULL))
        return Fail("could not reserve publication redirection root");
    const std::wstring root = rootBuffer.data(), first = root + L"\\first", second = root + L"\\second", link = root + L"\\link";
    const std::wstring target = link + L"\\target", stage = link + L"\\SALCP-stage";
    const char* failure = NULL;
    if (!CreateDirectoryW(first.c_str(), NULL) || !CreateDirectoryW(second.c_str(), NULL) ||
        !WritePublicationFixture(first + L"\\target", "old") || !WritePublicationFixture(first + L"\\SALCP-stage", "staged") ||
        !WritePublicationFixture(second + L"\\target", "newer") || !WritePublicationFixture(second + L"\\SALCP-stage", "unrelated") ||
        !CreatePublicationJunction(link, first))
        failure = "could not create publication junction fixtures";
    {
        CConditionalFilePublication publication;
        CPublicationFileSystem fileSystem;
        CPublicationRecordFixture records;
        if (failure == NULL && !publication.Open(target.c_str(), stage.c_str(), TRUE))
            failure = "could not open publication through junction";
        // Retarget after acquisition, at the same time a pathname-based commit
        // would previously resolve a different directory and overwrite its file.
        if (failure == NULL && (!RemoveDirectoryW(link.c_str()) || !CreatePublicationJunction(link, second)))
            failure = "could not retarget publication junction";
        if (failure == NULL)
        {
            CPublicationOutcome outcome = publication.Commit(fileSystem, CPublicationRecordFixture::Append, &records);
            if (!outcome.Committed || outcome.Error != ERROR_SUCCESS)
                failure = "publication did not retain its opened directory after junction retarget";
        }
    }
    if (failure == NULL && (ReadPublicationFixture(first + L"\\target") != "staged" ||
                            ReadPublicationFixture(second + L"\\target") != "newer" ||
                            ReadPublicationFixture(second + L"\\SALCP-stage") != "unrelated"))
        failure = "retargeted junction redirected publication into another directory";
    // Remove only owned exact paths, unlinking the junction before its targets.
    RemoveDirectoryW(link.c_str());
    const std::wstring directories[] = {first, second};
    for (const std::wstring& directory : directories)
    {
        DeleteFileW((directory + L"\\target").c_str());
        DeleteFileW((directory + L"\\SALCP-stage").c_str());
        DeleteFileW((directory + L"\\SALCP-stage.previous").c_str());
        RemoveDirectoryW(directory.c_str());
    }
    RemoveDirectoryW(root.c_str());
    return failure == NULL ? 0 : Fail(failure);
}

#include "OperationRecoveryTests.h" // shared fixture helpers above keep recovery tests within their owned directories
#include "FtpDownloadTests.h" // exercise private staging, restart evidence and persistent multi-waiter completion
#include "ConfigurationPayloadTests.h" // require intended registry entries, fields and one save-wide error result

int TestExecutionAdapterFaultInjection()
{
    const CPhaseFailingFileSystem::EFailingPhase phases[] = {
        CPhaseFailingFileSystem::fpCreate, CPhaseFailingFileSystem::fpWrite,
        CPhaseFailingFileSystem::fpMetadata, CPhaseFailingFileSystem::fpFlush,
        CPhaseFailingFileSystem::fpReplace, CPhaseFailingFileSystem::fpMove,
        CPhaseFailingFileSystem::fpDelete};
    const int expectedCalls[] = {1, 2, 3, 4, 5, 5, 6};

    const DWORD operationalErrors[] = {ERROR_DISK_FULL, ERROR_DISK_QUOTA_EXCEEDED, ERROR_ACCESS_DENIED, ERROR_SHARING_VIOLATION};
    for (size_t errorIndex = 0; errorIndex != _countof(operationalErrors); ++errorIndex)
    {
        for (size_t phaseIndex = 0; phaseIndex != _countof(phases); ++phaseIndex)
        {
            CPhaseFailingFileSystem fake(phases[phaseIndex], operationalErrors[errorIndex]);
            // Exercise one ordered durable sequence through the product seam, then
            // restore it before the stack-owned fake can be destroyed. A failing
            // pre-commit phase must never reach replacement or source deletion.
            SetOperationExecutionFileSystemForTests(&fake);
            CPhaseFailingFileSystem::EFailingPhase failedPhase =
                RunTransactionalFaultSequence(OperationExecutionFileSystem(), phases[phaseIndex] == CPhaseFailingFileSystem::fpMove);
            SetOperationExecutionFileSystemForTests(NULL);
            if (failedPhase != phases[phaseIndex] || fake.GetCalls() != expectedCalls[phaseIndex] ||
                GetLastError() != operationalErrors[errorIndex])
                return Fail("the execution filesystem fake did not stop the durable sequence at its selected phase/error pairing");
        }
    }
    return 0;
}

int TestBottomToolbarUtf8Captions()
{
    // The old 15-byte slot kept the lead byte of ż and the ANSI text fallback painted mojibake.
    const char* paths = u8"Szybkie \u015bcie\u017cki";
    std::wstring wide;
    int legacy = Utf8BoundedPrefixLength(paths, (int)strlen(paths), 15);
    if (legacy != 13 || !Utf8TextToWide(paths, legacy, wide) || wide != L"Szybkie \u015bcie")
        return Fail("bottom-toolbar truncation kept a partial Polish character");
    const char* row = u8"Pomoc,Zmie\u0144 nazw\u0119,Podgl\u0105d,Edytuj,Kopiuj,Przenie\u015b,Utw\u00f3rz katalog,Usu\u0144,Menu u\u017cytkownika,Menu,Po\u0142\u0105cz,Roz\u0142\u0105cz";
    const wchar_t* expected[] = {L"Pomoc", L"Zmie\u0144 nazw\u0119", L"Podgl\u0105d", L"Edytuj", L"Kopiuj", L"Przenie\u015b",
                                 L"Utw\u00f3rz katalog", L"Usu\u0144", L"Menu u\u017cytkownika", L"Menu", L"Po\u0142\u0105cz", L"Roz\u0142\u0105cz"};
    const char* field = row;
    for (int index = 0; index < 12; ++index)
    {
        const char* comma = strchr(field, index == 11 ? '\0' : ',');
        if (index < 11 && comma == NULL)
            return Fail("bottom-toolbar row did not contain twelve captions");
        int bytes = index == 11 ? (int)strlen(field) : (int)(comma - field);
        int keep = Utf8BoundedPrefixLength(field, bytes, 64);
        if (keep != bytes || !Utf8TextToWide(field, keep, wide) || wide != expected[index])
            return Fail("bottom-toolbar caption lost Polish diacritics");
        field = comma + 1;
    }
    return 0;
}
}

#include "ReorganizeTests.h" // reorganization plan core must link the real reorg namespace, not an anonymous one

int main()
{
    int result = TestCheckedArithmeticBoundaries();
    if (result == 0)
        result = TestResourceStringsUtf8(); // Polish bytes must reach the UTF-8 menu renderer unchanged
    if (result == 0)
        result = TestNativeControlTextUtf8(); // formatted text and caption round trips must bypass ACP
    if (result == 0)
        result = TestNativeMenusUtf8(); // HMENU round trips and popup painting must preserve the same Unicode commands.
    if (result == 0)
        result = TestNetworkResourcesUtf8(); // provider names must survive both display and subsequent navigation
    if (result == 0)
        result = TestUnicodeTextBoundaries(); // layout must preserve character boundaries and UTF-8 system/RDP messages
    if (result == 0)
        result = TestBottomToolbarUtf8Captions(); // function-key captions must stay valid UTF-8 Polish
    if (result == 0)
        result = TestUnicodeShellLinkTarget(); // COM targets must return complete UTF-8 paths
    if (result != 0)
        return result;
    result = TestNativeFileOperationCharacterization();
    // Run the real-filesystem regression before deterministic fault injection.
    if (result == 0)
        result = TestReadOnlyReplacement();
    if (result == 0)
        result = TestStableMoveSource(); // no source writes may escape through retry or commit gaps
    if (result == 0)
        result = TestConditionalPublication(); // an unexpected occupant must survive every publication race
    if (result == 0)
        result = TestPublicationDirectoryRedirection(); // renamed ancestors cannot redirect retained directory operations
    if (result == 0)
        result = TestOperationRecovery(); // changed or unresolved objects must survive another startup
    if (result == 0)
        result = TestFtpDownloadReliability(); // a failed local outcome must preserve the original files
    if (result == 0)
        result = TestConfigurationPayloadReliability(); // incomplete optional collections cannot become accepted snapshots
    if (result == 0)
        result = TestReorganizeCore(); // plan preview must not depend on the host UI
    return result != 0 ? result : TestExecutionAdapterFaultInjection();
}
