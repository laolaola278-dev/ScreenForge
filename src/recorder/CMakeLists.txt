# recorder：录制总控（Phase 4-B/5-A/5-B）
# 集成链路：Capture → Pipeline(帧队列+Pacing) → Encoder(IEncoder) → Muxer(IMuxer)
# 沙盒模式：SyntheticFrameSource + NvencSimulator（允许）
# 真实模式：WgcCaptureSource + NvEncoderHardware（真机可用，接口一致）
# 7-B：NVENC SDK 缺失时（SF_HAVE_NVENC_HW=0）硬件编码器库不编译，
#      HardwareRecordTest 自动回退模拟器，StabilityTest 明确报错退出
add_library(screenforge_recorder STATIC
    RecorderEngine.cpp
    RecordTest.cpp
    HardwareRecordTest.cpp
    StabilityTest.cpp
)

target_include_directories(screenforge_recorder PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/encoder        # NvCapabilities.h（无 SDK 依赖）
)

target_link_libraries(screenforge_recorder PUBLIC
    screenforge_simulation
    screenforge_muxer
    screenforge_pipeline
    screenforge_graphics
    d3d11 dxgi d3dcompiler
)

# 硬件编码器（可选：NVENC SDK 存在时启用）
if(TARGET screenforge_encoder)
    target_link_libraries(screenforge_recorder PUBLIC screenforge_encoder)
    target_compile_definitions(screenforge_recorder PUBLIC SF_HAVE_NVENC_HW)
endif()
if(TARGET screenforge_encoder_hw)
    target_link_libraries(screenforge_recorder PUBLIC screenforge_encoder_hw)
    target_compile_definitions(screenforge_recorder PUBLIC SF_HAVE_NVENC_HW)
endif()

target_compile_definitions(screenforge_recorder PUBLIC NOMINMAX)
