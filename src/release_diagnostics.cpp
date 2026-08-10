// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "release_diagnostics.h"

namespace
{
const LONG kReleaseDiagnosticCapacity = 128;
const size_t kReleaseDiagnosticDetailLength = 80;

struct CReleaseDiagnosticEntry
{
    volatile LONG Sequence;
    DWORD Tick;
    char Category[16];
    char Detail[kReleaseDiagnosticDetailLength];
};

// This fixed-size store is deliberately allocation-free so release diagnostics
// remain available during low-memory, I/O, and shutdown failures.
CReleaseDiagnosticEntry ReleaseDiagnosticEntries[kReleaseDiagnosticCapacity] = {};
volatile LONG ReleaseDiagnosticNextSequence = 0;

void CopySanitizedLabel(char* target, size_t targetSize, const char* source)
{
    if (targetSize == 0)
        return;

    const char* label = source == NULL ? "unknown" : source;
    for (const char* scan = label; *scan != 0; scan++)
        if (*scan == '\\' || *scan == '/')
            label = scan + 1;

    size_t output = 0;
    while (*label != 0 && output + 1 < targetSize)
    {
        const unsigned char value = (unsigned char)*label++;
        target[output++] = (value >= 'a' && value <= 'z') ||
                                 (value >= 'A' && value <= 'Z') ||
                                 (value >= '0' && value <= '9') ||
                                 value == '.' || value == '-' || value == '_'
                             ? (char)value
                             : '_';
    }
    target[output] = 0;
}

void RecordReleaseDiagnostic(const char* category, const char* detail)
{
    const LONG sequence = InterlockedIncrement(&ReleaseDiagnosticNextSequence);
    CReleaseDiagnosticEntry& entry = ReleaseDiagnosticEntries[(sequence - 1) % kReleaseDiagnosticCapacity];

    // A negative sequence marks an entry being rewritten, so snapshots never
    // export a half-written event while workers record concurrently.
    for (;;)
    {
        const LONG published = InterlockedCompareExchange(&entry.Sequence, 0, 0);
        if (published >= 0 && InterlockedCompareExchange(&entry.Sequence, -sequence, published) == published)
            break;
        YieldProcessor();
    }
    entry.Tick = GetTickCount();
    CopySanitizedLabel(entry.Category, _countof(entry.Category), category);
    CopySanitizedLabel(entry.Detail, _countof(entry.Detail), detail);
    MemoryBarrier();
    InterlockedCompareExchange(&entry.Sequence, sequence, -sequence);
}

void FormatAndRecord(const char* category, const char* format, int first, int second)
{
    char detail[kReleaseDiagnosticDetailLength];
    _snprintf_s(detail, _countof(detail), _TRUNCATE, format, first, second);
    RecordReleaseDiagnostic(category, detail);
}

void FormatDiagnosticLine(char* target, size_t targetSize, const CReleaseDiagnosticEntry& entry)
{
    _snprintf_s(target, targetSize, _TRUNCATE, "%ld tick=%lu %s: %s",
                entry.Sequence, entry.Tick, entry.Category, entry.Detail);
}

BOOL WriteDiagnosticText(HANDLE file, const char* text)
{
    DWORD written = 0;
    const DWORD length = (DWORD)strlen(text);
    return WriteFile(file, text, length, &written, NULL) && written == length;
}

template <typename TConsumer>
void EnumerateReleaseDiagnosticEntries(TConsumer consumer)
{
    const LONG newest = InterlockedCompareExchange(&ReleaseDiagnosticNextSequence, 0, 0);
    const LONG oldest = newest > kReleaseDiagnosticCapacity ? newest - kReleaseDiagnosticCapacity + 1 : 1;
    for (LONG sequence = oldest; sequence <= newest; sequence++)
    {
        CReleaseDiagnosticEntry& entry = ReleaseDiagnosticEntries[(sequence - 1) % kReleaseDiagnosticCapacity];
        // Claim the published slot while copying it so a fast producer cannot
        // overwrite a character buffer concurrently with this report snapshot.
        if (InterlockedCompareExchange(&entry.Sequence, -sequence, sequence) == sequence)
        {
            CReleaseDiagnosticEntry snapshot = {};
            snapshot.Sequence = sequence;
            snapshot.Tick = entry.Tick;
            lstrcpyn(snapshot.Category, entry.Category, _countof(snapshot.Category));
            lstrcpyn(snapshot.Detail, entry.Detail, _countof(snapshot.Detail));
            MemoryBarrier();
            InterlockedCompareExchange(&entry.Sequence, sequence, -sequence);
            consumer(snapshot);
        }
    }
}
} // namespace

void RecordReleaseDiagnosticOperationTransition(int fromState, int toState)
{
    FormatAndRecord("transition", "state_%d_to_%d", fromState, toState);
}

void RecordReleaseDiagnosticWait(const char* waitName, DWORD result)
{
    char detail[kReleaseDiagnosticDetailLength];
    CopySanitizedLabel(detail, _countof(detail), waitName);
    char resultText[20];
    _snprintf_s(resultText, _countof(resultText), _TRUNCATE, "_result_%lu", result);
    strncat_s(detail, _countof(detail), resultText, _TRUNCATE);
    RecordReleaseDiagnostic("wait", detail);
}

void RecordReleaseDiagnosticRetry(const char* operationName)
{
    RecordReleaseDiagnostic("retry", operationName);
}

void RecordReleaseDiagnosticPluginIdentity(const char* pluginName)
{
    RecordReleaseDiagnostic("plugin", pluginName);
}

void PrintReleaseDiagnosticRingBuffer(FReleaseDiagnosticPrintLine printLine, void* param)
{
    if (printLine == NULL)
        return;

    printLine(param, "Release diagnostic ring buffer (sanitized; paths and file names are excluded):", FALSE);
    EnumerateReleaseDiagnosticEntries([&](const CReleaseDiagnosticEntry& entry) {
        char line[160];
        FormatDiagnosticLine(line, _countof(line), entry);
        printLine(param, line, TRUE);
    });
    printLine(param, "", FALSE);
}

BOOL ExportReleaseDiagnosticRingBuffer(const char* fileName)
{
    HANDLE file = CreateFileUtf8(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    const char* header = "Open Salamander release diagnostic ring buffer (sanitized; paths and file names are excluded):\r\n";
    BOOL result = WriteDiagnosticText(file, header);
    if (result)
        EnumerateReleaseDiagnosticEntries([&](const CReleaseDiagnosticEntry& entry) {
            char line[164];
            FormatDiagnosticLine(line, _countof(line), entry);
            strncat_s(line, _countof(line), "\r\n", _TRUNCATE);
            if (!WriteDiagnosticText(file, line))
                result = FALSE;
        });
    CloseHandle(file);
    return result;
}
