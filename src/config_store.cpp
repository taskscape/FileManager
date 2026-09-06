// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include "common/configuration_payload.h"

#include <shlwapi.h>
#undef PathIsPrefix // otherwise, collision with CSalamanderGeneral::PathIsPrefix

#include "toolbar.h"
#include "stswnd.h"
#include "plugins.h"
#include "fileswnd.h"
#include "mainwnd.h"
#include "cfgdlg.h"
#include "usermenu.h"
#include "viewer.h"
#include "zip.h"
#include "pack.h"
#include "find.h"
#include "dialogs.h"
#include "logo.h"
#include "tasklist.h"
#include "pwdmngr.h"
#include "regwork.h"

#include <strsafe.h>

#include "config_store.h" // shared transactional-store internals

// Transactional configuration store extracted from mainwnd_config.cpp as a
// mechanical move: generation keys with checksum and schema validation/migration,
// commit/rollback transactions, and the UI-test configuration-store override.
// All cluster-local statics stay here; exported entry points are declared in
// salamand.h.
static const char* const FileManagerUiTestConfigurationRoot = "Software\\Open Salamander\\6.0-filemanager-testdata";
static BOOL FileManagerUiTestConfigurationStore = FALSE;

// The public root remains the path that existing host and plug-in code opens.  Once a
// transactional configuration has been committed it points at the selected generation;
// the immutable store root is used only to create and switch generations.
static char ConfigurationStoreRoot[512] = {};
static char ActiveConfigurationRoot[512] = {};
static const char* CONFIGURATION_GENERATIONS_REG = "Configuration Generations";
const char* CONFIGURATION_ACTIVE_GENERATION_REG = "Active Generation";
static const char* CONFIGURATION_TRANSACTION_COMPLETE_REG = "Transaction Complete";
static const char* CONFIGURATION_TRANSACTION_CHECKSUM_REG = "Transaction Checksum";
static const char* CONFIGURATION_SCHEMA_VERSION_REG = "Configuration Schema Version";
static const DWORD CONFIGURATION_SCHEMA_VERSION = 1;
static const char* const CONFIGURATION_PAYLOAD_VERSION_REG = "Payload Integrity Version";
static const char* const CONFIGURATION_EXPECTED_COUNT_REG = "Expected Item Count";
static const char* const CONFIGURATION_GENERATION_TOKEN_REG = "Transaction Identity";
static GUID LoadedConfigurationToken = {};
static BOOL LoadedConfigurationTokenValid = FALSE;

static BOOL ReadConfigurationGenerationToken(HKEY generation, GUID& token)
{
    DWORD type = 0, size = sizeof(token);
    const GUID absent = {};
    return RegQueryValueExA(generation, CONFIGURATION_GENERATION_TOKEN_REG, NULL, &type, (BYTE*)&token, &size) == ERROR_SUCCESS &&
           type == REG_BINARY && size == sizeof(token) && memcmp(&token, &absent, sizeof(token)) != 0;
}

void CaptureConfigurationGenerationForStartup(HKEY generation)
{
    // Slot numbers are reused; only the identity of the actually loaded snapshot
    // authorizes later deletion of its fallback, even after same-process saves.
    LoadedConfigurationTokenValid = UsesTransactionalConfigurationStore() &&
                                    ReadConfigurationGenerationToken(generation, LoadedConfigurationToken);
}

BOOL SetConfigurationCollectionExpectedCount(HKEY collection, int count)
{
    if (count < 0) { RecordConfigurationPayloadFailure(ERROR_INVALID_DATA); return FALSE; }
    const DWORD expected = (DWORD)count;
    return SetValue(collection, CONFIGURATION_EXPECTED_COUNT_REG, REG_DWORD, &expected, sizeof(expected));
}

