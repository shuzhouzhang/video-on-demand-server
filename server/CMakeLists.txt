cmake_minimum_required(VERSION 3.16)
project(video_on_demand_microservices VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(VOD_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set(VOD_DATABASE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/database)
set(VOD_SVC_USER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/svc_user/source)
set(VOD_SVC_VIDEO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/svc_video/source)

add_subdirectory(svc_gateway)
add_subdirectory(svc_user)
add_subdirectory(svc_video)
add_subdirectory(svc_file)
add_subdirectory(svc_transcode)
