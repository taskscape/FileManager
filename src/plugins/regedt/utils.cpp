// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include <strsafe.h>
#include <objbase.h>
#include <shobjidl.h>

#include "..\\..\\common\\monotonic_time.h"

BOOL PathAppend(WCHAR* path, WCHAR* more, int pathSize)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("PathAppend(, , %d)", pathSize);
    if (more[0] == L'\0')
    {
        return TRUE;
    }

    int len1 = (int)wcslen(path);
    int len2 = (int)wcslen(more);

    if (len1 && path[len1 - 1] != L'\\' && *more != L'\\')
    {
        if (len1 + 1 >= pathSize)
            return FALSE;
        path[len1++] = L'\\';
        path[len1] = L'\0';
    }
    if (len1 + len2 >= pathSize)
        return FALSE;
    wcscpy(path + len1, more);
    return TRUE;
}

BOOL CutDirectory(WCHAR* path, WCHAR* cutDir, int size)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CutDirectory(, , %d)", size);
    WCHAR* slash = wcsrchr(path, L'\\');
    if (!slash)
    {
        // path not starting with a slash
        if (*path == L'\0')
            return FALSE; // nothing left to shorten
        if (cutDir && size > 0)
            // This output is a display fragment, so retain the former deliberate clipping within its caller-provided capacity.
            StringCchCopyNW(cutDir, static_cast<size_t>(size), path, static_cast<size_t>(size - 1));
        *path = L'\0';
    }
    else
    {
        if (cutDir && size > 0)
            // This output is a display fragment, so retain the former deliberate clipping within its caller-provided capacity.
            StringCchCopyNW(cutDir, static_cast<size_t>(size), slash + 1, static_cast<size_t>(size - 1));
        if (slash != path)
            *slash = L'\0';
        else
            slash[1] = L'\0';
    }
    return TRUE;
}

WCHAR*
DupStr(const WCHAR* str)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("DupStr()");
    WCHAR* ret;
    int len = (int)(wcslen(str) + 1) * 2;
    ret = (WCHAR*)SG->Alloc(len);
    if (ret)
        memcpy(ret, str, len);
    return ret;
}

char* DupStrA(const WCHAR* str)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("DupStrA()");
    char* ret;
    int len = (int)wcslen(str);
    ret = (char*)SG->Alloc(len + 1);
    if (ret)
    {
        if (WStrToStr(ret, len + 1, str, len + 1) <= 0)
        {
            TRACE_E("Unable to convert Unicode to char, GetLastError()=" << ret);
            free(ret);
            ret = SG->DupStr("?");
        }
    }

    return ret;
}

char* StrNCat(char* dest, const char* sour, int destSize)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("StrNCat(, %s, %d)", sour, destSize);
    if (destSize == 0)
        return dest;
    char* start = dest;
    while (*dest)
        dest++;
    destSize -= (int)(dest - start);
    while (*sour && --destSize)
        *dest++ = *sour++;
    *dest = NULL;
    return start;
}

void RemoveTrailingSlashes(char* path)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("RemoveTrailingSlashes()");
    char* end = path + strlen(path);
    while (end > path && end[-1] == '\\')
        end--;
    *end = 0;
}

void RemoveTrailingSlashes(LPWSTR path)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("RemoveTrailingSlashes()");
    LPWSTR end = path + wcslen(path);
    while (end > path && end[-1] == L'\\')
        end--;
    *end = L'\0';
}

BOOL RegOperationError(int lastError, int error, int title, int keyRoot, LPWSTR keyName,
                       LPBOOL skip, LPBOOL skipAllErrors)
{
    CALL_STACK_MESSAGE5("RegOperationError(%d, %d, %d, %d, , , )", lastError,
                        error, title, keyRoot);
    if (skipAllErrors && *skipAllErrors)
    {
        if (skip)
            *skip = TRUE;
        return FALSE;
    }

    char buf[1024];
    int l = (int)strlen(strcpy(buf, LoadStr(error)));
    SG->GetErrorText(lastError, buf + l, 1024 - l);

    char fullName[MAX_FULL_KEYNAME]; // buffer for the full REG name shown in an error dialog
    l = WStrToStr(fullName, MAX_FULL_KEYNAME, PredefinedHKeys[keyRoot].KeyName) - 1;
    fullName[l++] = '\\';
    l += WStrToStr(fullName + l, MAX_FULL_KEYNAME - l, keyName) - 1;
    fullName[l] = 0; // just in case

    int res = skip ? SG->DialogError(GetParent(), BUTTONS_RETRYSKIPCANCEL, fullName, buf, LoadStr(title)) : SG->DialogError(GetParent(), BUTTONS_RETRYCANCEL, fullName, buf, LoadStr(title));
    switch (res)
    {
    case DIALOG_RETRY:
        return TRUE;

    case DIALOG_SKIPALL:
        *skipAllErrors = TRUE;
    case DIALOG_SKIP:
        *skip = TRUE;
        return FALSE;

    default:
        if (skip)
            *skip = FALSE;
        return FALSE; // DIALOG_CANCEL
    }
}

