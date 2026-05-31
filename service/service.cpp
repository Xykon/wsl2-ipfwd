#include "service.h"
#include "net_util.h"
#include "../common/app_paths.h"
#include "../common/protocol.h"
#include "../common/version.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <shlobj.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <ctime>

using json = nlohmann::json;

// ---- Static SCM trampolines ------------------------------------------------

static VOID WINAPI SvcMain(DWORD argc, LPWSTR* argv) {
    Wsl2IpFwdService::Instance().ServiceMain(argc, argv);
}
static VOID WINAPI SvcCtrl(DWORD ctrl) {
    Wsl2IpFwdService::Instance().CtrlHandler(ctrl);
}

// ---- Singleton -------------------------------------------------------------

Wsl2IpFwdService& Wsl2IpFwdService::Instance() {
    static Wsl2IpFwdService inst;
    return inst;
}

Wsl2IpFwdService::Wsl2IpFwdService()
    : ipcServer_([this](const std::string& req) { return HandleRequest(req); }) {}

// ---- Service entry points --------------------------------------------------

void Wsl2IpFwdService::RunAsService() {
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), SvcMain },
        { nullptr, nullptr }
    };
    StartServiceCtrlDispatcherW(table);
}

void Wsl2IpFwdService::RunInteractive(bool trace) {
    interactive_ = true;
    trace_       = trace;
    running_ = true;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OpenLog();

    Log("=== WSL2 IP Forwarder -- debug mode ===");
    config_ = Config::Load();
    Log("Config loaded. Distro: \"" + (config_.wsl_distro.empty() ? "(default)" : config_.wsl_distro) + "\"");
    Log("Poll interval: " + std::to_string(config_.poll_interval_ms) + " ms  |  Offline threshold: " + std::to_string(config_.offline_threshold_ms) + " ms");

    ClearStalePendingUpdate();

    monitor_.SetDistro(config_.wsl_distro);

    std::string wslIp = monitor_.GetWslIp();
    if (wslIp.empty())
        Log("WARNING: Could not get WSL2 IP — WSL may not be running.");
    else
        Log("WSL2 IP: " + wslIp);

    if (!firewall_.Initialize())
        Log("WARNING: Firewall COM init failed (run as administrator?)");
    else
        Log("Firewall COM initialized.");

    ipcServer_.Start();
    Log("IPC server started on pipe \\\\.\\pipe\\wsl2ipfwd");

    upnp_.SetLogger([this](const std::string& m) { Log("[UPnP] " + m); });
    upnp_.Start();

    Log("Listening for GUI connections. Press Ctrl+C to stop.\n");

    startTime_ = std::chrono::steady_clock::now();
    MonitorLoop();

    upnp_.Stop();
    ipcServer_.Stop();
    firewall_.Uninitialize();
    if (stopEvent_ != INVALID_HANDLE_VALUE) CloseHandle(stopEvent_);
}

void Wsl2IpFwdService::ServiceMain(DWORD /*argc*/, LPWSTR* /*argv*/) {
    statusHandle_ = RegisterServiceCtrlHandlerW(SERVICE_NAME, SvcCtrl);
    if (!statusHandle_) return;

    status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status_.dwCurrentState = SERVICE_START_PENDING;
    ReportStatus(SERVICE_START_PENDING, 3000);

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_) { ReportStatus(SERVICE_STOPPED); return; }

    OpenLog();
    config_ = Config::Load();
    ClearStalePendingUpdate();
    monitor_.SetDistro(config_.wsl_distro);
    firewall_.Initialize();
    ipcServer_.Start();
    upnp_.SetLogger([this](const std::string& m) { Log("[UPnP] " + m); });
    upnp_.Start();
    startTime_ = std::chrono::steady_clock::now();
    running_ = true;

    ReportStatus(SERVICE_RUNNING);
    Log("Service started. Version: " WSL2IPFWD_VERSION);

    monitorThread_ = std::thread(&Wsl2IpFwdService::MonitorLoop, this);

    WaitForSingleObject(stopEvent_, INFINITE);

    running_ = false;
    if (monitorThread_.joinable()) monitorThread_.join();

    Log("Service stopping — removing all active rules.");
    RemoveAllRules();
    upnp_.Stop();   // removes any UPnP router mappings it created
    ipcServer_.Stop();
    firewall_.Uninitialize();
    CloseHandle(stopEvent_);
    stopEvent_ = INVALID_HANDLE_VALUE;

    if (logFile_ != INVALID_HANDLE_VALUE) CloseHandle(logFile_);
    ReportStatus(SERVICE_STOPPED);
}

