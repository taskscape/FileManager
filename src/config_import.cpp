// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

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

#include "config_store.h" // shared transactional-store internals

#include <strsafe.h>

// Configuration import/upgrade from previous Salamander versions extracted from
// mainwnd_config.cpp as a mechanical move: AutoImportConfig handling, language
// detection, old-configuration discovery, registry-key rename helper, and
// deletion of superseded configurations.
//****************************************************************************
//
// GetUpgradeInfo
//
// Tries to find "AutoImportConfig" in the configuration key of this version of Salamander.
// If it is not found or if the key stored in AutoImportConfig does not exist
// (points to the key of this version, which makes no sense)
// or if it contains a corrupted (incomplete save) or empty configuration, it returns
// FALSE in 'autoImportConfig'. Otherwise it returns TRUE in 'autoImportConfig' and
// in 'autoImportConfigFromKey' returns the path of the key from which to import the configuration.
// Handles the case when AutoImportConfig points to a key that itself contains AutoImportConfig
// for another key. We simply follow the "target" key and leave intermediate keys untouched-
// if the import succeeds, the target key will be removed anyway. Returns FALSE only if the application should exit.
//
// If the configuration key of this version contains, besides AutoImportConfig, also the "Configuration"
// key (expected to be a saved configuration),
// we ask the user whether to:
//   - Use the current configuration and ignore the old one (we do not delete it so the user does not lose data,
//     and it does not require that much space anyway). In this case delete AutoImportConfig immediately.
//     This is done silently, if AutoImportConfig points to this version of Salamander`s key
//     (DEFAULT OFFER because it does not cause data loss and users may dismiss the message box without reading).
//   - Delete the current configuration and import the old version. In this case remove everything except AutoImportConfig.
//   - Exit the application - simply return FALSE.

