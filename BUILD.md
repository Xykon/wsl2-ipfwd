# Building WSL2 IP Forwarder

## Prerequisites

| Component | Requirement |
|-----------|-------------|
| **C++ targets** (service, notify) | Visual Studio 2022 or [VS Build Tools](https://aka.ms/vs/17/release/vs_BuildTools.exe) — "Desktop development with C++" workload |
| **C# GUI** (gui-cs) | [.NET 8 SDK](https://dotnet.microsoft.com/download/dotnet/8.0) |
| **Installer** (optional) | [Inno Setup 6 or 7](https://jrsoftware.org/isinfo.php) |

No package manager (vcpkg, NuGet, etc.) is needed.
[nlohmann/json](https://github.com/nlohmann/json) is vendored as a single header in `third_party/nlohmann/json.hpp`.

> **Network + git required for the first CMake configure.**
> The service links [miniupnpc](https://github.com/miniupnp/miniupnp) (for UPnP
> router port mapping), pulled in automatically via CMake `FetchContent` at
> configure time and cached under `build\…\_deps\`. This needs `git` on `PATH`
> and internet access the first time you configure. The pinned tag and link
> target are in the root `CMakeLists.txt` / `service/CMakeLists.txt`.

---

## Build the C++ executables

```powershell
# From a regular PowerShell or VS Developer PowerShell:
.\build.ps1

# Debug build:
.\build.ps1 -Config Debug

# Clean rebuild:
.\build.ps1 -Clean

# Package the portable zip (build the C# GUI into bin first, see below):
.\build.ps1 -Portable
```

Outputs in `build\Release\bin\`:
- `wsl2ipfwd-service.exe` — Windows service
- `wsl2ipfwd-notify.exe` — tray balloon notification helper
- `wsl2ipfwd-updater.exe` — elevated updater (portable/auto-update helper)

`-Portable` additionally produces `build\Release\wsl2ipfwd-portable-<ver>.zip`
(everything in `bin\` minus debug symbols). Build the C# GUI into `bin\` first so
it is included.

> cmake is bundled with Visual Studio and is found automatically.
> No separate cmake install is required.

---

## Build the C# GUI

```powershell
dotnet build gui-cs\gui-cs.csproj -c Release
```

Output: `build\Release\bin\wsl2ipfwd-gui-cs.exe` (and companion files)

The GUI requires the **.NET 8 Desktop Runtime** on the target machine.
Install it via winget:
```
winget install Microsoft.DotNet.DesktopRuntime.8
```

---

## Build the installer (optional)

Build both the C++ targets and the C# GUI first, then:

```powershell
# From the build output directory:
Push-Location build\Release
cmake --build . --target installer
Pop-Location
# -> build\Release\installer\wsl2ipfwd-setup.exe
```

Or compile directly with Inno Setup:
```powershell
& "C:\Program Files\Inno Setup 7\ISCC.exe" wsl2ipfwd.iss
```

---

## Install

Run `wsl2ipfwd-setup.exe` (the Inno Setup installer, requires admin). It will:
1. Stop and remove any existing service
2. Copy executables to `%ProgramFiles%\WSL2 IP Forwarder\`
3. Register `wsl2ipfwd` as a Windows service (auto-start)
4. Create Start Menu shortcuts
5. Start the service

---

## Manual service management

```powershell
# Install & start
.\wsl2ipfwd-service.exe --install
sc start wsl2ipfwd

# Install and start in one elevated step (used by the GUI's Install Service)
.\wsl2ipfwd-service.exe --install-and-start

# Stop & remove
sc stop wsl2ipfwd
.\wsl2ipfwd-service.exe --uninstall

# Stop + start in one step (used by the GUI's Restart Service)
.\wsl2ipfwd-service.exe --restart

# Run in console (debug without SCM). Add --trace to log every poll cycle;
# without it, only port changes are logged.
.\wsl2ipfwd-service.exe --debug
.\wsl2ipfwd-service.exe --debug --trace
```

The GUI exposes all of these via the **Service** menu (no manual commands
needed), requesting elevation only when actually required.

---

## Configuration

Config is stored at `%ProgramData%\wsl2ipfwd\config.json`.
The GUI writes changes live via the named pipe; the service reloads on every poll cycle.

### config.json structure

```json
{
  "wsl_distro": "",
  "poll_interval_ms": 5000,
  "offline_threshold_ms": 30000,
  "listen_address": "0.0.0.0",
  "log_level": 0,
  "notify_new_ports": true,
  "notify_while_gui_active": true,
  "ignore_system_ports": true,
  "notify_ignore_ports": [],
  "update_check_enabled": false,
  "update_check_interval_hours": 24,
  "auto_forward_expressions": ["3000-4000"],
  "auto_forward_local_expressions": ["43000-44000"],
  "auto_forward_fw_public": true,
  "auto_forward_fw_private": true,
  "auto_forward_fw_domain": false,
  "port_filter_whitelist": false,
  "port_filter_expressions": ["22"],
  "ports": {
    "8080": {
      "enabled": true,
      "fw_domain": false,
      "fw_private": true,
      "fw_public": true,
      "local_port": 0,
      "upnp_enabled": false,
      "upnp_remote_port": 0
    }
  }
}
```

Notes:
- `log_level`: 0 = normal, 1 = debug, 2 = trace.
- `local_port` / `upnp_remote_port`: 0 means "same as the source port".
- `auto_forward_local_expressions` is index-aligned with
  `auto_forward_expressions` (empty entry = same port).
- The service preserves service-managed update state and absent per-port
  entries across `set_config`, so the GUI never has to send the full `ports` map.

GUI-only settings (close/tray behaviour, port-filter mode, row comments) are
stored separately at `%AppData%\WSL2IpFwd\settings.json` and are never sent to
the service.

---

## Architecture

```
  ┌─────────────────────────────────────┐
  │  wsl2ipfwd-gui-cs.exe  (WinForms)   │
  └─────────────────────────────────────┘
                   ▲ │
                   │ │  Named Pipe
                   │ ▼
  ┌────────────────────────────────────────────────┐
  │  wsl2ipfwd-service.exe  (Windows Service)      │
  │                                                │
  │  ┌──────────────────────────────────────────┐  │
  │  │ WslMonitor  –  polls /proc/net/tcp       │  │
  │  └──────────────────────────────────────────┘  │
  │  ┌──────────────────────────────────────────┐  │
  │  │ PortForwarder  –  netsh portproxy        │  │
  │  └──────────────────────────────────────────┘  │
  │  ┌──────────────────────────────────────────┐  │
  │  │ FirewallManager  –  INetFwPolicy2 (COM)  │  │
  │  └──────────────────────────────────────────┘  │
  │  ┌──────────────────────────────────────────┐  │
  │  │ UpnpManager  –  miniupnpc (worker thread) │  │
  │  └──────────────────────────────────────────┘  │
  │  ┌──────────────────────────────────────────┐  │
  │  │ UpdateChecker  –  WinHTTP → GitHub        │  │
  │  └──────────────────────────────────────────┘  │
  └────────────────────────────────────────────────┘
        │            │           │            │
     wsl.exe       netsh      COM API      SSDP/UDP
        │            │           │            │
        ▼            ▼           ▼            ▼
    WSL2 Linux   Windows IP   WF Rules   Router (IGD)
```

## Service log

`%ProgramData%\wsl2ipfwd\service.log`
