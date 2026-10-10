using System.Text;
using System.Text.RegularExpressions;

namespace ScriptEditorWebview.Settings;

/// <summary>Which editor a script opens in.</summary>
internal enum EditorMode
{
    /// <summary>The Monaco overlay this mod puts over Studio's script editor.</summary>
    Monaco,

    /// <summary>Studio's own script editor, with nothing of this mod on top of it.</summary>
    Native
}

/// <summary>
///     The mod's settings file, <c>script-editor-webview.toml</c> in the mod's folder. It holds one
///     key, so it is read here with the handful of TOML rules that key needs instead of a parser:
///     comments, blank lines, and <c>key = "value"</c> before the first table.
/// </summary>
internal static partial class EditorSettings
{
    public const string FileName = "script-editor-webview.toml";

    public const EditorMode DefaultMode = EditorMode.Monaco;

    private const string Key = "editor";

    private const string Template =
        """
        # Script Editor Webview
        # Which editor opens your scripts. Saving this file takes effect at once, without
        # restarting Studio, and the "Use the Monaco editor" item in Studio's Mods menu
        # writes this same line.
        #   "monaco"  the VS Code editor this mod puts over Studio's script editor
        #   "native"  Studio's own script editor, with nothing on top of it
        editor = "{0}"

        """;

    /// <summary>
    ///     The mode the file asks for. When the file cannot be used, the mode is
    ///     <see cref="DefaultMode" /> and <paramref name="problem" /> says what was wrong with it.
    /// </summary>
    public static EditorMode Read(string path, out string? problem)
    {
        problem = null;

        string[] lines;
        try
        {
            if (!File.Exists(path))
            {
                problem = $"'{path}' does not exist";
                return DefaultMode;
            }

            lines = ReadAllLinesShared(path);
        }
        catch (Exception ex)
        {
            problem = $"'{path}' could not be read ({ex.Message})";
            return DefaultMode;
        }

        for (var index = 0; index < lines.Length; index++)
        {
            var line = lines[index].Trim();
            if (line.Length == 0 || line[0] == '#') continue;

            // Everything after a table header belongs to that table; the key lives above them.
            if (line[0] == '[') break;

            var assignment = AssignmentPattern().Match(line);
            if (!assignment.Success)
            {
                problem = $"line {index + 1} of '{path}' is not 'key = value': {line}";
                return DefaultMode;
            }

            if (assignment.Groups["key"].Value != Key) continue;

            var value = assignment.Groups["value"].Value;
            if (ModeOf(value) is { } mode) return mode;

            problem = $"line {index + 1} of '{path}' has {Key} = {value}, which is neither \"monaco\" nor \"native\"";
            return DefaultMode;
        }

        problem = $"'{path}' has no '{Key}' key";
        return DefaultMode;
    }

    /// <summary>
    ///     Records the mode. A file that reads cleanly keeps everything else it holds; one that does
    ///     not is replaced by the commented default, so the menu item always leaves a usable file.
    /// </summary>
    public static void Write(string path, EditorMode mode)
    {
        var name = NameOf(mode);
        string content;

        Read(path, out var problem);
        if (problem is null)
        {
            var lines = ReadAllLinesShared(path);
            for (var index = 0; index < lines.Length; index++)
            {
                var assignment = AssignmentPattern().Match(lines[index].Trim());
                if (!assignment.Success || assignment.Groups["key"].Value != Key) continue;

                lines[index] = $"{Key} = \"{name}\"";
                break;
            }

            content = string.Join('\n', lines) + '\n';
        }
        else
        {
            content = string.Format(Template, name).ReplaceLineEndings("\n");
        }

        // Written beside the file and moved over it, so a Studio that dies mid-write leaves either
        // the old settings or the new ones, never half a file.
        var staging = path + ".tmp";
        File.WriteAllText(staging, content, new UTF8Encoding(false));
        File.Move(staging, path, true);
    }

    public static string NameOf(EditorMode mode)
    {
        return mode == EditorMode.Monaco ? "monaco" : "native";
    }

    /// <summary>Something that changes whenever the file does, cheap enough to ask for every second.</summary>
    public static (bool Exists, DateTime WrittenUtc, long Length) StampOf(string path)
    {
        try
        {
            var info = new FileInfo(path);
            return info.Exists ? (true, info.LastWriteTimeUtc, info.Length) : default;
        }
        catch
        {
            return default;
        }
    }

    private static EditorMode? ModeOf(string value)
    {
        if (value.Length < 2 || value[0] != value[^1] || value[0] is not ('"' or '\'')) return null;

        return value[1..^1].Trim().ToLowerInvariant() switch
        {
            "monaco" => EditorMode.Monaco,
            "native" => EditorMode.Native,
            _ => null
        };
    }

    /// <summary>An editor saving the file may still hold it open; reading must not need it closed.</summary>
    private static string[] ReadAllLinesShared(string path)
    {
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        using var reader = new StreamReader(stream, Encoding.UTF8);

        return reader.ReadToEnd().ReplaceLineEndings("\n").TrimEnd('\n').Split('\n');
    }

    [GeneratedRegex("""^(?<key>[A-Za-z0-9_-]+)\s*=\s*(?<value>"[^"]*"|'[^']*'|[^#]*?)\s*(?:#.*)?$""")]
    private static partial Regex AssignmentPattern();
}
