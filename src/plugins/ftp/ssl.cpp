// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"
#include <strsafe.h> // counted bounded copies (StringCchCopyNA)

#define MAX_DER_CERT_SIZE 5120
#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

#define FTP_CERTIFICATE_EXCEPTION_LIMIT 64
#define FTP_CERTIFICATE_FINGERPRINT_SIZE 32
#define FTP_CERTIFICATE_SESSION_EXCEPTION_HOURS 8
#define FTP_CERTIFICATE_PERSISTENT_EXCEPTION_DAYS 30

static const char* FTP_CERTIFICATE_EXCEPTIONS_KEY = "Certificate Exceptions";
static const char* FTP_CERTIFICATE_EXCEPTION_HOST = "Host";
static const char* FTP_CERTIFICATE_EXCEPTION_PORT = "Port";
static const char* FTP_CERTIFICATE_EXCEPTION_SPKI = "SPKI SHA-256";
static const char* FTP_CERTIFICATE_EXCEPTION_CERTIFICATE = "Certificate SHA-256";
static const char* FTP_CERTIFICATE_EXCEPTION_SCOPE = "Scope";
static const char* FTP_CERTIFICATE_EXCEPTION_EXPIRY = "Expiry";

struct CCertificateExceptionRecord
{
    char Host[HOST_MAX_SIZE];
    unsigned short Port;
    BYTE SPKIFingerprint[FTP_CERTIFICATE_FINGERPRINT_SIZE];
    BYTE CertificateFingerprint[FTP_CERTIFICATE_FINGERPRINT_SIZE];
    FILETIME ExpiresAt;
    DWORD Scope;
};

static bool IsExpired(const FILETIME& expiresAt)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER nowValue = {};
    ULARGE_INTEGER expiryValue = {};
    nowValue.LowPart = now.dwLowDateTime;
    nowValue.HighPart = now.dwHighDateTime;
    expiryValue.LowPart = expiresAt.dwLowDateTime;
    expiryValue.HighPart = expiresAt.dwHighDateTime;
    return expiryValue.QuadPart <= nowValue.QuadPart;
}

static FILETIME CertificateExceptionExpiry(CCertificateExceptionScope scope)
{
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER expiry = {};
    expiry.LowPart = now.dwLowDateTime;
    expiry.HighPart = now.dwHighDateTime;
    const ULONGLONG hours = scope == cesPersistent ? FTP_CERTIFICATE_PERSISTENT_EXCEPTION_DAYS * 24ULL
                                                    : FTP_CERTIFICATE_SESSION_EXCEPTION_HOURS;
    expiry.QuadPart += hours * 60ULL * 60ULL * 10000000ULL;
    FILETIME result = {expiry.LowPart, expiry.HighPart};
    return result;
}

static bool GetCertificateFingerprints(const BYTE* der, int derLength, BYTE* spkiFingerprint,
                                       BYTE* certificateFingerprint)
{
    if (der == NULL || derLength <= 0)
        return false;

    PCCERT_CONTEXT certificate = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der, derLength);
    if (certificate == NULL)
        return false;

    DWORD fingerprintLength = FTP_CERTIFICATE_FINGERPRINT_SIZE;
    bool ok = CryptHashCertificate(0, CALG_SHA_256, 0, der, derLength, certificateFingerprint, &fingerprintLength) &&
              fingerprintLength == FTP_CERTIFICATE_FINGERPRINT_SIZE;
    fingerprintLength = FTP_CERTIFICATE_FINGERPRINT_SIZE;
    ok = ok && CryptHashPublicKeyInfo(0, CALG_SHA_256, 0, X509_ASN_ENCODING,
                                      &certificate->pCertInfo->SubjectPublicKeyInfo, spkiFingerprint,
                                      &fingerprintLength) &&
         fingerprintLength == FTP_CERTIFICATE_FINGERPRINT_SIZE;
    CertFreeCertificateContext(certificate);
    return ok;
}

class CCertificateExceptionStore
{
public:
    CCertificateExceptionStore() : Count(0)
    {
        // Certificate decisions can be consulted by connection and operation workers concurrently.
        HANDLES(InitializeCriticalSection(&CriticalSection));
    }

    ~CCertificateExceptionStore() { HANDLES(DeleteCriticalSection(&CriticalSection)); }

    bool Remember(const char* host, unsigned short port, const BYTE* der, int derLength,
                  CCertificateExceptionScope scope)
    {
        CCertificateExceptionRecord record = {};
        if (host == NULL || host[0] == 0 || !GetCertificateFingerprints(der, derLength, record.SPKIFingerprint,
                                                                          record.CertificateFingerprint))
            return false;

        lstrcpynA(record.Host, host, SizeOf(record.Host));
        record.Port = port;
        record.Scope = scope;
        record.ExpiresAt = CertificateExceptionExpiry(scope);

        HANDLES(EnterCriticalSection(&CriticalSection));
        ClampCount();
        int replace = -1;
        for (int i = 0; i < Count; ++i)
        {
            if (_stricmp(Records[i].Host, record.Host) == 0 && Records[i].Port == record.Port &&
                memcmp(Records[i].CertificateFingerprint, record.CertificateFingerprint,
                       FTP_CERTIFICATE_FINGERPRINT_SIZE) == 0)
            {
                replace = i;
                break;
            }
        }
        if (replace == -1)
        {
            // A fixed store prevents repeated hostile certificates from growing configuration without bound.
            replace = Count < FTP_CERTIFICATE_EXCEPTION_LIMIT ? Count++ : 0;
        }
        // ClampCount keeps replace in range so a negative Count cannot write Records[-1].
        if (replace >= 0 && replace < FTP_CERTIFICATE_EXCEPTION_LIMIT)
            Records[replace] = record;
        HANDLES(LeaveCriticalSection(&CriticalSection));
        return true;
    }

    bool IsAccepted(const char* host, unsigned short port, const BYTE* der, int derLength, bool* endpointKnown)
    {
        BYTE spkiFingerprint[FTP_CERTIFICATE_FINGERPRINT_SIZE];
        BYTE certificateFingerprint[FTP_CERTIFICATE_FINGERPRINT_SIZE];
        if (endpointKnown)
            *endpointKnown = false;
        if (host == NULL || host[0] == 0 ||
            !GetCertificateFingerprints(der, derLength, spkiFingerprint, certificateFingerprint))
            return false;

        bool accepted = false;
        HANDLES(EnterCriticalSection(&CriticalSection));
        ClampCount();
        // Stop once i is outside the live prefix: removing the last expired slot with
        // only `i >= 0` decrements Count through zero and Records[-1] overlays Config.ConParamsCS.
        for (int i = Count - 1; i >= 0 && i < Count;)
        {
            if (IsExpired(Records[i].ExpiresAt))
            {
                RemoveAt(i);
                continue;
            }
            if (_stricmp(Records[i].Host, host) == 0 && Records[i].Port == port)
            {
                if (endpointKnown)
                    *endpointKnown = true;
                // Both pins must match: preserving an SPKI across renewal does not silently approve a new certificate.
                if (memcmp(Records[i].SPKIFingerprint, spkiFingerprint, FTP_CERTIFICATE_FINGERPRINT_SIZE) == 0 &&
                    memcmp(Records[i].CertificateFingerprint, certificateFingerprint,
                           FTP_CERTIFICATE_FINGERPRINT_SIZE) == 0)
                {
                    accepted = true;
                    break;
                }
            }
            --i;
        }
        HANDLES(LeaveCriticalSection(&CriticalSection));
        return accepted;
    }