static BOOL ValidateConfigurationPayloadCollections(HKEY generation)
{
    DWORD version = 0, type = 0, length = sizeof(version);
    const LONG result = RegQueryValueExA(generation, CONFIGURATION_PAYLOAD_VERSION_REG, NULL,
                                        &type, (BYTE*)&version, &length);
    if (result == ERROR_FILE_NOT_FOUND) return TRUE; // older verified profiles have no collection manifest
    if (result != ERROR_SUCCESS || type != REG_DWORD || length != sizeof(version) || version != 1) return FALSE;
    GUID identity;
    if (!ReadConfigurationGenerationToken(generation, identity)) return FALSE;
    // Verify the fields each host collection always writes. Conditional command
    // fields also remain protected by the first-error latch and full-tree checksum.
    const CConfigurationRequiredField custom[] = {
        {"Type", REG_DWORD}, {"Title", REG_SZ}, {"Ext", REG_SZ}};
    const CConfigurationRequiredField predefined[] = {
        {"Packer UID", REG_DWORD}, {"Packer Executable", REG_SZ}, {"Use Packer Executable To Unpack", REG_DWORD}};
    const CConfigurationRequiredField association[] = {
        {"Extension List", REG_SZ}, {"Packer Supported", REG_DWORD}, {"Packer Index", REG_DWORD}, {"Unpacker Index", REG_DWORD}};
    const CConfigurationRequiredField highlight[] = {
        {"Masks", REG_SZ}, {"Attributes", REG_DWORD}, {"Valid Attributes", REG_DWORD},
        {"Item Fg Normal", REG_DWORD}, {"Item Fg Selected", REG_DWORD}, {"Item Fg Focused", REG_DWORD},
        {"Item Fg Focused and Selected", REG_DWORD}, {"Item Fg Highlight", REG_DWORD},
        {"Item Bk Normal", REG_DWORD}, {"Item Bk Selected", REG_DWORD}, {"Item Bk Focused", REG_DWORD},
        {"Item Bk Focused and Selected", REG_DWORD}, {"Item Bk Highlight", REG_DWORD}};
    const CConfigurationRequiredField menu[] = {
        {"Item Name", REG_SZ}, {"Command", REG_SZ}, {"Arguments", REG_SZ}, {"Initial Directory", REG_SZ},
        {"Execute Through Shell", REG_DWORD}, {"Open Shell Window", REG_DWORD}, {"Close Shell Window", REG_DWORD},
        {"Icon", REG_SZ}, {"Type", REG_DWORD}, {"Show In Toolbar", REG_DWORD}};
    const struct { const char* Path; const CConfigurationRequiredField* Fields; size_t FieldCount; } collections[] = {
        {"Packers & Unpackers\\Custom Packers", custom, ARRAYSIZE(custom)},
        {"Packers & Unpackers\\Custom Unpackers", custom, ARRAYSIZE(custom)},
        {"Packers & Unpackers\\Predefined Packers", predefined, ARRAYSIZE(predefined)},
        {"Packers & Unpackers\\Archive Association", association, ARRAYSIZE(association)},
        {"User Menu", menu, ARRAYSIZE(menu)}, {"Colors\\Panel Items Hilighting", highlight, ARRAYSIZE(highlight)}};
    for (size_t index = 0; index < ARRAYSIZE(collections); ++index)
    {
        HKEY collection = NULL;
        if (!OpenKeyAux(NULL, generation, collections[index].Path, collection)) return FALSE;
        DWORD count = 0;
        BOOL valid = GetValueAux(NULL, collection, CONFIGURATION_EXPECTED_COUNT_REG, REG_DWORD, &count, sizeof(count));
        if (valid)
        {
            try
            {
                valid = ValidateConfigurationCollection(collection, count, collections[index].Fields, collections[index].FieldCount);
            }
            catch (const std::bad_alloc&) { valid = FALSE; }
        }
        CloseKeyAux(collection);
        if (!valid) return FALSE;
    }
    return TRUE;
}

