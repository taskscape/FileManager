// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// RFC 4180 CSV for registers (drawing registers, approvals, licences) and the
// CSV manifest. Registers are untrusted input, so rows, columns, and cell
// sizes are bounded (C.4.1).

#include <string>
#include <vector>

namespace handoff
{

struct CsvTable
{
    std::vector<std::wstring> Header;
    std::vector<std::vector<std::wstring>> Rows;
    // Case-insensitive header lookup; -1 when absent.
    int Column(const std::wstring& name) const;
    std::wstring Cell(size_t row, int column) const;
};

// UTF-8 (BOM optional). The separator is ',' unless the header row contains
// ';' and no ',' — spreadsheet applications in many European locales export
// semicolon-separated "CSV".
bool ParseCsv(const unsigned char* data, size_t length, CsvTable& table, std::wstring& error);

// One RFC 4180 field for output. Cells starting with = + - @ TAB or CR are
// prefixed with ' so spreadsheets never evaluate them (formula injection, C.7.2).
std::wstring CsvField(const std::wstring& value);

} // namespace handoff
