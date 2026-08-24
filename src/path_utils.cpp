// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA)

#include <shobjidl.h>

#include "common\\thread_owner.h"

#include "mainwnd.h"
#include "usermenu.h"
#include "plugins.h"
#include "fileswnd.h"
#include "cfgdlg.h"
#include "dialogs.h"
#include "pack.h"
#include "execute.h"
#include "shellib.h"
#include "menu.h"

// ****************************************************************************

BOOL SalPathAppend(char* path, const char* name, int pathSize)
{
    if (path == NULL || name == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAppend()");
        return FALSE;
    }
    if (*name == '\\')
        name++;
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] == '\\')
        l--;
    if (*name != 0)
    {
        int n = (int)strlen(name);
        if (l + 1 + n < pathSize) // fits including trailing null?
        {
            if (l != 0)
                path[l] = '\\';
            else
                l = -1;
            memcpy(path + l + 1, name, n + 1);
        }
        else
            return FALSE;
    }
    else
        path[l] = 0;
    return TRUE;
}

BOOL SalPathAppendW(WCHAR* path, const WCHAR* name, int pathSizeInChars)
{
    if (path == NULL || name == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAppendW()");
        return FALSE;
    }
    if (*name == L'\\' || *name == L'/')
        name++;
    int l = (int)wcslen(path);
    if (l > 0 && (path[l - 1] == L'\\' || path[l - 1] == L'/'))
        l--;
    if (*name != 0)
    {
        int n = (int)wcslen(name);
        if (l + 1 + n < pathSizeInChars)
        {
            if (l != 0)
                path[l] = L'\\';
            else
                l = -1;
            memcpy(path + l + 1, name, (n + 1) * sizeof(WCHAR));
        }
        else
            return FALSE;
    }
    else
        path[l] = 0;
    return TRUE;
}

// ****************************************************************************

BOOL SalPathAddBackslash(char* path, int pathSize)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddBackslash()");
        return FALSE;
    }
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] != '\\')
    {
        if (l + 1 < pathSize)
        {
            path[l] = '\\';
            path[l + 1] = 0;
        }
        else
            return FALSE;
    }
    return TRUE;
}

BOOL SalPathAddBackslashW(WCHAR* path, int pathSizeInChars)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddBackslashW()");
        return FALSE;
    }
    int l = (int)wcslen(path);
    if (l > 0 && path[l - 1] != L'\\' && path[l - 1] != L'/')
    {
        if (l + 1 < pathSizeInChars)
        {
            path[l] = L'\\';
            path[l + 1] = 0;
        }
        else
            return FALSE;
    }
    return TRUE;
}

// ****************************************************************************

void SalPathRemoveBackslash(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveBackslash()");
        return;
    }
    int l = (int)strlen(path);
    if (l > 0 && path[l - 1] == '\\')
        path[l - 1] = 0;
}

void SalPathRemoveBackslashW(WCHAR* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveBackslashW()");
        return;
    }
    int l = (int)wcslen(path);
    if (l > 0 && (path[l - 1] == L'\\' || path[l - 1] == L'/'))
        path[l - 1] = 0;
}

void SalPathStripPath(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathStripPath()");
        return;
    }
    char* name = strrchr(path, '\\');
    if (name != NULL)
        memmove(path, name + 1, strlen(name + 1) + 1);
}

void SalPathStripPathW(WCHAR* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathStripPathW()");
        return;
    }
    WCHAR* name = wcsrchr(path, L'\\');
    WCHAR* forwardName = wcsrchr(path, L'/');
    if (forwardName != NULL && (name == NULL || forwardName > name))
        name = forwardName;
    if (name != NULL)
        memmove(path, name + 1, (wcslen(name + 1) + 1) * sizeof(WCHAR));
}

void SalPathRemoveExtension(char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveExtension()");
        return;
    }

    int len = (int)strlen(path);
    char* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == '.')
        {
            *iterator = 0;
            break;
        }
        if (*iterator == '\\')
            break;
    }
}

void SalPathRemoveExtensionW(WCHAR* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRemoveExtensionW()");
        return;
    }

    int len = (int)wcslen(path);
    WCHAR* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == L'.')
        {
            *iterator = 0;
            break;
        }
        if (*iterator == L'\\' || *iterator == L'/')
            break;
    }
}

BOOL SalPathAddExtension(char* path, const char* extension, int pathSize)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddExtension()");
        return FALSE;
    }

    int len = (int)strlen(path);
    char* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == '.')
        {
            return TRUE;
        }
        if (*iterator == '\\')
            break;
    }

    int extLen = (int)strlen(extension);
    if (len + extLen < pathSize)
    {
        memcpy(path + len, extension, extLen + 1);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL SalPathAddExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathAddExtensionW()");
        return FALSE;
    }

    int len = (int)wcslen(path);
    WCHAR* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == L'.')
        {
            return TRUE;
        }
        if (*iterator == L'\\' || *iterator == L'/')
            break;
    }

    int extLen = (int)wcslen(extension);
    if (len + extLen < pathSizeInChars)
    {
        memcpy(path + len, extension, (extLen + 1) * sizeof(WCHAR));
        return TRUE;
    }
    else
        return FALSE;
}

BOOL SalPathRenameExtension(char* path, const char* extension, int pathSize)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRenameExtension()");
        return FALSE;
    }

    int len = (int)strlen(path);
    char* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == '.')
        {
            len = (int)(iterator - path);
            break;
        }
        if (*iterator == '\\')
            break;
    }

    int extLen = (int)strlen(extension);
    if (len + extLen < pathSize)
    {
        memcpy(path + len, extension, extLen + 1);
        return TRUE;
    }
    else
        return FALSE;
}

BOOL SalPathRenameExtensionW(WCHAR* path, const WCHAR* extension, int pathSizeInChars)
{
    if (path == NULL || extension == NULL)
    {
        TRACE_E("Unexpected situation in SalPathRenameExtensionW()");
        return FALSE;
    }

    int len = (int)wcslen(path);
    WCHAR* iterator = path + len;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == L'.')
        {
            len = (int)(iterator - path);
            break;
        }
        if (*iterator == L'\\' || *iterator == L'/')
            break;
    }

    int extLen = (int)wcslen(extension);
    if (len + extLen < pathSizeInChars)
    {
        memcpy(path + len, extension, (extLen + 1) * sizeof(WCHAR));
        return TRUE;
    }
    else
        return FALSE;
}

const char* SalPathFindFileName(const char* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathFindFileName()");
        return "";
    }

    int len = (int)strlen(path);
    if (len <= 1)
        return path;
    const char* iterator = path + len - 1;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == '\\')
            return iterator + 1;
    }
    return path;
}

const WCHAR* SalPathFindFileNameW(const WCHAR* path)
{
    if (path == NULL)
    {
        TRACE_E("Unexpected situation in SalPathFindFileNameW()");
        return L"";
    }

    int len = (int)wcslen(path);
    if (len <= 1)
        return path;
    const WCHAR* iterator = path + len - 1;
    while (iterator > path)
    {
        iterator--;
        if (*iterator == L'\\' || *iterator == L'/')
            return iterator + 1;
    }
    return path;
}

// ****************************************************************************

