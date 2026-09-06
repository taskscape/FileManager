// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

// Exercise the production status/collection validator on owned registry keys and a real worker.
struct CConfigurationRegistryFixture
{
    HKEY Key = NULL;
    std::wstring Path;
    CConfigurationRegistryFixture()
    {
        GUID id; WCHAR text[40];
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, ARRAYSIZE(text))) return;
        Path = L"Software\\Taskscape\\FileManager\\ReliabilityProbe\\" + std::wstring(text);
        DWORD disposition;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, Path.c_str(), 0, NULL, 0, KEY_ALL_ACCESS,
                            NULL, &Key, &disposition) != ERROR_SUCCESS || disposition != REG_CREATED_NEW_KEY)
        { if (Key) RegCloseKey(Key); Key = NULL; Path.clear(); }
    }
    ~CConfigurationRegistryFixture()
    {
        if (Key) { RegDeleteTreeW(Key, NULL); RegCloseKey(Key); }
        if (!Path.empty()) RegDeleteKeyW(HKEY_CURRENT_USER, Path.c_str());
    }
};
int TestConfigurationPayloadReliability()
{
    CConfigurationPayloadStatus state;
    state.Record(ERROR_ACCESS_DENIED);
    if (state.Error() != ERROR_SUCCESS) return 1;
    state.Begin();
    state.Record(ERROR_FILE_NOT_FOUND, TRUE);
    if (state.Error() != ERROR_SUCCESS) return 2;
    auto worker = std::async(std::launch::async, [&state]() { state.Record(ERROR_DISK_FULL); });
    worker.get();
    for (int i = 0; i < 100; ++i) state.Record(ERROR_SUCCESS);
    state.Record(ERROR_ACCESS_DENIED);
    if (state.End() != ERROR_DISK_FULL) return 3;
    state.Begin();
    if (state.End() != ERROR_SUCCESS) return 4;

    CConfigurationRegistryFixture fixture;
    if (!fixture.Key) return 5;
    const CConfigurationRequiredField fields[] = {{"Masks", REG_SZ}, {"Attributes", REG_DWORD}};
    const DWORD attributes = 16;
    const auto fill = [&](const char* name) {
        HKEY child;
        if (RegCreateKeyExA(fixture.Key, name, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &child, NULL)) return false;
        BOOL ok = RegSetValueExA(child, "Masks", 0, REG_SZ, (const BYTE*)"*.txt", 6) == ERROR_SUCCESS &&
                  RegSetValueExA(child, "Attributes", 0, REG_DWORD, (const BYTE*)&attributes, sizeof(attributes)) == ERROR_SUCCESS;
        RegCloseKey(child);
        return ok != FALSE;
    };
    if (!ValidateConfigurationCollection(fixture.Key, 0, fields, ARRAYSIZE(fields)) || !fill("1") || !fill("2")) return 6;
    if (!ValidateConfigurationCollection(fixture.Key, 2, fields, ARRAYSIZE(fields)) ||
        ValidateConfigurationCollection(fixture.Key, 1, fields, ARRAYSIZE(fields)) ||
        ValidateConfigurationCollection(fixture.Key, 3, fields, ARRAYSIZE(fields))) return 7;
    HKEY child;
    if (RegOpenKeyExA(fixture.Key, "2", 0, KEY_ALL_ACCESS, &child)) return 8;
    RegDeleteValueA(child, "Masks");
    if (ValidateConfigurationCollection(fixture.Key, 2, fields, ARRAYSIZE(fields))) return 9;
    RegSetValueExA(child, "Masks", 0, REG_SZ, NULL, 0);
    if (ValidateConfigurationCollection(fixture.Key, 2, fields, ARRAYSIZE(fields))) return 10;
    RegSetValueExA(child, "Masks", 0, REG_SZ, (const BYTE*)"*.txt", 6);
    RegSetValueExA(child, "Attributes", 0, REG_DWORD, (const BYTE*)&attributes, 1);
    if (ValidateConfigurationCollection(fixture.Key, 2, fields, ARRAYSIZE(fields))) return 11;
    RegCloseKey(child);
    RegDeleteTreeA(fixture.Key, "2");
    if (!fill("3") || ValidateConfigurationCollection(fixture.Key, 2, fields, ARRAYSIZE(fields))) return 12;
    puts("Configuration payload: first-error lifetime, worker failure, count, field, string, type and numbering checks passed.");
    return 0;
}
