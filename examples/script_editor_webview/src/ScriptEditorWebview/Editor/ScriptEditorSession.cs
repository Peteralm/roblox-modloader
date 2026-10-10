using System.Text.Json.Nodes;
using RML.Core.Api;
using ScriptEditorWebview.Engine;
using ScriptEditorWebview.Lsp;
using ScriptEditorWebview.Native;
using ScriptEditorWebview.Threading;
using ScriptEditorWebview.WebView;

namespace ScriptEditorWebview.Editor;

/// <summary>
///     One Monaco instance over one native script editor. The session never touches the engine: it
///     hands the editor's edits to <see cref="EngineAgent" /> and receives the document's text back
///     from the poll loop, so the only Roblox contract it depends on lives in agent.luau.
/// </summary>
internal sealed class ScriptEditorSession : IDisposable
{
    /// <summary>
    ///     Root of the workspace luau-lsp is told about. Nothing exists there: the documents live in
    ///     the editor and the tree of the place comes from the engine agent.
    /// </summary>
    private const string WorkspaceRoot = "file:///rml";

    private readonly EngineAgent _agent;
    private readonly GuiDispatcher _gui;
    private readonly LuauLspBridge _lsp;
    private readonly string _uri;
    private readonly string _webRoot;

    private readonly WebViewHost _webView;
    private bool _disposed;
    /// <summary>True once the page holds the document's text and is on screen.</summary>
    private bool _editorSynced;

    private string _lastSyncedText = string.Empty;
    private volatile bool _lspReady;
    private string? _pendingText;
    private string? _sourcemap;

    public ScriptEditorSession(GuiDispatcher gui, EngineAgent agent, int documentId, string name, string path,
        string text, IntPtr editorHwnd, ModContext context)
    {
        _gui = gui;
        _agent = agent;
        DocumentId = documentId;
        Name = name;

        // The agent gives every open script a path and puts the same one in the tree it sends to
        // luau-lsp; a document opened under it is how the server learns what `script` is.
        _uri = string.IsNullOrEmpty(path) ? $"{WorkspaceRoot}/main.luau" : $"{WorkspaceRoot}/{path}";
        _pendingText = text;
        EditorHwnd = editorHwnd;
        _webRoot = Path.Combine(context.AssemblyDirectory, "web");

        _webView = new WebViewHost(gui);
        _lsp = new LuauLspBridge(
            Path.Combine(context.AssemblyDirectory, "tools", "bin", "luau-lsp.exe"),
            Path.Combine(context.AssemblyDirectory, "tools", "cache", "globalTypes.PluginSecurity.d.luau"));
    }

    public int DocumentId { get; }

    /// <summary>Full name of the script this session mirrors, for logging.</summary>
    public string Name { get; }

    public IntPtr EditorHwnd { get; }

    public void Dispose()
    {
        if (_disposed) return;

        _disposed = true;

        _webView.MessageReceived -= OnWebMessage;
        _lsp.ServerMessage -= OnLspServerMessage;
        _lsp.DocumentOpened -= OnLspDocumentOpened;

        _lsp.Dispose();
        _webView.Dispose();
    }

    public void Start()
    {
        _webView.MessageReceived += OnWebMessage;

        _lsp.ServerMessage += OnLspServerMessage;
        _lsp.DocumentOpened += OnLspDocumentOpened;
        _lsp.Start();

        Win32.EnsureClipChildren(EditorHwnd);

        _webView.Initialize(EditorHwnd, _webRoot, $"index.html?uri={Uri.EscapeDataString(_uri)}");
    }

    public void SyncBounds()
    {
        _webView.SyncBounds();
    }

    /// <summary>Text the engine reports for this document, on its way to Monaco.</summary>
    public void OnEngineText(string text)
    {
        if (_disposed) return;

        // Until the page has taken its first text it reads the document from the engine itself,
        // which is newer than anything a report still in flight can say.
        if (!_editorSynced)
        {
            _pendingText = text;
            return;
        }

        PushTextToEditor(text);
    }

    /// <summary>
    ///     Tells the editor how many of its edits the engine committed. Monaco counts the ones it is
    ///     still owed and ignores a full-text push while any is outstanding, which is what keeps a
    ///     report that crossed a keystroke from rolling the buffer back.
    /// </summary>
    public void OnEditsApplied(int count)
    {
        if (_disposed || count <= 0) return;

        for (var i = 0; i < count; i++) _webView.PostMessage("{\"type\":\"editor.applied\"}");
    }

