using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace FileManager.UiTests.Infrastructure;

// Real multipart traffic stays on loopback; no UI test publishes a user's files to the service.
internal sealed class LoopbackInfostackerServer : IDisposable
{
    private readonly TcpListener listener = new(IPAddress.Loopback, 0);
    private readonly CancellationTokenSource stop = new();
    private readonly Task worker;
    internal readonly ConcurrentQueue<Upload> Requests = new();
    internal readonly TaskCompletionSource Received = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal readonly TaskCompletionSource Release = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal int Status = 200;
    internal string Response = "{\"id\":\"file-test-123\"}";
    internal bool HoldResponse;
    internal string Url { get; }

    internal sealed record Part(string Headers, byte[] Bytes);
    internal sealed record Upload(string Headers, IReadOnlyList<Part> Parts);

    internal LoopbackInfostackerServer()
    {
        listener.Start();
        Url = $"http://127.0.0.1:{((IPEndPoint)listener.LocalEndpoint).Port}/";
        worker = Run();
    }

    private async Task Run()
    {
        try
        {
            while (!stop.IsCancellationRequested)
            {
                using var client = await listener.AcceptTcpClientAsync(stop.Token);
                using var stream = client.GetStream();
                var headerBytes = new List<byte>();
                var single = new byte[1];
                while (headerBytes.Count < 65536)
                {
                    if (await stream.ReadAsync(single, stop.Token) != 1) throw new IOException("Incomplete HTTP headers.");
                    headerBytes.Add(single[0]);
                    if (headerBytes.Count >= 4 && headerBytes.TakeLast(4).SequenceEqual("\r\n\r\n"u8.ToArray())) break;
                }
                var headers = Encoding.ASCII.GetString(headerBytes.ToArray());
                var lengthLine = headers.Split("\r\n").Single(line => line.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase));
                var length = int.Parse(lengthLine.Split(':')[1].Trim());
                if (length < 0 || length > 110 * 1024 * 1024) throw new IOException("Unbounded test upload.");
                var body = new byte[length];
                await stream.ReadExactlyAsync(body, stop.Token);
                var boundary = headers.Split("\r\n").Single(line => line.StartsWith("Content-Type:", StringComparison.OrdinalIgnoreCase)).Split("boundary=")[1];
                var parts = new List<Part>();
                // Latin1 provides a reversible byte-to-character mapping, including arbitrary binary payloads.
                foreach (var block in Encoding.Latin1.GetString(body).Split("--" + boundary).Skip(1))
                {
                    if (block.StartsWith("--", StringComparison.Ordinal)) break;
                    var separator = block.IndexOf("\r\n\r\n", StringComparison.Ordinal);
                    if (separator < 0 || !block.EndsWith("\r\n", StringComparison.Ordinal)) throw new IOException("Malformed multipart framing.");
                    parts.Add(new Part(Encoding.UTF8.GetString(Encoding.Latin1.GetBytes(block[..separator])),
                                       Encoding.Latin1.GetBytes(block[(separator + 4)..^2])));
                }
                Requests.Enqueue(new Upload(headers, parts));
                Received.TrySetResult();
                if (HoldResponse) await Release.Task.WaitAsync(stop.Token);
                var response = Encoding.UTF8.GetBytes(Response);
                var responseHeaders = Encoding.ASCII.GetBytes($"HTTP/1.1 {Status} Fixture\r\nContent-Type: application/json\r\nContent-Length: {response.Length}\r\nConnection: close\r\n\r\n");
                // Fragment JSON to exercise the production response accumulation path.
                try
                {
                    await stream.WriteAsync(responseHeaders, stop.Token);
                    foreach (var chunk in response.Chunk(3)) await stream.WriteAsync(chunk, stop.Token);
                }
                // A cancelled client may reset its held response; keep request parsing and other I/O failures visible.
                catch (IOException error) when (HoldResponse && error.InnerException is SocketException
                    { SocketErrorCode: SocketError.ConnectionReset or SocketError.ConnectionAborted }) { }
            }
        }
        catch (OperationCanceledException) when (stop.IsCancellationRequested) { }
        catch (IOException) when (stop.IsCancellationRequested) { }
        catch (SocketException) when (stop.IsCancellationRequested) { }
    }

    public void Dispose()
    {
        stop.Cancel();
        listener.Stop();
        Release.TrySetResult();
        try { worker.GetAwaiter().GetResult(); }
        finally { stop.Dispose(); }
    }
}
