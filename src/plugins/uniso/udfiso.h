// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "fs.h"
#include "iso9660.h"
#include "udf.h"
#include "hfs.h"

// ****************************************************************************
//
// CUDFISO
//

// Bridge filesystem that lists ISO 9660, UDF, and/or HFS trees from the same track.
class CUDFISO : public CUnISOFSAbstract
{
public:
    DWORD ExtentOffset;

protected:
    CISO9660* ISO;
    CUDF* UDF;
    CHFS* HFS;

public:
    CUDFISO(CISOImage* image, DWORD extent);
    virtual ~CUDFISO();
    // methods

    virtual BOOL Open(BOOL quiet);
    virtual BOOL DumpInfo(FILE* outStream);
    virtual BOOL ListDirectory(char* path, int session,
                               CSalamanderDirectoryAbstract* dir, CPluginDataInterfaceAbstract*& pluginData);
    virtual int UnpackFile(CSalamanderForOperationsAbstract* salamander, const char* srcPath, const char* path,
                           const char* nameInArc, const CFileData* fileData, DWORD& silent, BOOL& toSkip);
};
