// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

// Persistent object evidence includes creation time to reject file-ID reuse;
// readiness additionally binds length and digest to the bytes actually observed.
struct CRecoveryObjectEvidence
{
    DWORD Volume = 0, IndexHigh = 0, IndexLow = 0;
    ULONGLONG Creation = 0, WriteTime = 0, Length = 0;
    BYTE Digest[32] = {};
    BOOL Present = FALSE, DigestValid = FALSE, PlainFile = FALSE;
};

inline ULONGLONG RecoveryFileTime(const FILETIME& time)
{
    return ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
}

inline BOOL ReadRecoveryObjectIdentity(HANDLE file, CRecoveryObjectEvidence& evidence)
{
    BY_HANDLE_FILE_INFORMATION information;
    if (!GetFileInformationByHandle(file, &information)) return FALSE;
    evidence = CRecoveryObjectEvidence();
    evidence.Present = TRUE;
    evidence.Volume = information.dwVolumeSerialNumber;
    evidence.IndexHigh = information.nFileIndexHigh;
    evidence.IndexLow = information.nFileIndexLow;
    evidence.Creation = RecoveryFileTime(information.ftCreationTime);
    evidence.WriteTime = RecoveryFileTime(information.ftLastWriteTime);
    evidence.Length = ((ULONGLONG)information.nFileSizeHigh << 32) | information.nFileSizeLow;
    evidence.PlainFile = (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
    return TRUE;
}

inline BOOL SameRecoveryObject(const CRecoveryObjectEvidence& expected, const CRecoveryObjectEvidence& actual)
{
    return expected.Present == actual.Present && (!expected.Present ||
        (expected.Volume == actual.Volume && expected.IndexHigh == actual.IndexHigh &&
         expected.IndexLow == actual.IndexLow && expected.Creation == actual.Creation));
}

// This journal version fingerprints the main data stream. Preserve stream-bearing
// files for manual recovery instead of treating an incomplete fingerprint as permission to delete.
inline BOOL RecoveryHasOnlyPrimaryStream(HANDLE file)
{
    DWORD flags = 0;
    if (GetVolumeInformationByHandleW(file, NULL, 0, NULL, NULL, &flags, NULL, 0) &&
        (flags & FILE_NAMED_STREAMS) == 0) return TRUE;
    std::vector<BYTE> buffer(1024, 0);
    if (!GetFileInformationByHandleEx(file, FileStreamInfo, buffer.data(), (DWORD)buffer.size())) return FALSE;
    const FILE_STREAM_INFO* stream = (const FILE_STREAM_INFO*)buffer.data();
    const WCHAR primary[] = L"::$DATA";
    return stream->NextEntryOffset == 0 && stream->StreamNameLength == sizeof(primary) - sizeof(WCHAR) &&
           memcmp(stream->StreamName, primary, sizeof(primary) - sizeof(WCHAR)) == 0;
}

inline BOOL CalculateRecoveryDigest(HANDLE file, BYTE digest[32])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD objectLength = 0, returned = 0;
    DWORD error = ERROR_SUCCESS;
    std::vector<BYTE> object;
    std::vector<BYTE> buffer(64 * 1024);
    LARGE_INTEGER start = {};
    if (!SetFilePointerEx(file, start, NULL, FILE_BEGIN)) return FALSE;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, (BYTE*)&objectLength, sizeof(objectLength), &returned, 0) < 0)
        error = ERROR_NOT_SUPPORTED;
    if (error == ERROR_SUCCESS)
    {
        object.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, NULL, 0, 0) < 0)
            error = ERROR_NOT_SUPPORTED;
    }
    while (error == ERROR_SUCCESS)
    {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), (DWORD)buffer.size(), &read, NULL)) { error = GetLastError(); break; }
        if (read == 0) break;
        if (BCryptHashData(hash, buffer.data(), read, 0) < 0) error = ERROR_READ_FAULT;
    }
    if (error == ERROR_SUCCESS && BCryptFinishHash(hash, digest, 32, 0) < 0) error = ERROR_READ_FAULT;
    if (hash != NULL) BCryptDestroyHash(hash);
    if (algorithm != NULL) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (error != ERROR_SUCCESS) SetLastError(error);
    return error == ERROR_SUCCESS;
}

