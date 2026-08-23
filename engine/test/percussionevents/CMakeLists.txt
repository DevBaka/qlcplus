add_executable(percussionevents_test WIN32
    percussionevents_test.cpp percussionevents_test.h
)
target_include_directories(percussionevents_test PRIVATE
    ../../../plugins/interfaces
    ../../audio/src
    ../../src
)

target_link_libraries(percussionevents_test PRIVATE
    Qt${QT_MAJOR_VERSION}::Core
    Qt${QT_MAJOR_VERSION}::Gui
    Qt${QT_MAJOR_VERSION}::Test
    qlcplusengine
)
