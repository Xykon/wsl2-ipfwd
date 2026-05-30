// Data models that mirror the JSON structures in common/config.h
// JsonPropertyName attributes ensure the snake_case keys used by the C++ service
// are round-tripped correctly by System.Text.Json.

using System.Text.Json.Serialization;

namespace Wsl2IpFwdGui;

/// <summary>Per-port forwarding and firewall configuration.</summary>
public class PortConfig
{
    [JsonPropertyName("enabled")]    public bool Enabled   { get; set; }
    [JsonPropertyName("fw_domain")]  public bool FwDomain  { get; set; }
    [JsonPropertyName("fw_private")] public bool FwPrivate { get; set; } = true;
    [JsonPropertyName("fw_public")]  public bool FwPublic  { get; set; } = true;
    // 0 = listen on the same port number as detected in WSL2.
    [JsonPropertyName("local_port")] public int  LocalPort { get; set; }
    // UPnP: expose the local port to the internet via the router (opt-in, per port).
    [JsonPropertyName("upnp_enabled")]     public bool UpnpEnabled    { get; set; }
    [JsonPropertyName("upnp_remote_port")] public int  UpnpRemotePort { get; set; } // 0 = same as local port
}

/// <summary>One row in the list_ports response array.</summary>
public class PortEntry
{
    [JsonPropertyName("port")]            public int        Port          { get; set; }
    [JsonPropertyName("protocol")]        public string     Protocol      { get; set; } = "tcp";
    [JsonPropertyName("detected")]        public bool       Detected      { get; set; }
    [JsonPropertyName("forwarded")]       public bool       Forwarded     { get; set; }
    [JsonPropertyName("firewall_active")] public bool       FirewallActive { get; set; }
    [JsonPropertyName("upnp_active")]     public bool       UpnpActive    { get; set; }
    [JsonPropertyName("last_seen_ms")]    public long       LastSeenMs    { get; set; }
    [JsonPropertyName("config")]          public PortConfig Config        { get; set; } = new();
}

/// <summary>
/// Global service configuration (get_config / set_config).
/// The "ports" map is intentionally excluded when sending set_config —
/// the service preserves existing per-port entries when the key is absent.
/// </summary>
public class GlobalConfig
{
    [JsonPropertyName("wsl_distro")]
    public string WslDistro { get; set; } = "";

    [JsonPropertyName("poll_interval_ms")]
    public int PollIntervalMs { get; set; } = 5000;

    [JsonPropertyName("offline_threshold_ms")]
    public int OfflineThresholdMs { get; set; } = 30000;

    [JsonPropertyName("listen_address")]
    public string ListenAddress { get; set; } = "0.0.0.0";

    // Service log verbosity: 0 = normal, 1 = debug, 2 = trace.
    [JsonPropertyName("log_level")]
    public int LogLevel { get; set; } = 0;

    [JsonPropertyName("notify_new_ports")]
    public bool NotifyNewPorts { get; set; } = true;

    [JsonPropertyName("notify_while_gui_active")]
    public bool NotifyWhileGuiActive { get; set; } = true;

    [JsonPropertyName("ignore_system_ports")]
    public bool IgnoreSystemPorts { get; set; } = true;

    [JsonPropertyName("notify_ignore_ports")]
    public List<int> NotifyIgnorePorts { get; set; } = [];

    // Update-check settings (sent to the service and persisted in config.json)
    [JsonPropertyName("update_check_enabled")]
    public bool UpdateCheckEnabled { get; set; } = false;

    [JsonPropertyName("update_check_interval_hours")]
    public int UpdateCheckIntervalHours { get; set; } = 24;

    // Automatic forwarding — ports matching these expressions are forwarded
    // automatically when detected in WSL2 (enforced by the service).
    [JsonPropertyName("auto_forward_expressions")]
    public List<string> AutoForwardExpressions { get; set; } = new();