void Wsl2IpFwdService::CtrlHandler(DWORD ctrl) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportStatus(SERVICE_STOP_PENDING, 5000);
        running_ = false;
        SetEvent(stopEvent_);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        break;
    }
    ReportStatus(status_.dwCurrentState);
}

void Wsl2IpFwdService::ReportStatus(DWORD state, DWORD waitHint) {
    static DWORD checkpoint = 1;
    status_.dwCurrentState  = state;
    status_.dwWaitHint      = waitHint;
    status_.dwWin32ExitCode = NO_ERROR;
    status_.dwCheckPoint    = (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkpoint++;
    status_.dwControlsAccepted = (state == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    if (statusHandle_) SetServiceStatus(statusHandle_, &status_);
}

// ---- Stale-update cleanup --------------------------------------------------

void Wsl2IpFwdService::ClearStalePendingUpdate() {
    if (config_.pending_update_version.empty()) return;
    // If we're already at or past the pending version, the update was installed
    if (!UpdateChecker::IsNewerVersion(config_.pending_update_version, WSL2IPFWD_VERSION)) {
        Log("Clearing stale update notification (current: " WSL2IPFWD_VERSION
            ", was pending: " + config_.pending_update_version + ")");
        config_.pending_update_version.clear();
        config_.pending_update_url.clear();
        Config::Save(config_);
    }
}

// ---- Expression matching (auto-forward and port-filter notification) -------

// Trim leading/trailing whitespace.
static std::string TrimWs(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Match a single comma-free token: either "N" or "Lo-Hi".
static bool MatchToken(uint16_t port, const std::string& token) {
    std::string t = TrimWs(token);
    if (t.empty()) return false;
    // Range: look for '-' after position 0 (position 0 dash would be negative,
    // which we don't support here).
    auto dash = t.find('-');
    if (dash != std::string::npos && dash > 0) {
        try {
            int lo = std::stoi(t.substr(0, dash));
            int hi = std::stoi(t.substr(dash + 1));
            return port >= static_cast<uint16_t>(lo) && port <= static_cast<uint16_t>(hi);
        } catch (...) { return false; }
    }
    // Single port number.
    try {
        return port == static_cast<uint16_t>(std::stoi(t));
    } catch (...) { return false; }
}

// Match port against an expression string (comma-separated tokens, each a
// single port number or a Lo-Hi range).  Returns true on the first match.
static bool MatchesExpression(uint16_t port, const std::string& expr) {
    std::istringstream ss(expr);
    std::string token;
    while (std::getline(ss, token, ','))
        if (MatchToken(port, token)) return true;
    return false;
}

// Given a port that matched wslExpr, compute the corresponding local port using
// the positionally-parallel localExpr.  Returns 0 (= "same as port") when
// localExpr is empty or its structure does not align with wslExpr.
//   "80, 443"   ↔ "40080, 40443"   (single ↔ single, by position)
//   "3000-4000" ↔ "43000-44000"    (range  ↔ range, preserving offset)
static uint16_t MappedLocalPort(uint16_t port,
                                const std::string& wslExpr,
                                const std::string& localExpr) {
    if (localExpr.empty()) return 0;

    std::vector<std::string> wslToks, locToks;
    { std::istringstream ss(wslExpr);   std::string t; while (std::getline(ss, t, ',')) wslToks.push_back(t); }
    { std::istringstream ss(localExpr); std::string t; while (std::getline(ss, t, ',')) locToks.push_back(t); }
    if (wslToks.size() != locToks.size()) return 0;  // structure mismatch → same port

    for (size_t i = 0; i < wslToks.size(); ++i) {
        std::string wt = TrimWs(wslToks[i]);
        std::string lt = TrimWs(locToks[i]);
        try {
            auto wdash = wt.find('-');
            if (wdash != std::string::npos && wdash > 0) {
                int lo = std::stoi(wt.substr(0, wdash));
                int hi = std::stoi(wt.substr(wdash + 1));
                if (port >= lo && port <= hi) {
                    auto ldash = lt.find('-');
                    if (ldash == std::string::npos || ldash == 0) return 0; // not a range
                    int llo = std::stoi(lt.substr(0, ldash));
                    return static_cast<uint16_t>(llo + (port - lo));
                }
            } else {
                if (std::stoi(wt) == port) {
                    if (lt.find('-') != std::string::npos) return 0; // local must be single
                    return static_cast<uint16_t>(std::stoi(lt));
                }
            }
        } catch (...) { return 0; }
    }
    return 0;
}

// Returns true if the port-filter (whitelist/blacklist) says this port should
// be hidden/ignored entirely.  An empty expression list allows everything.
//   • blacklist: filtered out when it matches any expression
//   • whitelist: filtered out when it matches no expression
bool Wsl2IpFwdService::IsPortFilteredOut(uint16_t port) const {
    if (config_.port_filter_expressions.empty()) return false;
    bool matches = false;
    for (auto& expr : config_.port_filter_expressions)
        if (MatchesExpression(port, expr)) { matches = true; break; }
    return config_.port_filter_whitelist ? !matches : matches;
}

// ---- Monitor loop ----------------------------------------------------------

void Wsl2IpFwdService::RefreshUserToken() {
    if (interactive_) return;

    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) return;

    HANDLE hToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hToken)) return;

    HANDLE hPrimary = nullptr;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, nullptr,
                          SecurityImpersonation, TokenPrimary, &hPrimary)) {
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);
    monitor_.SetUserToken(hPrimary);
}

