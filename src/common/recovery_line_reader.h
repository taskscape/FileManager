// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <string>
#include <vector>

// A journal's total size is unbounded, while one record is bounded. Recovery
// must see a complete final record before any parsed plan authorizes mutation.
class CRecoveryLineReader
{
public:
    explicit CRecoveryLineReader(HANDLE file) : File(file), Buffer(64 * 1024), Used(0), Position(0), Error(ERROR_SUCCESS), AtEnd(FALSE) {}
    enum EResult { Failed = -1, End = 0, Line = 1 };
    EResult Next(std::string& line)
    {
        line.clear();
        if (Error != ERROR_SUCCESS) return Failed;
        while (true)
        {
            if (Position == Used)
            {
                if (AtEnd) return line.empty() ? End : Fail(ERROR_HANDLE_EOF);
                if (!ReadFile(File, Buffer.data(), (DWORD)Buffer.size(), &Used, NULL)) return Fail(GetLastError());
                Position = 0;
                if (Used == 0) { AtEnd = TRUE; continue; }
            }
            const char value = Buffer[Position++];
            if (value == '\n')
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return Line;
            }
            if (value == 0) return Fail(ERROR_INVALID_DATA);
            if (line.size() == 64 * 1024) return Fail(ERROR_BUFFER_OVERFLOW);
            line += value;
        }
    }
    DWORD GetError() const { return Error; }
private:
    HANDLE File;
    std::vector<char> Buffer;
    DWORD Used, Position, Error;
    BOOL AtEnd;
    EResult Fail(DWORD error) { Error = error; SetLastError(error); return Failed; }
};
