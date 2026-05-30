#include "service.h"
#include <iostream>
#include <string>

// Install the service into SCM
static bool InstallService() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        std::wcerr << L"OpenSCManager failed: " << GetLastError() << L"\n";
        return false;
    }

    std::wstring binPath = std::wstring(L"\"") + path + L"\"";

    SC_HANDLE svc = CreateServiceW(
        scm, SERVICE_NAME, SERVICE_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,           // Start automatically at boot
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr, nullptr, nullptr,
        nullptr, nullptr);            // Run as LocalSystem

    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        if (err == ERROR_SERVICE_EXISTS) {
            std::wcout << L"Service already installed.\n";
            return true;
        }
        std::wcerr << L"CreateService failed: " << err << L"\n";
        return false;
    }

    // Set description
    SERVICE_DESCRIPTIONW desc = {};
    desc.lpDescription = const_cast<LPWSTR>(
        L"Monitors WSL2 ports and manages Windows port forwarding and firewall rules.");
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    std::wcout << L"Service installed successfully.\n";
    return true;
}

static bool UninstallService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (!svc) {
        CloseServiceHandle(scm);
        std::wcerr << L"Service not found.\n";
        return false;
    }

    // Try to stop it first
    SERVICE_STATUS st = {};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    Sleep(1000);

    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    if (ok) std::wcout << L"Service uninstalled.\n";
    else    std::wcerr << L"DeleteService failed: " << GetLastError() << L"\n";
    return ok != FALSE;
}

static bool StartSvc() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_START);
    if (!svc) { CloseServiceHandle(scm); return false; }
    BOOL ok = StartServiceW(svc, 0, nullptr);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING;
}

static bool StopSvc() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_STOP);
    if (!svc) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS st = {};
    BOOL ok = ControlService(svc, SERVICE_CONTROL_STOP, &st);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok != FALSE;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2) {
        std::wstring arg = argv[1];
        if (arg == L"--install")   { return InstallService()   ? 0 : 1; }
        if (arg == L"--uninstall") { return UninstallService() ? 0 : 1; }
        if (arg == L"--start")     { return StartSvc()         ? 0 : 1; }
        if (arg == L"--stop")      { return StopSvc()          ? 0 : 1; }
        if (arg == L"--install-and-start") {
            // Install then immediately start — used by the GUI so both steps
            // happen inside the single already-elevated process (one UAC prompt).
            if (!InstallService()) return 1;
            Sleep(200); // brief pause for SCM to register the new service
            return StartSvc() ? 0 : 1;
        }
        if (arg == L"--restart") {
            // Stop then start — used by the GUI as a single elevated call so
            // only one UAC prompt is needed when the GUI is not already admin.
            StopSvc(); // ignore failure (service may already be stopped)
            Sleep(1500);
            return StartSvc() ? 0 : 1;
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