// ****************************************************************************

void LoadHistory(HKEY regKey, const char* keyPattern, LPWSTR* history,
                 LPWSTR buffer, int bufferSize, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("LoadHistory(, %s, , , %d, )", keyPattern, bufferSize);
    char buf[32];
    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        SalPrintf(buf, 32, keyPattern, i);
        if (!registry->GetValue(regKey, buf, REG_BINARY, buffer, bufferSize * 2))
            break;
        LPWSTR ptr = new WCHAR[wcslen(buffer) + 1];
        if (!ptr)
            break;
        wcscpy(ptr, buffer);
        history[i] = ptr;
    }
}

void SaveHistory(HKEY regKey, const char* keyPattern, LPWSTR* history, CSalamanderRegistryAbstract* registry)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("SaveHistory(, %s, , )", keyPattern);
    char buf[32];
    int i;
    for (i = 0; i < MAX_HISTORY_ENTRIES; i++)
    {
        if (history[i] == NULL)
            break;
        SalPrintf(buf, 32, keyPattern, i);
        registry->SetValue(regKey, buf, REG_BINARY, history[i], (int)wcslen(history[i]) * 2 + 2);
    }
}

// ****************************************************************************

BOOL TestForCancel()
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("TestForCancel()");  // Petr: prilis pomaly call-stack
    // This polling deadline is private to the cancellation prompt and must remain ordered after a tick wrap.
    static CMonotonicTimePoint nextTest;
    if (!CMonotonicClock::HasReached(nextTest, CMonotonicClock::Now()))
        return FALSE;

    BOOL cancel = FALSE;
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8001) && GetForegroundWindow() == SG->GetMainWindowHWND())
    {
        MSG msg; // discard the buffered ESC
        while (PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
            ;
        cancel = TRUE;
    }
    else
    {
        cancel = SG->GetSafeWaitWindowClosePressed();
    }

    cancel = cancel && SG->SalMessageBox(SG->GetMsgBoxParent(),
                                         LoadStr(IDS_CANCEL), LoadStr(IDS_QUESTION),
                                         MB_YESNO | MB_ICONQUESTION | MSGBOXEX_ESCAPEENABLED) == IDYES;
    UpdateWindow(SG->GetMainWindowHWND());
    nextTest = CMonotonicClock::DeadlineAfter(150);
    SG->WaitForESCRelease();
    return cancel;
}

BOOL DuplicateChar(WCHAR dup, LPWSTR buffer, int bufferSize)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("DuplicateChar(, , %d)", bufferSize);
    if (buffer == NULL)
    {
        TRACE_E("1. unexpected situation in DuplicateChar()");
        return FALSE;
    }
    LPWSTR s = buffer;
    int l = (int)wcslen(buffer);
    if (l >= bufferSize)
    {
        TRACE_E("2. unexpected situation in DuplicateChar()");
        return FALSE;
    }
    BOOL ret = TRUE;
    while (*s != L'\0')
    {
        if (*s == dup)
        {
            if (l + 1 < bufferSize)
            {
                memmove(s + 1, s, (l - (s - buffer) + 1) * 2); // duplicate the quote
                l++;
                s++;
            }
            else // doesn't fit, trim the buffer
            {
                ret = FALSE;
                memmove(s + 1, s, (l - (s - buffer)) * 2); // duplicate the quote, trim by one char
                buffer[l] = L'\0';
                s++;
            }
        }
        s++;
    }
    return ret;
}

