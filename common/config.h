#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

struct PortConfig {
    bool     enabled    = false;
    bool     fw_domain  = false;
    bool     fw_private = true;
    bool     fw_public  = true;
    uint16_t local_port = 0;   // 0 = listen on the same port number as detected
    // UPnP: when enabled, open a port mapping on the internet router so the
    // local port is reachable from the wider network.  Opt-in, per port.
    bool     upnp_enabled     = false;
    uint16_t upnp_remote_port = 0;   // 0 = same as the effective local port
};

inline void to_json(nlohmann::json& j, const PortConfig& c) {
    j = {{"enabled", c.enabled}, {"fw_domain", c.fw_domain},
         {"fw_private", c.fw_private}, {"fw_public", c.fw_public},
         {"local_port", c.local_port},
         {"upnp_enabled", c.upnp_enabled}, {"upnp_remote_port", c.upnp_remote_port}};
}
inline void from_json(const nlohmann::json& j, PortConfig& c) {
    c.enabled    = j.value("enabled",    false);
    c.fw_domain  = j.value("fw_domain",  false);
    c.fw_private = j.value("fw_private", true);
    c.fw_public  = j.value("fw_public",  true);
    c.local_port = static_cast<uint16_t>(j.value("local_port", 0));
    c.upnp_enabled     = j.value("upnp_enabled", false);
    c.upnp_remote_port = static_cast<uint16_t>(j.value("upnp_remote_port", 0));
}

struct GlobalConfig {
    std::string wsl_distro;           // empty = default distro
    int         poll_interval_ms  = 5000;
    int         offline_threshold_ms = 30000;
    std::string listen_address    = "0.0.0.0";

    // Service log verbosity (written to service.log; also applies in --debug mode)
    // 0 = normal  (errors and significant events only — default)
    // 1 = debug   (also log port appear/gone changes)
    // 2 = trace   (log every poll cycle — very verbose)
    int log_level = 0;

    // Notification settings
    bool notify_new_ports    = true;  // show alert when an unconfigured port appears
    bool notify_while_gui_active = true; // false = suppress balloons while GUI is connected
    bool ignore_system_ports = true;  // suppress notifications for ports < 1024
    std::vector<uint16_t> notify_ignore_ports; // user-defined extra ports to suppress

    std::map<uint16_t, PortConfig> ports;

    // Update-check settings (controlled by the GUI via set_config)
    bool update_check_enabled        = false;
    int  update_check_interval_hours = 24;   // 24 = daily, 168 = weekly

    // Update-check state (managed entirely by the service; preserved on set_config)
    std::string pending_update_version;   // e.g. "v1.2.3"; empty = no pending update
    std::string pending_update_url;       // browser download URL of the setup exe
    int64_t     update_last_check_utc = 0; // Unix timestamp of last check attempt

    // Automatic forwarding — ports matching these expressions are forwarded
    // automatically when detected in WSL2.  Controlled by the GUI via set_config.
    std::vector<std::string> auto_forward_expressions;
    // Optional per-entry local-port mapping, index-aligned with the expressions
    // above.  Empty entry (or empty list) = listen on the same port number.
    // Must be structurally parallel: "80, 443" ↔ "40080, 40443"; "3000-4000" ↔
    // "43000-44000".
    std::vector<std::string> auto_forward_local_expressions;
    bool auto_forward_fw_public  = true;
    bool auto_forward_fw_private = true;
    bool auto_forward_fw_domain  = false;

    // Port filter — sent by the GUI so the service can apply whitelist/blacklist
    // mode when deciding whether to show a notification for a new port.
    bool port_filter_whitelist = false;
    std::vector<std::string> port_filter_expressions;
};

