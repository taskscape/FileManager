// SPDX-FileCopyrightText: 2023 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later
// CommentsTranslationProject: TRANSLATED

#pragma once

#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <schannel.h>
#include <security.h>
#include <cryptuiapi.h>

#define SSLCONERR_NOERROR 0
#define SSLCONERR_CANRETRY 1
#define SSLCONERR_DONOTRETRY 2
#define SSLCONERR_UNVERIFIEDCERT 3

// These values are private to this adapter; callers translate them with SSLtoWS2Error.
#define SSL_ERROR_NONE 0
#define SSL_ERROR_SSL 1
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_SYSCALL 5
#define SSL_ERROR_ZERO_RETURN 6

class CCertificate
{
public:
    CCertificate(BYTE* pDERCert, int DERCertLen, BYTE* pPKCS7Cert, int PKCS7CertLen, bool bValid, LPCSTR host);
    LONG AddRef();
    LONG Release();
    void ShowCertificate(HWND hParent);
    bool CheckCertificate(LPTSTR buf, int maxlen);

    // The caller must ensure the certificate is not in use on another thread.
    void SetVerified(bool verified) { bVerified = verified; };

    bool IsSame(BYTE* pDERCert, int DERCertLen, BYTE* pPKCS7Cert, int PKCS7CertLen);
    bool IsVerified() { return bVerified; };
    LPCWSTR GetHostName() { return Host; };

private:
    ~CCertificate();

    LONG nRefCount;
    BYTE *pDERData, *pPKCS7Data;
    int nDERDataLen, nPKCS7DataLen;
    bool bVerified;
    LPWSTR Host;
};

class CCertificateErrDialog : public CCenteredDialog
{
protected:
    const char* ErrorStr;

public:
    CCertificateErrDialog(HWND hParent, const char* errorStr);

protected:
    virtual INT_PTR DialogProc(UINT uMsg, WPARAM wParam, LPARAM lParam);
};

// SChannel is a stream security package.  Keeping this opaque type behind the
// historic SSL name limits the migration to the TLS adapter and its call sites.
struct CSchannelConnection;
typedef CSchannelConnection SSL;

bool InitSSL(int logUID, int* errorID);
void FreeSSL(int loadStatus = 0);
void SSLThreadLocalCleanup();

int SSLRead(SSL* ssl, void* data, int size);
int SSLWrite(SSL* ssl, const void* data, int size);
int SSLPending(SSL* ssl);
int SSLGetError(SSL* ssl, int result);
int SSLShutdown(SSL* ssl);
void SSLFree(SSL* ssl);
int SSLtoWS2Error(int err);
