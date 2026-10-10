using System.Diagnostics;
using System.Text.Json.Nodes;
using RML.Core.Api;
using RML.Core.Modding;
using RML.Logging;
using Roblox;
using ScriptEditorWebview.Editor;
using ScriptEditorWebview.Engine;
using ScriptEditorWebview.Native;
using ScriptEditorWebview.Qt;
using ScriptEditorWebview.Settings;
using ScriptEditorWebview.Threading;

namespace ScriptEditorWebview;

/// <summary>
///     Puts a Monaco editor over Studio's native script editor.
///     The engine half lives in <c>luau/agent.luau</c> and is driven from <see cref="EngineAgent" />;
///     this class owns only what Luau cannot reach: the Qt window to cover, the WebView on top of it,
///     and the language server beside it.
/// </summary>
[Mod("script-editor-webview", "0.1.0", Author = "Revolution", Description = "Monaco Editor on Roblox")]
public sealed class ScriptEditorWebviewMod : ModBase, IDataModelAware
{
    private const int ReconcileIntervalMs = 200;

    /// <summary>Ticks a document waits for a freshly created editor window before we guess one.</summary>
    private const int WidgetGraceTicks = 3;

    /// <summary>Ticks we give the engine to answer a self-check before treating it as broken.</summary>
    private const int CheckTimeoutTicks = 25;

    /// <summary>Self-checks a document may leave unanswered before its editor is left to Studio.</summary>
    private const int CheckAttempts = 3;

    /// <summary>Ticks between two looks at the settings file: once a second.</summary>
    private const int SettingsPollTicks = 5;

    /// <summary>Ticks every document gets to confirm its text when the overlays are being taken down.</summary>
    private const int HandoverTimeoutTicks = 50;

    public new static readonly ILogger Logger = Log.CreateLogger("ScriptEditorWebview");

    private static readonly string[] ScriptEditorClassNames =
    [
        "StudioScriptEditor",
        "RBX::ScriptEditor::ScriptEditor",
        "ScriptTextEditorWidget"
    ];

    private readonly Dictionary<IntPtr, bool> _knownWidgets = [];
    private readonly Queue<IntPtr> _newWidgets = new();
    private readonly List<PendingDocument> _pending = [];
    private readonly Dictionary<int, ScriptEditorSession> _sessions = [];

    /// <summary>The document whose tab is in front, as of the last poll; 0 when no script tab is.</summary>
    private int _activeDocument;

    private bool _activeDocumentKnown;

    private EngineAgent? _agent;
    private string? _agentFailure;
    private bool _agentStarting;
    private bool _editDataModelLive;

    private GuiDispatcher? _gui;

    /// <summary>
    ///     Not null while the overlays are being taken down for Studio's own editor: the documents
    ///     that have not confirmed their text yet, and the tick the wait ends at.
    /// </summary>
    private Handover? _handover;

    private IMenuNode? _menu;
    private EditorMode _mode = EditorSettings.DefaultMode;
    private bool _pollInFlight;
    private Timer? _reconcileTimer;
    private SelfTestChannel? _selfTest;
    private string _settingsPath = string.Empty;
    private string? _settingsProblem;
    private (bool Exists, DateTime WrittenUtc, long Length) _settingsStamp;
    private string? _sourcemap;
    private int _tick;

    public void OnDataModelLoaded(DataModel game, DataModelType dataModelType)
    {
        if (dataModelType != DataModelType.Edit) return;

        _editDataModelLive = true;
        Logger.Info("edit data model is live; starting the engine agent");
    }

    public void OnDataModelUnloaded(DataModel game, DataModelType dataModelType)
    {
        if (dataModelType != DataModelType.Edit) return;

        _editDataModelLive = false;

        _gui?.Post(() =>
        {
            DisposeAllSessions();
            _pending.Clear();
            _knownWidgets.Clear();
            _newWidgets.Clear();
            _handover = null;

            _agent?.Dispose();
            _agent = null;
            _agentFailure = null;
        });
    }

    public override int OnLoad()
    {
        _settingsPath = System.IO.Path.Combine(Context.Directory, EditorSettings.FileName);
        LoadSettings(true);

        try
        {
            BuildMenu();
        }
        catch (Exception ex)
        {
            Logger.Error($"failed to register the Mods menu items: {ex.Message}");
        }

        if (Environment.GetEnvironmentVariable("RML_MONACO_SELFTEST") is { Length: > 0 })
            _selfTest = new SelfTestChannel(Context.Directory, OnMenuToggled);

        WarnWhenTypesAreForAnotherStudio();

        _gui = new GuiDispatcher();
        _reconcileTimer = new Timer(_ => OnReconcileTick(), null, TimeSpan.FromMilliseconds(250),
            TimeSpan.FromMilliseconds(ReconcileIntervalMs));

        Logger.Info($"loaded from '{Context.Directory}', editor = \"{EditorSettings.NameOf(_mode)}\"");
        return 0;
    }

