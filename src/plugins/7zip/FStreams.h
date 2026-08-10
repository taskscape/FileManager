// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "7za/CPP/7zip/Common/FileStreams.h"

class CRetryableOutFileStream : public IOutStream,
                                public CMyUnknownImp
{
public:
    // Composition retains retry prompts while 26.02 keeps its concrete file streams final.
    Z7_IFACES_IMP_UNK_2(ISequentialOutStream, IOutStream)

public:
    CRetryableOutFileStream(HWND hParentWnd);

    bool Open(CFSTR fileName, DWORD creationDisposition);
    bool SetMTime(const CFiTime* mTime);

private:
    HWND hParentWnd;
    COutFileStream* FileStream;
    CMyComPtr<IOutStream> Stream;
};

class CRetryableInFileStream : public IInStream,
                               public CMyUnknownImp
{
public:
    // The input adapter follows the same composition rule so retry behavior remains available to archive parsing.
    Z7_IFACES_IMP_UNK_2(ISequentialInStream, IInStream)

public:
    CRetryableInFileStream(HWND hParentWnd);

    bool Open(CFSTR fileName);

private:
    HWND hParentWnd;
    CMyComPtr<IInStream> Stream;
};
