// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "csv.h"
#include "text_util.h"

namespace handoff
{

static const size_t CsvMaxBytes = 4 * 1024 * 1024;
static const size_t CsvMaxRows = 50000;
static const size_t CsvMaxColumns = 64;
static const size_t CsvMaxCell = 4096;

int CsvTable::Column(const std::wstring& name) const
{
    for (size_t i = 0; i < Header.size(); i++)
        if (EqualsNoCase(Trim(Header[i]), Trim(name)))
            return (int)i;
    return -1;
}

std::wstring CsvTable::Cell(size_t row, int column) const
{
    if (row >= Rows.size() || column < 0 || (size_t)column >= Rows[row].size())
        return std::wstring();
    return Rows[row][(size_t)column];
}

bool ParseCsv(const unsigned char* data, size_t length, CsvTable& table, std::wstring& error)
{
    table = CsvTable();
    if (length > CsvMaxBytes)
    {
        error = L"the file is larger than " + FormatSize(CsvMaxBytes);
        return false;
    }
    size_t start = (length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) ? 3 : 0;
    std::wstring text;
    if (!Utf8ToWide((const char*)data + start, length - start, text))
    {
        error = L"the file is not valid UTF-8";
        return false;
    }
    size_t firstLineEnd = text.find_first_of(L"\r\n");
    std::wstring firstLine = text.substr(0, firstLineEnd);
    wchar_t separator = (firstLine.find(L';') != std::wstring::npos && firstLine.find(L',') == std::wstring::npos) ? L';' : L',';

    std::vector<std::vector<std::wstring>> rows;
    std::vector<std::wstring> row;
    std::wstring cell;
    bool quoted = false, cellStarted = false;
    auto endCell = [&]() -> bool {
        if (row.size() >= CsvMaxColumns)
        {
            error = L"more than " + NumberText((int64_t)CsvMaxColumns) + L" columns";
            return false;
        }
        row.push_back(cell);
        cell.clear();
        cellStarted = false;
        return true;
    };
    auto endRow = [&]() -> bool {
        if (!endCell())
            return false;
        bool blank = row.size() == 1 && row[0].empty();
        if (!blank)
        {
            if (rows.size() > CsvMaxRows)
            {
                error = L"more than " + NumberText((int64_t)CsvMaxRows) + L" rows";
                return false;
            }
            rows.push_back(row);
        }
        row.clear();
        return true;
    };
    for (size_t i = 0; i < text.size(); i++)
    {
        wchar_t c = text[i];
        if (quoted)
        {
            if (c == L'"')
            {
                if (i + 1 < text.size() && text[i + 1] == L'"')
                {
                    cell += L'"';
                    i++;
                }
                else
                    quoted = false;
            }
            else
                cell += c;
        }
        else if (c == L'"' && !cellStarted)
        {
            quoted = true;
            cellStarted = true;
        }
        else if (c == separator)
        {
            if (!endCell())
                return false;
        }
        else if (c == L'\r' || c == L'\n')
        {
            if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n')
                i++;
            if (!endRow())
                return false;
        }
        else
        {
            cell += c;
            cellStarted = true;
        }
        if (cell.size() > CsvMaxCell)
        {
            error = L"a cell is longer than " + NumberText((int64_t)CsvMaxCell) + L" characters";
            return false;
        }
    }
    if (quoted)
    {
        error = L"unterminated quoted field";
        return false;
    }
    if ((cellStarted || !cell.empty() || !row.empty()) && !endRow())
        return false;
    if (rows.empty())
    {
        error = L"the header row is missing";
        return false;
    }
    table.Header = rows[0];
    table.Rows.assign(rows.begin() + 1, rows.end());
    return true;
}

std::wstring CsvField(const std::wstring& value)
{
    std::wstring cell = value;
    if (!cell.empty() && (cell[0] == L'=' || cell[0] == L'+' || cell[0] == L'-' || cell[0] == L'@' ||
                          cell[0] == L'\t' || cell[0] == L'\r'))
        cell.insert(cell.begin(), L'\'');
    if (cell.find_first_of(L",\"\r\n;") == std::wstring::npos)
        return cell;
    return L"\"" + ReplaceAll(cell, L"\"", L"\"\"") + L"\"";
}

} // namespace handoff
