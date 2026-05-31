add_executable(wsl2ipfwd-updater WIN32
    updater.cpp
    ../common/svc_control.cpp
)

target_include_directories(wsl2ipfwd-updater PRIVATE
    "${CMAKE_SOURCE_DIR}/common"
)

target_link_libraries(wsl2ipfwd-updater PRIVATE
    advapi32   # SCM (svc_control)
    shell32    # CommandLineToArgvW / ShellExecuteW
)

# WIN32 subsystem (no console flash) + embed a requireAdministrator manifest so
# the updater always runs elevated (service control + writing to Program Files).
set_target_properties(wsl2ipfwd-updater PROPERTIES
    WIN32_EXECUTABLE TRUE
    LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