    void Load(HKEY regKey, CSalamanderRegistryAbstract* registry)
    {
        ClearPersistent();
        HKEY exceptionsKey;
        if (!registry->OpenKey(regKey, FTP_CERTIFICATE_EXCEPTIONS_KEY, exceptionsKey))
            return;

        for (int i = 1; i <= FTP_CERTIFICATE_EXCEPTION_LIMIT; ++i)
        {
            char keyName[12];
            HKEY entryKey;
            _itoa(i, keyName, 10);
            if (!registry->OpenKey(exceptionsKey, keyName, entryKey))
                continue;

            CCertificateExceptionRecord record = {};
            DWORD port = 0;
            bool valid = registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_HOST, REG_SZ, record.Host,
                                            SizeOf(record.Host)) &&
                         registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_PORT, REG_DWORD, &port, sizeof(port)) &&
                         registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_SPKI, REG_BINARY, record.SPKIFingerprint,
                                            sizeof(record.SPKIFingerprint)) &&
                         registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_CERTIFICATE, REG_BINARY,
                                            record.CertificateFingerprint, sizeof(record.CertificateFingerprint)) &&
                         registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_SCOPE, REG_DWORD, &record.Scope,
                                            sizeof(record.Scope)) &&
                         registry->GetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_EXPIRY, REG_BINARY, &record.ExpiresAt,
                                            sizeof(record.ExpiresAt));
            registry->CloseKey(entryKey);
            record.Port = (unsigned short)port;
            if (valid && record.Host[0] != 0 && port <= USHRT_MAX && record.Scope == cesPersistent && !IsExpired(record.ExpiresAt))
                RememberLoaded(record);
        }
        registry->CloseKey(exceptionsKey);
    }

    void Save(HKEY regKey, CSalamanderRegistryAbstract* registry)
    {
        HKEY exceptionsKey;
        if (!registry->CreateKey(regKey, FTP_CERTIFICATE_EXCEPTIONS_KEY, exceptionsKey))
            return;

        registry->ClearKey(exceptionsKey);
        HANDLES(EnterCriticalSection(&CriticalSection));
        ClampCount();
        int saved = 0;
        for (int i = 0; i < Count; ++i)
        {
            const CCertificateExceptionRecord& record = Records[i];
            if (record.Scope != cesPersistent || IsExpired(record.ExpiresAt))
                continue;
            char keyName[12];
            HKEY entryKey;
            _itoa(++saved, keyName, 10);
            if (registry->CreateKey(exceptionsKey, keyName, entryKey))
            {
                DWORD port = record.Port;
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_HOST, REG_SZ, record.Host, -1);
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_PORT, REG_DWORD, &port, sizeof(port));
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_SPKI, REG_BINARY, record.SPKIFingerprint,
                                   sizeof(record.SPKIFingerprint));
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_CERTIFICATE, REG_BINARY,
                                   record.CertificateFingerprint, sizeof(record.CertificateFingerprint));
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_SCOPE, REG_DWORD, &record.Scope,
                                   sizeof(record.Scope));
                registry->SetValue(entryKey, FTP_CERTIFICATE_EXCEPTION_EXPIRY, REG_BINARY, &record.ExpiresAt,
                                   sizeof(record.ExpiresAt));
                registry->CloseKey(entryKey);
            }
        }
        HANDLES(LeaveCriticalSection(&CriticalSection));
        registry->CloseKey(exceptionsKey);
    }

private:
    void ClearPersistent()
    {
        // A configuration reload must not retain a remembered exception deleted from the profile.
        HANDLES(EnterCriticalSection(&CriticalSection));
        ClampCount();
        for (int i = Count - 1; i >= 0 && i < Count;)
        {
            if (Records[i].Scope == cesPersistent)
            {
                RemoveAt(i);
                continue;
            }
            --i;
        }
        HANDLES(LeaveCriticalSection(&CriticalSection));
    }

    void RememberLoaded(const CCertificateExceptionRecord& record)
    {
        HANDLES(EnterCriticalSection(&CriticalSection));
        ClampCount();
        if (Count >= 0 && Count < FTP_CERTIFICATE_EXCEPTION_LIMIT)
            Records[Count++] = record;
        HANDLES(LeaveCriticalSection(&CriticalSection));
    }

    void ClampCount()
    {
        // Records[-1] occupies the bytes immediately before this store, including Config.ConParamsCS.
        if (Count < 0)
            Count = 0;
        if (Count > FTP_CERTIFICATE_EXCEPTION_LIMIT)
            Count = FTP_CERTIFICATE_EXCEPTION_LIMIT;
    }

    void RemoveAt(int i)
    {
        // Reject indexes outside the live prefix so Count never decrements through zero.
        if (i < 0 || i >= Count)
            return;
        Records[i] = Records[--Count];
    }

    CRITICAL_SECTION CriticalSection;
    CCertificateExceptionRecord Records[FTP_CERTIFICATE_EXCEPTION_LIMIT];
    int Count;
};

static CCertificateExceptionStore CertificateExceptions;

void LoadCertificateExceptions(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CertificateExceptions.Load(regKey, registry);
}

void SaveCertificateExceptions(HKEY regKey, CSalamanderRegistryAbstract* registry)
{
    CertificateExceptions.Save(regKey, registry);
}

#ifndef SP_PROT_TLS1_2_CLIENT
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#endif
#ifndef SP_PROT_TLS1_3_CLIENT
#define SP_PROT_TLS1_3_CLIENT 0x00002000
#endif

struct CSchannelConnection
{
    CtxtHandle Context;
    bool ContextValid;
    SOCKET Socket;
    SecPkgContext_StreamSizes StreamSizes;
    BYTE* Encrypted;
    DWORD EncryptedLength;
    DWORD EncryptedCapacity;
    BYTE* Decrypted;
    DWORD DecryptedLength;
    BYTE* PendingWrite;
    DWORD PendingWriteLength;
    DWORD PendingWriteOffset;
    int PendingPlaintextLength;
    int LastError;

    // Handshake output survives WSAEWOULDBLOCK so the next FD_WRITE can resume without blocking this socket thread.
    BYTE* HandshakeWrite;
    DWORD HandshakeWriteLength;
    DWORD HandshakeWriteOffset;
    SECURITY_STATUS HandshakeStatus;
    bool HandshakeStarted;
};

static CredHandle SChannelCredentials;
static bool bSSLInited = false;

static bool EnsureBuffer(BYTE*& buffer, DWORD& capacity, DWORD required)
{
    if (required <= capacity)
        return true;
    DWORD newCapacity = capacity == 0 ? 32768 : capacity;
    while (newCapacity < required)
    {
        if (newCapacity > 1024 * 1024)
            return false;
        newCapacity *= 2;
    }
    BYTE* newBuffer = (BYTE*)realloc(buffer, newCapacity);
    if (newBuffer == NULL)
        return false;
    buffer = newBuffer;
    capacity = newCapacity;
    return true;
}

static void AddNewLine(char*& buf, int& maxlen)
{
    if (maxlen > 0 && buf[0])
    {
        int len = (int)_tcslen(buf);
        maxlen -= len;
        buf += len;
        if (maxlen && (buf[-1] != '\n') && (buf[-1] != '\r'))
        {
            _tcscpy(buf, _T("\n"));
            maxlen--;
            buf++;
        }
    }
}

static bool CheckCertificate(BYTE* pCert, int certLen, LPTSTR buf, int maxlen, const char* host,
                             LPCWSTR hostW)
{
    CERT_CHAIN_PARA chainPara;
    PCCERT_CHAIN_CONTEXT chainContext = NULL;
    CERT_CHAIN_POLICY_PARA policyPara;
    CERT_CHAIN_POLICY_STATUS policyStatus;
    HTTPSPolicyCallbackData httpsPolicy;
    PCCERT_CONTEXT certContext = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, pCert, certLen);
    WCHAR peerName[256] = {};

    if (maxlen > 0)
        buf[0] = 0;
    if (!certContext)
    {
        StringCchCopyNA(buf, maxlen, SalamanderGeneral->GetErrorText(GetLastError()), maxlen); // counted bounded copy instead of lstrcpyn
        return false;
    }

    bool checkRevocation = true;