BOOL ConfigureFileManagerUiTestConfigurationStore()
{
    FileManagerUiTestConfigurationStore = FALSE;
    if (!IsFileManagerUiTestSandboxRequested())
        return TRUE;

    char testDataRoot[MAX_PATH];
    char requestedRoot[200];
    DWORD length = GetEnvironmentVariableA("FILEMANAGER_UI_CONFIG_ROOT", requestedRoot, _countof(requestedRoot));
    if (!GetFileManagerUiTestDataRoot(testDataRoot, _countof(testDataRoot)) ||
        length == 0 || length >= _countof(requestedRoot) ||
        _stricmp(requestedRoot, FileManagerUiTestConfigurationRoot) != 0)
    {
        // Refuse a malformed sandbox rather than letting a test launch read or write the user's real configuration.
        TRACE_E("ConfigureFileManagerUiTestConfigurationStore(): invalid UI-test sandbox configuration.");
        return FALSE;
    }

    SalamanderConfigurationRoots[0] = FileManagerUiTestConfigurationRoot;
    FileManagerUiTestConfigurationStore = TRUE;
    return TRUE;
}

BOOL IsFileManagerUiTestConfigurationStore()
{
    return FileManagerUiTestConfigurationStore;
}
static char ConfigurationSchemaDiagnostic[256] = {};

static const char* GetConfigurationGenerationName(DWORD generation)
{
    return generation == 0 ? "Generation 0" : "Generation 1";
}

static void HashConfigurationBytes(DWORD& checksum, const void* data, DWORD size)
{
    const BYTE* bytes = (const BYTE*)data;
    for (DWORD i = 0; i < size; ++i)
    {
        checksum ^= bytes[i];
        checksum *= 16777619u;
    }
}

// The checksum deliberately excludes its own value and the completion marker.  Registry
// enumeration is stable for a fixed key, so this catches partial trees and changed values
// before an active-generation switch is allowed.
static BOOL CalculateConfigurationChecksum(HKEY key, DWORD& checksum)
{
    char name[513];
    DWORD nameSize;
    DWORD type;
    DWORD dataSize;
    LONG result;
    DWORD index = 0;

    for (;;)
    {
        nameSize = sizeof(name);
        result = RegEnumValue(key, index, name, &nameSize, NULL, &type, NULL, NULL);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS)
            return FALSE;
        name[nameSize] = 0;

        if (strcmp(name, CONFIGURATION_TRANSACTION_COMPLETE_REG) != 0 &&
            strcmp(name, CONFIGURATION_TRANSACTION_CHECKSUM_REG) != 0 &&
            strcmp(name, CONFIGURATION_SCHEMA_VERSION_REG) != 0)
        {
            result = RegQueryValueEx(key, name, NULL, &type, NULL, &dataSize);
            if (result != ERROR_SUCCESS)
                return FALSE;
            HashConfigurationBytes(checksum, name, nameSize + 1);
            HashConfigurationBytes(checksum, &type, sizeof(type));
            HashConfigurationBytes(checksum, &dataSize, sizeof(dataSize));
            BYTE* data = dataSize == 0 ? NULL : (BYTE*)malloc(dataSize);
            if (dataSize != 0 && data == NULL)
                return FALSE;
            DWORD readSize = dataSize;
            result = RegQueryValueEx(key, name, NULL, &type, data, &readSize);
            if (result == ERROR_SUCCESS && readSize == dataSize)
                HashConfigurationBytes(checksum, data, dataSize);
            if (data != NULL)
                free(data);
            if (result != ERROR_SUCCESS || readSize != dataSize)
                return FALSE;
        }
        ++index;
    }

    index = 0;
    for (;;)
    {
        nameSize = sizeof(name);
        result = RegEnumKeyEx(key, index, name, &nameSize, NULL, NULL, NULL, NULL);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS)
            return FALSE;
        name[nameSize] = 0;
        HashConfigurationBytes(checksum, name, nameSize + 1);
        HKEY child;
        if (RegOpenKeyEx(key, name, 0, KEY_READ, &child) != ERROR_SUCCESS)
            return FALSE;
        BOOL childOK = CalculateConfigurationChecksum(child, checksum);
        RegCloseKey(child);
        if (!childOK)
            return FALSE;
        ++index;
    }
    return TRUE;
}

