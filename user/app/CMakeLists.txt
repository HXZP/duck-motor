add_library(user_app STATIC)

target_sources(user_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/user_info.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/as5600.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/can_protocol.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/flash.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/foc_init.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/log.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/soft_iic.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/user_main.c"
)

target_include_directories(user_app PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(user_app PUBLIC
    stm32cubemx
    user_kernel
    user_foc
    user_rtt
)
