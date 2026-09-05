add_executable(transcode_service
    source/main.cc
    source/svc_server.cc
    source/svc_data.cc
    source/svc_rpc.cc
    source/svc_worker.cc
    ../common/auth.cc
    ../common/config.cc
    ../common/outbox.cc
    ../common/outbox_dispatcher.cc
    ../common/util.cc
    ../common/bitelog.cc
    ../database/database.cc
)

target_include_directories(transcode_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
    ${VOD_DATABASE_DIR}
)

target_link_libraries(transcode_service PRIVATE
    jsoncpp fmt spdlog cpp-httplib Threads::Threads
    odb-mysql odb mysqlclient
)

if(VOD_ENABLE_REFERENCE_RUNTIME)
    target_sources(transcode_service PRIVATE
        ../common/remote_object_storage.cc
        ../common/etcd_registry.cc
        ../common/service_registry.cc
        ../common/brpc_http_bridge.cc
        ../common/rabbitmq_publisher.cc
        ../common/rabbitmq_consumer.cc
    )
    target_link_libraries(transcode_service PRIVATE vod_proto vod_reference_dependencies)
endif()
