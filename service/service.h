#pragma once
#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <string>
#include <map>
#include <set>
#include <atomic>
#include <thread>
#include <chrono>
#include "../common/config.h"
#include "wsl_monitor.h"
#include "port_forwarder.h"
#include "firewall.h"
#include "ipc_server.h"
#include "update_checker.h"
#include "upnp_manager.h"
#include "../common/svc_control.h"   // SERVICE_NAME / SERVICE_DISPLAY + svc::*

struct ActivePort {
    std::chrono::steady_clock::time_point lastSeen;
    bool forwarded  = false;
    bool fwRule     = false;
    std::string wslIp;       // IP used when rule was created
    uint16_t    localPort = 0; // listen/firewall port the rules were created on
};

// WSL2 networking mode (read from the user's .wslconfig).
//   Nat         — full forwarding support (default)
//   Mirrored    — WSL shares the host IP; same-port forwarding only
//   VirtioProxy — LAN forwarding impossible; the service does nothing
enum class NetMode { Nat = 0, Mirrored = 1, VirtioProxy = 2 };

class Wsl2IpFwdService {
public:
    static Wsl2IpFwdService& Instance();

    void RunAsService();
    void RunInteractive(bool trace = false); // for debugging outside SCM

    // SCM callbacks (must be public for static trampolines)
    void ServiceMain(DWORD argc, LPWSTR* argv);
    void CtrlHandler(DWORD ctrl);

private:
    Wsl2IpFwdService();

    void ReportStatus(DWORD state, DWORD waitHint = 0);
    void ClearStalePendingUpdate();
    void MonitorLoop();
    void ApplyPort(uint16_t port, const std::string& wslIp, const PortConfig& cfg);
    void RemovePort(uint16_t port);
    // True if the whitelist/blacklist filter says this port should be ignored.
    bool IsPortFilteredOut(uint16_t port) const;
    // Windows-side listen port for a config entry. In mirrored/virtioproxy modes
    // custom local ports don't work (the portproxy can't hairpin to the shared
    // host IP), so this returns the WSL port and ignores cfg.local_port.
    uint16_t EffectiveLocalPort(uint16_t port, const PortConfig& cfg) const;

    // Determine the current networking mode (primarily from the user's
    // .wslconfig; falls back to the IP heuristic when unreadable).
    void RefreshNetMode(const std::string& wslIp);
    std::wstring UserWslConfigPath();
    void RemoveAllRules();
    std::string HandleRequest(const std::string& json);

    // Show a Windows notification balloon in the active user's session
    void NotifyUser(uint16_t port);
    void NotifyUserUpdate(const std::string& version);

    // Shared helper: launch wsl2ipfwd-notify.exe with the given command line in
    // the active interactive user's session.
    void LaunchNotifyProcess(const std::wstring& cmdLine);

    // Obtain/refresh the active user's token and pass it to WslMonitor so that
    // wsl.exe commands run in the user's session (not SYSTEM's).
    void RefreshUserToken();

    // Logging
    void Log(const std::string& msg);
    void OpenLog();

    SERVICE_STATUS_HANDLE statusHandle_{nullptr};
    SERVICE_STATUS        status_{};

    std::atomic<bool>  running_{false};
    HANDLE             stopEvent_{INVALID_HANDLE_VALUE};
    std::thread        monitorThread_;
    std::chrono::steady_clock::time_point startTime_;

    GlobalConfig         config_;
    std::map<uint16_t, ActivePort> activePorts_;
    std::string          currentWslIp_;

    // Networking mode (read from .wslconfig; IP heuristic as fallback).
    std::atomic<int>     netMode_{0};       // NetMode value
    std::wstring         wslConfigPath_;    // cached path to the user's .wslconfig

    // New-port notification tracking
    std::set<uint16_t>    seenPorts_;        // ports already processed this session
    std::string           notifiedUpdateVer_; // version for which we've already shown a balloon

    // Update checker
    UpdateChecker         updater_;
    std::atomic<bool>     pendingUpdateCheck_{false}; // set by CMD_CHECK_UPDATE_NOW handler

    WslMonitor     monitor_;
    PortForwarder  forwarder_;
    FirewallManager firewall_;
    IpcServer      ipcServer_;
    UpnpManager    upnp_;

    HANDLE logFile_{INVALID_HANDLE_VALUE};
    bool   interactive_{false}; // true when running via --debug (prints to stdout)
    bool   trace_{false};       // true when --trace is also passed: log every poll cycle

    // Change-detection state for --debug (non-trace) mode
    std::set<uint16_t> lastPollPorts_;    // port set from the previous poll
    std::string        lastLoggedWslIp_;  // WSL IP from the previous poll
};