    // Index-aligned with AutoForwardExpressions. Empty entry = same port number.
    [JsonPropertyName("auto_forward_local_expressions")]
    public List<string> AutoForwardLocalExpressions { get; set; } = new();

    [JsonPropertyName("auto_forward_fw_public")]
    public bool AutoForwardFwPublic { get; set; } = true;

    [JsonPropertyName("auto_forward_fw_private")]
    public bool AutoForwardFwPrivate { get; set; } = true;

    [JsonPropertyName("auto_forward_fw_domain")]
    public bool AutoForwardFwDomain { get; set; } = false;

    // Port filter — sent to service so it can apply whitelist/blacklist mode
    // when deciding whether to notify the user about a newly detected port.
    [JsonPropertyName("port_filter_whitelist")]
    public bool PortFilterIsWhitelist { get; set; } = false;

    [JsonPropertyName("port_filter_expressions")]
    public List<string> PortFilterExpressions { get; set; } = new();
}

/// <summary>A single port-filter entry stored in local (GUI-only) settings.</summary>
public class PortFilterEntry
{
    [JsonPropertyName("expression")]
    public string Expression { get; set; } = "";

    // Auto-forward rows only: parallel local-port expression ("" = same port).
    // Unused by port-filter rows.
    [JsonPropertyName("local_expression")]
    public string LocalExpression { get; set; } = "";

    [JsonPropertyName("comment")]
    public string Comment { get; set; } = "";
}

/// <summary>
/// GUI-only settings stored locally in %AppData%\WSL2IpFwd\settings.json.
/// These are never sent to the service.
/// </summary>
public class LocalSettings
{
    [JsonPropertyName("exit_on_close")]
    public bool ExitOnClose { get; set; } = false;

    [JsonPropertyName("suppress_tray_notification")]
    public bool SuppressTrayNotification { get; set; } = false;

    [JsonPropertyName("port_filters")]
    public List<PortFilterEntry> PortFilters { get; set; } = new();

    /// <summary>
    /// When false (default) the filter list is a blacklist: matched ports are hidden.
    /// When true it is a whitelist: only matched ports are shown; everything else is hidden.
    /// An empty filter list always shows all ports regardless of this flag.
    /// </summary>
    [JsonPropertyName("port_filter_whitelist")]
    public bool PortFilterIsWhitelist { get; set; } = false;

    /// <summary>
    /// Auto-forward rule entries (expression + optional comment).
    /// The plain expressions are sent to the service via GlobalConfig;
    /// comments are stored here for display in the GUI only.
    /// </summary>
    [JsonPropertyName("auto_forward_entries")]
    public List<PortFilterEntry> AutoForwardEntries { get; set; } = new();
}

/// <summary>Update availability info returned by get_update_info.</summary>
public class UpdateInfo
{
    [JsonPropertyName("available")]
    public bool   Available { get; set; }

    [JsonPropertyName("version")]
    public string Version { get; set; } = "";

    [JsonPropertyName("url")]
    public string Url { get; set; } = "";
}

/// <summary>Snapshot of service health (get_status response).</summary>
public class ServiceStatus
{
    [JsonPropertyName("wsl_ip")]             public string WslIp            { get; set; } = "";
    [JsonPropertyName("wsl_running")]        public bool   WslRunning       { get; set; }
    [JsonPropertyName("service_uptime_s")]   public long   ServiceUptimeS   { get; set; }
    [JsonPropertyName("active_forwardings")] public int    ActiveForwardings { get; set; }

    // WSL2 networking mode: "nat", "mirrored", or "virtioproxy".
    [JsonPropertyName("net_mode")] public string NetMode { get; set; } = "nat";

    [JsonIgnore] public bool IsMirrored    => NetMode == "mirrored";
    [JsonIgnore] public bool IsVirtioProxy => NetMode == "virtioproxy";
    // True when custom local ports are ignored by the service (mirrored/virtioproxy).
    [JsonIgnore] public bool IgnoresCustomLocalPorts => IsMirrored || IsVirtioProxy;
}
