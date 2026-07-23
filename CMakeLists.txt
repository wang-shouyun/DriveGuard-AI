# SPDX-FileCopyrightText: 2026 Rao Jing
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.20)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DRIVEGUARD_VERSION_TEXT)
string(STRIP "${DRIVEGUARD_VERSION_TEXT}" DRIVEGUARD_VERSION_TEXT)

project(DriveGuardAI VERSION ${DRIVEGUARD_VERSION_TEXT} LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Sql)

qt_standard_project_setup()

set(DRIVEGUARD_VERSION_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    @ONLY
)

qt_add_executable(DriveGuardAI
    WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    resources.qrc
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
)

target_link_libraries(DriveGuardAI
    PRIVATE
        Qt6::Widgets
        Qt6::Charts
        Qt6::Sql
)

if(WIN32)
    target_link_libraries(DriveGuardAI PRIVATE winmm)
endif()

target_compile_definitions(DriveGuardAI
    PRIVATE
        DRIVEGUARD_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        DRIVEGUARD_VERSION="${DRIVEGUARD_VERSION_TEXT}"
)

set_target_properties(DriveGuardAI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
