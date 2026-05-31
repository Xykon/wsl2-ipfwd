#include "service.h"
#include "../common/svc_control.h"
#include <string>

// Full path to this executable (registered as the service binary).
static std::wstring SelfPath() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2) {
        std::wstring arg = argv[1];
        if (arg == L"--install")   { return svc::Install(SelfPath()) ? 0 : 1; }
        if (arg == L"--uninstall") { return svc::Uninstall()         ? 0 : 1; }
        if (arg == L"--start")     { return svc::Start()             ? 0 : 1; }
        if (arg == L"--stop")      { return svc::Stop()              ? 0 : 1; }
        if (arg == L"--install-and-start") {
            // Install then immediately start — used by the GUI so both steps
            // happen inside the single already-elevated process (one UAC prompt).
            if (!svc::Install(SelfPath())) return 1;
            Sleep(200); // brief pause for SCM to register the new service
            return svc::Start() ? 0 : 1;
        }
        if (arg == L"--restart") {
            // Stop then start — used by the GUI as a single elevated call so
            // only one UAC prompt is needed when the GUI is not already admin.
            svc::Stop(); // ignore failure (service may already be stopped)
            Sleep(1500);
            return svc::Start() ? 0 : 1;
        }
        if (arg == L"--debug") {
            // Run service logic interactively (console, no SCM).
            // Add --trace to log every poll cycle; without it only changes are logged.
            bool trace = false;
            for (int i = 2; i < argc; ++i)
                if (std::wstring(argv[i]) == L"--trace") trace = true;
            Wsl2IpFwdService::Instance().RunInteractive(trace);
            return 0;
        }
    }

    // Default: hand off to SCM (called by SCM when service starts)
    Wsl2IpFwdService::Instance().RunAsService();
    return 0;
}