    public override void OnUnload()
    {
        _reconcileTimer?.Dispose();
        _reconcileTimer = null;

        _menu?.Dispose();
        _menu = null;

        DisposeAllSessions();

        _agent?.Dispose();
        _agent = null;

        _gui?.Dispose();
        _gui = null;

        Logger.Info("unloaded");
    }

    /// <summary>
    ///     The completions come from a definitions file built for one Studio build
    ///     (<c>eng/update-roblox-types.mjs</c>). Studio updates weekly, so the day the two drift
    ///     apart is said out loud instead of showing up as members that quietly are not there.
    /// </summary>
    private void WarnWhenTypesAreForAnotherStudio()
    {
        const string Regenerate = "regenerate them with: node eng/update-roblox-types.mjs --studio <this Studio's folder>";

        try
        {
            var studio = FileVersionInfo.GetVersionInfo(Environment.ProcessPath ?? string.Empty).FileVersion?
                .Replace(", ", ".", StringComparison.Ordinal);

            var record = System.IO.Path.Combine(Context.AssemblyDirectory, "tools", "cache", "roblox-types.json");
            if (!System.IO.File.Exists(record))
            {
                Logger.Warn($"the Roblox type definitions do not say which Studio they describe; {Regenerate}");
                return;
            }

            var built = JsonNode.Parse(System.IO.File.ReadAllText(record))?["studioVersion"]?.GetValue<string>();
            if (built == studio) return;

            Logger.Warn(
                $"the Roblox type definitions describe Studio {built}, this is Studio {studio}: completions may miss what changed; {Regenerate}");
        }
        catch (Exception ex)
        {
            Logger.Warn($"could not tell which Studio the Roblox type definitions describe: {ex.Message}");
        }
    }

    private void OnReconcileTick()
    {
        var gui = _gui;
        if (gui is null) return;

        if (!gui.IsReady)
        {
            gui.EnsureStarted(() => Logger.Info("GUI dispatcher online"));
            return;
        }

        gui.Post(Reconcile);
    }

    private void Reconcile()
    {
        if (_gui is null) return;

        _tick++;
        if (_tick % SettingsPollTicks == 0) PollSettingsFile();

        _selfTest?.Pump();

        if (_handover is not null) DriveHandover();

        // An overlay only outlives the switch to Studio's editor when its document would not
        // confirm its text; it keeps working until it does, and nothing new is covered meanwhile.
        if (_handover is not null || _mode == EditorMode.Native)
        {
            if (_sessions.Count > 0)
            {
                BeginPoll();
                foreach (var session in _sessions.Values) session.SyncBounds();
            }
            else if (_handover is null)
            {
                StopAgent();
            }

            return;
        }

        if (!EnsureAgent()) return;

        TrackEditorWidgets();
        BeginPoll();
        AttachPendingDocuments();

        foreach (var session in _sessions.Values) session.SyncBounds();
    }

    /// <summary>
    ///     Reads the settings file and applies the mode it asks for. A file that cannot be used
    ///     means the default, and says so once per problem rather than once per look.
    /// </summary>
    private void LoadSettings(bool initial)
    {
        var mode = EditorSettings.Read(_settingsPath, out var problem);

        if (problem is not null && problem != _settingsProblem)
        {
            Logger.Warn(
                $"{problem}; using the default, editor = \"{EditorSettings.NameOf(EditorSettings.DefaultMode)}\"");

            // A missing file is written out so there is something to edit; one that is there but
            // wrong is left alone, it is the user's text.
            if (!System.IO.File.Exists(_settingsPath))
                try
                {
                    EditorSettings.Write(_settingsPath, EditorSettings.DefaultMode);
                }
                catch (Exception ex)
                {
                    Logger.Warn($"could not create '{_settingsPath}': {ex.Message}");
                }
        }

        _settingsProblem = problem;
        _settingsStamp = EditorSettings.StampOf(_settingsPath);

        if (initial)
        {
            _mode = mode;
            return;
        }

        if (mode == _mode) return;

        SetMode(mode, EditorSettings.FileName);

        // The loader has no way to tick an item from outside, so the menu is rebuilt with the new
        // state; that is what keeps it in step with a change made in the file.
        try
        {
            BuildMenu();
        }
        catch (Exception ex)
        {
            Logger.Error($"failed to refresh the Mods menu items: {ex.Message}");
        }
    }

