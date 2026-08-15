using System.Net;
using System.Net.Sockets;

namespace FileManager.UiTests.Infrastructure;

// A one-connection loopback server keeps protocol failure timing under test
// control without reaching a public endpoint or relying on a shared service.
internal sealed class ScriptedProtocolServer : IAsyncDisposable
{
    private readonly TcpListener listener;
    private readonly CancellationTokenSource cancellation;
    private readonly Task serverTask;

    private ScriptedProtocolServer(TcpListener listener, CancellationTokenSource cancellation, Task serverTask)
    {
        this.listener = listener;
        this.cancellation = cancellation;
        this.serverTask = serverTask;
    }

    public int Port => ((IPEndPoint)listener.LocalEndpoint).Port;

    // Tests await this boundary when a scripted close itself is the behavior under assertion.
    public Task Completion => serverTask;

    public static ScriptedProtocolServer Start(Func<NetworkStream, CancellationToken, Task> script)
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var cancellation = new CancellationTokenSource();
        var serverTask = ServeOneConnectionAsync(listener, cancellation.Token, script);
        return new ScriptedProtocolServer(listener, cancellation, serverTask);
    }

    private static async Task ServeOneConnectionAsync(
        TcpListener listener,
        CancellationToken cancellationToken,
        Func<NetworkStream, CancellationToken, Task> script)
    {
        try
        {
            using var client = await listener.AcceptTcpClientAsync(cancellationToken);
            await script(client.GetStream(), cancellationToken);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            // Disposal intentionally interrupts a script that is stalling a client.
        }
    }

    public async ValueTask DisposeAsync()
    {
        cancellation.Cancel();
        listener.Stop();
        await serverTask;
        cancellation.Dispose();
    }

}
