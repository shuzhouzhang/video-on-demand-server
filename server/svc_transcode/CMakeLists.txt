add_executable(transcode_service
    source/main.cc
    source/svc_server.cc
    source/svc_data.cc
    source/svc_rpc.cc
    source/svc_mq.cc
    ../common/config.cc
    ../common/util.cc
    ../common/bitelog.cc
)

target_include_directories(transcode_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
)

target_link_libraries(transcode_service PRIVATE
    jsoncpp fmt spdlog cpp-httplib Threads::Threads
)