BOOL GetUpgradeInfo(BOOL* autoImportConfig, char* autoImportConfigFromKey, int autoImportConfigFromKeySize)
{
    HKEY rootKey;
    DWORD saveInProgress; // dummy
    BOOL doNotExit = TRUE;
    if (autoImportConfigFromKeySize > 0)
        *autoImportConfigFromKey = 0;
    if (IsFileManagerUiTestConfigurationStore())
    {
        // Test configuration must start clean and must never import an older user-profile configuration.
        *autoImportConfig = FALSE;
        return TRUE;
    }
    LoadSaveToRegistryMutex.Enter();
    int rounds = 0; // prevent infinite loops
    *autoImportConfig = FALSE;
    if (HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], 0,
                               KEY_READ, &rootKey)) == ERROR_SUCCESS)
    {
        HKEY oldCfgKey;
        char oldKeyName[200];

        if (GetValue(rootKey, SALAMANDER_AUTO_IMPORT_CONFIG, REG_SZ, oldKeyName, 200))
        { // we found "AutoImportConfig"
        OPEN_AUTO_IMPORT_CONFIG_KEY:
            // An import target is a registry identity; validation must not use a clipped parent key.
            if (SUCCEEDED(StringCchCopyA(autoImportConfigFromKey, autoImportConfigFromKeySize, SalamanderConfigurationRoots[0])) &&
                CutDirectory(autoImportConfigFromKey) &&
                SalPathAppend(autoImportConfigFromKey, oldKeyName, autoImportConfigFromKeySize) &&
                !IsTheSamePath(autoImportConfigFromKey, SalamanderConfigurationRoots[0]) &&     // the key stored in AutoImportConfig does not point to this version's key
                HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, autoImportConfigFromKey, 0, KEY_READ, // the key stored in AutoImportConfig can be opened (otherwise it doesn't exist?)
                                       &oldCfgKey)) == ERROR_SUCCESS)
            {
                // if the current "target" key also contains AutoImportConfig, follow it...
                if (GetValue(oldCfgKey, SALAMANDER_AUTO_IMPORT_CONFIG, REG_SZ, oldKeyName, 200) && ++rounds <= 50)
                {
                    HANDLES(RegCloseKey(oldCfgKey));
                    goto OPEN_AUTO_IMPORT_CONFIG_KEY;
                }
                HKEY cfgKey;
                if (rounds <= 50 &&
                    !GetValue(oldCfgKey, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)) &&
                    HANDLES_Q(RegOpenKeyEx(oldCfgKey, SALAMANDER_CONFIG_REG, 0, KEY_READ, &cfgKey)) == ERROR_SUCCESS)
                {
                    HANDLES(RegCloseKey(cfgKey));
                    *autoImportConfig = TRUE; // configuration is valid and not empty
                }
                HANDLES(RegCloseKey(oldCfgKey));
            }
        }
        if (*autoImportConfig) // check whether this version's key also contains configuration (besides "AutoImportConfig")
        {
            HKEY cfgKey;
            // Probe the complete current configuration key before deciding whether to overwrite it.
            if (SUCCEEDED(StringCchCopyA(oldKeyName, _countof(oldKeyName), SalamanderConfigurationRoots[0])) &&
                SalPathAppend(oldKeyName, SALAMANDER_CONFIG_REG, 200) &&
                HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, oldKeyName, 0, KEY_READ, &cfgKey)) == ERROR_SUCCESS)
            {
                HANDLES(RegCloseKey(cfgKey));
                BOOL clearCfg = FALSE;
                if (!GetValue(rootKey, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)))
                { // this key contains a valid configuration; ask the user what to do
                    HANDLES(RegCloseKey(rootKey));
                    rootKey = NULL;
                    LoadSaveToRegistryMutex.Leave();

                    MSGBOXEX_PARAMS params;
                    memset(&params, 0, sizeof(params));
                    params.HParent = NULL;
                    params.Flags = MB_ABORTRETRYIGNORE | MB_ICONQUESTION | MB_SETFOREGROUND;
                    params.Caption = SALAMANDER_TEXT_VERSION;
                    // The confirmation dialog displays a bounded registry-key tail.
                    StringCchCopyNA(oldKeyName, _countof(oldKeyName), autoImportConfigFromKey, _countof(oldKeyName) - 1);
                    char* keyName;
                    if (!CutDirectory(oldKeyName, &keyName))
                        keyName = oldKeyName; // theoretically cannot happen
                    char buf[1000];
                    sprintf(buf, "You have upgraded from %s (old version) to %s (new version). The configuration of the old "
                                 "version should be imported to the new version now, but there is already existing "
                                 "configuration for the new version. You can use this existing configuration (the configuration of "
                                 "the old version remains in registry, so you can import it later). Or you can overwrite "
                                 "this existing configuration (it would be lost) with the configuration of the old version. "
                                 "Or you can exit Open Salamander and solve this problem later.",
                            keyName, SALAMANDER_TEXT_VERSION);
                    params.Text = buf;
                    char aliasBtnNames[200];
                    sprintf(aliasBtnNames, "%d\t%s\t%d\t%s\t%d\t%s",
                            DIALOG_ABORT, "&Use Existing Configuration",
                            DIALOG_RETRY, "&Overwrite Existing Configuration",
                            DIALOG_IGNORE, "&Exit");
                    params.AliasBtnNames = aliasBtnNames;
                    int res = SalMessageBoxEx(&params);
                    switch (res)
                    {
                    case DIALOG_ABORT:
                        *autoImportConfig = FALSE;
                        break;
                    case DIALOG_RETRY:
                        clearCfg = TRUE;
                        break;

                    // case DIALOG_IGNORE:
                    default:
                        doNotExit = FALSE;
                        break;
                    }

                    LoadSaveToRegistryMutex.Enter();
                }
                else
                    clearCfg = TRUE; // configuration is corrupted, delete it
                if (clearCfg &&
                    HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], 0,
                                           KEY_READ | KEY_WRITE, &cfgKey)) == ERROR_SUCCESS)
                { // delete the configuration and leave only "AutoImportConfig" (recreate it)
                    ClearKey(cfgKey);
                    // The recreated import value must preserve the complete target-key identity.
                    if (FAILED(StringCchCopyA(oldKeyName, _countof(oldKeyName), autoImportConfigFromKey)))
                        oldKeyName[0] = 0;
                    char* keyName;
                    if (!CutDirectory(oldKeyName, &keyName))
                        keyName = oldKeyName; // theoretically cannot happen
                    SetValue(cfgKey, SALAMANDER_AUTO_IMPORT_CONFIG, REG_SZ, keyName, -1);
                    HANDLES(RegCloseKey(cfgKey));
                }
            }
        }
        if (rootKey != NULL)
            HANDLES(RegCloseKey(rootKey));
    }
    if (!*autoImportConfig && // this version's key lacks "AutoImportConfig" or does not point to a valid old configuration
        HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], 0,
                               KEY_READ | KEY_WRITE, &rootKey)) == ERROR_SUCCESS)
    { // remove "AutoImportConfig" from this version's key (if it exists it makes no sense here)
        RegDeleteValue(rootKey, SALAMANDER_AUTO_IMPORT_CONFIG);
        HANDLES(RegCloseKey(rootKey));
    }
    LoadSaveToRegistryMutex.Leave();
    return doNotExit;
}