CHECK_CERT_AGAIN:
    memset(&chainPara, 0, sizeof(chainPara));
    chainPara.cbSize = sizeof(chainPara);
    if (!CertGetCertificateChain(NULL, certContext, NULL, NULL, &chainPara,
                                 (checkRevocation ? CERT_CHAIN_REVOCATION_CHECK_CHAIN : 0) |
                                     CERT_CHAIN_CACHE_END_CERT |
                                     CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE,
                                 NULL, &chainContext))
    {
        StringCchCopyNA(buf, maxlen, SalamanderGeneral->GetErrorText(GetLastError()), maxlen); // counted bounded copy instead of lstrcpyn
        CertFreeCertificateContext(certContext);
        return false;
    }

    memset(&httpsPolicy, 0, sizeof(httpsPolicy));
    httpsPolicy.cbStruct = sizeof(httpsPolicy);
    httpsPolicy.dwAuthType = AUTHTYPE_SERVER;
    if (host != NULL)
        MultiByteToWideChar(CP_ACP, 0, host, -1, peerName, SizeOf(peerName));
    else if (hostW != NULL)
        lstrcpynW(peerName, hostW, SizeOf(peerName));
    httpsPolicy.pwszServerName = peerName;

    memset(&policyPara, 0, sizeof(policyPara));
    policyPara.cbSize = sizeof(policyPara);
    policyPara.pvExtraPolicyPara = &httpsPolicy;
    memset(&policyStatus, 0, sizeof(policyStatus));
    policyStatus.cbSize = sizeof(policyStatus);

    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chainContext, &policyPara, &policyStatus))
    {
        StringCchCopyNA(buf, maxlen, SalamanderGeneral->GetErrorText(GetLastError()), maxlen); // counted bounded copy instead of lstrcpyn
        CertFreeCertificateChain(chainContext);
        CertFreeCertificateContext(certContext);
        return false;
    }

    bool ok = true;
    if (policyStatus.dwError)
    {
        if (checkRevocation && policyStatus.dwError == CRYPT_E_NO_REVOCATION_CHECK)
        {
            CertFreeCertificateChain(chainContext);
            chainContext = NULL;
            checkRevocation = false;
            goto CHECK_CERT_AGAIN;
        }
        ok = false;
        StringCchCopyNA(buf, maxlen, SalamanderGeneral->GetErrorText(policyStatus.dwError), maxlen); // counted bounded copy instead of lstrcpyn
    }
    int timeResult = CertVerifyTimeValidity(NULL, certContext->pCertInfo);
    if (timeResult != 0)
    {
        ok = false;
        AddNewLine(buf, maxlen);
        StringCchCopyNA(buf, maxlen, LoadStr(timeResult < 0 ? IDS_SSL_ERR_NOTYETVALID : IDS_SSL_ERR_EXPIRED), maxlen); // counted bounded copy instead of lstrcpyn
    }
    CertFreeCertificateChain(chainContext);
    CertFreeCertificateContext(certContext);
    return ok;
}

static bool ViewCertificate(HWND hParent, BYTE* certData, int certDataLen, BYTE* pkcs7Data, int pkcs7DataLen, LPCWSTR title)
{
    CRYPTUI_VIEWCERTIFICATE_STRUCTW view = {};
    PCCERT_CONTEXT cert = CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, certData, certDataLen);
    if (!cert)
    {
        SalamanderGeneral->SalMessageBox(hParent, SalamanderGeneral->GetErrorText(GetLastError()), LoadStr(IDS_FTPPLUGINTITLE), MB_OK | MB_ICONSTOP);
        return false;
    }
    HCERTSTORE store = NULL;
    if (pkcs7Data != NULL && pkcs7DataLen > 0)
    {
        CRYPT_INTEGER_BLOB blob = {(DWORD)pkcs7DataLen, pkcs7Data};
        store = CertOpenStore(CERT_STORE_PROV_PKCS7, PKCS_7_ASN_ENCODING, NULL, 0, &blob);
    }
    view.dwSize = sizeof(view);
    view.hwndParent = hParent;
    view.dwFlags = CRYPTUI_DISABLE_EDITPROPERTIES;
    view.szTitle = title;
    view.pCertContext = cert;
    if (store)
    {
        view.cStores = 1;
        view.rghStores = &store;
    }
    if (!CryptUIDlgViewCertificateW(&view, NULL) && GetLastError() != ERROR_CANCELLED)
        SalamanderGeneral->SalMessageBox(hParent, SalamanderGeneral->GetErrorText(GetLastError()), LoadStr(IDS_FTPPLUGINTITLE), MB_OK | MB_ICONSTOP);
    if (store)
        CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
    CertFreeCertificateContext(cert);
    return true;
}

static void SecurityStatusText(SECURITY_STATUS status, char* buffer, int bufferSize)
{
    if (bufferSize <= 0)
        return;
    DWORD chars = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, status, 0, buffer, bufferSize, NULL);
    if (chars == 0)
        _snprintf_s(buffer, bufferSize, _TRUNCATE, "SChannel status 0x%08lX", (DWORD)status);
}

static bool SendBlocking(SOCKET socket, const BYTE* buffer, DWORD length, DWORD* error)
{
    DWORD sentTotal = 0;
    while (sentTotal < length)
    {
        int sent = send(socket, (const char*)buffer + sentTotal, (int)(length - sentTotal), 0);
        if (sent == SOCKET_ERROR)
        {
            if (error)
                *error = WSAGetLastError();
            return false;
        }
        if (sent == 0)
        {
            if (error)
                *error = WSAECONNRESET;
            return false;
        }
        sentTotal += sent;
    }
    return true;
}

static bool SendSecurityToken(SOCKET socket, SecBuffer* token, DWORD* error)
{
    bool ok = true;
    if (token->pvBuffer != NULL && token->cbBuffer != 0)
        ok = SendBlocking(socket, (const BYTE*)token->pvBuffer, token->cbBuffer, error);
    if (token->pvBuffer != NULL)
        FreeContextBuffer(token->pvBuffer);
    token->pvBuffer = NULL;
    token->cbBuffer = 0;
    return ok;
}

static bool CompleteHandshake(CSchannelConnection* connection, LPCSTR target, DWORD* socketError, SECURITY_STATUS* securityError)
{
    DWORD attributes = 0;
    TimeStamp expiry;
    SecBuffer output = {0, SECBUFFER_TOKEN, NULL};
    SecBufferDesc outputDesc = {SECBUFFER_VERSION, 1, &output};
    DWORD requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                         ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION;
    SECURITY_STATUS status = InitializeSecurityContextA(&SChannelCredentials, NULL, (SEC_CHAR*)target,
                                                        requestFlags, 0, SECURITY_NATIVE_DREP, NULL, 0,
                                                        &connection->Context, &outputDesc, &attributes, &expiry);
    connection->ContextValid = status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK;
    if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)
    {
        *securityError = status;
        if (output.pvBuffer)
            FreeContextBuffer(output.pvBuffer);
        return false;
    }
    if (!SendSecurityToken(connection->Socket, &output, socketError))
        return false;

    while (status == SEC_I_CONTINUE_NEEDED || status == SEC_E_INCOMPLETE_MESSAGE)
    {
        if (status == SEC_E_INCOMPLETE_MESSAGE || connection->EncryptedLength == 0)
        {
            if (!EnsureBuffer(connection->Encrypted, connection->EncryptedCapacity, connection->EncryptedLength + 16384))
            {
                *securityError = SEC_E_INSUFFICIENT_MEMORY;
                return false;
            }
            int received = recv(connection->Socket, (char*)connection->Encrypted + connection->EncryptedLength,
                                (int)(connection->EncryptedCapacity - connection->EncryptedLength), 0);
            if (received == SOCKET_ERROR || received == 0)
            {
                *socketError = received == 0 ? WSAECONNRESET : WSAGetLastError();
                return false;
            }
            connection->EncryptedLength += received;
        }

        SecBuffer inputBuffers[2] = {
            {connection->EncryptedLength, SECBUFFER_TOKEN, connection->Encrypted},
            {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc inputDesc = {SECBUFFER_VERSION, 2, inputBuffers};
        output.cbBuffer = 0;
        output.BufferType = SECBUFFER_TOKEN;
        output.pvBuffer = NULL;
        status = InitializeSecurityContextA(&SChannelCredentials, &connection->Context, (SEC_CHAR*)target,
                                           requestFlags, 0, SECURITY_NATIVE_DREP, &inputDesc, 0,
                                           NULL, &outputDesc, &attributes, &expiry);
        if (status == SEC_E_INCOMPLETE_MESSAGE)
            continue;
        if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)
        {
            *securityError = status;
            if (output.pvBuffer)
                FreeContextBuffer(output.pvBuffer);
            return false;
        }
        if (!SendSecurityToken(connection->Socket, &output, socketError))
            return false;
        if (inputBuffers[1].BufferType == SECBUFFER_EXTRA)
        {
            memmove(connection->Encrypted, inputBuffers[1].pvBuffer, inputBuffers[1].cbBuffer);
            connection->EncryptedLength = inputBuffers[1].cbBuffer;
        }
        else
            connection->EncryptedLength = 0;
    }
    if (QueryContextAttributes(&connection->Context, SECPKG_ATTR_STREAM_SIZES, &connection->StreamSizes) != SEC_E_OK)
    {
        *securityError = SEC_E_INTERNAL_ERROR;
        return false;
    }
    return true;
}

