add_executable(api_gateway
    source/main.cc
    ../common/http_client.cc
    ../common/config.cc
    ../common/redis_session_manager.cc
    ../common/util.cc
    ../common/bitelog.cc
)

target_include_directories(api_gateway PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
)

target_link_libraries(api_gateway PRIVATE
    jsoncpp fmt spdlog cpp-httplib hiredis Threads::Threads
)