void Wsl2IpFwdService::MonitorLoop() {
    while (running_) {
        RefreshUserToken();

        // Reload config each cycle so GUI changes take effect without restart
        config_ = Config::Load();
        monitor_.SetDistro(config_.wsl_distro);

        // Effective log level: --debug/--trace flags take precedence over config,
        // but the config setting also applies when running as a service daemon.
        int effLog = interactive_ ? (trace_ ? 2 : 1) : 0;
        if (config_.log_level > effLog) effLog = config_.log_level;

        std::string wslIp = monitor_.GetWslIp();
        if (!wslIp.empty()) {
            currentWslIp_ = wslIp;

            // Determine networking mode from .wslconfig (IP heuristic fallback).
            RefreshNetMode(wslIp);
            NetMode mode = static_cast<NetMode>(netMode_.load());

            auto ports = monitor_.GetListeningPorts();

            if (effLog >= 1) {
                // Build current port set for change detection (levels 1 and 2).
                std::set<uint16_t> currentPorts;
                for (auto& p : ports) currentPorts.insert(p.port);

                if (effLog >= 2) {
                    // Trace: log every poll cycle.
                    std::string portList;
                    for (auto& p : ports)
                        portList += std::to_string(p.port) + "/" + p.protocol + " ";
                    Log("Poll: WSL IP=" + wslIp + "  detected ports: " +
                        (portList.empty() ? "(none)" : portList));
                } else {
                    // Debug: log only when something changed.
                    if (wslIp != lastLoggedWslIp_) {
                        Log("WSL IP: " + wslIp);
                        lastLoggedWslIp_ = wslIp;
                        // IP change → reset so current ports are re-reported.
                        lastPollPorts_.clear();
                    }
                    for (auto& p : ports)
                        if (!lastPollPorts_.count(p.port))
                            Log("Port appeared: " + std::to_string(p.port) + "/" + p.protocol);
                    for (auto prev : lastPollPorts_)
                        if (!currentPorts.count(prev))
                            Log("Port gone:     " + std::to_string(prev));
                }
                lastPollPorts_ = currentPorts;
                lastLoggedWslIp_ = wslIp;
            }

            if (mode == NetMode::VirtioProxy) {
                // VirtioProxy can't expose WSL2 to the LAN — do nothing, and tear
                // down anything we previously created so no stale/broken rules linger.
                RemoveAllRules();
            } else {

            auto now = std::chrono::steady_clock::now();
            for (auto& p : ports)
                activePorts_[p.port].lastSeen = now;

            // Check for newly-detected ports — auto-forward and/or notify.
            for (auto& p : ports) {
                uint16_t port = p.port;

                // Filter gate: a filtered-out port (blacklisted, or not in the
                // whitelist) is treated as if it does not exist — no auto-forward,
                // no notification, no config entry.  Not marked seen, so it is
                // re-evaluated automatically if the filter later allows it.
                if (IsPortFilteredOut(port)) continue;

                if (seenPorts_.count(port)) continue;
                if (config_.ports.count(port)) {
                    seenPorts_.insert(port);
                    continue;
                }
                seenPorts_.insert(port);

                // Auto-forward: if the port matches any configured expression,
                // add it immediately with forwarding enabled (no notification).
                // The index-aligned local expression (if any) maps it to a custom
                // local port.
                bool     autoForward = false;
                uint16_t mappedLocal = 0;   // 0 = same port
                for (size_t i = 0; i < config_.auto_forward_expressions.size(); ++i) {
                    if (MatchesExpression(port, config_.auto_forward_expressions[i])) {
                        autoForward = true;
                        const std::string localExpr =
                            (i < config_.auto_forward_local_expressions.size())
                                ? config_.auto_forward_local_expressions[i] : std::string{};
                        mappedLocal = MappedLocalPort(port, config_.auto_forward_expressions[i], localExpr);
                        break;
                    }
                }

                if (autoForward) {
                    PortConfig afCfg;
                    afCfg.enabled    = true;
                    afCfg.local_port = mappedLocal;
                    afCfg.fw_public  = config_.auto_forward_fw_public;
                    afCfg.fw_private = config_.auto_forward_fw_private;
                    afCfg.fw_domain  = config_.auto_forward_fw_domain;
                    config_.ports[port] = afCfg;
                    Config::Save(config_);
                    std::string via = mappedLocal ? (" (local " + std::to_string(mappedLocal) + ")") : "";
                    Log("Port " + std::to_string(port) + via + " auto-forwarded (matched expression).");
                } else if (config_.notify_new_ports) {
                    // Not auto-forwarded — decide whether to show a notification.
                    // (Filter suppression already handled by the gate above.)
                    bool suppress = false;
                    if (config_.ignore_system_ports && port < 1024) suppress = true;
                    if (!suppress) {
                        for (auto ig : config_.notify_ignore_ports)
                            if (ig == port) { suppress = true; break; }
                    }

                    // Suppress balloon if user opted out while GUI is connected
                    if (!suppress && !config_.notify_while_gui_active
                            && ipcServer_.ActiveClientCount() > 0)
                        suppress = true;

                    config_.ports[port] = PortConfig{};
                    Config::Save(config_);
                    Log("New port " + std::to_string(port) + " added to config (disabled).");
                    if (!suppress) {
                        Log("Showing notification for new port " + std::to_string(port) + ".");
                        NotifyUser(port);
                    }
                }
            }

            // Process configured ports
            for (auto& [port, cfg] : config_.ports) {
                // Safety net: never forward a filtered-out port even if it
                // somehow remains in config (e.g. config edited on disk).
                if (IsPortFilteredOut(port)) {
                    if (activePorts_.count(port) && activePorts_[port].forwarded)
                        RemovePort(port);
                    continue;
                }
                if (!cfg.enabled) {
                    if (activePorts_.count(port) && activePorts_[port].forwarded)
                        RemovePort(port);
                    continue;
                }
                bool detected = std::any_of(ports.begin(), ports.end(),
                    [port = port](const WslPortInfo& p) { return p.port == port; });
                if (detected)
                    ApplyPort(port, wslIp, cfg);
            }

            // Offline threshold
            for (auto it = activePorts_.begin(); it != activePorts_.end(); ) {
                if (!it->second.forwarded) { ++it; continue; }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - it->second.lastSeen).count();
                if (elapsed > config_.offline_threshold_ms) {
                    Log("Port " + std::to_string(it->first) + " offline threshold exceeded — removing rules.");
                    RemovePort(it->first);
                }
                ++it;
            }

            } // end (mode != VirtioProxy)
        } else {
            currentWslIp_.clear();
            // Log the offline transition once (debug/trace). The empty-IP guard
            // means it fires only on the first poll after WSL goes away.
            if (effLog >= 1 && !lastLoggedWslIp_.empty()) {
                Log("WSL not running.");
                lastLoggedWslIp_.clear();
                lastPollPorts_.clear();
            }
        }

        // ---- UPnP desired-state -------------------------------------------------
        // Declare a router mapping for every enabled port that opted in to UPnP
        // and is currently being forwarded.  The UpnpManager reconciles this on
        // its own thread, so a slow/absent IGD never stalls the monitor loop.
        {
            std::map<uint16_t, UpnpMapping> desiredUpnp;
            for (auto& [port, cfg] : config_.ports) {
                if (!cfg.enabled || !cfg.upnp_enabled) continue;
                auto ait = activePorts_.find(port);
                if (ait == activePorts_.end() || !ait->second.forwarded) continue;
                uint16_t localPort = EffectiveLocalPort(port, cfg);
                uint16_t extPort   = cfg.upnp_remote_port ? cfg.upnp_remote_port : localPort;
                desiredUpnp[extPort] = UpnpMapping{ localPort,
                    "WSL2 IP Forwarder port " + std::to_string(port) };
            }
            upnp_.SetDesired(std::move(desiredUpnp));
        }

        // ---- Update check -------------------------------------------------------
        {
            bool doCheck = pendingUpdateCheck_.exchange(false);
            if (!doCheck && config_.update_check_enabled &&
                config_.update_check_interval_hours > 0) {
                auto nowTs       = static_cast<int64_t>(std::time(nullptr));
                auto intervalSec = static_cast<int64_t>(config_.update_check_interval_hours) * 3600LL;
                doCheck = (nowTs - config_.update_last_check_utc >= intervalSec);
            }
            if (doCheck) {
                Log("Running update check...");
                bool found = updater_.CheckNow();
                config_.update_last_check_utc = updater_.LastCheckUtc();

                if (found) {
                    auto ui = updater_.GetPendingUpdate();
                    config_.pending_update_version = ui.version;
                    config_.pending_update_url     = ui.url;
                    Log("Update available: " + ui.version + "  url=" + ui.url);
                    // Notify only once per version per service session
                    if (ui.version != notifiedUpdateVer_) {
                        notifiedUpdateVer_ = ui.version;
                        NotifyUserUpdate(ui.version);
                    }
                } else if (updater_.LastCheckSucceeded()) {
                    // We're on the latest — clear stale pending
                    config_.pending_update_version.clear();
                    config_.pending_update_url.clear();
                    Log("Update check: running latest version (" WSL2IPFWD_VERSION ").");
                } else {
                    Log("Update check: network error, retaining pending state.");
                }
                Config::Save(config_);
            }
        }

        DWORD wait = static_cast<DWORD>(std::max(1000, config_.poll_interval_ms));
        if (WaitForSingleObject(stopEvent_, wait) == WAIT_OBJECT_0) break;
    }
}

