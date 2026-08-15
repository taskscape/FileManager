// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h>

#include "datarh.h"

BOOL PathAppend(char* path, const char* name, int pathSize);

BOOL FileExists(const char* fileName)
{
    DWORD attr = GetFileAttributesUtf8Local(fileName);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

BOOL TrimOnSecondUnderscore(char* masked, size_t maskedCapacity)
{
    size_t index = 0;
    int cnt = 0;
    while (index < maskedCapacity && masked[index] != 0)
    {
        if (masked[index] == '_')
        {
            cnt++;
            if (cnt == 2)
            {
                // The wildcard replacement needs two writable bytes after the separator.
                if (index + 2 >= maskedCapacity)
                    return FALSE;
                masked[index + 1] = '*';
                masked[index + 2] = 0;
                return TRUE;
            }
        }
        index++;
    }
    return FALSE;
}

template<size_t capacity>
static BOOL CopyMUIPath(char (&destination)[capacity], const char* source)
{
    // MUI discovery must keep complete path identities before probing or loading resources.
    return SUCCEEDED(StringCchCopyA(destination, capacity, source));
}

// try to locate 'fileName' in the translated tree rooted at 'translatedMUIRoot' under the subpath 'originalMUIDir'
// if found, store its path in 'translatedFileName' and return TRUE
// otherwise return FALSE
BOOL LookupForTranslatedFile(const char* originalMUIRoot, const char* originalMUISubDir, const char* fileName,
                             const char* translatedMUIRoot, char* translatedFileName, size_t translatedFileNameCapacity)
{
    BOOL ret = FALSE;
    char buff[MAX_PATH];
    if (!CopyMUIPath(buff, translatedMUIRoot))
        return FALSE;

    // the directory name ends with numbers after the second underscore that we must trim because they differ for each localization
    char trim[MAX_PATH];
    if (!CopyMUIPath(trim, originalMUISubDir) ||
        !TrimOnSecondUnderscore(trim, _countof(trim)) ||
        !PathAppend(buff, trim, _countof(buff)))
        return FALSE;

    WIN32_FIND_DATA find;
    HANDLE hFind = HANDLES_Q(FindFirstFile(buff, &find));
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        { // look for the first level of subdirectories
            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (find.cFileName[0] != 0 && strcmp(find.cFileName, ".") != 0 && strcmp(find.cFileName, "..") != 0)
                {
                    char foundFile[MAX_PATH];
                    if (CopyMUIPath(foundFile, translatedMUIRoot) &&
                        PathAppend(foundFile, find.cFileName, _countof(foundFile)) &&
                        PathAppend(foundFile, fileName, _countof(foundFile)) &&
                        FileExists(foundFile) &&
                        SUCCEEDED(StringCchCopyA(translatedFileName, translatedFileNameCapacity, foundFile)))
                    {
                        ret = TRUE;
                    }
                }
            }
        } while (!ret && FindNextFile(hFind, &find));
        HANDLES(FindClose(hFind));
    }

    return ret;
}

BOOL EnumMUIFiles(CData* data, const char* originalMUIRoot, const char* originalMUISubPath, const char* translatedMUIRoot)
{
    char buff[MAX_PATH];
    if (!CopyMUIPath(buff, originalMUIRoot) ||
        !PathAppend(buff, originalMUISubPath, _countof(buff)) ||
        !PathAppend(buff, "*.mui", _countof(buff)))
        return TRUE;

    WIN32_FIND_DATA find;
    HANDLE hFind = HANDLES_Q(FindFirstFile(buff, &find));
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        { // look for files
            if ((find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                if (find.cFileName[0] != 0)
                {
                    char originalFileName[MAX_PATH];
                    if (!CopyMUIPath(originalFileName, originalMUIRoot) ||
                        !PathAppend(originalFileName, originalMUISubPath, _countof(originalFileName)) ||
                        !PathAppend(originalFileName, find.cFileName, _countof(originalFileName)))
                        continue;

                    char translatedFileName[MAX_PATH];
                    if (LookupForTranslatedFile(originalMUIRoot, originalMUISubPath, find.cFileName, translatedMUIRoot,
                                                translatedFileName, _countof(translatedFileName)))
                    {
                        // load resources from the original and translated DLL
                        data->Load(originalFileName, translatedFileName, FALSE);
                    }
                    else
                    {
                        TRACE_I("Ignoring original file " << originalFileName);
                    }
                }
            }
        } while (FindNextFile(hFind, &find));
        HANDLES(FindClose(hFind));
    }

    return TRUE;
}

BOOL EnumMUIDirectories(CData* data, const char* originalMUIRoot, const char* translatedMUIRoot)
{
    char buff[MAX_PATH];
    if (!CopyMUIPath(buff, originalMUIRoot) || !PathAppend(buff, "*.*", _countof(buff)))
        return TRUE;

    WIN32_FIND_DATA find;
    HANDLE hFind = HANDLES_Q(FindFirstFile(buff, &find));
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        { // look for the first level of subdirectories
            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (find.cFileName[0] != 0 && strcmp(find.cFileName, ".") != 0 && strcmp(find.cFileName, "..") != 0)
                {
                    // found a directory that may already contain its own *.dll.mui files
                    // inspect them
                    EnumMUIFiles(data, originalMUIRoot, find.cFileName, translatedMUIRoot);
                }
            }
        } while (FindNextFile(hFind, &find));
        HANDLES(FindClose(hFind));
    }
    return TRUE;
}

BOOL CData::LoadMUIPackages(const char* originalMUI, const char* translatedMUI)
{
    // clear the existing data
    StrData.DestroyMembers();
    MenuData.DestroyMembers();
    DlgData.DestroyMembers();
    CleanTranslationStates();
    SalMenuSections.DestroyMembers();
    IgnoreLstItems.DestroyMembers();
    CheckLstItems.DestroyMembers();
    DataRH.Clean();

    MUIMode = TRUE;
    MUIDialogID = 1; // reset the counter for unique IDs
    MUIMenuID = 1;
    MUIStringID = 1;

    EnumMUIDirectories(this, originalMUI, translatedMUI);

    return TRUE;
}
