using System.Text.Json.Nodes;
using RML.Core.Api;
using Roblox;

namespace ScriptEditorWebview;

/// <summary>
///     How the regression checks reach the mod when no editor page is up to talk through, which is
///     exactly the state they have to inspect after a switch to Studio's own editor. It exists only
///     when Studio is started with <c>RML_MONACO_SELFTEST</c> set: a check drops
///     <c>selftest-command.json</c> in the mod's folder and reads the answer from
///     <c>selftest-result.json</c>.
///     <c>eval</c> runs Luau in the Edit data model and returns what it returns, which lets a check
///     read a script's source without going through the agent it is checking; <c>menu</c> does what
///     ticking or unticking the Mods menu item does.
/// </summary>
internal sealed class SelfTestChannel(string directory, Action<bool> toggleMenu)
{
    private readonly string _commandPath = System.IO.Path.Combine(directory, "selftest-command.json");
    private readonly string _resultPath = System.IO.Path.Combine(directory, "selftest-result.json");

    public void Pump()
    {
        if (!System.IO.File.Exists(_commandPath)) return;

        JsonObject? command;
        try
        {
            command = JsonNode.Parse(System.IO.File.ReadAllText(_commandPath)) as JsonObject;
            System.IO.File.Delete(_commandPath);
        }
        catch
        {
            // Still being written; the next tick reads it whole.
            return;
        }

        var id = command?["id"]?.GetValue<int>() ?? 0;

        switch (command?["action"]?.GetValue<string>())
        {
            case "menu":
                toggleMenu(command["monaco"]?.GetValue<bool>() ?? true);
                Answer(id, true, string.Empty);
                break;

            case "eval":
                LuauScriptManager.EvaluateAsync(DataModelType.Edit, command["source"]?.GetValue<string>() ?? string.Empty,
                        "@rml/monaco/selftest")
                    .ContinueWith(task => Answer(id, !task.IsFaulted,
                        task.IsFaulted
                            ? task.Exception?.GetBaseException().Message ?? "failed"
                            : task.Result.AsString() ?? string.Empty));
                break;

            default:
                Answer(id, false, "unknown action");
                break;
        }
    }

    private void Answer(int id, bool ok, string value)
    {
        var result = new JsonObject
        {
            ["id"] = id,
            ["ok"] = ok,
            ["value"] = value
        };

        try
        {
            var staging = _resultPath + ".tmp";
            System.IO.File.WriteAllText(staging, result.ToJsonString());
            System.IO.File.Move(staging, _resultPath, true);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Warn($"writing the self-test answer failed: {ex.Message}");
        }
    }
}
