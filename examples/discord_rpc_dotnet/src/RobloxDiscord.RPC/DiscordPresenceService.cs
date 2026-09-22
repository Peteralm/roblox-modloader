using System.Text.Json.Nodes;
using DiscordRPC;

namespace DiscordRpc;

internal sealed class DiscordPresenceService : IDisposable
{
    private const string ApplicationId = "1396335710755098757";
    private const string LargeImageText = "Roblox Studio";
    private const string SmallImageText = "RobloxModLoader";

    private readonly DiscordRpcClient _client;
    private readonly WebPresenceBridge _bridge = new();
    private readonly DateTime _sessionStart = DateTime.UtcNow;
    private readonly object _gate = new();

    private JsonObject? _activity;
    private bool _desktopConnected;

    public DiscordPresenceService()
    {
        _client = new DiscordRpcClient(ApplicationId);
        _client.OnReady += OnDesktopReady;
        _client.OnClose += OnDesktopClosed;
        _client.OnError += (_, e) => DiscordRpc.Logger.Error($"Discord error: {e.Message}");
        _client.Initialize();
    }

    public void Dispose()
    {
        try
        {
            // Detach first: disposing the client raises OnClose, which would republish the activity.
            _client.OnReady -= OnDesktopReady;
            _client.OnClose -= OnDesktopClosed;

            lock (_gate)
            {
                _bridge.Publish(null);
                _bridge.Dispose();
            }

            if (_client.IsDisposed) return;

            _client.ClearPresence();
            _client.Dispose();
        }
        catch (Exception ex)
        {
            DiscordRpc.Logger.Error($"Error during shutdown: {ex.Message}");
        }
    }

    public void SetEditing(string placeName, string? thumbnailUrl)
    {
        Set($"Editing {placeName}", PresenceState.Editing, thumbnailUrl);
    }

    public void SetPlayTesting(string placeName, string? thumbnailUrl)
    {
        Set($"Play testing {placeName}", PresenceState.PlayTesting, thumbnailUrl);
    }

    public void SetIdle()
    {
        Set("In Roblox Studio", PresenceState.Idle, null);
    }

    private void Set(string details, PresenceState state, string? thumbnailUrl)
    {
        var largeImage = string.IsNullOrEmpty(thumbnailUrl) ? "studio" : thumbnailUrl;
        var smallImage = state switch
        {
            PresenceState.Idle => "idle",
            PresenceState.Editing => "editing",
            PresenceState.PlayTesting => "play",
            _ => "studio"
        };

        PublishWebActivity(BuildWebActivity(details, state, largeImage, smallImage));

        if (_client.IsDisposed) return;

        _client.SetPresence(new RichPresence
        {
            Details = details,
            State = state.ToString(),
            Timestamps = new Timestamps(_sessionStart),
            Assets = new Assets
            {
                LargeImageKey = largeImage,
                LargeImageText = LargeImageText,
                SmallImageKey = smallImage,
                SmallImageText = SmallImageText
            }
        });
    }

    /// <summary>
    /// Remembers the current activity and pushes it to Discord Web only while the desktop app is
    /// absent, so a running desktop client never shows the same activity twice.
    /// </summary>
    private void PublishWebActivity(JsonObject activity)
    {
        lock (_gate)
        {
            _activity = activity;

            if (_desktopConnected) return;

            _bridge.Publish(activity);
        }
    }

    private void OnDesktopReady(object sender, DiscordRPC.Message.ReadyMessage message)
    {
        DiscordRpc.Logger.Info($"Connected to Discord as {message.User.Username}; the Discord Web fallback is now idle.");
        SetDesktopConnected(true);
    }

    private void OnDesktopClosed(object sender, DiscordRPC.Message.CloseMessage message)
    {
        DiscordRpc.Logger.Info($"Discord desktop dropped the connection ({message.Reason}); falling back to Discord Web.");
        SetDesktopConnected(false);
    }

    private void SetDesktopConnected(bool connected)
    {
        lock (_gate)
        {
            if (_desktopConnected == connected) return;

            _desktopConnected = connected;
            _bridge.Publish(connected ? null : _activity);
        }
    }

    private JsonObject BuildWebActivity(string details, PresenceState state, string largeImage, string smallImage)
    {
        return new JsonObject
        {
            ["application_id"] = ApplicationId,
            ["type"] = 0,
            ["flags"] = 0,
            ["details"] = details,
            ["state"] = state.ToString(),
            ["timestamps"] = new JsonObject
            {
                ["start"] = new DateTimeOffset(_sessionStart, TimeSpan.Zero).ToUnixTimeMilliseconds()
            },
            ["assets"] = new JsonObject
            {
                ["large_image"] = largeImage,
                ["large_text"] = LargeImageText,
                ["small_image"] = smallImage,
                ["small_text"] = SmallImageText
            }
        };
    }

    private enum PresenceState
    {
        Idle,
        Editing,
        PlayTesting
    }
}