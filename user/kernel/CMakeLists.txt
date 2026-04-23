add_library(user_kernel STATIC)

target_sources(user_kernel PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/pt-thread.c"
)

target_include_directories(user_kernel PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(user_kernel PUBLIC
    stm32cubemx
)
