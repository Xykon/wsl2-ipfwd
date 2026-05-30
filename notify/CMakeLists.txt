add_executable(wsl2ipfwd-notify WIN32 notify.cpp)

target_compile_definitions(wsl2ipfwd-notify PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

target_link_libraries(wsl2ipfwd-notify PRIVATE shell32)

set_target_properties(wsl2ipfwd-notify PROPERTIES
    WIN32_EXECUTABLE TRUE)