BOOL SalGetTempFileName(const char* path, const char* prefix, char* tmpName, int tmpNameLen, BOOL file)
{
    if (tmpName == NULL || tmpNameLen <= 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    WCHAR tmpDirW[MAX_PATH + 10];
    WCHAR* endW = tmpDirW + MAX_PATH + 10;
    if (path == NULL)
    {
        if (!GetTempPathW(MAX_PATH, tmpDirW))
        {
            DWORD err = GetLastError();
            TRACE_E("Unable to get TEMP directory.");
            SetLastError(err);
            return FALSE;
        }
        if (GetFileAttributesW(tmpDirW) == 0xFFFFFFFF)
        {
            SalMessageBox(NULL, LoadStr(IDS_TMPDIRERROR), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            if (GetSystemDirectoryW(tmpDirW, MAX_PATH) == 0)
            {
                DWORD err = GetLastError();
                TRACE_E("Unable to get system directory.");
                SetLastError(err);
                return FALSE;
            }
        }
    }
    else
    {
        if (ConvertUtf8ToWide(path, -1, tmpDirW, _countof(tmpDirW)) == 0)
        {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return FALSE;
        }
    }

    // Temporary paths are conventional NUL-terminated wide strings.
    WCHAR* sW = tmpDirW + wcslen(tmpDirW);
    if (sW > tmpDirW && *(sW - 1) != L'\\')
    {
        if (sW >= endW)
        {
            TRACE_E("Too long path in SalGetTempFileName().");
            SetLastError(ERROR_BUFFER_OVERFLOW);
            return FALSE;
        }
        *sW++ = L'\\';
    }

    WCHAR prefixW[128];
    prefixW[0] = 0;
    if (prefix != NULL && *prefix != 0)
    {
        if (ConvertUtf8ToWide(prefix, -1, prefixW, _countof(prefixW)) == 0)
        {
            SetLastError(ERROR_NO_UNICODE_TRANSLATION);
            return FALSE;
        }
        const WCHAR* pW = prefixW;
        while (sW < endW && *pW != 0)
            *sW++ = *pW++;
    }

    if ((sW - tmpDirW) + 8 < MAX_PATH) // dost mista pro pripojeni "XXXX.tmp"
    {
        // Fold the 64-bit uptime so temporary-name probing does not restart with the 32-bit tick cycle.
        const CMonotonicTimePoint timeSeed = CMonotonicClock::Now();
        DWORD randNum = (DWORD)(timeSeed ^ (timeSeed >> 32)) & 0xFFF;
        while (1)
        {
            // The collision suffix must fit the remaining temporary-path buffer before probing it.
            if (_snwprintf_s(sW, (size_t)(endW - sW), _TRUNCATE, L"%X.tmp", randNum++) < 0)
            {
                SetLastError(ERROR_BUFFER_OVERFLOW);
                return FALSE;
            }
            if (file) // file
            {
                HANDLE h = HANDLES_Q(CreateFileW(tmpDirW, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                                 FILE_ATTRIBUTE_NORMAL, NULL));
                if (h != INVALID_HANDLE_VALUE)
                {
                    HANDLES(CloseHandle(h));
                    if (ConvertWideToUtf8(tmpDirW, -1, tmpName, tmpNameLen) == 0)
                    {
                        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
                        return FALSE;
                    }
                    return TRUE;
                }
            }
            else // directory
            {
                if (CreateDirectoryW(tmpDirW, NULL))
                {
                    if (ConvertWideToUtf8(tmpDirW, -1, tmpName, tmpNameLen) == 0)
                    {
                        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
                        return FALSE;
                    }
                    return TRUE;
                }
            }
            DWORD err = GetLastError();
            if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS)
            {
                TRACE_E("Unable to create temporary " << (file ? "file" : "directory") << ": " << GetErrorText(err));
                SetLastError(err);
                return FALSE;
            }
        }
    }
    else
    {
        TRACE_E("Too long file name in SalGetTempFileName().");
        SetLastError(ERROR_BUFFER_OVERFLOW);
        return FALSE;
    }
}

// ****************************************************************************

int HandleFileException(EXCEPTION_POINTERS* e, char* fileMem, DWORD fileMemSize)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) // in-page-error definitely means file error
    {
        return EXCEPTION_EXECUTE_HANDLER; // spustime __except blok
    }
    else
    {
        if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&    // access violation means file error only if error address corresponds to file
            (e->ExceptionRecord->NumberParameters >= 2 &&                         // have something to test
             e->ExceptionRecord->ExceptionInformation[1] >= (ULONG_PTR)fileMem && // error ptr in file view
             e->ExceptionRecord->ExceptionInformation[1] < ((ULONG_PTR)fileMem) + fileMemSize))
        {
            return EXCEPTION_EXECUTE_HANDLER; // spustime __except blok
        }
        else
        {
            return EXCEPTION_CONTINUE_SEARCH; // hodime vyjimku dale ... call-stacku
        }
    }
}

// ****************************************************************************

BOOL SalRemovePointsFromPath(char* afterRoot)
{
    char* d = afterRoot; // ukazatel za root cestu
    while (*d != 0)
    {
        while (*d != 0 && *d != '.')
            d++;
        if (*d == '.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == '\\') // '.' after root path or "\."
            {
                if (*(d + 1) == '.' && (*(d + 2) == '\\' || *(d + 2) == 0)) // ".."
                {
                    char* l = d - 1;
                    while (l > afterRoot && *(l - 1) != '\\')
                        l--;
                    if (l >= afterRoot) // skip directory + ".."
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, strlen(d + 3) + 1);
                        d = l;
                    }
                    else
                        return FALSE; // ".." nelze vypustit
                }
                else
                {
                    if (*(d + 1) == '\\' || *(d + 1) == 0) // "."
                    {
                        if (*(d + 1) == 0)
                            *d = 0;
                        else
                            memmove(d, d + 2, strlen(d + 2) + 1);
                    }
                    else
                        d++;
                }
            }
            else
                d++;
        }
    }
    return TRUE;
}

BOOL SalRemovePointsFromPath(WCHAR* afterRoot)
{
    WCHAR* d = afterRoot; // ukazatel za root cestu
    while (*d != 0)
    {
        while (*d != 0 && *d != L'.')
            d++;
        if (*d == L'.')
        {
            if (d == afterRoot || d > afterRoot && *(d - 1) == L'\\') // '.' after root path or "\."
            {
                if (*(d + 1) == L'.' && (*(d + 2) == L'\\' || *(d + 2) == 0)) // ".."
                {
                    WCHAR* l = d - 1;
                    while (l > afterRoot && *(l - 1) != L'\\')
                        l--;
                    if (l >= afterRoot) // skip directory + ".."
                    {
                        if (*(d + 2) == 0)
                            *l = 0;
                        else
                            memmove(l, d + 3, sizeof(WCHAR) * (wcslen(d + 3) + 1));
                        d = l;
                    }
                    else
                        return FALSE; // ".." nelze vypustit
                }
                else
                {
                    if (*(d + 1) == L'\\' || *(d + 1) == 0) // "."
                    {
                        if (*(d + 1) == 0)
                            *d = 0;
                        else
                            memmove(d, d + 2, sizeof(WCHAR) * (wcslen(d + 2) + 1));
                    }
                    else
                        d++;
                }
            }
            else
                d++;
        }
    }
    return TRUE;
}