//****************************************************************************
//
// FindLanguageFromPrevVerOfSal
//
// Retrieves the language (the .slg module used) from an older version of Salamander.
// The oldest version from which we obtain this information is 2.53 beta 2 (the first version shipped with multiple languages: CZ+DE+EN).
// If a configuration for the current version exists or such a language is not found, returns FALSE.
// Otherwise returns the language in 'slgName' (MAX_PATH buffer).

BOOL FindLanguageFromPrevVerOfSal(char* slgName)
{
    HKEY hCfgKey;
    HKEY hRootKey;
    int rootIndex = 0;
    const char* root;
    DWORD saveInProgress; // dummy

    slgName[0] = 0;
    LoadSaveToRegistryMutex.Enter();
    do
    {
        // check if the key exists and if a configuration is stored under it
        root = SalamanderConfigurationRoots[rootIndex];
        BOOL rootFound = HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, root, 0, KEY_READ, &hRootKey)) == ERROR_SUCCESS;
        BOOL cfgFound = rootFound && HANDLES_Q(RegOpenKeyEx(hRootKey, SALAMANDER_CONFIG_REG, 0,
                                                            KEY_READ, &hCfgKey)) == ERROR_SUCCESS;
        if (cfgFound && GetValue(hRootKey, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)))
        { // the configuration is corrupted
            cfgFound = FALSE;
            HANDLES(RegCloseKey(hCfgKey));
        }
        DWORD configVersion = 1; // this is configuration from 1.52 or older
        if (cfgFound)
        {
            HKEY actKey;
            if (HANDLES_Q(RegOpenKeyEx(hRootKey, SALAMANDER_VERSION_REG, 0, KEY_READ, &actKey) == ERROR_SUCCESS))
            {
                configVersion = 2; // configuration from 1.6b1
                GetValue(actKey, SALAMANDER_VERSIONREG_REG, REG_DWORD, &configVersion, sizeof(DWORD));
                HANDLES(RegCloseKey(actKey));
            }
        }
        if (rootFound)
            HANDLES(RegCloseKey(hRootKey));
        if (cfgFound)
        {
            BOOL found = FALSE;
            if (rootIndex != 0 &&                      // only for one of the older keys
                configVersion >= 59 /* 2.53 beta 2 */) // before 2.53 beta 2 there was only English, so reading makes no sense; offer system default language or manual selection of the language
            {
                GetValue(hCfgKey, CONFIG_LANGUAGE_REG, REG_SZ, slgName, MAX_PATH);
                found = slgName[0] != 0;
            }
            HANDLES(RegCloseKey(hCfgKey));
            LoadSaveToRegistryMutex.Leave();
            return found;
        }
        rootIndex++;
    } while (rootIndex < (IsFileManagerUiTestConfigurationStore() ? 1 : SALCFG_ROOTS_COUNT));

    LoadSaveToRegistryMutex.Leave();
    return FALSE;
}