uint16_t Wsl2IpFwdService::EffectiveLocalPort(uint16_t port, const PortConfig& cfg) const {
    NetMode m = static_cast<NetMode>(netMode_.load());
    // Custom local ports don't work in mirrored or virtioproxy modes.
    if (m == NetMode::Mirrored || m == NetMode::VirtioProxy) return port;
    return cfg.local_port ? cfg.local_port : port;
}

std::wstring Wsl2IpFwdService::UserWslConfigPath() {
    std::wstring profile;
    if (interactive_) {
        wchar_t buf[MAX_PATH] = {};
        DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) profile = buf;
    } else {
        DWORD sessionId = WTSGetActiveConsoleSessionId();
        if (sessionId != 0xFFFFFFFF) {
            HANDLE hToken = nullptr;
            if (WTSQueryUserToken(sessionId, &hToken)) {
                wchar_t buf[MAX_PATH] = {};
                DWORD len = MAX_PATH;
                if (GetUserProfileDirectoryW(hToken, buf, &len)) profile = buf;
                CloseHandle(hToken);
            }
        }
    }
    if (profile.empty()) return L"";
    return profile + L"\\.wslconfig";
}

void Wsl2IpFwdService::RefreshNetMode(const std::string& wslIp) {
    if (wslConfigPath_.empty())
        wslConfigPath_ = UserWslConfigPath();   // resolve once; retry while empty

    NetMode mode = NetMode::Nat;
    bool decided = false;

    if (!wslConfigPath_.empty()) {
        std::ifstream f(wslConfigPath_.c_str());
        if (f) {
            auto trim = [](std::string& s) {
                auto a = s.find_first_not_of(" \t\r\n");
                if (a == std::string::npos) { s.clear(); return; }
                auto b = s.find_last_not_of(" \t\r\n");
                s = s.substr(a, b - a + 1);
            };
            auto lower = [](std::string& s) {
                for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
            };
            std::string line, section;
            while (std::getline(f, line)) {
                auto cmt = line.find_first_of("#;");
                if (cmt != std::string::npos) line = line.substr(0, cmt);
                trim(line);
                if (line.empty()) continue;
                if (line.front() == '[') {
                    auto end = line.find(']');
                    section = (end != std::string::npos) ? line.substr(1, end - 1) : "";
                    lower(section);
                    continue;
                }
                auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                std::string key = line.substr(0, eq), val = line.substr(eq + 1);
                trim(key); lower(key); trim(val); lower(val);
                if (section == "wsl2" && key == "networkingmode") {
                    if      (val == "virtioproxy") mode = NetMode::VirtioProxy;
                    else if (val == "mirrored")    mode = NetMode::Mirrored;
                    else                            mode = NetMode::Nat;
                    decided = true;
                    break;
                }
            }
        }
    }

    if (!decided) {
        // No explicit networkingMode — infer mirrored from a shared host IP.
        mode = IsLocalIpv4(wslIp) ? NetMode::Mirrored : NetMode::Nat;
    }

    int prev = netMode_.exchange(static_cast<int>(mode));
    if (prev != static_cast<int>(mode)) {
        const char* desc =
            mode == NetMode::VirtioProxy ? "VirtioProxy (LAN forwarding not supported — service idle)"
          : mode == NetMode::Mirrored    ? "mirrored (custom local ports ignored)"
          :                                "NAT";
        Log(std::string("Networking mode: ") + desc);
    }
}

