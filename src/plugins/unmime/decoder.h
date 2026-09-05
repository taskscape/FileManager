// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

/// main export ////////////////////////////////////////////////////////////////

BOOL DecodeSelectedBlocks(LPCTSTR pszFileName, CParserOutput* output, LPCTSTR dir, FILETIME* pft,
                          CSalamanderForOperationsAbstract* Salamander, const CQuadWord& totalSize,
                          BOOL* pAborted = NULL, BOOL bOnlyOneFile = FALSE);

extern int iErrorStr;

/// export for parser.cpp /////////////////////////////////////////////////////

// Base decoder that writes decoded bytes (or just counts size) into a target file.
class CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL bLastLine) { return TRUE; };
    virtual BOOL End();
    virtual void SaveState();
    virtual void RestoreState();

    int iDecodedSize;

protected:
    HANDLE HFile;
    BOOL bCalcSize;
    char* PBuffer;
    int iBufPos;
    char FileName[MAX_PATH];

    virtual BOOL BufferedWrite(const void* pData, int nBytes);
};

// No-op decoder used for blocks that should not produce output.
class CNullDecoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE, char*, BOOL) { return TRUE; };
    virtual BOOL End() { return TRUE; };
};

// Copies 7bit/8bit/binary text lines unchanged into the output file.
class CTextDecoder : public CDecoder
{
public:
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL bLastLine);
};

// Quoted-printable decoder for MIME body parts.
class CQPDecoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL bLastLine);

private:
    BYTE table[256];
};

// Base64 decoder for MIME body parts.
class CBase64Decoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL);
    virtual void SaveState();
    virtual void RestoreState();

private:
    BOOL DecodeChar(char newchar);
    char c[4];
    int n;
    BOOL bDataDone;
    BYTE table[256];
};

// UUencode/XXencode decoder (bXX selects the alphabet).
class CUUXXDecoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL);
    BOOL bXX;

private:
    BYTE table[256];
};

// BinHex 4.0 decoder that extracts the data fork and reports CRC failures.
class CBinHexDecoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL);
    virtual BOOL End();

    BOOL bFinished, bCRCFailed;
    int iDataLength, iResourceLength;
    char cFileName[64];

private:
    enum
    {
        CS_START,
        CS_DATA,
        CS_END
    } eCharState;

    enum
    {
        RS_NORMALBYTE,
        RS_COUNTBYTE
    } eRLEState;

    enum
    {
        BS_START,
        BS_FILENAME,
        BS_CRAP1,
        BS_DATALENGTH,
        BS_RESOURCELENGTH,
        BS_HEADERCRC,
        BS_DATA,
        BS_DATACRC,
        BS_END
    } eBinaryState;

    BOOL DecodeChar(char c);
    void DecodeRLE(BYTE b);
    void DecodeBinary(BYTE b);

    int counter, garbage_counter;
    int name_length;
    char acc[4];
    int n;
    WORD data_crc, calculated_crc;
    BYTE rle_last;
    BYTE table[256];
};

// yEnc decoder for Usenet attachments, including CRC32 checking.
class CYEncDecoder : public CDecoder
{
public:
    virtual BOOL Start(HANDLE hFile, char* fileName, BOOL bJustCalcSize = FALSE);
    virtual BOOL DecodeLine(LPTSTR pszLine, BOOL);

    BOOL bError;
    DWORD CRC;

private:
    UINT32 crctable[256];
};

void BuildUUTable(BYTE* pTable);
void BuildXXTable(BYTE* pTable);
