using System.Drawing;
using Microsoft.Web.WebView2.Core;
using ScriptEditorWebview.Native;
using ScriptEditorWebview.Threading;

namespace ScriptEditorWebview.WebView;

internal sealed class WebViewHost(GuiDispatcher gui) : IDisposable
{
    private const string VirtualHost = "rml.scripteditor";

    private CoreWebView2Controller? _controller;
    private Rectangle _lastBounds;
    private bool _disposed;
    private string _page = "index.html";
    private IntPtr _parentHwnd;
    private bool _shown;
    private string _webRootPath = string.Empty;
    private CoreWebView2? _webView;

    public bool IsReady { get; private set; }

    public void Dispose()
    {
        if (_disposed) return;

        _disposed = true;
        IsReady = false;

        gui.Post(() =>
        {
            if (_webView is not null)
            {
                _webView.WebMessageReceived -= OnWebMessageReceived;
                _webView = null;
            }

            if (_controller is null) return;

            try
            {
                _controller.Close();
            }
            catch
            {
                // ignored
            }

            _controller = null;
        });
    }

    public event Action<string>? MessageReceived;

    public event Action? Ready;

    /// <summary>
    ///     Creates the web view over <paramref name="parentHwnd" /> and loads <paramref name="page" />,
    ///     a path under <paramref name="webRootPath" /> that may carry a query string. The view stays
    ///     invisible until <see cref="Show" />: what is under it is a working editor, and covering it
    ///     with a page that has no text yet is the empty flash this avoids.
    /// </summary>
    public void Initialize(IntPtr parentHwnd, string webRootPath, string page)
    {
        _parentHwnd = parentHwnd;
        _webRootPath = webRootPath;
        _page = page;
        gui.Post(BeginInitialize);
    }

    private void BeginInitialize()
    {
        if (_disposed || _parentHwnd == IntPtr.Zero) return;

        var userDataFolder = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "RobloxModLoader", "ScriptEditorWebview", "WebView2");
        Directory.CreateDirectory(userDataFolder);

        CoreWebView2Environment.CreateAsync(null, userDataFolder)
            .ContinueWith(task =>
            {
                if (task.IsFaulted || task.Result is null)
                {
                    ScriptEditorWebviewMod.Logger.Error(
                        $"WebView2 environment creation failed (is the WebView2 Runtime installed?): {task.Exception}");
                    return;
                }

                gui.Post(() => OnEnvironmentReady(task.Result));
            });
    }

    private void OnEnvironmentReady(CoreWebView2Environment environment)
    {
        if (_disposed || _parentHwnd == IntPtr.Zero) return;

        environment.CreateCoreWebView2ControllerAsync(_parentHwnd)
            .ContinueWith(task =>
            {
                if (task.IsFaulted || task.Result is null)
                {
                    ScriptEditorWebviewMod.Logger.Error($"WebView2 controller creation failed: {task.Exception}");
                    return;
                }

                gui.Post(() => OnControllerReady(task.Result));
            });
    }

    private void OnControllerReady(CoreWebView2Controller controller)
    {
        if (_disposed)
        {
            controller.Close();
            return;
        }

        _controller = controller;
        _webView = controller.CoreWebView2;

        _webView.WebMessageReceived += OnWebMessageReceived;

        var settings = _webView.Settings;
        settings.AreDevToolsEnabled = true;
        settings.IsStatusBarEnabled = false;
        settings.AreDefaultContextMenusEnabled = false;
        settings.IsZoomControlEnabled = false;

        _webView.SetVirtualHostNameToFolderMapping(
            VirtualHost, _webRootPath, CoreWebView2HostResourceAccessKind.Allow);

        var bounds = ClientRect(_parentHwnd);
        controller.Bounds = bounds;
        controller.IsVisible = _shown;

        Win32.RaiseChildWindows(_parentHwnd, "Chrome_WidgetWin");

        _webView.Navigate($"https://{VirtualHost}/{_page}");

        IsReady = true;
        ScriptEditorWebviewMod.Logger.Info(
            $"WebView2 host ready (parent 0x{_parentHwnd:X}, bounds {bounds.Width}x{bounds.Height})");
        Ready?.Invoke();
    }

    private void OnWebMessageReceived(object? sender, CoreWebView2WebMessageReceivedEventArgs e)
    {
        string payload;
        try
        {
            payload = e.TryGetWebMessageAsString();
        }
        catch
        {
            payload = e.WebMessageAsJson;
        }

        try
        {
            MessageReceived?.Invoke(payload);
        }
        catch (Exception ex)
        {
            ScriptEditorWebviewMod.Logger.Error($"WebView message handler threw: {ex}");
        }
    }

    public void PostMessage(string payload)
    {
        if (_disposed) return;

        gui.Post(() =>
        {
            if (!IsReady || _webView is null) return;

            try
            {
                _webView.PostWebMessageAsString(payload);
            }
            catch (Exception ex)
            {
                ScriptEditorWebviewMod.Logger.Error($"PostWebMessageAsString failed: {ex}");
            }
        });
    }

    public void SyncBounds()
    {
        if (_disposed) return;

        gui.Post(() =>
        {
            if (!IsReady || _controller is null || _parentHwnd == IntPtr.Zero) return;

            // Reassigning Bounds relays out the whole web view; at the reconcile tick rate that is a
            // resize storm, so only a real geometry change is worth one.
            var bounds = ClientRect(_parentHwnd);
            if (bounds == _lastBounds) return;

            _lastBounds = bounds;
            _controller.Bounds = bounds;
            Win32.RaiseChildWindows(_parentHwnd, "Chrome_WidgetWin");
        });
    }

    /// <summary>Puts the page on screen. Called once the page holds the document's text.</summary>
    public void Show()
    {
        if (_disposed) return;

        gui.Post(() =>
        {
            _shown = true;
            if (_controller is null) return;

            _controller.IsVisible = true;
            Win32.RaiseChildWindows(_parentHwnd, "Chrome_WidgetWin");
        });
    }

    private static Rectangle ClientRect(IntPtr hwnd)
    {
        if (hwnd != IntPtr.Zero && Win32.GetClientRect(hwnd, out var rect))
            return new Rectangle(0, 0, Math.Max(1, rect.Width), Math.Max(1, rect.Height));

        return new Rectangle(0, 0, 1, 1);
    }
}