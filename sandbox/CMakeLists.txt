# sandbox：沙盒运行时模拟器（独立模块，不修改核心代码）
# 链路：SyntheticCapture → FrameQueue → IEncoder(SimEncoder) → IMuxer(SimMuxer) → 虚拟 MP4 报告
# 用途：无硬件环境验证录制流程/架构；全部组件为 Simulator，禁止伪装真实 WGC/NVENC
# 依赖：仅 pipeline（FrameQueue）+ graphics（Frame.h 头）；不调用 D3D11/WGC/NVENC/FFmpeg
add_library(screenforge_sandbox STATIC
    SyntheticCapture.cpp
    VirtualAudioSource.cpp
    SimEncoder.cpp
    SimMuxer.cpp
    SandboxRecorder.cpp
)

target_include_directories(screenforge_sandbox PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(screenforge_sandbox PUBLIC
    screenforge_pipeline
    screenforge_graphics
)

target_compile_definitions(screenforge_sandbox PUBLIC NOMINMAX)
