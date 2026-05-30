set(SERVICE_SOURCES
    main.cpp
    service.cpp
    update_checker.cpp
    wsl_monitor.cpp
    port_forwarder.cpp
    firewall.cpp
    ipc_server.cpp
    upnp_manager.cpp
    net_util.cpp
    ../common/config.cpp
)

# ---- Update URL (configurable for local testing) ----------------------------
# Default: real GitHub Releases API.
# Override for local test:
#   cmake ... -DWSL2IPFWD_UPDATE_URL=http://localhost:8080/releases/latest
set(WSL2IPFWD_UPDATE_URL
    "https://api.github.com/repos/Xykon/wsl2-ipfwd/releases/latest"
    CACHE STRING "GitHub Releases API URL used by the update checker")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/update_url.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/update_url.h"
    @ONLY
)

add_executable(wsl2ipfwd-service WIN32 ${SERVICE_SOURCES})

target_include_directories(wsl2ipfwd-service PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_BINARY_DIR}"   # generated update_url.h
    # miniupnpc headers come from the linked libminiupnpc-static target's
    # INTERFACE include dir (…/miniupnpc/include); no manual entry needed.
)

target_link_libraries(wsl2ipfwd-service PRIVATE
    wsl2ipfwd_common
    advapi32
    shell32
    ole32
    oleaut32
    wtsapi32    # WTSQueryUserToken / WTSGetActiveConsoleSessionId
    userenv     # CreateEnvironmentBlock
    winhttp     # WinHTTP HTTPS client (update checker)
    ws2_32      # Winsock (miniupnpc sockets)
    iphlpapi    # miniupnpc adapter/route enumeration
    libminiupnpc-static  # UPnP IGD client
)

# Service does not need a WinMain; use wmain
target_compile_definitions(wsl2ipfwd-service PRIVATE _CONSOLE)
set_target_properties(wsl2ipfwd-service PROPERTIES
    WIN32_EXECUTABLE FALSE
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)

# Manifest for UAC elevation (service installer needs admin)
# Service itself runs as SYSTEM so no UAC manifest needed at runtime,
# but the install/uninstall commands do.
set_source_files_properties(main.cpp PROPERTIES COMPILE_FLAGS "")
