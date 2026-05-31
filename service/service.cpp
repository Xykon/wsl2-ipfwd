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
    MigrateConfig();
    Log("Poll interval: " + std::to_string(config_.poll_interval_ms) + " ms  |  Offline threshold: " + std::to_string(config_.offline_threshold_ms) + " ms");

    ClearStalePendingUpdate();

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
    MigrateConfig();
    ClearStalePendingUpdate();
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
bool Wsl2IpFwdService::IsPortFilteredOut(uint16_t port, const std::string& distro) const {
    // Effective filter set for this distro = its distro-scoped entries plus all
    // general (untagged) entries. An entry with no parallel distro tag is general.
    bool matches = false;
    bool anyApplicable = false;
    const auto& exprs   = config_.port_filter_expressions;
    const auto& distros = config_.port_filter_distros;
    for (size_t i = 0; i < exprs.size(); ++i) {
        const std::string& scope = (i < distros.size()) ? distros[i] : std::string{};
        if (!scope.empty() && scope != distro) continue;  // scoped to a different distro
        anyApplicable = true;
        if (MatchesExpression(port, exprs[i])) { matches = true; break; }
    }
    // No expressions apply to this distro → allow everything (mirrors the old
    // "empty list allows everything" behavior, per-distro).
    if (!anyApplicable) return false;
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

void Wsl2IpFwdService::MigrateConfig() {
    if (config_.config_version >= CURRENT_CONFIG_VERSION) return;

    // Which distro do the legacy flat ports belong to? The previously-configured
    // distro, or (if none) the resolved default distro.
    std::string target = config_.wsl_distro;
    if (target.empty()) target = monitor_.DefaultDistro();

    // Have legacy ports but can't resolve a distro yet (WSL down) — retry later.
    if (!config_.ports.empty() && target.empty()) return;

    if (!config_.ports.empty()) {
        config_.distro_ports[target] = config_.ports;
        config_.ports.clear();
    }
    if (config_.wsl_distros.empty() && !target.empty())
        config_.wsl_distros = { target };

    config_.config_version = CURRENT_CONFIG_VERSION;
    Config::Save(config_);
    Log("Migrated config to v" + std::to_string(CURRENT_CONFIG_VERSION) +
        (target.empty() ? "" : (" (legacy ports -> distro \"" + target + "\")")));
}

void Wsl2IpFwdService::RefreshMonitoredDistros() {
    std::vector<std::string> running = monitor_.RunningDistros();
    std::vector<std::string> next;
    for (auto& d : running) {
        if (config_.wsl_distros.empty() ||
            std::find(config_.wsl_distros.begin(), config_.wsl_distros.end(), d)
                != config_.wsl_distros.end())
            next.push_back(d);
    }
    // Tear down rules for distros we previously monitored but no longer do.
    for (auto& d : monitoredDistros_)
        if (std::find(next.begin(), next.end(), d) == next.end()) {
            RemoveDistroRules(d);
            distroIps_.erase(d);
            detectedPorts_.erase(d);
        }
    monitoredDistros_ = next;
}

void Wsl2IpFwdService::ProcessDistro(const std::string& distro) {
    int effLog = interactive_ ? (trace_ ? 2 : 1) : 0;
    if (config_.log_level > effLog) effLog = config_.log_level;

    std::string wslIp = monitor_.GetWslIp(distro);
    if (wslIp.empty()) {
        if (effLog >= 1 && !lastLoggedIp_[distro].empty()) {
            Log("[" + distro + "] not running.");
            lastLoggedIp_[distro].clear();
            lastPollPorts_[distro].clear();
        }
        RemoveDistroRules(distro);
        distroIps_.erase(distro);
        detectedPorts_.erase(distro);
        return;
    }
    distroIps_[distro] = wslIp;

    // Networking mode comes from .wslconfig (global to WSL2); the IP heuristic
    // fallback uses this distro's IP.
    RefreshNetMode(wslIp);
    NetMode mode = static_cast<NetMode>(netMode_.load());

    auto ports = monitor_.GetListeningPorts(distro);
    detectedPorts_[distro] = ports;   // cache for list_ports

    if (effLog >= 1) {
        std::set<uint16_t> cur;
        for (auto& p : ports) cur.insert(p.port);
        if (effLog >= 2) {
            std::string list;
            for (auto& p : ports) list += std::to_string(p.port) + "/" + p.protocol + " ";
            Log("[" + distro + "] poll IP=" + wslIp + " ports: " + (list.empty() ? "(none)" : list));
        } else {
            if (wslIp != lastLoggedIp_[distro]) {
                Log("[" + distro + "] IP: " + wslIp);
                lastPollPorts_[distro].clear();
            }
            for (auto& p : ports)
                if (!lastPollPorts_[distro].count(p.port))
                    Log("[" + distro + "] port appeared: " + std::to_string(p.port) + "/" + p.protocol);
            for (auto prev : lastPollPorts_[distro])
                if (!cur.count(prev))
                    Log("[" + distro + "] port gone: " + std::to_string(prev));
        }
        lastPollPorts_[distro] = cur;
        lastLoggedIp_[distro]  = wslIp;
    }

    if (mode == NetMode::VirtioProxy) { RemoveDistroRules(distro); return; }

    auto  now    = std::chrono::steady_clock::now();
    auto& active = activePorts_[distro];
    auto& seen   = seenPorts_[distro];
    auto& dports = config_.distro_ports[distro];

    for (auto& p : ports) active[p.port].lastSeen = now;

    // New-port detection (auto-forward / notify), filtered by whitelist/blacklist.
    for (auto& p : ports) {
        uint16_t port = p.port;
        if (IsPortFilteredOut(port, distro)) continue;
        if (seen.count(port)) continue;
        if (dports.count(port)) { seen.insert(port); continue; }
        seen.insert(port);

        // Two-pass match: a rule scoped to this distro takes precedence over a
        // general (untagged) rule. Pass 0 = distro-scoped only, pass 1 = general.
        bool     autoForward = false;
        uint16_t mappedLocal = 0;
        for (int pass = 0; pass < 2 && !autoForward; ++pass) {
            for (size_t i = 0; i < config_.auto_forward_expressions.size(); ++i) {
                const std::string& scope = (i < config_.auto_forward_distros.size())
                                           ? config_.auto_forward_distros[i] : std::string{};
                bool isScoped = !scope.empty();
                if (pass == 0 && (!isScoped || scope != distro)) continue;  // this distro only
                if (pass == 1 && isScoped) continue;                        // general only
                if (MatchesExpression(port, config_.auto_forward_expressions[i])) {
                    autoForward = true;
                    const std::string le = (i < config_.auto_forward_local_expressions.size())
                                           ? config_.auto_forward_local_expressions[i] : std::string{};
                    mappedLocal = MappedLocalPort(port, config_.auto_forward_expressions[i], le);
                    break;
                }
            }
        }

        if (autoForward) {
            PortConfig af;
            af.enabled    = true;
            af.local_port = mappedLocal;
            af.fw_public  = config_.auto_forward_fw_public;
            af.fw_private = config_.auto_forward_fw_private;
            af.fw_domain  = config_.auto_forward_fw_domain;
            dports[port]  = af;
            Config::Save(config_);
            std::string via = mappedLocal ? (" (local " + std::to_string(mappedLocal) + ")") : "";
            Log("[" + distro + "] port " + std::to_string(port) + via + " auto-forwarded.");
        } else if (config_.notify_new_ports) {
            bool suppress = false;
            if (config_.ignore_system_ports && port < 1024) suppress = true;
            if (!suppress)
                for (auto ig : config_.notify_ignore_ports)
                    if (ig == port) { suppress = true; break; }
            if (!suppress && !config_.notify_while_gui_active && ipcServer_.ActiveClientCount() > 0)
                suppress = true;

            dports[port] = PortConfig{};
            Config::Save(config_);
            Log("[" + distro + "] new port " + std::to_string(port) + " added (disabled).");
            if (!suppress) NotifyUser(port);
        }
    }

    // Process configured ports for this distro.
    for (auto& [port, cfg] : dports) {
        if (IsPortFilteredOut(port, distro) || !cfg.enabled) {
            if (active.count(port) && active[port].forwarded) RemovePort(distro, port);
            continue;
        }
        bool detected = std::any_of(ports.begin(), ports.end(),
            [port = port](const WslPortInfo& p) { return p.port == port; });
        if (detected) ApplyPort(distro, port, wslIp, cfg);
    }

    // Offline threshold for this distro.
    for (auto& [port, ap] : active) {
        if (!ap.forwarded) continue;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - ap.lastSeen).count();
        if (elapsed > config_.offline_threshold_ms) {
            Log("[" + distro + "] port " + std::to_string(port) + " offline — removing rules.");
            RemovePort(distro, port);
        }
    }
}

