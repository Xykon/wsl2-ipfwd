// MainForm — the primary application window.
//
// Responsibilities:
//   • Connect to the service and auto-refresh every 5 seconds
//   • Display a port list with live status indicators
//   • Offer Refresh / Configure / Remove / Settings actions
//   • Live in the system tray when minimised or closed

using System.ComponentModel;
using System.Diagnostics;
using System.Net.Http;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Wsl2IpFwdGui;

public partial class MainForm : Form
{
    // ---- State --------------------------------------------------------------

    private readonly IpcClient _ipc = new();
    private System.Windows.Forms.Timer _pollTimer = null!;
    private NotifyIcon _tray = null!;

    private ServiceStatus? _lastStatus;
    private List<PortEntry> _ports = [];

    // Change-detection: skip UI rebuild when data hasn't changed.
    // _lastPortsFingerprint intentionally excludes last_seen_ms (which changes
    // every poll cycle even when port state is unchanged) so we only rebuild
    // the list when something that is actually displayed has changed.
    private string _lastStatusJson       = "";
    private string _lastPortsFingerprint = "";

    // When true, clicking [X] minimises to tray instead of quitting
    private bool _minimiseOnClose = true;

    // GUI-only settings loaded from %AppData%\WSL2IpFwd\settings.json
    private LocalSettings _localSettings = new();

    // ---- Update state -------------------------------------------------------
    private bool    _updateDismissed   = false;  // true when user clicked Later
    private string? _extractedUpdater  = null;   // path to updater.exe from the downloaded zip

    // Update bar can show a GitHub release or a GUI/service version mismatch.
    private enum UpdateBarMode { None, Release, Mismatch }
    private UpdateBarMode _updateBar         = UpdateBarMode.None;
    private bool          _versionMismatch   = false;
    private bool          _mismatchDismissed = false;

    // ---- Service-action button state ----------------------------------------
    private enum ServiceButtonMode { None, Restart, Start, Install }
    private ServiceButtonMode _serviceButtonMode  = ServiceButtonMode.None;
    private bool              _serviceOpInProgress = false; // guards button text during ops
    private bool              _wasConnected        = false; // detects connect/disconnect transitions

    // ---- Construction -------------------------------------------------------

    public MainForm()
    {
        _localSettings = LocalSettingsManager.Load();
        InitializeComponent();
        Icon = CreateAppIcon();   // not parseable inside InitializeComponent
        SetupTray();
        SetupTimer();

        // Start the first connection attempt once the window is visible
        Load += async (_, _) =>
        {
            UpdateServiceButton();          // set initial button label before first poll
            await TryConnectAndRefreshAsync();
            _pollTimer.Start();
        };
    }

    // ---- System tray --------------------------------------------------------

    private void SetupTray()
    {
        _tray = new NotifyIcon
        {
            Icon             = CreateAppIcon(),
            Text             = "WSL2 IP Forwarder",
            Visible          = true,
            ContextMenuStrip = BuildTrayMenu()
        };
        _tray.DoubleClick += (_, _) => RestoreWindow();
    }

    private ContextMenuStrip BuildTrayMenu()
    {
        var menu = new ContextMenuStrip();
        menu.Items.Add("Open",    null, (_, _) => RestoreWindow());
        menu.Items.Add("Refresh", null, async (_, _) => await ForceRefreshAsync());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Exit",    null, (_, _) => QuitApp());
        return menu;
    }

    private void RestoreWindow()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    private void QuitApp()
    {
        _minimiseOnClose = false;
        _tray.Visible    = false;
        Close();
    }

    // ---- Window lifecycle ---------------------------------------------------

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        // Minimize to tray unless: (a) ExitOnClose is set, (b) QuitApp() was called
        // (_minimiseOnClose=false), or (c) the close came from somewhere other than
        // the user clicking [X].
        if (_minimiseOnClose
            && e.CloseReason == CloseReason.UserClosing
            && !_localSettings.ExitOnClose)
        {
            e.Cancel = true;
            Hide();
            if (!_localSettings.SuppressTrayNotification)
                _tray.ShowBalloonTip(
                    2000, "WSL2 IP Forwarder",
                    "Still running in the background.\nDouble-click the tray icon to reopen.",
                    ToolTipIcon.Info);
            return;
        }