inline void to_json(nlohmann::json& j, const GlobalConfig& g) {
    nlohmann::json ports_obj = nlohmann::json::object();
    for (auto& [port, cfg] : g.ports)
        ports_obj[std::to_string(port)] = cfg;
    nlohmann::json ignore_arr = nlohmann::json::array();
    for (auto p : g.notify_ignore_ports) ignore_arr.push_back(p);
    j = {
        {"wsl_distro",            g.wsl_distro},
        {"poll_interval_ms",      g.poll_interval_ms},
        {"offline_threshold_ms",  g.offline_threshold_ms},
        {"listen_address",        g.listen_address},
        {"log_level",                     g.log_level},
        {"notify_new_ports",              g.notify_new_ports},
        {"notify_while_gui_active",       g.notify_while_gui_active},
        {"ignore_system_ports",           g.ignore_system_ports},
        {"notify_ignore_ports",           ignore_arr},
        {"ports",                         ports_obj},
        {"update_check_enabled",          g.update_check_enabled},
        {"update_check_interval_hours",   g.update_check_interval_hours},
        {"pending_update_version",        g.pending_update_version},
        {"pending_update_url",            g.pending_update_url},
        {"update_last_check_utc",         g.update_last_check_utc},
        {"auto_forward_expressions",      g.auto_forward_expressions},
        {"auto_forward_local_expressions",g.auto_forward_local_expressions},
        {"auto_forward_fw_public",        g.auto_forward_fw_public},
        {"auto_forward_fw_private",       g.auto_forward_fw_private},
        {"auto_forward_fw_domain",        g.auto_forward_fw_domain},
        {"port_filter_whitelist",         g.port_filter_whitelist},
        {"port_filter_expressions",       g.port_filter_expressions}
    };
}
inline void from_json(const nlohmann::json& j, GlobalConfig& g) {
    g.wsl_distro           = j.value("wsl_distro",           std::string{});
    g.poll_interval_ms     = j.value("poll_interval_ms",     5000);
    g.offline_threshold_ms = j.value("offline_threshold_ms", 30000);
    g.listen_address       = j.value("listen_address",       std::string{"0.0.0.0"});
    g.log_level                 = j.value("log_level",                 0);
    g.notify_new_ports          = j.value("notify_new_ports",          true);
    g.notify_while_gui_active   = j.value("notify_while_gui_active",   true);
    g.ignore_system_ports       = j.value("ignore_system_ports",       true);
    if (j.contains("notify_ignore_ports") && j["notify_ignore_ports"].is_array()) {
        g.notify_ignore_ports.clear();
        for (auto& v : j["notify_ignore_ports"])
            g.notify_ignore_ports.push_back(v.get<uint16_t>());
    }
    g.update_check_enabled        = j.value("update_check_enabled",        false);
    g.update_check_interval_hours = j.value("update_check_interval_hours", 24);
    g.pending_update_version      = j.value("pending_update_version",      std::string{});
    g.pending_update_url          = j.value("pending_update_url",          std::string{});
    g.update_last_check_utc       = j.value("update_last_check_utc",       int64_t{0});
    if (j.contains("auto_forward_expressions") && j["auto_forward_expressions"].is_array()) {
        g.auto_forward_expressions.clear();
        for (auto& v : j["auto_forward_expressions"])
            g.auto_forward_expressions.push_back(v.get<std::string>());
    }
    if (j.contains("auto_forward_local_expressions") && j["auto_forward_local_expressions"].is_array()) {
        g.auto_forward_local_expressions.clear();
        for (auto& v : j["auto_forward_local_expressions"])
            g.auto_forward_local_expressions.push_back(v.get<std::string>());
    }
    g.auto_forward_fw_public  = j.value("auto_forward_fw_public",  true);
    g.auto_forward_fw_private = j.value("auto_forward_fw_private", true);
    g.auto_forward_fw_domain  = j.value("auto_forward_fw_domain",  false);
    g.port_filter_whitelist   = j.value("port_filter_whitelist",   false);
    if (j.contains("port_filter_expressions") && j["port_filter_expressions"].is_array()) {
        g.port_filter_expressions.clear();
        for (auto& v : j["port_filter_expressions"])
            g.port_filter_expressions.push_back(v.get<std::string>());
    }
    if (j.contains("ports") && j["ports"].is_object()) {
        for (auto& [k, v] : j["ports"].items()) {
            try {
                uint16_t port = static_cast<uint16_t>(std::stoul(k));
                g.ports[port] = v.get<PortConfig>();
            } catch (...) {}
        }
    }
}

// Load/save config from %ProgramData%\wsl2ipfwd\config.json
namespace Config {
    GlobalConfig Load();
    bool Save(const GlobalConfig& cfg);
    std::wstring ConfigPath();
}
