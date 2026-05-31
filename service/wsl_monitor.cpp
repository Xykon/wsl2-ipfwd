#include "wsl_monitor.h"
#include <userenv.h>
#include <sstream>
#include <algorithm>
#include <set>

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

// Decode the UTF-16LE bytes produced by the wsl.exe CLI into UTF-8.
std::string Utf16ToUtf8(const std::string& bytes) {
    size_t off = 0;
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0xFF && (unsigned char)bytes[1] == 0xFE)
        off = 2; // skip BOM
    int wlen = static_cast<int>((bytes.size() - off) / 2);
    if (wlen <= 0) return {};
    const wchar_t* wptr = reinterpret_cast<const wchar_t*>(bytes.data() + off);
    int n = WideCharToMultiByte(CP_UTF8, 0, wptr, wlen, nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wptr, wlen, out.data(), n, nullptr, nullptr);
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> SplitLines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string line;
    while (std::getline(ss, line)) {
        std::string t = Trim(line);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

} // namespace

WslMonitor::~WslMonitor() {
    if (userToken_ && userToken_ != INVALID_HANDLE_VALUE)
        CloseHandle(userToken_);
}

void WslMonitor::SetUserToken(HANDLE hToken) {
    if (userToken_ && userToken_ != INVALID_HANDLE_VALUE)
        CloseHandle(userToken_);
    userToken_ = hToken;
}

std::string WslMonitor::CaptureProcess(const std::wstring& cmdLine) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = INVALID_HANDLE_VALUE, hWritePipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return {};
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE hNull = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si = {};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hWritePipe;
    si.hStdError   = (hNull != INVALID_HANDLE_VALUE) ? hNull : hWritePipe;
    si.hStdInput   = (hNull != INVALID_HANDLE_VALUE) ? hNull : nullptr;

    PROCESS_INFORMATION pi = {};
    std::wstring cmdBuf = cmdLine;
    BOOL ok = FALSE;
    LPVOID envBlock = nullptr;

    if (userToken_) {
        CreateEnvironmentBlock(&envBlock, userToken_, FALSE);
        ok = CreateProcessAsUserW(
            userToken_, nullptr, cmdBuf.data(),
            nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
            envBlock, nullptr, &si, &pi);
    }
    if (!ok) {
        ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr,
                            TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    }

    CloseHandle(hWritePipe);
    if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);
    if (envBlock) DestroyEnvironmentBlock(envBlock);

    if (!ok) { CloseHandle(hReadPipe); return {}; }

    std::string output;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0)
        output.append(buf, bytesRead);
    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, 10000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return output;
}

std::string WslMonitor::RunWslCommand(const std::string& distro, const std::string& cmd,
                                      bool asRoot) {
    // NB: do NOT quote the distro name. wsl.exe does its own command-line
    // parsing and treats surrounding double-quotes literally, yielding
    // "no distribution with the supplied name" (Wsl/Service/WSL_E_DISTRO_NOT_FOUND).
    std::wstring wcmd = L"wsl.exe";
    if (!distro.empty()) {
        wcmd += L" -d ";
        wcmd += Utf8ToWide(distro);
    }
    if (asRoot) wcmd += L" -u root";
    wcmd += L" -- sh -c \"" + Utf8ToWide(cmd) + L"\"";
    return CaptureProcess(wcmd);   // Linux command output is UTF-8
}

std::string WslMonitor::RunWslCli(const std::wstring& args) {
    return Utf16ToUtf8(CaptureProcess(L"wsl.exe " + args));
}

