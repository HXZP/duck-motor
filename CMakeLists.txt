cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

project(motor_duck LANGUAGES NONE)

include(ExternalProject)

option(BUILD_BOOT "Build boot project" OFF)
option(BUILD_APP "Build stm32f103 application project" ON)

set(APP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/stm32f103")
set(BOOT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/boot")

set(APP_TOOLCHAIN_FILE
    "${APP_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake"
    CACHE FILEPATH
    "Toolchain file used by the stm32f103 application project"
)

set(BOOT_TOOLCHAIN_FILE
    "${BOOT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake"
    CACHE FILEPATH
    "Toolchain file used by the boot project"
)

if(BUILD_APP)
    ExternalProject_Add(app
        SOURCE_DIR "${APP_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/app"
        BUILD_ALWAYS ON
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_TOOLCHAIN_FILE=${APP_TOOLCHAIN_FILE}
        INSTALL_COMMAND ""
    )
endif()

if(BUILD_BOOT)
    ExternalProject_Add(boot
        SOURCE_DIR "${BOOT_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_BINARY_DIR}/boot"
        BUILD_ALWAYS ON
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
            -DCMAKE_TOOLCHAIN_FILE=${BOOT_TOOLCHAIN_FILE}
        INSTALL_COMMAND ""
    )
endif()
