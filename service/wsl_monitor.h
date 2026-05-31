#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct WslPortInfo {
    uint16_t    port;
    std::string protocol; // "tcp" or "tcp6"
};

struct WslDistroInfo {
    std::string name;
    bool        running   = false;
    bool        isDefault = false;
};

class WslMonitor {
public:
    WslMonitor() = default;
    ~WslMonitor();

    // Per-distro queries (empty distro = the default distro).
    std::vector<WslPortInfo> GetListeningPorts(const std::string& distro);
    std::string              GetWslIp(const std::string& distro);

    // Distro enumeration (via the wsl.exe CLI).
    std::vector<WslDistroInfo> ListDistros();
    std::vector<std::string>   RunningDistros();
    std::string                DefaultDistro();

    // Provide a primary user token so WSL commands run in the logged-in user's
    // session rather than the SYSTEM context.  Takes ownership of the handle.
    void SetUserToken(HANDLE hPrimaryToken);

private:
    HANDLE userToken_ = nullptr;

    // Capture a child process's stdout as raw bytes (uses the user token if set).
    std::string CaptureProcess(const std::wstring& cmdLine);

    // Run a command inside a distro: `wsl -d <distro> [-u root] -- sh -c "<cmd>"`
    // (UTF-8 out). asRoot is required for ss to resolve process ownership of
    // sockets owned by other users.
    std::string RunWslCommand(const std::string& distro, const std::string& cmd,
                              bool asRoot = false);

    // Run the wsl.exe CLI: `wsl.exe <args>` (output is UTF-16LE -> decoded narrow).
    std::string RunWslCli(const std::wstring& args);

    // Parse `ss -Htlnp` output, keeping only sockets OWNED by a process in this
    // distro (i.e. lines with a resolvable users:((...)) field). Because all WSL2
    // distros share one network namespace, every distro sees every socket; the
    // owning process is only visible in the distro it actually runs in.
    void ParseSsListening(const std::string& output, std::vector<WslPortInfo>& result);

    // Legacy fallback for distros without the `ss` tool: parse /proc/net/tcp.
    // Cannot attribute ownership (lists all sockets in the shared namespace).
    void ParseProcNetTcp(const std::string& output, const std::string& proto,
                         std::vector<WslPortInfo>& result);
};