// Converts user-typed path text into a canonical absolute path in place.
// Handles leading-space trimming, UNC validation (\\server\share completeness,
// rejection of \\?\ and \\.\ device forms), drive-relative forms, environment
// and hot-path expansion done by callers, relative paths against 'curDir'
// ('allowRelPathWithSpaces' tolerates spaces in the relative part), and
// detects nethood-only input ('callNethood'). On failure returns FALSE with
// 'errTextID' set to the message ID and 'nextFocus' suggesting which edit
// field to focus for correction.
BOOL SalGetFullName(char* name, int* errTextID, const char* curDir, char* nextFocus,
                    BOOL* callNethood, int nameBufSize, BOOL allowRelPathWithSpaces)
{
    CALL_STACK_MESSAGE5("SalGetFullName(%s, , %s, , , %d, %d)", name, curDir, nameBufSize, allowRelPathWithSpaces);
    int err = 0;

    int rootOffset = 3; // offset of beginning of directory part of path (3 for "c:\path")
    char* s = name;
    while (*s >= 1 && *s <= ' ')
        s++;
    if (*s == '\\' && *(s + 1) == '\\') // UNC (\\server\share\...)
    {                                   // eliminace mezer na zacatku cesty
        if (s != name)
            memmove(name, s, strlen(s) + 1);
        s = name + 2;
        if (*s == '.' || *s == '?')
            err = IDS_PATHISINVALID; // cesty jako \\?\Volume{6e76293d-1828-11df-8f3c-806e6f6e6963}\ a \\.\PhysicalDisk5\ tady proste nepodporujeme...
        else
        {
            if (*s == 0 || *s == '\\')
            {
                if (callNethood != NULL)
                    *callNethood = *s == 0;
                err = IDS_SERVERNAMEMISSING;
            }
            else
            {
                while (*s != 0 && *s != '\\')
                    s++; // prejeti servername
                if (*s == '\\')
                    s++;
                if (s - name > MAX_PATH - 1)
                    err = IDS_SERVERNAMEMISSING; // nalezeny text je moc dlouhy na to, aby to byl server
                else
                {
                    if (*s == 0 || *s == '\\')
                    {
                        if (callNethood != NULL)
                            *callNethood = *s == 0 && (*(s - 1) != '.' || *(s - 2) != '\\') && (*(s - 1) != '\\' || *(s - 2) != '.' || *(s - 3) != '\\'); // nejde o "\\." ani "\\.\" (zacatek cesty typu "\\.\C:\")
                        err = IDS_SHARENAMEMISSING;
                    }
                    else
                    {
                        while (*s != 0 && *s != '\\')
                            s++; // prejeti sharename
                        if ((s - name) + 1 > MAX_PATH - 1)
                            err = IDS_SHARENAMEMISSING; // nalezeny text je moc dlouhy na to, aby to byl share (+1 za koncovy backslash)
                        if (*s == '\\')
                            s++;
                    }
                }
            }
        }
    }
    else // path specified using drive letter (c:\...)
    {
        if (*s != 0)
        {
            if (*(s + 1) == ':') // "c:..."
            {
                if (*(s + 2) == '\\') // "c:\..."
                {                     // eliminace mezer na zacatku cesty
                    if (s != name)
                        memmove(name, s, strlen(s) + 1);
                }
                else // "c:path..."
                {
                    int l1 = (int)strlen(s + 2); // length of the rest ("path...")
                    if (LowerCase[*s] >= 'a' && LowerCase[*s] <= 'z')
                    {
                        const char* head;
                        if (curDir != NULL && LowerCase[curDir[0]] == LowerCase[*s])
                            head = curDir;
                        else
                            head = DefaultDir[LowerCase[*s] - 'a'];
                        int l2 = (int)strlen(head);
                        if (head[l2 - 1] != '\\')
                            l2++; // misto pro '\\'
                        if (l1 + l2 >= nameBufSize)
                            err = IDS_TOOLONGPATH;
                        else // sestaveni full path
                        {
                            memmove(name + l2, s + 2, l1 + 1);
                            *(name + l2 - 1) = '\\';
                            memmove(name, head, l2 - 1);
                        }
                    }
                    else
                        err = IDS_INVALIDDRIVE;
                }
            }
            else
            {
                if (curDir != NULL)
                {
                    // u relativnich cest bez '\\' na zacatku nebudeme pri zaplem 'allowRelPathWithSpaces' povazovat
                    // spaces as a mistake (directory and file names may start with a space, although Windows and other software
                    // vcetne Salama se tomu snazi predejit)
                    if (allowRelPathWithSpaces && *s != '\\')
                        s = name;
                    int l1 = (int)strlen(s);
                    if (*s == '\\') // "\path...."
                    {
                        if (curDir[0] == '\\' && curDir[1] == '\\') // UNC
                        {
                            const char* root = curDir + 2;
                            while (*root != 0 && *root != '\\')
                                root++;
                            root++; // '\\'
                            while (*root != 0 && *root != '\\')
                                root++;
                            if (l1 + (root - curDir) >= nameBufSize)
                                err = IDS_TOOLONGPATH;
                            else // sestaveni cesty z rootu akt. disku
                            {
                                memmove(name + (root - curDir), s, l1 + 1);
                                memmove(name, curDir, root - curDir);
                            }
                            rootOffset = (int)(root - curDir) + 1;
                        }
                        else
                        {
                            if (l1 + 2 >= nameBufSize)
                                err = IDS_TOOLONGPATH;
                            else
                            {
                                memmove(name + 2, s, l1 + 1);
                                name[0] = curDir[0];
                                name[1] = ':';
                            }
                        }
                    }
                    else // "path..."
                    {
                        if (nextFocus != NULL)
                        {
                            char* test = name;
                            while (*test != 0 && *test != '\\')
                                test++;
                            if (*test == 0 && (int)strlen(name) < MAX_PATH)
                                // Focus labels retain their fixed panel-presentation capacity.
                                StringCchCopyNA(nextFocus, MAX_PATH, name, MAX_PATH - 1);
                        }

                        int l2 = (int)strlen(curDir);
                        if (curDir[l2 - 1] != '\\')
                            l2++;
                        if (l1 + l2 >= nameBufSize)
                            err = IDS_TOOLONGPATH;
                        else
                        {
                            memmove(name + l2, s, l1 + 1);
                            name[l2 - 1] = '\\';
                            memmove(name, curDir, l2 - 1);
                        }
                    }
                }
                else
                    err = IDS_INCOMLETEFILENAME;
            }
            s = name + rootOffset;
        }
        else
        {
            name[0] = 0;
            err = IDS_EMPTYNAMENOTALLOWED;
        }
    }

    if (err == 0) // eliminace '.' a '..' v ceste
    {
        if (!SalRemovePointsFromPath(s))
            err = IDS_PATHISINVALID;
    }

    if (err == 0) // vyhozeni pripadneho nezadouciho backslashe z konce retezce
    {
        int l = (int)strlen(name);
        if (l > 1 && name[1] == ':') // typ cesty "c:\path"
        {
            if (l > 3) // not root path
            {
                if (name[l - 1] == '\\')
                    name[l - 1] = 0; // orez backslashe
            }
            else
            {
                name[2] = '\\'; // root path, backslash necessary ("c:\")
                name[3] = 0;
            }
        }
        else
        {
            if (name[0] == '\\' && name[1] == '\\' && name[2] == '.' && name[3] == '\\' && name[4] != 0 && name[5] == ':') // path type "\\.\C:\"
            {
                if (l > 7) // not root path
                {
                    if (name[l - 1] == '\\')
                        name[l - 1] = 0; // orez backslashe
                }
                else
                {
                    name[6] = '\\'; // root path, backslash necessary ("\\.\C:\")
                    name[7] = 0;
                }
            }
            else // UNC path
            {
                if (l > 0 && name[l - 1] == '\\')
                    name[l - 1] = 0; // orez backslashe
            }
        }
    }

    if (errTextID != NULL)
        *errTextID = err;

    return err == 0;
}

// ****************************************************************************

struct CAuxThread
{
    HANDLE Thread;
    CThreadOwner* Owner;
    LPCSTR Description;

    CAuxThread(HANDLE thread, LPCSTR description)
        : Thread(thread), Owner(NULL), Description(description)
    {
    }

    CAuxThread(CThreadOwner* owner, LPCSTR description)
        : Thread(NULL), Owner(owner), Description(description)
    {
    }
};

// Keep a purpose beside each legacy handle so a shutdown deadline tells the
// operator which component is still preventing resource teardown.
TDirectArray<CAuxThread> AuxThreads(10, 5);

void AuxThreadBody(BOOL add, HANDLE thread, BOOL testIfFinished, LPCSTR description, CThreadOwner* owner = NULL)
{
    // Prevent re-entrance
    static CCriticalSection cs;
    CEnterCriticalSection enterCS(cs);

    static BOOL finished = FALSE;
    if (!finished) // after calling ShutdownAuxThreads() we no longer accept anything
    {
        if (add)
        {
            // vycistime pole od threadu, ktere jiz dobehly
            for (int i = 0; i < AuxThreads.Count; i++)
            {
                DWORD code;
                HANDLE tracked = AuxThreads[i].Owner != NULL ? AuxThreads[i].Owner->GetThreadHandle() : AuxThreads[i].Thread;
                if (!GetExitCodeThread(tracked, &code) || code != STILL_ACTIVE)
                { // thread uz dobehl
                    if (AuxThreads[i].Owner != NULL)
                    {
                        AuxThreads[i].Owner->StopAndJoin(CThreadShutdownDeadline(AuxThreads[i].Description));
                        delete AuxThreads[i].Owner;
                    }
                    else
                        HANDLES(CloseHandle(tracked));
                    AuxThreads.Delete(i);
                    i--;
                }
            }
            BOOL skipAdd = FALSE;
            if (testIfFinished)
            {
                DWORD code;
                if (!GetExitCodeThread(thread, &code) || code != STILL_ACTIVE)
                { // thread uz dobehl
                    HANDLES(CloseHandle(thread));
                    skipAdd = TRUE;
                }
            }
            // pridame novy thread
            if (!skipAdd)
                AuxThreads.Add(owner != NULL ? CAuxThread(owner, description) : CAuxThread(thread, description));
        }
        else
        {
            finished = TRUE;
            for (int i = 0; i < AuxThreads.Count; i++)
            {
                CAuxThread& auxiliary = AuxThreads[i];
                HANDLE t = auxiliary.Owner != NULL ? auxiliary.Owner->GetThreadHandle() : auxiliary.Thread;
                DWORD code;
                if (GetExitCodeThread(t, &code) && code == STILL_ACTIVE)
                    // These legacy workers may still reference host globals, so
                    // deadline breach is diagnostic and cannot justify detaching.
                    CThreadShutdownDeadline(auxiliary.Description).WaitForSafeJoin(t);
                if (auxiliary.Owner != NULL)
                {
                    auxiliary.Owner->StopAndJoin(CThreadShutdownDeadline(auxiliary.Description));
                    delete auxiliary.Owner;
                }
                else
                    HANDLES(CloseHandle(t));
            }
            AuxThreads.DestroyMembers();
        }
    }
    else
        TRACE_E("AuxThreadBody(): calling after ShutdownAuxThreads() is not supported! add=" << add);
}

void AddAuxThread(HANDLE thread, BOOL testIfFinished, LPCSTR description)
{
    // Preserve a stable component label for deadline diagnostics at process exit.
    AuxThreadBody(TRUE, thread, testIfFinished, description);
}

void AddOwnedAuxThread(CThreadOwner* owner, LPCSTR description)
{
    if (owner == NULL || !owner->HasThread())
    {
        delete owner;
        return;
    }
    // The registry owns the worker through shutdown so its stop/completion handles stay valid.
    AuxThreadBody(TRUE, NULL, FALSE, description, owner);
}

void ShutdownAuxThreads()
{
    // Final teardown cannot release globals until every legacy worker is joined.
    AuxThreadBody(FALSE, NULL, FALSE, NULL);
}

// ****************************************************************************

