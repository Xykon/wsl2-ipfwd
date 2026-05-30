cmake_minimum_required(VERSION 3.20)
project(wsl2ipfwd VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Require Windows
if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only.")
endif()

# Unicode build
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# Treat source and execution charset as UTF-8 (avoids mojibake for u-literals)
add_compile_options(/utf-8)

# All executables land in the same bin/ directory so the Inno Setup installer
# can find them with the wildcard Source: "build\Release\bin\*"
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# nlohmann/json — vendored as a single header in third_party/nlohmann/json.hpp
# No external packages or vcpkg required.
add_library(wsl2ipfwd_common INTERFACE)
target_include_directories(wsl2ipfwd_common INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

# ---------------------------------------------------------------------------
# miniupnpc — UPnP IGD client used by the service to open router port mappings.
# The Windows IUPnPNAT COM API cannot reliably discover the IGD from a service
# running as LocalSystem (session 0); miniupnpc does its own SSDP over UDP.
# Fetched at configure time (requires git + network). Static lib only.
# ---------------------------------------------------------------------------
include(FetchContent)
set(UPNPC_BUILD_SHARED  OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_STATIC  ON  CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG        miniupnpc_2_2_3
    SOURCE_SUBDIR  miniupnpc
)
FetchContent_MakeAvailable(miniupnpc)

add_subdirectory(service)
add_subdirectory(notify)

# ---------------------------------------------------------------------------
# Optional Inno Setup installer target
# Build C++ and C# targets first, then: cmake --build . --target installer
# ---------------------------------------------------------------------------
find_program(ISCC_EXECUTABLE
    NAMES ISCC.exe iscc.exe
    PATHS
        "C:/Program Files/Inno Setup 7"
        "C:/Program Files (x86)/Inno Setup 6"
    NO_DEFAULT_PATH
)
if(ISCC_EXECUTABLE)
    add_custom_target(installer
        COMMAND "${ISCC_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/wsl2ipfwd.iss"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Compiling Inno Setup installer..."
        DEPENDS wsl2ipfwd-service wsl2ipfwd-notify
    )
    message(STATUS "Inno Setup found at: ${ISCC_EXECUTABLE}")
    message(STATUS "  -> cmake --build . --target installer")
else()
    message(STATUS "Inno Setup not found -- installer target skipped.")
    message(STATUS "  Install from https://jrsoftware.org/isinfo.php")
endif()
