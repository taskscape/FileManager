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
    [TestCase(SslProtocols.Tls12)]
    [TestCase(SslProtocols.Tls13)]
    public async Task LocalTlsServerNegotiatesTheRequiredProtocol(SslProtocols protocol)
    {
        using var certificate = CreateCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();

        var server = AcceptAndAuthenticateAsync(listener, certificate, protocol);
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port);
        using var stream = new SslStream(client.GetStream(), false, (_, _, _, _) => true);
        try
        {
            await stream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
            {
                TargetHost = "localhost",
                EnabledSslProtocols = protocol,
                CertificateRevocationCheckMode = X509RevocationMode.NoCheck
            });
        }
        catch (Exception clientError)
        {
            try { await server; }
            catch (Exception serverError) { throw new AssertionException($"TLS server failed: {serverError}", clientError); }
            throw;
        }

        Assert.That(stream.SslProtocol, Is.EqualTo(protocol));
        Assert.That(await server, Is.EqualTo(protocol));
    }

    [Test]
    public async Task SelfSignedServerCertificateIsRejectedWithoutAnExplicitUserException()
    {
        using var certificate = CreateCertificate();
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();

        var server = AcceptAndAuthenticateAsync(listener, certificate, SslProtocols.Tls12);
        using var client = new TcpClient();
        await client.ConnectAsync(IPAddress.Loopback, ((IPEndPoint)listener.LocalEndpoint).Port);
        using var stream = new SslStream(client.GetStream(), false);

        Assert.That(async () => await stream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
        {
            TargetHost = "localhost",
            EnabledSslProtocols = SslProtocols.Tls12,
            CertificateRevocationCheckMode = X509RevocationMode.NoCheck
        }), Throws.TypeOf<AuthenticationException>());

        try { await server; }
        catch (AuthenticationException) { }
    }

    private static async Task<SslProtocols> AcceptAndAuthenticateAsync(TcpListener listener, X509Certificate2 certificate, SslProtocols protocol)
    {
        using var server = await listener.AcceptTcpClientAsync();
        using var stream = new SslStream(server.GetStream(), false);
        await stream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
        {
            ServerCertificate = certificate,
            EnabledSslProtocols = protocol,
            ClientCertificateRequired = false,
            CertificateRevocationCheckMode = X509RevocationMode.NoCheck
        });
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
        // Windows SChannel cannot use an ephemeral private-key handle for a
        // server credential, so use the .NET 10-compatible PFX loader with UserKeySet.
        return X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pfx), null, X509KeyStorageFlags.UserKeySet, null);
    }
}
