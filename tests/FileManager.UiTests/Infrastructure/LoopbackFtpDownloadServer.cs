using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace FileManager.UiTests.Infrastructure;

// A private multi-connection server exercises the actual FTP worker and its
// secondary control connections. Every endpoint is restricted to loopback.
internal sealed class LoopbackFtpDownloadServer : IAsyncDisposable
{
    private readonly TcpListener listener = new(IPAddress.Loopback, 0);
    private readonly CancellationTokenSource cancellation = new();
    private readonly ConcurrentBag<Task> clients = [];
    private readonly Task acceptTask;
    private readonly int pauseAfterBytes;
    internal readonly byte[] Payload = Enumerable.Range(0, 256 * 1024).Select(index => (byte)(index % 251)).ToArray();
    internal readonly ConcurrentQueue<string> Commands = new();
    internal TaskCompletionSource ListingSent { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal TaskCompletionSource TransferPaused { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal TaskCompletionSource ReleaseTransfer { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal TaskCompletionSource NetworkCompleted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal TaskCompletionSource DeleteReceived { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    internal bool DisconnectAtPause { get; set; }
    internal int Port => ((IPEndPoint)listener.LocalEndpoint).Port;
    internal const string FileName = "reliability-payload.bin";

    internal LoopbackFtpDownloadServer(bool pauseBeforeAnyBytes = false)
    {
        pauseAfterBytes = pauseBeforeAnyBytes ? 0 : Payload.Length / 2;
        listener.Start();
        acceptTask = AcceptAsync();
    }

    private async Task AcceptAsync()
    {
        try
        {
            while (!cancellation.IsCancellationRequested)
                clients.Add(ServeAsync(await listener.AcceptTcpClientAsync(cancellation.Token)));
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested) { }
        catch (SocketException) when (cancellation.IsCancellationRequested) { }
    }

    private async Task ServeAsync(TcpClient client)
    {
        using (client)
        {
            TcpListener? passive = null;
            IPEndPoint? active = null;
            var stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.ASCII, false, leaveOpen: true);
            var token = cancellation.Token;
            long offset = 0;
            try
            {
                await Reply("220 FileManager loopback fixture ready");
                while (await reader.ReadLineAsync(token) is { } line)
                {
                    var separator = line.IndexOf(' ');
                    var command = (separator < 0 ? line : line[..separator]).ToUpperInvariant();
                    var argument = separator < 0 ? string.Empty : line[(separator + 1)..];
                    Commands.Enqueue(command == "PASS" ? "PASS <omitted>" : line);
                    switch (command)
                    {
                        case "USER": await Reply("331 Password required"); break;
                        case "PASS": await Reply("230 Logged in"); break;
                        case "SYST": await Reply("215 UNIX Type: L8"); break;
                        case "FEAT": await Reply("211-Features\r\n MLST type*;size*;modify*;\r\n SIZE\r\n MDTM\r\n REST STREAM\r\n211 End"); break;
                        case "PWD": case "XPWD": await Reply("257 \"/\" is current directory"); break;
                        case "CWD": case "CDUP": await Reply("250 Directory changed"); break;
                        case "TYPE": case "OPTS": case "NOOP": await Reply("200 OK"); break;
                        case "SIZE": await Reply($"213 {Payload.Length}"); break;
                        case "MDTM": await Reply("213 20260905000000"); break;
                        case "PASV": case "EPSV":
                            passive?.Stop();
                            passive = new TcpListener(IPAddress.Loopback, 0);
                            passive.Start();
                            var port = ((IPEndPoint)passive.LocalEndpoint).Port;
                            await Reply(command == "PASV" ? $"227 Entering Passive Mode (127,0,0,1,{port / 256},{port % 256})" :
                                                           $"229 Entering Extended Passive Mode (|||{port}|)");
                            break;
                        case "PORT":
                            var components = argument.Split(',').Select(int.Parse).ToArray();
                            var address = IPAddress.Parse(string.Join('.', components.Take(4)));
                            if (components.Length != 6 || !IPAddress.IsLoopback(address)) throw new InvalidDataException("Non-loopback FTP data address.");
                            active = new IPEndPoint(address, components[4] * 256 + components[5]);
                            await Reply("200 PORT accepted");
                            break;
                        case "REST":
                            offset = long.Parse(argument, System.Globalization.CultureInfo.InvariantCulture);
                            await Reply("350 Restart accepted");
                            break;
                        case "LIST": case "MLSD": case "NLST":
                            await Reply("150 Opening listing data connection");
                            using (var data = await DataConnection())
                            {
                                var listing = command == "MLSD" ? $"type=file;size={Payload.Length};modify=20260905000000; {FileName}\r\n" :
                                              command == "NLST" ? FileName + "\r\n" : $"-rw-r--r-- 1 fixture fixture {Payload.Length} Sep 05 2026 {FileName}\r\n";
                                await data.GetStream().WriteAsync(Encoding.ASCII.GetBytes(listing), token);
                            }
                            await Reply("226 Listing complete");
                            ListingSent.TrySetResult();
                            break;
                        case "RETR":
                            if (Path.GetFileName(argument) != FileName || offset < 0 || offset > Payload.Length)
                                throw new InvalidDataException("Unexpected FTP retrieval request: " + line);
                            await Reply($"150 Opening binary data connection ({Payload.Length} bytes)");
                            using (var data = await DataConnection())
                            {
                                var start = (int)offset;
                                var boundary = Math.Max(start, pauseAfterBytes);
                                await data.GetStream().WriteAsync(Payload.AsMemory(start, boundary - start), token);
                                TransferPaused.TrySetResult();
                                await ReleaseTransfer.Task.WaitAsync(token);
                                if (!DisconnectAtPause)
                                    await data.GetStream().WriteAsync(Payload.AsMemory(boundary), token);
                            }
                            offset = 0;
                            await Reply(DisconnectAtPause ? "426 Transfer interrupted" : "226 Transfer complete");
                            NetworkCompleted.TrySetResult();
                            break;
                        case "DELE":
                            DeleteReceived.TrySetResult();
                            await Reply("250 File deleted");
                            break;
                        case "ABOR": await Reply("226 Transfer aborted"); break;
                        case "QUIT": await Reply("221 Goodbye"); return;
                        default: await Reply("502 Command not implemented"); break;
                    }
                }
            }
            catch (OperationCanceledException) when (token.IsCancellationRequested) { }
            catch (IOException) when (token.IsCancellationRequested || !client.Connected) { }
            finally { passive?.Stop(); }

            Task Reply(string text) => stream.WriteAsync(Encoding.ASCII.GetBytes(text + "\r\n"), token).AsTask();
            async Task<TcpClient> DataConnection()
            {
                if (passive is not null)
                {
                    var data = await passive.AcceptTcpClientAsync(token);
                    passive.Stop(); passive = null;
                    return data;
                }
                if (active is null) throw new InvalidDataException("FTP client did not select a data endpoint.");
                var connection = new TcpClient();
                await connection.ConnectAsync(active.Address, active.Port, token);
                return connection;
            }
        }
    }

    public async ValueTask DisposeAsync()
    {
        cancellation.Cancel();
        listener.Stop();
        await acceptTask;
        await Task.WhenAll(clients);
        cancellation.Dispose();
    }
}
