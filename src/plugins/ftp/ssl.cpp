// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#include "precomp.h"

#define MAX_DER_CERT_SIZE 5120
#define SizeOf(x) (sizeof(x) / sizeof(x[0]))

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
        lstrcpyn(buf, SalamanderGeneral->GetErrorText(GetLastError()), maxlen);
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
        lstrcpyn(buf, SalamanderGeneral->GetErrorText(GetLastError()), maxlen);
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
        lstrcpyn(buf, SalamanderGeneral->GetErrorText(GetLastError()), maxlen);
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
        lstrcpyn(buf, SalamanderGeneral->GetErrorText(policyStatus.dwError), maxlen);
    }
    int timeResult = CertVerifyTimeValidity(NULL, certContext->pCertInfo);
    if (timeResult != 0)
    {
        ok = false;
        AddNewLine(buf, maxlen);
        lstrcpyn(buf, LoadStr(timeResult < 0 ? IDS_SSL_ERR_NOTYETVALID : IDS_SSL_ERR_EXPIRED), maxlen);
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
    return ssl ? (int)ssl->DecryptedLength : 0;
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
        char message[256];
        if (socketError == WSAETIMEDOUT)
            lstrcpyn(message, "TLS handshake deadline expired.", SizeOf(message));
        else if (socketError != NO_ERROR)
            lstrcpyn(message, SalamanderGeneral->GetErrorText(socketError), SizeOf(message));
        else
            SecurityStatusText(securityError, message, SizeOf(message));
        if (errorID)
            *errorID = IDS_SSL_ERR_CONNECT;
        if (errorBufLen > 0)
            _snprintf_s(errorBuf, errorBufLen, _TRUNCATE, LoadStr(IDS_SSL_ERR_CONNECT_ERR), (int)(socketError ? socketError : securityError), message);
        Logs.LogMessage(logUID, "TLS ERROR: ", -1, TRUE);
        Logs.LogMessage(logUID, message, -1, TRUE);
        Logs.LogMessage(logUID, "\r\n", -1, TRUE);
        // A timed-out or failed handshake cannot become usable later; closing it
        // also releases any pending socket operation before retry/error handling.
        CloseSocket(NULL);
        SSLFree(connection);
        if (sslErrorOccured)
            *sslErrorOccured = SSLCONERR_CANRETRY;
        return FALSE;
    }

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

    bool accepted = false;
    if (pCertificate && pCertificate->IsSame(der, derLength, NULL, 0))
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
        pCertificate = new CCertificate(der, derLength, NULL, 0, true, HostAddress);
        accepted = true;
    }
    if (!accepted)
    {
        Logs.LogMessage(logUID, LoadStr(IDS_SSL_LOG_CERTNOTVERIFIED), -1, TRUE);
        if (unverifiedCert)
            *unverifiedCert = new CCertificate(der, derLength, NULL, 0, false, HostAddress);
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
        conForReuse->ReuseSSLSession = 2; // SChannel maintains its own target-name session cache.
    if (sslErrorOccured)
        *sslErrorOccured = SSLCONERR_NOERROR;
    return TRUE;
}

CCertificateErrDialog::CCertificateErrDialog(HWND hParent, const char* errorStr)
    : CCenteredDialog(HLanguage, IDD_CERTIFICATE, hParent), ErrorStr(errorStr)
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
        if (LOWORD(wParam) == IDB_CERTIFICATE_VIEW && HIWORD(wParam) == BN_CLICKED)
            EndDialog(HWindow, IDB_CERTIFICATE_VIEW);
        break;
    }
    return CCenteredDialog::DialogProc(uMsg, wParam, lParam);
}

CCertificate::CCertificate(BYTE* derCert, int derCertLen, BYTE* pkcs7Cert, int pkcs7CertLen, bool valid, LPCSTR host)
    : nRefCount(1), pDERData(NULL), pPKCS7Data(NULL), nDERDataLen(0), nPKCS7DataLen(0), bVerified(valid), Host(NULL)
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
        int length = MultiByteToWideChar(CP_ACP, 0, host, -1, NULL, 0);
        if (length > 0 && (Host = (LPWSTR)malloc(length * sizeof(WCHAR))) != NULL)
            MultiByteToWideChar(CP_ACP, 0, host, -1, Host, length);
    }
}

CCertificate::~CCertificate()
{
    free(Host);
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
