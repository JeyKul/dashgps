cmake_minimum_required(VERSION 3.21)
project(dashgps VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Parser library: plain C++, no Qt
add_library(dashgps_core STATIC src/gpsparser.cpp src/exporters.cpp)
target_include_directories(dashgps_core PUBLIC src)

add_executable(dashgps-cli src/cli.cpp)
target_link_libraries(dashgps-cli PRIVATE dashgps_core)

option(DASHGPS_GUI "Build the Qt desktop app" ON)
if(DASHGPS_GUI)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)
    qt_standard_project_setup()
    qt_add_executable(dashgps WIN32 MACOSX_BUNDLE
        src/main.cpp src/mainwindow.cpp src/mainwindow.h src/mapwidget.cpp src/mapwidget.h src/icons.cpp src/icons.h
        src/stereoaudio.cpp src/stereoaudio.h src/videoarea.cpp src/videoarea.h)
    qt_add_resources(dashgps "res" PREFIX "/" BASE res FILES res/icon.svg)
    target_compile_definitions(dashgps PRIVATE DASHGPS_VERSION="${PROJECT_VERSION}")
    target_link_libraries(dashgps PRIVATE dashgps_core
        Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Network)
    set_target_properties(dashgps PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.jeykul.dashgps
        MACOSX_BUNDLE_BUNDLE_NAME DashGPS
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    include(GNUInstallDirs)
    install(TARGETS dashgps dashgps-cli BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(UNIX AND NOT APPLE)
        install(FILES res/dashgps.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES res/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps RENAME dashgps.svg)
    endif()
endif()
