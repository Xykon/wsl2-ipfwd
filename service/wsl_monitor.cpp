#include "wsl_monitor.h"
#include <userenv.h>
#include <sstream>
#include <algorithm>
#include <set>

WslMonitor::WslMonitor(std::string distro) : distro_(std::move(distro)) {}

WslMonitor::~WslMonitor() {
    if (userToken_ && userToken_ != INVALID_HANDLE_VALUE)
        CloseHandle(userToken_);
}

void WslMonitor::SetUserToken(HANDLE hToken) {
    if (userToken_ && userToken_ != INVALID_HANDLE_VALUE)
        CloseHandle(userToken_);
    userToken_ = hToken;
}

// Run a shell command inside WSL and return stdout as string.
// When userToken_ is set, the process is created in the logged-in user's
// session (required because WSL2 belongs to the user, not SYSTEM).
std::string WslMonitor::RunWslCommand(const std::string& cmd) {
    std::wstring wcmd = L"wsl.exe";
    if (!distro_.empty()) {
        wcmd += L" -d ";
        wcmd += std::wstring(distro_.begin(), distro_.end());
    }
    wcmd += L" -- sh -c \"" + std::wstring(cmd.begin(), cmd.end()) + L"\"";

    // Pipe for capturing stdout
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = INVALID_HANDLE_VALUE, hWritePipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return {};
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0); // read end not inherited

    // NUL device for stdin and stderr — avoids inheriting invalid service handles
    HANDLE hNull = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb        = sizeof(si);
    si.dwFlags   = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = hWritePipe;
    si.hStdError  = (hNull != INVALID_HANDLE_VALUE) ? hNull : hWritePipe;
    si.hStdInput  = (hNull != INVALID_HANDLE_VALUE) ? hNull : nullptr;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdBuf = wcmd;
    BOOL ok = FALSE;
    LPVOID envBlock = nullptr;

    if (userToken_) {
        // Run as the logged-in user so wsl.exe connects to their WSL instance
        CreateEnvironmentBlock(&envBlock, userToken_, FALSE);
        ok = CreateProcessAsUserW(
            userToken_, nullptr, cmdBuf.data(),
            nullptr, nullptr, TRUE,               // inherit handles
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            envBlock, nullptr, &si, &pi);
    }

    if (!ok) {
        // Fallback: run in current process context (works in --debug / interactive mode)
        ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    }

    // Close handles the child has inherited / no longer needed in parent
    CloseHandle(hWritePipe);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    if (envBlock) DestroyEnvironmentBlock(envBlock);

    if (!ok) { CloseHandle(hReadPipe); return {}; }

    std::string output;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }
    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
}

// /proc/net/tcp format (hex, space-separated):
//   sl  local_address rem_address st  ...
//   0: 0100007F:0035  00000000:0000  0A ...
// local_address = XXXXXXXX:PPPP, port is last 4 hex chars, state 0A = LISTEN
void WslMonitor::ParseProcNetTcp(const std::string& output, const std::string& proto,
                                  std::vector<WslPortInfo>& result) {
    std::istringstream ss(output);
    std::string line;
    bool firstLine = true;
    std::set<uint16_t> seen;
    for (auto& e : result) seen.insert(e.port); // avoid duplicates from tcp/tcp6

    while (std::getline(ss, line)) {
        if (firstLine) { firstLine = false; continue; } // skip header
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        line = line.substr(p);

        std::istringstream ls(line);
        std::string sl, local, rem, st;
        ls >> sl >> local >> rem >> st;
        if (st != "0A" && st != "0a") continue; // only LISTEN

        size_t colon = local.find(':');
        if (colon == std::string::npos) continue;
        std::string portHex = local.substr(colon + 1);
        if (portHex.empty()) continue;
        uint16_t port = static_cast<uint16_t>(std::stoul(portHex, nullptr, 16));
        if (port == 0) continue;
        if (seen.count(port)) continue;
        seen.insert(port);
        result.push_back({port, proto});
    }
}

std::vector<WslPortInfo> WslMonitor::GetListeningPorts() {
    std::vector<WslPortInfo> result;
    std::string out = RunWslCommand(
        "cat /proc/net/tcp 2>/dev/null && echo '---TCP6---' && cat /proc/net/tcp6 2>/dev/null");
    if (out.empty()) return result;

    size_t split = out.find("---TCP6---");
    if (split == std::string::npos) {
        ParseProcNetTcp(out, "tcp", result);
    } else {
        ParseProcNetTcp(out.substr(0, split), "tcp", result);
        ParseProcNetTcp(out.substr(split + 10), "tcp6", result);
    }
    return result;
}

std::string WslMonitor::GetWslIp() {
    std::string out = RunWslCommand("hostname -I 2>/dev/null");
    if (out.empty()) return {};
    // Take first whitespace-delimited token
    size_t end = out.find_first_of(" \t\r\n");
    if (end != std::string::npos) out = out.substr(0, end);
    // Validate: must be a dotted-decimal IPv4 address (digits and dots only,
    // at least one dot).  This filters out any wsl.exe error messages.
    if (out.find('.') == std::string::npos) return {};
    for (char c : out)
        if (!isdigit(static_cast<unsigned char>(c)) && c != '.') return {};
    return out;
}

bool WslMonitor::IsWslRunning() {
    return !GetWslIp().empty();
}
