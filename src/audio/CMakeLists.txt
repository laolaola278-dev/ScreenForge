# audio：WASAPI 音频捕获（Phase 6-A）
# AudioCapture（Loopback 系统声音）+ MicrophoneCapture（麦克风）
# 48kHz / stereo / PCM int16；时间戳统一 QPC
# 依赖：ole32（COM）、Windows 自带 WASAPI（mmdeviceapi/audioclient）
add_library(screenforge_audio STATIC
    AudioCapture.cpp
    MicrophoneCapture.cpp
    AudioRecordTest.cpp
)

target_include_directories(screenforge_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(screenforge_audio PUBLIC
    screenforge_muxer
    screenforge_simulation
    screenforge_recorder
    ole32
)

# 硬件编码器（可选：NVENC SDK 存在时启用）
if(TARGET screenforge_encoder)
    target_link_libraries(screenforge_audio PUBLIC screenforge_encoder)
endif()

target_compile_definitions(screenforge_audio PUBLIC NOMINMAX)
