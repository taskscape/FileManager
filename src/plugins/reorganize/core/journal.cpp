// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "journal.h"

namespace reorg
{

std::string FormatJournalRecord(const std::string& type, const std::vector<std::string>& fields)
{
    std::string body = type;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        body.push_back('|');
        body += PercentEncode(fields[i]);
    }
    DWORD crc = Crc32(body.data(), body.size());
    char suffix[32];
    _snprintf_s(suffix, _countof(suffix), _TRUNCATE, "|crc=%08X", crc);
    body += suffix;
    body += "\r\n";
    return body;
}

bool ParseJournal(const std::string& text, CJournal& journal)
{
    journal = CJournal();
    size_t begin = 0;
    std::string last;
    bool lastComplete = false;
    while (begin < text.size())
    {
        size_t end = text.find("\r\n", begin);
        if (end == std::string::npos)
        {
            last = text.substr(begin);
            lastComplete = false;
            break;
        }
        std::string line = text.substr(begin, end - begin);
        begin = end + 2;
        size_t crcPos = line.rfind("|crc=");
        if (crcPos == std::string::npos)
        {
            // A record that is not last and has no checksum cannot be trusted.
            if (text.find("\r\n", begin) != std::string::npos)
                journal.ManualOnly = true;
            continue;
        }
        std::string body = line.substr(0, crcPos);
        std::string crcText = line.substr(crcPos + 5);
        unsigned crc = 0;
        sscanf_s(crcText.c_str(), "%08x", &crc);
        DWORD actual = Crc32(body.data(), body.size());
        CJournalRecord record;
        record.ValidCrc = actual == crc;
        size_t bar = body.find('|');
        record.Type = bar == std::string::npos ? body : body.substr(0, bar);
        size_t field = bar == std::string::npos ? body.size() : bar + 1;
        while (field < body.size())
        {
            size_t next = body.find('|', field);
            std::string raw = next == std::string::npos ? body.substr(field) : body.substr(field, next - field);
            std::string decoded;
            if (!PercentDecode(raw, decoded))
                decoded = raw;
            record.Fields.push_back(decoded);
            if (next == std::string::npos)
                break;
            field = next + 1;
        }
        journal.Records.push_back(record);
        lastComplete = true;
        (void)last;
    }
    if (!journal.Records.empty())
    {
        CJournalRecord tail = journal.Records.back();
        if (!tail.ValidCrc)
            journal.Records.pop_back();
        for (size_t i = 0; i < journal.Records.size(); ++i)
        {
            if (!journal.Records[i].ValidCrc)
                journal.ManualOnly = true;
        }
    }
    if (!lastComplete && !last.empty())
    {
        // Torn final record: discard it and keep the durable prefix.
    }
    bool inSegment = false;
    bool ended = true;
    for (size_t i = 0; i < journal.Records.size(); ++i)
    {
        if (journal.Records[i].Type == "BEGIN-APPLY")
        {
            inSegment = true;
            ended = false;
        }
        else if (journal.Records[i].Type == "END-APPLY")
            ended = true;
    }
    journal.Incomplete = inSegment && !ended;
    return !journal.ManualOnly;
}

bool CJournalWriter::Append(const std::string& type, const std::vector<std::string>& fields, DWORD& error)
{
    if (FailNext)
    {
        FailNext = false;
        error = ERROR_DISK_FULL;
        return false;
    }
    Text += FormatJournalRecord(type, fields);
    ++SinceFlush;
    error = ERROR_SUCCESS;
    if (type == "FAIL" || SinceFlush >= FlushEvery)
        return Flush(error);
    return true;
}

bool CJournalWriter::Flush(DWORD& error)
{
    SinceFlush = 0;
    error = ERROR_SUCCESS;
    return true;
}

} // namespace reorg
