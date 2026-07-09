add_executable(user_service
    source/main.cc
    source/svc_server.cc
    source/user_repository.cc
    source/svc_data.cc
    source/svc_rpc.cc
    source/svc_sync.cc
    source/svc_mq.cc
    ../common/http_server.cc
    ../common/config.cc
    ../common/redis_session_manager.cc
    ../common/util.cc
    ../common/bitelog.cc
    ../database/database.cc
    ../svc_video/source/video.cc
    ../svc_video/source/video_repository.cc
)

target_include_directories(user_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
    ${VOD_DATABASE_DIR}
    ${VOD_SVC_VIDEO_DIR}
)

target_link_libraries(user_service PRIVATE
    jsoncpp fmt spdlog odb-mysql odb mysqlclient cpp-httplib hiredis Threads::Threads
)
