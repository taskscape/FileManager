// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "7za/CPP/Common/MyCom.h"
#include "7za/CPP/Common/MyString.h"

#include "7za/CPP/7zip/IPassword.h"
#include "7za/CPP/7zip/Archive/IArchive.h"

class CArchiveOpenCallbackImp : public IArchiveOpenCallback,
                                public IArchiveOpenVolumeCallback,
                                public ICryptoGetTextPassword,
                                public CMyUnknownImp
{
public:
    // Use 26.02's COM declarations so the plug-in advertises each legacy callback interface correctly.
    Z7_IFACES_IMP_UNK_3(IArchiveOpenCallback,
                        IArchiveOpenVolumeCallback,
                        ICryptoGetTextPassword)

public:

private:
    UString& Password;

public:
    // If entered, the password gets propagated towards password
    CArchiveOpenCallbackImp(UString& password);
    ~CArchiveOpenCallbackImp();
};
