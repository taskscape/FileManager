// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Minimal self-registering test harness for the host-independent Handoff
// engine (handoff-spec.md C.12.1). Tests run in one process on an MTA thread
// because WIC and Windows.Data.Pdf are used exactly as the plug-in workers use them.

#include <windows.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace tests
{

struct TestCase
{
    const char* Group;
    const char* Name;
    void (*Run)();
};

std::vector<TestCase>& Registry();
int& FailureCount();
const char*& CurrentTest();

struct Registrar
{
    Registrar(const char* group, const char* name, void (*run)()) { Registry().push_back(TestCase{group, name, run}); }
};

bool Check(bool condition, const char* expression, const char* file, int line);
bool CheckText(const std::wstring& actual, const std::wstring& expected, const char* expression, const char* file, int line);

} // namespace tests

#define HT_TEST(group, name)                                                                   \
    static void Test_##group##_##name();                                                       \
    static tests::Registrar Register_##group##_##name(#group, #name, Test_##group##_##name);  \
    static void Test_##group##_##name()

#define HT_CHECK(condition) tests::Check((condition), #condition, __FILE__, __LINE__)
#define HT_CHECK_TEXT(actual, expected) tests::CheckText((actual), (expected), #actual, __FILE__, __LINE__)
