using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class SChannelTlsIntegrationTests
{
    // SChannel credential acquisition can exceed five seconds on a busy CI profile without indicating a protocol hang.
    private static readonly TimeSpan HandshakeTimeout = TimeSpan.FromSeconds(15);

    [TestCase(SslProtocols.Tls12)]
    [TestCase(SslProtocols.Tls13)]
    public async Task LocalTlsServerNegotiatesTheRequiredProtocol(SslProtocols protocol)
    {
        // Windows 10 SChannel never negotiates TLS 1.3; skip that platform prerequisite instead of waiting for a doomed handshake.
        if (protocol == SslProtocols.Tls13 && Environment.OSVersion.Version.Build < 20348)
            Assert.Ignore("TLS 1.3 requires Windows 11 or Windows Server 2022 and later.");

        using var timeout = new CancellationTokenSource(HandshakeTimeout);
        using var certificate = CreateCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();

        var server = AcceptAndAuthenticateAsync(listener, certificate, protocol, timeout.Token);
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, timeout.Token);
        using var stream = new SslStream(client.GetStream(), false, (_, _, _, _) => true);
        try
        {
            await stream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = "localhost",
                EnabledSslProtocols = protocol,
                CertificateRevocationCheckMode = X509RevocationMode.NoCheck
            }, timeout.Token);

            Assert.That(stream.SslProtocol, Is.EqualTo(protocol));
            Assert.That(await server.WaitAsync(timeout.Token), Is.EqualTo(protocol));
        }
        catch (OperationCanceledException error) when (timeout.IsCancellationRequested)
        {
            throw new AssertionException($"The local {protocol} SChannel handshake did not complete within {HandshakeTimeout.TotalSeconds:0} seconds.", error);
        }
        catch (Exception clientError)
        {
            try { await server.WaitAsync(timeout.Token); }
            catch (OperationCanceledException serverError) when (timeout.IsCancellationRequested)
            {
                // Preserve the initiating client error while classifying the peer cancellation as the shared handshake deadline.
                throw new AssertionException($"The local {protocol} SChannel handshake did not complete within {HandshakeTimeout.TotalSeconds:0} seconds.",
                                             new AggregateException(clientError, serverError));
            }
            catch (Exception serverError) { throw new AssertionException($"TLS server failed: {serverError}", clientError); }
            throw;
        }
        finally
        {
            await StopAndObserveServerAsync(timeout, listener, server);
        }
    }

    [Test]
    public async Task SelfSignedServerCertificateIsRejectedWithoutAnExplicitUserException()
    {
        using var timeout = new CancellationTokenSource(HandshakeTimeout);
        using var certificate = CreateCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();

        var server = AcceptAndAuthenticateAsync(listener, certificate, SslProtocols.Tls12, timeout.Token);
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port, timeout.Token);
        using var stream = new SslStream(client.GetStream(), false);

        try
        {
            Assert.That(async () => await stream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = "localhost",
                EnabledSslProtocols = SslProtocols.Tls12,
                CertificateRevocationCheckMode = X509RevocationMode.NoCheck
            }, timeout.Token), Throws.TypeOf<AuthenticationException>());

            try { await server.WaitAsync(timeout.Token); }
            catch (AuthenticationException) { }
        }
        catch (OperationCanceledException error) when (timeout.IsCancellationRequested)
        {
            throw new AssertionException($"The self-signed certificate rejection did not complete within {HandshakeTimeout.TotalSeconds:0} seconds.", error);
        }
        finally
        {
            await StopAndObserveServerAsync(timeout, listener, server);
        }
    }

    private static async Task StopAndObserveServerAsync(CancellationTokenSource timeout, TcpListener listener, Task server)
    {
        // Cancel, close, and observe every peer task so failed handshakes cannot retain a listener or testhost thread.
        timeout.Cancel();
        listener.Stop();
        try { await server.WaitAsync(TimeSpan.FromSeconds(1)); }
        catch { }
    }

    private static async Task<SslProtocols> AcceptAndAuthenticateAsync(TcpListener listener, X509Certificate2 certificate,
                                                                        SslProtocols protocol, CancellationToken cancellationToken)
    {
        using var server = await listener.AcceptTcpClientAsync(cancellationToken);
        using var stream = new SslStream(server.GetStream(), false);
        await stream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
        {
            ServerCertificate = certificate,
            EnabledSslProtocols = protocol,
            ClientCertificateRequired = false,
            CertificateRevocationCheckMode = X509RevocationMode.NoCheck
        }, cancellationToken);
        return stream.SslProtocol;
    }

    private static X509Certificate2 CreateCertificate()
    {
        using var key = RSA.Create(2048);
        var request = new CertificateRequest("CN=localhost", key, HashAlgorithmName.SHA256, RSASignaturePadding.Pkcs1);
        var san = new SubjectAlternativeNameBuilder();
        san.AddDnsName("localhost");
        request.CertificateExtensions.Add(san.Build());
        request.CertificateExtensions.Add(new X509BasicConstraintsExtension(false, false, 0, false));
        request.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature | X509KeyUsageFlags.KeyEncipherment, false));
        request.CertificateExtensions.Add(new X509SubjectKeyIdentifierExtension(request.PublicKey, false));
        using var generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        // SChannel requires a persisted server key; a machine-key import avoids the user-profile deletion race between cases.
        return new X509Certificate2(generated.Export(X509ContentType.Pfx), (string?)null,
                                    X509KeyStorageFlags.MachineKeySet | X509KeyStorageFlags.Exportable);
    }
}
