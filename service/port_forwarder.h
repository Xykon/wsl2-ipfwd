#pragma once
#include <string>
#include <cstdint>

class PortForwarder {
public:
    // Add a v4tov4 portproxy rule: listen on listenAddr:listenPort, forward to
    // wslIp:connectPort.  listenPort and connectPort may differ (custom local port).
    bool AddRule(uint16_t listenPort, uint16_t connectPort, const std::string& wslIp,
                 const std::string& listenAddr = "0.0.0.0");

    // Remove a v4tov4 portproxy rule (identified by its listen port).
    bool RemoveRule(uint16_t listenPort, const std::string& listenAddr = "0.0.0.0");

    // Check whether a rule exists (by querying current portproxy table).
    bool RuleExists(uint16_t listenPort, const std::string& listenAddr = "0.0.0.0");

private:
    // Run "netsh <args>" elevated (service runs as SYSTEM) and return exit code
    int RunNetsh(const std::wstring& args, std::wstring* output = nullptr);
};