/*
#define STOPREFRESHSTACKSIZE 50

class CStopRefreshStack
{
  protected:
    DWORD CallerCalledFromArr[STOPREFRESHSTACKSIZE];  // pole navratovych adres funkci, odkud se volal BeginStopRefresh()
    DWORD CalledFromArr[STOPREFRESHSTACKSIZE];        // pole adres, odkud se volal BeginStopRefresh()
    int Count;                                        // pocet prvku v predchozich dvou polich
    int Ignored;                                      // number of BeginStopRefresh() calls we had to ignore (STOPREFRESHSTACKSIZE too small -> possibly increase it)

  public:
    CStopRefreshStack() {Count = 0; Ignored = 0;}
    ~CStopRefreshStack() {CheckIfEmpty(3);} // tri BeginStopRefresh() jsou OK: pro oba panely se vola BeginStopRefresh() a treti se vola z WM_USER_CLOSE_MAINWND (ten se vola jako prvni)

    void Push(DWORD caller_called_from, DWORD called_from);
    void Pop(DWORD caller_called_from, DWORD called_from);
    void CheckIfEmpty(int checkLevel);
};

void
CStopRefreshStack::Push(DWORD caller_called_from, DWORD called_from)
{
  if (Count < STOPREFRESHSTACKSIZE)
  {
    CallerCalledFromArr[Count] = caller_called_from;
    CalledFromArr[Count] = called_from;
    Count++;
  }
  else
  {
    Ignored++;
    TRACE_E("CStopRefreshStack::Push(): you should increase STOPREFRESHSTACKSIZE! ignored=" << Ignored);
  }
}

void
CStopRefreshStack::Pop(DWORD caller_called_from, DWORD called_from)
{
  if (Ignored == 0)
  {
    if (Count > 0)
    {
      Count--;
      if (CallerCalledFromArr[Count] != caller_called_from)
      {
        TRACE_E("CStopRefreshStack::Pop(): strange situation: BeginCallerCalledFrom!=StopCallerCalledFrom - BeginCalledFrom,StopCalledFrom");
        TRACE_E("CStopRefreshStack::Pop(): strange situation: 0x" << std::hex <<
                CallerCalledFromArr[Count] << "!=0x" << caller_called_from << " - 0x" <<
                CalledFromArr[Count] << ",0x" << called_from << std::dec);
      }
    }
    else TRACE_E("CStopRefreshStack::Pop(): unexpected call!");
  }
  else Ignored--;
}

void
CStopRefreshStack::CheckIfEmpty(int checkLevel)
{
  if (Count > checkLevel)
  {
    TRACE_E("CStopRefreshStack::CheckIfEmpty(" << checkLevel << "): listing remaining BeginStopRefresh calls: CallerCalledFrom,CalledFrom");
    int i;
    for (i = 0; i < Count; i++)
    {
      TRACE_E("CStopRefreshStack::CheckIfEmpty():: 0x" << std::hex <<
              CallerCalledFromArr[i] << ",0x" << CalledFromArr[i] << std::dec);
    }
  }
}

CStopRefreshStack StopRefreshStack;
*/

void BeginStopRefresh(BOOL debugSkipOneCaller, BOOL debugDoNotTestCaller)
{
    /*
#ifdef _DEBUG     // test whether BeginStopRefresh() and EndStopRefresh() are called from the same function (by caller return address -> does not detect an "error" when called from different functions that are both called by the same function)
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

pokud bude jeste nekdy potreba ozivit tenhle kod, vyuzit toho, ze lze nahradit (x86 i x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    if (debugSkipOneCaller) caller_called_from = *(DWORD*)((char*)(*(DWORD *)(*register_ebp)) + 4);
    else caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  StopRefreshStack.Push(debugDoNotTestCaller ? 0 : caller_called_from, called_from);
#endif // _DEBUG
*/

    //  if (StopRefresh == 0) TRACE_I("Begin stop refresh mode");
    StopRefresh++;
}

void EndStopRefresh(BOOL postRefresh, BOOL debugSkipOneCaller, BOOL debugDoNotTestCaller)
{
    /*
#ifdef _DEBUG     // test whether BeginStopRefresh() and EndStopRefresh() are called from the same function (by caller return address -> does not detect an "error" when called from different functions that are both called by the same function)
  DWORD *register_ebp;
  __asm mov register_ebp, ebp
  DWORD called_from, caller_called_from;
  __try
  {
    called_from = *(DWORD*)((char*)register_ebp + 4);

pokud bude jeste nekdy potreba ozivit tenhle kod, vyuzit toho, ze lze nahradit (x86 i x64):
    called_from = *(DWORD_PTR *)_AddressOfReturnAddress();

    if (debugSkipOneCaller) caller_called_from = *(DWORD*)((char*)(*(DWORD *)(*register_ebp)) + 4);
    else caller_called_from = *(DWORD*)((char*)(*register_ebp) + 4);
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    called_from = -1;
    caller_called_from = -1;
  }
  StopRefreshStack.Pop(debugDoNotTestCaller ? 0 : caller_called_from, called_from);
#endif // _DEBUG
*/

    if (StopRefresh < 1)
    {
        TRACE_E("Incorrect call to EndStopRefresh().");
        StopRefresh = 0;
    }
    else
    {
        if (--StopRefresh == 0)
        {
            //      TRACE_I("End stop refresh mode");
            // if we blocked any refresh, give it a chance to run
            if (postRefresh && MainWindow != NULL)
            {
                if (MainWindow->LeftPanel != NULL)
                {
                    PostMessage(MainWindow->LeftPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                }
                if (MainWindow->RightPanel != NULL)
                {
                    PostMessage(MainWindow->RightPanel->HWindow, WM_USER_SM_END_NOTIFY, 0, 0);
                }
            }

            if (MainWindow != NULL && MainWindow->NeedToResentDispachChangeNotif &&
                !IsInPlugin()) // if it is still in the plug-in, sending the message makes no sense
            {
                MainWindow->NeedToResentDispachChangeNotif = FALSE;

                // postneme zadost o rozeslani zprav o zmenach na cestach
                HANDLES(EnterCriticalSection(&TimeCounterSection));
                int t1 = MyTimeCounter++;
                HANDLES(LeaveCriticalSection(&TimeCounterSection));
                PostMessage(MainWindow->HWindow, WM_USER_DISPACHCHANGENOTIF, 0, t1);
            }
        }
    }
}

// ****************************************************************************

void BeginStopIconRepaint()
{
    StopIconRepaint++;
}

void EndStopIconRepaint(BOOL postRepaint)
{
    if (StopIconRepaint > 0)
    {
        if (--StopIconRepaint == 0 && PostAllIconsRepaint)
        {
            if (postRepaint && MainWindow != NULL)
            {
                PostMessage(MainWindow->HWindow, WM_USER_REPAINTALLICONS, 0, 0);
            }
            PostAllIconsRepaint = FALSE;
        }
    }
    else
    {
        TRACE_E("Incorrect call to EndStopIconRepaint().");
        StopIconRepaint = 0;
    }
}

// ****************************************************************************

void BeginStopStatusbarRepaint()
{
    StopStatusbarRepaint++;
}

void EndStopStatusbarRepaint()
{
    if (StopStatusbarRepaint > 0)
    {
        if (--StopStatusbarRepaint == 0 && PostStatusbarRepaint)
        {
            PostStatusbarRepaint = FALSE;
            PostMessage(MainWindow->HWindow, WM_USER_REPAINTSTATUSBARS, 0, 0);
        }
    }
    else
    {
        TRACE_E("Incorrect call to EndStopStatusbarRepaint().");
        StopStatusbarRepaint = 0;
    }
}

// ****************************************************************************

BOOL CanChangeDirectory()
{
    if (ChangeDirectoryAllowed == 0)
        return TRUE;
    else
    {
        ChangeDirectoryRequest = TRUE;
        return FALSE;
    }
}

// ****************************************************************************

void AllowChangeDirectory(BOOL allow)
{
    if (allow)
    {
        if (ChangeDirectoryAllowed == 0)
        {
            TRACE_E("Incorrect call to AllowChangeDirectory().");
            return;
        }
        if (--ChangeDirectoryAllowed == 0)
        {
            if (ChangeDirectoryRequest)
                SetCurrentDirectoryToSystem();
            ChangeDirectoryRequest = FALSE;
        }
    }
    else
        ChangeDirectoryAllowed++;
}

// ****************************************************************************

void SetCurrentDirectoryToSystem()
{
    WCHAR bufW[MAX_PATH];
    GetSystemDirectoryW(bufW, MAX_PATH);
    SetCurrentDirectoryW(bufW);
}

// ****************************************************************************

void _RemoveTemporaryDir(const char* dir)
{
    char path[MAX_PATH + 2];
    WIN32_FIND_DATAW fileW;
    WIN32_FIND_DATA file;
    int dirLen = (int)strlen(dir);
    if (dirLen <= 0 || dirLen >= MAX_PATH)
        return;
    // The existing MAX_PATH guard above makes this recursive identity copy exact.
    StringCchCopyA(path, _countof(path), dir);
    char* end = path + strlen(path);
    if (*(end - 1) != '\\')
        *end++ = '\\';
    *end++ = '*';
    *end = 0;
    CStrP pathW(ConvertAllocUtf8ToWide(path, -1));
    HANDLE find = pathW != NULL ? HANDLES_Q(FindFirstFileW(pathW, &fileW)) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            ConvertFindDataWToUtf8(fileW, &file);
            if (file.cFileName[0] != 0 && strcmp(file.cFileName, "..") && strcmp(file.cFileName, ".") &&
                (end - path) + strlen(file.cFileName) < MAX_PATH)
            {
                StringCchCopyA(end, _countof(path) - (end - path), file.cFileName);
                ClearReadOnlyAttr(path, file.dwFileAttributes);
                if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    _RemoveTemporaryDir(path);
                else
                {
                    CStrP delPathW(ConvertAllocUtf8ToWide(path, -1));
                    if (delPathW != NULL)
                        DeleteFileW(delPathW);
                }
            }
        } while (FindNextFileW(find, &fileW));
        HANDLES(FindClose(find));
    }
    *(end - 1) = 0;
    {
        CStrP removePathW(ConvertAllocUtf8ToWide(path, -1));
        if (removePathW != NULL)
            RemoveDirectoryW(removePathW);
    }
}

