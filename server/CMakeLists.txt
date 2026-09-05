cmake_minimum_required(VERSION 3.16)
project(video_on_demand_microservices VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

option(VOD_ENABLE_REFERENCE_RUNTIME
       "Build the brpc/protobuf/etcd/RabbitMQ/FastDFS runtime adapters" ON)

if(VOD_ENABLE_REFERENCE_RUNTIME)
    find_package(Protobuf REQUIRED)
    find_package(CURL REQUIRED)
    find_library(BRPC_LIBRARY NAMES brpc REQUIRED)
    find_library(GFLAGS_LIBRARY NAMES gflags REQUIRED)
    find_library(LEVELDB_LIBRARY NAMES leveldb REQUIRED)
    find_library(PROTOC_LIBRARY NAMES protoc REQUIRED)
    find_library(AMQP_CPP_LIBRARY NAMES amqpcpp REQUIRED)
    find_library(EVENT_LIBRARY NAMES event REQUIRED)
    find_library(FASTDFS_CLIENT_LIBRARY NAMES fdfsclient REQUIRED)
    find_library(FASTCOMMON_LIBRARY NAMES fastcommon REQUIRED)

    add_library(vod_reference_dependencies INTERFACE)
    target_link_libraries(vod_reference_dependencies INTERFACE
        ${BRPC_LIBRARY}
        ${GFLAGS_LIBRARY}
        protobuf::libprotobuf
        ${LEVELDB_LIBRARY}
        ${PROTOC_LIBRARY}
        OpenSSL::SSL
        OpenSSL::Crypto
        ${CMAKE_DL_LIBS}
        ZLIB::ZLIB
        rt
        CURL::libcurl
        ${AMQP_CPP_LIBRARY}
        ${EVENT_LIBRARY}
        ${FASTDFS_CLIENT_LIBRARY}
        ${FASTCOMMON_LIBRARY}
    )
    target_compile_definitions(vod_reference_dependencies INTERFACE
        VOD_ENABLE_REFERENCE_RUNTIME=1
    )
    add_subdirectory(proto)
endif()

set(VOD_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set(VOD_DATABASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/database)
set(VOD_REPOSITORY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/repository)
set(VOD_SVC_USER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/svc_user/source)
set(VOD_SVC_VIDEO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/svc_video/source)

add_subdirectory(svc_gateway)
add_subdirectory(svc_user)
add_subdirectory(svc_video)
add_subdirectory(svc_file)
add_subdirectory(svc_transcode)

include(CTest)
if(BUILD_TESTING AND VOD_ENABLE_REFERENCE_RUNTIME)
    add_executable(native_rpc_test
        ../test/native_rpc/native_rpc_test.cc
        svc_user/source/native_rpc.cc svc_video/source/native_rpc.cc
        svc_user/source/user_routes.cc svc_video/source/video_routes.cc
        svc_video/source/interaction_routes.cc common/email_verification.cc
        common/route_support.cc common/auth.cc common/util.cc common/bitelog.cc
        common/redis_session_manager.cc common/session_token.cc data/video.cc)
    target_link_libraries(native_rpc_test PRIVATE vod_proto vod_reference_dependencies
        jsoncpp fmt spdlog cpp-httplib hiredis Threads::Threads)
    add_test(NAME native_rpc_contract COMMAND native_rpc_test)
endif()
