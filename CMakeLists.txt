cmake_minimum_required(VERSION 3.21)
project(Metronome LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/utf-8 /W3)

# The sequencer has no Qt or Windows dependencies, so it can be tested on its own.
add_library(metronome_core STATIC src/Sequencer.cpp)
target_include_directories(metronome_core PUBLIC src)

enable_testing()
add_executable(sequencer_test tests/sequencer_test.cpp)
target_link_libraries(sequencer_test PRIVATE metronome_core)
add_test(NAME sequencer_test COMMAND sequencer_test)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
qt_standard_project_setup()

qt_add_executable(Metronome WIN32
    src/main.cpp
    src/AudioEngine.cpp
    src/GaugeView.cpp
    src/BeatStripView.cpp
    src/TempoDial.cpp
    src/MainWindow.cpp
)
target_link_libraries(Metronome PRIVATE metronome_core Qt6::Widgets Qt6::OpenGLWidgets ole32 avrt)

add_custom_command(TARGET Metronome POST_BUILD
    COMMAND "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}/windeployqt.exe" --no-translations --no-system-d3d-compiler
            --no-opengl-sw "$<TARGET_FILE:Metronome>"
    VERBATIM)
