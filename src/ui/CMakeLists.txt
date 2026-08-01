# ui：Qt6 用户界面（Phase 7-A）
# 架构隔离：本目录禁止 include d3d11 / nvenc / wasapi / ffmpeg
# UI 只通过 IRecorderUiBackend 抽象接口控制录制核心（Engine API）
# 硬件装配实现在 src/app/UiBackend.cpp（允许 include 硬件头）
set(CMAKE_AUTOMOC ON)

add_library(screenforge_ui STATIC
    MainWindow.cpp
)

target_include_directories(screenforge_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(screenforge_ui PUBLIC
    Qt6::Widgets
)

target_compile_definitions(screenforge_ui PUBLIC NOMINMAX)
