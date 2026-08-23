# Throwaway diagnostic tool - see wavanalyze.cpp. Not wired into test.sh.
add_executable(wavanalyze
    wavanalyze.cpp
)
target_include_directories(wavanalyze PRIVATE
    ../../../plugins/interfaces
    ../../audio/src
    ../../src
)
target_link_libraries(wavanalyze PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    qlcplusaudio
    qlcplusengine
)
