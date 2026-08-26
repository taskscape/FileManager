using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

[TestFixture]
[NonParallelizable]
public sealed class DeterministicNetworkFixtureTests
{
    [Test]
    public async Task Local_http_fixture_delivers_a_fragmented_response_without_a_public_endpoint()
    {
        const string response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
        await using var server = ScriptedProtocolServer.Start(async (stream, cancellationToken) =>
        {
            // Split on protocol boundaries so callers cannot accidentally rely on a single socket read.
            await stream.WriteAsync(Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\n"), cancellationToken);
            await Task.Delay(15, cancellationToken);
            await stream.WriteAsync(Encoding.ASCII.GetBytes("Content-Length: 2\r\n\r\nOK"), cancellationToken);
        });

        using var client = new TcpClient();
        await client.ConnectAsync("127.0.0.1", server.Port);
        var actual = await ReadExactlyAsync(client.GetStream(), response.Length);

        Assert.That(actual, Is.EqualTo(response));
    }

    [Test]
    public async Task Local_ftp_fixture_scripts_fragmented_multiline_reply_and_disconnect()
    {
        const string reply = "220-OpenSalamander fixture\r\n220 Ready\r\n";
        await using var server = ScriptedProtocolServer.Start(async (stream, cancellationToken) =>
        {
            await stream.WriteAsync(Encoding.ASCII.GetBytes("220-OpenSalamander fixture\r\n"), cancellationToken);
            await Task.Delay(15, cancellationToken);
            // Returning after the terminal line deliberately closes the control socket.
            await stream.WriteAsync(Encoding.ASCII.GetBytes("220 Ready\r\n"), cancellationToken);
        });

        using var client = new TcpClient();
        await client.ConnectAsync("127.0.0.1", server.Port);
        var stream = client.GetStream();
        var actual = await ReadExactlyAsync(stream, reply.Length);
        var trailingRead = await stream.ReadAsync(new byte[1]);

        Assert.That(actual, Is.EqualTo(reply));
        Assert.That(trailingRead, Is.Zero, "The scripted fixture must make the reconnect/disconnect boundary observable.");
    }

    [Test]
    public async Task Local_ftps_fixture_negotiates_tls_before_the_scripted_ftp_reply()
    {
        using var certificate = CreateCertificate();
        await using var server = ScriptedProtocolServer.Start(async (networkStream, cancellationToken) =>
        {
            using var tlsStream = new SslStream(networkStream, false);
            await tlsStream.AuthenticateAsServerAsync(new SslServerAuthenticationOptions
            {
                ServerCertificate = certificate,
                EnabledSslProtocols = SslProtocols.Tls12,
                CertificateRevocationCheckMode = X509RevocationMode.NoCheck
            }, cancellationToken);
            await tlsStream.WriteAsync(Encoding.ASCII.GetBytes("220 FTPS fixture ready\r\n"), cancellationToken);
        });

        using var client = new TcpClient();
        await client.ConnectAsync("127.0.0.1", server.Port);
        using var tlsStream = new SslStream(client.GetStream(), false, (_, _, _, _) => true);
        await tlsStream.AuthenticateAsClientAsync(new SslClientAuthenticationOptions
        {
            TargetHost = "localhost",
            EnabledSslProtocols = SslProtocols.Tls12,
            CertificateRevocationCheckMode = X509RevocationMode.NoCheck
        });

        Assert.That(await ReadExactlyAsync(tlsStream, "220 FTPS fixture ready\r\n".Length),
                    Is.EqualTo("220 FTPS fixture ready\r\n"));
    }

    [Test]
    public async Task Local_fixture_can_stall_until_the_client_deadline_cancels()
    {
        await using var server = ScriptedProtocolServer.Start((_, cancellationToken) =>
            Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken));

        using var client = new TcpClient();
        await client.ConnectAsync("127.0.0.1", server.Port);

        Assert.That(async () => await client.GetStream().ReadAsync(new byte[1]).AsTask().WaitAsync(TimeSpan.FromMilliseconds(150)),
                    Throws.TypeOf<TimeoutException>());
    }

    [TestFixture]
    [NonParallelizable]
    public sealed class ProductFtpControlConnectionTests : FileManagerUiTestBase
    {
        [Test]
        public async Task Quick_connect_consumes_a_fragmented_greeting_before_the_fixture_disconnects()
        {
            var receivedCommand = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
            await using var server = ScriptedProtocolServer.Start(async (stream, cancellationToken) =>
            {
                // A split multiline greeting makes the native reply reader cross socket-read boundaries before login begins.
                await stream.WriteAsync(Encoding.ASCII.GetBytes("220-OpenSalamander fixture\r\n"), cancellationToken);
                await Task.Delay(15, cancellationToken);
                await stream.WriteAsync(Encoding.ASCII.GetBytes("220 Ready\r\n"), cancellationToken);
                receivedCommand.TrySetResult(await ReadFtpLineAsync(stream, cancellationToken));
                // Returning closes the socket immediately after the first login command.
            });

            var connectDialog = OpenFtpConnectDialog();
            ConnectFtpServer(connectDialog, $"127.0.0.1:{server.Port}");

            Assert.That(await receivedCommand.Task.WaitAsync(TimeSpan.FromSeconds(10)), Does.StartWith("USER "),
                        "The FTP plug-in did not reach login after the fragmented greeting.");
            await server.Completion.WaitAsync(TimeSpan.FromSeconds(10));
        }
    }

    private static async Task<string> ReadExactlyAsync(Stream stream, int byteCount)
    {
        var buffer = new byte[byteCount];
        var offset = 0;
        while (offset != buffer.Length)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(offset));
            if (read == 0)
                throw new EndOfStreamException($"The fixture disconnected after {offset} of {buffer.Length} bytes.");
            offset += read;
        }
        return Encoding.ASCII.GetString(buffer);
    }

    private static async Task<string> ReadFtpLineAsync(Stream stream, CancellationToken cancellationToken)
    {
        var bytes = new List<byte>();
        while (true)
        {
            var oneByte = new byte[1];
            if (await stream.ReadAsync(oneByte, cancellationToken) == 0)
                throw new EndOfStreamException("The FTP client disconnected before sending its login command.");
            bytes.Add(oneByte[0]);
            if (bytes.Count >= 2 && bytes[^2] == '\r' && bytes[^1] == '\n')
                return Encoding.ASCII.GetString(bytes.ToArray());
        }
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
        using var generated = request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1), DateTimeOffset.UtcNow.AddDays(1));
        // SChannel requires a persisted user-key handle, so load the PFX through the .NET 10-compatible loader.
        return X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pfx), null, X509KeyStorageFlags.UserKeySet, null);
    }
}
