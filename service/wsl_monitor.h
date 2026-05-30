#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

struct WslPortInfo {
    uint16_t    port;
    std::string protocol; // "tcp" or "tcp6"
};

class WslMonitor {
public:
    explicit WslMonitor(std::string distro = {});
    ~WslMonitor();

    // Returns currently listening ports inside WSL2
    std::vector<WslPortInfo> GetListeningPorts();

    // Returns the WSL2 VM IP address (e.g. "172.29.x.x")
    std::string GetWslIp();

    // Returns true if WSL2 appears to be running
    bool IsWslRunning();

    void SetDistro(std::string distro) { distro_ = std::move(distro); }

    // Provide a primary user token so WSL commands run in the logged-in user's
    // session rather than the SYSTEM context.  Takes ownership of the handle.
    // Pass nullptr to clear (reverts to direct CreateProcessW).
    void SetUserToken(HANDLE hPrimaryToken);

private:
    std::string distro_;
    HANDLE      userToken_ = nullptr;

    // Runs a command inside WSL and captures stdout
    std::string RunWslCommand(const std::string& cmd);

    // Parses /proc/net/tcp or tcp6 output, appends to result
    void ParseProcNetTcp(const std::string& output, const std::string& proto,
                         std::vector<WslPortInfo>& result);
};
