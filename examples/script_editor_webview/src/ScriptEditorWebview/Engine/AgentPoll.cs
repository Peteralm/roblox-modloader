using System.Text.Json.Serialization;

namespace ScriptEditorWebview.Engine;

/// <summary>What the Luau agent reports back on one poll. Mirrors the payload built in agent.luau.</summary>
internal sealed class AgentPoll
{
    [JsonPropertyName("events")] public AgentEvent[] Events { get; init; } = [];

    [JsonPropertyName("applied")] public AgentApplied[] Applied { get; init; } = [];

    [JsonPropertyName("sourcemap")] public string? Sourcemap { get; init; }

    [JsonPropertyName("sourcemapNodes")] public int SourcemapNodes { get; init; }

    /// <summary>The document whose tab is in front, or 0 when no script tab is.</summary>
    [JsonPropertyName("active")] public int Active { get; init; }

    /// <summary>False when Studio refused to say which script is active; <see cref="Active" /> then means nothing.</summary>
    [JsonPropertyName("activeKnown")] public bool ActiveKnown { get; init; }
}

/// <summary>
///     One thing that happened in the engine: a document opened or closed, its text changed, or a
///     write failed. <see cref="Stale" /> marks a text report the editor has already typed past.
/// </summary>
internal sealed class AgentEvent
{
    [JsonPropertyName("kind")] public string Kind { get; init; } = string.Empty;

    [JsonPropertyName("id")] public int Id { get; init; }

    [JsonPropertyName("name")] public string? Name { get; init; }

    [JsonPropertyName("uri")] public string? Uri { get; init; }

    /// <summary>The path luau-lsp knows the script by, relative to the workspace root.</summary>
    [JsonPropertyName("path")] public string? Path { get; init; }

    [JsonPropertyName("text")] public string? Text { get; init; }

    [JsonPropertyName("stale")] public bool Stale { get; init; }

    [JsonPropertyName("ok")] public bool Ok { get; init; }

    [JsonPropertyName("message")] public string? Message { get; init; }

    /// <summary>Bytes a document holds once it has been handed back to Studio's editor.</summary>
    [JsonPropertyName("length")] public int Length { get; init; }

    /// <summary>True when handing a document back had to rewrite all of it to match the editor.</summary>
    [JsonPropertyName("rewritten")] public bool Rewritten { get; init; }
}

/// <summary>How many of the editor's own edits the engine committed for one document.</summary>
internal sealed class AgentApplied
{
    [JsonPropertyName("id")] public int Id { get; init; }

    [JsonPropertyName("count")] public int Count { get; init; }
}
