# WSL2 IP Forwarder

Automatically exposes services running inside WSL2 to your local network — and
optionally to the internet — with no manual `netsh` commands or firewall rules.

WSL2 runs inside a lightweight VM. Services that listen on a port inside that VM
are normally only reachable from the Windows host itself (`localhost`). WSL2 IP
Forwarder runs a background Windows service that watches WSL2 for listening
ports and creates the matching `netsh portproxy` and Windows Firewall rules to
make them reachable from other machines — with optional custom local ports,
automatic forwarding rules, and UPnP router port mapping.

---

## Requirements

- Windows 10 version 2004 or later (64-bit)
- WSL2 with at least one Linux distribution installed
- [.NET 8 Desktop Runtime](https://dotnet.microsoft.com/download/dotnet/8.0)
  for the GUI (`winget install Microsoft.DotNet.DesktopRuntime.8`)

---

## Installation

Run `wsl2ipfwd-setup.exe` as Administrator. It will:

1. Copy all files to `%ProgramFiles%\WSL2 IP Forwarder\`
2. Register `wsl2ipfwd` as an auto-start Windows service
3. Create a Start Menu shortcut for the GUI
4. Start the service immediately

To build from source, see [BUILD.md](BUILD.md).

### Portable (no installer)

Each release also publishes `wsl2ipfwd-portable-<version>.zip`. Extract it
anywhere (e.g. a tools folder or USB drive) and run **`wsl2ipfwd-updater.exe`
once** (it elevates) to register and start the service from that folder — or
just launch the GUI and use **Service → Install Service**.

In portable mode (i.e. when the app is **not** under `%ProgramFiles%`), all
state lives next to the executables instead of the system folders:
`config.json`, `service.log`, and `settings.json` are written to the app
directory, so the whole installation is self-contained and movable.

The portable build still requires the **.NET 8 Desktop Runtime** and registers a
Windows service (which needs admin once).

---

## Getting Started

Launch **WSL2 IP Forwarder** from the Start Menu. The main window connects to
the background service and shows a live list of every port the service knows
about in WSL2. The status bar shows whether the GUI is connected to the service.

If it shows *"Service not running"*, the **Service** menu (or the action button
on the toolbar) lets you start or install it — see
[Managing the Service](#managing-the-service).

---

## Main Window

### Port list columns

| Column | Meaning |
|--------|---------|
| **Distribution** | The WSL2 distribution that **owns** the listening port (see note below) |
| **Port** | The port number listening inside WSL2 |
| **Local Port** | The Windows-side listen port. A custom value if remapped; the same number while actively forwarded; `—` when not forwarded |
| **Protocol** | TCP or UDP |
| **In WSL2** | ✓ = currently listening inside WSL2 |
| **Forwarded** | ✓ = active `netsh portproxy` rule exists |
| **Firewall** | ✓ = active Windows Firewall inbound rule exists |
| **UPnP** | `—` if not enabled; otherwise the external router port with live status: **✓** = mapping present on the router, **…** = enabled but not yet mapped |
| **Rule** | *Enabled* or *Disabled* — whether the port is configured for forwarding |

> **How ports are attributed to a distribution:** All WSL2 distributions share a
> single virtual machine and **one network namespace** (one IP), so a port can be
> bound by only one distribution at a time and every distribution "sees" every
> listening socket. The service identifies the real owner by running `ss -tlnp`
> as root in each distribution and keeping only the sockets whose owning process
> is visible there. Loopback-only binds (`127.0.0.0/8`, `::1`) are skipped — they
> can't be reached from the Windows host. (Distributions without the `ss` tool
> fall back to `/proc/net/tcp`, which can't attribute ownership.)

### Row colours

| Colour | Meaning |
|--------|---------|
| Green | Enabled and actively forwarded |
| Orange | Enabled but not yet forwarded (WSL2 may not be running) |
| Grey | Port is no longer detected in WSL2 |

### Toolbar buttons

| Button | Action |
|--------|--------|
| **⟳ Refresh** | Force an immediate data refresh from the service |
| **Configure** | Open the per-port configuration dialog for the selected row |
| **✕ Remove** | Remove the selected port's rules and configuration entirely |
| **☰ Settings** | Open the global settings dialog |
| **Clear Rules** | Bulk-remove rules matching a set of filters |
| **Restart / Start / Install Service** | Context-sensitive service action (see [Managing the Service](#managing-the-service)) |

You can also **right-click** a row for **Configure** / **Remove**, and
**double-click** a row to open its configuration.

The **menu bar** provides **File**, **View**, **Service**, and **Help** menus.

---

## Forwarding a Port

By default, new ports detected in WSL2 are added to the list but forwarding is
**not** enabled automatically — you choose which ports to expose.

1. Select the port row and click **Configure**, or double-click the row.
2. *(Optional)* Set a **Local port** to listen on a different Windows-side port
   than the WSL2 port — see [Custom Local Ports](#custom-local-ports). Leave
   blank to use the same number.
3. Tick **Enable port forwarding for this port**.
4. Under *Create Windows Firewall rule for profile(s)* select which network
   profiles may accept inbound connections:
   - **Domain** — corporate/domain networks
   - **Private** — home / trusted networks
   - **Public** — public hotspots (use with caution)
5. *(Optional)* Under *Internet access (UPnP)*, expose the port beyond your LAN —
   see [Exposing a Port to the Internet](#exposing-a-port-to-the-internet-upnp).
6. Click **OK**. The service applies the change within one poll cycle
   (default 5 seconds).

A port is only reachable from other machines once it is **Enabled** — that is
what creates the Windows Firewall allow rule. Disabling later leaves the port in
the list as *Disabled* so you can re-enable it without reconfiguring.

---

## Custom Local Ports

By default a WSL2 port is exposed on the **same** Windows port number. You can
remap it — e.g. expose WSL2 port `3000` on Windows port `13000` — by setting a
**Local port** in the Configure dialog.

> **Requires NAT networking mode.** Custom local ports rely on the portproxy
> hop, which does not work in mirrored networking mode. See
> [WSL2 Networking Modes](#wsl2-networking-modes).

---

## Exposing a Port to the Internet (UPnP)

The Configure dialog's **Internet access (UPnP)** section can open a port
mapping on your router so the port is reachable from the wider internet, not
just your LAN.

1. Enable forwarding for the port first (UPnP builds on top of it).
2. Tick **Expose this port to the internet via the router**.
3. *(Optional)* Set a **Remote port** — the external port on the router. Leave
   blank to use the same number as the local port.
4. Click **OK**.

The router mapping points `Router:remotePort → Windows-host:localPort`, and the
existing forwarding takes it the rest of the way to WSL2.

**Requirements & caveats:**

- UPnP must be enabled on your router (look for *UPnP IGD* in its settings).
- IPv4 only; has no effect behind carrier-grade NAT or double-NAT.
- This is **opt-in per port** by design — automatic forwarding never uses UPnP.
- **This exposes the port to the public internet.** Only enable it for services
  you intend to be publicly reachable, and prefer the **Private** firewall
  profile plus authentication on the service itself.
- Mappings are removed automatically when forwarding stops or the service shuts
  down, and re-asserted periodically (some routers expire them).

UPnP activity is logged with a `[UPnP]` prefix in `service.log`.

---

## Automatic Forwarding

The **Auto Forward** tab in Settings forwards matching ports **automatically**
the moment they are detected in WSL2 — no per-port clicking.

- Add expressions in the **WSL2 Port** column: a single port (`80`), a
  comma list (`80, 443, 8080`), or a range (`3000-4000`).
- *(Optional)* The **Local Port** column remaps them in parallel — it must
  mirror the WSL2 Port structure: `80, 443` → `40080, 40443`, or
  `3000-4000` → `43000-44000`. A mismatched structure is flagged inline and
  blocks **Apply** until fixed. Leave blank to use the same port.
- *(Optional)* The **Distribution** column scopes a rule to one distribution.
  `(All)` applies to every distribution; a distro-specific rule **takes
  precedence** over an `(All)` rule for the same port. This lets you, e.g.,
  auto-forward `3000-4000` → `33000-34000` in *Ubuntu* and `3000-4000` →
  `43000-44000` in *Debian* with two separate rules.
- The **Public / Private / Domain** checkboxes set the firewall profiles applied
  to *all* auto-forwarded ports.
- **Apply to existing rules** (checked by default) — normally auto-forward only
  catches ports the *first time* they appear. With this checked, clicking
  **Apply** also retroactively enables forwarding for any port that is *already
  detected* and matches a rule. Uncheck it to apply rules only to ports detected
  from now on (e.g. to preserve a port you manually disabled).

Auto-forwarding deliberately never enables UPnP — internet exposure is always a
manual, per-port decision.

> **Note:** The Port Filter takes precedence. A port that is blacklisted (or not
> in the whitelist) is never auto-forwarded — see below.

---

## Port Filter (Whitelist / Blacklist)

The **Port Filter** tab controls which ports the app deals with at all. Filtered
ports are hidden from the list, never notified, never auto-forwarded, and are
removed from the configuration.

Choose a mode:

- **Blacklist** (default) — matching ports are *ignored*.
- **Whitelist** — *only* matching ports are processed; everything else is ignored.

An empty filter list shows everything regardless of mode. Each entry accepts a
single port (`22`), a comma list (`22, 80, 443`), or a range
(`32768-60999`), with an optional *Comment*.

Each entry also has a **Distribution** column. `(All)` applies the entry to every
distribution; a distro-specific entry applies only there. A distribution's
effective filter set is its own scoped entries **plus** all `(All)` entries — so
you can whitelist `80, 443` in *Debian* only while `3000-4000` is whitelisted
everywhere. The whitelist/blacklist mode itself stays global.

Because the filter is authoritative, changing it prunes any now-filtered ports
from the service configuration (tearing down their rules). If a filter conflicts
with an auto-forward rule, the Settings dialog shows a warning.

---

## Removing & Clearing Rules

**Remove** (toolbar, right-click, or the per-row menu) deletes a single port:
its config entry, its portproxy rule, and its firewall rule. The port is then
treated as brand new — if still listening in WSL2 it is re-evaluated (including
auto-forward and filter rules) on the next poll.

**Clear Rules** bulk-removes by category:

| Option | What gets removed |
|--------|------------------|
| **Include Disabled rules** | Ports where forwarding is turned off |
| **Include Enabled rules** | Ports where forwarding is turned on |
| **Include Active rules** | Ports currently forwarding traffic |
| **Include system ports (< 1000)** | Ports below 1000 (off by default as a safety guard) |

A confirmation dialog lists the exact ports before anything is deleted.

---

## Settings

Open **☰ Settings**. Changes are sent to the service when you click **Apply**.

### General

| Setting | Description |
|---------|-------------|
| **Show balloon notification when a new port is detected** | Tray balloon when an unconfigured port appears in WSL2. |
| **Show balloon notification while GUI is active** | When off, balloons are suppressed while the GUI is connected (you can already see new ports in the list). |
| **Exit on close** | Clicking ✕ exits instead of minimising to the tray. |
| **Don't show 'still running' notification when minimised to tray** | Suppresses the minimise-to-tray balloon. |
| **Automatically check for updates** + interval | Periodically checks for a newer release. **Check now** runs an immediate check and shows the result inline. |

### WSL2

| Setting | Description |
|---------|-------------|
| **Distributions to monitor** | Check the distributions to forward ports from. None checked = all running distributions. The service polls the selected running distros, staggering the queries across the poll interval. |
| **Poll interval (ms)** | How often each distro is checked for port changes. Default 5 000. With N distros, one is queried every *interval ÷ N*. |
| **Offline threshold (ms)** | How long without a response before WSL2 is considered offline. Default 30 000. |
| **Listen address** | Windows-side address portproxy binds to. `0.0.0.0` = all interfaces; `127.0.0.1` = localhost only. |
| **Service log level** | `Normal` (events only), `Debug` (log port changes), or `Trace` (log every poll). Written to `service.log`; applies within a few seconds. |

### Auto Forward

See [Automatic Forwarding](#automatic-forwarding).

### Port Filter

See [Port Filter](#port-filter-whitelist--blacklist).

---

## Managing the Service

The **Service** menu and the toolbar's context-sensitive action button let you
control the Windows service without opening `services.msc`:

| Action | When available | Notes |
|--------|----------------|-------|
| **Start** | Service installed but stopped | Tries without elevation first; prompts for admin only if required |
| **Stop** | Service running | |
| **Restart** | Service running | Stop + start; single UAC prompt only if elevation is needed |
| **Install Service** | Not installed, but `wsl2ipfwd-service.exe` is present | Installs and starts in one elevated step |
| **Uninstall Service** | Service installed | Stops, removes rules, and deletes the service |
| **Restart as Administrator** | GUI not already elevated | Relaunches the GUI elevated so subsequent service actions need no further prompts |

The toolbar button shows **Restart Service**, **Start Service**, or **Install
Service** depending on the current state.

---

## Updates

When *Automatically check for updates* is on, the service periodically checks
the project's GitHub releases. If a newer version is available, a yellow bar
appears at the top of the window with **Download**, then **Install Now** (or
**Later** to dismiss). You can also trigger a check from
**Settings → General → Check now**.

Updates use the portable zip: clicking Install Now downloads it, extracts it to
a temp folder, and launches the bundled `wsl2ipfwd-updater.exe` (one UAC
prompt). The updater waits for the GUI to close, stops the service, copies the
new files over the current installation (Program Files for an installed copy, or
the portable folder), reinstalls the service, and reopens the GUI.

---

## WSL2 Networking Modes

WSL2 networking mode is set in `%UserProfile%\.wslconfig`:

```ini
[wsl2]
networkingMode=nat          # default — fully supported
# networkingMode=mirrored   # Windows 11 22H2+ — same-port forwarding + UPnP
# networkingMode=virtioproxy # NOT supported for LAN forwarding (see below)
```

**Use NAT (recommended) or mirrored.** NAT supports every feature; mirrored
supports same-port forwarding and UPnP.

### NAT mode (default) — full functionality

WSL2 gets its own `172.x` IP on a virtual adapter. The service forwards
`Windows-host:localPort → wsl:port` via portproxy. **Everything works**,
including custom local ports and UPnP.

### Mirrored mode — same-port forwarding only

WSL2 shares the Windows host's network interface and IP, so a WSL2 listener is
*already* reachable on the host's LAN IP at the **same port number** — the
portproxy hop is largely redundant. Consequences:

- ✅ **Same-port forwarding works.** You still must **Enable** the port — that
  creates the Windows Firewall rule that allows inbound traffic. (Windows blocks
  inbound by default; this is expected, not a bug.)
- ✅ **UPnP works**, including a custom **Remote port** — the router maps the
  external port to the WSL2 port directly.
- ❌ **Custom local ports do *not* work.** Remapping (e.g. `8081 → 3001`) relies
  on the portproxy connecting to the shared host IP, which mirrored mode does not
  hairpin, so the connection never reaches the WSL2 listener.

**The app detects mirrored mode automatically** (the WSL2 IP matches a host
adapter IP). When detected, the service **ignores custom local ports** for
forwarding (it uses the WSL2 port), and the main window shows a note next to the
*WSL2: Running* status. UPnP remote ports are unaffected.

**If you need custom local ports, use NAT mode.**

### VirtioProxy mode — not supported for LAN forwarding

VirtioProxy is a newer, experimental mode (not in Microsoft's official
networking docs). It redirects `127.0.0.1` to the WSL2 service but does **not**
expose that service on the host's LAN interface, so **inbound LAN forwarding
cannot work**:

- ✅ The service is reachable from Windows and from inside WSL2 via `localhost`.
- ❌ It is **not** reachable from other machines on the LAN. The portproxy rule's
  target is the host's own IP (`0.0.0.0:port → host-ip:port`), which loops back
  to the listener rather than reaching the WSL2 service.

The app reads your `.wslconfig` and detects VirtioProxy directly. When detected,
the **service does nothing** (it creates no portproxy, firewall, or UPnP rules,
and removes any it previously made), and the main window shows a **red
"⚠ VirtioProxy — LAN forwarding not supported"** note next to the WSL2 status.
**Switch to NAT or mirrored mode for LAN access.**

---

## System Tray

When *Exit on close* is off (the default), closing the window minimises the GUI
to the tray; the service keeps running regardless of whether the GUI is open.

| Tray menu item | Action |
|------|--------|
| Open | Restore the main window |
| Refresh | Force an immediate data refresh |
| Exit | Quit the GUI (service keeps running) |

---

## Troubleshooting

| Problem | What to check |
|---------|--------------|
| GUI shows *"Service not running"* | Use **Service → Start** / **Install Service**, or check `services.msc`. See `%ProgramData%\wsl2ipfwd\service.log`. |
| Port listed but not forwarded | Ensure WSL2 is running (`wsl --status`) and the distro setting matches your running distro. |
| Forwarding active but port unreachable | Confirm the port is **Enabled** (creates the firewall rule) and the correct firewall profiles are ticked. |
| Custom local port unreachable | You are likely in **mirrored** networking mode — custom local ports require **NAT** mode. See [WSL2 Networking Modes](#wsl2-networking-modes). |
| Works from Windows/localhost but not from the LAN | You are likely in **VirtioProxy** networking mode, which doesn't expose WSL2 services to the LAN. Switch to **NAT** or **mirrored**. See [WSL2 Networking Modes](#wsl2-networking-modes). |
| UPnP mapping not created | Enable UPnP/IGD on your router. Check `service.log` for `[UPnP]` lines. Has no effect behind CGNAT/double-NAT. |
| Rules disappear after WSL2 IP change | Expected — WSL2 gets a new IP on each start; the service re-creates rules automatically. |
| Need more detail in the log | Set **Settings → WSL2 → Service log level** to *Debug* or *Trace*. |

### Files

| File | Contents |
|------|----------|
| `%ProgramData%\wsl2ipfwd\service.log` | Service activity and errors |
| `%ProgramData%\wsl2ipfwd\config.json` | Persistent service config (ports, auto-forward, filter, log level) |
| `%AppData%\WSL2IpFwd\settings.json` | GUI-only settings (tray, filter mode, comments) |

---

## Uninstall

Use **Service → Uninstall Service** in the GUI, or run the installer's
uninstaller from *Settings → Apps*. Uninstalling stops and removes the service
and cleans up all portproxy and firewall rules it created.
