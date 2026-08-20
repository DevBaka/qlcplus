project(platforms)

if(UNIX AND NOT APPLE)
    add_subdirectory(linux)
endif()
if(WIN32)
    # TEMPORARILY disabled: packaging step requires MSYS2 (libusb/audio DLLs), not installed locally.
    # Only affects `cmake --install` / NSIS packaging, not compiling/running the app.
    # add_subdirectory(windows)
endif()
if(APPLE)
    add_subdirectory(macos)
endif()