void Wsl2IpFwdService::ApplyPort(uint16_t port, const std::string& wslIp, const PortConfig& cfg) {
    auto& ap = activePorts_[port];

    // Effective local (listen) port: 0 in config means "same as the WSL port";
    // mirrored mode forces the WSL port (custom local ports don't work there).
    uint16_t listenPort = EffectiveLocalPort(port, cfg);

    // If the local port was changed in config, tear down the rules created on the
    // old listen port before rebuilding on the new one.
    if (ap.localPort != 0 && ap.localPort != listenPort) {
        if (ap.forwarded) { forwarder_.RemoveRule(ap.localPort, config_.listen_address); ap.forwarded = false; }
        if (ap.fwRule)    { firewall_.RemoveRule(ap.localPort);                           ap.fwRule    = false; }
        Log("Local port for " + std::to_string(port) + " changed to "
            + std::to_string(listenPort) + " — rebuilding rules.");
    }

    if (!ap.forwarded || ap.wslIp != wslIp) {
        if (ap.forwarded && ap.wslIp != wslIp)
            forwarder_.RemoveRule(listenPort, config_.listen_address);

        if (forwarder_.AddRule(listenPort, port, wslIp, config_.listen_address)) {
            ap.forwarded = true;
            ap.wslIp     = wslIp;
            ap.localPort = listenPort;
            std::string via = (listenPort == port) ? "" : (" (local " + std::to_string(listenPort) + ")");
            Log("Added portproxy rule for port " + std::to_string(port) + via + " -> " + wslIp);
        }
    }

    if (!ap.fwRule) {
        long profiles = 0;
        if (cfg.fw_domain)  profiles |= FW_DOMAIN;
        if (cfg.fw_private) profiles |= FW_PRIVATE;
        if (cfg.fw_public)  profiles |= FW_PUBLIC;
        if (profiles && firewall_.AddRule(listenPort, profiles)) {
            ap.fwRule    = true;
            ap.localPort = listenPort;
            Log("Added firewall rule for port " + std::to_string(listenPort));
        }
    }
}