void _RemoveTemporaryDirW(const WCHAR* dir)
{
    if (dir == NULL || *dir == L'\0')
        return;

    CPathW pattern(dir);
    pattern.AddBackslash();
    pattern.Append(L"*");

    WIN32_FIND_DATAW fileW;
    HANDLE find = HANDLES_Q(FindFirstFileW(pattern.GetPathForWin32Api(), &fileW));
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fileW.cFileName[0] != L'\0' && wcscmp(fileW.cFileName, L"..") != 0 && wcscmp(fileW.cFileName, L".") != 0)
            {
                CPathW childPath(dir);
                childPath.AddBackslash();
                childPath.Append(fileW.cFileName);

                ClearReadOnlyAttrW(childPath.CStr(), fileW.dwFileAttributes);
                if (fileW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    _RemoveTemporaryDirW(childPath.CStr());
                }
                else
                {
                    CWidePath delW(childPath.CStr());
                    const WCHAR* apiPath = delW.GetPathForWin32Api();
                    if (apiPath != NULL)
                        DeleteFileW(apiPath);
                }
            }
        } while (FindNextFileW(find, &fileW));
        HANDLES(FindClose(find));
    }

    CWidePath removeW(dir);
    const WCHAR* removeApiPath = removeW.GetPathForWin32Api();
    if (removeApiPath != NULL)
        RemoveDirectoryW(removeApiPath);
}

void RemoveTemporaryDir(const char* dir)
{
    CALL_STACK_MESSAGE2("RemoveTemporaryDir(%s)", dir);
    {
        CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
        if (dirW != NULL)
            SetCurrentDirectoryW(dirW);
    } // aby to lepe odsejpalo (system ma rad cur-dir)
    if (strlen(dir) < MAX_PATH)
        _RemoveTemporaryDir(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it can't be deleted

    ClearReadOnlyAttr(dir);
    {
        CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
        if (dirW != NULL)
            RemoveDirectoryW(dirW);
    }
}

void RemoveTemporaryDirW(const WCHAR* dir)
{
    if (dir == NULL || *dir == L'\0')
        return;

    SetCurrentDirectoryW(dir);
    _RemoveTemporaryDirW(dir);
    SetCurrentDirectoryToSystem();

    ClearReadOnlyAttrW(dir);
    CWidePath dirW(dir);
    const WCHAR* apiPath = dirW.GetPathForWin32Api();
    if (apiPath != NULL)
        RemoveDirectoryW(apiPath);
}

// ****************************************************************************

void _RemoveEmptyDirs(const char* dir)
{
    char path[MAX_PATH + 2];
    WIN32_FIND_DATAW fileW;
    WIN32_FIND_DATA file;
    int dirLen = (int)strlen(dir);
    if (dirLen <= 0 || dirLen >= MAX_PATH)
        return;
    // The existing MAX_PATH guard above makes this recursive identity copy exact.
    StringCchCopyA(path, _countof(path), dir);
    char* end = path + strlen(path);
    if (*(end - 1) != '\\')
        *end++ = '\\';
    *end++ = '*';
    *end = 0;
    CStrP pathW(ConvertAllocUtf8ToWide(path, -1));
    HANDLE find = pathW != NULL ? HANDLES_Q(FindFirstFileW(pathW, &fileW)) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            ConvertFindDataWToUtf8(fileW, &file);
            if (file.cFileName[0] != 0 && strcmp(file.cFileName, "..") && strcmp(file.cFileName, "."))
            {
                if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    (end - path) + strlen(file.cFileName) < MAX_PATH)
                {
                    StringCchCopyA(end, _countof(path) - (end - path), file.cFileName);
                    ClearReadOnlyAttr(path, file.dwFileAttributes);
                    _RemoveEmptyDirs(path);
                }
            }
        } while (FindNextFileW(find, &fileW));
        HANDLES(FindClose(find));
    }
    *(end - 1) = 0;
    {
        CStrP removePathW(ConvertAllocUtf8ToWide(path, -1));
        if (removePathW != NULL)
            RemoveDirectoryW(removePathW);
    }
}

void _RemoveEmptyDirsW(const WCHAR* dir)
{
    if (dir == NULL || *dir == L'\0')
        return;

    CPathW pattern(dir);
    pattern.AddBackslash();
    pattern.Append(L"*");

    WIN32_FIND_DATAW fileW;
    HANDLE find = HANDLES_Q(FindFirstFileW(pattern.GetPathForWin32Api(), &fileW));
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (fileW.cFileName[0] != L'\0' && wcscmp(fileW.cFileName, L"..") != 0 && wcscmp(fileW.cFileName, L".") != 0)
            {
                if (fileW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    CPathW childPath(dir);
                    childPath.AddBackslash();
                    childPath.Append(fileW.cFileName);

                    ClearReadOnlyAttrW(childPath.CStr(), fileW.dwFileAttributes);
                    _RemoveEmptyDirsW(childPath.CStr());
                }
            }
        } while (FindNextFileW(find, &fileW));
        HANDLES(FindClose(find));
    }

    CWidePath removeW(dir);
    const WCHAR* removeApiPath = removeW.GetPathForWin32Api();
    if (removeApiPath != NULL)
        RemoveDirectoryW(removeApiPath);
}

void RemoveEmptyDirs(const char* dir)
{
    CALL_STACK_MESSAGE2("RemoveEmptyDirs(%s)", dir);
    {
        CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
        if (dirW != NULL)
            SetCurrentDirectoryW(dirW);
    } // aby to lepe odsejpalo (system ma rad cur-dir)
    if (strlen(dir) < MAX_PATH)
        _RemoveEmptyDirs(dir);
    SetCurrentDirectoryToSystem(); // must leave it, otherwise it can't be deleted

    ClearReadOnlyAttr(dir);
    {
        CStrP dirW(ConvertAllocUtf8ToWide(dir, -1));
        if (dirW != NULL)
            RemoveDirectoryW(dirW);
    }
}

void RemoveEmptyDirsW(const WCHAR* dir)
{
    if (dir == NULL || *dir == L'\0')
        return;

    SetCurrentDirectoryW(dir);
    _RemoveEmptyDirsW(dir);
    SetCurrentDirectoryToSystem();

    ClearReadOnlyAttrW(dir);
    CWidePath dirW(dir);
    const WCHAR* apiPath = dirW.GetPathForWin32Api();
    if (apiPath != NULL)
        RemoveDirectoryW(apiPath);
}

// ****************************************************************************