// obtains a number from a string (unsigned decimal format); returns TRUE, if a number was found
// ignores white spaces before and after the number
BOOL GetNumFromStr(const char* s, DWORD* retNum)
{
    DWORD n = 0;
    while (*s != 0 && *s <= ' ')
        s++;
    BOOL mayBeOK = *s >= '0' && *s <= '9';
    while (*s >= '0' && *s <= '9')
        n = 10 * n + (*s++ - '0');
    while (*s != 0 && *s <= ' ')
        s++;
    *retNum = n;
    return mayBeOK && *s == 0;
}

void CheckShutdownParams()
{
    // HKEY_CURRENT_USER\Control Panel\Desktop\WaitToKillAppTimeout=20000,REG_SZ ... warn if less than 20000
    // HKEY_CURRENT_USER\Control Panel\Desktop\AutoEndTasks=0,REG_SZ ... warn if not 0
    // W2K and XP have it; I could not find it on Vista but supposedly it is there (info from the internet)

    BOOL showWarning = FALSE;
    HKEY key;
    if (OpenKeyAux(NULL, HKEY_CURRENT_USER, "Control Panel\\Desktop", key))
    {
        char num[100];
        DWORD value;
        if (GetValueAux(NULL, key, "WaitToKillAppTimeout", REG_SZ, num, 100) &&
            GetNumFromStr(num, &value) && value < 20000)
        {
            TRACE_E("CheckShutdownParams(): WaitToKillAppTimeout is '" << num << "' (" << value << ")");
            showWarning = TRUE;
        }
        if (GetValueAux(NULL, key, "AutoEndTasks", REG_SZ, num, 100) &&
            GetNumFromStr(num, &value) && value != 0)
        {
            TRACE_E("CheckShutdownParams(): AutoEndTasks is '" << num << "' (" << value << ")");
            showWarning = TRUE;
        }
        CloseKeyAux(key);
    }

    if (showWarning)
        SalMessageBox(NULL, LoadStr(IDS_CHANGEDSHUTDOWNPARS), SALAMANDER_TEXT_VERSION, MB_OK | MB_ICONWARNING);
}

BOOL MyRegRenameKey(HKEY key, const char* name, const char* newName)
{
    BOOL ret = FALSE;
    // There is also NtRenameKey but I could not get it working (requires UNICODE_STRING
    // and probably the key opened via NtOpenKey with the key passed via OBJECT_ATTRIBUTES initialized through
    // InitializeObjectAttributes). It's overly complicated and not frequently used code,
    // so we'll do it the slow but simple way... copy the key to a new one and then delete the original
    HKEY newKey;
    if (!OpenKeyAux(NULL, key, newName, newKey)) // verify if the target key does not already exist
    {
        if (CreateKeyAux(NULL, key, newName, newKey)) // create the target key
        {
            // I also tried RegCopyTree (didn't work without KEY_ALL_ACCESS) and the speed was the same as SHCopyKey
            if (SHCopyKey(key, name, newKey, 0) == ERROR_SUCCESS) // copy into the target key
                ret = TRUE;
            CloseKeyAux(newKey);
            if (ret)
                SHDeleteKey(key, name);
        }
        else
            TRACE_E("MyRegRenameKey(): unable to create target key: " << newName);
    }
    else
    {
        CloseKeyAux(newKey);
        TRACE_E("MyRegRenameKey(): target key already exists: " << newName);
    }
    return ret;
}

//****************************************************************************
//
// FindLatestConfiguration
//
// Tries to find a configuration that matches our program version.
// If it succeeds, 'loadConfiguration' variable is set and the function returns TRUE.
// If a configuration for this version does not exist yet, the function scans
// older configurations from the 'SalamanderConfigurationRoots' array (from newest to oldest).
// When it finds one of the configurations, a dialog is shown offering to convert it into the current configuration
// and delete it from the registry. After the last dialog, it returns TRUE and fills
// 'deleteConfigurations' and 'loadConfiguration' according to the user's choice.
// If the user chooses to exit the application, the function returns FALSE.
//