void Wsl2IpFwdService::RemovePort(uint16_t port) {
    auto it = activePorts_.find(port);
    if (it == activePorts_.end()) return;
    // Rules live on the listen port the rules were created with (falls back to
    // the WSL port for legacy entries created before custom local ports).
    uint16_t listenPort = it->second.localPort ? it->second.localPort : port;
    if (it->second.forwarded) {
        forwarder_.RemoveRule(listenPort, config_.listen_address);
        it->second.forwarded = false;
        Log("Removed portproxy rule for port " + std::to_string(listenPort));
    }
    if (it->second.fwRule) {
        firewall_.RemoveRule(listenPort);
        it->second.fwRule = false;
        Log("Removed firewall rule for port " + std::to_string(listenPort));
    }
}

void Wsl2IpFwdService::RemoveAllRules() {
    for (auto& [port, ap] : activePorts_)
        RemovePort(port);
}

// ---- IPC request handler ---------------------------------------------------

std::string Wsl2IpFwdService::HandleRequest(const std::string& body) {
    json req, res;
    int id = 0;
    try {
        req = json::parse(body);
        id = req.value("id", 0);
        std::string cmd = req.value("cmd", "");

        if (cmd == proto::CMD_LIST_PORTS) {
            json arr = json::array();
            auto detected = monitor_.GetListeningPorts();
            std::map<uint16_t, bool> allPorts;
            for (auto& p : detected)  allPorts[p.port] = true;
            for (auto& [p, _] : config_.ports) allPorts[p] = false;
            for (auto& [p, _] : activePorts_)  allPorts[p] = false;

            auto now = std::chrono::steady_clock::now();
            auto upnpActive = upnp_.ActiveExternalPorts();  // live router-mapping snapshot
            for (auto& [port, _] : allPorts) {
                json e;
                e["port"]     = port;
                e["protocol"] = "tcp";
                bool det = std::any_of(detected.begin(), detected.end(),
                    [port=port](const WslPortInfo& x){ return x.port == port; });
                e["detected"] = det;
                auto ait = activePorts_.find(port);
                e["forwarded"]       = ait != activePorts_.end() && ait->second.forwarded;
                e["firewall_active"] = ait != activePorts_.end() && ait->second.fwRule;
                if (ait != activePorts_.end()) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - ait->second.lastSeen).count();
                    e["last_seen_ms"] = ms;
                } else {
                    e["last_seen_ms"] = -1;
                }
                auto cit = config_.ports.find(port);
                const PortConfig pc = cit != config_.ports.end() ? cit->second : PortConfig{};
                e["config"] = pc;

                // Whether this port's UPnP mapping is actually present on the router.
                bool upnpActiveFlag = false;
                if (pc.upnp_enabled) {
                    uint16_t effLocal = EffectiveLocalPort(port, pc);
                    uint16_t effExt   = pc.upnp_remote_port ? pc.upnp_remote_port : effLocal;
                    upnpActiveFlag = upnpActive.count(effExt) > 0;
                }
                e["upnp_active"] = upnpActiveFlag;

                arr.push_back(e);
            }
            res = {{"id", id}, {"ok", true}, {"data", arr}};

        } else if (cmd == proto::CMD_GET_CONFIG) {
            res = {{"id", id}, {"ok", true}, {"data", config_}};

        } else if (cmd == proto::CMD_SET_CONFIG) {
            GlobalConfig newCfg = req.at("data").get<GlobalConfig>();
            // Preserve per-port configs absent from the GUI payload
            for (auto& [p, c] : config_.ports)
                if (!newCfg.ports.count(p)) newCfg.ports[p] = c;
            // Preserve service-managed update state (never overwritten by GUI)
            newCfg.pending_update_version = config_.pending_update_version;
            newCfg.pending_update_url     = config_.pending_update_url;
            newCfg.update_last_check_utc  = config_.update_last_check_utc;
            config_ = newCfg;

            // Prune ports the (possibly updated) whitelist/blacklist now filters
            // out: tear down their rules, drop them from config, and forget them
            // so they are re-evaluated cleanly if the filter later allows them.
            std::vector<uint16_t> filteredOut;
            for (auto& [port, _] : config_.ports)
                if (IsPortFilteredOut(port)) filteredOut.push_back(port);
            for (uint16_t port : filteredOut) {
                RemovePort(port);
                config_.ports.erase(port);
                activePorts_.erase(port);
                seenPorts_.erase(port);
                Log("Port " + std::to_string(port) +
                    " removed from config (filtered out by updated whitelist/blacklist).");
            }

            Config::Save(config_);
            monitor_.SetDistro(config_.wsl_distro);
            res = {{"id", id}, {"ok", true}};

        } else if (cmd == proto::CMD_SET_PORT_CFG) {
            uint16_t port = req.at("port").get<uint16_t>();
            PortConfig cfg = req.at("config").get<PortConfig>();
            config_.ports[port] = cfg;
            Config::Save(config_);
            if (!cfg.enabled) RemovePort(port);
            seenPorts_.insert(port);
            res = {{"id", id}, {"ok", true}};

        } else if (cmd == proto::CMD_GET_STATUS) {
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime_).count();
            json d;
            d["wsl_ip"]            = currentWslIp_;
            d["wsl_running"]       = !currentWslIp_.empty();
            d["service_uptime_s"]  = uptime;
            d["active_forwardings"]= (int)std::count_if(activePorts_.begin(), activePorts_.end(),
                [](const auto& p){ return p.second.forwarded; });
            d["service_version"]   = WSL2IPFWD_VERSION;
            {
                NetMode m = static_cast<NetMode>(netMode_.load());
                d["net_mode"]      = m == NetMode::VirtioProxy ? "virtioproxy"
                                   : m == NetMode::Mirrored    ? "mirrored"
                                   :                              "nat";
                d["mirrored_mode"] = (m == NetMode::Mirrored);  // back-compat
            }
            res = {{"id", id}, {"ok", true}, {"data", d}};

        } else if (cmd == proto::CMD_REMOVE_PORT) {
            uint16_t port = req.at("port").get<uint16_t>();
            RemovePort(port);
            config_.ports.erase(port);
            activePorts_.erase(port);
            // Forget the port entirely so a later detection is treated as brand
            // new — re-running auto-forward and whitelist/blacklist evaluation.
            seenPorts_.erase(port);
            Config::Save(config_);
            Log("Removed port " + std::to_string(port) + " from config.");
            res = {{"id", id}, {"ok", true}};

        } else if (cmd == proto::CMD_GET_UPDATE_INFO) {
            // Validate: if pending version is no longer newer (already installed), clear it
            bool avail = !config_.pending_update_version.empty()
                      && UpdateChecker::IsNewerVersion(
                             config_.pending_update_version, WSL2IPFWD_VERSION);
            if (!avail && !config_.pending_update_version.empty()) {
                config_.pending_update_version.clear();
                config_.pending_update_url.clear();
            }
            json d;
            d["available"] = avail;
            d["version"]   = config_.pending_update_version;
            d["url"]       = config_.pending_update_url;
            res = {{"id", id}, {"ok", true}, {"data", d}};

        } else if (cmd == proto::CMD_CHECK_UPDATE_NOW) {
            pendingUpdateCheck_.store(true);
            res = {{"id", id}, {"ok", true}};

        } else {
            res = {{"id", id}, {"ok", false}, {"error", "unknown command"}};
        }
    } catch (std::exception& e) {
        res = {{"id", id}, {"ok", false}, {"error", e.what()}};
    }
    return res.dump();
}

