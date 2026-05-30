// Wire protocol constants — must stay in sync with common/protocol.h
//
// Wire format  (same as the C++ service):
//   4 bytes  little-endian uint32  body length
//   N bytes  UTF-8 JSON body
//
// Request:  { "cmd": "<command>", "id": <int>, ...extra fields... }
// Response: { "id": <int>, "ok": true,  "data": <payload> }
//         | { "id": <int>, "ok": false, "error": "<message>" }

namespace Wsl2IpFwdGui;

internal static class Protocol
{
    /// <summary>Local named pipe served by wsl2ipfwd-service.exe.</summary>
    public const string PipeName = "wsl2ipfwd";

    // ---- Commands (GUI → service) ----------------------------------------
    public const string CmdListPorts  = "list_ports";   // → PortEntry[]
    public const string CmdGetConfig  = "get_config";   // → GlobalConfig
    public const string CmdSetConfig  = "set_config";   // data: GlobalConfig → ok
    public const string CmdSetPortCfg = "set_port_cfg"; // port + config → ok
    public const string CmdGetStatus  = "get_status";   // → ServiceStatus
    public const string CmdRemovePort     = "remove_port";       // port → ok
    public const string CmdGetUpdateInfo  = "get_update_info";   // → UpdateInfo
    public const string CmdCheckUpdateNow = "check_update_now";  // trigger immediate check → ok
}
