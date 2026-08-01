# encoder：NVENC 硬件编码（Phase 3-A，旧入口；--verify 用，7-B 起默认不编译进 exe）
# 依赖 NVIDIA Video Codec SDK（nvEncodeAPI.h）
# 可选：SDK 缺失时跳过本库（SIMULATION-only 模式），除非 SF_REQUIRE_NVENC=ON

find_path(NVENC_INCLUDE_DIR nvEncodeAPI.h
    HINTS
        $ENV{NVENC_SDK_ROOT}/include
        $ENV{NVENC_SDK_ROOT}
        ${CMAKE_SOURCE_DIR}/vendor/nvenc/include
        ${CMAKE_SOURCE_DIR}/vendor/nvenc
    PATH_SUFFIXES include
)

if(NOT NVENC_INCLUDE_DIR)
    if(SF_REQUIRE_NVENC)
        message(FATAL_ERROR
            "SF_REQUIRE_NVENC=ON 但未找到 NVENC SDK (nvEncodeAPI.h)。\n"
            "请从 https://developer.nvidia.com/nvidia-video-codec-sdk 下载，\n"
            "并设置 NVENC_SDK_ROOT，或将 nvEncodeAPI.h 放入 vendor/nvenc/include/。")
    else()
        message(STATUS "NVENC SDK 未找到 → screenforge_encoder 跳过（SIMULATION-only 模式，NvencSimulator 回退）")
        return()
    endif()
endif()

message(STATUS "NVENC SDK 头文件: ${NVENC_INCLUDE_DIR}/nvEncodeAPI.h")

add_library(screenforge_encoder STATIC
    NvEncoder.cpp
)

target_include_directories(screenforge_encoder PUBLIC
    ${NVENC_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(screenforge_encoder PUBLIC
    screenforge_graphics
    screenforge_pipeline
    d3d11 dxgi d3dcompiler
)

target_compile_definitions(screenforge_encoder PUBLIC NOMINMAX)
target_compile_definitions(screenforge_encoder PUBLIC SF_HAVE_NVENC_HW)