LPWSTR
UnDuplicateChar(WCHAR dup, LPWSTR buffer)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("UnDuplicateChar(, )");
    if (buffer == NULL)
    {
        TRACE_E("1. unexpected situation in UnDuplicateChar()");
        return FALSE;
    }
    LPWSTR s = buffer;
    BOOL ret = TRUE;
    while (*s != L'\0')
    {
        if (s[0] == dup && s[1] == dup)
        {
            MoveMemory(s, s + 1, (wcslen(s + 1) + 1) * 2);
        }
        s++;
    }
    return buffer;
}

BOOL ParseFullPath(WCHAR* path, WCHAR*& keyName, int& keyRoot)
{
    CALL_STACK_MESSAGE2("ParseFullPath(, , %d)", keyRoot);
    if (path[0] == L'\0')
        return FALSE;
    if (path[0] == L'\\' && path[1] == L'\0')
    {
        keyName = path + 1;
        keyRoot = -1;
        return TRUE;
    }
    keyName = wcschr(path + 1, L'\\');
    if (keyName == NULL)
        keyName = path + wcslen(path);
    int len = (int)(keyName - (path + 1));
    if (*keyName == L'\\')
        keyName++;
    if (len)
    {
        int i = 0;
        while (PredefinedHKeys[i].HKey != NULL)
        {
            if ((int)wcslen(PredefinedHKeys[i].KeyName) == len &&
                CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE,
                               PredefinedHKeys[i].KeyName, len,
                               path + 1, len) == CSTR_EQUAL &&
                ((path + 1)[len] == L'\0' || (path + 1)[len] == L'\\'))
            {
                keyRoot = i;
                return TRUE;
            }
            i++;
        }
    }
    return FALSE;
}

inline BOOL IsXdigit(WCHAR c)
{
    CALL_STACK_MESSAGE_NONE
    return c >= L'0' && c <= L'9' || towlower(c) >= L'a' && towlower(c) <= L'f';
}

void ConvertHexToString(LPWSTR text, char* hex, int& len)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("ConvertHexToString(, , %d)", len);
    len = 0;
    LPWSTR s = text; //, st = text;
    BYTE value = 0;
    BOOL openedQuotesASCII = FALSE;
    BOOL openedQuotesUnicode = FALSE;
    while (1)
    {
        if (*s == L'"' && !openedQuotesASCII)
        {
            s++;
            openedQuotesUnicode = !openedQuotesUnicode;
            continue;
        }
        if (*s == L'\'' && !openedQuotesUnicode)
        {
            s++;
            openedQuotesASCII = !openedQuotesASCII;
            continue;
        }
        if (openedQuotesUnicode)
        {
            if (*s == 0)
                break;
            else
            {
                *(LPWSTR)(hex + len) = *s++;
                len += 2;
            }
        }
        else
        {
            if (openedQuotesASCII)
            {
                if (*s == 0)
                    break;
                else
                {
                    WStrToStr(hex + len, 1, s++, 1);
                    len += 1;
                }
            }
            else
            {
                if (IsXdigit(*s))
                {
                    if (*s >= L'0' && *s <= L'9')
                        value = (BYTE)(*s - L'0'); // first digit
                    else
                        value = (BYTE)(10 + (towlower(*s) - L'a'));
                    s++;
                    if (IsXdigit(*s)) // second digit
                    {
                        value <<= 4;
                        if (*s >= L'0' && *s <= L'9')
                            value |= (BYTE)(*s - '0');
                        else
                            value |= (BYTE)(10 + (towlower(*s) - L'a'));
                        s++;
                    }
                    hex[len++] = value;
                }
                else
                {
                    if (*s == L'\0')
                        break; // end of string
                    else
                        s++; // skip the space
                }
            }
        }
    }
}

BOOL ValidateHexString(LPWSTR text)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("ValidateHexString()");
    int len = 0;
    LPWSTR s = text; //, st = text;
    BYTE value = 0;
    BOOL openedQuotesASCII = FALSE;
    BOOL openedQuotesUnicode = FALSE;
    while (1)
    {
        if (*s == L'"' && !openedQuotesASCII)
        {
            s++;
            openedQuotesUnicode = !openedQuotesUnicode;
            continue;
        }
        if (*s == L'\'' && !openedQuotesUnicode)
        {
            s++;
            openedQuotesASCII = !openedQuotesASCII;
            continue;
        }
        if (openedQuotesUnicode)
        {
            if (*s == 0)
                break;
            s++;
            len += 2;
        }
        else
        {
            if (openedQuotesASCII)
            {
                if (*s == 0)
                    break;
                else
                {
                    s++;
                    len += 1;
                }
            }
            else
            {
                if (IsXdigit(*s))
                {
                    s++;
                    if (IsXdigit(*s)) // second digit (required)
                    {
                        s++;
                    }
                    else
                        return FALSE;
                }
                else
                {
                    if (*s == L'\0')
                        break; // end of string
                    else
                    {
                        if (*s != L' ')
                            return FALSE;
                        s++; // skip the space
                    }
                }
            }
        }
    }
    return TRUE;
}

