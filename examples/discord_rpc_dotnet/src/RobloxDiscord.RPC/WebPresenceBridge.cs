using System.Net;
using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;

namespace DiscordRpc;

/// <summary>
/// Publishes presence over the arRPC bridge protocol on loopback, so Discord Web clients
/// (for example Vencord's "WebRichPresence (arRPC)" plugin) can show the activity while the
/// Discord desktop app is not running. The desktop IPC transport is independent of this one.
/// </summary>
internal sealed class WebPresenceBridge : IDisposable
{
    private const int DefaultPort = 1337;
    private const string PortVariable = "RML_DISCORD_WEB_BRIDGE_PORT";
    private const string SocketId = "roblox-studio";
    private const string HandshakeGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    private const int MaxHandshakeBytes = 8 * 1024;
    private const int MaxClients = 4;

    private static readonly TimeSpan HandshakeTimeout = TimeSpan.FromSeconds(5);
    private static readonly TimeSpan CloseTimeout = TimeSpan.FromMilliseconds(500);
    private static readonly TimeSpan KeepAliveInterval = TimeSpan.FromSeconds(30);

    /// <summary>Browser origins allowed to read the activity; anything else is a local page snooping.</summary>
    private static readonly string[] AllowedOrigins =
    [
        "https://discord.com",
        "https://ptb.discord.com",
        "https://canary.discord.com"
    ];

    private readonly CancellationTokenSource _shutdown = new();
    private readonly List<BridgeClient> _clients = [];
    private readonly TcpListener? _listener;
    private string? _lastMessage;
    private int _disposed;

    public WebPresenceBridge()
    {
        var port = ResolvePort();
        if (port == 0)
        {
            DiscordRpc.Logger.Info($"Web presence bridge disabled by {PortVariable}.");
            return;
        }

        var listener = new TcpListener(IPAddress.Loopback, port);

        try
        {
            listener.Start();
        }
        catch (SocketException ex)
        {
            DiscordRpc.Logger.Warn($"Web presence bridge disabled, 127.0.0.1:{port} is taken ({ex.SocketErrorCode}).");
            return;
        }

        _listener = listener;
        DiscordRpc.Logger.Info($"Web presence bridge listening on 127.0.0.1:{port}.");

        _ = Task.Run(AcceptLoopAsync);
    }

    /// <summary>Broadcasts an activity to every connected Discord Web client; null clears it.</summary>
    public void Publish(JsonObject? activity)
    {
        var message = new JsonObject
        {
            ["activity"] = activity,
            ["pid"] = Environment.ProcessId,
            ["socketId"] = SocketId
        }.ToJsonString();

        BridgeClient[] targets;

        lock (_clients)
        {
            _lastMessage = message;
            targets = _clients.ToArray();
        }

        foreach (var client in targets)
        {
            _ = SendAsync(client, () => message);
        }
    }

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

        _listener?.Stop();

        // Close politely first: a queued clear is still on the wire, and the token below cancels it.
        var clients = Snapshot();
        Task.WhenAll(clients.Select(client => client.CloseAsync())).Wait(CloseTimeout);

        foreach (var client in clients)
        {
            Drop(client);
        }