BOOL FindLatestConfiguration(BOOL* deleteConfigurations, const char*& loadConfiguration)
{
    HKEY hRootKey;
    loadConfiguration = NULL; // we don't want to load any configuration - default values will be used
    int rootIndex = 0;
    const char* root;
    DWORD saveInProgress; // dummy
    HKEY hCfgKey;

    CImportConfigDialog dlg;
    ZeroMemory(dlg.ConfigurationExist, sizeof(dlg.ConfigurationExist)); // none of the configurations found yet
    dlg.DeleteConfigurations = deleteConfigurations;
    dlg.IndexOfConfigurationToLoad = -1;

    BOOL offerImportDlg = FALSE; // if an old configuration or keys exist, offer import

    LoadSaveToRegistryMutex.Enter();

    char backup[200];
    sprintf_s(backup, "%s.backup.63A7CD13", SalamanderConfigurationRoots[0]); // "63A7CD13" prevents the key name from matching the user name
    HKEY backupKey;
    BOOL backupFound = OpenKeyAux(NULL, HKEY_CURRENT_USER, backup, backupKey);
    if (backupFound)
    {
        DWORD copyIsOK;
        if (GetValueAux(NULL, backupKey, SALAMANDER_COPY_IS_OK, REG_DWORD, &copyIsOK, sizeof(DWORD)))
            copyIsOK = 1; // backup is valid
        else
            copyIsOK = 0; // backup is corrupted
        HANDLES(RegCloseKey(backupKey));
        if (!copyIsOK) // delete the corrupted backup and pretend it never existed (it probably wasn't fully created)
        {
            TRACE_I("Configuration backup is incomplete, removing... " << backup);
            SHDeleteKey(HKEY_CURRENT_USER, backup);
            backupFound = FALSE;
        }
        else
            TRACE_I("Configuration backup is OK: " << backup);
    }

    do
    {
        root = SalamanderConfigurationRoots[rootIndex];
        // check whether the key exists
        BOOL rootFound = OpenKeyAux(NULL, HKEY_CURRENT_USER, root, hRootKey);
        if (rootFound &&
            GetValueAux(NULL, hRootKey, SALAMANDER_SAVE_IN_PROGRESS, REG_DWORD, &saveInProgress, sizeof(DWORD)))
        { // this configuration is corrupted
            TRACE_E("Configuration is corrupted!");
            rootFound = FALSE;
            CloseKeyAux(hRootKey);
            if (rootIndex == 0 && backupFound) // use the backup, if available and don't bother the user
            {
                char corrupted[200];
                sprintf_s(corrupted, "%s.corrupted.63A7CD13", root); // "63A7CD13" prevents the key name from matching the user name
                SHDeleteKey(HKEY_CURRENT_USER, corrupted);           // if we already have a corrupted configuration, remove it-one is enough
                if (MyRegRenameKey(HKEY_CURRENT_USER, root, corrupted) &&
                    MyRegRenameKey(HKEY_CURRENT_USER, backup, root))
                {
                    backupFound = FALSE;
                    if (CreateKeyAux(NULL, HKEY_CURRENT_USER, root, hRootKey))
                    {
                        DeleteValueAux(hRootKey, SALAMANDER_COPY_IS_OK);
                        CloseKeyAux(hRootKey);
                    }
                    TRACE_I("Corrupted configuration was moved to: " << corrupted);
                    TRACE_I("Using configuration backup instead ...");
                    continue; // in the second pass load configuration from the backup created during "critical shutdown"
                }
                else
                    TRACE_E("Unable to move corrupted configuration or configuration backup.");
            }

            if (rootIndex == 0) // for the active version inform the user about the corrupted configuration and let them back up the key, then try to delete it (older versions - simply ignore the corrupted configuration)
            {
                char buf[1500];
                _snprintf_s(buf, _TRUNCATE, LoadStr(IDS_CORRUPTEDCONFIGFOUND), root);
                LoadSaveToRegistryMutex.Leave();

                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = NULL;
                params.Flags = MB_OKCANCEL | MB_ICONERROR | MB_DEFBUTTON2;
                params.Caption = SALAMANDER_TEXT_VERSION;
                params.Text = buf;
                char aliasBtnNames[200];
                /* used by the export_mnu.py script that generates salmenu.mnu for the Translator
   we let the message box buttons handle hotkey collisions by simulating it as a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_CORRUPTEDCONFIGREMOVEBTN
  {MNTT_IT, IDS_SELLANGEXITBUTTON
  {MNTT_PE, 0
};
*/
                sprintf(aliasBtnNames, "%d\t%s\t%d\t%s", DIALOG_OK, LoadStr(IDS_CORRUPTEDCONFIGREMOVEBTN),
                        DIALOG_CANCEL, LoadStr(IDS_SELLANGEXITBUTTON));
                params.AliasBtnNames = aliasBtnNames;
                if (SalMessageBoxEx(&params) == IDCANCEL)
                {
                    CheckShutdownParams(); // optionally show this warning; if they rename the key in the registry they might never see it
                    return FALSE;          // Exit
                }

                CheckShutdownParams();
                LoadSaveToRegistryMutex.Enter();
                if (HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, root, 0, KEY_READ | KEY_WRITE, &hRootKey)) == ERROR_SUCCESS)
                { // delete the corrupted configuration (if it's still there - user might have renamed it for backup)
                    TRACE_I("Deleting corrupted configuration on user demand: " << root);
                    ClearKeyAux(hRootKey);
                    CloseKeyAux(hRootKey);
                    DeleteKeyAux(HKEY_CURRENT_USER, root);
                }
            }
        }
        // A version root can contain either the legacy direct tree or a committed
        // generation.  Never offer a staging/incomplete generation as importable.
        BOOL cfgFound = FALSE;
        if (rootFound)
        {
            DWORD activeGeneration;
            HKEY generationKey;
            BOOL hasActiveGeneration = GetValueAux(NULL, hRootKey, CONFIGURATION_ACTIVE_GENERATION_REG, REG_DWORD,
                                                    &activeGeneration, sizeof(activeGeneration)) && activeGeneration <= 1;
            if (hasActiveGeneration &&
                (OpenCommittedConfigurationGeneration(hRootKey, activeGeneration, generationKey) ||
                 OpenCommittedConfigurationGeneration(hRootKey, 1 - activeGeneration, generationKey)))
            {
                cfgFound = OpenKeyAux(NULL, generationKey, SALAMANDER_CONFIG_REG, hCfgKey);
                CloseKeyAux(generationKey);
            }
            else
            {
                cfgFound = OpenKeyAux(NULL, hRootKey, SALAMANDER_CONFIG_REG, hCfgKey);
            }
        }
        if (rootFound)
            CloseKeyAux(hRootKey);

        if (rootIndex == 0 && backupFound) // backup not needed, remove it
        {
            TRACE_I("Removing unnecessary configuration backup: " << backup);
            SHDeleteKey(HKEY_CURRENT_USER, backup);
            backupFound = FALSE;
        }

        if (cfgFound) // a key is considered a configuration key only if the "Configuration" subkey exists (mere existence of the key isn't enough because it may contain only "AutoImportConfig")
        {
            CloseKeyAux(hCfgKey);
            if (rootIndex == 0)
            {
                // this is the key for the active program version => confirm loading it and return
                loadConfiguration = root;
                LoadSaveToRegistryMutex.Leave();
                return TRUE;
            }
            // this is one of the older keys

            // offer this configuration for import and deletion
            dlg.ConfigurationExist[rootIndex] = TRUE;
            offerImportDlg = TRUE;
        }
        rootIndex++;
    } while (rootIndex < SALCFG_ROOTS_COUNT);

    LoadSaveToRegistryMutex.Leave();

    if (offerImportDlg)
    {
        HWND hSplash = GetSplashScreenHandle(); // if a splash screen exists, temporarily hide it
        if (hSplash != NULL)
            ShowWindow(hSplash, SW_HIDE);

        int dlgRet = (int)dlg.Execute();

        if (hSplash != NULL)
        {
            ShowWindow(hSplash, SW_SHOW);
            UpdateWindow(hSplash);
        }

        if (dlgRet == IDCANCEL)
        {
            return FALSE; // user wants to quit Salamander
        }
        if (dlg.IndexOfConfigurationToLoad != -1)
            loadConfiguration = SalamanderConfigurationRoots[dlg.IndexOfConfigurationToLoad];
    }
    return TRUE;
}

