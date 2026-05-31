#include "svc_control.h"
#include <windows.h>

namespace svc {

bool Install(const std::wstring& serviceExePath) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) return false;

    std::wstring binPath = L"\"" + serviceExePath + L"\"";

    SC_HANDLE svc = CreateServiceW(
        scm, SERVICE_NAME, SERVICE_DISPLAY,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        binPath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);   // LocalSystem

    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        return err == ERROR_SERVICE_EXISTS;
    }

    SERVICE_DESCRIPTIONW desc = {};
    desc.lpDescription = const_cast<LPWSTR>(
        L"Monitors WSL2 ports and manages Windows port forwarding and firewall rules.");
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool Uninstall() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;

    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_STOP | DELETE);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        // Already gone counts as success.
        return err == ERROR_SERVICE_DOES_NOT_EXIST;
    }

    SERVICE_STATUS st = {};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);
    Sleep(1000);

    BOOL ok = DeleteService(svc);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok != FALSE;
}

bool Start() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_START);
    if (!svc) { CloseServiceHandle(scm); return false; }
    BOOL ok = StartServiceW(svc, 0, nullptr);
    DWORD err = GetLastError();
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return ok || err == ERROR_SERVICE_ALREADY_RUNNING;
}

bool Stop(unsigned timeoutMs) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!svc) {
        DWORD err = GetLastError();
        CloseServiceHandle(scm);
        return err == ERROR_SERVICE_DOES_NOT_EXIST;   // nothing to stop
    }

    SERVICE_STATUS st = {};
    ControlService(svc, SERVICE_CONTROL_STOP, &st);

    // Wait for the service to actually reach STOPPED so its exe is unlocked.
    DWORD waited = 0;
    const DWORD step = 250;
    while (QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_STOPPED) {
        if (waited >= timeoutMs) break;
        Sleep(step);
        waited += step;
    }
    bool stopped = (st.dwCurrentState == SERVICE_STOPPED);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return stopped;
}

bool IsInstalled() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    bool exists = (svc != nullptr);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return exists;
}

} // namespace svc
