add_library(user_boot STATIC)

target_sources(user_boot PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/can_protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/log.c"
)

target_include_directories(user_boot PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(user_boot PUBLIC
    stm32cubemx
    user_rtt
)