        _pollTimer.Stop();
        _ipc.Dispose();
        _tray.Dispose();
        base.OnFormClosing(e);
    }

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        // Only hide to tray on minimize when ExitOnClose is off
        if (WindowState == FormWindowState.Minimized && !_localSettings.ExitOnClose)
            Hide();
    }

    // ---- Auto-refresh timer -------------------------------------------------

    private void SetupTimer()
    {
        _pollTimer = new System.Windows.Forms.Timer { Interval = 5000 };
        _pollTimer.Tick += async (_, _) =>
        {
            _pollTimer.Stop();   // prevent re-entrant ticks while awaiting
            try   { await TryConnectAndRefreshAsync(); }
            finally { _pollTimer.Start(); }
        };
    }

    // ---- IPC helpers --------------------------------------------------------

    private async Task TryConnectAndRefreshAsync()
    {
        if (!_ipc.IsConnected)
        {
            bool ok = await _ipc.ConnectAsync(timeoutMs: 1500);
            if (!ok) { ShowDisconnected(); return; }
        }
        await RefreshDataAsync();
    }

    /// <summary>Stop the timer, force a refresh, restart the timer.</summary>
    private async Task ForceRefreshAsync()
    {
        _pollTimer.Stop();
        _lastStatusJson       = "";   // invalidate cache so the UI always updates on a manual refresh
        _lastPortsFingerprint = "";
        try   { await TryConnectAndRefreshAsync(); }
        finally { _pollTimer.Start(); }
    }

    private async Task RefreshDataAsync()
    {
        // --- Status ---
        var statusResp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdGetStatus });
        if (statusResp is null) { ShowDisconnected(); return; }

        bool statusChanged = false;
        if (statusResp["ok"]?.GetValue<bool>() == true && statusResp["data"] is JsonObject sd)
        {
            var json = sd.ToJsonString();
            if (json != _lastStatusJson)
            {
                _lastStatusJson = json;
                string prevMode = _lastStatus?.NetMode ?? "nat";
                _lastStatus     = JsonSerializer.Deserialize<ServiceStatus>(json);
                statusChanged   = true;
                // Networking mode changes how the Local Port / UPnP columns render,
                // so force a port-list rebuild when it changes (rare — needs a WSL
                // restart) even if the port set itself is unchanged.
                if ((_lastStatus?.NetMode ?? "nat") != prevMode)
                    _lastPortsFingerprint = "";
            }
        }

        // --- Port list ---
        var portsResp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdListPorts });
        if (portsResp is null) { ShowDisconnected(); return; }

        bool portsChanged = false;
        if (portsResp["ok"]?.GetValue<bool>() == true && portsResp["data"] is JsonArray da)
        {
            var fingerprint = StablePortsFingerprint(da);
            if (fingerprint != _lastPortsFingerprint)
            {
                _lastPortsFingerprint = fingerprint;
                _ports = JsonSerializer.Deserialize<List<PortEntry>>(da.ToJsonString()) ?? [];
                portsChanged = true;
            }
        }

        if (statusChanged) UpdateStatusPanel();
        if (portsChanged)  RebuildPortList();
        ShowConnected();   // always update the "Updated HH:mm:ss" timestamp

        // Re-check for a pending update every poll. The service discovers updates
        // asynchronously (and shows the balloon); polling here makes the in-app
        // update bar appear during the session, not only after a restart.
        await CheckForUpdateAsync();
    }

    // ---- UI update ----------------------------------------------------------

    private void UpdateStatusPanel()
    {
        if (_lastStatus is null) return;

        if (_lastStatus.WslRunning)
        {
            lblWslStatus.Text      = $"WSL2:  ●  Running  ({_lastStatus.WslIp})";
            lblWslStatus.ForeColor = Color.FromArgb(0, 140, 0);
        }
        else
        {
            lblWslStatus.Text      = "WSL2:  ○  Not running";
            lblWslStatus.ForeColor = Color.OrangeRed;
        }

        // Networking-mode note, placed just after the (auto-sized) status label.
        if (_lastStatus.WslRunning && _lastStatus.IsVirtioProxy)
        {
            lblMirrored.Text      = "⚠  VirtioProxy — LAN forwarding not supported";
            lblMirrored.ForeColor = Color.FromArgb(200, 0, 0);   // red
            lblMirrored.Location  = new Point(lblWslStatus.Right + 10, lblWslStatus.Top + 4);
            lblMirrored.Visible   = true;
        }
        else if (_lastStatus.WslRunning && _lastStatus.IsMirrored)
        {
            lblMirrored.Text      = "(mirrored mode — custom local ports are ignored for forwarding)";
            lblMirrored.ForeColor = Color.FromArgb(180, 95, 0);  // amber
            lblMirrored.Location  = new Point(lblWslStatus.Right + 10, lblWslStatus.Top + 4);
            lblMirrored.Visible   = true;
        }
        else
        {
            lblMirrored.Visible = false;
        }

        var uptime = TimeSpan.FromSeconds(_lastStatus.ServiceUptimeS);
        lblUptime.Text = $"Uptime: {FormatUptime(uptime)}   ·   Active forwardings: {_lastStatus.ActiveForwardings}";

        RefreshVersionMismatch();
    }

    /// <summary>
    /// Shows the update bar in "mismatch" mode when the running service reports a
    /// different version than this GUI. The single Update Service button reinstalls
    /// the service from this app's folder. Takes priority over the release bar.
    /// </summary>
    private void RefreshVersionMismatch()
    {
        string svc = _lastStatus?.ServiceVersion ?? "";
        string gui = GetCurrentVersion();
        _versionMismatch = !string.IsNullOrEmpty(svc) && svc != gui;

        if (_versionMismatch && !_mismatchDismissed)
        {
            lblUpdateText.Text       = $"  🔄  Service is version {svc} but the app is {gui} — update the service to match.";
            btnUpdateService.Visible = true;
            btnDownload.Visible      = false;
            btnInstallNow.Visible    = false;
            btnUpdateLater.Visible   = true;
            pnlUpdate.Visible        = true;
            _updateBar = UpdateBarMode.Mismatch;
        }
        else if (_updateBar == UpdateBarMode.Mismatch)
        {
            // Mismatch cleared (or dismissed) — hide and re-evaluate a release update.
            btnUpdateService.Visible = false;
            pnlUpdate.Visible        = false;
            _updateBar               = UpdateBarMode.None;
            // The next poll's CheckForUpdateAsync re-shows a release bar if one is pending.
        }
    }

    private void RebuildPortList()
    {
        // ---- Remember which row was selected so we can restore it ------------
        // Key on (distro, port) so the selection survives reordering.
        string? prevSelectedKey = null;
        if (lvPorts.SelectedItems.Count > 0)
        {
            var sp = (PortEntry)lvPorts.SelectedItems[0].Tag!;
            prevSelectedKey = sp.Distro + "|" + sp.Port;
        }

        lvPorts.BeginUpdate();
        lvPorts.Items.Clear();

        // In mirrored/virtioproxy modes the service ignores custom local ports, so
        // reflect the effective behaviour (the WSL port) rather than the config value.
        bool ignoreCustomLocal = _lastStatus?.IgnoresCustomLocalPorts ?? false;

        foreach (var p in _ports)
        {
            if (IsFiltered(p.Port)) continue;   // hidden by port-filter rules

            bool hasCustomLocal = !ignoreCustomLocal && p.Config.LocalPort > 0;
            int effectiveLocal  = hasCustomLocal ? p.Config.LocalPort : p.Port;

            // Column 0 = Distribution; then Port, Local Port, …
            var item = new ListViewItem(p.Distro);
            item.SubItems.Add(p.Port.ToString());
            // Local port: custom mapping if set (and honoured); otherwise the port
            // number itself while actively forwarded; a dash when not forwarded.
            string localPortText = hasCustomLocal ? p.Config.LocalPort.ToString()
                                 : p.Forwarded     ? p.Port.ToString()
                                 : "—";
            item.SubItems.Add(localPortText);
            item.SubItems.Add(p.Protocol.ToUpperInvariant());
            item.SubItems.Add(BoolGlyph(p.Detected));
            item.SubItems.Add(BoolGlyph(p.Forwarded));
            item.SubItems.Add(BoolGlyph(p.FirewallActive));
            // UPnP: shows the external (router) port with live mapping status —
            //   ✓ = mapping present on the router, … = enabled but not yet mapped.
            // The remote port still applies in mirrored mode (the router maps it
            // to the WSL port), so only the internal/local side is forced.
            if (p.Config.UpnpEnabled)
            {
                int effectiveRemote = p.Config.UpnpRemotePort > 0 ? p.Config.UpnpRemotePort : effectiveLocal;
                item.SubItems.Add((p.UpnpActive ? "✓ " : "… ") + effectiveRemote);
            }
            else
            {
                item.SubItems.Add("—");
            }
            item.SubItems.Add(p.Config.Enabled ? "Enabled" : "Disabled");
            item.Tag = p;

            // Colour-code rows for quick visual scanning:
            //   grey   = port no longer detected in WSL2
            //   green  = enabled and actively forwarded
            //   orange = enabled but forwarding not yet active
            //   default = configured but disabled
            if (!p.Detected)
                item.ForeColor = SystemColors.GrayText;
            else if (p.Config.Enabled && p.Forwarded)
                item.ForeColor = Color.FromArgb(0, 128, 0);
            else if (p.Config.Enabled)
                item.ForeColor = Color.DarkOrange;

            lvPorts.Items.Add(item);
        }

        // ---- Restore selection -----------------------------------------------
        if (prevSelectedKey != null)
        {
            foreach (ListViewItem item in lvPorts.Items)
            {
                var pe = (PortEntry)item.Tag!;
                if (pe.Distro + "|" + pe.Port == prevSelectedKey)
                {
                    item.Selected = true;
                    item.Focused  = true;   // keep keyboard focus on the same row
                    item.EnsureVisible();
                    break;
                }
            }
        }

        lvPorts.EndUpdate();
        ResizeLastColumn();   // fill any remaining horizontal space
        UpdateButtonState();
    }

    private void ShowConnected()
    {
        tsslStatus.Text      = "●  Connected to service";
        tsslStatus.ForeColor = Color.FromArgb(0, 140, 0);
        tsslUpdated.Text     = $"Updated {DateTime.Now:HH:mm:ss}";

        // Refresh the service-action button on the first tick after (re-)connection
        if (!_wasConnected) { _wasConnected = true; UpdateServiceButton(); }
        // (Update-availability is polled in RefreshDataAsync every cycle.)
    }

    private void ShowDisconnected()
    {
        _lastStatusJson       = "";     // reset cache so next successful poll triggers a full UI rebuild
        _lastPortsFingerprint = "";
        tsslStatus.Text       = "○  Service not running — retrying…";
        tsslStatus.ForeColor  = Color.OrangeRed;
        lblWslStatus.Text     = "WSL2:  –  Unknown";
        lblWslStatus.ForeColor = SystemColors.GrayText;
        lblUptime.Text        = "";
        tsslUpdated.Text      = $"Last attempt {DateTime.Now:HH:mm:ss}";

        // The service is gone (stopped / not installed / unreachable) — the last
        // known port list is stale, so clear it rather than showing ports as
        // forwarded when nothing is actually running.
        _lastStatus = null;
        if (_ports.Count > 0)
        {
            _ports = [];
            lvPorts.Items.Clear();
            UpdateButtonState();
        }

        // Update service-action button on disconnect (service may have stopped/uninstalled)
        if (_wasConnected) { _wasConnected = false; UpdateServiceButton(); }
    }

    private void UpdateButtonState()
    {
        bool sel = lvPorts.SelectedItems.Count > 0;
        btnConfigure.Enabled = sel && _ipc.IsConnected;
        btnRemove.Enabled    = sel && _ipc.IsConnected;
    }

    // ---- Button / menu handlers ---------------------------------------------

    private async void btnRefresh_Click(object sender, EventArgs e)
        => await ForceRefreshAsync();

    private void btnConfigure_Click(object sender, EventArgs e)
        => OpenPortConfig();

    private async void btnRemove_Click(object sender, EventArgs e)
    {
        if (lvPorts.SelectedItems.Count == 0) return;
        var p = (PortEntry)lvPorts.SelectedItems[0].Tag!;

        var answer = MessageBox.Show(
            $"Remove all rules for port {p.Port} in {p.Distro}?\n\n" +
            "The port will be removed from the configuration and any active\n" +
            "port-proxy and firewall rules will be deleted.",
            "Confirm Remove", MessageBoxButtons.YesNo, MessageBoxIcon.Question);

        if (answer != DialogResult.Yes) return;

        var resp = await _ipc.SendAsync(new JsonObject
        {
            ["cmd"]    = Protocol.CmdRemovePort,
            ["distro"] = p.Distro,
            ["port"]   = p.Port
        });

        if (resp?["ok"]?.GetValue<bool>() == true)
            await ForceRefreshAsync();
        else
            ShowError("Failed to remove port rules. Check the service log.");
    }

    private async void btnSettings_Click(object sender, EventArgs e)
    {
        if (!_ipc.IsConnected)
        {
            ShowError("Not connected to the service.");
            return;
        }

        var resp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdGetConfig });
        if (resp?["ok"]?.GetValue<bool>() != true)
        {
            ShowError("Could not retrieve settings from the service.");
            return;
        }

        var cfg = JsonSerializer.Deserialize<GlobalConfig>(resp["data"]!.ToJsonString()) ?? new();

        // Fetch the distro list for the WSL2 tab's checkbox selector.
        var distros = await ListDistrosAsync();

        using var dlg = new SettingsDialog(cfg, _localSettings, distros, CheckNowFromSettingsAsync,
                                           _lastStatus?.LogDir);
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        // --- Service-side settings ---
        var updated  = dlg.GetConfig();
        var local    = dlg.GetLocalSettings();
        var dataNode = JsonNode.Parse(JsonSerializer.Serialize(updated))!;

        var setResp = await _ipc.SendAsync(new JsonObject
        {
            ["cmd"]  = Protocol.CmdSetConfig,
            ["data"] = dataNode,
            // One-shot: retroactively enable auto-forward for already-detected
            // matching ports when the user left "Apply to existing rules" checked.
            ["apply_auto_forward_existing"] = local.ApplyAutoForwardToExisting
        });

        if (setResp?["ok"]?.GetValue<bool>() != true)
        {
            ShowError("Failed to save settings. Check the service log.");
            return;
        }

        // --- GUI-only (local) settings ---
        _localSettings = local;
        LocalSettingsManager.Save(_localSettings);

        await ForceRefreshAsync();
    }

    private async void btnClear_Click(object sender, EventArgs e)
    {
        if (!_ipc.IsConnected) { ShowError("Not connected to the service."); return; }

        using var dlg = new ClearRulesDialog();
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        // Build the list of ports that match the selected criteria.
        // System-ports guard is applied last so that ports < 1000 are never
        // accidentally removed unless the user explicitly opted in.
        var toRemove = _ports.Where(p =>
        {
            if (p.Port < 1000 && !dlg.IncludeSystemPorts) return false;

            return (dlg.IncludeDisabled && !p.Config.Enabled) ||
                   (dlg.IncludeEnabled  &&  p.Config.Enabled) ||
                   (dlg.IncludeActive   &&  p.Forwarded);
        }).ToList();

        if (toRemove.Count == 0)
        {
            MessageBox.Show("No rules match the selected criteria — nothing to remove.",
                "Clear Rules", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        const int maxListed = 20;
        string portList = toRemove.Count <= maxListed
            ? string.Join(", ", toRemove.Select(p => p.Port))
            : string.Join(", ", toRemove.Take(maxListed).Select(p => p.Port))
              + $"  … and {toRemove.Count - maxListed} more";

        var confirm = MessageBox.Show(
            $"Remove {toRemove.Count} rule(s)?\n\n" +
            $"Ports: {portList}\n\n" +
            "Active port-proxy and firewall rules will also be deleted.",
            "Confirm Clear", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
        if (confirm != DialogResult.Yes) return;

        int failed = 0;
        foreach (var p in toRemove)
        {
            var resp = await _ipc.SendAsync(new JsonObject
            {
                ["cmd"]    = Protocol.CmdRemovePort,
                ["distro"] = p.Distro,
                ["port"]   = p.Port
            });
            if (resp?["ok"]?.GetValue<bool>() != true) failed++;
        }

        if (failed > 0)
            ShowError($"Failed to remove {failed} rule(s). Check the service log.");

        await ForceRefreshAsync();
    }

    private void lvPorts_DoubleClick(object sender, EventArgs e)
        => OpenPortConfig();

    private void lvPorts_SelectedIndexChanged(object sender, EventArgs e)
        => UpdateButtonState();

    private void lvPorts_MouseDown(object sender, MouseEventArgs e)
    {
        if (e.Button != MouseButtons.Right) return;
        // Select whichever row is under the cursor so the context menu
        // operates on the right port even if the user right-clicks without
        // first left-clicking to select.
        var hit = lvPorts.HitTest(e.Location);
        if (hit.Item is not null)
        {
            hit.Item.Selected = true;
            hit.Item.Focused  = true;
        }
    }

    private void ctxPorts_Opening(object sender, System.ComponentModel.CancelEventArgs e)
    {
        bool hasSelection = lvPorts.SelectedItems.Count > 0;
        bool connected    = _ipc.IsConnected;
        ctxConfigure.Enabled = hasSelection && connected;
        ctxRemove.Enabled    = hasSelection && connected;
        // Suppress the menu entirely when right-clicking empty list space
        if (!hasSelection) e.Cancel = true;
    }

    private void OpenPortConfig()
    {
        if (lvPorts.SelectedItems.Count == 0) return;
        if (!_ipc.IsConnected) { ShowError("Not connected to the service."); return; }

        var port = (PortEntry)lvPorts.SelectedItems[0].Tag!;
        using var dlg = new PortConfigDialog(port);
        if (dlg.ShowDialog(this) != DialogResult.OK) return;

        // Fire the IPC call and refresh (we're already on the UI thread)
        _ = SavePortConfigAsync(port.Distro, port.Port, dlg.GetConfig());
    }

    private async Task SavePortConfigAsync(string distro, int portNumber, PortConfig cfg)
    {
        var cfgNode = JsonNode.Parse(JsonSerializer.Serialize(cfg))!;
        var resp = await _ipc.SendAsync(new JsonObject
        {
            ["cmd"]    = Protocol.CmdSetPortCfg,
            ["distro"] = distro,
            ["port"]   = portNumber,
            ["config"] = cfgNode
        });

        if (resp?["ok"]?.GetValue<bool>() == true)
            await ForceRefreshAsync();
        else
            ShowError("Failed to save port configuration. Check the service log.");
    }

    // ---- Menu handlers ------------------------------------------------------

    private void exitMenuItem_Click(object sender, EventArgs e)      => QuitApp();
    private async void refreshMenuItem_Click(object sender, EventArgs e) => await ForceRefreshAsync();
    private void settingsMenuItem_Click(object sender, EventArgs e)  => btnSettings_Click(sender, e);
    private void aboutMenuItem_Click(object sender, EventArgs e)
    {
        var infoVer    = Assembly.GetExecutingAssembly()
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()
            ?.InformationalVersion ?? "1.0.0";
        var parts      = infoVer.Split('+', 2);
        var version    = parts[0];
        var hashSuffix = parts.Length > 1 ? $"  ({parts[1]})" : "";

        MessageBox.Show(
            $"WSL2 IP Forwarder  v{version}{hashSuffix}\n\n" +
            "Manages Windows port-proxy and firewall rules for\n" +
            "services running inside WSL2.\n\n" +
            "https://github.com/Xykon/wsl2-ipfwd",
            "About WSL2 IP Forwarder",
            MessageBoxButtons.OK,
            MessageBoxIcon.Information);
    }

    /// <summary>Returns the bare version number (e.g. "1.2.3") from the assembly manifest.</summary>
    private static string GetCurrentVersion()
    {
        var infoVer = Assembly.GetExecutingAssembly()
            .GetCustomAttribute<AssemblyInformationalVersionAttribute>()
            ?.InformationalVersion ?? "1.0.0";
        return infoVer.Split('+', 2)[0];
    }

    // ---- Utilities ----------------------------------------------------------

    // ---- Panel paint / resize handlers (referenced from Designer.cs) -------

    private void pnlInfo_Paint(object sender, PaintEventArgs e)
        => e.Graphics.DrawLine(SystemPens.ControlDark, 0, pnlInfo.Height - 1, pnlInfo.Width, pnlInfo.Height - 1);

    private void pnlButtons_Paint(object sender, PaintEventArgs e)
        => e.Graphics.DrawLine(SystemPens.ControlDark, 0, 0, pnlButtons.Width, 0);

    private void lvPorts_Resize(object sender, EventArgs e)
        => ResizeLastColumn();

    /// <summary>
    /// Stretches the last ListView column to fill any remaining horizontal space,
    /// so the list never shows an empty grey area to the right of the columns.
    /// Called on every resize event and after each list rebuild.
    /// </summary>
    private void ResizeLastColumn()
    {
        if (lvPorts.Columns.Count == 0) return;
        int usedWidth = 0;
        for (int i = 0; i < lvPorts.Columns.Count - 1; i++)
            usedWidth += lvPorts.Columns[i].Width;
        // ClientSize.Width already excludes the vertical scrollbar when it is visible
        int remaining = lvPorts.ClientSize.Width - usedWidth;
        if (remaining > 60)   // don't shrink below a usable minimum
            lvPorts.Columns[lvPorts.Columns.Count - 1].Width = remaining;
    }

    // ---- Update bar ---------------------------------------------------------

    /// <summary>
    /// Queries the service for pending update info and shows the update bar if
    /// a newer version is available.  Safe to call from any context — it only
    /// fires one IPC request and updates the UI on the UI thread.
    /// </summary>
    private async Task CheckForUpdateAsync()
    {
        if (_versionMismatch) return;   // the mismatch bar takes priority
        if (_updateDismissed) return;

        var resp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdGetUpdateInfo });
        if (resp?["ok"]?.GetValue<bool>() != true || resp["data"] is not JsonObject data) return;

        bool available = data["available"]?.GetValue<bool>() ?? false;
        if (!available)
        {
            if (_updateBar == UpdateBarMode.Release)
            {
                pnlUpdate.Visible = false;
                _updateBar = UpdateBarMode.None;
            }
            return;
        }

        // Already showing the release bar — leave it alone so a re-poll doesn't
        // clobber an in-progress "Downloading…/Install Now" state.
        if (_updateBar == UpdateBarMode.Release) return;

        string version = data["version"]?.GetValue<string>() ?? "";
        string url     = data["url"]?.GetValue<string>()     ?? "";

        lblUpdateText.Text       = $"  🔔  Version {version} is available  ·  You are running {GetCurrentVersion()}";
        btnDownload.Tag          = url;   // stash download URL for the click handler
        btnDownload.Text         = "Download";
        btnDownload.Enabled      = true;
        btnDownload.Visible      = true;
        btnInstallNow.Visible    = false;
        btnUpdateService.Visible = false;
        btnUpdateLater.Visible   = true;
        pnlUpdate.Visible        = true;
        _updateBar = UpdateBarMode.Release;
    }

    /// <summary>
    /// Callback passed to SettingsDialog for its inline "Check now" button.
    /// Sends the check command, waits for the service to complete the request
    /// (MonitorLoop wakes within poll_interval_ms ≤5s, HTTP timeout ≤8s),
    /// then returns the current update state.
    /// </summary>
    /// <summary>Queries the service for installed distributions (for the settings UI).</summary>
    private async Task<List<DistroEntry>> ListDistrosAsync()
    {
        if (!_ipc.IsConnected) return new();
        var resp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdListDistros });
        if (resp?["ok"]?.GetValue<bool>() == true && resp["data"] is JsonArray arr)
            return JsonSerializer.Deserialize<List<DistroEntry>>(arr.ToJsonString()) ?? new();
        return new();
    }

    private async Task<UpdateInfo?> CheckNowFromSettingsAsync()
    {
        if (!_ipc.IsConnected)
            throw new InvalidOperationException("Not connected to the service.");

        await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdCheckUpdateNow });

        // Poll every second for up to 15s, returning as soon as the service
        // has had at least 8s to complete the check (fast on local test server,
        // up to 13s worst-case on a slow GitHub API call).
        for (int i = 0; i < 15; i++)
        {
            await Task.Delay(1000);
            if (!_ipc.IsConnected) return null;

            var resp = await _ipc.SendAsync(new JsonObject { ["cmd"] = Protocol.CmdGetUpdateInfo });
            if (resp?["ok"]?.GetValue<bool>() == true && resp["data"] is JsonObject data)
            {
                bool available = data["available"]?.GetValue<bool>() ?? false;
                // An update is available — this is definitely a fresh result.
                if (available)
                    return new UpdateInfo
                    {
                        Available = true,
                        Version   = data["version"]?.GetValue<string>() ?? "",
                        Url       = data["url"]?.GetValue<string>()      ?? ""
                    };

                // Not available yet — keep polling until the service has had
                // at least 8 seconds to perform the HTTP check.
                if (i >= 7)
                    return new UpdateInfo { Available = false };
            }
        }
        return null;   // timed out
    }

    private async void btnDownload_Click(object sender, EventArgs e)
    {
        string url = btnDownload.Tag as string ?? "";
        if (string.IsNullOrEmpty(url)) return;

        btnDownload.Enabled = false;
        lblUpdateText.Text  = "  ⬇  Downloading update…";

        try
        {
            string tempZip    = Path.Combine(Path.GetTempPath(), "wsl2ipfwd-update.zip");
            string extractDir = Path.Combine(Path.GetTempPath(),
                                             "wsl2ipfwd-update-" + Guid.NewGuid().ToString("N"));

            using (var http = new HttpClient())
            {
                http.DefaultRequestHeaders.UserAgent.ParseAdd("wsl2ipfwd-gui/1.0");
                using var stream = await http.GetStreamAsync(url);
                using var file   = new FileStream(tempZip, FileMode.Create, FileAccess.Write);
                await stream.CopyToAsync(file);
            }

            await Task.Run(() =>
            {
                if (Directory.Exists(extractDir)) Directory.Delete(extractDir, true);
                System.IO.Compression.ZipFile.ExtractToDirectory(tempZip, extractDir);
            });

            // updater.exe sits at the zip root (or nested) — find it.
            _extractedUpdater = Directory
                .EnumerateFiles(extractDir, "wsl2ipfwd-updater.exe", SearchOption.AllDirectories)
                .FirstOrDefault()
                ?? throw new FileNotFoundException("updater not found in the downloaded package.");

            lblUpdateText.Text    = "  ✅  Update ready — click Install Now";
            btnInstallNow.Visible = true;
            btnDownload.Visible   = false;
        }
        catch (Exception ex)
        {
            lblUpdateText.Text  = $"  ⚠  Download failed: {ex.Message}";
            btnDownload.Enabled = true;
        }
    }

    private void btnInstallNow_Click(object sender, EventArgs e)
    {
        if (_extractedUpdater is null || !File.Exists(_extractedUpdater)) return;

        try
        {
            // Launch the elevated updater (its manifest triggers a single UAC prompt).
            // It waits for this GUI to exit, stops the service, copies the new files
            // into our own directory, then reinstalls + restarts the service.
            Process.Start(new ProcessStartInfo
            {
                FileName        = _extractedUpdater,
                Arguments       = $"--update --dest \"{AppPaths.AppDir}\" --wait-pid {Environment.ProcessId}",
                UseShellExecute = true
            });
        }
        catch (System.ComponentModel.Win32Exception we) when (we.NativeErrorCode == 1223)
        {
            return;   // user cancelled the UAC prompt
        }
        catch (Exception ex)
        {
            ShowError($"Failed to launch the updater: {ex.Message}");
            return;
        }

        // Exit so the updater can replace our files; it reopens the GUI when done.
        QuitApp();
    }

    private void btnUpdateLater_Click(object sender, EventArgs e)
    {
        if (_updateBar == UpdateBarMode.Mismatch) _mismatchDismissed = true;
        else                                      _updateDismissed   = true;
        pnlUpdate.Visible = false;
        _updateBar = UpdateBarMode.None;
    }

    private async void btnUpdateService_Click(object sender, EventArgs e)
    {
        string updater = Path.Combine(AppPaths.AppDir, "wsl2ipfwd-updater.exe");
        if (!File.Exists(updater))
        {
            ShowError("wsl2ipfwd-updater.exe was not found next to the application.");
            return;
        }

        try
        {
            // Standalone updater (elevates via its manifest): uninstall the old
            // service and install + start the new one from this app's folder.
            Process.Start(new ProcessStartInfo { FileName = updater, UseShellExecute = true });
        }
        catch (System.ComponentModel.Win32Exception we) when (we.NativeErrorCode == 1223)
        {
            return;   // user cancelled the UAC prompt
        }
        catch (Exception ex)
        {
            ShowError($"Failed to launch the updater: {ex.Message}");
            return;
        }

        btnUpdateService.Enabled = false;
        lblUpdateText.Text       = "  ⏳  Updating service…";
        await Task.Delay(4000);          // let the elevated updater reinstall + start
        await ForceRefreshAsync();        // reconnect & re-evaluate versions (clears the bar if matched)
        btnUpdateService.Enabled = true;
    }

    // ---- Service action (Restart / Install) ------------------------------------

    /// <summary>
    /// Refreshes the service-action button label and visibility.
    /// • Service installed  → "Restart Service"
    /// • Not installed, exe present → "Install Service"
    /// • Neither            → button hidden
    /// Guarded by _serviceOpInProgress so in-flight operations keep their label.
    /// </summary>
    private void UpdateServiceButton()
    {
        if (_serviceOpInProgress) return;

        if (IsServiceInstalled())
        {
            // Use the IPC connection state as a proxy for "service is running".
            // Connected → offer Restart; disconnected → offer Start.
            bool running = _ipc.IsConnected;
            btnServiceAction.Text    = running ? "Restart Service" : "Start Service";
            btnServiceAction.Visible = true;
            btnServiceAction.Enabled = true;
            _serviceButtonMode       = running ? ServiceButtonMode.Restart : ServiceButtonMode.Start;
            UpdateServiceMenu();
            return;
        }

        string? exe = ServiceExePath();
        if (exe is not null)
        {
            btnServiceAction.Text    = "Install Service";
            btnServiceAction.Visible = true;
            btnServiceAction.Enabled = true;
            _serviceButtonMode       = ServiceButtonMode.Install;
            UpdateServiceMenu();
            return;
        }

        // Service not installed and exe not found — hide the button
        btnServiceAction.Visible = false;
        _serviceButtonMode       = ServiceButtonMode.None;
        UpdateServiceMenu();
    }

    private static bool IsServiceInstalled()
    {
        using var key = Microsoft.Win32.Registry.LocalMachine
            .OpenSubKey(@"SYSTEM\CurrentControlSet\Services\wsl2ipfwd");
        return key is not null;
    }

    private static string? ServiceExePath()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "wsl2ipfwd-service.exe");
        return File.Exists(path) ? path : null;
    }

    private void UpdateServiceMenu()
    {
        bool installed = IsServiceInstalled();
        bool running   = _ipc.IsConnected;
        bool exeFound  = ServiceExePath() is not null;
        bool busy      = _serviceOpInProgress;
        bool isAdmin   = new System.Security.Principal.WindowsPrincipal(
                             System.Security.Principal.WindowsIdentity.GetCurrent())
                             .IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator);

        svcStartItem.Enabled   = !busy && installed && !running;
        svcStopItem.Enabled    = !busy && installed &&  running;
        svcRestartItem.Enabled = !busy && installed &&  running;
        svcInstallItem.Enabled   = !busy && !installed && exeFound;
        svcUninstallItem.Enabled = !busy && installed;
        svcRestartAdminItem.Enabled = !isAdmin;
    }

    private void serviceMenu_DropDownOpening(object? sender, EventArgs e)
        => UpdateServiceMenu();

    // ---- Service menu click handlers -----------------------------------------

    private async void svcStartItem_Click(object? sender, EventArgs e)
        => await StartServiceAsync();

    private async void svcStopItem_Click(object? sender, EventArgs e)
        => await StopServiceAsync();

    private async void svcRestartItem_Click(object? sender, EventArgs e)
        => await RestartServiceAsync();

    private async void svcInstallItem_Click(object? sender, EventArgs e)
        => await InstallServiceAsync();

    private async void svcUninstallItem_Click(object? sender, EventArgs e)
        => await UninstallServiceAsync();

    private void svcRestartAdminItem_Click(object? sender, EventArgs e)
        => RestartAsAdmin();

    private async void btnServiceAction_Click(object sender, EventArgs e)
    {
        switch (_serviceButtonMode)
        {
            case ServiceButtonMode.Restart: await RestartServiceAsync(); break;
            case ServiceButtonMode.Start:   await StartServiceAsync();   break;
            case ServiceButtonMode.Install: await InstallServiceAsync(); break;
        }
    }

    private async Task RestartServiceAsync()
    {
        _pollTimer.Stop();
        _serviceOpInProgress     = true;
        btnServiceAction.Enabled = false;
        btnServiceAction.Text    = "Restarting…";
        UpdateServiceMenu();

        try
        {
            // Step 1: stop without elevation (works when GUI is already elevated).
            int stopCode = await RunHiddenAsync("sc.exe", "stop wsl2ipfwd");

            if (stopCode == 0)
            {
                // Step 2a: start without elevation.
                await Task.Delay(1000);
                await RunHiddenAsync("sc.exe", "start wsl2ipfwd");
            }
            else
            {
                // Step 2b: sc.exe stop was denied (non-elevated token lacks
                // SERVICE_STOP rights — services.msc works because it
                // auto-elevates via its requireAdministrator manifest).
                // Use a single elevated --restart call: one UAC prompt, no window.
                string svcExe = ServiceExePath()
                    ?? throw new InvalidOperationException(
                        "Service executable not found next to the GUI.\n" +
                        "Use Service › Restart as Administrator to run elevated.");
                await RunElevatedHiddenAsync(svcExe, "--restart");
            }

            // Step 3: wait for the service to accept IPC connections.
            bool connected = await WaitForServiceAsync(timeoutMs: 12_000);
            if (!connected)
                throw new InvalidOperationException(
                    "Service did not accept IPC connections within 12 s after restart.");
        }
        catch (Exception ex)
        {
            ShowError($"Failed to restart service: {ex.Message}");
        }

        _serviceOpInProgress = false;
        UpdateServiceButton();
        await ForceRefreshAsync();
    }

    private async Task StartServiceAsync()
    {
        _pollTimer.Stop();
        _serviceOpInProgress     = true;
        btnServiceAction.Enabled = false;
        btnServiceAction.Text    = "Starting…";
        UpdateServiceMenu();

        try
        {
            await StartOrElevateAsync();
            bool connected = await WaitForServiceAsync(timeoutMs: 12_000);
            if (!connected)
                throw new InvalidOperationException(
                    "Service did not accept IPC connections within 12 s after start.");
        }
        catch (Exception ex)
        {
            ShowError($"Failed to start service: {ex.Message}");
        }

        _serviceOpInProgress = false;
        UpdateServiceButton();
        await ForceRefreshAsync();
    }

    /// <summary>
    /// Polls the IPC pipe every 500 ms until the service accepts a connection
    /// or <paramref name="timeoutMs"/> elapses.
    /// Returns <see langword="true"/> if the service came up, <see langword="false"/> on timeout.
    /// </summary>
    private async Task<bool> WaitForServiceAsync(int timeoutMs = 12_000)
    {
        var deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
        while (DateTime.UtcNow < deadline)
        {
            await Task.Delay(500);
            if (await _ipc.ConnectAsync(timeoutMs: 400))
                return true;
        }
        return false;
    }

    /// <summary>
    /// Starts the service without elevation.  If sc.exe returns a non-zero
    /// exit code (access denied), retries once via <see cref="RunElevatedHiddenAsync"/>
    /// so privilege is requested only when actually required and no console window appears.
    /// </summary>
    private async Task StartOrElevateAsync()
    {
        int exitCode = await RunHiddenAsync("sc.exe", "start wsl2ipfwd");
        if (exitCode == 0) return;
        await RunElevatedHiddenAsync("sc.exe", "start wsl2ipfwd");
    }

    private async Task InstallServiceAsync()
    {
        string? svcExe = ServiceExePath();
        if (svcExe is null) return;

        _pollTimer.Stop();
        _serviceOpInProgress     = true;
        btnServiceAction.Enabled = false;
        btnServiceAction.Text    = "Installing…";
        UpdateServiceMenu();

        try
        {
            // --install-and-start handles both steps inside the single elevated
            // process.  RunElevatedHiddenAsync uses ShellExecuteEx with SW_HIDE
            // so no console window appears at all — not even briefly.
            await RunElevatedHiddenAsync(svcExe, "--install-and-start");
            bool connected = await WaitForServiceAsync(timeoutMs: 15_000);
            if (!connected)
                throw new InvalidOperationException(
                    "Service did not accept IPC connections within 15 s after install.");
        }
        catch (Exception ex)
        {
            ShowError($"Failed to install service: {ex.Message}");
        }

        _serviceOpInProgress = false;
        UpdateServiceButton();
        await ForceRefreshAsync();
    }

    private async Task StopServiceAsync()
    {
        _serviceOpInProgress     = true;
        btnServiceAction.Enabled = false;
        btnServiceAction.Text    = "Stopping…";
        UpdateServiceMenu();

        try
        {
            int exitCode = await RunHiddenAsync("sc.exe", "stop wsl2ipfwd");
            if (exitCode != 0)
                await RunElevatedHiddenAsync("sc.exe", "stop wsl2ipfwd");
        }
        catch (Exception ex)
        {
            ShowError($"Failed to stop service: {ex.Message}");
        }

        _serviceOpInProgress = false;
        await Task.Delay(1000);
        UpdateServiceButton();
        await ForceRefreshAsync();
    }

    private async Task UninstallServiceAsync()
    {
        string? svcExe = ServiceExePath();
        if (svcExe is null)
        {
            ShowError("Service executable not found — cannot uninstall.\n" +
                      "Use Services (services.msc) to remove it manually.");
            return;
        }

        if (MessageBox.Show(
                "Uninstall the WSL2 IP Forwarder service?\n\n" +
                "All active port-proxy and firewall rules will be removed " +
                "and the service will no longer start automatically.",
                "Confirm Uninstall",
                MessageBoxButtons.YesNo,
                MessageBoxIcon.Warning) != DialogResult.Yes)
            return;

        _serviceOpInProgress     = true;
        btnServiceAction.Enabled = false;
        btnServiceAction.Text    = "Uninstalling…";
        UpdateServiceMenu();

        try
        {
            // --uninstall stops the service then deletes it; needs admin.
            await RunElevatedHiddenAsync(svcExe, "--uninstall");
        }
        catch (Exception ex)
        {
            ShowError($"Failed to uninstall service: {ex.Message}");
        }

        _serviceOpInProgress = false;
        await Task.Delay(1500);
        UpdateServiceButton();
        await ForceRefreshAsync();
    }

    /// <summary>
    /// Re-launches the GUI process with admin elevation then exits this instance.
    /// Once elevated, all service operations run without further UAC prompts.
    /// </summary>
    private void RestartAsAdmin()
    {
        try
        {
            string exePath = Process.GetCurrentProcess().MainModule!.FileName;
            Process.Start(new ProcessStartInfo(exePath)
            {
                UseShellExecute = true,
                Verb            = "runas"
            });
            QuitApp();
        }
        catch (Exception ex)
        {
            // NativeErrorCode 1223 = ERROR_CANCELLED — user dismissed the UAC prompt.
            if (ex is not Win32Exception we || we.NativeErrorCode != 1223)
                ShowError($"Failed to restart as administrator: {ex.Message}");
        }
    }

    /// <summary>
    /// Runs a process with no visible window, waits for it to exit, and
    /// returns its exit code (-1 if the process could not be started).
    /// </summary>
    private static async Task<int> RunHiddenAsync(string exe, string args)
    {
        using var proc = Process.Start(new ProcessStartInfo(exe, args)
        {
            UseShellExecute = false,
            CreateNoWindow  = true
        });
        if (proc is null) return -1;
        await Task.Run(() => proc.WaitForExit(15_000));
        return proc.ExitCode;
    }

    // ---- Elevated hidden process (P/Invoke) ------------------------------------
    //
    // Process.Start with UseShellExecute=true + Verb="runas" still shows a brief
    // console flash because Windows creates the console before honouring the
    // WindowStyle hint.  ShellExecuteEx with nShow=SW_HIDE prevents the window
    // from being created in the first place.

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct ShellExecuteInfo
    {
        public int    cbSize;
        public uint   fMask;
        public IntPtr hwnd;
        public string? lpVerb;
        public string? lpFile;
        public string? lpParameters;
        public string? lpDirectory;
        public int    nShow;
        public IntPtr hInstApp;
        public IntPtr lpIDList;
        public string? lpClass;
        public IntPtr hkeyClass;
        public uint   dwHotKey;
        public IntPtr hIconOrMonitor;   // union { hIcon, hMonitor }
        public IntPtr hProcess;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool ShellExecuteEx(ref ShellExecuteInfo lpExecInfo);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetExitCodeProcess(IntPtr hProcess, out uint lpExitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr hObject);

    private const uint SEE_MASK_NOCLOSEPROCESS = 0x00000040;
    private const int  SW_HIDE                 = 0;

    /// <summary>
    /// Runs a process elevated (UAC runas) with no visible window.
    /// Uses ShellExecuteEx directly so nShow=SW_HIDE is applied before any
    /// console is created, preventing even a brief flash.
    /// Returns the process exit code, or throws <see cref="Win32Exception"/> on failure.
    /// </summary>
    private static async Task<int> RunElevatedHiddenAsync(
        string file, string args, int timeoutMs = 30_000)
    {
        var sei = new ShellExecuteInfo
        {
            cbSize       = Marshal.SizeOf<ShellExecuteInfo>(),
            fMask        = SEE_MASK_NOCLOSEPROCESS,
            lpVerb       = "runas",
            lpFile       = file,
            lpParameters = args,
            nShow        = SW_HIDE
        };

        if (!ShellExecuteEx(ref sei))
            throw new Win32Exception(Marshal.GetLastWin32Error());

        if (sei.hProcess == IntPtr.Zero)
            return 0;

        return await Task.Run(() =>
        {
            WaitForSingleObject(sei.hProcess, (uint)timeoutMs);
            GetExitCodeProcess(sei.hProcess, out uint exitCode);
            CloseHandle(sei.hProcess);
            return (int)exitCode;
        });
    }

    // ---- Port filter --------------------------------------------------------

    /// <summary>
    /// Returns true if the port should be hidden from the list.
    /// Blacklist mode (default): a port is hidden when it matches any rule.
    /// Whitelist mode: a port is hidden when it matches NO rule.
    /// An empty rule list always shows everything, regardless of mode.
    /// </summary>
    private bool IsFiltered(int port)
    {
        if (_localSettings.PortFilters.Count == 0) return false;

        bool matchesAny = _localSettings.PortFilters
            .Any(f => MatchesExpression(port, f.Expression));

        return _localSettings.PortFilterIsWhitelist
            ? !matchesAny   // whitelist: hide ports that are NOT in the list
            :  matchesAny;  // blacklist: hide ports that ARE in the list
    }

    /// <summary>
    /// Evaluates one filter expression against a port number.
    /// Supports: single port "80", comma list "80, 443", range "3000-4000",
    /// or any mix (each comma-token is either a range or a single port).
    /// </summary>
    private static bool MatchesExpression(int port, string expr)
    {
        var opts = StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries;
        foreach (var token in expr.Split(',', opts))
        {
            var dashIdx = token.IndexOf('-');
            // Negative port numbers are invalid; a leading '-' that has nothing
            // before it is not a range — skip it.
            if (dashIdx > 0)
            {
                if (int.TryParse(token[..dashIdx],        out int lo) &&
                    int.TryParse(token[(dashIdx + 1)..], out int hi) &&
                    port >= lo && port <= hi)
                    return true;
            }
            else if (int.TryParse(token, out int p) && p == port)
            {
                return true;
            }
        }
        return false;
    }

    /// <summary>
    /// Returns a compact string covering every field displayed in the port list,
    /// intentionally omitting <c>last_seen_ms</c> which changes on every poll
    /// even when port state is unchanged.
    /// </summary>
    private static string StablePortsFingerprint(JsonArray arr)
    {
        var sb = new System.Text.StringBuilder(arr.Count * 48);
        foreach (var node in arr)
        {
            if (node is not JsonObject obj) continue;
            sb.Append(obj["distro"]);          sb.Append(',');
            sb.Append(obj["port"]);            sb.Append(',');
            sb.Append(obj["protocol"]);        sb.Append(',');
            sb.Append(obj["detected"]);        sb.Append(',');
            sb.Append(obj["forwarded"]);       sb.Append(',');
            sb.Append(obj["firewall_active"]); sb.Append(',');
            sb.Append(obj["upnp_active"]);     sb.Append(',');
            if (obj["config"] is JsonObject cfg)
            {
                sb.Append(cfg["enabled"]);          sb.Append(',');
                sb.Append(cfg["fw_domain"]);        sb.Append(',');
                sb.Append(cfg["fw_private"]);       sb.Append(',');
                sb.Append(cfg["fw_public"]);        sb.Append(',');
                sb.Append(cfg["local_port"]);       sb.Append(',');
                sb.Append(cfg["upnp_enabled"]);     sb.Append(',');
                sb.Append(cfg["upnp_remote_port"]);
            }
            sb.Append('|');
        }
        return sb.ToString();
    }

    private static string BoolGlyph(bool v) => v ? "✓" : "–";

    private static string FormatUptime(TimeSpan t) =>
        t.TotalHours >= 1  ? $"{(int)t.TotalHours}h {t.Minutes}m" :
        t.TotalMinutes >= 1 ? $"{t.Minutes}m {t.Seconds}s" :
                              $"{t.Seconds}s";

    private void ShowError(string msg) =>
        MessageBox.Show(msg, "WSL2 IP Forwarder", MessageBoxButtons.OK, MessageBoxIcon.Error);

    /// <summary>
    /// Generates a simple 32×32 icon at runtime so the app does not need
    /// an .ico file.  Replace this with Icon.ExtractAssociatedIcon or a
    /// proper embedded resource when you have a real icon.
    /// </summary>
    private static Icon CreateAppIcon()
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            // Blue circle background
            using var bg = new SolidBrush(Color.FromArgb(0, 120, 212));
            g.FillEllipse(bg, 2, 2, 28, 28);
            // White "W" letter
            using var font = new Font("Segoe UI", 14f, FontStyle.Bold, GraphicsUnit.Pixel);
            g.DrawString("W", font, Brushes.White, 5f, 5f);
        }
        return Icon.FromHandle(bmp.GetHicon());
    }
}
