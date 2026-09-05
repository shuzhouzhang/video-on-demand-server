add_executable(file_service
    source/main.cc
    source/file_repository.cc
    source/svc_server.cc
    source/svc_data.cc
    source/svc_rpc.cc
    source/svc_sync.cc
    source/svc_mq.cc
    ../common/session_token.cc
    ../common/config.cc
    ../common/object_storage.cc
    ../common/util.cc
    ../common/bitelog.cc
)

target_include_directories(file_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
)

target_link_libraries(file_service PRIVATE
    jsoncpp fmt spdlog cpp-httplib OpenSSL::Crypto Threads::Threads
)

if(VOD_ENABLE_REFERENCE_RUNTIME)
    target_sources(file_service PRIVATE
        ../common/etcd_registry.cc
        ../common/service_registry.cc
        ../common/brpc_http_bridge.cc
    )
    target_link_libraries(file_service PRIVATE vod_proto vod_reference_dependencies)
endif()
