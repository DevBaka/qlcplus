# Throwaway diagnostic tool - see chaserprofilesim.cpp. Not wired into test.sh.
add_executable(chaserprofilesim
    chaserprofilesim.cpp
)
target_include_directories(chaserprofilesim PRIVATE
    ../../../plugins/interfaces
    ../../src
)
target_link_libraries(chaserprofilesim PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    qlcplusengine
)
