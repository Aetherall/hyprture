cmake_minimum_required(VERSION 3.21)
project(hyprture-qml LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(HYPRTURE_PROTOCOL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../protocol" CACHE PATH "where hyprture-v1.xml is")

# GuiPrivate: for the window's wl_surface. The module is tied to the exact Qt
# it is built with, which is why it is built with quickshell's.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui GuiPrivate Quick)
qt_policy(SET QTP0001 NEW)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
if(NOT WAYLAND_SCANNER)
    find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
endif()

set(PROTOCOL_XML "${HYPRTURE_PROTOCOL_DIR}/hyprture-v1.xml")
set(PROTOCOL_H "${CMAKE_CURRENT_BINARY_DIR}/hyprture-v1-client-protocol.h")
set(PROTOCOL_C "${CMAKE_CURRENT_BINARY_DIR}/hyprture-v1-protocol.c")
add_custom_command(OUTPUT "${PROTOCOL_H}" COMMAND "${WAYLAND_SCANNER}" client-header "${PROTOCOL_XML}" "${PROTOCOL_H}" DEPENDS "${PROTOCOL_XML}")
add_custom_command(OUTPUT "${PROTOCOL_C}" COMMAND "${WAYLAND_SCANNER}" private-code "${PROTOCOL_XML}" "${PROTOCOL_C}" DEPENDS "${PROTOCOL_XML}")

qt_add_qml_module(hyprture
    URI Hyprture
    VERSION 1.0
    PLUGIN_TARGET hyprture
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Hyprture"
    SHARED
    SOURCES Aperture.hpp Aperture.cpp "${PROTOCOL_H}" "${PROTOCOL_C}"
)
target_include_directories(hyprture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(hyprture PRIVATE Qt6::Gui Qt6::GuiPrivate Qt6::Quick PkgConfig::WAYLAND)

include(GNUInstallDirs)
set(QML_DIR "${CMAKE_INSTALL_LIBDIR}/qt-6/qml/Hyprture")
install(TARGETS hyprture LIBRARY DESTINATION "${QML_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Hyprture/qmldir" DESTINATION "${QML_DIR}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Hyprture/hyprture.qmltypes" DESTINATION "${QML_DIR}" OPTIONAL)