        _shutdown.Cancel();
    }

    private static int ResolvePort()
    {
        var configured = Environment.GetEnvironmentVariable(PortVariable);
        if (string.IsNullOrWhiteSpace(configured)) return DefaultPort;

        if (int.TryParse(configured, out var port) && port is >= 0 and <= ushort.MaxValue) return port;

        DiscordRpc.Logger.Warn($"Ignoring {PortVariable}='{configured}', expected a port between 0 and 65535.");

        return DefaultPort;
    }

    private BridgeClient[] Snapshot()
    {
        lock (_clients)
        {
            return _clients.ToArray();
        }
    }

    private void Drop(BridgeClient client)
    {
        lock (_clients)
        {
            _clients.Remove(client);
        }

        client.Dispose();
    }

    private async Task AcceptLoopAsync()
    {
        while (!_shutdown.IsCancellationRequested)
        {
            try
            {
                var connection = await _listener!.AcceptTcpClientAsync(_shutdown.Token);
                _ = Task.Run(() => ServeAsync(connection));
            }
            catch (Exception ex) when (ex is OperationCanceledException or ObjectDisposedException ||
                                       Volatile.Read(ref _disposed) != 0)
            {
                return;
            }
            catch (Exception ex)
            {
                DiscordRpc.Logger.Error($"Web presence bridge stopped accepting clients: {ex.Message}");
                return;
            }
        }
    }

    private async Task ServeAsync(TcpClient connection)
    {
        BridgeClient? client = null;
        var accepted = false;

        try
        {
            connection.NoDelay = true;

            var stream = connection.GetStream();
            var request = await ReadHandshakeAsync(stream);
            var rejection = Validate(request);

            if (rejection is not null)
            {
                // A peer that sent nothing is a port probe, not a client worth a warning.
                if (request is not null) DiscordRpc.Logger.Warn($"Rejected a presence bridge client: {rejection}.");

                await WriteAsync(stream, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
                connection.Dispose();
                return;
            }

            await WriteAsync(stream, BuildHandshakeResponse(request!["sec-websocket-key"]));

            client = new BridgeClient(connection,
                WebSocket.CreateFromStream(stream, isServer: true, subProtocol: null, KeepAliveInterval));

            // Taking the slot inside the send callback keeps a concurrent Publish queued behind this
            // replay, so a client that connects mid-update never ends up with the older activity.
            await SendAsync(client, () =>
            {
                lock (_clients)
                {
                    if (_clients.Count >= MaxClients) return null;

                    _clients.Add(client);
                    accepted = true;

                    return _lastMessage;
                }
            });

            if (!accepted)
            {
                DiscordRpc.Logger.Warn("Rejected a presence bridge client: too many connected clients.");
                await client.CloseAsync(WebSocketCloseStatus.PolicyViolation, "too many connected clients");
                return;
            }

            // The replay may have dropped the client already.
            if (client.Socket.State != WebSocketState.Open) return;

            DiscordRpc.Logger.Info("Discord Web client connected to the presence bridge.");

            await DrainAsync(client);
        }
        catch (Exception ex) when (ex is OperationCanceledException or ObjectDisposedException)
        {
            // Shutting down.
        }
        catch (WebSocketException)
        {
            // The tab went away without a close frame.
        }
        catch (Exception ex)
        {
            DiscordRpc.Logger.Error($"Web presence bridge client failed: {ex.Message}");
        }
        finally
        {
            if (client is null)
            {
                connection.Dispose();
            }
            else
            {
                Drop(client);

                if (accepted) DiscordRpc.Logger.Info("Discord Web client disconnected from the presence bridge.");
            }
        }
    }

    /// <summary>Reads incoming frames until the client closes; the bridge never trusts their content.</summary>
    private async Task DrainAsync(BridgeClient client)
    {
        var buffer = new byte[256];

        while (client.Socket.State == WebSocketState.Open)
        {
            var received = await client.Socket.ReceiveAsync(buffer, _shutdown.Token);
            if (received.MessageType == WebSocketMessageType.Close) return;
        }
    }

    private async Task SendAsync(BridgeClient client, Func<string?> resolve)
    {
        try
        {
            await client.SendAsync(resolve, _shutdown.Token);
        }
        catch (Exception ex) when (ex is OperationCanceledException or ObjectDisposedException or WebSocketException)
        {
            Drop(client);
        }
        catch (Exception ex)
        {
            DiscordRpc.Logger.Error($"Failed to push presence to a Discord Web client: {ex.Message}");
            Drop(client);
        }
    }

    private string? Validate(Dictionary<string, string>? request)
    {
        if (request is null) return "malformed handshake";
        if (!request.ContainsKey("sec-websocket-key")) return "not a WebSocket handshake";

        if (!request.TryGetValue("upgrade", out var upgrade) ||
            !upgrade.Equals("websocket", StringComparison.OrdinalIgnoreCase))
        {
            return "missing WebSocket upgrade";
        }

        if (!request.TryGetValue("sec-websocket-version", out var version))
        {
            return "missing Sec-WebSocket-Version header";
        }

        if (version.Trim() != "13")
        {
            return $"unsupported WebSocket version '{version}'";
        }

        if (!request.TryGetValue("origin", out var origin) ||
            !AllowedOrigins.Contains(origin, StringComparer.OrdinalIgnoreCase))
        {
            return $"origin '{origin ?? "none"}' is not a Discord client";
        }

        lock (_clients)
        {
            if (_clients.Count >= MaxClients) return "too many connected clients";
        }

        return null;
    }

    private async Task<Dictionary<string, string>?> ReadHandshakeAsync(NetworkStream stream)
    {
        using var deadline = CancellationTokenSource.CreateLinkedTokenSource(_shutdown.Token);
        deadline.CancelAfter(HandshakeTimeout);

        var request = new StringBuilder();
        var buffer = new byte[1024];

        while (!request.ToString().Contains("\r\n\r\n"))
        {
            if (request.Length >= MaxHandshakeBytes) return null;

            var read = await stream.ReadAsync(buffer, deadline.Token);
            if (read == 0) return null;

            request.Append(Encoding.ASCII.GetString(buffer, 0, read));
        }

        var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        foreach (var line in request.ToString().Split("\r\n"))
        {
            var separator = line.IndexOf(':');
            if (separator < 0) continue;

            headers[line[..separator].Trim()] = line[(separator + 1)..].Trim();
        }

        return headers;
    }

    private Task WriteAsync(NetworkStream stream, string response)
    {
        return stream.WriteAsync(Encoding.ASCII.GetBytes(response), _shutdown.Token).AsTask();
    }

    private static string BuildHandshakeResponse(string key)
    {
        var accept = Convert.ToBase64String(SHA1.HashData(Encoding.ASCII.GetBytes(key + HandshakeGuid)));

        return "HTTP/1.1 101 Switching Protocols\r\n" +
               "Upgrade: websocket\r\n" +
               "Connection: Upgrade\r\n" +
               $"Sec-WebSocket-Accept: {accept}\r\n\r\n";
    }

    private sealed class BridgeClient(TcpClient connection, WebSocket socket) : IDisposable
    {
        private readonly SemaphoreSlim _sendLock = new(1, 1);

        public WebSocket Socket { get; } = socket;

        /// <summary>Sends what <paramref name="resolve"/> returns, evaluated in send order.</summary>
        public async Task SendAsync(Func<string?> resolve, CancellationToken cancellation)
        {
            await _sendLock.WaitAsync(cancellation).ConfigureAwait(false);

            try
            {
                var message = resolve();
                if (message is null || Socket.State != WebSocketState.Open) return;

                await Socket.SendAsync(Encoding.UTF8.GetBytes(message), WebSocketMessageType.Text, true, cancellation)
                    .ConfigureAwait(false);
            }
            finally
            {
                _sendLock.Release();
            }
        }

        public async Task CloseAsync(
            WebSocketCloseStatus status = WebSocketCloseStatus.NormalClosure,
            string reason = "Studio is shutting down")
        {
            await _sendLock.WaitAsync().ConfigureAwait(false);

            try
            {
                if (Socket.State != WebSocketState.Open) return;

                await Socket.CloseAsync(status, reason, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception ex) when (ex is WebSocketException or ObjectDisposedException or OperationCanceledException)
            {
                // The client is already gone.
            }
            finally
            {
                _sendLock.Release();
            }
        }

        /// <summary>The send lock is intentionally left undisposed: a send racing shutdown would
        /// otherwise fault on a disposed semaphore instead of seeing a closed socket.</summary>
        public void Dispose()
        {
            Socket.Dispose();
            connection.Dispose();
        }
    }
}
