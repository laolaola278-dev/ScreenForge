set(CMAKE_AUTOMOC ON)

# 7-B：移除旧 VerifyRun/旧 MainWindow 编译（源码保留于磁盘，未编译）
# 入口：main.cpp（UI 默认 / 命令行测试）+ UiBackend（硬件装配层）
add_executable(ScreenForge WIN32
    main.cpp
    Logger.cpp
    UiBackend.cpp
    SmokeTest.cpp
    ${CMAKE_SOURCE_DIR}/src/muxer/MuxBench.cpp
    ${CMAKE_SOURCE_DIR}/sandbox/SandboxDemo.cpp
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

# 延迟加载 FFmpeg DLL（避免 DllMain 死锁导致 QApplication 构造卡住）
# FFmpeg DLL 在 DllMain 中可能获取 loader lock，与 Qt 6.5.3 初始化冲突
# 延迟加载确保 FFmpeg DLL 在首次调用 avformat_xxx() 时才加载
if(MSVC)
    target_link_libraries(ScreenForge PRIVATE DelayImp.lib)
    target_link_options(ScreenForge PRIVATE
        /DELAYLOAD:avformat.dll
        /DELAYLOAD:avcodec.dll
        /DELAYLOAD:avutil.dll
    )
endif()

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