BOOL CheckAndCreateDirectory(const char* dir, HWND parent, BOOL quiet, char* errBuf,
                             int errBufSize, char* newDir, BOOL noRetryButton,
                             BOOL manualCrDir)
{
    CALL_STACK_MESSAGE2("CheckAndCreateDirectory(%s)", dir);
AGAIN:
    if (parent == NULL)
        parent = MainWindow->HWindow;
    if (newDir != NULL)
        newDir[0] = 0;
    int dirLen = (int)strlen(dir);
    if (dirLen >= MAX_PATH) // too long name
    {
        if (errBuf != NULL)
            strncpy_s(errBuf, errBufSize, LoadStr(IDS_TOOLONGNAME), _TRUNCATE);
        else
            SalMessageBox(parent, LoadStr(IDS_TOOLONGNAME), LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    DWORD attrs = SalGetFileAttributes(dir);
    char buf[MAX_PATH + 200];
    char name[MAX_PATH];
    if (attrs == 0xFFFFFFFF) // asi neexistuje, umoznime jej vytvorit
    {
        char root[MAX_PATH];
        GetRootPath(root, dir);
        if (dirLen <= (int)strlen(root)) // dir is the root directory
        {
            _snprintf_s(buf, _countof(buf), _TRUNCATE, LoadStr(IDS_CREATEDIRFAILED), dir);
            if (errBuf != NULL)
                strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
            else
                SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        int msgBoxRet = IDCANCEL;
        if (!quiet)
        {
            // if the user did not suppress it, show information about directory nonexistence
            if (Configuration.CnfrmCreateDir)
            {
                char title[100];
                char text[MAX_PATH + 500];
                char checkText[200];
                // Dialog text fields retain their fixed presentation buffers.
                StringCchCopyNA(title, _countof(title), LoadStr(IDS_QUESTION), _countof(title) - 1);
                _snprintf_s(text, _countof(text), _TRUNCATE, LoadStr(IDS_CREATEDIRECTORY), dir);
                StringCchCopyNA(checkText, _countof(checkText), LoadStr(IDS_DONTSHOWAGAINCD), _countof(checkText) - 1);
                BOOL dontShow = !Configuration.CnfrmCreateDir;

                MSGBOXEX_PARAMS params;
                memset(&params, 0, sizeof(params));
                params.HParent = parent;
                params.Flags = MSGBOXEX_OKCANCEL | MSGBOXEX_ICONQUESTION | MSGBOXEX_HINT;
                params.Caption = title;
                params.Text = text;
                params.CheckBoxText = checkText;
                params.CheckBoxValue = &dontShow;
                msgBoxRet = SalMessageBoxEx(&params);

                Configuration.CnfrmCreateDir = !dontShow;
            }
            else
                msgBoxRet = IDOK;
        }
        if (quiet || msgBoxRet == IDOK)
        {
            // Directory creation works only from a complete requested path.
            if (FAILED(StringCchCopyA(name, _countof(name), dir)))
                return FALSE;
            char* s;
            while (1) // find the first existing directory
            {
                s = strrchr(name, '\\');
                if (s == NULL)
                {
                    _snprintf_s(buf, _countof(buf), _TRUNCATE, LoadStr(IDS_CREATEDIRFAILED), dir);
                    if (errBuf != NULL)
                        strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                    else
                        SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }
                if (s - name > (int)strlen(root))
                    *s = 0;
                else
                {
                    StringCchCopyA(name, _countof(name), root);
                    break; // uz jsme na root-adresari
                }
                attrs = SalGetFileAttributes(name);
                if (attrs != 0xFFFFFFFF) // name exists
                {
                    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
                        break; // budeme stavet od tohoto adresare
                    else       // it is a file, that would not work ...
                    {
                        _snprintf_s(buf, _countof(buf), _TRUNCATE, LoadStr(IDS_NAMEUSEDFORFILE), name);
                        if (errBuf != NULL)
                            strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                        else
                        {
                            if (noRetryButton)
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), FALSE, IDD_ERROR3);
                                dlg.Execute();
                            }
                            else
                            {
                                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), TRUE);
                                if (dlg.Execute() == IDRETRY)
                                    goto AGAIN;
                                // SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                            }
                        }
                        return FALSE;
                    }
                }
            }
            s = name + strlen(name) - 1;
            if (*s != '\\')
            {
                *++s = '\\';
                *++s = 0;
            }
            const char* st = dir + strlen(name);
            if (*st == '\\')
                st++;
            int len = (int)strlen(name);
            BOOL first = TRUE;
            while (*st != 0)
            {
                BOOL invalidName = manualCrDir && *st <= ' '; // mezery na zacatku jmena vytvareneho adresare jsou nezadouci jen pri rucnim vytvareni (Windows to umi, ale je to potencialne nebezpecne)
                const char* slash = strchr(st, '\\');
                if (slash == NULL)
                    slash = st + strlen(st);
                memcpy(name + len, st, slash - st);
                name[len += (int)(slash - st)] = 0;
                if (name[len - 1] <= ' ' || name[len - 1] == '.')
                    invalidName = TRUE; // spaces and dots at the end of the created directory name are undesirable
            AGAIN2:
                if (invalidName || !CreateDirectoryUtf8(name, NULL))
                {
                    DWORD lastErr = invalidName ? ERROR_INVALID_NAME : GetLastError();
                    _snprintf_s(buf, _countof(buf), _TRUNCATE, LoadStr(IDS_CREATEDIRFAILED), name);
                    if (errBuf != NULL)
                        strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
                    else
                    {
                        if (noRetryButton)
                        {
                            CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(lastErr), FALSE, IDD_ERROR3);
                            dlg.Execute();
                        }
                        else
                        {
                            CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(lastErr), TRUE);
                            if (dlg.Execute() == IDRETRY)
                                goto AGAIN2;
                            //              SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
                        }
                    }
                    return FALSE;
                }
                else
                {
                    if (first && newDir != NULL)
                        StringCchCopyA(newDir, MAX_PATH, name);
                    first = FALSE;
                }
                name[len++] = '\\';
                if (*slash == '\\')
                    slash++;
                st = slash;
            }
            return TRUE;
        }
        return FALSE;
    }
    if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        return TRUE;
    else // file, that would not work ...
    {
        _snprintf_s(buf, _countof(buf), _TRUNCATE, LoadStr(IDS_NAMEUSEDFORFILE), dir);
        if (errBuf != NULL)
            strncpy_s(errBuf, errBufSize, buf, _TRUNCATE);
        else
        {
            if (noRetryButton)
            {
                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), FALSE, IDD_ERROR3);
                dlg.Execute();
            }
            else
            {
                CFileErrorDlg dlg(parent, LoadStr(IDS_ERRORCREATINGDIR), dir, GetErrorText(ERROR_ALREADY_EXISTS), TRUE);
                if (dlg.Execute() == IDRETRY)
                    goto AGAIN;
                //        SalMessageBox(parent, buf, LoadStr(IDS_ERRORTITLE), MB_OK | MB_ICONEXCLAMATION);
            }
        }
        return FALSE;
    }
}

//
// ****************************************************************************
// CToolTipWindow
//

LRESULT
CToolTipWindow::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == TTM_WINDOWFROMPOINT)
        return (LRESULT)ToolWindow;
    return CWindow::WindowProc(uMsg, wParam, lParam);
}


//****************************************************************************
//
// Mouse Wheel support
//

// Default values for SPI_GETWHEELSCROLLLINES and
// SPI_GETWHEELSCROLLCHARS
#define DEFAULT_LINES_TO_SCROLL 3
#define DEFAULT_CHARS_TO_SCROLL 3

// handle of the old mouse hook procedure
HHOOK HOldMouseWheelHookProc = NULL;
BOOL MouseWheelMSGThroughHook = FALSE;
// Keep the hook/window de-duplication interval valid during long-running sessions.
CMonotonicTimePoint MouseWheelMSGTime = 0;
BOOL GotMouseWheelScrollLines = FALSE;
BOOL GotMouseWheelScrollChars = FALSE;

UINT GetMouseWheelScrollLines()
{
    static UINT uCachedScrollLines;

    // if we've already got it and we're not refreshing,
    // return what we've already got

    if (GotMouseWheelScrollLines)
        return uCachedScrollLines;

    // see if we can find the mouse window

    GotMouseWheelScrollLines = TRUE;

    static UINT msgGetScrollLines;
    static WORD nRegisteredMessage = 0;

    if (nRegisteredMessage == 0)
    {
        msgGetScrollLines = ::RegisterWindowMessage(MSH_SCROLL_LINES);
        if (msgGetScrollLines == 0)
            nRegisteredMessage = 1; // couldn't register!  never try again
        else
            nRegisteredMessage = 2; // it worked: use it
    }

    if (nRegisteredMessage == 2)
    {
        HWND hwMouseWheel = NULL;
        hwMouseWheel = FindWindow(MSH_WHEELMODULE_CLASS, MSH_WHEELMODULE_TITLE);
        if (hwMouseWheel && msgGetScrollLines)
        {
            uCachedScrollLines = (UINT)::SendMessage(hwMouseWheel, msgGetScrollLines, 0, 0);
            return uCachedScrollLines;
        }
    }

    // couldn't use the window -- try system settings
    uCachedScrollLines = DEFAULT_LINES_TO_SCROLL;
    ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &uCachedScrollLines, 0);

    return uCachedScrollLines;
}

#define SPI_GETWHEELSCROLLCHARS 0x006C

UINT GetMouseWheelScrollChars()
{
    static UINT uCachedScrollChars;
    if (GotMouseWheelScrollChars)
        return uCachedScrollChars;

    if (WindowsVistaAndLater)
    {
        if (!SystemParametersInfo(SPI_GETWHEELSCROLLCHARS, 0, &uCachedScrollChars, 0))
            uCachedScrollChars = DEFAULT_CHARS_TO_SCROLL;
    }
    else
        uCachedScrollChars = DEFAULT_CHARS_TO_SCROLL;
    GotMouseWheelScrollChars = TRUE;
    return uCachedScrollChars;
}

BOOL PostMouseWheelMessage(MSG* pMSG)
{
    // let it find the window under the mouse cursor
    HWND hWindow = WindowFromPoint(pMSG->pt);
    if (hWindow != NULL)
    {
        char className[101];
        className[0] = 0;
        if (GetClassName(hWindow, className, 100) != 0)
        {
            // some versions of Synaptics touchpad (for example on HP notebooks) display their symbol window under the cursor
            // scrolovani; v takovem pripade se nebudeme snazit o routeni do "spravneho" okna pod kurzorem, protoze
            // se o to postara sam touchpad
            // /viewtopic.php?f=24&t=6039
            if (strcmp(className, "SynTrackCursorWindowClass") == 0 || strcmp(className, "Syn Visual Class") == 0)
            {
                //TRACE_I("Synaptics touchpad detected className="<<className);
                hWindow = pMSG->hwnd;
            }
            else
            {
                DWORD winProcessId = 0;
                GetWindowThreadProcessId(hWindow, &winProcessId);
                if (winProcessId != GetCurrentProcessId()) // WM_USER_* nema smysl posilat mimo nas proces
                    hWindow = pMSG->hwnd;
            }
        }
        else
        {
            TRACE_E("GetClassName() failed!");
            hWindow = pMSG->hwnd;
        }
        // if this is a ScrollBar with a parent, post the message to the parent.
        // Scrollbars v panelech nejsou subclassnute, takze je to momentalne jediny zpusob,
        // how the panel can learn about wheel scrolling if the cursor is over the scrollbar.
        className[0] = 0;
        if (GetClassName(hWindow, className, 100) == 0 || StrICmp(className, "scrollbar") == 0)
        {
            HWND hParent = GetParent(hWindow);
            if (hParent != NULL)
                hWindow = hParent;
        }
        PostMessage(hWindow, pMSG->message == WM_MOUSEWHEEL ? WM_USER_MOUSEWHEEL : WM_USER_MOUSEHWHEEL, pMSG->wParam, pMSG->lParam);
    }
    return TRUE;
}