    private void PollSettingsFile()
    {
        if (EditorSettings.StampOf(_settingsPath) == _settingsStamp) return;

        LoadSettings(false);
    }

    private void BuildMenu()
    {
        _menu?.Dispose();

        _menu = ModsMenu.AddSubmenu("Script Editor Webview");
        _menu.AddCheckable("Use the Monaco editor", _mode == EditorMode.Monaco, OnMenuToggled);
        _menu.AddAction("Reattach", RequestReattach);
    }

    /// <summary>The menu item writes the settings file, so the file and the menu say the same thing.</summary>
    private void OnMenuToggled(bool useMonaco)
    {
        var mode = useMonaco ? EditorMode.Monaco : EditorMode.Native;

        _gui?.Post(() =>
        {
            try
            {
                EditorSettings.Write(_settingsPath, mode);
                _settingsProblem = null;
                _settingsStamp = EditorSettings.StampOf(_settingsPath);
            }
            catch (Exception ex)
            {
                Logger.Error(
                    $"could not record editor = \"{EditorSettings.NameOf(mode)}\" in '{_settingsPath}', so it lasts until Studio closes: {ex.Message}");
            }

            SetMode(mode, "the Mods menu");
        });
    }

    private void SetMode(EditorMode mode, string source)
    {
        if (mode == _mode) return;

        _mode = mode;
        Logger.Info($"editor = \"{EditorSettings.NameOf(mode)}\", set from {source}");

        // A handover still running is left to finish first; what happens next is decided there.
        if (_handover is not null) return;

        if (mode == EditorMode.Native)
            BeginHandover();
        else if (_sessions.Count > 0)
            ResumeKeptSessions();

        // Otherwise going back to Monaco needs nothing here: the next tick starts the agent again,
        // and it announces every open document.
    }

    /// <summary>
    ///     Starts taking every overlay down. Each page is frozen and asked for its final text, the
    ///     engine is asked to hold exactly that, and the overlay goes only once it says it does:
    ///     Studio's editor shows the same document, so what it shows is what Monaco had.
    /// </summary>
    private void BeginHandover()
    {
        // A document that never got its overlay has nothing of Monaco's to save.
        _pending.Clear();
        _newWidgets.Clear();

        var waiting = new HashSet<int>();

        foreach (var (id, session) in _sessions.ToArray())
        {
            if (session.BeginHandover())
            {
                waiting.Add(id);
                continue;
            }

            RemoveSession(id);
        }

        _handover = new Handover(waiting, _tick + HandoverTimeoutTicks);
    }

    private void DriveHandover()
    {
        if (_handover is not { } handover) return;

        if (handover.Waiting.Count > 0 && _tick < handover.DeadlineTick) return;

        // A document the engine has not answered for keeps its overlay, still frozen: its text is
        // safe in the page, and the answer finishes the job whenever it comes.
        foreach (var id in handover.Waiting)
            if (_sessions.TryGetValue(id, out var session))
                Logger.Error(
                    $"'{session.Name}' did not confirm its text within {HandoverTimeoutTicks * ReconcileIntervalMs / 1000}s; its overlay stays, read-only, until the engine answers or the editor is switched back");

        _handover = null;

        if (_sessions.Count == 0)
        {
            // Also when Monaco was selected again meanwhile: a fresh agent announces every open
            // document, which is how the overlays come back.
            StopAgent();

            if (_mode == EditorMode.Native)
                Logger.Info("Studio's own editor is in charge: no overlay, no engine agent");

            return;
        }

        if (_mode == EditorMode.Monaco) ResumeKeptSessions();
    }

    /// <summary>
    ///     Monaco is selected again while some overlays were never taken down. They go back to
    ///     work, and the agent announces the documents whose overlays did go.
    /// </summary>
    private void ResumeKeptSessions()
    {
        foreach (var session in _sessions.Values) session.CancelHandover();

        _agent?.Resync();
    }