void WslMonitor::ParseProcNetTcp(const std::string& output, const std::string& proto,
                                  std::vector<WslPortInfo>& result) {
    std::istringstream ss(output);
    std::string line;
    bool firstLine = true;
    std::set<uint16_t> seen;
    for (auto& e : result) seen.insert(e.port);

    while (std::getline(ss, line)) {
        if (firstLine) { firstLine = false; continue; }
        size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) continue;
        line = line.substr(p);

        std::istringstream ls(line);
        std::string sl, local, rem, st;
        ls >> sl >> local >> rem >> st;
        if (st != "0A" && st != "0a") continue;

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

void WslMonitor::ParseSsListening(const std::string& output,
                                  std::vector<WslPortInfo>& result) {
    std::set<uint16_t> seen;
    for (auto& e : result) seen.insert(e.port);

    std::istringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        // Ownership filter: keep only sockets owned by a process in THIS distro.
        // All distros share one network namespace, so every distro sees every
        // socket; only the owner's `ss -p` resolves a users:((...)) field.
        if (line.find("users:((") == std::string::npos) continue;

        // Columns: State Recv-Q Send-Q Local:Port Peer:* users:((...))
        std::istringstream ls(line);
        std::string state, recvq, sendq, local;
        if (!(ls >> state >> recvq >> sendq >> local)) continue;

        size_t colon = local.find_last_of(':');
        if (colon == std::string::npos) continue;
        std::string portStr = local.substr(colon + 1);
        if (portStr.empty()) continue;

        uint16_t port = 0;
        try { port = static_cast<uint16_t>(std::stoul(portStr)); }
        catch (...) { continue; }
        if (port == 0) continue;

        // Skip sockets bound only to loopback — they can't be reached from the
        // Windows host through portproxy, so forwarding them is meaningless.
        // The bind address is everything before the final ':'; strip any IPv6
        // brackets and zone id (e.g. 127.0.0.53%lo, [::1]).
        std::string addr = local.substr(0, colon);
        size_t pct = addr.find('%');
        if (pct != std::string::npos) addr = addr.substr(0, pct);
        if (!addr.empty() && addr.front() == '[') addr.erase(0, 1);
        if (!addr.empty() && addr.back()  == ']') addr.pop_back();
        bool loopback = addr.rfind("127.", 0) == 0          // 127.0.0.0/8
                     || addr == "::1"                        // IPv6 loopback
                     || addr.rfind("::ffff:127.", 0) == 0;   // v4-mapped loopback
        if (loopback) continue;

        // Dedup after the loopback check so a 0.0.0.0 bind still wins over a
        // same-port loopback bind regardless of line order.
        if (seen.count(port)) continue;
        seen.insert(port);

        // IPv6 local addresses are bracketed, e.g. [::]:443 or [2001:db8::1]:80.
        std::string proto = (local.find('[') != std::string::npos) ? "tcp6" : "tcp";
        result.push_back({ port, proto });
    }
}

std::vector<WslPortInfo> WslMonitor::GetListeningPorts(const std::string& distro) {
    std::vector<WslPortInfo> result;

    // Prefer `ss -tlnp` (run as root) so we can attribute each listening socket
    // to its owning distro via the process field. If `ss` is unavailable, emit a
    // NOSS marker and fall back to /proc/net/tcp (which cannot attribute owners).
    std::string out = RunWslCommand(distro,
        "if command -v ss >/dev/null 2>&1; then ss -Htlnp; else echo __NOSS__; fi",
        /*asRoot=*/true);

    if (out.find("__NOSS__") == std::string::npos) {
        ParseSsListening(out, result);
        return result;
    }

    // ---- Fallback: no `ss` in this distro -----------------------------------
    // Cannot determine ownership; report all sockets in the shared namespace.
    std::string tcp = RunWslCommand(distro,
        "cat /proc/net/tcp 2>/dev/null && echo '---TCP6---' && cat /proc/net/tcp6 2>/dev/null");
    if (tcp.empty()) return result;

    size_t split = tcp.find("---TCP6---");
    if (split == std::string::npos) {
        ParseProcNetTcp(tcp, "tcp", result);
    } else {
        ParseProcNetTcp(tcp.substr(0, split), "tcp", result);
        ParseProcNetTcp(tcp.substr(split + 10), "tcp6", result);
    }
    return result;
}

std::string WslMonitor::GetWslIp(const std::string& distro) {
    std::string out = RunWslCommand(distro, "hostname -I 2>/dev/null");
    if (out.empty()) return {};
    size_t end = out.find_first_of(" \t\r\n");
    if (end != std::string::npos) out = out.substr(0, end);
    if (out.find('.') == std::string::npos) return {};
    for (char c : out)
        if (!isdigit(static_cast<unsigned char>(c)) && c != '.') return {};
    return out;
}

std::vector<std::string> WslMonitor::RunningDistros() {
    return SplitLines(RunWslCli(L"--list --running --quiet"));
}

std::string WslMonitor::DefaultDistro() {
    // The verbose listing marks the default distro with a leading '*'
    // (locale-independent). Columns: [*] NAME  STATE  VERSION.
    std::string out = RunWslCli(L"--list --verbose");
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        size_t star = line.find('*');
        if (star == std::string::npos) continue;
        std::istringstream ls(line.substr(star + 1));
        std::string name;
        ls >> name;        // first whitespace-delimited token after '*'
        if (!name.empty()) return name;
    }
    return {};
}

std::vector<WslDistroInfo> WslMonitor::ListDistros() {
    std::vector<WslDistroInfo> result;
    std::vector<std::string> all = SplitLines(RunWslCli(L"--list --quiet"));
    std::vector<std::string> runningV = RunningDistros();
    std::set<std::string> running(runningV.begin(), runningV.end());
    std::string def = DefaultDistro();

    for (auto& name : all) {
        if (name.empty()) continue;
        result.push_back({ name, running.count(name) > 0, name == def });
    }
    return result;
}
