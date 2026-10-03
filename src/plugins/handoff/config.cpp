// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "config.h"
#include "text_util.h"

// Registry layout of handoff-spec.md C.9. Paths are REG_BINARY UTF-8 without a
// terminator because the host's REG_SZ wrapper writes through the ANSI
// registry API, which would lose characters outside the active code page.

namespace
{

const DWORD kConfigVersion = 1;

const char* const kVersion = "Version";
const char* const kSpecLibrary = "Spec Library";
const char* const kRecentPrefix = "Recent Spec ";
const char* const kShowUnassigned = "Show Unassigned";
const char* const kFocusNewPackage = "Focus New Package";
const char* const kKeepFailed = "Keep Failed Staging";
const char* const kReviewPlacement = "Review Placement";
const char* const kVerifyPlacement = "Verify Placement";
const char* const kReviewColumns = "Review Columns";
const char* const kVerifyColumns = "Verify Columns";

// Bounds keep a damaged or hostile registry value from forcing large allocations.
const DWORD kMaxPathValueBytes = 32767 * 3;
const DWORD kMaxBlobBytes = 4096;

std::mutex ConfigLock;
HandoffConfig Current;

bool ReadBinary(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, DWORD maximum,
                std::vector<unsigned char>& value)
{
    DWORD size = 0;
    if (!registry->GetSize(key, name, REG_BINARY, size) || size == 0 || size > maximum)
        return false;
    value.assign(size, 0);
    if (!registry->GetValue(key, name, REG_BINARY, value.data(), size))
    {
        value.clear();
        return false;
    }
    return true;
}

bool ReadPath(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, std::wstring& path)
{
    std::vector<unsigned char> bytes;
    if (!ReadBinary(key, registry, name, kMaxPathValueBytes, bytes))
        return false;
    // Invalid UTF-8 means the value was not written by this plug-in; the default is used.
    std::wstring wide;
    if (!handoff::Utf8ToWide((const char*)bytes.data(), bytes.size(), wide) || wide.empty() ||
        wide.find(L'\0') != std::wstring::npos)
        return false;
    path = wide;
    return true;
}

void WriteBinary(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, const void* data, size_t size)
{
    if (size == 0)
        registry->DeleteValue(key, name);
    else
        registry->SetValue(key, name, REG_BINARY, data, (DWORD)size);
}

void WritePath(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, const std::wstring& path)
{
    std::string utf8 = handoff::WideToUtf8(path);
    WriteBinary(key, registry, name, utf8.data(), utf8.size());
}

bool ReadFlag(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, bool fallback)
{
    DWORD value = 0;
    if (!registry->GetValue(key, name, REG_DWORD, &value, sizeof(value)))
        return fallback;
    return value != 0;
}

void WriteFlag(HKEY key, CSalamanderRegistryAbstract* registry, const char* name, bool value)
{
    DWORD data = value ? 1 : 0;
    registry->SetValue(key, name, REG_DWORD, &data, sizeof(data));
}

std::string RecentName(size_t index)
{
    return std::string(kRecentPrefix) + std::to_string(index + 1);
}

} // namespace

HandoffConfig GetConfig()
{
    std::lock_guard<std::mutex> lock(ConfigLock);
    return Current;
}

void UpdateConfig(const std::function<void(HandoffConfig&)>& change)
{
    std::lock_guard<std::mutex> lock(ConfigLock);
    change(Current);
}

void AddRecentSpec(const std::wstring& path)
{
    UpdateConfig([&path](HandoffConfig& config) {
        auto& recent = config.RecentSpecs;
        recent.erase(std::remove_if(recent.begin(), recent.end(),
                                    [&path](const std::wstring& item) { return handoff::EqualsNoCase(item, path); }),
                     recent.end());
        recent.insert(recent.begin(), path);
        if (recent.size() > kMaxRecentSpecs)
            recent.resize(kMaxRecentSpecs);
    });
}

void LoadHandoffConfig(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    HandoffConfig config; // regKey == NULL loads defaults
    if (regKey != NULL)
    {
        ReadPath(regKey, registry, kSpecLibrary, config.SpecLibrary);
        for (size_t i = 0; i < kMaxRecentSpecs; i++)
        {
            std::wstring path;
            if (ReadPath(regKey, registry, RecentName(i).c_str(), path))
                config.RecentSpecs.push_back(path);
        }
        config.ShowUnassigned = ReadFlag(regKey, registry, kShowUnassigned, true);
        config.FocusNewPackage = ReadFlag(regKey, registry, kFocusNewPackage, true);
        config.KeepFailedStaging = ReadFlag(regKey, registry, kKeepFailed, false);
        ReadBinary(regKey, registry, kReviewPlacement, kMaxBlobBytes, config.ReviewPlacement);
        ReadBinary(regKey, registry, kVerifyPlacement, kMaxBlobBytes, config.VerifyPlacement);
        ReadBinary(regKey, registry, kReviewColumns, kMaxBlobBytes, config.ReviewColumns);
        ReadBinary(regKey, registry, kVerifyColumns, kMaxBlobBytes, config.VerifyColumns);
    }
    std::lock_guard<std::mutex> lock(ConfigLock);
    Current = config;
}

void SaveHandoffConfig(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    HandoffConfig config = GetConfig();
    DWORD version = kConfigVersion;
    registry->SetValue(regKey, kVersion, REG_DWORD, &version, sizeof(version));
    WritePath(regKey, registry, kSpecLibrary, config.SpecLibrary);
    for (size_t i = 0; i < kMaxRecentSpecs; i++)
        WritePath(regKey, registry, RecentName(i).c_str(), i < config.RecentSpecs.size() ? config.RecentSpecs[i] : std::wstring());
    WriteFlag(regKey, registry, kShowUnassigned, config.ShowUnassigned);
    WriteFlag(regKey, registry, kFocusNewPackage, config.FocusNewPackage);
    WriteFlag(regKey, registry, kKeepFailed, config.KeepFailedStaging);
    WriteBinary(regKey, registry, kReviewPlacement, config.ReviewPlacement.data(), config.ReviewPlacement.size());
    WriteBinary(regKey, registry, kVerifyPlacement, config.VerifyPlacement.data(), config.VerifyPlacement.size());
    WriteBinary(regKey, registry, kReviewColumns, config.ReviewColumns.data(), config.ReviewColumns.size());
    WriteBinary(regKey, registry, kVerifyColumns, config.VerifyColumns.data(), config.VerifyColumns.size());
}
