add_library(user_boot STATIC)

target_sources(user_boot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/ota_info.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/user_info.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/can_protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/crc16.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/jump.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/log.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ota_process.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/queue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ymodem_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ymodem_process.c"
)

target_include_directories(user_boot PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(user_boot PUBLIC
    stm32cubemx
    user_rtt
)
