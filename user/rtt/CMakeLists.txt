add_library(user_rtt STATIC)

target_sources(user_rtt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/SEGGER_RTT.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/SEGGER_RTT_printf.c"
)

target_include_directories(user_rtt PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