    private void OnSettled(AgentEvent report)
    {
        _handover?.Waiting.Remove(report.Id);

        // Studio's editor is no longer what was asked for: the overlay stays where it is.
        if (_mode != EditorMode.Native) return;

        if (!_sessions.TryGetValue(report.Id, out var session)) return;

        if (!report.Ok)
        {
            Logger.Error(
                $"'{session.Name}' keeps its overlay, its text could not be handed to Studio's editor: {report.Message}");
            session.CancelHandover();
            return;
        }

        Logger.Info(
            $"handed '{session.Name}' to Studio's editor: {report.Length} bytes{(report.Rewritten ? ", rewritten in one piece to match the editor" : string.Empty)}");
        RemoveSession(report.Id);
    }

    private void StopAgent()
    {
        if (_agent is null) return;

        _agent.Dispose();
        _agent = null;
        _agentFailure = null;
        _knownWidgets.Clear();
        _newWidgets.Clear();
        _sourcemap = null;
    }

    private void RemoveSession(int documentId)
    {
        if (!_sessions.Remove(documentId, out var session)) return;

        _knownWidgets.Remove(session.EditorHwnd);
        session.Dispose();
    }

    /// <summary>
    ///     Brings the Luau agent up once the Edit data model and the Luau host are both there. A
    ///     failure is reported once per reason, so a Studio build that broke the contract leaves one
    ///     actionable line instead of a line every tick.
    /// </summary>
    private bool EnsureAgent()
    {
        if (_agent is { IsRunning: true }) return true;

        if (!_editDataModelLive || _agentStarting) return false;

        var agent = _agent ??= new EngineAgent(System.IO.Path.Combine(Context.AssemblyDirectory, "luau", "agent.luau"));
        _agentStarting = true;

        agent.StartAsync().ContinueWith(task =>
        {
            var failure = task.IsFaulted ? task.Exception?.GetBaseException().Message : task.Result;

            _gui?.Post(() =>
            {
                _agentStarting = false;

                // The editor was switched off, or the place closed, while the agent was starting.
                if (!ReferenceEquals(_agent, agent))
                {
                    agent.Dispose();
                    return;
                }

                if (failure is null)
                {
                    _agentFailure = null;
                    Logger.Info("engine agent running: the script editor is driven from Luau");

                    // The regression check needs a document to type into and cannot click one open.
                    if (_selfTest is not null)
                    {
                        Logger.Info("RML_MONACO_SELFTEST is set: opening the scratch script documents");
                        agent.OpenScratchDocument();
                    }

                    return;
                }

                if (_agentFailure == failure) return;

                _agentFailure = failure;
                Logger.Error($"the engine agent could not start: {failure}");
            });
        }, TaskContinuationOptions.ExecuteSynchronously);

        return false;
    }

    private void BeginPoll()
    {
        if (_pollInFlight || _agent is not { } agent) return;

        _pollInFlight = true;

        agent.PollAsync().ContinueWith(task =>
        {
            var payload = task.IsFaulted ? null : task.Result;

            if (task.IsFaulted)
                Logger.Error($"polling the engine agent failed: {task.Exception?.GetBaseException().Message}");

            _gui?.Post(() =>
            {
                _pollInFlight = false;

                // An answer from an agent that has been stopped since describes documents nobody holds.
                if (payload is not null && ReferenceEquals(_agent, agent)) Consume(payload);
            });
        }, TaskContinuationOptions.ExecuteSynchronously);
    }

    private void Consume(AgentPoll payload)
    {
        _activeDocument = payload.Active;
        _activeDocumentKnown = payload.ActiveKnown;

        // Acknowledgements first: a text report in the same payload describes the document after
        // those writes landed, and the editor only trusts a full-text push once it owes nothing.
        foreach (var applied in payload.Applied)
            if (_sessions.TryGetValue(applied.Id, out var acknowledged))
                acknowledged.OnEditsApplied(applied.Count);

        // Only the newest text report per document may reach the editor. An older one in the same
        // payload predates writes the acknowledgements above already confirmed, so the editor would
        // take it as current and roll the buffer back for a frame.
        var newestText = new Dictionary<int, AgentEvent>();
        foreach (var report in payload.Events)
            if (report is { Kind: "text", Stale: false, Text: not null })
                newestText[report.Id] = report;

        foreach (var report in payload.Events)
            switch (report.Kind)
            {
                case "open":
                    OnDocumentOpened(report);
                    break;

                case "close":
                    OnDocumentClosed(report.Id);
                    break;

                case "text":
                    if (newestText.TryGetValue(report.Id, out var newest) && ReferenceEquals(newest, report) &&
                        _sessions.TryGetValue(report.Id, out var textTarget))
                        textTarget.OnEngineText(report.Text!);

                    break;

                case "error":
                    Logger.Error($"the engine agent reported: {report.Message}");
                    break;

                case "checked":
                    FinishAttach(report.Id, report.Ok, report.Message);
                    break;

                case "settled":
                    OnSettled(report);
                    break;
            }

        if (payload.Sourcemap is { Length: > 0 } sourcemap)
        {
            _sourcemap = sourcemap;
            Logger.Info($"sourcemap ready: {payload.SourcemapNodes} instances, {sourcemap.Length} bytes");
            foreach (var session in _sessions.Values) session.PushSourcemap(sourcemap);
        }
    }