// deletes keys according to the array returned by FindLatestConfiguration

void CMainWindow::DeleteOldConfigurations(BOOL* deleteConfigurations, BOOL autoImportConfig,
                                          const char* autoImportConfigFromKey,
                                          BOOL doNotDeleteImportedCfg)
{
    // anything to delete?
    BOOL dirty = FALSE;
    if (autoImportConfig)
        dirty = TRUE;
    else
    {
        int rootIndex;
        for (rootIndex = 0; rootIndex < SALCFG_ROOTS_COUNT; rootIndex++)
        {
            if (deleteConfigurations[rootIndex])
            {
                dirty = TRUE;
                break;
            }
        }
    }
    if (dirty)
    {
        // remove old configurations
        HCURSOR hOldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
        CWaitWindow analysing(HWindow, IDS_DELETINGCONFIGURATION, FALSE, ooStatic);
        analysing.Create();
        EnableWindow(HWindow, FALSE);
        LoadSaveToRegistryMutex.Enter();
        int rootIndex;
        for (rootIndex = 0; rootIndex < SALCFG_ROOTS_COUNT; rootIndex++)
        {
            if (deleteConfigurations[rootIndex])
            {
                HKEY hKey;
                const char* key = SalamanderConfigurationRoots[rootIndex];
                if (CreateKeyAux(NULL, HKEY_CURRENT_USER, key, hKey))
                {
                    ClearKeyAux(hKey);
                    CloseKeyAux(hKey);
                    DeleteKeyAux(HKEY_CURRENT_USER, key);
                }
            }
        }
        if (autoImportConfig) // clean old configuration (already stored in the new key) and remove "AutoImportConfig" from the new key
        {
            BOOL ok = FALSE;
            HKEY cfgKey;
            if (HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, SalamanderConfigurationRoots[0], 0,
                                       KEY_READ | KEY_WRITE, &cfgKey)) == ERROR_SUCCESS)
            { // remove "AutoImportConfig" value from the new key
                if (RegDeleteValue(cfgKey, SALAMANDER_AUTO_IMPORT_CONFIG) == ERROR_SUCCESS)
                    ok = TRUE;
                HANDLES(RegCloseKey(cfgKey));
            }
            if (!ok) // if this happens it's probably fine because we likely didn't
                     // write Salamander's configuration either (it goes to the
                     // same key) and the whole upgrade will need to be run again
            {
                TRACE_E("CMainWindow::DeleteOldConfigurations(): unable to delete " << SALAMANDER_AUTO_IMPORT_CONFIG << " value from HKCU\\" << SalamanderConfigurationRoots[0]);
            }
            else // clean the old configuration (already saved to the new key)
            {
                if (!doNotDeleteImportedCfg)
                {
                    if (HANDLES_Q(RegOpenKeyEx(HKEY_CURRENT_USER, autoImportConfigFromKey, 0,
                                               KEY_READ | KEY_WRITE, &cfgKey)) == ERROR_SUCCESS)
                    {
                        ClearKeyAux(cfgKey);
                        HANDLES(RegCloseKey(cfgKey));
                        DeleteKeyAux(HKEY_CURRENT_USER, autoImportConfigFromKey);
                    }
                }
            }
        }
        LoadSaveToRegistryMutex.Leave();
        EnableWindow(HWindow, TRUE);
        DestroyWindow(analysing.HWindow);
        SetCursor(hOldCursor);
    }
}
