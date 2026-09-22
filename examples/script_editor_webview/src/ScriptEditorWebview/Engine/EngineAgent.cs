using System.Text.Json;
using System.Text.Json.Nodes;
using RML.Core.Api;
using Roblox;

namespace ScriptEditorWebview.Engine;

/// <summary>
///     The .NET side of the engine agent: it loads <c>luau/agent.luau</c> into the Edit data model's
///     Luau VM once and then talks to the table that script returns.
///     Everything this mod does to a script document goes through here, which keeps the mod standing
///     on the documented <c>ScriptEditorService</c> API instead of on the engine's memory layout. The
///     three calls it makes never yield on the engine side, so none of them can stall Studio: writes
///     are queued and applied by a coroutine the engine itself resumes.
/// </summary>
internal sealed class EngineAgent : IDisposable
{
    private readonly string _agentPath;

    private LuauRef? _poll;
    private LuauRef? _push;
    private LuauRef? _resync;
    private LuauRef? _text;
    private LuauRef? _check;
    private LuauRef? _openScratch;
    private LuauRef? _stop;
    private LuauRef? _table;

    public EngineAgent(string agentPath)
    {
        _agentPath = agentPath;
    }

    public bool IsRunning => _table is not null;

    public void Dispose()
    {
        var stop = _stop;
        _stop = null;

        if (stop is not null)
            try
            {
                stop.InvokeAsync();
            }
            catch (Exception ex)
            {
                ScriptEditorWebviewMod.Logger.Debug($"stopping the engine agent failed: {ex.Message}");
            }

        _push?.Dispose();
        _poll?.Dispose();
        _check?.Dispose();
        _text?.Dispose();
        _openScratch?.Dispose();
        _resync?.Dispose();
        stop?.Dispose();
        _table?.Dispose();

        _push = null;
        _poll = null;
        _check = null;
        _text = null;
        _openScratch = null;
        _resync = null;
        _table = null;
    }

    /// <summary>
    ///     Evaluates the agent script and binds its entry points. Returns the reason it could not
    ///     start, or null on success — the caller turns that into one actionable log line.
    /// </summary>
    public async Task<string?> StartAsync()
    {
        if (_table is not null) return null;

        if (!System.IO.File.Exists(_agentPath)) return $"the agent script is missing at '{_agentPath}'";

        if (!LuauScriptManager.IsReady(DataModelType.Edit)) return "the Luau host is not ready yet";

        string source;
        try
        {
            source = await System.IO.File.ReadAllTextAsync(_agentPath);
        }
        catch (Exception ex)
        {
            return $"reading '{_agentPath}' failed: {ex.Message}";
        }

        LuauRef? table;
        try
        {
            var result = await LuauScriptManager.EvaluateAsync(DataModelType.Edit, source, "@rml/monaco/agent");
            table = result.AsRef();
        }
        catch (Exception ex)
        {
            return $"evaluating the agent script failed: {ex.Message}";
        }

        if (table is null) return "the agent script did not return its table";

        var push = (await table.IndexAsync("push")).AsRef();
        var poll = (await table.IndexAsync("poll")).AsRef();
        var check = (await table.IndexAsync("check")).AsRef();
        var text = (await table.IndexAsync("text")).AsRef();
        var openScratch = (await table.IndexAsync("openScratch")).AsRef();
        var resync = (await table.IndexAsync("resync")).AsRef();
        var stop = (await table.IndexAsync("stop")).AsRef();

        if (push is null || poll is null || check is null || text is null || openScratch is null ||
            resync is null || stop is null)
        {
            table.Dispose();
            return "the agent table is missing one of push/poll/check/text/openScratch/resync/stop";
        }

        _table = table;
        _push = push;
        _poll = poll;
        _check = check;
        _text = text;
        _openScratch = openScratch;
        _resync = resync;
        _stop = stop;

        return null;
    }

    /// <summary>Asks the agent to announce every open document again, for the Mods menu reattach.</summary>
    public void Resync()
    {
        if (_resync is null) return;

        try
        {
            _resync.InvokeAsync().ContinueWith(
                task => ScriptEditorWebviewMod.Logger.Error(
                    $"resyncing the open documents failed: {task.Exception?.GetBaseException().Message}"),
                TaskContinuationOptions.OnlyOnFaulted);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"resyncing the open documents failed: {ex.Message}");
        }
    }

    /// <summary>Queues one batch of editor edits. Fire and forget: the engine owns the ordering.</summary>
    public void PushEdits(int documentId, JsonArray edits)
    {
        if (_push is null || edits.Count == 0) return;

        var payload = new JsonObject
        {
            ["id"] = documentId,
            ["edits"] = edits.DeepClone()
        }.ToJsonString();

        try
        {
            _push.InvokeAsync(payload).ContinueWith(
                task => ScriptEditorWebviewMod.Logger.Error(
                    $"queueing an editor edit failed: {task.Exception?.GetBaseException().Message}"),
                TaskContinuationOptions.OnlyOnFaulted);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"queueing an editor edit failed: {ex.Message}");
        }
    }

    /// <summary>Drains everything that happened in the engine since the previous call.</summary>
    public async Task<AgentPoll?> PollAsync()
    {
        if (_poll is null) return null;

        var json = (await _poll.InvokeAsync()).AsString();
        if (string.IsNullOrEmpty(json)) return null;

        try
        {
            return JsonSerializer.Deserialize<AgentPoll>(json);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"the agent returned a payload we cannot read: {ex.Message}");
            return null;
        }
    }

    /// <summary>Text the engine holds for one document, for the sync check the editor can request.</summary>
    public async Task<string?> TextAsync(int documentId)
    {
        if (_text is null) return null;

        try
        {
            return (await _text.InvokeAsync(documentId)).AsString();
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Debug($"reading the document text failed: {ex.Message}");
            return null;
        }
    }

    /// <summary>Opens a scratch script document, so an automated check has something to type into.</summary>
    public void OpenScratchDocument()
    {
        if (_openScratch is null) return;

        try
        {
            _openScratch.InvokeAsync().ContinueWith(
                task => ScriptEditorWebviewMod.Logger.Error(
                    $"opening the scratch document failed: {task.Exception?.GetBaseException().Message}"),
                TaskContinuationOptions.OnlyOnFaulted);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"opening the scratch document failed: {ex.Message}");
        }
    }

    /// <summary>
    ///     Asks the engine to prove that a write reaches this document on this Studio build. The
    ///     answer arrives as a "checked" event on a later poll: the write yields, and a call into
    ///     Luau returns as soon as the thread parks, so a yielding result cannot come back inline.
    /// </summary>
    public void RequestCheck(int documentId)
    {
        if (_check is null) return;

        try
        {
            _check.InvokeAsync(documentId).ContinueWith(
                task => ScriptEditorWebviewMod.Logger.Error(
                    $"asking the engine agent for a self-check failed: {task.Exception?.GetBaseException().Message}"),
                TaskContinuationOptions.OnlyOnFaulted);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"asking the engine agent for a self-check failed: {ex.Message}");
        }
    }
}