static bool QueueHandshakeToken(CSchannelConnection* connection, SecBuffer* token, SECURITY_STATUS* securityError)
{
    bool queued = true;
    if (token->pvBuffer != NULL && token->cbBuffer != 0)
    {
        // A new SChannel flight is generated only after the previous flight has drained.
        if (connection->HandshakeWriteOffset < connection->HandshakeWriteLength)
        {
            *securityError = SEC_E_INTERNAL_ERROR;
            queued = false;
        }
        else
        {
            BYTE* output = (BYTE*)realloc(connection->HandshakeWrite, token->cbBuffer);
            if (output == NULL)
            {
                *securityError = SEC_E_INSUFFICIENT_MEMORY;
                queued = false;
            }
            else
            {
                connection->HandshakeWrite = output;
                memcpy(connection->HandshakeWrite, token->pvBuffer, token->cbBuffer);
                connection->HandshakeWriteLength = token->cbBuffer;
                connection->HandshakeWriteOffset = 0;
            }
        }
    }
    if (token->pvBuffer != NULL)
        FreeContextBuffer(token->pvBuffer);
    token->pvBuffer = NULL;
    token->cbBuffer = 0;
    return queued;
}

static CTlsHandshakeResult FlushHandshakeToken(CSchannelConnection* connection, DWORD* socketError)
{
    while (connection->HandshakeWriteOffset < connection->HandshakeWriteLength)
    {
        int sent = send(connection->Socket,
                        (const char*)connection->HandshakeWrite + connection->HandshakeWriteOffset,
                        (int)(connection->HandshakeWriteLength - connection->HandshakeWriteOffset), 0);
        if (sent == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK)
                return thrPending;
            *socketError = error;
            return thrFailed;
        }
        if (sent == 0)
        {
            *socketError = WSAECONNRESET;
            return thrFailed;
        }
        connection->HandshakeWriteOffset += sent;
    }
    connection->HandshakeWriteLength = 0;
    connection->HandshakeWriteOffset = 0;
    return thrCompleted;
}

static CTlsHandshakeResult ReceiveHandshakeToken(CSchannelConnection* connection, DWORD* socketError,
                                                  SECURITY_STATUS* securityError)
{
    if (!EnsureBuffer(connection->Encrypted, connection->EncryptedCapacity,
                      connection->EncryptedLength + 16384))
    {
        *securityError = SEC_E_INSUFFICIENT_MEMORY;
        return thrFailed;
    }
    int received = recv(connection->Socket, (char*)connection->Encrypted + connection->EncryptedLength,
                        (int)(connection->EncryptedCapacity - connection->EncryptedLength), 0);
    if (received == SOCKET_ERROR)
    {
        DWORD error = WSAGetLastError();
        if (error == WSAEWOULDBLOCK)
            return thrPending;
        *socketError = error;
        return thrFailed;
    }
    if (received == 0)
    {
        *socketError = WSAECONNRESET;
        return thrFailed;
    }
    connection->EncryptedLength += received;
    return thrCompleted;
}