// hook procedure for mouse messages
LRESULT CALLBACK MenuWheelHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    //  CALL_STACK_MESSAGE4("MenuWheelHookProc(%d, 0x%IX, 0x%IX)", nCode, wParam, lParam);
    LRESULT retValue = 0;

    retValue = CallNextHookEx(HOldMouseWheelHookProc, nCode, wParam, lParam);

    if (nCode < 0)
        return retValue;

    MSG* pMSG = (MSG*)lParam;
    MessagesKeeper.Add(pMSG); // if Salamander crashes, we will have message history

    // only WM_MOUSEWHEEL and WM_MOUSEHWHEEL interest us
    //
    // 7.10.2009 - AS253_B1_IB34: Manison nam hlasil, ze mu pod Windows Vista nefunguje horizontalni scroll.
    // Me fungoval (touto cestou). Po nainstalovani Intellipoint ovladacu v7 (predtime jsem na Vista x64
    // nemel zadne spesl ovladace) prestaly WM_MOUSEHWHEEL zpravy prochazet tudy a natejkaly primo do
    // Salamander panel. So disable this path and catch messages only in the panel.
    // poznamka: asi bychom stejnym zpusobem mohli odriznou i handling WM_MOUSEWHEEL, ale nebudu riskovat,
    // ze neco podelam na starsich OS (muzeme to zkusit s prechodem na W2K a dal)
    // note2: if it turns out we must catch WM_MOUSEHWHEEL also through this hook, it would need
    // provest runtime detekci, ze tudy zpravy WM_MOUSEHWHEEL natekaji a nasledne zakazat jejich zpracovani
    // v panelech a commandline.

    // 30.11.2012 - na foru se objevil clovek, kteremu WM_MOUSEHWEEL nechodi skrz message hook (stejna jako drive
    // u Manisona v pripade WM_MOUSEHWHEEL): /viewtopic.php?f=24&t=6039
    // so newly we will also catch the message in individual windows where it can potentially go (by focus)
    // a nasledne ji routit tak, aby se dorucila do okna pod kurzorem, jak jsme to vzdy delali

    // aktualne tedy budeme jak WM_MOUSEWHEEL tak WM_MOUSEHWHEEL poustet a uvidime, co na to beta testeri

    if ((pMSG->message != WM_MOUSEWHEEL && pMSG->message != WM_MOUSEHWHEEL) || (wParam == PM_NOREMOVE))
        return retValue;

    // if the message arrived "recently" through the other channel, ignore this channel
    const CMonotonicTimePoint mouseWheelNow = CMonotonicClock::Now();
    if (!MouseWheelMSGThroughHook && MouseWheelMSGTime != 0 &&
        !CMonotonicClock::HasElapsed(MouseWheelMSGTime, MOUSEWHEELMSG_VALID, mouseWheelNow))
        return retValue;
    MouseWheelMSGThroughHook = TRUE;
    MouseWheelMSGTime = mouseWheelNow;

    PostMouseWheelMessage(pMSG);

    return retValue;
}

BOOL InitializeMenuWheelHook()
{
    // setup hook for mouse messages
    DWORD threadID = GetCurrentThreadId();
    HOldMouseWheelHookProc = SetWindowsHookEx(WH_GETMESSAGE, // HANDLES neumi!
                                              MenuWheelHookProc,
                                              NULL, threadID);
    return (HOldMouseWheelHookProc != NULL);
}

BOOL ReleaseMenuWheelHook()
{
    // unhook mouse messages
    if (HOldMouseWheelHookProc != NULL)
    {
        UnhookWindowsHookEx(HOldMouseWheelHookProc); // HANDLES neumi!
        HOldMouseWheelHookProc = NULL;
    }
    return TRUE;
}


BOOL SalGetTempFileName(const char* path, const char* prefix, char* tmpName, BOOL file)
{
    return SalGetTempFileName(path, prefix, tmpName, MAX_PATH, file);
}



//****************************************************************************
//
// Directory editline/combobox support
//

#define DIRECTORY_COMMAND_BROWSE 1    // browse directory
#define DIRECTORY_COMMAND_LEFT 3      // path from the left panel
#define DIRECTORY_COMMAND_RIGHT 4     // path from the right panel
#define DIRECTORY_COMMAND_HOTPATHF 5  // prvni hot path
#define DIRECTORY_COMMAND_HOTPATHL 35 // posledni hot path

BOOL SetEditOrComboText(HWND hWnd, const char* text)
{
    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    CStrP wide(ConvertAllocUtf8ToWide(text != NULL ? text : "", -1));
    SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)(wide != NULL ? wide.Ptr : L""));
    SendMessage(hEdit, EM_SETSEL, 0, wide != NULL ? (LPARAM)wcslen(wide) : 0);
    return TRUE;
}

BOOL SetEditOrComboTextW(HWND hWnd, const WCHAR* text)
{
    if (hWnd == NULL)
        return FALSE;

    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    const WCHAR* val = text != NULL ? text : L"";
    SendMessageW(hEdit, WM_SETTEXT, 0, (LPARAM)val);
    SendMessage(hEdit, EM_SETSEL, 0, (LPARAM)wcslen(val));
    return TRUE;
}

BOOL GetEditOrComboTextUtf8(HWND hWnd, char* buf, int bufSize)
{
    if (buf == NULL || bufSize <= 0)
        return FALSE;
    buf[0] = 0;

    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0)
        return TRUE;

    CStrP wide((WCHAR*)malloc(sizeof(WCHAR) * (len + 1)));
    if (wide == NULL)
        return FALSE;
    GetWindowTextW(hEdit, wide, len + 1);
    ConvertWideToUtf8(wide, -1, buf, bufSize);
    return TRUE;
}

BOOL GetEditOrComboTextW(HWND hWnd, WCHAR* buf, int bufSizeInChars)
{
    if (buf == NULL || bufSizeInChars <= 0)
        return FALSE;
    buf[0] = 0;

    if (hWnd == NULL)
        return FALSE;

    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
    {
        TRACE_E("GetClassName failed on hWnd=0x" << hWnd);
        return FALSE;
    }

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            TRACE_E("Edit window was not found hWnd=0x" << hWnd);
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0)
        return TRUE;

    if (len >= bufSizeInChars)
        len = bufSizeInChars - 1;

    GetWindowTextW(hEdit, buf, len + 1);
    buf[len] = 0;
    return TRUE;
}

BOOL GetEditOrComboTextW(HWND hWnd, CPathW& path)
{
    path.Clear();
    if (hWnd == NULL)
        return FALSE;

    char className[31];
    className[0] = 0;
    if (GetClassName(hWnd, className, 30) == 0)
        return FALSE;

    HWND hEdit;
    if (StrICmp(className, "edit") != 0)
    {
        hEdit = GetWindow(hWnd, GW_CHILD);
        if (hEdit == NULL ||
            GetClassName(hEdit, className, 30) == 0 ||
            StrICmp(className, "edit") != 0)
        {
            return FALSE;
        }
    }
    else
        hEdit = hWnd;

    int len = GetWindowTextLengthW(hEdit);
    if (len <= 0)
        return TRUE;

    WCHAR* buffer = path.GetBuffer(len + 1);
    if (buffer == NULL)
        return FALSE;

    GetWindowTextW(hEdit, buffer, len + 1);
    path.ReleaseBuffer(len);
    return TRUE;
}

