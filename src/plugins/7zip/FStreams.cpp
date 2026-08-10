// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"

#include "FStreams.h"
#include "7Zip.h"
#include "7zclient.h"
#include "7zthreads.h"
#include "7zip.rh"
#include "7zip.rh2"
#include "lang\lang.rh"

BOOL ShowRetryAbortBox(HWND hParentWnd, int resID, DWORD err, ...)
{
    TCHAR msg[1024];
    va_list arglist;
    va_start(arglist, err);

    msg[0] = 0;
    vsprintf(msg, LoadStr(resID), arglist);
    va_end(arglist);

    if (!_tcsncmp(msg, _T("{!}"), 3))
    {
        TCHAR fmt[1024];
        SalamanderGeneral->ExpandPluralString(fmt, sizeof(fmt), msg, 1, &CQuadWord().SetUI64(((int*)&err)[1]));
        strcpy(msg, fmt);
    }
    TCHAR buf[2048 + 4];
    _stprintf(buf, _T("%s\n\n%s"), msg, SalamanderGeneral->GetErrorText(err));

    TCHAR btnBuffer[128];
    /* used by the export_mnu.py script, which generates salmenu.mnu for the Translator
   let the message box buttons handle hotkey collisions by simulating a menu
MENU_TEMPLATE_ITEM MsgBoxButtons[] = 
{
  {MNTT_PB, 0
  {MNTT_IT, IDS_BTN_RETRY
  {MNTT_IT, IDS_BTN_ABORT
  {MNTT_PE, 0
};
*/
    _stprintf(btnBuffer, _T("%d\t%s\t%d\t%s"),
              DIALOG_RETRY, LoadStr(IDS_BTN_RETRY),
              DIALOG_CANCEL, LoadStr(IDS_BTN_ABORT));

    MSGBOXEX_PARAMS mbep;
    ZeroMemory(&mbep, sizeof(mbep));
    mbep.HParent = hParentWnd;
    mbep.Caption = LoadStr(IDS_PLUGINNAME);
    mbep.Text = buf;
    mbep.Flags = MSGBOXEX_RETRYCANCEL | MSGBOXEX_ICONEXCLAMATION;
    mbep.AliasBtnNames = btnBuffer;
    if (hParentWnd)
    {
        return SendMessage(hParentWnd, WM_7ZIP, WM_7ZIP_SHOWMBOXEX, (LPARAM)&mbep) == DIALOG_RETRY;
    }
    else
    {
        // This can only happen when reading an archive file
        mbep.HParent = SalamanderGeneral->GetMsgBoxParent();
        return SalamanderGeneral->SalMessageBoxEx(&mbep) == DIALOG_RETRY;
    }
}

CRetryableOutFileStream::CRetryableOutFileStream(HWND _hParentWnd) : hParentWnd(_hParentWnd), FileStream(NULL)
{
}

// Keep retry handling at the host boundary because 26.02's concrete stream is intentionally final.
bool CRetryableOutFileStream::Open(CFSTR fileName, DWORD creationDisposition)
{
    COutFileStream* fileStream = new COutFileStream;
    if (fileStream == NULL)
        return false;

    CMyComPtr<IOutStream> stream(fileStream);
    bool opened = false;
    switch (creationDisposition)
    {
    case OPEN_EXISTING:
        opened = fileStream->Open_EXISTING(fileName);
        break;
    case OPEN_ALWAYS:
        opened = fileStream->Create_ALWAYS_or_Open_ALWAYS(fileName, false);
        break;
    case CREATE_ALWAYS:
        opened = fileStream->Create_ALWAYS(fileName);
        break;
    case CREATE_NEW:
        opened = fileStream->Create_NEW(fileName);
        break;
    default:
        return false;
    }

    if (!opened)
        return false;

    FileStream = fileStream;
    Stream = stream;
    return true;
}

bool CRetryableOutFileStream::SetMTime(const CFiTime* mTime)
{
    return FileStream != NULL && FileStream->SetMTime(mTime);
}

STDMETHODIMP CRetryableOutFileStream::Write(const void* data, UInt32 size, UInt32* processedSize)
{
    // A failed low-level write can report no progress, so keep the retry offset deterministic.
    UInt32 written = 0;
    HRESULT ret;

    if (processedSize)
        *processedSize = 0;
    while ((S_OK != (ret = Stream->Write(data, size, &written))) /*|| (size != written)*/)
    {
        // NOTE: COutFileStream::Write writes at most kChunkSizeMax bytes (4MB) at once
        data = (char*)data + written;
        size -= written;
        if (!ShowRetryAbortBox(hParentWnd, IDS_CANT_WRITE, ret & 0xFFFF, size))
        {
            ret = E_ABORT;
            break;
        }
        if (processedSize)
            *processedSize += written;
    }
    if (processedSize)
        *processedSize += written;

    return ret;
}

STDMETHODIMP CRetryableOutFileStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    return Stream->Seek(offset, seekOrigin, newPosition);
}

STDMETHODIMP CRetryableOutFileStream::SetSize(UInt64 newSize)
{
    return Stream->SetSize(newSize);
}

CRetryableInFileStream::CRetryableInFileStream(HWND _hParentWnd) : hParentWnd(_hParentWnd)
{
}

// Own the final 26.02 reader through its COM interface while preserving the host retry prompt.
bool CRetryableInFileStream::Open(CFSTR fileName)
{
    CInFileStream* fileStream = new CInFileStream;
    if (fileStream == NULL)
        return false;

    CMyComPtr<IInStream> stream(fileStream);
    if (!fileStream->Open(fileName))
        return false;

    Stream = stream;
    return true;
}

STDMETHODIMP CRetryableInFileStream::Read(void* data, UInt32 size, UInt32* processedSize)
{
    // A failed low-level read can report no progress, so keep the retry offset deterministic.
    UInt32 read = 0;
    HRESULT ret;

    if (processedSize)
        *processedSize = 0;
    while (S_OK != (ret = Stream->Read(data, size, &read)))
    {
        data = (char*)data + read;
        size -= read;
        if (!ShowRetryAbortBox(hParentWnd, hParentWnd ? IDS_CANT_READ : IDS_CANT_READ_ARCHIVE, ret & 0xFFFF, size))
        {
            ret = E_ABORT;
            break;
        }
        if (processedSize)
            *processedSize += read;
    }
    if (processedSize)
        *processedSize += read;

    return ret;
}

STDMETHODIMP CRetryableInFileStream::Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition)
{
    return Stream->Seek(offset, seekOrigin, newPosition);
}
