// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "compiler.h"

namespace reorg
{

struct CJournalRecord
{
    std::string Type;
    std::vector<std::string> Fields;
    bool ValidCrc;
};

struct CJournal
{
    std::vector<CJournalRecord> Records;
    bool ManualOnly;
    bool Incomplete;
    CJournal() : ManualOnly(false), Incomplete(false) {}
};

std::string FormatJournalRecord(const std::string& type, const std::vector<std::string>& fields);
bool ParseJournal(const std::string& text, CJournal& journal);

class CJournalWriter
{
public:
    std::string Text;
    bool FailNext;
    int FlushEvery;
    int SinceFlush;
    CJournalWriter() : FailNext(false), FlushEvery(64), SinceFlush(0) {}

    bool Append(const std::string& type, const std::vector<std::string>& fields, DWORD& error);
    bool Flush(DWORD& error);
};

} // namespace reorg