inline BOOL CaptureRecoveryEvidence(HANDLE file, CRecoveryObjectEvidence& evidence)
{
    if (!ReadRecoveryObjectIdentity(file, evidence)) return FALSE;
    if (!evidence.PlainFile) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
    CRecoveryObjectEvidence after;
    if (!CalculateRecoveryDigest(file, evidence.Digest) || !ReadRecoveryObjectIdentity(file, after)) return FALSE;
    if (!SameRecoveryObject(evidence, after) || evidence.Length != after.Length || evidence.WriteTime != after.WriteTime)
    { SetLastError(ERROR_FILE_INVALID); return FALSE; }
    evidence.DigestValid = TRUE;
    return TRUE;
}

inline BOOL VerifyRecoveryEvidence(HANDLE file, const CRecoveryObjectEvidence& expected)
{
    CRecoveryObjectEvidence actual;
    if (!expected.Present || !expected.DigestValid) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    if (!CaptureRecoveryEvidence(file, actual)) return FALSE;
    if (!SameRecoveryObject(expected, actual) || expected.Length != actual.Length ||
        expected.WriteTime != actual.WriteTime || memcmp(expected.Digest, actual.Digest, sizeof(actual.Digest)) != 0)
    { SetLastError(ERROR_INVALID_DATA); return FALSE; }
    return TRUE;
}

inline std::string RecoveryHex(ULONGLONG number)
{
    char digits[17];
    const char* alphabet = "0123456789abcdef";
    digits[16] = 0;
    for (int index = 15; index >= 0; --index) { digits[index] = alphabet[number & 15]; number >>= 4; }
    return digits;
}

inline BOOL ParseRecoveryHex(const std::string& value, ULONGLONG& number)
{
    if (value.empty() || value.size() > 16) return FALSE;
    number = 0;
    for (char digit : value)
    {
        const unsigned part = digit >= '0' && digit <= '9' ? digit - '0' :
                              digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : 16;
        if (part == 16) return FALSE;
        number = (number << 4) | part;
    }
    return TRUE;
}

inline std::string SerializeRecoveryEvidence(const CRecoveryObjectEvidence& evidence)
{
    if (!evidence.Present) return "absent";
    std::string result = RecoveryHex(evidence.Volume) + "," + RecoveryHex(evidence.IndexHigh) + "," +
                         RecoveryHex(evidence.IndexLow) + "," + RecoveryHex(evidence.Creation) + "," +
                         RecoveryHex(evidence.WriteTime) + "," + RecoveryHex(evidence.Length) + ",";
    if (!evidence.DigestValid) return result + "unverified";
    const char* digits = "0123456789abcdef";
    for (BYTE byte : evidence.Digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}

inline BOOL ParseRecoveryEvidence(const std::string& text, CRecoveryObjectEvidence& evidence)
{
    evidence = CRecoveryObjectEvidence();
    if (text == "absent") return TRUE;
    ULONGLONG values[6];
    size_t begin = 0;
    for (int index = 0; index < 6; ++index)
    {
        const size_t end = text.find(',', begin);
        if (end == std::string::npos || !ParseRecoveryHex(text.substr(begin, end - begin), values[index])) return FALSE;
        begin = end + 1;
    }
    if (values[0] > MAXDWORD || values[1] > MAXDWORD || values[2] > MAXDWORD) return FALSE;
    evidence.Volume = (DWORD)values[0]; evidence.IndexHigh = (DWORD)values[1]; evidence.IndexLow = (DWORD)values[2];
    evidence.Creation = values[3]; evidence.WriteTime = values[4]; evidence.Length = values[5];
    evidence.Present = TRUE;
    if (text.substr(begin) == "unverified") return TRUE;
    if (text.size() - begin != 64) return FALSE;
    for (size_t index = 0; index < 32; ++index)
    {
        ULONGLONG byte;
        if (!ParseRecoveryHex(text.substr(begin + index * 2, 2), byte)) return FALSE;
        evidence.Digest[index] = (BYTE)byte;
    }
    evidence.DigestValid = evidence.PlainFile = TRUE;
    return TRUE;
}
