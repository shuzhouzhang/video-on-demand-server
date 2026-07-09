add_executable(file_service
    source/main.cc
    source/file_repository.cc
    ../common/config.cc
    ../common/util.cc
    ../common/bitelog.cc
)

target_include_directories(file_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${VOD_COMMON_DIR}
)

target_link_libraries(file_service PRIVATE
    jsoncpp fmt spdlog cpp-httplib Threads::Threads
)
