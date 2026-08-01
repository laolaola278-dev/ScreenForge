# muxer：MP4 封装（Phase 4-A）
# 依赖 FFmpeg libavformat（fragmented MP4）：
#   1) 下载 FFmpeg 开发库（如 BtbN builds，含 include/ + lib/*.lib）
#   2) 设置环境变量 FFMPEG_ROOT 指向根目录，或安装到系统路径
# 仅链接 libavformat / libavcodec / libavutil

find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h
    HINTS
        $ENV{FFMPEG_ROOT}/include
        $ENV{FFMPEG_DIR}/include
        ${CMAKE_SOURCE_DIR}/vendor/ffmpeg/include
    PATH_SUFFIXES include
)

find_library(AVFORMAT_LIB avformat
    HINTS $ENV{FFMPEG_ROOT}/lib $ENV{FFMPEG_DIR}/lib ${CMAKE_SOURCE_DIR}/vendor/ffmpeg/lib)
find_library(AVCODEC_LIB avcodec
    HINTS $ENV{FFMPEG_ROOT}/lib $ENV{FFMPEG_DIR}/lib ${CMAKE_SOURCE_DIR}/vendor/ffmpeg/lib)
find_library(AVUTIL_LIB avutil
    HINTS $ENV{FFMPEG_ROOT}/lib $ENV{FFMPEG_DIR}/lib ${CMAKE_SOURCE_DIR}/vendor/ffmpeg/lib)

if(NOT FFMPEG_INCLUDE_DIR OR NOT AVFORMAT_LIB OR NOT AVCODEC_LIB OR NOT AVUTIL_LIB)
    message(FATAL_ERROR
        "未找到 FFmpeg 开发库 (libavformat/avformat.h + avformat/avcodec/avutil lib)。\n"
        "请下载 FFmpeg dev 包并设置 FFMPEG_ROOT 环境变量，或将文件放入 vendor/ffmpeg/。")
endif()

message(STATUS "FFmpeg 头文件: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "FFmpeg 库: ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB}")

add_library(screenforge_muxer STATIC
    Mp4Muxer.cpp
)

target_include_directories(screenforge_muxer PUBLIC
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src        # 供 "audio/AudioFrame.h" 包含
)

target_link_libraries(screenforge_muxer PUBLIC
    ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB}
)

target_compile_definitions(screenforge_muxer PUBLIC NOMINMAX)
