cmake_minimum_required(VERSION 3.22)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/STM32F103XX_FLASH.ld")
    message(FATAL_ERROR "Missing boot/STM32F103XX_FLASH.ld. BUILD_BOOT stays disabled until the linker script is provided.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME boot)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})

set(REPO_ROOT "${CMAKE_SOURCE_DIR}/..")
set(MEMORY_REPORT_SCRIPT "${REPO_ROOT}/scripts/print_memory_report.py")

add_subdirectory(cmake/stm32cubemx)

add_subdirectory("${REPO_ROOT}/user/rtt" "${CMAKE_BINARY_DIR}/user_rtt")
add_subdirectory("${REPO_ROOT}/user/boot" "${CMAKE_BINARY_DIR}/user_boot")

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${REPO_ROOT}/user"
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    user_boot
)

set(BOOT_OUTPUT_DIR "${REPO_ROOT}/output/${CMAKE_PROJECT_NAME}")
set(BOOT_OUTPUT_HEX "${BOOT_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.hex")
set(BOOT_OUTPUT_BIN "${BOOT_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.bin")
set(BOOT_OUTPUT_ELF "${BOOT_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.elf")
set(BOOT_OUTPUT_MAP "${BOOT_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.map")

add_custom_command(
    OUTPUT
        "${BOOT_OUTPUT_HEX}"
        "${BOOT_OUTPUT_BIN}"
        "${BOOT_OUTPUT_ELF}"
        "${BOOT_OUTPUT_MAP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${BOOT_OUTPUT_DIR}"
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${BOOT_OUTPUT_HEX}"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${BOOT_OUTPUT_BIN}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${BOOT_OUTPUT_ELF}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map" "${BOOT_OUTPUT_MAP}"
    DEPENDS
        ${CMAKE_PROJECT_NAME}
    COMMENT "Exporting boot artifacts to output/${CMAKE_PROJECT_NAME}/"
    VERBATIM
)

add_custom_target(${CMAKE_PROJECT_NAME}_artifacts ALL
    DEPENDS
        "${BOOT_OUTPUT_HEX}"
        "${BOOT_OUTPUT_BIN}"
        "${BOOT_OUTPUT_ELF}"
        "${BOOT_OUTPUT_MAP}"
)
