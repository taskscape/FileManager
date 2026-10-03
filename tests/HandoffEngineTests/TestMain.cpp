// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TestFramework.h"
#include "Fixtures.h"

#include <string.h>

namespace tests
{

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

int& FailureCount()
{
    static int failures = 0;
    return failures;
}

const char*& CurrentTest()
{
    static const char* current = "";
    return current;
}

bool Check(bool condition, const char* expression, const char* file, int line)
{
    if (!condition)
    {
        fprintf(stderr, "HandoffEngineTests: FAILED %s: %s (%s:%d)\n", CurrentTest(), expression, file, line);
        FailureCount()++;
    }
    return condition;
}

bool CheckText(const std::wstring& actual, const std::wstring& expected, const char* expression, const char* file, int line)
{
    if (actual == expected)
        return true;
    fprintf(stderr, "HandoffEngineTests: FAILED %s: %s (%s:%d)\n  actual:   %ls\n  expected: %ls\n", CurrentTest(), expression,
            file, line, actual.c_str(), expected.c_str());
    FailureCount()++;
    return false;
}

} // namespace tests

int wmain(int argc, wchar_t** argv)
{
    // Workers in the plug-in run in the multithreaded apartment; tests match that.
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        fprintf(stderr, "HandoffEngineTests: CoInitializeEx failed 0x%08lX\n", (unsigned long)hr);
        return 2;
    }
    std::string filter;
    if (argc > 1)
    {
        int length = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, NULL, 0, NULL, NULL);
        filter.resize((size_t)(length > 0 ? length - 1 : 0));
        WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, &filter[0], length, NULL, NULL);
    }
    if (!fixtures::CreateRoot())
    {
        fprintf(stderr, "HandoffEngineTests: cannot create the temporary fixture root\n");
        return 2;
    }
    int run = 0;
    for (const tests::TestCase& test : tests::Registry())
    {
        if (!filter.empty() && strcmp(filter.c_str(), test.Group) != 0)
            continue;
        std::string name = std::string(test.Group) + "." + test.Name;
        tests::CurrentTest() = name.c_str();
        int before = tests::FailureCount();
        test.Run();
        run++;
        printf("%s %s\n", tests::FailureCount() == before ? "PASS" : "FAIL", name.c_str());
        fflush(stdout);
    }
    // Every fixture lives below one GUID directory that is always removed.
    bool removed = fixtures::RemoveRoot();
    CoUninitialize();
    printf("HandoffEngineTests: %d tests, %d failures%s\n", run, tests::FailureCount(), removed ? "" : " (fixture cleanup incomplete)");
    return tests::FailureCount() == 0 && removed && run > 0 ? 0 : 1;
}
