set(CMAKE_AUTOMOC ON)

# 7-B：移除旧 VerifyRun/旧 MainWindow 编译（源码保留于磁盘，未编译）
# 入口：main.cpp（UI 默认 / 命令行测试）+ UiBackend（硬件装配层）
add_executable(ScreenForge WIN32
    main.cpp
    Logger.cpp
    UiBackend.cpp
    SmokeTest.cpp
    ../muxer/MuxBench.cpp
    ../sandbox/SandboxDemo.cpp
)

target_link_libraries(ScreenForge PRIVATE
    screenforge_graphics
    screenforge_capture
    screenforge_pipeline
    screenforge_simulation
    screenforge_muxer
    screenforge_recorder
    screenforge_audio
    screenforge_ui
    screenforge_sandbox
    Qt6::Widgets
)

# 硬件编码器（可选：NVENC SDK 存在时启用）
if(TARGET screenforge_encoder_hw)
    target_link_libraries(ScreenForge PRIVATE screenforge_encoder_hw)
    target_compile_definitions(ScreenForge PRIVATE SF_HAVE_NVENC_HW)
endif()
if(TARGET screenforge_encoder)
    target_link_libraries(ScreenForge PRIVATE screenforge_encoder)
endif()

# Release 输出统一到 build/bin/（多配置生成器实际落在 bin/Release/）
set_target_properties(ScreenForge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