static CTlsHandshakeResult AdvanceHandshake(CSchannelConnection* connection, LPCSTR target, bool start,
                                             DWORD* socketError, SECURITY_STATUS* securityError)
{
    const DWORD requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                               ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                               ISC_REQ_MANUAL_CRED_VALIDATION;

    if (start)
    {
        DWORD attributes = 0;
        TimeStamp expiry;
        SecBuffer output = {0, SECBUFFER_TOKEN, NULL};
        SecBufferDesc outputDesc = {SECBUFFER_VERSION, 1, &output};
        SECURITY_STATUS status = InitializeSecurityContextA(&SChannelCredentials, NULL, (SEC_CHAR*)target,
                                                            requestFlags, 0, SECURITY_NATIVE_DREP, NULL, 0,
                                                            &connection->Context, &outputDesc, &attributes, &expiry);
        connection->ContextValid = status == SEC_I_CONTINUE_NEEDED || status == SEC_E_OK;
        connection->HandshakeStarted = connection->ContextValid;
        connection->HandshakeStatus = status;
        if (!connection->ContextValid)
        {
            *securityError = status;
            if (output.pvBuffer != NULL)
                FreeContextBuffer(output.pvBuffer);
            return thrFailed;
        }
        if (!QueueHandshakeToken(connection, &output, securityError))
            return thrFailed;
    }
    else if (!connection->HandshakeStarted)
    {
        *securityError = SEC_E_INTERNAL_ERROR;
        return thrFailed;
    }

    for (;;)
    {
        CTlsHandshakeResult flushResult = FlushHandshakeToken(connection, socketError);
        if (flushResult != thrCompleted)
            return flushResult;

        if (connection->HandshakeStatus == SEC_E_OK)
        {
            if (QueryContextAttributes(&connection->Context, SECPKG_ATTR_STREAM_SIZES,
                                       &connection->StreamSizes) != SEC_E_OK)
            {
                *securityError = SEC_E_INTERNAL_ERROR;
                return thrFailed;
            }
            connection->LastError = SSL_ERROR_NONE;
            return thrCompleted;
        }
        if (connection->HandshakeStatus != SEC_I_CONTINUE_NEEDED &&
            connection->HandshakeStatus != SEC_E_INCOMPLETE_MESSAGE)
        {
            *securityError = connection->HandshakeStatus;
            return thrFailed;
        }

        if (connection->HandshakeStatus == SEC_E_INCOMPLETE_MESSAGE || connection->EncryptedLength == 0)
        {
            CTlsHandshakeResult receiveResult = ReceiveHandshakeToken(connection, socketError, securityError);
            if (receiveResult != thrCompleted)
                return receiveResult;
        }

        SecBuffer inputBuffers[2] = {
            {connection->EncryptedLength, SECBUFFER_TOKEN, connection->Encrypted},
            {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc inputDesc = {SECBUFFER_VERSION, 2, inputBuffers};
        SecBuffer output = {0, SECBUFFER_TOKEN, NULL};
        SecBufferDesc outputDesc = {SECBUFFER_VERSION, 1, &output};
        DWORD attributes = 0;
        TimeStamp expiry;
        SECURITY_STATUS status = InitializeSecurityContextA(&SChannelCredentials, &connection->Context,
                                                            (SEC_CHAR*)target, requestFlags, 0,
                                                            SECURITY_NATIVE_DREP, &inputDesc, 0, NULL,
                                                            &outputDesc, &attributes, &expiry);
        connection->HandshakeStatus = status;
        if (status == SEC_E_INCOMPLETE_MESSAGE)
        {
            if (output.pvBuffer != NULL)
                FreeContextBuffer(output.pvBuffer);
            continue;
        }
        if (status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK)
        {
            *securityError = status;
            if (output.pvBuffer != NULL)
                FreeContextBuffer(output.pvBuffer);
            return thrFailed;
        }

        // Preserve bytes beyond the final handshake record; they may already contain the file payload.
        if (inputBuffers[1].BufferType == SECBUFFER_EXTRA)
        {
            memmove(connection->Encrypted, inputBuffers[1].pvBuffer, inputBuffers[1].cbBuffer);
            connection->EncryptedLength = inputBuffers[1].cbBuffer;
        }
        else
            connection->EncryptedLength = 0;

        if (!QueueHandshakeToken(connection, &output, securityError))
            return thrFailed;
    }
}

static int FlushPendingWrite(CSchannelConnection* connection)
{
    while (connection->PendingWriteOffset < connection->PendingWriteLength)
    {
        int sent = send(connection->Socket, (const char*)connection->PendingWrite + connection->PendingWriteOffset,
                        (int)(connection->PendingWriteLength - connection->PendingWriteOffset), 0);
        if (sent == SOCKET_ERROR)
        {
            DWORD error = WSAGetLastError();
            connection->LastError = error == WSAEWOULDBLOCK ? SSL_ERROR_WANT_WRITE : SSL_ERROR_SYSCALL;
            return -1;
        }
        if (sent == 0)
        {
            connection->LastError = SSL_ERROR_ZERO_RETURN;
            return -1;
        }
        connection->PendingWriteOffset += sent;
    }
    return 0;
}

int SSLWrite(SSL* ssl, const void* data, int size)
{
    if (ssl == NULL || size <= 0)
        return size;
    if (ssl->PendingWriteLength != 0)
    {
        if (FlushPendingWrite(ssl) != 0)
            return -1;
        int completed = ssl->PendingPlaintextLength;
        ssl->PendingWriteLength = ssl->PendingWriteOffset = 0;
        ssl->PendingPlaintextLength = 0;
        ssl->LastError = SSL_ERROR_NONE;
        return completed;
    }
    int plainLength = min(size, (int)ssl->StreamSizes.cbMaximumMessage);
    DWORD total = ssl->StreamSizes.cbHeader + plainLength + ssl->StreamSizes.cbTrailer;
    BYTE* pendingWrite = (BYTE*)realloc(ssl->PendingWrite, total);
    if (pendingWrite == NULL)
    {
        ssl->LastError = SSL_ERROR_SSL;
        return -1;
    }
    ssl->PendingWrite = pendingWrite;
    memcpy(ssl->PendingWrite + ssl->StreamSizes.cbHeader, data, plainLength);
    SecBuffer buffers[4] = {
        {ssl->StreamSizes.cbHeader, SECBUFFER_STREAM_HEADER, ssl->PendingWrite},
        {(DWORD)plainLength, SECBUFFER_DATA, ssl->PendingWrite + ssl->StreamSizes.cbHeader},
        {ssl->StreamSizes.cbTrailer, SECBUFFER_STREAM_TRAILER, ssl->PendingWrite + ssl->StreamSizes.cbHeader + plainLength},
        {0, SECBUFFER_EMPTY, NULL}};
    SecBufferDesc descriptor = {SECBUFFER_VERSION, 4, buffers};
    SECURITY_STATUS status = EncryptMessage(&ssl->Context, 0, &descriptor, 0);
    if (status != SEC_E_OK)
    {
        ssl->LastError = SSL_ERROR_SSL;
        return -1;
    }
    ssl->PendingWriteLength = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    ssl->PendingWriteOffset = 0;
    ssl->PendingPlaintextLength = plainLength;
    if (FlushPendingWrite(ssl) != 0)
        return -1;
    ssl->PendingWriteLength = ssl->PendingWriteOffset = 0;
    ssl->PendingPlaintextLength = 0;
    ssl->LastError = SSL_ERROR_NONE;
    return plainLength;
}

static int CopyDecrypted(CSchannelConnection* ssl, void* data, int size)
{
    int count = min(size, (int)ssl->DecryptedLength);
    if (count > 0)
    {
        memcpy(data, ssl->Decrypted, count);
        ssl->DecryptedLength -= count;
        if (ssl->DecryptedLength)
            memmove(ssl->Decrypted, ssl->Decrypted + count, ssl->DecryptedLength);
    }
    return count;
}

int SSLRead(SSL* ssl, void* data, int size)
{
    if (ssl == NULL || size <= 0)
        return size;
    if (ssl->DecryptedLength)
        return CopyDecrypted(ssl, data, size);
    for (;;)
    {
        if (ssl->EncryptedLength == 0)
        {
            if (!EnsureBuffer(ssl->Encrypted, ssl->EncryptedCapacity, 16384))
            {
                ssl->LastError = SSL_ERROR_SSL;
                return -1;
            }
            int received = recv(ssl->Socket, (char*)ssl->Encrypted, (int)ssl->EncryptedCapacity, 0);
            if (received == SOCKET_ERROR)
            {
                DWORD error = WSAGetLastError();
                ssl->LastError = error == WSAEWOULDBLOCK ? SSL_ERROR_WANT_READ : SSL_ERROR_SYSCALL;
                return -1;
            }
            if (received == 0)
            {
                ssl->LastError = SSL_ERROR_ZERO_RETURN;
                return 0;
            }
            ssl->EncryptedLength = received;
        }
        SecBuffer buffers[4] = {
            {ssl->EncryptedLength, SECBUFFER_DATA, ssl->Encrypted},
            {0, SECBUFFER_EMPTY, NULL}, {0, SECBUFFER_EMPTY, NULL}, {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc descriptor = {SECBUFFER_VERSION, 4, buffers};
        SECURITY_STATUS status = DecryptMessage(&ssl->Context, &descriptor, 0, NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE)
        {
            if (!EnsureBuffer(ssl->Encrypted, ssl->EncryptedCapacity, ssl->EncryptedLength + 16384))
            {
                ssl->LastError = SSL_ERROR_SSL;
                return -1;
            }
            int received = recv(ssl->Socket, (char*)ssl->Encrypted + ssl->EncryptedLength,
                                (int)(ssl->EncryptedCapacity - ssl->EncryptedLength), 0);
            if (received == SOCKET_ERROR)
            {
                DWORD error = WSAGetLastError();
                ssl->LastError = error == WSAEWOULDBLOCK ? SSL_ERROR_WANT_READ : SSL_ERROR_SYSCALL;
                return -1;
            }
            if (received == 0)
            {
                ssl->LastError = SSL_ERROR_ZERO_RETURN;
                return 0;
            }
            ssl->EncryptedLength += received;
            continue;
        }
        if (status == SEC_I_CONTEXT_EXPIRED)
        {
            ssl->LastError = SSL_ERROR_ZERO_RETURN;
            return 0;
        }
        if (status != SEC_E_OK)
        {
            ssl->LastError = SSL_ERROR_SSL;
            return -1;
        }
        SecBuffer* dataBuffer = NULL;
        SecBuffer* extraBuffer = NULL;
        for (int i = 0; i < 4; i++)
        {
            if (buffers[i].BufferType == SECBUFFER_DATA)
                dataBuffer = &buffers[i];
            else if (buffers[i].BufferType == SECBUFFER_EXTRA)
                extraBuffer = &buffers[i];
        }
        if (dataBuffer && dataBuffer->cbBuffer)
        {
            BYTE* decrypted = (BYTE*)realloc(ssl->Decrypted, dataBuffer->cbBuffer);
            if (decrypted == NULL)
            {
                ssl->LastError = SSL_ERROR_SSL;
                return -1;
            }
            ssl->Decrypted = decrypted;
            // DecryptMessage points into Encrypted. Copy before moving the
            // SECBUFFER_EXTRA tail to the beginning of that input buffer.
            memcpy(ssl->Decrypted, dataBuffer->pvBuffer, dataBuffer->cbBuffer);
            ssl->DecryptedLength = dataBuffer->cbBuffer;
        }
        if (extraBuffer)
        {
            memmove(ssl->Encrypted, extraBuffer->pvBuffer, extraBuffer->cbBuffer);
            ssl->EncryptedLength = extraBuffer->cbBuffer;
        }
        else
            ssl->EncryptedLength = 0;
        if (dataBuffer && dataBuffer->cbBuffer)
        {
            ssl->LastError = SSL_ERROR_NONE;
            return CopyDecrypted(ssl, data, size);
        }
    }
}

int SSLPending(SSL* ssl)
{
    // SChannel may retain additional encrypted records from the same recv().
    // Callers use this as a readiness hint after a successful read, not a byte count.
    return ssl ? (ssl->DecryptedLength != 0 || ssl->EncryptedLength != 0) : 0;
}

int SSLGetError(SSL* ssl, int result)
{
    if (result > 0)
        return SSL_ERROR_NONE;
    return ssl ? ssl->LastError : SSL_ERROR_SSL;
}

int SSLShutdown(SSL* ssl)
{
    if (ssl == NULL || !ssl->ContextValid)
        return 1;
    DWORD shutdown = SCHANNEL_SHUTDOWN;
    SecBuffer input = {sizeof(shutdown), SECBUFFER_TOKEN, &shutdown};
    SecBufferDesc inputDesc = {SECBUFFER_VERSION, 1, &input};
    ApplyControlToken(&ssl->Context, &inputDesc);
    SecBuffer output = {0, SECBUFFER_TOKEN, NULL};
    SecBufferDesc outputDesc = {SECBUFFER_VERSION, 1, &output};
    DWORD attributes = 0;
    TimeStamp expiry;
    SECURITY_STATUS status = InitializeSecurityContext(NULL, &ssl->Context, NULL, 0, 0, SECURITY_NATIVE_DREP,
                                                        NULL, 0, NULL, &outputDesc, &attributes, &expiry);
    DWORD ignoredError = NO_ERROR;
    if (status == SEC_E_OK)
        SendSecurityToken(ssl->Socket, &output, &ignoredError);
    else if (output.pvBuffer)
        FreeContextBuffer(output.pvBuffer);
    return status == SEC_E_OK ? 1 : 0;
}

void SSLFree(SSL* ssl)
{
    if (ssl == NULL)
        return;
    if (ssl->ContextValid)
        DeleteSecurityContext(&ssl->Context);
    free(ssl->Encrypted);
    free(ssl->Decrypted);
    free(ssl->PendingWrite);
    free(ssl->HandshakeWrite);
    free(ssl);
}

int SSLtoWS2Error(int err)
{
    switch (err)
    {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        return WSAEWOULDBLOCK;
    case SSL_ERROR_ZERO_RETURN:
        return WSAECONNRESET;
    default:
        return err;
    }
}

bool InitSSL(int logUID, int* errorID)
{
    if (errorID)
        *errorID = -1;
    if (bSSLInited)
        return true;
    SCHANNEL_CRED credentials = {};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    credentials.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry;
    SECURITY_STATUS status = AcquireCredentialsHandleA(NULL, (LPSTR)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL,
                                                       &credentials, NULL, NULL, &SChannelCredentials, &expiry);
    if (status == SEC_E_OK)
    {
        bSSLInited = true;
        Logs.LogMessage(logUID, "TLS INFO: Using Windows SChannel (TLS 1.2/1.3 only).\r\n", -1);
        return true;
    }
    char message[256];
    SecurityStatusText(status, message, SizeOf(message));
    Logs.LogMessage(logUID, "TLS ERROR: ", -1);
    Logs.LogMessage(logUID, message, -1);
    Logs.LogMessage(logUID, "\r\n", -1);
    if (errorID)
        *errorID = IDS_SSL_ERR_OPENSSLNOTFOUND;
    return false;
}

void FreeSSL(int)
{
    if (bSSLInited)
        FreeCredentialsHandle(&SChannelCredentials);
    bSSLInited = false;
}

void SSLThreadLocalCleanup()
{
    // SChannel does not allocate per-thread state that callers must release.
}

static void ReportHandshakeFailure(int logUID, DWORD socketError, SECURITY_STATUS securityError,
                                   int* errorID, char* errorBuf, int errorBufLen)
{
    char message[256];
    if (socketError == WSAETIMEDOUT)
        StringCchCopyNA(message, SizeOf(message), "TLS handshake deadline expired.", SizeOf(message));
    else if (socketError != NO_ERROR)
        StringCchCopyNA(message, SizeOf(message), SalamanderGeneral->GetErrorText(socketError), SizeOf(message));
    else
        SecurityStatusText(securityError, message, SizeOf(message));
    if (errorID)
        *errorID = IDS_SSL_ERR_CONNECT;
    if (errorBufLen > 0)
        _snprintf_s(errorBuf, errorBufLen, _TRUNCATE, LoadStr(IDS_SSL_ERR_CONNECT_ERR),
                    (int)(socketError ? socketError : securityError), message);
    Logs.LogMessage(logUID, "TLS ERROR: ", -1, TRUE);
    Logs.LogMessage(logUID, message, -1, TRUE);
    Logs.LogMessage(logUID, "\r\n", -1, TRUE);
}

BOOL CSocket::FinishEncryptSocket(SSL* connection, int logUID, int* sslErrorOccured,
                                  CCertificate** unverifiedCert, int* errorID, char* errorBuf,
                                  int errorBufLen, CSocket* conForReuse)
{
    PCCERT_CONTEXT peerCert = NULL;
    if (QueryContextAttributes(&connection->Context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &peerCert) != SEC_E_OK || peerCert == NULL)
    {
        SSLFree(connection);
        if (errorID)
            *errorID = IDS_SSL_ERR_CONNECT;
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return FALSE;
    }
    BYTE* der = (BYTE*)malloc(peerCert->cbCertEncoded);
    if (!der)
    {
        CertFreeCertificateContext(peerCert);
        SSLFree(connection);
        if (errorID)
            *errorID = IDS_SSL_ERR_NEW;
        return FALSE;
    }
    memcpy(der, peerCert->pbCertEncoded, peerCert->cbCertEncoded);
    int derLength = peerCert->cbCertEncoded;
    CertFreeCertificateContext(peerCert);

    SecPkgContext_ConnectionInfo info = {};
    if (QueryContextAttributes(&connection->Context, SECPKG_ATTR_CONNECTION_INFO, &info) == SEC_E_OK)
    {
        char details[160];
        _snprintf_s(details, SizeOf(details), _TRUNCATE, "TLS INFO: protocol 0x%lX, cipher 0x%lX (%lu bits)\r\n",
                    info.dwProtocol, info.aiCipher, info.dwCipherStrength);
        Logs.LogMessage(logUID, details, -1);
    }

    // Whether this handshake actually resumed a cached session is asked of
    // SChannel, not inferred. The ReuseSSLSession value set at the end of this
    // function only records that SChannel owns the session cache; it says
    // nothing about what happened here, so it must never be read as evidence of
    // resumption (ftp-improvements.md section 5.2). A provider that does not
    // answer leaves the result genuinely unknown rather than optimistic.
    SecPkgContext_SessionInfo sessionInfo = {};
    if (QueryContextAttributes(&connection->Context, SECPKG_ATTR_SESSION_INFO, &sessionInfo) == SEC_E_OK)
    {
        BOOL reconnected = (sessionInfo.dwFlags & SSL_SESSION_RECONNECT) != 0;
        Logs.LogMessage(logUID, LoadStr(reconnected ? IDS_SSL_LOG_SESSIONRESUMED : IDS_SSL_LOG_SESSIONFULL), -1, TRUE);
        LastHandshakeResult = reconnected ? fmhResumed : fmhFull;
    }
    else
    {
        Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_SESSIONUNKNOWN), -1, TRUE);
        LastHandshakeResult = fmhUnknown;
    }

    bool accepted = false;
    // Reconnects must recheck exception expiry; data sockets intentionally inherit the already pinned control identity.
    if (pCertificate && pCertificate->IsSame(der, derLength, NULL, 0) &&
        (pCertificate->IsVerified() || pCertificate->IsCurrentException()) &&
        (IsDataConnection || pCertificate->MatchesEndpoint(HostAddress, HostPort)))
    {
        Logs.LogMessage(logUID, LoadStr(pCertificate->IsVerified() ? IDS_SSL_LOG_CERTVERIFIED : IDS_SSL_LOG_CERTACCEPTED), -1, TRUE);
        accepted = true;
    }
    else if (pCertificate)
    {
        Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTCHANGED), -1, TRUE);
        pCertificate->Release();
        pCertificate = NULL;
    }
    if (!accepted && CheckCertificate(der, derLength, errorBuf, errorBufLen, HostAddress, NULL))
    {
        Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTVERIFIED), -1, TRUE);
        pCertificate = new CCertificate(der, derLength, NULL, 0, true, HostAddress, HostPort);
        accepted = true;
    }
    if (!accepted)
    {
        bool endpointKnown = false;
        if (CertificateExceptions.IsAccepted(HostAddress, HostPort, der, derLength, &endpointKnown))
        {
            Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTACCEPTED), -1, TRUE);
            pCertificate = new CCertificate(der, derLength, NULL, 0, false, HostAddress, HostPort);
            accepted = true;
        }
        else if (endpointKnown)
        {
            // A stored exception exists for this endpoint but its pin or lifetime no longer matches; prompt again.
            Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTCHANGED), -1, TRUE);
        }
    }
    if (!accepted)
    {
        Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTNOTVERIFIED), -1, TRUE);
        if (unverifiedCert)
            *unverifiedCert = new CCertificate(der, derLength, NULL, 0, false, HostAddress, HostPort);
        else
        {
            SSLShutdown(connection);
            SSLFree(connection);
            free(der);
            if (sslErrorOccured)
                *sslErrorOccured = SSLCONERR_UNVERIFIEDCERT;
            return FALSE;
        }
    }
    free(der);
    SSLConn = connection;
    if (conForReuse)
    {
        // SChannel maintains its own target-name session cache, so there is no
        // client-side session object to hand over. This records that fact; it is
        // NOT a statement that this handshake resumed anything - the resumption
        // result comes from SECPKG_ATTR_SESSION_INFO above.
        conForReuse->ReuseSSLSession = 2;
    }
    if (sslErrorOccured)
        *sslErrorOccured = SSLCONERR_NOERROR;
    return TRUE;
}

