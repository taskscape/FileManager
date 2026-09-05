// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

struct SEndingFile
{
    const CFileData* fileData;
    char* path;
    BOOL isDir;
    char* mask;
};

class CNameTree;

struct SBranch
{
    unsigned char ch;
    CNameTree* next;
};

// Trie of names selected for unpack, used to match TAR members against a mask or selection.
class CNameTree
{
private:
    TDirectArray<SBranch> Branches;
    TDirectArray<SEndingFile>* EndingNames;

public:
    CNameTree() : Branches(1, 1) { EndingNames = NULL; }
    ~CNameTree();
    void Add(const char* name, const BOOL isDir, const char* path, const CFileData* fileData);
    BOOL IsNamePresent(const char* name, const BOOL hasExtension);
};

// Set of archive member names used to decide which TAR entries to extract.
class CNames
{
private:
    CNameTree NameTree;

public:
    CNames() {};
    ~CNames() {};
    void AddName(const char* name, const BOOL isDir, const char* path, const CFileData* fileData)
    {
        if (name == NULL || *name == '\0')
            return;
        NameTree.Add(name, isDir, path, fileData);
    }
    BOOL IsNamePresent(const char* name)
    {
        return NameTree.IsNamePresent(name, strchr(name, '.') != NULL);
    }
};
