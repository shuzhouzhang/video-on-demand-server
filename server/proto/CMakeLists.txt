set(VOD_PROTO_FILES
    base.proto
    business.proto
    file.proto
    http.proto
    message.proto
    runtime.proto
    user.proto
    video.proto
)

set(PROTOBUF_IMPORT_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
protobuf_generate_cpp(VOD_PROTO_SOURCES VOD_PROTO_HEADERS ${VOD_PROTO_FILES})

add_library(vod_proto STATIC ${VOD_PROTO_SOURCES} ${VOD_PROTO_HEADERS})
target_include_directories(vod_proto PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
# The runtime dependency bundle links protobuf after static brpc. Linking it
# here makes CMake emit protobuf before brpc, which breaks --as-needed builds.
target_compile_options(vod_proto PRIVATE -Wall -Wextra -Wpedantic)
