// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Delivery Handoff specification parser probe (handoff-spec.md C.12.2).
//
// Compiled by tools/test-handoff-spec-parser.ps1 straight from the plug-in's
// host-independent engine sources. Every corpus file is parsed and its
// findings ("CODE line:column", one per line) are compared with the sibling
// "<file>.expected"; valid inputs without an expectation must produce no
// findings. With --iterations N each input is also mutated deterministically
// (byte flips, truncation, duplication, deep-nesting insertion) and every
// mutant must parse without crashing in under one second.
//
// usage: handoff_spec_parser_probe [--record] [--iterations N]
//            --valid <dir> [--valid <dir>] --invalid <dir> --hostile <dir>

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "spec.h"
#include "text_util.h"

namespace
{

enum class Kind
{
    Valid,
    Invalid,
    Hostile
};

struct Input
{
    std::wstring Path;
    Kind Category;
};

std::vector<unsigned char> ReadFile(const std::wstring& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool ReadText(const std::wstring& path, std::string& text)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return true;
}

std::string Narrow(const std::wstring& text)
{
    return handoff::WideToUtf8(text);
}

// One line per finding, in the validator's reporting order.
std::string Describe(const handoff::SpecLoadResult& result)
{
    std::string text;
    for (const handoff::Finding& finding : result.Findings)
        text += Narrow(finding.Code) + " " + std::to_string(finding.Line) + ":" + std::to_string(finding.Column) + "\n";
    return text;
}

std::vector<std::wstring> ListInputs(const std::wstring& directory)
{
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((directory + L"\\*.json").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return files;
    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            files.push_back(directory + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(files.begin(), files.end());
    return files;
}

// Deterministic generator so a failing mutant can be reproduced from its iteration number.
struct Random
{
    uint64_t State;
    explicit Random(uint64_t seed) : State(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next()
    {
        State ^= State << 13;
        State ^= State >> 7;
        State ^= State << 17;
        return (uint32_t)(State >> 11);
    }
};

std::vector<unsigned char> Mutate(const std::vector<unsigned char>& source, Random& random)
{
    std::vector<unsigned char> data = source;
    switch (random.Next() % 4)
    {
    case 0: // byte flips
        for (int i = 0, count = 1 + (int)(random.Next() % 8); i < count && !data.empty(); i++)
            data[random.Next() % data.size()] ^= (unsigned char)(1 + random.Next() % 255);
        break;
    case 1: // truncation
        if (!data.empty())
            data.resize(random.Next() % data.size());
        break;
    case 2: // duplication of a slice
        if (!data.empty())
        {
            size_t start = random.Next() % data.size();
            size_t length = std::min<size_t>(data.size() - start, 1 + random.Next() % 512);
            std::vector<unsigned char> slice(data.begin() + start, data.begin() + start + length);
            data.insert(data.begin() + random.Next() % data.size(), slice.begin(), slice.end());
        }
        break;
    default: // deep nesting insertion
    {
        size_t depth = 16 + random.Next() % 4096;
        std::string nested = std::string(depth, '[') + "1" + std::string(depth, ']');
        size_t at = data.empty() ? 0 : random.Next() % data.size();
        data.insert(data.begin() + at, nested.begin(), nested.end());
        break;
    }
    }
    return data;
}

bool TimedParse(const std::vector<unsigned char>& data, double& seconds, handoff::SpecLoadResult& result)
{
    LARGE_INTEGER frequency, start, end;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    result = handoff::ParseSpecification(data.data(), data.size());
    QueryPerformanceCounter(&end);
    seconds = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
    return seconds < 1.0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::vector<Input> inputs;
    bool record = false;
    int iterations = 0;
    for (int i = 1; i < argc; i++)
    {
        std::wstring arg = argv[i];
        if (arg == L"--record")
            record = true;
        else if (arg == L"--iterations" && i + 1 < argc)
            iterations = _wtoi(argv[++i]);
        else if ((arg == L"--valid" || arg == L"--invalid" || arg == L"--hostile") && i + 1 < argc)
        {
            Kind kind = arg == L"--valid" ? Kind::Valid : arg == L"--invalid" ? Kind::Invalid : Kind::Hostile;
            for (const std::wstring& file : ListInputs(argv[++i]))
                inputs.push_back(Input{file, kind});
        }
        else
        {
            fprintf(stderr, "unknown argument: %ls\n", argv[i]);
            return 2;
        }
    }
    if (inputs.empty())
    {
        fprintf(stderr, "no corpus inputs were found\n");
        return 2;
    }

    int failures = 0;
    for (const Input& input : inputs)
    {
        std::vector<unsigned char> data = ReadFile(input.Path);
        handoff::SpecLoadResult result;
        double seconds = 0;
        if (!TimedParse(data, seconds, result))
        {
            fprintf(stderr, "SLOW %ls: %.3f s\n", input.Path.c_str(), seconds);
            failures++;
        }
        std::string actual = Describe(result);
        std::wstring expectedPath = input.Path + L".expected";
        std::string expected;
        bool hasExpected = ReadText(expectedPath, expected);
        if (record && input.Category != Kind::Valid)
        {
            std::ofstream out(expectedPath, std::ios::binary);
            out << actual;
            printf("RECORDED %ls\n", input.Path.c_str());
            continue;
        }
        if (input.Category == Kind::Invalid && !hasExpected)
        {
            fprintf(stderr, "MISSING %ls.expected\n", input.Path.c_str());
            failures++;
        }
        else if (input.Category == Kind::Valid && !hasExpected)
            hasExpected = true; // templates and edge cases must validate cleanly
        if (hasExpected && actual != expected)
        {
            fprintf(stderr, "MISMATCH %ls\n--- expected\n%s--- actual\n%s", input.Path.c_str(), expected.c_str(), actual.c_str());
            failures++;
        }
        else
            printf("OK %ls (%zu findings, %.1f ms)\n", input.Path.c_str(), result.Findings.size(), seconds * 1000.0);
    }

    // Mutation soak: every mutant must be rejected or accepted quickly and without crashing.
    for (int iteration = 1; iteration <= iterations; iteration++)
    {
        for (size_t index = 0; index < inputs.size(); index++)
        {
            std::vector<unsigned char> original = ReadFile(inputs[index].Path);
            Random random((uint64_t)iteration * 1000003u + index);
            std::vector<unsigned char> mutant = Mutate(original, random);
            handoff::SpecLoadResult result;
            double seconds = 0;
            if (!TimedParse(mutant, seconds, result))
            {
                fprintf(stderr, "SLOW mutant iteration %d of %ls: %.3f s\n", iteration, inputs[index].Path.c_str(), seconds);
                failures++;
            }
        }
    }

    printf("handoff_spec_parser_probe: %zu inputs, %d mutation iterations, %d failures\n", inputs.size(), iterations, failures);
    return failures == 0 ? 0 : 1;
}
