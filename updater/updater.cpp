// wsl2ipfwd-updater.exe — elevated helper for portable updates.
//
// Manifested as requireAdministrator (see CMakeLists.txt), so it always runs
// elevated. Two modes:
//
//   updater.exe
//       Standalone "set up this copy": stop + uninstall any existing service,
//       then install + start the service from this exe's own folder. Used to
//       register a freshly-extracted portable copy.
//
//   updater.exe --update --dest "<dir>" [--wait-pid <pid>]
//       GUI-driven update: wait for the GUI (pid) to exit, stop the service,
//       copy every file from this exe's folder (the temp-extracted zip) into
//       <dir>, then reinstall + start the service from <dir>. <dir> is the
//       directory the running app lives in (Program Files for an installed
//       copy, or the portable folder otherwise).

#include <windows.h>
#include <shellapi.h>
#include "../common/svc_control.h"
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kTitle[]   = L"WSL2 IP Forwarder Updater";
constexpr wchar_t kServiceExe[] = L"wsl2ipfwd-service.exe";
constexpr wchar_t kGuiExe[]     = L"wsl2ipfwd-gui-cs.exe";

std::wstring SelfDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return fs::path(path).parent_path().wstring();
}

// Returns the value following `flag` in argv, or empty if absent.
std::wstring ArgValue(const std::vector<std::wstring>& args, const std::wstring& flag) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == flag) return args[i + 1];
    return L"";
}
bool HasFlag(const std::vector<std::wstring>& args, const std::wstring& flag) {
    for (auto& a : args) if (a == flag) return true;
    return false;
}

void Fail(const std::wstring& msg) {
    MessageBoxW(nullptr, msg.c_str(), kTitle, MB_OK | MB_ICONERROR);
}

// Wait for a process (by pid) to exit, up to timeoutMs.
void WaitForPid(DWORD pid, DWORD timeoutMs) {
    if (pid == 0) return;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return;   // already gone
    WaitForSingleObject(h, timeoutMs);
    CloseHandle(h);
}

// Copy every entry from src into dst, overwriting. Returns false on error.
bool CopyTree(const fs::path& src, const fs::path& dst, std::wstring& err) {
    try {
        std::error_code ec;
        fs::create_directories(dst, ec);
        for (auto& entry : fs::recursive_directory_iterator(src)) {
            auto rel    = fs::relative(entry.path(), src, ec);
            auto target = dst / rel;
            if (entry.is_directory()) {
                fs::create_directories(target, ec);
            } else {
                fs::create_directories(target.parent_path(), ec);
                fs::copy_file(entry.path(), target,
                              fs::copy_options::overwrite_existing, ec);
                if (ec) { err = L"Failed to copy " + rel.wstring() + L": " +
                                std::wstring(ec.message().begin(), ec.message().end());
                          return false; }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::string m = e.what();
        err = L"Copy failed: " + std::wstring(m.begin(), m.end());
        return false;
    }
}

// Launch the GUI de-elevated (via explorer) so it runs as the normal user.
void RelaunchGui(const fs::path& dir) {
    fs::path gui = dir / kGuiExe;
    if (fs::exists(gui))
        ShellExecuteW(nullptr, nullptr, L"explorer.exe", gui.wstring().c_str(),
                      nullptr, SW_SHOWNORMAL);
}

int RunStandalone() {
    // Register (or re-register) the service from this copy's own folder.
    svc::Uninstall();
    Sleep(500);
    std::wstring svcExe = (fs::path(SelfDir()) / kServiceExe).wstring();
    if (!fs::exists(svcExe)) {
        Fail(std::wstring(kServiceExe) + L" not found next to the updater.");
        return 1;
    }
    if (!svc::Install(svcExe)) { Fail(L"Failed to install the service."); return 1; }
    Sleep(300);
    svc::Start();
    MessageBoxW(nullptr, L"WSL2 IP Forwarder service installed and started.",
                kTitle, MB_OK | MB_ICONINFORMATION);
    return 0;
}

int RunUpdate(const std::vector<std::wstring>& args) {
    std::wstring dest = ArgValue(args, L"--dest");
    if (dest.empty()) { Fail(L"--update requires --dest <dir>."); return 1; }

    std::wstring pidStr = ArgValue(args, L"--wait-pid");
    DWORD pid = pidStr.empty() ? 0
              : static_cast<DWORD>(std::wcstoul(pidStr.c_str(), nullptr, 10));

    // 1. Let the GUI fully exit so its files are unlocked.
    WaitForPid(pid, 30000);

    // 2. Stop the service so its exe is unlocked (ignore "not installed").
    svc::Stop();

    // 3. Copy the new files into the destination.
    fs::path src = SelfDir();
    fs::path dst = dest;
    std::error_code ec;
    if (fs::equivalent(src, dst, ec)) {
        Fail(L"Update source and destination are the same folder.");
        return 1;
    }
    std::wstring err;
    if (!CopyTree(src, dst, err)) { Fail(err); return 1; }

    // 4. Re-register the service from the destination and start it.
    svc::Uninstall();
    Sleep(500);
    std::wstring svcExe = (dst / kServiceExe).wstring();
    if (!svc::Install(svcExe)) { Fail(L"Failed to install the updated service."); return 1; }
    Sleep(300);
    svc::Start();

    // 5. Reopen the GUI for the user.
    RelaunchGui(dst);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argvRaw = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argvRaw[i]);
    if (argvRaw) LocalFree(argvRaw);

    return HasFlag(args, L"--update") ? RunUpdate(args) : RunStandalone();
}
