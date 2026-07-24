add_executable(video_service
    source/main.cc
    source/svc_server.cc
    ../data/video.cc
    source/video_repository.cc
    source/svc_data.cc
    source/svc_rpc.cc
    source/svc_sync.cc
    source/svc_mq.cc
    ../repository/admin_repository.cc
    ../common/http_server.cc
    ../common/auth.cc
    ../common/email_verification.cc
    ../common/config.cc
    ../common/redis_session_manager.cc
    ../common/util.cc
    ../common/bitelog.cc
    ../database/database.cc
)

target_include_directories(video_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
    ${VOD_DATABASE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../data
    ${VOD_REPOSITORY_DIR}
)

target_link_libraries(video_service PRIVATE
    jsoncpp fmt spdlog odb-mysql odb mysqlclient cpp-httplib hiredis OpenSSL::Crypto Threads::Threads
)
