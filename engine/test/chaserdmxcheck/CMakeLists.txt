# Throwaway diagnostic tool - see chaserdmxcheck.cpp. Not wired into test.sh.
add_executable(chaserdmxcheck
    chaserdmxcheck.cpp
)
target_include_directories(chaserdmxcheck PRIVATE
    ../../../plugins/interfaces
    ../../src
)
target_link_libraries(chaserdmxcheck PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    qlcplusengine
)
