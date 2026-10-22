using System.Net.Sockets;
using System.Text;
using FileManager.UiTests.Infrastructure;
using NUnit.Framework;

namespace FileManager.UiTests;

public sealed class LoopbackInfostackerServerTests
{
    [Test]
    public async Task Cancelled_held_response_does_not_stop_the_next_upload()
    {
        // Force the cancellation race with a TCP reset, then require another real upload to succeed.
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        using var server = new LoopbackInfostackerServer { HoldResponse = true };
        var endpoint = new Uri(server.Url);
        const string boundary = "infostacker-cancellation-probe";
        var body = Encoding.ASCII.GetBytes($"--{boundary}\r\nContent-Disposition: form-data; name=\"markdown\"\r\n\r\nprobe\r\n--{boundary}--\r\n");
        var headers = Encoding.ASCII.GetBytes($"POST /sharing/uploadmarkdownwithfiles HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: multipart/form-data; boundary={boundary}\r\nContent-Length: {body.Length}\r\nConnection: close\r\n\r\n");

        using (var cancelled = await Upload())
        {
            await server.Received.Task.WaitAsync(timeout.Token);
            cancelled.Client.LingerState = new LingerOption(true, 0);
        }
        server.Release.TrySetResult();

        using var next = await Upload();
        using var reader = new StreamReader(next.GetStream(), Encoding.UTF8);
        var response = await reader.ReadToEndAsync(timeout.Token);
        Assert.Multiple(() =>
        {
            Assert.That(response, Does.StartWith("HTTP/1.1 200"));
            Assert.That(response, Does.EndWith(server.Response));
            Assert.That(server.Requests, Has.Count.EqualTo(2));
        });

        async Task<TcpClient> Upload()
        {
            var client = new TcpClient();
            try
            {
                await client.ConnectAsync(endpoint.Host, endpoint.Port, timeout.Token);
                await client.GetStream().WriteAsync(headers.Concat(body).ToArray(), timeout.Token);
                return client;
            }
            catch
            {
                client.Dispose();
                throw;
            }
        }
    }
}
