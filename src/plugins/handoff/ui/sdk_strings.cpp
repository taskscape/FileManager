// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "../handoff.h"
#include "sdk_strings.h"
#include "text_util.h"

bool SdkToWide(const char* utf8, std::wstring& wide)
{
    wide.clear();
    if (utf8 == NULL)
        return false;
    return handoff::Utf8ToWide(utf8, strlen(utf8), wide);
}

std::wstring SdkToWide(const char* utf8)
{
    std::wstring wide;
    SdkToWide(utf8, wide);
    return wide;
}

bool WideToSdk(const std::wstring& wide, std::string& utf8)
{
    utf8.clear();
    if (wide.empty())
        return true;
    // WC_ERR_INVALID_CHARS rejects unpaired surrogates instead of writing U+FFFD into a path.
    int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), NULL, 0, NULL, NULL);
    if (length <= 0)
        return false;
    utf8.resize((size_t)length);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), (int)wide.size(), &utf8[0], length, NULL,
                               NULL) == length;
}

std::wstring Text(int id)
{
    // LoadStrW returns a pointer into a buffer shared by every thread and
    // plug-in; the text is copied before anything else can load a string.
    const WCHAR* text = SalamanderGeneral->LoadStrW(HLanguage, id);
    return text != NULL ? std::wstring(text) : std::wstring();
}

std::wstring TextF(int id, const std::vector<std::wstring>& args)
{
    return handoff::FormatPositional(Text(id), args);
}

std::wstring LanguageCatalog::Text(int textId, const wchar_t* english) const
{
    // A missing translation falls back to the engine's English text rather
    // than the host's "ERROR LOADING" placeholder.
    const WCHAR* text = NULL;
    int length = LoadStringW(HLanguage, (UINT)textId, (LPWSTR)&text, 0);
    if (length <= 0 || text == NULL)
        return english != NULL ? english : L"";
    return ::Text(textId);
}

const handoff::ITextCatalog& Catalog()
{
    static LanguageCatalog catalog;
    return catalog;
}