    public void PushSourcemap(string? sourcemap)
    {
        if (sourcemap is { Length: > 0 }) _sourcemap = sourcemap;

        if (_disposed || !_lspReady || _sourcemap is null or { Length: 0 }) return;

        try
        {
            _lsp.SendNotification("$/plugin/full", _sourcemap);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Debug($"pushing sourcemap failed: {ex.Message}");
        }
    }

    /// <summary>
    ///     Starts handing the document back to Studio's own editor: the page stops taking input and
    ///     sends everything it holds, which the engine is then asked to match. Returns false when
    ///     the page never got as far as showing the document, so there is nothing of its own to save
    ///     and the session can simply go.
    /// </summary>
    public bool BeginHandover()
    {
        if (_disposed || !_editorSynced) return false;

        _webView.PostMessage("{\"type\":\"editor.freeze\"}");
        return true;
    }

    /// <summary>Gives the page back to the user after a handover that did not go through.</summary>
    public void CancelHandover()
    {
        if (_disposed) return;

        _webView.PostMessage("{\"type\":\"editor.thaw\"}");
    }

    private void OnLspDocumentOpened()
    {
        _lspReady = true;
        PushSourcemap(null);
    }

    private void OnWebMessage(string raw)
    {
        JsonObject? message;
        try
        {
            message = JsonNode.Parse(raw) as JsonObject;
        }
        catch
        {
            return;
        }

        var type = message?["type"]?.GetValue<string>();
        switch (type)
        {
            // The text the mod was handed when the document opened can be seconds old by now, and
            // whoever was typing in Studio's editor meanwhile must not find those keystrokes gone,
            // so the page's first text is read from the engine at this moment.
            case "editor.ready":
                _agent.TextAsync(DocumentId).ContinueWith(task =>
                {
                    var current = task.IsFaulted ? null : task.Result;

                    _gui.Post(() =>
                    {
                        if (_disposed) return;

                        var text = current ?? _pendingText ?? string.Empty;
                        _pendingText = null;
                        PushTextToEditor(text, true, null, true);
                    });
                }, TaskContinuationOptions.ExecuteSynchronously);

                break;

            // The page holds the document: only now does it cover Studio's editor.
            case "editor.synced":
                _gui.Post(() =>
                {
                    if (_disposed || _editorSynced) return;

                    _editorSynced = true;
                    _webView.Show();

                    if (_pendingText is not { } newer) return;

                    _pendingText = null;
                    PushTextToEditor(newer);
                });
                break;

            // The frozen page's last word: everything it holds, for the engine to match.
            case "editor.final":
                if (message?["text"]?.GetValue<string>() is { } final) _agent.Settle(DocumentId, final);

                break;

            // The editor asks for the engine's own copy of the document; a regression check compares
            // the two buffers to prove a Studio update did not break the round trip.
            case "editor.verify":
                _agent.TextAsync(DocumentId).ContinueWith(task =>
                {
                    var text = task.IsFaulted ? null : task.Result;
                    var envelope = new JsonObject
                    {
                        ["type"] = "editor.engineText",
                        ["text"] = text ?? string.Empty
                    };
                    _webView.PostMessage(envelope.ToJsonString());
                }, TaskContinuationOptions.ExecuteSynchronously);

                break;

            // The editor missed a text report while it still owed the engine edits; it is asking
            // for the current document now that it owes nothing. The answer echoes how many edits
            // the editor had sent when it asked, so it can drop an answer that crossed newer typing.
            case "editor.resync":
                var sent = message?["sent"]?.GetValue<int>();
                _agent.TextAsync(DocumentId).ContinueWith(task =>
                {
                    if (task.IsFaulted || task.Result is not { } current) return;

                    _gui.Post(() => PushTextToEditor(current, true, sent));
                }, TaskContinuationOptions.ExecuteSynchronously);

                break;

            case "editor.edits":
                if (message?["edits"] is JsonArray edits && edits.Count > 0)
                    _agent.PushEdits(DocumentId, edits);

                break;

            case "lsp.message":
                var payload = message?["data"];
                if (payload is not null) _lsp.SendToServer(payload.ToJsonString());

                break;
        }
    }

    private void OnLspServerMessage(string json)
    {
        var envelope = $"{{\"type\":\"lsp.message\",\"data\":{json}}}";
        _webView.PostMessage(envelope);
    }

    private void PushTextToEditor(string text, bool force = false, int? sent = null, bool initial = false)
    {
        if (!force && text == _lastSyncedText) return;

        _lastSyncedText = text;

        var envelope = new JsonObject
        {
            ["type"] = "editor.setText",
            ["text"] = text,
            ["uri"] = _uri,
            ["remote"] = true
        };
        if (sent is { } count) envelope["sent"] = count;
        if (initial) envelope["initial"] = true;
        _webView.PostMessage(envelope.ToJsonString());
    }
}
