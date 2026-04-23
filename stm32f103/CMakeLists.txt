cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME motor)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

enable_language(C ASM)

add_executable(${CMAKE_PROJECT_NAME})

set(REPO_ROOT "${CMAKE_SOURCE_DIR}/..")

add_subdirectory(cmake/stm32cubemx)

add_subdirectory("${REPO_ROOT}/user/rtt" "${CMAKE_BINARY_DIR}/user_rtt")
add_subdirectory("${REPO_ROOT}/user/foc" "${CMAKE_BINARY_DIR}/user_foc")
add_subdirectory("${REPO_ROOT}/user/app" "${CMAKE_BINARY_DIR}/user_app")

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${REPO_ROOT}/user"
)

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    user_app
)

set(APP_OUTPUT_DIR "${REPO_ROOT}/output/${CMAKE_PROJECT_NAME}")
set(APP_OUTPUT_HEX "${APP_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.hex")
set(APP_OUTPUT_BIN "${APP_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.bin")
set(APP_OUTPUT_ELF "${APP_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.elf")
set(APP_OUTPUT_MAP "${APP_OUTPUT_DIR}/${CMAKE_PROJECT_NAME}.map")

add_custom_command(
    OUTPUT
        "${APP_OUTPUT_HEX}"
        "${APP_OUTPUT_BIN}"
        "${APP_OUTPUT_ELF}"
        "${APP_OUTPUT_MAP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_OUTPUT_DIR}"
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${APP_OUTPUT_HEX}"
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${APP_OUTPUT_BIN}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${CMAKE_PROJECT_NAME}> "${APP_OUTPUT_ELF}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map" "${APP_OUTPUT_MAP}"
    DEPENDS
        ${CMAKE_PROJECT_NAME}
    COMMENT "Exporting app artifacts to output/${CMAKE_PROJECT_NAME}/"
    VERBATIM
)

add_custom_target(${CMAKE_PROJECT_NAME}_artifacts ALL
    DEPENDS
        "${APP_OUTPUT_HEX}"
        "${APP_OUTPUT_BIN}"
        "${APP_OUTPUT_ELF}"
        "${APP_OUTPUT_MAP}"
)