    private void OnDocumentOpened(AgentEvent report)
    {
        // Studio's editor is in charge, or about to be: a document opened now stays with it.
        if (_mode == EditorMode.Native || _handover is not null) return;

        if (_sessions.ContainsKey(report.Id) || _pending.Any(document => document.Id == report.Id)) return;

        _pending.Add(new PendingDocument(report.Id, report.Name ?? "<script>", report.Path ?? string.Empty,
            report.Text ?? string.Empty));
    }

    private void OnDocumentClosed(int documentId)
    {
        _pending.RemoveAll(document => document.Id == documentId);
        _handover?.Waiting.Remove(documentId);

        RemoveSession(documentId);
    }

    /// <summary>
    ///     Keeps the set of editor windows Studio currently has, and queues the ones that appeared
    ///     since the last tick. A document is paired with the window that showed up with it, which
    ///     beats picking the largest one: the largest visible editor is whichever tab you were on.
    /// </summary>
    private void TrackEditorWidgets()
    {
        foreach (var key in _knownWidgets.Keys.ToArray()) _knownWidgets[key] = false;

        foreach (var widget in QApplication.FindWidgets(IsScriptEditorWidget))
        {
            var hwnd = widget.WinId();
            if (hwnd == IntPtr.Zero) continue;

            if (_knownWidgets.ContainsKey(hwnd))
            {
                _knownWidgets[hwnd] = true;
                continue;
            }

            _knownWidgets[hwnd] = true;

            if (IsUsableEditorWindow(hwnd)) _newWidgets.Enqueue(hwnd);
        }

        foreach (var (hwnd, present) in _knownWidgets.ToArray())
            if (!present)
                _knownWidgets.Remove(hwnd);
    }

    private void AttachPendingDocuments()
    {
        if (_pending.Count == 0 || _agent is null) return;

        foreach (var document in _pending.ToArray())
        {
            document.Ticks++;

            if (document.Checking)
            {
                // The engine answers a check on its own thread; if it never does, the build is
                // broken in a way the overlay must not paper over. One silence is not that: a
                // check asked while Studio is still opening its first documents can sit for
                // longer than this, so the document is asked again before it is given up on.
                if (document.Ticks - document.CheckedAtTick <= CheckTimeoutTicks) continue;

                if (++document.ChecksUnanswered >= CheckAttempts)
                {
                    Refuse(document, "the engine never answered the self-check");
                    continue;
                }

                document.Checking = false;
                document.Hwnd = IntPtr.Zero;

                continue;
            }

            if (!MayTakeWindow(document)) continue;

            var hwnd = TakeEditorWindow(document.Ticks);
            if (hwnd == IntPtr.Zero) continue;

            document.Checking = true;
            document.CheckedAtTick = document.Ticks;
            document.Hwnd = hwnd;

            _agent.RequestCheck(document.Id);
        }
    }

    /// <summary>
    ///     Only the editor window of the tab in front is visible, so only the document Studio calls
    ///     active can be the one it shows. With several documents waiting — every open script, after
    ///     a switch back from Studio's editor — handing the visible window to the first in line put
    ///     one script's text over another script's tab. The others wait for their tab to come up.
    /// </summary>
    private bool MayTakeWindow(PendingDocument document)
    {
        if (!_activeDocumentKnown) return true;

        if (_activeDocument != 0) return document.Id == _activeDocument;

        // No script tab is in front. A lone document can only belong to the window that appears
        // with it; with more than one there is no telling, and the next poll will.
        return _pending.Count == 1;
    }

