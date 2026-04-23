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

set(MEMORY_REPORT_SCRIPT
    "${CMAKE_SOURCE_DIR}/scripts/print_memory_report.py"
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

if(BUILD_APP OR BUILD_BOOT)
    unset(Python3_EXECUTABLE CACHE)
    find_program(Python3_EXECUTABLE NAMES python python3)
    if(NOT Python3_EXECUTABLE)
        message(FATAL_ERROR "Python3 not found.")
    endif()

    find_program(ARM_OBJDUMP_EXECUTABLE NAMES arm-none-eabi-objdump)
    if(NOT ARM_OBJDUMP_EXECUTABLE)
        message(FATAL_ERROR "arm-none-eabi-objdump not found.")
    endif()

    set(BUILD_REPORT_COMMANDS)
    set(BUILD_REPORT_DEPS)

    if(BUILD_BOOT)
        list(APPEND BUILD_REPORT_DEPS boot)
        list(APPEND BUILD_REPORT_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E echo "  BOOT MEMORY REPORT"
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                ${Python3_EXECUTABLE}
                ${MEMORY_REPORT_SCRIPT}
                ${ARM_OBJDUMP_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/output/boot/boot.elf
                ${BOOT_SOURCE_DIR}/STM32F103XX_FLASH.ld
        )
    endif()

    if(BUILD_APP)
        list(APPEND BUILD_REPORT_DEPS app)
        list(APPEND BUILD_REPORT_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E echo ""
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E echo "  APP MEMORY REPORT"
            COMMAND ${CMAKE_COMMAND} -E echo "=================================================="
            COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
                ${Python3_EXECUTABLE}
                ${MEMORY_REPORT_SCRIPT}
                ${ARM_OBJDUMP_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/output/motor/motor.elf
                ${APP_SOURCE_DIR}/STM32F103XX_FLASH.ld
        )
    endif()

    if(BUILD_REPORT_COMMANDS)
        add_custom_target(build_reports ALL
            ${BUILD_REPORT_COMMANDS}
            VERBATIM
        )

        if(BUILD_REPORT_DEPS)
            add_dependencies(build_reports ${BUILD_REPORT_DEPS})
        endif()
    endif()
endif()