static BOOL GetRequiredDword(HKEY key, const char* valueName, DWORD& value)
{
    return GetValueAux(NULL, key, valueName, REG_DWORD, &value, sizeof(value));
}

static BOOL GetRequiredInt(HKEY key, const char* valueName, int& value)
{
    DWORD rawValue;
    if (!GetRequiredDword(key, valueName, rawValue))
        return FALSE;
    value = (int)rawValue;
    return TRUE;
}

static BOOL ValidateRequiredDwordRange(HKEY key, const char* valueName, DWORD minimum, DWORD maximum)
{
    DWORD value;
    return GetRequiredDword(key, valueName, value) && value >= minimum && value <= maximum;
}

static BOOL ValidateRequiredPercentage(HKEY key, const char* valueName)
{
    char value[20];
    char extra;
    double percentage;
    return GetValueAux(NULL, key, valueName, REG_SZ, value, sizeof(value)) &&
           sscanf(value, "%lf%c", &percentage, &extra) == 1 &&
           percentage >= 0.0 && percentage <= 100.0;
}

static BOOL ValidateWindowConfigurationSchema(HKEY generationKey)
{
    HKEY windowKey = NULL;
    int left, right, top, bottom;
    BOOL valid = OpenKeyAux(NULL, generationKey, "Window", windowKey) &&
                 GetRequiredInt(windowKey, "Left", left) &&
                 GetRequiredInt(windowKey, "Right", right) &&
                 GetRequiredInt(windowKey, "Top", top) &&
                 GetRequiredInt(windowKey, "Bottom", bottom) &&
                 left < right && top < bottom &&
                 ValidateRequiredDwordRange(windowKey, "Show", 0, 11) &&
                 ValidateRequiredPercentage(windowKey, "Split Position") &&
                 ValidateRequiredPercentage(windowKey, "Before Zoom Split Position");
    if (windowKey != NULL)
        CloseKeyAux(windowKey);
    return valid;
}

// This is the schema gate for a complete saved host profile.  The transaction checksum
// protects every persisted value; the checks below reject values that are individually
// well-formed but unsafe together before LoadConfig exposes the snapshot to global state.
static BOOL ValidateCompleteConfigurationSchema(HKEY generationKey)
{
    DWORD schemaVersion;
    DWORD configVersion;
    DWORD visibleDrives;
    DWORD separatedDrives;
    HKEY versionKey = NULL;
    HKEY configKey = NULL;
    HKEY viewerKey = NULL;
    BOOL valid = GetRequiredDword(generationKey, CONFIGURATION_SCHEMA_VERSION_REG, schemaVersion) &&
                 schemaVersion == CONFIGURATION_SCHEMA_VERSION &&
                 OpenKeyAux(NULL, generationKey, "Version", versionKey) &&
                 // Keep validation aligned with the long-standing writer key, or every staged save is rejected.
                 GetRequiredDword(versionKey, SALAMANDER_VERSIONREG_REG, configVersion) &&
                 configVersion == THIS_CONFIG_VERSION &&
                 OpenKeyAux(NULL, generationKey, "Configuration", configKey) &&
                 ValidateRequiredDwordRange(configKey, "Size Format", SIZE_FORMAT_BYTES, SIZE_FORMAT_MIXED) &&
                 ValidateRequiredDwordRange(configKey, "Thumbnail Size", THUMBNAIL_SIZE_MIN, THUMBNAIL_SIZE_MAX) &&
                 ValidateRequiredDwordRange(configKey, "Title bar mode", TITLE_BAR_MODE_DIRECTORY, TITLE_BAR_MODE_FULLPATH) &&
                 ValidateRequiredDwordRange(configKey, "Make File List Destination", 0, 2) &&
                 ValidateRequiredDwordRange(configKey, "Time Resolution", 0, 3600) &&
                 ValidateRequiredDwordRange(configKey, "DragDrop Min Time", 0, 60000) &&
                 GetRequiredDword(configKey, "Visible Drives", visibleDrives) &&
                 GetRequiredDword(configKey, "Separated Drives", separatedDrives) &&
                 (visibleDrives & ~DRIVES_MASK) == 0 &&
                 (separatedDrives & ~DRIVES_MASK) == 0 &&
                 (separatedDrives & ~visibleDrives) == 0 &&
                 OpenKeyAux(NULL, generationKey, "Viewer", viewerKey) &&
                 ValidateRequiredDwordRange(viewerKey, "Tabelator Size", 1, 30) &&
                 ValidateRequiredDwordRange(viewerKey, "Default Mode", 0, 2) &&
                 ValidateWindowConfigurationSchema(generationKey) &&
                 ValidateConfigurationPayloadCollections(generationKey);
    if (viewerKey != NULL)
        CloseKeyAux(viewerKey);
    if (configKey != NULL)
        CloseKeyAux(configKey);
    if (versionKey != NULL)
        CloseKeyAux(versionKey);
    return valid;
}