    /// <summary>
    ///     Puts the overlay up only after a write has round-tripped through the engine on this build.
    ///     A failed check leaves Studio's own editor in place, which still works.
    /// </summary>
    private void FinishAttach(int documentId, bool ok, string? message)
    {
        var document = _pending.FirstOrDefault(candidate => candidate.Id == documentId);
        if (document is null) return;

        document.Checking = false;

        if (!ok)
        {
            Refuse(document, message ?? "the self-check failed for an unknown reason");
            return;
        }

        // The window was picked when the check was asked for, on what the previous poll said was the
        // active document. This answer comes with a newer word on that: if another tab came to the
        // front in between, the window picked may be that tab's, and the document waits its turn.
        if (_gui is null || _agent is null || !IsUsableEditorWindow(document.Hwnd) || !MayTakeWindow(document))
        {
            document.Hwnd = IntPtr.Zero;
            return;
        }

        var session = new ScriptEditorSession(_gui, _agent, document.Id, document.Name, document.Path, document.Text,
            document.Hwnd, Context);

        _pending.Remove(document);
        _sessions[document.Id] = session;

        session.Start();
        session.PushSourcemap(_sourcemap);

        Logger.Info($"attached editor overlay to '{document.Name}' (hwnd 0x{document.Hwnd:X})");
    }

    private void Refuse(PendingDocument document, string reason)
    {
        _pending.Remove(document);
        Logger.Error(
            $"refusing to cover the editor of '{document.Name}': writing to a script document does not work on this Studio build ({reason})");
    }

    /// <summary>
    ///     The window that appeared with this document, or — once a document has waited out the
    ///     grace period without a new window showing up, which is what happens for the documents
    ///     already open when the mod loads — the largest editor window nobody covers yet.
    /// </summary>
    private IntPtr TakeEditorWindow(int ticks)
    {
        while (_newWidgets.Count > 0)
        {
            var candidate = _newWidgets.Dequeue();
            if (IsUsableEditorWindow(candidate) && !IsAttached(candidate)) return candidate;
        }

        if (ticks < WidgetGraceTicks) return IntPtr.Zero;

        var best = IntPtr.Zero;
        long bestArea = -1;

        foreach (var hwnd in _knownWidgets.Keys)
        {
            if (IsAttached(hwnd) || !Win32.GetClientRect(hwnd, out var rect) || !IsUsableEditorWindow(hwnd)) continue;

            var area = (long)rect.Width * rect.Height;
            if (area <= bestArea) continue;

            bestArea = area;
            best = hwnd;
        }

        return best;
    }

    private bool IsAttached(IntPtr hwnd)
    {
        if (_sessions.Values.Any(session => session.EditorHwnd == hwnd)) return true;

        return _pending.Any(document => document.Hwnd == hwnd);
    }

    private static bool IsUsableEditorWindow(IntPtr hwnd)
    {
        if (hwnd == IntPtr.Zero || !Win32.IsWindowVisible(hwnd)) return false;

        if (!Win32.GetClientRect(hwnd, out var rect)) return false;

        return rect.Width is >= 200 and < 32000 && rect.Height is >= 120 and < 32000;
    }

    private static bool IsScriptEditorWidget(QWidget widget)
    {
        var className = widget.ClassName;
        if (string.IsNullOrEmpty(className)) return false;

        return ScriptEditorClassNames.Any(name => className == name || widget.Inherits(name)) ||
               className.Contains("ScriptEditor", StringComparison.Ordinal);
    }

    private void RequestReattach()
    {
        _gui?.Post(() =>
        {
            if (_mode == EditorMode.Native || _handover is not null)
            {
                Logger.Info("reattach requested from the Mods menu, but Studio's own editor is selected");
                return;
            }

            DisposeAllSessions();
            _pending.Clear();
            _knownWidgets.Clear();
            _newWidgets.Clear();

            _agent?.Resync();

            Logger.Info("reattach requested from the Mods menu");
        });
    }

    private void DisposeAllSessions()
    {
        foreach (var session in _sessions.Values) session.Dispose();

        _sessions.Clear();
    }

    /// <summary>A document the engine announced, waiting for the window it belongs to.</summary>
    private sealed class PendingDocument(int id, string name, string path, string text)
    {
        public readonly int Id = id;
        public readonly string Name = name;
        public readonly string Path = path;
        public readonly string Text = text;

        public bool Checking;
        public int CheckedAtTick;
        public int ChecksUnanswered;
        public IntPtr Hwnd;
        public int Ticks;
    }

    /// <summary>The overlays being taken down: who still owes a confirmation, and until when.</summary>
    private sealed class Handover(HashSet<int> waiting, int deadlineTick)
    {
        public readonly int DeadlineTick = deadlineTick;
        public readonly HashSet<int> Waiting = waiting;
    }
}