// ****************************************************************************
//
// CBuffer
//

BOOL CBuffer::Reserve(int size)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE2("CBuffer::Reserve(%d)", size);
    if (size > Allocated)
    {
        int s = max(Allocated * 2, size);
        Release();
        Buffer = malloc(s);
        if (Buffer)
            Allocated = s;
        else
            return FALSE;
    }
    return TRUE;
}

// ****************************************************************************

char* Replace(char* string, char s, char d)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE3("Replace(, %d, %d)", s, d);
    char* iterator = string;
    while (*iterator)
    {
        if (*iterator == s)
            *iterator = d;
        iterator++;
    }
    return string;
}

// The Shell file dialog is Unicode-only, so ANSI plug-in text converts at this boundary.
static WCHAR* DupWideFromAnsi(const char* text)
{
    int len = MultiByteToWideChar(CP_ACP, 0, text, -1, NULL, 0);
    if (len == 0)
        return NULL;
    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide != NULL && MultiByteToWideChar(CP_ACP, 0, text, -1, wide, len) == 0)
    {
        free(wide);
        return NULL;
    }
    return wide;
}

BOOL GetOpenFileName(HWND parent, const char* title, const char* filter, char* buffer, BOOL save)
{
    CALL_STACK_MESSAGE4("GetOpenFileName(, %s, %s, , %d)", title, filter, save);
    char buf[200];
    // A common-dialog filter must remain complete and double-null compatible; do not pass a truncated filter to the shell.
    if (FAILED(StringCchCopyA(buf, _countof(buf), filter)))
        return FALSE;
    Replace(buf, '\t', '\0');

    // count the filter segments; they alternate description/pattern and end with an empty one
    UINT segCount = 0;
    for (const char* s = buf; *s != '\0'; )
    {
        segCount++;
        while (*s)
            s++;
        s++;
    }
    if (segCount < 2 || (segCount % 2) != 0)
        return FALSE;

    // migrate to the modern Shell file dialog; the former literal flag set is kept
    // (no shell overwrite prompt for save, an existing file is required for open,
    // and the dialog does not change the process current directory)
    HRESULT comInit = CoInitialize(NULL);
    if (FAILED(comInit) && comInit != RPC_E_CHANGED_MODE)
        return FALSE;
    BOOL comOwned = SUCCEEDED(comInit);

    UINT pairCount = segCount / 2;
    COMDLG_FILTERSPEC* specs = new COMDLG_FILTERSPEC[pairCount];
    WCHAR** texts = new WCHAR*[segCount];
    BOOL setupOK = specs != NULL && texts != NULL;
    UINT filled = 0;
    if (setupOK)
    {
        memset(specs, 0, sizeof(COMDLG_FILTERSPEC) * pairCount);
        const char* s = buf;
        while (setupOK && *s != '\0')
        {
            WCHAR* wide = DupWideFromAnsi(s); // dup stops at the segment terminator
            if (wide == NULL)
                setupOK = FALSE;
            else
            {
                texts[filled] = wide;
                if ((filled % 2) == 0)
                    specs[filled / 2].pszName = wide;
                else
                    specs[filled / 2].pszSpec = wide;
                filled++;
                s += strlen(s) + 1;
            }
        }
        setupOK = filled == segCount;
    }

    BOOL ret = FALSE;
    IFileDialog* fileDialog = NULL;
    if (setupOK &&
        SUCCEEDED(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, NULL,
                                   CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fileDialog))) &&
        fileDialog != NULL)
    {
        DWORD options = FOS_NOCHANGEDIR;
        fileDialog->GetOptions(&options);
        options |= FOS_NOCHANGEDIR;
        if (!save)
            options |= FOS_FILEMUSTEXIST;
        options &= ~FOS_OVERWRITEPROMPT; // parity with the former flag set
        fileDialog->SetOptions(options);

        WCHAR* titleW = DupWideFromAnsi(title);
        if (titleW != NULL)
        {
            fileDialog->SetTitle(titleW);
            free(titleW);
        }
        fileDialog->SetFileTypes(pairCount, specs);
        fileDialog->SetFileTypeIndex(1);

        // the persisted buffer holds either a directory (seeds only the location) or a
        // file name (seeds the location via its parent plus the suggested leaf name)
        DWORD attr = SG->SalGetFileAttributes(buffer);
        WCHAR* nameW = DupWideFromAnsi(buffer);
        if (nameW != NULL)
        {
            if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                IShellItem* folder = NULL;
                if (SUCCEEDED(SHCreateItemFromParsingName(nameW, NULL, IID_PPV_ARGS(&folder))))
                {
                    fileDialog->SetFolder(folder);
                    folder->Release();
                }
            }
            else
            {
                PWSTR sep = wcsrchr(nameW, L'\\');
                if (sep != NULL)
                {
                    *sep = 0;
                    if (nameW[0] != 0)
                    {
                        IShellItem* folder = NULL;
                        if (SUCCEEDED(SHCreateItemFromParsingName(nameW, NULL, IID_PPV_ARGS(&folder))))
                        {
                            fileDialog->SetFolder(folder);
                            folder->Release();
                        }
                    }
                    fileDialog->SetFileName(sep + 1);
                }
                else
                    fileDialog->SetFileName(nameW);
            }
            free(nameW);
        }

        if (SUCCEEDED(fileDialog->Show(parent)))
        {
            IShellItem* item = NULL;
            PWSTR pathW = NULL;
            if (SUCCEEDED(fileDialog->GetResult(&item)) && item != NULL &&
                SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathW)) && pathW != NULL)
            {
                char fileName[MAX_PATH];
                int converted = WideCharToMultiByte(CP_ACP, 0, pathW, -1, fileName, MAX_PATH, NULL, NULL);
                CoTaskMemFree(pathW);
                // a result that cannot fit the fixed caller field fails instead of truncating
                ret = converted > 0 && strlen(fileName) < MAX_PATH;
                if (ret)
                    strcpy(buffer, fileName);
            }
            if (item != NULL)
                item->Release();
        }
        fileDialog->Release();
    }

    if (texts != NULL)
    {
        for (UINT i = 0; i < filled; i++)
            free(texts[i]);
        delete[] texts;
    }
    delete[] specs;
    if (comOwned)
        CoUninitialize();
    return ret;
}

