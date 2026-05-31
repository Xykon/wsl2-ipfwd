#pragma once
#include <string>

// Portable vs installed location handling.
//
// "Installed"  — the executable lives under %ProgramFiles% (set up by the Inno
//                installer). Config + logs live in %ProgramData%\wsl2ipfwd.
// "Portable"   — the executable lives anywhere else (an extracted zip). Config +
//                logs live next to the executable, so the whole app is movable.
namespace apppaths {
    // Directory of the current executable (no trailing backslash).
    std::wstring ExeDir();

    // True if the executable is NOT under Program Files (i.e. portable).
    bool IsPortable();

    // Base directory for config/log files (created if needed):
    //   portable  -> ExeDir()
    //   installed -> %ProgramData%\wsl2ipfwd
    std::wstring DataDir();
}