// Schema migrations are explicit and idempotent.  Schema version 0 is the original
// transactional snapshot format; adding its version marker is safe because that marker
// is intentionally excluded from the content checksum above.
static BOOL MigrateConfigurationSchema(HKEY generationKey)
{
    DWORD schemaVersion = 0;
    DWORD valueType = 0;
    DWORD valueSize = sizeof(schemaVersion);
    LONG readResult = RegQueryValueEx(generationKey, CONFIGURATION_SCHEMA_VERSION_REG, NULL,
                                      &valueType, (BYTE*)&schemaVersion, &valueSize);
    if (readResult == ERROR_FILE_NOT_FOUND)
        schemaVersion = 0;
    else if (readResult != ERROR_SUCCESS || valueType != REG_DWORD || valueSize != sizeof(schemaVersion))
        return FALSE;
    if (schemaVersion > CONFIGURATION_SCHEMA_VERSION)
        return FALSE;

    while (schemaVersion < CONFIGURATION_SCHEMA_VERSION)
    {
        switch (schemaVersion)
        {
        case 0:
            schemaVersion = 1;
            if (!SetValue(generationKey, CONFIGURATION_SCHEMA_VERSION_REG, REG_DWORD,
                          &schemaVersion, sizeof(schemaVersion)))
                return FALSE;
            break;

        default:
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL IsCommittedConfigurationGeneration(HKEY generationKey)
{
    DWORD complete = 0;
    DWORD expectedChecksum = 0;
    DWORD checksum = 2166136261u;
    HKEY configKey = NULL;
    HKEY versionKey = NULL;
    if (!GetValueAux(NULL, generationKey, CONFIGURATION_TRANSACTION_COMPLETE_REG, REG_DWORD,
                     &complete, sizeof(complete)) ||
        complete != 1 ||
        !GetValueAux(NULL, generationKey, CONFIGURATION_TRANSACTION_CHECKSUM_REG, REG_DWORD,
                     &expectedChecksum, sizeof(expectedChecksum)) ||
        !OpenKeyAux(NULL, generationKey, "Configuration", configKey) ||
        !OpenKeyAux(NULL, generationKey, "Version", versionKey))
    {
        if (configKey != NULL)
            CloseKeyAux(configKey);
        if (versionKey != NULL)
            CloseKeyAux(versionKey);
        return FALSE;
    }
    CloseKeyAux(configKey);
    CloseKeyAux(versionKey);
    return CalculateConfigurationChecksum(generationKey, checksum) && checksum == expectedChecksum;
}

BOOL OpenCommittedConfigurationGeneration(HKEY storeKey, DWORD generation, HKEY& generationKey)
{
    HKEY generationsKey = NULL;
    generationKey = NULL;
    if (!OpenKeyAux(NULL, storeKey, CONFIGURATION_GENERATIONS_REG, generationsKey))
    {
        return FALSE;
    }
    if (!OpenKeyAux(NULL, generationsKey, GetConfigurationGenerationName(generation), generationKey))
    {
        CloseKeyAux(generationsKey);
        return FALSE;
    }
    CloseKeyAux(generationsKey);
    if (IsCommittedConfigurationGeneration(generationKey) &&
        MigrateConfigurationSchema(generationKey) &&
        ValidateCompleteConfigurationSchema(generationKey))
        return TRUE;
    CloseKeyAux(generationKey);
    return FALSE;
}

const char* GetConfigurationSchemaDiagnostic()
{
    return ConfigurationSchemaDiagnostic[0] == 0 ? NULL : ConfigurationSchemaDiagnostic;
}

const char* GetConfigurationStoreRoot()
{
    // Snapshot paths are transient; migration fallbacks must use the stable version root instead.
    return ConfigurationStoreRoot[0] != 0 ? ConfigurationStoreRoot : NULL;
}

void SetConfigurationStoreRoot(const char* root)
{
    ConfigurationStoreRoot[0] = 0;
    ActiveConfigurationRoot[0] = 0;
    LoadedConfigurationTokenValid = FALSE;
    SALAMANDER_ROOT_REG = root;
    if (root != NULL)
    {
        // The configuration-store root is a registry identity and must never be retained truncated.
        if (FAILED(StringCchCopyA(ConfigurationStoreRoot, _countof(ConfigurationStoreRoot), root)))
            ConfigurationStoreRoot[0] = 0;
    }
}

BOOL SelectCommittedConfigurationGeneration()
{
    if (ConfigurationStoreRoot[0] == 0)
        return FALSE;

    HKEY storeKey;
    if (!OpenKeyAux(NULL, HKEY_CURRENT_USER, ConfigurationStoreRoot, storeKey))
        return FALSE;

    ConfigurationSchemaDiagnostic[0] = 0;
    DWORD activeGeneration;
    BOOL selected = FALSE;
    if (GetValueAux(NULL, storeKey, CONFIGURATION_ACTIVE_GENERATION_REG, REG_DWORD,
                    &activeGeneration, sizeof(activeGeneration)) && activeGeneration <= 1)
    {
        HKEY generationKey;
        if (OpenCommittedConfigurationGeneration(storeKey, activeGeneration, generationKey))
        {
            CloseKeyAux(generationKey);
            selected = TRUE;
        }
        else
        {
            DWORD fallbackGeneration = 1 - activeGeneration;
            if (OpenCommittedConfigurationGeneration(storeKey, fallbackGeneration, generationKey))
            {
                // The interrupted/corrupt active slot is never exposed to normal readers.
                SetValueAux(NULL, storeKey, CONFIGURATION_ACTIVE_GENERATION_REG, REG_DWORD,
                            &fallbackGeneration, sizeof(fallbackGeneration));
                RegFlushKey(storeKey);
                CloseKeyAux(generationKey);
                activeGeneration = fallbackGeneration;
                selected = TRUE;
                // Schema diagnostics are bounded presentation text.
                StringCchCopyNA(ConfigurationSchemaDiagnostic, _countof(ConfigurationSchemaDiagnostic),
                                "Open Salamander ignored an invalid configuration profile and restored the last verified profile.",
                                _countof(ConfigurationSchemaDiagnostic) - 1);
            }
            else
                // Schema diagnostics are bounded presentation text.
                StringCchCopyNA(ConfigurationSchemaDiagnostic, _countof(ConfigurationSchemaDiagnostic),
                                "Open Salamander could not validate the saved configuration. The default profile will be used.",
                                _countof(ConfigurationSchemaDiagnostic) - 1);
        }
    }
    CloseKeyAux(storeKey);

    if (selected)
    {
        _snprintf_s(ActiveConfigurationRoot, _TRUNCATE, "%s\\%s\\%s", ConfigurationStoreRoot,
                    CONFIGURATION_GENERATIONS_REG, GetConfigurationGenerationName(activeGeneration));
        SALAMANDER_ROOT_REG = ActiveConfigurationRoot;
    }
    else
    {
        // Legacy and imported configurations remain readable; the next save migrates them.
        SALAMANDER_ROOT_REG = ConfigurationStoreRoot;
    }
    return selected;
}

BOOL UsesTransactionalConfigurationStore()
{
    return ConfigurationStoreRoot[0] != 0 && SALAMANDER_ROOT_REG == ActiveConfigurationRoot;
}

BOOL BeginConfigurationTransaction(HKEY& storeKey, HKEY& generationKey, DWORD& generation)
{
    storeKey = NULL;
    generationKey = NULL;
    if (ConfigurationStoreRoot[0] == 0 ||
        !CreateKey(HKEY_CURRENT_USER, ConfigurationStoreRoot, storeKey))
        return FALSE;

    DWORD activeGeneration = 1, selectorType = 0, selectorLength = sizeof(activeGeneration);
    const LONG selectorResult = RegQueryValueExA(storeKey, CONFIGURATION_ACTIVE_GENERATION_REG, NULL,
        &selectorType, (BYTE*)&activeGeneration, &selectorLength);
    // A failed selector read must never choose and clear a possibly active slot.
    if (selectorResult != ERROR_FILE_NOT_FOUND &&
        (selectorResult != ERROR_SUCCESS || selectorType != REG_DWORD ||
         selectorLength != sizeof(activeGeneration) || activeGeneration > 1))
    {
        RecordConfigurationPayloadFailure(selectorResult != ERROR_SUCCESS ? selectorResult : ERROR_INVALID_DATA);
        CloseKey(storeKey);
        return FALSE;
    }
    generation = selectorResult == ERROR_FILE_NOT_FOUND ? 0 : 1 - activeGeneration;

    HKEY generationsKey = NULL;
    if (!CreateKey(storeKey, CONFIGURATION_GENERATIONS_REG, generationsKey) ||
        !CreateKey(generationsKey, GetConfigurationGenerationName(generation), generationKey))
    {
        if (generationsKey != NULL)
            CloseKey(generationsKey);
        CloseKey(storeKey);
        return FALSE;
    }
    CloseKey(generationsKey);
    if (!ClearKey(generationKey))
    {
        CloseKey(generationKey);
        CloseKey(storeKey);
        return FALSE;
    }
    // New payloads require intended collection counts; legacy profiles remain
    // readable, and this marker participates in the full-tree checksum.
    const DWORD payloadVersion = 1;
    GUID identity;
    if (FAILED(CoCreateGuid(&identity)))
    {
        RecordConfigurationPayloadFailure(ERROR_NOT_ENOUGH_MEMORY);
        CloseKey(generationKey);
        CloseKey(storeKey);
        return FALSE;
    }
    if (!SetValue(generationKey, CONFIGURATION_GENERATION_TOKEN_REG, REG_BINARY, &identity, sizeof(identity)) ||
        !SetValue(generationKey, CONFIGURATION_PAYLOAD_VERSION_REG, REG_DWORD, &payloadVersion, sizeof(payloadVersion)))
    {
        CloseKey(generationKey);
        CloseKey(storeKey);
        return FALSE;
    }
    return TRUE;
}

BOOL CommitConfigurationTransaction(HKEY storeKey, HKEY generationKey, DWORD generation)
{
    DWORD checksum = 2166136261u;
    DWORD complete = 1;
    // Named fault points make the atomic tail independently testable without inferring its indices from plug-in payload size.
    BOOL committed = ConfigurationPayloadWritesSucceeded() &&
                     MigrateConfigurationSchema(generationKey) &&
                     ValidateCompleteConfigurationSchema(generationKey) &&
                     CalculateConfigurationChecksum(generationKey, checksum) &&
                     PassConfigurationTransactionFaultPoint(
                         SetValue(generationKey, CONFIGURATION_TRANSACTION_CHECKSUM_REG, REG_DWORD,
                                  &checksum, sizeof(checksum)),
                         "checksum") &&
                     PassConfigurationTransactionFaultPoint(
                         SetValue(generationKey, CONFIGURATION_TRANSACTION_COMPLETE_REG, REG_DWORD,
                                  &complete, sizeof(complete)),
                         "complete") &&
                     PassConfigurationTransactionFaultPoint(
                         FlushConfigurationRegistryKey(generationKey) == ERROR_SUCCESS,
                         "generation-flush") &&
                     IsCommittedConfigurationGeneration(generationKey) &&
                     ConfigurationPayloadWritesSucceeded() &&
                     PassConfigurationTransactionFaultPoint(
                         SetValue(storeKey, CONFIGURATION_ACTIVE_GENERATION_REG, REG_DWORD,
                                  &generation, sizeof(generation)),
                         "selector") &&
                     PassConfigurationTransactionFaultPoint(
                         FlushConfigurationRegistryKey(storeKey) == ERROR_SUCCESS,
                         "store-flush");
    if (committed)
    {
        _snprintf_s(ActiveConfigurationRoot, _TRUNCATE, "%s\\%s\\%s", ConfigurationStoreRoot,
                    CONFIGURATION_GENERATIONS_REG, GetConfigurationGenerationName(generation));
        SALAMANDER_ROOT_REG = ActiveConfigurationRoot; // the selector write above is the atomic commit point
    }
    return committed;
}

void RetirePreviousConfigurationGenerationAfterSuccessfulStartup()
{
    // Do not discard the fallback until this process has successfully restored the chosen generation.
    if (ConfigurationStoreRoot[0] == 0 || SALAMANDER_ROOT_REG != ActiveConfigurationRoot ||
        !LoadedConfigurationTokenValid)
        return;
    // Keep selector revalidation and optional deletion in the same cross-process
    // critical section used by staging, committing, and loading generations.
    if (!ConfigurationRetirementTestBarrier("before-lock")) return;
    if (!LoadSaveToRegistryMutex.EnterChecked()) return;
    HKEY storeKey;
    if (OpenKeyAux(NULL, HKEY_CURRENT_USER, ConfigurationStoreRoot, storeKey))
    {
        DWORD activeGeneration;
        if (GetValueAux(NULL, storeKey, CONFIGURATION_ACTIVE_GENERATION_REG, REG_DWORD,
                        &activeGeneration, sizeof(activeGeneration)) && activeGeneration <= 1 &&
            ConfigurationRetirementTestBarrier("after-selector"))
        {
            HKEY activeKey = NULL;
            GUID activeToken;
            if (OpenCommittedConfigurationGeneration(storeKey, activeGeneration, activeKey))
            {
                const BOOL confirmed = ReadConfigurationGenerationToken(activeKey, activeToken) &&
                    memcmp(&activeToken, &LoadedConfigurationToken, sizeof(activeToken)) == 0;
                CloseKeyAux(activeKey);
                HKEY generationsKey;
                if (confirmed && OpenKeyAux(NULL, storeKey, CONFIGURATION_GENERATIONS_REG, generationsKey))
                {
                    SHDeleteKey(generationsKey, GetConfigurationGenerationName(1 - activeGeneration));
                    CloseKeyAux(generationsKey);
                }
            }
        }
        CloseKeyAux(storeKey);
    }
    LoadSaveToRegistryMutex.Leave();
    ConfigurationRetirementTestBarrier("finished");
}
