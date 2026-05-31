#include "app_paths.h"
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <cwctype>

namespace {

std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool StartsWith(const std::wstring& s, const std::wstring& prefix) {
    if (prefix.empty() || s.size() < prefix.size()) return false;
    return ToLower(s).compare(0, prefix.size(), ToLower(prefix)) == 0;
}

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
    return (n > 0 && n < MAX_PATH) ? std::wstring(buf) : std::wstring();
}

} // namespace

namespace apppaths {

std::wstring ExeDir() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::filesystem::path p(path);
    return p.parent_path().wstring();
}

bool IsPortable() {
    const std::wstring exe = ExeDir();
    for (const wchar_t* var : { L"ProgramFiles", L"ProgramFiles(x86)", L"ProgramW6432" }) {
        std::wstring pf = EnvVar(var);
        if (!pf.empty() && StartsWith(exe, pf)) return false;   // installed
    }
    return true;   // not under Program Files -> portable
}

std::wstring DataDir() {
    std::wstring dir;
    if (IsPortable()) {
        dir = ExeDir();
    } else {
        wchar_t pd[MAX_PATH] = {};
        SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, pd);
        dir = std::wstring(pd) + L"\\wsl2ipfwd";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

} // namespace apppaths