BOOL RemoveFSNameFromPath(LPWSTR path)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("RemoveFSNameFromPath()");
    LPWSTR iterator = path;
    while (*iterator != L'\0' && *iterator != L'\\')
    {
        if (*iterator == L':')
        {
            char fsName[MAX_PATH];
            if (iterator - path)
            {
                int len = (int)WStrToStr(fsName, MAX_PATH, path, (int)(iterator - path));
                if (len == (int)strlen(AssignedFSName) && SG->StrNICmp(fsName, AssignedFSName, len) == 0)
                {
                    memmove(path, iterator + 1, (wcslen(iterator + 1) + 1) * 2);
                    return TRUE;
                }
                else
                    return FALSE; // unknown FS
            }
            else
                return FALSE; // unknown FS
        }
        iterator++;
    }
    return TRUE;
}

BOOL DecStringToNumber(char* string, QWORD& qw)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("DecStringToNumber(, )");
    QWORD ret = 0;

    // trim leading whitespace
    while (isspace(*string))
        string++;

    if (*string == 0)
        return FALSE;

    while (isdigit(*string))
    {
        if (ret > (((QWORD)-1) - (*string - '0')) / 10)
            return FALSE; // overflow
        ret = ret * 10 + *string - '0';
        string++;
    }

    // trim trailing whitespace
    while (*string)
    {
        if (!isspace(*string))
            return FALSE;
        string++;
    }

    qw = ret;
    return TRUE;
}

