#pragma once
#include <string>
#include <cstdint>

// Named pipe used for GUI <-> service IPC
#define WSL2IPFWD_PIPE_NAME L"\\\\.\\pipe\\wsl2ipfwd"

// Wire format: 4-byte little-endian uint32 length, then UTF-8 JSON body
// All commands: {"cmd":"...", "id":N, ...extra fields...}
// All responses: {"id":N, "ok":true/false, "data":{...}} or {"id":N,"ok":false,"error":"..."}

namespace proto {

// Commands (GUI -> Service)
constexpr const char* CMD_LIST_PORTS    = "list_ports";    // -> array of PortEntry
constexpr const char* CMD_GET_CONFIG    = "get_config";    // -> GlobalConfig
constexpr const char* CMD_SET_CONFIG    = "set_config";    // body: GlobalConfig -> ok
constexpr const char* CMD_SET_PORT_CFG  = "set_port_cfg";  // body: {port, config} -> ok
constexpr const char* CMD_GET_STATUS    = "get_status";    // -> StatusInfo
constexpr const char* CMD_REMOVE_PORT   = "remove_port";   // body: {port} -> ok (force remove rule)
constexpr const char* CMD_GET_UPDATE_INFO  = "get_update_info";  // -> {available, version, url}
constexpr const char* CMD_CHECK_UPDATE_NOW = "check_update_now"; // trigger immediate check -> ok
constexpr const char* CMD_LIST_DISTROS     = "list_distros";     // -> array of {name, running, default}

// PortEntry JSON keys (in list_ports response data array)
//  distro, port, protocol, detected, forwarded, firewall_active, upnp_active, last_seen_ms
//  config: { enabled, fw_domain, fw_private, fw_public, local_port, upnp_* }
// set_port_cfg / remove_port bodies include "distro".

// GlobalConfig JSON keys
//  config_version, wsl_distros[], poll_interval_ms, offline_threshold_ms, listen_address

// StatusInfo JSON keys
//  wsl_ip, wsl_running, service_uptime_s, active_forwardings

} // namespace proto
