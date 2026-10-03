// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mapping.h"

namespace reorg
{
namespace
{

std::vector<std::string> ParseCsvRecords(const std::string& csv)
{
    std::string text = csv;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        text.erase(0, 3);
    std::vector<std::string> records;
    std::string current;
    bool quote = false;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (quote)
        {
            if (ch == '"')
            {
                if (i + 1 < text.size() && text[i + 1] == '"')
                {
                    current.push_back('"');
                    ++i;
                }
                else
                    quote = false;
            }
            else
                current.push_back(ch);
        }
        else if (ch == '"')
            quote = true;
        else if (ch == '\n')
        {
            if (!current.empty() && current.back() == '\r')
                current.pop_back();
            records.push_back(current);
            current.clear();
        }
        else
            current.push_back(ch);
    }
    if (!current.empty())
    {
        if (current.back() == '\r')
            current.pop_back();
        records.push_back(current);
    }
    return records;
}

std::vector<std::string> SplitCsv(const std::string& record)
{
    std::vector<std::string> fields;
    std::string current;
    bool quote = false;
    for (size_t i = 0; i < record.size(); ++i)
    {
        char ch = record[i];
        if (quote)
        {
            if (ch == '"')
            {
                if (i + 1 < record.size() && record[i + 1] == '"')
                {
                    current.push_back('"');
                    ++i;
                }
                else
                    quote = false;
            }
            else
                current.push_back(ch);
        }
        else if (ch == '"')
            quote = true;
        else if (ch == ',')
        {
            fields.push_back(current);
            current.clear();
        }
        else
            current.push_back(ch);
    }
    fields.push_back(current);
    return fields;
}

std::wstring ResolveSide(const std::wstring& value, const std::wstring& root)
{
    if (value.size() >= 2 && ((value[1] == L':') || (value[0] == L'\\' && value[1] == L'\\')))
    {
        std::wstring normalized, error;
        if (NormalizePath(value, normalized, error))
            return normalized;
        return value;
    }
    return JoinPath(root, value);
}

} // namespace

CMappingResult ParseMapping(const std::string& csv, const std::wstring& scopeRoot, const std::wstring& destinationRoot, const CSnapshot& snapshot)
{
    CMappingResult result;
    std::vector<std::string> records = ParseCsvRecords(csv);
    if (records.empty())
        return result;
    for (size_t i = 1; i < records.size(); ++i)
    {
        if (TrimAscii(records[i]).empty())
            continue;
        std::vector<std::string> fields = SplitCsv(records[i]);
        CMappingRow row;
        row.Row = (int)i + 1;
        row.Accepted = false;
        if (fields.size() < 2)
        {
            row.Error = L"expected source,destination";
            result.Rows.push_back(row);
            continue;
        }
        row.Source = ResolveSide(Utf8ToWide(fields[0]), scopeRoot);
        std::wstring destination = Utf8ToWide(fields[1]);
        bool into = !destination.empty() && (destination.back() == L'\\' || destination.back() == L'/');
        destination = ResolveSide(destination, destinationRoot);
        if (into)
        {
            row.DestinationDir = destination;
            row.NewName.clear();
        }
        else
        {
            row.DestinationDir = ParentPath(destination);
            row.NewName = LeafName(destination);
        }
        if (fields.size() >= 3)
            row.Note = Utf8ToWide(fields[2]);
        if (snapshot.Find(row.Source) == NULL && !snapshot.Items.empty())
            row.Error = L"source was not found";
        else
            row.Accepted = true;
        result.Rows.push_back(row);
    }
    return result;
}

} // namespace reorg
