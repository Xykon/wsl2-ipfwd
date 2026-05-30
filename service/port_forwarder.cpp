#include "port_forwarder.h"
#include <windows.h>
#include <string>
#include <sstream>

static std::wstring ToWide(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

int PortForwarder::RunNetsh(const std::wstring& args, std::wstring* output) {
    std::wstring cmdLine = L"netsh " + args;

    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = INVALID_HANDLE_VALUE, hWrite = INVALID_HANDLE_VALUE;
    if (output) {
        CreatePipe(&hRead, &hWrite, &sa, 0);
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (output) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION pi = {};
    std::wstring buf = cmdLine;
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr,
                             output ? TRUE : FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (output) CloseHandle(hWrite);
    if (!ok) { if (output) CloseHandle(hRead); return -1; }

    if (output) {
        char tmp[4096];
        DWORD n;
        std::string out;
        while (ReadFile(hRead, tmp, sizeof(tmp) - 1, &n, nullptr) && n > 0) {
            tmp[n] = '\0';
            out += tmp;
        }
        CloseHandle(hRead);
        *output = std::wstring(out.begin(), out.end());
    }

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

bool PortForwarder::AddRule(uint16_t listenPort, uint16_t connectPort,
                             const std::string& wslIp, const std::string& listenAddr) {
    std::wostringstream cmd;
    cmd << L"interface portproxy add v4tov4"
        << L" listenaddress=" << ToWide(listenAddr)
        << L" listenport="    << listenPort
        << L" connectaddress="<< ToWide(wslIp)
        << L" connectport="   << connectPort;
    return RunNetsh(cmd.str()) == 0;
}

bool PortForwarder::RemoveRule(uint16_t listenPort, const std::string& listenAddr) {
    std::wostringstream cmd;
    cmd << L"interface portproxy delete v4tov4"
        << L" listenaddress=" << ToWide(listenAddr)
        << L" listenport="    << listenPort;
    return RunNetsh(cmd.str()) == 0;
}

bool PortForwarder::RuleExists(uint16_t listenPort, const std::string& listenAddr) {
    std::wstring output;
    RunNetsh(L"interface portproxy show v4tov4", &output);
    // Output lines contain "listenaddr  listenport  connectaddr  connectport"
    std::wstring portStr = std::to_wstring(listenPort);
    std::wstring addrStr = ToWide(listenAddr);
    // Simple check: both the address and port appear on the same line
    std::wistringstream ss(output);
    std::wstring line;
    while (std::getline(ss, line)) {
        if (line.find(addrStr) != std::wstring::npos &&
            line.find(portStr) != std::wstring::npos)
            return true;
    }
    return false;
}