BOOL CSocket::EncryptSocket(int logUID, int* sslErrorOccured, CCertificate** unverifiedCert,
                             int* errorID, char* errorBuf, int errorBufLen, CSocket* conForReuse)
{
    if (errorID)
        *errorID = -1;
    if (errorBufLen > 0)
        errorBuf[0] = 0;
    if (unverifiedCert)
        *unverifiedCert = NULL;
    if (sslErrorOccured)
        *sslErrorOccured = SSLConn ? SSLCONERR_NOERROR : SSLCONERR_DONOTRETRY;
    if (SSLConn)
        return TRUE;
    if (!bSSLInited)
        return FALSE;

    SSL* connection = (SSL*)calloc(1, sizeof(SSL));
    if (!connection)
    {
        if (errorID)
            *errorID = IDS_SSL_ERR_NEW;
        return FALSE;
    }
    connection->Socket = Socket;
    connection->LastError = SSL_ERROR_SSL;

    if (HostAddress == NULL || HostAddress[0] == 0)
    {
        SSLFree(connection);
        if (errorID)
            *errorID = IDS_SSL_ERR_CONNECT;
        return FALSE;
    }

    HWND window = SocketsThread->GetHiddenWindow();
    WSAAsyncSelect(Socket, window, 0, 0);
    u_long blocking = 0;
    ioctlsocket(Socket, FIONBIO, &blocking);
    // SChannel drives this legacy handshake through blocking send/recv calls;
    // bound that narrow phase so a silent TLS peer cannot prevent shutdown.
    DWORD tlsHandshakeTimeout = Config.GetServerRepliesTimeout() * 1000;
    if (tlsHandshakeTimeout < 1000)
        tlsHandshakeTimeout = 1000;
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tlsHandshakeTimeout, sizeof(tlsHandshakeTimeout));
    setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tlsHandshakeTimeout, sizeof(tlsHandshakeTimeout));
    DWORD socketError = NO_ERROR;
    SECURITY_STATUS securityError = SEC_E_OK;
    bool connected = CompleteHandshake(connection, HostAddress, &socketError, &securityError);

    DWORD noSocketDeadline = 0;
    setsockopt(Socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&noSocketDeadline, sizeof(noSocketDeadline));
    setsockopt(Socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&noSocketDeadline, sizeof(noSocketDeadline));
    u_long nonBlocking = 1;
    ioctlsocket(Socket, FIONBIO, &nonBlocking);
    WSAAsyncSelect(Socket, window, Msg, FD_READ | FD_CLOSE | FD_WRITE);
    if (!connected)
    {
        ReportHandshakeFailure(logUID, socketError, securityError, errorID, errorBuf, errorBufLen);
        // A timed-out or failed handshake cannot become usable later; closing it
        // also releases any pending socket operation before retry/error handling.
        CloseSocket(NULL);
        SSLFree(connection);
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return FALSE;
    }
    return FinishEncryptSocket(connection, logUID, sslErrorOccured, unverifiedCert, errorID,
                               errorBuf, errorBufLen, conForReuse);
}