// ---- Logging ---------------------------------------------------------------

void Wsl2IpFwdService::OpenLog() {
    // Portable build -> next to the exe; installed build -> %ProgramData%.
    std::wstring path = apppaths::DataDir() + L"\\service.log";
    logFile_ = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void Wsl2IpFwdService::Log(const std::string& msg) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[1024];
    int n = snprintf(buf, sizeof(buf), "[%04d-%02d-%02d %02d:%02d:%02d] %s\r\n",
                     t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, msg.c_str());
    if (n < 0) n = 0;
    if (logFile_ != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(logFile_, buf, static_cast<DWORD>(n), &written, nullptr);
    }
    OutputDebugStringA(buf);
    if (interactive_) {
        std::string line(buf, n);
        if (line.size() >= 2 && line[line.size()-2] == '\r')
            line[line.size()-2] = '\n', line.resize(line.size()-1);
        fputs(line.c_str(), stdout);
        fflush(stdout);
    }
}

// ---- User-session notifications --------------------------------------------

// Shared helper — launches wsl2ipfwd-notify.exe with the given command line in
// the active interactive user's session (or directly in interactive/debug mode).
void Wsl2IpFwdService::LaunchNotifyProcess(const std::wstring& cmdLine) {
    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.lpDesktop   = const_cast<LPWSTR>(L"WinSta0\\Default");
    PROCESS_INFORMATION pi = {};

    auto launch = [&](HANDLE hToken, LPVOID envBlock) -> bool {
        std::wstring buf = cmdLine;
        BOOL ok = hToken
            ? CreateProcessAsUserW(hToken, nullptr, buf.data(),
                  nullptr, nullptr, FALSE,
                  CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW,
                  envBlock, nullptr, &si, &pi)
            : CreateProcessW(nullptr, buf.data(),
                  nullptr, nullptr, FALSE,
                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        if (ok) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
        return ok;
    };

    if (interactive_) {
        if (!launch(nullptr, nullptr))
            Log("LaunchNotifyProcess: launch failed (" + std::to_string(GetLastError()) + ")");
        return;
    }

    DWORD sessionId = WTSGetActiveConsoleSessionId();
    if (sessionId == 0xFFFFFFFF) {
        Log("LaunchNotifyProcess: no active console session.");
        return;
    }
    HANDLE hToken = nullptr;
    if (!WTSQueryUserToken(sessionId, &hToken)) {
        Log("LaunchNotifyProcess: WTSQueryUserToken failed (" + std::to_string(GetLastError()) + ")");
        return;
    }
    HANDLE hPrimary = nullptr;
    if (!DuplicateTokenEx(hToken, TOKEN_ALL_ACCESS, nullptr,
                          SecurityImpersonation, TokenPrimary, &hPrimary)) {
        Log("LaunchNotifyProcess: DuplicateTokenEx failed (" + std::to_string(GetLastError()) + ")");
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    LPVOID envBlock = nullptr;
    CreateEnvironmentBlock(&envBlock, hPrimary, FALSE);

    if (!launch(hPrimary, envBlock))
        Log("LaunchNotifyProcess: CreateProcessAsUser failed (" + std::to_string(GetLastError()) + ")");

    if (envBlock) DestroyEnvironmentBlock(envBlock);
    CloseHandle(hPrimary);
}

void Wsl2IpFwdService::NotifyUser(uint16_t port) {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring dir      = std::filesystem::path(selfPath).parent_path().wstring();
    std::wstring notifyExe = dir + L"\\wsl2ipfwd-notify.exe";
    std::wstring guiExe    = dir + L"\\wsl2ipfwd-gui-cs.exe";

    if (GetFileAttributesW(notifyExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("NotifyUser: wsl2ipfwd-notify.exe not found — skipping.");
        return;
    }

    std::wstring cmd = L"\"" + notifyExe + L"\" "
                     + std::to_wstring(port)
                     + L" \"" + guiExe + L"\"";
    LaunchNotifyProcess(cmd);
    Log("NotifyUser: notification shown for port " + std::to_string(port));
}

void Wsl2IpFwdService::NotifyUserUpdate(const std::string& version) {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring dir       = std::filesystem::path(selfPath).parent_path().wstring();
    std::wstring notifyExe = dir + L"\\wsl2ipfwd-notify.exe";
    std::wstring guiExe    = dir + L"\\wsl2ipfwd-gui-cs.exe";

    if (GetFileAttributesW(notifyExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("NotifyUserUpdate: wsl2ipfwd-notify.exe not found — skipping.");
        return;
    }

    std::wstring versionW(version.begin(), version.end());
    std::wstring cmd = L"\"" + notifyExe + L"\" --update " + versionW + L" \"" + guiExe + L"\"";
    LaunchNotifyProcess(cmd);
    Log("NotifyUserUpdate: update balloon shown for " + version);
}