BOOL HexStringToNumber(char* string, QWORD& qw)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("HexStringToNumber(, )");
    QWORD ret = 0;

    // trim leading whitespace
    while (isspace(*string))
        string++;

    if (string[0] == 0)
        return FALSE;

    while (isxdigit(*string))
    {
        if (ret >
            (((QWORD)-1) - (isdigit(*string) ? *string - '0' : tolower(*string) - 'a' + 10)) / 10)
            return FALSE; // overflow
        ret = (ret << 4) + (isdigit(*string) ? *string - '0' : tolower(*string) - 'a' + 10);
        string++;
    }

    // trim trailing whitespace
    while (*string)
    {
        if (!isspace(*string))
            return FALSE;
        string++;
    }

    qw = ret;
    return TRUE;
}

char* ReplaceUnsafeCharacters(char* string)
{
    CALL_STACK_MESSAGE_NONE
    //  CALL_STACK_MESSAGE1("ReplaceUnsafeCharacters()");
    char* iterator = string;
    while (*iterator)
    {
        if (*iterator > 0 && *iterator <= 31 ||
            strchr("*?<>:\"/\\|", *iterator))
            *iterator = '_';
        iterator++;
    }
    return string;
}

/*
LPDLGTEMPLATE
LoadDlgTemplate(int id, DWORD &size)
{
  CALL_STACK_MESSAGE3("LoadDlgTemplate(%d, 0x%X)", id, size);
  LPDLGTEMPLATE ret = NULL;
  HRSRC hRsrc = FindResource(HLanguage, MAKEINTRESOURCE(id),  MAKEINTRESOURCE(RT_DIALOG));
  if (hRsrc)
  {
    void * data = LoadResource(HLanguage, hRsrc);
    if (data)
    {
      size = SizeofResource(HLanguage, hRsrc);
      if (size)
      {
        ret = (LPDLGTEMPLATE) malloc(size);
        if (ret)
        {
          memcpy(ret, data, size);
        }
        else TRACE_E("Low memory");
      }
      else TRACE_E("Unable to get size of resource");
    }
    else TRACE_E("Unable to load resource");
  }
  else TRACE_E("Unable to find resource");
  return ret;
}
*/

/*
LPDLGTEMPLATE
ReplaceDlgTemplateFont(LPDLGTEMPLATE dlgTemplate, DWORD &size, LPCWSTR newFont)
{
  CALL_STACK_MESSAGE2("ReplaceDlgTemplateFont(, 0x%X, )", size);
  if (!(dlgTemplate->style & DS_SETFONT ))
  {
    TRACE_E("ReplaceDlgTemplateFont: Dialog template lacks DS_SETFONT.");
    return dlgTemplate;
  }

  LPWSTR ptr = LPWSTR((char *)dlgTemplate + sizeof(DLGTEMPLATE));

  // skip the menu array
  if (*ptr == 0xFFFF) ptr += 2; // followed by the ordinal value of a menu resource
  else
  {
    // this is a NULL-terminated string
    ptr += wcslen(ptr) + 1;
  }

  // skip the class array
  if (*ptr == 0xFFFF) ptr += 2; // followed by the ordinal value of a predefined system window class
  else
  {
    // this is a NULL-terminated string
    ptr += wcslen(ptr) + 1;
  }

  // skip the window title
  ptr += wcslen(ptr) + 1;

  // skip the point size
  ptr++;

  // adjust the template size and move the data following the font name
  int len1 = wcslen(ptr) + 1;
  int len2 = wcslen(newFont) + 1;
  DWORD dest = (DWORD(ptr + len2) - DWORD(dlgTemplate) + 3)/4*4;
  DWORD sour = (DWORD(ptr + len1) - DWORD(dlgTemplate) + 3)/4*4;
  if (sour > dest)
  {
    memmove((char*)dlgTemplate + dest, (char*)dlgTemplate + sour, size - sour);
    size -= sour - dest;
  }
  else
  {
    if (sour < dest)
    {
      DWORD offset = DWORD(ptr) - DWORD(dlgTemplate);
      void * p = realloc(dlgTemplate, size + (dest - sour));
      if (!p)
      {
        TRACE_E("Low memory");
        return dlgTemplate;
      }
      dlgTemplate = (LPDLGTEMPLATE) p;
      ptr = LPWSTR(DWORD(dlgTemplate) + offset);

      memmove((char*)dlgTemplate + dest, (char*)dlgTemplate + sour, size - sour);
      size += dest - sour;
    }
  }

  // write the new font name
  wcscpy(ptr, newFont);
  
  return dlgTemplate;
}
*/