CTlsHandshakeResult CSocket::BeginAsyncEncryptControlSocket(int logUID, int* sslErrorOccured,
                                                            CCertificate** unverifiedCert, int* errorID,
                                                            char* errorBuf, int errorBufLen)
{
    // Same state machine as the data-channel handshake, with the certificate
    // outputs the worker's login flow needs. Migrating the control login off the
    // blocking EncryptSocket() is what lets one worker's stalled TLS handshake
    // stop blocking the other workers' payload progress: releasing
    // WorkerCritSect around a blocking call never made the socket thread
    // asynchronous (ftp-improvements.md section 5.4).
    if (errorID != NULL)
        *errorID = -1;
    if (errorBufLen > 0)
        errorBuf[0] = 0;
    if (unverifiedCert != NULL)
        *unverifiedCert = NULL;
    if (sslErrorOccured != NULL)
        *sslErrorOccured = SSLConn ? SSLCONERR_NOERROR : SSLCONERR_DONOTRETRY;
    if (SSLConn)
        return thrCompleted;
    if (SSLHandshakeConn)
    {
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }
    if (!bSSLInited || HostAddress == NULL || HostAddress[0] == 0)
    {
        if (errorID != NULL)
            *errorID = IDS_SSL_ERR_CONNECT;
        return thrFailed;
    }

    SSL* connection = (SSL*)calloc(1, sizeof(SSL));
    if (connection == NULL)
    {
        if (errorID != NULL)
            *errorID = IDS_SSL_ERR_NEW;
        return thrFailed;
    }
    connection->Socket = Socket;
    connection->LastError = SSL_ERROR_SSL;

    HandshakeStartTime = CMonotonicClock::Now(); // measured duration starts with the first token

    DWORD socketError = NO_ERROR;
    SECURITY_STATUS securityError = SEC_E_OK;
    CTlsHandshakeResult result = AdvanceHandshake(connection, HostAddress, true,
                                                  &socketError, &securityError);
    if (result == thrFailed)
    {
        ReportHandshakeFailure(logUID, socketError, securityError, errorID, errorBuf, errorBufLen);
        SSLFree(connection);
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return thrFailed;
    }
    if (result == thrPending)
    {
        // Ownership stays on the socket until a readiness event completes or
        // cancels the handshake; the caller resumes it from its socket events.
        SSLHandshakeConn = connection;
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }

    // 'conForReuse' is deliberately NULL here: this *is* the control connection,
    // and its data connections reuse it, not the other way round.
    return FinishEncryptSocket(connection, logUID, sslErrorOccured, unverifiedCert, errorID,
                               errorBuf, errorBufLen, NULL)
               ? thrCompleted
               : thrFailed;
}

