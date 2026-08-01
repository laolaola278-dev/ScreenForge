# ScreenForge 真实工程 — 模块划分（按开发阶段逐步填充）
# Phase 0-5A 已启用：app + graphics + capture + pipeline + encoder + encoder_hw（真实 NVENC）+ simulation + muxer + recorder
# 后续阶段：audio(WASAPI) → 最终 UI 完善

add_subdirectory(app)
add_subdirectory(graphics)
add_subdirectory(capture)
add_subdirectory(pipeline)
add_subdirectory(encoder)
add_subdirectory(encoder/NvEncoderHardware)
add_subdirectory(simulation)
add_subdirectory(muxer)
add_subdirectory(recorder)
add_subdirectory(audio)
add_subdirectory(ui)
add_subdirectory(../sandbox)   # Sandbox 模拟器（独立模块，不改核心）
