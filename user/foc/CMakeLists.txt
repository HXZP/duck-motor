add_library(user_foc STATIC)

target_sources(user_foc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/foc_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/foc_math.c"
)

target_include_directories(user_foc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(user_foc PUBLIC
    user_rtt
)