CTlsHandshakeResult CSocket::ContinueAsyncEncryptControlSocket(int logUID, int* sslErrorOccured,
                                                               CCertificate** unverifiedCert, int* errorID,
                                                               char* errorBuf, int errorBufLen)
{
    if (errorID != NULL)
        *errorID = -1;
    if (errorBufLen > 0)
        errorBuf[0] = 0;
    if (unverifiedCert != NULL)
        *unverifiedCert = NULL;
    if (SSLConn)
    {
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrCompleted;
    }
    if (SSLHandshakeConn == NULL)
    {
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_DONOTRETRY;
        if (errorID != NULL)
            *errorID = IDS_SSL_ERR_CONNECT;
        return thrFailed;
    }

    DWORD socketError = NO_ERROR;
    SECURITY_STATUS securityError = SEC_E_OK;
    CTlsHandshakeResult result = AdvanceHandshake(SSLHandshakeConn, HostAddress, false,
                                                  &socketError, &securityError);
    if (result == thrPending)
    {
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }

    SSL* connection = SSLHandshakeConn;
    SSLHandshakeConn = NULL;
    if (result == thrFailed)
    {
        ReportHandshakeFailure(logUID, socketError, securityError, errorID, errorBuf, errorBufLen);
        SSLFree(connection);
        if (sslErrorOccured != NULL)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return thrFailed;
    }

    return FinishEncryptSocket(connection, logUID, sslErrorOccured, unverifiedCert, errorID,
                               errorBuf, errorBufLen, NULL)
               ? thrCompleted
               : thrFailed;
}

CTlsHandshakeResult CSocket::BeginAsyncEncryptSocket(int logUID, int* sslErrorOccured,
                                                     CSocket* conForReuse)
{
    if (sslErrorOccured)
        *sslErrorOccured = SSLConn ? SSLCONERR_NOERROR : SSLCONERR_DONOTRETRY;
    if (SSLConn)
        return thrCompleted;
    if (SSLHandshakeConn)
    {
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }
    if (!bSSLInited || HostAddress == NULL || HostAddress[0] == 0)
        return thrFailed;

    SSL* connection = (SSL*)calloc(1, sizeof(SSL));
    if (!connection)
        return thrFailed;
    connection->Socket = Socket;
    connection->LastError = SSL_ERROR_SSL;

    DWORD socketError = NO_ERROR;
    SECURITY_STATUS securityError = SEC_E_OK;
    HandshakeStartTime = CMonotonicClock::Now(); // measured duration starts with the first token
    CTlsHandshakeResult result = AdvanceHandshake(connection, HostAddress, true,
                                                  &socketError, &securityError);
    if (result == thrFailed)
    {
        ReportHandshakeFailure(logUID, socketError, securityError, NULL, NULL, 0);
        SSLFree(connection);
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return thrFailed;
    }
    if (result == thrPending)
    {
        // Ownership stays on the socket until a readiness event completes or cancels the handshake.
        SSLHandshakeConn = connection;
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }

    return FinishEncryptSocket(connection, logUID, sslErrorOccured, NULL, NULL, NULL, 0,
                               conForReuse)
               ? thrCompleted
               : thrFailed;
}

CTlsHandshakeResult CSocket::ContinueAsyncEncryptSocket(int logUID, int* sslErrorOccured,
                                                        CSocket* conForReuse)
{
    if (SSLConn)
    {
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrCompleted;
    }
    if (SSLHandshakeConn == NULL)
    {
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_DONOTRETRY;
        return thrFailed;
    }

    DWORD socketError = NO_ERROR;
    SECURITY_STATUS securityError = SEC_E_OK;
    CTlsHandshakeResult result = AdvanceHandshake(SSLHandshakeConn, HostAddress, false,
                                                  &socketError, &securityError);
    if (result == thrPending)
    {
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_NOERROR;
        return thrPending;
    }

    SSL* connection = SSLHandshakeConn;
    SSLHandshakeConn = NULL;
    if (result == thrFailed)
    {
        ReportHandshakeFailure(logUID, socketError, securityError, NULL, NULL, 0);
        SSLFree(connection);
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return thrFailed;
    }

    return FinishEncryptSocket(connection, logUID, sslErrorOccured, NULL, NULL, NULL, 0,
                               conForReuse)
               ? thrCompleted
               : thrFailed;
}

CCertificateErrDialog::CCertificateErrDialog(HWND hParent, const char* errorStr)
    : CCenteredDialog(HLanguage, IDD_CERTIFICATE, hParent), ErrorStr(errorStr), RememberedException(false)
{
}

INT_PTR CCertificateErrDialog::DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetDlgItemText(HWindow, IDT_CERTIFICATE_ERROR, ErrorStr);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
            RememberedException = IsDlgButtonChecked(HWindow, IDB_CERTIFICATE_REMEMBER) == BST_CHECKED;
        if (LOWORD(wParam) == IDB_CERTIFICATE_VIEW && HIWORD(wParam) == BN_CLICKED)
            EndDialog(HWindow, IDB_CERTIFICATE_VIEW);
        break;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

CCertificate::CCertificate(BYTE* derCert, int derCertLen, BYTE* pkcs7Cert, int pkcs7CertLen, bool valid, LPCSTR host,
                           unsigned short port)
    : nRefCount(1), pDERData(NULL), pPKCS7Data(NULL), nDERDataLen(0), nPKCS7DataLen(0), bVerified(valid), Host(NULL),
      HostAddress(NULL), Port(port)
{
    if (derCertLen > 0 && (pDERData = (BYTE*)malloc(derCertLen)) != NULL)
    {
        nDERDataLen = derCertLen;
        memcpy(pDERData, derCert, derCertLen);
    }
    if (pkcs7CertLen > 0 && (pPKCS7Data = (BYTE*)malloc(pkcs7CertLen)) != NULL)
    {
        nPKCS7DataLen = pkcs7CertLen;
        memcpy(pPKCS7Data, pkcs7Cert, pkcs7CertLen);
    }
    if (host)
    {
        HostAddress = _strdup(host);
        int length = MultiByteToWideChar(CP_ACP, 0, host, -1, NULL, 0);
        if (length > 0 && (Host = (LPWSTR)malloc(length * sizeof(WCHAR))) != NULL)
            MultiByteToWideChar(CP_ACP, 0, host, -1, Host, length);
    }
}

CCertificate::~CCertificate()
{
    free(Host);
    free(HostAddress);
    free(pDERData);
    free(pPKCS7Data);
}

LONG CCertificate::AddRef() { return InterlockedIncrement(&nRefCount); }

LONG CCertificate::Release()
{
    LONG result = InterlockedDecrement(&nRefCount);
    if (!result)
        delete this;
    return result;
}

void CCertificate::ShowCertificate(HWND hParent) { ViewCertificate(hParent, pDERData, nDERDataLen, pPKCS7Data, nPKCS7DataLen, Host); }
bool CCertificate::CheckCertificate(LPTSTR buf, int maxlen) { return ::CheckCertificate(pDERData, nDERDataLen, buf, maxlen, NULL, Host); }

bool CCertificate::IsSame(BYTE* derCert, int derCertLen, BYTE* pkcs7Cert, int pkcs7CertLen)
{
    return derCertLen == nDERDataLen && pkcs7CertLen == nPKCS7DataLen &&
           (derCertLen == 0 || memcmp(pDERData, derCert, derCertLen) == 0) &&
           (pkcs7CertLen == 0 || memcmp(pPKCS7Data, pkcs7Cert, pkcs7CertLen) == 0);
}

bool CCertificate::MatchesEndpoint(LPCSTR host, unsigned short port) const
{
    return HostAddress != NULL && host != NULL && _stricmp(HostAddress, host) == 0 && Port == port;
}

bool CCertificate::RememberException(CCertificateExceptionScope scope) const
{
    return CertificateExceptions.Remember(HostAddress, Port, pDERData, nDERDataLen, scope);
}

bool CCertificate::IsCurrentException() const
{
    // Data sockets inherit the control certificate, so validate the stored control endpoint rather than their ephemeral port.
    return CertificateExceptions.IsAccepted(HostAddress, Port, pDERData, nDERDataLen, NULL);
}