void Wsl2IpFwdService::PublishUpnpDesired() {
    std::map<uint16_t, UpnpMapping> desired;
    for (auto& [distro, dports] : config_.distro_ports) {
        auto ai = activePorts_.find(distro);
        if (ai == activePorts_.end()) continue;
        for (auto& [port, cfg] : dports) {
            if (!cfg.enabled || !cfg.upnp_enabled) continue;
            auto pit = ai->second.find(port);
            if (pit == ai->second.end() || !pit->second.forwarded) continue;
            uint16_t localPort = EffectiveLocalPort(port, cfg);
            uint16_t extPort   = cfg.upnp_remote_port ? cfg.upnp_remote_port : localPort;
            desired[extPort] = UpnpMapping{ localPort,
                "WSL2 IP Forwarder " + distro + " port " + std::to_string(port) };
        }
    }
    upnp_.SetDesired(std::move(desired));
}

void Wsl2IpFwdService::MonitorLoop() {
    while (running_) {
        RefreshUserToken();

        // Reload config each cycle so GUI changes take effect without restart.
        config_ = Config::Load();
        MigrateConfig();

        // Recompute the monitored-distro set once per full poll interval.
        auto now = std::chrono::steady_clock::now();
        bool fullCycle = monitoredDistros_.empty() ||
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDistroEnum_).count()
                >= config_.poll_interval_ms;
        if (fullCycle) {
            RefreshMonitoredDistros();
            lastDistroEnum_ = now;
            distroIndex_    = 0;
        }

        // Process exactly one distro this tick (staggered across the interval).
        if (!monitoredDistros_.empty()) {
            if (distroIndex_ >= monitoredDistros_.size()) distroIndex_ = 0;
            ProcessDistro(monitoredDistros_[distroIndex_]);
            ++distroIndex_;
        }

        // UPnP desired-state, rebuilt from all distros' active ports.
        PublishUpnpDesired();

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

        // Stagger: with N monitored distros, query one every poll_interval / N so
        // each distro is still polled once per interval. Floor at 500 ms.
        int n = monitoredDistros_.empty() ? 1 : static_cast<int>(monitoredDistros_.size());
        DWORD wait = static_cast<DWORD>(std::max(500, config_.poll_interval_ms / n));
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

void Wsl2IpFwdService::ApplyPort(const std::string& distro, uint16_t port,
                                 const std::string& wslIp, const PortConfig& cfg) {
    auto& ap = activePorts_[distro][port];

    // Effective local (listen) port: 0 in config means "same as the WSL port";
    // mirrored mode forces the WSL port (custom local ports don't work there).
    uint16_t listenPort = EffectiveLocalPort(port, cfg);

    // If the local port changed, tear down the old rules before rebuilding —
    // but only the OS rule if no other distro still uses that old listen port.
    if (ap.localPort != 0 && ap.localPort != listenPort) {
        if (ap.forwarded) {
            if (!ListenPortForwardedByOther(distro, ap.localPort))
                forwarder_.RemoveRule(ap.localPort, config_.listen_address);
            ap.forwarded = false;
        }
        if (ap.fwRule) {
            if (!ListenPortFirewalledByOther(distro, ap.localPort))
                firewall_.RemoveRule(ap.localPort);
            ap.fwRule = false;
        }
        Log("[" + distro + "] local port for " + std::to_string(port) + " changed to "
            + std::to_string(listenPort) + " — rebuilding rules.");
    }

    if (!ap.forwarded || ap.wslIp != wslIp) {
        // No explicit remove on a WSL-IP change: `netsh portproxy add` overwrites
        // the existing entry atomically, avoiding a gap on a shared listen port.
        if (forwarder_.AddRule(listenPort, port, wslIp, config_.listen_address)) {
            ap.forwarded = true;
            ap.wslIp     = wslIp;
            ap.localPort = listenPort;
            std::string via = (listenPort == port) ? "" : (" (local " + std::to_string(listenPort) + ")");
            Log("[" + distro + "] portproxy " + std::to_string(port) + via + " -> " + wslIp);
        } else {
            // Likely a host listen-port collision with another distro (best-effort
            // until per-distro local ports). Leave forwarded=false.
            Log("[" + distro + "] could not add portproxy on listen port "
                + std::to_string(listenPort) + " (in use by another distro?).");
        }
    }

    // Only open the firewall once forwarding is actually established, so a port
    // that lost a host-port collision doesn't create/clobber a shared rule.
    if (ap.forwarded && !ap.fwRule) {
        long profiles = 0;
        if (cfg.fw_domain)  profiles |= FW_DOMAIN;
        if (cfg.fw_private) profiles |= FW_PRIVATE;
        if (cfg.fw_public)  profiles |= FW_PUBLIC;
        if (profiles && firewall_.AddRule(listenPort, profiles)) {
            ap.fwRule    = true;
            ap.localPort = listenPort;
            Log("[" + distro + "] firewall rule for port " + std::to_string(listenPort));
        }
    }
}

bool Wsl2IpFwdService::ListenPortForwardedByOther(const std::string& distro,
                                                  uint16_t listenPort) const {
    for (auto& [d, ports] : activePorts_) {
        if (d == distro) continue;
        for (auto& [p, ap] : ports)
            if (ap.forwarded && ap.localPort == listenPort) return true;
    }
    return false;
}

bool Wsl2IpFwdService::ListenPortFirewalledByOther(const std::string& distro,
                                                   uint16_t listenPort) const {
    for (auto& [d, ports] : activePorts_) {
        if (d == distro) continue;
        for (auto& [p, ap] : ports)
            if (ap.fwRule && ap.localPort == listenPort) return true;
    }
    return false;
}

void Wsl2IpFwdService::RemovePort(const std::string& distro, uint16_t port) {
    auto dit = activePorts_.find(distro);
    if (dit == activePorts_.end()) return;
    auto it = dit->second.find(port);
    if (it == dit->second.end()) return;
    uint16_t listenPort = it->second.localPort ? it->second.localPort : port;
    if (it->second.forwarded) {
        // Only tear down the shared OS rule if no other distro still uses it
        // (handoff between distros on the same listen port).
        if (!ListenPortForwardedByOther(distro, listenPort)) {
            forwarder_.RemoveRule(listenPort, config_.listen_address);
            Log("[" + distro + "] removed portproxy on port " + std::to_string(listenPort));
        } else {
            Log("[" + distro + "] released portproxy on port " + std::to_string(listenPort)
                + " (still in use by another distribution).");
        }
        it->second.forwarded = false;
    }
    if (it->second.fwRule) {
        if (!ListenPortFirewalledByOther(distro, listenPort)) {
            firewall_.RemoveRule(listenPort);
            Log("[" + distro + "] removed firewall rule on port " + std::to_string(listenPort));
        }
        it->second.fwRule = false;
    }
}

void Wsl2IpFwdService::RemoveDistroRules(const std::string& distro) {
    auto dit = activePorts_.find(distro);
    if (dit == activePorts_.end()) return;
    std::vector<uint16_t> ports;
    for (auto& [port, _] : dit->second) ports.push_back(port);
    for (uint16_t p : ports) RemovePort(distro, p);
}

void Wsl2IpFwdService::RemoveAllRules() {
    for (auto& [distro, ports] : activePorts_) {
        std::vector<uint16_t> ps;
        for (auto& [port, _] : ports) ps.push_back(port);
        for (uint16_t p : ps) RemovePort(distro, p);
    }
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
            auto now = std::chrono::steady_clock::now();
            auto upnpActive = upnp_.ActiveExternalPorts();  // live router-mapping snapshot

            // Union of distros known from detection, config, or active state.
            std::set<std::string> distros;
            for (auto& [d, _] : detectedPorts_)       distros.insert(d);
            for (auto& [d, _] : config_.distro_ports) distros.insert(d);
            for (auto& [d, _] : activePorts_)         distros.insert(d);

            for (const std::string& distro : distros) {
                const auto* det    = detectedPorts_.count(distro) ? &detectedPorts_.at(distro)   : nullptr;
                const auto* dports = config_.distro_ports.count(distro) ? &config_.distro_ports.at(distro) : nullptr;
                const auto* active = activePorts_.count(distro) ? &activePorts_.at(distro)       : nullptr;

                std::set<uint16_t> ports;
                if (det)    for (auto& p : *det)    ports.insert(p.port);
                if (dports) for (auto& [p, _] : *dports) ports.insert(p);
                if (active) for (auto& [p, _] : *active) ports.insert(p);

                for (uint16_t port : ports) {
                    json e;
                    e["distro"]   = distro;
                    e["port"]     = port;
                    e["protocol"] = "tcp";
                    bool detFlag = det && std::any_of(det->begin(), det->end(),
                        [port](const WslPortInfo& x){ return x.port == port; });
                    e["detected"] = detFlag;

                    const ActivePort* ap = nullptr;
                    if (active) { auto it = active->find(port); if (it != active->end()) ap = &it->second; }
                    e["forwarded"]       = ap && ap->forwarded;
                    e["firewall_active"] = ap && ap->fwRule;
                    e["last_seen_ms"]    = ap ? (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    now - ap->lastSeen).count() : -1;

                    PortConfig pc;
                    if (dports) { auto it = dports->find(port); if (it != dports->end()) pc = it->second; }
                    e["config"] = pc;

                    bool upnpActiveFlag = false;
                    if (pc.upnp_enabled) {
                        uint16_t effLocal = EffectiveLocalPort(port, pc);
                        uint16_t effExt   = pc.upnp_remote_port ? pc.upnp_remote_port : effLocal;
                        upnpActiveFlag = upnpActive.count(effExt) > 0;
                    }
                    e["upnp_active"] = upnpActiveFlag;
                    arr.push_back(e);
                }
            }
            res = {{"id", id}, {"ok", true}, {"data", arr}};

        } else if (cmd == proto::CMD_LIST_DISTROS) {
            json arr = json::array();
            for (auto& d : monitor_.ListDistros()) {
                bool enabled = config_.wsl_distros.empty() ||
                    std::find(config_.wsl_distros.begin(), config_.wsl_distros.end(), d.name)
                        != config_.wsl_distros.end();
                arr.push_back({{"name", d.name}, {"running", d.running},
                               {"default", d.isDefault}, {"enabled", enabled}});
            }
            res = {{"id", id}, {"ok", true}, {"data", arr}};

        } else if (cmd == proto::CMD_GET_CONFIG) {
            res = {{"id", id}, {"ok", true}, {"data", config_}};

        } else if (cmd == proto::CMD_SET_CONFIG) {
            GlobalConfig newCfg = req.at("data").get<GlobalConfig>();
            // Preserve service-managed state the GUI doesn't send.
            newCfg.distro_ports           = config_.distro_ports;
            newCfg.config_version         = config_.config_version;
            newCfg.pending_update_version = config_.pending_update_version;
            newCfg.pending_update_url     = config_.pending_update_url;
            newCfg.update_last_check_utc  = config_.update_last_check_utc;
            config_ = newCfg;

            // Prune ports the (possibly updated) whitelist/blacklist now filters
            // out, across all distros.
            for (auto& [distro, dports] : config_.distro_ports) {
                std::vector<uint16_t> filteredOut;
                for (auto& [port, _] : dports)
                    if (IsPortFilteredOut(port, distro)) filteredOut.push_back(port);
                for (uint16_t port : filteredOut) {
                    RemovePort(distro, port);
                    dports.erase(port);
                    if (activePorts_.count(distro)) activePorts_[distro].erase(port);
                    if (seenPorts_.count(distro))   seenPorts_[distro].erase(port);
                    Log("[" + distro + "] port " + std::to_string(port) +
                        " removed (filtered out by updated whitelist/blacklist).");
                }
            }

            Config::Save(config_);
            res = {{"id", id}, {"ok", true}};

        } else if (cmd == proto::CMD_SET_PORT_CFG) {
            std::string distro = req.value("distro", std::string{});
            uint16_t port = req.at("port").get<uint16_t>();
            PortConfig cfg = req.at("config").get<PortConfig>();
            config_.distro_ports[distro][port] = cfg;
            Config::Save(config_);
            if (!cfg.enabled) RemovePort(distro, port);
            seenPorts_[distro].insert(port);
            res = {{"id", id}, {"ok", true}};

        } else if (cmd == proto::CMD_GET_STATUS) {
            auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - startTime_).count();
            // Summary across distros: first known IP, count of running distros,
            // total active forwardings.
            std::string anyIp;
            for (auto& [d, ip] : distroIps_) { if (!ip.empty()) { anyIp = ip; break; } }
            int forwardings = 0;
            for (auto& [d, ports] : activePorts_)
                for (auto& [p, ap] : ports) if (ap.forwarded) ++forwardings;

            json d;
            d["wsl_ip"]            = anyIp;
            d["wsl_running"]       = !distroIps_.empty();
            d["distro_count"]      = (int)distroIps_.size();
            d["service_uptime_s"]  = uptime;
            d["active_forwardings"]= forwardings;
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
            std::string distro = req.value("distro", std::string{});
            uint16_t port = req.at("port").get<uint16_t>();
            RemovePort(distro, port);
            config_.distro_ports[distro].erase(port);
            if (activePorts_.count(distro)) activePorts_[distro].erase(port);
            if (seenPorts_.count(distro))   seenPorts_[distro].erase(port);
            Config::Save(config_);
            Log("[" + distro + "] removed port " + std::to_string(port) + " from config.");
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