DWORD TrackDirectoryMenu(HWND hDialog, int buttonID, BOOL selectMenuItem)
{
    RECT r;
    GetWindowRect(GetDlgItem(hDialog, buttonID), &r);

    CMenuPopup popup;
    MENU_ITEM_INFO mii;
    mii.Mask = MENU_MASK_TYPE | MENU_MASK_ID | MENU_MASK_STRING | MENU_MASK_STATE;
    mii.Type = MENU_TYPE_STRING;
    mii.State = 0;

    MENU_ITEM_INFO miiSep;
    miiSep.Mask = MENU_MASK_TYPE;
    miiSep.Type = MENU_TYPE_SEPARATOR;

    /* slouzi pro skript export_mnu.py, ktery generuje salmenu.mnu pro Translator
   udrzovat synchronizovane s volanim InsertItem() dole...
MENU_TEMPLATE_ITEM CopyMoveBrowseMenu[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_PATHMENU_BROWSE
  {MNTT_IT, IDS_PATHMENU_LEFT
  {MNTT_IT, IDS_PATHMENU_RIGHT
  {MNTT_PE, 0
};
*/

    mii.ID = DIRECTORY_COMMAND_BROWSE;
    mii.String = LoadStr(IDS_PATHMENU_BROWSE);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    //  mii.ID = 2;
    //  mii.String = "Tree...\tCtrl+T";
    //  popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    popup.InsertItem(0xFFFFFFFF, TRUE, &miiSep);

    mii.ID = DIRECTORY_COMMAND_LEFT;
    mii.String = LoadStr(IDS_PATHMENU_LEFT);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    mii.ID = DIRECTORY_COMMAND_RIGHT;
    mii.String = LoadStr(IDS_PATHMENU_RIGHT);
    popup.InsertItem(0xFFFFFFFF, TRUE, &mii);

    // pripojime hotpaths, existuji-li
    DWORD firstID = DIRECTORY_COMMAND_HOTPATHF;
    MainWindow->HotPaths.FillHotPathsMenu(&popup, firstID, FALSE, FALSE, FALSE, TRUE);

    DWORD flags = MENU_TRACK_RETURNCMD;
    if (selectMenuItem)
    {
        popup.SetSelectedItemIndex(0);
        flags |= MENU_TRACK_SELECT;
    }
    return popup.Track(flags, r.right, r.top, hDialog, &r);
}

DWORD OnKeyDownHandleSelectAll(DWORD keyCode, HWND hDialog, int editID)
{
    // od Windows Vista uz SelectAll standardne funguje, takze tam nechame select all na nich
    if (WindowsVistaAndLater)
        return FALSE;

    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (controlPressed && !shiftPressed && !altPressed)
    {
        if (keyCode == 'A')
        {
            // select all
            HWND hChild = GetDlgItem(hDialog, editID);
            if (hChild != NULL)
            {
                char className[30];
                GetClassName(hChild, className, 29);
                className[29] = 0;
                BOOL combo = (stricmp(className, "combobox") == 0);
                if (combo)
                    SendMessage(hChild, CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
                else
                    SendMessage(hChild, EM_SETSEL, 0, -1);
                return TRUE;
            }
        }
    }
    return FALSE;
}

void InvokeDirectoryMenuCommand(DWORD cmd, HWND hDialog, int editID, int editBufSize);

void OnDirectoryButton(HWND hDialog, int editID, int editBufSize, int buttonID, WPARAM wParam, LPARAM lParam)
{
    BOOL selectMenuItem = LOWORD(lParam);
    DWORD cmd = TrackDirectoryMenu(hDialog, buttonID, selectMenuItem);
    InvokeDirectoryMenuCommand(cmd, hDialog, editID, editBufSize);
}

DWORD OnDirectoryKeyDown(DWORD keyCode, HWND hDialog, int editID, int editBufSize, int buttonID)
{
    BOOL controlPressed = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    BOOL altPressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    BOOL shiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (!controlPressed && !shiftPressed && altPressed && keyCode == VK_RIGHT)
    {
        OnDirectoryButton(hDialog, editID, editBufSize, buttonID, MAKELPARAM(buttonID, 0), MAKELPARAM(TRUE, 0));
        return TRUE;
    }
    if (controlPressed && !shiftPressed && !altPressed)
    {
        switch (keyCode)
        {
        case 'B':
        {
            InvokeDirectoryMenuCommand(DIRECTORY_COMMAND_BROWSE, hDialog, editID, editBufSize);
            return TRUE;
        }

        case 219: // '['
        case 221: // ']'
        {
            InvokeDirectoryMenuCommand((keyCode == 219) ? DIRECTORY_COMMAND_LEFT : DIRECTORY_COMMAND_RIGHT, hDialog, editID, editBufSize);
            return TRUE;
        }

        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '0':
        {
            int index = keyCode == '0' ? 9 : keyCode - '1';
            InvokeDirectoryMenuCommand(DIRECTORY_COMMAND_HOTPATHF + index, hDialog, editID, editBufSize);
            return TRUE;
        }
        }
    }
    return FALSE;
}

void InvokeDirectoryMenuCommand(DWORD cmd, HWND hDialog, int editID, int editBufSize)
{
    char path[2 * MAX_PATH];
    BOOL setPathToEdit = FALSE;
    switch (cmd)
    {
    case 0:
    {
        return;
    }

    case DIRECTORY_COMMAND_BROWSE:
    {
        // browse
        GetEditOrComboTextUtf8(GetDlgItem(hDialog, editID), path, MAX_PATH);
        char caption[100];
        {
            CStrP wide((WCHAR*)malloc(sizeof(WCHAR) * 100));
            if (wide != NULL && GetWindowTextW(hDialog, wide, 100) > 0)
                ConvertWideToUtf8(wide, -1, caption, 100);
            else
                caption[0] = 0;
        }
        if (GetTargetDirectory(hDialog, hDialog, caption, LoadStr(IDS_BROWSETARGETDIRECTORY), path, FALSE, path))
            setPathToEdit = TRUE;
        break;
    }

        //    case 2:
        //    {
        //      // tree
        //      break;
        //    }

    case DIRECTORY_COMMAND_LEFT:
    case DIRECTORY_COMMAND_RIGHT:
    {
        // left/right panel directory
        CFilesWindow* panel = (cmd == DIRECTORY_COMMAND_LEFT) ? MainWindow->LeftPanel : MainWindow->RightPanel;
        if (panel != NULL)
        {
            panel->GetGeneralPath(path, 2 * MAX_PATH, TRUE);
            setPathToEdit = TRUE;
        }
        break;
    }

    default:
    {
        // hot path
        if (cmd >= DIRECTORY_COMMAND_HOTPATHF && cmd <= DIRECTORY_COMMAND_HOTPATHL)
        {
            if (MainWindow->GetExpandedHotPath(hDialog, cmd - DIRECTORY_COMMAND_HOTPATHF, path, 2 * MAX_PATH))
                setPathToEdit = TRUE;
        }
        else
            TRACE_E("Unknown cmd=" << cmd);
    }
    }
    if (setPathToEdit)
    {
        if ((int)strlen(path) >= editBufSize)
        {
            TRACE_E("InvokeDirectoryMenuCommand(): too long path! len=" << (int)strlen(path));
            path[editBufSize - 1] = 0;
        }
        SetEditOrComboText(GetDlgItem(hDialog, editID), path);
    }
}

//****************************************************************************
//
// CKeyForwarder
//

class CKeyForwarder : public CWindow
{
protected:
    BOOL SkipCharacter; // zamezuje pipnuti pro zpracovane klavesy
    HWND HDialog;       // dialog, kam budeme zasilat WM_USER_KEYDOWN
    int CtrlID;         // pro WM_USER_KEYDOWN

public:
    CKeyForwarder(HWND hDialog, int ctrlID, CObjectOrigin origin = ooAllocated);

protected:
    virtual LRESULT WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

CKeyForwarder::CKeyForwarder(HWND hDialog, int ctrlID, CObjectOrigin origin)
    : CWindow(origin)
{
    SkipCharacter = FALSE;
    HDialog = hDialog;
    CtrlID = ctrlID;
}

LRESULT
CKeyForwarder::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CALL_STACK_MESSAGE4("CKeyForwarder::WindowProc(0x%X, 0x%IX, 0x%IX)", uMsg, wParam, lParam);
    switch (uMsg)
    {
    case WM_CHAR:
    {
        if (SkipCharacter)
        {
            SkipCharacter = FALSE;
            return 0;
        }
        break;
    }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    {
        SkipCharacter = TRUE; // zamezime pipani
        BOOL ret = (BOOL)SendMessage(HDialog, WM_USER_KEYDOWN, MAKELPARAM(CtrlID, 0), wParam);
        if (ret)
            return 0;
        SkipCharacter = FALSE;
        break;
    }

    case WM_SYSKEYUP:
    case WM_KEYUP:
    {
        SkipCharacter = FALSE; // pro jistotu
        break;
    }
    }
    return CWindow::WindowProc(uMsg, wParam, lParam);
}

BOOL CreateKeyForwarder(HWND hDialog, int ctrlID)
{
    HWND hWindow = GetDlgItem(hDialog, ctrlID);
    char className[31];
    className[0] = 0;
    if (GetClassName(hWindow, className, 30) == 0 || StrICmp(className, "edit") != 0)
    {
        // mohlo by jit o combobox, zkusime sahnout pro vnitrni edit
        hWindow = GetWindow(hWindow, GW_CHILD);
        if (hWindow == NULL || GetClassName(hWindow, className, 30) == 0 || StrICmp(className, "edit") != 0)
        {
            TRACE_E("CreateKeyForwarder: edit window was not found ClassName is " << className);
            return FALSE;
        }
    }

    CKeyForwarder* edit = new CKeyForwarder(hDialog, ctrlID);
    if (edit != NULL)
    {
        edit->AttachToWindow(hWindow);
        return TRUE;
    }
    return FALSE;
}
