# simulation：录制核心链路模拟器（Phase 3-B Sandbox）
# SyntheticFrameSource → FrameQueue → Pacing → IEncoder(NvencSimulator) → Bitstream
# 不依赖 NVIDIA SDK / 真实硬件；未来替换 NvencSimulator 为 NvEncoder 即可接入真实 NVENC
add_library(screenforge_simulation STATIC
    SyntheticFrameSource.cpp
    NvencSimulator.cpp
    PipelineBenchmark.cpp
)

target_include_directories(screenforge_simulation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src        # 供 "encoder/IEncoder.h" 包含
)

target_link_libraries(screenforge_simulation PUBLIC
    screenforge_pipeline
    screenforge_graphics
)

target_compile_definitions(screenforge_simulation PUBLIC NOMINMAX)
