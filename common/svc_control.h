#pragma once
#include <string>

// Windows service identity, shared by the service host (main.cpp) and the
// standalone updater.
inline constexpr wchar_t SERVICE_NAME[]    = L"wsl2ipfwd";
inline constexpr wchar_t SERVICE_DISPLAY[] = L"WSL2 IP Forwarder Service";

// Thin SCM wrappers used to install / remove / start / stop the service.
// All are synchronous and return false on failure.
namespace svc {
    // Register the service (auto-start) pointing at serviceExePath. Returns true
    // if it was created or already exists.
    bool Install(const std::wstring& serviceExePath);

    // Stop (if running) and delete the service. Returns true if removed or absent.
    bool Uninstall();

    bool Start();

    // Stop the service and wait until it reaches STOPPED (or timeout).
    bool Stop(unsigned timeoutMs = 15000);

    bool IsInstalled();
}
