#include "config.h"
#include "app_paths.h"
#include <windows.h>
#include <fstream>
#include <filesystem>

namespace Config {

std::wstring ConfigPath() {
    // Portable build -> next to the exe; installed build -> %ProgramData%.
    return apppaths::DataDir() + L"\\config.json";
}

GlobalConfig Load() {
    GlobalConfig cfg;
    std::wstring path = ConfigPath();
    try {
        std::ifstream f(path);
        if (!f.is_open()) return cfg;
        nlohmann::json j;
        f >> j;
        cfg = j.get<GlobalConfig>();
    } catch (...) {}
    return cfg;
}

bool Save(const GlobalConfig& cfg) {
    std::wstring path = ConfigPath();
    try {
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path());
        std::ofstream f(path);
        if (!f.is_open()) return false;
        nlohmann::json j = cfg;
        f << j.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Config
