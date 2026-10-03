// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The SDK boundary is UTF-8 (char*); the plug-in and its engine work in
// UTF-16. Conversions are strict, so a string that cannot be represented is
// reported instead of silently altered (handoff-spec.md T5.6).

#include <string>
#include <vector>

#include "findings.h"

// FocusNameInPanel copies its arguments into MAX_PATH + 200 byte buffers and
// ignores longer ones (src/zip_general_api.cpp); requests that do not fit are
// dropped with a message instead of being truncated.
const size_t kSdkFocusBytes = MAX_PATH + 200;
// Heap buffer for GetPanelPath: any long path in UTF-8 plus the terminator.
const int kSdkPathBufferBytes = 32767 * 3 + 1;

// Strict UTF-8 -> UTF-16; false for NULL or invalid input.
bool SdkToWide(const char* utf8, std::wstring& wide);
std::wstring SdkToWide(const char* utf8);
// UTF-16 -> UTF-8 for SDK calls; false when 'wide' contains unpaired surrogates.
bool WideToSdk(const std::wstring& wide, std::string& utf8);

// Localized UI text copied out of the host's shared cyclic LoadStrW buffer.
std::wstring Text(int id);
std::wstring TextF(int id, const std::vector<std::wstring>& args);

// Engine catalogue backed by the language module (IDS_HO_* and IDS_HL_*).
class LanguageCatalog : public handoff::ITextCatalog
{
public:
    std::wstring Text(int textId, const wchar_t* english) const override;
};

const handoff::ITextCatalog& Catalog();
